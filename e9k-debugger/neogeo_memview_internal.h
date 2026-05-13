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
#include <SDL.h>
#include <SDL_ttf.h>

#include "e9ui.h"
#include "e9ui_scrollbar.h"
#include "e9ui_step_buttons.h"
#include "e9ui_window.h"
#include "neogeo_memview.h"

#define NEOGEO_MEMVIEW_RAM_BASE_MIN 0x00100000u
#define NEOGEO_MEMVIEW_RAM_BASE_MAX 0x0010ffffu
#define NEOGEO_MEMVIEW_ZRAM_BASE_MIN 0x0000f800u
#define NEOGEO_MEMVIEW_ZRAM_BASE_MAX 0x0000ffffu
#define NEOGEO_MEMVIEW_Z80_PROCESSOR_ID 1u
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
#define NEOGEO_MEMVIEW_TOOLBAR_MAX_ROW_ITEMS 64
#define NEOGEO_MEMVIEW_ROM_ENTRY_MAX 12
#define NEOGEO_MEMVIEW_SPRITE_COUNT 382u
#define NEOGEO_MEMVIEW_SPRITE_FIRST_VISIBLE 1u
#define NEOGEO_MEMVIEW_SPRITE_CHAIN_SCAN_END 381u
#define NEOGEO_MEMVIEW_SPRITE_MAX_ROWS 32u
#define NEOGEO_MEMVIEW_SPRITE_VRAM_WORDS_PER_SPRITE 64u
#define NEOGEO_MEMVIEW_SPRITE_TILE_ROW_WORDS 2u
#define NEOGEO_MEMVIEW_SCB3_WORD_OFFSET 0x8200u
#define NEOGEO_MEMVIEW_SCB3_CHAIN_FLAG 0x40u
#define NEOGEO_MEMVIEW_SCB3_ROW_MASK 0x3fu
#define NEOGEO_MEMVIEW_SCB3_YPOS_MASK 0x01ffu
#define NEOGEO_MEMVIEW_SCB3_YPOS_SHIFT 7u
#define NEOGEO_MEMVIEW_SPRITE_TILE_HIGH_MASK 0x00f0u
#define NEOGEO_MEMVIEW_SPRITE_TILE_HIGH_SHIFT 12u
#define NEOGEO_MEMVIEW_SPRITE_PALETTE_SHIFT 8u
#define NEOGEO_MEMVIEW_SPRITE_PALETTE_MASK 0x00ffu
#define NEOGEO_MEMVIEW_FNV1A64_OFFSET_BASIS 1469598103934665603ull
#define NEOGEO_MEMVIEW_FNV1A64_PRIME 1099511628211ull

typedef enum neogeo_memview_mode {
    neogeo_memview_mode_ram = 0,
    neogeo_memview_mode_crom = 1,
    neogeo_memview_mode_zram = 2,
    neogeo_memview_mode_roms = 3
} neogeo_memview_mode_t;

typedef struct neogeo_memview_overview_range {
    uint32_t baseAddr;
    uint32_t sizeBytes;
} neogeo_memview_overview_range_t;

typedef struct neogeo_memview_state neogeo_memview_state_t;

struct neogeo_memview_state {
    e9ui_window_state_t windowState;
    e9ui_context_t ctx;
    e9ui_component_t *root;
    e9ui_component_t *canvas;
    e9ui_component_t *modeButtonRam;
    e9ui_component_t *modeButtonCrom;
    e9ui_component_t *modeButtonZram;
    e9ui_component_t *modeButtonRoms;
    e9ui_component_t *addressBox;
    e9ui_component_t *widthBox;
    e9ui_component_t *widthSeek;
    e9ui_component_t *zoomSeek;
    e9ui_component_t *overviewZoomSeek;
    e9ui_component_t *overlayBodyHost;
    neogeo_memview_mode_t mode;
    uint32_t ramBaseAddr;
    uint32_t cromBaseAddr;
    uint32_t zramBaseAddr;
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
    int zramBaseAddrHasSaved;
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
    int romsScrollY;
    int contentPixelWidth;
    SDL_Renderer *renderer;
    SDL_Texture *mainTexture;
    uint32_t *mainPixels;
    size_t mainPixelsCap;
    int mainTextureW;
    int mainTextureH;
    int mainCromCacheValid;
    uint32_t mainCromCacheBaseAddr;
    uint32_t mainCromCacheCromTiles;
    int mainCromCacheTilesPerRow;
    int mainCromCacheVisibleRows;
    int mainCromCachePixelPx;
    int mainCromCacheTileGapPx;
    int mainCromCacheTexW;
    int mainCromCacheTexH;
    uint64_t mainCromCachePaletteToken;
    int mainRomsCacheValid;
    int mainRomsCacheTexW;
    int mainRomsCacheTexH;
    int mainRomsCacheCols;
    int mainRomsCacheCardW;
    int mainRomsCacheCardH;
    int mainRomsCacheContentW;
    int mainRomsCacheContentH;
    uint64_t mainRomsCacheToken;
    e9k_debug_rom_entry_t mainRomsEntries[NEOGEO_MEMVIEW_ROM_ENTRY_MAX];
    size_t mainRomsEntryCount;
    uint64_t mainRomsContentToken;
    int mainRomsEntriesValid;
    uint16_t *followPrevSpriteVram;
    size_t followPrevSpriteVramWords;
    uint32_t followPendingStartRow;
    uint32_t followPendingFrames;
    SDL_Texture *overviewTexture;
    uint32_t *overviewPixels;
    size_t overviewPixelsCap;
    int overviewTextureW;
    int overviewTextureH;
    uint32_t *overviewTileRows;
    uint32_t *overviewTileCols;
    size_t overviewTileMapCap;
    uint32_t overviewTileMapCromTiles;
    uint32_t overviewTileMapTilesPerRow;
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
    neogeo_memview_overview_range_t overviewRanges[2];
    int (*widthSeekDefaultHandleEvent)(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev);
    int (*zoomSeekDefaultHandleEvent)(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev);
    int (*overviewZoomSeekDefaultHandleEvent)(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev);
};

uint32_t
neogeo_memview_clampRamRowBytes(uint32_t rowBytes);

int
neogeo_memview_clampOverviewZoomLevel(int zoomLevel);

uint32_t
neogeo_memview_clampCromTilesPerRow(uint32_t tilesPerRow);

uint32_t
neogeo_memview_clampRamBaseAddr(uint32_t baseAddr);

uint32_t
neogeo_memview_clampZramBaseAddr(uint32_t baseAddr);

uint32_t
neogeo_memview_cromSize(void);

uint32_t
neogeo_memview_clampCromBaseAddr(uint32_t baseAddr);

uint32_t
neogeo_memview_cromTotalRows(const neogeo_memview_state_t *ui);

uint32_t
neogeo_memview_currentBaseAddr(const neogeo_memview_state_t *ui);

uint32_t
neogeo_memview_currentRowBytes(const neogeo_memview_state_t *ui);

int
neogeo_memview_readRange(const neogeo_memview_state_t *ui, uint32_t addr, void *out, size_t sizeBytes);

uint32_t
neogeo_memview_argb(uint8_t r, uint8_t g, uint8_t b);

uint32_t
neogeo_memview_amberColor(unsigned index);

int
neogeo_memview_ensureTexture(SDL_Renderer *renderer,
                             SDL_Texture **texture,
                             uint32_t **pixels,
                             size_t *pixelsCap,
                             int *textureW,
                             int *textureH,
                             int width,
                             int height);

uint8_t
neogeo_memview_readCromPixel(const uint8_t *data, size_t sizeBytes, uint32_t tileBaseAddr, unsigned x, unsigned y);

int
neogeo_memview_measureAddressGutterPx(const e9ui_context_t *ctx, TTF_Font *font);

void
neogeo_memview_setView(neogeo_memview_state_t *ui, uint32_t baseAddr, uint32_t rowBytes, int resetScroll);

void
neogeo_memview_initOverviewRanges(neogeo_memview_state_t *ui);

int
neogeo_memview_overviewRangeCount(const neogeo_memview_state_t *ui);

e9ui_rect_t
neogeo_memview_overviewBounds(const neogeo_memview_state_t *ui, e9ui_component_t *self, const e9ui_context_t *ctx, TTF_Font *font);

e9ui_rect_t
neogeo_memview_overviewContentBounds(const e9ui_context_t *ctx, const e9ui_rect_t *overviewBounds);

int
neogeo_memview_rebuildOverviewBackgroundTexture(neogeo_memview_state_t *ui, e9ui_context_t *ctx);

void
neogeo_memview_renderOverviewSelection(neogeo_memview_state_t *ui,
                                       e9ui_context_t *ctx,
                                       const e9ui_rect_t *overviewBounds,
                                       int visibleRows);

int
neogeo_memview_rebuildOverviewTexture(neogeo_memview_state_t *ui,
                                      e9ui_context_t *ctx,
                                      const e9ui_rect_t *overviewBounds);

int
neogeo_memview_overviewNavigate(neogeo_memview_state_t *ui, e9ui_component_t *self, e9ui_context_t *ctx, int mx, int my);
