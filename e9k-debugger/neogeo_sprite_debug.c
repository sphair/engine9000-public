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
#define NG_SPRITES_PER_LINE_MAX 96
#define NG_MAX_SPRITES 382
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 224

typedef struct neogeo_sprite_debug_state {
    e9ui_window_state_t windowState;
    SDL_Window *window;
    SDL_Renderer *renderer;
    e9ui_component_t *overlayBodyHost;
    SDL_Texture *texture;
    uint32_t *pixels;
    size_t pixels_cap;
    int tex_w;
    int tex_h;
    int logicalW;
    int logicalH;
    uint32_t hist_grad[DBG_HIST_WIDTH];
    int hist_grad_ready;
    uint32_t last_hash;
    int cached_valid;
    int hist_x0_anchor;
    e9k_debug_sprite_state_t lastState;
    int hasLastState;
} neogeo_sprite_debug_state_t;

typedef struct neogeo_sprite_debug_overlay_body_state {
    int unused;
} neogeo_sprite_debug_overlay_body_state_t;

static neogeo_sprite_debug_state_t s_dbg = {
    .windowState.winX = E9UI_WINDOW_COORD_UNSET,
    .windowState.winY = E9UI_WINDOW_COORD_UNSET,
    .windowState.openMinWidthPx = 420,
    .windowState.openMinHeightPx = 360,
    .windowState.openCenterWhenNoSaved = 1,
};
static int neogeo_sprite_debug_histogramEnabled = 1;

static e9ui_window_backend_t
neogeo_sprite_debug_windowBackend(void)
{
    return e9ui_window_backend_overlay;
}

int
neogeo_sprite_debug_handleKeydown(const SDL_KeyboardEvent *kev)
{
    if (!kev || !s_dbg.windowState.open) {
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
neogeo_sprite_debug_presentTexture(int base_w, int base_h, int presentFrame)
{
    SDL_Rect viewport = { 0, 0, 0, 0 };
    SDL_RenderGetViewport(s_dbg.renderer, &viewport);
    int out_w = viewport.w;
    int out_h = viewport.h;
    if (out_w <= 0 || out_h <= 0) {
        SDL_GetRendererOutputSize(s_dbg.renderer, &out_w, &out_h);
        viewport.x = 0;
        viewport.y = 0;
        viewport.w = out_w;
        viewport.h = out_h;
    }
    if (out_w <= 0 || out_h <= 0) {
        if (s_dbg.window) {
            SDL_GetWindowSize(s_dbg.window, &out_w, &out_h);
            viewport.x = 0;
            viewport.y = 0;
            viewport.w = out_w;
            viewport.h = out_h;
        }
    }
    float scale_x = out_w > 0 ? (float)out_w / (float)base_w : 1.0f;
    float scale_y = out_h > 0 ? (float)out_h / (float)base_h : 1.0f;
    float scale = scale_x < scale_y ? scale_x : scale_y;
    if (scale <= 0.0f) {
        scale = 1.0f;
    }
    int dst_w = (int)((float)base_w * scale + 0.5f);
    int dst_h = (int)((float)base_h * scale + 0.5f);
    int dst_x = (out_w - dst_w) / 2;
    int dst_y = (out_h - dst_h) / 2;
    SDL_Rect dst = { dst_x, dst_y, dst_w, dst_h };
    SDL_SetRenderDrawColor(s_dbg.renderer, 0, 0, 0, 255);
    SDL_Rect clearRect = { 0, 0, out_w, out_h };
    SDL_RenderFillRect(s_dbg.renderer, &clearRect);
    SDL_Rect src = { 0, 0, base_w, base_h };
    SDL_RenderCopy(s_dbg.renderer, s_dbg.texture, &src, &dst);
    if (neogeo_sprite_debug_histogramEnabled) {
        int hist_x = base_w + DBG_GAP;
        int hist_w = DBG_HIST_WIDTH;
        SDL_Rect hist_src = { hist_x, 0, hist_w, base_h };
        SDL_Rect hist_dst = { dst_x + dst_w + (int)((float)DBG_GAP * scale + 0.5f),
                              dst_y,
                              (int)((float)hist_w * scale + 0.5f),
                              dst_h };
        SDL_RenderCopy(s_dbg.renderer, s_dbg.texture, &hist_src, &hist_dst);
    }
    if (presentFrame) {
        SDL_RenderPresent(s_dbg.renderer);
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
neogeo_sprite_debug_renderFrameInternal(const e9k_debug_sprite_state_t *st, int presentFrame);

static void
neogeo_sprite_debug_overlayBodyRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) {
        return;
    }
    if (!s_dbg.windowState.open || !s_dbg.hasLastState) {
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
    s_dbg.window = ctx->window;
    s_dbg.renderer = ctx->renderer;
    neogeo_sprite_debug_renderFrameInternal(&s_dbg.lastState, 0);
    SDL_RenderSetViewport(ctx->renderer, &prevViewport);
    if (hadClip) {
        SDL_RenderSetClipRect(ctx->renderer, &prevClip);
    }
}

static void
neogeo_sprite_debug_overlayBodyDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    neogeo_sprite_debug_overlay_body_state_t *st = (neogeo_sprite_debug_overlay_body_state_t *)self->state;
    alloc_free(st);
    self->state = NULL;
}

static e9ui_component_t *
neogeo_sprite_debug_makeOverlayBodyHost(void)
{
    e9ui_component_t *host = (e9ui_component_t *)alloc_calloc(1, sizeof(*host));
    if (!host) {
        return NULL;
    }
    neogeo_sprite_debug_overlay_body_state_t *st =
        (neogeo_sprite_debug_overlay_body_state_t *)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(host);
        return NULL;
    }
    host->name = "neogeo_sprite_debug_overlay_body";
    host->state = st;
    host->preferredHeight = neogeo_sprite_debug_overlayBodyPreferredHeight;
    host->layout = neogeo_sprite_debug_overlayBodyLayout;
    host->render = neogeo_sprite_debug_overlayBodyRender;
    host->dtor = neogeo_sprite_debug_overlayBodyDtor;
    return host;
}

static void
neogeo_sprite_debug_overlayWindowCloseRequested(e9ui_window_t *window, void *user)
{
    (void)window;
    (void)user;
    neogeo_sprite_debug_toggle();
}

static const uint8_t g_lut_hshrink[0x10][0x10] = {
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

static uint32_t
sprite_dbgColor(Uint8 r, Uint8 g, Uint8 b)
{
    return (uint32_t)(0xFF000000u | (r << 16) | (g << 8) | b);
}

static uint32_t
sprite_dbgHueColor(float h)
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
    return sprite_dbgColor((Uint8)(rr * 255.0f), (Uint8)(gg * 255.0f), (Uint8)(bb * 255.0f));
}

static unsigned
sprite_dbgCountShrinkWidth(unsigned hval)
{
    unsigned h = (hval & 0x0F);
    unsigned w = 0;
    for (unsigned p = 0; p < 16; ++p) {
        w += (unsigned)g_lut_hshrink[h][p];
    }
    return w;
}

static uint32_t
sprite_dbgHashSprites(const uint16_t *scb2, const uint16_t *scb3, const uint16_t *scb4)
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
sprite_dbgDrawDigits3x5(uint32_t *pixels, int pitch, int ext_w, int ext_h,
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
    const int glyph_w = 3;
    const int glyph_h = 5;
    const int spacing = 1;
    int cx = x;
    int cy = y;
    if (!pixels || !buf) {
        return;
    }
    for (int i = 0; buf[i]; ++i) {
        char ch = buf[i];
        if (ch < '0' || ch > '9') {
            cx += glyph_w + spacing;
            continue;
        }
        int d = ch - '0';
        for (int ry = 0; ry < glyph_h; ++ry) {
            uint8_t rowbits = digits[d][ry];
            for (int rx = 0; rx < glyph_w; ++rx) {
                if (rowbits & (uint8_t)(1u << (glyph_w - 1 - rx))) {
                    int px = cx + rx;
                    int py = cy + ry;
                    if (px >= 0 && px < ext_w && py >= 0 && py < ext_h) {
                        pixels[py * pitch + px] = color;
                    }
                }
            }
        }
        cx += glyph_w + spacing;
    }
}

static void
sprite_dbgFillRectAbs(uint32_t *pixels, int pitch, int ext_w, int ext_h,
                      int x, int y, int w, int h, uint32_t color)
{
    int x0 = x;
    int y0 = y;
    int x1 = x0 + w;
    int y1 = y0 + h;
    if (x1 <= 0 || y1 <= 0) {
        return;
    }
    if (x0 >= ext_w || y0 >= ext_h) {
        return;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > ext_w) x1 = ext_w;
    if (y1 > ext_h) y1 = ext_h;
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
sprite_dbgDrawRectAbs(uint32_t *pixels, int pitch, int ext_w, int ext_h,
                      int x, int y, int w, int h, uint32_t color)
{
    sprite_dbgFillRectAbs(pixels, pitch, ext_w, ext_h, x, y, w, 1, color);
    sprite_dbgFillRectAbs(pixels, pitch, ext_w, ext_h, x, y + h - 1, w, 1, color);
    sprite_dbgFillRectAbs(pixels, pitch, ext_w, ext_h, x, y, 1, h, color);
    sprite_dbgFillRectAbs(pixels, pitch, ext_w, ext_h, x + w - 1, y, 1, h, color);
}

static void
sprite_dbgFillRectCoord(uint32_t *pixels, int pitch, int ext_w, int ext_h,
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
    sprite_dbgFillRectAbs(pixels, pitch, ext_w, ext_h, x0, y0, w, h, color);
}

void
neogeo_sprite_debug_toggle(void)
{
    if (!s_dbg.windowState.open) {
        s_dbg.windowState.windowHost = e9ui_windowCreate(neogeo_sprite_debug_windowBackend());
        if (!s_dbg.windowState.windowHost) {
            return;
        }
        int lw = NG_COORD_W;
        int lh = NG_COORD_H;
        if (neogeo_sprite_debug_histogramEnabled) {
            lw += DBG_GAP + DBG_HIST_WIDTH;
        }
        s_dbg.logicalW = lw;
        s_dbg.logicalH = lh;
        s_dbg.overlayBodyHost = neogeo_sprite_debug_makeOverlayBodyHost();
        e9ui_rect_t rect = e9ui_windowResolveStateOpenRect(&e9ui->ctx,
                                                           neogeo_sprite_debug_windowDefaultRect(&e9ui->ctx),
                                                           &s_dbg.windowState);
        e9ui_windowOpen(s_dbg.windowState.windowHost,
                                     "Sprite Debug",
                                     rect,
                                     s_dbg.overlayBodyHost,
                                     neogeo_sprite_debug_overlayWindowCloseRequested,
                                     NULL,
			             &e9ui->ctx);
        s_dbg.window = e9ui->ctx.window;
        s_dbg.renderer = e9ui->ctx.renderer;
        s_dbg.hist_x0_anchor = -1;
        s_dbg.windowState.open = 1;
        aux_window_register(&neogeo_sprite_debug_auxWindowOps, &s_dbg);
    } else {
        aux_window_unregister(&neogeo_sprite_debug_auxWindowOps, &s_dbg);
        if (s_dbg.texture) {
            SDL_DestroyTexture(s_dbg.texture);
            s_dbg.texture = NULL;
        }
        if (s_dbg.pixels) {
            free(s_dbg.pixels);
            s_dbg.pixels = NULL;
            s_dbg.pixels_cap = 0;
        }
        s_dbg.tex_w = 0;
        s_dbg.tex_h = 0;
        s_dbg.logicalW = 0;
        s_dbg.logicalH = 0;
        s_dbg.hist_grad_ready = 0;
        s_dbg.cached_valid = 0;
        s_dbg.last_hash = 0;
        if (s_dbg.windowState.windowHost) {
            e9ui_windowDestroy(s_dbg.windowState.windowHost);
            s_dbg.windowState.windowHost = NULL;
        }
        s_dbg.overlayBodyHost = NULL;
        s_dbg.hist_x0_anchor = -1;
        s_dbg.windowState.open = 0;
        s_dbg.hasLastState = 0;
        s_dbg.window = NULL;
        s_dbg.renderer = NULL;
    }
}

int
neogeo_sprite_debug_is_open(void)
{
    return s_dbg.windowState.open ? 1 : 0;
}

void
neogeo_sprite_debug_setMainWindowFocused(int focused)
{
    (void)focused;
}

void
neogeo_sprite_debug_renderFrameInternal(const e9k_debug_sprite_state_t *st, int presentFrame)
{
    if (!s_dbg.windowState.open || !s_dbg.renderer) {
        return;
    }
    if (!st || !st->vram) {
        return;
    }
    const uint16_t *vram = st->vram;
    if (st->vram_words <= (0x8400u + NG_MAX_SPRITES)) {
        return;
    }
    const uint16_t *scb2 = vram + 0x8000;
    const uint16_t *scb3 = vram + 0x8200;
    const uint16_t *scb4 = vram + 0x8400;

    const int base_w = NG_COORD_W;
    const int base_h = NG_COORD_H;
    int ext_w = base_w;
    const int ext_h = base_h;
    if (neogeo_sprite_debug_histogramEnabled) {
        ext_w += DBG_GAP + DBG_HIST_WIDTH;
    }
    if (s_dbg.tex_w != ext_w || s_dbg.tex_h != ext_h) {
        SDL_DestroyTexture(s_dbg.texture);
        s_dbg.texture = SDL_CreateTexture(s_dbg.renderer, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STREAMING, ext_w, ext_h);
        s_dbg.tex_w = ext_w;
        s_dbg.tex_h = ext_h;
    }
    if (!s_dbg.texture) {
        return;
    }
    uint32_t hash = sprite_dbgHashSprites(scb2, scb3, scb4);
    if (s_dbg.cached_valid && hash == s_dbg.last_hash && s_dbg.texture) {
        neogeo_sprite_debug_presentTexture(base_w, base_h, presentFrame);
        return;
    }
    size_t needed = (size_t)ext_w * (size_t)ext_h;
    if (needed > s_dbg.pixels_cap) {
        uint32_t *next = (uint32_t *)realloc(s_dbg.pixels, needed * sizeof(uint32_t));
        if (!next) {
            return;
        }
        s_dbg.pixels = next;
        s_dbg.pixels_cap = needed;
    }
    uint32_t *pixels = s_dbg.pixels;
    const uint32_t col_bg = sprite_dbgColor(68, 68, 68);
    const uint32_t col_black = sprite_dbgColor(0, 0, 0);
    const uint32_t col_white = sprite_dbgColor(255, 255, 255);
    const uint32_t col_green = sprite_dbgColor(0, 255, 0);
    const uint32_t col_hist_bg = sprite_dbgColor(34, 34, 34);
    const uint32_t col_bounds = sprite_dbgColor(120, 120, 120);
    for (size_t i = 0; i < needed; ++i) {
        pixels[i] = col_black;
    }
    sprite_dbgFillRectAbs(pixels, ext_w, ext_w, ext_h, 0, 0, base_w, base_h, col_bg);
    sprite_dbgDrawRectAbs(pixels, ext_w, ext_w, ext_h,
                          NG_COORD_OFFSET_X, NG_COORD_OFFSET_Y,
                          NG_COORD_SIZE, NG_COORD_SIZE, col_bounds);

    SDL_Rect screen_r = { NG_VISIBLE_X0, NG_VISIBLE_Y0, NG_VISIBLE_W, NG_VISIBLE_H };
    sprite_dbgFillRectCoord(pixels, ext_w, ext_w, ext_h,
                            screen_r.x, screen_r.y, screen_r.w, screen_r.h, col_black);
    {
        int bx0 = screen_r.x - 1 + NG_COORD_OFFSET_X;
        int by0 = screen_r.y - 1 + NG_COORD_OFFSET_Y;
        int bw = screen_r.w + 2;
        int bh = screen_r.h + 2;
        sprite_dbgDrawRectAbs(pixels, ext_w, ext_w, ext_h, bx0, by0, bw, bh, col_white);
    }

    unsigned viscount_line[NG_VISIBLE_H];
    for (int i = 0; i < NG_VISIBLE_H; ++i) {
        viscount_line[i] = 0;
    }
    int screen_h = (st->screen_h > 0) ? st->screen_h : SCREEN_HEIGHT;
    int active_total = 0;
    for (unsigned i = 1; i < (unsigned)NG_MAX_SPRITES; ) {
        if (scb3[i] & 0x40u) {
            ++i;
            continue;
        }
        uint16_t scb3b = scb3[i];
        unsigned bh = (unsigned)(scb3b & 0x3f);
        unsigned by = (unsigned)((scb3b >> 7) & 0x01ff);
        unsigned len = 1;
        while ((i + len) < (unsigned)NG_MAX_SPRITES && (scb3[i + len] & 0x40u)) {
            ++len;
        }
        if (bh != 0 && by != (unsigned)screen_h) {
            active_total += (int)len;
        }
        i += len;
    }
    unsigned sprlimit = st->sprlimit ? st->sprlimit : NG_SPRITES_PER_LINE_MAX;
    if (sprlimit == 0) {
        sprlimit = NG_SPRITES_PER_LINE_MAX;
    }
    int maxcnt = 0;
    for (int line = 0; line < NG_COORD_SIZE; ++line) {
        unsigned sprcount = 0;
        unsigned viscount = 0;

        unsigned xpos = 0;
        unsigned ypos = 0;
        unsigned sprsize = 0;
        unsigned hshrink = 0x0F;

        for (unsigned i = 1; i < (unsigned)NG_MAX_SPRITES; ++i) {
            uint16_t scb3w = scb3[i];
            uint16_t scb2w = scb2[i];
            uint16_t scb4w = scb4[i];

            if (scb3w & 0x40) {
                xpos = (unsigned)((xpos + (hshrink + 1)) & NG_WRAP_MASK);
            } else {
                xpos = (unsigned)((scb4w >> 7) & NG_WRAP_MASK);
                ypos = (unsigned)((scb3w >> 7) & NG_WRAP_MASK);
                sprsize = (unsigned)(scb3w & 0x3F);
            }
            hshrink = (unsigned)((scb2w >> 8) & 0x0F);

            int vline = line + NG_LINE_OFFSET;
            unsigned srow = (unsigned)(((vline - (int)(0x200 - (int)ypos))) & NG_WRAP_MASK);
            if ((sprsize == 0) || (srow >= (sprsize << 4))) {
                continue;
            }

            if (sprcount == NG_SPRITES_PER_LINE_MAX) {
                break;
            }
            sprcount++;

            int w = (int)sprite_dbgCountShrinkWidth(hshrink);
            if (w <= 0) {
                continue;
            }
            int x0 = (int)(xpos & NG_WRAP_MASK);
            int xsum = x0 + w;
            int visible = (x0 < NG_VISIBLE_W) || (xsum > NG_COORD_SIZE);
            if (visible) {
                viscount++;
            }

            sprite_dbgFillRectCoord(pixels, ext_w, ext_w, ext_h, x0, line, 1, 1, col_green);
            sprite_dbgFillRectCoord(pixels, ext_w, ext_w, ext_h, x0 + w - 1, line, 1, 1, col_green);
            sprite_dbgFillRectCoord(pixels, ext_w, ext_w, ext_h, x0 - NG_COORD_SIZE, line, 1, 1, col_green);
            sprite_dbgFillRectCoord(pixels, ext_w, ext_w, ext_h, x0 - NG_COORD_SIZE + w - 1, line, 1, 1, col_green);
            sprite_dbgFillRectCoord(pixels, ext_w, ext_w, ext_h, x0, line - NG_COORD_SIZE, 1, 1, col_green);
            sprite_dbgFillRectCoord(pixels, ext_w, ext_w, ext_h, x0 + w - 1, line - NG_COORD_SIZE, 1, 1, col_green);
            sprite_dbgFillRectCoord(pixels, ext_w, ext_w, ext_h, x0 - NG_COORD_SIZE, line - NG_COORD_SIZE, 1, 1, col_green);
            sprite_dbgFillRectCoord(pixels, ext_w, ext_w, ext_h, x0 - NG_COORD_SIZE + w - 1, line - NG_COORD_SIZE, 1, 1, col_green);

            unsigned total_h = (unsigned)(sprsize << 4);
            if (srow == 0u || (srow + 1u) == total_h) {
            sprite_dbgFillRectCoord(pixels, ext_w, ext_w, ext_h, x0, line, w, 1, col_green);
            sprite_dbgFillRectCoord(pixels, ext_w, ext_w, ext_h, x0 - NG_COORD_SIZE, line, w, 1, col_green);
            sprite_dbgFillRectCoord(pixels, ext_w, ext_w, ext_h, x0, line - NG_COORD_SIZE, w, 1, col_green);
            sprite_dbgFillRectCoord(pixels, ext_w, ext_w, ext_h, x0 - NG_COORD_SIZE, line - NG_COORD_SIZE, w, 1, col_green);
        }
        }

        if (line >= NG_VISIBLE_Y0 && line < (NG_VISIBLE_Y0 + NG_VISIBLE_H)) {
            viscount_line[line - NG_VISIBLE_Y0] = viscount;
        }
        if ((int)sprcount > maxcnt) {
            maxcnt = (int)sprcount;
        }
    }

    if (neogeo_sprite_debug_histogramEnabled) {
        int hist_x0 = NG_COORD_OFFSET_X + NG_COORD_SIZE + DBG_GAP;
        int hist_w = DBG_HIST_WIDTH;
        if (hist_w < 1) {
            hist_w = 1;
        }

        sprite_dbgFillRectAbs(pixels, ext_w, ext_w, ext_h,
                              hist_x0, NG_VISIBLE_Y0 + NG_COORD_OFFSET_Y,
                              hist_w, NG_VISIBLE_H, col_hist_bg);
        if (!s_dbg.hist_grad_ready) {
            int denomx = (DBG_HIST_WIDTH > 1) ? (DBG_HIST_WIDTH - 1) : 1;
            for (int dx = 0; dx < DBG_HIST_WIDTH; ++dx) {
                float t = (float)dx / (float)denomx;
                float h = (1.0f / 3.0f) * (1.0f - t);
                s_dbg.hist_grad[dx] = sprite_dbgHueColor(h);
            }
            s_dbg.hist_grad_ready = 1;
        }
        for (int line = 0; line < NG_VISIBLE_H; ++line) {
            int viscount = (int)viscount_line[line];
            int bar_len = (int)((viscount * (unsigned)hist_w) / NG_SPRITES_PER_LINE_MAX);
            if (bar_len > hist_w) {
                bar_len = hist_w;
            }
            if (bar_len > 0) {
                int y = NG_VISIBLE_Y0 + line + NG_COORD_OFFSET_Y;
                uint32_t *row = pixels + y * ext_w + hist_x0;
                for (int dx = 0; dx < bar_len; ++dx) {
                    row[dx] = s_dbg.hist_grad[dx];
                }
            }
        }

        {
            int bx0 = hist_x0 - 1;
            int by0 = NG_VISIBLE_Y0 - 1 + NG_COORD_OFFSET_Y;
            int bw = hist_w + 2;
            int bh = NG_VISIBLE_H + 2;
            sprite_dbgDrawRectAbs(pixels, ext_w, ext_w, ext_h, bx0, by0, bw, bh, col_white);
        }

        {
            const int glyph_w = 3;
            const int glyph_h = 5;
            const int spacing = 1;
            const int pad = 4;
            const int stats_y = NG_VISIBLE_Y0 + NG_COORD_OFFSET_Y + NG_VISIBLE_H + 6;
            if (stats_y + glyph_h + pad * 2 <= ext_h) {
                char buf[16];
                int n = snprintf(buf, sizeof(buf), "%d", maxcnt);
                if (n < 1) {
                    n = 1;
                }
                if (n > (int)(sizeof(buf) - 1)) {
                    n = (int)(sizeof(buf) - 1);
                    buf[n] = '\0';
                }
                int text_w = n * glyph_w + (n - 1) * spacing;
                int badge_w = text_w + pad * 2;
                int badge_h = glyph_h + pad * 2;
                int bx = hist_x0 + hist_w - badge_w;
                int by = stats_y;
                uint32_t badge_col = (maxcnt > (int)sprlimit) ? sprite_dbgColor(200, 0, 0) : sprite_dbgColor(64, 64, 64);
                sprite_dbgFillRectAbs(pixels, ext_w, ext_w, ext_h, bx, by, badge_w, badge_h, badge_col);
                sprite_dbgDrawDigits3x5(pixels, ext_w, ext_w, ext_h, bx + pad, by + pad, buf, col_white);

                n = snprintf(buf, sizeof(buf), "%d", active_total);
                if (n < 1) {
                    n = 1;
                }
                if (n > (int)(sizeof(buf) - 1)) {
                    n = (int)(sizeof(buf) - 1);
                    buf[n] = '\0';
                }
                text_w = n * glyph_w + (n - 1) * spacing;
                badge_w = text_w + pad * 2;
                badge_h = glyph_h + pad * 2;
                bx = hist_x0;
                by = stats_y;
                badge_col = (active_total > (int)(NG_MAX_SPRITES - 1)) ? sprite_dbgColor(200, 0, 0) : sprite_dbgColor(64, 64, 64);
                sprite_dbgFillRectAbs(pixels, ext_w, ext_w, ext_h, bx, by, badge_w, badge_h, badge_col);
                sprite_dbgDrawDigits3x5(pixels, ext_w, ext_w, ext_h, bx + pad, by + pad, buf, col_white);
            }
        }
    }

    SDL_UpdateTexture(s_dbg.texture, NULL, pixels, ext_w * (int)sizeof(uint32_t));
    neogeo_sprite_debug_presentTexture(base_w, base_h, presentFrame);
    s_dbg.cached_valid = 1;
    s_dbg.last_hash = hash;
}

void
neogeo_sprite_debug_render(const e9k_debug_sprite_state_t *st)
{
    if (st) {
        s_dbg.lastState = *st;
        s_dbg.hasLastState = 1;
    }
    if (!s_dbg.windowState.open) {
        return;
    }
    if (e9ui_windowCaptureStateRectChanged(&s_dbg.windowState,
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
    e9ui_windowPersistStateRect(file,
                                "comp.sprite_debug",
                                &s_dbg.windowState,
                                &e9ui->ctx);
}

int
neogeo_sprite_debug_loadConfigProperty(const char *prop, const char *value)
{
    if (!prop || !value) {
        return 0;
    }
    int intValue = 0;
    if (strcmp(prop, "win_x") == 0) {
        if (!neogeo_sprite_debug_parseInt(value, &intValue)) {
            return 0;
        }
        s_dbg.windowState.winX = intValue;
    } else if (strcmp(prop, "win_y") == 0) {
        if (!neogeo_sprite_debug_parseInt(value, &intValue)) {
            return 0;
        }
        s_dbg.windowState.winY = intValue;
    } else if (strcmp(prop, "win_w") == 0) {
        if (!neogeo_sprite_debug_parseInt(value, &intValue)) {
            return 0;
        }
        s_dbg.windowState.winW = intValue;
    } else if (strcmp(prop, "win_h") == 0) {
        if (!neogeo_sprite_debug_parseInt(value, &intValue)) {
            return 0;
        }
        s_dbg.windowState.winH = intValue;
    } else {
        return 0;
    }
    s_dbg.windowState.winHasSaved =
        e9ui_windowHasSavedPosition(s_dbg.windowState.winX, s_dbg.windowState.winY);
    return 1;
}
