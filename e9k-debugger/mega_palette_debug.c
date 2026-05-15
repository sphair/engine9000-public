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
#include "mega_palette_debug.h"
#include "alloc.h"
#include "config.h"
#include "e9ui.h"
#include "e9ui_scroll.h"
#include "libretro_host.h"

#define MEGA_PALETTE_DEBUG_CRAM_MEMORY_ID 4u
#define MEGA_PALETTE_DEBUG_BANK_COUNT 4
#define MEGA_PALETTE_DEBUG_PALETTES_PER_BANK 1
#define MEGA_PALETTE_DEBUG_COLORS_PER_PALETTE 16
#define MEGA_PALETTE_DEBUG_GRID_COLS 1
#define MEGA_PALETTE_DEBUG_GRID_ROWS 1
#define MEGA_PALETTE_DEBUG_SWATCH_COLS 4
#define MEGA_PALETTE_DEBUG_SWATCH_ROWS 4
#define MEGA_PALETTE_DEBUG_SWATCH_SIZE 8
#define MEGA_PALETTE_DEBUG_SWATCH_GAP 1
#define MEGA_PALETTE_DEBUG_PALETTE_BORDER 1
#define MEGA_PALETTE_DEBUG_PALETTE_GAP 3
#define MEGA_PALETTE_DEBUG_PANEL_PAD 10
#define MEGA_PALETTE_DEBUG_PANEL_HEADER_H 0
#define MEGA_PALETTE_DEBUG_PANEL_GAP 18
#define MEGA_PALETTE_DEBUG_OUTER_PAD 10
#define MEGA_PALETTE_DEBUG_RIGHT_PAD 18

typedef struct mega_palette_debug_state
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
    uint32_t greyscaleMask;
    int cachedValid;
} mega_palette_debug_state_t;

typedef struct mega_palette_debug_overlay_body_state
{
    int unused;
} mega_palette_debug_overlay_body_state_t;

static mega_palette_debug_state_t mega_palette_debugState = {
    .windowState.winX = E9UI_WINDOW_COORD_UNSET,
    .windowState.winY = E9UI_WINDOW_COORD_UNSET,
    .windowState.openMinWidthPx = 160,
    .windowState.openMinHeightPx = 260,
    .windowState.openCenterWhenNoSaved = 1,
};

static const aux_window_ops_t mega_palette_debug_auxWindowOps = {
    .setFocus = mega_palette_debug_setMainWindowFocused,
    .render = mega_palette_debug_render,
};

static e9ui_window_backend_t
mega_palette_debug_windowBackend(void)
{
    return e9ui_window_backend_overlay;
}

static int
mega_palette_debug_parseInt(const char *value, int *out)
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
mega_palette_debug_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t
mega_palette_debug_cramColor(const uint16_t *cram, size_t cramWords, unsigned paletteBank, unsigned colorIndex)
{
    size_t offset = (size_t)(paletteBank & 3u) * 16u + (size_t)(colorIndex & 15u);
    uint16_t color = 0u;
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;

    if (!cram || offset >= cramWords) {
        return mega_palette_debug_argb(255, 16, 16, 16);
    }
    color = cram[offset];
    r = (uint8_t)((((color >> 1) & 0x07u) * 255u) / 7u);
    g = (uint8_t)((((color >> 5) & 0x07u) * 255u) / 7u);
    b = (uint8_t)((((color >> 9) & 0x07u) * 255u) / 7u);
    if (mega_palette_debugState.greyscaleMask & (1u << (paletteBank & 3u))) {
        uint8_t grey = (uint8_t)((30u * r + 59u * g + 11u * b) / 100u);
        r = grey;
        g = grey;
        b = grey;
    }
    return mega_palette_debug_argb(255, r, g, b);
}

static void
mega_palette_debug_fillRect(uint32_t *pixels,
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
mega_palette_debug_drawRect(uint32_t *pixels,
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

    mega_palette_debug_fillRect(pixels, pitch, width, height, x, y, w, 1, color);
    mega_palette_debug_fillRect(pixels, pitch, width, height, x, y + h - 1, w, 1, color);
    mega_palette_debug_fillRect(pixels, pitch, width, height, x, y, 1, h, color);
    mega_palette_debug_fillRect(pixels, pitch, width, height, x + w - 1, y, 1, h, color);
}

static uint32_t
mega_palette_debug_hashCram(const uint16_t *cram, size_t cramWords)
{
    uint32_t hash = 2166136261u;

    if (!cram || cramWords == 0u) {
        return 0u;
    }
    if (cramWords > 64u) {
        cramWords = 64u;
    }
    for (size_t i = 0; i < cramWords; ++i) {
        hash ^= cram[i];
        hash *= 16777619u;
    }
    hash ^= mega_palette_debugState.greyscaleMask;
    hash *= 16777619u;
    return hash;
}

static void
mega_palette_debug_baseSize(int *outW, int *outH)
{
    const int paletteCellW =
        MEGA_PALETTE_DEBUG_PALETTE_BORDER * 2 +
        MEGA_PALETTE_DEBUG_SWATCH_COLS * MEGA_PALETTE_DEBUG_SWATCH_SIZE +
        (MEGA_PALETTE_DEBUG_SWATCH_COLS - 1) * MEGA_PALETTE_DEBUG_SWATCH_GAP;
    const int paletteCellH =
        MEGA_PALETTE_DEBUG_PALETTE_BORDER * 2 +
        MEGA_PALETTE_DEBUG_SWATCH_ROWS * MEGA_PALETTE_DEBUG_SWATCH_SIZE +
        (MEGA_PALETTE_DEBUG_SWATCH_ROWS - 1) * MEGA_PALETTE_DEBUG_SWATCH_GAP;
    const int panelW =
        MEGA_PALETTE_DEBUG_PANEL_PAD * 2 +
        MEGA_PALETTE_DEBUG_GRID_COLS * paletteCellW +
        (MEGA_PALETTE_DEBUG_GRID_COLS - 1) * MEGA_PALETTE_DEBUG_PALETTE_GAP;
    const int panelH =
        MEGA_PALETTE_DEBUG_PANEL_HEADER_H +
        MEGA_PALETTE_DEBUG_PANEL_PAD * 2 +
        MEGA_PALETTE_DEBUG_GRID_ROWS * paletteCellH +
        (MEGA_PALETTE_DEBUG_GRID_ROWS - 1) * MEGA_PALETTE_DEBUG_PALETTE_GAP;

    if (outW) {
        *outW = panelW + MEGA_PALETTE_DEBUG_OUTER_PAD * 2;
    }
    if (outH) {
        *outH = panelH * MEGA_PALETTE_DEBUG_BANK_COUNT +
                (MEGA_PALETTE_DEBUG_BANK_COUNT - 1) * MEGA_PALETTE_DEBUG_PANEL_GAP +
                MEGA_PALETTE_DEBUG_OUTER_PAD * 2;
    }
}

static float
mega_palette_debug_scaleForWidth(int availW, int baseW)
{
    int drawW = availW - MEGA_PALETTE_DEBUG_OUTER_PAD - MEGA_PALETTE_DEBUG_RIGHT_PAD;

    if (baseW <= 0) {
        return 1.0f;
    }
    if (drawW <= 0) {
        drawW = availW > 0 ? availW : baseW;
    }
    return (float)drawW / (float)baseW;
}

static int
mega_palette_debug_contentHeightForWidth(int availW)
{
    int baseW = 0;
    int baseH = 0;
    float scale = 1.0f;
    int contentH = 0;

    mega_palette_debug_baseSize(&baseW, &baseH);
    scale = mega_palette_debug_scaleForWidth(availW, baseW);
    contentH = (int)((float)baseH * scale + 0.5f) + MEGA_PALETTE_DEBUG_OUTER_PAD * 2;
    if (contentH < 1) {
        contentH = 1;
    }
    return contentH;
}

static void
mega_palette_debug_presentTexture(const e9ui_rect_t *bounds, int baseW, int baseH)
{
    SDL_Rect clearRect = {0, 0, 0, 0};
    SDL_Rect dst = {0, 0, 0, 0};
    SDL_Rect src = {0, 0, 0, 0};
    float scale = 1.0f;

    if (!bounds || bounds->w <= 0 || bounds->h <= 0) {
        return;
    }

    scale = mega_palette_debug_scaleForWidth(bounds->w, baseW);
    dst.w = (int)((float)baseW * scale + 0.5f);
    dst.h = (int)((float)baseH * scale + 0.5f);
    dst.x = bounds->x + MEGA_PALETTE_DEBUG_OUTER_PAD;
    dst.y = bounds->y + MEGA_PALETTE_DEBUG_OUTER_PAD;
    src.w = baseW;
    src.h = baseH;

    SDL_SetRenderDrawColor(mega_palette_debugState.renderer, 0, 0, 0, 255);
    clearRect.x = bounds->x;
    clearRect.y = bounds->y;
    clearRect.w = bounds->w;
    clearRect.h = bounds->h;
    SDL_RenderFillRect(mega_palette_debugState.renderer, &clearRect);
    SDL_RenderCopy(mega_palette_debugState.renderer, mega_palette_debugState.texture, &src, &dst);
}

static void
mega_palette_debug_renderBank(uint32_t *pixels,
                                int pitch,
                                int width,
                                int height,
                                const uint16_t *cram,
                                size_t cramWords,
                                unsigned bankIndex,
                                int panelX,
                                int panelY,
                                int panelW,
                                int panelH)
{
    const int swatchStride = MEGA_PALETTE_DEBUG_SWATCH_SIZE + MEGA_PALETTE_DEBUG_SWATCH_GAP;
    const int paletteCellW =
        MEGA_PALETTE_DEBUG_PALETTE_BORDER * 2 +
        MEGA_PALETTE_DEBUG_SWATCH_COLS * MEGA_PALETTE_DEBUG_SWATCH_SIZE +
        (MEGA_PALETTE_DEBUG_SWATCH_COLS - 1) * MEGA_PALETTE_DEBUG_SWATCH_GAP;
    const int paletteCellH =
        MEGA_PALETTE_DEBUG_PALETTE_BORDER * 2 +
        MEGA_PALETTE_DEBUG_SWATCH_ROWS * MEGA_PALETTE_DEBUG_SWATCH_SIZE +
        (MEGA_PALETTE_DEBUG_SWATCH_ROWS - 1) * MEGA_PALETTE_DEBUG_SWATCH_GAP;
    const int contentX = panelX + MEGA_PALETTE_DEBUG_PANEL_PAD;
    const int contentY = panelY + MEGA_PALETTE_DEBUG_PANEL_HEADER_H;
    const uint32_t panelBg = mega_palette_debug_argb(255, 26, 28, 32);
    const uint32_t panelBorder = (mega_palette_debugState.greyscaleMask & (1u << (bankIndex & 3u))) ?
        mega_palette_debug_argb(255, 236, 236, 236) :
        mega_palette_debug_argb(255, 92, 160, 220);
    const uint32_t paletteBorder = mega_palette_debug_argb(255, 14, 16, 20);

    mega_palette_debug_fillRect(pixels, pitch, width, height, panelX, panelY, panelW, panelH, panelBg);
    mega_palette_debug_drawRect(pixels, pitch, width, height, panelX, panelY, panelW, panelH, panelBorder);

    for (int paletteRow = 0; paletteRow < MEGA_PALETTE_DEBUG_GRID_ROWS; ++paletteRow) {
        for (int paletteCol = 0; paletteCol < MEGA_PALETTE_DEBUG_GRID_COLS; ++paletteCol) {
            int paletteX = contentX + paletteCol * (paletteCellW + MEGA_PALETTE_DEBUG_PALETTE_GAP);
            int paletteY = contentY + MEGA_PALETTE_DEBUG_PANEL_PAD +
                           paletteRow * (paletteCellH + MEGA_PALETTE_DEBUG_PALETTE_GAP);

            mega_palette_debug_drawRect(pixels,
                                          pitch,
                                          width,
                                          height,
                                          paletteX,
                                          paletteY,
                                          paletteCellW,
                                          paletteCellH,
                                          paletteBorder);

            for (int colorIndex = 0; colorIndex < MEGA_PALETTE_DEBUG_COLORS_PER_PALETTE; ++colorIndex) {
                int swatchCol = colorIndex % MEGA_PALETTE_DEBUG_SWATCH_COLS;
                int swatchRow = colorIndex / MEGA_PALETTE_DEBUG_SWATCH_COLS;
                int swatchX = paletteX + MEGA_PALETTE_DEBUG_PALETTE_BORDER + swatchCol * swatchStride;
                int swatchY = paletteY + MEGA_PALETTE_DEBUG_PALETTE_BORDER + swatchRow * swatchStride;
                uint32_t color = mega_palette_debug_cramColor(cram,
                                                              cramWords,
                                                              bankIndex,
                                                              (unsigned)colorIndex);
                mega_palette_debug_fillRect(pixels,
                                              pitch,
                                              width,
                                              height,
                                              swatchX,
                                              swatchY,
                                              MEGA_PALETTE_DEBUG_SWATCH_SIZE,
                                              MEGA_PALETTE_DEBUG_SWATCH_SIZE,
                                              color);
            }
        }
    }
}

static int
mega_palette_debug_paletteAtPoint(const e9ui_rect_t *bounds, int mx, int my, unsigned *outBank)
{
    const int paletteCellW =
        MEGA_PALETTE_DEBUG_PALETTE_BORDER * 2 +
        MEGA_PALETTE_DEBUG_SWATCH_COLS * MEGA_PALETTE_DEBUG_SWATCH_SIZE +
        (MEGA_PALETTE_DEBUG_SWATCH_COLS - 1) * MEGA_PALETTE_DEBUG_SWATCH_GAP;
    const int paletteCellH =
        MEGA_PALETTE_DEBUG_PALETTE_BORDER * 2 +
        MEGA_PALETTE_DEBUG_SWATCH_ROWS * MEGA_PALETTE_DEBUG_SWATCH_SIZE +
        (MEGA_PALETTE_DEBUG_SWATCH_ROWS - 1) * MEGA_PALETTE_DEBUG_SWATCH_GAP;
    const int panelW =
        MEGA_PALETTE_DEBUG_PANEL_PAD * 2 +
        MEGA_PALETTE_DEBUG_GRID_COLS * paletteCellW +
        (MEGA_PALETTE_DEBUG_GRID_COLS - 1) * MEGA_PALETTE_DEBUG_PALETTE_GAP;
    const int panelH =
        MEGA_PALETTE_DEBUG_PANEL_HEADER_H +
        MEGA_PALETTE_DEBUG_PANEL_PAD * 2 +
        MEGA_PALETTE_DEBUG_GRID_ROWS * paletteCellH +
        (MEGA_PALETTE_DEBUG_GRID_ROWS - 1) * MEGA_PALETTE_DEBUG_PALETTE_GAP;
    int baseW = 0;
    int baseH = 0;
    float scale = 1.0f;
    int localX = 0;
    int localY = 0;

    if (!bounds || !outBank || bounds->w <= 0 || bounds->h <= 0) {
        return 0;
    }

    mega_palette_debug_baseSize(&baseW, &baseH);
    scale = mega_palette_debug_scaleForWidth(bounds->w, baseW);
    if (scale <= 0.0f) {
        return 0;
    }
    localX = (int)(((float)(mx - bounds->x - MEGA_PALETTE_DEBUG_OUTER_PAD) / scale) + 0.5f);
    localY = (int)(((float)(my - bounds->y - MEGA_PALETTE_DEBUG_OUTER_PAD) / scale) + 0.5f);
    if (localX < 0 || localX >= baseW || localY < 0 || localY >= baseH) {
        return 0;
    }

    for (unsigned bankIndex = 0; bankIndex < MEGA_PALETTE_DEBUG_BANK_COUNT; ++bankIndex) {
        int panelX = MEGA_PALETTE_DEBUG_OUTER_PAD;
        int panelY = MEGA_PALETTE_DEBUG_OUTER_PAD +
                     (int)bankIndex * (panelH + MEGA_PALETTE_DEBUG_PANEL_GAP);
        if (localX >= panelX && localX < panelX + panelW &&
            localY >= panelY && localY < panelY + panelH) {
            *outBank = bankIndex;
            return 1;
        }
    }
    return 0;
}

static void
mega_palette_debug_applyGreyscaleMask(uint32_t mask)
{
    mega_palette_debugState.greyscaleMask = mask & 0x0fu;
    mega_palette_debugState.cachedValid = 0;
    (void)libretro_host_megadrive_setPaletteGreyscaleMask(mega_palette_debugState.greyscaleMask);
}

static void
mega_palette_debug_overlayBodyClick(e9ui_component_t *self,
                                    e9ui_context_t *ctx,
                                    const e9ui_mouse_event_t *mouseEv)
{
    unsigned bankIndex = 0;
    uint32_t mask = 0u;

    (void)ctx;

    if (!self || !mouseEv || mouseEv->button != E9UI_MOUSE_BUTTON_LEFT) {
        return;
    }
    if (!mega_palette_debug_paletteAtPoint(&self->bounds, mouseEv->x, mouseEv->y, &bankIndex)) {
        return;
    }
    mask = mega_palette_debugState.greyscaleMask ^ (1u << (bankIndex & 3u));
    mega_palette_debug_applyGreyscaleMask(mask);
}

static void
mega_palette_debug_renderFrameInternal(const e9ui_rect_t *bounds)
{
    const int paletteCellW =
        MEGA_PALETTE_DEBUG_PALETTE_BORDER * 2 +
        MEGA_PALETTE_DEBUG_SWATCH_COLS * MEGA_PALETTE_DEBUG_SWATCH_SIZE +
        (MEGA_PALETTE_DEBUG_SWATCH_COLS - 1) * MEGA_PALETTE_DEBUG_SWATCH_GAP;
    const int paletteCellH =
        MEGA_PALETTE_DEBUG_PALETTE_BORDER * 2 +
        MEGA_PALETTE_DEBUG_SWATCH_ROWS * MEGA_PALETTE_DEBUG_SWATCH_SIZE +
        (MEGA_PALETTE_DEBUG_SWATCH_ROWS - 1) * MEGA_PALETTE_DEBUG_SWATCH_GAP;
    const int panelW =
        MEGA_PALETTE_DEBUG_PANEL_PAD * 2 +
        MEGA_PALETTE_DEBUG_GRID_COLS * paletteCellW +
        (MEGA_PALETTE_DEBUG_GRID_COLS - 1) * MEGA_PALETTE_DEBUG_PALETTE_GAP;
    const int panelH =
        MEGA_PALETTE_DEBUG_PANEL_HEADER_H +
        MEGA_PALETTE_DEBUG_PANEL_PAD * 2 +
        MEGA_PALETTE_DEBUG_GRID_ROWS * paletteCellH +
        (MEGA_PALETTE_DEBUG_GRID_ROWS - 1) * MEGA_PALETTE_DEBUG_PALETTE_GAP;
    int baseW = 0;
    int baseH = 0;
    const uint32_t bgColor = mega_palette_debug_argb(255, 10, 12, 16);
    const uint16_t *cram = NULL;
    size_t cramBytes = 0u;
    size_t cramWords = 0u;
    uint32_t hash = 0u;
    size_t needed = 0;

    if (!bounds || !mega_palette_debugState.windowState.open || !mega_palette_debugState.renderer) {
        return;
    }

    mega_palette_debug_baseSize(&baseW, &baseH);
    (void)libretro_host_megadrive_getPaletteGreyscaleMask(&mega_palette_debugState.greyscaleMask);
    cram = (const uint16_t *)libretro_host_getMemory(MEGA_PALETTE_DEBUG_CRAM_MEMORY_ID, &cramBytes);
    cramWords = cramBytes / sizeof(*cram);
    if (!cram || cramWords == 0u) {
        return;
    }

    if (mega_palette_debugState.texW != baseW || mega_palette_debugState.texH != baseH) {
        if (mega_palette_debugState.texture) {
            SDL_DestroyTexture(mega_palette_debugState.texture);
            mega_palette_debugState.texture = NULL;
        }
        mega_palette_debugState.texture = SDL_CreateTexture(mega_palette_debugState.renderer,
                                                              SDL_PIXELFORMAT_ARGB8888,
                                                              SDL_TEXTUREACCESS_STREAMING,
                                                              baseW,
                                                              baseH);
        mega_palette_debugState.texW = baseW;
        mega_palette_debugState.texH = baseH;
        mega_palette_debugState.logicalW = baseW;
        mega_palette_debugState.logicalH = baseH;
        mega_palette_debugState.cachedValid = 0;
    }
    if (!mega_palette_debugState.texture) {
        return;
    }

    hash = mega_palette_debug_hashCram(cram, cramWords);
    if (mega_palette_debugState.cachedValid && hash == mega_palette_debugState.lastHash) {
        mega_palette_debug_presentTexture(bounds, baseW, baseH);
        return;
    }

    needed = (size_t)baseW * (size_t)baseH;
    if (needed > mega_palette_debugState.pixelsCap) {
        uint32_t *nextPixels = (uint32_t *)realloc(mega_palette_debugState.pixels,
                                                   needed * sizeof(uint32_t));
        if (!nextPixels) {
            return;
        }
        mega_palette_debugState.pixels = nextPixels;
        mega_palette_debugState.pixelsCap = needed;
    }

    for (size_t i = 0; i < needed; ++i) {
        mega_palette_debugState.pixels[i] = bgColor;
    }

    for (unsigned bankIndex = 0; bankIndex < MEGA_PALETTE_DEBUG_BANK_COUNT; ++bankIndex) {
        int panelX = MEGA_PALETTE_DEBUG_OUTER_PAD;
        int panelY = MEGA_PALETTE_DEBUG_OUTER_PAD +
                     (int)bankIndex * (panelH + MEGA_PALETTE_DEBUG_PANEL_GAP);

        mega_palette_debug_renderBank(mega_palette_debugState.pixels,
                                        baseW,
                                        baseW,
                                        baseH,
                                        cram,
                                        cramWords,
                                        bankIndex,
                                        panelX,
                                        panelY,
                                        panelW,
                                        panelH);
    }

    SDL_UpdateTexture(mega_palette_debugState.texture,
                      NULL,
                      mega_palette_debugState.pixels,
                      baseW * (int)sizeof(uint32_t));
    mega_palette_debug_presentTexture(bounds, baseW, baseH);
    mega_palette_debugState.cachedValid = 1;
    mega_palette_debugState.lastHash = hash;
}

static int
mega_palette_debug_overlayBodyPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)ctx;
    return mega_palette_debug_contentHeightForWidth(availW);
}

static void
mega_palette_debug_overlayBodyLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static void
mega_palette_debug_overlayBodyRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) {
        return;
    }
    if (!mega_palette_debugState.windowState.open) {
        return;
    }

    mega_palette_debugState.window = ctx->window;
    mega_palette_debugState.renderer = ctx->renderer;
    mega_palette_debug_renderFrameInternal(&self->bounds);
}

static void
mega_palette_debug_overlayBodyDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    mega_palette_debug_overlay_body_state_t *state = NULL;

    (void)ctx;

    if (!self) {
        return;
    }
    state = (mega_palette_debug_overlay_body_state_t *)self->state;
    alloc_free(state);
    self->state = NULL;
}

static e9ui_component_t *
mega_palette_debug_makeOverlayBodyHost(void)
{
    e9ui_component_t *host = NULL;
    mega_palette_debug_overlay_body_state_t *state = NULL;

    host = (e9ui_component_t *)alloc_calloc(1, sizeof(*host));
    if (!host) {
        return NULL;
    }
    state = (mega_palette_debug_overlay_body_state_t *)alloc_calloc(1, sizeof(*state));
    if (!state) {
        alloc_free(host);
        return NULL;
    }

    host->name = "mega_palette_debug_overlay_body";
    host->state = state;
    host->preferredHeight = mega_palette_debug_overlayBodyPreferredHeight;
    host->layout = mega_palette_debug_overlayBodyLayout;
    host->render = mega_palette_debug_overlayBodyRender;
    host->onClick = mega_palette_debug_overlayBodyClick;
    host->dtor = mega_palette_debug_overlayBodyDtor;
    return host;
}

static void
mega_palette_debug_overlayWindowCloseRequested(e9ui_window_t *window, void *user)
{
    (void)window;
    (void)user;
    mega_palette_debug_toggle();
}

static e9ui_rect_t
mega_palette_debug_windowDefaultRect(const e9ui_context_t *ctx)
{
    e9ui_rect_t rect = {
        e9ui_scale_px(ctx, 96),
        e9ui_scale_px(ctx, 96),
        e9ui_scale_px(ctx, 260),
        e9ui_scale_px(ctx, 560)
    };

    return rect;
}

int
mega_palette_debug_handleKeydown(const SDL_KeyboardEvent *kev)
{
    if (!kev || !mega_palette_debugState.windowState.open) {
        return 0;
    }
    if (kev->repeat != 0) {
        return 0;
    }
    if (kev->keysym.sym == SDLK_ESCAPE) {
        mega_palette_debug_toggle();
        return 1;
    }
    return 0;
}

void
mega_palette_debug_toggle(void)
{
    if (!mega_palette_debugState.windowState.open) {
        e9ui_rect_t rect;

        mega_palette_debugState.windowState.windowHost =
            e9ui_windowCreate(mega_palette_debug_windowBackend());
        if (!mega_palette_debugState.windowState.windowHost) {
            return;
        }
        e9ui_windowSetMinSize(mega_palette_debugState.windowState.windowHost,
                              mega_palette_debugState.windowState.openMinWidthPx,
                              mega_palette_debugState.windowState.openMinHeightPx);

        mega_palette_debugState.root = e9ui_stack_makeVertical();
        mega_palette_debugState.overlayBodyHost = mega_palette_debug_makeOverlayBodyHost();
        if (mega_palette_debugState.root && mega_palette_debugState.overlayBodyHost) {
            e9ui_component_t *scroll = e9ui_scroll_make(mega_palette_debugState.overlayBodyHost);

            e9ui_stack_addFlex(mega_palette_debugState.root,
                               scroll ? scroll : mega_palette_debugState.overlayBodyHost);
        }

        rect = e9ui_windowResolveStateOpenRect(&e9ui->ctx,
                                               mega_palette_debug_windowDefaultRect(&e9ui->ctx),
                                               &mega_palette_debugState.windowState);
        e9ui_windowOpen(mega_palette_debugState.windowState.windowHost,
                        "MEGA PALETTE",
                        rect,
                        mega_palette_debugState.root ?
                            mega_palette_debugState.root :
                            mega_palette_debugState.overlayBodyHost,
                        mega_palette_debug_overlayWindowCloseRequested,
                        NULL,
                        &e9ui->ctx);
        mega_palette_debugState.window = e9ui->ctx.window;
        mega_palette_debugState.renderer = e9ui->ctx.renderer;
        mega_palette_debugState.windowState.open = 1;
        aux_window_register(&mega_palette_debug_auxWindowOps, &mega_palette_debugState);
        return;
    }

    aux_window_unregister(&mega_palette_debug_auxWindowOps, &mega_palette_debugState);
    (void)e9ui_windowCaptureStateRectSnapshot(&mega_palette_debugState.windowState, &e9ui->ctx);
    config_saveConfig();
    if (mega_palette_debugState.texture) {
        SDL_DestroyTexture(mega_palette_debugState.texture);
        mega_palette_debugState.texture = NULL;
    }
    if (mega_palette_debugState.pixels) {
        free(mega_palette_debugState.pixels);
        mega_palette_debugState.pixels = NULL;
    }
    mega_palette_debugState.pixelsCap = 0;
    mega_palette_debugState.texW = 0;
    mega_palette_debugState.texH = 0;
    mega_palette_debugState.logicalW = 0;
    mega_palette_debugState.logicalH = 0;
    mega_palette_debugState.cachedValid = 0;
    mega_palette_debugState.lastHash = 0u;
    if (mega_palette_debugState.windowState.windowHost) {
        e9ui_windowDestroy(mega_palette_debugState.windowState.windowHost);
        mega_palette_debugState.windowState.windowHost = NULL;
    }
    mega_palette_debugState.overlayBodyHost = NULL;
    mega_palette_debugState.root = NULL;
    mega_palette_debugState.windowState.open = 0;
    mega_palette_debugState.window = NULL;
    mega_palette_debugState.renderer = NULL;
}

int
mega_palette_debug_isOpen(void)
{
    return mega_palette_debugState.windowState.open ? 1 : 0;
}

void
mega_palette_debug_setMainWindowFocused(int focused)
{
    (void)focused;
}

void
mega_palette_debug_render(void)
{
    if (!mega_palette_debugState.windowState.open) {
        return;
    }
    if (e9ui_windowCaptureStateRectChanged(&mega_palette_debugState.windowState, &e9ui->ctx)) {
        config_saveConfig();
    }
}

void
mega_palette_debug_persistConfig(FILE *file)
{
    if (!file) {
        return;
    }
    e9ui_windowPersistStateRect(file,
                                "comp.mega_palette_debug",
                                &mega_palette_debugState.windowState,
                                &e9ui->ctx);
}

int
mega_palette_debug_loadConfigProperty(const char *prop, const char *value)
{
    int intValue = 0;

    if (!prop || !value) {
        return 0;
    }

    if (strcmp(prop, "win_x") == 0) {
        if (!mega_palette_debug_parseInt(value, &intValue)) {
            return 0;
        }
        mega_palette_debugState.windowState.winX = intValue;
    } else if (strcmp(prop, "win_y") == 0) {
        if (!mega_palette_debug_parseInt(value, &intValue)) {
            return 0;
        }
        mega_palette_debugState.windowState.winY = intValue;
    } else if (strcmp(prop, "win_w") == 0) {
        if (!mega_palette_debug_parseInt(value, &intValue)) {
            return 0;
        }
        mega_palette_debugState.windowState.winW = intValue;
    } else if (strcmp(prop, "win_h") == 0) {
        if (!mega_palette_debug_parseInt(value, &intValue)) {
            return 0;
        }
        mega_palette_debugState.windowState.winH = intValue;
    } else {
        return 0;
    }

    mega_palette_debugState.windowState.winHasSaved =
        e9ui_windowHasSavedPosition(mega_palette_debugState.windowState.winX,
                                    mega_palette_debugState.windowState.winY);
    return 1;
}
