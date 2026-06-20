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

#define E9K_DEBUG_GEO_ADPCM_A_CHANNELS 6
#define E9K_DEBUG_GEO_AUDIO_SPECTRUM_SAMPLES 1024
#define E9K_DEBUG_GEO_AUDIO_MUTE_FM (1u << 0)
#define E9K_DEBUG_GEO_AUDIO_MUTE_SSG (1u << 1)
#define E9K_DEBUG_GEO_AUDIO_MUTE_ADPCM_A0 (1u << 2)
#define E9K_DEBUG_GEO_AUDIO_MUTE_ADPCM_A1 (1u << 3)
#define E9K_DEBUG_GEO_AUDIO_MUTE_ADPCM_A2 (1u << 4)
#define E9K_DEBUG_GEO_AUDIO_MUTE_ADPCM_A3 (1u << 5)
#define E9K_DEBUG_GEO_AUDIO_MUTE_ADPCM_A4 (1u << 6)
#define E9K_DEBUG_GEO_AUDIO_MUTE_ADPCM_A5 (1u << 7)
#define E9K_DEBUG_GEO_AUDIO_MUTE_ADPCM_B (1u << 8)
#define E9K_DEBUG_GEO_PALETTE_GRAYSCALE_MASK_WORDS 16
#define E9K_DEBUG_GEO_DEFAULT_TOP_CROP 8
#define E9K_DEBUG_GEO_VIDEO_TOP_BORDER 16
#define E9K_DEBUG_GEO_SPRITE_DISPLAY_SCANLINES 8
#define E9K_DEBUG_GEO_AUDIO_MUTE_ADPCM_A_MASK \
    (E9K_DEBUG_GEO_AUDIO_MUTE_ADPCM_A0 | E9K_DEBUG_GEO_AUDIO_MUTE_ADPCM_A1 | \
     E9K_DEBUG_GEO_AUDIO_MUTE_ADPCM_A2 | E9K_DEBUG_GEO_AUDIO_MUTE_ADPCM_A3 | \
     E9K_DEBUG_GEO_AUDIO_MUTE_ADPCM_A4 | E9K_DEBUG_GEO_AUDIO_MUTE_ADPCM_A5)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct e9k_debug_rom_region {
    const uint8_t *data;
    size_t size;
} e9k_debug_rom_region_t;

typedef struct e9k_debug_rom_entry {
    char label[16];
    const uint8_t *data;
    size_t size;
} e9k_debug_rom_entry_t;

typedef struct geo_debug_sprite_state {
    const uint16_t *vram;
    size_t vram_words;
    unsigned sprlimit;
    int screen_w;
    int screen_h;
    int visible_h;
    int crop_t;
    int crop_b;
    int crop_l;
    int crop_r;
    unsigned autoAnimationCounter;
    unsigned autoAnimationDisabled;
} e9k_debug_sprite_state_t;

#define E9K_DEBUG_GEO_SPRITE_SELECTION_MASK_WORDS 12

static inline int
e9k_debug_geo_spriteVisibleLineOffset(const e9k_debug_sprite_state_t *state)
{
    int cropT = (state && state->crop_t >= 0) ? state->crop_t : E9K_DEBUG_GEO_DEFAULT_TOP_CROP;
    return cropT + E9K_DEBUG_GEO_VIDEO_TOP_BORDER - E9K_DEBUG_GEO_SPRITE_DISPLAY_SCANLINES;
}

typedef struct geo_debug_sprite_grayscale_selection {
    int enabled;
    int highlightChain;
    int invert;
    int hide;
    uint32_t spriteMask[E9K_DEBUG_GEO_SPRITE_SELECTION_MASK_WORDS];
} e9k_debug_sprite_grayscale_selection_t;

typedef struct geo_debug_palette_state {
    const uint32_t *colors;
    size_t color_count;
    unsigned active_bank;
} e9k_debug_palette_state_t;

typedef struct geo_debug_palette_grayscale_mask {
    uint32_t words[E9K_DEBUG_GEO_PALETTE_GRAYSCALE_MASK_WORDS];
} e9k_debug_palette_grayscale_mask_t;

typedef enum geo_debug_fix_layer_mode {
    e9k_debug_geo_fix_layer_mode_normal = 0,
    e9k_debug_geo_fix_layer_mode_grayscale = 1,
    e9k_debug_geo_fix_layer_mode_hidden = 2
} e9k_debug_geo_fix_layer_mode_t;

typedef struct geo_debug_audio_source {
    int32_t peakL;
    int32_t peakR;
    int32_t volumeL;
    int32_t volumeR;
    uint8_t hasVolume;
} e9k_debug_audio_source_t;

typedef struct geo_debug_audio_frame {
    uint64_t frameNo;
    e9k_debug_audio_source_t fm;
    e9k_debug_audio_source_t ssg;
    e9k_debug_audio_source_t adpcmA[E9K_DEBUG_GEO_ADPCM_A_CHANNELS];
    e9k_debug_audio_source_t adpcmB;
    e9k_debug_audio_source_t mixed;
    uint32_t adpcmBPlaybackMilliHz;
    uint32_t sampleRate;
    uint32_t mixedSampleCount;
    int16_t mixedSamples[E9K_DEBUG_GEO_AUDIO_SPECTRUM_SAMPLES];
} e9k_debug_audio_frame_t;


#ifdef __cplusplus
}
#endif
