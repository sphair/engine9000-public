/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "libretro_host_internal.h"

bool
libretro_host_neogeo_getSpriteState(e9k_debug_sprite_state_t *out)
{
    if (!out || !libretro_host.debugNeogeoGetSpriteState) {
        return false;
    }
    size_t n = libretro_host.debugNeogeoGetSpriteState(out, sizeof(*out));
    return n == sizeof(*out);
}

bool
libretro_host_neogeo_getP1Rom(e9k_debug_rom_region_t *out)
{
    if (!out || !libretro_host.debugNeogeoGetP1Rom) {
        return false;
    }
    size_t n = libretro_host.debugNeogeoGetP1Rom(out, sizeof(*out));
    if (n != sizeof(*out) || !out->data || out->size == 0) {
        fprintf(stderr, "libretro: libretro_host_neogeo_getP1Rom failed (n=%zu data=%p size=%zu)\n",
                n, out ? (void *)out->data : NULL, out ? (size_t)out->size : 0);
        return false;
    }
    return true;
}

bool
libretro_host_neogeo_getCRom(e9k_debug_rom_region_t *out)
{
    if (!out || !libretro_host.debugNeogeoGetCRom) {
        return false;
    }
    size_t n = libretro_host.debugNeogeoGetCRom(out, sizeof(*out));
    if (n != sizeof(*out) || !out->data || out->size == 0) {
        fprintf(stderr, "libretro: libretro_host_neogeo_getCRom failed (n=%zu data=%p size=%zu)\n",
                n, out ? (void *)out->data : NULL, out ? (size_t)out->size : 0);
        return false;
    }
    return true;
}

bool
libretro_host_neogeo_getFixRom(e9k_debug_rom_region_t *out)
{
    if (!out || !libretro_host.debugNeogeoGetFixRom) {
        return false;
    }
    size_t n = libretro_host.debugNeogeoGetFixRom(out, sizeof(*out));
    if (n != sizeof(*out) || !out->data || out->size == 0) {
        fprintf(stderr, "libretro: libretro_host_neogeo_getFixRom failed (n=%zu data=%p size=%zu)\n",
                n, out ? (void *)out->data : NULL, out ? (size_t)out->size : 0);
        return false;
    }
    return true;
}

size_t
libretro_host_neogeo_getRoms(e9k_debug_rom_entry_t *out, size_t cap)
{
    size_t n = 0u;

    if (!libretro_host.debugNeogeoGetRoms) {
        return 0u;
    }
    n = libretro_host.debugNeogeoGetRoms(out, cap * sizeof(*out));
    if (n > cap * sizeof(*out)) {
        n = cap * sizeof(*out);
    }
    return n / sizeof(*out);
}

bool
libretro_host_neogeo_getPaletteState(e9k_debug_palette_state_t *out)
{
    if (!out || !libretro_host.debugNeogeoGetPaletteState) {
        return false;
    }
    size_t n = libretro_host.debugNeogeoGetPaletteState(out, sizeof(*out));
    if (n != sizeof(*out) || !out->colors || out->color_count == 0u) {
        fprintf(stderr, "libretro: libretro_host_neogeo_getPaletteState failed (n=%zu colors=%p count=%zu bank=%u)\n",
                n,
                out ? (const void *)out->colors : NULL,
                out ? (size_t)out->color_count : 0u,
                out ? out->active_bank : 0u);
        return false;
    }
    return true;
}

bool
libretro_host_neogeo_getAudioFrame(e9k_debug_audio_frame_t *out)
{
    if (!out || !libretro_host.debugNeogeoGetAudioFrame) {
        return false;
    }
    size_t n = libretro_host.debugNeogeoGetAudioFrame(out, sizeof(*out));
    return n == sizeof(*out);
}

bool
libretro_host_neogeo_setAudioVisEnabled(int enabled)
{
    if (!libretro_host.debugNeogeoSetAudioVisEnabled) {
        return false;
    }
    libretro_host.debugNeogeoSetAudioVisEnabled(enabled ? 1 : 0);
    return true;
}

bool
libretro_host_neogeo_setAudioMuteMask(uint32_t mask)
{
    if (!libretro_host.debugNeogeoSetAudioMuteMask) {
        return false;
    }
    libretro_host.debugNeogeoSetAudioMuteMask(mask);
    return true;
}

bool
libretro_host_neogeo_setRegisterLogFrameCallback(e9k_debug_geo_register_log_frame_callback_t cb, void *user)
{
    if (!libretro_host.debugNeogeoSetRegisterLogFrameCallback) {
        return false;
    }
    libretro_host.debugNeogeoSetRegisterLogFrameCallback(cb, user);
    return true;
}

void
libretro_host_neogeo_bindApis(void)
{
    libretro_host.debugNeogeoGetSpriteState =
        (e9k_debug_neogeo_get_sprite_state_fn_t)libretro_host_loadSymbol("e9k_debug_neogeo_get_sprite_state");
    libretro_host.debugNeogeoGetP1Rom =
        (e9k_debug_neogeo_get_p1_rom_fn_t)libretro_host_loadSymbol("e9k_debug_neogeo_get_p1_rom");
    libretro_host.debugNeogeoGetCRom =
        (e9k_debug_neogeo_get_c_rom_fn_t)libretro_host_loadSymbol("e9k_debug_neogeo_get_c_rom");
    libretro_host.debugNeogeoGetFixRom =
        (e9k_debug_neogeo_get_fix_rom_fn_t)libretro_host_loadSymbol("e9k_debug_neogeo_get_fix_rom");
    libretro_host.debugNeogeoGetRoms =
        (e9k_debug_neogeo_get_roms_fn_t)libretro_host_loadSymbol("e9k_debug_neogeo_get_roms");
    libretro_host.debugNeogeoGetPaletteState =
        (e9k_debug_neogeo_get_palette_state_fn_t)libretro_host_loadSymbol("e9k_debug_neogeo_get_palette_state");
    libretro_host.debugNeogeoGetAudioFrame =
        (e9k_debug_neogeo_get_audio_frame_fn_t)libretro_host_loadSymbol("e9k_debug_neogeo_get_audio_frame");
    libretro_host.debugNeogeoSetAudioVisEnabled =
        (e9k_debug_neogeo_set_audio_vis_enabled_fn_t)libretro_host_loadSymbol("e9k_debug_neogeo_set_audio_vis_enabled");
    libretro_host.debugNeogeoSetAudioMuteMask =
        (e9k_debug_neogeo_set_audio_mute_mask_fn_t)libretro_host_loadSymbol("e9k_debug_neogeo_set_audio_mute_mask");
    libretro_host.debugNeogeoSetRegisterLogFrameCallback =
        (e9k_debug_set_neogeo_register_log_frame_callback_fn_t)libretro_host_loadSymbol("e9k_debug_set_neogeo_register_log_frame_callback");
}

void
libretro_host_neogeo_unbindApis(void)
{
    libretro_host.debugNeogeoGetSpriteState = NULL;
    libretro_host.debugNeogeoGetP1Rom = NULL;
    libretro_host.debugNeogeoGetCRom = NULL;
    libretro_host.debugNeogeoGetFixRom = NULL;
    libretro_host.debugNeogeoGetRoms = NULL;
    libretro_host.debugNeogeoGetPaletteState = NULL;
    libretro_host.debugNeogeoGetAudioFrame = NULL;
    libretro_host.debugNeogeoSetAudioVisEnabled = NULL;
    libretro_host.debugNeogeoSetAudioMuteMask = NULL;
    libretro_host.debugNeogeoSetRegisterLogFrameCallback = NULL;
}
