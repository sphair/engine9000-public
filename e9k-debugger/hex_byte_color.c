/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "hex_byte_color.h"

static int hex_byte_color_enabled = 1;

#define HEX_BYTE_COLOR_ATLAS_COLS 16
#define HEX_BYTE_COLOR_ATLAS_ROWS 16
#define HEX_BYTE_COLOR_BATCH_MAX 64

typedef struct hex_byte_color_texture_entry
{
    SDL_Texture *texture;
    int w;
    int h;
} hex_byte_color_texture_entry_t;

typedef struct hex_byte_color_texture_cache
{
    SDL_Renderer *renderer;
    TTF_Font *font;
    hex_byte_color_texture_entry_t hex[256];
    hex_byte_color_texture_entry_t ascii[256];
} hex_byte_color_texture_cache_t;

typedef struct hex_byte_color_atlas
{
    SDL_Renderer *renderer;
    TTF_Font *font;
    SDL_Texture *texture;
    int cellW;
    int cellH;
    int textureW;
    int textureH;
    int glyphW[256];
    int glyphH[256];
} hex_byte_color_atlas_t;

static hex_byte_color_texture_cache_t hex_byte_color_textureCache = {0};
static hex_byte_color_atlas_t hex_byte_color_atlas = {0};

int
hex_byte_color_isEnabled(void)
{
    return hex_byte_color_enabled ? 1 : 0;
}

void
hex_byte_color_setEnabled(int enabled)
{
    hex_byte_color_enabled = enabled ? 1 : 0;
}

SDL_Color
hex_byte_color_get(uint8_t byte)
{
    static const SDL_Color nybbleColors[16] = {
        (SDL_Color){0xff, 0x76, 0xa9, 0xff},
        (SDL_Color){0xff, 0x78, 0x77, 0xff},
        (SDL_Color){0xff, 0x86, 0x2e, 0xff},
        (SDL_Color){0xf9, 0x91, 0x00, 0xff},
        (SDL_Color){0xec, 0x9b, 0x00, 0xff},
        (SDL_Color){0xc4, 0xb1, 0x00, 0xff},
        (SDL_Color){0x87, 0xc3, 0x34, 0xff},
        (SDL_Color){0x62, 0xc9, 0x58, 0xff},
        (SDL_Color){0x40, 0xcc, 0x6d, 0xff},
        (SDL_Color){0x00, 0xd0, 0x8c, 0xff},
        (SDL_Color){0x00, 0xd1, 0xbb, 0xff},
        (SDL_Color){0x00, 0xca, 0xea, 0xff},
        (SDL_Color){0x00, 0xbe, 0xff, 0xff},
        (SDL_Color){0x52, 0xb0, 0xff, 0xff},
        (SDL_Color){0xb6, 0x93, 0xff, 0xff},
        (SDL_Color){0xe9, 0x80, 0xe7, 0xff}
    };

    if (byte == 0x00u) {
        return (SDL_Color){0x80, 0x80, 0x80, 0xff};
    }
    if (byte == 0xffu) {
        return (SDL_Color){0xff, 0xff, 0xff, 0xff};
    }
    return nybbleColors[(byte >> 4) & 0x0fu];
}

static void
hex_byte_color_clearTextureEntry(hex_byte_color_texture_entry_t *entry)
{
    if (!entry) {
        return;
    }
    if (entry->texture) {
        SDL_DestroyTexture(entry->texture);
        entry->texture = NULL;
    }
    entry->w = 0;
    entry->h = 0;
}

static void
hex_byte_color_clearTextureCache(void)
{
    for (int i = 0; i < 256; ++i) {
        hex_byte_color_clearTextureEntry(&hex_byte_color_textureCache.hex[i]);
        hex_byte_color_clearTextureEntry(&hex_byte_color_textureCache.ascii[i]);
    }
    hex_byte_color_textureCache.renderer = NULL;
    hex_byte_color_textureCache.font = NULL;
}

static void
hex_byte_color_clearAtlas(void)
{
    if (hex_byte_color_atlas.texture) {
        SDL_DestroyTexture(hex_byte_color_atlas.texture);
    }
    memset(&hex_byte_color_atlas, 0, sizeof(hex_byte_color_atlas));
}

static SDL_Texture *
hex_byte_color_getAtlas(SDL_Renderer *renderer, TTF_Font *font)
{
    static const char hexChars[] = "0123456789ABCDEF";
    int cellW = 0;
    int cellH = 0;

    if (!renderer || !font) {
        return NULL;
    }
    if (hex_byte_color_atlas.texture &&
        hex_byte_color_atlas.renderer == renderer &&
        hex_byte_color_atlas.font == font) {
        return hex_byte_color_atlas.texture;
    }

    hex_byte_color_clearAtlas();
    if (TTF_SizeText(font, "FF", &cellW, &cellH) != 0 || cellW <= 0 || cellH <= 0) {
        return NULL;
    }

    int textureW = cellW * HEX_BYTE_COLOR_ATLAS_COLS;
    int textureH = cellH * HEX_BYTE_COLOR_ATLAS_ROWS;
    SDL_Texture *atlas = SDL_CreateTexture(renderer,
                                           SDL_PIXELFORMAT_RGBA8888,
                                           SDL_TEXTUREACCESS_TARGET,
                                           textureW,
                                           textureH);
    if (!atlas) {
        return NULL;
    }
    SDL_SetTextureBlendMode(atlas, SDL_BLENDMODE_BLEND);

    SDL_Texture *oldTarget = SDL_GetRenderTarget(renderer);
    SDL_BlendMode oldBlend = SDL_BLENDMODE_BLEND;
    SDL_GetRenderDrawBlendMode(renderer, &oldBlend);
    SDL_SetRenderTarget(renderer, atlas);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    for (int byte = 0; byte < 256; ++byte) {
        char text[3];
        text[0] = hexChars[(byte >> 4) & 0x0f];
        text[1] = hexChars[byte & 0x0f];
        text[2] = '\0';
        SDL_Surface *surface = TTF_RenderText_Blended(font, text, hex_byte_color_get((uint8_t)byte));
        if (!surface) {
            continue;
        }
        SDL_Texture *glyph = SDL_CreateTextureFromSurface(renderer, surface);
        if (glyph) {
            SDL_SetTextureBlendMode(glyph, SDL_BLENDMODE_BLEND);
            SDL_Rect dst = {
                (byte % HEX_BYTE_COLOR_ATLAS_COLS) * cellW,
                (byte / HEX_BYTE_COLOR_ATLAS_COLS) * cellH,
                surface->w,
                surface->h
            };
            SDL_RenderCopy(renderer, glyph, NULL, &dst);
            SDL_DestroyTexture(glyph);
            hex_byte_color_atlas.glyphW[byte] = surface->w;
            hex_byte_color_atlas.glyphH[byte] = surface->h;
        }
        SDL_FreeSurface(surface);
    }

    SDL_SetRenderTarget(renderer, oldTarget);
    SDL_SetRenderDrawBlendMode(renderer, oldBlend);

    hex_byte_color_atlas.renderer = renderer;
    hex_byte_color_atlas.font = font;
    hex_byte_color_atlas.texture = atlas;
    hex_byte_color_atlas.cellW = cellW;
    hex_byte_color_atlas.cellH = cellH;
    hex_byte_color_atlas.textureW = textureW;
    hex_byte_color_atlas.textureH = textureH;
    return atlas;
}

static hex_byte_color_texture_entry_t *
hex_byte_color_getTextureEntry(SDL_Renderer *renderer, TTF_Font *font, uint8_t byte, int ascii)
{
    static const char hexChars[] = "0123456789ABCDEF";
    char text[3];
    hex_byte_color_texture_entry_t *entry = NULL;

    if (!renderer || !font) {
        return NULL;
    }
    if (hex_byte_color_textureCache.renderer != renderer || hex_byte_color_textureCache.font != font) {
        hex_byte_color_clearTextureCache();
        hex_byte_color_textureCache.renderer = renderer;
        hex_byte_color_textureCache.font = font;
    }

    entry = ascii ? &hex_byte_color_textureCache.ascii[byte] : &hex_byte_color_textureCache.hex[byte];
    if (entry->texture) {
        return entry;
    }

    if (ascii) {
        text[0] = (byte >= 32u && byte <= 126u) ? (char)byte : '.';
        text[1] = '\0';
    } else {
        text[0] = hexChars[(byte >> 4) & 0x0fu];
        text[1] = hexChars[byte & 0x0fu];
        text[2] = '\0';
    }

    SDL_Surface *surface = TTF_RenderText_Blended(font, text, hex_byte_color_get(byte));
    if (!surface) {
        return NULL;
    }
    entry->texture = SDL_CreateTextureFromSurface(renderer, surface);
    entry->w = surface->w;
    entry->h = surface->h;
    SDL_FreeSurface(surface);

    if (!entry->texture) {
        entry->w = 0;
        entry->h = 0;
        return NULL;
    }
    return entry;
}

static void
hex_byte_color_drawTextureEntry(SDL_Renderer *renderer,
                                hex_byte_color_texture_entry_t *entry,
                                int x,
                                int y)
{
    if (!renderer || !entry || !entry->texture) {
        return;
    }
    SDL_Rect dst = { x, y, entry->w, entry->h };
    SDL_RenderCopy(renderer, entry->texture, NULL, &dst);
}

void
hex_byte_color_drawHexByte(SDL_Renderer *renderer, TTF_Font *font, uint8_t byte, int x, int y)
{
    hex_byte_color_drawTextureEntry(renderer,
                                    hex_byte_color_getTextureEntry(renderer, font, byte, 0),
                                    x,
                                    y);
}

int
hex_byte_color_drawHexByteRow(SDL_Renderer *renderer,
                              TTF_Font *font,
                              const uint8_t *bytes,
                              int byteCount,
                              int x,
                              int y,
                              int columnWidth)
{
    if (!renderer || !font || !bytes || byteCount <= 0 || columnWidth <= 0) {
        return 0;
    }
    if (byteCount > HEX_BYTE_COLOR_BATCH_MAX) {
        return 0;
    }
    SDL_Texture *atlas = hex_byte_color_getAtlas(renderer, font);
    if (!atlas || hex_byte_color_atlas.textureW <= 0 || hex_byte_color_atlas.textureH <= 0) {
        return 0;
    }

    SDL_Vertex vertices[HEX_BYTE_COLOR_BATCH_MAX * 4];
    int indices[HEX_BYTE_COLOR_BATCH_MAX * 6];
    SDL_Color white = {255, 255, 255, 255};

    for (int i = 0; i < byteCount; ++i) {
        int byte = bytes[i];
        int col = byte % HEX_BYTE_COLOR_ATLAS_COLS;
        int row = byte / HEX_BYTE_COLOR_ATLAS_COLS;
        float sx0 = (float)(col * hex_byte_color_atlas.cellW) / (float)hex_byte_color_atlas.textureW;
        float sy0 = (float)(row * hex_byte_color_atlas.cellH) / (float)hex_byte_color_atlas.textureH;
        float sx1 = (float)(col * hex_byte_color_atlas.cellW + hex_byte_color_atlas.glyphW[byte]) /
                    (float)hex_byte_color_atlas.textureW;
        float sy1 = (float)(row * hex_byte_color_atlas.cellH + hex_byte_color_atlas.glyphH[byte]) /
                    (float)hex_byte_color_atlas.textureH;
        float dx0 = (float)(x + i * 3 * columnWidth);
        float dy0 = (float)y;
        float dx1 = dx0 + (float)hex_byte_color_atlas.glyphW[byte];
        float dy1 = dy0 + (float)hex_byte_color_atlas.glyphH[byte];
        int vi = i * 4;
        int ii = i * 6;

        vertices[vi + 0] = (SDL_Vertex){ {dx0, dy0}, white, {sx0, sy0} };
        vertices[vi + 1] = (SDL_Vertex){ {dx1, dy0}, white, {sx1, sy0} };
        vertices[vi + 2] = (SDL_Vertex){ {dx1, dy1}, white, {sx1, sy1} };
        vertices[vi + 3] = (SDL_Vertex){ {dx0, dy1}, white, {sx0, sy1} };

        indices[ii + 0] = vi + 0;
        indices[ii + 1] = vi + 1;
        indices[ii + 2] = vi + 2;
        indices[ii + 3] = vi + 0;
        indices[ii + 4] = vi + 2;
        indices[ii + 5] = vi + 3;
    }

    if (SDL_RenderGeometry(renderer, atlas, vertices, byteCount * 4, indices, byteCount * 6) != 0) {
        return 0;
    }
    return 1;
}

void
hex_byte_color_drawAsciiByte(SDL_Renderer *renderer, TTF_Font *font, uint8_t byte, int x, int y)
{
    hex_byte_color_drawTextureEntry(renderer,
                                    hex_byte_color_getTextureEntry(renderer, font, byte, 1),
                                    x,
                                    y);
}
