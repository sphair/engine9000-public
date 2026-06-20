/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "profile_list.h"
#include "analyse.h"
#include "e9ui.h"
#include "strutil.h"
#include "ui.h"

typedef struct {
    unsigned int pc;
    unsigned long long samples;
    unsigned long long cycles;
    unsigned long long total;
    char sampleText[80];
    char locationText[ANALYSE_LOCATION_TEXT_CAP];
    char sourceText[ANALYSE_SOURCE_TEXT_CAP];
    char file[PATH_MAX];
    int line;
    TTF_Font *measureFont;
    int sampleTextWidth;
    int sampleTextHeight;
    int locationTextWidth;
    int locationTextHeight;
    int sourceTextWidth;
    int sourceTextHeight;
    SDL_Rect locationRect;
    SDL_Rect sourceRect;
    SDL_Rect sampleRect;
    SDL_Renderer *textureRenderer;
    TTF_Font *textureFont;
    SDL_Texture *sampleTexture;
    SDL_Texture *locationTexture;
    SDL_Texture *sourceTexture;
} profile_hotspot_state_t;

static int  profile_hotspot_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW);
static void profile_hotspot_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds);
static void profile_hotspot_render(e9ui_component_t *self, e9ui_context_t *ctx);
static void profile_hotspot_dtor(e9ui_component_t *self, e9ui_context_t *ctx);
static void profile_hotspot_onClick(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouse_ev);
static void profile_hotspot_linkClicked(e9ui_context_t *ctx, void *user);
static void profile_hotspot_renderBar(e9ui_component_t *self, e9ui_context_t *ctx, const profile_hotspot_state_t *st);

static void
profile_hotspot_destroyTextures(profile_hotspot_state_t *st)
{
    if (!st) {
        return;
    }
    if (st->sampleTexture) {
        SDL_DestroyTexture(st->sampleTexture);
        st->sampleTexture = NULL;
    }
    if (st->locationTexture) {
        SDL_DestroyTexture(st->locationTexture);
        st->locationTexture = NULL;
    }
    if (st->sourceTexture) {
        SDL_DestroyTexture(st->sourceTexture);
        st->sourceTexture = NULL;
    }
    st->textureRenderer = NULL;
    st->textureFont = NULL;
}

static SDL_Texture *
profile_hotspot_createTexture(SDL_Renderer *renderer,
                              TTF_Font *font,
                              const char *text,
                              SDL_Color color,
                              int *outWidth,
                              int *outHeight)
{
    if (outWidth) {
        *outWidth = 0;
    }
    if (outHeight) {
        *outHeight = 0;
    }
    if (!renderer || !font || !text || !*text) {
        return NULL;
    }
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, color);
    if (!surface) {
        return NULL;
    }
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (outWidth) {
        *outWidth = surface->w;
    }
    if (outHeight) {
        *outHeight = surface->h;
    }
    SDL_FreeSurface(surface);
    return texture;
}

static void
profile_hotspot_updateTextures(profile_hotspot_state_t *st, SDL_Renderer *renderer, TTF_Font *font)
{
    if (!st || !renderer || !font) {
        return;
    }
    if (st->textureRenderer == renderer && st->textureFont == font) {
        return;
    }
    profile_hotspot_destroyTextures(st);
    st->textureRenderer = renderer;
    st->textureFont = font;
    st->measureFont = font;
    st->sampleTextHeight = TTF_FontHeight(font);
    if (st->sampleTextHeight <= 0) {
        st->sampleTextHeight = 16;
    }
    st->locationTextHeight = st->sampleTextHeight;
    st->sourceTextHeight = st->sampleTextHeight;
    SDL_Color primary = { 230, 230, 230, 255 };
    SDL_Color location = { 170, 190, 230, 255 };
    SDL_Color secondary = { 165, 165, 170, 255 };
    st->sampleTexture = profile_hotspot_createTexture(renderer,
                                                      font,
                                                      st->sampleText,
                                                      primary,
                                                      &st->sampleTextWidth,
                                                      &st->sampleTextHeight);
    st->locationTexture = profile_hotspot_createTexture(renderer,
                                                        font,
                                                        st->locationText,
                                                        location,
                                                        &st->locationTextWidth,
                                                        &st->locationTextHeight);
    st->sourceTexture = profile_hotspot_createTexture(renderer,
                                                      font,
                                                      st->sourceText,
                                                      secondary,
                                                      &st->sourceTextWidth,
                                                      &st->sourceTextHeight);
}

static int
profile_hotspot_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;

    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : (ctx ? ctx->font : NULL);
    profile_hotspot_state_t *st = self ? (profile_hotspot_state_t*)self->state : NULL;
    if (font && st && ctx && ctx->renderer) {
        profile_hotspot_updateTextures(st, ctx->renderer, font);
    }
    int lh = st && st->sampleTextHeight > 0 ? st->sampleTextHeight : (font ? TTF_FontHeight(font) : 16);
    if (lh <= 0) lh = 16;

    int padY = e9ui_scale_px(ctx, PROFILE_LIST_PADDING_Y);
    return padY * 2 + lh;
}

static void
profile_hotspot_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    self->bounds = bounds;

    profile_hotspot_state_t *st = (profile_hotspot_state_t*)self->state;
    if (!st) return;

    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : (ctx ? ctx->font : NULL);
    if (font && ctx && ctx->renderer) {
        profile_hotspot_updateTextures(st, ctx->renderer, font);
    }

    int padX = e9ui_scale_px(ctx, PROFILE_LIST_PADDING_X);
    int padY = e9ui_scale_px(ctx, PROFILE_LIST_PADDING_Y);
    int margin = e9ui_scale_px(ctx, 8);

    int sampleWidth = st->sampleTextWidth;
    int sampleHeight = st->sampleTextHeight;
    if (sampleHeight <= 0) sampleHeight = 16;

    int locW = st->locationTextWidth;
    int locH = st->locationTextHeight > 0 ? st->locationTextHeight : sampleHeight;

    int locationX = bounds.x + padX;
    int textY = bounds.y + padY;

    int sampleX = bounds.x + bounds.w - padX - sampleWidth;
    int inlineTextWidth = locW;
    if (st->sourceText[0]) {
        inlineTextWidth += margin + st->sourceTextWidth;
    }
    int minSampleX = locationX + inlineTextWidth + margin;
    if (sampleX < minSampleX) sampleX = minSampleX;

    int maxSampleX = bounds.x + bounds.w - padX - sampleWidth;
    if (sampleX > maxSampleX) sampleX = maxSampleX;

    int sourceX = locationX + locW + margin;
    int sourceW = sampleX - sourceX - margin;
    if (sourceW < 0) {
        sourceW = 0;
    }

    st->locationRect = (SDL_Rect){ locationX, textY, locW, locH };
    st->sourceRect = (SDL_Rect){ sourceX, textY, sourceW, locH };
    st->sampleRect   = (SDL_Rect){ sampleX,   textY, sampleWidth, sampleHeight };

}

static void
profile_hotspot_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx || !ctx->renderer) return;

    profile_hotspot_state_t *st = (profile_hotspot_state_t*)self->state;

    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    if (!font) return;

    SDL_Rect bg = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_SetRenderDrawColor(ctx->renderer, 18, 18, 24, 255);
    SDL_RenderFillRect(ctx->renderer, &bg);
    profile_hotspot_renderBar(self, ctx, st);

    profile_hotspot_updateTextures(st, ctx->renderer, font);

    if (st->locationTexture) {
        int visibleW = st->locationTextWidth;
        int maxW = st->sampleRect.x - st->locationRect.x - e9ui_scale_px(ctx, 8);
        if (visibleW > maxW) {
            visibleW = maxW;
        }
        if (visibleW > 0) {
            SDL_Rect src = { 0, 0, visibleW, st->locationTextHeight };
            SDL_Rect dst = { st->locationRect.x, st->locationRect.y, visibleW, st->locationTextHeight };
            SDL_RenderCopy(ctx->renderer, st->locationTexture, &src, &dst);
        }
    }

    if (st->sourceTexture && st->sourceRect.w > 0 && st->sourceRect.h > 0) {
        int visibleW = st->sourceTextWidth;
        if (visibleW > st->sourceRect.w) {
            visibleW = st->sourceRect.w;
        }
        SDL_Rect src = { 0, 0, visibleW, st->sourceTextHeight };
        SDL_Rect dst = { st->sourceRect.x, st->sourceRect.y, visibleW, st->sourceTextHeight };
        SDL_RenderCopy(ctx->renderer, st->sourceTexture, &src, &dst);
    }

    if (st->sampleTexture) {
        SDL_Rect samplesRect = { st->sampleRect.x, st->sampleRect.y, st->sampleTextWidth, st->sampleTextHeight };
        SDL_RenderCopy(ctx->renderer, st->sampleTexture, NULL, &samplesRect);
    }
}

static void
profile_hotspot_renderBar(e9ui_component_t *self, e9ui_context_t *ctx, const profile_hotspot_state_t *st)
{
    unsigned long long value;
    int barWidth;

    if (!self || !ctx || !ctx->renderer || !st || st->total == 0) {
        return;
    }

    value = profile_list_showSamples() ? st->samples : st->cycles;
    if (value == 0) {
        return;
    }

    if (value >= st->total) {
        barWidth = self->bounds.w;
    } else {
        long double fraction = (long double)value / (long double)st->total;
        barWidth = (int)(fraction * (long double)self->bounds.w);
        if (barWidth < 1) {
            barWidth = 1;
        }
        if (barWidth > self->bounds.w) {
            barWidth = self->bounds.w;
        }
    }

    SDL_Rect bar = { self->bounds.x, self->bounds.y, barWidth, self->bounds.h };
    SDL_BlendMode prevBlend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(ctx->renderer, &prevBlend);
    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ctx->renderer, 52, 132, 190, 70);
    SDL_RenderFillRect(ctx->renderer, &bar);
    SDL_SetRenderDrawBlendMode(ctx->renderer, prevBlend);
}

static void
profile_hotspot_onClick(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouse_ev)
{
    (void)mouse_ev;
    if (!self || !self->state) {
        return;
    }
    profile_hotspot_linkClicked(ctx, self->state);
}

static void
profile_hotspot_linkClicked(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    profile_hotspot_state_t *st = (profile_hotspot_state_t*)user;
    if (!st) return;

    ui_openSourceLocation(st->file, st->line, (uint32_t)st->pc & 0x00ffffffu);
}

static void
profile_hotspot_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) return;
    profile_hotspot_state_t *st = (profile_hotspot_state_t*)self->state;
    profile_hotspot_destroyTextures(st);
}

e9ui_component_t *
profile_hotspot_make(unsigned int pc,
                     unsigned long long samples,
                     unsigned long long cycles,
                     unsigned long long total,
                     int showSamples,
                     const char *location,
                     const char *source,
                     const char *file,
                     int line)
{
    e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    if (!comp) return NULL;

    profile_hotspot_state_t *st = (profile_hotspot_state_t*)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(comp);
        return NULL;
    }

    st->pc = pc;
    st->samples = samples;
    st->cycles = cycles;
    st->total = total;

    if (showSamples) {
        snprintf(st->sampleText, sizeof(st->sampleText), "%llu samples", samples);
    } else {
        snprintf(st->sampleText, sizeof(st->sampleText), "%llu cycles", cycles);
    }

    if (location && *location) {
        strutil_strlcpy(st->locationText, sizeof(st->locationText), location);
    } else {
        snprintf(st->locationText, sizeof(st->locationText), "PC: 0x%08X", pc);
    }
    if (source && *source) {
        strutil_strlcpy(st->sourceText, sizeof(st->sourceText), source);
    }
    if (file && *file) {
        strutil_strlcpy(st->file, sizeof(st->file), file);
        st->line = line;
    }

    comp->name = "profile_hotspot";
    comp->state = st;
    comp->preferredHeight = profile_hotspot_preferredHeight;
    comp->layout = profile_hotspot_layout;
    comp->render = profile_hotspot_render;
    comp->onClick = profile_hotspot_onClick;
    comp->dtor = profile_hotspot_dtor;

    return comp;
}
