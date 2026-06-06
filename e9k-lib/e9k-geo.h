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
    int crop_t;
    int crop_b;
    int crop_l;
    int crop_r;
} e9k_debug_sprite_state_t;

typedef struct geo_debug_palette_state {
    const uint32_t *colors;
    size_t color_count;
    unsigned active_bank;
} e9k_debug_palette_state_t;

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
