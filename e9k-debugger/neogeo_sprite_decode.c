/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <string.h>

#include "neogeo_sprite_decode.h"

#define NEOGEO_SPRITE_DECODE_SCB2_WORD_OFFSET 0x8000u
#define NEOGEO_SPRITE_DECODE_SCB3_WORD_OFFSET 0x8200u
#define NEOGEO_SPRITE_DECODE_SCB4_WORD_OFFSET 0x8400u
#define NEOGEO_SPRITE_DECODE_SCB3_CHAIN_FLAG 0x40u
#define NEOGEO_SPRITE_DECODE_SCB3_ROW_MASK 0x3fu
#define NEOGEO_SPRITE_DECODE_SCB3_YPOS_SHIFT 7u
#define NEOGEO_SPRITE_DECODE_SCB4_XPOS_SHIFT 7u
#define NEOGEO_SPRITE_DECODE_SCB2_VSHRINK_MASK 0x00ffu
#define NEOGEO_SPRITE_DECODE_SCB2_HSHRINK_MASK 0x0fu
#define NEOGEO_SPRITE_DECODE_SCB2_HSHRINK_SHIFT 8u
#define NEOGEO_SPRITE_DECODE_SPRITE_VRAM_WORDS_PER_SPRITE 64u
#define NEOGEO_SPRITE_DECODE_SPRITE_TILE_ODD_WORD_OFFSET 1u
#define NEOGEO_SPRITE_DECODE_SPRITE_ANIM_MASK 0x0cu

/* Horizontal shrink table translated from Mame neogeo_spr.cpp and verified on real hardware.
   license:BSD-3-Clause
   copyright-holders:Bryan McPhail,Ernesto Corvi,Andrew Prime,Zsolt Vasvari */
static const uint8_t neogeo_sprite_decode_lutHshrink[0x10][0x10] = {
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

unsigned
neogeo_sprite_decode_countShrinkWidth(unsigned hshrink)
{
    unsigned h = hshrink & NEOGEO_SPRITE_DECODE_SCB2_HSHRINK_MASK;
    unsigned width = 0u;

    for (unsigned pixel = 0u; pixel < 16u; ++pixel) {
        width += (unsigned)neogeo_sprite_decode_lutHshrink[h][pixel];
    }
    return width;
}

int
neogeo_sprite_decode_hshrinkPixelVisible(unsigned hshrink, unsigned pixel)
{
    unsigned h = hshrink & NEOGEO_SPRITE_DECODE_SCB2_HSHRINK_MASK;
    unsigned p = pixel & 0x0fu;

    return neogeo_sprite_decode_lutHshrink[h][p] ? 1 : 0;
}

int
neogeo_sprite_decode_spriteHasAnimBits(const uint16_t *vram,
                                       size_t vramWords,
                                       unsigned spriteIndex,
                                       unsigned sprsize)
{
    unsigned baseWordOffset = spriteIndex * NEOGEO_SPRITE_DECODE_SPRITE_VRAM_WORDS_PER_SPRITE;
    unsigned tileRows = sprsize;
    unsigned maxTileRows = NEOGEO_SPRITE_DECODE_SPRITE_VRAM_WORDS_PER_SPRITE / 2u;

    if (!vram || baseWordOffset >= vramWords) {
        return 0;
    }
    if (tileRows == 0u) {
        return 0;
    }
    if (tileRows > maxTileRows) {
        tileRows = maxTileRows;
    }
    for (unsigned tileRow = 0u; tileRow < tileRows; ++tileRow) {
        unsigned oddWordOffset = baseWordOffset + tileRow * 2u +
            NEOGEO_SPRITE_DECODE_SPRITE_TILE_ODD_WORD_OFFSET;
        if (oddWordOffset >= vramWords) {
            break;
        }
        if (vram[oddWordOffset] & NEOGEO_SPRITE_DECODE_SPRITE_ANIM_MASK) {
            return 1;
        }
    }
    return 0;
}

unsigned
neogeo_sprite_decode_visibleLineSpriteRow(const e9k_debug_sprite_state_t *st,
                                          const neogeo_sprite_decode_sprite_t *sprite,
                                          int visibleLine)
{
    return (unsigned)(((visibleLine + e9k_debug_geo_spriteVisibleLineOffset(st)) -
                       (int)(NEOGEO_SPRITE_DECODE_COORD_SIZE - (int)sprite->ypos)) &
                      NEOGEO_SPRITE_DECODE_WRAP_MASK);
}

int
neogeo_sprite_decode_decodeSprites(const e9k_debug_sprite_state_t *st,
                                   neogeo_sprite_decode_sprite_t *decodedSprites,
                                   neogeo_sprite_decode_line_sprites_t *lineSprites,
                                   uint8_t *chainHasAnimBits,
                                   unsigned sprlimit)
{
    if (!st || !st->vram || !decodedSprites) {
        return 0;
    }
    if (st->vram_words <= (NEOGEO_SPRITE_DECODE_SCB4_WORD_OFFSET +
                           NEOGEO_SPRITE_DECODE_MAX_SPRITES)) {
        return 0;
    }

    const uint16_t *vram = st->vram;
    const uint16_t *scb2 = vram + NEOGEO_SPRITE_DECODE_SCB2_WORD_OFFSET;
    const uint16_t *scb3 = vram + NEOGEO_SPRITE_DECODE_SCB3_WORD_OFFSET;
    const uint16_t *scb4 = vram + NEOGEO_SPRITE_DECODE_SCB4_WORD_OFFSET;
    unsigned xpos = 0u;
    unsigned ypos = 0u;
    unsigned sprsize = 0u;
    unsigned hshrink = NEOGEO_SPRITE_DECODE_SCB2_HSHRINK_MASK;
    unsigned vshrink = NEOGEO_SPRITE_DECODE_SCB2_VSHRINK_MASK;
    unsigned chainRootIndex = 0u;
    unsigned chainOffset = 0u;
    int lineOffset = e9k_debug_geo_spriteVisibleLineOffset(st);

    if (sprlimit == 0u || sprlimit > NEOGEO_SPRITE_DECODE_SPRITES_PER_LINE_MAX) {
        sprlimit = NEOGEO_SPRITE_DECODE_SPRITES_PER_LINE_MAX;
    }

    memset(decodedSprites, 0, sizeof(neogeo_sprite_decode_sprite_t) * NEOGEO_SPRITE_DECODE_MAX_SPRITES);
    if (lineSprites) {
        memset(lineSprites, 0, sizeof(neogeo_sprite_decode_line_sprites_t) * NEOGEO_SPRITE_DECODE_LINE_COUNT);
    }
    if (chainHasAnimBits) {
        memset(chainHasAnimBits, 0, NEOGEO_SPRITE_DECODE_MAX_SPRITES);
    }

    for (unsigned i = 0u; i < NEOGEO_SPRITE_DECODE_MAX_SPRITES; ++i) {
        uint16_t scb3w = scb3[i];
        uint16_t scb2w = scb2[i];
        uint16_t scb4w = scb4[i];

        decodedSprites[i].scb2 = scb2w;
        decodedSprites[i].scb3 = scb3w;
        decodedSprites[i].scb4 = scb4w;

        if (i != 0u && (scb3w & NEOGEO_SPRITE_DECODE_SCB3_CHAIN_FLAG)) {
            xpos = (unsigned)((xpos + (hshrink + 1u)) & NEOGEO_SPRITE_DECODE_WRAP_MASK);
            chainOffset++;
        } else {
            chainRootIndex = i;
            chainOffset = 0u;
            xpos = (unsigned)((scb4w >> NEOGEO_SPRITE_DECODE_SCB4_XPOS_SHIFT) &
                              NEOGEO_SPRITE_DECODE_WRAP_MASK);
            ypos = (unsigned)((scb3w >> NEOGEO_SPRITE_DECODE_SCB3_YPOS_SHIFT) &
                              NEOGEO_SPRITE_DECODE_WRAP_MASK);
            sprsize = (unsigned)(scb3w & NEOGEO_SPRITE_DECODE_SCB3_ROW_MASK);
            vshrink = (unsigned)(scb2w & NEOGEO_SPRITE_DECODE_SCB2_VSHRINK_MASK);
        }
        hshrink = (unsigned)((scb2w >> NEOGEO_SPRITE_DECODE_SCB2_HSHRINK_SHIFT) &
                             NEOGEO_SPRITE_DECODE_SCB2_HSHRINK_MASK);

        decodedSprites[i].xpos = xpos;
        decodedSprites[i].ypos = ypos;
        decodedSprites[i].sprsize = sprsize;
        decodedSprites[i].hshrink = hshrink;
        decodedSprites[i].vshrink = vshrink;
        decodedSprites[i].chainRootIndex = chainRootIndex;
        decodedSprites[i].chainOffset = chainOffset;
        decodedSprites[i].width = (int)neogeo_sprite_decode_countShrinkWidth(hshrink);

        unsigned oddWordOffset = i * NEOGEO_SPRITE_DECODE_SPRITE_VRAM_WORDS_PER_SPRITE +
            NEOGEO_SPRITE_DECODE_SPRITE_TILE_ODD_WORD_OFFSET;
        if (oddWordOffset < st->vram_words) {
            decodedSprites[i].hasAnimBits =
                neogeo_sprite_decode_spriteHasAnimBits(vram, st->vram_words, i, sprsize);
            decodedSprites[i].paletteBank =
                (unsigned)((vram[oddWordOffset] >> NEOGEO_SPRITE_DECODE_SPRITE_PALETTE_SHIFT) &
                           NEOGEO_SPRITE_DECODE_SPRITE_PALETTE_MASK);
        }
        if (chainHasAnimBits &&
            decodedSprites[i].hasAnimBits &&
            chainRootIndex < NEOGEO_SPRITE_DECODE_MAX_SPRITES) {
            chainHasAnimBits[chainRootIndex] = 1u;
        }

        unsigned totalH = sprsize << 4;
        if (!lineSprites || totalH == 0u) {
            continue;
        }
        for (unsigned row = 0u; row < totalH && row < NEOGEO_SPRITE_DECODE_LINE_COUNT; ++row) {
            unsigned line = (unsigned)((NEOGEO_SPRITE_DECODE_COORD_SIZE - ypos + row - lineOffset) &
                                       NEOGEO_SPRITE_DECODE_WRAP_MASK);
            neogeo_sprite_decode_line_sprites_t *lineList = &lineSprites[line];

            if (lineList->count >= sprlimit) {
                continue;
            }
            lineList->indices[lineList->count] = (uint16_t)i;
            lineList->count++;
        }
    }
    return 1;
}
