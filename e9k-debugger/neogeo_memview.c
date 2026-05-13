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
#include "neogeo_memview_internal.h"

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

typedef struct neogeo_memview_checkbox_binding {
    int *value;
    int *hasSaved;
    int resetFollow;
} neogeo_memview_checkbox_binding_t;

typedef struct neogeo_memview_step_buttons_action_ctx {
    neogeo_memview_state_t *ui;
    e9ui_component_t *canvas;
} neogeo_memview_step_buttons_action_ctx_t;

static neogeo_memview_state_t neogeo_memview_stateSingleton = {
    .windowState.winX = E9UI_WINDOW_COORD_UNSET,
    .windowState.winY = E9UI_WINDOW_COORD_UNSET,
    .windowState.openMinWidthNoSavedSizePx = 640,
    .windowState.openMinHeightNoSavedSizePx = 360,
    .windowState.openCenterWhenNoSaved = 1,
    .mode = neogeo_memview_mode_ram,
    .ramBaseAddr = NEOGEO_MEMVIEW_RAM_BASE_MIN,
    .cromBaseAddr = 0u,
    .zramBaseAddr = NEOGEO_MEMVIEW_ZRAM_BASE_MIN,
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

static const neogeo_memview_mode_t neogeo_memview_modeButtonModes[] = {
    neogeo_memview_mode_ram,
    neogeo_memview_mode_crom,
    neogeo_memview_mode_zram,
    neogeo_memview_mode_roms
};

static neogeo_memview_checkbox_binding_t neogeo_memview_checkboxBindings[] = {
    { &neogeo_memview_stateSingleton.showAddressColumn, &neogeo_memview_stateSingleton.showAddressColumnHasSaved, 0 },
    { &neogeo_memview_stateSingleton.showOverviewColumn, &neogeo_memview_stateSingleton.showOverviewColumnHasSaved, 0 },
    { &neogeo_memview_stateSingleton.followActiveSprites, &neogeo_memview_stateSingleton.followActiveSpritesHasSaved, 1 }
};

static int
neogeo_memview_stepButtonsGutterWidth(const e9ui_context_t *ctx, e9ui_component_t *self);

static int
neogeo_memview_canvasVisibleRows(const neogeo_memview_state_t *ui, const e9ui_rect_t *bounds);

static uint32_t
neogeo_memview_clampBaseForView(const neogeo_memview_state_t *ui, const e9ui_rect_t *bounds, uint32_t baseAddr);

static void
neogeo_memview_syncTextboxesFromState(neogeo_memview_state_t *ui);

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
neogeo_memview_clampInt(int value, int minValue, int maxValue)
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
neogeo_memview_clampZoomLevel(int zoomLevel)
{
    return neogeo_memview_clampInt(zoomLevel, NEOGEO_MEMVIEW_ZOOM_MIN, NEOGEO_MEMVIEW_ZOOM_MAX);
}

int
neogeo_memview_clampOverviewZoomLevel(int zoomLevel)
{
    return neogeo_memview_clampInt(zoomLevel, NEOGEO_MEMVIEW_OVERVIEW_ZOOM_MIN, NEOGEO_MEMVIEW_OVERVIEW_ZOOM_MAX);
}

static uint32_t
neogeo_memview_clampU32(uint32_t value, uint32_t minValue, uint32_t maxValue)
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
neogeo_memview_clampRamRowBytes(uint32_t rowBytes)
{
    return neogeo_memview_clampU32(rowBytes ? rowBytes : NEOGEO_MEMVIEW_DEFAULT_RAM_ROW_BYTES,
                                   1u,
                                   NEOGEO_MEMVIEW_MAX_RAM_ROW_BYTES);
}

uint32_t
neogeo_memview_clampCromTilesPerRow(uint32_t tilesPerRow)
{
    return neogeo_memview_clampU32(tilesPerRow ? tilesPerRow : NEOGEO_MEMVIEW_DEFAULT_CROM_TILES_PER_ROW,
                                   1u,
                                   NEOGEO_MEMVIEW_MAX_CROM_TILES_PER_ROW);
}

uint32_t
neogeo_memview_clampRamBaseAddr(uint32_t baseAddr)
{
    return neogeo_memview_clampU32(baseAddr, NEOGEO_MEMVIEW_RAM_BASE_MIN, NEOGEO_MEMVIEW_RAM_BASE_MAX);
}

uint32_t
neogeo_memview_clampZramBaseAddr(uint32_t baseAddr)
{
    return neogeo_memview_clampU32(baseAddr, NEOGEO_MEMVIEW_ZRAM_BASE_MIN, NEOGEO_MEMVIEW_ZRAM_BASE_MAX);
}

uint32_t
neogeo_memview_cromSize(void)
{
    e9k_debug_rom_region_t crom = { 0 };

    if (!libretro_host_neogeo_getCRom(&crom)) {
        return 0u;
    }
    if (crom.size > 0xffffffffu) {
        return 0xffffffffu;
    }
    return (uint32_t)crom.size;
}

uint32_t
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
    rowBytes = neogeo_memview_clampCromTilesPerRow(ui->cromTilesPerRow) * NEOGEO_MEMVIEW_TILE_BYTES;
    if (rowBytes == 0u) {
        return baseAddr;
    }
    baseAddr -= baseAddr % rowBytes;
    return neogeo_memview_clampCromBaseAddr(baseAddr);
}

static int
neogeo_memview_zoomScaledPx(const neogeo_memview_state_t *ui, int basePx)
{
    int scaled = e9ui_scale_px(&ui->ctx, basePx);
    int zoomLevel = neogeo_memview_clampZoomLevel(ui->zoomLevel);

    scaled = (scaled * zoomLevel) / 8;
    if (scaled < 1) {
        scaled = 1;
    }
    return scaled;
}

static int
neogeo_memview_ramBitPx(const neogeo_memview_state_t *ui)
{
    return neogeo_memview_zoomScaledPx(ui, NEOGEO_MEMVIEW_RAM_BIT_PX);
}

static int
neogeo_memview_ramRowPx(const neogeo_memview_state_t *ui)
{
    return neogeo_memview_zoomScaledPx(ui, NEOGEO_MEMVIEW_RAM_ROW_PX);
}

static int
neogeo_memview_tilePixelPx(const neogeo_memview_state_t *ui)
{
    return neogeo_memview_ramBitPx(ui);
}

uint32_t
neogeo_memview_currentBaseAddr(const neogeo_memview_state_t *ui)
{
    if (ui->mode == neogeo_memview_mode_crom) {
        return ui->cromBaseAddr;
    }
    if (ui->mode == neogeo_memview_mode_zram) {
        return ui->zramBaseAddr;
    }
    return ui->ramBaseAddr;
}

static void
neogeo_memview_setCurrentBaseAddrSaved(neogeo_memview_state_t *ui, uint32_t baseAddr)
{
    if (ui->mode == neogeo_memview_mode_crom) {
        ui->cromBaseAddr = baseAddr;
        ui->cromBaseAddrHasSaved = 1;
    } else if (ui->mode == neogeo_memview_mode_zram) {
        ui->zramBaseAddr = baseAddr;
        ui->zramBaseAddrHasSaved = 1;
    } else {
        ui->ramBaseAddr = baseAddr;
        ui->ramBaseAddrHasSaved = 1;
    }
}

uint32_t
neogeo_memview_cromTotalRows(const neogeo_memview_state_t *ui)
{
    uint32_t cromTiles = neogeo_memview_cromSize() / NEOGEO_MEMVIEW_TILE_BYTES;
    uint32_t tilesPerRow = neogeo_memview_clampCromTilesPerRow(ui->cromTilesPerRow);

    if (cromTiles == 0u || tilesPerRow == 0u) {
        return 0u;
    }
    return (cromTiles + tilesPerRow - 1u) / tilesPerRow;
}

uint32_t
neogeo_memview_currentRowBytes(const neogeo_memview_state_t *ui)
{
    if (ui->mode == neogeo_memview_mode_crom) {
        return neogeo_memview_clampCromTilesPerRow(ui->cromTilesPerRow) * NEOGEO_MEMVIEW_TILE_BYTES;
    }
    if (ui->mode == neogeo_memview_mode_roms) {
        return 1u;
    }
    return neogeo_memview_clampRamRowBytes(ui->ramRowBytes);
}

static void
neogeo_memview_countActiveSpriteRows(const e9k_debug_sprite_state_t *spriteState,
                                     uint32_t cromTiles,
                                     uint32_t tilesPerRow,
                                     uint32_t totalRows,
                                     const uint16_t *prevVram,
                                     size_t prevVramWords,
                                     uint32_t *rowActiveCounts,
                                     uint32_t *rowChangedCounts)
{
    const uint16_t *vram = NULL;
    const uint16_t *scb3 = NULL;
    int screenHeight = 0;

    if (!spriteState || !spriteState->vram ||
        spriteState->vram_words <= NEOGEO_MEMVIEW_SCB3_WORD_OFFSET + NEOGEO_MEMVIEW_SPRITE_CHAIN_SCAN_END ||
        cromTiles == 0u || tilesPerRow == 0u || totalRows == 0u || !rowActiveCounts) {
        return;
    }
    vram = spriteState->vram;
    scb3 = vram + NEOGEO_MEMVIEW_SCB3_WORD_OFFSET;
    screenHeight = spriteState->screen_h > 0 ? spriteState->screen_h : 256;
    for (unsigned i = NEOGEO_MEMVIEW_SPRITE_FIRST_VISIBLE; i < NEOGEO_MEMVIEW_SPRITE_CHAIN_SCAN_END; ) {
        unsigned len = 1u;
        uint16_t scb3w = 0u;
        unsigned spriteRows = 0u;
        unsigned ypos = 0u;

        scb3w = scb3[i];
        if (scb3w & NEOGEO_MEMVIEW_SCB3_CHAIN_FLAG) {
            ++i;
            continue;
        }
        while ((i + len) < NEOGEO_MEMVIEW_SPRITE_CHAIN_SCAN_END &&
               (scb3[i + len] & NEOGEO_MEMVIEW_SCB3_CHAIN_FLAG)) {
            ++len;
        }
        spriteRows = (unsigned)(scb3w & NEOGEO_MEMVIEW_SCB3_ROW_MASK);
        ypos = (unsigned)((scb3w >> NEOGEO_MEMVIEW_SCB3_YPOS_SHIFT) & NEOGEO_MEMVIEW_SCB3_YPOS_MASK);
        if (spriteRows != 0u && ypos != (unsigned)screenHeight) {
            if (spriteRows > NEOGEO_MEMVIEW_SPRITE_MAX_ROWS) {
                spriteRows = NEOGEO_MEMVIEW_SPRITE_MAX_ROWS;
            }
            for (unsigned chainIndex = 0u; chainIndex < len; ++chainIndex) {
                unsigned spriteIndex = i + chainIndex;

                for (unsigned row = 0u; row < spriteRows; ++row) {
                    unsigned evenWordOffset = spriteIndex * NEOGEO_MEMVIEW_SPRITE_VRAM_WORDS_PER_SPRITE +
                                              row * NEOGEO_MEMVIEW_SPRITE_TILE_ROW_WORDS;
                    unsigned oddWordOffset = evenWordOffset + 1u;
                    uint32_t spriteTileNum = 0u;
                    uint32_t spriteRow = 0u;

                    if (oddWordOffset >= spriteState->vram_words) {
                        break;
                    }
                    spriteTileNum = (uint32_t)(vram[evenWordOffset] |
                                               ((vram[oddWordOffset] & NEOGEO_MEMVIEW_SPRITE_TILE_HIGH_MASK) <<
                                                NEOGEO_MEMVIEW_SPRITE_TILE_HIGH_SHIFT));
                    spriteTileNum %= cromTiles;
                    spriteRow = spriteTileNum / tilesPerRow;
                    if (spriteRow < totalRows) {
                        rowActiveCounts[spriteRow]++;
                        if (rowChangedCounts &&
                            prevVram &&
                            prevVramWords == spriteState->vram_words &&
                            (prevVram[evenWordOffset] != vram[evenWordOffset] ||
                             prevVram[oddWordOffset] != vram[oddWordOffset])) {
                            rowChangedCounts[spriteRow]++;
                        }
                    }
                }
            }
        }
        i += len;
    }
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

    if (!bounds || ui->mode != neogeo_memview_mode_crom || !ui->followActiveSprites) {
        return;
    }
    if (!libretro_host_neogeo_getSpriteState(&spriteState) || !spriteState.vram || spriteState.vram_words == 0u) {
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
    neogeo_memview_countActiveSpriteRows(&spriteState,
                                         cromTiles,
                                         tilesPerRow,
                                         totalRows,
                                         ui->followPrevSpriteVram,
                                         ui->followPrevSpriteVramWords,
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
    if ((!ui->followPrevSpriteVramWords || ui->followPrevSpriteVramWords != spriteState.vram_words) &&
        bootstrapBestActiveSum > 0u && bootstrapBestStartRow != currentRow) {
        neogeo_memview_setCurrentBaseAddrSaved(ui,
                                               neogeo_memview_clampBaseForView(ui,
                                                                               bounds,
                                                                               bootstrapBestStartRow * tilesPerRow * NEOGEO_MEMVIEW_TILE_BYTES));
        ui->followPendingStartRow = bootstrapBestStartRow;
        ui->followPendingFrames = 0u;
        neogeo_memview_syncTextboxesFromState(ui);
        config_saveConfig();
    }
    if (ui->followPrevSpriteVram && ui->followPrevSpriteVramWords == spriteState.vram_words &&
        bestQualified && ui->followPendingFrames >= NEOGEO_MEMVIEW_FOLLOW_STABLE_FRAMES) {
        neogeo_memview_setCurrentBaseAddrSaved(ui,
                                               neogeo_memview_clampBaseForView(ui,
                                                                               bounds,
                                                                               bestStartRow * tilesPerRow * NEOGEO_MEMVIEW_TILE_BYTES));
        ui->followPendingStartRow = bestStartRow;
        ui->followPendingFrames = 0u;
        neogeo_memview_syncTextboxesFromState(ui);
        config_saveConfig();
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
    uint32_t cromTiles = 0u;
    uint32_t tilesPerRow = 0u;
    uint32_t totalRows = 0u;
    uint32_t *rowActiveCounts = NULL;
    uint32_t bestRow = 0u;
    uint32_t bestCount = 0u;
    uint32_t baseAddr = 0u;

    if (!libretro_host_neogeo_getSpriteState(&spriteState) || !spriteState.vram || spriteState.vram_words == 0u) {
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
    neogeo_memview_countActiveSpriteRows(&spriteState,
                                         cromTiles,
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
    baseAddr = bestCount > 0u ? bestRow * tilesPerRow * NEOGEO_MEMVIEW_TILE_BYTES : 0u;
    alloc_free(rowActiveCounts);
    return neogeo_memview_alignCromBaseAddrToRow(ui, baseAddr);
}

static int
neogeo_memview_canvasVisibleRows(const neogeo_memview_state_t *ui, const e9ui_rect_t *bounds)
{
    int usableHeight = 0;
    int rowPx = 1;

    if (!bounds) {
        return 1;
    }
    usableHeight = bounds->h - e9ui_scale_px(&ui->ctx, NEOGEO_MEMVIEW_TOP_PAD_PX) -
                   e9ui_scale_px(&ui->ctx, NEOGEO_MEMVIEW_BOTTOM_PAD_PX);
    if (usableHeight < 1) {
        usableHeight = 1;
    }
    if (ui->mode == neogeo_memview_mode_roms) {
        rowPx = e9ui_scale_px(&ui->ctx, 48);
    } else if (ui->mode == neogeo_memview_mode_crom) {
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

    if (!bounds) {
        return baseAddr;
    }
    if (ui->mode == neogeo_memview_mode_roms) {
        return 0u;
    }
    if (ui->mode == neogeo_memview_mode_crom) {
        rangeStart = 0u;
        rangeSize = neogeo_memview_cromSize();
        if (rangeSize < NEOGEO_MEMVIEW_TILE_BYTES) {
            return 0u;
        }
    } else if (ui->mode == neogeo_memview_mode_zram) {
        rangeStart = NEOGEO_MEMVIEW_ZRAM_BASE_MIN;
        rangeSize = NEOGEO_MEMVIEW_ZRAM_BASE_MAX - NEOGEO_MEMVIEW_ZRAM_BASE_MIN + 1u;
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
    } else if (ui->mode == neogeo_memview_mode_zram) {
        baseAddr = neogeo_memview_clampZramBaseAddr(baseAddr);
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

    widthValue = ui->mode == neogeo_memview_mode_crom ?
        neogeo_memview_clampCromTilesPerRow(ui->cromTilesPerRow) :
        neogeo_memview_clampRamRowBytes(ui->ramRowBytes);

    if (ui->addressBox) {
        if (ui->mode == neogeo_memview_mode_roms) {
            e9ui_textbox_setPlaceholder(ui->addressBox, "");
            e9ui_textbox_setText(ui->addressBox, "");
            e9ui_setDisabled(ui->addressBox, 1);
        } else {
            baseAddr = neogeo_memview_currentBaseAddr(ui);
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
neogeo_memview_updateModeButton(e9ui_component_t *button, int active, const e9k_theme_button_t *activeTheme)
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
neogeo_memview_updateModeButtons(neogeo_memview_state_t *ui)
{
    const e9k_theme_button_t *activeTheme = e9ui_theme_button_preset_profile_active();

    neogeo_memview_updateModeButton(ui->modeButtonRam, ui->mode == neogeo_memview_mode_ram, activeTheme);
    neogeo_memview_updateModeButton(ui->modeButtonCrom, ui->mode == neogeo_memview_mode_crom, activeTheme);
    neogeo_memview_updateModeButton(ui->modeButtonZram, ui->mode == neogeo_memview_mode_zram, activeTheme);
    neogeo_memview_updateModeButton(ui->modeButtonRoms, ui->mode == neogeo_memview_mode_roms, activeTheme);
}

void
neogeo_memview_setView(neogeo_memview_state_t *ui, uint32_t baseAddr, uint32_t rowBytes, int resetScroll)
{
    const e9ui_rect_t *bounds = ui->canvas ? &ui->canvas->bounds : &(e9ui_rect_t){ 0, 0, 640, 360 };

    if (ui->mode == neogeo_memview_mode_crom) {
        ui->cromTilesPerRow = neogeo_memview_clampCromTilesPerRow(rowBytes / NEOGEO_MEMVIEW_TILE_BYTES);
        ui->cromTilesPerRowHasSaved = 1;
    } else if (ui->mode == neogeo_memview_mode_zram) {
        ui->ramRowBytes = neogeo_memview_clampRamRowBytes(rowBytes);
        ui->ramRowBytesHasSaved = 1;
    } else if (ui->mode == neogeo_memview_mode_roms) {
        ui->ramRowBytes = neogeo_memview_clampRamRowBytes(rowBytes);
        ui->ramRowBytesHasSaved = 1;
    } else {
        ui->ramRowBytes = neogeo_memview_clampRamRowBytes(rowBytes);
        ui->ramRowBytesHasSaved = 1;
    }
    neogeo_memview_setCurrentBaseAddrSaved(ui, neogeo_memview_clampBaseForView(ui, bounds, baseAddr));
    if (resetScroll) {
        ui->scrollX = 0;
    }
    neogeo_memview_syncTextboxesFromState(ui);
    config_saveConfig();
}

int
neogeo_memview_readRange(const neogeo_memview_state_t *ui, uint32_t addr, void *out, size_t sizeBytes)
{
    e9k_debug_rom_region_t crom = { 0 };

    if (!out || sizeBytes == 0u) {
        return 0;
    }
    if (ui->mode == neogeo_memview_mode_crom) {
        if (!libretro_host_neogeo_getCRom(&crom) || !crom.data) {
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
    if (ui->mode == neogeo_memview_mode_zram) {
        uint32_t readAddr = addr;
        size_t dstOffset = 0u;
        size_t readableBytes = sizeBytes;

        memset(out, 0, sizeBytes);
        if (readAddr < NEOGEO_MEMVIEW_ZRAM_BASE_MIN) {
            dstOffset = (size_t)(NEOGEO_MEMVIEW_ZRAM_BASE_MIN - readAddr);
            if (dstOffset >= sizeBytes) {
                return 0;
            }
            readAddr = NEOGEO_MEMVIEW_ZRAM_BASE_MIN;
            readableBytes = sizeBytes - dstOffset;
        }
        if (readAddr > NEOGEO_MEMVIEW_ZRAM_BASE_MAX) {
            return 0;
        }
        if ((uint64_t)readAddr + (uint64_t)readableBytes > (uint64_t)NEOGEO_MEMVIEW_ZRAM_BASE_MAX + 1u) {
            readableBytes = (size_t)((uint64_t)NEOGEO_MEMVIEW_ZRAM_BASE_MAX + 1u - (uint64_t)readAddr);
        }
        return libretro_host_debugReadProcessorMemory(NEOGEO_MEMVIEW_Z80_PROCESSOR_ID,
                                                      readAddr,
                                                      (uint8_t *)out + dstOffset,
                                                      readableBytes) ? 1 : 0;
    }
    if (ui->mode == neogeo_memview_mode_roms) {
        memset(out, 0, sizeBytes);
        return 0;
    }
    return libretro_host_debugReadMemory(addr, out, sizeBytes) ? 1 : 0;
}

uint32_t
neogeo_memview_argb(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

uint32_t
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

int
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

uint8_t
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
    if (!libretro_host_neogeo_getSpriteState(&spriteState) || !spriteState.vram || spriteState.vram_words == 0u) {
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
    for (unsigned i = 0; i < NEOGEO_MEMVIEW_SPRITE_COUNT; ++i) {
        for (unsigned row = 0; row < NEOGEO_MEMVIEW_SPRITE_MAX_ROWS; ++row) {
            unsigned evenWordOffset = i * NEOGEO_MEMVIEW_SPRITE_VRAM_WORDS_PER_SPRITE +
                                      row * NEOGEO_MEMVIEW_SPRITE_TILE_ROW_WORDS;
            unsigned oddWordOffset = evenWordOffset + 1u;
            uint32_t spriteTileNum = 0u;
            size_t tileIndex = 0u;
            uint8_t paletteBank = 0u;
            int inVisibleRange = 0;

            if (oddWordOffset >= spriteState.vram_words) {
                break;
            }
            spriteTileNum = (uint32_t)(vram[evenWordOffset] |
                                       ((vram[oddWordOffset] & NEOGEO_MEMVIEW_SPRITE_TILE_HIGH_MASK) <<
                                        NEOGEO_MEMVIEW_SPRITE_TILE_HIGH_SHIFT));
            spriteTileNum %= cromTiles;
            paletteBank = (uint8_t)((vram[oddWordOffset] >> NEOGEO_MEMVIEW_SPRITE_PALETTE_SHIFT) &
                                    NEOGEO_MEMVIEW_SPRITE_PALETTE_MASK);

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

static uint64_t
neogeo_memview_cromPaletteToken(const e9k_debug_palette_state_t *paletteState,
                                const uint8_t *tilePaletteBanks,
                                const uint8_t *tileHasPalette,
                                size_t visibleTileCount,
                                int haveLivePalette)
{
    uint64_t hash = NEOGEO_MEMVIEW_FNV1A64_OFFSET_BASIS;

    hash ^= (uint64_t)haveLivePalette;
    hash *= NEOGEO_MEMVIEW_FNV1A64_PRIME;
    if (!haveLivePalette || !paletteState || !tilePaletteBanks || !tileHasPalette) {
        return hash;
    }
    hash ^= (uint64_t)paletteState->active_bank;
    hash *= NEOGEO_MEMVIEW_FNV1A64_PRIME;
    hash ^= (uint64_t)paletteState->color_count;
    hash *= NEOGEO_MEMVIEW_FNV1A64_PRIME;
    for (size_t tileIndex = 0u; tileIndex < visibleTileCount; ++tileIndex) {
        uint8_t hasPalette = tileHasPalette[tileIndex];
        uint8_t paletteBank = hasPalette ? tilePaletteBanks[tileIndex] : 0u;
        uint32_t paletteOffset = paletteState->active_bank * 4096u + ((uint32_t)paletteBank << 4);

        hash ^= (uint64_t)hasPalette;
        hash *= NEOGEO_MEMVIEW_FNV1A64_PRIME;
        hash ^= (uint64_t)paletteBank;
        hash *= NEOGEO_MEMVIEW_FNV1A64_PRIME;
        if (!hasPalette || !paletteState->colors) {
            continue;
        }
        for (unsigned colorIndex = 1u; colorIndex < 16u; ++colorIndex) {
            if ((size_t)paletteOffset + (size_t)colorIndex < paletteState->color_count) {
                hash ^= (uint64_t)paletteState->colors[paletteOffset + colorIndex];
                hash *= NEOGEO_MEMVIEW_FNV1A64_PRIME;
            }
        }
    }
    return hash;
}

int
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
neogeo_memview_leftGutterPx(const neogeo_memview_state_t *ui, const e9ui_context_t *ctx, TTF_Font *font)
{
    int left = 0;

    if (ui->showAddressColumn) {
        left += neogeo_memview_measureAddressGutterPx(ctx, font);
    }
    if (ui->showOverviewColumn && neogeo_memview_overviewRangeCount(ui) > 0) {
        if (left > 0) {
            left += e9ui_scale_px(ctx, NEOGEO_MEMVIEW_GUTTER_GAP_PX);
        }
        left += e9ui_scale_px(ctx, NEOGEO_MEMVIEW_OVERVIEW_GUTTER_PX);
    }
    if (left > 0) {
        left += e9ui_scale_px(ctx, NEOGEO_MEMVIEW_GUTTER_GAP_PX);
    }
    return left;
}

static e9ui_rect_t
neogeo_memview_hscrollBounds(const neogeo_memview_state_t *ui, const e9ui_component_t *self)
{
    e9ui_rect_t bounds = { 0, 0, 0, 0 };
    int rightGutter = 0;

    if (!self) {
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

static void
neogeo_memview_scrollRows(neogeo_memview_state_t *ui, const e9ui_rect_t *bounds, int rows)
{
    int64_t delta = 0;
    uint32_t baseAddr = 0u;

    if (!bounds || rows == 0) {
        return;
    }
    if (ui->mode == neogeo_memview_mode_roms) {
        ui->romsScrollY += rows * e9ui_scale_px(&ui->ctx, 48);
        if (ui->romsScrollY < 0) {
            ui->romsScrollY = 0;
        }
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
    neogeo_memview_setCurrentBaseAddrSaved(ui, baseAddr);
    neogeo_memview_syncTextboxesFromState(ui);
    config_saveConfig();
}

static int
neogeo_memview_stepButtonsOnAction(void *user, e9ui_step_buttons_action_t action)
{
    neogeo_memview_step_buttons_action_ctx_t *actionCtx = (neogeo_memview_step_buttons_action_ctx_t *)user;
    int rows = 0;
    int pageRows = 0;

    if (!actionCtx || !actionCtx->canvas) {
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
neogeo_memview_toolbarWrapLayoutRow(e9ui_context_t *ctx,
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
neogeo_memview_toolbarWrapLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    neogeo_memview_toolbar_wrap_state_t *state = NULL;
    e9ui_component_t *rowChildren[NEOGEO_MEMVIEW_TOOLBAR_MAX_ROW_ITEMS];
    int rowWidths[NEOGEO_MEMVIEW_TOOLBAR_MAX_ROW_ITEMS];
    int rowHeights[NEOGEO_MEMVIEW_TOOLBAR_MAX_ROW_ITEMS];
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
    state = (neogeo_memview_toolbar_wrap_state_t *)self->state;
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
            neogeo_memview_toolbarWrapLayoutRow(ctx,
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
        if (rowCount < NEOGEO_MEMVIEW_TOOLBAR_MAX_ROW_ITEMS) {
            rowChildren[rowCount] = child;
            rowWidths[rowCount] = childW;
            rowHeights[rowCount] = childH;
            rowCount++;
        }
        x += childW + gap;
    }
    neogeo_memview_toolbarWrapLayoutRow(ctx,
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
    if (ui->mode == mode) {
        return;
    }
    ui->mode = mode;
    ui->modeHasSaved = 1;
    ui->scrollX = 0;
    ui->mainCromCacheValid = 0;
    ui->followPrevSpriteVramWords = 0u;
    neogeo_memview_initOverviewRanges(ui);
    neogeo_memview_syncTextboxesFromState(ui);
    neogeo_memview_updateModeButtons(ui);
    config_saveConfig();
}

static void
neogeo_memview_setMode(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    neogeo_memview_switchMode(&neogeo_memview_stateSingleton, *(const neogeo_memview_mode_t *)user);
}

static void
neogeo_memview_onCheckboxChanged(e9ui_component_t *self, e9ui_context_t *ctx, int checked, void *user)
{
    neogeo_memview_checkbox_binding_t *binding = (neogeo_memview_checkbox_binding_t *)user;

    (void)self;
    (void)ctx;
    *binding->value = checked ? 1 : 0;
    *binding->hasSaved = 1;
    if (binding->resetFollow) {
        neogeo_memview_stateSingleton.followPrevSpriteVramWords = 0u;
    }
    config_saveConfig();
}

static void
neogeo_memview_onAddressSubmit(e9ui_context_t *ctx, void *user)
{
    neogeo_memview_state_t *ui = (neogeo_memview_state_t *)user;
    const char *text = NULL;
    unsigned long long parsed = 0u;
    char *end = NULL;

    (void)ctx;
    if (!ui->addressBox) {
        return;
    }
    text = e9ui_textbox_getText(ui->addressBox);
    if (!neogeo_memview_parseU64SmartHex(text, &parsed, &end) || !end || *end != '\0') {
        neogeo_memview_syncTextboxesFromState(ui);
        return;
    }
    if (ui->mode == neogeo_memview_mode_roms) {
        neogeo_memview_syncTextboxesFromState(ui);
        return;
    }
    if (ui->mode == neogeo_memview_mode_crom) {
        neogeo_memview_setView(ui, neogeo_memview_clampCromBaseAddr((uint32_t)parsed), ui->cromTilesPerRow * NEOGEO_MEMVIEW_TILE_BYTES, 1);
    } else if (ui->mode == neogeo_memview_mode_zram) {
        neogeo_memview_setView(ui, neogeo_memview_clampZramBaseAddr((uint32_t)parsed), ui->ramRowBytes, 1);
    } else {
        neogeo_memview_setView(ui, neogeo_memview_clampRamBaseAddr((uint32_t)parsed), ui->ramRowBytes, 1);
    }
}

static void
neogeo_memview_setWidthValue(neogeo_memview_state_t *ui, uint32_t value)
{
    if (ui->mode == neogeo_memview_mode_crom) {
        ui->cromTilesPerRow = neogeo_memview_clampCromTilesPerRow(value);
        ui->cromTilesPerRowHasSaved = 1;
        ui->cromBaseAddr = neogeo_memview_alignCromBaseAddrToRow(ui, ui->cromBaseAddr);
        ui->cromBaseAddrHasSaved = 1;
    } else {
        ui->ramRowBytes = neogeo_memview_clampRamRowBytes(value);
        ui->ramRowBytesHasSaved = 1;
    }
    ui->scrollX = 0;
    neogeo_memview_syncTextboxesFromState(ui);
    config_saveConfig();
}

static void
neogeo_memview_onWidthSubmit(e9ui_context_t *ctx, void *user)
{
    neogeo_memview_state_t *ui = (neogeo_memview_state_t *)user;
    const char *text = NULL;
    unsigned long long parsed = 0u;
    char *end = NULL;

    (void)ctx;
    if (!ui->widthBox) {
        return;
    }
    text = e9ui_textbox_getText(ui->widthBox);
    if (!neogeo_memview_parseU64SmartHex(text, &parsed, &end) || !end || *end != '\0' || parsed == 0u) {
        neogeo_memview_syncTextboxesFromState(ui);
        return;
    }
    neogeo_memview_setWidthValue(ui, (uint32_t)parsed);
}

static void
neogeo_memview_widthSeekTooltip(float percent, char *out, size_t cap, void *user)
{
    neogeo_memview_state_t *ui = (neogeo_memview_state_t *)user;
    uint32_t value = 1u;

    if (!out || cap == 0u) {
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

    if (ui->mode == neogeo_memview_mode_crom) {
        value = 1u + (uint32_t)(percent * (float)(NEOGEO_MEMVIEW_MAX_CROM_TILES_PER_ROW - 1u) + 0.5f);
    } else {
        value = 1u + (uint32_t)(percent * (float)(NEOGEO_MEMVIEW_MAX_RAM_ROW_BYTES - 1u) + 0.5f);
    }
    neogeo_memview_setWidthValue(ui, value);
}

static void
neogeo_memview_onZoomSeekChanged(float percent, void *user)
{
    neogeo_memview_state_t *ui = (neogeo_memview_state_t *)user;
    int zoomLevel = 0;

    zoomLevel = NEOGEO_MEMVIEW_ZOOM_MIN +
        (int)(percent * (float)(NEOGEO_MEMVIEW_ZOOM_MAX - NEOGEO_MEMVIEW_ZOOM_MIN) + 0.5f);
    ui->zoomLevel = neogeo_memview_clampZoomLevel(zoomLevel);
    ui->zoomHasSaved = 1;
    ui->scrollX = 0;
    neogeo_memview_syncTextboxesFromState(ui);
    config_saveConfig();
}

static void
neogeo_memview_onOverviewZoomSeekChanged(float percent, void *user)
{
    neogeo_memview_state_t *ui = (neogeo_memview_state_t *)user;
    int zoomLevel = NEOGEO_MEMVIEW_OVERVIEW_ZOOM_MIN +
                    (int)(percent * (float)(NEOGEO_MEMVIEW_OVERVIEW_ZOOM_MAX - NEOGEO_MEMVIEW_OVERVIEW_ZOOM_MIN) + 0.5f);

    ui->overviewZoomLevel = neogeo_memview_clampOverviewZoomLevel(zoomLevel);
    ui->overviewZoomHasSaved = 1;
    config_saveConfig();
}

static int
neogeo_memview_keyStep(SDL_Keycode key)
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
neogeo_memview_handleWidthKey(neogeo_memview_state_t *ui, SDL_Keycode key)
{
    int nextValue = 0;

    nextValue = ui->mode == neogeo_memview_mode_crom ? (int)ui->cromTilesPerRow : (int)ui->ramRowBytes;
    if (key == SDLK_LEFT) {
        nextValue--;
    } else if (key == SDLK_RIGHT) {
        nextValue++;
    } else {
        return 0;
    }
    neogeo_memview_setWidthValue(ui, (uint32_t)nextValue);
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
    int step = 0;

    if (!self || !ctx || !ev) {
        return 0;
    }
    step = ev->type == SDL_KEYDOWN && e9ui_getFocus(ctx) == self ? neogeo_memview_keyStep(ev->key.keysym.sym) : 0;
    if (step != 0) {
        ui->zoomLevel = neogeo_memview_clampZoomLevel(ui->zoomLevel + step);
        ui->zoomHasSaved = 1;
        ui->scrollX = 0;
        neogeo_memview_syncTextboxesFromState(ui);
        config_saveConfig();
        return 1;
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
    int step = 0;

    if (!self || !ctx || !ev) {
        return 0;
    }
    step = ev->type == SDL_KEYDOWN && ev->key.repeat == 0 ? neogeo_memview_keyStep(ev->key.keysym.sym) : 0;
    if (step != 0) {
        ui->overviewZoomLevel = neogeo_memview_clampOverviewZoomLevel(ui->overviewZoomLevel + step);
        ui->overviewZoomHasSaved = 1;
        neogeo_memview_syncTextboxesFromState(ui);
        config_saveConfig();
        return 1;
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
neogeo_memview_renderMainTexture(neogeo_memview_state_t *ui,
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
neogeo_memview_canvasRenderRam(neogeo_memview_state_t *ui,
                               e9ui_context_t *ctx,
                               e9ui_component_t *self,
                               int bitViewW,
                               int rowAreaY,
                               int bitAreaX)
{
    int rowBytes = (int)neogeo_memview_clampRamRowBytes(ui->ramRowBytes);
    int bitPx = neogeo_memview_ramBitPx(ui);
    int rowPx = neogeo_memview_ramRowPx(ui);
    int visibleRows = neogeo_memview_canvasVisibleRows(ui, &self->bounds);
    int texW = 0;
    int texH = 0;
    uint8_t *rowData = NULL;
    size_t dataSize = (size_t)rowBytes * (size_t)visibleRows;
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    int labelStepRows = 8;
    int fontHeight = 16;
    uint32_t baseAddr = neogeo_memview_currentBaseAddr(ui);
    uint32_t rangeStart = NEOGEO_MEMVIEW_RAM_BASE_MIN;
    uint32_t rangeEnd = NEOGEO_MEMVIEW_RAM_BASE_MAX;

    ui->mainCromCacheValid = 0;
    ui->mainRomsCacheValid = 0;
    if (ui->mode == neogeo_memview_mode_zram) {
        uint32_t clampedBaseAddr = neogeo_memview_clampBaseForView(ui, &self->bounds, baseAddr);

        rangeStart = NEOGEO_MEMVIEW_ZRAM_BASE_MIN;
        rangeEnd = NEOGEO_MEMVIEW_ZRAM_BASE_MAX;
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
    texW = rowBytes * 8 * bitPx + e9ui_scale_px(&ui->ctx, NEOGEO_MEMVIEW_RIGHT_PAD_PX);
    texH = visibleRows * rowPx;
    dataSize = (size_t)rowBytes * (size_t)visibleRows;
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
    (void)neogeo_memview_readRange(ui, baseAddr, rowData, dataSize);

    for (int row = 0; row < visibleRows; ++row) {
        uint32_t rowAddr = baseAddr + (uint32_t)row * (uint32_t)rowBytes;

        if (ui->mode == neogeo_memview_mode_zram && (rowAddr < rangeStart || rowAddr > rangeEnd)) {
            continue;
        }
        if (ui->showAddressColumn && (row % labelStepRows) == 0 && font) {
            neogeo_memview_drawAddressLabel(ui, ctx, font, rowAddr, self->bounds.x + 6, rowAreaY + row * rowPx - 1);
        }
        for (int byteIndex = 0; byteIndex < rowBytes; ++byteIndex) {
            uint32_t byteAddr = rowAddr + (uint32_t)byteIndex;
            uint8_t value = rowData[row * rowBytes + byteIndex];
            int byteX = byteIndex * 8 * bitPx;

            if (ui->mode == neogeo_memview_mode_zram && (byteAddr < rangeStart || byteAddr > rangeEnd)) {
                continue;
            }
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

    neogeo_memview_renderMainTexture(ui, ctx, texW, texH, bitViewW, rowAreaY, bitAreaX, 1);
    alloc_free(rowData);
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
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    e9k_debug_palette_state_t paletteState;
    uint32_t *visibleTileNums = NULL;
    uint8_t *tilePaletteBanks = NULL;
    uint8_t *tileHasPalette = NULL;
    size_t visibleTileCount = 0u;
    int haveLivePalette = 0;
    uint64_t paletteToken = 0u;

    ui->mainRomsCacheValid = 0;
    if (!neogeo_memview_ensureTexture(ctx->renderer,
                                      &ui->mainTexture,
                                      &ui->mainPixels,
                                      &ui->mainPixelsCap,
                                      &ui->mainTextureW,
                                      &ui->mainTextureH,
                                      texW,
                                      texH)) {
        ui->mainCromCacheValid = 0;
        return;
    }
    visibleTileCount = (size_t)visibleRows * (size_t)tilesPerRow;
    if (visibleTileCount > 0u) {
        visibleTileNums = (uint32_t *)alloc_calloc(visibleTileCount, sizeof(*visibleTileNums));
        tilePaletteBanks = (uint8_t *)alloc_calloc(visibleTileCount, sizeof(*tilePaletteBanks));
        tileHasPalette = (uint8_t *)alloc_calloc(visibleTileCount, sizeof(*tileHasPalette));
        if (!visibleTileNums || !tilePaletteBanks || !tileHasPalette) {
            alloc_free(visibleTileNums);
            alloc_free(tilePaletteBanks);
            alloc_free(tileHasPalette);
            return;
        }
    }
    memset(&paletteState, 0, sizeof(paletteState));
    haveLivePalette = libretro_host_neogeo_getPaletteState(&paletteState) &&
                      paletteState.colors &&
                      paletteState.color_count > 0u;

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
    paletteToken = neogeo_memview_cromPaletteToken(&paletteState,
                                                   tilePaletteBanks,
                                                   tileHasPalette,
                                                   visibleTileCount,
                                                   haveLivePalette);

    for (int row = 0; row < visibleRows; ++row) {
        uint32_t rowAddr = ui->cromBaseAddr + (uint32_t)row * (uint32_t)tilesPerRow * NEOGEO_MEMVIEW_TILE_BYTES;

        if (ui->showAddressColumn && font) {
            neogeo_memview_drawAddressLabel(ui, ctx, font, rowAddr, self->bounds.x + 6, rowAreaY + row * rowStridePx - 1);
        }
    }
    if (ui->mainCromCacheValid &&
        ui->mainCromCacheBaseAddr == ui->cromBaseAddr &&
        ui->mainCromCacheCromTiles == cromTiles &&
        ui->mainCromCacheTilesPerRow == tilesPerRow &&
        ui->mainCromCacheVisibleRows == visibleRows &&
        ui->mainCromCachePixelPx == pixelPx &&
        ui->mainCromCacheTileGapPx == tileGapPx &&
        ui->mainCromCacheTexW == texW &&
        ui->mainCromCacheTexH == texH &&
        ui->mainCromCachePaletteToken == paletteToken) {
        neogeo_memview_renderMainTexture(ui, ctx, texW, texH, bitViewW, rowAreaY, bitAreaX, 0);
        alloc_free(visibleTileNums);
        alloc_free(tilePaletteBanks);
        alloc_free(tileHasPalette);
        return;
    }

    memset(ui->mainPixels, 0, (size_t)texW * (size_t)texH * sizeof(*ui->mainPixels));
    tileData = (uint8_t *)alloc_calloc(dataSize, 1);
    if (!tileData) {
        ui->mainCromCacheValid = 0;
        alloc_free(visibleTileNums);
        alloc_free(tilePaletteBanks);
        alloc_free(tileHasPalette);
        return;
    }
    (void)neogeo_memview_readRange(ui, ui->cromBaseAddr, tileData, dataSize);

    for (int row = 0; row < visibleRows; ++row) {
        for (int tile = 0; tile < tilesPerRow; ++tile) {
            size_t tileIndex = (size_t)row * (size_t)tilesPerRow + (size_t)tile;
            uint32_t tileBase = (uint32_t)((row * tilesPerRow + tile) * NEOGEO_MEMVIEW_TILE_BYTES);
            uint32_t paletteOffset = 0u;
            uint32_t colors[16];
            uint32_t rowPixels[NEOGEO_MEMVIEW_TILE_W * NEOGEO_MEMVIEW_ZOOM_MAX];
            int tileX = tile * tileStridePx;
            int tileY = row * rowStridePx;
            int useLiveTilePalette = haveLivePalette && tileHasPalette && tileHasPalette[tileIndex];

            for (unsigned colorIndex = 0u; colorIndex < 16u; ++colorIndex) {
                colors[colorIndex] = neogeo_memview_amberColor(colorIndex);
            }
            if (useLiveTilePalette) {
                paletteOffset = paletteState.active_bank * 4096u + ((uint32_t)tilePaletteBanks[tileIndex] << 4);
                for (unsigned colorIndex = 1u; colorIndex < 16u; ++colorIndex) {
                    if ((size_t)paletteOffset + (size_t)colorIndex < paletteState.color_count) {
                        colors[colorIndex] = paletteState.colors[paletteOffset + colorIndex];
                    }
                }
            }

            for (unsigned py = 0; py < NEOGEO_MEMVIEW_TILE_H; ++py) {
                int rowPixelCount = 0;

                for (unsigned half = 0u; half < 2u; ++half) {
                    uint32_t rowBase = tileBase + (half == 0u ? 64u : 0u) + py * 4u;
                    uint8_t plane0 = tileData[rowBase + 0u];
                    uint8_t plane1 = tileData[rowBase + 2u];
                    uint8_t plane2 = tileData[rowBase + 1u];
                    uint8_t plane3 = tileData[rowBase + 3u];

                    for (unsigned bit = 0u; bit < 8u; ++bit) {
                        uint32_t paletteIndex = ((uint32_t)(plane0 >> bit) & 1u) |
                                                (((uint32_t)(plane1 >> bit) & 1u) << 1u) |
                                                (((uint32_t)(plane2 >> bit) & 1u) << 2u) |
                                                (((uint32_t)(plane3 >> bit) & 1u) << 3u);
                        uint32_t color = colors[paletteIndex];

                        for (int zoomX = 0; zoomX < pixelPx; ++zoomX) {
                            rowPixels[rowPixelCount++] = color;
                        }
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

    ui->mainCromCacheValid = 1;
    ui->mainCromCacheBaseAddr = ui->cromBaseAddr;
    ui->mainCromCacheCromTiles = cromTiles;
    ui->mainCromCacheTilesPerRow = tilesPerRow;
    ui->mainCromCacheVisibleRows = visibleRows;
    ui->mainCromCachePixelPx = pixelPx;
    ui->mainCromCacheTileGapPx = tileGapPx;
    ui->mainCromCacheTexW = texW;
    ui->mainCromCacheTexH = texH;
    ui->mainCromCachePaletteToken = paletteToken;
    neogeo_memview_renderMainTexture(ui, ctx, texW, texH, bitViewW, rowAreaY, bitAreaX, 1);
    alloc_free(tileData);
    alloc_free(visibleTileNums);
    alloc_free(tilePaletteBanks);
    alloc_free(tileHasPalette);
}

static void
neogeo_memview_formatSize(size_t size, char *out, size_t cap)
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
neogeo_memview_appendText(char *out, size_t cap, size_t *pos, const char *text)
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
neogeo_memview_drawText(neogeo_memview_state_t *ui,
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
neogeo_memview_romSampleColor(const uint8_t *data, size_t size, size_t start, size_t end)
{
    uint64_t sum = 0u;
    size_t count = 0u;
    uint32_t intensity = 0u;
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;

    if (!data || size == 0u) {
        return neogeo_memview_argb(24, 27, 34);
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
    return neogeo_memview_argb(r, g, b);
}

static size_t
neogeo_memview_romEntryDisplaySize(const e9k_debug_rom_entry_t *rom);

static const uint8_t *
neogeo_memview_romEntryDisplayData(const e9k_debug_rom_entry_t *rom)
{
    size_t displaySize = 0u;

    if (!rom) {
        return NULL;
    }
    displaySize = neogeo_memview_romEntryDisplaySize(rom);
    if (displaySize != 0u && displaySize != rom->size && rom->size > 0x80000u && strcmp(rom->label, "M1") == 0) {
        return rom->data + 0x10000u;
    }
    return rom->data;
}

static size_t
neogeo_memview_romEntryDisplaySize(const e9k_debug_rom_entry_t *rom)
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
neogeo_memview_drawRomContents(neogeo_memview_state_t *ui,
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
    displayData = neogeo_memview_romEntryDisplayData(rom);
    displaySize = neogeo_memview_romEntryDisplaySize(rom);
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
                    neogeo_memview_romSampleColor(displayData, displaySize, start, end);
            }
        }
    }
}

static size_t
neogeo_memview_getRomEntries(e9k_debug_rom_entry_t *roms, size_t cap)
{
    size_t count = libretro_host_neogeo_getRoms(roms, cap);

    if (count == 0u && roms && cap >= 3u) {
        e9k_debug_rom_region_t region = { 0 };

        if (libretro_host_neogeo_getP1Rom(&region) && count < cap) {
            snprintf(roms[count].label, sizeof(roms[count].label), "P");
            roms[count].data = region.data;
            roms[count].size = region.size;
            count++;
        }
        if (libretro_host_neogeo_getFixRom(&region) && count < cap) {
            snprintf(roms[count].label, sizeof(roms[count].label), "FIX");
            roms[count].data = region.data;
            roms[count].size = region.size;
            count++;
        }
        if (libretro_host_neogeo_getCRom(&region) && count < cap) {
            snprintf(roms[count].label, sizeof(roms[count].label), "C");
            roms[count].data = region.data;
            roms[count].size = region.size;
            count++;
        }
    }
    return count;
}

static uint64_t
neogeo_memview_romsContentToken(const e9k_debug_rom_entry_t *roms, size_t romCount)
{
    uint64_t hash = NEOGEO_MEMVIEW_FNV1A64_OFFSET_BASIS;

    hash ^= (uint64_t)romCount;
    hash *= NEOGEO_MEMVIEW_FNV1A64_PRIME;
    for (size_t i = 0u; i < romCount; ++i) {
        const e9k_debug_rom_entry_t *rom = &roms[i];

        hash ^= (uint64_t)(uintptr_t)rom->data;
        hash *= NEOGEO_MEMVIEW_FNV1A64_PRIME;
        hash ^= (uint64_t)rom->size;
        hash *= NEOGEO_MEMVIEW_FNV1A64_PRIME;
        hash ^= (uint64_t)(uintptr_t)neogeo_memview_romEntryDisplayData(rom);
        hash *= NEOGEO_MEMVIEW_FNV1A64_PRIME;
        hash ^= (uint64_t)neogeo_memview_romEntryDisplaySize(rom);
        hash *= NEOGEO_MEMVIEW_FNV1A64_PRIME;
        for (size_t c = 0u; c < sizeof(rom->label) && rom->label[c]; ++c) {
            hash ^= (uint64_t)(uint8_t)rom->label[c];
            hash *= NEOGEO_MEMVIEW_FNV1A64_PRIME;
        }
    }
    return hash;
}

static void
neogeo_memview_initRomsEntries(neogeo_memview_state_t *ui)
{
    if (!ui || ui->mainRomsEntriesValid) {
        return;
    }
    memset(ui->mainRomsEntries, 0, sizeof(ui->mainRomsEntries));
    ui->mainRomsEntryCount = neogeo_memview_getRomEntries(ui->mainRomsEntries,
                                                          sizeof(ui->mainRomsEntries) / sizeof(ui->mainRomsEntries[0]));
    ui->mainRomsContentToken = neogeo_memview_romsContentToken(ui->mainRomsEntries, ui->mainRomsEntryCount);
    ui->mainRomsEntriesValid = 1;
}

static void
neogeo_memview_canvasRenderRoms(neogeo_memview_state_t *ui,
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

    ui->mainCromCacheValid = 0;
    neogeo_memview_initRomsEntries(ui);
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
    texW = pad * 2 + cols * cardW + (cols - 1) * gap + e9ui_scale_px(&ui->ctx, NEOGEO_MEMVIEW_RIGHT_PAD_PX);
    texH = gridH;
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

            neogeo_memview_fillRect(ui->mainPixels, texW, texW, texH, cardX, cardY, cardW, cardH, neogeo_memview_argb(21, 24, 31));
            neogeo_memview_fillRect(ui->mainPixels, texW, texW, texH, cardX, cardY, cardW, 1, neogeo_memview_argb(92, 101, 116));
            neogeo_memview_fillRect(ui->mainPixels, texW, texW, texH, cardX, cardY + cardH - 1, cardW, 1, neogeo_memview_argb(92, 101, 116));
            neogeo_memview_fillRect(ui->mainPixels, texW, texW, texH, cardX, cardY, 1, cardH, neogeo_memview_argb(92, 101, 116));
            neogeo_memview_fillRect(ui->mainPixels, texW, texW, texH, cardX + cardW - 1, cardY, 1, cardH, neogeo_memview_argb(92, 101, 116));
            neogeo_memview_drawRomContents(ui, &roms[i], texW, texH, contentX, cardY + contentY, contentW, contentH);
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
        neogeo_memview_formatSize(neogeo_memview_romEntryDisplaySize(&roms[i]), sizeLabel, sizeof(sizeLabel));
        label[0] = '\0';
        neogeo_memview_appendText(label, sizeof(label), &labelPos, roms[i].label[0] ? roms[i].label : "ROM");
        neogeo_memview_appendText(label, sizeof(label), &labelPos, "  ");
        neogeo_memview_appendText(label, sizeof(label), &labelPos, sizeLabel);
        neogeo_memview_drawText(ui, ctx, font, label, (SDL_Color){ 236, 238, 242, 255 }, cardX + contentPad, cardY + contentPad);
    }
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

    if (ui->mode == neogeo_memview_mode_roms) {
        neogeo_memview_canvasRenderRoms(ui, ctx, self, bitViewW, rowAreaY, bitAreaX);
    } else if (ui->mode == neogeo_memview_mode_crom) {
        neogeo_memview_canvasRenderCrom(ui, ctx, self, bitViewW, rowAreaY, bitAreaX);
    } else {
        neogeo_memview_canvasRenderRam(ui, ctx, self, bitViewW, rowAreaY, bitAreaX);
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

    canvas = (e9ui_component_t *)alloc_calloc(1, sizeof(*canvas));
    if (!canvas) {
        return NULL;
    }
    canvas->name = "neogeo_memview_canvas";
    canvas->state = ui;
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
    ui->canvas = NULL;
    ui->modeButtonRam = NULL;
    ui->modeButtonCrom = NULL;
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
neogeo_memview_buildRoot(neogeo_memview_state_t *ui)
{
    e9ui_component_t *root = e9ui_stack_makeVertical();
    e9ui_component_t *toolbar = neogeo_memview_makeToolbarWrap();

    ui->addressBox = e9ui_textbox_make(32, neogeo_memview_onAddressSubmit, NULL, ui);
    ui->widthBox = e9ui_textbox_make(16, neogeo_memview_onWidthSubmit, NULL, ui);
    ui->widthSeek = e9ui_seek_bar_make();
    ui->zoomSeek = e9ui_seek_bar_make();
    ui->overviewZoomSeek = e9ui_seek_bar_make();
    ui->modeButtonRam = e9ui_button_make("RAM", neogeo_memview_setMode, (void *)&neogeo_memview_modeButtonModes[0]);
    ui->modeButtonCrom = e9ui_button_make("CROM", neogeo_memview_setMode, (void *)&neogeo_memview_modeButtonModes[1]);
    ui->modeButtonZram = e9ui_button_make("ZRAM", neogeo_memview_setMode, (void *)&neogeo_memview_modeButtonModes[2]);
    ui->modeButtonRoms = e9ui_button_make("ROMS", neogeo_memview_setMode, (void *)&neogeo_memview_modeButtonModes[3]);
    ui->canvas = neogeo_memview_makeCanvas(ui);

    e9ui_component_t *addressLabel = e9ui_text_make("Address");
    e9ui_component_t *widthLabel = e9ui_text_make("Width");
    e9ui_component_t *zoomLabel = e9ui_text_make("Zoom");
    e9ui_component_t *overviewZoomLabel = e9ui_text_make("Ov Zoom");
    e9ui_component_t *showAddress = e9ui_checkbox_make("Addr", ui->showAddressColumn, neogeo_memview_onCheckboxChanged, &neogeo_memview_checkboxBindings[0]);
    e9ui_component_t *showOverview = e9ui_checkbox_make("Overview", ui->showOverviewColumn, neogeo_memview_onCheckboxChanged, &neogeo_memview_checkboxBindings[1]);
    e9ui_component_t *followActive = e9ui_checkbox_make("Follow", ui->followActiveSprites, neogeo_memview_onCheckboxChanged, &neogeo_memview_checkboxBindings[2]);

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
    e9ui_button_setMini(ui->modeButtonZram, 1);
    e9ui_button_setMini(ui->modeButtonRoms, 1);
    e9ui_button_setLargestLabel(ui->modeButtonRam, "CROM");
    e9ui_button_setLargestLabel(ui->modeButtonCrom, "CROM");
    e9ui_button_setLargestLabel(ui->modeButtonZram, "CROM");
    e9ui_button_setLargestLabel(ui->modeButtonRoms, "CROM");

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

    int labelAddrW = neogeo_memview_measureToolbarTextWidth(&ui->ctx, toolbarFont, "Address", 8, 64);
    int addrBoxW = neogeo_memview_measureToolbarTextboxWidth(toolbarFont, "0x00100000", 110);
    int labelWidthW = neogeo_memview_measureToolbarTextWidth(&ui->ctx, toolbarFont, "Width", 8, 48);
    int widthBoxW = neogeo_memview_measureToolbarTextboxWidth(toolbarFont, "256", 56);
    int widthSeekW = e9ui_scale_px(&ui->ctx, 180);
    int zoomLabelW = neogeo_memview_measureToolbarTextWidth(&ui->ctx, toolbarFont, "Zoom", 8, 40);
    int zoomSeekW = e9ui_scale_px(&ui->ctx, 180);
    int overviewZoomLabelW = neogeo_memview_measureToolbarTextWidth(&ui->ctx, toolbarFont, "Ov Zoom", 8, 56);
    int overviewZoomSeekW = e9ui_scale_px(&ui->ctx, 150);

    e9ui_component_t *groupGeneral = e9ui_hstack_make();
    e9ui_component_t *groupAddress = e9ui_hstack_make();
    e9ui_component_t *groupWidth = e9ui_hstack_make();
    e9ui_component_t *groupZoom = e9ui_hstack_make();
    e9ui_component_t *groupOverviewZoom = e9ui_hstack_make();

    e9ui_hstack_addFixed(groupGeneral, ui->modeButtonRam, modeButtonW);
    e9ui_hstack_addFixed(groupGeneral, e9ui_spacer_make(gapSmall), gapSmall);
    e9ui_hstack_addFixed(groupGeneral, ui->modeButtonCrom, modeButtonW);
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
    int groupGeneralW = modeButtonW + gapSmall + modeButtonW + gapSmall + modeButtonW + gapSmall + modeButtonW + gapSmall + checkboxAddrW + gapSmall + checkboxOverviewW + gapSmall + checkboxFollowW;

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

    e9ui_component_t *groupGeneralItem = neogeo_memview_makeToolbarItem(groupGeneral, groupGeneralW);
    e9ui_component_t *groupAddressItem = neogeo_memview_makeToolbarItem(groupAddress, groupAddressW);
    e9ui_component_t *groupWidthItem = neogeo_memview_makeToolbarItem(groupWidth, groupWidthW);
    e9ui_component_t *groupZoomItem = neogeo_memview_makeToolbarItem(groupZoom, groupZoomW);
    e9ui_component_t *groupOverviewZoomItem = neogeo_memview_makeToolbarItem(groupOverviewZoom, groupOverviewZoomW);

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
neogeo_memview_syncOverlayContext(neogeo_memview_state_t *ui, e9ui_context_t *ctx, e9ui_rect_t bounds)
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
neogeo_memview_overlayBodyLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    neogeo_memview_overlay_body_state_t *state = NULL;
    neogeo_memview_state_t *ui = NULL;

    if (!self || !ctx || !self->state) {
        return;
    }
    self->bounds = bounds;
    state = (neogeo_memview_overlay_body_state_t *)self->state;
    ui = state->ui;
    neogeo_memview_syncOverlayContext(ui, ctx, bounds);

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
    ui = state->ui;
    if (!ui->windowState.open) {
        return;
    }

    neogeo_memview_syncOverlayContext(ui, ctx, self->bounds);
    ui->ctx.mouseX = ctx->mouseX;
    ui->ctx.mouseY = ctx->mouseY;
    ui->ctx.mousePrevX = ctx->mousePrevX;
    ui->ctx.mousePrevY = ctx->mousePrevY;

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

    if (!ui->root) {
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
neogeo_memview_destroyTexture(SDL_Texture **texture)
{
    if (!*texture) {
        return;
    }
    SDL_DestroyTexture(*texture);
    *texture = NULL;
}

static void
neogeo_memview_releaseRuntimeState(neogeo_memview_state_t *ui)
{
    neogeo_memview_destroyTexture(&ui->mainTexture);
    neogeo_memview_destroyTexture(&ui->overviewTexture);
    neogeo_memview_destroyTexture(&ui->overviewBackgroundTexture);
    alloc_free(ui->mainPixels);
    alloc_free(ui->overviewPixels);
    alloc_free(ui->overviewTileRows);
    alloc_free(ui->overviewTileCols);
    alloc_free(ui->overviewBackgroundPixels);
    alloc_free(ui->followPrevSpriteVram);
    ui->mainPixels = NULL;
    ui->overviewPixels = NULL;
    ui->overviewTileRows = NULL;
    ui->overviewTileCols = NULL;
    ui->overviewBackgroundPixels = NULL;
    ui->followPrevSpriteVram = NULL;
    ui->mainPixelsCap = 0u;
    ui->overviewPixelsCap = 0u;
    ui->overviewTileMapCap = 0u;
    ui->overviewTileMapCromTiles = 0u;
    ui->overviewTileMapTilesPerRow = 0u;
    ui->overviewBackgroundPixelsCap = 0u;
    ui->followPrevSpriteVramWords = 0u;
    ui->mainTextureW = 0;
    ui->mainTextureH = 0;
    ui->mainCromCacheValid = 0;
    ui->mainCromCacheBaseAddr = 0u;
    ui->mainCromCacheCromTiles = 0u;
    ui->mainCromCacheTilesPerRow = 0;
    ui->mainCromCacheVisibleRows = 0;
    ui->mainCromCachePixelPx = 0;
    ui->mainCromCacheTileGapPx = 0;
    ui->mainCromCacheTexW = 0;
    ui->mainCromCacheTexH = 0;
    ui->mainCromCachePaletteToken = 0u;
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
    ui->zramBaseAddr = ui->zramBaseAddrHasSaved ? neogeo_memview_clampZramBaseAddr(ui->zramBaseAddr) : NEOGEO_MEMVIEW_ZRAM_BASE_MIN;
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
                    "RAM/ROMS",
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
        fprintf(file, "comp.neogeo_memview.mode=%d\n", (int)ui->mode);
    }
    if (ui->ramBaseAddrHasSaved) {
        fprintf(file, "comp.neogeo_memview.ram_base_addr=%u\n", (unsigned)ui->ramBaseAddr);
    }
    if (ui->cromBaseAddrHasSaved) {
        fprintf(file, "comp.neogeo_memview.crom_base_addr=%u\n", (unsigned)ui->cromBaseAddr);
    }
    if (ui->zramBaseAddrHasSaved) {
        fprintf(file, "comp.neogeo_memview.zram_base_addr=%u\n", (unsigned)ui->zramBaseAddr);
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
        if (intValue == (int)neogeo_memview_mode_roms) {
            ui->mode = neogeo_memview_mode_roms;
        } else if (intValue == (int)neogeo_memview_mode_zram) {
            ui->mode = neogeo_memview_mode_zram;
        } else if (intValue == (int)neogeo_memview_mode_crom) {
            ui->mode = neogeo_memview_mode_crom;
        } else {
            ui->mode = neogeo_memview_mode_ram;
        }
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
    if (strcmp(prop, "zram_base_addr") == 0) {
        if (!neogeo_memview_parseU64SmartHex(value, &parsed, &end) || !end || *end != '\0') {
            return 0;
        }
        ui->zramBaseAddr = neogeo_memview_clampZramBaseAddr((uint32_t)parsed);
        ui->zramBaseAddrHasSaved = 1;
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
