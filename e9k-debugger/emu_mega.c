/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "debugger_input_bindings.h"
#include "emu_mega.h"
#include "e9ui.h"
#include "libretro.h"
#include "mega_memview.h"
#include "mega_palette_debug.h"
#include "mega_sprite_debug.h"
#include "target.h"

typedef struct
{
    SDL_Texture *texture;
    uint32_t *pixels;
    size_t pixelsCap;
    int texW;
    int texH;
    uint32_t lastHash;
    int valid;
    int lastScreenW;
    int lastScreenH;
    int lastCropTop;
    int lastCropBottom;
    int lastCropLeft;
    int lastCropRight;
    int lastLineCount;
    int lastSpriteLimit;
    int lastTileLimit;
    int lastFrameSpriteUsed;
    int lastFrameSpriteMax;
    uint32_t *grad;
    size_t gradCap;
    int gradW;
    SDL_Renderer *renderer;
} emu_mega_overlay_cache_t;

typedef enum
{
    emu_mega_histogramModeOff = 0,
    emu_mega_histogramModeSprites = 1,
    emu_mega_histogramModeTiles = 2
} emu_mega_histogram_mode_t;

static emu_mega_overlay_cache_t emu_mega_overlayCache = {0};
static emu_mega_histogram_mode_t emu_mega_histogramMode = emu_mega_histogramModeOff;
static e9ui_component_t *emu_mega_histogramBtn = NULL;
static int emu_mega_spriteShadowReady = 0;
static e9k_debug_mega_sprite_state_t emu_mega_spriteShadow;

void
emu_mega_setSpriteState(const e9k_debug_mega_sprite_state_t *state, int ready)
{
    if (ready && state) {
        emu_mega_spriteShadow = *state;
        emu_mega_spriteShadowReady = 1;
    } else {
        emu_mega_spriteShadowReady = 0;
    }
}

static uint32_t
emu_mega_argb(Uint8 a, Uint8 r, Uint8 g, Uint8 b)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void
emu_mega_hueToRgb(float h, Uint8 *r, Uint8 *g, Uint8 *b)
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
    *r = (Uint8)(rr * 255.0f);
    *g = (Uint8)(gg * 255.0f);
    *b = (Uint8)(bb * 255.0f);
}

static void
emu_mega_fillRectPixels(uint32_t *pixels, int width, int height, int x, int y, int w, int h, uint32_t color)
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

static uint32_t
emu_mega_stateHash(const e9k_debug_mega_sprite_state_t *st)
{
    if (!st) {
        return 0u;
    }
    int lineCount = st->lineCount;
    if (lineCount < 0) {
        lineCount = 0;
    }
    if (lineCount > E9K_DEBUG_MEGA_MAX_LINES) {
        lineCount = E9K_DEBUG_MEGA_MAX_LINES;
    }
    uint32_t h = 2166136261u;
    for (int i = 0; i < lineCount; ++i) {
        h ^= st->spritesPerLine[i];
        h *= 16777619u;
        h ^= st->tilesPerLine[i];
        h *= 16777619u;
        h ^= st->lineFlags[i];
        h *= 16777619u;
    }
    h ^= (uint32_t)st->frameSpriteUsed;
    h *= 16777619u;
    h ^= (uint32_t)st->frameSpriteMax;
    h *= 16777619u;
    return h;
}

static void
emu_mega_toggleHistogram(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    e9ui_component_t *comp = (e9ui_component_t *)user;
    if (!comp || !comp->state) {
        return;
    }
    if (emu_mega_histogramMode == emu_mega_histogramModeOff) {
        emu_mega_histogramMode = emu_mega_histogramModeSprites;
    } else if (emu_mega_histogramMode == emu_mega_histogramModeSprites) {
        emu_mega_histogramMode = emu_mega_histogramModeTiles;
    } else {
        emu_mega_histogramMode = emu_mega_histogramModeOff;
    }
    if (emu_mega_histogramBtn) {
        const char *label = "Histogram";
        if (emu_mega_histogramMode == emu_mega_histogramModeSprites) {
            label = "Histogram: Spr";
        } else if (emu_mega_histogramMode == emu_mega_histogramModeTiles) {
            label = "Histogram: Tile";
        }
        e9ui_button_setLabel(emu_mega_histogramBtn, label);
    }
}

static void
emu_mega_toggleSpriteDebug(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    mega_sprite_debug_toggle();
}

static void
emu_mega_toggleMemview(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    mega_memview_toggle();
}

static void
emu_mega_togglePaletteDebug(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    mega_palette_debug_toggle();
}

static void
emu_mega_renderHistogram(SDL_Renderer *renderer, const SDL_Rect *dst, const e9k_debug_mega_sprite_state_t *st)
{
    if (!renderer || !dst || !st) {
        return;
    }
    int screenW = st->screenW > 0 ? st->screenW : 320;
    int screenH = st->screenH > 0 ? st->screenH : 240;
    if (screenW <= 0 || screenH <= 0) {
        return;
    }

    int lineCount = st->lineCount;
    if (lineCount < 0) {
        lineCount = 0;
    }
    if (lineCount > E9K_DEBUG_MEGA_MAX_LINES) {
        lineCount = E9K_DEBUG_MEGA_MAX_LINES;
    }
    if (lineCount > screenH) {
        lineCount = screenH;
    }

    int spriteLimit = st->spriteLimitPerLine;
    int tileLimit = st->tileLimitPerLine;
    if (spriteLimit <= 0) {
        spriteLimit = (screenW >= 320) ? 20 : 16;
    }
    if (tileLimit <= 0) {
        tileLimit = (screenW >= 320) ? 40 : 32;
    }

    uint32_t hash = emu_mega_stateHash(st);
    int paramsChanged = 0;
    if (emu_mega_overlayCache.renderer != renderer) {
        if (emu_mega_overlayCache.texture) {
            SDL_DestroyTexture(emu_mega_overlayCache.texture);
            emu_mega_overlayCache.texture = NULL;
        }
        emu_mega_overlayCache.renderer = renderer;
        emu_mega_overlayCache.valid = 0;
    }
    if (emu_mega_overlayCache.lastScreenW != screenW ||
        emu_mega_overlayCache.lastScreenH != screenH ||
        emu_mega_overlayCache.lastCropTop != st->cropTop ||
        emu_mega_overlayCache.lastCropBottom != st->cropBottom ||
        emu_mega_overlayCache.lastCropLeft != st->cropLeft ||
        emu_mega_overlayCache.lastCropRight != st->cropRight ||
        emu_mega_overlayCache.lastLineCount != lineCount ||
        emu_mega_overlayCache.lastSpriteLimit != spriteLimit ||
        emu_mega_overlayCache.lastTileLimit != tileLimit ||
        emu_mega_overlayCache.lastFrameSpriteUsed != st->frameSpriteUsed ||
        emu_mega_overlayCache.lastFrameSpriteMax != st->frameSpriteMax ||
        emu_mega_overlayCache.texW != screenW ||
        emu_mega_overlayCache.texH != screenH) {
        paramsChanged = 1;
    }
    if (emu_mega_overlayCache.valid && !paramsChanged && emu_mega_overlayCache.lastHash == hash) {
        SDL_SetTextureBlendMode(emu_mega_overlayCache.texture, SDL_BLENDMODE_BLEND);
        SDL_RenderCopy(renderer, emu_mega_overlayCache.texture, NULL, dst);
        return;
    }
    if (!emu_mega_overlayCache.grad || emu_mega_overlayCache.gradW != screenW) {
        size_t needed = (size_t)(screenW > 0 ? screenW : 1);
        if (needed > emu_mega_overlayCache.gradCap) {
            uint32_t *next = (uint32_t *)realloc(emu_mega_overlayCache.grad, needed * sizeof(uint32_t));
            if (!next) {
                return;
            }
            emu_mega_overlayCache.grad = next;
            emu_mega_overlayCache.gradCap = needed;
        }
        emu_mega_overlayCache.gradW = screenW;
        int denomX = (screenW > 1) ? (screenW - 1) : 1;
        for (int dx = 0; dx < screenW; ++dx) {
            float t = (float)dx / (float)denomX;
            float h = (1.0f / 3.0f) * (1.0f - t);
            Uint8 rr;
            Uint8 gg;
            Uint8 bb;
            emu_mega_hueToRgb(h, &rr, &gg, &bb);
            emu_mega_overlayCache.grad[dx] = emu_mega_argb(160, rr, gg, bb);
        }
    }

    if (!emu_mega_overlayCache.texture || emu_mega_overlayCache.texW != screenW || emu_mega_overlayCache.texH != screenH) {
        if (emu_mega_overlayCache.texture) {
            SDL_DestroyTexture(emu_mega_overlayCache.texture);
        }
        emu_mega_overlayCache.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                                          SDL_TEXTUREACCESS_STREAMING, screenW, screenH);
        emu_mega_overlayCache.texW = screenW;
        emu_mega_overlayCache.texH = screenH;
        if (!emu_mega_overlayCache.texture) {
            return;
        }
    }

    size_t pixelCount = (size_t)screenW * (size_t)screenH;
    if (pixelCount > emu_mega_overlayCache.pixelsCap) {
        uint32_t *next = (uint32_t *)realloc(emu_mega_overlayCache.pixels, pixelCount * sizeof(uint32_t));
        if (!next) {
            return;
        }
        emu_mega_overlayCache.pixels = next;
        emu_mega_overlayCache.pixelsCap = pixelCount;
    }
    uint32_t *pixels = emu_mega_overlayCache.pixels;
    memset(pixels, 0, pixelCount * sizeof(uint32_t));

    int modeSprites = (emu_mega_histogramMode == emu_mega_histogramModeSprites) ? 1 : 0;
    int maxMetric = 0;
    for (int y = 0; y < lineCount; ++y) {
        int spriteCount = (int)st->spritesPerLine[y];
        int tileCount = (int)st->tilesPerLine[y];
        uint8_t flags = st->lineFlags[y];

        int value = modeSprites ? spriteCount : tileCount;
        if (value > maxMetric) {
            maxMetric = value;
        }
        int limit = modeSprites ? spriteLimit : tileLimit;
        int len = (value * screenW) / limit;
        if (len > screenW) {
            len = screenW;
        }
        if (len < 0) {
            len = 0;
        }
        int overflow = modeSprites
                       ? ((flags & E9K_DEBUG_MEGA_LINEFLAG_SPRITE_OVERFLOW) ? 1 : 0)
                       : ((flags & E9K_DEBUG_MEGA_LINEFLAG_TILE_OVERFLOW) ? 1 : 0);
        uint32_t *row = pixels + (size_t)y * (size_t)screenW;
        for (int x = 0; x < len; ++x) {
            row[x] = overflow ? emu_mega_argb(200, 220, 0, 0) : emu_mega_overlayCache.grad[x];
        }
    }

    {
        int barMargin = 4;
        int barWidth = screenW - barMargin * 2;
        if (barWidth < 1) {
            barWidth = 1;
        }
        int topSpace = st->cropTop > 0 ? st->cropTop : 0;
        int barHeight = topSpace > 3 ? (topSpace - 2) : 6;
        if (barHeight < 4) {
            barHeight = 4;
        }
        if (barHeight > 12) {
            barHeight = 12;
        }
        int barY = 1;
        int barX = barMargin;
        int frameMax = st->frameSpriteMax > 0 ? st->frameSpriteMax : 80;
        int frameUsed = st->frameSpriteUsed;
        if (frameUsed < 0) {
            frameUsed = 0;
        }
        int fillWidth = (frameUsed * barWidth) / frameMax;
        if (fillWidth > barWidth) {
            fillWidth = barWidth;
        }
        if (fillWidth < 0) {
            fillWidth = 0;
        }
        uint32_t fillColor = emu_mega_argb(200, 64, 180, 64);
        if (frameUsed * 100 >= frameMax * 95) {
            fillColor = emu_mega_argb(220, 220, 0, 0);
        } else if (frameUsed * 100 >= frameMax * 75) {
            fillColor = emu_mega_argb(220, 220, 180, 0);
        }
        emu_mega_fillRectPixels(pixels, screenW, screenH, barX, barY, barWidth, barHeight, emu_mega_argb(140, 24, 24, 24));
        emu_mega_fillRectPixels(pixels, screenW, screenH, barX, barY, fillWidth, barHeight, fillColor);
        emu_mega_fillRectPixels(pixels, screenW, screenH, barX - 1, barY - 1, barWidth + 2, 1, emu_mega_argb(200, 255, 255, 255));
        emu_mega_fillRectPixels(pixels, screenW, screenH, barX - 1, barY + barHeight, barWidth + 2, 1, emu_mega_argb(200, 255, 255, 255));
        emu_mega_fillRectPixels(pixels, screenW, screenH, barX - 1, barY - 1, 1, barHeight + 2, emu_mega_argb(200, 255, 255, 255));
        emu_mega_fillRectPixels(pixels, screenW, screenH, barX + barWidth, barY - 1, 1, barHeight + 2, emu_mega_argb(200, 255, 255, 255));

    }

    SDL_UpdateTexture(emu_mega_overlayCache.texture, NULL, pixels, screenW * (int)sizeof(uint32_t));
    SDL_SetTextureBlendMode(emu_mega_overlayCache.texture, SDL_BLENDMODE_BLEND);
    SDL_RenderCopy(renderer, emu_mega_overlayCache.texture, NULL, dst);
    emu_mega_overlayCache.lastHash = hash;
    emu_mega_overlayCache.valid = 1;
    emu_mega_overlayCache.lastScreenW = screenW;
    emu_mega_overlayCache.lastScreenH = screenH;
    emu_mega_overlayCache.lastCropTop = st->cropTop;
    emu_mega_overlayCache.lastCropBottom = st->cropBottom;
    emu_mega_overlayCache.lastCropLeft = st->cropLeft;
    emu_mega_overlayCache.lastCropRight = st->cropRight;
    emu_mega_overlayCache.lastLineCount = lineCount;
    emu_mega_overlayCache.lastSpriteLimit = spriteLimit;
    emu_mega_overlayCache.lastTileLimit = tileLimit;
    emu_mega_overlayCache.lastFrameSpriteUsed = st->frameSpriteUsed;
    emu_mega_overlayCache.lastFrameSpriteMax = st->frameSpriteMax;
}

static int
emu_mega_mapKeyToJoypad(SDL_Keycode key, unsigned *id)
{
    return debugger_input_bindings_mapKeyToJoypad(TARGET_MEGADRIVE,
                                                  (target && target->coreOptionGetValue)
                                                      ? target->coreOptionGetValue
                                                      : NULL,
                                                  key,
                                                  id);
}

static uint16_t
emu_mega_translateModifiers(SDL_Keymod mod)
{
    uint16_t out = 0;
    if (mod & KMOD_SHIFT) {
        out |= RETROKMOD_SHIFT;
    }
    if (mod & KMOD_CTRL) {
        out |= RETROKMOD_CTRL;
    }
    if (mod & KMOD_ALT) {
        out |= RETROKMOD_ALT;
    }
    if (mod & KMOD_GUI) {
        out |= RETROKMOD_META;
    }
    if (mod & KMOD_NUM) {
        out |= RETROKMOD_NUMLOCK;
    }
    if (mod & KMOD_CAPS) {
        out |= RETROKMOD_CAPSLOCK;
    }
    return out;
}

static uint32_t
emu_mega_translateCharacter(SDL_Keycode key, SDL_Keymod mod)
{
    if (key < 32 || key >= 127) {
        return 0;
    }
    int shift = (mod & KMOD_SHIFT) ? 1 : 0;
    int caps = (mod & KMOD_CAPS) ? 1 : 0;
    if (key >= 'a' && key <= 'z') {
        if (shift ^ caps) {
            return (uint32_t)toupper((int)key);
        }
        return (uint32_t)key;
    }
    if (!shift) {
        return (uint32_t)key;
    }
    switch (key) {
    case '1': return '!';
    case '2': return '@';
    case '3': return '#';
    case '4': return '$';
    case '5': return '%';
    case '6': return '^';
    case '7': return '&';
    case '8': return '*';
    case '9': return '(';
    case '0': return ')';
    case '-': return '_';
    case '=': return '+';
    case '[': return '{';
    case ']': return '}';
    case '\\': return '|';
    case ';': return ':';
    case '\'': return '"';
    case ',': return '<';
    case '.': return '>';
    case '/': return '?';
    case '`': return '~';
    default:
        break;
    }
    return (uint32_t)key;
}

static unsigned
emu_mega_translateKey(SDL_Keycode key)
{
    if (key >= 32 && key < 127) {
        if (key >= 'A' && key <= 'Z') {
            return (unsigned)tolower((int)key);
        }
        return (unsigned)key;
    }
    switch (key) {
    case SDLK_BACKSPACE: return RETROK_BACKSPACE;
    case SDLK_TAB: return RETROK_TAB;
    case SDLK_RETURN: return RETROK_RETURN;
    case SDLK_ESCAPE: return RETROK_ESCAPE;
    case SDLK_DELETE: return RETROK_DELETE;
    case SDLK_INSERT: return RETROK_INSERT;
    case SDLK_HOME: return RETROK_HOME;
    case SDLK_END: return RETROK_END;
    case SDLK_PAGEUP: return RETROK_PAGEUP;
    case SDLK_PAGEDOWN: return RETROK_PAGEDOWN;
    case SDLK_UP: return RETROK_UP;
    case SDLK_DOWN: return RETROK_DOWN;
    case SDLK_LEFT: return RETROK_LEFT;
    case SDLK_RIGHT: return RETROK_RIGHT;
    case SDLK_F1: return RETROK_F1;
    case SDLK_F2: return RETROK_F2;
    case SDLK_F3: return RETROK_F3;
    case SDLK_F4: return RETROK_F4;
    case SDLK_F5: return RETROK_F5;
    case SDLK_F6: return RETROK_F6;
    case SDLK_F7: return RETROK_F7;
    case SDLK_F8: return RETROK_F8;
    case SDLK_F9: return RETROK_F9;
    case SDLK_F10: return RETROK_F10;
    case SDLK_F11: return RETROK_F11;
    case SDLK_F12: return RETROK_F12;
    case SDLK_LSHIFT: return RETROK_LSHIFT;
    case SDLK_RSHIFT: return RETROK_RSHIFT;
    case SDLK_LCTRL: return RETROK_LCTRL;
    case SDLK_RCTRL: return RETROK_RCTRL;
    case SDLK_LALT: return RETROK_LALT;
    case SDLK_RALT: return RETROK_RALT;
    case SDLK_LGUI: return RETROK_LMETA;
    case SDLK_RGUI: return RETROK_RMETA;
    default:
        break;
    }
    return RETROK_UNKNOWN;
}

static void
emu_mega_createOverlays(e9ui_component_t *comp, e9ui_component_t *button_stack)
{
    e9ui_component_t *btn = e9ui_button_make("Histogram", emu_mega_toggleHistogram, comp);
    if (btn) {
        emu_mega_histogramBtn = btn;
        e9ui_button_setMini(btn, 1);
        e9ui_setFocusTarget(btn, comp);
        void *histogramBtnMeta = alloc_strdup("histogram");
        e9ui_child_add(button_stack, btn, histogramBtnMeta);
    }

    e9ui_component_t *btnDebug = e9ui_button_make("Sprite Debug", emu_mega_toggleSpriteDebug, comp);
    if (btnDebug) {
        e9ui_button_setMini(btnDebug, 1);
        e9ui_setFocusTarget(btnDebug, comp);
        void *spriteDebugBtnMeta = alloc_strdup("mega_sprite_debug");
        e9ui_child_add(button_stack, btnDebug, spriteDebugBtnMeta);
    }

    e9ui_component_t *btnMemview = e9ui_button_make("RAM/ROMS", emu_mega_toggleMemview, comp);
    if (btnMemview) {
        e9ui_button_setMini(btnMemview, 1);
        e9ui_setFocusTarget(btnMemview, comp);
        void *memviewBtnMeta = alloc_strdup("mega_memview");
        e9ui_child_add(button_stack, btnMemview, memviewBtnMeta);
    }

    e9ui_component_t *btnPalette = e9ui_button_make("Palette", emu_mega_togglePaletteDebug, comp);
    if (btnPalette) {
        e9ui_button_setMini(btnPalette, 1);
        e9ui_setFocusTarget(btnPalette, comp);
        void *paletteBtnMeta = alloc_strdup("mega_palette_debug");
        e9ui_child_add(button_stack, btnPalette, paletteBtnMeta);
    }
}

static void
emu_mega_render(e9ui_context_t *ctx, SDL_Rect *dst)
{
    if (emu_mega_histogramMode != emu_mega_histogramModeOff && emu_mega_spriteShadowReady) {
        emu_mega_renderHistogram(ctx->renderer, dst, &emu_mega_spriteShadow);
    }
    if (mega_sprite_debug_is_open() && emu_mega_spriteShadowReady) {
        mega_sprite_debug_render(&emu_mega_spriteShadow);
    }
    if (mega_memview_isOpen()) {
        mega_memview_render();
    }
}

const emu_system_iface_t emu_mega_iface = {
    .translateCharacter = emu_mega_translateCharacter,
    .translateModifiers = emu_mega_translateModifiers,
    .translateKey = emu_mega_translateKey,
    .mapKeyToJoypad = emu_mega_mapKeyToJoypad,
    .createOverlays = emu_mega_createOverlays,
    .render = emu_mega_render,
    .destroy = NULL,
};
