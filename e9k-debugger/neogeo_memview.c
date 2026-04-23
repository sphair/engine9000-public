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
#include "aux_window.h"
#include "config.h"
#include "debugger.h"
#include "e9ui.h"
#include "e9ui_box.h"
#include "e9ui_button.h"
#include "e9ui_checkbox.h"
#include "e9ui_hstack.h"
#include "e9ui_scrollbar.h"
#include "e9ui_seek_bar.h"
#include "e9ui_spacer.h"
#include "e9ui_stack.h"
#include "e9ui_step_buttons.h"
#include "e9ui_text.h"
#include "e9ui_textbox.h"
#include "e9ui_window.h"
#include "libretro_host.h"
#include "neogeo_memview.h"

#define NEOGEO_MEMVIEW_TITLE "ENGINE9000 DEBUGGER - NEO GEO MEMVIEW"
#define NEOGEO_MEMVIEW_RAM_BASE_MIN 0x00100000u
#define NEOGEO_MEMVIEW_RAM_BASE_MAX 0x0010ffffu
#define NEOGEO_MEMVIEW_DEFAULT_RAM_ROW_BYTES 32u
#define NEOGEO_MEMVIEW_MAX_RAM_ROW_BYTES 256u
#define NEOGEO_MEMVIEW_DEFAULT_CROM_TILES_PER_ROW 8u
#define NEOGEO_MEMVIEW_MAX_CROM_TILES_PER_ROW 64u
#define NEOGEO_MEMVIEW_TILE_BYTES 128u
#define NEOGEO_MEMVIEW_TILE_W 16
#define NEOGEO_MEMVIEW_TILE_H 16
#define NEOGEO_MEMVIEW_TILE_GAP_PX 2
#define NEOGEO_MEMVIEW_GUTTER_GAP_PX 8
#define NEOGEO_MEMVIEW_OVERVIEW_GUTTER_PX 56
#define NEOGEO_MEMVIEW_RIGHT_PAD_PX 16
#define NEOGEO_MEMVIEW_TOP_PAD_PX 8
#define NEOGEO_MEMVIEW_BOTTOM_PAD_PX 8
#define NEOGEO_MEMVIEW_RAM_BIT_PX 2
#define NEOGEO_MEMVIEW_RAM_ROW_PX 2
#define NEOGEO_MEMVIEW_ZOOM_MIN 1
#define NEOGEO_MEMVIEW_ZOOM_MAX 32
#define NEOGEO_MEMVIEW_ZOOM_DEFAULT 8
#define NEOGEO_MEMVIEW_OVERVIEW_ZOOM_MIN 1
#define NEOGEO_MEMVIEW_OVERVIEW_ZOOM_MAX 32
#define NEOGEO_MEMVIEW_OVERVIEW_ZOOM_DEFAULT 1
#define NEOGEO_MEMVIEW_FOLLOW_SCORE_MARGIN_NUM 3u
#define NEOGEO_MEMVIEW_FOLLOW_SCORE_MARGIN_DEN 2u
#define NEOGEO_MEMVIEW_FOLLOW_MIN_CHANGED_TILES 8u
#define NEOGEO_MEMVIEW_FOLLOW_MIN_ACTIVE_TILES 16u
#define NEOGEO_MEMVIEW_FOLLOW_MIN_SCORE 256u
#define NEOGEO_MEMVIEW_FOLLOW_STABLE_FRAMES 3u

typedef enum neogeo_memview_mode {
    neogeo_memview_mode_ram = 0,
    neogeo_memview_mode_crom = 1
} neogeo_memview_mode_t;

typedef struct neogeo_memview_overview_range {
    uint32_t baseAddr;
    uint32_t sizeBytes;
} neogeo_memview_overview_range_t;

typedef struct neogeo_memview_state neogeo_memview_state_t;

typedef struct neogeo_memview_overlay_body_state {
    neogeo_memview_state_t *ui;
} neogeo_memview_overlay_body_state_t;

typedef struct neogeo_memview_toolbar_item_state {
    e9ui_component_t *child;
    int widthPx;
} neogeo_memview_toolbar_item_state_t;

typedef struct neogeo_memview_toolbar_wrap_state {
    int padPx;
    int gapPx;
} neogeo_memview_toolbar_wrap_state_t;

typedef struct neogeo_memview_crom_profile_accum {
    uint64_t frames;
    double overviewTotalMs;
    double overviewLiveMapMs;
    double overviewDrawMs;
    double overviewUploadMs;
    double cromTotalMs;
    double cromReadMs;
    double cromPaletteMapMs;
    double cromDrawMs;
    double cromUploadMs;
} neogeo_memview_crom_profile_accum_t;

typedef struct neogeo_memview_step_buttons_action_ctx {
    neogeo_memview_state_t *ui;
    e9ui_component_t *canvas;
} neogeo_memview_step_buttons_action_ctx_t;

struct neogeo_memview_state {
    e9ui_window_state_t windowState;
    e9ui_context_t ctx;
    e9ui_component_t *root;
    e9ui_component_t *canvas;
    e9ui_component_t *modeButtonRam;
    e9ui_component_t *modeButtonCrom;
    e9ui_component_t *addressBox;
    e9ui_component_t *widthBox;
    e9ui_component_t *widthSeek;
    e9ui_component_t *zoomSeek;
    e9ui_component_t *overviewZoomSeek;
    e9ui_component_t *overlayBodyHost;
    neogeo_memview_mode_t mode;
    uint32_t ramBaseAddr;
    uint32_t cromBaseAddr;
    uint32_t ramRowBytes;
    uint32_t cromTilesPerRow;
    int zoomLevel;
    int overviewZoomLevel;
    int showAddressColumn;
    int showOverviewColumn;
    int followActiveSprites;
    int modeHasSaved;
    int ramBaseAddrHasSaved;
    int cromBaseAddrHasSaved;
    int ramRowBytesHasSaved;
    int cromTilesPerRowHasSaved;
    int zoomHasSaved;
    int overviewZoomHasSaved;
    int showAddressColumnHasSaved;
    int showOverviewColumnHasSaved;
    int followActiveSpritesHasSaved;
    e9ui_step_buttons_state_t stepButtons;
    e9ui_scrollbar_state_t hScrollbar;
    int scrollX;
    int contentPixelWidth;
    SDL_Renderer *renderer;
    SDL_Texture *mainTexture;
    uint32_t *mainPixels;
    size_t mainPixelsCap;
    int mainTextureW;
    int mainTextureH;
    uint16_t *followPrevSpriteVram;
    size_t followPrevSpriteVramWords;
    uint32_t followPendingStartRow;
    uint32_t followPendingFrames;
    SDL_Texture *overviewTexture;
    uint32_t *overviewPixels;
    size_t overviewPixelsCap;
    int overviewTextureW;
    int overviewTextureH;
    SDL_Texture *overviewBackgroundTexture;
    uint32_t *overviewBackgroundPixels;
    size_t overviewBackgroundPixelsCap;
    int overviewBackgroundTextureW;
    int overviewBackgroundTextureH;
    uint32_t overviewWindowStartRow;
    uint32_t overviewWindowVisibleRows;
    uint32_t overviewBackgroundStartRow;
    uint32_t overviewBackgroundVisibleRows;
    uint32_t overviewBackgroundTilesPerRow;
    int overviewBackgroundMode;
    uint64_t overviewBackgroundContentToken;
    double lastOverviewTotalMs;
    double lastOverviewLiveMapMs;
    double lastOverviewDrawMs;
    double lastOverviewUploadMs;
    neogeo_memview_overview_range_t overviewRanges[2];
    int (*widthSeekDefaultHandleEvent)(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev);
    int (*zoomSeekDefaultHandleEvent)(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev);
    int (*overviewZoomSeekDefaultHandleEvent)(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev);
};

static neogeo_memview_state_t neogeo_memview_stateSingleton = {
    .windowState.winX = E9UI_WINDOW_COORD_UNSET,
    .windowState.winY = E9UI_WINDOW_COORD_UNSET,
    .windowState.openMinWidthNoSavedSizePx = 640,
    .windowState.openMinHeightNoSavedSizePx = 360,
    .windowState.openCenterWhenNoSaved = 1,
    .mode = neogeo_memview_mode_ram,
    .ramBaseAddr = NEOGEO_MEMVIEW_RAM_BASE_MIN,
    .cromBaseAddr = 0u,
    .ramRowBytes = NEOGEO_MEMVIEW_DEFAULT_RAM_ROW_BYTES,
    .cromTilesPerRow = NEOGEO_MEMVIEW_DEFAULT_CROM_TILES_PER_ROW,
    .zoomLevel = NEOGEO_MEMVIEW_ZOOM_DEFAULT,
    .overviewZoomLevel = NEOGEO_MEMVIEW_OVERVIEW_ZOOM_DEFAULT,
    .showAddressColumn = 1,
    .showOverviewColumn = 1,
    .followActiveSprites = 0
};

static const aux_window_ops_t neogeo_memview_auxWindowOps = {
    .setFocus = neogeo_memview_setMainWindowFocused,
    .render = neogeo_memview_render,
};

static uint64_t
neogeo_memview_perfNow(void)
{
    return (uint64_t)SDL_GetPerformanceCounter();
}

static double
neogeo_memview_perfElapsedMs(uint64_t start, uint64_t end)
{
    uint64_t freq = (uint64_t)SDL_GetPerformanceFrequency();

    if (freq == 0u || end < start) {
        return 0.0;
    }
    return (double)(end - start) * 1000.0 / (double)freq;
}

static void
neogeo_memview_profileAccumPrint(const neogeo_memview_crom_profile_accum_t *accum)
{
    if (!accum || accum->frames == 0u) {
        return;
    }
    printf("neogeo_memview crom profile avg over %llu frames: overview total=%.3fms livemap=%.3fms draw=%.3fms upload=%.3fms | main total=%.3fms read=%.3fms palmap=%.3fms draw=%.3fms upload=%.3fms\n",
           (unsigned long long)accum->frames,
           accum->overviewTotalMs / (double)accum->frames,
           accum->overviewLiveMapMs / (double)accum->frames,
           accum->overviewDrawMs / (double)accum->frames,
           accum->overviewUploadMs / (double)accum->frames,
           accum->cromTotalMs / (double)accum->frames,
           accum->cromReadMs / (double)accum->frames,
           accum->cromPaletteMapMs / (double)accum->frames,
           accum->cromDrawMs / (double)accum->frames,
           accum->cromUploadMs / (double)accum->frames);
}

static void
neogeo_memview_profileAccumRecord(double overviewTotalMs,
                                  double overviewLiveMapMs,
                                  double overviewDrawMs,
                                  double overviewUploadMs,
                                  double cromTotalMs,
                                  double cromReadMs,
                                  double cromPaletteMapMs,
                                  double cromDrawMs,
                                  double cromUploadMs)
{
    static neogeo_memview_crom_profile_accum_t accum;

    accum.frames++;
    accum.overviewTotalMs += overviewTotalMs;
    accum.overviewLiveMapMs += overviewLiveMapMs;
    accum.overviewDrawMs += overviewDrawMs;
    accum.overviewUploadMs += overviewUploadMs;
    accum.cromTotalMs += cromTotalMs;
    accum.cromReadMs += cromReadMs;
    accum.cromPaletteMapMs += cromPaletteMapMs;
    accum.cromDrawMs += cromDrawMs;
    accum.cromUploadMs += cromUploadMs;
    if ((accum.frames % 60u) == 0u) {
        neogeo_memview_profileAccumPrint(&accum);
        memset(&accum, 0, sizeof(accum));
    }
}

static int
neogeo_memview_stepButtonsGutterWidth(const e9ui_context_t *ctx, e9ui_component_t *self);

static int
neogeo_memview_canvasVisibleRows(const neogeo_memview_state_t *ui, const e9ui_rect_t *bounds);

static uint32_t
neogeo_memview_clampBaseForView(const neogeo_memview_state_t *ui, const e9ui_rect_t *bounds, uint32_t baseAddr);

static void
neogeo_memview_syncTextboxesFromState(neogeo_memview_state_t *ui);

static void
neogeo_memview_saveWindowState(neogeo_memview_state_t *ui);

static int
neogeo_memview_parseInt(const char *value, int *out)
{
    char *end = NULL;
    long parsed = 0;

    if (!value || !out) {
        return 0;
    }
    parsed = strtol(value, &end, 10);
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
neogeo_memview_parseU64SmartHex(const char *text, unsigned long long *outValue, char **outEnd)
{
    const char *cursor = NULL;
    int base = 0;
    char *end = NULL;

    if (!text || !outValue) {
        return 0;
    }
    cursor = text;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') {
        cursor++;
    }
    if (*cursor == '$') {
        cursor++;
        base = 16;
    } else if (!(cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X'))) {
        for (const char *scan = cursor; *scan; ++scan) {
            if (*scan == ' ' || *scan == '\t' || *scan == '\r' || *scan == '\n') {
                break;
            }
            if ((*scan >= 'a' && *scan <= 'f') || (*scan >= 'A' && *scan <= 'F')) {
                base = 16;
                break;
            }
        }
    }

    *outValue = strtoull(cursor, &end, base);
    if (!end || end == cursor) {
        return 0;
    }
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') {
        end++;
    }
    if (outEnd) {
        *outEnd = end;
    }
    return 1;
}

static int
neogeo_memview_measureToolbarTextWidth(const e9ui_context_t *ctx,
                                       TTF_Font *font,
                                       const char *text,
                                       int basePx,
                                       int fallbackPx)
{
    int textW = 0;
    int textH = 0;
    int width = 0;

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
neogeo_memview_measureToolbarTextboxWidth(TTF_Font *font,
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

static int
neogeo_memview_clampZoomLevel(int zoomLevel)
{
    if (zoomLevel < NEOGEO_MEMVIEW_ZOOM_MIN) {
        zoomLevel = NEOGEO_MEMVIEW_ZOOM_MIN;
    }
    if (zoomLevel > NEOGEO_MEMVIEW_ZOOM_MAX) {
        zoomLevel = NEOGEO_MEMVIEW_ZOOM_MAX;
    }
    return zoomLevel;
}

static int
neogeo_memview_clampOverviewZoomLevel(int zoomLevel)
{
    if (zoomLevel < NEOGEO_MEMVIEW_OVERVIEW_ZOOM_MIN) {
        zoomLevel = NEOGEO_MEMVIEW_OVERVIEW_ZOOM_MIN;
    }
    if (zoomLevel > NEOGEO_MEMVIEW_OVERVIEW_ZOOM_MAX) {
        zoomLevel = NEOGEO_MEMVIEW_OVERVIEW_ZOOM_MAX;
    }
    return zoomLevel;
}

static uint32_t
neogeo_memview_clampRamRowBytes(uint32_t rowBytes)
{
    if (rowBytes < 1u) {
        rowBytes = NEOGEO_MEMVIEW_DEFAULT_RAM_ROW_BYTES;
    }
    if (rowBytes > NEOGEO_MEMVIEW_MAX_RAM_ROW_BYTES) {
        rowBytes = NEOGEO_MEMVIEW_MAX_RAM_ROW_BYTES;
    }
    return rowBytes;
}

static uint32_t
neogeo_memview_clampCromTilesPerRow(uint32_t tilesPerRow)
{
    if (tilesPerRow < 1u) {
        tilesPerRow = NEOGEO_MEMVIEW_DEFAULT_CROM_TILES_PER_ROW;
    }
    if (tilesPerRow > NEOGEO_MEMVIEW_MAX_CROM_TILES_PER_ROW) {
        tilesPerRow = NEOGEO_MEMVIEW_MAX_CROM_TILES_PER_ROW;
    }
    return tilesPerRow;
}

static uint32_t
neogeo_memview_clampRamBaseAddr(uint32_t baseAddr)
{
    if (baseAddr < NEOGEO_MEMVIEW_RAM_BASE_MIN) {
        baseAddr = NEOGEO_MEMVIEW_RAM_BASE_MIN;
    }
    if (baseAddr > NEOGEO_MEMVIEW_RAM_BASE_MAX) {
        baseAddr = NEOGEO_MEMVIEW_RAM_BASE_MAX;
    }
    return baseAddr;
}

static uint32_t
neogeo_memview_cromSize(void)
{
    e9k_debug_rom_region_t crom = { 0 };

    if (!libretro_host_debugGetCRom(&crom)) {
        return 0u;
    }
    if (crom.size > 0xffffffffu) {
        return 0xffffffffu;
    }
    return (uint32_t)crom.size;
}

static uint32_t
neogeo_memview_clampCromBaseAddr(uint32_t baseAddr)
{
    uint32_t sizeBytes = neogeo_memview_cromSize();
    uint32_t maxBaseAddr = 0u;

    if (sizeBytes < NEOGEO_MEMVIEW_TILE_BYTES) {
        return 0u;
    }
    maxBaseAddr = sizeBytes - NEOGEO_MEMVIEW_TILE_BYTES;
    baseAddr &= ~(NEOGEO_MEMVIEW_TILE_BYTES - 1u);
    if (baseAddr > maxBaseAddr) {
        baseAddr = maxBaseAddr & ~(NEOGEO_MEMVIEW_TILE_BYTES - 1u);
    }
    return baseAddr;
}

static uint32_t
neogeo_memview_alignCromBaseAddrToRow(const neogeo_memview_state_t *ui, uint32_t baseAddr)
{
    uint32_t rowBytes = 0u;

    baseAddr = neogeo_memview_clampCromBaseAddr(baseAddr);
    if (!ui) {
        return baseAddr;
    }
    rowBytes = neogeo_memview_clampCromTilesPerRow(ui->cromTilesPerRow) * NEOGEO_MEMVIEW_TILE_BYTES;
    if (rowBytes == 0u) {
        return baseAddr;
    }
    baseAddr -= baseAddr % rowBytes;
    return neogeo_memview_clampCromBaseAddr(baseAddr);
}

static int
neogeo_memview_ramBitPx(const neogeo_memview_state_t *ui)
{
    int scaled = e9ui_scale_px(&ui->ctx, NEOGEO_MEMVIEW_RAM_BIT_PX);
    int zoomLevel = ui ? neogeo_memview_clampZoomLevel(ui->zoomLevel) : NEOGEO_MEMVIEW_ZOOM_DEFAULT;

    scaled = (scaled * zoomLevel) / 8;
    if (scaled < 1) {
        scaled = 1;
    }
    return scaled;
}

static int
neogeo_memview_ramRowPx(const neogeo_memview_state_t *ui)
{
    int scaled = e9ui_scale_px(&ui->ctx, NEOGEO_MEMVIEW_RAM_ROW_PX);
    int zoomLevel = ui ? neogeo_memview_clampZoomLevel(ui->zoomLevel) : NEOGEO_MEMVIEW_ZOOM_DEFAULT;

    scaled = (scaled * zoomLevel) / 8;
    if (scaled < 1) {
        scaled = 1;
    }
    return scaled;
}

static int
neogeo_memview_tilePixelPx(const neogeo_memview_state_t *ui)
{
    int bitPx = neogeo_memview_ramBitPx(ui);

    if (bitPx < 1) {
        bitPx = 1;
    }
    return bitPx;
}

static uint32_t
neogeo_memview_currentBaseAddr(const neogeo_memview_state_t *ui)
{
    if (!ui) {
        return 0u;
    }
    return ui->mode == neogeo_memview_mode_crom ? ui->cromBaseAddr : ui->ramBaseAddr;
}

static uint32_t
neogeo_memview_cromTotalRows(const neogeo_memview_state_t *ui)
{
    uint32_t cromTiles = neogeo_memview_cromSize() / NEOGEO_MEMVIEW_TILE_BYTES;
    uint32_t tilesPerRow = ui ? neogeo_memview_clampCromTilesPerRow(ui->cromTilesPerRow) : NEOGEO_MEMVIEW_DEFAULT_CROM_TILES_PER_ROW;

    if (cromTiles == 0u || tilesPerRow == 0u) {
        return 0u;
    }
    return (cromTiles + tilesPerRow - 1u) / tilesPerRow;
}

static void
neogeo_memview_cromOverviewWindow(neogeo_memview_state_t *ui,
                                  uint32_t *outStartRow,
                                  uint32_t *outVisibleRows)
{
    uint32_t totalRows = neogeo_memview_cromTotalRows(ui);
    uint32_t zoomLevel = (uint32_t)neogeo_memview_clampOverviewZoomLevel(ui ? ui->overviewZoomLevel : NEOGEO_MEMVIEW_OVERVIEW_ZOOM_DEFAULT);
    uint32_t visibleRows = 0u;
    uint32_t currentTile = 0u;
    uint32_t currentRow = 0u;
    uint32_t startRow = 0u;
    uint32_t marginRows = 0u;

    if (outStartRow) {
        *outStartRow = 0u;
    }
    if (outVisibleRows) {
        *outVisibleRows = totalRows;
    }
    if (!ui || totalRows == 0u) {
        return;
    }
    visibleRows = (totalRows + zoomLevel - 1u) / zoomLevel;
    if (visibleRows < 1u) {
        visibleRows = 1u;
    }
    if (visibleRows > totalRows) {
        visibleRows = totalRows;
    }
    currentTile = ui->cromBaseAddr / NEOGEO_MEMVIEW_TILE_BYTES;
    currentRow = currentTile / neogeo_memview_clampCromTilesPerRow(ui->cromTilesPerRow);
    marginRows = visibleRows / 8u;
    if (marginRows < 1u) {
        marginRows = 1u;
    }
    if (marginRows * 2u >= visibleRows) {
        marginRows = visibleRows > 1u ? (visibleRows - 1u) / 2u : 0u;
    }
    startRow = ui->overviewWindowVisibleRows == visibleRows ? ui->overviewWindowStartRow : 0u;
    if (startRow + visibleRows > totalRows) {
        startRow = totalRows - visibleRows;
    }
    if (currentRow < startRow + marginRows) {
        if (currentRow > marginRows) {
            startRow = currentRow - marginRows;
        } else {
            startRow = 0u;
        }
    } else if (currentRow >= startRow + visibleRows - marginRows) {
        startRow = currentRow + marginRows + 1u > visibleRows ? currentRow + marginRows + 1u - visibleRows : 0u;
    }
    if (startRow + visibleRows > totalRows) {
        startRow = totalRows - visibleRows;
    }
    ui->overviewWindowStartRow = startRow;
    ui->overviewWindowVisibleRows = visibleRows;
    if (outStartRow) {
        *outStartRow = startRow;
    }
    if (outVisibleRows) {
        *outVisibleRows = visibleRows;
    }
}

static uint32_t
neogeo_memview_currentRowBytes(const neogeo_memview_state_t *ui)
{
    if (!ui) {
        return 0u;
    }
    if (ui->mode == neogeo_memview_mode_crom) {
        return neogeo_memview_clampCromTilesPerRow(ui->cromTilesPerRow) * NEOGEO_MEMVIEW_TILE_BYTES;
    }
    return neogeo_memview_clampRamRowBytes(ui->ramRowBytes);
}

static void
neogeo_memview_initOverviewRanges(neogeo_memview_state_t *ui)
{
    uint32_t cromSize = 0u;

    if (!ui) {
        return;
    }
    memset(ui->overviewRanges, 0, sizeof(ui->overviewRanges));
    if (ui->mode == neogeo_memview_mode_crom) {
        cromSize = neogeo_memview_cromSize();
        if (cromSize > 0u) {
            ui->overviewRanges[0].baseAddr = 0u;
            ui->overviewRanges[0].sizeBytes = cromSize;
        }
        return;
    }
    ui->overviewRanges[0].baseAddr = NEOGEO_MEMVIEW_RAM_BASE_MIN;
    ui->overviewRanges[0].sizeBytes = NEOGEO_MEMVIEW_RAM_BASE_MAX - NEOGEO_MEMVIEW_RAM_BASE_MIN + 1u;
}

static void
neogeo_memview_followActiveCromWindow(neogeo_memview_state_t *ui, const e9ui_rect_t *bounds)
{
    e9k_debug_sprite_state_t spriteState;
    uint32_t cromTiles = 0u;
    uint32_t tilesPerRow = 0u;
    uint32_t totalRows = 0u;
    uint32_t maxStartRow = 0u;
    uint32_t visibleRows = 0u;
    uint32_t currentRow = 0u;
    uint32_t activeSum = 0u;
    uint32_t changedSum = 0u;
    uint32_t currentActiveSum = 0u;
    uint32_t currentChangedSum = 0u;
    uint32_t bestStartRow = 0u;
    uint32_t *rowActiveCounts = NULL;
    uint32_t *rowChangedCounts = NULL;
    const uint16_t *vram = NULL;
    const uint16_t *scb3 = NULL;
    int screenHeight = 0;
    size_t prevBytes = 0u;
    uint16_t *nextPrevVram = NULL;
    uint64_t currentScore = 0u;
    uint64_t bestScore = 0u;
    uint64_t thresholdScore = 0u;
    uint32_t bestActiveSum = 0u;
    uint32_t bestChangedSum = 0u;
    uint32_t bootstrapBestStartRow = 0u;
    uint32_t bootstrapBestActiveSum = 0u;
    int bestQualified = 0;

    if (!ui || !bounds || ui->mode != neogeo_memview_mode_crom || !ui->followActiveSprites) {
        return;
    }
    if (!libretro_host_debugGetSpriteState(&spriteState) || !spriteState.vram || spriteState.vram_words == 0u) {
        return;
    }
    cromTiles = neogeo_memview_cromSize() / NEOGEO_MEMVIEW_TILE_BYTES;
    tilesPerRow = neogeo_memview_clampCromTilesPerRow(ui->cromTilesPerRow);
    totalRows = neogeo_memview_cromTotalRows(ui);
    visibleRows = (uint32_t)neogeo_memview_canvasVisibleRows(ui, bounds);
    if (cromTiles == 0u || tilesPerRow == 0u || totalRows == 0u || visibleRows == 0u) {
        return;
    }
    if (visibleRows > totalRows) {
        visibleRows = totalRows;
    }
    maxStartRow = totalRows > visibleRows ? totalRows - visibleRows : 0u;
    currentRow = (ui->cromBaseAddr / NEOGEO_MEMVIEW_TILE_BYTES) / tilesPerRow;
    if (currentRow > maxStartRow) {
        currentRow = maxStartRow;
    }
    rowActiveCounts = (uint32_t *)alloc_calloc(totalRows, sizeof(*rowActiveCounts));
    rowChangedCounts = (uint32_t *)alloc_calloc(totalRows, sizeof(*rowChangedCounts));
    if (!rowActiveCounts || !rowChangedCounts) {
        alloc_free(rowActiveCounts);
        alloc_free(rowChangedCounts);
        return;
    }
    prevBytes = spriteState.vram_words * sizeof(*spriteState.vram);
    if (spriteState.vram_words != 0u) {
        nextPrevVram = (uint16_t *)realloc(ui->followPrevSpriteVram, prevBytes);
        if (!nextPrevVram) {
            alloc_free(rowActiveCounts);
            alloc_free(rowChangedCounts);
            return;
        }
        ui->followPrevSpriteVram = nextPrevVram;
    }
    vram = spriteState.vram;
    scb3 = vram + 0x8200u;
    screenHeight = spriteState.screen_h > 0 ? spriteState.screen_h : 256;
    for (unsigned i = 1u; i < 381u; ) {
        unsigned len = 1u;
        uint16_t scb3w = 0u;
        unsigned spriteRows = 0u;
        unsigned ypos = 0u;

        scb3w = scb3[i];
        if (scb3w & 0x40u) {
            ++i;
            continue;
        }
        while ((i + len) < 381u && (scb3[i + len] & 0x40u)) {
            ++len;
        }
        spriteRows = (unsigned)(scb3w & 0x3fu);
        ypos = (unsigned)((scb3w >> 7) & 0x01ffu);
        if (spriteRows != 0u && ypos != (unsigned)screenHeight) {
            if (spriteRows > 32u) {
                spriteRows = 32u;
            }
            for (unsigned chainIndex = 0u; chainIndex < len; ++chainIndex) {
                unsigned spriteIndex = i + chainIndex;

                for (unsigned row = 0u; row < spriteRows; ++row) {
                    unsigned evenWordOffset = (spriteIndex << 6) + (row << 1);
                    unsigned oddWordOffset = evenWordOffset + 1u;
                    uint32_t spriteTileNum = 0u;
                    uint32_t spriteRow = 0u;
                    int changed = 0;

                    if (oddWordOffset >= spriteState.vram_words) {
                        break;
                    }
                    spriteTileNum = (uint32_t)(vram[evenWordOffset] | ((vram[oddWordOffset] & 0x00f0u) << 12));
                    spriteTileNum %= cromTiles;
                    spriteRow = spriteTileNum / tilesPerRow;
                    if (spriteRow < totalRows) {
                        rowActiveCounts[spriteRow]++;
                        if (ui->followPrevSpriteVram &&
                            ui->followPrevSpriteVramWords == spriteState.vram_words &&
                            (ui->followPrevSpriteVram[evenWordOffset] != vram[evenWordOffset] ||
                             ui->followPrevSpriteVram[oddWordOffset] != vram[oddWordOffset])) {
                            changed = 1;
                        }
                        if (changed) {
                            rowChangedCounts[spriteRow]++;
                        }
                    }
                }
            }
        }
        i += len;
    }
    for (uint32_t row = currentRow; row < currentRow + visibleRows; ++row) {
        currentActiveSum += rowActiveCounts[row];
        currentChangedSum += rowChangedCounts[row];
    }
    for (uint32_t row = 0u; row < visibleRows; ++row) {
        activeSum += rowActiveCounts[row];
        changedSum += rowChangedCounts[row];
    }
    bestScore = (uint64_t)activeSum * (uint64_t)changedSum;
    bestActiveSum = activeSum;
    bestChangedSum = changedSum;
    bootstrapBestActiveSum = activeSum;
    currentScore = (uint64_t)currentActiveSum * (uint64_t)currentChangedSum;
    for (uint32_t startRow = 1u; startRow <= maxStartRow; ++startRow) {
        activeSum -= rowActiveCounts[startRow - 1u];
        activeSum += rowActiveCounts[startRow + visibleRows - 1u];
        changedSum -= rowChangedCounts[startRow - 1u];
        changedSum += rowChangedCounts[startRow + visibleRows - 1u];
        if (activeSum > bootstrapBestActiveSum) {
            bootstrapBestActiveSum = activeSum;
            bootstrapBestStartRow = startRow;
        }
        if ((uint64_t)activeSum * (uint64_t)changedSum > bestScore) {
            bestScore = (uint64_t)activeSum * (uint64_t)changedSum;
            bestActiveSum = activeSum;
            bestChangedSum = changedSum;
            bestStartRow = startRow;
        }
    }
    thresholdScore = (currentScore * (uint64_t)NEOGEO_MEMVIEW_FOLLOW_SCORE_MARGIN_NUM +
                      (uint64_t)NEOGEO_MEMVIEW_FOLLOW_SCORE_MARGIN_DEN - 1u) /
                     (uint64_t)NEOGEO_MEMVIEW_FOLLOW_SCORE_MARGIN_DEN;
    bestQualified = bestChangedSum >= NEOGEO_MEMVIEW_FOLLOW_MIN_CHANGED_TILES &&
                    bestActiveSum >= NEOGEO_MEMVIEW_FOLLOW_MIN_ACTIVE_TILES &&
                    bestScore >= NEOGEO_MEMVIEW_FOLLOW_MIN_SCORE &&
                    bestScore > thresholdScore &&
                    bestStartRow != currentRow;
    if (bestQualified) {
        if (ui->followPendingStartRow == bestStartRow) {
            ui->followPendingFrames++;
        } else {
            ui->followPendingStartRow = bestStartRow;
            ui->followPendingFrames = 1u;
        }
    } else {
        ui->followPendingStartRow = currentRow;
        ui->followPendingFrames = 0u;
    }
    if (ui->followPrevSpriteVram && ui->followPrevSpriteVramWords == spriteState.vram_words) {
        printf("neogeo_memview follow: current_row=%u best_row=%u current_active=%u current_changed=%u current_score=%llu best_active=%u best_changed=%u best_score=%llu threshold=%llu qualified=%d pending_row=%u pending_frames=%u\n",
               currentRow,
               bestStartRow,
               currentActiveSum,
               currentChangedSum,
               (unsigned long long)currentScore,
               bestActiveSum,
               bestChangedSum,
               (unsigned long long)bestScore,
               (unsigned long long)thresholdScore,
               bestQualified,
               ui->followPendingStartRow,
               ui->followPendingFrames);
    }
    if ((!ui->followPrevSpriteVramWords || ui->followPrevSpriteVramWords != spriteState.vram_words) &&
        bootstrapBestActiveSum > 0u && bootstrapBestStartRow != currentRow) {
        ui->cromBaseAddr = neogeo_memview_clampBaseForView(ui,
                                                           bounds,
                                                           bootstrapBestStartRow * tilesPerRow * NEOGEO_MEMVIEW_TILE_BYTES);
        ui->cromBaseAddrHasSaved = 1;
        ui->followPendingStartRow = bootstrapBestStartRow;
        ui->followPendingFrames = 0u;
        neogeo_memview_syncTextboxesFromState(ui);
        neogeo_memview_saveWindowState(ui);
    }
    if (ui->followPrevSpriteVram && ui->followPrevSpriteVramWords == spriteState.vram_words &&
        bestQualified && ui->followPendingFrames >= NEOGEO_MEMVIEW_FOLLOW_STABLE_FRAMES) {
        ui->cromBaseAddr = neogeo_memview_clampBaseForView(ui,
                                                           bounds,
                                                           bestStartRow * tilesPerRow * NEOGEO_MEMVIEW_TILE_BYTES);
        ui->cromBaseAddrHasSaved = 1;
        ui->followPendingStartRow = bestStartRow;
        ui->followPendingFrames = 0u;
        neogeo_memview_syncTextboxesFromState(ui);
        neogeo_memview_saveWindowState(ui);
    }
    if (ui->followPrevSpriteVram && spriteState.vram_words != 0u) {
        memcpy(ui->followPrevSpriteVram, spriteState.vram, prevBytes);
        ui->followPrevSpriteVramWords = spriteState.vram_words;
    }
    alloc_free(rowActiveCounts);
    alloc_free(rowChangedCounts);
}

static uint32_t
neogeo_memview_findInitialCromBaseAddr(const neogeo_memview_state_t *ui)
{
    e9k_debug_sprite_state_t spriteState;
    const uint16_t *vram = NULL;
    const uint16_t *scb3 = NULL;
    uint32_t cromTiles = 0u;
    uint32_t tilesPerRow = 0u;
    uint32_t totalRows = 0u;
    uint32_t *rowActiveCounts = NULL;
    uint32_t bestRow = 0u;
    uint32_t bestCount = 0u;
    int screenHeight = 0;
    uint32_t baseAddr = 0u;

    if (!ui) {
        return 0u;
    }
    if (!libretro_host_debugGetSpriteState(&spriteState) || !spriteState.vram || spriteState.vram_words == 0u) {
        return 0u;
    }
    cromTiles = neogeo_memview_cromSize() / NEOGEO_MEMVIEW_TILE_BYTES;
    tilesPerRow = neogeo_memview_clampCromTilesPerRow(ui->cromTilesPerRow);
    totalRows = neogeo_memview_cromTotalRows(ui);
    if (cromTiles == 0u || tilesPerRow == 0u || totalRows == 0u) {
        return 0u;
    }
    rowActiveCounts = (uint32_t *)alloc_calloc(totalRows, sizeof(*rowActiveCounts));
    if (!rowActiveCounts) {
        return 0u;
    }
    vram = spriteState.vram;
    scb3 = vram + 0x8200u;
    screenHeight = spriteState.screen_h > 0 ? spriteState.screen_h : 256;
    for (unsigned i = 1u; i < 381u; ) {
        unsigned len = 1u;
        uint16_t scb3w = scb3[i];
        unsigned spriteRows = 0u;
        unsigned ypos = 0u;

        if (scb3w & 0x40u) {
            ++i;
            continue;
        }
        while ((i + len) < 381u && (scb3[i + len] & 0x40u)) {
            ++len;
        }
        spriteRows = (unsigned)(scb3w & 0x3fu);
        ypos = (unsigned)((scb3w >> 7) & 0x01ffu);
        if (spriteRows != 0u && ypos != (unsigned)screenHeight) {
            if (spriteRows > 32u) {
                spriteRows = 32u;
            }
            for (unsigned chainIndex = 0u; chainIndex < len; ++chainIndex) {
                unsigned spriteIndex = i + chainIndex;

                for (unsigned row = 0u; row < spriteRows; ++row) {
                    unsigned evenWordOffset = (spriteIndex << 6) + (row << 1);
                    unsigned oddWordOffset = evenWordOffset + 1u;
                    uint32_t spriteTileNum = 0u;
                    uint32_t spriteRow = 0u;

                    if (oddWordOffset >= spriteState.vram_words) {
                        break;
                    }
                    spriteTileNum = (uint32_t)(vram[evenWordOffset] | ((vram[oddWordOffset] & 0x00f0u) << 12));
                    spriteTileNum %= cromTiles;
                    spriteRow = spriteTileNum / tilesPerRow;
                    if (spriteRow < totalRows) {
                        rowActiveCounts[spriteRow]++;
                        if (rowActiveCounts[spriteRow] > bestCount) {
                            bestCount = rowActiveCounts[spriteRow];
                            bestRow = spriteRow;
                        }
                    }
                }
            }
        }
        i += len;
    }
    baseAddr = bestCount > 0u ? bestRow * tilesPerRow * NEOGEO_MEMVIEW_TILE_BYTES : 0u;
    alloc_free(rowActiveCounts);
    return neogeo_memview_alignCromBaseAddrToRow(ui, baseAddr);
}

static int
neogeo_memview_overviewRangeCount(const neogeo_memview_state_t *ui)
{
    int count = 0;

    if (!ui) {
        return 0;
    }
    for (int i = 0; i < (int)(sizeof(ui->overviewRanges) / sizeof(ui->overviewRanges[0])); ++i) {
        if (ui->overviewRanges[i].sizeBytes != 0u) {
            count++;
        }
    }
    return count;
}

static int
neogeo_memview_canvasVisibleRows(const neogeo_memview_state_t *ui, const e9ui_rect_t *bounds)
{
    int usableHeight = 0;
    int rowPx = 1;

    if (!ui || !bounds) {
        return 1;
    }
    usableHeight = bounds->h - e9ui_scale_px(&ui->ctx, NEOGEO_MEMVIEW_TOP_PAD_PX) -
                   e9ui_scale_px(&ui->ctx, NEOGEO_MEMVIEW_BOTTOM_PAD_PX);
    if (usableHeight < 1) {
        usableHeight = 1;
    }
    if (ui->mode == neogeo_memview_mode_crom) {
        rowPx = NEOGEO_MEMVIEW_TILE_H * neogeo_memview_tilePixelPx(ui) +
                e9ui_scale_px(&ui->ctx, NEOGEO_MEMVIEW_TILE_GAP_PX);
    } else {
        rowPx = neogeo_memview_ramRowPx(ui);
    }
    if (rowPx < 1) {
        rowPx = 1;
    }
    usableHeight /= rowPx;
    if (usableHeight < 1) {
        usableHeight = 1;
    }
    return usableHeight;
}

static uint32_t
neogeo_memview_clampBaseForView(const neogeo_memview_state_t *ui, const e9ui_rect_t *bounds, uint32_t baseAddr)
{
    uint32_t rangeStart = 0u;
    uint32_t rangeSize = 0u;
    uint64_t viewBytes = 0u;
    uint64_t maxBase = 0u;

    if (!ui || !bounds) {
        return baseAddr;
    }
    if (ui->mode == neogeo_memview_mode_crom) {
        rangeStart = 0u;
        rangeSize = neogeo_memview_cromSize();
        if (rangeSize < NEOGEO_MEMVIEW_TILE_BYTES) {
            return 0u;
        }
    } else {
        rangeStart = NEOGEO_MEMVIEW_RAM_BASE_MIN;
        rangeSize = NEOGEO_MEMVIEW_RAM_BASE_MAX - NEOGEO_MEMVIEW_RAM_BASE_MIN + 1u;
    }

    viewBytes = (uint64_t)neogeo_memview_canvasVisibleRows(ui, bounds) *
                (uint64_t)neogeo_memview_currentRowBytes(ui);
    if (viewBytes >= rangeSize) {
        maxBase = rangeStart;
    } else {
        maxBase = (uint64_t)rangeStart + (uint64_t)rangeSize - viewBytes;
    }

    if (ui->mode == neogeo_memview_mode_crom) {
        baseAddr = neogeo_memview_alignCromBaseAddrToRow(ui, baseAddr);
    } else {
        baseAddr = neogeo_memview_clampRamBaseAddr(baseAddr);
    }
    if ((uint64_t)baseAddr > maxBase) {
        baseAddr = (uint32_t)maxBase;
    }
    if (ui->mode == neogeo_memview_mode_crom) {
        baseAddr = neogeo_memview_alignCromBaseAddrToRow(ui, baseAddr);
    }
    return baseAddr;
}

static void
neogeo_memview_syncTextboxesFromState(neogeo_memview_state_t *ui)
{
    char label[32];
    uint32_t baseAddr = 0u;
    uint32_t widthValue = 0u;

    if (!ui) {
        return;
    }
    baseAddr = neogeo_memview_currentBaseAddr(ui);
    widthValue = ui->mode == neogeo_memview_mode_crom ?
        neogeo_memview_clampCromTilesPerRow(ui->cromTilesPerRow) :
        neogeo_memview_clampRamRowBytes(ui->ramRowBytes);

    snprintf(label, sizeof(label), "0x%06X", (unsigned)(baseAddr & 0x00ffffffu));
    if (ui->addressBox) {
        e9ui_textbox_setText(ui->addressBox, label);
    }
    snprintf(label, sizeof(label), "%u", (unsigned)widthValue);
    if (ui->widthBox) {
        e9ui_textbox_setText(ui->widthBox, label);
    }
    if (ui->widthSeek) {
        float percent = 0.0f;
        if (ui->mode == neogeo_memview_mode_crom) {
            percent = (float)(widthValue - 1u) / (float)(NEOGEO_MEMVIEW_MAX_CROM_TILES_PER_ROW - 1u);
        } else {
            percent = (float)(widthValue - 1u) / (float)(NEOGEO_MEMVIEW_MAX_RAM_ROW_BYTES - 1u);
        }
        e9ui_seek_bar_setPercent(ui->widthSeek, percent);
    }
    if (ui->zoomSeek) {
        float percent = 0.0f;
        if (NEOGEO_MEMVIEW_ZOOM_MAX > NEOGEO_MEMVIEW_ZOOM_MIN) {
            percent = (float)(ui->zoomLevel - NEOGEO_MEMVIEW_ZOOM_MIN) /
                      (float)(NEOGEO_MEMVIEW_ZOOM_MAX - NEOGEO_MEMVIEW_ZOOM_MIN);
        }
        e9ui_seek_bar_setPercent(ui->zoomSeek, percent);
    }
    if (ui->overviewZoomSeek) {
        float percent = 0.0f;
        int zoomLevel = neogeo_memview_clampOverviewZoomLevel(ui->overviewZoomLevel);

        if (NEOGEO_MEMVIEW_OVERVIEW_ZOOM_MAX > NEOGEO_MEMVIEW_OVERVIEW_ZOOM_MIN) {
            percent = (float)(zoomLevel - NEOGEO_MEMVIEW_OVERVIEW_ZOOM_MIN) /
                      (float)(NEOGEO_MEMVIEW_OVERVIEW_ZOOM_MAX - NEOGEO_MEMVIEW_OVERVIEW_ZOOM_MIN);
        }
        e9ui_seek_bar_setPercent(ui->overviewZoomSeek, percent);
    }
}

static void
neogeo_memview_updateModeButtons(neogeo_memview_state_t *ui)
{
    const e9k_theme_button_t *activeTheme = e9ui_theme_button_preset_profile_active();

    if (!ui) {
        return;
    }
    if (ui->modeButtonRam) {
        if (ui->mode == neogeo_memview_mode_ram) {
            e9ui_button_setTheme(ui->modeButtonRam, activeTheme);
        } else {
            e9ui_button_setTheme(ui->modeButtonRam, NULL);
        }
    }
    if (ui->modeButtonCrom) {
        if (ui->mode == neogeo_memview_mode_crom) {
            e9ui_button_setTheme(ui->modeButtonCrom, activeTheme);
        } else {
            e9ui_button_setTheme(ui->modeButtonCrom, NULL);
        }
    }
}

static void
neogeo_memview_saveWindowState(neogeo_memview_state_t *ui)
{
    (void)ui;
    config_saveConfig();
}

static void
neogeo_memview_setView(neogeo_memview_state_t *ui, uint32_t baseAddr, uint32_t rowBytesLike, int resetScroll)
{
    if (!ui) {
        return;
    }
    if (ui->mode == neogeo_memview_mode_crom) {
        ui->cromTilesPerRow = neogeo_memview_clampCromTilesPerRow(rowBytesLike / NEOGEO_MEMVIEW_TILE_BYTES);
        ui->cromTilesPerRowHasSaved = 1;
        ui->cromBaseAddr = neogeo_memview_clampBaseForView(ui,
                                                           ui->canvas ? &ui->canvas->bounds : &(e9ui_rect_t){ 0, 0, 640, 360 },
                                                           baseAddr);
        ui->cromBaseAddrHasSaved = 1;
    } else {
        ui->ramRowBytes = neogeo_memview_clampRamRowBytes(rowBytesLike);
        ui->ramRowBytesHasSaved = 1;
        ui->ramBaseAddr = neogeo_memview_clampBaseForView(ui,
                                                          ui->canvas ? &ui->canvas->bounds : &(e9ui_rect_t){ 0, 0, 640, 360 },
                                                          baseAddr);
        ui->ramBaseAddrHasSaved = 1;
    }
    if (resetScroll) {
        ui->scrollX = 0;
    }
    neogeo_memview_syncTextboxesFromState(ui);
    neogeo_memview_saveWindowState(ui);
}

static int
neogeo_memview_readRange(const neogeo_memview_state_t *ui, uint32_t addr, void *out, size_t sizeBytes)
{
    e9k_debug_rom_region_t crom = { 0 };

    if (!ui || !out || sizeBytes == 0u) {
        return 0;
    }
    if (ui->mode == neogeo_memview_mode_crom) {
        if (!libretro_host_debugGetCRom(&crom) || !crom.data) {
            memset(out, 0, sizeBytes);
            return 0;
        }
        if ((uint64_t)addr + (uint64_t)sizeBytes > crom.size) {
            memset(out, 0, sizeBytes);
            if (addr >= crom.size) {
                return 0;
            }
            memcpy(out, crom.data + addr, (size_t)(crom.size - addr));
            return 1;
        }
        memcpy(out, crom.data + addr, sizeBytes);
        return 1;
    }
    return libretro_host_debugReadMemory(addr, out, sizeBytes) ? 1 : 0;
}

static uint32_t
neogeo_memview_argb(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t
neogeo_memview_amberColor(unsigned index)
{
    static const uint32_t palette[16] = {
        0xff110700u, 0xff211000u, 0xff311700u, 0xff422000u,
        0xff522900u, 0xff633100u, 0xff733a00u, 0xff844300u,
        0xff944c00u, 0xffa55500u, 0xffb65e00u, 0xffc66700u,
        0xffd77008u, 0xffe77e20u, 0xfff09144u, 0xffffbd73u
    };

    return palette[index & 15u];
}

static uint64_t
neogeo_memview_overviewContentToken(const neogeo_memview_state_t *ui)
{
    uint64_t hash = 1469598103934665603ull;

    if (!ui) {
        return 0u;
    }
    if (ui->mode == neogeo_memview_mode_crom) {
        e9k_debug_rom_region_t crom = { 0 };

        if (!libretro_host_debugGetCRom(&crom)) {
            return 0u;
        }
        hash ^= (uint64_t)(uintptr_t)crom.data;
        hash *= 1099511628211ull;
        hash ^= (uint64_t)crom.size;
        hash *= 1099511628211ull;
        return hash;
    } else {
        uint8_t chunk[256];

        for (uint32_t addr = NEOGEO_MEMVIEW_RAM_BASE_MIN; addr <= NEOGEO_MEMVIEW_RAM_BASE_MAX; addr += (uint32_t)sizeof(chunk)) {
            size_t toRead = sizeof(chunk);

            if (addr + toRead - 1u > NEOGEO_MEMVIEW_RAM_BASE_MAX) {
                toRead = (size_t)(NEOGEO_MEMVIEW_RAM_BASE_MAX - addr + 1u);
            }
            memset(chunk, 0, sizeof(chunk));
            (void)libretro_host_debugReadMemory(addr, chunk, toRead);
            for (size_t i = 0; i < toRead; ++i) {
                hash ^= (uint64_t)chunk[i];
                hash *= 1099511628211ull;
            }
            if (toRead < sizeof(chunk)) {
                break;
            }
        }
        return hash;
    }
}

static void
neogeo_memview_fillRect(uint32_t *pixels,
                        int pitch,
                        int width,
                        int height,
                        int x,
                        int y,
                        int w,
                        int h,
                        uint32_t color)
{
    int x0 = x;
    int y0 = y;
    int x1 = x + w;
    int y1 = y + h;

    if (!pixels || pitch <= 0 || width <= 0 || height <= 0 || w <= 0 || h <= 0) {
        return;
    }
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > width) {
        x1 = width;
    }
    if (y1 > height) {
        y1 = height;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    for (int yy = y0; yy < y1; ++yy) {
        uint32_t *row = pixels + (size_t)yy * (size_t)pitch + (size_t)x0;
        for (int xx = x0; xx < x1; ++xx) {
            row[xx - x0] = color;
        }
    }
}

static int
neogeo_memview_ensureTexture(SDL_Renderer *renderer,
                             SDL_Texture **texture,
                             uint32_t **pixels,
                             size_t *pixelsCap,
                             int *textureW,
                             int *textureH,
                             int width,
                             int height)
{
    uint32_t *newPixels = NULL;
    size_t neededPixels = 0u;

    if (!renderer || !texture || !pixels || !pixelsCap || !textureW || !textureH || width <= 0 || height <= 0) {
        return 0;
    }
    neededPixels = (size_t)width * (size_t)height;
    if (neededPixels > *pixelsCap) {
        newPixels = (uint32_t *)realloc(*pixels, neededPixels * sizeof(**pixels));
        if (!newPixels) {
            return 0;
        }
        *pixels = newPixels;
        *pixelsCap = neededPixels;
    }
    if (*texture && (*textureW != width || *textureH != height)) {
        SDL_DestroyTexture(*texture);
        *texture = NULL;
    }
    if (!*texture) {
        *texture = SDL_CreateTexture(renderer,
                                     SDL_PIXELFORMAT_ARGB8888,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     width,
                                     height);
        if (!*texture) {
            return 0;
        }
        SDL_SetTextureBlendMode(*texture, SDL_BLENDMODE_BLEND);
    }
    *textureW = width;
    *textureH = height;
    return 1;
}

static uint8_t
neogeo_memview_readCromPixel(const uint8_t *data, size_t sizeBytes, uint32_t tileBaseAddr, unsigned x, unsigned y)
{
    uint8_t value = 0u;
    uint32_t pixelBaseAddr = 0u;
    unsigned bit = 0u;
    unsigned v0 = 0u;
    unsigned v1 = 0u;
    unsigned v2 = 0u;
    unsigned v3 = 0u;

    if (!data || tileBaseAddr + NEOGEO_MEMVIEW_TILE_BYTES > sizeBytes || x >= 16u || y >= 16u) {
        return 0u;
    }
    pixelBaseAddr = tileBaseAddr + (((x & 0x08u) ^ 0x08u) << 3) + (y << 2);
    if (pixelBaseAddr + 3u >= sizeBytes) {
        return 0u;
    }
    bit = x & 0x07u;
    v0 = (unsigned)data[pixelBaseAddr + 0u] >> bit;
    v1 = (unsigned)data[pixelBaseAddr + 2u] >> bit;
    v2 = (unsigned)data[pixelBaseAddr + 1u] >> bit;
    v3 = (unsigned)data[pixelBaseAddr + 3u] >> bit;
    value |= (uint8_t)((v0 & 0x01u) << 0u);
    value |= (uint8_t)((v1 & 0x01u) << 1u);
    value |= (uint8_t)((v2 & 0x01u) << 2u);
    value |= (uint8_t)((v3 & 0x01u) << 3u);
    return value;
}

static void
neogeo_memview_buildVisibleTilePaletteMap(const uint32_t *visibleTileNums,
                                          size_t visibleTileCount,
                                          uint8_t *tilePaletteBanks,
                                          uint8_t *tileHasPalette)
{
    e9k_debug_sprite_state_t spriteState;
    const uint16_t *vram = NULL;
    uint32_t cromTiles = 0u;
    uint32_t visibleBaseTile = 0u;
    uint32_t visibleEndTile = 0u;
    int wraps = 0;

    if (!visibleTileNums || !tilePaletteBanks || !tileHasPalette || visibleTileCount == 0u) {
        return;
    }
    if (!libretro_host_debugGetSpriteState(&spriteState) || !spriteState.vram || spriteState.vram_words == 0u) {
        return;
    }
    cromTiles = neogeo_memview_cromSize() / NEOGEO_MEMVIEW_TILE_BYTES;
    if (cromTiles == 0u) {
        return;
    }
    visibleBaseTile = visibleTileNums[0];
    visibleEndTile = (uint32_t)((visibleBaseTile + (uint32_t)visibleTileCount) % cromTiles);
    wraps = visibleTileCount > (size_t)(cromTiles - visibleBaseTile) ? 1 : 0;
    vram = spriteState.vram;
    for (unsigned i = 0; i < 382u; ++i) {
        for (unsigned row = 0; row < 32u; ++row) {
            unsigned evenWordOffset = (i << 6) + (row << 1);
            unsigned oddWordOffset = evenWordOffset + 1u;
            uint32_t spriteTileNum = 0u;
            size_t tileIndex = 0u;
            uint8_t paletteBank = 0u;
            int inVisibleRange = 0;

            if (oddWordOffset >= spriteState.vram_words) {
                break;
            }
            spriteTileNum = (uint32_t)(vram[evenWordOffset] | ((vram[oddWordOffset] & 0x00f0u) << 12));
            spriteTileNum %= cromTiles;
            paletteBank = (uint8_t)((vram[oddWordOffset] >> 8) & 0x00ffu);

            if (!wraps) {
                if (spriteTileNum >= visibleBaseTile && spriteTileNum < visibleBaseTile + (uint32_t)visibleTileCount) {
                    tileIndex = (size_t)(spriteTileNum - visibleBaseTile);
                    inVisibleRange = 1;
                }
            } else {
                if (spriteTileNum >= visibleBaseTile) {
                    tileIndex = (size_t)(spriteTileNum - visibleBaseTile);
                    inVisibleRange = 1;
                } else if (spriteTileNum < visibleEndTile) {
                    tileIndex = (size_t)(cromTiles - visibleBaseTile) + (size_t)spriteTileNum;
                    inVisibleRange = 1;
                }
            }
            if (inVisibleRange && tileIndex < visibleTileCount && !tileHasPalette[tileIndex]) {
                tilePaletteBanks[tileIndex] = paletteBank;
                tileHasPalette[tileIndex] = 1u;
            }
        }
    }
}

static int
neogeo_memview_overviewLiveTilePos(neogeo_memview_state_t *ui,
                                   int contentWidth,
                                   int contentHeight,
                                   uint32_t spriteTileNum,
                                   int *outX,
                                   int *outY)
{
    uint32_t cromTiles = 0u;
    uint32_t tilesPerRow = 0u;
    uint32_t totalRows = 0u;
    uint32_t overviewStartRow = 0u;
    uint32_t overviewVisibleRows = 0u;
    uint32_t tileRow = 0u;
    uint32_t tileCol = 0u;
    uint64_t rowIndex = 0u;
    uint64_t colIndex = 0u;

    if (!ui || !outX || !outY || contentWidth <= 0 || contentHeight <= 0) {
        return 0;
    }
    cromTiles = neogeo_memview_cromSize() / NEOGEO_MEMVIEW_TILE_BYTES;
    if (cromTiles == 0u) {
        return 0;
    }
    tilesPerRow = neogeo_memview_clampCromTilesPerRow(ui->cromTilesPerRow);
    totalRows = neogeo_memview_cromTotalRows(ui);
    if (tilesPerRow == 0u || totalRows == 0u) {
        return 0;
    }
    neogeo_memview_cromOverviewWindow(ui, &overviewStartRow, &overviewVisibleRows);
    spriteTileNum %= cromTiles;
    tileRow = spriteTileNum / tilesPerRow;
    tileCol = spriteTileNum % tilesPerRow;
    if (tileRow < overviewStartRow || tileRow >= overviewStartRow + overviewVisibleRows) {
        return 0;
    }
    rowIndex = ((uint64_t)(tileRow - overviewStartRow) * (uint64_t)contentHeight) / (uint64_t)overviewVisibleRows;
    colIndex = ((uint64_t)tileCol * (uint64_t)contentWidth) / (uint64_t)tilesPerRow;
    if ((int)rowIndex >= contentHeight) {
        rowIndex = (uint64_t)(contentHeight - 1);
    }
    if ((int)colIndex >= contentWidth) {
        colIndex = (uint64_t)(contentWidth - 1);
    }
    *outX = (int)colIndex;
    *outY = (int)rowIndex;
    return 1;
}

static int
neogeo_memview_measureAddressGutterPx(const e9ui_context_t *ctx, TTF_Font *font)
{
    int textW = 0;
    int textH = 0;
    e9ui_context_t tempCtx = ctx ? *ctx : (e9ui ? e9ui->ctx : (e9ui_context_t){ 0 });
    int gutter = e9ui_scale_px(&tempCtx, 10);

    if (!font) {
        font = e9ui->theme.text.source ? e9ui->theme.text.source : (ctx ? ctx->font : NULL);
    }
    if (font && TTF_SizeUTF8(font, "00100000", &textW, &textH) == 0) {
        gutter += textW;
    } else {
        gutter += e9ui_scale_px(&tempCtx, 72);
    }
    return gutter;
}

static int
neogeo_memview_measureOverviewGutterPx(const e9ui_context_t *ctx)
{
    e9ui_context_t tempCtx = ctx ? *ctx : (e9ui ? e9ui->ctx : (e9ui_context_t){ 0 });
    return e9ui_scale_px(&tempCtx, NEOGEO_MEMVIEW_OVERVIEW_GUTTER_PX);
}

static int
neogeo_memview_leftGutterPx(const neogeo_memview_state_t *ui, const e9ui_context_t *ctx, TTF_Font *font)
{
    int left = 0;

    if (!ui) {
        return 0;
    }
    if (ui->showAddressColumn) {
        left += neogeo_memview_measureAddressGutterPx(ctx, font);
    }
    if (ui->showOverviewColumn && neogeo_memview_overviewRangeCount(ui) > 0) {
        if (left > 0) {
            left += e9ui_scale_px(ctx, NEOGEO_MEMVIEW_GUTTER_GAP_PX);
        }
        left += neogeo_memview_measureOverviewGutterPx(ctx);
    }
    if (left > 0) {
        left += e9ui_scale_px(ctx, NEOGEO_MEMVIEW_GUTTER_GAP_PX);
    }
    return left;
}

static e9ui_rect_t
neogeo_memview_overviewBounds(const neogeo_memview_state_t *ui, e9ui_component_t *self, const e9ui_context_t *ctx, TTF_Font *font)
{
    e9ui_rect_t bounds = { 0, 0, 0, 0 };
    int x = 0;

    if (!ui || !self || !ctx || !ui->showOverviewColumn || neogeo_memview_overviewRangeCount(ui) <= 0) {
        return bounds;
    }
    x = self->bounds.x;
    if (ui->showAddressColumn) {
        x += neogeo_memview_measureAddressGutterPx(ctx, font);
        x += e9ui_scale_px(ctx, NEOGEO_MEMVIEW_GUTTER_GAP_PX);
    }
    bounds.x = x;
    bounds.y = self->bounds.y + e9ui_scale_px(ctx, NEOGEO_MEMVIEW_TOP_PAD_PX);
    bounds.w = neogeo_memview_measureOverviewGutterPx(ctx);
    bounds.h = self->bounds.h - e9ui_scale_px(ctx, NEOGEO_MEMVIEW_TOP_PAD_PX) -
               e9ui_scale_px(ctx, NEOGEO_MEMVIEW_BOTTOM_PAD_PX);
    if (bounds.h < 1) {
        bounds.h = 1;
    }
    return bounds;
}

static e9ui_rect_t
neogeo_memview_overviewContentBounds(const e9ui_context_t *ctx, const e9ui_rect_t *overviewBounds)
{
    e9ui_rect_t bounds = { 0, 0, 0, 0 };
    int inset = 0;

    if (!ctx || !overviewBounds || overviewBounds->w <= 0 || overviewBounds->h <= 0) {
        return bounds;
    }
    inset = e9ui_scale_px(ctx, 4);
    bounds = *overviewBounds;
    bounds.x += inset;
    bounds.y += inset;
    bounds.w -= inset * 2;
    bounds.h -= inset * 2;
    if (bounds.w < 1) {
        bounds.w = 1;
    }
    if (bounds.h < 1) {
        bounds.h = 1;
    }
    return bounds;
}

static e9ui_rect_t
neogeo_memview_hscrollBounds(const neogeo_memview_state_t *ui, const e9ui_component_t *self)
{
    e9ui_rect_t bounds = { 0, 0, 0, 0 };
    int rightGutter = 0;

    if (!ui || !self) {
        return bounds;
    }
    rightGutter = neogeo_memview_stepButtonsGutterWidth(&ui->ctx, (e9ui_component_t *)self);
    bounds.x = self->bounds.x + neogeo_memview_leftGutterPx(ui, &ui->ctx, ui->ctx.font);
    bounds.y = self->bounds.y + self->bounds.h - e9ui_scale_px(&ui->ctx, 12);
    bounds.w = self->bounds.w - (bounds.x - self->bounds.x) - rightGutter;
    bounds.h = e9ui_scale_px(&ui->ctx, 12);
    if (bounds.w < 1) {
        bounds.w = 1;
    }
    return bounds;
}

static void
neogeo_memview_drawAddressLabel(const neogeo_memview_state_t *ui,
                                e9ui_context_t *ctx,
                                TTF_Font *font,
                                uint32_t addr,
                                int x,
                                int y)
{
    char label[16];
    SDL_Color color = { 232, 232, 236, 255 };

    (void)ui;
    if (!ctx || !font) {
        return;
    }
    snprintf(label, sizeof(label), "%08X", (unsigned)addr);
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
neogeo_memview_rebuildOverviewBackgroundTexture(neogeo_memview_state_t *ui, e9ui_context_t *ctx)
{
    e9ui_rect_t overviewBounds;
    e9ui_rect_t contentBounds;
    uint32_t sample[256];
    e9k_debug_rom_region_t crom = { 0 };
    uint64_t overviewStartTicks = neogeo_memview_perfNow();
    uint64_t phaseStartTicks = overviewStartTicks;
    double drawMs = 0.0;
    double uploadMs = 0.0;
    uint32_t cacheStartRow = 0u;
    uint32_t cacheVisibleRows = 0u;
    uint32_t cacheTilesPerRow = 0u;
    uint64_t cacheContentToken = 0u;

    if (!ui || !ctx || !ui->canvas) {
        return 0;
    }
    overviewBounds = neogeo_memview_overviewBounds(ui, ui->canvas, ctx, ctx->font);
    contentBounds = neogeo_memview_overviewContentBounds(ctx, &overviewBounds);
    if (contentBounds.w <= 0 || contentBounds.h <= 0) {
        return 0;
    }
    if (ui->mode == neogeo_memview_mode_crom) {
        cacheTilesPerRow = neogeo_memview_clampCromTilesPerRow(ui->cromTilesPerRow);
        neogeo_memview_cromOverviewWindow(ui, &cacheStartRow, &cacheVisibleRows);
    }
    cacheContentToken = neogeo_memview_overviewContentToken(ui);
    if (ui->overviewBackgroundTexture &&
        ui->overviewBackgroundTextureW == contentBounds.w &&
        ui->overviewBackgroundTextureH == contentBounds.h &&
        ui->overviewBackgroundMode == (int)ui->mode &&
        ui->overviewBackgroundStartRow == cacheStartRow &&
        ui->overviewBackgroundVisibleRows == cacheVisibleRows &&
        ui->overviewBackgroundTilesPerRow == cacheTilesPerRow &&
        ui->overviewBackgroundContentToken == cacheContentToken) {
        ui->lastOverviewTotalMs = 0.0;
        ui->lastOverviewLiveMapMs = 0.0;
        ui->lastOverviewDrawMs = 0.0;
        ui->lastOverviewUploadMs = 0.0;
        return 1;
    }
    if (!neogeo_memview_ensureTexture(ctx->renderer,
                                      &ui->overviewBackgroundTexture,
                                      &ui->overviewBackgroundPixels,
                                      &ui->overviewBackgroundPixelsCap,
                                      &ui->overviewBackgroundTextureW,
                                      &ui->overviewBackgroundTextureH,
                                      contentBounds.w,
                                      contentBounds.h)) {
        return 0;
    }

    memset(ui->overviewBackgroundPixels, 0, (size_t)contentBounds.w * (size_t)contentBounds.h * sizeof(*ui->overviewBackgroundPixels));
    if (ui->mode == neogeo_memview_mode_crom) {
        (void)libretro_host_debugGetCRom(&crom);
    }
    if (ui->mode == neogeo_memview_mode_crom) {
        uint32_t cromTiles = neogeo_memview_cromSize() / NEOGEO_MEMVIEW_TILE_BYTES;
        uint32_t tilesPerRow = neogeo_memview_clampCromTilesPerRow(ui->cromTilesPerRow);
        uint32_t totalRows = neogeo_memview_cromTotalRows(ui);
        uint32_t overviewStartRow = 0u;
        uint32_t overviewVisibleRows = totalRows;

        if (crom.data && crom.size >= NEOGEO_MEMVIEW_TILE_BYTES && cromTiles > 0u && tilesPerRow > 0u && totalRows > 0u) {
            neogeo_memview_cromOverviewWindow(ui, &overviewStartRow, &overviewVisibleRows);
            for (uint32_t tileRow = overviewStartRow; tileRow < overviewStartRow + overviewVisibleRows; ++tileRow) {
                uint32_t localRow = tileRow - overviewStartRow;
                int y0 = (int)(((uint64_t)localRow * (uint64_t)contentBounds.h) / (uint64_t)overviewVisibleRows);
                int y1 = (int)(((uint64_t)(localRow + 1u) * (uint64_t)contentBounds.h) / (uint64_t)overviewVisibleRows);

                if (y1 <= y0) {
                    y1 = y0 + 1;
                }
                if (y0 >= contentBounds.h) {
                    break;
                }
                if (y1 > contentBounds.h) {
                    y1 = contentBounds.h;
                }
                for (uint32_t tileCol = 0; tileCol < tilesPerRow; ++tileCol) {
                    uint32_t tileNum = tileRow * tilesPerRow + tileCol;
                    uint32_t tileBase = 0u;
                    uint64_t pixelSum = 0u;
                    uint64_t pixelCount = 0u;
                    uint32_t brightness = 0u;
                    int x0 = (int)(((uint64_t)tileCol * (uint64_t)contentBounds.w) / (uint64_t)tilesPerRow);
                    int x1 = (int)(((uint64_t)(tileCol + 1u) * (uint64_t)contentBounds.w) / (uint64_t)tilesPerRow);
                    uint32_t color = 0u;

                    if (tileNum >= cromTiles) {
                        continue;
                    }
                    if (x1 <= x0) {
                        x1 = x0 + 1;
                    }
                    if (x0 >= contentBounds.w) {
                        continue;
                    }
                    if (x1 > contentBounds.w) {
                        x1 = contentBounds.w;
                    }
                    tileBase = tileNum * NEOGEO_MEMVIEW_TILE_BYTES;
                    for (unsigned py = 0; py < NEOGEO_MEMVIEW_TILE_H; py += 2u) {
                        for (unsigned px = 0; px < NEOGEO_MEMVIEW_TILE_W; px += 2u) {
                            pixelSum += neogeo_memview_readCromPixel(crom.data, crom.size, tileBase, px, py);
                            ++pixelCount;
                        }
                    }
                    if (pixelCount > 0u) {
                        brightness = (uint32_t)(pixelSum / pixelCount);
                    }
                    if (brightness > 15u) {
                        brightness = 15u;
                    }
                    color = neogeo_memview_amberColor(brightness);
                    for (int yy = y0; yy < y1; ++yy) {
                        for (int xx = x0; xx < x1; ++xx) {
                            ui->overviewBackgroundPixels[(size_t)yy * (size_t)contentBounds.w + (size_t)xx] = color;
                        }
                    }
                }
            }
            drawMs = neogeo_memview_perfElapsedMs(phaseStartTicks, neogeo_memview_perfNow());
            phaseStartTicks = neogeo_memview_perfNow();
            SDL_UpdateTexture(ui->overviewBackgroundTexture,
                              NULL,
                              ui->overviewBackgroundPixels,
                              contentBounds.w * (int)sizeof(*ui->overviewBackgroundPixels));
            uploadMs = neogeo_memview_perfElapsedMs(phaseStartTicks, neogeo_memview_perfNow());
            ui->lastOverviewTotalMs = neogeo_memview_perfElapsedMs(overviewStartTicks, neogeo_memview_perfNow());
            ui->lastOverviewLiveMapMs = 0.0;
            ui->lastOverviewDrawMs = drawMs;
            ui->lastOverviewUploadMs = uploadMs;
            ui->overviewBackgroundMode = (int)ui->mode;
            ui->overviewBackgroundStartRow = cacheStartRow;
            ui->overviewBackgroundVisibleRows = cacheVisibleRows;
            ui->overviewBackgroundTilesPerRow = cacheTilesPerRow;
            ui->overviewBackgroundContentToken = cacheContentToken;
            return 1;
        }
    }

    for (int yy = 0; yy < contentBounds.h; ++yy) {
        neogeo_memview_overview_range_t *range = &ui->overviewRanges[0];

        if (range->sizeBytes == 0u) {
            continue;
        }
        if (ui->mode != neogeo_memview_mode_crom) {
            uint32_t rowBytes = neogeo_memview_clampRamRowBytes(ui->ramRowBytes);
            uint32_t totalRows = rowBytes > 0u ? (range->sizeBytes + rowBytes - 1u) / rowBytes : 0u;
            uint32_t sourceRow = 0u;
            uint32_t rowBaseAddr = 0u;
            uint32_t readableBytes = rowBytes;
            const uint8_t *sampleBytes = (const uint8_t *)sample;
            const uint8_t offR = 33u;
            const uint8_t offG = 18u;
            const uint8_t offB = 0u;
            const uint8_t onR = 255u;
            const uint8_t onG = 189u;
            const uint8_t onB = 115u;

            if (rowBytes == 0u || totalRows == 0u) {
                continue;
            }
            sourceRow = (uint32_t)(((uint64_t)yy * (uint64_t)totalRows) / (uint64_t)contentBounds.h);
            if (sourceRow >= totalRows) {
                sourceRow = totalRows - 1u;
            }
            rowBaseAddr = range->baseAddr + sourceRow * rowBytes;
            if (rowBaseAddr < range->baseAddr || rowBaseAddr >= range->baseAddr + range->sizeBytes) {
                continue;
            }
            if (rowBaseAddr + readableBytes > range->baseAddr + range->sizeBytes) {
                readableBytes = range->baseAddr + range->sizeBytes - rowBaseAddr;
            }

            memset(sample, 0, sizeof(sample));
            (void)neogeo_memview_readRange(ui, rowBaseAddr, sample, readableBytes);
            for (int xx = 0; xx < contentBounds.w; ++xx) {
                uint32_t rowBits = rowBytes * 8u;
                uint32_t startBit = (uint32_t)(((uint64_t)xx * (uint64_t)rowBits) / (uint64_t)contentBounds.w);
                uint32_t endBit = (uint32_t)((((uint64_t)xx + 1u) * (uint64_t)rowBits) / (uint64_t)contentBounds.w);
                uint32_t setBits = 0u;
                uint32_t sampleBits = 0u;
                uint32_t intensity = 0u;
                uint8_t r = 0u;
                uint8_t g = 0u;
                uint8_t b = 0u;
                uint32_t color = 0u;

                if (endBit <= startBit) {
                    endBit = startBit + 1u;
                }
                if (endBit > rowBits) {
                    endBit = rowBits;
                }
                for (uint32_t bitIndex = startBit; bitIndex < endBit; ++bitIndex) {
                    uint32_t byteIndex = bitIndex >> 3;
                    uint32_t bitInByte = bitIndex & 7u;

                    if (byteIndex >= readableBytes) {
                        continue;
                    }
                    if (sampleBytes[byteIndex] & (uint8_t)(0x80u >> bitInByte)) {
                        setBits++;
                    }
                    sampleBits++;
                }
                if (sampleBits == 0u) {
                    sampleBits = 1u;
                }
                intensity = (setBits * 255u) / sampleBits;
                r = (uint8_t)(offR + (((uint32_t)(onR - offR) * intensity) / 255u));
                g = (uint8_t)(offG + (((uint32_t)(onG - offG) * intensity) / 255u));
                b = (uint8_t)(offB + (((uint32_t)(onB - offB) * intensity) / 255u));
                color = neogeo_memview_argb(r, g, b);
                ui->overviewBackgroundPixels[(size_t)yy * (size_t)contentBounds.w + (size_t)xx] = color;
            }
        }
    }
    drawMs = neogeo_memview_perfElapsedMs(phaseStartTicks, neogeo_memview_perfNow());
    phaseStartTicks = neogeo_memview_perfNow();

    SDL_UpdateTexture(ui->overviewBackgroundTexture,
                      NULL,
                      ui->overviewBackgroundPixels,
                      contentBounds.w * (int)sizeof(*ui->overviewBackgroundPixels));
    uploadMs = neogeo_memview_perfElapsedMs(phaseStartTicks, neogeo_memview_perfNow());
    ui->lastOverviewTotalMs = neogeo_memview_perfElapsedMs(overviewStartTicks, neogeo_memview_perfNow());
    ui->lastOverviewLiveMapMs = 0.0;
    ui->lastOverviewDrawMs = drawMs;
    ui->lastOverviewUploadMs = uploadMs;
    ui->overviewBackgroundMode = (int)ui->mode;
    ui->overviewBackgroundStartRow = cacheStartRow;
    ui->overviewBackgroundVisibleRows = cacheVisibleRows;
    ui->overviewBackgroundTilesPerRow = cacheTilesPerRow;
    ui->overviewBackgroundContentToken = cacheContentToken;
    return 1;
}

static void
neogeo_memview_renderOverviewSelection(neogeo_memview_state_t *ui,
                                       e9ui_context_t *ctx,
                                       const e9ui_rect_t *overviewBounds,
                                       int visibleRows)
{
    e9ui_rect_t contentBounds;
    uint32_t baseAddr = 0u;
    uint64_t viewBytes = 0u;
    neogeo_memview_overview_range_t *range = NULL;
    uint64_t startOff = 0u;
    uint64_t endOff = 0u;
    SDL_Rect selection;

    if (!ui || !ctx || !overviewBounds || overviewBounds->w <= 0 || overviewBounds->h <= 0 || visibleRows <= 0) {
        return;
    }
    range = &ui->overviewRanges[0];
    if (range->sizeBytes == 0u) {
        return;
    }
    contentBounds = neogeo_memview_overviewContentBounds(ctx, overviewBounds);
    baseAddr = neogeo_memview_currentBaseAddr(ui);
    viewBytes = (uint64_t)visibleRows * (uint64_t)neogeo_memview_currentRowBytes(ui);
    if (ui->mode == neogeo_memview_mode_crom) {
        uint32_t tilesPerRow = neogeo_memview_clampCromTilesPerRow(ui->cromTilesPerRow);
        uint32_t totalRows = neogeo_memview_cromTotalRows(ui);
        uint32_t overviewStartRow = 0u;
        uint32_t overviewVisibleRows = totalRows;
        uint32_t baseTile = 0u;
        uint32_t baseRow = 0u;
        uint32_t endRow = 0u;

        neogeo_memview_cromOverviewWindow(ui, &overviewStartRow, &overviewVisibleRows);
        if (tilesPerRow == 0u || overviewVisibleRows == 0u) {
            return;
        }
        baseTile = baseAddr / NEOGEO_MEMVIEW_TILE_BYTES;
        baseRow = baseTile / tilesPerRow;
        endRow = baseRow + (uint32_t)visibleRows;
        if (baseRow < overviewStartRow) {
            baseRow = overviewStartRow;
        }
        if (endRow > overviewStartRow + overviewVisibleRows) {
            endRow = overviewStartRow + overviewVisibleRows;
        }
        selection.x = contentBounds.x;
        selection.y = contentBounds.y + (int)((uint64_t)(baseRow - overviewStartRow) * (uint64_t)contentBounds.h / (uint64_t)overviewVisibleRows);
        selection.w = contentBounds.w;
        selection.h = (int)((uint64_t)(endRow - baseRow) * (uint64_t)contentBounds.h / (uint64_t)overviewVisibleRows);
        if (selection.h < 2) {
            selection.h = 2;
        }
        SDL_SetRenderDrawColor(ctx->renderer, 255, 255, 255, 220);
        SDL_RenderDrawRect(ctx->renderer, &selection);
        return;
    }
    startOff = (uint64_t)(baseAddr - range->baseAddr);
    endOff = startOff + viewBytes;
    if (endOff > range->sizeBytes) {
        endOff = range->sizeBytes;
    }
    selection.x = contentBounds.x;
    selection.y = contentBounds.y + (int)((startOff * (uint64_t)contentBounds.h) / (uint64_t)range->sizeBytes);
    selection.w = contentBounds.w;
    selection.h = (int)(((endOff - startOff) * (uint64_t)contentBounds.h) / (uint64_t)range->sizeBytes);
    if (selection.h < 2) {
        selection.h = 2;
    }
    SDL_SetRenderDrawColor(ctx->renderer, 255, 255, 255, 220);
    SDL_RenderDrawRect(ctx->renderer, &selection);
}

static double
neogeo_memview_rebuildOverviewTexture(neogeo_memview_state_t *ui,
                                      e9ui_context_t *ctx,
                                      const e9ui_rect_t *overviewBounds)
{
    e9ui_rect_t contentBounds;
    e9k_debug_sprite_state_t spriteState;
    const uint16_t *vram = NULL;
    uint64_t startTicks = neogeo_memview_perfNow();
    uint64_t phaseStartTicks = startTicks;
    double liveMapMs = 0.0;
    double uploadMs = 0.0;

    if (!ui || !ctx || !overviewBounds || ui->mode != neogeo_memview_mode_crom) {
        return 0.0;
    }
    contentBounds = neogeo_memview_overviewContentBounds(ctx, overviewBounds);
    if (contentBounds.w <= 0 || contentBounds.h <= 0) {
        return 0.0;
    }
    if (!ui->overviewBackgroundTexture || !ui->overviewBackgroundPixels) {
        return 0.0;
    }
    if (!neogeo_memview_ensureTexture(ctx->renderer,
                                      &ui->overviewTexture,
                                      &ui->overviewPixels,
                                      &ui->overviewPixelsCap,
                                      &ui->overviewTextureW,
                                      &ui->overviewTextureH,
                                      contentBounds.w,
                                      contentBounds.h)) {
        return 0.0;
    }
    memcpy(ui->overviewPixels,
           ui->overviewBackgroundPixels,
           (size_t)contentBounds.w * (size_t)contentBounds.h * sizeof(*ui->overviewPixels));
    if (!libretro_host_debugGetSpriteState(&spriteState) || !spriteState.vram || spriteState.vram_words == 0u) {
        return 0.0;
    }
    vram = spriteState.vram;
    for (unsigned i = 0; i < 382u; ++i) {
        for (unsigned row = 0; row < 32u; ++row) {
            unsigned evenWordOffset = (i << 6) + (row << 1);
            unsigned oddWordOffset = evenWordOffset + 1u;
            uint32_t spriteTileNum = 0u;
            int markerX = 0;
            int markerY = 0;

            if (oddWordOffset >= spriteState.vram_words) {
                break;
            }
            spriteTileNum = (uint32_t)(vram[evenWordOffset] | ((vram[oddWordOffset] & 0x00f0u) << 12));
            if (!neogeo_memview_overviewLiveTilePos(ui,
                                                    contentBounds.w,
                                                    contentBounds.h,
                                                    spriteTileNum,
                                                    &markerX,
                                                    &markerY)) {
                continue;
            }
            for (int yy = markerY - 1; yy <= markerY + 1; ++yy) {
                for (int xx = markerX - 1; xx <= markerX + 1; ++xx) {
                    if (xx >= 0 && xx < contentBounds.w && yy >= 0 && yy < contentBounds.h) {
                        ui->overviewPixels[(size_t)yy * (size_t)contentBounds.w + (size_t)xx] = neogeo_memview_argb(90, 220, 140);
                    }
                }
            }
        }
    }
    liveMapMs = neogeo_memview_perfElapsedMs(phaseStartTicks, neogeo_memview_perfNow());
    phaseStartTicks = neogeo_memview_perfNow();
    SDL_UpdateTexture(ui->overviewTexture,
                      NULL,
                      ui->overviewPixels,
                      contentBounds.w * (int)sizeof(*ui->overviewPixels));
    uploadMs = neogeo_memview_perfElapsedMs(phaseStartTicks, neogeo_memview_perfNow());
    ui->lastOverviewLiveMapMs = liveMapMs;
    ui->lastOverviewUploadMs = uploadMs;
    ui->lastOverviewTotalMs = neogeo_memview_perfElapsedMs(startTicks, neogeo_memview_perfNow());
    return ui->lastOverviewTotalMs;
}

static int
neogeo_memview_overviewNavigate(neogeo_memview_state_t *ui, e9ui_component_t *self, e9ui_context_t *ctx, int mx, int my)
{
    e9ui_rect_t overviewBounds;
    e9ui_rect_t contentBounds;
    neogeo_memview_overview_range_t *range = NULL;
    uint64_t pos = 0u;
    uint32_t baseAddr = 0u;

    if (!ui || !self || !ctx) {
        return 0;
    }
    overviewBounds = neogeo_memview_overviewBounds(ui, self, ctx, ctx->font);
    contentBounds = neogeo_memview_overviewContentBounds(ctx, &overviewBounds);
    if (mx < contentBounds.x || mx >= contentBounds.x + contentBounds.w ||
        my < contentBounds.y || my >= contentBounds.y + contentBounds.h) {
        return 0;
    }
    range = &ui->overviewRanges[0];
    if (range->sizeBytes == 0u) {
        return 0;
    }
    pos = ((uint64_t)(my - contentBounds.y) * (uint64_t)range->sizeBytes) / (uint64_t)contentBounds.h;
    baseAddr = range->baseAddr + (uint32_t)pos;
    if (ui->mode == neogeo_memview_mode_crom) {
        uint32_t tilesPerRow = neogeo_memview_clampCromTilesPerRow(ui->cromTilesPerRow);
        uint32_t totalRows = neogeo_memview_cromTotalRows(ui);
        uint32_t overviewStartRow = 0u;
        uint32_t overviewVisibleRows = totalRows;
        uint32_t targetRow = 0u;
        uint32_t targetCol = 0u;
        uint32_t targetTile = 0u;

        neogeo_memview_cromOverviewWindow(ui, &overviewStartRow, &overviewVisibleRows);
        if (tilesPerRow == 0u || overviewVisibleRows == 0u) {
            return 0;
        }
        targetRow = overviewStartRow + (uint32_t)(((uint64_t)(my - contentBounds.y) * (uint64_t)overviewVisibleRows) / (uint64_t)contentBounds.h);
        targetCol = 0u;
        if (targetRow >= totalRows) {
            targetRow = totalRows - 1u;
        }
        targetTile = targetRow * tilesPerRow + targetCol;
        baseAddr = neogeo_memview_clampCromBaseAddr(targetTile * NEOGEO_MEMVIEW_TILE_BYTES);
        neogeo_memview_setView(ui, baseAddr, ui->cromTilesPerRow * NEOGEO_MEMVIEW_TILE_BYTES, 1);
    } else {
        baseAddr = neogeo_memview_clampRamBaseAddr(baseAddr);
        neogeo_memview_setView(ui, baseAddr, ui->ramRowBytes, 1);
    }
    return 1;
}

static void
neogeo_memview_scrollRows(neogeo_memview_state_t *ui, const e9ui_rect_t *bounds, int rows)
{
    int64_t delta = 0;
    uint32_t baseAddr = 0u;

    if (!ui || !bounds || rows == 0) {
        return;
    }
    delta = (int64_t)rows * (int64_t)neogeo_memview_currentRowBytes(ui);
    baseAddr = neogeo_memview_currentBaseAddr(ui);
    if (delta < 0 && (uint64_t)(-delta) > baseAddr) {
        baseAddr = 0u;
    } else {
        baseAddr = (uint32_t)((int64_t)baseAddr + delta);
    }
    baseAddr = neogeo_memview_clampBaseForView(ui, bounds, baseAddr);
    if (ui->mode == neogeo_memview_mode_crom) {
        ui->cromBaseAddr = baseAddr;
        ui->cromBaseAddrHasSaved = 1;
    } else {
        ui->ramBaseAddr = baseAddr;
        ui->ramBaseAddrHasSaved = 1;
    }
    neogeo_memview_syncTextboxesFromState(ui);
    neogeo_memview_saveWindowState(ui);
}

static int
neogeo_memview_stepButtonsOnAction(void *user, e9ui_step_buttons_action_t action)
{
    neogeo_memview_step_buttons_action_ctx_t *actionCtx = (neogeo_memview_step_buttons_action_ctx_t *)user;
    int rows = 0;
    int pageRows = 0;

    if (!actionCtx || !actionCtx->ui || !actionCtx->canvas) {
        return 0;
    }
    pageRows = neogeo_memview_canvasVisibleRows(actionCtx->ui, &actionCtx->canvas->bounds) / 4;
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
    neogeo_memview_scrollRows(actionCtx->ui, &actionCtx->canvas->bounds, rows);
    return 1;
}

static int
neogeo_memview_toolbarItemPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    neogeo_memview_toolbar_item_state_t *state = NULL;

    (void)availW;
    if (!self || !ctx || !self->state) {
        return 0;
    }
    state = (neogeo_memview_toolbar_item_state_t *)self->state;
    if (!state->child || !state->child->preferredHeight) {
        return 0;
    }
    return state->child->preferredHeight(state->child, ctx, state->widthPx);
}

static void
neogeo_memview_toolbarItemLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    neogeo_memview_toolbar_item_state_t *state = NULL;

    if (!self || !ctx || !self->state) {
        return;
    }
    self->bounds = bounds;
    state = (neogeo_memview_toolbar_item_state_t *)self->state;
    if (state->child && state->child->layout) {
        state->child->layout(state->child, ctx, bounds);
    }
}

static void
neogeo_memview_toolbarItemRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    neogeo_memview_toolbar_item_state_t *state = NULL;

    if (!self || !ctx || !self->state) {
        return;
    }
    state = (neogeo_memview_toolbar_item_state_t *)self->state;
    if (state->child && state->child->render) {
        state->child->render(state->child, ctx);
    }
}

static void
neogeo_memview_toolbarItemDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    alloc_free(self->state);
    self->state = NULL;
}

static e9ui_component_t *
neogeo_memview_makeToolbarItem(e9ui_component_t *child, int widthPx)
{
    e9ui_component_t *item = NULL;
    neogeo_memview_toolbar_item_state_t *state = NULL;

    if (!child || widthPx <= 0) {
        return NULL;
    }
    item = (e9ui_component_t *)alloc_calloc(1, sizeof(*item));
    state = (neogeo_memview_toolbar_item_state_t *)alloc_calloc(1, sizeof(*state));
    if (!item || !state) {
        alloc_free(item);
        alloc_free(state);
        return NULL;
    }
    state->child = child;
    state->widthPx = widthPx;
    item->name = "neogeo_memview_toolbar_item";
    item->state = state;
    item->preferredHeight = neogeo_memview_toolbarItemPreferredHeight;
    item->layout = neogeo_memview_toolbarItemLayout;
    item->render = neogeo_memview_toolbarItemRender;
    item->dtor = neogeo_memview_toolbarItemDtor;
    e9ui_child_add(item, child, NULL);
    return item;
}

static int
neogeo_memview_toolbarItemWidth(const e9ui_component_t *item)
{
    const neogeo_memview_toolbar_item_state_t *state = NULL;

    if (!item || !item->state) {
        return 0;
    }
    state = (const neogeo_memview_toolbar_item_state_t *)item->state;
    return state->widthPx > 0 ? state->widthPx : 0;
}

static int
neogeo_memview_toolbarWrapPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    neogeo_memview_toolbar_wrap_state_t *state = NULL;
    int pad = 0;
    int gap = 0;
    int x = 0;
    int y = 0;
    int rowH = 0;
    int rightLimit = 0;

    if (!self || !ctx || !self->state) {
        return 0;
    }
    state = (neogeo_memview_toolbar_wrap_state_t *)self->state;
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
        childW = neogeo_memview_toolbarItemWidth(child);
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
neogeo_memview_toolbarWrapLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    neogeo_memview_toolbar_wrap_state_t *state = NULL;
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
    state = (neogeo_memview_toolbar_wrap_state_t *)self->state;
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
        childW = neogeo_memview_toolbarItemWidth(child);
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
neogeo_memview_toolbarWrapRender(e9ui_component_t *self, e9ui_context_t *ctx)
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
neogeo_memview_toolbarWrapDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    alloc_free(self->state);
    self->state = NULL;
}

static e9ui_component_t *
neogeo_memview_makeToolbarWrap(void)
{
    e9ui_component_t *comp = NULL;
    neogeo_memview_toolbar_wrap_state_t *state = NULL;

    comp = (e9ui_component_t *)alloc_calloc(1, sizeof(*comp));
    state = (neogeo_memview_toolbar_wrap_state_t *)alloc_calloc(1, sizeof(*state));
    if (!comp || !state) {
        alloc_free(comp);
        alloc_free(state);
        return NULL;
    }
    state->padPx = 0;
    state->gapPx = 12;
    comp->name = "neogeo_memview_toolbar_wrap";
    comp->state = state;
    comp->preferredHeight = neogeo_memview_toolbarWrapPreferredHeight;
    comp->layout = neogeo_memview_toolbarWrapLayout;
    comp->render = neogeo_memview_toolbarWrapRender;
    comp->dtor = neogeo_memview_toolbarWrapDtor;
    return comp;
}

static void
neogeo_memview_seekBarLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self) {
        return;
    }
    e9ui_seek_bar_layoutInParent(self, ctx, bounds);
    if (self->bounds.h < bounds.h) {
        self->bounds.y = bounds.y + (bounds.h - self->bounds.h) / 2;
    }
}

static void
neogeo_memview_initSeekBar(e9ui_component_t *seekBar,
                           int (*handleEvent)(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev),
                           void (*onChanged)(float percent, void *user),
                           void (*tooltip)(float percent, char *out, size_t cap, void *user),
                           void *user,
                           int (**outDefaultHandleEvent)(e9ui_component_t *, e9ui_context_t *, const e9ui_event_t *))
{
    if (!seekBar) {
        return;
    }
    if (outDefaultHandleEvent) {
        *outDefaultHandleEvent = seekBar->handleEvent;
    }
    seekBar->layout = neogeo_memview_seekBarLayout;
    seekBar->handleEvent = handleEvent;
    e9ui_seek_bar_setMargins(seekBar, 0, 0, 0);
    e9ui_seek_bar_setHeight(seekBar, 14);
    e9ui_seek_bar_setHoverMargin(seekBar, 6);
    e9ui_seek_bar_setCallback(seekBar, onChanged, user);
    e9ui_seek_bar_setTooltipCallback(seekBar, tooltip, user);
}

static void
neogeo_memview_switchMode(neogeo_memview_state_t *ui, neogeo_memview_mode_t mode)
{
    if (!ui || ui->mode == mode) {
        return;
    }
    ui->mode = mode;
    ui->modeHasSaved = 1;
    ui->scrollX = 0;
    ui->followPrevSpriteVramWords = 0u;
    neogeo_memview_initOverviewRanges(ui);
    neogeo_memview_syncTextboxesFromState(ui);
    neogeo_memview_updateModeButtons(ui);
    neogeo_memview_saveWindowState(ui);
}

static void
neogeo_memview_setRamMode(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    neogeo_memview_switchMode((neogeo_memview_state_t *)user, neogeo_memview_mode_ram);
}

static void
neogeo_memview_setCromMode(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    neogeo_memview_switchMode((neogeo_memview_state_t *)user, neogeo_memview_mode_crom);
}

static void
neogeo_memview_onShowAddressColumnChanged(e9ui_component_t *self, e9ui_context_t *ctx, int checked, void *user)
{
    neogeo_memview_state_t *ui = (neogeo_memview_state_t *)user;

    (void)self;
    (void)ctx;
    if (!ui) {
        return;
    }
    ui->showAddressColumn = checked ? 1 : 0;
    ui->showAddressColumnHasSaved = 1;
    neogeo_memview_saveWindowState(ui);
}

static void
neogeo_memview_onShowOverviewColumnChanged(e9ui_component_t *self, e9ui_context_t *ctx, int checked, void *user)
{
    neogeo_memview_state_t *ui = (neogeo_memview_state_t *)user;

    (void)self;
    (void)ctx;
    if (!ui) {
        return;
    }
    ui->showOverviewColumn = checked ? 1 : 0;
    ui->showOverviewColumnHasSaved = 1;
    neogeo_memview_saveWindowState(ui);
}

static void
neogeo_memview_onFollowActiveSpritesChanged(e9ui_component_t *self, e9ui_context_t *ctx, int checked, void *user)
{
    neogeo_memview_state_t *ui = (neogeo_memview_state_t *)user;

    (void)self;
    (void)ctx;
    if (!ui) {
        return;
    }
    ui->followActiveSprites = checked ? 1 : 0;
    ui->followActiveSpritesHasSaved = 1;
    ui->followPrevSpriteVramWords = 0u;
    neogeo_memview_saveWindowState(ui);
}

static void
neogeo_memview_onAddressSubmit(e9ui_context_t *ctx, void *user)
{
    neogeo_memview_state_t *ui = (neogeo_memview_state_t *)user;
    const char *text = NULL;
    unsigned long long parsed = 0u;
    char *end = NULL;

    (void)ctx;
    if (!ui || !ui->addressBox) {
        return;
    }
    text = e9ui_textbox_getText(ui->addressBox);
    if (!neogeo_memview_parseU64SmartHex(text, &parsed, &end) || !end || *end != '\0') {
        neogeo_memview_syncTextboxesFromState(ui);
        return;
    }
    if (ui->mode == neogeo_memview_mode_crom) {
        neogeo_memview_setView(ui, neogeo_memview_clampCromBaseAddr((uint32_t)parsed), ui->cromTilesPerRow * NEOGEO_MEMVIEW_TILE_BYTES, 1);
    } else {
        neogeo_memview_setView(ui, neogeo_memview_clampRamBaseAddr((uint32_t)parsed), ui->ramRowBytes, 1);
    }
}

static void
neogeo_memview_onWidthSubmit(e9ui_context_t *ctx, void *user)
{
    neogeo_memview_state_t *ui = (neogeo_memview_state_t *)user;
    const char *text = NULL;
    unsigned long long parsed = 0u;
    char *end = NULL;

    (void)ctx;
    if (!ui || !ui->widthBox) {
        return;
    }
    text = e9ui_textbox_getText(ui->widthBox);
    if (!neogeo_memview_parseU64SmartHex(text, &parsed, &end) || !end || *end != '\0' || parsed == 0u) {
        neogeo_memview_syncTextboxesFromState(ui);
        return;
    }
    if (ui->mode == neogeo_memview_mode_crom) {
        ui->cromTilesPerRow = neogeo_memview_clampCromTilesPerRow((uint32_t)parsed);
        ui->cromTilesPerRowHasSaved = 1;
        ui->cromBaseAddr = neogeo_memview_alignCromBaseAddrToRow(ui, ui->cromBaseAddr);
        ui->cromBaseAddrHasSaved = 1;
    } else {
        ui->ramRowBytes = neogeo_memview_clampRamRowBytes((uint32_t)parsed);
        ui->ramRowBytesHasSaved = 1;
    }
    ui->scrollX = 0;
    neogeo_memview_syncTextboxesFromState(ui);
    neogeo_memview_saveWindowState(ui);
}

static void
neogeo_memview_widthSeekTooltip(float percent, char *out, size_t cap, void *user)
{
    neogeo_memview_state_t *ui = (neogeo_memview_state_t *)user;
    uint32_t value = 1u;

    if (!out || cap == 0u || !ui) {
        return;
    }
    if (ui->mode == neogeo_memview_mode_crom) {
        value = 1u + (uint32_t)(percent * (float)(NEOGEO_MEMVIEW_MAX_CROM_TILES_PER_ROW - 1u) + 0.5f);
        value = neogeo_memview_clampCromTilesPerRow(value);
        snprintf(out, cap, "%u tiles", (unsigned)value);
    } else {
        value = 1u + (uint32_t)(percent * (float)(NEOGEO_MEMVIEW_MAX_RAM_ROW_BYTES - 1u) + 0.5f);
        value = neogeo_memview_clampRamRowBytes(value);
        snprintf(out, cap, "%u bytes", (unsigned)value);
    }
}

static void
neogeo_memview_zoomSeekTooltip(float percent, char *out, size_t cap, void *user)
{
    int zoomLevel = 0;

    (void)user;
    if (!out || cap == 0u) {
        return;
    }
    zoomLevel = NEOGEO_MEMVIEW_ZOOM_MIN +
        (int)(percent * (float)(NEOGEO_MEMVIEW_ZOOM_MAX - NEOGEO_MEMVIEW_ZOOM_MIN) + 0.5f);
    zoomLevel = neogeo_memview_clampZoomLevel(zoomLevel);
    snprintf(out, cap, "%dx", zoomLevel);
}

static void
neogeo_memview_overviewZoomSeekTooltip(float percent, char *out, size_t cap, void *user)
{
    int zoomLevel = NEOGEO_MEMVIEW_OVERVIEW_ZOOM_MIN +
                    (int)(percent * (float)(NEOGEO_MEMVIEW_OVERVIEW_ZOOM_MAX - NEOGEO_MEMVIEW_OVERVIEW_ZOOM_MIN) + 0.5f);

    (void)user;
    if (!out || cap == 0u) {
        return;
    }
    zoomLevel = neogeo_memview_clampOverviewZoomLevel(zoomLevel);
    snprintf(out, cap, "%dx", zoomLevel);
}

static void
neogeo_memview_onWidthSeekChanged(float percent, void *user)
{
    neogeo_memview_state_t *ui = (neogeo_memview_state_t *)user;
    uint32_t value = 1u;

    if (!ui) {
        return;
    }
    if (ui->mode == neogeo_memview_mode_crom) {
        value = 1u + (uint32_t)(percent * (float)(NEOGEO_MEMVIEW_MAX_CROM_TILES_PER_ROW - 1u) + 0.5f);
        ui->cromTilesPerRow = neogeo_memview_clampCromTilesPerRow(value);
        ui->cromTilesPerRowHasSaved = 1;
        ui->cromBaseAddr = neogeo_memview_alignCromBaseAddrToRow(ui, ui->cromBaseAddr);
        ui->cromBaseAddrHasSaved = 1;
    } else {
        value = 1u + (uint32_t)(percent * (float)(NEOGEO_MEMVIEW_MAX_RAM_ROW_BYTES - 1u) + 0.5f);
        ui->ramRowBytes = neogeo_memview_clampRamRowBytes(value);
        ui->ramRowBytesHasSaved = 1;
    }
    ui->scrollX = 0;
    neogeo_memview_syncTextboxesFromState(ui);
    neogeo_memview_saveWindowState(ui);
}

static void
neogeo_memview_onZoomSeekChanged(float percent, void *user)
{
    neogeo_memview_state_t *ui = (neogeo_memview_state_t *)user;
    int zoomLevel = 0;

    if (!ui) {
        return;
    }
    zoomLevel = NEOGEO_MEMVIEW_ZOOM_MIN +
        (int)(percent * (float)(NEOGEO_MEMVIEW_ZOOM_MAX - NEOGEO_MEMVIEW_ZOOM_MIN) + 0.5f);
    ui->zoomLevel = neogeo_memview_clampZoomLevel(zoomLevel);
    ui->zoomHasSaved = 1;
    ui->scrollX = 0;
    neogeo_memview_syncTextboxesFromState(ui);
    neogeo_memview_saveWindowState(ui);
}

static void
neogeo_memview_onOverviewZoomSeekChanged(float percent, void *user)
{
    neogeo_memview_state_t *ui = (neogeo_memview_state_t *)user;
    int zoomLevel = NEOGEO_MEMVIEW_OVERVIEW_ZOOM_MIN +
                    (int)(percent * (float)(NEOGEO_MEMVIEW_OVERVIEW_ZOOM_MAX - NEOGEO_MEMVIEW_OVERVIEW_ZOOM_MIN) + 0.5f);

    if (!ui) {
        return;
    }
    ui->overviewZoomLevel = neogeo_memview_clampOverviewZoomLevel(zoomLevel);
    ui->overviewZoomHasSaved = 1;
    neogeo_memview_saveWindowState(ui);
}

static int
neogeo_memview_handleWidthKey(neogeo_memview_state_t *ui, SDL_Keycode key)
{
    int nextValue = 0;

    if (!ui) {
        return 0;
    }
    nextValue = ui->mode == neogeo_memview_mode_crom ? (int)ui->cromTilesPerRow : (int)ui->ramRowBytes;
    if (key == SDLK_LEFT) {
        nextValue--;
    } else if (key == SDLK_RIGHT) {
        nextValue++;
    } else {
        return 0;
    }
    if (ui->mode == neogeo_memview_mode_crom) {
        ui->cromTilesPerRow = neogeo_memview_clampCromTilesPerRow((uint32_t)nextValue);
        ui->cromTilesPerRowHasSaved = 1;
        ui->cromBaseAddr = neogeo_memview_alignCromBaseAddrToRow(ui, ui->cromBaseAddr);
        ui->cromBaseAddrHasSaved = 1;
    } else {
        ui->ramRowBytes = neogeo_memview_clampRamRowBytes((uint32_t)nextValue);
        ui->ramRowBytesHasSaved = 1;
    }
    ui->scrollX = 0;
    neogeo_memview_syncTextboxesFromState(ui);
    neogeo_memview_saveWindowState(ui);
    return 1;
}

static int
neogeo_memview_widthSeekHandleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    neogeo_memview_state_t *ui = &neogeo_memview_stateSingleton;

    if (!self || !ctx || !ev) {
        return 0;
    }
    if (ev->type == SDL_KEYDOWN && e9ui_getFocus(ctx) == self) {
        if (neogeo_memview_handleWidthKey(ui, ev->key.keysym.sym)) {
            return 1;
        }
    }
    if (ui->widthSeekDefaultHandleEvent) {
        return ui->widthSeekDefaultHandleEvent(self, ctx, ev);
    }
    return 0;
}

static int
neogeo_memview_zoomSeekHandleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    neogeo_memview_state_t *ui = &neogeo_memview_stateSingleton;

    if (!self || !ctx || !ev) {
        return 0;
    }
    if (ev->type == SDL_KEYDOWN && e9ui_getFocus(ctx) == self) {
        if (ev->key.keysym.sym == SDLK_LEFT || ev->key.keysym.sym == SDLK_DOWN) {
            ui->zoomLevel = neogeo_memview_clampZoomLevel(ui->zoomLevel - 1);
            ui->zoomHasSaved = 1;
            ui->scrollX = 0;
            neogeo_memview_syncTextboxesFromState(ui);
            neogeo_memview_saveWindowState(ui);
            return 1;
        }
        if (ev->key.keysym.sym == SDLK_RIGHT || ev->key.keysym.sym == SDLK_UP) {
            ui->zoomLevel = neogeo_memview_clampZoomLevel(ui->zoomLevel + 1);
            ui->zoomHasSaved = 1;
            ui->scrollX = 0;
            neogeo_memview_syncTextboxesFromState(ui);
            neogeo_memview_saveWindowState(ui);
            return 1;
        }
    }
    if (ui->zoomSeekDefaultHandleEvent) {
        return ui->zoomSeekDefaultHandleEvent(self, ctx, ev);
    }
    return 0;
}

static int
neogeo_memview_overviewZoomSeekHandleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    neogeo_memview_state_t *ui = &neogeo_memview_stateSingleton;

    if (!self || !ctx || !ev || !ui) {
        return 0;
    }
    if (ev->type == SDL_KEYDOWN && ev->key.repeat == 0) {
        switch (ev->key.keysym.sym) {
        case SDLK_LEFT:
        case SDLK_DOWN:
            ui->overviewZoomLevel = neogeo_memview_clampOverviewZoomLevel(ui->overviewZoomLevel - 1);
            ui->overviewZoomHasSaved = 1;
            neogeo_memview_syncTextboxesFromState(ui);
            neogeo_memview_saveWindowState(ui);
            return 1;
        case SDLK_RIGHT:
        case SDLK_UP:
            ui->overviewZoomLevel = neogeo_memview_clampOverviewZoomLevel(ui->overviewZoomLevel + 1);
            ui->overviewZoomHasSaved = 1;
            neogeo_memview_syncTextboxesFromState(ui);
            neogeo_memview_saveWindowState(ui);
            return 1;
        default:
            break;
        }
    }
    if (ui->overviewZoomSeekDefaultHandleEvent) {
        return ui->overviewZoomSeekDefaultHandleEvent(self, ctx, ev);
    }
    return 0;
}

static void
neogeo_memview_canvasLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static void
neogeo_memview_canvasRenderRam(neogeo_memview_state_t *ui,
                               e9ui_context_t *ctx,
                               e9ui_component_t *self,
                               int bitViewW,
                               int rowAreaY,
                               int bitAreaX,
                               int leftGutterPx)
{
    int rowBytes = (int)neogeo_memview_clampRamRowBytes(ui->ramRowBytes);
    int bitPx = neogeo_memview_ramBitPx(ui);
    int rowPx = neogeo_memview_ramRowPx(ui);
    int visibleRows = neogeo_memview_canvasVisibleRows(ui, &self->bounds);
    int texW = rowBytes * 8 * bitPx + e9ui_scale_px(&ui->ctx, NEOGEO_MEMVIEW_RIGHT_PAD_PX);
    int texH = visibleRows * rowPx;
    SDL_Rect srcRect;
    SDL_Rect dstRect;
    uint8_t *rowData = NULL;
    size_t dataSize = (size_t)rowBytes * (size_t)visibleRows;
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    int labelStepRows = 8;
    int fontHeight = 16;

    if (font) {
        fontHeight = TTF_FontHeight(font);
        if (fontHeight > 0) {
            int minStep = (fontHeight + rowPx - 1) / rowPx;
            if (minStep > labelStepRows) {
                labelStepRows = minStep;
            }
        }
    }
    if (!neogeo_memview_ensureTexture(ctx->renderer,
                                      &ui->mainTexture,
                                      &ui->mainPixels,
                                      &ui->mainPixelsCap,
                                      &ui->mainTextureW,
                                      &ui->mainTextureH,
                                      texW,
                                      texH)) {
        return;
    }
    memset(ui->mainPixels, 0, (size_t)texW * (size_t)texH * sizeof(*ui->mainPixels));
    rowData = (uint8_t *)alloc_calloc(dataSize, 1);
    if (!rowData) {
        return;
    }
    (void)neogeo_memview_readRange(ui, ui->ramBaseAddr, rowData, dataSize);

    for (int row = 0; row < visibleRows; ++row) {
        uint32_t rowAddr = ui->ramBaseAddr + (uint32_t)row * (uint32_t)rowBytes;
        if (ui->showAddressColumn && (row % labelStepRows) == 0 && font) {
            neogeo_memview_drawAddressLabel(ui, ctx, font, rowAddr, self->bounds.x + 6, rowAreaY + row * rowPx - 1);
        }
        for (int byteIndex = 0; byteIndex < rowBytes; ++byteIndex) {
            uint8_t value = rowData[row * rowBytes + byteIndex];
            int byteX = byteIndex * 8 * bitPx;
            if ((byteIndex & 1) == 0) {
                neogeo_memview_fillRect(ui->mainPixels, texW, texW, texH, byteX, row * rowPx, 1, rowPx, neogeo_memview_argb(38, 42, 52));
            }
            for (int bit = 0; bit < 8; ++bit) {
                uint32_t color = (value & (uint8_t)(0x80u >> bit)) ? neogeo_memview_amberColor(15) : neogeo_memview_argb(33, 18, 0);
                neogeo_memview_fillRect(ui->mainPixels,
                                        texW,
                                        texW,
                                        texH,
                                        byteX + bit * bitPx,
                                        row * rowPx,
                                        bitPx,
                                        rowPx,
                                        color);
            }
        }
    }

    SDL_UpdateTexture(ui->mainTexture, NULL, ui->mainPixels, texW * (int)sizeof(*ui->mainPixels));
    ui->contentPixelWidth = texW;
    srcRect.x = ui->scrollX;
    srcRect.y = 0;
    srcRect.w = bitViewW < texW - ui->scrollX ? bitViewW : texW - ui->scrollX;
    srcRect.h = texH;
    if (srcRect.w > 0 && srcRect.h > 0) {
        dstRect.x = bitAreaX;
        dstRect.y = rowAreaY;
        dstRect.w = srcRect.w;
        dstRect.h = srcRect.h;
        SDL_RenderCopy(ctx->renderer, ui->mainTexture, &srcRect, &dstRect);
    }
    alloc_free(rowData);
    (void)leftGutterPx;
}

static void
neogeo_memview_canvasRenderCrom(neogeo_memview_state_t *ui,
                                e9ui_context_t *ctx,
                                e9ui_component_t *self,
                                int bitViewW,
                                int rowAreaY,
                                int bitAreaX)
{
    int tilesPerRow = (int)neogeo_memview_clampCromTilesPerRow(ui->cromTilesPerRow);
    int pixelPx = neogeo_memview_tilePixelPx(ui);
    int tileGapPx = e9ui_scale_px(&ui->ctx, NEOGEO_MEMVIEW_TILE_GAP_PX);
    int visibleRows = neogeo_memview_canvasVisibleRows(ui, &self->bounds);
    int tileStridePx = NEOGEO_MEMVIEW_TILE_W * pixelPx + tileGapPx;
    int rowStridePx = NEOGEO_MEMVIEW_TILE_H * pixelPx + tileGapPx;
    int texW = tilesPerRow * tileStridePx + e9ui_scale_px(&ui->ctx, NEOGEO_MEMVIEW_RIGHT_PAD_PX);
    int texH = visibleRows * rowStridePx;
    uint32_t cromTiles = neogeo_memview_cromSize() / NEOGEO_MEMVIEW_TILE_BYTES;
    size_t dataSize = (size_t)visibleRows * (size_t)tilesPerRow * NEOGEO_MEMVIEW_TILE_BYTES;
    uint8_t *tileData = NULL;
    SDL_Rect srcRect;
    SDL_Rect dstRect;
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    e9k_debug_palette_state_t paletteState;
    uint32_t *visibleTileNums = NULL;
    uint8_t *tilePaletteBanks = NULL;
    uint8_t *tileHasPalette = NULL;
    size_t visibleTileCount = 0u;
    int haveLivePalette = 0;
    uint64_t cromStartTicks = neogeo_memview_perfNow();
    uint64_t phaseStartTicks = cromStartTicks;
    double readMs = 0.0;
    double paletteMapMs = 0.0;
    double drawMs = 0.0;
    double uploadMs = 0.0;
    double totalMs = 0.0;

    if (!neogeo_memview_ensureTexture(ctx->renderer,
                                      &ui->mainTexture,
                                      &ui->mainPixels,
                                      &ui->mainPixelsCap,
                                      &ui->mainTextureW,
                                      &ui->mainTextureH,
                                      texW,
                                      texH)) {
        return;
    }
    memset(ui->mainPixels, 0, (size_t)texW * (size_t)texH * sizeof(*ui->mainPixels));
    tileData = (uint8_t *)alloc_calloc(dataSize, 1);
    if (!tileData) {
        return;
    }
    visibleTileCount = (size_t)visibleRows * (size_t)tilesPerRow;
    if (visibleTileCount > 0u) {
        visibleTileNums = (uint32_t *)alloc_calloc(visibleTileCount, sizeof(*visibleTileNums));
        tilePaletteBanks = (uint8_t *)alloc_calloc(visibleTileCount, sizeof(*tilePaletteBanks));
        tileHasPalette = (uint8_t *)alloc_calloc(visibleTileCount, sizeof(*tileHasPalette));
        if (!visibleTileNums || !tilePaletteBanks || !tileHasPalette) {
            alloc_free(tileData);
            alloc_free(visibleTileNums);
            alloc_free(tilePaletteBanks);
            alloc_free(tileHasPalette);
            return;
        }
    }
    memset(&paletteState, 0, sizeof(paletteState));
    haveLivePalette = libretro_host_debugGetGeoPaletteState(&paletteState) &&
                      paletteState.colors &&
                      paletteState.color_count > 0u;
    (void)neogeo_memview_readRange(ui, ui->cromBaseAddr, tileData, dataSize);
    readMs = neogeo_memview_perfElapsedMs(phaseStartTicks, neogeo_memview_perfNow());
    phaseStartTicks = neogeo_memview_perfNow();

    for (size_t tileIndex = 0; tileIndex < visibleTileCount; ++tileIndex) {
        uint32_t tileNum = (ui->cromBaseAddr / NEOGEO_MEMVIEW_TILE_BYTES) + (uint32_t)tileIndex;

        if (cromTiles != 0u) {
            tileNum %= cromTiles;
        }
        visibleTileNums[tileIndex] = tileNum;
    }
    if (haveLivePalette && visibleTileCount > 0u) {
        neogeo_memview_buildVisibleTilePaletteMap(visibleTileNums,
                                                  visibleTileCount,
                                                  tilePaletteBanks,
                                                  tileHasPalette);
    }
    paletteMapMs = neogeo_memview_perfElapsedMs(phaseStartTicks, neogeo_memview_perfNow());
    phaseStartTicks = neogeo_memview_perfNow();

    for (int row = 0; row < visibleRows; ++row) {
        uint32_t rowAddr = ui->cromBaseAddr + (uint32_t)row * (uint32_t)tilesPerRow * NEOGEO_MEMVIEW_TILE_BYTES;
        if (ui->showAddressColumn && font) {
            neogeo_memview_drawAddressLabel(ui, ctx, font, rowAddr, self->bounds.x + 6, rowAreaY + row * rowStridePx - 1);
        }
        for (int tile = 0; tile < tilesPerRow; ++tile) {
            size_t tileIndex = (size_t)row * (size_t)tilesPerRow + (size_t)tile;
            uint32_t tileBase = (uint32_t)((row * tilesPerRow + tile) * NEOGEO_MEMVIEW_TILE_BYTES);
            uint32_t tileNum = visibleTileNums ? visibleTileNums[tileIndex] : 0u;
            uint32_t paletteOffset = 0u;
            int tileX = tile * tileStridePx;
            int tileY = row * rowStridePx;

            if (haveLivePalette && tileHasPalette && tileHasPalette[tileIndex]) {
                paletteOffset = paletteState.active_bank * 4096u + ((uint32_t)tilePaletteBanks[tileIndex] << 4);
            }

            for (unsigned py = 0; py < NEOGEO_MEMVIEW_TILE_H; ++py) {
                for (unsigned px = 0; px < NEOGEO_MEMVIEW_TILE_W; ++px) {
                    uint8_t paletteIndex = neogeo_memview_readCromPixel(tileData, dataSize, tileBase, px, py);
                    uint32_t color = neogeo_memview_amberColor(paletteIndex);

                    if (paletteIndex != 0u &&
                        haveLivePalette &&
                        tileHasPalette &&
                        tileHasPalette[tileIndex] &&
                        (size_t)paletteOffset + (size_t)paletteIndex < paletteState.color_count) {
                        color = paletteState.colors[paletteOffset + paletteIndex];
                    }
                    neogeo_memview_fillRect(ui->mainPixels,
                                            texW,
                                            texW,
                                            texH,
                                            tileX + (int)px * pixelPx,
                                            tileY + (int)py * pixelPx,
                                            pixelPx,
                                            pixelPx,
                                            color);
                }
            }
            (void)tileNum;
        }
    }
    drawMs = neogeo_memview_perfElapsedMs(phaseStartTicks, neogeo_memview_perfNow());
    phaseStartTicks = neogeo_memview_perfNow();

    SDL_UpdateTexture(ui->mainTexture, NULL, ui->mainPixels, texW * (int)sizeof(*ui->mainPixels));
    uploadMs = neogeo_memview_perfElapsedMs(phaseStartTicks, neogeo_memview_perfNow());
    totalMs = neogeo_memview_perfElapsedMs(cromStartTicks, neogeo_memview_perfNow());
    neogeo_memview_profileAccumRecord(ui->lastOverviewTotalMs,
                                      ui->lastOverviewLiveMapMs,
                                      ui->lastOverviewDrawMs,
                                      ui->lastOverviewUploadMs,
                                      totalMs,
                                      readMs,
                                      paletteMapMs,
                                      drawMs,
                                      uploadMs);
    ui->contentPixelWidth = texW;
    srcRect.x = ui->scrollX;
    srcRect.y = 0;
    srcRect.w = bitViewW < texW - ui->scrollX ? bitViewW : texW - ui->scrollX;
    srcRect.h = texH;
    if (srcRect.w > 0 && srcRect.h > 0) {
        dstRect.x = bitAreaX;
        dstRect.y = rowAreaY;
        dstRect.w = srcRect.w;
        dstRect.h = srcRect.h;
        SDL_RenderCopy(ctx->renderer, ui->mainTexture, &srcRect, &dstRect);
    }
    alloc_free(tileData);
    alloc_free(visibleTileNums);
    alloc_free(tilePaletteBanks);
    alloc_free(tileHasPalette);
}

static void
neogeo_memview_canvasRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    neogeo_memview_state_t *ui = NULL;
    neogeo_memview_step_buttons_action_ctx_t actionCtx;
    SDL_Rect clip;
    SDL_Rect prevClip;
    SDL_Rect fillRect;
    e9ui_rect_t overviewBounds;
    e9ui_rect_t hscrollBounds;
    TTF_Font *font = NULL;
    int hadClip = 0;
    int leftGutterPx = 0;
    int rightGutter = 0;
    int bitAreaX = 0;
    int rowAreaY = 0;
    int bitViewW = 0;
    int visibleRows = 1;

    if (!self || !ctx || !ctx->renderer) {
        return;
    }
    ui = (neogeo_memview_state_t *)self->state;
    if (!ui) {
        return;
    }
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
    e9ui_step_buttons_tick(ctx, self->bounds, 0, 1, &ui->stepButtons, &actionCtx, neogeo_memview_stepButtonsOnAction);

    fillRect = clip;
    SDL_SetRenderDrawColor(ctx->renderer, 15, 17, 22, 255);
    SDL_RenderFillRect(ctx->renderer, &fillRect);

    font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    leftGutterPx = neogeo_memview_leftGutterPx(ui, ctx, font);
    rightGutter = neogeo_memview_stepButtonsGutterWidth(ctx, self);
    if (rightGutter < 0) {
        rightGutter = 0;
    }
    overviewBounds = neogeo_memview_overviewBounds(ui, self, ctx, font);
    bitAreaX = self->bounds.x + leftGutterPx;
    rowAreaY = self->bounds.y + e9ui_scale_px(&ui->ctx, NEOGEO_MEMVIEW_TOP_PAD_PX);
    bitViewW = self->bounds.w - leftGutterPx - rightGutter;
    if (bitViewW < 1) {
        bitViewW = 1;
    }
    visibleRows = neogeo_memview_canvasVisibleRows(ui, &self->bounds);
    if (ui->mode == neogeo_memview_mode_crom && ui->followActiveSprites) {
        neogeo_memview_followActiveCromWindow(ui, &self->bounds);
    }

    SDL_RenderSetClipRect(ctx->renderer, &clip);

    if (overviewBounds.w > 0 && overviewBounds.h > 0) {
        e9ui_rect_t overviewContentBounds = neogeo_memview_overviewContentBounds(ctx, &overviewBounds);

        if (neogeo_memview_rebuildOverviewBackgroundTexture(ui, ctx) && ui->overviewBackgroundTexture) {
            SDL_Rect dst = { overviewContentBounds.x, overviewContentBounds.y, overviewContentBounds.w, overviewContentBounds.h };
            if (ui->mode == neogeo_memview_mode_crom) {
                if (neogeo_memview_rebuildOverviewTexture(ui, ctx, &overviewBounds) && ui->overviewTexture) {
                    SDL_RenderCopy(ctx->renderer, ui->overviewTexture, NULL, &dst);
                } else {
                    SDL_RenderCopy(ctx->renderer, ui->overviewBackgroundTexture, NULL, &dst);
                }
            } else {
                SDL_RenderCopy(ctx->renderer, ui->overviewBackgroundTexture, NULL, &dst);
            }
            SDL_SetRenderDrawColor(ctx->renderer, 96, 96, 108, 255);
            SDL_RenderDrawRect(ctx->renderer, &(SDL_Rect){ overviewBounds.x, overviewBounds.y, overviewBounds.w, overviewBounds.h });
            neogeo_memview_renderOverviewSelection(ui, ctx, &overviewBounds, visibleRows);
        }
    }

    if (ui->mode == neogeo_memview_mode_crom) {
        neogeo_memview_canvasRenderCrom(ui, ctx, self, bitViewW, rowAreaY, bitAreaX);
    } else {
        neogeo_memview_canvasRenderRam(ui, ctx, self, bitViewW, rowAreaY, bitAreaX, leftGutterPx);
    }

    hscrollBounds = neogeo_memview_hscrollBounds(ui, self);
    if (hscrollBounds.w < 1) {
        hscrollBounds.w = 1;
    }
    {
        int scrollY = 0;
        e9ui_scrollbar_clamp(hscrollBounds.w, 1, ui->contentPixelWidth, 1, &ui->scrollX, &scrollY);
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
    e9ui_step_buttons_render(ctx, self->bounds, 0, 1, &ui->stepButtons);
}

static void
neogeo_memview_canvasDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->state = NULL;
}

static int
neogeo_memview_canvasHandleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    neogeo_memview_state_t *ui = NULL;
    neogeo_memview_step_buttons_action_ctx_t actionCtx;
    e9ui_rect_t hscrollBounds;
    int scrollX = 0;
    int scrollY = 0;
    int mx = 0;
    int my = 0;

    if (!self || !ctx || !ev) {
        return 0;
    }
    ui = (neogeo_memview_state_t *)self->state;
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
        if (neogeo_memview_overviewNavigate(ui, self, ctx, mx, my)) {
            return 1;
        }
    }

    hscrollBounds = neogeo_memview_hscrollBounds(ui, self);
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

    if (e9ui_step_buttons_handleEvent(ctx,
                                      ev,
                                      self->bounds,
                                      0,
                                      1,
                                      &ui->stepButtons,
                                      &actionCtx,
                                      neogeo_memview_stepButtonsOnAction)) {
        return 1;
    }

    if (ev->type == SDL_MOUSEWHEEL &&
        mx >= self->bounds.x &&
        mx < self->bounds.x + self->bounds.w &&
        my >= self->bounds.y &&
        my < self->bounds.y + self->bounds.h) {
        if (ev->wheel.x != 0) {
            ui->scrollX -= ev->wheel.x * e9ui_scale_px(ctx, 24);
            e9ui_scrollbar_clamp(hscrollBounds.w, 1, ui->contentPixelWidth, 1, &ui->scrollX, &scrollY);
        }
        if (ev->wheel.y != 0) {
            neogeo_memview_scrollRows(ui, &self->bounds, ev->wheel.y);
            return 1;
        }
        if (ev->wheel.x != 0) {
            return 1;
        }
    }

    if (ev->type == SDL_KEYDOWN && e9ui_getFocus(ctx) == self) {
        if (ev->key.keysym.sym == SDLK_PAGEUP) {
            neogeo_memview_scrollRows(ui, &self->bounds, -neogeo_memview_canvasVisibleRows(ui, &self->bounds));
            return 1;
        }
        if (ev->key.keysym.sym == SDLK_PAGEDOWN) {
            neogeo_memview_scrollRows(ui, &self->bounds, neogeo_memview_canvasVisibleRows(ui, &self->bounds));
            return 1;
        }
        if (ev->key.keysym.sym == SDLK_UP) {
            neogeo_memview_scrollRows(ui, &self->bounds, -1);
            return 1;
        }
        if (ev->key.keysym.sym == SDLK_DOWN) {
            neogeo_memview_scrollRows(ui, &self->bounds, 1);
            return 1;
        }
        if (ev->key.keysym.sym == SDLK_LEFT) {
            if ((ev->key.keysym.mod & KMOD_SHIFT) != 0) {
                return neogeo_memview_handleWidthKey(ui, ev->key.keysym.sym);
            }
            ui->scrollX -= e9ui_scale_px(ctx, 24);
            e9ui_scrollbar_clamp(hscrollBounds.w, 1, ui->contentPixelWidth, 1, &ui->scrollX, &scrollY);
            return 1;
        }
        if (ev->key.keysym.sym == SDLK_RIGHT) {
            if ((ev->key.keysym.mod & KMOD_SHIFT) != 0) {
                return neogeo_memview_handleWidthKey(ui, ev->key.keysym.sym);
            }
            ui->scrollX += e9ui_scale_px(ctx, 24);
            e9ui_scrollbar_clamp(hscrollBounds.w, 1, ui->contentPixelWidth, 1, &ui->scrollX, &scrollY);
            return 1;
        }
    }

    return 0;
}

static e9ui_component_t *
neogeo_memview_makeCanvas(neogeo_memview_state_t *ui)
{
    e9ui_component_t *canvas = NULL;

    if (!ui) {
        return NULL;
    }
    canvas = (e9ui_component_t *)alloc_calloc(1, sizeof(*canvas));
    if (!canvas) {
        return NULL;
    }
    canvas->name = "neogeo_memview_canvas";
    canvas->state = ui;
    canvas->preferredHeight = NULL;
    canvas->layout = neogeo_memview_canvasLayout;
    canvas->render = neogeo_memview_canvasRender;
    canvas->handleEvent = neogeo_memview_canvasHandleEvent;
    canvas->dtor = neogeo_memview_canvasDtor;
    canvas->focusable = 1;
    return canvas;
}

static void
neogeo_memview_clearUiRefs(neogeo_memview_state_t *ui)
{
    if (!ui) {
        return;
    }
    ui->canvas = NULL;
    ui->modeButtonRam = NULL;
    ui->modeButtonCrom = NULL;
    ui->addressBox = NULL;
    ui->widthBox = NULL;
    ui->widthSeek = NULL;
    ui->zoomSeek = NULL;
    ui->overviewZoomSeek = NULL;
    ui->overlayBodyHost = NULL;
}

static e9ui_component_t *
neogeo_memview_buildRoot(neogeo_memview_state_t *ui)
{
    e9ui_component_t *root = NULL;
    e9ui_component_t *toolbar = NULL;
    e9ui_component_t *toolbarBox = NULL;
    e9ui_component_t *groupGeneral = NULL;
    e9ui_component_t *groupAddress = NULL;
    e9ui_component_t *groupWidth = NULL;
    e9ui_component_t *groupZoom = NULL;
    e9ui_component_t *groupOverviewZoom = NULL;
    e9ui_component_t *groupGeneralItem = NULL;
    e9ui_component_t *groupAddressItem = NULL;
    e9ui_component_t *groupWidthItem = NULL;
    e9ui_component_t *groupZoomItem = NULL;
    e9ui_component_t *groupOverviewZoomItem = NULL;
    e9ui_component_t *showAddress = NULL;
    e9ui_component_t *showOverview = NULL;
    e9ui_component_t *followActive = NULL;
    e9ui_component_t *addressLabel = NULL;
    e9ui_component_t *widthLabel = NULL;
    e9ui_component_t *zoomLabel = NULL;
    TTF_Font *toolbarFont = NULL;
    int gapSmall = 0;
    int modeButtonW = 0;
    int checkboxAddrW = 0;
    int checkboxOverviewW = 0;
    int checkboxFollowW = 0;
    int labelAddrW = 0;
    int addrBoxW = 0;
    int labelWidthW = 0;
    int widthBoxW = 0;
    int widthSeekW = 0;
    int zoomLabelW = 0;
    int zoomSeekW = 0;
    int overviewZoomSeekW = 0;
    int textH = 0;
    int groupGeneralW = 0;
    int groupAddressW = 0;
    int groupWidthW = 0;
    int groupZoomW = 0;
    int groupOverviewZoomW = 0;

    if (!ui) {
        return NULL;
    }

    root = e9ui_stack_makeVertical();
    toolbar = neogeo_memview_makeToolbarWrap();
    ui->addressBox = e9ui_textbox_make(32, neogeo_memview_onAddressSubmit, NULL, ui);
    ui->widthBox = e9ui_textbox_make(16, neogeo_memview_onWidthSubmit, NULL, ui);
    ui->widthSeek = e9ui_seek_bar_make();
    ui->zoomSeek = e9ui_seek_bar_make();
    ui->overviewZoomSeek = e9ui_seek_bar_make();
    ui->modeButtonRam = e9ui_button_make("RAM", neogeo_memview_setRamMode, ui);
    ui->modeButtonCrom = e9ui_button_make("CROM", neogeo_memview_setCromMode, ui);
    ui->canvas = neogeo_memview_makeCanvas(ui);

    addressLabel = e9ui_text_make("Address");
    widthLabel = e9ui_text_make("Width");
    zoomLabel = e9ui_text_make("Zoom");
    e9ui_component_t *overviewZoomLabel = e9ui_text_make("Ov Zoom");
    showAddress = e9ui_checkbox_make("Addr", ui->showAddressColumn, neogeo_memview_onShowAddressColumnChanged, ui);
    showOverview = e9ui_checkbox_make("Overview", ui->showOverviewColumn, neogeo_memview_onShowOverviewColumnChanged, ui);
    followActive = e9ui_checkbox_make("Follow", ui->followActiveSprites, neogeo_memview_onFollowActiveSpritesChanged, ui);

    e9ui_textbox_setPlaceholder(ui->addressBox, "0x00100000");
    e9ui_textbox_setPlaceholder(ui->widthBox, "32");
    e9ui_textbox_setFocusBorderVisible(ui->addressBox, 0);
    e9ui_textbox_setFocusBorderVisible(ui->widthBox, 0);
    neogeo_memview_initSeekBar(ui->widthSeek,
                               neogeo_memview_widthSeekHandleEvent,
                               neogeo_memview_onWidthSeekChanged,
                               neogeo_memview_widthSeekTooltip,
                               ui,
                               &ui->widthSeekDefaultHandleEvent);
    neogeo_memview_initSeekBar(ui->zoomSeek,
                               neogeo_memview_zoomSeekHandleEvent,
                               neogeo_memview_onZoomSeekChanged,
                               neogeo_memview_zoomSeekTooltip,
                               ui,
                               &ui->zoomSeekDefaultHandleEvent);
    neogeo_memview_initSeekBar(ui->overviewZoomSeek,
                               neogeo_memview_overviewZoomSeekHandleEvent,
                               neogeo_memview_onOverviewZoomSeekChanged,
                               neogeo_memview_overviewZoomSeekTooltip,
                               ui,
                               &ui->overviewZoomSeekDefaultHandleEvent);

    e9ui_button_setMini(ui->modeButtonRam, 1);
    e9ui_button_setMini(ui->modeButtonCrom, 1);
    e9ui_button_setLargestLabel(ui->modeButtonRam, "CROM");
    e9ui_button_setLargestLabel(ui->modeButtonCrom, "CROM");

    toolbarFont = e9ui->theme.text.source ? e9ui->theme.text.source : ui->ctx.font;
    gapSmall = e9ui_scale_px(&ui->ctx, 6);
    modeButtonW = e9ui_scale_px(&ui->ctx, 72);
    e9ui_button_measure(ui->modeButtonRam, &ui->ctx, &modeButtonW, &textH);
    e9ui_checkbox_measure(showAddress, &ui->ctx, &checkboxAddrW, &textH);
    e9ui_checkbox_measure(showOverview, &ui->ctx, &checkboxOverviewW, &textH);
    e9ui_checkbox_measure(followActive, &ui->ctx, &checkboxFollowW, &textH);
    labelAddrW = neogeo_memview_measureToolbarTextWidth(&ui->ctx, toolbarFont, "Address", 8, 64);
    addrBoxW = neogeo_memview_measureToolbarTextboxWidth(toolbarFont, "0x00100000", 110);
    labelWidthW = neogeo_memview_measureToolbarTextWidth(&ui->ctx, toolbarFont, "Width", 8, 48);
    widthBoxW = neogeo_memview_measureToolbarTextboxWidth(toolbarFont, "256", 56);
    widthSeekW = e9ui_scale_px(&ui->ctx, 180);
    zoomLabelW = neogeo_memview_measureToolbarTextWidth(&ui->ctx, toolbarFont, "Zoom", 8, 40);
    zoomSeekW = e9ui_scale_px(&ui->ctx, 180);
    int overviewZoomLabelW = neogeo_memview_measureToolbarTextWidth(&ui->ctx, toolbarFont, "Ov Zoom", 8, 56);
    overviewZoomSeekW = e9ui_scale_px(&ui->ctx, 150);

    groupGeneral = e9ui_hstack_make();
    groupAddress = e9ui_hstack_make();
    groupWidth = e9ui_hstack_make();
    groupZoom = e9ui_hstack_make();
    groupOverviewZoom = e9ui_hstack_make();

    e9ui_hstack_addFixed(groupGeneral, ui->modeButtonRam, modeButtonW);
    e9ui_hstack_addFixed(groupGeneral, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupGeneral, ui->modeButtonCrom, modeButtonW);
    e9ui_hstack_addFixed(groupGeneral, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupGeneral, showAddress, checkboxAddrW);
    e9ui_hstack_addFixed(groupGeneral, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupGeneral, showOverview, checkboxOverviewW);
    e9ui_hstack_addFixed(groupGeneral, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupGeneral, followActive, checkboxFollowW);
    groupGeneralW = modeButtonW + gapSmall + modeButtonW + gapSmall + checkboxAddrW + gapSmall + checkboxOverviewW + gapSmall + checkboxFollowW;

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

    e9ui_hstack_addFixed(groupOverviewZoom, overviewZoomLabel, overviewZoomLabelW);
    e9ui_hstack_addFixed(groupOverviewZoom, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupOverviewZoom, ui->overviewZoomSeek, overviewZoomSeekW);
    groupOverviewZoomW = overviewZoomLabelW + gapSmall + overviewZoomSeekW;

    groupGeneralItem = neogeo_memview_makeToolbarItem(groupGeneral, groupGeneralW);
    groupAddressItem = neogeo_memview_makeToolbarItem(groupAddress, groupAddressW);
    groupWidthItem = neogeo_memview_makeToolbarItem(groupWidth, groupWidthW);
    groupZoomItem = neogeo_memview_makeToolbarItem(groupZoom, groupZoomW);
    groupOverviewZoomItem = neogeo_memview_makeToolbarItem(groupOverviewZoom, groupOverviewZoomW);

    e9ui_child_add(toolbar, groupGeneralItem, NULL);
    e9ui_child_add(toolbar, groupAddressItem, NULL);
    e9ui_child_add(toolbar, groupWidthItem, NULL);
    e9ui_child_add(toolbar, groupZoomItem, NULL);
    e9ui_child_add(toolbar, groupOverviewZoomItem, NULL);

    toolbarBox = e9ui_box_make(toolbar);
    e9ui_box_setPadding(toolbarBox, 8);
    e9ui_box_setBorder(toolbarBox, E9UI_BORDER_BOTTOM, (SDL_Color){ 70, 70, 70, 255 }, 1);

    e9ui_stack_addFixed(root, toolbarBox);
    e9ui_stack_addFlex(root, ui->canvas);

    neogeo_memview_syncTextboxesFromState(ui);
    neogeo_memview_updateModeButtons(ui);
    return root;
}

static e9ui_rect_t
neogeo_memview_windowDefaultRect(const e9ui_context_t *ctx)
{
    e9ui_rect_t rect;

    rect.w = e9ui_scale_px(ctx, 980);
    rect.h = e9ui_scale_px(ctx, 720);
    rect.x = (ctx->winW - rect.w) / 2;
    rect.y = (ctx->winH - rect.h) / 2;
    return rect;
}

static int
neogeo_memview_stepButtonsGutterWidth(const e9ui_context_t *ctx, e9ui_component_t *self)
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

static e9ui_window_backend_t
neogeo_memview_windowBackend(void)
{
    return e9ui_window_backend_overlay;
}

static int
neogeo_memview_overlayBodyPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)ctx;
    (void)availW;
    return 0;
}

static void
neogeo_memview_overlayBodyLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    neogeo_memview_overlay_body_state_t *state = NULL;
    neogeo_memview_state_t *ui = NULL;

    if (!self || !ctx || !self->state) {
        return;
    }
    self->bounds = bounds;
    state = (neogeo_memview_overlay_body_state_t *)self->state;
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
neogeo_memview_overlayBodyRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    neogeo_memview_overlay_body_state_t *state = NULL;
    neogeo_memview_state_t *ui = NULL;

    if (!self || !ctx || !self->state) {
        return;
    }
    state = (neogeo_memview_overlay_body_state_t *)self->state;
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
neogeo_memview_overlayBodyDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    alloc_free(self->state);
    self->state = NULL;
}

static e9ui_component_t *
neogeo_memview_makeOverlayBodyHost(neogeo_memview_state_t *ui)
{
    e9ui_component_t *host = NULL;
    neogeo_memview_overlay_body_state_t *state = NULL;

    if (!ui || !ui->root) {
        return NULL;
    }
    host = (e9ui_component_t *)alloc_calloc(1, sizeof(*host));
    state = (neogeo_memview_overlay_body_state_t *)alloc_calloc(1, sizeof(*state));
    if (!host || !state) {
        alloc_free(host);
        alloc_free(state);
        return NULL;
    }
    state->ui = ui;
    host->name = "neogeo_memview_overlay_body";
    host->state = state;
    host->preferredHeight = neogeo_memview_overlayBodyPreferredHeight;
    host->layout = neogeo_memview_overlayBodyLayout;
    host->render = neogeo_memview_overlayBodyRender;
    host->dtor = neogeo_memview_overlayBodyDtor;
    e9ui_child_add(host, ui->root, alloc_strdup("neogeo_memview_root"));
    return host;
}

static void
neogeo_memview_releaseRuntimeState(neogeo_memview_state_t *ui)
{
    if (!ui) {
        return;
    }
    if (ui->mainTexture) {
        SDL_DestroyTexture(ui->mainTexture);
        ui->mainTexture = NULL;
    }
    if (ui->overviewTexture) {
        SDL_DestroyTexture(ui->overviewTexture);
        ui->overviewTexture = NULL;
    }
    if (ui->overviewBackgroundTexture) {
        SDL_DestroyTexture(ui->overviewBackgroundTexture);
        ui->overviewBackgroundTexture = NULL;
    }
    alloc_free(ui->mainPixels);
    alloc_free(ui->overviewPixels);
    alloc_free(ui->overviewBackgroundPixels);
    alloc_free(ui->followPrevSpriteVram);
    ui->mainPixels = NULL;
    ui->overviewPixels = NULL;
    ui->overviewBackgroundPixels = NULL;
    ui->followPrevSpriteVram = NULL;
    ui->mainPixelsCap = 0u;
    ui->overviewPixelsCap = 0u;
    ui->overviewBackgroundPixelsCap = 0u;
    ui->followPrevSpriteVramWords = 0u;
    ui->mainTextureW = 0;
    ui->mainTextureH = 0;
    ui->overviewTextureW = 0;
    ui->overviewTextureH = 0;
    ui->overviewBackgroundTextureW = 0;
    ui->overviewBackgroundTextureH = 0;
    ui->overviewWindowStartRow = 0u;
    ui->overviewWindowVisibleRows = 0u;
    ui->overviewBackgroundStartRow = 0u;
    ui->overviewBackgroundVisibleRows = 0u;
    ui->overviewBackgroundTilesPerRow = 0u;
    ui->overviewBackgroundMode = -1;
    ui->overviewBackgroundContentToken = 0u;
    ui->contentPixelWidth = 0;
    ui->scrollX = 0;
    neogeo_memview_clearUiRefs(ui);
}

static void
neogeo_memview_overlayWindowCloseRequested(e9ui_window_t *window, void *user)
{
    (void)window;
    (void)user;
    neogeo_memview_toggle();
}

static int
neogeo_memview_init(void)
{
    neogeo_memview_state_t *ui = &neogeo_memview_stateSingleton;
    e9ui_component_t *overlayBodyHost = NULL;
    e9ui_rect_t rect;

    if (ui->windowState.open) {
        return 1;
    }

    neogeo_memview_releaseRuntimeState(ui);
    ui->ctx = e9ui->ctx;
    neogeo_memview_initOverviewRanges(ui);
    ui->windowState.windowHost = e9ui_windowCreate(neogeo_memview_windowBackend());
    if (!ui->windowState.windowHost) {
        return 0;
    }
    ui->ramBaseAddr = ui->ramBaseAddrHasSaved ? neogeo_memview_clampRamBaseAddr(ui->ramBaseAddr) : NEOGEO_MEMVIEW_RAM_BASE_MIN;
    ui->cromBaseAddr = ui->cromBaseAddrHasSaved ? neogeo_memview_alignCromBaseAddrToRow(ui, ui->cromBaseAddr) : neogeo_memview_findInitialCromBaseAddr(ui);
    ui->ramRowBytes = ui->ramRowBytesHasSaved ? neogeo_memview_clampRamRowBytes(ui->ramRowBytes) : NEOGEO_MEMVIEW_DEFAULT_RAM_ROW_BYTES;
    ui->cromTilesPerRow = ui->cromTilesPerRowHasSaved ? neogeo_memview_clampCromTilesPerRow(ui->cromTilesPerRow) : NEOGEO_MEMVIEW_DEFAULT_CROM_TILES_PER_ROW;
    ui->zoomLevel = ui->zoomHasSaved ? neogeo_memview_clampZoomLevel(ui->zoomLevel) : NEOGEO_MEMVIEW_ZOOM_DEFAULT;
    ui->overviewZoomLevel = ui->overviewZoomHasSaved ? neogeo_memview_clampOverviewZoomLevel(ui->overviewZoomLevel) : NEOGEO_MEMVIEW_OVERVIEW_ZOOM_DEFAULT;

    ui->root = neogeo_memview_buildRoot(ui);
    if (!ui->root) {
        e9ui_windowDestroy(ui->windowState.windowHost);
        ui->windowState.windowHost = NULL;
        return 0;
    }
    rect = e9ui_windowResolveStateOpenRect(&e9ui->ctx,
                                           neogeo_memview_windowDefaultRect(&e9ui->ctx),
                                           &ui->windowState);
    overlayBodyHost = neogeo_memview_makeOverlayBodyHost(ui);
    if (!overlayBodyHost) {
        e9ui_windowDestroy(ui->windowState.windowHost);
        ui->windowState.windowHost = NULL;
        ui->root = NULL;
        return 0;
    }
    ui->overlayBodyHost = overlayBodyHost;
    e9ui_windowOpen(ui->windowState.windowHost,
                    NEOGEO_MEMVIEW_TITLE,
                    rect,
                    overlayBodyHost,
                    neogeo_memview_overlayWindowCloseRequested,
                    ui,
                    &e9ui->ctx);
    ui->ctx = e9ui->ctx;
    ui->windowState.open = 1;
    aux_window_register(&neogeo_memview_auxWindowOps, ui);
    return 1;
}

static void
neogeo_memview_shutdown(void)
{
    neogeo_memview_state_t *ui = &neogeo_memview_stateSingleton;

    if (!ui->windowState.open) {
        return;
    }
    aux_window_unregister(&neogeo_memview_auxWindowOps, ui);
    if (ui->windowState.windowHost) {
        e9ui_windowDestroy(ui->windowState.windowHost);
        ui->windowState.windowHost = NULL;
    }
    ui->windowState.open = 0;
    ui->root = NULL;
    neogeo_memview_releaseRuntimeState(ui);
}

void
neogeo_memview_toggle(void)
{
    if (neogeo_memview_isOpen()) {
        neogeo_memview_shutdown();
    } else {
        (void)neogeo_memview_init();
    }
}

int
neogeo_memview_isOpen(void)
{
    return neogeo_memview_stateSingleton.windowState.open ? 1 : 0;
}

void
neogeo_memview_setMainWindowFocused(int focused)
{
    (void)focused;
}

void
neogeo_memview_render(void)
{
    neogeo_memview_state_t *ui = &neogeo_memview_stateSingleton;

    if (!ui->windowState.open) {
        return;
    }
    if (e9ui_windowCaptureStateRectChanged(&ui->windowState, &e9ui->ctx)) {
        config_saveConfig();
    }
}

void
neogeo_memview_persistConfig(FILE *file)
{
    neogeo_memview_state_t *ui = &neogeo_memview_stateSingleton;

    if (!file) {
        return;
    }
    e9ui_windowPersistStateRect(file, "comp.neogeo_memview", &ui->windowState, &e9ui->ctx);
    if (ui->modeHasSaved) {
        fprintf(file, "comp.neogeo_memview.mode=%d\n", ui->mode == neogeo_memview_mode_crom ? 1 : 0);
    }
    if (ui->ramBaseAddrHasSaved) {
        fprintf(file, "comp.neogeo_memview.ram_base_addr=%u\n", (unsigned)ui->ramBaseAddr);
    }
    if (ui->cromBaseAddrHasSaved) {
        fprintf(file, "comp.neogeo_memview.crom_base_addr=%u\n", (unsigned)ui->cromBaseAddr);
    }
    if (ui->ramRowBytesHasSaved) {
        fprintf(file, "comp.neogeo_memview.ram_row_bytes=%u\n", (unsigned)ui->ramRowBytes);
    }
    if (ui->cromTilesPerRowHasSaved) {
        fprintf(file, "comp.neogeo_memview.crom_tiles_per_row=%u\n", (unsigned)ui->cromTilesPerRow);
    }
    if (ui->zoomHasSaved) {
        fprintf(file, "comp.neogeo_memview.zoom=%d\n", ui->zoomLevel);
    }
    if (ui->overviewZoomHasSaved) {
        fprintf(file, "comp.neogeo_memview.overview_zoom=%d\n", ui->overviewZoomLevel);
    }
    if (ui->showAddressColumnHasSaved) {
        fprintf(file, "comp.neogeo_memview.show_address=%d\n", ui->showAddressColumn ? 1 : 0);
    }
    if (ui->showOverviewColumnHasSaved) {
        fprintf(file, "comp.neogeo_memview.show_overview=%d\n", ui->showOverviewColumn ? 1 : 0);
    }
    if (ui->followActiveSpritesHasSaved) {
        fprintf(file, "comp.neogeo_memview.follow=%d\n", ui->followActiveSprites ? 1 : 0);
    }
}

int
neogeo_memview_loadConfigProperty(const char *prop, const char *value)
{
    neogeo_memview_state_t *ui = &neogeo_memview_stateSingleton;
    int intValue = 0;
    unsigned long long parsed = 0u;
    char *end = NULL;

    if (!prop || !value) {
        return 0;
    }
    if (strcmp(prop, "win_x") == 0) {
        if (!neogeo_memview_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winX = intValue;
        ui->windowState.winHasSaved = e9ui_windowHasSavedPosition(ui->windowState.winX, ui->windowState.winY);
        return 1;
    }
    if (strcmp(prop, "win_y") == 0) {
        if (!neogeo_memview_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winY = intValue;
        ui->windowState.winHasSaved = e9ui_windowHasSavedPosition(ui->windowState.winX, ui->windowState.winY);
        return 1;
    }
    if (strcmp(prop, "win_w") == 0) {
        if (!neogeo_memview_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winW = intValue;
        ui->windowState.winHasSaved = e9ui_windowHasSavedPosition(ui->windowState.winX, ui->windowState.winY);
        return 1;
    }
    if (strcmp(prop, "win_h") == 0) {
        if (!neogeo_memview_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winH = intValue;
        ui->windowState.winHasSaved = e9ui_windowHasSavedPosition(ui->windowState.winX, ui->windowState.winY);
        return 1;
    }
    if (strcmp(prop, "mode") == 0) {
        if (!neogeo_memview_parseInt(value, &intValue)) {
            return 0;
        }
        ui->mode = intValue ? neogeo_memview_mode_crom : neogeo_memview_mode_ram;
        ui->modeHasSaved = 1;
        return 1;
    }
    if (strcmp(prop, "ram_base_addr") == 0) {
        if (!neogeo_memview_parseU64SmartHex(value, &parsed, &end) || !end || *end != '\0') {
            return 0;
        }
        ui->ramBaseAddr = neogeo_memview_clampRamBaseAddr((uint32_t)parsed);
        ui->ramBaseAddrHasSaved = 1;
        return 1;
    }
    if (strcmp(prop, "crom_base_addr") == 0) {
        if (!neogeo_memview_parseU64SmartHex(value, &parsed, &end) || !end || *end != '\0') {
            return 0;
        }
        ui->cromBaseAddr = neogeo_memview_alignCromBaseAddrToRow(ui, (uint32_t)parsed);
        ui->cromBaseAddrHasSaved = 1;
        return 1;
    }
    if (strcmp(prop, "ram_row_bytes") == 0 || strcmp(prop, "row_bytes") == 0) {
        if (!neogeo_memview_parseU64SmartHex(value, &parsed, &end) || !end || *end != '\0' || parsed == 0u) {
            return 0;
        }
        ui->ramRowBytes = neogeo_memview_clampRamRowBytes((uint32_t)parsed);
        ui->ramRowBytesHasSaved = 1;
        return 1;
    }
    if (strcmp(prop, "crom_tiles_per_row") == 0) {
        if (!neogeo_memview_parseU64SmartHex(value, &parsed, &end) || !end || *end != '\0' || parsed == 0u) {
            return 0;
        }
        ui->cromTilesPerRow = neogeo_memview_clampCromTilesPerRow((uint32_t)parsed);
        ui->cromTilesPerRowHasSaved = 1;
        return 1;
    }
    if (strcmp(prop, "zoom") == 0) {
        if (!neogeo_memview_parseInt(value, &intValue)) {
            return 0;
        }
        ui->zoomLevel = neogeo_memview_clampZoomLevel(intValue);
        ui->zoomHasSaved = 1;
        return 1;
    }
    if (strcmp(prop, "overview_zoom") == 0) {
        if (!neogeo_memview_parseInt(value, &intValue)) {
            return 0;
        }
        ui->overviewZoomLevel = neogeo_memview_clampOverviewZoomLevel(intValue);
        ui->overviewZoomHasSaved = 1;
        return 1;
    }
    if (strcmp(prop, "show_address") == 0) {
        if (!neogeo_memview_parseInt(value, &intValue)) {
            return 0;
        }
        ui->showAddressColumn = intValue ? 1 : 0;
        ui->showAddressColumnHasSaved = 1;
        return 1;
    }
    if (strcmp(prop, "show_overview") == 0) {
        if (!neogeo_memview_parseInt(value, &intValue)) {
            return 0;
        }
        ui->showOverviewColumn = intValue ? 1 : 0;
        ui->showOverviewColumnHasSaved = 1;
        return 1;
    }
    if (strcmp(prop, "follow") == 0) {
        if (!neogeo_memview_parseInt(value, &intValue)) {
            return 0;
        }
        ui->followActiveSprites = intValue ? 1 : 0;
        ui->followActiveSpritesHasSaved = 1;
        return 1;
    }
    return 0;
}
