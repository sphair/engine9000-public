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
#include <stdlib.h>
#include <string.h>

#include "aux_window.h"
#include "mega_sprite_debug.h"
#include "alloc.h"
#include "config.h"
#include "e9ui.h"

#define MEGA_SPRITE_DEBUG_HIST_WIDTH 320
#define MEGA_SPRITE_DEBUG_GAP 8
#define MEGA_SPRITE_DEBUG_MIN_W 320
#define MEGA_SPRITE_DEBUG_MIN_H 224
#define MEGA_SPRITE_DEBUG_MARGIN_X 64
#define MEGA_SPRITE_DEBUG_MARGIN_Y 48
#define MEGA_SPRITE_DEBUG_HIST_LEGEND_H 40
#define MEGA_SPRITE_DEBUG_FRAME_BLOCK_H 18
#define MEGA_SPRITE_DEBUG_LEGEND_H MEGA_SPRITE_DEBUG_FRAME_BLOCK_H
#define MEGA_SPRITE_DEBUG_HIST_RIGHT_PAD 20

#define MEGA_SPRITE_DEBUG_LINK_NORMAL 0
#define MEGA_SPRITE_DEBUG_LINK_BROKEN 1
#define MEGA_SPRITE_DEBUG_LINK_LOOP 2
#define MEGA_SPRITE_DEBUG_LINK_END 3

typedef struct mega_sprite_debug_link_diag
{
    int traversal[E9K_DEBUG_MEGA_MAX_FRAME_SPRITES];
    int traversalCount;
    int orderByIndex[E9K_DEBUG_MEGA_MAX_FRAME_SPRITES];
    int nextByIndex[E9K_DEBUG_MEGA_MAX_FRAME_SPRITES];
    uint8_t edgeStatus[E9K_DEBUG_MEGA_MAX_FRAME_SPRITES];
    int visitedCount;
    int uniqueCount;
    int duplicates;
    int broken;
    int loops;
    int firstBadIndex;
    int firstLoopStart;
} mega_sprite_debug_link_diag_t;

typedef struct mega_sprite_debug_state
{
    e9ui_window_state_t windowState;
    SDL_Window *window;
    SDL_Renderer *renderer;
    e9ui_component_t *overlayBodyHost;
    SDL_Texture *texture;
    uint32_t *pixels;
    size_t pixelsCap;
    int texW;
    int texH;
    int logicalW;
    int logicalH;
    uint32_t grad[MEGA_SPRITE_DEBUG_HIST_WIDTH];
    int gradReady;
    uint32_t lastHash;
    int cachedValid;
    int showLinks;
    int showOrderNumbers;
    int highlightIssuesOnly;
    e9k_debug_mega_sprite_state_t lastState;
    int hasLastState;
} mega_sprite_debug_state_t;

static mega_sprite_debug_state_t mega_sprite_debug_state = {
    .windowState.winX = E9UI_WINDOW_COORD_UNSET,
    .windowState.winY = E9UI_WINDOW_COORD_UNSET,
    .windowState.openMinWidthPx = 520,
    .windowState.openMinHeightPx = 360,
    .windowState.openCenterWhenNoSaved = 1,
};

typedef struct mega_sprite_debug_overlay_body_state
{
    int unused;
} mega_sprite_debug_overlay_body_state_t;

static e9ui_window_backend_t
mega_sprite_debug_windowBackend(void)
{
    return e9ui_window_backend_overlay;
}

static const aux_window_ops_t mega_sprite_debug_auxWindowOps = {
    .setFocus = mega_sprite_debug_setMainWindowFocused,
    .handleKeydown = mega_sprite_debug_handleKeydown,
};

static uint32_t
mega_sprite_debug_color(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint32_t)(0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
}

static void
mega_sprite_debug_hueToRgb(float h, uint8_t *r, uint8_t *g, uint8_t *b)
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
    *r = (uint8_t)(rr * 255.0f);
    *g = (uint8_t)(gg * 255.0f);
    *b = (uint8_t)(bb * 255.0f);
}

static int
mega_sprite_debug_parseInt(const char *value, int *out)
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
mega_sprite_debug_fillRect(uint32_t *pixels, int width, int height, int x, int y, int w, int h, uint32_t color)
{
    if (!pixels || width <= 0 || height <= 0 || w <= 0 || h <= 0) {
        return;
    }
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;
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
        uint32_t *row = pixels + (size_t)yy * (size_t)width + x0;
        for (int xx = x0; xx < x1; ++xx) {
            row[xx - x0] = color;
        }
    }
}

static e9ui_rect_t
mega_sprite_debug_windowDefaultRect(const e9ui_context_t *ctx)
{
    e9ui_rect_t rect = {
        e9ui_scale_px(ctx, 96),
        e9ui_scale_px(ctx, 96),
        e9ui_scale_px(ctx, 900),
        e9ui_scale_px(ctx, 560)
    };
    return rect;
}

static int
mega_sprite_debug_overlayBodyPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)ctx;
    (void)availW;
    return 0;
}

static void
mega_sprite_debug_overlayBodyLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static int
mega_sprite_debug_overlayHandleLocalKey(SDL_Keycode key)
{
    if (key == SDLK_l) {
        mega_sprite_debug_state.showLinks = mega_sprite_debug_state.showLinks ? 0 : 1;
        mega_sprite_debug_state.cachedValid = 0;
        e9ui_showTransientMessage(mega_sprite_debug_state.showLinks ? "MEGA LINKS ON" : "MEGA LINKS OFF");
        return 1;
    }
    if (key == SDLK_o) {
        mega_sprite_debug_state.showOrderNumbers = mega_sprite_debug_state.showOrderNumbers ? 0 : 1;
        mega_sprite_debug_state.cachedValid = 0;
        e9ui_showTransientMessage(mega_sprite_debug_state.showOrderNumbers ? "MEGA ORDER ON" : "MEGA ORDER OFF");
        return 1;
    }
    if (key == SDLK_i) {
        mega_sprite_debug_state.highlightIssuesOnly = mega_sprite_debug_state.highlightIssuesOnly ? 0 : 1;
        mega_sprite_debug_state.cachedValid = 0;
        e9ui_showTransientMessage(mega_sprite_debug_state.highlightIssuesOnly ? "MEGA ISSUES ON" : "MEGA ISSUES OFF");
        return 1;
    }
    return 0;
}

static void
mega_sprite_debug_renderFrameInternal(const e9k_debug_mega_sprite_state_t *st, int presentFrame);

static int
mega_sprite_debug_overlayBodyHandleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    (void)ctx;
    if (!self || !ev) {
        return 0;
    }
    if (ev->type == SDL_KEYDOWN) {
        if (ev->key.repeat != 0) {
            return 0;
        }
        if ((ev->key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) != 0) {
            return 0;
        }
        return mega_sprite_debug_overlayHandleLocalKey(ev->key.keysym.sym);
    }
    return 0;
}

static void
mega_sprite_debug_overlayBodyRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) {
        return;
    }
    if (!mega_sprite_debug_state.windowState.open || !mega_sprite_debug_state.hasLastState) {
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
    mega_sprite_debug_state.window = ctx->window;
    mega_sprite_debug_state.renderer = ctx->renderer;
    mega_sprite_debug_renderFrameInternal(&mega_sprite_debug_state.lastState, 0);
    SDL_RenderSetViewport(ctx->renderer, &prevViewport);
    if (hadClip) {
        SDL_RenderSetClipRect(ctx->renderer, &prevClip);
    }
}

static void
mega_sprite_debug_overlayBodyDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    mega_sprite_debug_overlay_body_state_t *st = (mega_sprite_debug_overlay_body_state_t *)self->state;
    alloc_free(st);
    self->state = NULL;
}

static e9ui_component_t *
mega_sprite_debug_makeOverlayBodyHost(void)
{
    e9ui_component_t *host = (e9ui_component_t *)alloc_calloc(1, sizeof(*host));
    if (!host) {
        return NULL;
    }
    mega_sprite_debug_overlay_body_state_t *st =
        (mega_sprite_debug_overlay_body_state_t *)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(host);
        return NULL;
    }
    host->name = "mega_sprite_debug_overlay_body";
    host->state = st;
    host->focusable = 1;
    host->preferredHeight = mega_sprite_debug_overlayBodyPreferredHeight;
    host->layout = mega_sprite_debug_overlayBodyLayout;
    host->render = mega_sprite_debug_overlayBodyRender;
    host->handleEvent = mega_sprite_debug_overlayBodyHandleEvent;
    host->dtor = mega_sprite_debug_overlayBodyDtor;
    return host;
}

static void
mega_sprite_debug_overlayWindowCloseRequested(e9ui_window_t *window, void *user)
{
    (void)window;
    (void)user;
    mega_sprite_debug_toggle();
}

static void
mega_sprite_debug_drawRect(uint32_t *pixels, int width, int height, int x, int y, int w, int h, uint32_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    mega_sprite_debug_fillRect(pixels, width, height, x, y, w, 1, color);
    mega_sprite_debug_fillRect(pixels, width, height, x, y + h - 1, w, 1, color);
    mega_sprite_debug_fillRect(pixels, width, height, x, y, 1, h, color);
    mega_sprite_debug_fillRect(pixels, width, height, x + w - 1, y, 1, h, color);
}

static void
mega_sprite_debug_drawChar5x7(uint32_t *pixels, int width, int height, int x, int y, char ch, uint32_t color)
{
    static const struct {
        char ch;
        uint8_t rows[7];
    } glyphs[] = {
        { '0', {0x0e,0x11,0x13,0x15,0x19,0x11,0x0e} },
        { '1', {0x04,0x0c,0x04,0x04,0x04,0x04,0x0e} },
        { '2', {0x0e,0x11,0x01,0x02,0x04,0x08,0x1f} },
        { '3', {0x1e,0x01,0x01,0x06,0x01,0x01,0x1e} },
        { '4', {0x02,0x06,0x0a,0x12,0x1f,0x02,0x02} },
        { '5', {0x1f,0x10,0x1e,0x01,0x01,0x11,0x0e} },
        { '6', {0x06,0x08,0x10,0x1e,0x11,0x11,0x0e} },
        { '7', {0x1f,0x01,0x02,0x04,0x08,0x08,0x08} },
        { '8', {0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e} },
        { '9', {0x0e,0x11,0x11,0x0f,0x01,0x02,0x0c} },
        { '-', {0x00,0x00,0x00,0x1f,0x00,0x00,0x00} },
        { ':', {0x00,0x04,0x04,0x00,0x04,0x04,0x00} },
        { '/', {0x01,0x02,0x04,0x08,0x10,0x00,0x00} },
        { 'A', {0x0e,0x11,0x11,0x1f,0x11,0x11,0x11} },
        { 'B', {0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e} },
        { 'C', {0x0f,0x10,0x10,0x10,0x10,0x10,0x0f} },
        { 'D', {0x1e,0x11,0x11,0x11,0x11,0x11,0x1e} },
        { 'E', {0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f} },
        { 'F', {0x1f,0x10,0x10,0x1e,0x10,0x10,0x10} },
        { 'G', {0x0f,0x10,0x10,0x13,0x11,0x11,0x0f} },
        { 'H', {0x11,0x11,0x11,0x1f,0x11,0x11,0x11} },
        { 'I', {0x1f,0x04,0x04,0x04,0x04,0x04,0x1f} },
        { 'J', {0x01,0x01,0x01,0x01,0x11,0x11,0x0e} },
        { 'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11} },
        { 'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1f} },
        { 'M', {0x11,0x1b,0x15,0x15,0x11,0x11,0x11} },
        { 'N', {0x11,0x19,0x15,0x13,0x11,0x11,0x11} },
        { 'O', {0x0e,0x11,0x11,0x11,0x11,0x11,0x0e} },
        { 'P', {0x1e,0x11,0x11,0x1e,0x10,0x10,0x10} },
        { 'Q', {0x0e,0x11,0x11,0x11,0x15,0x12,0x0d} },
        { 'R', {0x1e,0x11,0x11,0x1e,0x14,0x12,0x11} },
        { 'S', {0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e} },
        { 'T', {0x1f,0x04,0x04,0x04,0x04,0x04,0x04} },
        { 'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0e} },
        { 'V', {0x11,0x11,0x11,0x11,0x11,0x0a,0x04} },
        { 'W', {0x11,0x11,0x11,0x15,0x15,0x15,0x0a} },
        { 'X', {0x11,0x11,0x0a,0x04,0x0a,0x11,0x11} },
        { 'Y', {0x11,0x11,0x0a,0x04,0x04,0x04,0x04} },
        { ' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00} }
    };

    const uint8_t *rows = NULL;
    for (size_t i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); ++i) {
        if (glyphs[i].ch == ch) {
            rows = glyphs[i].rows;
            break;
        }
    }
    if (!rows) {
        rows = glyphs[sizeof(glyphs) / sizeof(glyphs[0]) - 1].rows;
    }

    for (int ry = 0; ry < 7; ++ry) {
        uint8_t bits = rows[ry];
        for (int rx = 0; rx < 5; ++rx) {
            if (bits & (uint8_t)(1u << (4 - rx))) {
                int px = x + rx;
                int py = y + ry;
                if (px >= 0 && py >= 0 && px < width && py < height) {
                    pixels[(size_t)py * (size_t)width + (size_t)px] = color;
                }
            }
        }
    }
}

static void
mega_sprite_debug_drawText5x7(uint32_t *pixels, int width, int height, int x, int y, const char *text, uint32_t color)
{
    if (!pixels || !text) {
        return;
    }
    int cx = x;
    for (int i = 0; text[i]; ++i) {
        char ch = text[i];
        if (ch >= 'a' && ch <= 'z') {
            ch = (char)(ch - 'a' + 'A');
        }
        mega_sprite_debug_drawChar5x7(pixels, width, height, cx, y, ch, color);
        cx += 6;
    }
}

static void
mega_sprite_debug_drawLine(uint32_t *pixels, int width, int height, int x0, int y0, int x1, int y1, uint32_t color)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        if (x0 >= 0 && y0 >= 0 && x0 < width && y0 < height) {
            pixels[(size_t)y0 * (size_t)width + (size_t)x0] = color;
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void
mega_sprite_debug_drawCross(uint32_t *pixels, int width, int height, int cx, int cy, int radius, uint32_t color)
{
    mega_sprite_debug_drawLine(pixels, width, height, cx - radius, cy - radius, cx + radius, cy + radius, color);
    mega_sprite_debug_drawLine(pixels, width, height, cx - radius, cy + radius, cx + radius, cy - radius, color);
}

static void
mega_sprite_debug_formatInt(char *buf, size_t bufSize, int value)
{
    if (!buf || bufSize == 0) {
        return;
    }
    (void)snprintf(buf, bufSize, "%d", value);
}

static void
mega_sprite_debug_analyzeLinks(const e9k_debug_mega_sprite_state_t *st, mega_sprite_debug_link_diag_t *diag)
{
    if (!st || !diag) {
        return;
    }
    memset(diag, 0, sizeof(*diag));
    diag->firstBadIndex = -1;
    diag->firstLoopStart = -1;
    for (int i = 0; i < E9K_DEBUG_MEGA_MAX_FRAME_SPRITES; ++i) {
        diag->orderByIndex[i] = -1;
        diag->nextByIndex[i] = -1;
    }

    int entryCount = st->spriteEntryCount;
    if (entryCount < 0) {
        entryCount = 0;
    }
    if (entryCount > E9K_DEBUG_MEGA_MAX_FRAME_SPRITES) {
        entryCount = E9K_DEBUG_MEGA_MAX_FRAME_SPRITES;
    }
    if (entryCount == 0) {
        return;
    }

    int current = 0;
    int guard = 0;
    while (guard < entryCount) {
        if (current < 0 || current >= entryCount) {
            diag->broken++;
            if (diag->firstBadIndex < 0) {
                diag->firstBadIndex = current;
            }
            break;
        }
        if (diag->orderByIndex[current] >= 0) {
            diag->duplicates++;
            diag->loops++;
            if (diag->firstLoopStart < 0) {
                diag->firstLoopStart = current;
            }
            break;
        }

        diag->orderByIndex[current] = diag->traversalCount;
        diag->traversal[diag->traversalCount++] = current;
        diag->visitedCount++;
        diag->uniqueCount++;

        int next = (int)st->spriteEntries[current].link;
        diag->nextByIndex[current] = next;
        if (next == 0) {
            diag->edgeStatus[current] = MEGA_SPRITE_DEBUG_LINK_END;
            break;
        }
        if (next < 0 || next >= entryCount) {
            diag->edgeStatus[current] = MEGA_SPRITE_DEBUG_LINK_BROKEN;
            diag->broken++;
            if (diag->firstBadIndex < 0) {
                diag->firstBadIndex = current;
            }
            break;
        }
        if (diag->orderByIndex[next] >= 0) {
            diag->edgeStatus[current] = MEGA_SPRITE_DEBUG_LINK_LOOP;
            diag->duplicates++;
            diag->loops++;
            if (diag->firstLoopStart < 0) {
                diag->firstLoopStart = next;
            }
            break;
        }
        diag->edgeStatus[current] = MEGA_SPRITE_DEBUG_LINK_NORMAL;
        current = next;
        guard++;
    }
}

static uint32_t
mega_sprite_debug_hashState(const e9k_debug_mega_sprite_state_t *st)
{
    uint32_t h = 2166136261u;
    if (!st) {
        return h;
    }
    const uint8_t *bytes = (const uint8_t *)st;
    size_t size = sizeof(*st);
    for (size_t i = 0; i < size; ++i) {
        h ^= (uint32_t)bytes[i];
        h *= 16777619u;
    }
    h ^= (uint32_t)(mega_sprite_debug_state.showLinks ? 0x00010000u : 0u);
    h ^= (uint32_t)(mega_sprite_debug_state.showOrderNumbers ? 0x00020000u : 0u);
    h ^= (uint32_t)(mega_sprite_debug_state.highlightIssuesOnly ? 0x00040000u : 0u);
    h *= 16777619u;
    return h;
}

static void
mega_sprite_debug_presentTexture(int baseW, int baseH, int presentFrame)
{
    SDL_Rect viewport = { 0, 0, 0, 0 };
    SDL_RenderGetViewport(mega_sprite_debug_state.renderer, &viewport);
    int outW = viewport.w;
    int outH = viewport.h;
    if (outW <= 0 || outH <= 0) {
        SDL_GetRendererOutputSize(mega_sprite_debug_state.renderer, &outW, &outH);
        viewport.x = 0;
        viewport.y = 0;
        viewport.w = outW;
        viewport.h = outH;
    }
    if ((outW <= 0 || outH <= 0) && mega_sprite_debug_state.window) {
        SDL_GetWindowSize(mega_sprite_debug_state.window, &outW, &outH);
        viewport.x = 0;
        viewport.y = 0;
        viewport.w = outW;
        viewport.h = outH;
    }
    float scaleX = outW > 0 ? (float)outW / (float)baseW : 1.0f;
    float scaleY = outH > 0 ? (float)outH / (float)baseH : 1.0f;
    float scale = scaleX < scaleY ? scaleX : scaleY;
    if (scale <= 0.0f) {
        scale = 1.0f;
    }
    int dstW = (int)((float)baseW * scale + 0.5f);
    int dstH = (int)((float)baseH * scale + 0.5f);
    SDL_Rect dst = { (outW - dstW) / 2, (outH - dstH) / 2, dstW, dstH };
    SDL_SetRenderDrawColor(mega_sprite_debug_state.renderer, 0, 0, 0, 255);
    SDL_Rect clearRect = { 0, 0, outW, outH };
    SDL_RenderFillRect(mega_sprite_debug_state.renderer, &clearRect);
    SDL_Rect src = { 0, 0, baseW, baseH };
    SDL_RenderCopy(mega_sprite_debug_state.renderer, mega_sprite_debug_state.texture, &src, &dst);
    if (presentFrame) {
        SDL_RenderPresent(mega_sprite_debug_state.renderer);
    }
}

void
mega_sprite_debug_toggle(void)
{
    if (!mega_sprite_debug_state.windowState.open) {
        mega_sprite_debug_state.windowState.windowHost = e9ui_windowCreate(mega_sprite_debug_windowBackend());
        if (!mega_sprite_debug_state.windowState.windowHost) {
            return;
        }
        int lw = MEGA_SPRITE_DEBUG_MIN_W + MEGA_SPRITE_DEBUG_GAP + MEGA_SPRITE_DEBUG_HIST_WIDTH;
        int lh = MEGA_SPRITE_DEBUG_MIN_H;
        mega_sprite_debug_state.logicalW = lw;
        mega_sprite_debug_state.logicalH = lh;
        mega_sprite_debug_state.overlayBodyHost = mega_sprite_debug_makeOverlayBodyHost();
        if (!mega_sprite_debug_state.overlayBodyHost) {
            e9ui_windowDestroy(mega_sprite_debug_state.windowState.windowHost);
            mega_sprite_debug_state.windowState.windowHost = NULL;
            return;
        }
        e9ui_rect_t rect = e9ui_windowResolveStateOpenRect(&e9ui->ctx,
                                                           mega_sprite_debug_windowDefaultRect(&e9ui->ctx),
                                                           &mega_sprite_debug_state.windowState);
        e9ui_windowOpen(mega_sprite_debug_state.windowState.windowHost,
                                     "MEGA SPRITE DEBUG",
                                     rect,
                                     mega_sprite_debug_state.overlayBodyHost,
                                     mega_sprite_debug_overlayWindowCloseRequested,
                                     NULL,
			             &e9ui->ctx);

        mega_sprite_debug_state.window = e9ui->ctx.window;
        mega_sprite_debug_state.renderer = e9ui->ctx.renderer;
        mega_sprite_debug_state.windowState.open = 1;
        mega_sprite_debug_state.showLinks = 1;
        mega_sprite_debug_state.showOrderNumbers = 1;
        mega_sprite_debug_state.highlightIssuesOnly = 0;
        mega_sprite_debug_state.cachedValid = 0;
        mega_sprite_debug_state.lastHash = 0u;
        aux_window_register(&mega_sprite_debug_auxWindowOps, &mega_sprite_debug_state);
        return;
    }

    aux_window_unregister(&mega_sprite_debug_auxWindowOps, &mega_sprite_debug_state);
    if (mega_sprite_debug_state.texture) {
        SDL_DestroyTexture(mega_sprite_debug_state.texture);
        mega_sprite_debug_state.texture = NULL;
    }
    if (mega_sprite_debug_state.pixels) {
        free(mega_sprite_debug_state.pixels);
        mega_sprite_debug_state.pixels = NULL;
        mega_sprite_debug_state.pixelsCap = 0;
    }
    if (mega_sprite_debug_state.windowState.windowHost) {
        e9ui_windowDestroy(mega_sprite_debug_state.windowState.windowHost);
        mega_sprite_debug_state.windowState.windowHost = NULL;
    }
    mega_sprite_debug_state.overlayBodyHost = NULL;
    mega_sprite_debug_state.window = NULL;
    mega_sprite_debug_state.renderer = NULL;
    mega_sprite_debug_state.texW = 0;
    mega_sprite_debug_state.texH = 0;
    mega_sprite_debug_state.windowState.open = 0;
    mega_sprite_debug_state.cachedValid = 0;
    mega_sprite_debug_state.hasLastState = 0;
}

int
mega_sprite_debug_is_open(void)
{
    return mega_sprite_debug_state.windowState.open ? 1 : 0;
}

int
mega_sprite_debug_handleKeydown(const SDL_KeyboardEvent *kev)
{
    if (!kev || !mega_sprite_debug_state.windowState.open) {
        return 0;
    }
    if (kev->repeat != 0) {
        return 0;
    }
    if (kev->keysym.sym == SDLK_ESCAPE) {
        if (mega_sprite_debug_is_open()) {
            mega_sprite_debug_toggle();
        }
        return 1;
    }
    return 0;
}

void
mega_sprite_debug_setMainWindowFocused(int focused)
{
    (void)focused;
}

static void
mega_sprite_debug_renderFrameInternal(const e9k_debug_mega_sprite_state_t *st, int presentFrame)
{
    if (!mega_sprite_debug_state.windowState.open || !mega_sprite_debug_state.renderer || !st) {
        return;
    }

    int screenW = st->screenW > 0 ? st->screenW : MEGA_SPRITE_DEBUG_MIN_W;
    int screenH = st->screenH > 0 ? st->screenH : MEGA_SPRITE_DEBUG_MIN_H;
    int lineCount = st->lineCount > 0 ? st->lineCount : screenH;
    if (lineCount > E9K_DEBUG_MEGA_MAX_LINES) {
        lineCount = E9K_DEBUG_MEGA_MAX_LINES;
    }

    int visW = screenW - st->cropLeft - st->cropRight;
    int visH = lineCount - st->cropTop - st->cropBottom;
    if (visW <= 0) {
        visW = screenW;
    }
    if (visH <= 0) {
        visH = lineCount;
    }
    if (visW < 1) {
        visW = 1;
    }
    if (visH < 1) {
        visH = 1;
    }

    int histBoxW = (MEGA_SPRITE_DEBUG_HIST_WIDTH - MEGA_SPRITE_DEBUG_GAP) / 2;
    int panelW = histBoxW;
    int panelX = MEGA_SPRITE_DEBUG_GAP;
    int worldX = panelX + panelW + MEGA_SPRITE_DEBUG_GAP;
    int worldW = visW + (MEGA_SPRITE_DEBUG_MARGIN_X * 2);
    int worldH = visH + (MEGA_SPRITE_DEBUG_MARGIN_Y * 2);
    int baseW = worldX + worldW + MEGA_SPRITE_DEBUG_GAP + MEGA_SPRITE_DEBUG_HIST_WIDTH + MEGA_SPRITE_DEBUG_HIST_RIGHT_PAD;
    int baseH = worldH + MEGA_SPRITE_DEBUG_GAP + MEGA_SPRITE_DEBUG_LEGEND_H;
    if (baseW < (worldX + MEGA_SPRITE_DEBUG_MIN_W + (MEGA_SPRITE_DEBUG_MARGIN_X * 2) + MEGA_SPRITE_DEBUG_GAP + MEGA_SPRITE_DEBUG_HIST_WIDTH + MEGA_SPRITE_DEBUG_HIST_RIGHT_PAD)) {
        baseW = worldX + MEGA_SPRITE_DEBUG_MIN_W + (MEGA_SPRITE_DEBUG_MARGIN_X * 2) + MEGA_SPRITE_DEBUG_GAP + MEGA_SPRITE_DEBUG_HIST_WIDTH + MEGA_SPRITE_DEBUG_HIST_RIGHT_PAD;
    }
    if (baseH < (MEGA_SPRITE_DEBUG_MIN_H + (MEGA_SPRITE_DEBUG_MARGIN_Y * 2) + MEGA_SPRITE_DEBUG_GAP + MEGA_SPRITE_DEBUG_LEGEND_H)) {
        baseH = MEGA_SPRITE_DEBUG_MIN_H + (MEGA_SPRITE_DEBUG_MARGIN_Y * 2) + MEGA_SPRITE_DEBUG_GAP + MEGA_SPRITE_DEBUG_LEGEND_H;
    }

    if (mega_sprite_debug_state.texW != baseW || mega_sprite_debug_state.texH != baseH) {
        SDL_DestroyTexture(mega_sprite_debug_state.texture);
        mega_sprite_debug_state.texture = SDL_CreateTexture(mega_sprite_debug_state.renderer,
                                                            SDL_PIXELFORMAT_ARGB8888,
                                                            SDL_TEXTUREACCESS_STREAMING,
                                                            baseW, baseH);
        mega_sprite_debug_state.texW = baseW;
        mega_sprite_debug_state.texH = baseH;
        mega_sprite_debug_state.cachedValid = 0;
    }
    if (!mega_sprite_debug_state.texture) {
        return;
    }

    uint32_t hash = mega_sprite_debug_hashState(st);
    if (mega_sprite_debug_state.cachedValid && mega_sprite_debug_state.lastHash == hash) {
        mega_sprite_debug_presentTexture(baseW, baseH, presentFrame);
        return;
    }

    size_t pixelCount = (size_t)baseW * (size_t)baseH;
    if (pixelCount > mega_sprite_debug_state.pixelsCap) {
        uint32_t *next = (uint32_t *)realloc(mega_sprite_debug_state.pixels, pixelCount * sizeof(uint32_t));
        if (!next) {
            return;
        }
        mega_sprite_debug_state.pixels = next;
        mega_sprite_debug_state.pixelsCap = pixelCount;
    }
    uint32_t *pixels = mega_sprite_debug_state.pixels;

    uint32_t colBg = mega_sprite_debug_color(20, 20, 20);
    uint32_t colBorder = mega_sprite_debug_color(255, 255, 255);
    uint32_t colMask = mega_sprite_debug_color(32, 64, 160);
    uint32_t colSpriteOverflow = mega_sprite_debug_color(220, 0, 0);
    uint32_t colTileOverflow = mega_sprite_debug_color(220, 160, 0);
    uint32_t colFrameOk = mega_sprite_debug_color(64, 180, 64);
    uint32_t colFrameWarn = mega_sprite_debug_color(220, 180, 0);
    uint32_t colFrameBad = mega_sprite_debug_color(220, 0, 0);
    uint32_t colOutlineRendered = mega_sprite_debug_color(0, 220, 0);
    uint32_t colOutlineDropped = mega_sprite_debug_color(160, 160, 160);
    uint32_t colLinkNormal = mega_sprite_debug_color(255, 255, 255);
    uint32_t colLinkBroken = mega_sprite_debug_color(255, 160, 0);
    uint32_t colLinkLoop = mega_sprite_debug_color(255, 0, 255);
    uint32_t colLinkEnd = mega_sprite_debug_color(0, 220, 220);

    for (size_t i = 0; i < pixelCount; ++i) {
        pixels[i] = colBg;
    }

    int viewX = worldX + MEGA_SPRITE_DEBUG_MARGIN_X;
    int viewY = MEGA_SPRITE_DEBUG_MARGIN_Y;
    int viewW = visW;
    int viewH = visH;
    mega_sprite_debug_fillRect(pixels, baseW, baseH, viewX, viewY, viewW, 1, colBorder);
    mega_sprite_debug_fillRect(pixels, baseW, baseH, viewX, viewY + viewH - 1, viewW, 1, colBorder);
    mega_sprite_debug_fillRect(pixels, baseW, baseH, viewX, viewY, 1, viewH, colBorder);
    mega_sprite_debug_fillRect(pixels, baseW, baseH, viewX + viewW - 1, viewY, 1, viewH, colBorder);

    int histX = worldX + worldW + MEGA_SPRITE_DEBUG_GAP;
    int histInnerW = histBoxW - 2;
    int histSpriteX = histX;
    int histTileX = histX + histBoxW + MEGA_SPRITE_DEBUG_GAP;
    int panelY = viewY;
    int panelH = viewH;
    int panelTopPad = 10;
    int panelBoxGap = 14;
    int linksBoxY = panelY + panelTopPad;
    int linksBoxH = (panelH - panelTopPad - panelBoxGap) / 2;
    int legendBoxY = linksBoxY + linksBoxH + panelBoxGap;
    int legendBoxH = panelY + panelH - legendBoxY;
    mega_sprite_debug_drawText5x7(pixels, baseW, baseH, viewX + 3, viewY - 9, "SPRITES", colBorder);
    mega_sprite_debug_drawText5x7(pixels, baseW, baseH, histSpriteX + 3, viewY - 9, "SPRITES/LINE", colBorder);
    mega_sprite_debug_drawText5x7(pixels, baseW, baseH, histTileX + 3, viewY - 9, "TILES/LINE", colBorder);
    mega_sprite_debug_drawText5x7(pixels, baseW, baseH, panelX + 3, panelY, "LINKS", colBorder);
    mega_sprite_debug_drawText5x7(pixels, baseW, baseH, panelX + 3, linksBoxY + linksBoxH + 3, "STATES", colBorder);
    mega_sprite_debug_drawRect(pixels, baseW, baseH, histSpriteX, viewY, histBoxW, viewH, colBorder);
    mega_sprite_debug_drawRect(pixels, baseW, baseH, histTileX, viewY, histBoxW, viewH, colBorder);
    mega_sprite_debug_drawRect(pixels, baseW, baseH, panelX, linksBoxY, panelW, linksBoxH, colBorder);
    mega_sprite_debug_drawRect(pixels, baseW, baseH, panelX, legendBoxY, panelW, legendBoxH, colBorder);

    if (!mega_sprite_debug_state.gradReady) {
        int denom = (MEGA_SPRITE_DEBUG_HIST_WIDTH > 1) ? (MEGA_SPRITE_DEBUG_HIST_WIDTH - 1) : 1;
        for (int i = 0; i < MEGA_SPRITE_DEBUG_HIST_WIDTH; ++i) {
            float t = (float)i / (float)denom;
            float h = (1.0f / 3.0f) * (1.0f - t);
            uint8_t rr = 0;
            uint8_t gg = 0;
            uint8_t bb = 0;
            mega_sprite_debug_hueToRgb(h, &rr, &gg, &bb);
            mega_sprite_debug_state.grad[i] = mega_sprite_debug_color(rr, gg, bb);
        }
        mega_sprite_debug_state.gradReady = 1;
    }

    int entryCount = st->spriteEntryCount;
    if (entryCount < 0) {
        entryCount = 0;
    }
    if (entryCount > E9K_DEBUG_MEGA_MAX_FRAME_SPRITES) {
        entryCount = E9K_DEBUG_MEGA_MAX_FRAME_SPRITES;
    }
    mega_sprite_debug_link_diag_t diag = {0};
    mega_sprite_debug_analyzeLinks(st, &diag);

    for (int i = 0; i < entryCount; ++i) {
        const e9k_debug_mega_sprite_entry_t *entry = &st->spriteEntries[i];
        int x = viewX + ((int)entry->x - st->cropLeft);
        int y = viewY + ((int)entry->y - st->cropTop);
        int w = (int)entry->width;
        int h = (int)entry->height;
        if (w <= 0 || h <= 0) {
            continue;
        }
        uint8_t flags = entry->flags;
        uint32_t color = (flags & E9K_DEBUG_MEGA_SPRITEFLAG_RENDERED) ? colOutlineRendered : colOutlineDropped;
        if (flags & E9K_DEBUG_MEGA_SPRITEFLAG_OVERFLOW_SPRITE) {
            color = colSpriteOverflow;
        } else if (flags & E9K_DEBUG_MEGA_SPRITEFLAG_OVERFLOW_TILE) {
            color = colTileOverflow;
        } else if (flags & E9K_DEBUG_MEGA_SPRITEFLAG_MASKED) {
            color = colMask;
        }

        int isIssue = 0;
        int edgeStatus = (i >= 0 && i < entryCount) ? diag.edgeStatus[i] : 0;
        if (edgeStatus == MEGA_SPRITE_DEBUG_LINK_BROKEN || edgeStatus == MEGA_SPRITE_DEBUG_LINK_LOOP) {
            isIssue = 1;
        }
        if (flags & (E9K_DEBUG_MEGA_SPRITEFLAG_OVERFLOW_SPRITE | E9K_DEBUG_MEGA_SPRITEFLAG_OVERFLOW_TILE)) {
            isIssue = 1;
        }
        if (mega_sprite_debug_state.highlightIssuesOnly && !isIssue) {
            continue;
        }
        mega_sprite_debug_drawRect(pixels, baseW, baseH, x, y, w, h, color);

        if (diag.orderByIndex[i] >= 0) {
            uint32_t linkColor = colLinkNormal;
            if (edgeStatus == MEGA_SPRITE_DEBUG_LINK_BROKEN) {
                linkColor = colLinkBroken;
            } else if (edgeStatus == MEGA_SPRITE_DEBUG_LINK_LOOP) {
                linkColor = colLinkLoop;
            } else if (edgeStatus == MEGA_SPRITE_DEBUG_LINK_END) {
                linkColor = colLinkEnd;
            }
            if (edgeStatus != MEGA_SPRITE_DEBUG_LINK_NORMAL &&
                (!mega_sprite_debug_state.highlightIssuesOnly || edgeStatus == MEGA_SPRITE_DEBUG_LINK_BROKEN || edgeStatus == MEGA_SPRITE_DEBUG_LINK_LOOP) &&
                w > 2 && h > 2) {
                mega_sprite_debug_drawRect(pixels, baseW, baseH, x + 1, y + 1, w - 2, h - 2, linkColor);
            }
            if (mega_sprite_debug_state.showOrderNumbers && (!mega_sprite_debug_state.highlightIssuesOnly || isIssue)) {
                char num[16];
                mega_sprite_debug_formatInt(num, sizeof(num), diag.orderByIndex[i]);
                mega_sprite_debug_drawText5x7(pixels, baseW, baseH, x + 2, y + 2, num, linkColor);
            }
        }
    }

    if (mega_sprite_debug_state.showLinks) {
        for (int t = 0; t < diag.traversalCount; ++t) {
            int srcIndex = diag.traversal[t];
            if (srcIndex < 0 || srcIndex >= entryCount) {
                continue;
            }
            const e9k_debug_mega_sprite_entry_t *srcEntry = &st->spriteEntries[srcIndex];
            int sx = viewX + ((int)srcEntry->x - st->cropLeft) + ((int)srcEntry->width / 2);
            int sy = viewY + ((int)srcEntry->y - st->cropTop) + ((int)srcEntry->height / 2);
            uint8_t status = diag.edgeStatus[srcIndex];
            uint32_t linkColor = colLinkNormal;
            if (status == MEGA_SPRITE_DEBUG_LINK_BROKEN) {
                linkColor = colLinkBroken;
            } else if (status == MEGA_SPRITE_DEBUG_LINK_LOOP) {
                linkColor = colLinkLoop;
            } else if (status == MEGA_SPRITE_DEBUG_LINK_END) {
                linkColor = colLinkEnd;
            }
            if (mega_sprite_debug_state.highlightIssuesOnly && status != MEGA_SPRITE_DEBUG_LINK_BROKEN && status != MEGA_SPRITE_DEBUG_LINK_LOOP) {
                continue;
            }

            int nextIndex = diag.nextByIndex[srcIndex];
            if (status == MEGA_SPRITE_DEBUG_LINK_NORMAL || status == MEGA_SPRITE_DEBUG_LINK_LOOP) {
                if (nextIndex >= 0 && nextIndex < entryCount) {
                    const e9k_debug_mega_sprite_entry_t *dstEntry = &st->spriteEntries[nextIndex];
                    int dx = viewX + ((int)dstEntry->x - st->cropLeft) + ((int)dstEntry->width / 2);
                    int dy = viewY + ((int)dstEntry->y - st->cropTop) + ((int)dstEntry->height / 2);
                    mega_sprite_debug_drawLine(pixels, baseW, baseH, sx, sy, dx, dy, linkColor);
                }
            } else if (status == MEGA_SPRITE_DEBUG_LINK_BROKEN) {
                mega_sprite_debug_drawCross(pixels, baseW, baseH, sx, sy, 3, linkColor);
            } else if (status == MEGA_SPRITE_DEBUG_LINK_END) {
                mega_sprite_debug_fillRect(pixels, baseW, baseH, sx - 2, sy - 2, 5, 5, linkColor);
                mega_sprite_debug_drawRect(pixels, baseW, baseH, sx - 3, sy - 3, 7, 7, colBorder);
            }
        }
    }

    int spriteLimit = st->spriteLimitPerLine > 0 ? st->spriteLimitPerLine : 20;
    int tileLimit = st->tileLimitPerLine > 0 ? st->tileLimitPerLine : 320;
    int frameMax = st->frameSpriteMax > 0 ? st->frameSpriteMax : 80;

    for (int y = 0; y < viewH; ++y) {
        int drawY = viewY + y;
        if (drawY <= viewY || drawY >= (viewY + viewH - 1)) {
            continue;
        }
        int srcLine = y + st->cropTop;
        if (srcLine < 0 || srcLine >= lineCount) {
            continue;
        }
        uint8_t flags = st->lineFlags[srcLine];
        if (flags & E9K_DEBUG_MEGA_LINEFLAG_MASKED) {
            mega_sprite_debug_fillRect(pixels, baseW, baseH, viewX + 1, drawY, viewW - 2, 1, colMask);
        }
        if (flags & E9K_DEBUG_MEGA_LINEFLAG_TILE_OVERFLOW) {
            mega_sprite_debug_fillRect(pixels, baseW, baseH, viewX + 1, drawY, viewW - 2, 1, colTileOverflow);
        }
        if (flags & E9K_DEBUG_MEGA_LINEFLAG_SPRITE_OVERFLOW) {
            mega_sprite_debug_fillRect(pixels, baseW, baseH, viewX + 1, drawY, viewW - 2, 1, colSpriteOverflow);
        }

        int spriteBar = (int)((st->spritesPerLine[srcLine] * histInnerW) / spriteLimit);
        int tileBar = (int)((st->tilesPerLine[srcLine] * histInnerW) / tileLimit);
        if (spriteBar > histInnerW) {
            spriteBar = histInnerW;
        }
        if (tileBar > histInnerW) {
            tileBar = histInnerW;
        }

        uint32_t *spriteRow = pixels + (size_t)drawY * (size_t)baseW + histSpriteX + 1;
        uint32_t *tileRow = pixels + (size_t)drawY * (size_t)baseW + histTileX + 1;
        for (int x = 0; x < spriteBar; ++x) {
            int gx = (x * (MEGA_SPRITE_DEBUG_HIST_WIDTH - 1)) / (histInnerW > 1 ? (histInnerW - 1) : 1);
            spriteRow[x] = mega_sprite_debug_state.grad[gx];
        }
        for (int x = 0; x < tileBar; ++x) {
            int b = ((histInnerW - 1 - x) * 255) / (histInnerW > 1 ? (histInnerW - 1) : 1);
            int g = b / 2;
            tileRow[x] = mega_sprite_debug_color(64, (uint8_t)g, (uint8_t)b);
        }
    }

    for (int y = 0; y < viewH; ++y) {
        int srcLine = y + st->cropTop;
        if (srcLine < 0 || srcLine >= lineCount) {
            continue;
        }
        uint8_t flags = st->lineFlags[srcLine];
        if (!(flags & (E9K_DEBUG_MEGA_LINEFLAG_SPRITE_OVERFLOW | E9K_DEBUG_MEGA_LINEFLAG_TILE_OVERFLOW))) {
            continue;
        }
        int drawY = viewY + y;
        if (drawY <= viewY || drawY >= (viewY + viewH - 1)) {
            continue;
        }
        int lineOrdinal = 0;
        for (int t = 0; t < diag.traversalCount; ++t) {
            int idx = diag.traversal[t];
            if (idx < 0 || idx >= entryCount) {
                continue;
            }
            const e9k_debug_mega_sprite_entry_t *entry = &st->spriteEntries[idx];
            int sy = (int)entry->y - st->cropTop;
            int sh = (int)entry->height;
            if (sh <= 0) {
                continue;
            }
            if (y < sy || y >= sy + sh) {
                continue;
            }
            int sx = viewX + ((int)entry->x - st->cropLeft);
            int sw = (int)entry->width;
            if (sw <= 1) {
                continue;
            }
            uint32_t ordColor = mega_sprite_debug_state.grad[(lineOrdinal * (MEGA_SPRITE_DEBUG_HIST_WIDTH - 1)) / (diag.traversalCount > 1 ? (diag.traversalCount - 1) : 1)];
            int markerW = sw - 2;
            if (markerW > 10) {
                markerW = 10;
            }
            if (markerW < 1) {
                markerW = 1;
            }
            mega_sprite_debug_fillRect(pixels, baseW, baseH, sx + 1, drawY, markerW, 1, ordColor);
            lineOrdinal++;
        }
    }

    int frameBarW = viewW > 8 ? viewW - 8 : viewW;
    int frameBarH = 6;
    int frameX = viewX + 4;
    int frameTitleY = worldH + MEGA_SPRITE_DEBUG_GAP + 2;
    int frameY = frameTitleY + 8;
    if (frameBarW > 0 && frameBarH > 0 && frameY + frameBarH < baseH) {
        int used = st->frameSpriteUsed;
        if (used < 0) {
            used = 0;
        }
        int fillW = (int)((used * frameBarW) / frameMax);
        if (fillW > frameBarW) {
            fillW = frameBarW;
        }
        uint32_t fillColor = colFrameOk;
        if (used > frameMax) {
            fillColor = colFrameBad;
        } else if (used > (frameMax * 3) / 4) {
            fillColor = colFrameWarn;
        }
        mega_sprite_debug_drawText5x7(pixels, baseW, baseH, frameX, frameTitleY, "FRAME SPRITES", colBorder);
        mega_sprite_debug_fillRect(pixels, baseW, baseH, frameX, frameY, fillW, frameBarH, fillColor);
        mega_sprite_debug_fillRect(pixels, baseW, baseH, frameX - 1, frameY - 1, frameBarW + 2, 1, colBorder);
        mega_sprite_debug_fillRect(pixels, baseW, baseH, frameX - 1, frameY + frameBarH, frameBarW + 2, 1, colBorder);
        mega_sprite_debug_fillRect(pixels, baseW, baseH, frameX - 1, frameY - 1, 1, frameBarH + 2, colBorder);
        mega_sprite_debug_fillRect(pixels, baseW, baseH, frameX + frameBarW, frameY - 1, 1, frameBarH + 2, colBorder);
    }

    {
        char buf[64];
        uint32_t colOrderHint = mega_sprite_debug_state.grad[MEGA_SPRITE_DEBUG_HIST_WIDTH / 2];
        int tx = panelX + 4;
        int ty = linksBoxY + 3;
        int step = 9;
        mega_sprite_debug_formatInt(buf, sizeof(buf), diag.visitedCount);
        mega_sprite_debug_drawText5x7(pixels, baseW, baseH, tx, ty, "VISITED:", colBorder);
        mega_sprite_debug_drawText5x7(pixels, baseW, baseH, tx + 54, ty, buf, colLinkNormal);
        ty += step;
        mega_sprite_debug_formatInt(buf, sizeof(buf), diag.uniqueCount);
        mega_sprite_debug_drawText5x7(pixels, baseW, baseH, tx, ty, "UNIQUE:", colBorder);
        mega_sprite_debug_drawText5x7(pixels, baseW, baseH, tx + 54, ty, buf, colLinkNormal);
        ty += step;
        mega_sprite_debug_formatInt(buf, sizeof(buf), diag.duplicates);
        mega_sprite_debug_drawText5x7(pixels, baseW, baseH, tx, ty, "DUPES:", colBorder);
        mega_sprite_debug_drawText5x7(pixels, baseW, baseH, tx + 54, ty, buf, colLinkLoop);
        ty += step;
        mega_sprite_debug_formatInt(buf, sizeof(buf), diag.broken);
        mega_sprite_debug_drawText5x7(pixels, baseW, baseH, tx, ty, "BROKEN:", colBorder);
        mega_sprite_debug_drawText5x7(pixels, baseW, baseH, tx + 54, ty, buf, colLinkBroken);
        ty += step;
        mega_sprite_debug_formatInt(buf, sizeof(buf), diag.loops);
        mega_sprite_debug_drawText5x7(pixels, baseW, baseH, tx, ty, "LOOPS:", colBorder);
        mega_sprite_debug_drawText5x7(pixels, baseW, baseH, tx + 54, ty, buf, colLinkLoop);
        ty += step + 1;
        mega_sprite_debug_formatInt(buf, sizeof(buf), diag.firstBadIndex);
        mega_sprite_debug_drawText5x7(pixels, baseW, baseH, tx, ty, "FIRST BAD:", colBorder);
        mega_sprite_debug_drawText5x7(pixels, baseW, baseH, tx + 68, ty, buf, colLinkBroken);
        ty += step;
        mega_sprite_debug_formatInt(buf, sizeof(buf), diag.firstLoopStart);
        mega_sprite_debug_drawText5x7(pixels, baseW, baseH, tx, ty, "FIRST LOOP:", colBorder);
        mega_sprite_debug_drawText5x7(pixels, baseW, baseH, tx + 74, ty, buf, colLinkLoop);

        {
            int legendY = legendBoxY + 4;
            {
                int colGap = 6;
                int colW = (panelW - 8 - colGap) / 2;
                int leftX = tx;
                int rightX = tx + colW + colGap;
                int rowH = 9;

                mega_sprite_debug_drawText5x7(pixels, baseW, baseH, leftX, legendY, "SPRITE STATE", colBorder);
                mega_sprite_debug_drawText5x7(pixels, baseW, baseH, rightX, legendY, "LINK STATE", colBorder);
                legendY += rowH;

                {
                    static const char *spriteLabels[5] = { "RENDERED", "DROPPED", "SPRITE OVF", "TILE OVF", "MASKED" };
                    uint32_t spriteColors[5] = { colOutlineRendered, colOutlineDropped, colSpriteOverflow, colTileOverflow, colMask };
                    for (int i = 0; i < 5; ++i) {
                        int ly = legendY + (i * rowH);
                        int swY = ly + 1;
                        mega_sprite_debug_fillRect(pixels, baseW, baseH, leftX, swY, 8, 6, spriteColors[i]);
                        mega_sprite_debug_drawRect(pixels, baseW, baseH, leftX - 1, swY - 1, 10, 8, colBorder);
                        mega_sprite_debug_drawText5x7(pixels, baseW, baseH, leftX + 12, ly, spriteLabels[i], colBorder);
                    }
                }

                {
                    static const char *linkLabels[4] = { "NORMAL", "BROKEN", "LOOP", "END" };
                    uint32_t linkColors[4] = { colLinkNormal, colLinkBroken, colLinkLoop, colLinkEnd };
                    for (int i = 0; i < 4; ++i) {
                        int ly = legendY + (i * rowH);
                        int swY = ly + 1;
                        mega_sprite_debug_fillRect(pixels, baseW, baseH, rightX, swY, 8, 6, linkColors[i]);
                        mega_sprite_debug_drawRect(pixels, baseW, baseH, rightX - 1, swY - 1, 10, 8, colBorder);
                        mega_sprite_debug_drawText5x7(pixels, baseW, baseH, rightX + 12, ly, linkLabels[i], colBorder);
                    }
                }

                {
                    int orderRowY = legendY + (5 * rowH) + 2;
                    int orderRowX = tx;
                    int orderRowW = panelW - 8;
                    int orderLabelW = orderRowW / 2;
                    int gradX = orderRowX + orderLabelW;
                    int gradW = orderRowW - orderLabelW;
                    int gradY = orderRowY + 1;
                    int gradH = 6;
                    mega_sprite_debug_drawText5x7(pixels, baseW, baseH, orderRowX, orderRowY, "ORDER HINT", colBorder);
                    if (gradW > 2) {
                        for (int gx = 0; gx < gradW; ++gx) {
                            int gi = (gx * (MEGA_SPRITE_DEBUG_HIST_WIDTH - 1)) / (gradW - 1);
                            mega_sprite_debug_fillRect(pixels, baseW, baseH, gradX + gx, gradY, 1, gradH, mega_sprite_debug_state.grad[gi]);
                        }
                        mega_sprite_debug_drawRect(pixels, baseW, baseH, gradX - 1, gradY - 1, gradW + 2, gradH + 2, colBorder);
                    } else {
                        mega_sprite_debug_fillRect(pixels, baseW, baseH, gradX, gradY, gradW, gradH, colOrderHint);
                    }
                }
            }
        }

        ty = linksBoxY + linksBoxH - (step * 3) - 3;
        mega_sprite_debug_drawText5x7(pixels, baseW, baseH, tx, ty, "L LINKS", mega_sprite_debug_state.showLinks ? colLinkEnd : colOutlineDropped);
        ty += step;
        mega_sprite_debug_drawText5x7(pixels, baseW, baseH, tx, ty, "O ORDER", mega_sprite_debug_state.showOrderNumbers ? colLinkEnd : colOutlineDropped);
        ty += step;
        mega_sprite_debug_drawText5x7(pixels, baseW, baseH, tx, ty, "I ISSUES", mega_sprite_debug_state.highlightIssuesOnly ? colLinkEnd : colOutlineDropped);
    }

    SDL_UpdateTexture(mega_sprite_debug_state.texture, NULL, pixels, baseW * (int)sizeof(uint32_t));
    mega_sprite_debug_presentTexture(baseW, baseH, presentFrame);
    mega_sprite_debug_state.cachedValid = 1;
    mega_sprite_debug_state.lastHash = hash;
}

void
mega_sprite_debug_render(const e9k_debug_mega_sprite_state_t *st)
{
    if (st) {
        mega_sprite_debug_state.lastState = *st;
        mega_sprite_debug_state.hasLastState = 1;
    }
    if (!mega_sprite_debug_state.windowState.open) {
        return;
    }
    if (e9ui_windowCaptureStateRectChanged(&mega_sprite_debug_state.windowState,
                                           &e9ui->ctx)) {
        config_saveConfig();
    }
}

void
mega_sprite_debug_persistConfig(FILE *file)
{
    if (!file) {
        return;
    }
    e9ui_windowPersistStateRect(file,
                                "comp.mega_sprite_debug",
                                &mega_sprite_debug_state.windowState,
                                &e9ui->ctx);
}

int
mega_sprite_debug_loadConfigProperty(const char *prop, const char *value)
{
    if (!prop || !value) {
        return 0;
    }
    int intValue = 0;
    if (strcmp(prop, "win_x") == 0) {
        if (!mega_sprite_debug_parseInt(value, &intValue)) {
            return 0;
        }
        mega_sprite_debug_state.windowState.winX = intValue;
    } else if (strcmp(prop, "win_y") == 0) {
        if (!mega_sprite_debug_parseInt(value, &intValue)) {
            return 0;
        }
        mega_sprite_debug_state.windowState.winY = intValue;
    } else if (strcmp(prop, "win_w") == 0) {
        if (!mega_sprite_debug_parseInt(value, &intValue)) {
            return 0;
        }
        mega_sprite_debug_state.windowState.winW = intValue;
    } else if (strcmp(prop, "win_h") == 0) {
        if (!mega_sprite_debug_parseInt(value, &intValue)) {
            return 0;
        }
        mega_sprite_debug_state.windowState.winH = intValue;
    } else {
        return 0;
    }
    mega_sprite_debug_state.windowState.winHasSaved =
        e9ui_windowHasSavedPosition(mega_sprite_debug_state.windowState.winX,
                                    mega_sprite_debug_state.windowState.winY);
    return 1;
}
