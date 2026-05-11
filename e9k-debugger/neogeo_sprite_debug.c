/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "aux_window.h"
#include "neogeo_sprite_debug.h"
#include "alloc.h"
#include "config.h"
#include "e9ui.h"
#include "e9ui_button.h"
#include "e9ui_hstack.h"
#include "e9ui_spacer.h"
#include "e9ui_stack.h"
#include "e9ui_theme.h"
#include "e9ui_vspacer.h"
#include "libretro_host.h"

#define NG_COORD_SIZE 512
#define NG_WRAP_MASK 0x1FF
#define NG_VISIBLE_X0 0
#define NG_VISIBLE_Y0 0
#define NG_VISIBLE_W 320
#define NG_VISIBLE_H 224
#define NG_LINE_OFFSET 16
#define NG_COORD_MIN_X (-192)
#define NG_COORD_MIN_Y (-272)
#define NG_COORD_MAX_X 511
#define NG_COORD_MAX_Y 511
#define NG_COORD_W (NG_COORD_MAX_X - NG_COORD_MIN_X + 1)
#define NG_COORD_H (NG_COORD_MAX_Y - NG_COORD_MIN_Y + 1)
#define NG_COORD_OFFSET_X (-NG_COORD_MIN_X)
#define NG_COORD_OFFSET_Y (-NG_COORD_MIN_Y)
#define DBG_HIST_WIDTH 160
#define DBG_GAP 8
#define NG_FIX_MAP_BASE 0x7000u
#define NG_FIX_MAP_WORDS 0x0500u
#define NG_FIX_TILE_MASK 0x0fffu
#define NG_FIX_PALETTE_SHIFT 8u
#define NG_FIX_PALETTE_MASK 0xf0u
#define NG_FIX_COLS 40
#define NG_FIX_ROWS 32
#define NG_FIX_PANEL_PAD 8
#define NG_SPRITES_PER_LINE_MAX 96
#define NG_MAX_SPRITES 382
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 224
#define NEOGEO_SPRITE_DEBUG_SCB2_WORD_OFFSET 0x8000u
#define NEOGEO_SPRITE_DEBUG_SCB3_WORD_OFFSET 0x8200u
#define NEOGEO_SPRITE_DEBUG_SCB4_WORD_OFFSET 0x8400u
#define NEOGEO_SPRITE_DEBUG_SCB3_CHAIN_FLAG 0x40u
#define NEOGEO_SPRITE_DEBUG_SCB3_ROW_MASK 0x3fu
#define NEOGEO_SPRITE_DEBUG_SCB3_YPOS_MASK 0x01ffu
#define NEOGEO_SPRITE_DEBUG_SCB3_YPOS_SHIFT 7u
#define NEOGEO_SPRITE_DEBUG_SCB4_XPOS_SHIFT 7u
#define NEOGEO_SPRITE_DEBUG_SCB2_VSHRINK_MASK 0x00ffu
#define NEOGEO_SPRITE_DEBUG_SCB2_HSHRINK_MASK 0x0fu
#define NEOGEO_SPRITE_DEBUG_SCB2_HSHRINK_SHIFT 8u
#define NEOGEO_SPRITE_DEBUG_SPRITE_VRAM_WORDS_PER_SPRITE 64u
#define NEOGEO_SPRITE_DEBUG_SPRITE_TILE_ODD_WORD_OFFSET 1u
#define NEOGEO_SPRITE_DEBUG_SPRITE_ANIM_MASK 0x0cu
#define NEOGEO_SPRITE_DEBUG_SPRITE_PALETTE_SHIFT 4u
#define NEOGEO_SPRITE_DEBUG_SPRITE_PALETTE_MASK 0x00ffu
#define NEOGEO_SPRITE_DEBUG_CONTROL_GAP 8
#define NEOGEO_SPRITE_DEBUG_CONTROL_VGAP 6
#define NEOGEO_SPRITE_DEBUG_VIEW_MODE_COUNT 4

typedef enum neogeo_sprite_debug_view_mode {
    neogeo_sprite_debug_view_mode_normal = 0,
    neogeo_sprite_debug_view_mode_shrink = 1,
    neogeo_sprite_debug_view_mode_palette = 2,
    neogeo_sprite_debug_view_mode_chain = 3
} neogeo_sprite_debug_view_mode_t;

typedef struct neogeo_sprite_debug_state {
    e9ui_window_state_t windowState;
    SDL_Window *window;
    SDL_Renderer *renderer;
    e9ui_component_t *root;
    e9ui_component_t *overlayBodyHost;
    e9ui_component_t *modeButtons[NEOGEO_SPRITE_DEBUG_VIEW_MODE_COUNT];
    SDL_Texture *texture;
    uint32_t *pixels;
    size_t pixelsCap;
    int texW;
    int texH;
    uint32_t histGrad[DBG_HIST_WIDTH];
    int histGradReady;
    uint32_t lastHash;
    int cachedValid;
    e9k_debug_sprite_state_t lastState;
    int hasLastState;
    neogeo_sprite_debug_view_mode_t viewMode;
} neogeo_sprite_debug_state_t;

static neogeo_sprite_debug_state_t neogeo_sprite_debugState = {
    .windowState.winX = E9UI_WINDOW_COORD_UNSET,
    .windowState.winY = E9UI_WINDOW_COORD_UNSET,
    .windowState.openMinWidthPx = 420,
    .windowState.openMinHeightPx = 360,
    .windowState.openCenterWhenNoSaved = 1,
};

static int neogeo_sprite_debug_histogramEnabled = 1;

/* neogeo_sprite_debug_lut_hshrink (translated) from Mame neogeo_spr.cpp - horizontal zoom table - verified on real hardware
   license:BSD-3-Clause
   copyright-holders:Bryan McPhail,Ernesto Corvi,Andrew Prime,Zsolt Vasvari */

static const uint8_t neogeo_sprite_debug_lut_hshrink[0x10][0x10] = {
    { 0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0 },
    { 0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0 },
    { 0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0 },
    { 0,0,1,0,1,0,0,0,1,0,0,0,1,0,0,0 },
    { 0,0,1,0,1,0,0,0,1,0,0,0,1,0,1,0 },
    { 0,0,1,0,1,0,1,0,1,0,0,0,1,0,1,0 },
    { 0,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0 },
    { 1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0 },
    { 1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0 },
    { 1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0 },
    { 1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1 },
    { 1,0,1,1,1,0,1,1,1,1,1,0,1,0,1,1 },
    { 1,0,1,1,1,0,1,1,1,1,1,0,1,1,1,1 },
    { 1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1 },
    { 1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1 },
    { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
};

static void
neogeo_sprite_debug_renderFrameInternal(const e9k_debug_sprite_state_t *st, int presentFrame);

static void
neogeo_sprite_debug_updateModeButtons(void);

static uint32_t
neogeo_sprite_debug_hueColor(float h);

static uint32_t
neogeo_sprite_debug_color(Uint8 r, Uint8 g, Uint8 b);

static unsigned
neogeo_sprite_debug_countShrinkWidth(unsigned hval);

static uint32_t
neogeo_sprite_debug_hashWords(const uint16_t *words, size_t count);

static void
neogeo_sprite_debug_drawDigits3x5(uint32_t *pixels, int pitch, int extW, int extH,
                        int x, int y, const char *buf, uint32_t color);

static void
neogeo_sprite_debug_fillRectAbs(uint32_t *pixels, int pitch, int extW, int extH,
                      int x, int y, int w, int h, uint32_t color);

static void
neogeo_sprite_debug_drawRectAbs(uint32_t *pixels, int pitch, int extW, int extH,
                      int x, int y, int w, int h, uint32_t color);

static void
neogeo_sprite_debug_fillRectCoordWrapped(uint32_t *pixels, int pitch, int extW, int extH,
                               int x, int y, int w, int h, uint32_t color);

static int
neogeo_sprite_debug_drawBadge(uint32_t *pixels, int pitch, int extW, int extH,
                    int x, int y, int value, int alignRight, int limit,
                    uint32_t colWhite);

static void
neogeo_sprite_debug_drawFixMiniMap(uint32_t *pixels, int pitch, int extW, int extH,
                         int x, int y, int w, int h, const uint16_t *vram,
                         const uint8_t *fixrom, size_t fixromSize);

static int
neogeo_sprite_debug_fixTileHasPixels(const uint8_t *fixrom, size_t fixromSize, unsigned tileNum);

static uint32_t
neogeo_sprite_debug_paletteColor(unsigned paletteBank);

static uint32_t
neogeo_sprite_debug_chainColor(unsigned chainRootIndex);

static uint32_t
neogeo_sprite_debug_shrinkRgbColorFromFractions(float horizontalShrink, float verticalShrink);

static int
neogeo_sprite_debug_isValidViewMode(neogeo_sprite_debug_view_mode_t mode);

static void
neogeo_sprite_debug_drawShrinkLegend(uint32_t *pixels, int pitch, int extW, int extH,
                          int x, int y, int w, int h);


static e9ui_window_backend_t
neogeo_sprite_debug_windowBackend(void)
{
    return e9ui_window_backend_overlay;
}

int
neogeo_sprite_debug_handleKeydown(const SDL_KeyboardEvent *kev)
{
    if (!kev || !neogeo_sprite_debugState.windowState.open) {
        return 0;
    }
    if (kev->repeat != 0) {
        return 0;
    }
    if (kev->keysym.sym == SDLK_ESCAPE) {
        if (neogeo_sprite_debug_is_open()) {
            neogeo_sprite_debug_toggle();
        }
        return 1;
    }
    return 0;
}

static const aux_window_ops_t neogeo_sprite_debug_auxWindowOps = {
    .setFocus = neogeo_sprite_debug_setMainWindowFocused,
    .handleKeydown = neogeo_sprite_debug_handleKeydown,
};

static int
neogeo_sprite_debug_parseInt(const char *value, int *out)
{
    if (!value || !out) {
        return 0;
    }
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (!end || end == value) {
        return 0;
    }
    if (parsed < INT_MIN || parsed > INT_MAX) {
        return 0;
    }
    *out = (int)parsed;
    return 1;
}

static void
neogeo_sprite_debug_presentTexture(int baseW, int baseH, int presentFrame)
{
    SDL_Rect viewport = { 0, 0, 0, 0 };
    SDL_RenderGetViewport(neogeo_sprite_debugState.renderer, &viewport);
    int outW = viewport.w;
    int outH = viewport.h;
    if (outW <= 0 || outH <= 0) {
        SDL_GetRendererOutputSize(neogeo_sprite_debugState.renderer, &outW, &outH);
        viewport.x = 0;
        viewport.y = 0;
        viewport.w = outW;
        viewport.h = outH;
    }
    if (outW <= 0 || outH <= 0) {
        if (neogeo_sprite_debugState.window) {
            SDL_GetWindowSize(neogeo_sprite_debugState.window, &outW, &outH);
            viewport.x = 0;
            viewport.y = 0;
            viewport.w = outW;
            viewport.h = outH;
        }
    }
    float scaleX = outW > 0 ? (float)outW / (float)baseW : 1.0f;
    float scaleY = outH > 0 ? (float)outH / (float)baseH : 1.0f;
    float scale = scaleX < scaleY ? scaleX : scaleY;
    if (scale <= 0.0f) {
        scale = 1.0f;
    }
    int dstW = (int)((float)baseW * scale + 0.5f);
    int dstH = (int)((float)baseH * scale + 0.5f);
    int dstX = (outW - dstW) / 2;
    int dstY = (outH - dstH) / 2;
    SDL_Rect dst = { dstX, dstY, dstW, dstH };
    SDL_SetRenderDrawColor(neogeo_sprite_debugState.renderer, 0, 0, 0, 255);
    SDL_Rect clearRect = { 0, 0, outW, outH };
    SDL_RenderFillRect(neogeo_sprite_debugState.renderer, &clearRect);
    SDL_Rect src = { 0, 0, baseW, baseH };
    SDL_RenderCopy(neogeo_sprite_debugState.renderer, neogeo_sprite_debugState.texture, &src, &dst);
    if (neogeo_sprite_debug_histogramEnabled) {
        int histX = baseW + DBG_GAP;
        int histW = DBG_HIST_WIDTH;
        SDL_Rect histSrc = { histX, 0, histW, baseH };
        SDL_Rect histDst = { dstX + dstW + (int)((float)DBG_GAP * scale + 0.5f),
                              dstY,
                              (int)((float)histW * scale + 0.5f),
                              dstH };
        SDL_RenderCopy(neogeo_sprite_debugState.renderer, neogeo_sprite_debugState.texture, &histSrc, &histDst);
    }
    if (presentFrame) {
        SDL_RenderPresent(neogeo_sprite_debugState.renderer);
    }
}

static e9ui_rect_t
neogeo_sprite_debug_windowDefaultRect(const e9ui_context_t *ctx)
{
    e9ui_rect_t rect = {
        e9ui_scale_px(ctx, 96),
        e9ui_scale_px(ctx, 96),
        e9ui_scale_px(ctx, 768),
        e9ui_scale_px(ctx, 768)
    };
    return rect;
}

static int
neogeo_sprite_debug_overlayBodyPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)ctx;
    (void)availW;
    return 0;
}

static void
neogeo_sprite_debug_overlayBodyLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}


static void
neogeo_sprite_debug_overlayBodyRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) {
        return;
    }
    if (!neogeo_sprite_debugState.windowState.open || !neogeo_sprite_debugState.hasLastState) {
        return;
    }
    SDL_Rect prevViewport;
    SDL_Rect viewport = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_Rect prevClip = { 0, 0, 0, 0 };
    int hadClip = SDL_RenderIsClipEnabled(ctx->renderer) ? 1 : 0;
    if (hadClip) {
        SDL_RenderGetClipRect(ctx->renderer, &prevClip);
    }
    SDL_RenderGetViewport(ctx->renderer, &prevViewport);
    SDL_RenderSetViewport(ctx->renderer, &viewport);
    if (hadClip) {
        SDL_Rect localClip = {
            prevClip.x - self->bounds.x,
            prevClip.y - self->bounds.y,
            prevClip.w,
            prevClip.h
        };
        SDL_Rect viewportLocal = { 0, 0, self->bounds.w, self->bounds.h };
        SDL_Rect clipped;
        if (SDL_IntersectRect(&localClip, &viewportLocal, &clipped)) {
            SDL_RenderSetClipRect(ctx->renderer, &clipped);
        } else {
            SDL_Rect empty = { 0, 0, 0, 0 };
            SDL_RenderSetClipRect(ctx->renderer, &empty);
        }
    }
    neogeo_sprite_debugState.window = ctx->window;
    neogeo_sprite_debugState.renderer = ctx->renderer;
    neogeo_sprite_debug_renderFrameInternal(&neogeo_sprite_debugState.lastState, 0);
    SDL_RenderSetViewport(ctx->renderer, &prevViewport);
    if (hadClip) {
        SDL_RenderSetClipRect(ctx->renderer, &prevClip);
    }
}

static int
neogeo_sprite_debug_isValidViewMode(neogeo_sprite_debug_view_mode_t mode)
{
    return mode >= neogeo_sprite_debug_view_mode_normal &&
        mode < (neogeo_sprite_debug_view_mode_t)NEOGEO_SPRITE_DEBUG_VIEW_MODE_COUNT;
}

static void
neogeo_sprite_debug_setViewMode(neogeo_sprite_debug_view_mode_t mode)
{
    if (!neogeo_sprite_debug_isValidViewMode(mode)) {
        mode = neogeo_sprite_debug_view_mode_normal;
    }
    if (neogeo_sprite_debugState.viewMode == mode) {
        return;
    }
    neogeo_sprite_debugState.viewMode = mode;
    neogeo_sprite_debugState.cachedValid = 0;
    neogeo_sprite_debug_updateModeButtons();
    config_saveConfig();
}

static void
neogeo_sprite_debug_setMode(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    neogeo_sprite_debug_setViewMode((neogeo_sprite_debug_view_mode_t)(uintptr_t)user);
}

static void
neogeo_sprite_debug_updateModeButtons(void)
{
    const e9k_theme_button_t *activeTheme = e9ui_theme_button_preset_profile_active();
    for (int i = 0; i < NEOGEO_SPRITE_DEBUG_VIEW_MODE_COUNT; ++i) {
        if (neogeo_sprite_debugState.modeButtons[i]) {
            if (neogeo_sprite_debugState.viewMode == (neogeo_sprite_debug_view_mode_t)i) {
                e9ui_button_setTheme(neogeo_sprite_debugState.modeButtons[i], activeTheme);
            } else {
                e9ui_button_clearTheme(neogeo_sprite_debugState.modeButtons[i]);
            }
        }
    }
}

static e9ui_component_t *
neogeo_sprite_debug_makeControlsRow(void)
{
    static const struct {
        const char *label;
        const char *largestLabel;
        neogeo_sprite_debug_view_mode_t mode;
    } modeButtons[] = {
        { "Normal", "Normal", neogeo_sprite_debug_view_mode_normal },
        { "Shrink", "Palette", neogeo_sprite_debug_view_mode_shrink },
        { "Palette", "Palette", neogeo_sprite_debug_view_mode_palette },
        { "Chain", "Palette", neogeo_sprite_debug_view_mode_chain },
    };
    e9ui_component_t *row = e9ui_hstack_make();
    int gapPx = NEOGEO_SPRITE_DEBUG_CONTROL_GAP;

    if (!row) {
        return NULL;
    }

    for (int i = 0; i < NEOGEO_SPRITE_DEBUG_VIEW_MODE_COUNT; ++i) {
        int buttonW = 0;
        e9ui_component_t *button = e9ui_button_make(modeButtons[i].label,
                                                    neogeo_sprite_debug_setMode,
                                                    (void *)(uintptr_t)modeButtons[i].mode);
        if (!button) {
            return row;
        }
        if (i > 0) {
            e9ui_hstack_addFixed(row, e9ui_spacer_make(gapPx), gapPx);
        }
        e9ui_button_setMini(button, 1);
        e9ui_button_setLargestLabel(button, modeButtons[i].largestLabel);
        e9ui_button_measure(button, &e9ui->ctx, &buttonW, NULL);
        e9ui_hstack_addFixed(row, button, buttonW);
        neogeo_sprite_debugState.modeButtons[modeButtons[i].mode] = button;
    }

    e9ui_hstack_addFlex(row, e9ui_spacer_make(1));
    neogeo_sprite_debug_updateModeButtons();
    return row;
}

static uint32_t
neogeo_sprite_debug_shrinkRgbColor(unsigned hshrink, unsigned vshrink)
{
    float horizontalCoverage = (float)neogeo_sprite_debug_countShrinkWidth(hshrink) / 16.0f;
    float verticalCoverage = (float)(vshrink + 1u) / 256.0f;
    float horizontalShrink = 1.0f - horizontalCoverage;
    float verticalShrink = 1.0f - verticalCoverage;

    return neogeo_sprite_debug_shrinkRgbColorFromFractions(horizontalShrink, verticalShrink);
}

static uint32_t
neogeo_sprite_debug_paletteColor(unsigned paletteBank)
{
    float hue = (float)(paletteBank & 0xFFu) / 256.0f;
    return neogeo_sprite_debug_hueColor(hue);
}

static uint32_t
neogeo_sprite_debug_chainColor(unsigned chainRootIndex)
{
    float hue = (float)(chainRootIndex % 24u) / 24.0f;
    return neogeo_sprite_debug_hueColor(hue);
}

static uint32_t
neogeo_sprite_debug_shrinkRgbColorFromFractions(float horizontalShrink, float verticalShrink)
{
    Uint8 red = 0;
    Uint8 blue = 0;

    if (horizontalShrink < 0.0f) {
        horizontalShrink = 0.0f;
    }
    if (horizontalShrink > 1.0f) {
        horizontalShrink = 1.0f;
    }
    if (verticalShrink < 0.0f) {
        verticalShrink = 0.0f;
    }
    if (verticalShrink > 1.0f) {
        verticalShrink = 1.0f;
    }

    red = (Uint8)(horizontalShrink * 255.0f);
    blue = (Uint8)(verticalShrink * 255.0f);
    return neogeo_sprite_debug_color(red, 255, blue);
}

static void
neogeo_sprite_debug_drawShrinkLegend(uint32_t *pixels, int pitch, int extW, int extH,
                          int x, int y, int w, int h)
{
    const int outerPad = 6;
    const int plotPadX = 10;
    const int plotPadY = 10;
    const int axisLabelGap = 4;
    const int labelH = 5;
    const int cornerSwatch = 6;
    const uint32_t colPanel = neogeo_sprite_debug_color(22, 22, 22);
    const uint32_t colPanelInner = neogeo_sprite_debug_color(10, 10, 10);
    const uint32_t colAxis = neogeo_sprite_debug_color(160, 160, 160);
    const uint32_t colWhite = neogeo_sprite_debug_color(255, 255, 255);
    const uint32_t colShadow = neogeo_sprite_debug_color(0, 0, 0);
    int plotSize = 0;
    int plotX = 0;
    int plotY = 0;
    int plotW = 0;
    int plotH = 0;

    if (!pixels || w <= 24 || h <= 28) {
        return;
    }

    neogeo_sprite_debug_fillRectAbs(pixels, pitch, extW, extH, x, y, w, h, colPanel);
    neogeo_sprite_debug_drawRectAbs(pixels, pitch, extW, extH, x, y, w, h, colWhite);
    neogeo_sprite_debug_drawRectAbs(pixels, pitch, extW, extH, x + 1, y + 1, w - 2, h - 2, colShadow);

    plotW = w - plotPadX * 2;
    plotH = h - plotPadY * 2 - labelH - axisLabelGap;
    if (plotW <= 2 || plotH <= 2) {
        return;
    }
    plotSize = plotW < plotH ? plotW : plotH;
    plotW = plotSize;
    plotH = plotSize;
    plotX = x + (w - plotW) / 2;
    plotY = y + plotPadY;

    neogeo_sprite_debug_fillRectAbs(pixels, pitch, extW, extH, plotX - outerPad, plotY - outerPad,
                          plotW + outerPad * 2, plotH + outerPad * 2, colPanelInner);
    neogeo_sprite_debug_drawRectAbs(pixels, pitch, extW, extH, plotX - 1, plotY - 1, plotW + 2, plotH + 2, colAxis);

    for (int yy = 0; yy < plotH; ++yy) {
        float verticalShrink = plotH > 1 ? (float)yy / (float)(plotH - 1) : 0.0f;
        for (int xx = 0; xx < plotW; ++xx) {
            float horizontalShrink = plotW > 1 ? (float)xx / (float)(plotW - 1) : 0.0f;
            pixels[(plotY + yy) * pitch + (plotX + xx)] =
                neogeo_sprite_debug_shrinkRgbColorFromFractions(horizontalShrink, verticalShrink);
        }
    }

    neogeo_sprite_debug_fillRectAbs(pixels, pitch, extW, extH, plotX, plotY, cornerSwatch, cornerSwatch,
                          neogeo_sprite_debug_shrinkRgbColorFromFractions(0.0f, 0.0f));
    neogeo_sprite_debug_fillRectAbs(pixels, pitch, extW, extH, plotX + plotW - cornerSwatch, plotY, cornerSwatch, cornerSwatch,
                          neogeo_sprite_debug_shrinkRgbColorFromFractions(1.0f, 0.0f));
    neogeo_sprite_debug_fillRectAbs(pixels, pitch, extW, extH, plotX, plotY + plotH - cornerSwatch, cornerSwatch, cornerSwatch,
                          neogeo_sprite_debug_shrinkRgbColorFromFractions(0.0f, 1.0f));
    neogeo_sprite_debug_fillRectAbs(pixels, pitch, extW, extH, plotX + plotW - cornerSwatch, plotY + plotH - cornerSwatch, cornerSwatch, cornerSwatch,
                          neogeo_sprite_debug_shrinkRgbColorFromFractions(1.0f, 1.0f));

    neogeo_sprite_debug_drawDigits3x5(pixels, pitch, extW, extH, plotX, plotY + plotH + axisLabelGap, "0", colWhite);
    neogeo_sprite_debug_drawDigits3x5(pixels, pitch, extW, extH, plotX + plotW - 12, plotY + plotH + axisLabelGap, "255", colWhite);
    neogeo_sprite_debug_drawDigits3x5(pixels, pitch, extW, extH, plotX, plotY, "0", colWhite);
    neogeo_sprite_debug_drawDigits3x5(pixels, pitch, extW, extH, plotX, plotY + plotH - labelH, "255", colWhite);
}

static e9ui_component_t *
neogeo_sprite_debug_makeOverlayBodyHost(void)
{
    e9ui_component_t *host = (e9ui_component_t *)alloc_calloc(1, sizeof(*host));
    if (!host) {
        return NULL;
    }
    host->name = "neogeo_sprite_debug_overlay_body";
    host->preferredHeight = neogeo_sprite_debug_overlayBodyPreferredHeight;
    host->layout = neogeo_sprite_debug_overlayBodyLayout;
    host->render = neogeo_sprite_debug_overlayBodyRender;
    return host;
}

static void
neogeo_sprite_debug_overlayWindowCloseRequested(e9ui_window_t *window, void *user)
{
    (void)window;
    (void)user;
    neogeo_sprite_debug_toggle();
}

static uint32_t
neogeo_sprite_debug_color(Uint8 r, Uint8 g, Uint8 b)
{
    return (uint32_t)(0xFF000000u | (r << 16) | (g << 8) | b);
}

static uint32_t
neogeo_sprite_debug_hueColor(float h)
{
    if (h < 0.0f) {
        h -= (int)h;
    }
    if (h >= 1.0f) {
        h -= (int)h;
    }
    float i = floorf(h * 6.0f);
    float f = h * 6.0f - i;
    float q = 1.0f - f;
    int ii = ((int)i) % 6;
    float rr = 0.0f;
    float gg = 0.0f;
    float bb = 0.0f;
    switch (ii) {
    case 0: rr = 1.0f; gg = f; bb = 0.0f; break;
    case 1: rr = q; gg = 1.0f; bb = 0.0f; break;
    case 2: rr = 0.0f; gg = 1.0f; bb = f; break;
    case 3: rr = 0.0f; gg = q; bb = 1.0f; break;
    case 4: rr = f; gg = 0.0f; bb = 1.0f; break;
    case 5: rr = 1.0f; gg = 0.0f; bb = q; break;
    }
    return neogeo_sprite_debug_color((Uint8)(rr * 255.0f), (Uint8)(gg * 255.0f), (Uint8)(bb * 255.0f));
}

static unsigned
neogeo_sprite_debug_countShrinkWidth(unsigned hval)
{
    unsigned h = (hval & NEOGEO_SPRITE_DEBUG_SCB2_HSHRINK_MASK);
    unsigned w = 0;
    for (unsigned p = 0; p < 16; ++p) {
        w += (unsigned)neogeo_sprite_debug_lut_hshrink[h][p];
    }
    return w;
}

static uint32_t
neogeo_sprite_debug_hashSprites(const uint16_t *scb2, const uint16_t *scb3, const uint16_t *scb4)
{
    uint32_t h = 2166136261u;
    for (unsigned i = 1; i < (unsigned)NG_MAX_SPRITES; ++i) {
        h ^= scb2[i];
        h *= 16777619u;
        h ^= scb3[i];
        h *= 16777619u;
        h ^= scb4[i];
        h *= 16777619u;
    }
    return h;
}

static void
neogeo_sprite_debug_drawFixMiniMap(uint32_t *pixels, int pitch, int extW, int extH,
                         int x, int y, int w, int h, const uint16_t *vram,
                         const uint8_t *fixrom, size_t fixromSize)
{
    const uint32_t colPanel = neogeo_sprite_debug_color(22, 22, 22);
    const uint32_t colPanelInner = neogeo_sprite_debug_color(10, 10, 10);
    const uint32_t colWhite = neogeo_sprite_debug_color(255, 255, 255);
    const uint32_t colGreen = neogeo_sprite_debug_color(0, 255, 0);
    const uint32_t colGrid = neogeo_sprite_debug_color(44, 44, 44);

    if (!pixels || !vram || w < 3 || h < 3) {
        return;
    }

    neogeo_sprite_debug_fillRectAbs(pixels, pitch, extW, extH, x, y, w, h, colPanel);
    neogeo_sprite_debug_drawRectAbs(pixels, pitch, extW, extH, x, y, w, h, colWhite);
    neogeo_sprite_debug_fillRectAbs(pixels, pitch, extW, extH, x + 1, y + 1, w - 2, h - 2, colPanelInner);

    {
        const int innerPad = 4;
        int plotX = x + innerPad;
        int plotY = y + innerPad;
        int plotW = w - innerPad * 2;
        int plotH = h - innerPad * 2;

        if (plotW <= 0 || plotH <= 0) {
            return;
        }

        for (int row = 0; row < NG_FIX_ROWS; ++row) {
            int y0 = plotY + (row * plotH) / NG_FIX_ROWS;
            int y1 = plotY + ((row + 1) * plotH) / NG_FIX_ROWS;

            if (y1 <= y0) {
                y1 = y0 + 1;
            }
            for (int col = 0; col < NG_FIX_COLS; ++col) {
                uint16_t entry = vram[NG_FIX_MAP_BASE + (size_t)row + ((size_t)col << 5)];
                int x0 = plotX + (col * plotW) / NG_FIX_COLS;
                int x1 = plotX + ((col + 1) * plotW) / NG_FIX_COLS;
                uint32_t tileCol = colGrid;

                if (x1 <= x0) {
                    x1 = x0 + 1;
                }
                if ((entry & NG_FIX_TILE_MASK) != 0u) {
                    unsigned tileNum = (unsigned)(entry & NG_FIX_TILE_MASK);
                    unsigned paletteBank = (unsigned)((entry >> NG_FIX_PALETTE_SHIFT) & NG_FIX_PALETTE_MASK);

                    if (neogeo_sprite_debug_fixTileHasPixels(fixrom, fixromSize, tileNum)) {
                        tileCol = colGreen;
                        if (neogeo_sprite_debugState.viewMode == neogeo_sprite_debug_view_mode_palette) {
                            tileCol = neogeo_sprite_debug_paletteColor(paletteBank);
                        }
                    }
                }
                neogeo_sprite_debug_fillRectAbs(pixels, pitch, extW, extH,
                                      x0, y0, x1 - x0, y1 - y0, tileCol);
            }
        }
    }
}

static void
neogeo_sprite_debug_drawDigits3x5(uint32_t *pixels, int pitch, int extW, int extH,
                        int x, int y, const char *buf, uint32_t color)
{
    static const uint8_t digits[10][5] = {
        {0b111,0b101,0b101,0b101,0b111},
        {0b010,0b110,0b010,0b010,0b111},
        {0b111,0b001,0b111,0b100,0b111},
        {0b111,0b001,0b111,0b001,0b111},
        {0b101,0b101,0b111,0b001,0b001},
        {0b111,0b100,0b111,0b001,0b111},
        {0b111,0b100,0b111,0b101,0b111},
        {0b111,0b001,0b010,0b010,0b010},
        {0b111,0b101,0b111,0b101,0b111},
        {0b111,0b101,0b111,0b001,0b111},
    };
    const int glyphW = 3;
    const int glyphH = 5;
    const int spacing = 1;
    int cx = x;
    int cy = y;
    if (!pixels || !buf) {
        return;
    }
    for (int i = 0; buf[i]; ++i) {
        char ch = buf[i];
        if (ch < '0' || ch > '9') {
            cx += glyphW + spacing;
            continue;
        }
        int d = ch - '0';
        for (int ry = 0; ry < glyphH; ++ry) {
            uint8_t rowBits = digits[d][ry];
            for (int rx = 0; rx < glyphW; ++rx) {
                if (rowBits & (uint8_t)(1u << (glyphW - 1 - rx))) {
                    int px = cx + rx;
                    int py = cy + ry;
                    if (px >= 0 && px < extW && py >= 0 && py < extH) {
                        pixels[py * pitch + px] = color;
                    }
                }
            }
        }
        cx += glyphW + spacing;
    }
}

static uint32_t
neogeo_sprite_debug_hashWords(const uint16_t *words, size_t count)
{
    uint32_t h = 2166136261u;

    if (!words) {
        return h;
    }
    for (size_t i = 0; i < count; ++i) {
        h ^= words[i];
        h *= 16777619u;
    }
    return h;
}

static int
neogeo_sprite_debug_fixTileHasPixels(const uint8_t *fixrom, size_t fixromSize, unsigned tileNum)
{
    size_t tileBase = (size_t)tileNum << 5;

    if (!fixrom || tileBase + 32u > fixromSize) {
        return 0;
    }
    for (size_t i = 0; i < 32u; ++i) {
        if (fixrom[tileBase + i] != 0u) {
            return 1;
        }
    }
    return 0;
}

static void
neogeo_sprite_debug_fillRectAbs(uint32_t *pixels, int pitch, int extW, int extH,
                      int x, int y, int w, int h, uint32_t color)
{
    int x0 = x;
    int y0 = y;
    int x1 = x0 + w;
    int y1 = y0 + h;
    if (x1 <= 0 || y1 <= 0) {
        return;
    }
    if (x0 >= extW || y0 >= extH) {
        return;
    }
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > extW) {
        x1 = extW;
    }
    if (y1 > extH) {
        y1 = extH;
    }
    int cw = x1 - x0;
    int ch = y1 - y0;
    if (cw <= 0 || ch <= 0) {
        return;
    }
    for (int yy = y0; yy < y0 + ch; ++yy) {
        uint32_t *row = pixels + yy * pitch + x0;
        for (int xx = 0; xx < cw; ++xx) {
            row[xx] = color;
        }
    }
}

static void
neogeo_sprite_debug_drawRectAbs(uint32_t *pixels, int pitch, int extW, int extH,
                      int x, int y, int w, int h, uint32_t color)
{
    neogeo_sprite_debug_fillRectAbs(pixels, pitch, extW, extH, x, y, w, 1, color);
    neogeo_sprite_debug_fillRectAbs(pixels, pitch, extW, extH, x, y + h - 1, w, 1, color);
    neogeo_sprite_debug_fillRectAbs(pixels, pitch, extW, extH, x, y, 1, h, color);
    neogeo_sprite_debug_fillRectAbs(pixels, pitch, extW, extH, x + w - 1, y, 1, h, color);
}

static void
neogeo_sprite_debug_fillRectCoord(uint32_t *pixels, int pitch, int extW, int extH,
                        int cx, int cy, int cw, int ch, uint32_t color)
{
    if (cw <= 0 || ch <= 0) {
        return;
    }
    int sx = cx + NG_COORD_OFFSET_X;
    int sy = cy + NG_COORD_OFFSET_Y;
    int x0 = sx;
    int y0 = sy;
    int x1 = sx + cw;
    int y1 = sy + ch;
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > NG_COORD_W) {
        x1 = NG_COORD_W;
    }
    if (y1 > NG_COORD_H) {
        y1 = NG_COORD_H;
    }
    int w = x1 - x0;
    int h = y1 - y0;
    if (w <= 0 || h <= 0) {
        return;
    }
    neogeo_sprite_debug_fillRectAbs(pixels, pitch, extW, extH, x0, y0, w, h, color);
}

static void
neogeo_sprite_debug_fillRectCoordWrapped(uint32_t *pixels, int pitch, int extW, int extH,
                               int x, int y, int w, int h, uint32_t color)
{
    neogeo_sprite_debug_fillRectCoord(pixels, pitch, extW, extH, x, y, w, h, color);
    neogeo_sprite_debug_fillRectCoord(pixels, pitch, extW, extH, x - NG_COORD_SIZE, y, w, h, color);
    neogeo_sprite_debug_fillRectCoord(pixels, pitch, extW, extH, x, y - NG_COORD_SIZE, w, h, color);
    neogeo_sprite_debug_fillRectCoord(pixels, pitch, extW, extH, x - NG_COORD_SIZE, y - NG_COORD_SIZE, w, h, color);
}

static int
neogeo_sprite_debug_drawBadge(uint32_t *pixels, int pitch, int extW, int extH,
                    int x, int y, int value, int alignRight, int limit,
                    uint32_t colWhite)
{
    const int glyphW = 3;
    const int glyphH = 5;
    const int spacing = 1;
    const int pad = 4;
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%d", value);

    if (n < 1) {
        n = 1;
    }
    if (n > (int)(sizeof(buf) - 1)) {
        n = (int)(sizeof(buf) - 1);
        buf[n] = '\0';
    }

    int textW = n * glyphW + (n - 1) * spacing;
    int badgeW = textW + pad * 2;
    int badgeH = glyphH + pad * 2;
    int bx = alignRight ? x - badgeW : x;
    uint32_t badgeCol = value > limit ? neogeo_sprite_debug_color(200, 0, 0) : neogeo_sprite_debug_color(64, 64, 64);

    neogeo_sprite_debug_fillRectAbs(pixels, pitch, extW, extH, bx, y, badgeW, badgeH, badgeCol);
    neogeo_sprite_debug_drawDigits3x5(pixels, pitch, extW, extH, bx + pad, y + pad, buf, colWhite);
    return badgeH;
}

void
neogeo_sprite_debug_toggle(void)
{
    if (!neogeo_sprite_debugState.windowState.open) {
        e9ui_component_t *controlsRow = NULL;

        neogeo_sprite_debugState.windowState.windowHost = e9ui_windowCreate(neogeo_sprite_debug_windowBackend());
        if (!neogeo_sprite_debugState.windowState.windowHost) {
            return;
        }
        neogeo_sprite_debugState.root = e9ui_stack_makeVertical();
        neogeo_sprite_debugState.overlayBodyHost = neogeo_sprite_debug_makeOverlayBodyHost();
        memset(neogeo_sprite_debugState.modeButtons, 0, sizeof(neogeo_sprite_debugState.modeButtons));
        controlsRow = neogeo_sprite_debug_makeControlsRow();
        if (neogeo_sprite_debugState.root && neogeo_sprite_debugState.overlayBodyHost) {
            if (controlsRow) {
                e9ui_stack_addFixed(neogeo_sprite_debugState.root, controlsRow);
                e9ui_stack_addFixed(neogeo_sprite_debugState.root, e9ui_vspacer_make(NEOGEO_SPRITE_DEBUG_CONTROL_VGAP));
            }
            e9ui_stack_addFlex(neogeo_sprite_debugState.root, neogeo_sprite_debugState.overlayBodyHost);
        }
        e9ui_rect_t rect = e9ui_windowResolveStateOpenRect(&e9ui->ctx,
                                                           neogeo_sprite_debug_windowDefaultRect(&e9ui->ctx),
                                                           &neogeo_sprite_debugState.windowState);
        e9ui_windowOpen(neogeo_sprite_debugState.windowState.windowHost,
                                     "ENGINE9000 DEBUGGER - Sprites",
                                     rect,
                                     neogeo_sprite_debugState.root ? neogeo_sprite_debugState.root : neogeo_sprite_debugState.overlayBodyHost,
                                     neogeo_sprite_debug_overlayWindowCloseRequested,
                                     NULL,
			             &e9ui->ctx);
        neogeo_sprite_debugState.window = e9ui->ctx.window;
        neogeo_sprite_debugState.renderer = e9ui->ctx.renderer;
        neogeo_sprite_debugState.windowState.open = 1;
        aux_window_register(&neogeo_sprite_debug_auxWindowOps, &neogeo_sprite_debugState);
    } else {
        aux_window_unregister(&neogeo_sprite_debug_auxWindowOps, &neogeo_sprite_debugState);
        if (neogeo_sprite_debugState.texture) {
            SDL_DestroyTexture(neogeo_sprite_debugState.texture);
            neogeo_sprite_debugState.texture = NULL;
        }
        if (neogeo_sprite_debugState.pixels) {
            free(neogeo_sprite_debugState.pixels);
            neogeo_sprite_debugState.pixels = NULL;
            neogeo_sprite_debugState.pixelsCap = 0;
        }
        neogeo_sprite_debugState.texW = 0;
        neogeo_sprite_debugState.texH = 0;
        neogeo_sprite_debugState.histGradReady = 0;
        neogeo_sprite_debugState.cachedValid = 0;
        neogeo_sprite_debugState.lastHash = 0;
        if (neogeo_sprite_debugState.windowState.windowHost) {
            e9ui_windowDestroy(neogeo_sprite_debugState.windowState.windowHost);
            neogeo_sprite_debugState.windowState.windowHost = NULL;
        }
        neogeo_sprite_debugState.overlayBodyHost = NULL;
        neogeo_sprite_debugState.root = NULL;
        memset(neogeo_sprite_debugState.modeButtons, 0, sizeof(neogeo_sprite_debugState.modeButtons));
        neogeo_sprite_debugState.windowState.open = 0;
        neogeo_sprite_debugState.hasLastState = 0;
        neogeo_sprite_debugState.window = NULL;
        neogeo_sprite_debugState.renderer = NULL;
    }
}

int
neogeo_sprite_debug_is_open(void)
{
    return neogeo_sprite_debugState.windowState.open ? 1 : 0;
}

void
neogeo_sprite_debug_setMainWindowFocused(int focused)
{
    (void)focused;
}

void
neogeo_sprite_debug_renderFrameInternal(const e9k_debug_sprite_state_t *st, int presentFrame)
{
    if (!neogeo_sprite_debugState.windowState.open || !neogeo_sprite_debugState.renderer) {
        return;
    }
    if (!st || !st->vram) {
        return;
    }
    const uint16_t *vram = st->vram;
    e9k_debug_rom_region_t fixrom = { 0 };
    const uint8_t *fixromData = NULL;
    size_t fixromSize = 0u;
    if (st->vram_words <= (NEOGEO_SPRITE_DEBUG_SCB4_WORD_OFFSET + NG_MAX_SPRITES)) {
        return;
    }
    if (libretro_host_debugGetFixRom(&fixrom)) {
        fixromData = fixrom.data;
        fixromSize = fixrom.size;
    }
    const uint16_t *scb2 = vram + NEOGEO_SPRITE_DEBUG_SCB2_WORD_OFFSET;
    const uint16_t *scb3 = vram + NEOGEO_SPRITE_DEBUG_SCB3_WORD_OFFSET;
    const uint16_t *scb4 = vram + NEOGEO_SPRITE_DEBUG_SCB4_WORD_OFFSET;

    const int baseW = NG_COORD_W;
    const int baseH = NG_COORD_H;
    int extW = baseW;
    const int extH = baseH;
    if (neogeo_sprite_debug_histogramEnabled) {
        extW += DBG_GAP + DBG_HIST_WIDTH;
    }
    if (neogeo_sprite_debugState.texW != extW || neogeo_sprite_debugState.texH != extH) {
        SDL_DestroyTexture(neogeo_sprite_debugState.texture);
        neogeo_sprite_debugState.texture = SDL_CreateTexture(neogeo_sprite_debugState.renderer, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STREAMING, extW, extH);
        neogeo_sprite_debugState.texW = extW;
        neogeo_sprite_debugState.texH = extH;
    }
    if (!neogeo_sprite_debugState.texture) {
        return;
    }
    uint32_t hash = neogeo_sprite_debug_hashSprites(scb2, scb3, scb4);
    hash ^= neogeo_sprite_debug_hashWords(vram + NG_FIX_MAP_BASE, NG_FIX_MAP_WORDS);
    hash *= 16777619u;
    if (neogeo_sprite_debugState.cachedValid && hash == neogeo_sprite_debugState.lastHash && neogeo_sprite_debugState.texture) {
        neogeo_sprite_debug_presentTexture(baseW, baseH, presentFrame);
        return;
    }
    size_t needed = (size_t)extW * (size_t)extH;
    if (needed > neogeo_sprite_debugState.pixelsCap) {
        uint32_t *next = (uint32_t *)realloc(neogeo_sprite_debugState.pixels, needed * sizeof(uint32_t));
        if (!next) {
            return;
        }
        neogeo_sprite_debugState.pixels = next;
        neogeo_sprite_debugState.pixelsCap = needed;
    }
    uint32_t *pixels = neogeo_sprite_debugState.pixels;
    const uint32_t colBg = neogeo_sprite_debug_color(68, 68, 68);
    const uint32_t colBlack = neogeo_sprite_debug_color(0, 0, 0);
    const uint32_t colWhite = neogeo_sprite_debug_color(255, 255, 255);
    const uint32_t colGreen = neogeo_sprite_debug_color(0, 255, 0);
    const uint32_t colAnim = neogeo_sprite_debug_color(255, 192, 0);
    const uint32_t colHistBg = neogeo_sprite_debug_color(34, 34, 34);
    const uint32_t colBounds = neogeo_sprite_debug_color(120, 120, 120);
    for (size_t i = 0; i < needed; ++i) {
        pixels[i] = colBlack;
    }
    neogeo_sprite_debug_fillRectAbs(pixels, extW, extW, extH, 0, 0, baseW, baseH, colBg);
    neogeo_sprite_debug_drawRectAbs(pixels, extW, extW, extH,
                          NG_COORD_OFFSET_X, NG_COORD_OFFSET_Y,
                          NG_COORD_SIZE, NG_COORD_SIZE, colBounds);

    SDL_Rect screenRect = { NG_VISIBLE_X0, NG_VISIBLE_Y0, NG_VISIBLE_W, NG_VISIBLE_H };
    neogeo_sprite_debug_fillRectCoord(pixels, extW, extW, extH,
                            screenRect.x, screenRect.y, screenRect.w, screenRect.h, colBlack);
    {
        int bx0 = screenRect.x - 1 + NG_COORD_OFFSET_X;
        int by0 = screenRect.y - 1 + NG_COORD_OFFSET_Y;
        int bw = screenRect.w + 2;
        int bh = screenRect.h + 2;
        neogeo_sprite_debug_drawRectAbs(pixels, extW, extW, extH, bx0, by0, bw, bh, colWhite);
    }

    unsigned viscountLine[NG_VISIBLE_H];
    for (int i = 0; i < NG_VISIBLE_H; ++i) {
        viscountLine[i] = 0;
    }
    int screenH = (st->screen_h > 0) ? st->screen_h : SCREEN_HEIGHT;
    int activeTotal = 0;
    for (unsigned i = 1; i < (unsigned)NG_MAX_SPRITES; ) {
        if (scb3[i] & NEOGEO_SPRITE_DEBUG_SCB3_CHAIN_FLAG) {
            ++i;
            continue;
        }
        uint16_t scb3b = scb3[i];
        unsigned bh = (unsigned)(scb3b & NEOGEO_SPRITE_DEBUG_SCB3_ROW_MASK);
        unsigned by = (unsigned)((scb3b >> NEOGEO_SPRITE_DEBUG_SCB3_YPOS_SHIFT) &
                                 NEOGEO_SPRITE_DEBUG_SCB3_YPOS_MASK);
        unsigned len = 1;
        while ((i + len) < (unsigned)NG_MAX_SPRITES &&
               (scb3[i + len] & NEOGEO_SPRITE_DEBUG_SCB3_CHAIN_FLAG)) {
            ++len;
        }
        if (bh != 0 && by != (unsigned)screenH) {
            activeTotal += (int)len;
        }
        i += len;
    }
    unsigned sprlimit = st->sprlimit ? st->sprlimit : NG_SPRITES_PER_LINE_MAX;
    int maxcnt = 0;
    for (int line = 0; line < NG_COORD_SIZE; ++line) {
        unsigned sprcount = 0;
        unsigned viscount = 0;

        unsigned xpos = 0;
        unsigned ypos = 0;
        unsigned sprsize = 0;
        unsigned hshrink = NEOGEO_SPRITE_DEBUG_SCB2_HSHRINK_MASK;
        unsigned vshrink = NEOGEO_SPRITE_DEBUG_SCB2_VSHRINK_MASK;
        unsigned chainRootIndex = 1;

        for (unsigned i = 1; i < (unsigned)NG_MAX_SPRITES; ++i) {
            uint16_t scb3w = scb3[i];
            uint16_t scb2w = scb2[i];
            uint16_t scb4w = scb4[i];

            if (scb3w & NEOGEO_SPRITE_DEBUG_SCB3_CHAIN_FLAG) {
                xpos = (unsigned)((xpos + (hshrink + 1)) & NG_WRAP_MASK);
            } else {
                chainRootIndex = i;
                xpos = (unsigned)((scb4w >> NEOGEO_SPRITE_DEBUG_SCB4_XPOS_SHIFT) & NG_WRAP_MASK);
                ypos = (unsigned)((scb3w >> NEOGEO_SPRITE_DEBUG_SCB3_YPOS_SHIFT) & NG_WRAP_MASK);
                sprsize = (unsigned)(scb3w & NEOGEO_SPRITE_DEBUG_SCB3_ROW_MASK);
                vshrink = (unsigned)(scb2w & NEOGEO_SPRITE_DEBUG_SCB2_VSHRINK_MASK);
            }
            hshrink = (unsigned)((scb2w >> NEOGEO_SPRITE_DEBUG_SCB2_HSHRINK_SHIFT) &
                                 NEOGEO_SPRITE_DEBUG_SCB2_HSHRINK_MASK);

            int vline = line + NG_LINE_OFFSET;
            unsigned srow = (unsigned)(((vline - (int)(NG_COORD_SIZE - (int)ypos))) & NG_WRAP_MASK);
            if ((sprsize == 0) || (srow >= (sprsize << 4))) {
                continue;
            }

            if (sprcount == NG_SPRITES_PER_LINE_MAX) {
                break;
            }
            sprcount++;

            int w = (int)neogeo_sprite_debug_countShrinkWidth(hshrink);
            if (w <= 0) {
                continue;
            }
            int hasAnimBits = 0;
            unsigned paletteBank = 0;
            unsigned oddWordOffset = i * NEOGEO_SPRITE_DEBUG_SPRITE_VRAM_WORDS_PER_SPRITE +
                NEOGEO_SPRITE_DEBUG_SPRITE_TILE_ODD_WORD_OFFSET;
            if (oddWordOffset < st->vram_words) {
                if (vram[oddWordOffset] & NEOGEO_SPRITE_DEBUG_SPRITE_ANIM_MASK) {
                    hasAnimBits = 1;
                }
                paletteBank = (unsigned)((vram[oddWordOffset] >> NEOGEO_SPRITE_DEBUG_SPRITE_PALETTE_SHIFT) &
                                         NEOGEO_SPRITE_DEBUG_SPRITE_PALETTE_MASK);
            }
            uint32_t spriteCol = hasAnimBits ? colAnim : colGreen;
            if (neogeo_sprite_debugState.viewMode == neogeo_sprite_debug_view_mode_shrink) {
                spriteCol = neogeo_sprite_debug_shrinkRgbColor(hshrink, vshrink);
            } else if (neogeo_sprite_debugState.viewMode == neogeo_sprite_debug_view_mode_palette) {
                spriteCol = neogeo_sprite_debug_paletteColor(paletteBank);
            } else if (neogeo_sprite_debugState.viewMode == neogeo_sprite_debug_view_mode_chain) {
                spriteCol = neogeo_sprite_debug_chainColor(chainRootIndex);
            }
            int x0 = (int)(xpos & NG_WRAP_MASK);
            int xsum = x0 + w;
            int visible = (x0 < NG_VISIBLE_W) || (xsum > NG_COORD_SIZE);
            if (visible) {
                viscount++;
            }

            neogeo_sprite_debug_fillRectCoordWrapped(pixels, extW, extW, extH, x0, line, 1, 1, spriteCol);
            neogeo_sprite_debug_fillRectCoordWrapped(pixels, extW, extW, extH, x0 + w - 1, line, 1, 1, spriteCol);

            unsigned totalH = (unsigned)(sprsize << 4);
            if (srow == 0u || (srow + 1u) == totalH) {
                neogeo_sprite_debug_fillRectCoordWrapped(pixels, extW, extW, extH, x0, line, w, 1, spriteCol);
            }
        }

        if (line >= NG_VISIBLE_Y0 && line < (NG_VISIBLE_Y0 + NG_VISIBLE_H)) {
            viscountLine[line - NG_VISIBLE_Y0] = viscount;
        }
        if ((int)sprcount > maxcnt) {
            maxcnt = (int)sprcount;
        }
    }

    if (neogeo_sprite_debug_histogramEnabled) {
        int histX0 = NG_COORD_OFFSET_X + NG_COORD_SIZE + DBG_GAP;
        int histW = DBG_HIST_WIDTH;
        int fixW = histW;
        int fixH = (fixW * NG_FIX_ROWS) / NG_FIX_COLS;

        if (histW < 1) {
            histW = 1;
        }
        if (fixH > NG_COORD_OFFSET_Y - NG_FIX_PANEL_PAD * 2) {
            fixH = NG_COORD_OFFSET_Y - NG_FIX_PANEL_PAD * 2;
        }
        if (fixH >= 3) {
            neogeo_sprite_debug_drawFixMiniMap(pixels, extW, extW, extH,
                                     histX0,
                                     NG_FIX_PANEL_PAD,
                                     fixW,
                                     fixH,
                                     vram,
                                     fixromData,
                                     fixromSize);
        }

        neogeo_sprite_debug_fillRectAbs(pixels, extW, extW, extH,
                              histX0, NG_VISIBLE_Y0 + NG_COORD_OFFSET_Y,
                              histW, NG_VISIBLE_H, colHistBg);
        if (!neogeo_sprite_debugState.histGradReady) {
            int denomx = (DBG_HIST_WIDTH > 1) ? (DBG_HIST_WIDTH - 1) : 1;
            for (int dx = 0; dx < DBG_HIST_WIDTH; ++dx) {
                float t = (float)dx / (float)denomx;
                float h = (1.0f / 3.0f) * (1.0f - t);
                neogeo_sprite_debugState.histGrad[dx] = neogeo_sprite_debug_hueColor(h);
            }
            neogeo_sprite_debugState.histGradReady = 1;
        }
        for (int line = 0; line < NG_VISIBLE_H; ++line) {
            int viscount = (int)viscountLine[line];
            int barLen = (int)((viscount * (unsigned)histW) / NG_SPRITES_PER_LINE_MAX);
            if (barLen > histW) {
                barLen = histW;
            }
            if (barLen > 0) {
                int y = NG_VISIBLE_Y0 + line + NG_COORD_OFFSET_Y;
                uint32_t *row = pixels + y * extW + histX0;
                for (int dx = 0; dx < barLen; ++dx) {
                    row[dx] = neogeo_sprite_debugState.histGrad[dx];
                }
            }
        }

        {
            int bx0 = histX0 - 1;
            int by0 = NG_VISIBLE_Y0 - 1 + NG_COORD_OFFSET_Y;
            int bw = histW + 2;
            int bh = NG_VISIBLE_H + 2;
            neogeo_sprite_debug_drawRectAbs(pixels, extW, extW, extH, bx0, by0, bw, bh, colWhite);
        }

        {
            const int glyphH = 5;
            const int pad = 4;
            const int legendGap = 10;
            const int statsY = NG_VISIBLE_Y0 + NG_COORD_OFFSET_Y + NG_VISIBLE_H + 6;
            if (statsY + glyphH + pad * 2 <= extH) {
                int rightBadgeH = neogeo_sprite_debug_drawBadge(pixels, extW, extW, extH,
                                                       histX0 + histW, statsY,
                                                       maxcnt, 1, (int)sprlimit, colWhite);
                int leftBadgeH = neogeo_sprite_debug_drawBadge(pixels, extW, extW, extH,
                                                      histX0, statsY,
                                                      activeTotal, 0, NG_MAX_SPRITES - 1, colWhite);

                if (neogeo_sprite_debugState.viewMode == neogeo_sprite_debug_view_mode_shrink) {
                    int legendH = histW;
                    int legendY = statsY + (leftBadgeH > rightBadgeH ? leftBadgeH : rightBadgeH) + legendGap;
                    if (legendY + legendH <= extH) {
                        neogeo_sprite_debug_drawShrinkLegend(pixels, extW, extW, extH,
                                                  histX0, legendY, histW, legendH);
                    }
                }
            }
        }
    }

    SDL_UpdateTexture(neogeo_sprite_debugState.texture, NULL, pixels, extW * (int)sizeof(uint32_t));
    neogeo_sprite_debug_presentTexture(baseW, baseH, presentFrame);
    neogeo_sprite_debugState.cachedValid = 1;
    neogeo_sprite_debugState.lastHash = hash;
}

void
neogeo_sprite_debug_render(const e9k_debug_sprite_state_t *st)
{
    if (st) {
        neogeo_sprite_debugState.lastState = *st;
        neogeo_sprite_debugState.hasLastState = 1;
    }
    if (!neogeo_sprite_debugState.windowState.open) {
        return;
    }
    if (e9ui_windowCaptureStateRectChanged(&neogeo_sprite_debugState.windowState,
                                           &e9ui->ctx)) {
        config_saveConfig();
    }
}

void
neogeo_sprite_debug_persistConfig(FILE *file)
{
    if (!file) {
        return;
    }
    fprintf(file, "comp.sprite_debug.view_mode=%d\n", (int)neogeo_sprite_debugState.viewMode);
    e9ui_windowPersistStateRect(file,
                                "comp.sprite_debug",
                                &neogeo_sprite_debugState.windowState,
                                &e9ui->ctx);
}

int
neogeo_sprite_debug_loadConfigProperty(const char *prop, const char *value)
{
    static const struct {
        const char *prop;
        int *target;
    } rectProps[] = {
        { "win_x", &neogeo_sprite_debugState.windowState.winX },
        { "win_y", &neogeo_sprite_debugState.windowState.winY },
        { "win_w", &neogeo_sprite_debugState.windowState.winW },
        { "win_h", &neogeo_sprite_debugState.windowState.winH },
    };
    if (!prop || !value) {
        return 0;
    }
    int intValue = 0;

    for (int i = 0; i < (int)(sizeof(rectProps) / sizeof(rectProps[0])); ++i) {
        if (strcmp(prop, rectProps[i].prop) == 0) {
            if (!neogeo_sprite_debug_parseInt(value, &intValue)) {
                return 0;
            }
            *rectProps[i].target = intValue;
            neogeo_sprite_debugState.windowState.winHasSaved =
                e9ui_windowHasSavedPosition(neogeo_sprite_debugState.windowState.winX, neogeo_sprite_debugState.windowState.winY);
            return 1;
        }
    }
    if (strcmp(prop, "view_mode") != 0) {
        return 0;
    }
    if (!neogeo_sprite_debug_parseInt(value, &intValue)) {
        return 0;
    }
    neogeo_sprite_debugState.viewMode = neogeo_sprite_debug_isValidViewMode((neogeo_sprite_debug_view_mode_t)intValue) ?
        (neogeo_sprite_debug_view_mode_t)intValue :
        neogeo_sprite_debug_view_mode_normal;
    if (neogeo_sprite_debugState.windowState.open) {
        neogeo_sprite_debugState.cachedValid = 0;
        neogeo_sprite_debug_updateModeButtons();
    }
    return 1;
}
