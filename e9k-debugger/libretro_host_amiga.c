/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "libretro_host_internal.h"

void
libretro_host_amiga_setEstimateFpsEnabled(int enabled)
{
    int nextEnabled = enabled ? 1 : 0;
    if (libretro_host.estimateFpsEnabled == nextEnabled) {
        return;
    }
    libretro_host.estimateFpsEnabled = nextEnabled;
    libretro_host_clearEstimateFpsState();
}

int
libretro_host_amiga_getEstimateFpsEnabled(void)
{
    return libretro_host.estimateFpsEnabled ? 1 : 0;
}

double
libretro_host_amiga_getEstimatedVideoFps(void)
{
    return libretro_host.estimateFpsValue;
}

unsigned
libretro_host_amiga_getEstimatedVideoDistinctColors(void)
{
    return libretro_host.estimateFpsDistinctColorCount;
}

bool
libretro_host_amiga_getEstimatedVideoVisibleArea(unsigned *outWidth, unsigned *outHeight)
{
    if (!outWidth || !outHeight) {
        return false;
    }
    if (libretro_host.estimateFpsVisibleWidth == 0u ||
        libretro_host.estimateFpsVisibleHeight == 0u) {
        return false;
    }
    *outWidth = libretro_host.estimateFpsVisibleWidth;
    *outHeight = libretro_host.estimateFpsVisibleHeight;
    return true;
}

bool
libretro_host_amiga_getDmaAddr(int **outAddr)
{
    if (!outAddr) {
        return false;
    }
    *outAddr = NULL;
    if (!libretro_host.debugAmigaGetDmaAddr) {
        libretro_host.debugAmigaGetDmaAddr = (e9k_debug_amiga_get_dma_addr_fn_t)
            libretro_host_loadSymbol("e9k_debug_amiga_get_dma_addr");
    }
    if (!libretro_host.debugAmigaGetDmaAddr) {
        return false;
    }
    *outAddr = libretro_host.debugAmigaGetDmaAddr();
    return *outAddr != NULL;
}

bool
libretro_host_amiga_getCopperAddr(int **outAddr)
{
    if (!outAddr) {
        return false;
    }
    *outAddr = NULL;
    if (!libretro_host.debugAmigaGetCopperAddr) {
        libretro_host.debugAmigaGetCopperAddr = (e9k_debug_amiga_get_copper_addr_fn_t)
            libretro_host_loadSymbol("e9k_debug_amiga_get_copper_addr");
    }
    if (!libretro_host.debugAmigaGetCopperAddr) {
        return false;
    }
    *outAddr = libretro_host.debugAmigaGetCopperAddr();
    return *outAddr != NULL;
}

bool
libretro_host_amiga_setDebugBaseCallback(void (*cb)(uint32_t section, uint32_t base))
{
    if (!libretro_host.setAmigaDebugBaseCallback) {
        libretro_host.setAmigaDebugBaseCallback = (e9k_debug_amiga_set_base_callback_fn_t)
            libretro_host_loadSymbol("e9k_debug_set_debug_base_callback");
    }
    if (!libretro_host.setAmigaDebugBaseCallback) {
        return false;
    }
    libretro_host.setAmigaDebugBaseCallback(cb);
    return true;
}

bool
libretro_host_amiga_setDebugBaseStackCallback(void (*cb)(uint32_t section, uint32_t base, uint32_t size))
{
    if (!libretro_host.setAmigaDebugBaseStackCallback) {
        libretro_host.setAmigaDebugBaseStackCallback = (e9k_debug_amiga_set_base_stack_callback_fn_t)
            libretro_host_loadSymbol("e9k_debug_set_debug_base_stack_callback");
    }
    if (!libretro_host.setAmigaDebugBaseStackCallback) {
        return false;
    }
    libretro_host.setAmigaDebugBaseStackCallback(cb);
    return true;
}

bool
libretro_host_amiga_setDebugBreakpointCallback(void (*cb)(uint32_t addr))
{
    if (!libretro_host.setAmigaDebugBreakpointCallback) {
        libretro_host.setAmigaDebugBreakpointCallback = (e9k_debug_set_breakpoint_callback_fn_t)
            libretro_host_loadSymbol("e9k_debug_set_debug_breakpoint_callback");
    }
    if (!libretro_host.setAmigaDebugBreakpointCallback) {
        return false;
    }
    libretro_host.setAmigaDebugBreakpointCallback(cb);
    return true;
}

bool
libretro_host_amiga_setDebugExitCallback(void (*cb)(void))
{
    if (!libretro_host.setAmigaDebugExitCallback) {
        libretro_host.setAmigaDebugExitCallback = (e9k_debug_set_exit_callback_fn_t)
            libretro_host_loadSymbol("e9k_debug_set_debug_exit_callback");
    }
    if (!libretro_host.setAmigaDebugExitCallback) {
        return false;
    }
    libretro_host.setAmigaDebugExitCallback(cb);
    return true;
}

bool
libretro_host_amiga_setDebugSmokeStartCallback(void (*cb)(void))
{
    if (!libretro_host.setAmigaDebugSmokeStartCallback) {
        libretro_host.setAmigaDebugSmokeStartCallback = (e9k_debug_set_smoke_start_callback_fn_t)
            libretro_host_loadSymbol("e9k_debug_set_debug_smoke_start_callback");
    }
    if (!libretro_host.setAmigaDebugSmokeStartCallback) {
        return false;
    }
    libretro_host.setAmigaDebugSmokeStartCallback(cb);
    return true;
}

bool
libretro_host_amiga_setDebugProfileStartCallback(void (*cb)(void))
{
    if (!libretro_host.setAmigaDebugProfileStartCallback) {
        libretro_host.setAmigaDebugProfileStartCallback = (e9k_debug_set_profile_start_callback_fn_t)
            libretro_host_loadSymbol("e9k_debug_set_debug_profile_start_callback");
    }
    if (!libretro_host.setAmigaDebugProfileStartCallback) {
        return false;
    }
    libretro_host.setAmigaDebugProfileStartCallback(cb);
    return true;
}

bool
libretro_host_amiga_setDebugArgs(const uint32_t *args, size_t count)
{
    if (!libretro_host.setAmigaDebugArgs) {
        libretro_host.setAmigaDebugArgs = (e9k_debug_set_args_fn_t)
            libretro_host_loadSymbol("e9k_debug_set_debug_args");
    }
    if (!libretro_host.setAmigaDebugArgs) {
        return false;
    }
    libretro_host.setAmigaDebugArgs(args, count);
    return true;
}

bool
libretro_host_amiga_setDeterministic(int enabled)
{
    if (!libretro_host.setAmigaDeterministic) {
        libretro_host.setAmigaDeterministic = (e9k_debug_amiga_set_deterministic_fn_t)
            libretro_host_loadSymbol("e9k_debug_setDeterministic");
    }
    if (!libretro_host.setAmigaDeterministic) {
        return false;
    }
    libretro_host.setAmigaDeterministic(enabled ? 1 : 0);
    return true;
}

const e9k_debug_ami_custom_reg_state_t *
libretro_host_amiga_getCustomRegs(void)
{
    if (!libretro_host.debugAmigaGetCustomRegs) {
        libretro_host.debugAmigaGetCustomRegs = (e9k_debug_amiga_get_custom_regs_fn_t)
            libretro_host_loadSymbol("e9k_debug_amiga_get_custom_regs");
    }
    if (!libretro_host.debugAmigaGetCustomRegs) {
        return NULL;
    }
    return libretro_host.debugAmigaGetCustomRegs();
}

bool
libretro_host_amiga_setBlitterDebug(int enabled)
{
    if (!libretro_host.debugAmigaSetBlitterDebug) {
        libretro_host.debugAmigaSetBlitterDebug = (e9k_debug_amiga_set_blitter_debug_fn_t)
            libretro_host_loadSymbol("e9k_debug_amiga_set_blitter_debug");
    }
    if (!libretro_host.debugAmigaSetBlitterDebug) {
        return false;
    }
    libretro_host.debugAmigaSetBlitterDebug(enabled ? 1 : 0);
    return true;
}

bool
libretro_host_amiga_getBlitterDebug(int *outEnabled)
{
    if (outEnabled) {
        *outEnabled = 0;
    }
    if (!libretro_host.debugAmigaGetBlitterDebug) {
        libretro_host.debugAmigaGetBlitterDebug = (e9k_debug_amiga_get_blitter_debug_fn_t)
            libretro_host_loadSymbol("e9k_debug_amiga_get_blitter_debug");
    }
    if (!libretro_host.debugAmigaGetBlitterDebug) {
        return false;
    }
    if (outEnabled) {
        *outEnabled = libretro_host.debugAmigaGetBlitterDebug() ? 1 : 0;
    }
    return true;
}

size_t
libretro_host_amiga_readBlitterVisSpans(e9k_debug_ami_blitter_vis_span_t *out, size_t cap, uint32_t *outWidth, uint32_t *outHeight)
{
    if (outWidth) {
        *outWidth = 0u;
    }
    if (outHeight) {
        *outHeight = 0u;
    }
    if (!libretro_host.debugAmigaBlitterVisReadSpans) {
        libretro_host.debugAmigaBlitterVisReadSpans = (e9k_debug_amiga_blitter_vis_read_spans_fn_t)
            libretro_host_loadSymbol("e9k_debug_amiga_blitter_vis_read_spans");
        if (!libretro_host.debugAmigaBlitterVisReadSpans) {
            libretro_host.debugAmigaBlitterVisReadSpans = (e9k_debug_amiga_blitter_vis_read_spans_fn_t)
                libretro_host_loadSymbol("e9k_debug_amiga_blitter_vis_read_points");
        }
    }
    if (!libretro_host.debugAmigaBlitterVisReadSpans) {
        return 0u;
    }
    return libretro_host.debugAmigaBlitterVisReadSpans(out, cap, outWidth, outHeight);
}

size_t
libretro_host_amiga_readBlitterVisPoints(e9k_debug_ami_blitter_vis_point_t *out, size_t cap, uint32_t *outWidth, uint32_t *outHeight)
{
    return libretro_host_amiga_readBlitterVisSpans(out, cap, outWidth, outHeight);
}

bool
libretro_host_amiga_readBlitterVisStats(e9k_debug_ami_blitter_vis_stats_t *out)
{
    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!libretro_host.debugAmigaBlitterVisReadStats) {
        libretro_host.debugAmigaBlitterVisReadStats = (e9k_debug_amiga_blitter_vis_read_stats_fn_t)
            libretro_host_loadSymbol("e9k_debug_amiga_blitter_vis_read_stats");
    }
    if (!libretro_host.debugAmigaBlitterVisReadStats) {
        return false;
    }
    return libretro_host.debugAmigaBlitterVisReadStats(out, sizeof(*out)) == sizeof(*out);
}

size_t
libretro_host_amiga_readBlitterVisWordTags(uint32_t addr, uint32_t *out, size_t cap)
{
    if (!out || cap == 0u) {
        return 0u;
    }
    memset(out, 0, cap * sizeof(*out));
    if (!libretro_host.debugAmigaBlitterVisReadWordTags) {
        libretro_host.debugAmigaBlitterVisReadWordTags = (e9k_debug_amiga_blitter_vis_read_word_tags_fn_t)
            libretro_host_loadSymbol("e9k_debug_amiga_blitter_vis_read_word_tags");
    }
    if (!libretro_host.debugAmigaBlitterVisReadWordTags) {
        return 0u;
    }
    return libretro_host.debugAmigaBlitterVisReadWordTags(addr, out, cap);
}

bool
libretro_host_amiga_setSpriteVis(int enabled)
{
    if (!libretro_host.debugAmigaSetSpriteVis) {
        libretro_host.debugAmigaSetSpriteVis = (e9k_debug_amiga_set_sprite_vis_fn_t)
            libretro_host_loadSymbol("e9k_debug_amiga_set_sprite_vis");
    }
    if (!libretro_host.debugAmigaSetSpriteVis) {
        return false;
    }
    libretro_host.debugAmigaSetSpriteVis(enabled ? 1 : 0);
    return true;
}

bool
libretro_host_amiga_getSpriteVis(int *outEnabled)
{
    if (outEnabled) {
        *outEnabled = 0;
    }
    if (!libretro_host.debugAmigaGetSpriteVis) {
        libretro_host.debugAmigaGetSpriteVis = (e9k_debug_amiga_get_sprite_vis_fn_t)
            libretro_host_loadSymbol("e9k_debug_amiga_get_sprite_vis");
    }
    if (!libretro_host.debugAmigaGetSpriteVis) {
        return false;
    }
    if (outEnabled) {
        *outEnabled = libretro_host.debugAmigaGetSpriteVis() ? 1 : 0;
    }
    return true;
}

bool
libretro_host_amiga_setStatusbar(int enabled)
{
    if (!libretro_host.debugAmigaSetStatusbar) {
        libretro_host.debugAmigaSetStatusbar = (e9k_debug_amiga_set_statusbar_fn_t)
            libretro_host_loadSymbol("e9k_debug_amiga_set_statusbar");
    }
    if (!libretro_host.debugAmigaSetStatusbar) {
        return false;
    }
    libretro_host.debugAmigaSetStatusbar(enabled ? 1 : 0);
    return true;
}

bool
libretro_host_amiga_getStatusbar(int *outEnabled)
{
    if (outEnabled) {
        *outEnabled = 0;
    }
    if (!libretro_host.debugAmigaGetStatusbar) {
        libretro_host.debugAmigaGetStatusbar = (e9k_debug_amiga_get_statusbar_fn_t)
            libretro_host_loadSymbol("e9k_debug_amiga_get_statusbar");
    }
    if (!libretro_host.debugAmigaGetStatusbar) {
        return false;
    }
    if (outEnabled) {
        *outEnabled = libretro_host.debugAmigaGetStatusbar() ? 1 : 0;
    }
    return true;
}

size_t
libretro_host_amiga_readSpriteVisPoints(e9k_debug_ami_sprite_vis_point_t *out, size_t cap, uint32_t *outWidth, uint32_t *outHeight)
{
    if (outWidth) {
        *outWidth = 0u;
    }
    if (outHeight) {
        *outHeight = 0u;
    }
    if (!libretro_host.debugAmigaSpriteVisReadPoints) {
        libretro_host.debugAmigaSpriteVisReadPoints = (e9k_debug_amiga_sprite_vis_read_points_fn_t)
            libretro_host_loadSymbol("e9k_debug_amiga_sprite_vis_read_points");
    }
    if (!libretro_host.debugAmigaSpriteVisReadPoints) {
        return 0u;
    }
    return libretro_host.debugAmigaSpriteVisReadPoints(out, cap, outWidth, outHeight);
}

const e9k_debug_ami_dma_debug_frame_view_t *
libretro_host_amiga_getDmaDebugFrameView(uint32_t frameSelect)
{
    if (!libretro_host.debugAmigaDmaDebugGetFrameView) {
        libretro_host.debugAmigaDmaDebugGetFrameView = (e9k_debug_amiga_dma_debug_get_frame_view_fn_t)
            libretro_host_loadSymbol("e9k_debug_amiga_dma_debug_get_frame_view");
    }
    if (!libretro_host.debugAmigaDmaDebugGetFrameView) {
        return NULL;
    }
    return libretro_host.debugAmigaDmaDebugGetFrameView(frameSelect);
}

const e9k_debug_ami_copper_debug_frame_view_t *
libretro_host_amiga_getCopperDebugFrameView(uint32_t frameSelect)
{
    if (!libretro_host.debugAmigaCopperDebugGetFrameView) {
        libretro_host.debugAmigaCopperDebugGetFrameView = (e9k_debug_amiga_copper_debug_get_frame_view_fn_t)
            libretro_host_loadSymbol("e9k_debug_amiga_copper_debug_get_frame_view");
    }
    if (!libretro_host.debugAmigaCopperDebugGetFrameView) {
        return NULL;
    }
    return libretro_host.debugAmigaCopperDebugGetFrameView(frameSelect);
}

bool
libretro_host_amiga_getVideoLineCount(int *outLineCount)
{
    if (outLineCount) {
        *outLineCount = 0;
    }
    if (!libretro_host.debugAmigaGetVideoLineCount) {
        libretro_host.debugAmigaGetVideoLineCount = (e9k_debug_amiga_get_video_line_count_fn_t)
            libretro_host_loadSymbol("e9k_debug_amiga_get_video_line_count");
    }
    if (!libretro_host.debugAmigaGetVideoLineCount) {
        return false;
    }
    int lineCount = libretro_host.debugAmigaGetVideoLineCount();
    if (lineCount <= 0) {
        return false;
    }
    if (outLineCount) {
        *outLineCount = lineCount;
    }
    return true;
}

bool
libretro_host_amiga_getRasterLineCount(int *outLineCount)
{
    if (outLineCount) {
        *outLineCount = 0;
    }
    if (!libretro_host.debugAmigaGetRasterLineCount) {
        libretro_host.debugAmigaGetRasterLineCount = (e9k_debug_amiga_get_raster_line_count_fn_t)
            libretro_host_loadSymbol("e9k_debug_amiga_get_raster_line_count");
    }
    if (!libretro_host.debugAmigaGetRasterLineCount) {
        return false;
    }
    int lineCount = libretro_host.debugAmigaGetRasterLineCount();
    if (lineCount <= 0) {
        return false;
    }
    if (outLineCount) {
        *outLineCount = lineCount;
    }
    return true;
}

bool
libretro_host_amiga_videoLineToCoreLine(int videoLine, int *outCoreLine)
{
    if (outCoreLine) {
        *outCoreLine = -1;
    }
    if (!libretro_host.debugAmigaVideoLineToCoreLine) {
        libretro_host.debugAmigaVideoLineToCoreLine = (e9k_debug_amiga_video_line_to_core_line_fn_t)
            libretro_host_loadSymbol("e9k_debug_amiga_video_line_to_core_line");
    }
    if (!libretro_host.debugAmigaVideoLineToCoreLine) {
        return false;
    }
    int coreLine = libretro_host.debugAmigaVideoLineToCoreLine(videoLine);
    if (coreLine < 0) {
        return false;
    }
    if (outCoreLine) {
        *outCoreLine = coreLine;
    }
    return true;
}

bool
libretro_host_amiga_coreLineToVideoLine(int coreLine, int *outVideoLine)
{
    if (outVideoLine) {
        *outVideoLine = -1;
    }
    if (!libretro_host.debugAmigaCoreLineToVideoLine) {
        libretro_host.debugAmigaCoreLineToVideoLine = (e9k_debug_amiga_core_line_to_video_line_fn_t)
            libretro_host_loadSymbol("e9k_debug_amiga_core_line_to_video_line");
    }
    if (!libretro_host.debugAmigaCoreLineToVideoLine) {
        return false;
    }
    int videoLine = libretro_host.debugAmigaCoreLineToVideoLine(coreLine);
    if (videoLine < 0) {
        return false;
    }
    if (outVideoLine) {
        *outVideoLine = videoLine;
    }
    return true;
}

const e9k_debug_ami_video_line_state_t *
libretro_host_amiga_getVideoLineStates(void)
{
    if (!libretro_host.debugAmigaGetVideoLineStates) {
        libretro_host.debugAmigaGetVideoLineStates = (e9k_debug_amiga_get_video_line_states_fn_t)
            libretro_host_loadSymbol("e9k_debug_amiga_get_video_line_states");
    }
    if (!libretro_host.debugAmigaGetVideoLineStates) {
        return NULL;
    }
    return libretro_host.debugAmigaGetVideoLineStates();
}

bool
libretro_host_amiga_setFloppyPath(int drive, const char *path)
{
    if (!libretro_host.debugAmigaSetFloppyPath) {
        libretro_host.debugAmigaSetFloppyPath = (e9k_debug_amiga_set_floppy_path_fn_t)
            libretro_host_loadSymbol("e9k_debug_amiga_set_floppy_path");
    }
    if (!libretro_host.debugAmigaSetFloppyPath) {
        return false;
    }
    return libretro_host.debugAmigaSetFloppyPath(drive, path ? path : "");
}

bool
libretro_host_amiga_setCustomLogFrameCallback(e9k_debug_ami_custom_log_frame_callback_t cb, void *user)
{
    if (!libretro_host.setAmigaCustomLogFrameCallback) {
        libretro_host.setAmigaCustomLogFrameCallback = (e9k_debug_set_amiga_custom_log_frame_callback_fn_t)
            libretro_host_loadSymbol("e9k_debug_set_amiga_custom_log_frame_callback");
    }
    if (!libretro_host.setAmigaCustomLogFrameCallback) {
        return false;
    }
    libretro_host.setAmigaCustomLogFrameCallback(cb, user);
    return true;
}
