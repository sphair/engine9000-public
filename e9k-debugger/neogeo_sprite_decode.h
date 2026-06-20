/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "libretro_host.h"

#define NEOGEO_SPRITE_DECODE_COORD_SIZE 512
#define NEOGEO_SPRITE_DECODE_WRAP_MASK 0x1ff
#define NEOGEO_SPRITE_DECODE_MAX_SPRITES 382
#define NEOGEO_SPRITE_DECODE_SPRITES_PER_LINE_MAX 96
#define NEOGEO_SPRITE_DECODE_LINE_COUNT NEOGEO_SPRITE_DECODE_COORD_SIZE
#define NEOGEO_SPRITE_DECODE_SPRITE_PALETTE_SHIFT 8u
#define NEOGEO_SPRITE_DECODE_SPRITE_PALETTE_MASK 0x00ffu
#define NEOGEO_SPRITE_DECODE_VISIBLE_X0 0
#define NEOGEO_SPRITE_DECODE_VISIBLE_Y0 0
#define NEOGEO_SPRITE_DECODE_VISIBLE_W 320
#define NEOGEO_SPRITE_DECODE_VISIBLE_H 224
#define NEOGEO_SPRITE_DECODE_COORD_MIN_X (-192)
#define NEOGEO_SPRITE_DECODE_COORD_MIN_Y (-272)
#define NEOGEO_SPRITE_DECODE_COORD_MAX_X 511
#define NEOGEO_SPRITE_DECODE_COORD_MAX_Y 511
#define NEOGEO_SPRITE_DECODE_COORD_W \
    (NEOGEO_SPRITE_DECODE_COORD_MAX_X - NEOGEO_SPRITE_DECODE_COORD_MIN_X + 1)
#define NEOGEO_SPRITE_DECODE_COORD_H \
    (NEOGEO_SPRITE_DECODE_COORD_MAX_Y - NEOGEO_SPRITE_DECODE_COORD_MIN_Y + 1)
#define NEOGEO_SPRITE_DECODE_COORD_OFFSET_X (-NEOGEO_SPRITE_DECODE_COORD_MIN_X)
#define NEOGEO_SPRITE_DECODE_COORD_OFFSET_Y (-NEOGEO_SPRITE_DECODE_COORD_MIN_Y)

typedef struct neogeo_sprite_decode_sprite {
    uint16_t scb2;
    uint16_t scb3;
    uint16_t scb4;
    unsigned xpos;
    unsigned ypos;
    unsigned sprsize;
    unsigned hshrink;
    unsigned vshrink;
    unsigned chainRootIndex;
    unsigned chainOffset;
    unsigned paletteBank;
    int width;
    int hasAnimBits;
} neogeo_sprite_decode_sprite_t;

typedef struct neogeo_sprite_decode_line_sprites {
    uint16_t indices[NEOGEO_SPRITE_DECODE_SPRITES_PER_LINE_MAX];
    uint8_t count;
} neogeo_sprite_decode_line_sprites_t;

unsigned
neogeo_sprite_decode_countShrinkWidth(unsigned hshrink);

int
neogeo_sprite_decode_hshrinkPixelVisible(unsigned hshrink, unsigned pixel);

int
neogeo_sprite_decode_spriteHasAnimBits(const uint16_t *vram,
                                       size_t vramWords,
                                       unsigned spriteIndex,
                                       unsigned sprsize);

unsigned
neogeo_sprite_decode_visibleLineSpriteRow(const e9k_debug_sprite_state_t *st,
                                          const neogeo_sprite_decode_sprite_t *sprite,
                                          int visibleLine);

int
neogeo_sprite_decode_decodeSprites(const e9k_debug_sprite_state_t *st,
                                   neogeo_sprite_decode_sprite_t *decodedSprites,
                                   neogeo_sprite_decode_line_sprites_t *lineSprites,
                                   uint8_t *chainHasAnimBits,
                                   unsigned sprlimit);
