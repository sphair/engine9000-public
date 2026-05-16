/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#define E9K_DEBUG_MEGA_MAX_LINES 240
#define E9K_DEBUG_MEGA_MAX_FRAME_SPRITES 80
#define E9K_DEBUG_MEGA_AUDIO_MUTE_FM (1u << 0)
#define E9K_DEBUG_MEGA_AUDIO_MUTE_PSG (1u << 1)
#define E9K_DEBUG_MEGA_AUDIO_MUTE_DAC (1u << 2)

#define E9K_DEBUG_MEGA_LINEFLAG_TILE_OVERFLOW   0x01u
#define E9K_DEBUG_MEGA_LINEFLAG_SPRITE_OVERFLOW 0x02u
#define E9K_DEBUG_MEGA_LINEFLAG_MASKED          0x04u

/* Per-sprite flags in e9k_debug_mega_sprite_entry_t.flags. */
#define E9K_DEBUG_MEGA_SPRITEFLAG_VISIBLE         0x01u
#define E9K_DEBUG_MEGA_SPRITEFLAG_RENDERED        0x02u
#define E9K_DEBUG_MEGA_SPRITEFLAG_OVERFLOW_SPRITE 0x04u
#define E9K_DEBUG_MEGA_SPRITEFLAG_OVERFLOW_TILE   0x08u
#define E9K_DEBUG_MEGA_SPRITEFLAG_MASKED          0x10u

typedef struct e9k_debug_mega_sprite_entry
{
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t satIndex;
    uint16_t link;
    uint16_t tileIndex;
    uint8_t palette;
    uint8_t widthTiles;
    uint8_t heightTiles;
    uint8_t flags;
    uint8_t _reserved[3];
} e9k_debug_mega_sprite_entry_t;

typedef struct e9k_debug_mega_sprite_state
{
    int screenW;
    int screenH;
    int cropTop;
    int cropBottom;
    int cropLeft;
    int cropRight;
    int lineCount;
    int spriteLimitPerLine;
    int tileLimitPerLine;
    int frameSpriteUsed;
    int frameSpriteMax;
    uint8_t vdpRegs[32];
    uint8_t spritesPerLine[E9K_DEBUG_MEGA_MAX_LINES];
    uint8_t tilesPerLine[E9K_DEBUG_MEGA_MAX_LINES];
    uint8_t lineFlags[E9K_DEBUG_MEGA_MAX_LINES];
    int spriteEntryCount;
    e9k_debug_mega_sprite_entry_t spriteEntries[E9K_DEBUG_MEGA_MAX_FRAME_SPRITES];
} e9k_debug_mega_sprite_state_t;

typedef struct e9k_debug_mega_audio_source
{
    int32_t peakL;
    int32_t peakR;
} e9k_debug_mega_audio_source_t;

typedef struct e9k_debug_mega_audio_frame
{
    uint64_t frameNo;
    e9k_debug_mega_audio_source_t fm;
    e9k_debug_mega_audio_source_t psg;
    e9k_debug_mega_audio_source_t dac;
    e9k_debug_mega_audio_source_t mixed;
} e9k_debug_mega_audio_frame_t;
