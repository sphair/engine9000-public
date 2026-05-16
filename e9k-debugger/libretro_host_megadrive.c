/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <stdbool.h>

#include "libretro_host_internal.h"

bool
libretro_host_megadrive_getSpriteState(e9k_debug_mega_sprite_state_t *out)
{
    if (!out || !libretro_host.debugMegadriveGetSpriteState) {
        return false;
    }
    size_t n = libretro_host.debugMegadriveGetSpriteState(out, sizeof(*out));
    return n == sizeof(*out);
}

size_t
libretro_host_megadrive_getRoms(e9k_debug_rom_entry_t *out, size_t cap)
{
    size_t n = 0u;

    if (!libretro_host.debugMegadriveGetRoms) {
        return 0u;
    }
    n = libretro_host.debugMegadriveGetRoms(out, cap * sizeof(*out));
    if (n > cap * sizeof(*out)) {
        n = cap * sizeof(*out);
    }
    return n / sizeof(*out);
}

bool
libretro_host_megadrive_setPaletteGreyscaleMask(uint32_t mask)
{
    if (!libretro_host.debugMegadriveSetPaletteGreyscaleMask) {
        return false;
    }
    libretro_host.debugMegadriveSetPaletteGreyscaleMask(mask);
    return true;
}

bool
libretro_host_megadrive_getPaletteGreyscaleMask(uint32_t *outMask)
{
    if (!outMask || !libretro_host.debugMegadriveGetPaletteGreyscaleMask) {
        return false;
    }
    *outMask = libretro_host.debugMegadriveGetPaletteGreyscaleMask();
    return true;
}

bool
libretro_host_megadrive_getAudioFrame(e9k_debug_mega_audio_frame_t *out)
{
    if (!out || !libretro_host.debugMegadriveGetAudioFrame) {
        return false;
    }
    size_t n = libretro_host.debugMegadriveGetAudioFrame(out, sizeof(*out));
    return n == sizeof(*out);
}

bool
libretro_host_megadrive_setAudioVisEnabled(int enabled)
{
    if (!libretro_host.debugMegadriveSetAudioVisEnabled) {
        return false;
    }
    libretro_host.debugMegadriveSetAudioVisEnabled(enabled ? 1 : 0);
    return true;
}

bool
libretro_host_megadrive_setAudioMuteMask(uint32_t mask)
{
    if (!libretro_host.debugMegadriveSetAudioMuteMask) {
        return false;
    }
    libretro_host.debugMegadriveSetAudioMuteMask(mask);
    return true;
}

void
libretro_host_megadrive_bindApis(void)
{
    libretro_host.debugMegadriveGetSpriteState =
        (e9k_debug_megadrive_get_sprite_state_fn_t)libretro_host_loadSymbol("e9k_debug_megadrive_get_sprite_state");
    libretro_host.debugMegadriveGetRoms =
        (e9k_debug_megadrive_get_roms_fn_t)libretro_host_loadSymbol("e9k_debug_megadrive_get_roms");
    libretro_host.debugMegadriveSetPaletteGreyscaleMask =
        (e9k_debug_megadrive_set_palette_greyscale_mask_fn_t)libretro_host_loadSymbol("e9k_debug_megadrive_set_palette_greyscale_mask");
    libretro_host.debugMegadriveGetPaletteGreyscaleMask =
        (e9k_debug_megadrive_get_palette_greyscale_mask_fn_t)libretro_host_loadSymbol("e9k_debug_megadrive_get_palette_greyscale_mask");
    libretro_host.debugMegadriveGetAudioFrame =
        (e9k_debug_megadrive_get_audio_frame_fn_t)libretro_host_loadSymbol("e9k_debug_megadrive_get_audio_frame");
    libretro_host.debugMegadriveSetAudioVisEnabled =
        (e9k_debug_megadrive_set_audio_vis_enabled_fn_t)libretro_host_loadSymbol("e9k_debug_megadrive_set_audio_vis_enabled");
    libretro_host.debugMegadriveSetAudioMuteMask =
        (e9k_debug_megadrive_set_audio_mute_mask_fn_t)libretro_host_loadSymbol("e9k_debug_megadrive_set_audio_mute_mask");
}

void
libretro_host_megadrive_unbindApis(void)
{
    libretro_host.debugMegadriveGetSpriteState = NULL;
    libretro_host.debugMegadriveGetRoms = NULL;
    libretro_host.debugMegadriveSetPaletteGreyscaleMask = NULL;
    libretro_host.debugMegadriveGetPaletteGreyscaleMask = NULL;
    libretro_host.debugMegadriveGetAudioFrame = NULL;
    libretro_host.debugMegadriveSetAudioVisEnabled = NULL;
    libretro_host.debugMegadriveSetAudioMuteMask = NULL;
}
