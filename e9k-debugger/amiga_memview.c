/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include <SDL_ttf.h>

#include "alloc.h"
#include "amiga_memview.h"
#include "aux_window.h"
#include "config.h"
#include "debugger.h"
#include "emu_ami.h"
#include "e9ui.h"
#include "e9ui_box.h"
#include "e9ui_button.h"
#include "e9ui_checkbox.h"
#include "e9ui_hstack.h"
#include "e9ui_scrollbar.h"
#include "e9ui_seek_bar.h"
#include "e9ui_step_buttons.h"
#include "e9ui_spacer.h"
#include "e9ui_stack.h"
#include "e9ui_text.h"
#include "e9ui_textbox.h"
#include "libretro_host.h"
#include "platform.h"
#include "settings.h"
#include "strutil.h"
#include "tinyfiledialogs.h"

#define AMIGA_MEMVIEW_TITLE "ENGINE9000 DEBUGGER - RAM"
#define AMIGA_MEMVIEW_DEFAULT_ROW_BYTES 40u
#define AMIGA_MEMVIEW_MAX_ROW_BYTES 512u
#define AMIGA_MEMVIEW_GUTTER_GAP_PX 8
#define AMIGA_MEMVIEW_OVERVIEW_GUTTER_PX 56
#define AMIGA_MEMVIEW_OVERVIEW_TEXTURE_W 64
#define AMIGA_MEMVIEW_OVERVIEW_RANGE_TEXTURE_H 128
#define AMIGA_MEMVIEW_OVERVIEW_MAX_RANGES 8
#define AMIGA_MEMVIEW_OVERVIEW_DEFAULT_RANGE0_BASE 0x000000u
#define AMIGA_MEMVIEW_OVERVIEW_DEFAULT_RANGE1_BASE 0xC00000u
#define AMIGA_MEMVIEW_OVERVIEW_FAST_RANGE_BASE 0x200000u
#define AMIGA_MEMVIEW_RIGHT_PAD_PX 16
#define AMIGA_MEMVIEW_TOP_PAD_PX 8
#define AMIGA_MEMVIEW_BOTTOM_PAD_PX 8
#define AMIGA_MEMVIEW_BIT_PX 2
#define AMIGA_MEMVIEW_ROW_PX 2
#define AMIGA_MEMVIEW_ZOOM_MIN 1
#define AMIGA_MEMVIEW_ZOOM_MAX 32
#define AMIGA_MEMVIEW_ZOOM_DEFAULT 8
#define AMIGA_MEMVIEW_REG_DDFSTRT 0x092u
#define AMIGA_MEMVIEW_REG_DDFSTOP 0x094u
#define AMIGA_MEMVIEW_REG_BPLCON0 0x100u
#define AMIGA_MEMVIEW_REG_BPL1MOD 0x108u
#define AMIGA_MEMVIEW_REG_BPL2MOD 0x10au
#define AMIGA_MEMVIEW_REG_BPL1PTH 0x0e0u
#define AMIGA_MEMVIEW_BPLPTR_COUNT 8
#define AMIGA_MEMVIEW_AUTO_CANDIDATE_MAX 64
#define AMIGA_MEMVIEW_EXPORT_MAX_RANGES 64
#define AMIGA_MEMVIEW_EXPORT_CHUNK 65536u

typedef struct amiga_memview_state amiga_memview_state_t;

typedef struct amiga_memview_overlay_body_state {
    amiga_memview_state_t *ui;
} amiga_memview_overlay_body_state_t;

typedef struct amiga_memview_step_buttons_action_ctx {
    amiga_memview_state_t *ui;
    e9ui_component_t *canvas;
} amiga_memview_step_buttons_action_ctx_t;

typedef struct amiga_memview_auto {
    uint32_t baseAddr;
    uint32_t rowBytes;
} amiga_memview_auto_t;

typedef struct amiga_memview_overview_range {
    uint32_t baseAddr;
    uint32_t sizeBytes;
} amiga_memview_overview_range_t;

typedef struct amiga_memview_toolbar_item_state {
    e9ui_component_t *child;
    int widthPx;
} amiga_memview_toolbar_item_state_t;

typedef struct amiga_memview_toolbar_wrap_state {
    int padPx;
    int gapPx;
} amiga_memview_toolbar_wrap_state_t;

typedef struct amiga_memview_legend_state {
    amiga_memview_state_t *ui;
} amiga_memview_legend_state_t;

struct amiga_memview_state {
    e9ui_window_state_t windowState;
    e9ui_context_t ctx;
    e9ui_component_t *root;
    e9ui_component_t *canvas;
    e9ui_component_t *autoButton;
    e9ui_component_t *addressBox;
    e9ui_component_t *widthBox;
    e9ui_component_t *widthSeek;
    e9ui_component_t *zoomSeek;
    uint32_t baseAddr;
    uint32_t rowBytes;
    int zoomLevel;
    int showAddressColumn;
    int showOverviewColumn;
    e9ui_step_buttons_state_t stepButtons;
    e9ui_scrollbar_state_t hScrollbar;
    int scrollX;
    int contentPixelWidth;
    uint8_t *readBuffer;
    size_t readBufferCap;
    uint32_t *blitTagBuffer;
    size_t blitTagBufferCap;
    SDL_Renderer *bitRenderer;
    SDL_Texture *bitTexture;
    uint32_t *bitPixels;
    size_t bitPixelsCap;
    int bitTextureW;
    int bitTextureH;
    int bitCacheValid;
    uint32_t bitCacheBaseAddr;
    uint32_t bitCacheRowBytes;
    int bitCacheFirstRow;
    int bitCacheLastRow;
    int bitCacheFirstBit;
    int bitCacheLastBit;
    uint64_t bitCacheFrameCounter;
    SDL_Renderer *overviewRenderer;
    SDL_Texture *overviewTexture;
    uint32_t *overviewPixels;
    size_t overviewPixelsCap;
    int overviewTextureW;
    int overviewTextureH;
    int overviewCacheValid;
    uint64_t overviewCacheFrameCounter;
    amiga_memview_overview_range_t overviewRanges[AMIGA_MEMVIEW_OVERVIEW_MAX_RANGES];
    int baseAddrHasSaved;
    int rowBytesHasSaved;
    int zoomHasSaved;
    int showAddressColumnHasSaved;
    int showOverviewColumnHasSaved;
    amiga_memview_auto_t autoCandidates[AMIGA_MEMVIEW_AUTO_CANDIDATE_MAX];
    int autoCandidateCount;
    int autoCandidateIndex;
    int (*widthSeekDefaultHandleEvent)(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev);
    int (*zoomSeekDefaultHandleEvent)(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev);
};

static amiga_memview_state_t amiga_memview_stateSingleton = {
    .windowState.winX = E9UI_WINDOW_COORD_UNSET,
    .windowState.winY = E9UI_WINDOW_COORD_UNSET,
    .windowState.openMinWidthNoSavedSizePx = 640,
    .windowState.openMinHeightNoSavedSizePx = 360,
    .windowState.openCenterWhenNoSaved = 1,
};

static const aux_window_ops_t amiga_memview_auxWindowOps = {
    .setFocus = amiga_memview_setMainWindowFocused,
    .render = amiga_memview_render,
};

enum
{
    amiga_memview_ram_type_chip = 0,
    amiga_memview_ram_type_slow,
    amiga_memview_ram_type_fast,
    amiga_memview_ram_type_other,
    amiga_memview_ram_type_count
};

static int
amiga_memview_measureAddressGutterPx(const e9ui_context_t *ctx, TTF_Font *font);

static void
amiga_memview_setView(amiga_memview_state_t *ui, uint32_t baseAddr, uint32_t rowBytes, int resetScroll);

static int
amiga_memview_clampZoomLevel(int zoomLevel);

static int
amiga_memview_leftGutterPx(const amiga_memview_state_t *ui, const e9ui_context_t *ctx, TTF_Font *font);

static int
amiga_memview_ramTypeFromBaseAddr(uint32_t baseAddr);

static void
amiga_memview_seekBarLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds);

static void
amiga_memview_saveWindowState(amiga_memview_state_t *ui);

static void
amiga_memview_syncAutoButtonLabel(amiga_memview_state_t *ui);

static uint16_t
amiga_memview_regValue(const e9k_debug_ami_custom_reg_state_t *regs, uint16_t regOffset)
{
    uint16_t normalized = (uint16_t)(regOffset & 0x01feu);
    return regs ? regs[normalized >> 1].value : 0u;
}

static const e9k_debug_ami_video_line_state_t *
amiga_memview_videoLineState(const e9k_debug_ami_video_line_state_t *videoLineStates, int videoLine)
{
    int coreLine = -1;

    if (!videoLineStates) {
        return NULL;
    }
    if (!libretro_host_debugAmiVideoLineToCoreLine(videoLine, &coreLine)) {
        return NULL;
    }
    if (coreLine < 0) {
        return NULL;
    }
    return &videoLineStates[coreLine];
}

static int
amiga_memview_bitPx(const amiga_memview_state_t *ui)
{
    int scaled = e9ui_scale_px(&ui->ctx, AMIGA_MEMVIEW_BIT_PX);
    int zoomLevel = ui ? amiga_memview_clampZoomLevel(ui->zoomLevel) : AMIGA_MEMVIEW_ZOOM_DEFAULT;

    scaled = (scaled * zoomLevel) / 8;
    if (scaled < 1) {
        scaled = 1;
    }
    return scaled;
}

static int
amiga_memview_rowPx(const amiga_memview_state_t *ui)
{
    int scaled = e9ui_scale_px(&ui->ctx, AMIGA_MEMVIEW_ROW_PX);
    int zoomLevel = ui ? amiga_memview_clampZoomLevel(ui->zoomLevel) : AMIGA_MEMVIEW_ZOOM_DEFAULT;

    scaled = (scaled * zoomLevel) / 8;
    if (scaled < 1) {
        scaled = 1;
    }
    return scaled;
}

static uint32_t
amiga_memview_regPointerValue(const e9k_debug_ami_custom_reg_state_t *regs, int planeIndex)
{
    uint16_t baseOffset = (uint16_t)(AMIGA_MEMVIEW_REG_BPL1PTH + (uint16_t)(planeIndex * 4));
    uint32_t hi = amiga_memview_regValue(regs, baseOffset);
    uint32_t lo = amiga_memview_regValue(regs, (uint16_t)(baseOffset + 2u));
    return ((hi << 16) | lo) & 0x00ffffffu;
}

static int
amiga_memview_parseInt(const char *value, int *out)
{
    if (!value || !out) {
        return 0;
    }
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (!end || end == value || *end != '\0') {
        return 0;
    }
    if (parsed < INT_MIN || parsed > INT_MAX) {
        return 0;
    }
    *out = (int)parsed;
    return 1;
}

static int
amiga_memview_parseU64SmartHex(const char *text, unsigned long long *outValue, char **outEnd)
{
    if (outValue) {
        *outValue = 0;
    }
    if (outEnd) {
        *outEnd = NULL;
    }
    if (!text || !outValue) {
        return 0;
    }

    const char *cursor = text;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') {
        cursor++;
    }

    int base = 0;
    const char *parseStart = cursor;
    if (*parseStart == '$') {
        parseStart++;
        base = 16;
    } else if (!(parseStart[0] == '0' && (parseStart[1] == 'x' || parseStart[1] == 'X'))) {
        for (const char *scan = parseStart; *scan; ++scan) {
            if (*scan == ' ' || *scan == '\t' || *scan == '\r' || *scan == '\n') {
                break;
            }
            if ((*scan >= 'a' && *scan <= 'f') || (*scan >= 'A' && *scan <= 'F')) {
                base = 16;
                break;
            }
        }
    }

    char *end = NULL;
    unsigned long long value = strtoull(parseStart, &end, base);
    if (outEnd) {
        *outEnd = end;
    }
    if (!end || end == parseStart) {
        return 0;
    }
    *outValue = value;
    return 1;
}

static int
amiga_memview_getAddressLimits(uint32_t *outMinAddr, uint32_t *outMaxAddr)
{
    if (outMinAddr) {
        *outMinAddr = 0u;
    }
    if (outMaxAddr) {
        *outMaxAddr = 0x00ffffffu;
    }
    if (target && target->memoryGetLimits) {
        return target->memoryGetLimits(outMinAddr, outMaxAddr);
    }
    return 0;
}

static uint32_t
amiga_memview_clampBaseAddr(uint32_t baseAddr)
{
    uint32_t minAddr = 0u;
    uint32_t maxAddr = 0x00ffffffu;
    if (!amiga_memview_getAddressLimits(&minAddr, &maxAddr)) {
        minAddr = 0u;
        maxAddr = 0x00ffffffu;
    }
    if (baseAddr < minAddr) {
        baseAddr = minAddr;
    }
    if (baseAddr > maxAddr) {
        baseAddr = maxAddr;
    }
    return baseAddr;
}

static uint32_t
amiga_memview_clampRowBytes(uint32_t rowBytes)
{
    if (rowBytes < 2u) {
        rowBytes = AMIGA_MEMVIEW_DEFAULT_ROW_BYTES;
    }
    if (rowBytes > AMIGA_MEMVIEW_MAX_ROW_BYTES) {
        rowBytes = AMIGA_MEMVIEW_MAX_ROW_BYTES;
    }
    rowBytes &= ~1u;
    if (rowBytes < 2u) {
        rowBytes = 2u;
    }
    return rowBytes;
}

static int
amiga_memview_clampZoomLevel(int zoomLevel)
{
    if (zoomLevel < AMIGA_MEMVIEW_ZOOM_MIN) {
        zoomLevel = AMIGA_MEMVIEW_ZOOM_MIN;
    }
    if (zoomLevel > AMIGA_MEMVIEW_ZOOM_MAX) {
        zoomLevel = AMIGA_MEMVIEW_ZOOM_MAX;
    }
    return zoomLevel;
}

static void
amiga_memview_saveWindowState(amiga_memview_state_t *ui)
{
    if (!ui) {
        return;
    }
    if (ui->windowState.open && ui->windowState.windowHost) {
        (void)e9ui_windowCaptureStateRectSnapshot(&ui->windowState, &e9ui->ctx);
    }
    config_saveConfig();
}

static void
amiga_memview_syncAutoButtonLabel(amiga_memview_state_t *ui)
{
    char label[32];

    if (!ui || !ui->autoButton) {
        return;
    }
    if (ui->autoCandidateCount > 0 && ui->autoCandidateIndex >= 0 && ui->autoCandidateIndex < ui->autoCandidateCount) {
        snprintf(label,
                 sizeof(label),
                 "Auto (%d/%d)",
                 ui->autoCandidateIndex + 1,
                 ui->autoCandidateCount);
    } else {
        snprintf(label, sizeof(label), "Auto");
    }
    e9ui_button_setLabel(ui->autoButton, label);
}

static float
amiga_memview_widthSeekPercent(uint32_t rowBytes)
{
    uint32_t clamped = amiga_memview_clampRowBytes(rowBytes);
    uint32_t stepCount = AMIGA_MEMVIEW_MAX_ROW_BYTES / 2u;

    if (stepCount <= 1u) {
        return 0.0f;
    }
    return (float)((clamped / 2u) - 1u) / (float)(stepCount - 1u);
}

static uint32_t
amiga_memview_widthSeekRowBytes(float percent)
{
    float clamped = percent;
    uint32_t stepCount = AMIGA_MEMVIEW_MAX_ROW_BYTES / 2u;
    uint32_t stepIndex = 0u;
    uint32_t rowBytes = 2u;

    if (clamped < 0.0f) {
        clamped = 0.0f;
    }
    if (clamped > 1.0f) {
        clamped = 1.0f;
    }
    if (stepCount > 1u) {
        stepIndex = (uint32_t)(clamped * (float)(stepCount - 1u) + 0.5f);
    }
    rowBytes = 2u + stepIndex * 2u;
    return amiga_memview_clampRowBytes(rowBytes);
}

static void
amiga_memview_widthSeekTooltip(float percent, char *out, size_t cap, void *user)
{
    amiga_memview_state_t *ui = (amiga_memview_state_t*)user;
    uint32_t rowBytes = amiga_memview_widthSeekRowBytes(percent);

    (void)ui;
    if (!out || cap == 0u) {
        return;
    }
    snprintf(out, cap, "Width %u words (%u bytes)", (unsigned)(rowBytes / 2u), (unsigned)rowBytes);
}

static int
amiga_memview_handleWidthKey(amiga_memview_state_t *ui, SDL_Keycode key, SDL_Keymod mod)
{
    uint32_t nextRowBytes = 0u;

    if (!ui) {
        return 0;
    }

    (void)mod;
    nextRowBytes = ui->rowBytes;
    switch (key) {
    case SDLK_LEFT:
    case SDLK_DOWN:
        nextRowBytes = ui->rowBytes > 2u ? ui->rowBytes - 2u : 2u;
        break;
    case SDLK_RIGHT:
    case SDLK_UP:
        nextRowBytes = ui->rowBytes + 2u;
        break;
    case SDLK_HOME:
        nextRowBytes = 2u;
        break;
    case SDLK_END:
        nextRowBytes = AMIGA_MEMVIEW_MAX_ROW_BYTES;
        break;
    default:
        return 0;
    }

    nextRowBytes = amiga_memview_clampRowBytes(nextRowBytes);
    if (nextRowBytes == ui->rowBytes) {
        return 1;
    }
    amiga_memview_setView(ui, ui->baseAddr, nextRowBytes, 1);
    return 1;
}

static float
amiga_memview_zoomSeekPercent(int zoomLevel)
{
    int clamped = amiga_memview_clampZoomLevel(zoomLevel);

    if (AMIGA_MEMVIEW_ZOOM_MAX <= AMIGA_MEMVIEW_ZOOM_MIN) {
        return 0.0f;
    }
    return (float)(clamped - AMIGA_MEMVIEW_ZOOM_MIN) /
           (float)(AMIGA_MEMVIEW_ZOOM_MAX - AMIGA_MEMVIEW_ZOOM_MIN);
}

static int
amiga_memview_zoomSeekLevel(float percent)
{
    float clamped = percent;
    int zoomLevel = AMIGA_MEMVIEW_ZOOM_DEFAULT;

    if (clamped < 0.0f) {
        clamped = 0.0f;
    }
    if (clamped > 1.0f) {
        clamped = 1.0f;
    }
    zoomLevel = AMIGA_MEMVIEW_ZOOM_MIN +
        (int)(clamped * (float)(AMIGA_MEMVIEW_ZOOM_MAX - AMIGA_MEMVIEW_ZOOM_MIN) + 0.5f);
    return amiga_memview_clampZoomLevel(zoomLevel);
}

static void
amiga_memview_zoomSeekTooltip(float percent, char *out, size_t cap, void *user)
{
    amiga_memview_state_t *ui = (amiga_memview_state_t*)user;
    int zoomLevel = amiga_memview_zoomSeekLevel(percent);
    int whole = zoomLevel / 8;
    int frac = zoomLevel & 7;

    (void)ui;
    if (!out || cap == 0u) {
        return;
    }
    if (frac == 0) {
        snprintf(out, cap, "Zoom %dx", whole);
    } else if (frac == 1) {
        snprintf(out, cap, "Zoom %d.125x", whole);
    } else if (frac == 2) {
        snprintf(out, cap, "Zoom %d.25x", whole);
    } else if (frac == 3) {
        snprintf(out, cap, "Zoom %d.375x", whole);
    } else if (frac == 4) {
        snprintf(out, cap, "Zoom %d.5x", whole);
    } else if (frac == 5) {
        snprintf(out, cap, "Zoom %d.625x", whole);
    } else if (frac == 6) {
        snprintf(out, cap, "Zoom %d.75x", whole);
    } else {
        snprintf(out, cap, "Zoom %d.875x", whole);
    }
}

static void
amiga_memview_syncTextboxesFromState(amiga_memview_state_t *ui)
{
    char buffer[32];

    if (!ui) {
        return;
    }

    if (ui->addressBox) {
        snprintf(buffer, sizeof(buffer), "0x%06X", (unsigned)(ui->baseAddr & 0x00ffffffu));
        e9ui_textbox_setText(ui->addressBox, buffer);
    }
    if (ui->widthBox) {
        snprintf(buffer, sizeof(buffer), "%u", (unsigned)(ui->rowBytes / 2u));
        e9ui_textbox_setText(ui->widthBox, buffer);
    }
    if (ui->widthSeek) {
        e9ui_seek_bar_setPercent(ui->widthSeek, amiga_memview_widthSeekPercent(ui->rowBytes));
    }
    if (ui->zoomSeek) {
        e9ui_seek_bar_setPercent(ui->zoomSeek, amiga_memview_zoomSeekPercent(ui->zoomLevel));
    }
}

static void
amiga_memview_scrollReset(amiga_memview_state_t *ui)
{
    if (!ui) {
        return;
    }
    ui->scrollX = 0;
}

static int
amiga_memview_widthSeekHandleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    amiga_memview_state_t *ui = &amiga_memview_stateSingleton;

    if (!self || !ctx || !ev) {
        return 0;
    }
    if (ui->widthSeek != self) {
        return 0;
    }
    if (ev->type == SDL_KEYDOWN && e9ui_getFocus(ctx) == self) {
        if (amiga_memview_handleWidthKey(ui, ev->key.keysym.sym, ev->key.keysym.mod)) {
            return 1;
        }
    }
    if (ui->widthSeekDefaultHandleEvent) {
        return ui->widthSeekDefaultHandleEvent(self, ctx, ev);
    }
    return 0;
}

static void
amiga_memview_onZoomSeekChanged(float percent, void *user)
{
    amiga_memview_state_t *ui = (amiga_memview_state_t*)user;
    int zoomLevel = 0;

    if (!ui) {
        return;
    }
    zoomLevel = amiga_memview_zoomSeekLevel(percent);
    if (zoomLevel == ui->zoomLevel) {
        return;
    }
    ui->zoomLevel = zoomLevel;
    ui->zoomHasSaved = 1;
    ui->bitCacheValid = 0;
    amiga_memview_syncTextboxesFromState(ui);
    amiga_memview_saveWindowState(ui);
}

static void
amiga_memview_initSeekBar(e9ui_component_t *seekBar,
                          int (*handleEvent)(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev),
                          void (*changedCallback)(float percent, void *user),
                          e9ui_seek_bar_tooltip_cb_t tooltipCallback,
                          void *user,
                          int (**outDefaultHandleEvent)(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev))
{
    seekBar->layout = amiga_memview_seekBarLayout;
    *outDefaultHandleEvent = seekBar->handleEvent;
    seekBar->handleEvent = handleEvent;
    e9ui_seek_bar_setMargins(seekBar, 0, 0, 0);
    e9ui_seek_bar_setHeight(seekBar, 14);
    e9ui_seek_bar_setHoverMargin(seekBar, 6);
    e9ui_seek_bar_setCallback(seekBar, changedCallback, user);
    e9ui_seek_bar_setTooltipCallback(seekBar, tooltipCallback, user);
}

static int
amiga_memview_zoomSeekHandleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    amiga_memview_state_t *ui = &amiga_memview_stateSingleton;
    int nextZoomLevel = 0;
    int shift = 0;

    if (!self || !ctx || !ev) {
        return 0;
    }
    if (ui->zoomSeek != self) {
        return 0;
    }
    if (ev->type == SDL_KEYDOWN && e9ui_getFocus(ctx) == self) {
        shift = (ev->key.keysym.mod & KMOD_SHIFT) ? 1 : 0;
        nextZoomLevel = ui->zoomLevel;
        switch (ev->key.keysym.sym) {
        case SDLK_LEFT:
        case SDLK_DOWN:
            nextZoomLevel -= shift ? 2 : 1;
            break;
        case SDLK_RIGHT:
        case SDLK_UP:
            nextZoomLevel += shift ? 2 : 1;
            break;
        case SDLK_HOME:
            nextZoomLevel = AMIGA_MEMVIEW_ZOOM_MIN;
            break;
        case SDLK_END:
            nextZoomLevel = AMIGA_MEMVIEW_ZOOM_MAX;
            break;
        default:
            nextZoomLevel = 0;
            break;
        }
        if (nextZoomLevel != 0) {
            ui->zoomLevel = amiga_memview_clampZoomLevel(nextZoomLevel);
            ui->zoomHasSaved = 1;
            ui->bitCacheValid = 0;
            amiga_memview_syncTextboxesFromState(ui);
            amiga_memview_saveWindowState(ui);
            return 1;
        }
    }
    if (ui->zoomSeekDefaultHandleEvent) {
        return ui->zoomSeekDefaultHandleEvent(self, ctx, ev);
    }
    return 0;
}

static int
amiga_memview_ensureReadBuffer(amiga_memview_state_t *ui, size_t need)
{
    uint8_t *resized = NULL;

    if (!ui) {
        return 0;
    }
    if (need <= ui->readBufferCap) {
        return 1;
    }
    resized = (uint8_t*)alloc_realloc(ui->readBuffer, need);
    if (!resized) {
        return 0;
    }
    ui->readBuffer = resized;
    ui->readBufferCap = need;
    return 1;
}

static void
amiga_memview_zeroBlitTagBuffer(amiga_memview_state_t *ui, size_t wordCount)
{
    if (!ui || !ui->blitTagBuffer || wordCount == 0u) {
        return;
    }
    memset(ui->blitTagBuffer, 0, wordCount * sizeof(*ui->blitTagBuffer));
}

static int
amiga_memview_ensureBlitTagBuffer(amiga_memview_state_t *ui, size_t needWords)
{
    uint32_t *resized = NULL;

    if (!ui) {
        return 0;
    }
    if (needWords <= ui->blitTagBufferCap) {
        return 1;
    }
    resized = (uint32_t*)alloc_realloc(ui->blitTagBuffer, needWords * sizeof(*resized));
    if (!resized) {
        return 0;
    }
    ui->blitTagBuffer = resized;
    ui->blitTagBufferCap = needWords;
    return 1;
}

static void
amiga_memview_discardBitTexture(amiga_memview_state_t *ui)
{
    if (!ui) {
        return;
    }
    if (ui->bitTexture) {
        SDL_DestroyTexture(ui->bitTexture);
        ui->bitTexture = NULL;
    }
    ui->bitRenderer = NULL;
    ui->bitTextureW = 0;
    ui->bitTextureH = 0;
    ui->bitCacheValid = 0;
}

static int
amiga_memview_ensureOverviewPixels(amiga_memview_state_t *ui, size_t needPixels)
{
    uint32_t *resized = NULL;

    if (!ui) {
        return 0;
    }
    if (needPixels <= ui->overviewPixelsCap) {
        return 1;
    }
    resized = (uint32_t*)alloc_realloc(ui->overviewPixels, needPixels * sizeof(*resized));
    if (!resized) {
        return 0;
    }
    ui->overviewPixels = resized;
    ui->overviewPixelsCap = needPixels;
    return 1;
}

static void
amiga_memview_discardOverviewTexture(amiga_memview_state_t *ui)
{
    if (!ui) {
        return;
    }
    if (ui->overviewTexture) {
        SDL_DestroyTexture(ui->overviewTexture);
        ui->overviewTexture = NULL;
    }
    ui->overviewRenderer = NULL;
    ui->overviewTextureW = 0;
    ui->overviewTextureH = 0;
    ui->overviewCacheValid = 0;
}

static int
amiga_memview_ensureOverviewTexture(amiga_memview_state_t *ui, SDL_Renderer *renderer, int width, int height)
{
    if (!ui || !renderer || width <= 0 || height <= 0) {
        return 0;
    }

    if (ui->overviewTexture &&
        (ui->overviewRenderer != renderer || ui->overviewTextureW != width || ui->overviewTextureH != height)) {
        amiga_memview_discardOverviewTexture(ui);
    }

    if (!ui->overviewTexture) {
        ui->overviewTexture = SDL_CreateTexture(renderer,
                                                SDL_PIXELFORMAT_ARGB8888,
                                                SDL_TEXTUREACCESS_STREAMING,
                                                width,
                                                height);
        if (!ui->overviewTexture) {
            return 0;
        }
        SDL_SetTextureBlendMode(ui->overviewTexture, SDL_BLENDMODE_BLEND);
        ui->overviewRenderer = renderer;
        ui->overviewTextureW = width;
        ui->overviewTextureH = height;
        ui->overviewCacheValid = 0;
    }

    return 1;
}

static int
amiga_memview_ensureBitPixels(amiga_memview_state_t *ui, size_t needPixels)
{
    uint32_t *resized = NULL;

    if (!ui) {
        return 0;
    }
    if (needPixels <= ui->bitPixelsCap) {
        return 1;
    }
    resized = (uint32_t*)alloc_realloc(ui->bitPixels, needPixels * sizeof(*resized));
    if (!resized) {
        return 0;
    }
    ui->bitPixels = resized;
    ui->bitPixelsCap = needPixels;
    return 1;
}

static int
amiga_memview_ensureBitTexture(amiga_memview_state_t *ui, SDL_Renderer *renderer, int width, int height)
{
    if (!ui || !renderer || width <= 0 || height <= 0) {
        return 0;
    }

    if (ui->bitTexture &&
        (ui->bitRenderer != renderer || ui->bitTextureW != width || ui->bitTextureH != height)) {
        amiga_memview_discardBitTexture(ui);
    }

    if (!ui->bitTexture) {
        ui->bitTexture = SDL_CreateTexture(renderer,
                                           SDL_PIXELFORMAT_ARGB8888,
                                           SDL_TEXTUREACCESS_STREAMING,
                                           width,
                                           height);
        if (!ui->bitTexture) {
            return 0;
        }
        SDL_SetTextureBlendMode(ui->bitTexture, SDL_BLENDMODE_BLEND);
        ui->bitRenderer = renderer;
        ui->bitTextureW = width;
        ui->bitTextureH = height;
        ui->bitCacheValid = 0;
    }

    return 1;
}

static int
amiga_memview_readRange(amiga_memview_state_t *ui, uint32_t baseAddr, uint8_t *data, size_t size)
{
    uint32_t minAddr = 0u;
    uint32_t maxAddr = 0x00ffffffu;
    uint64_t readStart = 0u;
    uint64_t readEnd = 0u;
    int hasLimits = 0;

    if (!ui || !data || size == 0u) {
        return 0;
    }

    memset(data, 0, size);
    hasLimits = amiga_memview_getAddressLimits(&minAddr, &maxAddr);
    if (!hasLimits) {
        minAddr = 0u;
        maxAddr = 0x00ffffffu;
    }

    readStart = (uint64_t)(baseAddr & 0x00ffffffu);
    readEnd = readStart + (uint64_t)size - 1u;
    if (readStart > (uint64_t)maxAddr || readEnd < (uint64_t)minAddr) {
        return 0;
    }
    if (readStart < (uint64_t)minAddr) {
        readStart = minAddr;
    }
    if (readEnd > (uint64_t)maxAddr) {
        readEnd = maxAddr;
    }
    if (readEnd < readStart) {
        return 0;
    }

    size_t dstOffset = (size_t)(readStart - (uint64_t)(baseAddr & 0x00ffffffu));
    size_t readSize = (size_t)(readEnd - readStart + 1u);
    if (dstOffset + readSize > size) {
        return 0;
    }
    return libretro_host_debugReadMemory((uint32_t)readStart, data + dstOffset, readSize) ? 1 : 0;
}


static uint32_t
amiga_memview_fetchBytesAuto(uint16_t ddfstrt, uint16_t ddfstop, uint16_t bplcon0)
{
    uint32_t start = ddfstrt & 0x01f8u;
    uint32_t stop = ddfstop & 0x01f8u;
    uint32_t fetchWords = 0u;
    uint32_t resolutionScale = 1u;

    if (stop < start) {
        uint32_t tmp = start;
        start = stop;
        stop = tmp;
    }
    if (stop >= start) {
        fetchWords = ((stop - start) >> 3) + 1u;
    }
    if ((bplcon0 & (1u << 6)) != 0u) {
        resolutionScale = 4u;
    } else if ((bplcon0 & (1u << 15)) != 0u) {
        resolutionScale = 2u;
    }
    if (fetchWords == 0u) {
        return 0u;
    }
    return fetchWords * resolutionScale * 2u;
}

static int
amiga_memview_appendAutoIfUnique(amiga_memview_auto_t *outAutos, int autoCount, int maxAutos, const amiga_memview_auto_t *autoState)
{
    if (!outAutos || !autoState || autoCount < 0 || autoCount >= maxAutos) {
        return autoCount;
    }
    for (int i = 0; i < autoCount; ++i) {
        if (outAutos[i].baseAddr == autoState->baseAddr &&
            outAutos[i].rowBytes == autoState->rowBytes) {
            return autoCount;
        }
    }
    outAutos[autoCount++] = *autoState;
    return autoCount;
}

static int
amiga_memview_collectRegisterAuto(amiga_memview_auto_t *outAutos, int maxAutos, const amiga_memview_auto_t *fallback)
{
    const e9k_debug_ami_custom_reg_state_t *regs = libretro_host_debugAmiGetCustomRegs();
    int autoCount = 0;

    if (!outAutos || maxAutos <= 0 || !fallback) {
        return 0;
    }

    if (regs) {
        uint16_t bplcon0 = amiga_memview_regValue(regs, AMIGA_MEMVIEW_REG_BPLCON0);
        uint16_t ddfstrt = amiga_memview_regValue(regs, AMIGA_MEMVIEW_REG_DDFSTRT);
        uint16_t ddfstop = amiga_memview_regValue(regs, AMIGA_MEMVIEW_REG_DDFSTOP);
        int planeCount = (int)((bplcon0 >> 12) & 0x7u);
        uint32_t fetchBytes = amiga_memview_fetchBytesAuto(ddfstrt, ddfstop, bplcon0);

        for (int i = 0; i < AMIGA_MEMVIEW_BPLPTR_COUNT; ++i) {
            uint32_t ptr = amiga_memview_regPointerValue(regs, i);
            amiga_memview_auto_t autoState = *fallback;

            if (planeCount > 0 && i >= planeCount && ptr == 0u) {
                continue;
            }
            if (ptr == 0u) {
                continue;
            }

            int moduloBytes = (i & 1) == 0 ?
                (int16_t)amiga_memview_regValue(regs, AMIGA_MEMVIEW_REG_BPL1MOD) :
                (int16_t)amiga_memview_regValue(regs, AMIGA_MEMVIEW_REG_BPL2MOD);
            int rowBytes = (int)fetchBytes + moduloBytes;

            autoState.baseAddr = amiga_memview_clampBaseAddr(ptr & 0x00ffffffu);
            if (rowBytes <= 0) {
                continue;
            }
            autoState.rowBytes = amiga_memview_clampRowBytes((uint32_t)rowBytes);

            autoCount = amiga_memview_appendAutoIfUnique(outAutos, autoCount, maxAutos, &autoState);
        }
    }

    return autoCount;
}

static int
amiga_memview_collectLineAuto(amiga_memview_auto_t *outAutos, int maxAutos, const amiga_memview_auto_t *fallback)
{
    const e9k_debug_ami_video_line_state_t *videoLineStates = NULL;
    int autoCount = 0;
    int lineCount = 0;

    if (!outAutos || maxAutos <= 0 || !fallback) {
        return 0;
    }
    if (!libretro_host_debugAmiGetVideoLineCount(&lineCount) || lineCount < 3) {
        return 0;
    }
    videoLineStates = libretro_host_debugAmiGetVideoLineStates();
    if (!videoLineStates) {
        return 0;
    }

    for (int plane = 0; plane < AMIGA_MEMVIEW_BPLPTR_COUNT && autoCount < maxAutos; ++plane) {
        int y = 0;

        while (y < lineCount - 2 && autoCount < maxAutos) {
            const e9k_debug_ami_video_line_state_t *line0 = amiga_memview_videoLineState(videoLineStates, y);
            const e9k_debug_ami_video_line_state_t *line1 = amiga_memview_videoLineState(videoLineStates, y + 1);
            const e9k_debug_ami_video_line_state_t *line2 = amiga_memview_videoLineState(videoLineStates, y + 2);
            if (!line0 || !line1 || !line2) {
                y++;
                continue;
            }
            uint32_t ptr0 = line0->ptr[plane];
            uint32_t ptr1 = line1->ptr[plane];
            uint32_t ptr2 = line2->ptr[plane];

            if (ptr0 == E9K_DEBUG_AMI_VIDEO_LINE_INVALID_PTR ||
                ptr1 == E9K_DEBUG_AMI_VIDEO_LINE_INVALID_PTR ||
                ptr2 == E9K_DEBUG_AMI_VIDEO_LINE_INVALID_PTR) {
                y++;
                continue;
            }

            int64_t delta = (int64_t)ptr1 - (int64_t)ptr0;
            int64_t deltaNext = (int64_t)ptr2 - (int64_t)ptr1;

            if (delta == 0 || delta != deltaNext) {
                y++;
                continue;
            }

            int h = 1;
            while (y + h < lineCount) {
                const e9k_debug_ami_video_line_state_t *prevLine = amiga_memview_videoLineState(videoLineStates, y + h - 1);
                const e9k_debug_ami_video_line_state_t *currLine = amiga_memview_videoLineState(videoLineStates, y + h);
                if (!prevLine || !currLine) {
                    break;
                }
                uint32_t prevPtr = prevLine->ptr[plane];
                uint32_t currPtr = currLine->ptr[plane];

                if (prevPtr == E9K_DEBUG_AMI_VIDEO_LINE_INVALID_PTR ||
                    currPtr == E9K_DEBUG_AMI_VIDEO_LINE_INVALID_PTR) {
                    break;
                }
                if (((int64_t)currPtr - (int64_t)prevPtr) != delta) {
                    break;
                }
                h++;
            }

            uint32_t baseAddr = ptr0;
            int64_t rowBytes64 = delta;
            if (rowBytes64 < 0) {
                int64_t base64 = (int64_t)ptr0 + rowBytes64 * h;
                if (base64 < 0 || base64 > 0x00ffffffll) {
                    y += h;
                    continue;
                }
                baseAddr = (uint32_t)base64;
                rowBytes64 = -rowBytes64;
            }
            if (rowBytes64 <= 0 || rowBytes64 > (int64_t)AMIGA_MEMVIEW_MAX_ROW_BYTES) {
                y += h;
                continue;
            }

            amiga_memview_auto_t autoState = *fallback;
            autoState.baseAddr = amiga_memview_clampBaseAddr(baseAddr);
            autoState.rowBytes = amiga_memview_clampRowBytes((uint32_t)rowBytes64);
            autoCount = amiga_memview_appendAutoIfUnique(outAutos, autoCount, maxAutos, &autoState);

            y += h;
        }
    }
    return autoCount;
}

static int
amiga_memview_collectAuto(amiga_memview_auto_t *outAutos, int maxAutos)
{
    amiga_memview_auto_t fallback = {
        .baseAddr = 0u,
        .rowBytes = AMIGA_MEMVIEW_DEFAULT_ROW_BYTES,
    };
    int autoCount = 0;

    if (!outAutos || maxAutos <= 0) {
        return 0;
    }

    autoCount = amiga_memview_collectLineAuto(outAutos, maxAutos, &fallback);
    if (autoCount == 0) {
        autoCount = amiga_memview_collectRegisterAuto(outAutos, maxAutos, &fallback);
    }
    if (autoCount == 0) {
        outAutos[0] = fallback;
        autoCount = 1;
    }
    for (int i = 0; i < autoCount; ++i) {
        outAutos[i].baseAddr = amiga_memview_clampBaseAddr(outAutos[i].baseAddr);
        outAutos[i].rowBytes = amiga_memview_clampRowBytes(outAutos[i].rowBytes);
    }
    return autoCount;
}

static void
amiga_memview_setView(amiga_memview_state_t *ui, uint32_t baseAddr, uint32_t rowBytes, int resetScroll)
{
    if (!ui) {
        return;
    }
    ui->baseAddr = amiga_memview_clampBaseAddr(baseAddr);
    ui->rowBytes = amiga_memview_clampRowBytes(rowBytes);
    ui->baseAddrHasSaved = 1;
    ui->rowBytesHasSaved = 1;
    ui->bitCacheValid = 0;
    amiga_memview_syncTextboxesFromState(ui);
    amiga_memview_syncAutoButtonLabel(ui);
    if (resetScroll) {
        amiga_memview_scrollReset(ui);
    }
    amiga_memview_saveWindowState(ui);
}

static void
amiga_memview_applyAuto(amiga_memview_state_t *ui, int resetScroll)
{
    amiga_memview_auto_t autos[AMIGA_MEMVIEW_AUTO_CANDIDATE_MAX];
    amiga_memview_auto_t *autoState = NULL;
    int autoIndex = 0;
    int refreshList = 0;

    if (!ui) {
        return;
    }
    if (ui->autoCandidateCount <= 0) {
        refreshList = 1;
    } else {
        autoIndex = ui->autoCandidateIndex + 1;
        if (autoIndex >= ui->autoCandidateCount) {
            refreshList = 1;
            autoIndex = 0;
        }
    }
    if (refreshList) {
        int autoCount = 0;

        autoCount = amiga_memview_collectAuto(autos, AMIGA_MEMVIEW_AUTO_CANDIDATE_MAX);
        if (autoCount <= 0) {
            return;
        }
        memcpy(ui->autoCandidates, autos, sizeof(amiga_memview_auto_t) * (size_t)autoCount);
        ui->autoCandidateCount = autoCount;
        autoIndex = 0;
    }
    ui->autoCandidateIndex = autoIndex;
    autoState = &ui->autoCandidates[autoIndex];
    amiga_memview_setView(ui, autoState->baseAddr, autoState->rowBytes, resetScroll);
}

static int
amiga_memview_parseAddressTextbox(amiga_memview_state_t *ui, uint32_t *outBaseAddr)
{
    unsigned long long parsed = 0u;
    char *end = NULL;
    const char *text = NULL;
    uint32_t minAddr = 0u;
    uint32_t maxAddr = 0x00ffffffu;

    if (outBaseAddr) {
        *outBaseAddr = 0u;
    }
    if (!ui || !ui->addressBox || !outBaseAddr) {
        return 0;
    }

    text = e9ui_textbox_getText(ui->addressBox);
    if (!amiga_memview_parseU64SmartHex(text, &parsed, &end) || !end || *end != '\0') {
        return 0;
    }
    if (parsed > 0x00ffffffull) {
        return 0;
    }
    if (!amiga_memview_getAddressLimits(&minAddr, &maxAddr)) {
        minAddr = 0u;
        maxAddr = 0x00ffffffu;
    }
    if ((uint32_t)parsed < minAddr || (uint32_t)parsed > maxAddr) {
        return 0;
    }
    *outBaseAddr = (uint32_t)parsed;
    return 1;
}

static int
amiga_memview_parseWidthTextbox(amiga_memview_state_t *ui, uint32_t *outRowBytes)
{
    unsigned long long parsed = 0u;
    char *end = NULL;
    const char *text = NULL;

    if (outRowBytes) {
        *outRowBytes = 0u;
    }
    if (!ui || !ui->widthBox || !outRowBytes) {
        return 0;
    }

    text = e9ui_textbox_getText(ui->widthBox);
    if (!amiga_memview_parseU64SmartHex(text, &parsed, &end) || !end || *end != '\0') {
        return 0;
    }
    if (parsed == 0u) {
        return 0;
    }
    if (parsed > (AMIGA_MEMVIEW_MAX_ROW_BYTES / 2u)) {
        return 0;
    }
    *outRowBytes = (uint32_t)parsed * 2u;
    return 1;
}

static void
amiga_memview_submitFromTextboxes(amiga_memview_state_t *ui)
{
    uint32_t baseAddr = 0u;
    uint32_t rowBytes = 0u;

    if (!ui) {
        return;
    }
    if (!amiga_memview_parseAddressTextbox(ui, &baseAddr)) {
        return;
    }
    if (!amiga_memview_parseWidthTextbox(ui, &rowBytes)) {
        return;
    }
    amiga_memview_setView(ui, baseAddr, rowBytes, 1);
}

static void
amiga_memview_onAddressSubmit(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    amiga_memview_submitFromTextboxes((amiga_memview_state_t*)user);
}

static void
amiga_memview_onWidthSubmit(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    amiga_memview_submitFromTextboxes((amiga_memview_state_t*)user);
}

static void
amiga_memview_onWidthSeekChanged(float percent, void *user)
{
    amiga_memview_state_t *ui = (amiga_memview_state_t*)user;
    uint32_t rowBytes = 0u;

    if (!ui) {
        return;
    }
    rowBytes = amiga_memview_widthSeekRowBytes(percent);
    if (rowBytes == ui->rowBytes) {
        return;
    }
    amiga_memview_setView(ui, ui->baseAddr, rowBytes, 1);
}

static void
amiga_memview_onAuto(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    amiga_memview_applyAuto((amiga_memview_state_t*)user, 1);
}

static int
amiga_memview_copyConfigName(char *out, size_t cap, const char *configPath)
{
    const char *base = NULL;
    const char *slash = NULL;
    const char *back = NULL;
    const char *dot = NULL;

    if (!out || cap == 0u || !configPath || !configPath[0]) {
        return 0;
    }

    slash = strrchr(configPath, '/');
    back = strrchr(configPath, '\\');
    base = slash > back ? slash : back;
    base = base ? base + 1 : configPath;
    if (!base || !base[0]) {
        return 0;
    }

    strutil_strlcpy(out, cap, base);
    if (out[0] == '\0') {
        return 0;
    }
    dot = strrchr(out, '.');
    if (dot && dot > out) {
        * (char*)dot = '\0';
    }
    if (out[0] == '\0') {
        return 0;
    }
    return 1;
}

static const char *
amiga_memview_ramTypeFileTag(int ramType)
{
    switch (ramType) {
    case amiga_memview_ram_type_chip:
        return "chip";
    case amiga_memview_ram_type_slow:
        return "slow";
    case amiga_memview_ram_type_fast:
        return "fast";
    default:
        return "other";
    }
}

static int
amiga_memview_buildExportFileName(char *out, size_t cap, const char *configName, int ramType)
{
    char stem[PATH_MAX];
    const char *tag = amiga_memview_ramTypeFileTag(ramType);
    size_t stemLen = 0u;

    if (!out || cap == 0u || !configName || !configName[0] || !tag || !tag[0]) {
        return 0;
    }
    strutil_join3Trunc(stem, sizeof(stem), configName, "-", tag);
    stemLen = strlen(configName) + 1u + strlen(tag);
    if (strlen(stem) != stemLen) {
        return 0;
    }
    strutil_join2Trunc(out, cap, stem, ".bin");
    if (strlen(out) != stemLen + 4u) {
        return 0;
    }
    return 1;
}

static int
amiga_memview_collectExportRanges(target_memory_range_t *outRanges, size_t cap, size_t *outCount)
{
    size_t count = 0u;
    size_t write = 0u;

    *outCount = 0u;
    if (!target || !target->memoryTrackGetRanges) {
        return 0;
    }
    if (!target->memoryTrackGetRanges(outRanges, cap, &count) || count == 0u) {
        return 0;
    }
    for (size_t i = 0; i < count; ++i) {
        if (outRanges[i].size == 0u) {
            continue;
        }
        outRanges[write++] = outRanges[i];
    }
    *outCount = write;
    return write > 0u ? 1 : 0;
}

static int
amiga_memview_buildOverwriteMessage(char *out, size_t cap, const char *const *paths, int pathCount)
{
    size_t pos = 0u;
    const char *header = "These files already exist:\n";
    const char *footer = "\n\nOverwrite these RAM dumps?";

    if (!out || cap == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (strlen(header) >= cap) {
        return 0;
    }
    memcpy(out, header, strlen(header) + 1u);
    pos = strlen(header);
    for (int i = 0; i < pathCount; ++i) {
        size_t pathLen = 0u;

        if (!paths[i] || !paths[i][0]) {
            continue;
        }
        pathLen = strlen(paths[i]);
        if (pos + pathLen >= cap) {
            return 0;
        }
        memcpy(out + pos, paths[i], pathLen);
        pos += pathLen;
        out[pos] = '\0';
        if (i + 1 < pathCount) {
            if (pos + 1u >= cap) {
                return 0;
            }
            out[pos++] = '\n';
            out[pos] = '\0';
        }
    }
    if (pos + strlen(footer) >= cap) {
        return 0;
    }
    memcpy(out + pos, footer, strlen(footer) + 1u);
    return 1;
}

static int
amiga_memview_writeExportType(amiga_memview_state_t *ui,
                              const target_memory_range_t *ranges,
                              size_t rangeCount,
                              int ramType,
                              const char *finalPath)
{
    uint8_t buffer[AMIGA_MEMVIEW_EXPORT_CHUNK];
    char tempPath[PATH_MAX];
    FILE *out = NULL;
    int wroteAny = 0;

    if (!ui || !ranges || rangeCount == 0u || !finalPath || !finalPath[0]) {
        return 0;
    }
    strutil_join2Trunc(tempPath, sizeof(tempPath), finalPath, ".tmp");
    if (strlen(tempPath) != strlen(finalPath) + 4u) {
        return 0;
    }

    out = fopen(tempPath, "wb");
    if (!out) {
        return 0;
    }

    for (size_t i = 0; i < rangeCount; ++i) {
        uint32_t addr = 0u;
        uint32_t remaining = 0u;

        if (ranges[i].size == 0u || amiga_memview_ramTypeFromBaseAddr(ranges[i].baseAddr) != ramType) {
            continue;
        }

        addr = ranges[i].baseAddr;
        remaining = ranges[i].size;
        wroteAny = 1;
        while (remaining > 0u) {
            size_t chunkSize = remaining < (uint32_t)sizeof(buffer) ? (size_t)remaining : sizeof(buffer);

            if (!amiga_memview_readRange(ui, addr, buffer, chunkSize)) {
                fclose(out);
                remove(tempPath);
                return 0;
            }
            if (fwrite(buffer, 1u, chunkSize, out) != chunkSize) {
                fclose(out);
                remove(tempPath);
                return 0;
            }
            addr += (uint32_t)chunkSize;
            remaining -= (uint32_t)chunkSize;
        }
    }

    if (fclose(out) != 0) {
        remove(tempPath);
        return 0;
    }
    if (!wroteAny) {
        remove(tempPath);
        return 1;
    }
    if (!debugger_platform_replaceFile(tempPath, finalPath)) {
        remove(tempPath);
        return 0;
    }
    return 1;
}

static void
amiga_memview_onSave(e9ui_context_t *ctx, void *user)
{
    amiga_memview_state_t *ui = (amiga_memview_state_t*)user;
    target_memory_range_t ranges[AMIGA_MEMVIEW_EXPORT_MAX_RANGES];
    char defaultDir[PATH_MAX];
    char fileName[PATH_MAX];
    char configName[PATH_MAX];
    char exportPaths[amiga_memview_ram_type_count][PATH_MAX];
    const char *overwritePaths[amiga_memview_ram_type_count];
    const char *folder = NULL;
    const char *configPath = NULL;
    const char *defaultPath = NULL;
    size_t rangeCount = 0u;
    int exportTypes[amiga_memview_ram_type_count];
    int exportTypeCount = 0;
    int overwriteCount = 0;

    (void)ctx;
    if (!ui) {
        return;
    }

    configPath = libretro_host_getRomPath();
    if (!configPath || !amiga_memview_copyConfigName(configName, sizeof(configName), configPath)) {
        e9ui_showTransientMessage("RAM SAVE FAILED");
        return;
    }
    if (!amiga_memview_collectExportRanges(ranges, countof(ranges), &rangeCount) || rangeCount == 0u) {
        e9ui_showTransientMessage("RAM SAVE FAILED");
        return;
    }

    if (debugger.libretro.saveDir[0] && settings_pathExistsDir(debugger.libretro.saveDir)) {
        defaultPath = debugger.libretro.saveDir;
    } else if (configPath && configPath[0]) {
        defaultPath = configPath;
    } else if (platform_getCurrentDir(defaultDir, sizeof(defaultDir))) {
        defaultPath = defaultDir;
    } else {
        defaultPath = ".";
    }
    folder = platform_selectFolderDialog("Select RAM dump folder", defaultPath);
    if (!folder || !folder[0]) {
        return;
    }
    if (!settings_pathExistsDir(folder)) {
        e9ui_showTransientMessage("RAM SAVE FAILED");
        return;
    }

    for (int ramType = 0; ramType < amiga_memview_ram_type_count; ++ramType) {
        int present = 0;

        for (size_t i = 0; i < rangeCount; ++i) {
            if (ranges[i].size != 0u && amiga_memview_ramTypeFromBaseAddr(ranges[i].baseAddr) == ramType) {
                present = 1;
                break;
            }
        }
        if (!present) {
            continue;
        }
        if (!amiga_memview_buildExportFileName(fileName, sizeof(fileName), configName, ramType) ||
            !debugger_platform_pathJoin(exportPaths[ramType],
                                        sizeof(exportPaths[ramType]),
                                        folder,
                                        fileName)) {
            e9ui_showTransientMessage("RAM SAVE FAILED");
            return;
        }
        exportTypes[exportTypeCount++] = ramType;
        if (settings_pathExistsFile(exportPaths[ramType])) {
            overwritePaths[overwriteCount++] = exportPaths[ramType];
        }
    }

    if (exportTypeCount == 0) {
        e9ui_showTransientMessage("RAM SAVE FAILED");
        return;
    }

    if (overwriteCount > 0) {
        char message[1024];

        if (!amiga_memview_buildOverwriteMessage(message, sizeof(message), overwritePaths, overwriteCount) ||
            !tinyfd_messageBox("Overwrite RAM dumps?", message, "yesno", "warning", 0)) {
            return;
        }
    }

    for (int i = 0; i < exportTypeCount; ++i) {
        int ramType = exportTypes[i];

        if (!amiga_memview_writeExportType(ui, ranges, rangeCount, ramType, exportPaths[ramType])) {
            e9ui_showTransientMessage("RAM SAVE FAILED");
            return;
        }
    }

    e9ui_showTransientMessage("RAM SAVED");
}

static void
amiga_memview_onShowAddressColumnChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    amiga_memview_state_t *ui = (amiga_memview_state_t*)user;

    (void)self;
    (void)ctx;
    if (!ui) {
        return;
    }
    ui->showAddressColumn = selected ? 1 : 0;
    ui->showAddressColumnHasSaved = 1;
    amiga_memview_saveWindowState(ui);
}

static void
amiga_memview_onShowOverviewColumnChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    amiga_memview_state_t *ui = (amiga_memview_state_t*)user;

    (void)self;
    (void)ctx;
    if (!ui) {
        return;
    }
    ui->showOverviewColumn = selected ? 1 : 0;
    ui->showOverviewColumnHasSaved = 1;
    amiga_memview_saveWindowState(ui);
}

static e9ui_window_backend_t
amiga_memview_windowBackend(void)
{
    return e9ui_window_backend_overlay;
}

static e9ui_rect_t
amiga_memview_windowDefaultRect(const e9ui_context_t *ctx)
{
    e9ui_rect_t rect = {
        e9ui_scale_px(ctx, 96),
        e9ui_scale_px(ctx, 88),
        e9ui_scale_px(ctx, 1240),
        e9ui_scale_px(ctx, 720)
    };
    return rect;
}

static int
amiga_memview_canvasPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)ctx;
    (void)availW;
    return 0;
}

static int
amiga_memview_toolbarItemPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    amiga_memview_toolbar_item_state_t *state = NULL;

    (void)availW;
    if (!self || !ctx || !self->state) {
        return 0;
    }
    state = (amiga_memview_toolbar_item_state_t*)self->state;
    if (!state->child || !state->child->preferredHeight) {
        return 0;
    }
    return state->child->preferredHeight(state->child, ctx, state->widthPx);
}

static void
amiga_memview_toolbarItemLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    amiga_memview_toolbar_item_state_t *state = NULL;

    if (!self || !ctx || !self->state) {
        return;
    }
    self->bounds = bounds;
    state = (amiga_memview_toolbar_item_state_t*)self->state;
    if (state->child && state->child->layout) {
        state->child->layout(state->child, ctx, bounds);
    }
}

static void
amiga_memview_toolbarItemRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    amiga_memview_toolbar_item_state_t *state = NULL;

    if (!self || !ctx || !self->state) {
        return;
    }
    state = (amiga_memview_toolbar_item_state_t*)self->state;
    if (state->child && state->child->render) {
        state->child->render(state->child, ctx);
    }
}

static void
amiga_memview_toolbarItemDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    alloc_free(self->state);
    self->state = NULL;
}

static e9ui_component_t *
amiga_memview_makeToolbarItem(e9ui_component_t *child, int widthPx)
{
    e9ui_component_t *item = NULL;
    amiga_memview_toolbar_item_state_t *state = NULL;

    if (!child || widthPx <= 0) {
        return NULL;
    }
    item = (e9ui_component_t*)alloc_calloc(1, sizeof(*item));
    state = (amiga_memview_toolbar_item_state_t*)alloc_calloc(1, sizeof(*state));
    if (!item || !state) {
        alloc_free(item);
        alloc_free(state);
        return NULL;
    }

    state->child = child;
    state->widthPx = widthPx;
    item->name = "amiga_memview_toolbar_item";
    item->state = state;
    item->preferredHeight = amiga_memview_toolbarItemPreferredHeight;
    item->layout = amiga_memview_toolbarItemLayout;
    item->render = amiga_memview_toolbarItemRender;
    item->dtor = amiga_memview_toolbarItemDtor;
    e9ui_child_add(item, child, NULL);
    return item;
}

static int
amiga_memview_toolbarItemWidth(const e9ui_component_t *item)
{
    const amiga_memview_toolbar_item_state_t *state = NULL;

    if (!item || !item->state) {
        return 0;
    }
    state = (const amiga_memview_toolbar_item_state_t*)item->state;
    return state->widthPx > 0 ? state->widthPx : 0;
}

static int
amiga_memview_toolbarWrapPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    amiga_memview_toolbar_wrap_state_t *state = NULL;
    int pad = 0;
    int gap = 0;
    int x = 0;
    int y = 0;
    int rowH = 0;
    int rightLimit = 0;

    if (!self || !ctx || !self->state) {
        return 0;
    }
    state = (amiga_memview_toolbar_wrap_state_t*)self->state;
    pad = e9ui_scale_px(ctx, state->padPx);
    gap = e9ui_scale_px(ctx, state->gapPx);
    x = pad;
    y = pad;
    rightLimit = availW - pad;
    if (rightLimit < pad) {
        rightLimit = pad;
    }

    e9ui_child_iterator it;
    e9ui_child_iterator *p = e9ui_child_iterateChildren(self, &it);
    while (e9ui_child_interateNext(p)) {
        e9ui_component_t *child = p->child;
        int childW = 0;
        int childH = 0;

        if (!child || e9ui_getHidden(child)) {
            continue;
        }
        childW = amiga_memview_toolbarItemWidth(child);
        if (childW <= 0) {
            continue;
        }
        if (child->preferredHeight) {
            childH = child->preferredHeight(child, ctx, childW);
        }
        if (childH <= 0) {
            childH = 24;
        }

        if (x > pad && x + childW > rightLimit) {
            x = pad;
            y += rowH + gap;
            rowH = 0;
        }
        if (childH > rowH) {
            rowH = childH;
        }
        x += childW + gap;
    }

    return y + rowH + pad;
}

static void
amiga_memview_toolbarWrapLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    amiga_memview_toolbar_wrap_state_t *state = NULL;
    int pad = 0;
    int gap = 0;
    int x = 0;
    int y = 0;
    int rowH = 0;
    int rightLimit = 0;

    if (!self || !ctx || !self->state) {
        return;
    }
    self->bounds = bounds;
    state = (amiga_memview_toolbar_wrap_state_t*)self->state;
    pad = e9ui_scale_px(ctx, state->padPx);
    gap = e9ui_scale_px(ctx, state->gapPx);
    x = bounds.x + pad;
    y = bounds.y + pad;
    rightLimit = bounds.x + bounds.w - pad;
    if (rightLimit < x) {
        rightLimit = x;
    }

    e9ui_child_iterator it;
    e9ui_child_iterator *p = e9ui_child_iterateChildren(self, &it);
    while (e9ui_child_interateNext(p)) {
        e9ui_component_t *child = p->child;
        int childW = 0;
        int childH = 0;

        if (!child || e9ui_getHidden(child)) {
            continue;
        }
        childW = amiga_memview_toolbarItemWidth(child);
        if (childW <= 0) {
            continue;
        }
        if (child->preferredHeight) {
            childH = child->preferredHeight(child, ctx, childW);
        }
        if (childH <= 0) {
            childH = 24;
        }

        if (x > bounds.x + pad && x + childW > rightLimit) {
            x = bounds.x + pad;
            y += rowH + gap;
            rowH = 0;
        }
        if (childH > rowH) {
            rowH = childH;
        }
        if (child->layout) {
            child->layout(child, ctx, (e9ui_rect_t){ x, y, childW, childH });
        }
        x += childW + gap;
    }
}

static void
amiga_memview_toolbarWrapRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx) {
        return;
    }

    e9ui_child_iterator it;
    e9ui_child_iterator *p = e9ui_child_iterateChildren(self, &it);
    while (e9ui_child_interateNext(p)) {
        e9ui_component_t *child = p->child;
        if (!child || e9ui_getHidden(child)) {
            continue;
        }
        if (child->render) {
            child->render(child, ctx);
        }
    }
}

static void
amiga_memview_toolbarWrapDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    alloc_free(self->state);
    self->state = NULL;
}

static e9ui_component_t *
amiga_memview_makeToolbarWrap(void)
{
    e9ui_component_t *comp = NULL;
    amiga_memview_toolbar_wrap_state_t *state = NULL;

    comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    state = (amiga_memview_toolbar_wrap_state_t*)alloc_calloc(1, sizeof(*state));
    if (!comp || !state) {
        alloc_free(comp);
        alloc_free(state);
        return NULL;
    }

    state->padPx = 0;
    state->gapPx = 12;
    comp->name = "amiga_memview_toolbar_wrap";
    comp->state = state;
    comp->preferredHeight = amiga_memview_toolbarWrapPreferredHeight;
    comp->layout = amiga_memview_toolbarWrapLayout;
    comp->render = amiga_memview_toolbarWrapRender;
    comp->dtor = amiga_memview_toolbarWrapDtor;
    return comp;
}

static int
amiga_memview_canvasVisibleRows(const amiga_memview_state_t *ui, const e9ui_rect_t *bounds)
{
    int innerHeight = 0;
    int rowPx = 1;
    int visibleRows = 1;

    if (!ui || !bounds) {
        return 1;
    }

    innerHeight = bounds->h -
        e9ui_scale_px(&ui->ctx, AMIGA_MEMVIEW_TOP_PAD_PX) -
        e9ui_scale_px(&ui->ctx, AMIGA_MEMVIEW_BOTTOM_PAD_PX);
    if (innerHeight < 1) {
        innerHeight = 1;
    }
    rowPx = amiga_memview_rowPx(ui);
    visibleRows = (innerHeight + rowPx - 1) / rowPx;
    if (visibleRows < 1) {
        visibleRows = 1;
    }
    return visibleRows;
}

static int
amiga_memview_stepButtonsGutterWidth(const e9ui_context_t *ctx, e9ui_component_t *self)
{
    int thickness = 0;
    int buttonW = 0;
    int margin = 0;
    int gutter = 0;

    if (!ctx || !self) {
        return 0;
    }
    thickness = e9ui_scale_px(ctx, 8);
    if (thickness < 4) {
        thickness = 4;
    }
    if (self->bounds.w > 0 && thickness >= self->bounds.w) {
        thickness = self->bounds.w > 1 ? self->bounds.w - 1 : 1;
    }
    if (thickness <= 0) {
        return 0;
    }
    buttonW = thickness * 2;
    if (buttonW > self->bounds.w) {
        buttonW = self->bounds.w;
    }
    margin = e9ui_scale_px(ctx, 4);
    if (margin < 0) {
        margin = 0;
    }
    gutter = buttonW + margin;
    if (gutter > self->bounds.w) {
        gutter = self->bounds.w;
    }
    return gutter > 0 ? gutter : 0;
}

static e9ui_rect_t
amiga_memview_hscrollBounds(const amiga_memview_state_t *ui, e9ui_component_t *self)
{
    e9ui_rect_t bounds = {0, 0, 0, 0};
    int rightGutter = 0;
    int leftGutter = 0;
    TTF_Font *font = NULL;

    if (!ui || !self) {
        return bounds;
    }
    rightGutter = amiga_memview_stepButtonsGutterWidth(&ui->ctx, self);
    if (rightGutter < 0) {
        rightGutter = 0;
    }
    if (rightGutter > self->bounds.w) {
        rightGutter = self->bounds.w;
    }
    font = e9ui->theme.text.source ? e9ui->theme.text.source : ui->ctx.font;
    leftGutter = amiga_memview_leftGutterPx(ui, &ui->ctx, font);
    bounds.x = self->bounds.x + leftGutter;
    bounds.y = self->bounds.y;
    bounds.w = self->bounds.w - leftGutter - rightGutter;
    bounds.h = self->bounds.h;
    if (bounds.w < 0) {
        bounds.w = 0;
    }
    return bounds;
}

static uint32_t
amiga_memview_clampBaseForView(const amiga_memview_state_t *ui, const e9ui_rect_t *bounds, uint32_t baseAddr)
{
    uint32_t minAddr = 0u;
    uint32_t maxAddr = 0x00ffffffu;
    uint64_t viewBytes = 0u;
    uint64_t span = 0u;
    uint32_t maxBase = 0u;

    if (!ui || !bounds) {
        return baseAddr & 0x00ffffffu;
    }
    if (!amiga_memview_getAddressLimits(&minAddr, &maxAddr)) {
        minAddr = 0u;
        maxAddr = 0x00ffffffu;
    }
    viewBytes = (uint64_t)amiga_memview_canvasVisibleRows(ui, bounds) *
                (uint64_t)amiga_memview_clampRowBytes(ui->rowBytes);
    span = viewBytes > 0u ? (viewBytes - 1u) : 0u;
    maxBase = maxAddr;
    if ((uint64_t)maxAddr >= span) {
        maxBase = (uint32_t)((uint64_t)maxAddr - span);
    } else {
        maxBase = minAddr;
    }
    if (maxBase < minAddr) {
        maxBase = minAddr;
    }
    baseAddr &= 0x00ffffffu;
    if (baseAddr < minAddr) {
        baseAddr = minAddr;
    }
    if (baseAddr > maxBase) {
        baseAddr = maxBase;
    }
    return baseAddr;
}

static int
amiga_memview_findOverviewRangeForAddr(const amiga_memview_state_t *ui, uint32_t addr)
{
    uint32_t start = 0u;
    uint32_t end = 0u;

    if (!ui) {
        return -1;
    }
    addr &= 0x00ffffffu;
    for (int i = 0; i < AMIGA_MEMVIEW_OVERVIEW_MAX_RANGES; ++i) {
        if (ui->overviewRanges[i].sizeBytes == 0u) {
            continue;
        }
        start = ui->overviewRanges[i].baseAddr & 0x00ffffffu;
        end = start + ui->overviewRanges[i].sizeBytes - 1u;
        if (addr >= start && addr <= end) {
            return i;
        }
    }
    return -1;
}

static uint32_t
amiga_memview_clampBaseForRangeView(const amiga_memview_state_t *ui,
                                    const e9ui_rect_t *bounds,
                                    const amiga_memview_overview_range_t *range,
                                    uint32_t baseAddr)
{
    uint32_t start = 0u;
    uint32_t end = 0u;
    uint64_t viewBytes = 0u;
    uint64_t span = 0u;
    uint32_t maxBase = 0u;

    if (!ui || !bounds || !range || range->sizeBytes == 0u) {
        return baseAddr & 0x00ffffffu;
    }

    start = range->baseAddr & 0x00ffffffu;
    end = start + range->sizeBytes - 1u;
    viewBytes = (uint64_t)amiga_memview_canvasVisibleRows(ui, bounds) *
                (uint64_t)amiga_memview_clampRowBytes(ui->rowBytes);
    span = viewBytes > 0u ? (viewBytes - 1u) : 0u;
    maxBase = start;
    if ((uint64_t)range->sizeBytes > span) {
        maxBase = (uint32_t)((uint64_t)end - span);
    }

    baseAddr &= 0x00ffffffu;
    if (baseAddr < start) {
        baseAddr = start;
    }
    if (baseAddr > maxBase) {
        baseAddr = maxBase;
    }
    return baseAddr;
}

static void
amiga_memview_scrollRows(amiga_memview_state_t *ui, const e9ui_rect_t *bounds, int rows)
{
    int64_t delta = 0;
    int64_t rawBase = 0;
    uint32_t clamped = 0u;
    int currentRange = -1;

    if (!ui || !bounds || rows == 0) {
        return;
    }
    delta = (int64_t)rows * (int64_t)amiga_memview_clampRowBytes(ui->rowBytes);
    rawBase = (int64_t)(ui->baseAddr & 0x00ffffffu) + delta;
    if (rawBase < 0) {
        rawBase = 0;
    }

    currentRange = amiga_memview_findOverviewRangeForAddr(ui, ui->baseAddr);
    if (currentRange >= 0) {
        const amiga_memview_overview_range_t *range = &ui->overviewRanges[currentRange];
        uint32_t rangeStart = range->baseAddr & 0x00ffffffu;
        uint32_t rangeEnd = rangeStart + range->sizeBytes - 1u;

        if (rows > 0 && (uint32_t)rawBase > rangeEnd) {
            for (int i = currentRange + 1; i < AMIGA_MEMVIEW_OVERVIEW_MAX_RANGES; ++i) {
                if (ui->overviewRanges[i].sizeBytes == 0u) {
                    continue;
                }
                clamped = amiga_memview_clampBaseForRangeView(ui,
                                                               bounds,
                                                               &ui->overviewRanges[i],
                                                               ui->overviewRanges[i].baseAddr);
                break;
            }
        } else if (rows < 0 && (uint32_t)rawBase < rangeStart) {
            for (int i = currentRange - 1; i >= 0; --i) {
                if (ui->overviewRanges[i].sizeBytes == 0u) {
                    continue;
                }
                clamped = amiga_memview_clampBaseForRangeView(ui,
                                                               bounds,
                                                               &ui->overviewRanges[i],
                                                               (ui->overviewRanges[i].baseAddr &
                                                                0x00ffffffu) +
                                                               ui->overviewRanges[i].sizeBytes -
                                                               1u);
                break;
            }
        } else {
            clamped = amiga_memview_clampBaseForView(ui, bounds, (uint32_t)rawBase);
        }
    }

    if (clamped == 0u) {
        clamped = amiga_memview_clampBaseForView(ui, bounds, (uint32_t)rawBase);
    }

    if (clamped == ui->baseAddr) {
        return;
    }
    ui->baseAddr = clamped;
    ui->baseAddrHasSaved = 1;
    ui->bitCacheValid = 0;
    amiga_memview_syncTextboxesFromState(ui);
    amiga_memview_saveWindowState(ui);
}

static int
amiga_memview_stepButtonsOnAction(void *user, e9ui_step_buttons_action_t action)
{
    amiga_memview_step_buttons_action_ctx_t *actionCtx = (amiga_memview_step_buttons_action_ctx_t*)user;
    int rows = 0;
    int pageRows = 0;

    if (!actionCtx || !actionCtx->ui || !actionCtx->canvas) {
        return 0;
    }
    pageRows = amiga_memview_canvasVisibleRows(actionCtx->ui, &actionCtx->canvas->bounds) / 4;
    if (pageRows < 1) {
        pageRows = 1;
    }
    switch (action) {
    case e9ui_step_buttons_action_page_up:
        rows = -pageRows;
        break;
    case e9ui_step_buttons_action_line_up:
        rows = -1;
        break;
    case e9ui_step_buttons_action_line_down:
        rows = 1;
        break;
    case e9ui_step_buttons_action_page_down:
        rows = pageRows;
        break;
    default:
        break;
    }
    if (rows == 0) {
        return 0;
    }
    amiga_memview_scrollRows(actionCtx->ui, &actionCtx->canvas->bounds, rows);
    return 1;
}

static void
amiga_memview_canvasLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static void
amiga_memview_seekBarLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self) {
        return;
    }
    e9ui_seek_bar_layoutInParent(self, ctx, bounds);
    if (self->bounds.h < bounds.h) {
        self->bounds.y = bounds.y + (bounds.h - self->bounds.h) / 2;
    }
}

static int
amiga_memview_ramTypeFromBaseAddr(uint32_t baseAddr)
{
    uint32_t start = baseAddr & 0x00ffffffu;

    if (start == 0x000000u) {
        return amiga_memview_ram_type_chip;
    }
    if (start == AMIGA_MEMVIEW_OVERVIEW_DEFAULT_RANGE1_BASE) {
        return amiga_memview_ram_type_slow;
    }
    if (start == AMIGA_MEMVIEW_OVERVIEW_FAST_RANGE_BASE) {
        return amiga_memview_ram_type_fast;
    }
    return amiga_memview_ram_type_other;
}

static const char *
amiga_memview_ramTypeLabel(int ramType)
{
    switch (ramType) {
    case amiga_memview_ram_type_chip:
        return "Chip";
    case amiga_memview_ram_type_slow:
        return "Slow";
    case amiga_memview_ram_type_fast:
        return "Fast";
    default:
        return "Other";
    }
}

static uint32_t
amiga_memview_presentRamTypeMask(const amiga_memview_state_t *ui)
{
    uint32_t mask = 0u;

    if (!ui) {
        return 0u;
    }
    for (int i = 0; i < AMIGA_MEMVIEW_OVERVIEW_MAX_RANGES; ++i) {
        const amiga_memview_overview_range_t *range = &ui->overviewRanges[i];
        int ramType = 0;

        if (range->sizeBytes == 0u) {
            continue;
        }
        ramType = amiga_memview_ramTypeFromBaseAddr(range->baseAddr);
        if (ramType >= 0 && ramType < amiga_memview_ram_type_count) {
            mask |= (uint32_t)(1u << ramType);
        }
    }
    return mask;
}

static SDL_Color
amiga_memview_rangeColor(uint32_t baseAddr)
{
    switch (amiga_memview_ramTypeFromBaseAddr(baseAddr)) {
    case amiga_memview_ram_type_chip:
        return (SDL_Color){ 120, 180, 255, 255 };
    case amiga_memview_ram_type_slow:
        return (SDL_Color){ 232, 104, 104, 255 };
    case amiga_memview_ram_type_fast:
        return (SDL_Color){ 116, 214, 130, 255 };
    default:
        return (SDL_Color){ 190, 150, 255, 255 };
    }
}

static int
amiga_memview_legendMeasureWidth(const amiga_memview_state_t *ui, e9ui_context_t *ctx)
{
    TTF_Font *font = NULL;
    uint32_t presentMask = 0u;
    int width = 0;
    int swatch = 0;
    int gap = 0;
    int itemGap = 0;

    if (!ui || !ctx) {
        return 0;
    }
    font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    presentMask = amiga_memview_presentRamTypeMask(ui);
    if (presentMask == 0u) {
        return 0;
    }
    swatch = e9ui_scale_px(ctx, 10);
    gap = e9ui_scale_px(ctx, 6);
    itemGap = e9ui_scale_px(ctx, 12);
    for (int ramType = 0; ramType < amiga_memview_ram_type_count; ++ramType) {
        const char *label = NULL;
        int textW = 0;
        int textH = 0;

        if ((presentMask & (uint32_t)(1u << ramType)) == 0u) {
            continue;
        }
        label = amiga_memview_ramTypeLabel(ramType);
        if (width > 0) {
            width += itemGap;
        }
        width += swatch + gap;
        if (font && label && TTF_SizeUTF8(font, label, &textW, &textH) == 0) {
            width += textW;
        } else {
            width += e9ui_scale_px(ctx, 40);
        }
    }
    return width;
}

static int
amiga_memview_legendPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    TTF_Font *font = NULL;
    int textH = 16;
    int padY = 0;

    (void)self;
    (void)availW;
    if (!ctx) {
        return 0;
    }
    font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    if (font) {
        textH = TTF_FontHeight(font);
    }
    if (textH <= 0) {
        textH = 16;
    }
    padY = e9ui_checkbox_getMargin(ctx);
    if (padY <= 0) {
        padY = e9ui_scale_px(ctx, 4);
    }
    return textH + padY * 2;
}

static void
amiga_memview_legendLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static void
amiga_memview_legendRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    amiga_memview_legend_state_t *state = NULL;
    amiga_memview_state_t *ui = NULL;
    TTF_Font *font = NULL;
    SDL_Color textColor = { 232, 232, 236, 255 };
    uint32_t presentMask = 0u;
    int lineHeight = 16;
    int swatch = 0;
    int gap = 0;
    int itemGap = 0;
    int x = 0;

    if (!self || !ctx || !ctx->renderer || !self->state) {
        return;
    }
    state = (amiga_memview_legend_state_t*)self->state;
    ui = state ? state->ui : NULL;
    if (!ui) {
        return;
    }
    font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    presentMask = amiga_memview_presentRamTypeMask(ui);
    if (presentMask == 0u) {
        return;
    }
    if (font) {
        lineHeight = TTF_FontHeight(font);
    }
    if (lineHeight <= 0) {
        lineHeight = 16;
    }
    swatch = e9ui_scale_px(ctx, 10);
    gap = e9ui_scale_px(ctx, 6);
    itemGap = e9ui_scale_px(ctx, 12);
    x = self->bounds.x;

    for (int ramType = 0; ramType < amiga_memview_ram_type_count; ++ramType) {
        const char *label = NULL;
        SDL_Color color;
        SDL_Rect swatchRect;
        int textW = 0;
        int textH = 0;
        int textY = 0;

        if ((presentMask & (uint32_t)(1u << ramType)) == 0u) {
            continue;
        }
        label = amiga_memview_ramTypeLabel(ramType);
        color = amiga_memview_rangeColor((ramType == amiga_memview_ram_type_chip) ? 0x000000u :
                                         (ramType == amiga_memview_ram_type_slow) ? AMIGA_MEMVIEW_OVERVIEW_DEFAULT_RANGE1_BASE :
                                         (ramType == amiga_memview_ram_type_fast) ? AMIGA_MEMVIEW_OVERVIEW_FAST_RANGE_BASE :
                                         0x00ffffffu);
        swatchRect.x = x;
        swatchRect.y = self->bounds.y + (self->bounds.h - swatch) / 2;
        swatchRect.w = swatch;
        swatchRect.h = swatch;
        SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ctx->renderer, color.r, color.g, color.b, 220);
        SDL_RenderFillRect(ctx->renderer, &swatchRect);
        SDL_SetRenderDrawColor(ctx->renderer, 220, 220, 220, 255);
        SDL_RenderDrawRect(ctx->renderer, &swatchRect);
        x += swatch + gap;
        if (!(font && label && TTF_SizeUTF8(font, label, &textW, &textH) == 0)) {
            textW = e9ui_scale_px(ctx, 40);
            textH = lineHeight;
        }
        textY = self->bounds.y + (self->bounds.h - textH) / 2;
        e9ui_drawSelectableText(ctx, NULL, font, label, textColor, x, textY, lineHeight, 0, NULL, 0, 0);
        x += textW;
        x += itemGap;
    }
}

static void
amiga_memview_legendDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    alloc_free(self->state);
    self->state = NULL;
}

static e9ui_component_t *
amiga_memview_makeLegend(amiga_memview_state_t *ui)
{
    e9ui_component_t *comp = NULL;
    amiga_memview_legend_state_t *state = NULL;

    if (!ui || amiga_memview_presentRamTypeMask(ui) == 0u) {
        return NULL;
    }
    comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    state = (amiga_memview_legend_state_t*)alloc_calloc(1, sizeof(*state));
    if (!comp || !state) {
        alloc_free(comp);
        alloc_free(state);
        return NULL;
    }
    state->ui = ui;
    comp->name = "amiga_memview_legend";
    comp->state = state;
    comp->preferredHeight = amiga_memview_legendPreferredHeight;
    comp->layout = amiga_memview_legendLayout;
    comp->render = amiga_memview_legendRender;
    comp->dtor = amiga_memview_legendDtor;
    return comp;
}

static SDL_Color
amiga_memview_addressLabelColor(const amiga_memview_state_t *ui, uint32_t addr)
{
    uint32_t maskedAddr = addr & 0x00ffffffu;

    if (ui) {
        for (int i = 0; i < AMIGA_MEMVIEW_OVERVIEW_MAX_RANGES; ++i) {
            const amiga_memview_overview_range_t *range = &ui->overviewRanges[i];
            uint32_t start = 0u;
            uint32_t end = 0u;

            if (range->sizeBytes == 0u) {
                continue;
            }
            start = range->baseAddr & 0x00ffffffu;
            end = start + range->sizeBytes;
            if (maskedAddr < start || maskedAddr >= end) {
                continue;
            }
            return amiga_memview_rangeColor(start);
        }
    }

    return (SDL_Color){ 166, 176, 192, 255 };
}

static void
amiga_memview_drawAddressLabel(const amiga_memview_state_t *ui, e9ui_context_t *ctx, TTF_Font *font, uint32_t addr, int x, int y)
{
    char label[16];
    SDL_Color color;

    if (!ctx || !font) {
        return;
    }
    snprintf(label, sizeof(label), "%06X", (unsigned)(addr & 0x00ffffffu));
    color = amiga_memview_addressLabelColor(ui, addr);
    e9ui_drawSelectableText(ctx,
                            NULL,
                            font,
                            label,
                            color,
                            x,
                            y,
                            TTF_FontHeight(font),
                            0,
                            NULL,
                            0,
                            0);
}

static int
amiga_memview_measureAddressGutterPx(const e9ui_context_t *ctx, TTF_Font *font)
{
    int textW = 0;
    int textH = 0;
    e9ui_context_t tempCtx = ctx ? *ctx : (e9ui ? e9ui->ctx : (e9ui_context_t){0});
    int gutter = e9ui_scale_px(&tempCtx, 10);

    if (!font) {
        font = e9ui->theme.text.source ? e9ui->theme.text.source : (ctx ? ctx->font : NULL);
    }
    if (font && TTF_SizeUTF8(font, "FFFFFF", &textW, &textH) == 0) {
        gutter += textW;
    }
    return gutter;
}

static int
amiga_memview_measureOverviewGutterPx(const e9ui_context_t *ctx)
{
    e9ui_context_t tempCtx = ctx ? *ctx : (e9ui ? e9ui->ctx : (e9ui_context_t){0});
    return e9ui_scale_px(&tempCtx, AMIGA_MEMVIEW_OVERVIEW_GUTTER_PX);
}

static uint32_t
amiga_memview_clampOverviewRangeSize(uint32_t sizeBytes)
{
    if (sizeBytes == 0u) {
        return 0u;
    }
    if (sizeBytes > 0x01000000u) {
        sizeBytes = 0x01000000u;
    }
    sizeBytes &= ~1u;
    if (sizeBytes < 2u) {
        sizeBytes = 2u;
    }
    return sizeBytes;
}

static void
amiga_memview_appendOverviewRange(amiga_memview_overview_range_t *ranges, int *count, uint32_t baseAddr, uint32_t sizeBytes)
{
    if (!ranges || !count || *count < 0 || *count >= AMIGA_MEMVIEW_OVERVIEW_MAX_RANGES) {
        return;
    }
    sizeBytes = amiga_memview_clampOverviewRangeSize(sizeBytes);
    if (sizeBytes == 0u) {
        return;
    }
    ranges[*count].baseAddr = baseAddr & 0x00ffffffu;
    ranges[*count].sizeBytes = sizeBytes;
    (*count)++;
}

static void
amiga_memview_sortOverviewRanges(amiga_memview_overview_range_t *ranges, int count)
{
    if (!ranges || count < 2) {
        return;
    }
    for (int i = 1; i < count; ++i) {
        amiga_memview_overview_range_t cur = ranges[i];
        int j = i;

        while (j > 0) {
            amiga_memview_overview_range_t prev = ranges[j - 1];

            if (prev.baseAddr < cur.baseAddr) {
                break;
            }
            if (prev.baseAddr == cur.baseAddr && prev.sizeBytes <= cur.sizeBytes) {
                break;
            }
            ranges[j] = prev;
            --j;
        }
        ranges[j] = cur;
    }
}

static void
amiga_memview_initDefaultOverviewRanges(amiga_memview_state_t *ui)
{
    target_memory_range_t targetRanges[AMIGA_MEMVIEW_OVERVIEW_MAX_RANGES];
    size_t targetCount = 0u;
    int write = 0;

    if (!ui) {
        return;
    }

    memset(ui->overviewRanges, 0, sizeof(ui->overviewRanges));
    memset(targetRanges, 0, sizeof(targetRanges));

    if (target && target->memoryTrackGetRanges) {
        if (target->memoryTrackGetRanges(targetRanges, AMIGA_MEMVIEW_OVERVIEW_MAX_RANGES, &targetCount)) {
            if (targetCount > AMIGA_MEMVIEW_OVERVIEW_MAX_RANGES) {
                targetCount = AMIGA_MEMVIEW_OVERVIEW_MAX_RANGES;
            }
            for (size_t i = 0; i < targetCount && write < AMIGA_MEMVIEW_OVERVIEW_MAX_RANGES; ++i) {
                uint32_t sizeBytes = amiga_memview_clampOverviewRangeSize(targetRanges[i].size);

                if (sizeBytes == 0u) {
                    continue;
                }
                amiga_memview_appendOverviewRange(ui->overviewRanges,
                                                  &write,
                                                  targetRanges[i].baseAddr,
                                                  sizeBytes);
            }
        }
    }

    amiga_memview_sortOverviewRanges(ui->overviewRanges, write);
    for (int i = 0; i < write; ++i) {
        ui->overviewRanges[i].baseAddr &= 0x00ffffffu;
        ui->overviewRanges[i].sizeBytes = amiga_memview_clampOverviewRangeSize(ui->overviewRanges[i].sizeBytes);
    }
}

static int
amiga_memview_overviewRangeCount(const amiga_memview_state_t *ui)
{
    int count = 0;

    if (!ui) {
        return 0;
    }
    for (int i = 0; i < AMIGA_MEMVIEW_OVERVIEW_MAX_RANGES; ++i) {
        if (ui->overviewRanges[i].sizeBytes != 0u) {
            count++;
        }
    }
    return count;
}

static int
amiga_memview_leftGutterPx(const amiga_memview_state_t *ui, const e9ui_context_t *ctx, TTF_Font *font)
{
    int left = 0;
    int haveSection = 0;

    if (!ui) {
        return 0;
    }
    if (ui->showAddressColumn) {
        left += amiga_memview_measureAddressGutterPx(ctx, font);
        haveSection = 1;
    }
    if (ui->showOverviewColumn && amiga_memview_overviewRangeCount(ui) > 0) {
        if (haveSection) {
            left += e9ui_scale_px(ctx, AMIGA_MEMVIEW_GUTTER_GAP_PX);
        }
        left += amiga_memview_measureOverviewGutterPx(ctx);
    }
    return left;
}

static e9ui_rect_t
amiga_memview_overviewBounds(const amiga_memview_state_t *ui, e9ui_component_t *self, const e9ui_context_t *ctx, TTF_Font *font)
{
    e9ui_rect_t bounds = {0, 0, 0, 0};
    int x = 0;

    if (!ui || !self || !ctx || !ui->showOverviewColumn || amiga_memview_overviewRangeCount(ui) <= 0) {
        return bounds;
    }

    x = self->bounds.x;
    if (ui->showAddressColumn) {
        x += amiga_memview_measureAddressGutterPx(ctx, font);
        x += e9ui_scale_px(ctx, AMIGA_MEMVIEW_GUTTER_GAP_PX);
    }

    bounds.x = x;
    bounds.y = self->bounds.y + e9ui_scale_px(ctx, AMIGA_MEMVIEW_TOP_PAD_PX);
    bounds.w = amiga_memview_measureOverviewGutterPx(ctx);
    bounds.h = self->bounds.h -
               e9ui_scale_px(ctx, AMIGA_MEMVIEW_TOP_PAD_PX) -
               e9ui_scale_px(ctx, AMIGA_MEMVIEW_BOTTOM_PAD_PX);
    if (bounds.h < 1) {
        bounds.h = 1;
    }
    return bounds;
}

static e9ui_rect_t
amiga_memview_overviewContentBounds(const e9ui_context_t *ctx, const e9ui_rect_t *overviewBounds)
{
    e9ui_rect_t bounds = {0, 0, 0, 0};
    int borderW = 0;

    if (!overviewBounds) {
        return bounds;
    }
    bounds = *overviewBounds;
    borderW = e9ui_scale_px(ctx, 2);
    if (borderW < 1) {
        borderW = 1;
    }
    if (bounds.w > borderW * 2 && bounds.h > borderW * 2) {
        bounds.x += borderW;
        bounds.y += borderW;
        bounds.w -= borderW * 2;
        bounds.h -= borderW * 2;
    }
    return bounds;
}

static int
amiga_memview_popcount16(uint16_t value)
{
    int count = 0;

    while (value != 0u) {
        count += (int)(value & 1u);
        value >>= 1;
    }
    return count;
}

static uint32_t
amiga_memview_overviewPixelColor(uint16_t word)
{
    int bits = amiga_memview_popcount16(word);
    uint32_t alpha = 0u;

    if (bits <= 0) {
        return 0u;
    }
    alpha = 24u + (uint32_t)((bits * 184) / 16);
    if (alpha > 255u) {
        alpha = 255u;
    }
    return (alpha << 24) | 0x00f2b43cU;
}

static uint32_t
amiga_memview_overviewBytesPerCell(const amiga_memview_overview_range_t *range)
{
    uint32_t cellCount = AMIGA_MEMVIEW_OVERVIEW_TEXTURE_W * AMIGA_MEMVIEW_OVERVIEW_RANGE_TEXTURE_H;
    uint32_t bytesPerCell = 2u;

    if (!range || range->sizeBytes == 0u || cellCount == 0u) {
        return 2u;
    }
    bytesPerCell = (range->sizeBytes + cellCount - 1u) / cellCount;
    bytesPerCell &= ~1u;
    if (bytesPerCell < 2u) {
        bytesPerCell = 2u;
    }
    return bytesPerCell;
}

static int
amiga_memview_rebuildOverviewTexture(amiga_memview_state_t *ui, e9ui_context_t *ctx)
{
    int rangeCount = 0;
    int texW = 0;
    int texH = 0;
    size_t pixelCount = 0u;
    int activeRange = 0;

    if (!ui || !ctx || !ctx->renderer) {
        return 0;
    }

    rangeCount = amiga_memview_overviewRangeCount(ui);
    if (rangeCount <= 0) {
        return 0;
    }

    texW = AMIGA_MEMVIEW_OVERVIEW_TEXTURE_W;
    texH = rangeCount * AMIGA_MEMVIEW_OVERVIEW_RANGE_TEXTURE_H;
    pixelCount = (size_t)texW * (size_t)texH;
    if (!amiga_memview_ensureOverviewPixels(ui, pixelCount) ||
        !amiga_memview_ensureOverviewTexture(ui, ctx->renderer, texW, texH)) {
        return 0;
    }

    memset(ui->overviewPixels, 0, pixelCount * sizeof(*ui->overviewPixels));

    for (int i = 0; i < AMIGA_MEMVIEW_OVERVIEW_MAX_RANGES; ++i) {
        const amiga_memview_overview_range_t *range = &ui->overviewRanges[i];
        uint32_t bytesPerCell = 0u;
        uint32_t rangeSize = 0u;
        int yBase = 0;

        if (range->sizeBytes == 0u) {
            continue;
        }

        bytesPerCell = amiga_memview_overviewBytesPerCell(range);
        rangeSize = range->sizeBytes;
        yBase = activeRange * AMIGA_MEMVIEW_OVERVIEW_RANGE_TEXTURE_H;
        activeRange++;

        if (!amiga_memview_ensureReadBuffer(ui, rangeSize)) {
            return 0;
        }
        memset(ui->readBuffer, 0, rangeSize);
        (void)amiga_memview_readRange(ui, range->baseAddr, ui->readBuffer, rangeSize);

        for (int y = 0; y < AMIGA_MEMVIEW_OVERVIEW_RANGE_TEXTURE_H; ++y) {
            for (int x = 0; x < AMIGA_MEMVIEW_OVERVIEW_TEXTURE_W; ++x) {
                size_t cellIndex = (size_t)y * AMIGA_MEMVIEW_OVERVIEW_TEXTURE_W + (size_t)x;
                size_t byteOffset = cellIndex * (size_t)bytesPerCell;
                uint16_t word = 0u;

                if (byteOffset + 1u < rangeSize) {
                    word = (uint16_t)(((uint16_t)ui->readBuffer[byteOffset] << 8) |
                                      (uint16_t)ui->readBuffer[byteOffset + 1u]);
                }
                ui->overviewPixels[(size_t)(yBase + y) * (size_t)texW + (size_t)x] =
                    amiga_memview_overviewPixelColor(word);
            }
        }
    }

    SDL_UpdateTexture(ui->overviewTexture, NULL, ui->overviewPixels, texW * (int)sizeof(*ui->overviewPixels));
    ui->overviewCacheFrameCounter = debugger.frameCounter;
    ui->overviewCacheValid = 1;
    return 1;
}

static void
amiga_memview_renderOverviewSelection(amiga_memview_state_t *ui, e9ui_context_t *ctx,
                                      const e9ui_rect_t *overviewBounds, int visibleRows)
{
    SDL_BlendMode oldBlendMode = SDL_BLENDMODE_NONE;
    e9ui_rect_t contentBounds = {0, 0, 0, 0};
    int rangeCount = 0;
    uint64_t viewBytes = 0u;
    uint64_t viewStart = 0u;
    uint64_t viewEnd = 0u;
    int activeRange = 0;

    if (!ui || !ctx || !ctx->renderer || !overviewBounds || overviewBounds->w <= 0 || overviewBounds->h <= 0) {
        return;
    }
    contentBounds = amiga_memview_overviewContentBounds(ctx, overviewBounds);
    if (contentBounds.w <= 0 || contentBounds.h <= 0) {
        return;
    }

    rangeCount = amiga_memview_overviewRangeCount(ui);
    if (rangeCount <= 0) {
        return;
    }

    viewBytes = (uint64_t)visibleRows * (uint64_t)amiga_memview_clampRowBytes(ui->rowBytes);
    if (viewBytes == 0u) {
        return;
    }
    viewStart = ui->baseAddr & 0x00ffffffu;
    viewEnd = viewStart + viewBytes;

    SDL_GetRenderDrawBlendMode(ctx->renderer, &oldBlendMode);
    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ctx->renderer, 70, 120, 180, 96);

    for (int i = 0; i < AMIGA_MEMVIEW_OVERVIEW_MAX_RANGES; ++i) {
        const amiga_memview_overview_range_t *range = &ui->overviewRanges[i];
        uint64_t rangeStart = 0u;
        uint64_t rangeEnd = 0u;
        uint64_t selStart = 0u;
        uint64_t selEnd = 0u;
        uint32_t bytesPerCell = 0u;
        uint64_t startCell = 0u;
        uint64_t endCell = 0u;
        int texYBase = 0;

        if (range->sizeBytes == 0u) {
            continue;
        }

        rangeStart = range->baseAddr & 0x00ffffffu;
        rangeEnd = rangeStart + range->sizeBytes;
        texYBase = activeRange * AMIGA_MEMVIEW_OVERVIEW_RANGE_TEXTURE_H;
        activeRange++;

        if (viewEnd <= rangeStart || viewStart >= rangeEnd) {
            continue;
        }

        selStart = viewStart > rangeStart ? viewStart : rangeStart;
        selEnd = viewEnd < rangeEnd ? viewEnd : rangeEnd;
        if (selEnd <= selStart) {
            continue;
        }

        bytesPerCell = amiga_memview_overviewBytesPerCell(range);
        startCell = (selStart - rangeStart) / bytesPerCell;
        endCell = (selEnd - rangeStart - 1u) / bytesPerCell;

        while (startCell <= endCell) {
            uint64_t row = startCell / AMIGA_MEMVIEW_OVERVIEW_TEXTURE_W;
            uint64_t rowEndCell = ((row + 1u) * AMIGA_MEMVIEW_OVERVIEW_TEXTURE_W) - 1u;
            uint64_t segmentEndCell = endCell < rowEndCell ? endCell : rowEndCell;
            int cellX0 = (int)(startCell % AMIGA_MEMVIEW_OVERVIEW_TEXTURE_W);
            int cellX1 = (int)(segmentEndCell % AMIGA_MEMVIEW_OVERVIEW_TEXTURE_W);
            int cellY = texYBase + (int)row;
            int x0 = contentBounds.x + (cellX0 * contentBounds.w) / AMIGA_MEMVIEW_OVERVIEW_TEXTURE_W;
            int x1 = contentBounds.x + ((cellX1 + 1) * contentBounds.w + AMIGA_MEMVIEW_OVERVIEW_TEXTURE_W - 1) /
                     AMIGA_MEMVIEW_OVERVIEW_TEXTURE_W;
            int y0 = contentBounds.y + (cellY * contentBounds.h) /
                     (rangeCount * AMIGA_MEMVIEW_OVERVIEW_RANGE_TEXTURE_H);
            int y1 = contentBounds.y + ((cellY + 1) * contentBounds.h +
                                          (rangeCount * AMIGA_MEMVIEW_OVERVIEW_RANGE_TEXTURE_H) - 1) /
                     (rangeCount * AMIGA_MEMVIEW_OVERVIEW_RANGE_TEXTURE_H);
            SDL_Rect rect = {
                x0,
                y0,
                x1 - x0,
                y1 - y0
            };

            if (rect.w < 1) {
                rect.w = 1;
            }
            if (rect.h < 1) {
                rect.h = 1;
            }
            SDL_RenderFillRect(ctx->renderer, &rect);
            startCell = segmentEndCell + 1u;
        }
    }

    SDL_SetRenderDrawBlendMode(ctx->renderer, oldBlendMode);
}

static int
amiga_memview_overviewNavigate(amiga_memview_state_t *ui, e9ui_component_t *self, e9ui_context_t *ctx, int mx, int my)
{
    e9ui_rect_t bounds = {0, 0, 0, 0};
    TTF_Font *font = NULL;
    int rangeCount = 0;
    int localX = 0;
    int localY = 0;
    int texX = 0;
    int texY = 0;
    int activeRange = 0;

    if (!ui || !self || !ctx) {
        return 0;
    }

    font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    bounds = amiga_memview_overviewBounds(ui, self, ctx, font);
    if (bounds.w <= 0 || bounds.h <= 0) {
        return 0;
    }
    if (mx < bounds.x || mx >= bounds.x + bounds.w || my < bounds.y || my >= bounds.y + bounds.h) {
        return 0;
    }

    rangeCount = amiga_memview_overviewRangeCount(ui);
    if (rangeCount <= 0) {
        return 0;
    }

    localX = mx - bounds.x;
    localY = my - bounds.y;
    texX = (localX * AMIGA_MEMVIEW_OVERVIEW_TEXTURE_W) / bounds.w;
    texY = (localY * (rangeCount * AMIGA_MEMVIEW_OVERVIEW_RANGE_TEXTURE_H)) / bounds.h;
    if (texX < 0) {
        texX = 0;
    }
    if (texX >= AMIGA_MEMVIEW_OVERVIEW_TEXTURE_W) {
        texX = AMIGA_MEMVIEW_OVERVIEW_TEXTURE_W - 1;
    }
    if (texY < 0) {
        texY = 0;
    }
    if (texY >= rangeCount * AMIGA_MEMVIEW_OVERVIEW_RANGE_TEXTURE_H) {
        texY = rangeCount * AMIGA_MEMVIEW_OVERVIEW_RANGE_TEXTURE_H - 1;
    }

    for (int i = 0; i < AMIGA_MEMVIEW_OVERVIEW_MAX_RANGES; ++i) {
        const amiga_memview_overview_range_t *range = &ui->overviewRanges[i];
        uint32_t bytesPerCell = 0u;
        uint64_t cellIndex = 0u;
        uint64_t addr = 0u;

        if (range->sizeBytes == 0u) {
            continue;
        }

        if (texY < (activeRange + 1) * AMIGA_MEMVIEW_OVERVIEW_RANGE_TEXTURE_H) {
            bytesPerCell = amiga_memview_overviewBytesPerCell(range);
            cellIndex = (uint64_t)(texY - activeRange * AMIGA_MEMVIEW_OVERVIEW_RANGE_TEXTURE_H) *
                        AMIGA_MEMVIEW_OVERVIEW_TEXTURE_W +
                        (uint64_t)texX;
            addr = (uint64_t)(range->baseAddr & 0x00ffffffu) + cellIndex * (uint64_t)bytesPerCell;
            if (addr > 0x00ffffffu) {
                addr = 0x00ffffffu;
            }
            amiga_memview_setView(ui,
                                  amiga_memview_clampBaseForView(ui, &self->bounds, (uint32_t)addr),
                                  ui->rowBytes,
                                  0);
            return 1;
        }
        activeRange++;
    }

    return 0;
}

static void
amiga_memview_canvasRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    amiga_memview_state_t *ui = NULL;
    amiga_memview_step_buttons_action_ctx_t actionCtx;
    SDL_Rect clip;
    SDL_Rect contentClip;
    SDL_Rect prevClip;
    SDL_Rect fillRect;
    SDL_Rect bitDstRect;
    e9ui_rect_t overviewBounds = {0, 0, 0, 0};
    TTF_Font *font = NULL;
    int fontHeight = 16;
    int firstRow = 0;
    int lastRow = -1;
    int firstBit = 0;
    int lastBit = -1;
    int firstByte = 0;
    int lastByte = -1;
    int totalBits = 0;
    int labelStepRows = 8;
    size_t visibleRowCount = 0u;
    int visibleBitCount = 0;
    size_t readBytes = 0u;
    size_t visiblePixelCount = 0u;
    uint32_t rowBytes = 0u;
    uint64_t firstVisibleAddr = 0u;
    int blitterVisEnabled = 0;
    int bitAreaX = 0;
    int rowAreaY = 0;
    int rebuildBitTexture = 0;
    int leftGutterPx = 0;
    int rightGutter = 0;
    int bitPx = 1;
    int rowPx = 1;
    uint32_t litColor = 0xfff2b43cU;
    int visibleRows = 1;
    int hadClip = 0;
    int bitViewW = 0;
    int scrollBitOffsetPx = 0;
    e9ui_rect_t hscrollBounds = {0, 0, 0, 0};

    if (!self || !ctx || !ctx->renderer) {
        return;
    }
    ui = (amiga_memview_state_t*)self->state;
    if (!ui) {
        return;
    }
    (void)libretro_host_debugAmiGetBlitterDebug(&blitterVisEnabled);

    hadClip = SDL_RenderIsClipEnabled(ctx->renderer) ? 1 : 0;
    if (hadClip) {
        SDL_RenderGetClipRect(ctx->renderer, &prevClip);
    }
    clip.x = self->bounds.x;
    clip.y = self->bounds.y;
    clip.w = self->bounds.w;
    clip.h = self->bounds.h;
    if (clip.w <= 0 || clip.h <= 0) {
        SDL_RenderSetClipRect(ctx->renderer, hadClip ? &prevClip : NULL);
        return;
    }

    actionCtx.ui = ui;
    actionCtx.canvas = self;
    e9ui_step_buttons_tick(ctx,
                           self->bounds,
                           0,
                           1,
                           &ui->stepButtons,
                           &actionCtx,
                           amiga_memview_stepButtonsOnAction);

    font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    if (font) {
        fontHeight = TTF_FontHeight(font);
        if (fontHeight <= 0) {
            fontHeight = 16;
        }
    }
    if (fontHeight > 0) {
        int minStep = 0;
        rowPx = amiga_memview_rowPx(ui);
        minStep = (fontHeight + rowPx - 1) / rowPx;
        if (minStep > labelStepRows) {
            labelStepRows = minStep;
        }
    }
    overviewBounds = amiga_memview_overviewBounds(ui, self, ctx, font);
    leftGutterPx = amiga_memview_leftGutterPx(ui, ctx, font);
    rightGutter = amiga_memview_stepButtonsGutterWidth(ctx, self);
    if (rightGutter < 0) {
        rightGutter = 0;
    }
    if (rightGutter > clip.w) {
        rightGutter = clip.w;
    }
    bitPx = amiga_memview_bitPx(ui);
    rowPx = amiga_memview_rowPx(ui);
    rowBytes = amiga_memview_clampRowBytes(ui->rowBytes);
    totalBits = (int)(rowBytes * 8u);
    visibleRows = amiga_memview_canvasVisibleRows(ui, &self->bounds);
    ui->baseAddr = amiga_memview_clampBaseForView(ui, &self->bounds, ui->baseAddr);
    ui->contentPixelWidth = totalBits * bitPx + e9ui_scale_px(&ui->ctx, AMIGA_MEMVIEW_RIGHT_PAD_PX);
    hscrollBounds = amiga_memview_hscrollBounds(ui, self);
    if (hscrollBounds.w < 1) {
        hscrollBounds.w = 1;
    }
    {
        int scrollY = 0;
        e9ui_scrollbar_clamp(hscrollBounds.w, 1, ui->contentPixelWidth, 1, &ui->scrollX, &scrollY);
    }
    contentClip = clip;
    contentClip.w -= rightGutter;
    if (contentClip.w < 0) {
        contentClip.w = 0;
    }
    if (contentClip.w > 0 && contentClip.h > 0) {
        if (hadClip) {
            SDL_Rect clipped;
            if (SDL_IntersectRect(&prevClip, &contentClip, &clipped)) {
                SDL_RenderSetClipRect(ctx->renderer, &clipped);
            } else {
                SDL_RenderSetClipRect(ctx->renderer, &contentClip);
            }
        } else {
            SDL_RenderSetClipRect(ctx->renderer, &contentClip);
        }
    } else {
        SDL_RenderSetClipRect(ctx->renderer, NULL);
    }

    bitAreaX = self->bounds.x + leftGutterPx;
    rowAreaY = self->bounds.y + e9ui_scale_px(&ui->ctx, AMIGA_MEMVIEW_TOP_PAD_PX);
    firstRow = 0;
    lastRow = visibleRows - 1;
    bitViewW = contentClip.w - leftGutterPx;
    if (bitViewW < 1) {
        bitViewW = 1;
    }
    firstBit = ui->scrollX / bitPx;
    lastBit = (ui->scrollX + bitViewW - 1) / bitPx;
    if (firstBit < 0) {
        firstBit = 0;
    }
    if (lastBit < firstBit) {
        lastBit = firstBit;
    }
    if (lastBit >= totalBits) {
        lastBit = totalBits - 1;
    }
    scrollBitOffsetPx = ui->scrollX - firstBit * bitPx;
    firstByte = firstBit / 8;
    lastByte = lastBit / 8;
    visibleBitCount = lastBit - firstBit + 1;
    if (visibleBitCount <= 0) {
        goto cleanup;
    }

    fillRect = clip;
    SDL_SetRenderDrawColor(ctx->renderer, 15, 17, 22, 255);
    SDL_RenderFillRect(ctx->renderer, &fillRect);

    if (leftGutterPx > 0) {
        SDL_Rect gutterRect = {
            self->bounds.x,
            contentClip.y,
            leftGutterPx,
            contentClip.h
        };
        SDL_SetRenderDrawColor(ctx->renderer, 10, 12, 16, 255);
        SDL_RenderFillRect(ctx->renderer, &gutterRect);
    }

    if (overviewBounds.w > 0 && overviewBounds.h > 0) {
        e9ui_rect_t overviewContentBounds = amiga_memview_overviewContentBounds(ctx, &overviewBounds);

        if (!ui->overviewCacheValid ||
            ui->overviewRenderer != ctx->renderer ||
            ui->overviewCacheFrameCounter != debugger.frameCounter) {
            (void)amiga_memview_rebuildOverviewTexture(ui, ctx);
        }
        if (overviewContentBounds.w > 0 && ui->overviewTexture) {
            SDL_Rect dst = {
                overviewContentBounds.x,
                overviewContentBounds.y,
                overviewContentBounds.w,
                overviewContentBounds.h
            };
            SDL_RenderCopy(ctx->renderer, ui->overviewTexture, NULL, &dst);
            {
                int borderW = overviewContentBounds.x - overviewBounds.x;
                int activeRange = 0;
                int rangeCount = amiga_memview_overviewRangeCount(ui);

                for (int i = 0; i < AMIGA_MEMVIEW_OVERVIEW_MAX_RANGES; ++i) {
                    const amiga_memview_overview_range_t *range = &ui->overviewRanges[i];

                    if (range->sizeBytes == 0u) {
                        continue;
                    }
                    SDL_Color color = amiga_memview_rangeColor(range->baseAddr);
                    int y0 = overviewBounds.y + (activeRange * overviewBounds.h) / rangeCount;
                    int y1 = overviewBounds.y + ((activeRange + 1) * overviewBounds.h) / rangeCount;
                    SDL_Rect borderRect = {
                        overviewBounds.x,
                        y0,
                        overviewBounds.w,
                        y1 - y0
                    };
                    activeRange++;
                    if (borderRect.h <= 0 || borderRect.w <= 0 || borderW <= 0) {
                        continue;
                    }
                    SDL_SetRenderDrawColor(ctx->renderer, color.r, color.g, color.b, 255);
                    for (int inset = 0; inset < borderW; ++inset) {
                        SDL_Rect lineRect = {
                            borderRect.x + inset,
                            borderRect.y + inset,
                            borderRect.w - inset * 2,
                            borderRect.h - inset * 2
                        };

                        if (lineRect.w <= 0 || lineRect.h <= 0) {
                            break;
                        }
                        SDL_RenderDrawRect(ctx->renderer, &lineRect);
                    }
                }
            }
            amiga_memview_renderOverviewSelection(ui, ctx, &overviewBounds, visibleRows);
        }
    }

    if (ui->showAddressColumn && overviewBounds.w > 0 && overviewBounds.h > 0) {
        SDL_SetRenderDrawColor(ctx->renderer, 32, 36, 44, 255);
        int separatorX = overviewBounds.x - e9ui_scale_px(ctx, AMIGA_MEMVIEW_GUTTER_GAP_PX / 2);
        SDL_RenderDrawLine(ctx->renderer,
                           separatorX,
                           contentClip.y,
                           separatorX,
                           contentClip.y + contentClip.h - 1);
    }

    {
        SDL_SetRenderDrawColor(ctx->renderer, 38, 42, 52, 255);
        for (int byteIndex = firstByte; byteIndex <= lastByte; ++byteIndex) {
            if ((byteIndex & 1) == 0) {
                int lineX = bitAreaX + byteIndex * 8 * bitPx - ui->scrollX;
                if (lineX >= contentClip.x && lineX < contentClip.x + contentClip.w) {
                    SDL_RenderDrawLine(ctx->renderer,
                                       lineX,
                                       contentClip.y,
                                       lineX,
                                       contentClip.y + contentClip.h - 1);
                }
            }
        }
    }

    {
        SDL_SetRenderDrawColor(ctx->renderer, 56, 62, 74, 255);
        for (int wordIndex = firstByte / 2; wordIndex <= lastByte / 2; ++wordIndex) {
            int lineX = bitAreaX + wordIndex * 16 * bitPx - ui->scrollX;
            if (lineX >= contentClip.x && lineX < contentClip.x + contentClip.w) {
                SDL_RenderDrawLine(ctx->renderer,
                                   lineX,
                                   contentClip.y,
                                   lineX,
                                   contentClip.y + contentClip.h - 1);
            }
        }
    }

    visibleRowCount = (size_t)(lastRow - firstRow + 1);
    readBytes = visibleRowCount * (size_t)rowBytes;
    if (visibleRowCount == 0u || !amiga_memview_ensureReadBuffer(ui, readBytes)) {
        goto cleanup;
    }

    firstVisibleAddr = (uint64_t)ui->baseAddr;
    (void)amiga_memview_readRange(ui,
                                  (uint32_t)(firstVisibleAddr & 0x00ffffffu),
                                  ui->readBuffer,
                                  readBytes);

    rebuildBitTexture = !ui->bitCacheValid ||
        ui->bitRenderer != ctx->renderer ||
        ui->bitCacheBaseAddr != ui->baseAddr ||
        ui->bitCacheRowBytes != rowBytes ||
        ui->bitCacheFirstRow != firstRow ||
        ui->bitCacheLastRow != lastRow ||
        ui->bitCacheFirstBit != firstBit ||
        ui->bitCacheLastBit != lastBit ||
        ui->bitCacheFrameCounter != debugger.frameCounter;

    if (rebuildBitTexture) {
        int texW = visibleBitCount * bitPx;
        int texH = (int)visibleRowCount * rowPx;
        uint32_t tagBaseAddr = 0u;
        size_t tagWordCount = 0u;
        size_t tagWordBaseOffset = 0u;

        visiblePixelCount = (size_t)texW * (size_t)texH;
        if (!amiga_memview_ensureBitPixels(ui, visiblePixelCount) ||
            !amiga_memview_ensureBitTexture(ui, ctx->renderer, texW, texH)) {
            goto cleanup;
        }

        memset(ui->bitPixels, 0, visiblePixelCount * sizeof(*ui->bitPixels));
        if (blitterVisEnabled) {
            tagBaseAddr = ui->baseAddr & 0x00fffffeu;
            tagWordBaseOffset = (size_t)(ui->baseAddr & 1u);
            tagWordCount = (tagWordBaseOffset + readBytes + 1u) / 2u;
            if (!amiga_memview_ensureBlitTagBuffer(ui, tagWordCount)) {
                goto cleanup;
            }
            amiga_memview_zeroBlitTagBuffer(ui, tagWordCount);
            libretro_host_debugAmiReadBlitterVisWordTags(tagBaseAddr, ui->blitTagBuffer, tagWordCount);
        }

        for (size_t visibleRow = 0u; visibleRow < visibleRowCount; ++visibleRow) {
            uint8_t *rowData = ui->readBuffer + visibleRow * (size_t)rowBytes;
            int dstRow0 = (int)visibleRow * rowPx;

            if (!blitterVisEnabled) {
                for (int byteIndex = firstByte; byteIndex <= lastByte; ++byteIndex) {
                    uint8_t value = rowData[byteIndex];
                    int byteBitStart = byteIndex * 8;
                    int localBitStart = byteBitStart - firstBit;

                    if (value == 0u || localBitStart >= visibleBitCount || (localBitStart + 8) <= 0) {
                        continue;
                    }

                    for (int bitIndex = 0; bitIndex < 8; ++bitIndex) {
                        int localBit = localBitStart + bitIndex;
                        int dstX = 0;
                        size_t pixelIndex0 = 0u;

                        if (localBit < 0 || localBit >= visibleBitCount) {
                            continue;
                        }
                        if ((value & (uint8_t)(0x80u >> bitIndex)) == 0u) {
                            continue;
                        }

                        dstX = localBit * bitPx;
                        for (int fillY = 0; fillY < rowPx; ++fillY) {
                            int dstRow = dstRow0 + fillY;
                            if (dstRow >= texH) {
                                break;
                            }
                            pixelIndex0 = (size_t)dstRow * (size_t)texW + (size_t)dstX;
                            for (int fillX = 0; fillX < bitPx; ++fillX) {
                                ui->bitPixels[pixelIndex0 + (size_t)fillX] = litColor;
                            }
                        }
                    }
                }
                continue;
            }

            for (int byteIndex = firstByte; byteIndex <= lastByte; ++byteIndex) {
                uint8_t value = rowData[byteIndex];
                int byteBitStart = byteIndex * 8;
                int localBitStart = byteBitStart - firstBit;
                size_t absoluteByteIndex = visibleRow * (size_t)rowBytes + (size_t)byteIndex;
                uint32_t tagBlitId = 0u;
                uint32_t bitColor = litColor;

                if (value == 0u || localBitStart >= visibleBitCount || (localBitStart + 8) <= 0) {
                    continue;
                }
                if (tagWordCount > 0u) {
                    size_t tagIndex = (tagWordBaseOffset + absoluteByteIndex) / 2u;
                    if (tagIndex < tagWordCount) {
                        tagBlitId = ui->blitTagBuffer[tagIndex];
                    }
                }
                if (tagBlitId != 0u) {
                    bitColor = 0xff000000u | (emu_ami_getBlitterVisColor(tagBlitId) & 0x00ffffffu);
                }

                for (int bitIndex = 0; bitIndex < 8; ++bitIndex) {
                    int localBit = localBitStart + bitIndex;
                    int dstX = 0;
                    size_t pixelIndex0 = 0u;

                    if (localBit < 0 || localBit >= visibleBitCount) {
                        continue;
                    }
                    if ((value & (uint8_t)(0x80u >> bitIndex)) == 0u) {
                        continue;
                    }

                    dstX = localBit * bitPx;
                    for (int fillY = 0; fillY < rowPx; ++fillY) {
                        int dstRow = dstRow0 + fillY;
                        if (dstRow >= texH) {
                            break;
                        }
                        pixelIndex0 = (size_t)dstRow * (size_t)texW + (size_t)dstX;
                        for (int fillX = 0; fillX < bitPx; ++fillX) {
                            ui->bitPixels[pixelIndex0 + (size_t)fillX] = bitColor;
                        }
                    }
                }
            }
        }

        SDL_UpdateTexture(ui->bitTexture, NULL, ui->bitPixels, texW * (int)sizeof(*ui->bitPixels));
        ui->bitCacheBaseAddr = ui->baseAddr;
        ui->bitCacheRowBytes = rowBytes;
        ui->bitCacheFirstRow = firstRow;
        ui->bitCacheLastRow = lastRow;
        ui->bitCacheFirstBit = firstBit;
        ui->bitCacheLastBit = lastBit;
        ui->bitCacheFrameCounter = debugger.frameCounter;
        ui->bitCacheValid = 1;
    }

    bitDstRect.x = bitAreaX - scrollBitOffsetPx;
    bitDstRect.y = rowAreaY;
    bitDstRect.w = visibleBitCount * bitPx;
    bitDstRect.h = (int)visibleRowCount * rowPx;
    if (ui->bitTexture) {
        SDL_RenderCopy(ctx->renderer, ui->bitTexture, NULL, &bitDstRect);
    }

    for (size_t visibleRow = 0u; visibleRow < visibleRowCount; ++visibleRow) {
        int rowIndex = (int)visibleRow;
        int rowY = rowAreaY + rowIndex * rowPx;

        if (ui->showAddressColumn && (rowIndex % labelStepRows) == 0 && font) {
            uint32_t rowAddr = (uint32_t)(((uint64_t)ui->baseAddr + (uint64_t)visibleRow * (uint64_t)rowBytes) & 0x00ffffffu);
            amiga_memview_drawAddressLabel(ui, ctx, font, rowAddr, self->bounds.x + 6, rowY - 1);
        }
    }

    SDL_RenderSetClipRect(ctx->renderer, hadClip ? &prevClip : NULL);
    e9ui_scrollbar_render(self,
                          ctx,
                          hscrollBounds,
                          hscrollBounds.w,
                          1,
                          ui->contentPixelWidth,
                          1,
                          ui->scrollX,
                          0);
    e9ui_step_buttons_render(ctx,
                             self->bounds,
                             0,
                             1,
                             &ui->stepButtons);
    return;

cleanup:
    SDL_RenderSetClipRect(ctx->renderer, hadClip ? &prevClip : NULL);
}

static void
amiga_memview_canvasDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->state = NULL;
}

static int
amiga_memview_canvasHandleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    amiga_memview_state_t *ui = NULL;
    amiga_memview_step_buttons_action_ctx_t actionCtx;
    e9ui_rect_t hscrollBounds = {0, 0, 0, 0};
    int scrollX = 0;
    int scrollY = 0;
    int mx = 0;
    int my = 0;

    if (!self || !ctx || !ev) {
        return 0;
    }
    ui = (amiga_memview_state_t*)self->state;
    if (!ui) {
        return 0;
    }

    actionCtx.ui = ui;
    actionCtx.canvas = self;

    if (ev->type == SDL_MOUSEMOTION) {
        mx = ev->motion.x;
        my = ev->motion.y;
    } else if (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP) {
        mx = ev->button.x;
        my = ev->button.y;
    } else if (ev->type == SDL_MOUSEWHEEL) {
        mx = e9ui->mouseX;
        my = e9ui->mouseY;
    }

    if ((ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_LEFT) ||
        (ev->type == SDL_MOUSEMOTION && (ev->motion.state & SDL_BUTTON_LMASK) != 0u)) {
        if (amiga_memview_overviewNavigate(ui, self, ctx, mx, my)) {
            return 1;
        }
    }

    if (ctx &&
        (ev->type == SDL_MOUSEMOTION || ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP)) {
        hscrollBounds = amiga_memview_hscrollBounds(ui, self);
        if (hscrollBounds.w < 1) {
            hscrollBounds.w = 1;
        }
        scrollX = ui->scrollX;
        scrollY = 0;
        if (e9ui_scrollbar_handleEvent(self,
                                       ctx,
                                       ev,
                                       hscrollBounds,
                                       hscrollBounds.w,
                                       1,
                                       ui->contentPixelWidth,
                                       1,
                                       &scrollX,
                                       &scrollY,
                                       &ui->hScrollbar)) {
            ui->scrollX = scrollX;
            return 1;
        }
    }

    if (ctx &&
        e9ui_step_buttons_handleEvent(ctx,
                                      ev,
                                      self->bounds,
                                      0,
                                      1,
                                      &ui->stepButtons,
                                      &actionCtx,
                                      amiga_memview_stepButtonsOnAction)) {
        return 1;
    }

    if (ev->type == SDL_MOUSEWHEEL &&
        mx >= self->bounds.x &&
        mx < self->bounds.x + self->bounds.w &&
        my >= self->bounds.y &&
        my < self->bounds.y + self->bounds.h) {
        int wheelX = ev->wheel.x;
        int wheelY = ev->wheel.y;

        if (ev->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
            wheelX = -wheelX;
            wheelY = -wheelY;
        }
        if (wheelX != 0) {
            hscrollBounds = amiga_memview_hscrollBounds(ui, self);
            if (hscrollBounds.w < 1) {
                hscrollBounds.w = 1;
            }
            ui->scrollX -= wheelX * e9ui_scale_px(ctx, 24);
            scrollY = 0;
            e9ui_scrollbar_clamp(hscrollBounds.w, 1, ui->contentPixelWidth, 1, &ui->scrollX, &scrollY);
        }
        if (wheelY != 0) {
            amiga_memview_scrollRows(ui, &self->bounds, wheelY);
            return 1;
        }
        if (wheelX != 0) {
            return 1;
        }
    }

    if (ev->type == SDL_KEYDOWN && ctx && e9ui_getFocus(ctx) == self) {
        if (ev->key.keysym.sym == SDLK_PAGEUP) {
            amiga_memview_scrollRows(ui, &self->bounds, -amiga_memview_canvasVisibleRows(ui, &self->bounds));
            return 1;
        }
        if (ev->key.keysym.sym == SDLK_PAGEDOWN) {
            amiga_memview_scrollRows(ui, &self->bounds, amiga_memview_canvasVisibleRows(ui, &self->bounds));
            return 1;
        }
        if (ev->key.keysym.sym == SDLK_UP) {
            amiga_memview_scrollRows(ui, &self->bounds, -1);
            return 1;
        }
        if (ev->key.keysym.sym == SDLK_DOWN) {
            amiga_memview_scrollRows(ui, &self->bounds, 1);
            return 1;
        }
        if (ev->key.keysym.sym == SDLK_LEFT) {
            if ((ev->key.keysym.mod & KMOD_SHIFT) != 0) {
                return amiga_memview_handleWidthKey(ui, ev->key.keysym.sym, ev->key.keysym.mod);
            }
            hscrollBounds = amiga_memview_hscrollBounds(ui, self);
            if (hscrollBounds.w < 1) {
                hscrollBounds.w = 1;
            }
            ui->scrollX -= e9ui_scale_px(ctx, 24);
            e9ui_scrollbar_clamp(hscrollBounds.w, 1, ui->contentPixelWidth, 1, &ui->scrollX, &scrollY);
            return 1;
        }
        if (ev->key.keysym.sym == SDLK_RIGHT) {
            if ((ev->key.keysym.mod & KMOD_SHIFT) != 0) {
                return amiga_memview_handleWidthKey(ui, ev->key.keysym.sym, ev->key.keysym.mod);
            }
            hscrollBounds = amiga_memview_hscrollBounds(ui, self);
            if (hscrollBounds.w < 1) {
                hscrollBounds.w = 1;
            }
            ui->scrollX += e9ui_scale_px(ctx, 24);
            e9ui_scrollbar_clamp(hscrollBounds.w, 1, ui->contentPixelWidth, 1, &ui->scrollX, &scrollY);
            return 1;
        }
    }

    return 0;
}

static e9ui_component_t *
amiga_memview_makeCanvas(amiga_memview_state_t *ui)
{
    e9ui_component_t *canvas = NULL;

    if (!ui) {
        return NULL;
    }
    canvas = (e9ui_component_t*)alloc_calloc(1, sizeof(*canvas));
    if (!canvas) {
        return NULL;
    }
    canvas->name = "amiga_memview_canvas";
    canvas->state = ui;
    canvas->preferredHeight = amiga_memview_canvasPreferredHeight;
    canvas->layout = amiga_memview_canvasLayout;
    canvas->render = amiga_memview_canvasRender;
    canvas->handleEvent = amiga_memview_canvasHandleEvent;
    canvas->dtor = amiga_memview_canvasDtor;
    canvas->focusable = 1;
    return canvas;
}

static void
amiga_memview_clearUiRefs(amiga_memview_state_t *ui)
{
    if (!ui) {
        return;
    }
    ui->canvas = NULL;
    ui->autoButton = NULL;
    ui->addressBox = NULL;
    ui->widthBox = NULL;
    ui->widthSeek = NULL;
    ui->zoomSeek = NULL;
}

static int
amiga_memview_measureToolbarTextWidth(const e9ui_context_t *ctx,
                                      TTF_Font *font,
                                      const char *text,
                                      int basePx,
                                      int fallbackPx)
{
    int width = 0;
    int textW = 0;
    int textH = 0;

    if (!ctx) {
        return 0;
    }
    width = e9ui_scale_px(ctx, basePx);
    if (font && text && TTF_SizeUTF8(font, text, &textW, &textH) == 0) {
        return width + textW;
    }
    return e9ui_scale_px(ctx, fallbackPx);
}

static int
amiga_memview_measureToolbarTextboxWidth(TTF_Font *font,
                                         const char *text,
                                         int fallbackPx)
{
    int textW = 0;
    int textH = 0;

    if (font && text && TTF_SizeUTF8(font, text, &textW, &textH) == 0) {
        return textW + 18;
    }
    return fallbackPx;
}

static e9ui_component_t *
amiga_memview_buildRoot(amiga_memview_state_t *ui)
{
    e9ui_component_t *root = NULL;
    e9ui_component_t *toolbar = NULL;
    e9ui_component_t *toolbarBox = NULL;
    e9ui_component_t *groupGeneral = NULL;
    e9ui_component_t *groupLegend = NULL;
    e9ui_component_t *groupAddress = NULL;
    e9ui_component_t *groupWidth = NULL;
    e9ui_component_t *groupZoom = NULL;
    e9ui_component_t *groupGeneralItem = NULL;
    e9ui_component_t *groupLegendItem = NULL;
    e9ui_component_t *groupAddressItem = NULL;
    e9ui_component_t *groupWidthItem = NULL;
    e9ui_component_t *groupZoomItem = NULL;
    e9ui_component_t *legend = NULL;
    e9ui_component_t *autoButton = NULL;
    e9ui_component_t *showAddress = NULL;
    e9ui_component_t *showOverview = NULL;
    e9ui_component_t *addressLabel = NULL;
    e9ui_component_t *widthLabel = NULL;
    e9ui_component_t *zoomLabel = NULL;
    e9ui_component_t *saveButton = NULL;
    TTF_Font *toolbarFont = NULL;
    int gapSmall = 0;
    int autoButtonW = 0;
    int saveButtonW = 0;
    int checkboxAddrW = 0;
    int checkboxOverviewW = 0;
    int labelAddrW = 0;
    int addrBoxW = 0;
    int labelWidthW = 0;
    int widthBoxW = 0;
    int widthSeekW = 0;
    int zoomLabelW = 0;
    int zoomSeekW = 0;
    int textH = 0;
    int groupGeneralW = 0;
    int groupLegendW = 0;
    int groupAddressW = 0;
    int groupWidthW = 0;
    int groupZoomW = 0;

    if (!ui) {
        return NULL;
    }

    root = e9ui_stack_makeVertical();
    toolbar = amiga_memview_makeToolbarWrap();
    ui->addressBox = e9ui_textbox_make(32, amiga_memview_onAddressSubmit, NULL, ui);
    ui->widthBox = e9ui_textbox_make(16, amiga_memview_onWidthSubmit, NULL, ui);
    ui->widthSeek = e9ui_seek_bar_make();
    ui->zoomSeek = e9ui_seek_bar_make();
    ui->canvas = amiga_memview_makeCanvas(ui);

    addressLabel = e9ui_text_make("Address");
    widthLabel = e9ui_text_make("Width");
    zoomLabel = e9ui_text_make("Zoom");
    legend = amiga_memview_makeLegend(ui);
    autoButton = e9ui_button_make("Auto", amiga_memview_onAuto, ui);
    saveButton = e9ui_button_make("Save", amiga_memview_onSave, ui);
    ui->autoButton = autoButton;
    showAddress = e9ui_checkbox_make("Addr", ui->showAddressColumn, amiga_memview_onShowAddressColumnChanged, ui);
    showOverview = e9ui_checkbox_make("Overview", ui->showOverviewColumn, amiga_memview_onShowOverviewColumnChanged, ui);

    e9ui_textbox_setPlaceholder(ui->addressBox, "0x000000");
    e9ui_textbox_setPlaceholder(ui->widthBox, "20");
    e9ui_textbox_setFocusBorderVisible(ui->addressBox, 0);
    e9ui_textbox_setFocusBorderVisible(ui->widthBox, 0);
    amiga_memview_initSeekBar(ui->widthSeek,
                                   amiga_memview_widthSeekHandleEvent,
                                   amiga_memview_onWidthSeekChanged,
                                   amiga_memview_widthSeekTooltip,
                                   ui,
			           &ui->widthSeekDefaultHandleEvent);
    amiga_memview_initSeekBar(ui->zoomSeek,
                                   amiga_memview_zoomSeekHandleEvent,
                                   amiga_memview_onZoomSeekChanged,
                                   amiga_memview_zoomSeekTooltip,
                                   ui,
			           &ui->zoomSeekDefaultHandleEvent);

    e9ui_button_setMini(autoButton, 1);
    e9ui_button_setMini(saveButton, 1);
    e9ui_button_setLargestLabel(autoButton, "Auto (64/64)");
    amiga_memview_syncAutoButtonLabel(ui);

    toolbarFont = e9ui->theme.text.source ? e9ui->theme.text.source : ui->ctx.font;
    gapSmall = e9ui_scale_px(&ui->ctx, 6);
    autoButtonW = e9ui_scale_px(&ui->ctx, 92);
    saveButtonW = e9ui_scale_px(&ui->ctx, 72);
    e9ui_button_measure(autoButton, &ui->ctx, &autoButtonW, &textH);
    e9ui_button_measure(saveButton, &ui->ctx, &saveButtonW, &textH);
    e9ui_checkbox_measure(showAddress, &ui->ctx, &checkboxAddrW, &textH);
    e9ui_checkbox_measure(showOverview, &ui->ctx, &checkboxOverviewW, &textH);
    labelAddrW = amiga_memview_measureToolbarTextWidth(&ui->ctx, toolbarFont, "Address", 8, 64);
    addrBoxW = amiga_memview_measureToolbarTextboxWidth(toolbarFont, "0x000000", 104);
    labelWidthW = amiga_memview_measureToolbarTextWidth(&ui->ctx, toolbarFont, "Width", 8, 48);
    widthBoxW = amiga_memview_measureToolbarTextboxWidth(toolbarFont, "256", 56);
    widthSeekW = e9ui_scale_px(&ui->ctx, 180);
    zoomLabelW = amiga_memview_measureToolbarTextWidth(&ui->ctx, toolbarFont, "Zoom", 8, 40);
    zoomSeekW = e9ui_scale_px(&ui->ctx, 180);

    groupGeneral = e9ui_hstack_make();
    groupLegend =  e9ui_hstack_make();
    groupAddress = e9ui_hstack_make();
    groupWidth = e9ui_hstack_make();
    groupZoom = e9ui_hstack_make();

    e9ui_hstack_addFixed(groupGeneral, autoButton, autoButtonW);
    e9ui_hstack_addFixed(groupGeneral, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupGeneral, showAddress, checkboxAddrW);
    e9ui_hstack_addFixed(groupGeneral, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupGeneral, showOverview, checkboxOverviewW);
    groupGeneralW = autoButtonW + gapSmall + checkboxAddrW + gapSmall + checkboxOverviewW;

    if (legend && groupLegend) {
        groupLegendW = amiga_memview_legendMeasureWidth(ui, &ui->ctx);
        if (groupLegendW > 0) {
            e9ui_hstack_addFixed(groupLegend, legend, groupLegendW);
        }
    }

    e9ui_hstack_addFixed(groupAddress, addressLabel, labelAddrW);
    e9ui_hstack_addFixed(groupAddress, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupAddress, ui->addressBox, addrBoxW);
    groupAddressW = labelAddrW + gapSmall + addrBoxW;

    e9ui_hstack_addFixed(groupWidth, widthLabel, labelWidthW);
    e9ui_hstack_addFixed(groupWidth, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupWidth, ui->widthBox, widthBoxW);
    e9ui_hstack_addFixed(groupWidth, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupWidth, ui->widthSeek, widthSeekW);
    groupWidthW = labelWidthW + gapSmall + widthBoxW + gapSmall + widthSeekW;

    e9ui_hstack_addFixed(groupZoom, zoomLabel, zoomLabelW);
    e9ui_hstack_addFixed(groupZoom, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupZoom, ui->zoomSeek, zoomSeekW);
    groupZoomW = zoomLabelW + gapSmall + zoomSeekW;

    groupGeneralItem = amiga_memview_makeToolbarItem(groupGeneral, groupGeneralW);
    groupLegendItem = (legend && groupLegend && groupLegendW > 0) ? amiga_memview_makeToolbarItem(groupLegend, groupLegendW) : NULL;
    groupAddressItem = amiga_memview_makeToolbarItem(groupAddress, groupAddressW);
    groupWidthItem = amiga_memview_makeToolbarItem(groupWidth, groupWidthW);
    groupZoomItem = amiga_memview_makeToolbarItem(groupZoom, groupZoomW);

    e9ui_child_add(toolbar, groupGeneralItem, NULL);
    e9ui_child_add(toolbar, groupAddressItem, NULL);
    e9ui_child_add(toolbar, groupWidthItem, NULL);
    e9ui_child_add(toolbar, groupZoomItem, NULL);
    e9ui_child_add(toolbar, groupLegendItem, NULL);
    e9ui_child_add(toolbar, amiga_memview_makeToolbarItem(saveButton, saveButtonW), NULL);

    toolbarBox = e9ui_box_make(toolbar);

    e9ui_box_setPadding(toolbarBox, 8);
    e9ui_box_setBorder(toolbarBox, E9UI_BORDER_BOTTOM, (SDL_Color){ 70, 70, 70, 255 }, 1);

    e9ui_stack_addFixed(root, toolbarBox);
    e9ui_stack_addFlex(root, ui->canvas);

    return root;
}

static int
amiga_memview_overlayBodyPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)ctx;
    (void)availW;
    return 0;
}

static void
amiga_memview_overlayBodyLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    amiga_memview_overlay_body_state_t *state = NULL;
    amiga_memview_state_t *ui = NULL;

    if (!self || !ctx || !self->state) {
        return;
    }

    self->bounds = bounds;
    state = (amiga_memview_overlay_body_state_t*)self->state;
    ui = state ? state->ui : NULL;
    if (!ui) {
        return;
    }

    ui->ctx = *ctx;
    ui->ctx.window = ctx->window;
    ui->ctx.renderer = ctx->renderer;
    ui->ctx.font = e9ui->ctx.font;
    ui->ctx.winW = bounds.w;
    ui->ctx.winH = bounds.h;
    ui->ctx.focusRoot = ui->root;
    ui->ctx.focusFullscreen = NULL;

    if (ui->root && ui->root->layout) {
        ui->root->layout(ui->root, &ui->ctx, bounds);
    }
}

static void
amiga_memview_overlayBodyRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    amiga_memview_overlay_body_state_t *state = NULL;
    amiga_memview_state_t *ui = NULL;

    if (!self || !ctx || !self->state) {
        return;
    }

    state = (amiga_memview_overlay_body_state_t*)self->state;
    ui = state ? state->ui : NULL;
    if (!ui || !ui->windowState.open) {
        return;
    }

    ui->ctx = *ctx;
    ui->ctx.window = ctx->window;
    ui->ctx.renderer = ctx->renderer;
    ui->ctx.font = e9ui->ctx.font;
    ui->ctx.winW = self->bounds.w;
    ui->ctx.winH = self->bounds.h;
    ui->ctx.mouseX = ctx->mouseX;
    ui->ctx.mouseY = ctx->mouseY;
    ui->ctx.mousePrevX = ctx->mousePrevX;
    ui->ctx.mousePrevY = ctx->mousePrevY;
    ui->ctx.focusRoot = ui->root;
    ui->ctx.focusFullscreen = NULL;

    if (ui->root && ui->root->render) {
        ui->root->render(ui->root, &ui->ctx);
    }
}

static void
amiga_memview_overlayBodyDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    alloc_free(self->state);
    self->state = NULL;
}

static e9ui_component_t *
amiga_memview_makeOverlayBodyHost(amiga_memview_state_t *ui)
{
    e9ui_component_t *host = NULL;
    amiga_memview_overlay_body_state_t *state = NULL;

    if (!ui || !ui->root) {
        return NULL;
    }

    host = (e9ui_component_t*)alloc_calloc(1, sizeof(*host));
    state = (amiga_memview_overlay_body_state_t*)alloc_calloc(1, sizeof(*state));
    if (!host || !state) {
        alloc_free(host);
        alloc_free(state);
        return NULL;
    }

    state->ui = ui;
    host->name = "amiga_memview_overlay_body";
    host->state = state;
    host->preferredHeight = amiga_memview_overlayBodyPreferredHeight;
    host->layout = amiga_memview_overlayBodyLayout;
    host->render = amiga_memview_overlayBodyRender;
    host->dtor = amiga_memview_overlayBodyDtor;
    e9ui_child_add(host, ui->root, alloc_strdup("amiga_memview_root"));
    return host;
}

static void
amiga_memview_overlayWindowCloseRequested(e9ui_window_t *window, void *user)
{
    (void)window;
    (void)user;
    amiga_memview_shutdown();
}

static void
amiga_memview_resetRuntimeState(amiga_memview_state_t *ui)
{
    if (!ui) {
        return;
    }
    ui->readBuffer = NULL;
    ui->readBufferCap = 0u;
    ui->blitTagBuffer = NULL;
    ui->blitTagBufferCap = 0u;
    ui->bitRenderer = NULL;
    ui->bitTexture = NULL;
    ui->bitPixels = NULL;
    ui->bitPixelsCap = 0u;
    ui->bitTextureW = 0;
    ui->bitTextureH = 0;
    ui->bitCacheValid = 0;
    ui->bitCacheBaseAddr = 0u;
    ui->bitCacheRowBytes = 0u;
    ui->bitCacheFirstRow = 0;
    ui->bitCacheLastRow = -1;
    ui->bitCacheFirstBit = 0;
    ui->bitCacheLastBit = -1;
    ui->bitCacheFrameCounter = 0u;
    ui->overviewRenderer = NULL;
    ui->overviewTexture = NULL;
    ui->overviewPixels = NULL;
    ui->overviewPixelsCap = 0u;
    ui->overviewTextureW = 0;
    ui->overviewTextureH = 0;
    ui->overviewCacheValid = 0;
    ui->overviewCacheFrameCounter = 0u;
    ui->autoCandidateCount = 0;
    ui->autoCandidateIndex = 0;
    ui->root = NULL;
    amiga_memview_clearUiRefs(ui);
    ui->zoomLevel = AMIGA_MEMVIEW_ZOOM_DEFAULT;
    if (!ui->showAddressColumnHasSaved) {
        ui->showAddressColumn = 1;
    }
    if (!ui->showOverviewColumnHasSaved) {
        ui->showOverviewColumn = 1;
    }
    ui->scrollX = 0;
    ui->contentPixelWidth = 0;
    memset(&ui->stepButtons, 0, sizeof(ui->stepButtons));
    memset(&ui->hScrollbar, 0, sizeof(ui->hScrollbar));
    ui->widthSeekDefaultHandleEvent = NULL;
    ui->zoomSeekDefaultHandleEvent = NULL;
}

static void
amiga_memview_releaseRuntimeState(amiga_memview_state_t *ui)
{
    if (!ui) {
        return;
    }

    alloc_free(ui->readBuffer);
    ui->readBuffer = NULL;
    ui->readBufferCap = 0u;
    alloc_free(ui->blitTagBuffer);
    ui->blitTagBuffer = NULL;
    ui->blitTagBufferCap = 0u;
    amiga_memview_discardBitTexture(ui);
    amiga_memview_discardOverviewTexture(ui);
    alloc_free(ui->bitPixels);
    ui->bitPixels = NULL;
    ui->bitPixelsCap = 0u;
    alloc_free(ui->overviewPixels);
    ui->overviewPixels = NULL;
    ui->overviewPixelsCap = 0u;
    ui->root = NULL;
    amiga_memview_clearUiRefs(ui);
    ui->widthSeekDefaultHandleEvent = NULL;
    ui->zoomSeekDefaultHandleEvent = NULL;
}

int
amiga_memview_init(void)
{
    amiga_memview_state_t *ui = &amiga_memview_stateSingleton;
    e9ui_component_t *overlayBodyHost = NULL;
    e9ui_rect_t rect;
    if (ui->windowState.open) {
        return 1;
    }

    amiga_memview_resetRuntimeState(ui);
    ui->ctx = e9ui->ctx;
    amiga_memview_initDefaultOverviewRanges(ui);

    ui->windowState.windowHost = e9ui_windowCreate(amiga_memview_windowBackend());
    if (!ui->windowState.windowHost) {
        return 0;
    }

    if (ui->rowBytesHasSaved) {
        ui->rowBytes = amiga_memview_clampRowBytes(ui->rowBytes);
    } else {
        ui->rowBytes = AMIGA_MEMVIEW_DEFAULT_ROW_BYTES;
    }
    if (ui->zoomHasSaved) {
        ui->zoomLevel = amiga_memview_clampZoomLevel(ui->zoomLevel);
    } else {
        ui->zoomLevel = AMIGA_MEMVIEW_ZOOM_DEFAULT;
    }
    if (ui->baseAddrHasSaved) {
        ui->baseAddr = amiga_memview_clampBaseAddr(ui->baseAddr);
    } else {
        ui->baseAddr = 0u;
    }

    ui->root = amiga_memview_buildRoot(ui);
    if (!ui->root) {
        e9ui_windowDestroy(ui->windowState.windowHost);
        ui->windowState.windowHost = NULL;
        return 0;
    }

    rect = e9ui_windowResolveStateOpenRect(&e9ui->ctx,
                                           amiga_memview_windowDefaultRect(&e9ui->ctx),
                                           &ui->windowState);

    overlayBodyHost = amiga_memview_makeOverlayBodyHost(ui);
    if (!overlayBodyHost) {
        e9ui_windowDestroy(ui->windowState.windowHost);
        ui->windowState.windowHost = NULL;
        ui->root = NULL;
        return 0;
    }

    e9ui_windowOpen(ui->windowState.windowHost,
                         AMIGA_MEMVIEW_TITLE,
                         rect,
                         overlayBodyHost,
                         amiga_memview_overlayWindowCloseRequested,
                         ui,
		         &e9ui->ctx);

    ui->ctx = e9ui->ctx;
    ui->windowState.open = 1;
    aux_window_register(&amiga_memview_auxWindowOps, ui);

    if (ui->baseAddrHasSaved && ui->rowBytesHasSaved) {
        amiga_memview_setView(ui, ui->baseAddr, ui->rowBytes, 1);
    } else {
        amiga_memview_applyAuto(ui, 1);
    }
    return 1;
}

void
amiga_memview_shutdown(void)
{
    amiga_memview_state_t *ui = &amiga_memview_stateSingleton;

    if (!ui->windowState.open) {
        return;
    }

    aux_window_unregister(&amiga_memview_auxWindowOps, ui);
    (void)e9ui_windowCaptureStateRectSnapshot(&ui->windowState, &e9ui->ctx);
    config_saveConfig();

    if (ui->windowState.windowHost) {
        e9ui_windowDestroy(ui->windowState.windowHost);
        ui->windowState.windowHost = NULL;
    }

    amiga_memview_releaseRuntimeState(ui);
    ui->windowState.open = 0;
    memset(&ui->ctx, 0, sizeof(ui->ctx));
}

void
amiga_memview_toggle(void)
{
    if (amiga_memview_isOpen()) {
        amiga_memview_shutdown();
        return;
    }
    (void)amiga_memview_init();
}

int
amiga_memview_isOpen(void)
{
    return amiga_memview_stateSingleton.windowState.open ? 1 : 0;
}

void
amiga_memview_setMainWindowFocused(int focused)
{
    (void)focused;
}

void
amiga_memview_render(void)
{
    amiga_memview_state_t *ui = &amiga_memview_stateSingleton;

    if (!ui->windowState.open) {
        return;
    }
    if (e9ui_windowCaptureStateRectChanged(&ui->windowState, &e9ui->ctx)) {
        config_saveConfig();
    }
}

void
amiga_memview_persistConfig(FILE *file)
{
    amiga_memview_state_t *ui = &amiga_memview_stateSingleton;

    if (!file) {
        return;
    }
    e9ui_windowPersistStateRect(file, "comp.amiga_memview", &ui->windowState, &e9ui->ctx);
    if (ui->rowBytesHasSaved) {
        fprintf(file, "comp.amiga_memview.row_bytes=%u\n", (unsigned)ui->rowBytes);
    }
    if (ui->zoomHasSaved) {
        fprintf(file, "comp.amiga_memview.zoom=%d\n", ui->zoomLevel);
    }
    if (ui->showAddressColumnHasSaved) {
        fprintf(file, "comp.amiga_memview.show_address=%d\n", ui->showAddressColumn ? 1 : 0);
    }
    if (ui->showOverviewColumnHasSaved) {
        fprintf(file, "comp.amiga_memview.show_overview=%d\n", ui->showOverviewColumn ? 1 : 0);
    }
}

int
amiga_memview_loadConfigProperty(const char *prop, const char *value)
{
    amiga_memview_state_t *ui = &amiga_memview_stateSingleton;
    int intValue = 0;
    unsigned long long parsed = 0u;
    char *end = NULL;

    if (!prop || !value) {
        return 0;
    }

    if (strcmp(prop, "win_x") == 0) {
        if (!amiga_memview_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winX = intValue;
        ui->windowState.winHasSaved =
            e9ui_windowHasSavedPosition(ui->windowState.winX, ui->windowState.winY);
        return 1;
    }
    if (strcmp(prop, "win_y") == 0) {
        if (!amiga_memview_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winY = intValue;
        ui->windowState.winHasSaved =
            e9ui_windowHasSavedPosition(ui->windowState.winX, ui->windowState.winY);
        return 1;
    }
    if (strcmp(prop, "win_w") == 0) {
        if (!amiga_memview_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winW = intValue;
        ui->windowState.winHasSaved =
            e9ui_windowHasSavedPosition(ui->windowState.winX, ui->windowState.winY);
        return 1;
    }
    if (strcmp(prop, "win_h") == 0) {
        if (!amiga_memview_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winH = intValue;
        ui->windowState.winHasSaved =
            e9ui_windowHasSavedPosition(ui->windowState.winX, ui->windowState.winY);
        return 1;
    }
    if (strcmp(prop, "base_addr") == 0) {
        return 1;
    }
    if (strcmp(prop, "row_bytes") == 0) {
        if (!amiga_memview_parseU64SmartHex(value, &parsed, &end) || !end || *end != '\0' || parsed == 0u) {
            return 0;
        }
        ui->rowBytes = amiga_memview_clampRowBytes((uint32_t)parsed);
        ui->rowBytesHasSaved = 1;
        return 1;
    }
    if (strcmp(prop, "zoom") == 0) {
        if (!amiga_memview_parseInt(value, &intValue)) {
            return 0;
        }
        ui->zoomLevel = amiga_memview_clampZoomLevel(intValue);
        ui->zoomHasSaved = 1;
        return 1;
    }
    if (strcmp(prop, "show_address") == 0) {
        if (!amiga_memview_parseInt(value, &intValue)) {
            return 0;
        }
        ui->showAddressColumn = intValue ? 1 : 0;
        ui->showAddressColumnHasSaved = 1;
        return 1;
    }
    if (strcmp(prop, "show_overview") == 0) {
        if (!amiga_memview_parseInt(value, &intValue)) {
            return 0;
        }
        ui->showOverviewColumn = intValue ? 1 : 0;
        ui->showOverviewColumnHasSaved = 1;
        return 1;
    }
    if (strncmp(prop, "overview_range", 14) == 0) {
        return 1;
    }

    return 0;
}
