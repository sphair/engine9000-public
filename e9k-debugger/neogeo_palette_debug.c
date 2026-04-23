/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "aux_window.h"
#include "neogeo_palette_debug.h"
#include "alloc.h"
#include "config.h"
#include "e9ui.h"
#include "libretro_host.h"

#define NEOGEO_PALETTE_DEBUG_BANK_COUNT 2
#define NEOGEO_PALETTE_DEBUG_PALETTES_PER_BANK 256
#define NEOGEO_PALETTE_DEBUG_COLORS_PER_PALETTE 16
#define NEOGEO_PALETTE_DEBUG_GRID_COLS 16
#define NEOGEO_PALETTE_DEBUG_GRID_ROWS 16
#define NEOGEO_PALETTE_DEBUG_SWATCH_COLS 4
#define NEOGEO_PALETTE_DEBUG_SWATCH_ROWS 4
#define NEOGEO_PALETTE_DEBUG_SWATCH_SIZE 8
#define NEOGEO_PALETTE_DEBUG_SWATCH_GAP 1
#define NEOGEO_PALETTE_DEBUG_PALETTE_BORDER 1
#define NEOGEO_PALETTE_DEBUG_PALETTE_GAP 3
#define NEOGEO_PALETTE_DEBUG_PANEL_PAD 10
#define NEOGEO_PALETTE_DEBUG_PANEL_HEADER_H 18
#define NEOGEO_PALETTE_DEBUG_PANEL_GAP 18
#define NEOGEO_PALETTE_DEBUG_OUTER_PAD 10

typedef struct neogeo_palette_debug_state
{
    e9ui_window_state_t windowState;
    SDL_Window *window;
    SDL_Renderer *renderer;
    e9ui_component_t *root;
    e9ui_component_t *overlayBodyHost;
    SDL_Texture *texture;
    uint32_t *pixels;
    size_t pixelsCap;
    int texW;
    int texH;
    int logicalW;
    int logicalH;
    uint32_t lastHash;
    int cachedValid;
} neogeo_palette_debug_state_t;

typedef struct neogeo_palette_debug_overlay_body_state
{
    int unused;
} neogeo_palette_debug_overlay_body_state_t;

static neogeo_palette_debug_state_t neogeo_palette_debugState = {
    .windowState.winX = E9UI_WINDOW_COORD_UNSET,
    .windowState.winY = E9UI_WINDOW_COORD_UNSET,
    .windowState.openMinWidthPx = 420,
    .windowState.openMinHeightPx = 360,
    .windowState.openCenterWhenNoSaved = 1,
};

static const aux_window_ops_t neogeo_palette_debug_auxWindowOps = {
    .setFocus = neogeo_palette_debug_setMainWindowFocused,
    .render = neogeo_palette_debug_render,
};

static e9ui_window_backend_t
neogeo_palette_debug_windowBackend(void)
{
    return e9ui_window_backend_overlay;
}

static int
neogeo_palette_debug_parseInt(const char *value, int *out)
{
    char *end = NULL;
    long parsed = 0;

    if (!value || !out) {
        return 0;
    }

    parsed = strtol(value, &end, 10);
    if (!end || end == value) {
        return 0;
    }
    if (parsed < INT_MIN || parsed > INT_MAX) {
        return 0;
    }
    *out = (int)parsed;
    return 1;
}

static uint32_t
neogeo_palette_debug_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void
neogeo_palette_debug_fillRect(uint32_t *pixels,
                              int pitch,
                              int width,
                              int height,
                              int x,
                              int y,
                              int w,
                              int h,
                              uint32_t color)
{
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;

    if (!pixels || width <= 0 || height <= 0 || w <= 0 || h <= 0) {
        return;
    }

    x0 = x < 0 ? 0 : x;
    y0 = y < 0 ? 0 : y;
    x1 = x + w;
    y1 = y + h;
    if (x1 > width) {
        x1 = width;
    }
    if (y1 > height) {
        y1 = height;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    for (int yy = y0; yy < y1; ++yy) {
        uint32_t *row = pixels + (size_t)yy * (size_t)pitch + (size_t)x0;
        for (int xx = x0; xx < x1; ++xx) {
            row[xx - x0] = color;
        }
    }
}

static void
neogeo_palette_debug_drawRect(uint32_t *pixels,
                              int pitch,
                              int width,
                              int height,
                              int x,
                              int y,
                              int w,
                              int h,
                              uint32_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }

    neogeo_palette_debug_fillRect(pixels, pitch, width, height, x, y, w, 1, color);
    neogeo_palette_debug_fillRect(pixels, pitch, width, height, x, y + h - 1, w, 1, color);
    neogeo_palette_debug_fillRect(pixels, pitch, width, height, x, y, 1, h, color);
    neogeo_palette_debug_fillRect(pixels, pitch, width, height, x + w - 1, y, 1, h, color);
}

static void
neogeo_palette_debug_drawDigit(uint32_t *pixels,
                               int pitch,
                               int width,
                               int height,
                               int x,
                               int y,
                               char digit,
                               int scale,
                               uint32_t color)
{
    static const uint8_t glyphs[10][5] = {
        {0x7, 0x5, 0x5, 0x5, 0x7},
        {0x2, 0x6, 0x2, 0x2, 0x7},
        {0x7, 0x1, 0x7, 0x4, 0x7},
        {0x7, 0x1, 0x7, 0x1, 0x7},
        {0x5, 0x5, 0x7, 0x1, 0x1},
        {0x7, 0x4, 0x7, 0x1, 0x7},
        {0x7, 0x4, 0x7, 0x5, 0x7},
        {0x7, 0x1, 0x2, 0x2, 0x2},
        {0x7, 0x5, 0x7, 0x5, 0x7},
        {0x7, 0x5, 0x7, 0x1, 0x7}
    };
    unsigned index = 0;

    if (digit < '0' || digit > '9' || scale <= 0) {
        return;
    }

    index = (unsigned)(digit - '0');
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 3; ++col) {
            if ((glyphs[index][row] & (1u << (2 - col))) == 0u) {
                continue;
            }
            neogeo_palette_debug_fillRect(pixels,
                                          pitch,
                                          width,
                                          height,
                                          x + col * scale,
                                          y + row * scale,
                                          scale,
                                          scale,
                                          color);
        }
    }
}

static void
neogeo_palette_debug_drawDigits(uint32_t *pixels,
                                int pitch,
                                int width,
                                int height,
                                int x,
                                int y,
                                unsigned value,
                                int scale,
                                uint32_t color)
{
    char text[16];
    int len = 0;

    snprintf(text, sizeof(text), "%u", value);
    len = (int)strlen(text);
    for (int i = 0; i < len; ++i) {
        neogeo_palette_debug_drawDigit(pixels,
                                       pitch,
                                       width,
                                       height,
                                       x + i * (3 * scale + scale),
                                       y,
                                       text[i],
                                       scale,
                                       color);
    }
}

static uint32_t
neogeo_palette_debug_hashPaletteState(const e9k_debug_palette_state_t *paletteState)
{
    uint32_t hash = 2166136261u;
    size_t totalColors = 0;

    if (!paletteState || !paletteState->colors) {
        return 0u;
    }

    totalColors = paletteState->color_count;
    for (size_t i = 0; i < totalColors; ++i) {
        hash ^= paletteState->colors[i];
        hash *= 16777619u;
    }
    hash ^= paletteState->active_bank;
    hash *= 16777619u;
    return hash;
}

static void
neogeo_palette_debug_presentTexture(int baseW, int baseH)
{
    SDL_Rect viewport = {0, 0, 0, 0};
    SDL_Rect clearRect = {0, 0, 0, 0};
    SDL_Rect dst = {0, 0, 0, 0};
    SDL_Rect src = {0, 0, 0, 0};
    int outW = 0;
    int outH = 0;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float scale = 1.0f;

    SDL_RenderGetViewport(neogeo_palette_debugState.renderer, &viewport);
    outW = viewport.w;
    outH = viewport.h;
    if (outW <= 0 || outH <= 0) {
        SDL_GetRendererOutputSize(neogeo_palette_debugState.renderer, &outW, &outH);
    }
    if (outW <= 0 || outH <= 0) {
        return;
    }

    scaleX = (float)outW / (float)baseW;
    scaleY = (float)outH / (float)baseH;
    scale = scaleX < scaleY ? scaleX : scaleY;
    if (scale <= 0.0f) {
        scale = 1.0f;
    }

    dst.w = (int)((float)baseW * scale + 0.5f);
    dst.h = (int)((float)baseH * scale + 0.5f);
    dst.x = (outW - dst.w) / 2;
    dst.y = (outH - dst.h) / 2;
    src.w = baseW;
    src.h = baseH;

    SDL_SetRenderDrawColor(neogeo_palette_debugState.renderer, 0, 0, 0, 255);
    clearRect.w = outW;
    clearRect.h = outH;
    SDL_RenderFillRect(neogeo_palette_debugState.renderer, &clearRect);
    SDL_RenderCopy(neogeo_palette_debugState.renderer, neogeo_palette_debugState.texture, &src, &dst);
}

static void
neogeo_palette_debug_renderBank(uint32_t *pixels,
                                int pitch,
                                int width,
                                int height,
                                const e9k_debug_palette_state_t *paletteState,
                                unsigned bankIndex,
                                int panelX,
                                int panelY,
                                int panelW,
                                int panelH)
{
    const int swatchStride = NEOGEO_PALETTE_DEBUG_SWATCH_SIZE + NEOGEO_PALETTE_DEBUG_SWATCH_GAP;
    const int paletteCellW =
        NEOGEO_PALETTE_DEBUG_PALETTE_BORDER * 2 +
        NEOGEO_PALETTE_DEBUG_SWATCH_COLS * NEOGEO_PALETTE_DEBUG_SWATCH_SIZE +
        (NEOGEO_PALETTE_DEBUG_SWATCH_COLS - 1) * NEOGEO_PALETTE_DEBUG_SWATCH_GAP;
    const int paletteCellH =
        NEOGEO_PALETTE_DEBUG_PALETTE_BORDER * 2 +
        NEOGEO_PALETTE_DEBUG_SWATCH_ROWS * NEOGEO_PALETTE_DEBUG_SWATCH_SIZE +
        (NEOGEO_PALETTE_DEBUG_SWATCH_ROWS - 1) * NEOGEO_PALETTE_DEBUG_SWATCH_GAP;
    const int contentX = panelX + NEOGEO_PALETTE_DEBUG_PANEL_PAD;
    const int contentY = panelY + NEOGEO_PALETTE_DEBUG_PANEL_HEADER_H;
    const uint32_t panelBg = neogeo_palette_debug_argb(255, 26, 28, 32);
    const uint32_t panelBorder = bankIndex == paletteState->active_bank ?
        neogeo_palette_debug_argb(255, 240, 240, 240) :
        neogeo_palette_debug_argb(255, 92, 96, 104);
    const uint32_t titleColor = bankIndex == paletteState->active_bank ?
        neogeo_palette_debug_argb(255, 255, 220, 96) :
        neogeo_palette_debug_argb(255, 180, 184, 192);
    const uint32_t activeMarker = bankIndex == paletteState->active_bank ?
        neogeo_palette_debug_argb(255, 255, 196, 64) :
        neogeo_palette_debug_argb(255, 80, 84, 92);
    const uint32_t paletteBorder = neogeo_palette_debug_argb(255, 14, 16, 20);
    const uint32_t transparentColor = neogeo_palette_debug_argb(255, 8, 8, 8);
    size_t bankBase = (size_t)bankIndex * 4096u;

    neogeo_palette_debug_fillRect(pixels, pitch, width, height, panelX, panelY, panelW, panelH, panelBg);
    neogeo_palette_debug_drawRect(pixels, pitch, width, height, panelX, panelY, panelW, panelH, panelBorder);
    neogeo_palette_debug_fillRect(pixels,
                                  pitch,
                                  width,
                                  height,
                                  panelX + 6,
                                  panelY + 6,
                                  10,
                                  10,
                                  activeMarker);
    neogeo_palette_debug_drawDigits(pixels,
                                    pitch,
                                    width,
                                    height,
                                    panelX + 22,
                                    panelY + 4,
                                    bankIndex,
                                    2,
                                    titleColor);

    for (int paletteRow = 0; paletteRow < NEOGEO_PALETTE_DEBUG_GRID_ROWS; ++paletteRow) {
        for (int paletteCol = 0; paletteCol < NEOGEO_PALETTE_DEBUG_GRID_COLS; ++paletteCol) {
            int paletteIndex = paletteRow * NEOGEO_PALETTE_DEBUG_GRID_COLS + paletteCol;
            int paletteX = contentX + paletteCol * (paletteCellW + NEOGEO_PALETTE_DEBUG_PALETTE_GAP);
            int paletteY = contentY + NEOGEO_PALETTE_DEBUG_PANEL_PAD +
                           paletteRow * (paletteCellH + NEOGEO_PALETTE_DEBUG_PALETTE_GAP);

            neogeo_palette_debug_drawRect(pixels,
                                          pitch,
                                          width,
                                          height,
                                          paletteX,
                                          paletteY,
                                          paletteCellW,
                                          paletteCellH,
                                          paletteBorder);

            for (int colorIndex = 0; colorIndex < NEOGEO_PALETTE_DEBUG_COLORS_PER_PALETTE; ++colorIndex) {
                int swatchCol = colorIndex % NEOGEO_PALETTE_DEBUG_SWATCH_COLS;
                int swatchRow = colorIndex / NEOGEO_PALETTE_DEBUG_SWATCH_COLS;
                int swatchX = paletteX + NEOGEO_PALETTE_DEBUG_PALETTE_BORDER + swatchCol * swatchStride;
                int swatchY = paletteY + NEOGEO_PALETTE_DEBUG_PALETTE_BORDER + swatchRow * swatchStride;
                size_t colorOffset = bankBase + (size_t)paletteIndex * 16u + (size_t)colorIndex;
                uint32_t color = transparentColor;

                if (colorOffset < paletteState->color_count) {
                    color = paletteState->colors[colorOffset];
                }
                neogeo_palette_debug_fillRect(pixels,
                                              pitch,
                                              width,
                                              height,
                                              swatchX,
                                              swatchY,
                                              NEOGEO_PALETTE_DEBUG_SWATCH_SIZE,
                                              NEOGEO_PALETTE_DEBUG_SWATCH_SIZE,
                                              color);
            }
        }
    }
}

static void
neogeo_palette_debug_renderFrameInternal(void)
{
    const int swatchStride = NEOGEO_PALETTE_DEBUG_SWATCH_SIZE + NEOGEO_PALETTE_DEBUG_SWATCH_GAP;
    const int paletteCellW =
        NEOGEO_PALETTE_DEBUG_PALETTE_BORDER * 2 +
        NEOGEO_PALETTE_DEBUG_SWATCH_COLS * NEOGEO_PALETTE_DEBUG_SWATCH_SIZE +
        (NEOGEO_PALETTE_DEBUG_SWATCH_COLS - 1) * NEOGEO_PALETTE_DEBUG_SWATCH_GAP;
    const int paletteCellH =
        NEOGEO_PALETTE_DEBUG_PALETTE_BORDER * 2 +
        NEOGEO_PALETTE_DEBUG_SWATCH_ROWS * NEOGEO_PALETTE_DEBUG_SWATCH_SIZE +
        (NEOGEO_PALETTE_DEBUG_SWATCH_ROWS - 1) * NEOGEO_PALETTE_DEBUG_SWATCH_GAP;
    const int panelW =
        NEOGEO_PALETTE_DEBUG_PANEL_PAD * 2 +
        NEOGEO_PALETTE_DEBUG_GRID_COLS * paletteCellW +
        (NEOGEO_PALETTE_DEBUG_GRID_COLS - 1) * NEOGEO_PALETTE_DEBUG_PALETTE_GAP;
    const int panelH =
        NEOGEO_PALETTE_DEBUG_PANEL_HEADER_H +
        NEOGEO_PALETTE_DEBUG_PANEL_PAD * 2 +
        NEOGEO_PALETTE_DEBUG_GRID_ROWS * paletteCellH +
        (NEOGEO_PALETTE_DEBUG_GRID_ROWS - 1) * NEOGEO_PALETTE_DEBUG_PALETTE_GAP;
    const int baseW = panelW + NEOGEO_PALETTE_DEBUG_OUTER_PAD * 2;
    const int baseH =
        panelH * NEOGEO_PALETTE_DEBUG_BANK_COUNT +
        NEOGEO_PALETTE_DEBUG_PANEL_GAP +
        NEOGEO_PALETTE_DEBUG_OUTER_PAD * 2;
    const uint32_t bgColor = neogeo_palette_debug_argb(255, 10, 12, 16);
    e9k_debug_palette_state_t paletteState;
    uint32_t hash = 0u;
    size_t needed = 0;

    (void)swatchStride;

    if (!neogeo_palette_debugState.windowState.open || !neogeo_palette_debugState.renderer) {
        return;
    }

    memset(&paletteState, 0, sizeof(paletteState));
    if (!libretro_host_debugGetGeoPaletteState(&paletteState)) {
        return;
    }

    if (neogeo_palette_debugState.texW != baseW || neogeo_palette_debugState.texH != baseH) {
        if (neogeo_palette_debugState.texture) {
            SDL_DestroyTexture(neogeo_palette_debugState.texture);
            neogeo_palette_debugState.texture = NULL;
        }
        neogeo_palette_debugState.texture = SDL_CreateTexture(neogeo_palette_debugState.renderer,
                                                              SDL_PIXELFORMAT_ARGB8888,
                                                              SDL_TEXTUREACCESS_STREAMING,
                                                              baseW,
                                                              baseH);
        neogeo_palette_debugState.texW = baseW;
        neogeo_palette_debugState.texH = baseH;
        neogeo_palette_debugState.logicalW = baseW;
        neogeo_palette_debugState.logicalH = baseH;
        neogeo_palette_debugState.cachedValid = 0;
    }
    if (!neogeo_palette_debugState.texture) {
        return;
    }

    hash = neogeo_palette_debug_hashPaletteState(&paletteState);
    if (neogeo_palette_debugState.cachedValid && hash == neogeo_palette_debugState.lastHash) {
        neogeo_palette_debug_presentTexture(baseW, baseH);
        return;
    }

    needed = (size_t)baseW * (size_t)baseH;
    if (needed > neogeo_palette_debugState.pixelsCap) {
        uint32_t *nextPixels = (uint32_t *)realloc(neogeo_palette_debugState.pixels,
                                                   needed * sizeof(uint32_t));
        if (!nextPixels) {
            return;
        }
        neogeo_palette_debugState.pixels = nextPixels;
        neogeo_palette_debugState.pixelsCap = needed;
    }

    for (size_t i = 0; i < needed; ++i) {
        neogeo_palette_debugState.pixels[i] = bgColor;
    }

    for (unsigned bankIndex = 0; bankIndex < NEOGEO_PALETTE_DEBUG_BANK_COUNT; ++bankIndex) {
        int panelX = NEOGEO_PALETTE_DEBUG_OUTER_PAD;
        int panelY = NEOGEO_PALETTE_DEBUG_OUTER_PAD +
                     (int)bankIndex * (panelH + NEOGEO_PALETTE_DEBUG_PANEL_GAP);

        neogeo_palette_debug_renderBank(neogeo_palette_debugState.pixels,
                                        baseW,
                                        baseW,
                                        baseH,
                                        &paletteState,
                                        bankIndex,
                                        panelX,
                                        panelY,
                                        panelW,
                                        panelH);
    }

    SDL_UpdateTexture(neogeo_palette_debugState.texture,
                      NULL,
                      neogeo_palette_debugState.pixels,
                      baseW * (int)sizeof(uint32_t));
    neogeo_palette_debug_presentTexture(baseW, baseH);
    neogeo_palette_debugState.cachedValid = 1;
    neogeo_palette_debugState.lastHash = hash;
}

static int
neogeo_palette_debug_overlayBodyPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)ctx;
    (void)availW;
    return 0;
}

static void
neogeo_palette_debug_overlayBodyLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static void
neogeo_palette_debug_overlayBodyRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    SDL_Rect prevViewport = {0, 0, 0, 0};
    SDL_Rect viewport = {0, 0, 0, 0};
    SDL_Rect prevClip = {0, 0, 0, 0};
    SDL_Rect localClip = {0, 0, 0, 0};
    SDL_Rect viewportLocal = {0, 0, 0, 0};
    SDL_Rect clipped = {0, 0, 0, 0};
    int hadClip = 0;

    if (!self || !ctx || !ctx->renderer) {
        return;
    }
    if (!neogeo_palette_debugState.windowState.open) {
        return;
    }

    viewport.x = self->bounds.x;
    viewport.y = self->bounds.y;
    viewport.w = self->bounds.w;
    viewport.h = self->bounds.h;
    hadClip = SDL_RenderIsClipEnabled(ctx->renderer) ? 1 : 0;
    if (hadClip) {
        SDL_RenderGetClipRect(ctx->renderer, &prevClip);
    }
    SDL_RenderGetViewport(ctx->renderer, &prevViewport);
    SDL_RenderSetViewport(ctx->renderer, &viewport);

    if (hadClip) {
        localClip.x = prevClip.x - self->bounds.x;
        localClip.y = prevClip.y - self->bounds.y;
        localClip.w = prevClip.w;
        localClip.h = prevClip.h;
        viewportLocal.w = self->bounds.w;
        viewportLocal.h = self->bounds.h;
        if (SDL_IntersectRect(&localClip, &viewportLocal, &clipped)) {
            SDL_RenderSetClipRect(ctx->renderer, &clipped);
        } else {
            SDL_Rect empty = {0, 0, 0, 0};
            SDL_RenderSetClipRect(ctx->renderer, &empty);
        }
    }

    neogeo_palette_debugState.window = ctx->window;
    neogeo_palette_debugState.renderer = ctx->renderer;
    neogeo_palette_debug_renderFrameInternal();
    SDL_RenderSetViewport(ctx->renderer, &prevViewport);
    if (hadClip) {
        SDL_RenderSetClipRect(ctx->renderer, &prevClip);
    }
}

static void
neogeo_palette_debug_overlayBodyDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    neogeo_palette_debug_overlay_body_state_t *state = NULL;

    (void)ctx;

    if (!self) {
        return;
    }
    state = (neogeo_palette_debug_overlay_body_state_t *)self->state;
    alloc_free(state);
    self->state = NULL;
}

static e9ui_component_t *
neogeo_palette_debug_makeOverlayBodyHost(void)
{
    e9ui_component_t *host = NULL;
    neogeo_palette_debug_overlay_body_state_t *state = NULL;

    host = (e9ui_component_t *)alloc_calloc(1, sizeof(*host));
    if (!host) {
        return NULL;
    }
    state = (neogeo_palette_debug_overlay_body_state_t *)alloc_calloc(1, sizeof(*state));
    if (!state) {
        alloc_free(host);
        return NULL;
    }

    host->name = "neogeo_palette_debug_overlay_body";
    host->state = state;
    host->preferredHeight = neogeo_palette_debug_overlayBodyPreferredHeight;
    host->layout = neogeo_palette_debug_overlayBodyLayout;
    host->render = neogeo_palette_debug_overlayBodyRender;
    host->dtor = neogeo_palette_debug_overlayBodyDtor;
    return host;
}

static void
neogeo_palette_debug_overlayWindowCloseRequested(e9ui_window_t *window, void *user)
{
    (void)window;
    (void)user;
    neogeo_palette_debug_toggle();
}

static e9ui_rect_t
neogeo_palette_debug_windowDefaultRect(const e9ui_context_t *ctx)
{
    e9ui_rect_t rect = {
        e9ui_scale_px(ctx, 96),
        e9ui_scale_px(ctx, 96),
        e9ui_scale_px(ctx, 760),
        e9ui_scale_px(ctx, 980)
    };

    return rect;
}

int
neogeo_palette_debug_handleKeydown(const SDL_KeyboardEvent *kev)
{
    if (!kev || !neogeo_palette_debugState.windowState.open) {
        return 0;
    }
    if (kev->repeat != 0) {
        return 0;
    }
    if (kev->keysym.sym == SDLK_ESCAPE) {
        neogeo_palette_debug_toggle();
        return 1;
    }
    return 0;
}

void
neogeo_palette_debug_toggle(void)
{
    if (!neogeo_palette_debugState.windowState.open) {
        e9ui_rect_t rect;

        neogeo_palette_debugState.windowState.windowHost =
            e9ui_windowCreate(neogeo_palette_debug_windowBackend());
        if (!neogeo_palette_debugState.windowState.windowHost) {
            return;
        }

        neogeo_palette_debugState.root = e9ui_stack_makeVertical();
        neogeo_palette_debugState.overlayBodyHost = neogeo_palette_debug_makeOverlayBodyHost();
        if (neogeo_palette_debugState.root && neogeo_palette_debugState.overlayBodyHost) {
            e9ui_stack_addFlex(neogeo_palette_debugState.root,
                               neogeo_palette_debugState.overlayBodyHost);
        }

        rect = e9ui_windowResolveStateOpenRect(&e9ui->ctx,
                                               neogeo_palette_debug_windowDefaultRect(&e9ui->ctx),
                                               &neogeo_palette_debugState.windowState);
        e9ui_windowOpen(neogeo_palette_debugState.windowState.windowHost,
                        "Palette Debug",
                        rect,
                        neogeo_palette_debugState.root ?
                            neogeo_palette_debugState.root :
                            neogeo_palette_debugState.overlayBodyHost,
                        neogeo_palette_debug_overlayWindowCloseRequested,
                        NULL,
                        &e9ui->ctx);
        neogeo_palette_debugState.window = e9ui->ctx.window;
        neogeo_palette_debugState.renderer = e9ui->ctx.renderer;
        neogeo_palette_debugState.windowState.open = 1;
        aux_window_register(&neogeo_palette_debug_auxWindowOps, &neogeo_palette_debugState);
        return;
    }

    aux_window_unregister(&neogeo_palette_debug_auxWindowOps, &neogeo_palette_debugState);
    (void)e9ui_windowCaptureStateRectSnapshot(&neogeo_palette_debugState.windowState, &e9ui->ctx);
    config_saveConfig();
    if (neogeo_palette_debugState.texture) {
        SDL_DestroyTexture(neogeo_palette_debugState.texture);
        neogeo_palette_debugState.texture = NULL;
    }
    if (neogeo_palette_debugState.pixels) {
        free(neogeo_palette_debugState.pixels);
        neogeo_palette_debugState.pixels = NULL;
    }
    neogeo_palette_debugState.pixelsCap = 0;
    neogeo_palette_debugState.texW = 0;
    neogeo_palette_debugState.texH = 0;
    neogeo_palette_debugState.logicalW = 0;
    neogeo_palette_debugState.logicalH = 0;
    neogeo_palette_debugState.cachedValid = 0;
    neogeo_palette_debugState.lastHash = 0u;
    if (neogeo_palette_debugState.windowState.windowHost) {
        e9ui_windowDestroy(neogeo_palette_debugState.windowState.windowHost);
        neogeo_palette_debugState.windowState.windowHost = NULL;
    }
    neogeo_palette_debugState.overlayBodyHost = NULL;
    neogeo_palette_debugState.root = NULL;
    neogeo_palette_debugState.windowState.open = 0;
    neogeo_palette_debugState.window = NULL;
    neogeo_palette_debugState.renderer = NULL;
}

int
neogeo_palette_debug_isOpen(void)
{
    return neogeo_palette_debugState.windowState.open ? 1 : 0;
}

void
neogeo_palette_debug_setMainWindowFocused(int focused)
{
    (void)focused;
}

void
neogeo_palette_debug_render(void)
{
    if (!neogeo_palette_debugState.windowState.open) {
        return;
    }
    if (e9ui_windowCaptureStateRectChanged(&neogeo_palette_debugState.windowState, &e9ui->ctx)) {
        config_saveConfig();
    }
}

void
neogeo_palette_debug_persistConfig(FILE *file)
{
    if (!file) {
        return;
    }
    e9ui_windowPersistStateRect(file,
                                "comp.neogeo_palette_debug",
                                &neogeo_palette_debugState.windowState,
                                &e9ui->ctx);
}

int
neogeo_palette_debug_loadConfigProperty(const char *prop, const char *value)
{
    int intValue = 0;

    if (!prop || !value) {
        return 0;
    }

    if (strcmp(prop, "win_x") == 0) {
        if (!neogeo_palette_debug_parseInt(value, &intValue)) {
            return 0;
        }
        neogeo_palette_debugState.windowState.winX = intValue;
    } else if (strcmp(prop, "win_y") == 0) {
        if (!neogeo_palette_debug_parseInt(value, &intValue)) {
            return 0;
        }
        neogeo_palette_debugState.windowState.winY = intValue;
    } else if (strcmp(prop, "win_w") == 0) {
        if (!neogeo_palette_debug_parseInt(value, &intValue)) {
            return 0;
        }
        neogeo_palette_debugState.windowState.winW = intValue;
    } else if (strcmp(prop, "win_h") == 0) {
        if (!neogeo_palette_debug_parseInt(value, &intValue)) {
            return 0;
        }
        neogeo_palette_debugState.windowState.winH = intValue;
    } else {
        return 0;
    }

    neogeo_palette_debugState.windowState.winHasSaved =
        e9ui_windowHasSavedPosition(neogeo_palette_debugState.windowState.winX,
                                    neogeo_palette_debugState.windowState.winY);
    return 1;
}
