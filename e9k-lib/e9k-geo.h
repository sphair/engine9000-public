/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct e9k_debug_rom_region {
    const uint8_t *data;
    size_t size;
} e9k_debug_rom_region_t;

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


#ifdef __cplusplus
}
#endif
