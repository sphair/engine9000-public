/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <ctype.h>
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
#include "mega_memview_internal.h"

typedef struct mega_memview_overlay_body_state {
    mega_memview_state_t *ui;
} mega_memview_overlay_body_state_t;

typedef struct mega_memview_toolbar_item_state {
    e9ui_component_t *child;
    int widthPx;
} mega_memview_toolbar_item_state_t;

typedef struct mega_memview_toolbar_wrap_state {
    int padPx;
    int gapPx;
} mega_memview_toolbar_wrap_state_t;

typedef struct mega_memview_checkbox_binding {
    const char *configProp;
    int *value;
    int *hasSaved;
    int resetFollow;
} mega_memview_checkbox_binding_t;

typedef struct mega_memview_step_buttons_action_ctx {
    mega_memview_state_t *ui;
    e9ui_component_t *canvas;
} mega_memview_step_buttons_action_ctx_t;

static mega_memview_state_t mega_memview_stateSingleton = {
    .windowState.winX = E9UI_WINDOW_COORD_UNSET,
    .windowState.winY = E9UI_WINDOW_COORD_UNSET,
    .windowState.openMinWidthNoSavedSizePx = 640,
    .windowState.openMinHeightNoSavedSizePx = 360,
    .windowState.openCenterWhenNoSaved = 1,
    .mode = mega_memview_mode_vram,
    .ramBaseAddr = MEGA_MEMVIEW_RAM_BASE_MIN,
    .vramBaseAddr = 0u,
    .zramBaseAddr = MEGA_MEMVIEW_ZRAM_BASE_MIN,
    .ramRowBytes = MEGA_MEMVIEW_DEFAULT_RAM_ROW_BYTES,
    .vramTilesPerRow = MEGA_MEMVIEW_DEFAULT_VRAM_TILES_PER_ROW,
    .zoomLevel = MEGA_MEMVIEW_ZOOM_DEFAULT,
    .overviewZoomLevel = MEGA_MEMVIEW_OVERVIEW_ZOOM_DEFAULT,
    .showAddressColumn = 1,
    .showOverviewColumn = 1,
    .followActiveSprites = 0,
    .colorSourceSprites = 1,
    .colorSourcePlaneA = 1,
    .colorSourcePlaneB = 1,
    .colorSourceHScroll = 1
};

static const aux_window_ops_t mega_memview_auxWindowOps = {
    .render = mega_memview_render,
};

static const mega_memview_mode_t mega_memview_modeButtonModes[] = {
    mega_memview_mode_ram,
    mega_memview_mode_vram,
    mega_memview_mode_zram,
    mega_memview_mode_roms
};

static mega_memview_checkbox_binding_t mega_memview_checkboxBindings[] = {
    { "show_address", &mega_memview_stateSingleton.showAddressColumn, &mega_memview_stateSingleton.showAddressColumnHasSaved, 0 },
    { "show_overview", &mega_memview_stateSingleton.showOverviewColumn, &mega_memview_stateSingleton.showOverviewColumnHasSaved, 0 },
    { "follow", &mega_memview_stateSingleton.followActiveSprites, &mega_memview_stateSingleton.followActiveSpritesHasSaved, 1 },
    { "color_sprites", &mega_memview_stateSingleton.colorSourceSprites, &mega_memview_stateSingleton.colorSourceSpritesHasSaved, 0 },
    { "color_plane_a", &mega_memview_stateSingleton.colorSourcePlaneA, &mega_memview_stateSingleton.colorSourcePlaneAHasSaved, 0 },
    { "color_plane_b", &mega_memview_stateSingleton.colorSourcePlaneB, &mega_memview_stateSingleton.colorSourcePlaneBHasSaved, 0 },
    { "color_hscroll", &mega_memview_stateSingleton.colorSourceHScroll, &mega_memview_stateSingleton.colorSourceHScrollHasSaved, 0 }
};

static int
mega_memview_stepButtonsGutterWidth(const e9ui_context_t *ctx, e9ui_component_t *self);

static int
mega_memview_canvasVisibleRows(const mega_memview_state_t *ui, const e9ui_rect_t *bounds);

static uint32_t
mega_memview_clampBaseForView(const mega_memview_state_t *ui, const e9ui_rect_t *bounds, uint32_t baseAddr);

static void
mega_memview_syncTextboxesFromState(mega_memview_state_t *ui);

static size_t
mega_memview_checkboxBindingCount(void)
{
    return sizeof(mega_memview_checkboxBindings) / sizeof(mega_memview_checkboxBindings[0]);
}

static int
mega_memview_parseInt(const char *value, int *out)
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
mega_memview_parseU64SmartHex(const char *text, unsigned long long *outValue, char **outEnd)
{
    const char *cursor = NULL;
    int base = 0;
    char *end = NULL;

    if (!text || !outValue) {
        return 0;
    }
    cursor = text;
    while (isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor == '$') {
        cursor++;
        base = 16;
    } else if (!(cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X'))) {
        for (const char *scan = cursor; *scan; ++scan) {
            if (isspace((unsigned char)*scan)) {
                break;
            }
            if (isxdigit((unsigned char)*scan) && isalpha((unsigned char)*scan)) {
                base = 16;
                break;
            }
        }
    }

    *outValue = strtoull(cursor, &end, base);
    if (!end || end == cursor) {
        return 0;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (outEnd) {
        *outEnd = end;
    }
    return 1;
}

static int
mega_memview_measureToolbarTextWidth(const e9ui_context_t *ctx,
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
mega_memview_measureToolbarTextboxWidth(TTF_Font *font,
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
mega_memview_clampInt(int value, int minValue, int maxValue)
{
    if (value < minValue) {
        value = minValue;
    }
    if (value > maxValue) {
        value = maxValue;
    }
    return value;
}

static int
mega_memview_clampZoomLevel(int zoomLevel)
{
    return mega_memview_clampInt(zoomLevel, MEGA_MEMVIEW_ZOOM_MIN, MEGA_MEMVIEW_ZOOM_MAX);
}

int
mega_memview_clampOverviewZoomLevel(int zoomLevel)
{
    return mega_memview_clampInt(zoomLevel, MEGA_MEMVIEW_OVERVIEW_ZOOM_MIN, MEGA_MEMVIEW_OVERVIEW_ZOOM_MAX);
}

static uint32_t
mega_memview_clampU32(uint32_t value, uint32_t minValue, uint32_t maxValue)
{
    if (value < minValue) {
        value = minValue;
    }
    if (value > maxValue) {
        value = maxValue;
    }
    return value;
}

uint32_t
mega_memview_clampRamRowBytes(uint32_t rowBytes)
{
    return mega_memview_clampU32(rowBytes ? rowBytes : MEGA_MEMVIEW_DEFAULT_RAM_ROW_BYTES,
                                   1u,
                                   MEGA_MEMVIEW_MAX_RAM_ROW_BYTES);
}

uint32_t
mega_memview_clampVramTilesPerRow(uint32_t tilesPerRow)
{
    return mega_memview_clampU32(tilesPerRow ? tilesPerRow : MEGA_MEMVIEW_DEFAULT_VRAM_TILES_PER_ROW,
                                   1u,
                                   MEGA_MEMVIEW_MAX_VRAM_TILES_PER_ROW);
}

uint32_t
mega_memview_clampRamBaseAddr(uint32_t baseAddr)
{
    return mega_memview_clampU32(baseAddr, MEGA_MEMVIEW_RAM_BASE_MIN, MEGA_MEMVIEW_RAM_BASE_MAX);
}

uint32_t
mega_memview_clampZramBaseAddr(uint32_t baseAddr)
{
    return mega_memview_clampU32(baseAddr, MEGA_MEMVIEW_ZRAM_BASE_MIN, MEGA_MEMVIEW_ZRAM_BASE_MAX);
}

uint32_t
mega_memview_vramSize(void)
{
    size_t vramSize = 0u;

    if (!libretro_host_getMemory(RETRO_MEMORY_VIDEO_RAM, &vramSize)) {
        return 0u;
    }
    if (vramSize > 0xffffffffu) {
        return 0xffffffffu;
    }
    return (uint32_t)vramSize;
}

uint32_t
mega_memview_clampVramBaseAddr(uint32_t baseAddr)
{
    uint32_t sizeBytes = mega_memview_vramSize();
    uint32_t maxBaseAddr = 0u;

    if (sizeBytes < MEGA_MEMVIEW_TILE_BYTES) {
        return 0u;
    }
    maxBaseAddr = sizeBytes - MEGA_MEMVIEW_TILE_BYTES;
    baseAddr &= ~(MEGA_MEMVIEW_TILE_BYTES - 1u);
    if (baseAddr > maxBaseAddr) {
        baseAddr = maxBaseAddr & ~(MEGA_MEMVIEW_TILE_BYTES - 1u);
    }
    return baseAddr;
}

static uint32_t
mega_memview_alignVramBaseAddrToRow(const mega_memview_state_t *ui, uint32_t baseAddr)
{
    uint32_t rowBytes = 0u;

    baseAddr = mega_memview_clampVramBaseAddr(baseAddr);
    rowBytes = mega_memview_clampVramTilesPerRow(ui->vramTilesPerRow) * MEGA_MEMVIEW_TILE_BYTES;
    if (rowBytes == 0u) {
        return baseAddr;
    }
    baseAddr -= baseAddr % rowBytes;
    return mega_memview_clampVramBaseAddr(baseAddr);
}

static int
mega_memview_zoomScaledPx(const mega_memview_state_t *ui, int basePx)
{
    int scaled = e9ui_scale_px(&ui->ctx, basePx);
    int zoomLevel = mega_memview_clampZoomLevel(ui->zoomLevel);

    scaled = (scaled * zoomLevel) / 8;
    if (scaled < 1) {
        scaled = 1;
    }
    return scaled;
}

static int
mega_memview_ramBitPx(const mega_memview_state_t *ui)
{
    return mega_memview_zoomScaledPx(ui, MEGA_MEMVIEW_RAM_BIT_PX);
}

static int
mega_memview_ramRowPx(const mega_memview_state_t *ui)
{
    return mega_memview_zoomScaledPx(ui, MEGA_MEMVIEW_RAM_ROW_PX);
}

static int
mega_memview_tilePixelPx(const mega_memview_state_t *ui)
{
    return mega_memview_ramBitPx(ui);
}

uint32_t
mega_memview_currentBaseAddr(const mega_memview_state_t *ui)
{
    if (ui->mode == mega_memview_mode_vram) {
        return ui->vramBaseAddr;
    }
    if (ui->mode == mega_memview_mode_zram) {
        return ui->zramBaseAddr;
    }
    return ui->ramBaseAddr;
}

static void
mega_memview_setCurrentBaseAddrSaved(mega_memview_state_t *ui, uint32_t baseAddr)
{
    if (ui->mode == mega_memview_mode_vram) {
        ui->vramBaseAddr = baseAddr;
        ui->vramBaseAddrHasSaved = 1;
    } else if (ui->mode == mega_memview_mode_zram) {
        ui->zramBaseAddr = baseAddr;
        ui->zramBaseAddrHasSaved = 1;
    } else {
        ui->ramBaseAddr = baseAddr;
        ui->ramBaseAddrHasSaved = 1;
    }
}

uint32_t
mega_memview_vramTotalRows(const mega_memview_state_t *ui)
{
    uint32_t vramTiles = mega_memview_vramSize() / MEGA_MEMVIEW_TILE_BYTES;
    uint32_t tilesPerRow = mega_memview_clampVramTilesPerRow(ui->vramTilesPerRow);

    if (vramTiles == 0u || tilesPerRow == 0u) {
        return 0u;
    }
    return (vramTiles + tilesPerRow - 1u) / tilesPerRow;
}

uint32_t
mega_memview_currentRowBytes(const mega_memview_state_t *ui)
{
    if (ui->mode == mega_memview_mode_vram) {
        return mega_memview_clampVramTilesPerRow(ui->vramTilesPerRow) * MEGA_MEMVIEW_TILE_BYTES;
    }
    if (ui->mode == mega_memview_mode_roms) {
        return 1u;
    }
    return mega_memview_clampRamRowBytes(ui->ramRowBytes);
}

static void
mega_memview_countActiveSpriteRows(const e9k_debug_mega_sprite_state_t *spriteState,
                                     uint32_t vramTiles,
                                     uint32_t tilesPerRow,
                                     uint32_t totalRows,
                                     const e9k_debug_mega_sprite_entry_t *prevEntries,
                                     size_t prevEntryCount,
                                     uint32_t *rowActiveCounts,
                                     uint32_t *rowChangedCounts)
{
    int spriteEntryCount = 0;
    int havePrevEntries = 0;

    if (!spriteState || vramTiles == 0u || tilesPerRow == 0u || totalRows == 0u || !rowActiveCounts) {
        return;
    }
    spriteEntryCount = spriteState->spriteEntryCount;
    if (spriteEntryCount < 0) {
        spriteEntryCount = 0;
    }
    if (spriteEntryCount > E9K_DEBUG_MEGA_MAX_FRAME_SPRITES) {
        spriteEntryCount = E9K_DEBUG_MEGA_MAX_FRAME_SPRITES;
    }
    havePrevEntries = prevEntries && prevEntryCount == (size_t)spriteEntryCount;
    for (int i = 0; i < spriteEntryCount; ++i) {
        const e9k_debug_mega_sprite_entry_t *entry = &spriteState->spriteEntries[i];
        uint32_t widthTiles = entry->widthTiles ? entry->widthTiles : 1u;
        uint32_t heightTiles = entry->heightTiles ? entry->heightTiles : 1u;
        int entryChanged = havePrevEntries && memcmp(entry, &prevEntries[i], sizeof(*entry)) != 0;

        if (!(entry->flags & (E9K_DEBUG_MEGA_SPRITEFLAG_VISIBLE | E9K_DEBUG_MEGA_SPRITEFLAG_RENDERED))) {
            continue;
        }
        for (uint32_t tx = 0u; tx < widthTiles; ++tx) {
            for (uint32_t ty = 0u; ty < heightTiles; ++ty) {
                uint32_t spriteTileNum = ((uint32_t)entry->tileIndex + tx * heightTiles + ty) % vramTiles;
                uint32_t spriteRow = spriteTileNum / tilesPerRow;

                if (spriteRow < totalRows) {
                    rowActiveCounts[spriteRow]++;
                    if (rowChangedCounts && entryChanged) {
                        rowChangedCounts[spriteRow]++;
                    }
                }
            }
        }
    }
}

static void
mega_memview_followActiveVramWindow(mega_memview_state_t *ui, const e9ui_rect_t *bounds)
{
    e9k_debug_mega_sprite_state_t spriteState;
    uint32_t vramTiles = 0u;
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
    size_t prevBytes = 0u;
    e9k_debug_mega_sprite_entry_t *nextPrevEntries = NULL;
    uint64_t currentScore = 0u;
    uint64_t bestScore = 0u;
    uint64_t thresholdScore = 0u;
    uint32_t bestActiveSum = 0u;
    uint32_t bestChangedSum = 0u;
    uint32_t bootstrapBestStartRow = 0u;
    uint32_t bootstrapBestActiveSum = 0u;
    int spriteEntryCount = 0;
    int bestQualified = 0;

    if (!bounds || ui->mode != mega_memview_mode_vram || !ui->followActiveSprites) {
        return;
    }
    if (!libretro_host_megadrive_getSpriteState(&spriteState)) {
        return;
    }
    spriteEntryCount = spriteState.spriteEntryCount;
    if (spriteEntryCount < 0) {
        spriteEntryCount = 0;
    }
    if (spriteEntryCount > E9K_DEBUG_MEGA_MAX_FRAME_SPRITES) {
        spriteEntryCount = E9K_DEBUG_MEGA_MAX_FRAME_SPRITES;
    }
    vramTiles = mega_memview_vramSize() / MEGA_MEMVIEW_TILE_BYTES;
    tilesPerRow = mega_memview_clampVramTilesPerRow(ui->vramTilesPerRow);
    totalRows = mega_memview_vramTotalRows(ui);
    visibleRows = (uint32_t)mega_memview_canvasVisibleRows(ui, bounds);
    if (vramTiles == 0u || tilesPerRow == 0u || totalRows == 0u || visibleRows == 0u) {
        return;
    }
    if (visibleRows > totalRows) {
        visibleRows = totalRows;
    }
    maxStartRow = totalRows > visibleRows ? totalRows - visibleRows : 0u;
    currentRow = (ui->vramBaseAddr / MEGA_MEMVIEW_TILE_BYTES) / tilesPerRow;
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
    prevBytes = (size_t)spriteEntryCount * sizeof(spriteState.spriteEntries[0]);
    if (spriteEntryCount != 0) {
        nextPrevEntries = (e9k_debug_mega_sprite_entry_t *)realloc(ui->followPrevSpriteEntries, prevBytes);
        if (!nextPrevEntries) {
            alloc_free(rowActiveCounts);
            alloc_free(rowChangedCounts);
            return;
        }
        ui->followPrevSpriteEntries = nextPrevEntries;
    }
    mega_memview_countActiveSpriteRows(&spriteState,
                                         vramTiles,
                                         tilesPerRow,
                                         totalRows,
                                         ui->followPrevSpriteEntries,
                                         ui->followPrevSpriteEntryCount,
                                         rowActiveCounts,
                                         rowChangedCounts);
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
    thresholdScore = (currentScore * (uint64_t)MEGA_MEMVIEW_FOLLOW_SCORE_MARGIN_NUM +
                      (uint64_t)MEGA_MEMVIEW_FOLLOW_SCORE_MARGIN_DEN - 1u) /
                     (uint64_t)MEGA_MEMVIEW_FOLLOW_SCORE_MARGIN_DEN;
    bestQualified = bestChangedSum >= MEGA_MEMVIEW_FOLLOW_MIN_CHANGED_TILES &&
                    bestActiveSum >= MEGA_MEMVIEW_FOLLOW_MIN_ACTIVE_TILES &&
                    bestScore >= MEGA_MEMVIEW_FOLLOW_MIN_SCORE &&
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
    if ((!ui->followPrevSpriteEntryCount || ui->followPrevSpriteEntryCount != (size_t)spriteEntryCount) &&
        bootstrapBestActiveSum > 0u && bootstrapBestStartRow != currentRow) {
        mega_memview_setCurrentBaseAddrSaved(ui,
                                               mega_memview_clampBaseForView(ui,
                                                                               bounds,
                                                                               bootstrapBestStartRow * tilesPerRow * MEGA_MEMVIEW_TILE_BYTES));
        ui->followPendingStartRow = bootstrapBestStartRow;
        ui->followPendingFrames = 0u;
        mega_memview_syncTextboxesFromState(ui);
        config_saveConfig();
    }
    if (ui->followPrevSpriteEntries && ui->followPrevSpriteEntryCount == (size_t)spriteEntryCount &&
        bestQualified && ui->followPendingFrames >= MEGA_MEMVIEW_FOLLOW_STABLE_FRAMES) {
        mega_memview_setCurrentBaseAddrSaved(ui,
                                               mega_memview_clampBaseForView(ui,
                                                                               bounds,
                                                                               bestStartRow * tilesPerRow * MEGA_MEMVIEW_TILE_BYTES));
        ui->followPendingStartRow = bestStartRow;
        ui->followPendingFrames = 0u;
        mega_memview_syncTextboxesFromState(ui);
        config_saveConfig();
    }
    if (ui->followPrevSpriteEntries && spriteEntryCount != 0) {
        memcpy(ui->followPrevSpriteEntries, spriteState.spriteEntries, prevBytes);
        ui->followPrevSpriteEntryCount = (size_t)spriteEntryCount;
    }
    alloc_free(rowActiveCounts);
    alloc_free(rowChangedCounts);
}

static uint32_t
mega_memview_findInitialVramBaseAddr(const mega_memview_state_t *ui)
{
    e9k_debug_mega_sprite_state_t spriteState;
    uint32_t vramTiles = 0u;
    uint32_t tilesPerRow = 0u;
    uint32_t totalRows = 0u;
    uint32_t *rowActiveCounts = NULL;
    uint32_t bestRow = 0u;
    uint32_t bestCount = 0u;
    uint32_t baseAddr = 0u;

    if (!libretro_host_megadrive_getSpriteState(&spriteState)) {
        return 0u;
    }
    vramTiles = mega_memview_vramSize() / MEGA_MEMVIEW_TILE_BYTES;
    tilesPerRow = mega_memview_clampVramTilesPerRow(ui->vramTilesPerRow);
    totalRows = mega_memview_vramTotalRows(ui);
    if (vramTiles == 0u || tilesPerRow == 0u || totalRows == 0u) {
        return 0u;
    }
    rowActiveCounts = (uint32_t *)alloc_calloc(totalRows, sizeof(*rowActiveCounts));
    if (!rowActiveCounts) {
        return 0u;
    }
    mega_memview_countActiveSpriteRows(&spriteState,
                                         vramTiles,
                                         tilesPerRow,
                                         totalRows,
                                         NULL,
                                         0u,
                                         rowActiveCounts,
                                         NULL);
    for (uint32_t row = 0u; row < totalRows; ++row) {
        if (rowActiveCounts[row] > bestCount) {
            bestCount = rowActiveCounts[row];
            bestRow = row;
        }
    }
    baseAddr = bestCount > 0u ? bestRow * tilesPerRow * MEGA_MEMVIEW_TILE_BYTES : 0u;
    alloc_free(rowActiveCounts);
    return mega_memview_alignVramBaseAddrToRow(ui, baseAddr);
}

static int
mega_memview_canvasVisibleRows(const mega_memview_state_t *ui, const e9ui_rect_t *bounds)
{
    int usableHeight = 0;
    int rowPx = 1;

    if (!bounds) {
        return 1;
    }
    usableHeight = bounds->h - e9ui_scale_px(&ui->ctx, MEGA_MEMVIEW_TOP_PAD_PX) -
                   e9ui_scale_px(&ui->ctx, MEGA_MEMVIEW_BOTTOM_PAD_PX);
    if (usableHeight < 1) {
        usableHeight = 1;
    }
    if (ui->mode == mega_memview_mode_roms) {
        rowPx = e9ui_scale_px(&ui->ctx, 48);
    } else if (ui->mode == mega_memview_mode_vram) {
        rowPx = MEGA_MEMVIEW_TILE_H * mega_memview_tilePixelPx(ui) +
                e9ui_scale_px(&ui->ctx, MEGA_MEMVIEW_TILE_GAP_PX);
    } else {
        rowPx = mega_memview_ramRowPx(ui);
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
mega_memview_clampBaseForView(const mega_memview_state_t *ui, const e9ui_rect_t *bounds, uint32_t baseAddr)
{
    uint32_t rangeStart = 0u;
    uint32_t rangeSize = 0u;
    uint64_t viewBytes = 0u;
    uint64_t maxBase = 0u;

    if (!bounds) {
        return baseAddr;
    }
    if (ui->mode == mega_memview_mode_roms) {
        return 0u;
    }
    if (ui->mode == mega_memview_mode_vram) {
        rangeStart = 0u;
        rangeSize = mega_memview_vramSize();
        if (rangeSize < MEGA_MEMVIEW_TILE_BYTES) {
            return 0u;
        }
    } else if (ui->mode == mega_memview_mode_zram) {
        rangeStart = MEGA_MEMVIEW_ZRAM_BASE_MIN;
        rangeSize = MEGA_MEMVIEW_ZRAM_BASE_MAX - MEGA_MEMVIEW_ZRAM_BASE_MIN + 1u;
    } else {
        rangeStart = MEGA_MEMVIEW_RAM_BASE_MIN;
        rangeSize = MEGA_MEMVIEW_RAM_BASE_MAX - MEGA_MEMVIEW_RAM_BASE_MIN + 1u;
    }

    viewBytes = (uint64_t)mega_memview_canvasVisibleRows(ui, bounds) *
                (uint64_t)mega_memview_currentRowBytes(ui);
    if (viewBytes >= rangeSize) {
        maxBase = rangeStart;
    } else {
        maxBase = (uint64_t)rangeStart + (uint64_t)rangeSize - viewBytes;
    }

    if (ui->mode == mega_memview_mode_vram) {
        baseAddr = mega_memview_alignVramBaseAddrToRow(ui, baseAddr);
    } else if (ui->mode == mega_memview_mode_zram) {
        baseAddr = mega_memview_clampZramBaseAddr(baseAddr);
    } else {
        baseAddr = mega_memview_clampRamBaseAddr(baseAddr);
    }
    if ((uint64_t)baseAddr > maxBase) {
        baseAddr = (uint32_t)maxBase;
    }
    if (ui->mode == mega_memview_mode_vram) {
        baseAddr = mega_memview_alignVramBaseAddrToRow(ui, baseAddr);
    }
    return baseAddr;
}

static void
mega_memview_syncTextboxesFromState(mega_memview_state_t *ui)
{
    char label[32];
    uint32_t baseAddr = 0u;
    uint32_t widthValue = 0u;

    widthValue = ui->mode == mega_memview_mode_vram ?
        mega_memview_clampVramTilesPerRow(ui->vramTilesPerRow) :
        mega_memview_clampRamRowBytes(ui->ramRowBytes);

    if (ui->addressBox) {
        if (ui->mode == mega_memview_mode_roms) {
            e9ui_textbox_setPlaceholder(ui->addressBox, "");
            e9ui_textbox_setText(ui->addressBox, "");
            e9ui_setDisabled(ui->addressBox, 1);
        } else {
            baseAddr = mega_memview_currentBaseAddr(ui);
            snprintf(label, sizeof(label), "0x%06X", (unsigned)(baseAddr & 0x00ffffffu));
            e9ui_textbox_setPlaceholder(ui->addressBox, "0x00100000");
            e9ui_textbox_setText(ui->addressBox, label);
            e9ui_setDisabled(ui->addressBox, 0);
        }
    }
    snprintf(label, sizeof(label), "%u", (unsigned)widthValue);
    if (ui->widthBox) {
        e9ui_textbox_setText(ui->widthBox, label);
    }
    if (ui->widthSeek) {
        float percent = 0.0f;
        if (ui->mode == mega_memview_mode_vram) {
            percent = (float)(widthValue - 1u) / (float)(MEGA_MEMVIEW_MAX_VRAM_TILES_PER_ROW - 1u);
        } else {
            percent = (float)(widthValue - 1u) / (float)(MEGA_MEMVIEW_MAX_RAM_ROW_BYTES - 1u);
        }
        e9ui_seek_bar_setPercent(ui->widthSeek, percent);
    }
    if (ui->zoomSeek) {
        float percent = 0.0f;
        if (MEGA_MEMVIEW_ZOOM_MAX > MEGA_MEMVIEW_ZOOM_MIN) {
            percent = (float)(ui->zoomLevel - MEGA_MEMVIEW_ZOOM_MIN) /
                      (float)(MEGA_MEMVIEW_ZOOM_MAX - MEGA_MEMVIEW_ZOOM_MIN);
        }
        e9ui_seek_bar_setPercent(ui->zoomSeek, percent);
    }
    if (ui->overviewZoomSeek) {
        float percent = 0.0f;
        int zoomLevel = mega_memview_clampOverviewZoomLevel(ui->overviewZoomLevel);

        if (MEGA_MEMVIEW_OVERVIEW_ZOOM_MAX > MEGA_MEMVIEW_OVERVIEW_ZOOM_MIN) {
            percent = (float)(zoomLevel - MEGA_MEMVIEW_OVERVIEW_ZOOM_MIN) /
                      (float)(MEGA_MEMVIEW_OVERVIEW_ZOOM_MAX - MEGA_MEMVIEW_OVERVIEW_ZOOM_MIN);
        }
        e9ui_seek_bar_setPercent(ui->overviewZoomSeek, percent);
    }
}

static void
mega_memview_updateModeButton(e9ui_component_t *button, int active, const e9k_theme_button_t *activeTheme)
{
    if (!button) {
        return;
    }
    if (active) {
        e9ui_button_setTheme(button, activeTheme);
    } else {
        e9ui_button_clearTheme(button);
    }
}

static void
mega_memview_updateModeButtons(mega_memview_state_t *ui)
{
    const e9k_theme_button_t *activeTheme = e9ui_theme_button_preset_profile_active();

    mega_memview_updateModeButton(ui->modeButtonRam, ui->mode == mega_memview_mode_ram, activeTheme);
    mega_memview_updateModeButton(ui->modeButtonVram, ui->mode == mega_memview_mode_vram, activeTheme);
    mega_memview_updateModeButton(ui->modeButtonZram, ui->mode == mega_memview_mode_zram, activeTheme);
    mega_memview_updateModeButton(ui->modeButtonRoms, ui->mode == mega_memview_mode_roms, activeTheme);
}

void
mega_memview_setView(mega_memview_state_t *ui, uint32_t baseAddr, uint32_t rowBytes, int resetScroll)
{
    const e9ui_rect_t *bounds = ui->canvas ? &ui->canvas->bounds : &(e9ui_rect_t){ 0, 0, 640, 360 };

    if (ui->mode == mega_memview_mode_vram) {
        ui->vramTilesPerRow = mega_memview_clampVramTilesPerRow(rowBytes / MEGA_MEMVIEW_TILE_BYTES);
        ui->vramTilesPerRowHasSaved = 1;
    } else if (ui->mode == mega_memview_mode_zram) {
        ui->ramRowBytes = mega_memview_clampRamRowBytes(rowBytes);
        ui->ramRowBytesHasSaved = 1;
    } else if (ui->mode == mega_memview_mode_roms) {
        ui->ramRowBytes = mega_memview_clampRamRowBytes(rowBytes);
        ui->ramRowBytesHasSaved = 1;
    } else {
        ui->ramRowBytes = mega_memview_clampRamRowBytes(rowBytes);
        ui->ramRowBytesHasSaved = 1;
    }
    mega_memview_setCurrentBaseAddrSaved(ui, mega_memview_clampBaseForView(ui, bounds, baseAddr));
    if (resetScroll) {
        ui->scrollX = 0;
    }
    mega_memview_syncTextboxesFromState(ui);
    config_saveConfig();
}

int
mega_memview_readRange(const mega_memview_state_t *ui, uint32_t addr, void *out, size_t sizeBytes)
{
    size_t vramSize = 0u;
    const uint8_t *vram = NULL;

    if (!out || sizeBytes == 0u) {
        return 0;
    }
    if (ui->mode == mega_memview_mode_vram) {
        vram = (const uint8_t *)libretro_host_getMemory(RETRO_MEMORY_VIDEO_RAM, &vramSize);
        if (!vram) {
            memset(out, 0, sizeBytes);
            return 0;
        }
        if ((uint64_t)addr + (uint64_t)sizeBytes > vramSize) {
            memset(out, 0, sizeBytes);
            if (addr >= vramSize) {
                return 0;
            }
            memcpy(out, vram + addr, (size_t)(vramSize - addr));
            return 1;
        }
        memcpy(out, vram + addr, sizeBytes);
        return 1;
    }
    if (ui->mode == mega_memview_mode_zram) {
        uint32_t readAddr = addr;
        size_t dstOffset = 0u;
        size_t readableBytes = sizeBytes;

        memset(out, 0, sizeBytes);
        if (readAddr > MEGA_MEMVIEW_ZRAM_BASE_MAX) {
            return 0;
        }
        if ((uint64_t)readAddr + (uint64_t)readableBytes > (uint64_t)MEGA_MEMVIEW_ZRAM_BASE_MAX + 1u) {
            readableBytes = (size_t)((uint64_t)MEGA_MEMVIEW_ZRAM_BASE_MAX + 1u - (uint64_t)readAddr);
        }
        return libretro_host_debugReadProcessorMemory(MEGA_MEMVIEW_Z80_PROCESSOR_ID,
                                                      readAddr,
                                                      (uint8_t *)out + dstOffset,
                                                      readableBytes) ? 1 : 0;
    }
    if (ui->mode == mega_memview_mode_roms) {
        memset(out, 0, sizeBytes);
        return 0;
    }
    return libretro_host_debugReadMemory(addr, out, sizeBytes) ? 1 : 0;
}

uint32_t
mega_memview_argb(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

uint32_t
mega_memview_amberColor(unsigned index)
{
    static const uint32_t palette[16] = {
        0xff110700u, 0xff211000u, 0xff311700u, 0xff422000u,
        0xff522900u, 0xff633100u, 0xff733a00u, 0xff844300u,
        0xff944c00u, 0xffa55500u, 0xffb65e00u, 0xffc66700u,
        0xffd77008u, 0xffe77e20u, 0xfff09144u, 0xffffbd73u
    };

    return palette[index & 15u];
}

static void
mega_memview_fillRect(uint32_t *pixels,
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

static uint32_t
mega_memview_vramUsageColor(uint8_t usage)
{
    if (usage && (usage & (uint8_t)(usage - 1u))) {
        return mega_memview_argb(250, 210, 70);
    }
    if (usage & MEGA_MEMVIEW_VRAM_USAGE_SAT) {
        return mega_memview_argb(225, 80, 210);
    }
    if (usage & MEGA_MEMVIEW_VRAM_USAGE_HSCROLL) {
        return mega_memview_argb(70, 185, 245);
    }
    if (usage & MEGA_MEMVIEW_VRAM_USAGE_PLANE_A_TABLE) {
        return mega_memview_argb(80, 220, 120);
    }
    if (usage & MEGA_MEMVIEW_VRAM_USAGE_PLANE_B_TABLE) {
        return mega_memview_argb(240, 70, 70);
    }
    if (usage & MEGA_MEMVIEW_VRAM_USAGE_WINDOW_TABLE) {
        return mega_memview_argb(110, 245, 190);
    }
    return 0u;
}

static void
mega_memview_renderVramUsageOverlay(mega_memview_state_t *ui,
                                      e9ui_context_t *ctx,
                                      const uint8_t *tileUsage,
                                      int visibleRows,
                                      int tilesPerRow,
                                      int pixelPx,
                                      int tileStridePx,
                                      int rowStridePx,
                                      int bitViewW,
                                      int rowAreaY,
                                      int bitAreaX)
{
    SDL_BlendMode previousBlendMode = SDL_BLENDMODE_NONE;
    Uint8 previousR = 0;
    Uint8 previousG = 0;
    Uint8 previousB = 0;
    Uint8 previousA = 0;
    int tileW = MEGA_MEMVIEW_TILE_W * pixelPx;
    int tileH = MEGA_MEMVIEW_TILE_H * pixelPx;
    int clipX0 = bitAreaX;
    int clipX1 = bitAreaX + bitViewW;

    if (!ui || !ctx || !ctx->renderer || !tileUsage || visibleRows <= 0 || tilesPerRow <= 0 ||
        tileW <= 0 || tileH <= 0 || bitViewW <= 0) {
        return;
    }

    SDL_GetRenderDrawBlendMode(ctx->renderer, &previousBlendMode);
    SDL_GetRenderDrawColor(ctx->renderer, &previousR, &previousG, &previousB, &previousA);
    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);

    for (int row = 0; row < visibleRows; ++row) {
        for (int tile = 0; tile < tilesPerRow; ++tile) {
            size_t tileIndex = (size_t)row * (size_t)tilesPerRow + (size_t)tile;
            uint32_t usageColor = 0u;
            SDL_Rect rect;

            if (!tileUsage[tileIndex]) {
                continue;
            }

            usageColor = mega_memview_vramUsageColor(tileUsage[tileIndex]);
            rect.x = bitAreaX - ui->scrollX + tile * tileStridePx;
            rect.y = rowAreaY + row * rowStridePx;
            rect.w = tileW;
            rect.h = tileH;
            if (rect.x < clipX0) {
                int delta = clipX0 - rect.x;

                rect.x = clipX0;
                rect.w -= delta;
            }
            if (rect.x + rect.w > clipX1) {
                rect.w = clipX1 - rect.x;
            }
            if (rect.w <= 0 || rect.h <= 0) {
                continue;
            }
            SDL_SetRenderDrawColor(ctx->renderer,
                                   (Uint8)((usageColor >> 16) & 0xffu),
                                   (Uint8)((usageColor >> 8) & 0xffu),
                                   (Uint8)(usageColor & 0xffu),
                                   118);
            SDL_RenderFillRect(ctx->renderer, &rect);
        }
    }

    SDL_SetRenderDrawBlendMode(ctx->renderer, previousBlendMode);
    SDL_SetRenderDrawColor(ctx->renderer, previousR, previousG, previousB, previousA);
}

int
mega_memview_ensureTexture(SDL_Renderer *renderer,
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

uint8_t
mega_memview_readVramPixel(const uint8_t *data, size_t sizeBytes, uint32_t tileBaseAddr, unsigned x, unsigned y)
{
    uint32_t pixelAddr = 0u;
    uint8_t value = 0u;

    if (!data || tileBaseAddr + MEGA_MEMVIEW_TILE_BYTES > sizeBytes ||
        x >= MEGA_MEMVIEW_TILE_W || y >= MEGA_MEMVIEW_TILE_H) {
        return 0u;
    }
    pixelAddr = tileBaseAddr + y * 4u + x / 2u;
    if (pixelAddr >= sizeBytes) {
        return 0u;
    }
    value = data[pixelAddr];
    value = (x & 1u) ? (uint8_t)(value & 0x0fu) : (uint8_t)(value >> 4);
    return value;
}

static uint32_t
mega_memview_cramColor(const uint16_t *cram, size_t cramWords, unsigned paletteBank, unsigned colorIndex)
{
    size_t offset = (size_t)(paletteBank & 3u) * 16u + (size_t)(colorIndex & 15u);
    uint16_t color = 0u;
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;

    if (!cram || offset >= cramWords) {
        return mega_memview_amberColor(colorIndex);
    }
    color = cram[offset];
    r = (uint8_t)((((color >> 1) & 0x07u) * 255u) / 7u);
    g = (uint8_t)((((color >> 5) & 0x07u) * 255u) / 7u);
    b = (uint8_t)((((color >> 9) & 0x07u) * 255u) / 7u);
    return mega_memview_argb(r, g, b);
}

static uint16_t
mega_memview_readVramWord(const uint8_t *vram, size_t vramSize, uint32_t wordOffset)
{
    size_t byteOffset = (size_t)wordOffset * 2u;

    if (!vram || byteOffset + 1u >= vramSize) {
        return 0u;
    }
    return (uint16_t)vram[byteOffset] | (uint16_t)((uint16_t)vram[byteOffset + 1u] << 8);
}

int
mega_memview_vdpPlaneTable(const e9k_debug_mega_sprite_state_t *videoState,
                             int plane,
                             mega_memview_plane_table_t *out)
{
    static const uint8_t shift[4] = { 5u, 6u, 5u, 7u };
    uint8_t reg16 = 0u;
    unsigned widthCode = 0u;
    unsigned heightCode = 0u;

    if (!videoState || !out) {
        return 0;
    }
    reg16 = videoState->vdpRegs[16];
    widthCode = (unsigned)(reg16 & 3u);
    heightCode = (unsigned)((reg16 >> 4) & 3u);
    out->cols = 1u << shift[widthCode];
    out->rows = ((uint32_t)heightCode << 5u) | 0x1fu;
    if (widthCode == 1u) {
        out->rows &= 0x3fu;
    } else if (widthCode > 1u) {
        out->rows = 0x1fu;
    }
    out->rows++;
    out->wordOffset = plane == 0 ?
        (uint32_t)(videoState->vdpRegs[2] & 0x38u) << 9u :
        (uint32_t)(videoState->vdpRegs[4] & 0x07u) << 12u;
    return 1;
}

static int
mega_memview_visibleTileToIndex(uint32_t tileNum,
                                  uint32_t visibleBaseTile,
                                  uint32_t visibleEndTile,
                                  size_t visibleTileCount,
                                  uint32_t vramTiles,
                                  int wraps,
                                  size_t *outTileIndex)
{
    size_t tileIndex = 0u;
    int inVisibleRange = 0;

    if (!outTileIndex) {
        return 0;
    }
    if (!wraps) {
        if (tileNum >= visibleBaseTile && tileNum < visibleBaseTile + (uint32_t)visibleTileCount) {
            tileIndex = (size_t)(tileNum - visibleBaseTile);
            inVisibleRange = 1;
        }
    } else {
        if (tileNum >= visibleBaseTile) {
            tileIndex = (size_t)(tileNum - visibleBaseTile);
            inVisibleRange = 1;
        } else if (tileNum < visibleEndTile) {
            tileIndex = (size_t)(vramTiles - visibleBaseTile) + (size_t)tileNum;
            inVisibleRange = 1;
        }
    }
    if (!inVisibleRange || tileIndex >= visibleTileCount) {
        return 0;
    }
    *outTileIndex = tileIndex;
    return 1;
}

static void
mega_memview_setVisibleTilePalette(uint32_t tileNum,
                                     uint8_t paletteBank,
                                     uint32_t visibleBaseTile,
                                     uint32_t visibleEndTile,
                                     size_t visibleTileCount,
                                     uint32_t vramTiles,
                                     int wraps,
                                     uint8_t *tilePaletteBanks,
                                     uint8_t *tileHasPalette)
{
    size_t tileIndex = 0u;

    if (!tilePaletteBanks || !tileHasPalette || vramTiles == 0u) {
        return;
    }
    tileNum %= vramTiles;
    if (mega_memview_visibleTileToIndex(tileNum,
                                          visibleBaseTile,
                                          visibleEndTile,
                                          visibleTileCount,
                                          vramTiles,
                                          wraps,
                                          &tileIndex) &&
        !tileHasPalette[tileIndex]) {
        tilePaletteBanks[tileIndex] = (uint8_t)(paletteBank & 3u);
        tileHasPalette[tileIndex] = 1u;
    }
}

static void
mega_memview_markVisibleVramByteRange(uint32_t baseAddr,
                                        uint32_t sizeBytes,
                                        uint8_t usage,
                                        uint32_t visibleBaseTile,
                                        size_t visibleTileCount,
                                        uint8_t *tileUsage)
{
    uint32_t startTile = 0u;
    uint32_t endTile = 0u;

    if (sizeBytes == 0u || !tileUsage || visibleTileCount == 0u) {
        return;
    }
    startTile = baseAddr / MEGA_MEMVIEW_TILE_BYTES;
    endTile = (baseAddr + sizeBytes - 1u) / MEGA_MEMVIEW_TILE_BYTES;
    for (uint32_t tileNum = startTile; tileNum <= endTile; ++tileNum) {
        size_t tileIndex = 0u;

        if (tileNum < visibleBaseTile) {
            continue;
        }
        tileIndex = (size_t)(tileNum - visibleBaseTile);
        if (tileIndex >= visibleTileCount) {
            continue;
        }
        tileUsage[tileIndex] |= usage;
    }
}

static uint32_t
mega_memview_vdpSpriteAttributeBase(const e9k_debug_mega_sprite_state_t *videoState)
{
    uint32_t table = 0u;

    if (!videoState) {
        return 0u;
    }
    table = (uint32_t)(videoState->vdpRegs[5] & 0x7fu);
    if (videoState->vdpRegs[12] & 1u) {
        table &= 0x7eu;
    }
    return table << 9u;
}

static uint32_t
mega_memview_vdpSpriteAttributeSize(const e9k_debug_mega_sprite_state_t *videoState)
{
    int spriteMax = 0;

    if (!videoState) {
        return 0u;
    }
    spriteMax = videoState->frameSpriteMax;
    if (spriteMax <= 0) {
        spriteMax = videoState->screenW >= 320 ? 80 : 64;
    }
    if (spriteMax > E9K_DEBUG_MEGA_MAX_FRAME_SPRITES) {
        spriteMax = E9K_DEBUG_MEGA_MAX_FRAME_SPRITES;
    }
    return (uint32_t)spriteMax * 8u;
}

static uint32_t
mega_memview_vdpHScrollBase(const e9k_debug_mega_sprite_state_t *videoState)
{
    if (!videoState) {
        return 0u;
    }
    return (uint32_t)(videoState->vdpRegs[13] & 0x3fu) << 10u;
}

static uint32_t
mega_memview_vdpPlaneNameTableBase(const e9k_debug_mega_sprite_state_t *videoState, int plane)
{
    mega_memview_plane_table_t table;

    if (!mega_memview_vdpPlaneTable(videoState, plane, &table)) {
        return 0u;
    }
    return table.wordOffset * 2u;
}

static uint32_t
mega_memview_vdpPlaneNameTableSize(const e9k_debug_mega_sprite_state_t *videoState)
{
    mega_memview_plane_table_t table;

    if (!mega_memview_vdpPlaneTable(videoState, 0, &table)) {
        return 0u;
    }
    return table.cols * table.rows * 2u;
}

static uint32_t
mega_memview_vdpWindowNameTableBase(const e9k_debug_mega_sprite_state_t *videoState)
{
    uint32_t mask = 0x3eu;

    if (!videoState) {
        return 0u;
    }
    if (videoState->vdpRegs[12] & 1u) {
        mask = 0x3cu;
    }
    return (uint32_t)(videoState->vdpRegs[3] & mask) << 10u;
}

static uint32_t
mega_memview_vdpWindowNameTableSize(const e9k_debug_mega_sprite_state_t *videoState)
{
    int lineCount = 0;
    uint32_t rowBytes = 0u;
    uint32_t rowHeight = 8u;
    uint32_t rows = 0u;

    if (!videoState) {
        return 0u;
    }
    lineCount = videoState->lineCount > 0 ? videoState->lineCount : videoState->screenH;
    if (lineCount <= 0) {
        lineCount = 224;
    }
    rowBytes = (videoState->vdpRegs[12] & 1u) ? 128u : 64u;
    if ((videoState->vdpRegs[12] & 6u) == 6u) {
        rowHeight = 16u;
    }
    rows = ((uint32_t)lineCount + rowHeight - 1u) / rowHeight;
    if (rows > 32u) {
        rows = 32u;
    }
    return rowBytes * rows;
}

static void
mega_memview_markVisibleVdpHScrollTable(const e9k_debug_mega_sprite_state_t *videoState,
                                          uint32_t visibleBaseTile,
                                          size_t visibleTileCount,
                                          uint8_t *tileUsage)
{
    int lineCount = 0;
    unsigned mode = 0u;
    uint32_t baseAddr = 0u;

    if (!videoState || !tileUsage) {
        return;
    }
    lineCount = videoState->lineCount > 0 ? videoState->lineCount : videoState->screenH;
    if (lineCount <= 0) {
        lineCount = 224;
    }
    baseAddr = mega_memview_vdpHScrollBase(videoState);
    mode = (unsigned)(videoState->vdpRegs[11] & 0x03u);
    if (mode == 0u) {
        mega_memview_markVisibleVramByteRange(baseAddr,
                                                4u,
                                                MEGA_MEMVIEW_VRAM_USAGE_HSCROLL,
                                                visibleBaseTile,
                                                visibleTileCount,
                                                tileUsage);
    } else if (mode == 1u) {
        mega_memview_markVisibleVramByteRange(baseAddr,
                                                32u,
                                                MEGA_MEMVIEW_VRAM_USAGE_HSCROLL,
                                                visibleBaseTile,
                                                visibleTileCount,
                                                tileUsage);
    } else if (mode == 2u) {
        int groups = (lineCount + 7) / 8;

        for (int group = 0; group < groups; ++group) {
            mega_memview_markVisibleVramByteRange(baseAddr + (uint32_t)group * 32u,
                                                    4u,
                                                    MEGA_MEMVIEW_VRAM_USAGE_HSCROLL,
                                                    visibleBaseTile,
                                                    visibleTileCount,
                                                    tileUsage);
        }
    } else {
        mega_memview_markVisibleVramByteRange(baseAddr,
                                                (uint32_t)lineCount * 4u,
                                                MEGA_MEMVIEW_VRAM_USAGE_HSCROLL,
                                                visibleBaseTile,
                                                visibleTileCount,
                                                tileUsage);
    }
}

static void
mega_memview_buildVisibleVramUsageMap(const mega_memview_state_t *ui,
                                        const e9k_debug_mega_sprite_state_t *videoState,
                                        uint32_t visibleBaseTile,
                                        size_t visibleTileCount,
                                        uint8_t *tileUsage)
{
    if (!ui || !videoState || !tileUsage || visibleTileCount == 0u) {
        return;
    }
    if (ui->colorSourceSprites) {
        mega_memview_markVisibleVramByteRange(mega_memview_vdpSpriteAttributeBase(videoState),
                                                mega_memview_vdpSpriteAttributeSize(videoState),
                                                MEGA_MEMVIEW_VRAM_USAGE_SAT,
                                                visibleBaseTile,
                                                visibleTileCount,
                                                tileUsage);
    }
    if (ui->colorSourceHScroll) {
        mega_memview_markVisibleVdpHScrollTable(videoState,
                                                  visibleBaseTile,
                                                  visibleTileCount,
                                                  tileUsage);
    }
    if (ui->colorSourcePlaneA) {
        mega_memview_markVisibleVramByteRange(mega_memview_vdpPlaneNameTableBase(videoState, 0),
                                                mega_memview_vdpPlaneNameTableSize(videoState),
                                                MEGA_MEMVIEW_VRAM_USAGE_PLANE_A_TABLE,
                                                visibleBaseTile,
                                                visibleTileCount,
                                                tileUsage);
        mega_memview_markVisibleVramByteRange(mega_memview_vdpWindowNameTableBase(videoState),
                                                mega_memview_vdpWindowNameTableSize(videoState),
                                                MEGA_MEMVIEW_VRAM_USAGE_WINDOW_TABLE,
                                                visibleBaseTile,
                                                visibleTileCount,
                                                tileUsage);
    }
    if (ui->colorSourcePlaneB) {
        mega_memview_markVisibleVramByteRange(mega_memview_vdpPlaneNameTableBase(videoState, 1),
                                                mega_memview_vdpPlaneNameTableSize(videoState),
                                                MEGA_MEMVIEW_VRAM_USAGE_PLANE_B_TABLE,
                                                visibleBaseTile,
                                                visibleTileCount,
                                                tileUsage);
    }
}

static void
mega_memview_buildVisiblePlanePaletteMap(const e9k_debug_mega_sprite_state_t *videoState,
                                           int plane,
                                           uint32_t visibleBaseTile,
                                           uint32_t visibleEndTile,
                                           size_t visibleTileCount,
                                           uint32_t vramTiles,
                                           int wraps,
                                           uint8_t *tilePaletteBanks,
                                           uint8_t *tileHasPalette)
{
    size_t vramSize = 0u;
    const uint8_t *vram = NULL;
    mega_memview_plane_table_t table;

    if (!videoState || vramTiles == 0u || !tilePaletteBanks || !tileHasPalette) {
        return;
    }
    vram = (const uint8_t *)libretro_host_getMemory(RETRO_MEMORY_VIDEO_RAM, &vramSize);
    if (!vram) {
        return;
    }
    if (!mega_memview_vdpPlaneTable(videoState, plane, &table)) {
        return;
    }
    for (uint32_t row = 0u; row < table.rows; ++row) {
        for (uint32_t col = 0u; col < table.cols; ++col) {
            uint16_t name = mega_memview_readVramWord(vram, vramSize, table.wordOffset + row * table.cols + col);
            uint32_t tileNum = (uint32_t)(name & 0x07ffu);
            uint8_t paletteBank = (uint8_t)((name >> 13) & 0x03u);

            mega_memview_setVisibleTilePalette(tileNum,
                                                 paletteBank,
                                                 visibleBaseTile,
                                                 visibleEndTile,
                                                 visibleTileCount,
                                                 vramTiles,
                                                 wraps,
                                                 tilePaletteBanks,
                                                 tileHasPalette);
        }
    }
}

static void
mega_memview_buildVisibleTilePaletteMap(const mega_memview_state_t *ui,
                                          const uint32_t *visibleTileNums,
                                          size_t visibleTileCount,
                                          uint8_t *tilePaletteBanks,
                                          uint8_t *tileHasPalette,
                                          uint8_t *tileUsage)
{
    e9k_debug_mega_sprite_state_t spriteState;
    uint32_t vramTiles = 0u;
    uint32_t visibleBaseTile = 0u;
    uint32_t visibleEndTile = 0u;
    int wraps = 0;

    if (!ui || !visibleTileNums || !tilePaletteBanks || !tileHasPalette || visibleTileCount == 0u) {
        return;
    }
    if (!libretro_host_megadrive_getSpriteState(&spriteState)) {
        return;
    }
    vramTiles = mega_memview_vramSize() / MEGA_MEMVIEW_TILE_BYTES;
    if (vramTiles == 0u) {
        return;
    }
    visibleBaseTile = visibleTileNums[0];
    mega_memview_buildVisibleVramUsageMap(ui,
                                            &spriteState,
                                            visibleBaseTile,
                                            visibleTileCount,
                                            tileUsage);
    visibleEndTile = (uint32_t)((visibleBaseTile + (uint32_t)visibleTileCount) % vramTiles);
    wraps = visibleTileCount > (size_t)(vramTiles - visibleBaseTile) ? 1 : 0;
    if (ui->colorSourceSprites) {
        for (int i = 0; i < spriteState.spriteEntryCount && i < E9K_DEBUG_MEGA_MAX_FRAME_SPRITES; ++i) {
            const e9k_debug_mega_sprite_entry_t *entry = &spriteState.spriteEntries[i];
            uint32_t widthTiles = entry->widthTiles ? entry->widthTiles : 1u;
            uint32_t heightTiles = entry->heightTiles ? entry->heightTiles : 1u;

            if (!(entry->flags & (E9K_DEBUG_MEGA_SPRITEFLAG_VISIBLE | E9K_DEBUG_MEGA_SPRITEFLAG_RENDERED))) {
                continue;
            }
            for (uint32_t tx = 0u; tx < widthTiles; ++tx) {
                for (uint32_t ty = 0u; ty < heightTiles; ++ty) {
                    uint32_t spriteTileNum = (uint32_t)entry->tileIndex + tx * heightTiles + ty;

                    mega_memview_setVisibleTilePalette(spriteTileNum,
                                                         entry->palette,
                                                         visibleBaseTile,
                                                         visibleEndTile,
                                                         visibleTileCount,
                                                         vramTiles,
                                                         wraps,
                                                         tilePaletteBanks,
                                                         tileHasPalette);
                    }
                }
            }
    }
    if (ui->colorSourcePlaneA) {
        mega_memview_buildVisiblePlanePaletteMap(&spriteState,
                                                   0,
                                                   visibleBaseTile,
                                                   visibleEndTile,
                                                   visibleTileCount,
                                                   vramTiles,
                                                   wraps,
                                                   tilePaletteBanks,
                                                   tileHasPalette);
    }
    if (ui->colorSourcePlaneB) {
        mega_memview_buildVisiblePlanePaletteMap(&spriteState,
                                                   1,
                                                   visibleBaseTile,
                                                   visibleEndTile,
                                                   visibleTileCount,
                                                   vramTiles,
                                                   wraps,
                                                   tilePaletteBanks,
                                                   tileHasPalette);
    }
}

static uint64_t
mega_memview_vramPaletteToken(const uint16_t *cram,
                                size_t cramWords,
                                const uint8_t *tilePaletteBanks,
                                const uint8_t *tileHasPalette,
                                size_t visibleTileCount,
                                int haveLivePalette)
{
    uint64_t hash = MEGA_MEMVIEW_FNV1A64_OFFSET_BASIS;

    hash ^= (uint64_t)haveLivePalette;
    hash *= MEGA_MEMVIEW_FNV1A64_PRIME;
    hash ^= (uint64_t)cramWords;
    hash *= MEGA_MEMVIEW_FNV1A64_PRIME;
    for (size_t tileIndex = 0u; tileIndex < visibleTileCount; ++tileIndex) {
        uint8_t hasPalette = tileHasPalette ? tileHasPalette[tileIndex] : 0u;
        uint8_t paletteBank = hasPalette && tilePaletteBanks ? tilePaletteBanks[tileIndex] : 0u;

        hash ^= (uint64_t)hasPalette;
        hash *= MEGA_MEMVIEW_FNV1A64_PRIME;
        hash ^= (uint64_t)paletteBank;
        hash *= MEGA_MEMVIEW_FNV1A64_PRIME;
        if (!haveLivePalette || !cram || !hasPalette) {
            continue;
        }
        for (unsigned colorIndex = 0u; colorIndex < 16u; ++colorIndex) {
            size_t offset = (size_t)(paletteBank & 3u) * 16u + (size_t)colorIndex;

            if (offset < cramWords) {
                hash ^= (uint64_t)cram[offset];
                hash *= MEGA_MEMVIEW_FNV1A64_PRIME;
            }
        }
    }
    return hash;
}

static uint64_t
mega_memview_vramContentToken(uint32_t baseAddr, size_t sizeBytes)
{
    size_t vramSize = 0u;
    const uint8_t *vram = NULL;
    size_t readableBytes = sizeBytes;
    uint64_t hash = MEGA_MEMVIEW_FNV1A64_OFFSET_BASIS;

    vram = (const uint8_t *)libretro_host_getMemory(RETRO_MEMORY_VIDEO_RAM, &vramSize);
    hash ^= (uint64_t)vramSize;
    hash *= MEGA_MEMVIEW_FNV1A64_PRIME;
    hash ^= (uint64_t)baseAddr;
    hash *= MEGA_MEMVIEW_FNV1A64_PRIME;
    hash ^= (uint64_t)sizeBytes;
    hash *= MEGA_MEMVIEW_FNV1A64_PRIME;
    if (!vram || baseAddr >= vramSize) {
        return hash;
    }
    if ((uint64_t)baseAddr + (uint64_t)readableBytes > (uint64_t)vramSize) {
        readableBytes = vramSize - (size_t)baseAddr;
    }
    hash ^= (uint64_t)readableBytes;
    hash *= MEGA_MEMVIEW_FNV1A64_PRIME;
    for (size_t i = 0u; i < readableBytes; ++i) {
        hash ^= (uint64_t)vram[(size_t)baseAddr + i];
        hash *= MEGA_MEMVIEW_FNV1A64_PRIME;
    }
    return hash;
}

int
mega_memview_measureAddressGutterPx(const e9ui_context_t *ctx, TTF_Font *font)
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
mega_memview_leftGutterPx(const mega_memview_state_t *ui, const e9ui_context_t *ctx, TTF_Font *font)
{
    int left = 0;

    if (ui->showAddressColumn) {
        left += mega_memview_measureAddressGutterPx(ctx, font);
    }
    if (ui->showOverviewColumn && mega_memview_overviewRangeCount(ui) > 0) {
        if (left > 0) {
            left += e9ui_scale_px(ctx, MEGA_MEMVIEW_GUTTER_GAP_PX);
        }
        left += e9ui_scale_px(ctx, MEGA_MEMVIEW_OVERVIEW_GUTTER_PX);
    }
    if (left > 0) {
        left += e9ui_scale_px(ctx, MEGA_MEMVIEW_GUTTER_GAP_PX);
    }
    return left;
}

static e9ui_rect_t
mega_memview_hscrollBounds(const mega_memview_state_t *ui, const e9ui_component_t *self)
{
    e9ui_rect_t bounds = { 0, 0, 0, 0 };
    int rightGutter = 0;

    if (!self) {
        return bounds;
    }
    rightGutter = mega_memview_stepButtonsGutterWidth(&ui->ctx, (e9ui_component_t *)self);
    bounds.x = self->bounds.x + mega_memview_leftGutterPx(ui, &ui->ctx, ui->ctx.font);
    bounds.y = self->bounds.y + self->bounds.h - e9ui_scale_px(&ui->ctx, 12);
    bounds.w = self->bounds.w - (bounds.x - self->bounds.x) - rightGutter;
    bounds.h = e9ui_scale_px(&ui->ctx, 12);
    if (bounds.w < 1) {
        bounds.w = 1;
    }
    return bounds;
}

static void
mega_memview_drawAddressLabel(const mega_memview_state_t *ui,
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

static void
mega_memview_scrollRows(mega_memview_state_t *ui, const e9ui_rect_t *bounds, int rows)
{
    int64_t delta = 0;
    uint32_t baseAddr = 0u;

    if (!bounds || rows == 0) {
        return;
    }
    if (ui->mode == mega_memview_mode_roms) {
        ui->romsScrollY += rows * e9ui_scale_px(&ui->ctx, 48);
        if (ui->romsScrollY < 0) {
            ui->romsScrollY = 0;
        }
        return;
    }
    delta = (int64_t)rows * (int64_t)mega_memview_currentRowBytes(ui);
    baseAddr = mega_memview_currentBaseAddr(ui);
    if (delta < 0 && (uint64_t)(-delta) > baseAddr) {
        baseAddr = 0u;
    } else {
        baseAddr = (uint32_t)((int64_t)baseAddr + delta);
    }
    baseAddr = mega_memview_clampBaseForView(ui, bounds, baseAddr);
    mega_memview_setCurrentBaseAddrSaved(ui, baseAddr);
    mega_memview_syncTextboxesFromState(ui);
    config_saveConfig();
}

static int
mega_memview_stepButtonsOnAction(void *user, e9ui_step_buttons_action_t action)
{
    mega_memview_step_buttons_action_ctx_t *actionCtx = (mega_memview_step_buttons_action_ctx_t *)user;
    int rows = 0;
    int pageRows = 0;

    if (!actionCtx || !actionCtx->canvas) {
        return 0;
    }
    pageRows = mega_memview_canvasVisibleRows(actionCtx->ui, &actionCtx->canvas->bounds) / 4;
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
    mega_memview_scrollRows(actionCtx->ui, &actionCtx->canvas->bounds, rows);
    return 1;
}

static int
mega_memview_toolbarItemPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    mega_memview_toolbar_item_state_t *state = NULL;

    (void)availW;
    if (!self || !ctx || !self->state) {
        return 0;
    }
    state = (mega_memview_toolbar_item_state_t *)self->state;
    if (!state->child || !state->child->preferredHeight) {
        return 0;
    }
    return state->child->preferredHeight(state->child, ctx, state->widthPx);
}

static void
mega_memview_toolbarItemLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    mega_memview_toolbar_item_state_t *state = NULL;

    if (!self || !ctx || !self->state) {
        return;
    }
    self->bounds = bounds;
    state = (mega_memview_toolbar_item_state_t *)self->state;
    if (state->child && state->child->layout) {
        state->child->layout(state->child, ctx, bounds);
    }
}

static void
mega_memview_toolbarItemRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    mega_memview_toolbar_item_state_t *state = NULL;

    if (!self || !ctx || !self->state) {
        return;
    }
    state = (mega_memview_toolbar_item_state_t *)self->state;
    if (state->child && state->child->render) {
        state->child->render(state->child, ctx);
    }
}

static void
mega_memview_toolbarItemDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    alloc_free(self->state);
    self->state = NULL;
}

static e9ui_component_t *
mega_memview_makeToolbarItem(e9ui_component_t *child, int widthPx)
{
    e9ui_component_t *item = NULL;
    mega_memview_toolbar_item_state_t *state = NULL;

    if (!child || widthPx <= 0) {
        return NULL;
    }
    item = (e9ui_component_t *)alloc_calloc(1, sizeof(*item));
    state = (mega_memview_toolbar_item_state_t *)alloc_calloc(1, sizeof(*state));
    if (!item || !state) {
        alloc_free(item);
        alloc_free(state);
        return NULL;
    }
    state->child = child;
    state->widthPx = widthPx;
    item->name = "mega_memview_toolbar_item";
    item->state = state;
    item->preferredHeight = mega_memview_toolbarItemPreferredHeight;
    item->layout = mega_memview_toolbarItemLayout;
    item->render = mega_memview_toolbarItemRender;
    item->dtor = mega_memview_toolbarItemDtor;
    e9ui_child_add(item, child, NULL);
    return item;
}

static int
mega_memview_toolbarItemWidth(const e9ui_component_t *item)
{
    const mega_memview_toolbar_item_state_t *state = NULL;

    if (!item || !item->state) {
        return 0;
    }
    state = (const mega_memview_toolbar_item_state_t *)item->state;
    return state->widthPx > 0 ? state->widthPx : 0;
}

static int
mega_memview_toolbarWrapPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    mega_memview_toolbar_wrap_state_t *state = NULL;
    int pad = 0;
    int gap = 0;
    int x = 0;
    int y = 0;
    int rowH = 0;
    int rightLimit = 0;

    if (!self || !ctx || !self->state) {
        return 0;
    }
    state = (mega_memview_toolbar_wrap_state_t *)self->state;
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
        childW = mega_memview_toolbarItemWidth(child);
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
mega_memview_toolbarWrapLayoutRow(e9ui_context_t *ctx,
                                    e9ui_component_t **children,
                                    const int *childWidths,
                                    const int *childHeights,
                                    int childCount,
                                    int rowX,
                                    int rowY,
                                    int rowH,
                                    int gap)
{
    int x = rowX;

    if (!ctx || !children || !childWidths || !childHeights || childCount <= 0) {
        return;
    }
    for (int i = 0; i < childCount; ++i) {
        e9ui_component_t *child = children[i];
        int childH = childHeights[i];
        int childY = rowY;

        if (!child) {
            continue;
        }
        if (childH < rowH) {
            childY = rowY + (rowH - childH) / 2;
        }
        if (child->layout) {
            child->layout(child, ctx, (e9ui_rect_t){ x, childY, childWidths[i], childH });
        }
        x += childWidths[i] + gap;
    }
}

static void
mega_memview_toolbarWrapLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    mega_memview_toolbar_wrap_state_t *state = NULL;
    e9ui_component_t *rowChildren[MEGA_MEMVIEW_TOOLBAR_MAX_ROW_ITEMS];
    int rowWidths[MEGA_MEMVIEW_TOOLBAR_MAX_ROW_ITEMS];
    int rowHeights[MEGA_MEMVIEW_TOOLBAR_MAX_ROW_ITEMS];
    int pad = 0;
    int gap = 0;
    int x = 0;
    int y = 0;
    int rowH = 0;
    int rowX = 0;
    int rowCount = 0;
    int rightLimit = 0;

    if (!self || !ctx || !self->state) {
        return;
    }
    self->bounds = bounds;
    state = (mega_memview_toolbar_wrap_state_t *)self->state;
    pad = e9ui_scale_px(ctx, state->padPx);
    gap = e9ui_scale_px(ctx, state->gapPx);
    x = bounds.x + pad;
    rowX = x;
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
        childW = mega_memview_toolbarItemWidth(child);
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
            mega_memview_toolbarWrapLayoutRow(ctx,
                                                rowChildren,
                                                rowWidths,
                                                rowHeights,
                                                rowCount,
                                                rowX,
                                                y,
                                                rowH,
                                                gap);
            y += rowH + gap;
            x = bounds.x + pad;
            rowX = x;
            rowH = 0;
            rowCount = 0;
        }
        if (childH > rowH) {
            rowH = childH;
        }
        if (rowCount < MEGA_MEMVIEW_TOOLBAR_MAX_ROW_ITEMS) {
            rowChildren[rowCount] = child;
            rowWidths[rowCount] = childW;
            rowHeights[rowCount] = childH;
            rowCount++;
        }
        x += childW + gap;
    }
    mega_memview_toolbarWrapLayoutRow(ctx,
                                        rowChildren,
                                        rowWidths,
                                        rowHeights,
                                        rowCount,
                                        rowX,
                                        y,
                                        rowH,
                                        gap);
}

static void
mega_memview_toolbarWrapRender(e9ui_component_t *self, e9ui_context_t *ctx)
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
mega_memview_toolbarWrapDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    alloc_free(self->state);
    self->state = NULL;
}

static e9ui_component_t *
mega_memview_makeToolbarWrap(void)
{
    e9ui_component_t *comp = NULL;
    mega_memview_toolbar_wrap_state_t *state = NULL;

    comp = (e9ui_component_t *)alloc_calloc(1, sizeof(*comp));
    state = (mega_memview_toolbar_wrap_state_t *)alloc_calloc(1, sizeof(*state));
    if (!comp || !state) {
        alloc_free(comp);
        alloc_free(state);
        return NULL;
    }
    state->padPx = 0;
    state->gapPx = 12;
    comp->name = "mega_memview_toolbar_wrap";
    comp->state = state;
    comp->preferredHeight = mega_memview_toolbarWrapPreferredHeight;
    comp->layout = mega_memview_toolbarWrapLayout;
    comp->render = mega_memview_toolbarWrapRender;
    comp->dtor = mega_memview_toolbarWrapDtor;
    return comp;
}

static void
mega_memview_seekBarLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
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
mega_memview_initSeekBar(e9ui_component_t *seekBar,
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
    seekBar->layout = mega_memview_seekBarLayout;
    seekBar->handleEvent = handleEvent;
    e9ui_seek_bar_setMargins(seekBar, 0, 0, 0);
    e9ui_seek_bar_setHeight(seekBar, 14);
    e9ui_seek_bar_setHoverMargin(seekBar, 6);
    e9ui_seek_bar_setCallback(seekBar, onChanged, user);
    e9ui_seek_bar_setTooltipCallback(seekBar, tooltip, user);
}

static void
mega_memview_switchMode(mega_memview_state_t *ui, mega_memview_mode_t mode)
{
    if (ui->mode == mode) {
        return;
    }
    ui->mode = mode;
    ui->modeHasSaved = 1;
    ui->scrollX = 0;
    ui->mainVramCacheValid = 0;
    ui->followPrevSpriteEntryCount = 0u;
    mega_memview_initOverviewRanges(ui);
    mega_memview_syncTextboxesFromState(ui);
    mega_memview_updateModeButtons(ui);
    config_saveConfig();
}

static void
mega_memview_setMode(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    mega_memview_switchMode(&mega_memview_stateSingleton, *(const mega_memview_mode_t *)user);
}

static void
mega_memview_onCheckboxChanged(e9ui_component_t *self, e9ui_context_t *ctx, int checked, void *user)
{
    mega_memview_checkbox_binding_t *binding = (mega_memview_checkbox_binding_t *)user;

    (void)self;
    (void)ctx;
    *binding->value = checked ? 1 : 0;
    *binding->hasSaved = 1;
    if (binding->resetFollow) {
        mega_memview_stateSingleton.followPrevSpriteEntryCount = 0u;
    }
    config_saveConfig();
}

static void
mega_memview_onAddressSubmit(e9ui_context_t *ctx, void *user)
{
    mega_memview_state_t *ui = (mega_memview_state_t *)user;
    const char *text = NULL;
    unsigned long long parsed = 0u;
    char *end = NULL;

    (void)ctx;
    if (!ui->addressBox) {
        return;
    }
    text = e9ui_textbox_getText(ui->addressBox);
    if (!mega_memview_parseU64SmartHex(text, &parsed, &end) || !end || *end != '\0') {
        mega_memview_syncTextboxesFromState(ui);
        return;
    }
    if (ui->mode == mega_memview_mode_roms) {
        mega_memview_syncTextboxesFromState(ui);
        return;
    }
    if (ui->mode == mega_memview_mode_vram) {
        mega_memview_setView(ui, mega_memview_clampVramBaseAddr((uint32_t)parsed), ui->vramTilesPerRow * MEGA_MEMVIEW_TILE_BYTES, 1);
    } else if (ui->mode == mega_memview_mode_zram) {
        mega_memview_setView(ui, mega_memview_clampZramBaseAddr((uint32_t)parsed), ui->ramRowBytes, 1);
    } else {
        mega_memview_setView(ui, mega_memview_clampRamBaseAddr((uint32_t)parsed), ui->ramRowBytes, 1);
    }
}

static void
mega_memview_setWidthValue(mega_memview_state_t *ui, uint32_t value)
{
    if (ui->mode == mega_memview_mode_vram) {
        ui->vramTilesPerRow = mega_memview_clampVramTilesPerRow(value);
        ui->vramTilesPerRowHasSaved = 1;
        ui->vramBaseAddr = mega_memview_alignVramBaseAddrToRow(ui, ui->vramBaseAddr);
        ui->vramBaseAddrHasSaved = 1;
    } else {
        ui->ramRowBytes = mega_memview_clampRamRowBytes(value);
        ui->ramRowBytesHasSaved = 1;
    }
    ui->scrollX = 0;
    mega_memview_syncTextboxesFromState(ui);
    config_saveConfig();
}

static void
mega_memview_onWidthSubmit(e9ui_context_t *ctx, void *user)
{
    mega_memview_state_t *ui = (mega_memview_state_t *)user;
    const char *text = NULL;
    unsigned long long parsed = 0u;
    char *end = NULL;

    (void)ctx;
    if (!ui->widthBox) {
        return;
    }
    text = e9ui_textbox_getText(ui->widthBox);
    if (!mega_memview_parseU64SmartHex(text, &parsed, &end) || !end || *end != '\0' || parsed == 0u) {
        mega_memview_syncTextboxesFromState(ui);
        return;
    }
    mega_memview_setWidthValue(ui, (uint32_t)parsed);
}

static void
mega_memview_widthSeekTooltip(float percent, char *out, size_t cap, void *user)
{
    mega_memview_state_t *ui = (mega_memview_state_t *)user;
    uint32_t value = 1u;

    if (!out || cap == 0u) {
        return;
    }
    if (ui->mode == mega_memview_mode_vram) {
        value = 1u + (uint32_t)(percent * (float)(MEGA_MEMVIEW_MAX_VRAM_TILES_PER_ROW - 1u) + 0.5f);
        value = mega_memview_clampVramTilesPerRow(value);
        snprintf(out, cap, "%u tiles", (unsigned)value);
    } else {
        value = 1u + (uint32_t)(percent * (float)(MEGA_MEMVIEW_MAX_RAM_ROW_BYTES - 1u) + 0.5f);
        value = mega_memview_clampRamRowBytes(value);
        snprintf(out, cap, "%u bytes", (unsigned)value);
    }
}

static void
mega_memview_zoomSeekTooltip(float percent, char *out, size_t cap, void *user)
{
    int zoomLevel = 0;

    (void)user;
    if (!out || cap == 0u) {
        return;
    }
    zoomLevel = MEGA_MEMVIEW_ZOOM_MIN +
        (int)(percent * (float)(MEGA_MEMVIEW_ZOOM_MAX - MEGA_MEMVIEW_ZOOM_MIN) + 0.5f);
    zoomLevel = mega_memview_clampZoomLevel(zoomLevel);
    snprintf(out, cap, "%dx", zoomLevel);
}

static void
mega_memview_overviewZoomSeekTooltip(float percent, char *out, size_t cap, void *user)
{
    int zoomLevel = MEGA_MEMVIEW_OVERVIEW_ZOOM_MIN +
                    (int)(percent * (float)(MEGA_MEMVIEW_OVERVIEW_ZOOM_MAX - MEGA_MEMVIEW_OVERVIEW_ZOOM_MIN) + 0.5f);

    (void)user;
    if (!out || cap == 0u) {
        return;
    }
    zoomLevel = mega_memview_clampOverviewZoomLevel(zoomLevel);
    snprintf(out, cap, "%dx", zoomLevel);
}

static void
mega_memview_onWidthSeekChanged(float percent, void *user)
{
    mega_memview_state_t *ui = (mega_memview_state_t *)user;
    uint32_t value = 1u;

    if (ui->mode == mega_memview_mode_vram) {
        value = 1u + (uint32_t)(percent * (float)(MEGA_MEMVIEW_MAX_VRAM_TILES_PER_ROW - 1u) + 0.5f);
    } else {
        value = 1u + (uint32_t)(percent * (float)(MEGA_MEMVIEW_MAX_RAM_ROW_BYTES - 1u) + 0.5f);
    }
    mega_memview_setWidthValue(ui, value);
}

static void
mega_memview_onZoomSeekChanged(float percent, void *user)
{
    mega_memview_state_t *ui = (mega_memview_state_t *)user;
    int zoomLevel = 0;

    zoomLevel = MEGA_MEMVIEW_ZOOM_MIN +
        (int)(percent * (float)(MEGA_MEMVIEW_ZOOM_MAX - MEGA_MEMVIEW_ZOOM_MIN) + 0.5f);
    ui->zoomLevel = mega_memview_clampZoomLevel(zoomLevel);
    ui->zoomHasSaved = 1;
    ui->scrollX = 0;
    mega_memview_syncTextboxesFromState(ui);
    config_saveConfig();
}

static void
mega_memview_onOverviewZoomSeekChanged(float percent, void *user)
{
    mega_memview_state_t *ui = (mega_memview_state_t *)user;
    int zoomLevel = MEGA_MEMVIEW_OVERVIEW_ZOOM_MIN +
                    (int)(percent * (float)(MEGA_MEMVIEW_OVERVIEW_ZOOM_MAX - MEGA_MEMVIEW_OVERVIEW_ZOOM_MIN) + 0.5f);

    ui->overviewZoomLevel = mega_memview_clampOverviewZoomLevel(zoomLevel);
    ui->overviewZoomHasSaved = 1;
    config_saveConfig();
}

static int
mega_memview_keyStep(SDL_Keycode key)
{
    switch (key) {
    case SDLK_LEFT:
    case SDLK_DOWN:
        return -1;
    case SDLK_RIGHT:
    case SDLK_UP:
        return 1;
    default:
        return 0;
    }
}

static int
mega_memview_handleWidthKey(mega_memview_state_t *ui, SDL_Keycode key)
{
    int nextValue = 0;

    nextValue = ui->mode == mega_memview_mode_vram ? (int)ui->vramTilesPerRow : (int)ui->ramRowBytes;
    if (key == SDLK_LEFT) {
        nextValue--;
    } else if (key == SDLK_RIGHT) {
        nextValue++;
    } else {
        return 0;
    }
    mega_memview_setWidthValue(ui, (uint32_t)nextValue);
    return 1;
}

static int
mega_memview_widthSeekHandleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    mega_memview_state_t *ui = &mega_memview_stateSingleton;

    if (!self || !ctx || !ev) {
        return 0;
    }
    if (ev->type == SDL_KEYDOWN && e9ui_getFocus(ctx) == self) {
        if (mega_memview_handleWidthKey(ui, ev->key.keysym.sym)) {
            return 1;
        }
    }
    if (ui->widthSeekDefaultHandleEvent) {
        return ui->widthSeekDefaultHandleEvent(self, ctx, ev);
    }
    return 0;
}

static int
mega_memview_zoomSeekHandleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    mega_memview_state_t *ui = &mega_memview_stateSingleton;
    int step = 0;

    if (!self || !ctx || !ev) {
        return 0;
    }
    step = ev->type == SDL_KEYDOWN && e9ui_getFocus(ctx) == self ? mega_memview_keyStep(ev->key.keysym.sym) : 0;
    if (step != 0) {
        ui->zoomLevel = mega_memview_clampZoomLevel(ui->zoomLevel + step);
        ui->zoomHasSaved = 1;
        ui->scrollX = 0;
        mega_memview_syncTextboxesFromState(ui);
        config_saveConfig();
        return 1;
    }
    if (ui->zoomSeekDefaultHandleEvent) {
        return ui->zoomSeekDefaultHandleEvent(self, ctx, ev);
    }
    return 0;
}

static int
mega_memview_overviewZoomSeekHandleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    mega_memview_state_t *ui = &mega_memview_stateSingleton;
    int step = 0;

    if (!self || !ctx || !ev) {
        return 0;
    }
    step = ev->type == SDL_KEYDOWN && ev->key.repeat == 0 ? mega_memview_keyStep(ev->key.keysym.sym) : 0;
    if (step != 0) {
        ui->overviewZoomLevel = mega_memview_clampOverviewZoomLevel(ui->overviewZoomLevel + step);
        ui->overviewZoomHasSaved = 1;
        mega_memview_syncTextboxesFromState(ui);
        config_saveConfig();
        return 1;
    }
    if (ui->overviewZoomSeekDefaultHandleEvent) {
        return ui->overviewZoomSeekDefaultHandleEvent(self, ctx, ev);
    }
    return 0;
}

static void
mega_memview_canvasLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static void
mega_memview_renderMainTexture(mega_memview_state_t *ui,
                                 e9ui_context_t *ctx,
                                 int texW,
                                 int texH,
                                 int bitViewW,
                                 int rowAreaY,
                                 int bitAreaX,
                                 int updateTexture)
{
    SDL_Rect srcRect = { ui->scrollX, 0, bitViewW < texW - ui->scrollX ? bitViewW : texW - ui->scrollX, texH };

    if (updateTexture) {
        SDL_UpdateTexture(ui->mainTexture, NULL, ui->mainPixels, texW * (int)sizeof(*ui->mainPixels));
    }
    ui->contentPixelWidth = texW;
    if (srcRect.w > 0 && srcRect.h > 0) {
        SDL_RenderCopy(ctx->renderer,
                       ui->mainTexture,
                       &srcRect,
                       &(SDL_Rect){ bitAreaX, rowAreaY, srcRect.w, srcRect.h });
    }
}

static void
mega_memview_canvasRenderRam(mega_memview_state_t *ui,
                               e9ui_context_t *ctx,
                               e9ui_component_t *self,
                               int bitViewW,
                               int rowAreaY,
                               int bitAreaX)
{
    int rowBytes = (int)mega_memview_clampRamRowBytes(ui->ramRowBytes);
    int bitPx = mega_memview_ramBitPx(ui);
    int rowPx = mega_memview_ramRowPx(ui);
    int visibleRows = mega_memview_canvasVisibleRows(ui, &self->bounds);
    int texW = 0;
    int texH = 0;
    uint8_t *rowData = NULL;
    size_t dataSize = (size_t)rowBytes * (size_t)visibleRows;
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    int labelStepRows = 8;
    int fontHeight = 16;
    uint32_t baseAddr = mega_memview_currentBaseAddr(ui);
    uint32_t rangeStart = MEGA_MEMVIEW_RAM_BASE_MIN;
    uint32_t rangeEnd = MEGA_MEMVIEW_RAM_BASE_MAX;

    ui->mainVramCacheValid = 0;
    ui->mainRomsCacheValid = 0;
    if (ui->mode == mega_memview_mode_zram) {
        uint32_t clampedBaseAddr = mega_memview_clampBaseForView(ui, &self->bounds, baseAddr);

        rangeStart = MEGA_MEMVIEW_ZRAM_BASE_MIN;
        rangeEnd = MEGA_MEMVIEW_ZRAM_BASE_MAX;
        if (clampedBaseAddr != baseAddr) {
            ui->zramBaseAddr = clampedBaseAddr;
            baseAddr = clampedBaseAddr;
        }
    }

    if (font) {
        fontHeight = TTF_FontHeight(font);
        if (fontHeight > 0) {
            int minStep = (fontHeight + rowPx - 1) / rowPx;
            if (minStep > labelStepRows) {
                labelStepRows = minStep;
            }
        }
    }
    texW = rowBytes * 8 * bitPx + e9ui_scale_px(&ui->ctx, MEGA_MEMVIEW_RIGHT_PAD_PX);
    texH = visibleRows * rowPx;
    dataSize = (size_t)rowBytes * (size_t)visibleRows;
    if (!mega_memview_ensureTexture(ctx->renderer,
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
    (void)mega_memview_readRange(ui, baseAddr, rowData, dataSize);

    for (int row = 0; row < visibleRows; ++row) {
        uint32_t rowAddr = baseAddr + (uint32_t)row * (uint32_t)rowBytes;

        if (ui->mode == mega_memview_mode_zram && (rowAddr < rangeStart || rowAddr > rangeEnd)) {
            continue;
        }
        if (ui->showAddressColumn && (row % labelStepRows) == 0 && font) {
            mega_memview_drawAddressLabel(ui, ctx, font, rowAddr, self->bounds.x + 6, rowAreaY + row * rowPx - 1);
        }
        for (int byteIndex = 0; byteIndex < rowBytes; ++byteIndex) {
            uint32_t byteAddr = rowAddr + (uint32_t)byteIndex;
            uint8_t value = rowData[row * rowBytes + byteIndex];
            int byteX = byteIndex * 8 * bitPx;

            if (ui->mode == mega_memview_mode_zram && (byteAddr < rangeStart || byteAddr > rangeEnd)) {
                continue;
            }
            if ((byteIndex & 1) == 0) {
                mega_memview_fillRect(ui->mainPixels, texW, texW, texH, byteX, row * rowPx, 1, rowPx, mega_memview_argb(38, 42, 52));
            }
            for (int bit = 0; bit < 8; ++bit) {
                uint32_t color = (value & (uint8_t)(0x80u >> bit)) ? mega_memview_amberColor(15) : mega_memview_argb(33, 18, 0);
                mega_memview_fillRect(ui->mainPixels,
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

    mega_memview_renderMainTexture(ui, ctx, texW, texH, bitViewW, rowAreaY, bitAreaX, 1);
    alloc_free(rowData);
}

static void
mega_memview_canvasRenderVram(mega_memview_state_t *ui,
                                e9ui_context_t *ctx,
                                e9ui_component_t *self,
                                int bitViewW,
                                int rowAreaY,
                                int bitAreaX)
{
    int tilesPerRow = (int)mega_memview_clampVramTilesPerRow(ui->vramTilesPerRow);
    int pixelPx = mega_memview_tilePixelPx(ui);
    int tileGapPx = e9ui_scale_px(&ui->ctx, MEGA_MEMVIEW_TILE_GAP_PX);
    int visibleRows = mega_memview_canvasVisibleRows(ui, &self->bounds);
    int tileStridePx = MEGA_MEMVIEW_TILE_W * pixelPx + tileGapPx;
    int rowStridePx = MEGA_MEMVIEW_TILE_H * pixelPx + tileGapPx;
    int texW = tilesPerRow * tileStridePx + e9ui_scale_px(&ui->ctx, MEGA_MEMVIEW_RIGHT_PAD_PX);
    int texH = visibleRows * rowStridePx;
    uint32_t vramTiles = mega_memview_vramSize() / MEGA_MEMVIEW_TILE_BYTES;
    size_t dataSize = (size_t)visibleRows * (size_t)tilesPerRow * MEGA_MEMVIEW_TILE_BYTES;
    uint8_t *tileData = NULL;
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    const uint16_t *cram = NULL;
    size_t cramBytes = 0u;
    size_t cramWords = 0u;
    uint32_t *visibleTileNums = NULL;
    uint8_t *tilePaletteBanks = NULL;
    uint8_t *tileHasPalette = NULL;
    uint8_t *tileUsage = NULL;
    size_t visibleTileCount = 0u;
    int haveLivePalette = 0;
    uint64_t contentToken = 0u;
    uint64_t paletteToken = 0u;

    ui->mainRomsCacheValid = 0;
    if (!mega_memview_ensureTexture(ctx->renderer,
                                      &ui->mainTexture,
                                      &ui->mainPixels,
                                      &ui->mainPixelsCap,
                                      &ui->mainTextureW,
                                      &ui->mainTextureH,
                                      texW,
                                      texH)) {
        ui->mainVramCacheValid = 0;
        return;
    }
    visibleTileCount = (size_t)visibleRows * (size_t)tilesPerRow;
    if (visibleTileCount > 0u) {
        visibleTileNums = (uint32_t *)alloc_calloc(visibleTileCount, sizeof(*visibleTileNums));
        tilePaletteBanks = (uint8_t *)alloc_calloc(visibleTileCount, sizeof(*tilePaletteBanks));
        tileHasPalette = (uint8_t *)alloc_calloc(visibleTileCount, sizeof(*tileHasPalette));
        tileUsage = (uint8_t *)alloc_calloc(visibleTileCount, sizeof(*tileUsage));
        if (!visibleTileNums || !tilePaletteBanks || !tileHasPalette || !tileUsage) {
            alloc_free(visibleTileNums);
            alloc_free(tilePaletteBanks);
            alloc_free(tileHasPalette);
            alloc_free(tileUsage);
            return;
        }
    }
    cram = (const uint16_t *)libretro_host_getMemory(4u, &cramBytes);
    cramWords = cramBytes / sizeof(*cram);
    haveLivePalette = cram && cramWords >= 64u;

    for (size_t tileIndex = 0; tileIndex < visibleTileCount; ++tileIndex) {
        uint32_t tileNum = (ui->vramBaseAddr / MEGA_MEMVIEW_TILE_BYTES) + (uint32_t)tileIndex;

        visibleTileNums[tileIndex] = tileNum;
    }
    if (visibleTileCount > 0u) {
        mega_memview_buildVisibleTilePaletteMap(ui,
                                                  visibleTileNums,
                                                  visibleTileCount,
                                                  tilePaletteBanks,
                                                  tileHasPalette,
                                                  tileUsage);
    }
    paletteToken = mega_memview_vramPaletteToken(cram,
                                                   cramWords,
                                                   tilePaletteBanks,
                                                   tileHasPalette,
                                                   visibleTileCount,
                                                   haveLivePalette);
    contentToken = mega_memview_vramContentToken(ui->vramBaseAddr, dataSize);

    for (int row = 0; row < visibleRows; ++row) {
        uint32_t rowAddr = ui->vramBaseAddr + (uint32_t)row * (uint32_t)tilesPerRow * MEGA_MEMVIEW_TILE_BYTES;

        if (rowAddr < mega_memview_vramSize() && ui->showAddressColumn && font) {
            mega_memview_drawAddressLabel(ui, ctx, font, rowAddr, self->bounds.x + 6, rowAreaY + row * rowStridePx - 1);
        }
    }
    if (ui->mainVramCacheValid &&
        ui->mainVramCacheBaseAddr == ui->vramBaseAddr &&
        ui->mainVramCacheVramTiles == vramTiles &&
        ui->mainVramCacheTilesPerRow == tilesPerRow &&
        ui->mainVramCacheVisibleRows == visibleRows &&
        ui->mainVramCachePixelPx == pixelPx &&
        ui->mainVramCacheTileGapPx == tileGapPx &&
        ui->mainVramCacheTexW == texW &&
        ui->mainVramCacheTexH == texH &&
        ui->mainVramCacheContentToken == contentToken &&
        ui->mainVramCachePaletteToken == paletteToken) {
        mega_memview_renderMainTexture(ui, ctx, texW, texH, bitViewW, rowAreaY, bitAreaX, 0);
        mega_memview_renderVramUsageOverlay(ui,
                                             ctx,
                                             tileUsage,
                                             visibleRows,
                                             tilesPerRow,
                                             pixelPx,
                                             tileStridePx,
                                             rowStridePx,
                                             bitViewW,
                                             rowAreaY,
                                             bitAreaX);
        alloc_free(visibleTileNums);
        alloc_free(tilePaletteBanks);
        alloc_free(tileHasPalette);
        alloc_free(tileUsage);
        return;
    }

    memset(ui->mainPixels, 0, (size_t)texW * (size_t)texH * sizeof(*ui->mainPixels));
    tileData = (uint8_t *)alloc_calloc(dataSize, 1);
    if (!tileData) {
        ui->mainVramCacheValid = 0;
        alloc_free(visibleTileNums);
        alloc_free(tilePaletteBanks);
        alloc_free(tileHasPalette);
        alloc_free(tileUsage);
        return;
    }
    (void)mega_memview_readRange(ui, ui->vramBaseAddr, tileData, dataSize);

    for (int row = 0; row < visibleRows; ++row) {
        for (int tile = 0; tile < tilesPerRow; ++tile) {
            size_t tileIndex = (size_t)row * (size_t)tilesPerRow + (size_t)tile;
            uint32_t tileBase = (uint32_t)((row * tilesPerRow + tile) * MEGA_MEMVIEW_TILE_BYTES);
            uint32_t colors[16];
            uint32_t rowPixels[MEGA_MEMVIEW_TILE_W * MEGA_MEMVIEW_ZOOM_MAX];
            int tileX = tile * tileStridePx;
            int tileY = row * rowStridePx;
            int useLiveTilePalette = haveLivePalette && tileHasPalette && tileHasPalette[tileIndex];
            uint32_t tileNum = (ui->vramBaseAddr / MEGA_MEMVIEW_TILE_BYTES) + (uint32_t)tileIndex;

            for (unsigned colorIndex = 0u; colorIndex < 16u; ++colorIndex) {
                colors[colorIndex] = mega_memview_amberColor(colorIndex);
            }
            if (useLiveTilePalette) {
                for (unsigned colorIndex = 0u; colorIndex < 16u; ++colorIndex) {
                    colors[colorIndex] = mega_memview_cramColor(cram, cramWords, tilePaletteBanks[tileIndex], colorIndex);
                }
            }
            if (tileNum >= vramTiles) {
                mega_memview_fillRect(ui->mainPixels,
                                        texW,
                                        texW,
                                        texH,
                                        tileX,
                                        tileY,
                                        MEGA_MEMVIEW_TILE_W * pixelPx,
                                        MEGA_MEMVIEW_TILE_H * pixelPx,
                                        mega_memview_argb(188, 192, 200));
                continue;
            }

            for (unsigned py = 0; py < MEGA_MEMVIEW_TILE_H; ++py) {
                int rowPixelCount = 0;

                for (unsigned px = 0u; px < MEGA_MEMVIEW_TILE_W; ++px) {
                    uint32_t paletteIndex = mega_memview_readVramPixel(tileData, dataSize, tileBase, px, py);
                    uint32_t color = colors[paletteIndex];

                    for (int zoomX = 0; zoomX < pixelPx; ++zoomX) {
                        rowPixels[rowPixelCount++] = color;
                    }
                }
                for (int zoomY = 0; zoomY < pixelPx; ++zoomY) {
                    uint32_t *dst = ui->mainPixels +
                                    (size_t)(tileY + (int)py * pixelPx + zoomY) * (size_t)texW +
                                    (size_t)tileX;

                    memcpy(dst, rowPixels, (size_t)rowPixelCount * sizeof(*rowPixels));
                }
            }
        }
    }

    ui->mainVramCacheValid = 1;
    ui->mainVramCacheBaseAddr = ui->vramBaseAddr;
    ui->mainVramCacheVramTiles = vramTiles;
    ui->mainVramCacheTilesPerRow = tilesPerRow;
    ui->mainVramCacheVisibleRows = visibleRows;
    ui->mainVramCachePixelPx = pixelPx;
    ui->mainVramCacheTileGapPx = tileGapPx;
    ui->mainVramCacheTexW = texW;
    ui->mainVramCacheTexH = texH;
    ui->mainVramCacheContentToken = contentToken;
    ui->mainVramCachePaletteToken = paletteToken;
    mega_memview_renderMainTexture(ui, ctx, texW, texH, bitViewW, rowAreaY, bitAreaX, 1);
    mega_memview_renderVramUsageOverlay(ui,
                                         ctx,
                                         tileUsage,
                                         visibleRows,
                                         tilesPerRow,
                                         pixelPx,
                                         tileStridePx,
                                         rowStridePx,
                                         bitViewW,
                                         rowAreaY,
                                         bitAreaX);
    alloc_free(tileData);
    alloc_free(visibleTileNums);
    alloc_free(tilePaletteBanks);
    alloc_free(tileHasPalette);
    alloc_free(tileUsage);
}

static void
mega_memview_formatSize(size_t size, char *out, size_t cap)
{
    if (!out || cap == 0u) {
        return;
    }
    if (size >= 1024u * 1024u) {
        snprintf(out, cap, "%u MB", (unsigned)((size + (1024u * 1024u - 1u)) / (1024u * 1024u)));
    } else if (size >= 1024u) {
        snprintf(out, cap, "%u KB", (unsigned)((size + 1023u) / 1024u));
    } else {
        snprintf(out, cap, "%u B", (unsigned)size);
    }
}

static void
mega_memview_appendText(char *out, size_t cap, size_t *pos, const char *text)
{
    size_t textLen = 0u;
    size_t available = 0u;

    if (!out || cap == 0u || !pos || !text) {
        return;
    }
    if (*pos >= cap) {
        out[cap - 1u] = '\0';
        *pos = cap - 1u;
        return;
    }
    textLen = strlen(text);
    available = cap - *pos - 1u;
    if (textLen > available) {
        textLen = available;
    }
    if (textLen > 0u) {
        memcpy(out + *pos, text, textLen);
        *pos += textLen;
    }
    out[*pos] = '\0';
}

static void
mega_memview_drawText(mega_memview_state_t *ui,
                        e9ui_context_t *ctx,
                        TTF_Font *font,
                        const char *text,
                        SDL_Color color,
                        int x,
                        int y)
{
    int lineHeight = 0;

    (void)ui;
    if (!ctx || !font || !text) {
        return;
    }
    lineHeight = TTF_FontHeight(font);
    if (lineHeight < 1) {
        lineHeight = 16;
    }
    e9ui_drawSelectableText(ctx, NULL, font, text, color, x, y, lineHeight, 0, NULL, 0, 0);
}

static uint32_t
mega_memview_romSampleColor(const uint8_t *data, size_t size, size_t start, size_t end)
{
    uint64_t sum = 0u;
    size_t count = 0u;
    uint32_t intensity = 0u;
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;

    if (!data || size == 0u) {
        return mega_memview_argb(24, 27, 34);
    }
    if (end <= start) {
        end = start + 1u;
    }
    if (start >= size) {
        start = size - 1u;
    }
    if (end > size) {
        end = size;
    }
    for (size_t i = start; i < end; ++i) {
        sum += data[i];
        count++;
    }
    if (count == 0u) {
        count = 1u;
    }
    intensity = (uint32_t)(sum / count);
    r = (uint8_t)(28u + (intensity * 74u) / 255u);
    g = (uint8_t)(42u + (intensity * 158u) / 255u);
    b = (uint8_t)(54u + (intensity * 164u) / 255u);
    return mega_memview_argb(r, g, b);
}

static size_t
mega_memview_romEntryDisplaySize(const e9k_debug_rom_entry_t *rom);

static const uint8_t *
mega_memview_romEntryDisplayData(const e9k_debug_rom_entry_t *rom)
{
    size_t displaySize = 0u;

    if (!rom) {
        return NULL;
    }
    displaySize = mega_memview_romEntryDisplaySize(rom);
    if (displaySize != 0u && displaySize != rom->size && rom->size > 0x80000u && strcmp(rom->label, "M1") == 0) {
        return rom->data + 0x10000u;
    }
    return rom->data;
}

static size_t
mega_memview_romEntryDisplaySize(const e9k_debug_rom_entry_t *rom)
{
    const char *romPath = NULL;
    FILE *file = NULL;
    uint8_t header[104];
    size_t readSize = 0u;
    uint32_t displaySize = 0u;

    if (!rom) {
        return 0u;
    }
    if (strcmp(rom->label, "M1") != 0 || rom->size <= 0x80000u) {
        return rom->size;
    }

    romPath = libretro_host_getRomPath();
    if (!romPath || !*romPath) {
        return rom->size;
    }
    file = fopen(romPath, "rb");
    if (file) {
        readSize = fread(header, 1, sizeof(header), file);
        fclose(file);
        if (readSize == sizeof(header) && memcmp(header + 96, "E9KD", 4) == 0) {
            displaySize = (uint32_t)header[100] |
                          ((uint32_t)header[101] << 8) |
                          ((uint32_t)header[102] << 16) |
                          ((uint32_t)header[103] << 24);
            if (displaySize != 0u && (size_t)displaySize <= rom->size - 0x10000u) {
                return (size_t)displaySize;
            }
        }
    }
    return rom->size;
}

static void
mega_memview_drawRomContents(mega_memview_state_t *ui,
                               const e9k_debug_rom_entry_t *rom,
                               int texW,
                               int texH,
                               int x,
                               int y,
                               int w,
                               int h)
{
    size_t totalPixels = 0u;
    const uint8_t *displayData = NULL;
    size_t displaySize = 0u;

    if (!ui || !rom || w <= 0 || h <= 0) {
        return;
    }
    displayData = mega_memview_romEntryDisplayData(rom);
    displaySize = mega_memview_romEntryDisplaySize(rom);
    if (!displayData || displaySize == 0u) {
        return;
    }
    totalPixels = (size_t)w * (size_t)h;
    if (totalPixels == 0u) {
        return;
    }
    for (int yy = 0; yy < h; ++yy) {
        for (int xx = 0; xx < w; ++xx) {
            size_t pixelIndex = (size_t)yy * (size_t)w + (size_t)xx;
            size_t start = (size_t)(((uint64_t)pixelIndex * (uint64_t)displaySize) / (uint64_t)totalPixels);
            size_t end = (size_t)(((uint64_t)(pixelIndex + 1u) * (uint64_t)displaySize) / (uint64_t)totalPixels);

            if (x + xx >= 0 && x + xx < texW && y + yy >= 0 && y + yy < texH) {
                ui->mainPixels[(size_t)(y + yy) * (size_t)texW + (size_t)(x + xx)] =
                    mega_memview_romSampleColor(displayData, displaySize, start, end);
            }
        }
    }
}

static uint64_t
mega_memview_romsContentToken(const e9k_debug_rom_entry_t *roms, size_t romCount)
{
    uint64_t hash = MEGA_MEMVIEW_FNV1A64_OFFSET_BASIS;

    hash ^= (uint64_t)romCount;
    hash *= MEGA_MEMVIEW_FNV1A64_PRIME;
    for (size_t i = 0u; i < romCount; ++i) {
        const e9k_debug_rom_entry_t *rom = &roms[i];

        hash ^= (uint64_t)(uintptr_t)rom->data;
        hash *= MEGA_MEMVIEW_FNV1A64_PRIME;
        hash ^= (uint64_t)rom->size;
        hash *= MEGA_MEMVIEW_FNV1A64_PRIME;
        hash ^= (uint64_t)(uintptr_t)mega_memview_romEntryDisplayData(rom);
        hash *= MEGA_MEMVIEW_FNV1A64_PRIME;
        hash ^= (uint64_t)mega_memview_romEntryDisplaySize(rom);
        hash *= MEGA_MEMVIEW_FNV1A64_PRIME;
        for (size_t c = 0u; c < sizeof(rom->label) && rom->label[c]; ++c) {
            hash ^= (uint64_t)(uint8_t)rom->label[c];
            hash *= MEGA_MEMVIEW_FNV1A64_PRIME;
        }
    }
    return hash;
}

static void
mega_memview_initRomsEntries(mega_memview_state_t *ui)
{
    if (!ui || ui->mainRomsEntriesValid) {
        return;
    }
    memset(ui->mainRomsEntries, 0, sizeof(ui->mainRomsEntries));
    ui->mainRomsEntryCount = libretro_host_megadrive_getRoms(ui->mainRomsEntries,
                                                             sizeof(ui->mainRomsEntries) / sizeof(ui->mainRomsEntries[0]));
    ui->mainRomsContentToken = mega_memview_romsContentToken(ui->mainRomsEntries, ui->mainRomsEntryCount);
    ui->mainRomsEntriesValid = 1;
}

static void
mega_memview_canvasRenderRoms(mega_memview_state_t *ui,
                                e9ui_context_t *ctx,
                                e9ui_component_t *self,
                                int bitViewW,
                                int rowAreaY,
                                int bitAreaX)
{
    e9k_debug_rom_entry_t *roms = ui->mainRomsEntries;
    size_t romCount = 0u;
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    int pad = e9ui_scale_px(&ui->ctx, 14);
    int gap = e9ui_scale_px(&ui->ctx, 14);
    int labelH = font ? TTF_FontHeight(font) : 16;
    int cardW = e9ui_scale_px(&ui->ctx, 260);
    int cardH = e9ui_scale_px(&ui->ctx, 170);
    int contentPad = e9ui_scale_px(&ui->ctx, 10);
    int contentY = labelH + contentPad + e9ui_scale_px(&ui->ctx, 4);
    int contentH = cardH - contentY - contentPad;
    int gridH = 0;
    int usableW = bitViewW > cardW ? bitViewW : cardW;
    int cols = (usableW + gap) / (cardW + gap);
    int rows = 0;
    int texW = 0;
    int texH = 0;
    int visibleH = 0;
    int maxScrollY = 0;
    uint64_t romsToken = 0u;
    int cacheValid = 0;
    char label[64];
    char sizeLabel[32];
    size_t labelPos = 0u;

    ui->mainVramCacheValid = 0;
    mega_memview_initRomsEntries(ui);
    romCount = ui->mainRomsEntryCount;
    romsToken = ui->mainRomsContentToken;
    if (cols < 1) {
        cols = 1;
    }
    rows = (int)((romCount + (size_t)cols - 1u) / (size_t)cols);
    if (rows < 1) {
        rows = 1;
    }
    gridH = pad * 2 + rows * cardH + (rows - 1) * gap;
    texW = pad * 2 + cols * cardW + (cols - 1) * gap + e9ui_scale_px(&ui->ctx, MEGA_MEMVIEW_RIGHT_PAD_PX);
    texH = gridH;
    if (!mega_memview_ensureTexture(ctx->renderer,
                                      &ui->mainTexture,
                                      &ui->mainPixels,
                                      &ui->mainPixelsCap,
                                      &ui->mainTextureW,
                                      &ui->mainTextureH,
                                      texW,
                                      texH)) {
        return;
    }
    cacheValid = ui->mainRomsCacheValid &&
                 ui->mainRomsCacheTexW == texW &&
                 ui->mainRomsCacheTexH == texH &&
                 ui->mainRomsCacheCols == cols &&
                 ui->mainRomsCacheCardW == cardW &&
                 ui->mainRomsCacheCardH == cardH &&
                 ui->mainRomsCacheContentW == cardW - contentPad * 2 &&
                 ui->mainRomsCacheContentH == contentH &&
                 ui->mainRomsCacheToken == romsToken;
    if (!cacheValid) {
        memset(ui->mainPixels, 0, (size_t)texW * (size_t)texH * sizeof(*ui->mainPixels));

        for (size_t i = 0u; i < romCount; ++i) {
            int col = (int)(i % (size_t)cols);
            int row = (int)(i / (size_t)cols);
            int cardX = pad + col * (cardW + gap);
            int cardY = pad + row * (cardH + gap);
            int contentX = cardX + contentPad;
            int contentW = cardW - contentPad * 2;

            mega_memview_fillRect(ui->mainPixels, texW, texW, texH, cardX, cardY, cardW, cardH, mega_memview_argb(21, 24, 31));
            mega_memview_fillRect(ui->mainPixels, texW, texW, texH, cardX, cardY, cardW, 1, mega_memview_argb(92, 101, 116));
            mega_memview_fillRect(ui->mainPixels, texW, texW, texH, cardX, cardY + cardH - 1, cardW, 1, mega_memview_argb(92, 101, 116));
            mega_memview_fillRect(ui->mainPixels, texW, texW, texH, cardX, cardY, 1, cardH, mega_memview_argb(92, 101, 116));
            mega_memview_fillRect(ui->mainPixels, texW, texW, texH, cardX + cardW - 1, cardY, 1, cardH, mega_memview_argb(92, 101, 116));
            mega_memview_drawRomContents(ui, &roms[i], texW, texH, contentX, cardY + contentY, contentW, contentH);
        }
        SDL_UpdateTexture(ui->mainTexture, NULL, ui->mainPixels, texW * (int)sizeof(*ui->mainPixels));
        ui->mainRomsCacheValid = 1;
        ui->mainRomsCacheTexW = texW;
        ui->mainRomsCacheTexH = texH;
        ui->mainRomsCacheCols = cols;
        ui->mainRomsCacheCardW = cardW;
        ui->mainRomsCacheCardH = cardH;
        ui->mainRomsCacheContentW = cardW - contentPad * 2;
        ui->mainRomsCacheContentH = contentH;
        ui->mainRomsCacheToken = romsToken;
    }

    ui->contentPixelWidth = texW;
    visibleH = self->bounds.y + self->bounds.h - rowAreaY;
    if (visibleH < 1) {
        visibleH = 1;
    }
    if (visibleH > texH) {
        visibleH = texH;
    }
    maxScrollY = texH > visibleH ? texH - visibleH : 0;
    if (ui->romsScrollY > maxScrollY) {
        ui->romsScrollY = maxScrollY;
    }
    if (ui->romsScrollY < 0) {
        ui->romsScrollY = 0;
    }
    if (ui->scrollX < 0) {
        ui->scrollX = 0;
    }
    if (ui->scrollX >= texW) {
        ui->scrollX = texW > 1 ? texW - 1 : 0;
    }
    if (bitViewW > texW - ui->scrollX) {
        bitViewW = texW - ui->scrollX;
    }
    if (bitViewW > 0 && visibleH > 0) {
        SDL_RenderCopy(ctx->renderer,
                       ui->mainTexture,
                       &(SDL_Rect){ ui->scrollX, ui->romsScrollY, bitViewW, visibleH },
                       &(SDL_Rect){ bitAreaX, rowAreaY, bitViewW, visibleH });
    }

    for (size_t i = 0u; i < romCount; ++i) {
        int col = (int)(i % (size_t)cols);
        int row = (int)(i / (size_t)cols);
        int cardX = bitAreaX - ui->scrollX + pad + col * (cardW + gap);
        int cardY = rowAreaY - ui->romsScrollY + pad + row * (cardH + gap);

        labelPos = 0u;
        mega_memview_formatSize(mega_memview_romEntryDisplaySize(&roms[i]), sizeLabel, sizeof(sizeLabel));
        label[0] = '\0';
        mega_memview_appendText(label, sizeof(label), &labelPos, roms[i].label[0] ? roms[i].label : "ROM");
        mega_memview_appendText(label, sizeof(label), &labelPos, "  ");
        mega_memview_appendText(label, sizeof(label), &labelPos, sizeLabel);
        mega_memview_drawText(ui, ctx, font, label, (SDL_Color){ 236, 238, 242, 255 }, cardX + contentPad, cardY + contentPad);
    }
}

static void
mega_memview_canvasRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    mega_memview_state_t *ui = NULL;
    mega_memview_step_buttons_action_ctx_t actionCtx;
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
    ui = (mega_memview_state_t *)self->state;
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
    e9ui_step_buttons_tick(ctx, self->bounds, 0, 1, &ui->stepButtons, &actionCtx, mega_memview_stepButtonsOnAction);

    fillRect = clip;
    SDL_SetRenderDrawColor(ctx->renderer, 15, 17, 22, 255);
    SDL_RenderFillRect(ctx->renderer, &fillRect);

    font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    leftGutterPx = mega_memview_leftGutterPx(ui, ctx, font);
    rightGutter = mega_memview_stepButtonsGutterWidth(ctx, self);
    if (rightGutter < 0) {
        rightGutter = 0;
    }
    overviewBounds = mega_memview_overviewBounds(ui, self, ctx, font);
    bitAreaX = self->bounds.x + leftGutterPx;
    rowAreaY = self->bounds.y + e9ui_scale_px(&ui->ctx, MEGA_MEMVIEW_TOP_PAD_PX);
    bitViewW = self->bounds.w - leftGutterPx - rightGutter;
    if (bitViewW < 1) {
        bitViewW = 1;
    }
    visibleRows = mega_memview_canvasVisibleRows(ui, &self->bounds);
    if (ui->mode == mega_memview_mode_vram && ui->followActiveSprites) {
        mega_memview_followActiveVramWindow(ui, &self->bounds);
    }

    SDL_RenderSetClipRect(ctx->renderer, &clip);

    if (overviewBounds.w > 0 && overviewBounds.h > 0) {
        e9ui_rect_t overviewContentBounds = mega_memview_overviewContentBounds(ctx, &overviewBounds);

        if (mega_memview_rebuildOverviewBackgroundTexture(ui, ctx) && ui->overviewBackgroundTexture) {
            SDL_Rect dst = { overviewContentBounds.x, overviewContentBounds.y, overviewContentBounds.w, overviewContentBounds.h };
            if (ui->mode == mega_memview_mode_vram) {
                if (mega_memview_rebuildOverviewTexture(ui, ctx, &overviewBounds) && ui->overviewTexture) {
                    SDL_RenderCopy(ctx->renderer, ui->overviewTexture, NULL, &dst);
                } else {
                    SDL_RenderCopy(ctx->renderer, ui->overviewBackgroundTexture, NULL, &dst);
                }
            } else {
                SDL_RenderCopy(ctx->renderer, ui->overviewBackgroundTexture, NULL, &dst);
            }
            SDL_SetRenderDrawColor(ctx->renderer, 96, 96, 108, 255);
            SDL_RenderDrawRect(ctx->renderer, &(SDL_Rect){ overviewBounds.x, overviewBounds.y, overviewBounds.w, overviewBounds.h });
            mega_memview_renderOverviewSelection(ui, ctx, &overviewBounds, visibleRows);
        }
    }

    if (ui->mode == mega_memview_mode_roms) {
        mega_memview_canvasRenderRoms(ui, ctx, self, bitViewW, rowAreaY, bitAreaX);
    } else if (ui->mode == mega_memview_mode_vram) {
        mega_memview_canvasRenderVram(ui, ctx, self, bitViewW, rowAreaY, bitAreaX);
    } else {
        mega_memview_canvasRenderRam(ui, ctx, self, bitViewW, rowAreaY, bitAreaX);
    }

    hscrollBounds = mega_memview_hscrollBounds(ui, self);
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
mega_memview_canvasDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->state = NULL;
}

static int
mega_memview_canvasHandleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    mega_memview_state_t *ui = NULL;
    mega_memview_step_buttons_action_ctx_t actionCtx;
    e9ui_rect_t hscrollBounds;
    int scrollX = 0;
    int scrollY = 0;
    int mx = 0;
    int my = 0;

    if (!self || !ctx || !ev) {
        return 0;
    }
    ui = (mega_memview_state_t *)self->state;

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
        if (mega_memview_overviewNavigate(ui, self, ctx, mx, my)) {
            return 1;
        }
    }

    hscrollBounds = mega_memview_hscrollBounds(ui, self);
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
                                      mega_memview_stepButtonsOnAction)) {
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
            mega_memview_scrollRows(ui, &self->bounds, ev->wheel.y);
            return 1;
        }
        if (ev->wheel.x != 0) {
            return 1;
        }
    }

    if (ev->type == SDL_KEYDOWN && e9ui_getFocus(ctx) == self) {
        if (ev->key.keysym.sym == SDLK_PAGEUP) {
            mega_memview_scrollRows(ui, &self->bounds, -mega_memview_canvasVisibleRows(ui, &self->bounds));
            return 1;
        }
        if (ev->key.keysym.sym == SDLK_PAGEDOWN) {
            mega_memview_scrollRows(ui, &self->bounds, mega_memview_canvasVisibleRows(ui, &self->bounds));
            return 1;
        }
        if (ev->key.keysym.sym == SDLK_UP) {
            mega_memview_scrollRows(ui, &self->bounds, -1);
            return 1;
        }
        if (ev->key.keysym.sym == SDLK_DOWN) {
            mega_memview_scrollRows(ui, &self->bounds, 1);
            return 1;
        }
        if (ev->key.keysym.sym == SDLK_LEFT) {
            if ((ev->key.keysym.mod & KMOD_SHIFT) != 0) {
                return mega_memview_handleWidthKey(ui, ev->key.keysym.sym);
            }
            ui->scrollX -= e9ui_scale_px(ctx, 24);
            e9ui_scrollbar_clamp(hscrollBounds.w, 1, ui->contentPixelWidth, 1, &ui->scrollX, &scrollY);
            return 1;
        }
        if (ev->key.keysym.sym == SDLK_RIGHT) {
            if ((ev->key.keysym.mod & KMOD_SHIFT) != 0) {
                return mega_memview_handleWidthKey(ui, ev->key.keysym.sym);
            }
            ui->scrollX += e9ui_scale_px(ctx, 24);
            e9ui_scrollbar_clamp(hscrollBounds.w, 1, ui->contentPixelWidth, 1, &ui->scrollX, &scrollY);
            return 1;
        }
    }

    return 0;
}

static e9ui_component_t *
mega_memview_makeCanvas(mega_memview_state_t *ui)
{
    e9ui_component_t *canvas = NULL;

    canvas = (e9ui_component_t *)alloc_calloc(1, sizeof(*canvas));
    if (!canvas) {
        return NULL;
    }
    canvas->name = "mega_memview_canvas";
    canvas->state = ui;
    canvas->layout = mega_memview_canvasLayout;
    canvas->render = mega_memview_canvasRender;
    canvas->handleEvent = mega_memview_canvasHandleEvent;
    canvas->dtor = mega_memview_canvasDtor;
    canvas->focusable = 1;
    return canvas;
}

static void
mega_memview_clearUiRefs(mega_memview_state_t *ui)
{
    ui->canvas = NULL;
    ui->modeButtonRam = NULL;
    ui->modeButtonVram = NULL;
    ui->modeButtonZram = NULL;
    ui->modeButtonRoms = NULL;
    ui->addressBox = NULL;
    ui->widthBox = NULL;
    ui->widthSeek = NULL;
    ui->zoomSeek = NULL;
    ui->overviewZoomSeek = NULL;
    ui->overlayBodyHost = NULL;
}

static e9ui_component_t *
mega_memview_buildRoot(mega_memview_state_t *ui)
{
    e9ui_component_t *root = e9ui_stack_makeVertical();
    e9ui_component_t *toolbar = mega_memview_makeToolbarWrap();

    ui->addressBox = e9ui_textbox_make(32, mega_memview_onAddressSubmit, NULL, ui);
    ui->widthBox = e9ui_textbox_make(16, mega_memview_onWidthSubmit, NULL, ui);
    ui->widthSeek = e9ui_seek_bar_make();
    ui->zoomSeek = e9ui_seek_bar_make();
    ui->overviewZoomSeek = e9ui_seek_bar_make();
    ui->modeButtonRam = e9ui_button_make("RAM", mega_memview_setMode, (void *)&mega_memview_modeButtonModes[0]);
    ui->modeButtonVram = e9ui_button_make("VRAM", mega_memview_setMode, (void *)&mega_memview_modeButtonModes[1]);
    ui->modeButtonZram = e9ui_button_make("ZRAM", mega_memview_setMode, (void *)&mega_memview_modeButtonModes[2]);
    ui->modeButtonRoms = e9ui_button_make("ROMS", mega_memview_setMode, (void *)&mega_memview_modeButtonModes[3]);
    ui->canvas = mega_memview_makeCanvas(ui);

    e9ui_component_t *addressLabel = e9ui_text_make("Address");
    e9ui_component_t *widthLabel = e9ui_text_make("Width");
    e9ui_component_t *zoomLabel = e9ui_text_make("Zoom");
    e9ui_component_t *overviewZoomLabel = e9ui_text_make("Ov Zoom");
    e9ui_component_t *showAddress = e9ui_checkbox_make("Addr", ui->showAddressColumn, mega_memview_onCheckboxChanged, &mega_memview_checkboxBindings[0]);
    e9ui_component_t *showOverview = e9ui_checkbox_make("Overview", ui->showOverviewColumn, mega_memview_onCheckboxChanged, &mega_memview_checkboxBindings[1]);
    e9ui_component_t *followActive = e9ui_checkbox_make("Follow", ui->followActiveSprites, mega_memview_onCheckboxChanged, &mega_memview_checkboxBindings[2]);
    e9ui_component_t *colorSprites = e9ui_checkbox_make("S", ui->colorSourceSprites, mega_memview_onCheckboxChanged, &mega_memview_checkboxBindings[3]);
    e9ui_component_t *colorPlaneA = e9ui_checkbox_make("A", ui->colorSourcePlaneA, mega_memview_onCheckboxChanged, &mega_memview_checkboxBindings[4]);
    e9ui_component_t *colorPlaneB = e9ui_checkbox_make("B", ui->colorSourcePlaneB, mega_memview_onCheckboxChanged, &mega_memview_checkboxBindings[5]);
    e9ui_component_t *colorHScroll = e9ui_checkbox_make("HS", ui->colorSourceHScroll, mega_memview_onCheckboxChanged, &mega_memview_checkboxBindings[6]);

    e9ui_textbox_setPlaceholder(ui->addressBox, "0x00ff0000");
    e9ui_textbox_setPlaceholder(ui->widthBox, "32");
    e9ui_textbox_setFocusBorderVisible(ui->addressBox, 0);
    e9ui_textbox_setFocusBorderVisible(ui->widthBox, 0);
    mega_memview_initSeekBar(ui->widthSeek,
                               mega_memview_widthSeekHandleEvent,
                               mega_memview_onWidthSeekChanged,
                               mega_memview_widthSeekTooltip,
                               ui,
                               &ui->widthSeekDefaultHandleEvent);
    mega_memview_initSeekBar(ui->zoomSeek,
                               mega_memview_zoomSeekHandleEvent,
                               mega_memview_onZoomSeekChanged,
                               mega_memview_zoomSeekTooltip,
                               ui,
                               &ui->zoomSeekDefaultHandleEvent);
    mega_memview_initSeekBar(ui->overviewZoomSeek,
                               mega_memview_overviewZoomSeekHandleEvent,
                               mega_memview_onOverviewZoomSeekChanged,
                               mega_memview_overviewZoomSeekTooltip,
                               ui,
                               &ui->overviewZoomSeekDefaultHandleEvent);

    e9ui_button_setMini(ui->modeButtonRam, 1);
    e9ui_button_setMini(ui->modeButtonVram, 1);
    e9ui_button_setMini(ui->modeButtonZram, 1);
    e9ui_button_setMini(ui->modeButtonRoms, 1);
    e9ui_button_setLargestLabel(ui->modeButtonRam, "ZRAM");
    e9ui_button_setLargestLabel(ui->modeButtonVram, "VRAM");
    e9ui_button_setLargestLabel(ui->modeButtonZram, "VRAM");
    e9ui_button_setLargestLabel(ui->modeButtonRoms, "VRAM");

    TTF_Font *toolbarFont = e9ui->theme.text.source ? e9ui->theme.text.source : ui->ctx.font;
    int gapSmall = e9ui_scale_px(&ui->ctx, 6);
    int textH = 0;
    int modeButtonW = e9ui_scale_px(&ui->ctx, 72);
    e9ui_button_measure(ui->modeButtonRam, &ui->ctx, &modeButtonW, &textH);

    int checkboxAddrW = 0;
    e9ui_checkbox_measure(showAddress, &ui->ctx, &checkboxAddrW, &textH);
    int checkboxOverviewW = 0;
    e9ui_checkbox_measure(showOverview, &ui->ctx, &checkboxOverviewW, &textH);
    int checkboxFollowW = 0;
    e9ui_checkbox_measure(followActive, &ui->ctx, &checkboxFollowW, &textH);
    int checkboxSpritesW = 0;
    e9ui_checkbox_measure(colorSprites, &ui->ctx, &checkboxSpritesW, &textH);
    int checkboxPlaneAW = 0;
    e9ui_checkbox_measure(colorPlaneA, &ui->ctx, &checkboxPlaneAW, &textH);
    int checkboxPlaneBW = 0;
    e9ui_checkbox_measure(colorPlaneB, &ui->ctx, &checkboxPlaneBW, &textH);
    int checkboxHScrollW = 0;
    e9ui_checkbox_measure(colorHScroll, &ui->ctx, &checkboxHScrollW, &textH);

    int labelAddrW = mega_memview_measureToolbarTextWidth(&ui->ctx, toolbarFont, "Address", 8, 64);
    int addrBoxW = mega_memview_measureToolbarTextboxWidth(toolbarFont, "0x00ff0000", 110);
    int labelWidthW = mega_memview_measureToolbarTextWidth(&ui->ctx, toolbarFont, "Width", 8, 48);
    int widthBoxW = mega_memview_measureToolbarTextboxWidth(toolbarFont, "256", 56);
    int widthSeekW = e9ui_scale_px(&ui->ctx, 180);
    int zoomLabelW = mega_memview_measureToolbarTextWidth(&ui->ctx, toolbarFont, "Zoom", 8, 40);
    int zoomSeekW = e9ui_scale_px(&ui->ctx, 180);
    int overviewZoomLabelW = mega_memview_measureToolbarTextWidth(&ui->ctx, toolbarFont, "Ov Zoom", 8, 56);
    int overviewZoomSeekW = e9ui_scale_px(&ui->ctx, 150);

    e9ui_component_t *groupGeneral = e9ui_hstack_make();
    e9ui_component_t *groupAddress = e9ui_hstack_make();
    e9ui_component_t *groupWidth = e9ui_hstack_make();
    e9ui_component_t *groupZoom = e9ui_hstack_make();
    e9ui_component_t *groupOverviewZoom = e9ui_hstack_make();

    e9ui_hstack_addFixed(groupGeneral, ui->modeButtonRam, modeButtonW);
    e9ui_hstack_addFixed(groupGeneral, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupGeneral, ui->modeButtonVram, modeButtonW);
    e9ui_hstack_addFixed(groupGeneral, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupGeneral, ui->modeButtonZram, modeButtonW);
    e9ui_hstack_addFixed(groupGeneral, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupGeneral, ui->modeButtonRoms, modeButtonW);
    e9ui_hstack_addFixed(groupGeneral, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupGeneral, showAddress, checkboxAddrW);
    e9ui_hstack_addFixed(groupGeneral, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupGeneral, showOverview, checkboxOverviewW);
    e9ui_hstack_addFixed(groupGeneral, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupGeneral, followActive, checkboxFollowW);
    e9ui_hstack_addFixed(groupGeneral, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupGeneral, colorSprites, checkboxSpritesW);
    e9ui_hstack_addFixed(groupGeneral, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupGeneral, colorPlaneA, checkboxPlaneAW);
    e9ui_hstack_addFixed(groupGeneral, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupGeneral, colorPlaneB, checkboxPlaneBW);
    e9ui_hstack_addFixed(groupGeneral, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupGeneral, colorHScroll, checkboxHScrollW);
    int groupGeneralW = modeButtonW + gapSmall + modeButtonW + gapSmall + modeButtonW + gapSmall + modeButtonW + gapSmall + checkboxAddrW + gapSmall + checkboxOverviewW + gapSmall + checkboxFollowW + gapSmall + checkboxSpritesW + gapSmall + checkboxPlaneAW + gapSmall + checkboxPlaneBW + gapSmall + checkboxHScrollW;

    e9ui_hstack_addFixed(groupAddress, addressLabel, labelAddrW);
    e9ui_hstack_addFixed(groupAddress, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupAddress, ui->addressBox, addrBoxW);
    int groupAddressW = labelAddrW + gapSmall + addrBoxW;

    e9ui_hstack_addFixed(groupWidth, widthLabel, labelWidthW);
    e9ui_hstack_addFixed(groupWidth, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupWidth, ui->widthBox, widthBoxW);
    e9ui_hstack_addFixed(groupWidth, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupWidth, ui->widthSeek, widthSeekW);
    int groupWidthW = labelWidthW + gapSmall + widthBoxW + gapSmall + widthSeekW;

    e9ui_hstack_addFixed(groupZoom, zoomLabel, zoomLabelW);
    e9ui_hstack_addFixed(groupZoom, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupZoom, ui->zoomSeek, zoomSeekW);
    int groupZoomW = zoomLabelW + gapSmall + zoomSeekW;

    e9ui_hstack_addFixed(groupOverviewZoom, overviewZoomLabel, overviewZoomLabelW);
    e9ui_hstack_addFixed(groupOverviewZoom, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupOverviewZoom, ui->overviewZoomSeek, overviewZoomSeekW);
    int groupOverviewZoomW = overviewZoomLabelW + gapSmall + overviewZoomSeekW;

    e9ui_component_t *groupGeneralItem = mega_memview_makeToolbarItem(groupGeneral, groupGeneralW);
    e9ui_component_t *groupAddressItem = mega_memview_makeToolbarItem(groupAddress, groupAddressW);
    e9ui_component_t *groupWidthItem = mega_memview_makeToolbarItem(groupWidth, groupWidthW);
    e9ui_component_t *groupZoomItem = mega_memview_makeToolbarItem(groupZoom, groupZoomW);
    e9ui_component_t *groupOverviewZoomItem = mega_memview_makeToolbarItem(groupOverviewZoom, groupOverviewZoomW);

    e9ui_child_add(toolbar, groupGeneralItem, NULL);
    e9ui_child_add(toolbar, groupAddressItem, NULL);
    e9ui_child_add(toolbar, groupWidthItem, NULL);
    e9ui_child_add(toolbar, groupZoomItem, NULL);
    e9ui_child_add(toolbar, groupOverviewZoomItem, NULL);

    e9ui_component_t *toolbarBox = e9ui_box_make(toolbar);
    e9ui_box_setPadding(toolbarBox, 8);
    e9ui_box_setBorder(toolbarBox, E9UI_BORDER_BOTTOM, (SDL_Color){ 70, 70, 70, 255 }, 1);

    e9ui_stack_addFixed(root, toolbarBox);
    e9ui_stack_addFlex(root, ui->canvas);

    mega_memview_syncTextboxesFromState(ui);
    mega_memview_updateModeButtons(ui);
    return root;
}

static e9ui_rect_t
mega_memview_windowDefaultRect(const e9ui_context_t *ctx)
{
    e9ui_rect_t rect;

    rect.w = e9ui_scale_px(ctx, 980);
    rect.h = e9ui_scale_px(ctx, 720);
    rect.x = (ctx->winW - rect.w) / 2;
    rect.y = (ctx->winH - rect.h) / 2;
    return rect;
}

static int
mega_memview_stepButtonsGutterWidth(const e9ui_context_t *ctx, e9ui_component_t *self)
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
mega_memview_windowBackend(void)
{
    return e9ui_window_backend_overlay;
}

static void
mega_memview_syncOverlayContext(mega_memview_state_t *ui, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    ui->ctx = *ctx;
    ui->ctx.window = ctx->window;
    ui->ctx.renderer = ctx->renderer;
    ui->ctx.font = e9ui->ctx.font;
    ui->ctx.winW = bounds.w;
    ui->ctx.winH = bounds.h;
    ui->ctx.focusRoot = ui->root;
    ui->ctx.focusFullscreen = NULL;
}

static void
mega_memview_overlayBodyLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    mega_memview_overlay_body_state_t *state = NULL;
    mega_memview_state_t *ui = NULL;

    if (!self || !ctx || !self->state) {
        return;
    }
    self->bounds = bounds;
    state = (mega_memview_overlay_body_state_t *)self->state;
    ui = state->ui;
    mega_memview_syncOverlayContext(ui, ctx, bounds);

    if (ui->root && ui->root->layout) {
        ui->root->layout(ui->root, &ui->ctx, bounds);
    }
}

static void
mega_memview_overlayBodyRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    mega_memview_overlay_body_state_t *state = NULL;
    mega_memview_state_t *ui = NULL;

    if (!self || !ctx || !self->state) {
        return;
    }
    state = (mega_memview_overlay_body_state_t *)self->state;
    ui = state->ui;
    if (!ui->windowState.open) {
        return;
    }

    mega_memview_syncOverlayContext(ui, ctx, self->bounds);
    ui->ctx.mouseX = ctx->mouseX;
    ui->ctx.mouseY = ctx->mouseY;
    ui->ctx.mousePrevX = ctx->mousePrevX;
    ui->ctx.mousePrevY = ctx->mousePrevY;

    if (ui->root && ui->root->render) {
        ui->root->render(ui->root, &ui->ctx);
    }
}

static void
mega_memview_overlayBodyDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    alloc_free(self->state);
    self->state = NULL;
}

static e9ui_component_t *
mega_memview_makeOverlayBodyHost(mega_memview_state_t *ui)
{
    e9ui_component_t *host = NULL;
    mega_memview_overlay_body_state_t *state = NULL;

    if (!ui->root) {
        return NULL;
    }
    host = (e9ui_component_t *)alloc_calloc(1, sizeof(*host));
    state = (mega_memview_overlay_body_state_t *)alloc_calloc(1, sizeof(*state));
    if (!host || !state) {
        alloc_free(host);
        alloc_free(state);
        return NULL;
    }
    state->ui = ui;
    host->name = "mega_memview_overlay_body";
    host->state = state;
    host->layout = mega_memview_overlayBodyLayout;
    host->render = mega_memview_overlayBodyRender;
    host->dtor = mega_memview_overlayBodyDtor;
    e9ui_child_add(host, ui->root, alloc_strdup("mega_memview_root"));
    return host;
}

static void
mega_memview_destroyTexture(SDL_Texture **texture)
{
    if (!*texture) {
        return;
    }
    SDL_DestroyTexture(*texture);
    *texture = NULL;
}

static void
mega_memview_releaseRuntimeState(mega_memview_state_t *ui)
{
    mega_memview_destroyTexture(&ui->mainTexture);
    mega_memview_destroyTexture(&ui->overviewTexture);
    mega_memview_destroyTexture(&ui->overviewBackgroundTexture);
    alloc_free(ui->mainPixels);
    alloc_free(ui->overviewPixels);
    alloc_free(ui->overviewTileRows);
    alloc_free(ui->overviewTileCols);
    alloc_free(ui->overviewBackgroundPixels);
    alloc_free(ui->followPrevSpriteEntries);
    ui->mainPixels = NULL;
    ui->overviewPixels = NULL;
    ui->overviewTileRows = NULL;
    ui->overviewTileCols = NULL;
    ui->overviewBackgroundPixels = NULL;
    ui->followPrevSpriteEntries = NULL;
    ui->mainPixelsCap = 0u;
    ui->overviewPixelsCap = 0u;
    ui->overviewTileMapCap = 0u;
    ui->overviewTileMapVramTiles = 0u;
    ui->overviewTileMapTilesPerRow = 0u;
    ui->overviewBackgroundPixelsCap = 0u;
    ui->followPrevSpriteEntryCount = 0u;
    ui->mainTextureW = 0;
    ui->mainTextureH = 0;
    ui->mainVramCacheValid = 0;
    ui->mainVramCacheBaseAddr = 0u;
    ui->mainVramCacheVramTiles = 0u;
    ui->mainVramCacheTilesPerRow = 0;
    ui->mainVramCacheVisibleRows = 0;
    ui->mainVramCachePixelPx = 0;
    ui->mainVramCacheTileGapPx = 0;
    ui->mainVramCacheTexW = 0;
    ui->mainVramCacheTexH = 0;
    ui->mainVramCacheContentToken = 0u;
    ui->mainVramCachePaletteToken = 0u;
    ui->mainRomsCacheValid = 0;
    ui->mainRomsCacheTexW = 0;
    ui->mainRomsCacheTexH = 0;
    ui->mainRomsCacheCols = 0;
    ui->mainRomsCacheCardW = 0;
    ui->mainRomsCacheCardH = 0;
    ui->mainRomsCacheContentW = 0;
    ui->mainRomsCacheContentH = 0;
    ui->mainRomsCacheToken = 0u;
    memset(ui->mainRomsEntries, 0, sizeof(ui->mainRomsEntries));
    ui->mainRomsEntryCount = 0u;
    ui->mainRomsContentToken = 0u;
    ui->mainRomsEntriesValid = 0;
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
    ui->romsScrollY = 0;
    mega_memview_clearUiRefs(ui);
}

static void
mega_memview_overlayWindowCloseRequested(e9ui_window_t *window, void *user)
{
    (void)window;
    (void)user;
    mega_memview_toggle();
}

static int
mega_memview_init(void)
{
    mega_memview_state_t *ui = &mega_memview_stateSingleton;
    e9ui_component_t *overlayBodyHost = NULL;
    e9ui_rect_t rect;

    if (ui->windowState.open) {
        return 1;
    }

    mega_memview_releaseRuntimeState(ui);
    ui->ctx = e9ui->ctx;
    mega_memview_initOverviewRanges(ui);
    ui->windowState.windowHost = e9ui_windowCreate(mega_memview_windowBackend());
    if (!ui->windowState.windowHost) {
        return 0;
    }
    ui->ramRowBytes = ui->ramRowBytesHasSaved ? mega_memview_clampRamRowBytes(ui->ramRowBytes) : MEGA_MEMVIEW_DEFAULT_RAM_ROW_BYTES;
    ui->vramTilesPerRow = ui->vramTilesPerRowHasSaved ? mega_memview_clampVramTilesPerRow(ui->vramTilesPerRow) : MEGA_MEMVIEW_DEFAULT_VRAM_TILES_PER_ROW;
    ui->ramBaseAddr = ui->ramBaseAddrHasSaved ? mega_memview_clampRamBaseAddr(ui->ramBaseAddr) : MEGA_MEMVIEW_RAM_BASE_MIN;
    ui->vramBaseAddr = ui->vramBaseAddrHasSaved ? mega_memview_alignVramBaseAddrToRow(ui, ui->vramBaseAddr) : mega_memview_findInitialVramBaseAddr(ui);
    ui->zramBaseAddr = ui->zramBaseAddrHasSaved ? mega_memview_clampZramBaseAddr(ui->zramBaseAddr) : MEGA_MEMVIEW_ZRAM_BASE_MIN;
    ui->zoomLevel = ui->zoomHasSaved ? mega_memview_clampZoomLevel(ui->zoomLevel) : MEGA_MEMVIEW_ZOOM_DEFAULT;
    ui->overviewZoomLevel = ui->overviewZoomHasSaved ? mega_memview_clampOverviewZoomLevel(ui->overviewZoomLevel) : MEGA_MEMVIEW_OVERVIEW_ZOOM_DEFAULT;

    ui->root = mega_memview_buildRoot(ui);
    if (!ui->root) {
        e9ui_windowDestroy(ui->windowState.windowHost);
        ui->windowState.windowHost = NULL;
        return 0;
    }
    rect = e9ui_windowResolveStateOpenRect(&e9ui->ctx,
                                           mega_memview_windowDefaultRect(&e9ui->ctx),
                                           &ui->windowState);
    overlayBodyHost = mega_memview_makeOverlayBodyHost(ui);
    if (!overlayBodyHost) {
        e9ui_windowDestroy(ui->windowState.windowHost);
        ui->windowState.windowHost = NULL;
        ui->root = NULL;
        return 0;
    }
    ui->overlayBodyHost = overlayBodyHost;
    e9ui_windowOpen(ui->windowState.windowHost,
                    "RAM/ROMS",
                    rect,
                    overlayBodyHost,
                    mega_memview_overlayWindowCloseRequested,
                    ui,
                    &e9ui->ctx);
    ui->ctx = e9ui->ctx;
    ui->windowState.open = 1;
    aux_window_register(&mega_memview_auxWindowOps, ui);
    return 1;
}

static void
mega_memview_shutdown(void)
{
    mega_memview_state_t *ui = &mega_memview_stateSingleton;

    if (!ui->windowState.open) {
        return;
    }
    aux_window_unregister(&mega_memview_auxWindowOps, ui);
    if (ui->windowState.windowHost) {
        e9ui_windowDestroy(ui->windowState.windowHost);
        ui->windowState.windowHost = NULL;
    }
    ui->windowState.open = 0;
    ui->root = NULL;
    mega_memview_releaseRuntimeState(ui);
}

void
mega_memview_toggle(void)
{
    if (mega_memview_isOpen()) {
        mega_memview_shutdown();
    } else {
        (void)mega_memview_init();
    }
}

int
mega_memview_isOpen(void)
{
    return mega_memview_stateSingleton.windowState.open ? 1 : 0;
}

void
mega_memview_render(void)
{
    mega_memview_state_t *ui = &mega_memview_stateSingleton;

    if (!ui->windowState.open) {
        return;
    }
    if (e9ui_windowCaptureStateRectChanged(&ui->windowState, &e9ui->ctx)) {
        config_saveConfig();
    }
}

void
mega_memview_persistConfig(FILE *file)
{
    mega_memview_state_t *ui = &mega_memview_stateSingleton;

    if (!file) {
        return;
    }
    e9ui_windowPersistStateRect(file, "comp.mega_memview", &ui->windowState, &e9ui->ctx);
    if (ui->modeHasSaved) {
        fprintf(file, "comp.mega_memview.mode=%d\n", (int)ui->mode);
    }
    if (ui->ramBaseAddrHasSaved) {
        fprintf(file, "comp.mega_memview.ram_base_addr=%u\n", (unsigned)ui->ramBaseAddr);
    }
    if (ui->vramBaseAddrHasSaved) {
        fprintf(file, "comp.mega_memview.vram_base_addr=%u\n", (unsigned)ui->vramBaseAddr);
    }
    if (ui->zramBaseAddrHasSaved) {
        fprintf(file, "comp.mega_memview.zram_base_addr=%u\n", (unsigned)ui->zramBaseAddr);
    }
    if (ui->ramRowBytesHasSaved) {
        fprintf(file, "comp.mega_memview.ram_row_bytes=%u\n", (unsigned)ui->ramRowBytes);
    }
    if (ui->vramTilesPerRowHasSaved) {
        fprintf(file, "comp.mega_memview.vram_tiles_per_row=%u\n", (unsigned)ui->vramTilesPerRow);
    }
    if (ui->zoomHasSaved) {
        fprintf(file, "comp.mega_memview.zoom=%d\n", ui->zoomLevel);
    }
    if (ui->overviewZoomHasSaved) {
        fprintf(file, "comp.mega_memview.overview_zoom=%d\n", ui->overviewZoomLevel);
    }
    for (size_t i = 0u; i < mega_memview_checkboxBindingCount(); ++i) {
        mega_memview_checkbox_binding_t *binding = &mega_memview_checkboxBindings[i];

        if (*binding->hasSaved) {
            fprintf(file, "comp.mega_memview.%s=%d\n", binding->configProp, *binding->value ? 1 : 0);
        }
    }
}

int
mega_memview_loadConfigProperty(const char *prop, const char *value)
{
    mega_memview_state_t *ui = &mega_memview_stateSingleton;
    int intValue = 0;
    unsigned long long parsed = 0u;
    char *end = NULL;

    if (!prop || !value) {
        return 0;
    }
    for (size_t i = 0u; i < mega_memview_checkboxBindingCount(); ++i) {
        mega_memview_checkbox_binding_t *binding = &mega_memview_checkboxBindings[i];

        if (strcmp(prop, binding->configProp) == 0) {
            if (!mega_memview_parseInt(value, &intValue)) {
                return 0;
            }
            *binding->value = intValue ? 1 : 0;
            *binding->hasSaved = 1;
            return 1;
        }
    }
    if (strcmp(prop, "win_x") == 0) {
        if (!mega_memview_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winX = intValue;
        ui->windowState.winHasSaved = e9ui_windowHasSavedPosition(ui->windowState.winX, ui->windowState.winY);
        return 1;
    }
    if (strcmp(prop, "win_y") == 0) {
        if (!mega_memview_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winY = intValue;
        ui->windowState.winHasSaved = e9ui_windowHasSavedPosition(ui->windowState.winX, ui->windowState.winY);
        return 1;
    }
    if (strcmp(prop, "win_w") == 0) {
        if (!mega_memview_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winW = intValue;
        ui->windowState.winHasSaved = e9ui_windowHasSavedPosition(ui->windowState.winX, ui->windowState.winY);
        return 1;
    }
    if (strcmp(prop, "win_h") == 0) {
        if (!mega_memview_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winH = intValue;
        ui->windowState.winHasSaved = e9ui_windowHasSavedPosition(ui->windowState.winX, ui->windowState.winY);
        return 1;
    }
    if (strcmp(prop, "mode") == 0) {
        if (!mega_memview_parseInt(value, &intValue)) {
            return 0;
        }
        if (intValue == (int)mega_memview_mode_roms) {
            ui->mode = mega_memview_mode_roms;
        } else if (intValue == (int)mega_memview_mode_zram) {
            ui->mode = mega_memview_mode_zram;
        } else if (intValue == (int)mega_memview_mode_vram) {
            ui->mode = mega_memview_mode_vram;
        } else {
            ui->mode = mega_memview_mode_ram;
        }
        ui->modeHasSaved = 1;
        return 1;
    }
    if (strcmp(prop, "ram_base_addr") == 0) {
        if (!mega_memview_parseU64SmartHex(value, &parsed, &end) || !end || *end != '\0') {
            return 0;
        }
        ui->ramBaseAddr = mega_memview_clampRamBaseAddr((uint32_t)parsed);
        ui->ramBaseAddrHasSaved = 1;
        return 1;
    }
    if (strcmp(prop, "vram_base_addr") == 0) {
        if (!mega_memview_parseU64SmartHex(value, &parsed, &end) || !end || *end != '\0') {
            return 0;
        }
        ui->vramBaseAddr = mega_memview_alignVramBaseAddrToRow(ui, (uint32_t)parsed);
        ui->vramBaseAddrHasSaved = 1;
        return 1;
    }
    if (strcmp(prop, "zram_base_addr") == 0) {
        if (!mega_memview_parseU64SmartHex(value, &parsed, &end) || !end || *end != '\0') {
            return 0;
        }
        ui->zramBaseAddr = mega_memview_clampZramBaseAddr((uint32_t)parsed);
        ui->zramBaseAddrHasSaved = 1;
        return 1;
    }
    if (strcmp(prop, "ram_row_bytes") == 0 || strcmp(prop, "row_bytes") == 0) {
        if (!mega_memview_parseU64SmartHex(value, &parsed, &end) || !end || *end != '\0' || parsed == 0u) {
            return 0;
        }
        ui->ramRowBytes = mega_memview_clampRamRowBytes((uint32_t)parsed);
        ui->ramRowBytesHasSaved = 1;
        return 1;
    }
    if (strcmp(prop, "vram_tiles_per_row") == 0) {
        if (!mega_memview_parseU64SmartHex(value, &parsed, &end) || !end || *end != '\0' || parsed == 0u) {
            return 0;
        }
        ui->vramTilesPerRow = mega_memview_clampVramTilesPerRow((uint32_t)parsed);
        ui->vramTilesPerRowHasSaved = 1;
        if (ui->vramBaseAddrHasSaved) {
            ui->vramBaseAddr = mega_memview_alignVramBaseAddrToRow(ui, ui->vramBaseAddr);
        }
        return 1;
    }
    if (strcmp(prop, "zoom") == 0) {
        if (!mega_memview_parseInt(value, &intValue)) {
            return 0;
        }
        ui->zoomLevel = mega_memview_clampZoomLevel(intValue);
        ui->zoomHasSaved = 1;
        return 1;
    }
    if (strcmp(prop, "overview_zoom") == 0) {
        if (!mega_memview_parseInt(value, &intValue)) {
            return 0;
        }
        ui->overviewZoomLevel = mega_memview_clampOverviewZoomLevel(intValue);
        ui->overviewZoomHasSaved = 1;
        return 1;
    }
    return 0;
}
