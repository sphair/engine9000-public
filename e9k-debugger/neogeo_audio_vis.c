/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "config.h"
#include "e9ui.h"
#include "e9ui_stack.h"
#include "e9ui_text_cache.h"
#include "libretro_host.h"
#include "neogeo_audio_vis.h"

#define NEOGEO_AUDIO_VIS_MIN_WIDTH 300
#define NEOGEO_AUDIO_VIS_MIN_HEIGHT 260
#define NEOGEO_AUDIO_VIS_LABEL_WIDTH 150
#define NEOGEO_AUDIO_VIS_LABEL_PAD 8
#define NEOGEO_AUDIO_VIS_LABEL_GAP 14
#define NEOGEO_AUDIO_VIS_CHECKBOX_WIDTH 18
#define NEOGEO_AUDIO_VIS_CHECKBOX_GAP 8
#define NEOGEO_AUDIO_VIS_METER_SEGMENTS 32
#define NEOGEO_AUDIO_VIS_METER_SEGMENT_GAP 2
#define NEOGEO_AUDIO_VIS_SCALE_DENOM 32768
#define NEOGEO_AUDIO_VIS_SEGMENT_BRIGHTNESS_MAX 255
#define NEOGEO_AUDIO_VIS_SEGMENT_HALF_LIFE_SECONDS 0.2
#define NEOGEO_AUDIO_VIS_FRAME_RATE 60.0
#define NEOGEO_AUDIO_VIS_ROW_COUNT (4 + E9K_DEBUG_GEO_ADPCM_A_CHANNELS)
#define NEOGEO_AUDIO_VIS_METER_COUNT (8 + E9K_DEBUG_GEO_ADPCM_A_CHANNELS * 2)
#define NEOGEO_AUDIO_VIS_MUTE_ROW_COUNT (3 + E9K_DEBUG_GEO_ADPCM_A_CHANNELS)

typedef struct neogeo_audio_vis_state {
    e9ui_window_state_t windowState;
    e9ui_component_t *body;
    e9k_debug_audio_frame_t lastFrame;
    uint32_t muteMask;
    uint8_t segmentBrightness[NEOGEO_AUDIO_VIS_METER_COUNT][NEOGEO_AUDIO_VIS_METER_SEGMENTS];
    uint64_t lastFadeFrame;
    int hasLastFadeFrame;
    int hasLastFrame;
} neogeo_audio_vis_state_t;

typedef struct neogeo_audio_vis_body_state {
    e9ui_component_t *muteCheckboxes[NEOGEO_AUDIO_VIS_MUTE_ROW_COUNT];
    int suppressMuteCallbacks;
} neogeo_audio_vis_body_state_t;

static neogeo_audio_vis_state_t neogeo_audio_vis_state = {
    .windowState.winX = E9UI_WINDOW_COORD_UNSET,
    .windowState.winY = E9UI_WINDOW_COORD_UNSET,
    .windowState.openMinWidthPx = NEOGEO_AUDIO_VIS_MIN_WIDTH,
    .windowState.openMinHeightPx = NEOGEO_AUDIO_VIS_MIN_HEIGHT,
    .windowState.openCenterWhenNoSaved = 1,
};

static e9ui_window_backend_t
neogeo_audio_vis_windowBackend(void)
{
    return e9ui_window_backend_overlay;
}

static int
neogeo_audio_vis_parseInt(const char *value, int *out)
{
    if (!value || !out) {
        return 0;
    }
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (!end || end == value || parsed < INT_MIN || parsed > INT_MAX) {
        return 0;
    }
    *out = (int)parsed;
    return 1;
}

static e9ui_rect_t
neogeo_audio_vis_windowDefaultRect(const e9ui_context_t *ctx)
{
    e9ui_rect_t rect = {
        e9ui_scale_px(ctx, 128),
        e9ui_scale_px(ctx, 128),
        e9ui_scale_px(ctx, 620),
        e9ui_scale_px(ctx, 360)
    };
    return rect;
}

static void
neogeo_audio_vis_fillRect(SDL_Renderer *renderer, int x, int y, int w, int h,
                          Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha)
{
    SDL_Rect rect = { x, y, w, h };
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);
    SDL_RenderFillRect(renderer, &rect);
}

static void
neogeo_audio_vis_drawRect(SDL_Renderer *renderer, int x, int y, int w, int h,
                          Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha)
{
    SDL_Rect rect = { x, y, w, h };
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);
    SDL_RenderDrawRect(renderer, &rect);
}

static int
neogeo_audio_vis_clampPeak(int value)
{
    if (value < 0) {
        value = -value;
    }
    if (value > NEOGEO_AUDIO_VIS_SCALE_DENOM) {
        value = NEOGEO_AUDIO_VIS_SCALE_DENOM;
    }
    return value;
}

static void
neogeo_audio_vis_resetSegmentBrightness(void)
{
    memset(neogeo_audio_vis_state.segmentBrightness, 0, sizeof(neogeo_audio_vis_state.segmentBrightness));
    neogeo_audio_vis_state.lastFadeFrame = 0;
    neogeo_audio_vis_state.hasLastFadeFrame = 0;
}

static double
neogeo_audio_vis_fadeFactor(uint64_t frameNo)
{
    if (!neogeo_audio_vis_state.hasLastFadeFrame || frameNo <= neogeo_audio_vis_state.lastFadeFrame) {
        neogeo_audio_vis_state.lastFadeFrame = frameNo;
        neogeo_audio_vis_state.hasLastFadeFrame = 1;
        return 1.0;
    }

    double elapsed = (double)(frameNo - neogeo_audio_vis_state.lastFadeFrame) / NEOGEO_AUDIO_VIS_FRAME_RATE;
    neogeo_audio_vis_state.lastFadeFrame = frameNo;
    neogeo_audio_vis_state.hasLastFadeFrame = 1;
    if (elapsed <= 0.0) {
        return 1.0;
    }
    return pow(0.5, elapsed / NEOGEO_AUDIO_VIS_SEGMENT_HALF_LIFE_SECONDS);
}

static void
neogeo_audio_vis_updateSegmentBrightness(int index, int peak, double fadeFactor)
{
    if (index < 0 || index >= NEOGEO_AUDIO_VIS_METER_COUNT) {
        return;
    }

    peak = neogeo_audio_vis_clampPeak(peak);
    for (int segment = 0; segment < NEOGEO_AUDIO_VIS_METER_SEGMENTS; segment++) {
        uint8_t *brightness = &neogeo_audio_vis_state.segmentBrightness[index][segment];
        int threshold = ((segment + 1) * NEOGEO_AUDIO_VIS_SCALE_DENOM) / NEOGEO_AUDIO_VIS_METER_SEGMENTS;
        if (peak >= threshold) {
            *brightness = NEOGEO_AUDIO_VIS_SEGMENT_BRIGHTNESS_MAX;
        } else if (*brightness > 0) {
            int decayed = (int)((double)*brightness * fadeFactor + 0.5);
            if (decayed >= *brightness && fadeFactor < 1.0) {
                decayed = *brightness - 1;
            }
            if (decayed < 0) {
                decayed = 0;
            }
            *brightness = (uint8_t)decayed;
        }
    }
}

static void
neogeo_audio_vis_segmentColor(int segment, Uint8 *red, Uint8 *green, Uint8 *blue)
{
    static const Uint8 stops[][3] = {
        { 35, 215, 80 },
        { 235, 225, 35 },
        { 245, 140, 28 },
        { 220, 30, 48 },
    };
    int stopCount = (int)(sizeof(stops) / sizeof(stops[0]));
    int scaled;
    int stopIndex;
    int frac;

    if (segment < 0) {
        segment = 0;
    }
    if (segment >= NEOGEO_AUDIO_VIS_METER_SEGMENTS) {
        segment = NEOGEO_AUDIO_VIS_METER_SEGMENTS - 1;
    }

    scaled = segment * 256 * (stopCount - 1) / (NEOGEO_AUDIO_VIS_METER_SEGMENTS - 1);
    stopIndex = scaled / 256;
    frac = scaled % 256;
    if (stopIndex >= stopCount - 1) {
        stopIndex = stopCount - 2;
        frac = 256;
    }

    *red = (Uint8)((stops[stopIndex][0] * (256 - frac) + stops[stopIndex + 1][0] * frac) / 256);
    *green = (Uint8)((stops[stopIndex][1] * (256 - frac) + stops[stopIndex + 1][1] * frac) / 256);
    *blue = (Uint8)((stops[stopIndex][2] * (256 - frac) + stops[stopIndex + 1][2] * frac) / 256);
}

static void
neogeo_audio_vis_greyscaleColor(Uint8 *red, Uint8 *green, Uint8 *blue)
{
    if (!red || !green || !blue) {
        return;
    }
    Uint8 grey = (Uint8)((30 * *red + 59 * *green + 11 * *blue) / 100);
    *red = grey;
    *green = grey;
    *blue = grey;
}

static void
neogeo_audio_vis_drawMeter(SDL_Renderer *renderer, int x, int y, int w, int h, int peak, int meterIndex, int muted)
{
    int peakValue = neogeo_audio_vis_clampPeak(peak);
    int segmentGap = NEOGEO_AUDIO_VIS_METER_SEGMENT_GAP;
    int segmentW = (w - segmentGap * (NEOGEO_AUDIO_VIS_METER_SEGMENTS - 1)) / NEOGEO_AUDIO_VIS_METER_SEGMENTS;
    neogeo_audio_vis_fillRect(renderer, x, y, w, h, 18, 18, 18, 255);
    if (segmentW <= 0) {
        segmentW = 1;
        segmentGap = 1;
    }
    for (int segment = 0; segment < NEOGEO_AUDIO_VIS_METER_SEGMENTS; segment++) {
        Uint8 red;
        Uint8 green;
        Uint8 blue;
        int threshold = ((segment + 1) * NEOGEO_AUDIO_VIS_SCALE_DENOM) / NEOGEO_AUDIO_VIS_METER_SEGMENTS;
        int active = peakValue >= threshold;
        int brightness = 0;
        int segmentX = x + segment * (segmentW + segmentGap);
        int remainingW = x + w - segmentX;
        int drawW = segmentW;
        if (remainingW <= 0) {
            break;
        }
        if (drawW > remainingW) {
            drawW = remainingW;
        }
        neogeo_audio_vis_segmentColor(segment, &red, &green, &blue);
        if (muted) {
            neogeo_audio_vis_greyscaleColor(&red, &green, &blue);
        }
        if (meterIndex >= 0 && meterIndex < NEOGEO_AUDIO_VIS_METER_COUNT) {
            brightness = neogeo_audio_vis_state.segmentBrightness[meterIndex][segment];
        }
        if (active) {
            neogeo_audio_vis_fillRect(renderer, segmentX, y, drawW, h, red, green, blue, 240);
        } else if (brightness > 0) {
            int dimRed = red / 5;
            int dimGreen = green / 5;
            int dimBlue = blue / 5;
            int trailRed = red / 2;
            int trailGreen = green / 2;
            int trailBlue = blue / 2;
            neogeo_audio_vis_fillRect(renderer, segmentX, y, drawW, h,
                                      (Uint8)(dimRed + ((trailRed - dimRed) * brightness) / 255),
                                      (Uint8)(dimGreen + ((trailGreen - dimGreen) * brightness) / 255),
                                      (Uint8)(dimBlue + ((trailBlue - dimBlue) * brightness) / 255),
                                      (Uint8)(150 + (90 * brightness) / 255));
        } else {
            red = (Uint8)(red / 5);
            green = (Uint8)(green / 5);
            blue = (Uint8)(blue / 5);
            neogeo_audio_vis_fillRect(renderer, segmentX, y, drawW, h, red, green, blue, 150);
        }
    }
    neogeo_audio_vis_drawRect(renderer, x, y, w, h, 68, 68, 68, 255);
}

static void
neogeo_audio_vis_drawTextClipped(e9ui_context_t *ctx,
                                 TTF_Font *font,
                                 int x,
                                 int y,
                                 int width,
                                 const char *text,
                                 SDL_Color color)
{
    if (!ctx || !ctx->renderer || !font || !text || width <= 0) {
        return;
    }
    int textW = 0;
    int textH = 0;
    SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, text, color, &textW, &textH);
    if (!tex || textW <= 0 || textH <= 0) {
        return;
    }

    SDL_Rect dst = { x, y, textW, textH };
    SDL_Rect clipRect = { x, y, width, textH };
    SDL_Rect prevClip = { 0, 0, 0, 0 };
    int hadClip = SDL_RenderIsClipEnabled(ctx->renderer) ? 1 : 0;
    if (hadClip) {
        SDL_RenderGetClipRect(ctx->renderer, &prevClip);
        SDL_Rect clipped;
        if (SDL_IntersectRect(&prevClip, &clipRect, &clipped)) {
            SDL_RenderSetClipRect(ctx->renderer, &clipped);
        } else {
            SDL_Rect empty = { 0, 0, 0, 0 };
            SDL_RenderSetClipRect(ctx->renderer, &empty);
        }
    } else {
        SDL_RenderSetClipRect(ctx->renderer, &clipRect);
    }
    SDL_RenderCopy(ctx->renderer, tex, NULL, &dst);
    if (hadClip) {
        SDL_RenderSetClipRect(ctx->renderer, &prevClip);
    } else {
        SDL_RenderSetClipRect(ctx->renderer, NULL);
    }
}

static int
neogeo_audio_vis_pointInRect(int px, int py, int x, int y, int w, int h)
{
    return px >= x && py >= y && px < x + w && py < y + h;
}

static uint32_t
neogeo_audio_vis_rowMuteMask(int rowIndex)
{
    if (rowIndex == 0) {
        return E9K_DEBUG_GEO_AUDIO_MUTE_FM;
    }
    if (rowIndex == 1) {
        return E9K_DEBUG_GEO_AUDIO_MUTE_SSG;
    }
    if (rowIndex >= 2 && rowIndex < 2 + E9K_DEBUG_GEO_ADPCM_A_CHANNELS) {
        return E9K_DEBUG_GEO_AUDIO_MUTE_ADPCM_A0 << (rowIndex - 2);
    }
    if (rowIndex == 2 + E9K_DEBUG_GEO_ADPCM_A_CHANNELS) {
        return E9K_DEBUG_GEO_AUDIO_MUTE_ADPCM_B;
    }
    return 0;
}

static void
neogeo_audio_vis_applyMuteMask(void)
{
    libretro_host_neogeo_setAudioMuteMask(neogeo_audio_vis_state.muteMask);
}

static void
neogeo_audio_vis_saveWindowRectIfChanged(void)
{
    if (!neogeo_audio_vis_state.windowState.open) {
        return;
    }
    if (e9ui_windowCaptureStateRectChanged(&neogeo_audio_vis_state.windowState, &e9ui->ctx)) {
        config_saveConfig();
    }
}

static void
neogeo_audio_vis_drawTooltip(e9ui_context_t *ctx, TTF_Font *font, int mouseX, int mouseY, const char *text)
{
    if (!ctx || !ctx->renderer || !font || !text || !text[0]) {
        return;
    }

    int textW = 0;
    int textH = 0;
    if (TTF_SizeUTF8(font, text, &textW, &textH) != 0 || textW <= 0 || textH <= 0) {
        return;
    }

    int padX = 8;
    int padY = 6;
    int tooltipW = textW + padX * 2;
    int tooltipH = textH + padY * 2;
    int tooltipX = mouseX + 14;
    int tooltipY = mouseY + 18;
    if (tooltipX + tooltipW > ctx->winW - 4) {
        tooltipX = ctx->winW - tooltipW - 4;
    }
    if (tooltipY + tooltipH > ctx->winH - 4) {
        tooltipY = ctx->winH - tooltipH - 4;
    }
    if (tooltipX < 4) {
        tooltipX = 4;
    }
    if (tooltipY < 4) {
        tooltipY = 4;
    }

    neogeo_audio_vis_fillRect(ctx->renderer, tooltipX, tooltipY, tooltipW, tooltipH, 20, 20, 24, 242);
    neogeo_audio_vis_drawRect(ctx->renderer, tooltipX, tooltipY, tooltipW, tooltipH, 92, 92, 102, 255);
    SDL_Color textColor = { 232, 232, 236, 255 };
    neogeo_audio_vis_drawTextClipped(ctx, font, tooltipX + padX, tooltipY + padY, textW, text, textColor);
}

static void
neogeo_audio_vis_drawSource(e9ui_context_t *ctx, TTF_Font *font, int x, int y, int w, int rowH,
                            const char *label, const e9k_debug_audio_source_t *source,
                            int meterIndex, int muted)
{
    SDL_Renderer *renderer = ctx ? ctx->renderer : NULL;
    int labelW = NEOGEO_AUDIO_VIS_LABEL_WIDTH;
    int labelPad = NEOGEO_AUDIO_VIS_LABEL_PAD;
    int gap = NEOGEO_AUDIO_VIS_LABEL_GAP;
    int meterX = x + labelW + gap;
    int meterW = w - labelW - gap;
    int textH = font ? TTF_FontHeight(font) : 16;
    SDL_Color textColor = e9ui->theme.button.text;

    if (!renderer || !source || meterW <= 0 || rowH <= 6) {
        return;
    }
    if (textH <= 0) {
        textH = 16;
    }

    neogeo_audio_vis_fillRect(renderer, x, y, labelW, rowH, 16, 16, 16, 230);
    neogeo_audio_vis_drawTextClipped(ctx,
                                     font,
                                     x + labelPad,
                                     y + (rowH - textH) / 2,
                                     labelW - labelPad * 2,
                                     label,
                                     textColor);
    int meterH = (rowH - 6) / 2;
    if (meterH <= 0) {
        return;
    }
    neogeo_audio_vis_drawMeter(renderer, meterX, y, meterW, meterH,
                               source->peakL,
                               meterIndex,
                               muted);
    neogeo_audio_vis_drawMeter(renderer, meterX, y + meterH + 4, meterW, meterH,
                               source->peakR,
                               meterIndex + 1,
                               muted);
}

static void
neogeo_audio_vis_updateSegmentBrightnessAll(const e9k_debug_audio_frame_t *frame)
{
    int meterIndex = 0;
    double fadeFactor;

    if (!frame) {
        return;
    }

    fadeFactor = neogeo_audio_vis_fadeFactor(frame->frameNo);
    neogeo_audio_vis_updateSegmentBrightness(meterIndex++, frame->fm.peakL, fadeFactor);
    neogeo_audio_vis_updateSegmentBrightness(meterIndex++, frame->fm.peakR, fadeFactor);
    neogeo_audio_vis_updateSegmentBrightness(meterIndex++, frame->ssg.peakL, fadeFactor);
    neogeo_audio_vis_updateSegmentBrightness(meterIndex++, frame->ssg.peakR, fadeFactor);
    for (int chnum = 0; chnum < E9K_DEBUG_GEO_ADPCM_A_CHANNELS; chnum++) {
        neogeo_audio_vis_updateSegmentBrightness(meterIndex++, frame->adpcmA[chnum].peakL, fadeFactor);
        neogeo_audio_vis_updateSegmentBrightness(meterIndex++, frame->adpcmA[chnum].peakR, fadeFactor);
    }
    neogeo_audio_vis_updateSegmentBrightness(meterIndex++, frame->adpcmB.peakL, fadeFactor);
    neogeo_audio_vis_updateSegmentBrightness(meterIndex++, frame->adpcmB.peakR, fadeFactor);
    neogeo_audio_vis_updateSegmentBrightness(meterIndex++, frame->mixed.peakL, fadeFactor);
    neogeo_audio_vis_updateSegmentBrightness(meterIndex++, frame->mixed.peakR, fadeFactor);
}

static int
neogeo_audio_vis_bodyRowHeight(const e9ui_rect_t *bounds)
{
    int pad = 16;
    int rowGap = 6;
    int availableH = bounds ? bounds->h - pad * 2 : 0;
    int rowH = (availableH - rowGap * (NEOGEO_AUDIO_VIS_ROW_COUNT - 1)) / NEOGEO_AUDIO_VIS_ROW_COUNT;
    if (rowH < 18) {
        rowH = 18;
    }
    return rowH;
}

static void
neogeo_audio_vis_bodySyncCheckboxes(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state) {
        return;
    }
    neogeo_audio_vis_body_state_t *st = (neogeo_audio_vis_body_state_t *)self->state;
    st->suppressMuteCallbacks = 1;
    for (int rowIndex = 0; rowIndex < NEOGEO_AUDIO_VIS_MUTE_ROW_COUNT; rowIndex++) {
        uint32_t muteBit = neogeo_audio_vis_rowMuteMask(rowIndex);
        e9ui_checkbox_setSelected(st->muteCheckboxes[rowIndex],
                                  (neogeo_audio_vis_state.muteMask & muteBit) ? 0 : 1,
                                  ctx);
    }
    st->suppressMuteCallbacks = 0;
}

static void
neogeo_audio_vis_bodyLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (self) {
        self->bounds = bounds;
    }
    if (!self || !self->state) {
        return;
    }

    neogeo_audio_vis_body_state_t *st = (neogeo_audio_vis_body_state_t *)self->state;
    int pad = 16;
    int top = bounds.y + pad;
    int rowGap = 6;
    int rowH = neogeo_audio_vis_bodyRowHeight(&bounds);
    int x = bounds.x + bounds.w - pad - NEOGEO_AUDIO_VIS_CHECKBOX_WIDTH;
    for (int rowIndex = 0; rowIndex < NEOGEO_AUDIO_VIS_MUTE_ROW_COUNT; rowIndex++) {
        if (st->muteCheckboxes[rowIndex] && st->muteCheckboxes[rowIndex]->layout) {
            e9ui_rect_t checkboxBounds = {
                x,
                top,
                NEOGEO_AUDIO_VIS_CHECKBOX_WIDTH,
                rowH
            };
            st->muteCheckboxes[rowIndex]->layout(st->muteCheckboxes[rowIndex], ctx, checkboxBounds);
        }
        top += rowH + rowGap;
    }
    neogeo_audio_vis_bodySyncCheckboxes(self, ctx);
}

static void
neogeo_audio_vis_bodyRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) {
        return;
    }

    e9ui_rect_t b = self->bounds;
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    if (!font) {
        return;
    }
    SDL_Rect bodyClip = { b.x, b.y, b.w, b.h };
    SDL_Rect prevClip = { 0, 0, 0, 0 };
    int hadClip = SDL_RenderIsClipEnabled(ctx->renderer) ? 1 : 0;
    if (hadClip) {
        SDL_RenderGetClipRect(ctx->renderer, &prevClip);
        SDL_Rect clipped;
        if (SDL_IntersectRect(&prevClip, &bodyClip, &clipped)) {
            SDL_RenderSetClipRect(ctx->renderer, &clipped);
        } else {
            SDL_Rect empty = { 0, 0, 0, 0 };
            SDL_RenderSetClipRect(ctx->renderer, &empty);
        }
    } else {
        SDL_RenderSetClipRect(ctx->renderer, &bodyClip);
    }
    neogeo_audio_vis_fillRect(ctx->renderer, b.x, b.y, b.w, b.h, 8, 8, 8, 255);
    if (!neogeo_audio_vis_state.hasLastFrame) {
        neogeo_audio_vis_drawTextClipped(ctx, font, b.x + 16, b.y + 16, b.w - 32,
                                         "NO AUDIO FRAME", e9ui->theme.button.text);
        if (hadClip) {
            SDL_RenderSetClipRect(ctx->renderer, &prevClip);
        } else {
            SDL_RenderSetClipRect(ctx->renderer, NULL);
        }
        return;
    }

    int pad = 16;
    int top = b.y + pad;
    int rowGap = 6;
    int rowH = neogeo_audio_vis_bodyRowHeight(&b);
    int x = b.x + pad;
    int w = b.w - pad * 2;
    int sourceX = x;
    int sourceW = w - NEOGEO_AUDIO_VIS_CHECKBOX_WIDTH - NEOGEO_AUDIO_VIS_CHECKBOX_GAP;
    int meterIndex = 0;
    int rowIndex = 0;
    char tooltip[64];
    int showTooltip = 0;
    const e9k_debug_audio_frame_t *frame = &neogeo_audio_vis_state.lastFrame;
    uint32_t muteBit = neogeo_audio_vis_rowMuteMask(rowIndex++);
    neogeo_audio_vis_drawSource(ctx, font, sourceX, top, sourceW, rowH, "FM", &frame->fm, meterIndex,
                                (neogeo_audio_vis_state.muteMask & muteBit) ? 1 : 0);
    meterIndex += 2;
    top += rowH + rowGap;
    muteBit = neogeo_audio_vis_rowMuteMask(rowIndex++);
    neogeo_audio_vis_drawSource(ctx, font, sourceX, top, sourceW, rowH, "SSG", &frame->ssg, meterIndex,
                                (neogeo_audio_vis_state.muteMask & muteBit) ? 1 : 0);
    meterIndex += 2;
    top += rowH + rowGap;
    for (int chnum = 0; chnum < E9K_DEBUG_GEO_ADPCM_A_CHANNELS; chnum++) {
        char label[32];
        if (frame->adpcmA[chnum].hasVolume) {
            snprintf(label, sizeof(label), "ADPCM-A%d %d/%d", chnum,
                     frame->adpcmA[chnum].volumeL,
                     frame->adpcmA[chnum].volumeR);
        } else {
            snprintf(label, sizeof(label), "ADPCM-A%d", chnum);
        }
        muteBit = neogeo_audio_vis_rowMuteMask(rowIndex++);
        neogeo_audio_vis_drawSource(ctx, font, sourceX, top, sourceW, rowH, label, &frame->adpcmA[chnum], meterIndex,
                                    (neogeo_audio_vis_state.muteMask & muteBit) ? 1 : 0);
        meterIndex += 2;
        top += rowH + rowGap;
    }
    muteBit = neogeo_audio_vis_rowMuteMask(rowIndex++);
    char adpcmBLabel[32];
    if (frame->adpcmB.hasVolume) {
        snprintf(adpcmBLabel, sizeof(adpcmBLabel), "ADPCM-B %d/%d", frame->adpcmB.volumeL, frame->adpcmB.volumeR);
    } else {
        snprintf(adpcmBLabel, sizeof(adpcmBLabel), "ADPCM-B");
    }
    neogeo_audio_vis_drawSource(ctx, font, sourceX, top, sourceW, rowH, adpcmBLabel, &frame->adpcmB, meterIndex,
                                (neogeo_audio_vis_state.muteMask & muteBit) ? 1 : 0);
    if (neogeo_audio_vis_pointInRect(ctx->mouseX, ctx->mouseY, x, top, w, rowH)) {
        uint32_t hz = frame->adpcmBPlaybackMilliHz / 1000u;
        uint32_t frac = frame->adpcmBPlaybackMilliHz % 1000u;
        snprintf(tooltip, sizeof(tooltip), "Playback frequency: %u.%03u Hz", hz, frac);
        showTooltip = 1;
    }
    meterIndex += 2;
    top += rowH + rowGap;
    neogeo_audio_vis_drawSource(ctx, font, sourceX, top, sourceW, rowH, "MIXED", &frame->mixed, meterIndex, 0);
    if (self->children) {
        e9ui_child_iterator iter;
        if (e9ui_child_iterateChildren(self, &iter)) {
            for (e9ui_child_iterator *it = e9ui_child_interateNext(&iter);
                 it;
                 it = e9ui_child_interateNext(&iter)) {
                if (it->child && !e9ui_getHidden(it->child) && it->child->render) {
                    it->child->render(it->child, ctx);
                }
            }
        }
    }
    if (showTooltip) {
        neogeo_audio_vis_drawTooltip(ctx, font, ctx->mouseX, ctx->mouseY, tooltip);
    }
    if (hadClip) {
        SDL_RenderSetClipRect(ctx->renderer, &prevClip);
    } else {
        SDL_RenderSetClipRect(ctx->renderer, NULL);
    }
}

static void
neogeo_audio_vis_bodyDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    alloc_free(self->state);
    self->state = NULL;
}

static void
neogeo_audio_vis_muteCheckboxChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    if (!user || !neogeo_audio_vis_state.body || !neogeo_audio_vis_state.body->state) {
        return;
    }

    neogeo_audio_vis_body_state_t *st = (neogeo_audio_vis_body_state_t *)neogeo_audio_vis_state.body->state;
    if (st->suppressMuteCallbacks) {
        return;
    }

    int rowIndex = *(int *)user;
    uint32_t muteBit = neogeo_audio_vis_rowMuteMask(rowIndex);
    if (!muteBit) {
        return;
    }
    if (selected) {
        neogeo_audio_vis_state.muteMask &= ~muteBit;
    } else {
        neogeo_audio_vis_state.muteMask |= muteBit;
    }
    neogeo_audio_vis_applyMuteMask();
    neogeo_audio_vis_bodySyncCheckboxes(neogeo_audio_vis_state.body, ctx);
}

static e9ui_component_t *
neogeo_audio_vis_makeBody(void)
{
    e9ui_component_t *body = (e9ui_component_t *)alloc_calloc(1, sizeof(*body));
    if (!body) {
        return NULL;
    }
    neogeo_audio_vis_body_state_t *st = (neogeo_audio_vis_body_state_t *)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(body);
        return NULL;
    }
    body->name = "neogeo_audio_vis_body";
    body->state = st;
    body->layout = neogeo_audio_vis_bodyLayout;
    body->render = neogeo_audio_vis_bodyRender;
    body->dtor = neogeo_audio_vis_bodyDtor;
    for (int rowIndex = 0; rowIndex < NEOGEO_AUDIO_VIS_MUTE_ROW_COUNT; rowIndex++) {
        int *rowUser = (int *)alloc_alloc(sizeof(*rowUser));
        if (!rowUser) {
            e9ui_childDestroy(body, &e9ui->ctx);
            return NULL;
        }
        *rowUser = rowIndex;
        st->muteCheckboxes[rowIndex] = e9ui_checkbox_make(NULL, 1, neogeo_audio_vis_muteCheckboxChanged, rowUser);
        if (!st->muteCheckboxes[rowIndex]) {
            alloc_free(rowUser);
            e9ui_childDestroy(body, &e9ui->ctx);
            return NULL;
        }
        e9ui_child_add(body, st->muteCheckboxes[rowIndex], rowUser);
    }
    return body;
}

static void
neogeo_audio_vis_windowCloseRequested(e9ui_window_t *window, void *user)
{
    (void)window;
    (void)user;
    neogeo_audio_vis_toggle();
}

void
neogeo_audio_vis_toggle(void)
{
    if (!neogeo_audio_vis_state.windowState.open) {
        neogeo_audio_vis_state.windowState.windowHost = e9ui_windowCreate(neogeo_audio_vis_windowBackend());
        if (!neogeo_audio_vis_state.windowState.windowHost) {
            return;
        }
        e9ui_windowSetMinSize(neogeo_audio_vis_state.windowState.windowHost,
                              NEOGEO_AUDIO_VIS_MIN_WIDTH,
                              NEOGEO_AUDIO_VIS_MIN_HEIGHT);
        neogeo_audio_vis_state.body = neogeo_audio_vis_makeBody();
        if (!neogeo_audio_vis_state.body) {
            e9ui_windowDestroy(neogeo_audio_vis_state.windowState.windowHost);
            neogeo_audio_vis_state.windowState.windowHost = NULL;
            return;
        }
        e9ui_rect_t rect = e9ui_windowResolveStateOpenRect(&e9ui->ctx,
                                                           neogeo_audio_vis_windowDefaultRect(&e9ui->ctx),
                                                           &neogeo_audio_vis_state.windowState);
        e9ui_windowOpen(neogeo_audio_vis_state.windowState.windowHost,
                        "AUDIO",
                        rect,
                        neogeo_audio_vis_state.body,
                        neogeo_audio_vis_windowCloseRequested,
                        NULL,
                        &e9ui->ctx);
        neogeo_audio_vis_state.windowState.open = 1;
        neogeo_audio_vis_state.muteMask = 0;
        neogeo_audio_vis_state.hasLastFrame = 0;
        neogeo_audio_vis_resetSegmentBrightness();
        libretro_host_neogeo_setAudioVisEnabled(1);
        neogeo_audio_vis_applyMuteMask();
    } else {
        neogeo_audio_vis_saveWindowRectIfChanged();
        neogeo_audio_vis_state.muteMask = 0;
        neogeo_audio_vis_applyMuteMask();
        libretro_host_neogeo_setAudioVisEnabled(0);
        if (neogeo_audio_vis_state.windowState.windowHost) {
            e9ui_windowDestroy(neogeo_audio_vis_state.windowState.windowHost);
            neogeo_audio_vis_state.windowState.windowHost = NULL;
        }
        neogeo_audio_vis_state.body = NULL;
        neogeo_audio_vis_state.windowState.open = 0;
        neogeo_audio_vis_state.hasLastFrame = 0;
        neogeo_audio_vis_resetSegmentBrightness();
    }
}

int
neogeo_audio_vis_isOpen(void)
{
    return neogeo_audio_vis_state.windowState.open ? 1 : 0;
}

void
neogeo_audio_vis_render(const e9k_debug_audio_frame_t *frame)
{
    if (!neogeo_audio_vis_state.windowState.open || !frame) {
        return;
    }
    neogeo_audio_vis_saveWindowRectIfChanged();
    if (!neogeo_audio_vis_state.hasLastFrame || frame->frameNo != neogeo_audio_vis_state.lastFrame.frameNo) {
        neogeo_audio_vis_updateSegmentBrightnessAll(frame);
    }
    neogeo_audio_vis_state.lastFrame = *frame;
    neogeo_audio_vis_state.hasLastFrame = 1;
}

void
neogeo_audio_vis_persistConfig(FILE *file)
{
    if (!file) {
        return;
    }
    e9ui_windowPersistStateRect(file,
                                "comp.neogeo_audio_vis",
                                &neogeo_audio_vis_state.windowState,
                                &e9ui->ctx);
}

int
neogeo_audio_vis_loadConfigProperty(const char *prop, const char *value)
{
    int intValue = 0;
    if (!prop || !value) {
        return 0;
    }
    if (strcmp(prop, "win_x") == 0) {
        if (!neogeo_audio_vis_parseInt(value, &intValue)) {
            return 0;
        }
        neogeo_audio_vis_state.windowState.winX = intValue;
        neogeo_audio_vis_state.windowState.winHasSaved =
            e9ui_windowHasSavedPosition(neogeo_audio_vis_state.windowState.winX,
                                        neogeo_audio_vis_state.windowState.winY);
        return 1;
    }
    if (strcmp(prop, "win_y") == 0) {
        if (!neogeo_audio_vis_parseInt(value, &intValue)) {
            return 0;
        }
        neogeo_audio_vis_state.windowState.winY = intValue;
        neogeo_audio_vis_state.windowState.winHasSaved =
            e9ui_windowHasSavedPosition(neogeo_audio_vis_state.windowState.winX,
                                        neogeo_audio_vis_state.windowState.winY);
        return 1;
    }
    if (strcmp(prop, "win_w") == 0) {
        if (!neogeo_audio_vis_parseInt(value, &intValue)) {
            return 0;
        }
        neogeo_audio_vis_state.windowState.winW = intValue;
        return 1;
    }
    if (strcmp(prop, "win_h") == 0) {
        if (!neogeo_audio_vis_parseInt(value, &intValue)) {
            return 0;
        }
        neogeo_audio_vis_state.windowState.winH = intValue;
        return 1;
    }
    return 0;
}
