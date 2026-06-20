/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "neogeo_sprite_list.h"
#include "neogeo_sprite_debug.h"
#include "alloc.h"
#include "config.h"
#include "debugger.h"
#include "e9ui.h"
#include "e9ui_button.h"
#include "e9ui_checkbox.h"
#include "e9ui_hstack.h"
#include "e9ui_scroll.h"
#include "e9ui_spacer.h"
#include "e9ui_stack.h"
#include "e9ui_text_cache.h"
#include "e9ui_textbox.h"
#include "e9ui_vspacer.h"
#include "libretro_host.h"
#include "neogeo_sprite_decode.h"
#include "runtime.h"

#define NEOGEO_SPRITE_LIST_MAX_SPRITES NEOGEO_SPRITE_DECODE_MAX_SPRITES
#define NEOGEO_SPRITE_LIST_SPRITE_VRAM_WORDS_PER_SPRITE 64u
#define NEOGEO_SPRITE_LIST_SPRITE_TILE_ODD_WORD_OFFSET 1u
#define NEOGEO_SPRITE_LIST_VISIBLE_W 320
#define NEOGEO_SPRITE_LIST_VISIBLE_H 224
#define NEOGEO_SPRITE_LIST_WRAP_MASK NEOGEO_SPRITE_DECODE_WRAP_MASK
#define NEOGEO_SPRITE_LIST_FILTER_TEXT_MAX 128
#define NEOGEO_SPRITE_LIST_FILTER_COUNT 11
#define NEOGEO_SPRITE_LIST_PAD 10
#define NEOGEO_SPRITE_LIST_GAP 8
#define NEOGEO_SPRITE_LIST_FILTER_GAP_Y 6
#define NEOGEO_SPRITE_LIST_COL_GAP 14
#define NEOGEO_SPRITE_LIST_COL_SELECT_W 34
#define NEOGEO_SPRITE_LIST_COL_INDEX_W 54
#define NEOGEO_SPRITE_LIST_COL_ROOT_W 132
#define NEOGEO_SPRITE_LIST_COL_STATUS_W 112
#define NEOGEO_SPRITE_LIST_COL_XPOS_W 64
#define NEOGEO_SPRITE_LIST_COL_YPOS_W 64
#define NEOGEO_SPRITE_LIST_COL_ROWS_W 84
#define NEOGEO_SPRITE_LIST_COL_WIDTH_W 56
#define NEOGEO_SPRITE_LIST_COL_SHRINK_W 98
#define NEOGEO_SPRITE_LIST_COL_PALETTE_W 58
#define NEOGEO_SPRITE_LIST_COL_SCB1_W 116
#define NEOGEO_SPRITE_LIST_COL_SCB_W 220

typedef struct neogeo_sprite_list_row {
    unsigned index;
    unsigned rootIndex;
    unsigned chainLength;
    unsigned chainOffset;
    unsigned xpos;
    unsigned ypos;
    unsigned rows;
    unsigned pixelHeight;
    unsigned width;
    unsigned hshrink;
    unsigned vshrink;
    unsigned paletteBank;
    unsigned scb1Tile;
    unsigned scb1Attr;
    unsigned scb2;
    unsigned scb3;
    unsigned scb4;
    int active;
    int visible;
    int sticky;
    int hasAnimBits;
} neogeo_sprite_list_row_t;

typedef struct neogeo_sprite_list_layout {
    int lineHeight;
    int contentWidth;
    int colSelectX;
    int colSelectW;
    int colIndexX;
    int colIndexW;
    int colRootX;
    int colRootW;
    int colStatusX;
    int colStatusW;
    int colXposX;
    int colXposW;
    int colYposX;
    int colYposW;
    int colRowsX;
    int colRowsW;
    int colWidthX;
    int colWidthW;
    int colShrinkX;
    int colShrinkW;
    int colPaletteX;
    int colPaletteW;
    int colScb1X;
    int colScb1W;
    int colScbX;
    int colScbW;
} neogeo_sprite_list_layout_t;

typedef struct neogeo_sprite_list_display_row {
    char index[16];
    char root[24];
    char status[24];
    char xpos[16];
    char ypos[16];
    char rows[24];
    char width[16];
    char shrink[24];
    char palette[16];
    char scb1[32];
    char scb[64];
} neogeo_sprite_list_display_row_t;

typedef enum neogeo_sprite_list_filter_index {
    neogeo_sprite_list_filter_index = 0,
    neogeo_sprite_list_filter_root = 1,
    neogeo_sprite_list_filter_status = 2,
    neogeo_sprite_list_filter_xpos = 3,
    neogeo_sprite_list_filter_ypos = 4,
    neogeo_sprite_list_filter_rows = 5,
    neogeo_sprite_list_filter_width = 6,
    neogeo_sprite_list_filter_shrink = 7,
    neogeo_sprite_list_filter_palette = 8,
    neogeo_sprite_list_filter_scb1 = 9,
    neogeo_sprite_list_filter_scb = 10
} neogeo_sprite_list_filter_index_t;

typedef struct neogeo_sprite_list_filter_cb {
    int filterIndex;
} neogeo_sprite_list_filter_cb_t;

typedef struct neogeo_sprite_list_overlay_body_state {
    int unused;
} neogeo_sprite_list_overlay_body_state_t;

typedef struct neogeo_sprite_list_canvas_state {
    int unused;
} neogeo_sprite_list_canvas_state_t;

typedef struct neogeo_sprite_list_checkbox_cb {
    int rowIndex;
} neogeo_sprite_list_checkbox_cb_t;

typedef struct neogeo_sprite_list_state {
    e9ui_window_state_t windowState;
    SDL_Window *window;
    SDL_Renderer *renderer;
    e9ui_context_t ctx;
    e9ui_component_t *root;
    e9ui_component_t *filterRoot;
    e9ui_component_t *listScroll;
    e9ui_component_t *listCanvas;
    e9ui_component_t *filterTextboxes[NEOGEO_SPRITE_LIST_FILTER_COUNT];
    e9ui_component_t *rowCheckboxes[NEOGEO_SPRITE_LIST_MAX_SPRITES];
    neogeo_sprite_list_checkbox_cb_t rowCheckboxCbs[NEOGEO_SPRITE_LIST_MAX_SPRITES];
    neogeo_sprite_list_filter_cb_t filterCbs[NEOGEO_SPRITE_LIST_FILTER_COUNT];
    e9k_debug_sprite_state_t lastState;
    int hasLastState;
    int rowsValid;
    int filterCacheDirty;
    int filteredCount;
    int filteredIndexes[NEOGEO_SPRITE_LIST_MAX_SPRITES];
    neogeo_sprite_list_row_t rows[NEOGEO_SPRITE_LIST_MAX_SPRITES];
    neogeo_sprite_list_display_row_t displayRows[NEOGEO_SPRITE_LIST_MAX_SPRITES];
    TTF_Font *measuredFont;
    int measurementsValid;
    int colIndexW;
    int colRootW;
    int colStatusW;
    int colXposW;
    int colYposW;
    int colRowsW;
    int colWidthW;
    int colShrinkW;
    int colPaletteW;
    int colScb1W;
    int colScbW;
    char filters[NEOGEO_SPRITE_LIST_FILTER_COUNT][NEOGEO_SPRITE_LIST_FILTER_TEXT_MAX];
    uint32_t selectedMask[E9K_DEBUG_GEO_SPRITE_SELECTION_MASK_WORDS];
    int activeOnly;
    int highlightChain;
    int collapseChain;
    int grayscaleSelection;
    int invertSelection;
    int hideSelection;
    int selectedSpriteIndex;
    int selectedChainRootIndex;
    int selectedRowIndex;
    int suppressCheckboxCallback;
} neogeo_sprite_list_state_t;

static neogeo_sprite_list_state_t neogeo_sprite_list_state = {
    .windowState.winX = E9UI_WINDOW_COORD_UNSET,
    .windowState.winY = E9UI_WINDOW_COORD_UNSET,
    .windowState.openMinWidthPx = 760,
    .windowState.openMinHeightPx = 300,
    .windowState.openCenterWhenNoSaved = 1,
    .filterCacheDirty = 1,
    .activeOnly = 1,
    .highlightChain = 1,
    .collapseChain = 0,
    .grayscaleSelection = 0,
    .invertSelection = 0,
    .hideSelection = 0,
    .selectedSpriteIndex = -1,
    .selectedChainRootIndex = -1,
    .selectedRowIndex = -1,
};

static void
neogeo_sprite_list_refreshDisplayMeasurements(neogeo_sprite_list_state_t *ui, TTF_Font *font);

static void
neogeo_sprite_list_makeRowCheckboxes(neogeo_sprite_list_state_t *ui, e9ui_component_t *canvas);

static void
neogeo_sprite_list_centerSelectedRow(neogeo_sprite_list_state_t *ui);

static const char *
neogeo_sprite_list_filterTextForDisplayRow(const neogeo_sprite_list_display_row_t *display, int index);

static void
neogeo_sprite_list_formatDisplayRows(neogeo_sprite_list_state_t *ui);

static int
neogeo_sprite_list_pointInRowContent(neogeo_sprite_list_state_t *ui,
                                     e9ui_context_t *ctx,
                                     const neogeo_sprite_list_layout_t *layout,
                                     int mouseX,
                                     int mouseY);

static int
neogeo_sprite_list_pointInCheckbox(neogeo_sprite_list_state_t *ui, int mouseX, int mouseY);

static void
neogeo_sprite_list_refreshPausedSelectionFrame(void);

static void
neogeo_sprite_list_checkboxChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user);

static int
neogeo_sprite_list_rowCheckboxVisible(const neogeo_sprite_list_state_t *ui, int rowIndex);

static void
neogeo_sprite_list_applySelection(neogeo_sprite_list_state_t *ui);

static e9ui_window_backend_t
neogeo_sprite_list_windowBackend(void)
{
    return e9ui_window_backend_overlay;
}

static int
neogeo_sprite_list_maskHasSprite(const uint32_t *mask, unsigned spriteIndex)
{
    unsigned wordIndex = spriteIndex >> 5;
    unsigned bitIndex = spriteIndex & 31u;

    if (wordIndex >= E9K_DEBUG_GEO_SPRITE_SELECTION_MASK_WORDS) {
        return 0;
    }
    return (mask[wordIndex] & (1u << bitIndex)) ? 1 : 0;
}

static void
neogeo_sprite_list_maskSetSprite(uint32_t *mask, unsigned spriteIndex, int selected)
{
    unsigned wordIndex = spriteIndex >> 5;
    unsigned bitIndex = spriteIndex & 31u;

    if (wordIndex >= E9K_DEBUG_GEO_SPRITE_SELECTION_MASK_WORDS) {
        return;
    }
    if (selected) {
        mask[wordIndex] |= 1u << bitIndex;
    } else {
        mask[wordIndex] &= ~(1u << bitIndex);
    }
}

static int
neogeo_sprite_list_maskHasBits(const uint32_t *mask)
{
    for (int i = 0; i < E9K_DEBUG_GEO_SPRITE_SELECTION_MASK_WORDS; ++i) {
        if (mask[i]) {
            return 1;
        }
    }
    return 0;
}

static int
neogeo_sprite_list_rowMaskIndex(const neogeo_sprite_list_state_t *ui, int rowIndex)
{
    if (!ui || rowIndex < 0 || rowIndex >= NEOGEO_SPRITE_LIST_MAX_SPRITES) {
        return -1;
    }
    return ui->highlightChain && ui->rowsValid ? (int)ui->rows[rowIndex].rootIndex : rowIndex;
}

static void
neogeo_sprite_list_setSelectedSprite(neogeo_sprite_list_state_t *ui, int spriteIndex)
{
    if (!ui || spriteIndex < 0 || spriteIndex >= NEOGEO_SPRITE_LIST_MAX_SPRITES) {
        if (ui) {
            ui->selectedSpriteIndex = -1;
            ui->selectedChainRootIndex = -1;
            ui->selectedRowIndex = -1;
        }
        return;
    }

    int rootIndex = ui->rowsValid ? (int)ui->rows[spriteIndex].rootIndex : spriteIndex;
    ui->selectedSpriteIndex = spriteIndex;
    ui->selectedChainRootIndex = rootIndex;
    ui->selectedRowIndex = ui->highlightChain && ui->collapseChain && ui->rowsValid ? rootIndex : spriteIndex;
}

static int
neogeo_sprite_list_rowCheckboxVisible(const neogeo_sprite_list_state_t *ui, int rowIndex)
{
    if (!ui || rowIndex < 0 || rowIndex >= NEOGEO_SPRITE_LIST_MAX_SPRITES) {
        return 0;
    }
    if (ui->highlightChain && ui->rowsValid && ui->rows[rowIndex].chainOffset != 0u) {
        return 0;
    }
    return 1;
}

static void
neogeo_sprite_list_clearHiddenChainSelections(neogeo_sprite_list_state_t *ui)
{
    if (!ui || !ui->highlightChain || !ui->rowsValid) {
        return;
    }
    for (int i = 0; i < NEOGEO_SPRITE_LIST_MAX_SPRITES; ++i) {
        if (ui->rows[i].chainOffset != 0u &&
            neogeo_sprite_list_maskHasSprite(ui->selectedMask, (unsigned)i)) {
            neogeo_sprite_list_maskSetSprite(ui->selectedMask, ui->rows[i].rootIndex, 1);
            neogeo_sprite_list_maskSetSprite(ui->selectedMask, (unsigned)i, 0);
        }
    }
}

static void
neogeo_sprite_list_syncRowCheckbox(neogeo_sprite_list_state_t *ui,
                                   e9ui_context_t *ctx,
                                   int rowIndex)
{
    if (!ui || rowIndex < 0 || rowIndex >= NEOGEO_SPRITE_LIST_MAX_SPRITES ||
        !ui->rowCheckboxes[rowIndex]) {
        return;
    }
    int maskIndex = neogeo_sprite_list_rowMaskIndex(ui, rowIndex);
    int selected = maskIndex >= 0 ?
        neogeo_sprite_list_maskHasSprite(ui->selectedMask, (unsigned)maskIndex) : 0;

    ui->suppressCheckboxCallback = 1;
    e9ui_checkbox_setSelected(ui->rowCheckboxes[rowIndex], selected, ctx);
    ui->suppressCheckboxCallback = 0;
}

static void
neogeo_sprite_list_checkboxChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    neogeo_sprite_list_checkbox_cb_t *cb = (neogeo_sprite_list_checkbox_cb_t *)user;
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;

    if (!cb || ui->suppressCheckboxCallback) {
        return;
    }
    int maskIndex = neogeo_sprite_list_rowMaskIndex(ui, cb->rowIndex);
    if (maskIndex < 0) {
        return;
    }
    neogeo_sprite_list_maskSetSprite(ui->selectedMask, (unsigned)maskIndex, selected ? 1 : 0);
    neogeo_sprite_list_applySelection(ui);
    neogeo_sprite_list_refreshPausedSelectionFrame();
    (void)ctx;
}

static void
neogeo_sprite_list_selectSingleCheckbox(neogeo_sprite_list_state_t *ui,
                                        e9ui_context_t *ctx,
                                        int rowIndex)
{
    memset(ui->selectedMask, 0, sizeof(ui->selectedMask));
    int maskIndex = neogeo_sprite_list_rowMaskIndex(ui, rowIndex);
    if (maskIndex >= 0) {
        neogeo_sprite_list_maskSetSprite(ui->selectedMask, (unsigned)maskIndex, 1);
    }
    for (int i = 0; i < NEOGEO_SPRITE_LIST_MAX_SPRITES; ++i) {
        neogeo_sprite_list_syncRowCheckbox(ui, ctx, i);
    }
}

static void
neogeo_sprite_list_makeRowCheckboxes(neogeo_sprite_list_state_t *ui, e9ui_component_t *canvas)
{
    if (!ui || !canvas) {
        return;
    }
    for (int i = 0; i < NEOGEO_SPRITE_LIST_MAX_SPRITES; ++i) {
        ui->rowCheckboxCbs[i].rowIndex = i;
        ui->rowCheckboxes[i] = e9ui_checkbox_make(NULL,
                                                  0,
                                                  neogeo_sprite_list_checkboxChanged,
                                                  &ui->rowCheckboxCbs[i]);
        if (ui->rowCheckboxes[i]) {
            e9ui_checkbox_setLeftMargin(ui->rowCheckboxes[i], 0);
            ui->rowCheckboxes[i]->focusable = 0;
            e9ui_child_add(canvas, ui->rowCheckboxes[i], 0);
        }
    }
}

static int
neogeo_sprite_list_pointInCheckbox(neogeo_sprite_list_state_t *ui, int mouseX, int mouseY)
{
    if (!ui) {
        return 0;
    }
    for (int i = 0; i < NEOGEO_SPRITE_LIST_MAX_SPRITES; ++i) {
        e9ui_component_t *checkbox = ui->rowCheckboxes[i];
        if (!checkbox || checkbox->bounds.w <= 0 || checkbox->bounds.h <= 0) {
            continue;
        }
        if (mouseX >= checkbox->bounds.x &&
            mouseX < checkbox->bounds.x + checkbox->bounds.w &&
            mouseY >= checkbox->bounds.y &&
            mouseY < checkbox->bounds.y + checkbox->bounds.h) {
            return 1;
        }
    }
    return 0;
}

static int
neogeo_sprite_list_parseInt(const char *value, int *out)
{
    if (!value || !out) {
        return 0;
    }
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (!end || end == value) {
        return 0;
    }
    if (parsed < INT_MIN || parsed > INT_MAX) {
        return 0;
    }
    *out = (int)parsed;
    return 1;
}

static int
neogeo_sprite_list_spriteIsVisible(const neogeo_sprite_list_row_t *row, int visibleH, int lineOffset)
{
    if (!row || !row->active || row->width == 0u || row->pixelHeight == 0u) {
        return 0;
    }
    int x0 = (int)(row->xpos & NEOGEO_SPRITE_LIST_WRAP_MASK);
    int x1 = x0 + (int)row->width;
    int visibleX = (x0 < NEOGEO_SPRITE_LIST_VISIBLE_W) || (x1 > 512);
    if (!visibleX) {
        return 0;
    }
    for (unsigned r = 0u; r < row->pixelHeight && r < 512u; ++r) {
        unsigned line = (unsigned)((512u - row->ypos + r - lineOffset) & NEOGEO_SPRITE_LIST_WRAP_MASK);
        if ((int)line < visibleH) {
            return 1;
        }
    }
    return 0;
}

static int
neogeo_sprite_list_decodeRows(neogeo_sprite_list_state_t *ui, const e9k_debug_sprite_state_t *st)
{
    if (!ui || !st || !st->vram) {
        return 0;
    }
    const uint16_t *vram = st->vram;
    neogeo_sprite_decode_sprite_t decodedSprites[NEOGEO_SPRITE_LIST_MAX_SPRITES];
    int screenH = st->screen_h > 0 ? st->screen_h : NEOGEO_SPRITE_LIST_VISIBLE_H;
    int visibleH = st->visible_h > 0 ? st->visible_h : screenH;
    int lineOffset = e9k_debug_geo_spriteVisibleLineOffset(st);

    if (!neogeo_sprite_decode_decodeSprites(st, decodedSprites, NULL, NULL, 0u)) {
        return 0;
    }

    memset(ui->rows, 0, sizeof(ui->rows));
    for (unsigned i = 0u; i < NEOGEO_SPRITE_LIST_MAX_SPRITES; ++i) {
        const neogeo_sprite_decode_sprite_t *decoded = &decodedSprites[i];

        neogeo_sprite_list_row_t *row = &ui->rows[i];
        row->index = i;
        row->rootIndex = decoded->chainRootIndex;
        row->chainOffset = decoded->chainOffset;
        row->xpos = decoded->xpos;
        row->ypos = decoded->ypos;
        row->rows = decoded->sprsize;
        row->pixelHeight = decoded->sprsize << 4;
        row->width = (unsigned)decoded->width;
        row->hshrink = decoded->hshrink;
        row->vshrink = decoded->vshrink;
        row->paletteBank = decoded->paletteBank;
        row->scb2 = decoded->scb2;
        row->scb3 = decoded->scb3;
        row->scb4 = decoded->scb4;
        row->sticky = decoded->chainOffset != 0u ? 1 : 0;
        row->active = (decoded->sprsize != 0u && decoded->ypos != (unsigned)screenH) ? 1 : 0;

        size_t evenWordOffset = (size_t)i * NEOGEO_SPRITE_LIST_SPRITE_VRAM_WORDS_PER_SPRITE;
        size_t oddWordOffset = evenWordOffset + NEOGEO_SPRITE_LIST_SPRITE_TILE_ODD_WORD_OFFSET;
        if (evenWordOffset < st->vram_words) {
            row->scb1Tile = (unsigned)vram[evenWordOffset];
        }
        if (oddWordOffset < st->vram_words) {
            row->scb1Attr = (unsigned)vram[oddWordOffset];
        }
        row->hasAnimBits = decoded->hasAnimBits;
        row->visible = neogeo_sprite_list_spriteIsVisible(row, visibleH, lineOffset);
    }

    for (int i = NEOGEO_SPRITE_LIST_MAX_SPRITES - 1; i >= 0; --i) {
        unsigned root = ui->rows[i].rootIndex;
        if (root < NEOGEO_SPRITE_LIST_MAX_SPRITES) {
            unsigned length = ui->rows[i].chainOffset + 1u;
            if (length > ui->rows[root].chainLength) {
                ui->rows[root].chainLength = length;
            }
            ui->rows[i].chainLength = ui->rows[root].chainLength;
        }
    }
    neogeo_sprite_list_formatDisplayRows(ui);
    ui->filterCacheDirty = 1;
    return 1;
}

static int
neogeo_sprite_list_containsInsensitive(const char *text, const char *needle)
{
    if (!text || !needle) {
        return 0;
    }
    if (!needle[0]) {
        return 1;
    }
    size_t needleLen = strlen(needle);
    size_t textLen = strlen(text);
    if (needleLen > textLen) {
        return 0;
    }
    for (size_t i = 0u; i + needleLen <= textLen; ++i) {
        size_t j = 0u;
        for (; j < needleLen; ++j) {
            int a = tolower((unsigned char)text[i + j]);
            int b = tolower((unsigned char)needle[j]);
            if (a != b) {
                break;
            }
        }
        if (j == needleLen) {
            return 1;
        }
    }
    return 0;
}

static void
neogeo_sprite_list_appendText(char *dest, size_t cap, const char *text)
{
    if (!dest || cap == 0u || !text) {
        return;
    }
    size_t len = strlen(dest);
    if (len >= cap - 1u) {
        return;
    }
    size_t left = cap - len - 1u;
    size_t n = strlen(text);
    if (n > left) {
        n = left;
    }
    memcpy(dest + len, text, n);
    dest[len + n] = '\0';
}

static void
neogeo_sprite_list_formatDisplayRow(const neogeo_sprite_list_row_t *row,
                                    neogeo_sprite_list_display_row_t *out)
{
    if (!row || !out) {
        return;
    }
    snprintf(out->index, sizeof(out->index), "%03u", row->index);
    snprintf(out->root, sizeof(out->root), "%03u+%u/%u", row->rootIndex, row->chainOffset, row->chainLength);
    out->status[0] = '\0';
    neogeo_sprite_list_appendText(out->status, sizeof(out->status), row->active ? (row->visible ? "VIS" : "ACT") : "OFF");
    if (row->sticky) {
        neogeo_sprite_list_appendText(out->status, sizeof(out->status), " CH");
    }
    if (row->hasAnimBits) {
        neogeo_sprite_list_appendText(out->status, sizeof(out->status), " AN");
    }
    snprintf(out->xpos, sizeof(out->xpos), "%03u", row->xpos);
    snprintf(out->ypos, sizeof(out->ypos), "%03u", row->ypos);
    snprintf(out->rows, sizeof(out->rows), "%02u/%03u", row->rows, row->pixelHeight);
    snprintf(out->width, sizeof(out->width), "%02u", row->width);
    snprintf(out->shrink, sizeof(out->shrink), "%x/%02x", row->hshrink, row->vshrink);
    snprintf(out->palette, sizeof(out->palette), "%02x", row->paletteBank);
    snprintf(out->scb1, sizeof(out->scb1), "%04x %04x", row->scb1Tile, row->scb1Attr);
    snprintf(out->scb, sizeof(out->scb), "%04x %04x %04x", row->scb2, row->scb3, row->scb4);
}

static void
neogeo_sprite_list_formatDisplayRows(neogeo_sprite_list_state_t *ui)
{
    if (!ui) {
        return;
    }
    for (int i = 0; i < NEOGEO_SPRITE_LIST_MAX_SPRITES; ++i) {
        neogeo_sprite_list_formatDisplayRow(&ui->rows[i], &ui->displayRows[i]);
    }
}

static int
neogeo_sprite_list_rowMatchesFilter(const neogeo_sprite_list_state_t *ui, int rowIndex)
{
    if (!ui || rowIndex < 0 || rowIndex >= NEOGEO_SPRITE_LIST_MAX_SPRITES) {
        return 0;
    }
    const neogeo_sprite_list_row_t *row = &ui->rows[rowIndex];
    if (ui->activeOnly && !row->active) {
        return 0;
    }
    if (ui->highlightChain && ui->collapseChain && ui->rowsValid && row->chainOffset != 0u) {
        return 0;
    }
    const neogeo_sprite_list_display_row_t *display = &ui->displayRows[rowIndex];
    for (int i = 0; i < NEOGEO_SPRITE_LIST_FILTER_COUNT; ++i) {
        if (ui->filters[i][0] == '\0') {
            continue;
        }
        const char *columnText = neogeo_sprite_list_filterTextForDisplayRow(display, i);
        if (!neogeo_sprite_list_containsInsensitive(columnText, ui->filters[i])) {
            return 0;
        }
    }
    return 1;
}

static void
neogeo_sprite_list_applySelection(neogeo_sprite_list_state_t *ui)
{
    int activeSelectedIndex = ui ? ui->selectedSpriteIndex : -1;

    if (!ui || activeSelectedIndex < 0 || activeSelectedIndex >= NEOGEO_SPRITE_LIST_MAX_SPRITES) {
        neogeo_sprite_debug_setSelection(-1, -1, 0);
    } else if (!ui->rowsValid) {
        neogeo_sprite_debug_setSelection(activeSelectedIndex, activeSelectedIndex, 0);
    } else {
        neogeo_sprite_debug_setSelection(activeSelectedIndex,
                                         ui->selectedChainRootIndex,
                                         ui->highlightChain);
    }
    neogeo_sprite_list_clearHiddenChainSelections(ui);
    if (!ui || !neogeo_sprite_list_maskHasBits(ui->selectedMask)) {
        e9k_debug_sprite_grayscale_selection_t selection = {0};
        (void)libretro_host_neogeo_setSpriteGrayscaleSelection(&selection);
        return;
    }

    e9k_debug_sprite_grayscale_selection_t selection = {
        .enabled = (ui->grayscaleSelection || ui->hideSelection) ? 1 : 0,
        .highlightChain = ui->highlightChain ? 1 : 0,
        .invert = ui->invertSelection ? 1 : 0,
        .hide = ui->hideSelection ? 1 : 0,
    };
    memcpy(selection.spriteMask, ui->selectedMask, sizeof(selection.spriteMask));
    (void)libretro_host_neogeo_setSpriteGrayscaleSelection(&selection);
}

static void
neogeo_sprite_list_refreshPausedSelectionFrame(void)
{
    (void)runtime_refreshCurrentFrameFromPrevious();
}

static int
neogeo_sprite_list_pointInRowContent(neogeo_sprite_list_state_t *ui,
                                     e9ui_context_t *ctx,
                                     const neogeo_sprite_list_layout_t *layout,
                                     int mouseX,
                                     int mouseY)
{
    int contentW = layout->contentWidth + 12;
    int contentH = (ui->filteredCount + 1) * layout->lineHeight;
    return e9ui_scroll_pointInContentPx(ui->listScroll, ctx, contentW, contentH, mouseX, mouseY);
}

static const char *
neogeo_sprite_list_filterLabel(int index)
{
    switch (index) {
    case neogeo_sprite_list_filter_index: return "#";
    case neogeo_sprite_list_filter_root: return "ROOT";
    case neogeo_sprite_list_filter_status: return "STATUS";
    case neogeo_sprite_list_filter_xpos: return "X";
    case neogeo_sprite_list_filter_ypos: return "Y";
    case neogeo_sprite_list_filter_rows: return "ROWS";
    case neogeo_sprite_list_filter_width: return "W";
    case neogeo_sprite_list_filter_shrink: return "SHRINK";
    case neogeo_sprite_list_filter_palette: return "PAL";
    case neogeo_sprite_list_filter_scb1: return "SCB1";
    case neogeo_sprite_list_filter_scb: return "SCB";
    default:
        break;
    }
    return "";
}

static int
neogeo_sprite_list_filterColumnWidth(const neogeo_sprite_list_state_t *ui, int index)
{
    if (!ui) {
        return 40;
    }
    switch (index) {
    case neogeo_sprite_list_filter_index: return ui->colIndexW;
    case neogeo_sprite_list_filter_root: return ui->colRootW;
    case neogeo_sprite_list_filter_status: return ui->colStatusW;
    case neogeo_sprite_list_filter_xpos: return ui->colXposW;
    case neogeo_sprite_list_filter_ypos: return ui->colYposW;
    case neogeo_sprite_list_filter_rows: return ui->colRowsW;
    case neogeo_sprite_list_filter_width: return ui->colWidthW;
    case neogeo_sprite_list_filter_shrink: return ui->colShrinkW;
    case neogeo_sprite_list_filter_palette: return ui->colPaletteW;
    case neogeo_sprite_list_filter_scb1: return ui->colScb1W;
    case neogeo_sprite_list_filter_scb: return ui->colScbW;
    default:
        break;
    }
    return 40;
}

static const char *
neogeo_sprite_list_filterTextForDisplayRow(const neogeo_sprite_list_display_row_t *display, int index)
{
    if (!display) {
        return "";
    }
    switch (index) {
    case neogeo_sprite_list_filter_index: return display->index;
    case neogeo_sprite_list_filter_root: return display->root;
    case neogeo_sprite_list_filter_status: return display->status;
    case neogeo_sprite_list_filter_xpos: return display->xpos;
    case neogeo_sprite_list_filter_ypos: return display->ypos;
    case neogeo_sprite_list_filter_rows: return display->rows;
    case neogeo_sprite_list_filter_width: return display->width;
    case neogeo_sprite_list_filter_shrink: return display->shrink;
    case neogeo_sprite_list_filter_palette: return display->palette;
    case neogeo_sprite_list_filter_scb1: return display->scb1;
    case neogeo_sprite_list_filter_scb: return display->scb;
    default:
        break;
    }
    return "";
}

static void
neogeo_sprite_list_rebuildFilterCache(neogeo_sprite_list_state_t *ui)
{
    if (!ui || !ui->filterCacheDirty) {
        return;
    }
    TTF_Font *font = ui->ctx.font ? ui->ctx.font : e9ui->ctx.font;
    if (!ui->measurementsValid || ui->measuredFont != font) {
        neogeo_sprite_list_refreshDisplayMeasurements(ui, font);
    }
    ui->filteredCount = 0;
    for (int i = 0; i < NEOGEO_SPRITE_LIST_MAX_SPRITES; ++i) {
        if (neogeo_sprite_list_rowMatchesFilter(ui, i)) {
            ui->filteredIndexes[ui->filteredCount++] = i;
        }
    }
    ui->filterCacheDirty = 0;
}

static int
neogeo_sprite_list_measureLineHeight(void)
{
    TTF_Font *font = e9ui->ctx.font;
    int lineHeight = font ? TTF_FontHeight(font) : 0;
    if (lineHeight <= 0) {
        lineHeight = 16;
    }
    return lineHeight;
}

static e9ui_rect_t
neogeo_sprite_list_windowDefaultRect(const e9ui_context_t *ctx)
{
    e9ui_rect_t rect = {
        e9ui_scale_px(ctx, 112),
        e9ui_scale_px(ctx, 112),
        e9ui_scale_px(ctx, 1140),
        e9ui_scale_px(ctx, 480)
    };
    return rect;
}

static int
neogeo_sprite_list_measureText(TTF_Font *font, const char *text)
{
    if (!font || !text) {
        return 0;
    }
    int textW = 0;
    int textH = 0;
    if (TTF_SizeUTF8(font, text, &textW, &textH) != 0) {
        return 0;
    }
    return textW;
}

static void
neogeo_sprite_list_measureColumn(TTF_Font *font, int *width, const char *text)
{
    if (!width) {
        return;
    }
    int textW = neogeo_sprite_list_measureText(font, text);
    if (textW > *width) {
        *width = textW;
    }
}

static void
neogeo_sprite_list_refreshDisplayMeasurements(neogeo_sprite_list_state_t *ui, TTF_Font *font)
{
    if (!ui) {
        return;
    }
    const int pad = 10;
    ui->colIndexW = NEOGEO_SPRITE_LIST_COL_INDEX_W;
    ui->colRootW = NEOGEO_SPRITE_LIST_COL_ROOT_W;
    ui->colStatusW = NEOGEO_SPRITE_LIST_COL_STATUS_W;
    ui->colXposW = NEOGEO_SPRITE_LIST_COL_XPOS_W;
    ui->colYposW = NEOGEO_SPRITE_LIST_COL_YPOS_W;
    ui->colRowsW = NEOGEO_SPRITE_LIST_COL_ROWS_W;
    ui->colWidthW = NEOGEO_SPRITE_LIST_COL_WIDTH_W;
    ui->colShrinkW = NEOGEO_SPRITE_LIST_COL_SHRINK_W;
    ui->colPaletteW = NEOGEO_SPRITE_LIST_COL_PALETTE_W;
    ui->colScb1W = NEOGEO_SPRITE_LIST_COL_SCB1_W;
    ui->colScbW = NEOGEO_SPRITE_LIST_COL_SCB_W;

    neogeo_sprite_list_measureColumn(font, &ui->colIndexW, "#");
    neogeo_sprite_list_measureColumn(font, &ui->colRootW, "ROOT");
    neogeo_sprite_list_measureColumn(font, &ui->colStatusW, "STATUS");
    neogeo_sprite_list_measureColumn(font, &ui->colXposW, "X");
    neogeo_sprite_list_measureColumn(font, &ui->colYposW, "Y");
    neogeo_sprite_list_measureColumn(font, &ui->colRowsW, "ROWS");
    neogeo_sprite_list_measureColumn(font, &ui->colWidthW, "W");
    neogeo_sprite_list_measureColumn(font, &ui->colShrinkW, "SHRINK");
    neogeo_sprite_list_measureColumn(font, &ui->colPaletteW, "PAL");
    neogeo_sprite_list_measureColumn(font, &ui->colScb1W, "SCB1");
    neogeo_sprite_list_measureColumn(font, &ui->colScbW, "SCB2 SCB3 SCB4");

    neogeo_sprite_list_measureColumn(font, &ui->colIndexW, "381");
    neogeo_sprite_list_measureColumn(font, &ui->colRootW, "381+381/382");
    neogeo_sprite_list_measureColumn(font, &ui->colStatusW, "VIS CH AN");
    neogeo_sprite_list_measureColumn(font, &ui->colXposW, "511");
    neogeo_sprite_list_measureColumn(font, &ui->colYposW, "511");
    neogeo_sprite_list_measureColumn(font, &ui->colRowsW, "63/1008");
    neogeo_sprite_list_measureColumn(font, &ui->colWidthW, "16");
    neogeo_sprite_list_measureColumn(font, &ui->colShrinkW, "f/ff");
    neogeo_sprite_list_measureColumn(font, &ui->colPaletteW, "ff");
    neogeo_sprite_list_measureColumn(font, &ui->colScb1W, "ffff ffff");
    neogeo_sprite_list_measureColumn(font, &ui->colScbW, "ffff ffff ffff");

    ui->colIndexW += pad;
    ui->colRootW += pad;
    ui->colStatusW += pad;
    ui->colXposW += pad;
    ui->colYposW += pad;
    ui->colRowsW += pad;
    ui->colWidthW += pad;
    ui->colShrinkW += pad;
    ui->colPaletteW += pad;
    ui->colScb1W += pad;
    ui->colScbW += pad;
    ui->measuredFont = font;
    ui->measurementsValid = 1;
}

static void
neogeo_sprite_list_computeLayout(const e9ui_context_t *ctx, int originX, neogeo_sprite_list_layout_t *out)
{
    if (!out) {
        return;
    }
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;
    TTF_Font *font = (ctx && ctx->font) ? ctx->font : e9ui->ctx.font;
    memset(out, 0, sizeof(*out));
    int gap = NEOGEO_SPRITE_LIST_COL_GAP;
    out->lineHeight = (ctx && ctx->font) ? TTF_FontHeight(ctx->font) : neogeo_sprite_list_measureLineHeight();
    if (out->lineHeight <= 0) {
        out->lineHeight = neogeo_sprite_list_measureLineHeight();
    }

    if (!ui->measurementsValid || ui->measuredFont != font) {
        neogeo_sprite_list_refreshDisplayMeasurements(ui, font);
    }
    out->colIndexW = ui->colIndexW;
    out->colSelectW = e9ui_scale_px(ctx, NEOGEO_SPRITE_LIST_COL_SELECT_W);
    if (out->colSelectW <= 0) {
        out->colSelectW = NEOGEO_SPRITE_LIST_COL_SELECT_W;
    }
    out->colRootW = ui->colRootW;
    out->colStatusW = ui->colStatusW;
    out->colXposW = ui->colXposW;
    out->colYposW = ui->colYposW;
    out->colRowsW = ui->colRowsW;
    out->colWidthW = ui->colWidthW;
    out->colShrinkW = ui->colShrinkW;
    out->colPaletteW = ui->colPaletteW;
    out->colScb1W = ui->colScb1W;
    out->colScbW = ui->colScbW;

    out->colSelectX = originX;
    out->colIndexX = out->colSelectX + out->colSelectW + gap;
    out->colRootX = out->colIndexX + out->colIndexW + gap;
    out->colStatusX = out->colRootX + out->colRootW + gap;
    out->colXposX = out->colStatusX + out->colStatusW + gap;
    out->colYposX = out->colXposX + out->colXposW + gap;
    out->colRowsX = out->colYposX + out->colYposW + gap;
    out->colWidthX = out->colRowsX + out->colRowsW + gap;
    out->colShrinkX = out->colWidthX + out->colWidthW + gap;
    out->colPaletteX = out->colShrinkX + out->colShrinkW + gap;
    out->colScb1X = out->colPaletteX + out->colPaletteW + gap;
    out->colScbX = out->colScb1X + out->colScb1W + gap;
    out->contentWidth = out->colScbX + out->colScbW - originX;
}

static void
neogeo_sprite_list_drawTextClipped(neogeo_sprite_list_state_t *ui,
                                   TTF_Font *font,
                                   int x,
                                   int y,
                                   int width,
                                   const char *text,
                                   SDL_Color color)
{
    if (!ui || !ui->renderer || !font || !text || width <= 0) {
        return;
    }
    int textW = 0;
    int textH = 0;
    SDL_Texture *tex = e9ui_text_cache_getText(ui->renderer, font, text, color, &textW, &textH);
    if (!tex || textW <= 0 || textH <= 0) {
        return;
    }
    SDL_Rect dst = { x, y, textW, textH };
    int hadClip = SDL_RenderIsClipEnabled(ui->renderer) ? 1 : 0;
    SDL_Rect prevClip = { 0, 0, 0, 0 };
    if (hadClip) {
        SDL_RenderGetClipRect(ui->renderer, &prevClip);
    }
    SDL_Rect clip = { x, y, width, textH };
    if (hadClip) {
        SDL_Rect clipped = { 0, 0, 0, 0 };
        if (SDL_IntersectRect(&prevClip, &clip, &clipped)) {
            SDL_RenderSetClipRect(ui->renderer, &clipped);
        } else {
            SDL_Rect empty = { 0, 0, 0, 0 };
            SDL_RenderSetClipRect(ui->renderer, &empty);
        }
    } else {
        SDL_RenderSetClipRect(ui->renderer, &clip);
    }
    SDL_RenderCopy(ui->renderer, tex, NULL, &dst);
    if (hadClip) {
        SDL_RenderSetClipRect(ui->renderer, &prevClip);
    } else {
        SDL_RenderSetClipRect(ui->renderer, NULL);
    }
}

static int
neogeo_sprite_list_canvasPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)ctx;
    (void)availW;
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;
    neogeo_sprite_list_rebuildFilterCache(ui);
    int lineHeight = neogeo_sprite_list_measureLineHeight();
    return (ui->filteredCount + 1) * lineHeight;
}

static void
neogeo_sprite_list_canvasLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static int
neogeo_sprite_list_canvasHandleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!self || !ev) {
        return 0;
    }
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;
    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_LEFT) {
        neogeo_sprite_list_layout_t layout;
        neogeo_sprite_list_computeLayout(ctx, self->bounds.x + e9ui_scale_px(ctx, 6), &layout);
        if (layout.lineHeight <= 0) {
            return 0;
        }
        neogeo_sprite_list_rebuildFilterCache(ui);
        if (!neogeo_sprite_list_pointInRowContent(ui, ctx, &layout, ev->button.x, ev->button.y)) {
            return 0;
        }
        if (neogeo_sprite_list_pointInCheckbox(ui, ev->button.x, ev->button.y)) {
            return 1;
        }
        int rowIndex = (ev->button.y - self->bounds.y) / layout.lineHeight - 1;
        if (rowIndex >= 0 && rowIndex < ui->filteredCount) {
            int clickedIndex = ui->filteredIndexes[rowIndex];
            if (ev->button.clicks >= 2) {
                neogeo_sprite_list_setSelectedSprite(ui, clickedIndex);
                neogeo_sprite_list_selectSingleCheckbox(ui, ctx, clickedIndex);
            } else if (ui->selectedSpriteIndex == clickedIndex) {
                neogeo_sprite_list_setSelectedSprite(ui, -1);
            } else {
                neogeo_sprite_list_setSelectedSprite(ui, clickedIndex);
            }
            neogeo_sprite_list_applySelection(ui);
            neogeo_sprite_list_refreshPausedSelectionFrame();
            return 1;
        }
    }
    return 0;
}

static void
neogeo_sprite_list_canvasRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) {
        return;
    }
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;
    if (!ui->windowState.open) {
        return;
    }
    ui->ctx = *ctx;
    ui->ctx.font = e9ui->ctx.font;
    ui->renderer = ctx->renderer;
    ui->window = ctx->window;

    TTF_Font *font = e9ui->ctx.font;
    if (!font) {
        return;
    }

    neogeo_sprite_list_layout_t layout;
    neogeo_sprite_list_computeLayout(ctx, self->bounds.x + e9ui_scale_px(ctx, 6), &layout);
    if (layout.lineHeight <= 0) {
        return;
    }
    if (ui->listScroll) {
        e9ui_scroll_setContentWidthPx(ui->listScroll, layout.contentWidth + 12);
    }
    neogeo_sprite_list_rebuildFilterCache(ui);

    SDL_Rect viewRect = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    if (SDL_RenderIsClipEnabled(ctx->renderer)) {
        SDL_Rect clipRect;
        SDL_RenderGetClipRect(ctx->renderer, &clipRect);
        if (!SDL_IntersectRect(&viewRect, &clipRect, &viewRect)) {
            return;
        }
    }

    SDL_SetRenderDrawColor(ctx->renderer, 12, 12, 12, 255);
    SDL_RenderFillRect(ctx->renderer, &viewRect);

    SDL_Color headerColor = { 150, 170, 190, 255 };
    SDL_Color rowColor = { 220, 220, 224, 255 };
    SDL_Color inactiveColor = { 116, 116, 124, 255 };
    SDL_Color visibleColor = { 128, 216, 150, 255 };
    SDL_Color chainColor = { 120, 190, 255, 255 };
    SDL_Color animColor = { 255, 198, 92, 255 };

    int headerY = self->bounds.y;
    neogeo_sprite_list_drawTextClipped(ui, font, layout.colSelectX, headerY, layout.colSelectW, "", headerColor);
    neogeo_sprite_list_drawTextClipped(ui, font, layout.colIndexX, headerY, layout.colIndexW, "#", headerColor);
    neogeo_sprite_list_drawTextClipped(ui, font, layout.colRootX, headerY, layout.colRootW, "ROOT", headerColor);
    neogeo_sprite_list_drawTextClipped(ui, font, layout.colStatusX, headerY, layout.colStatusW, "STATUS", headerColor);
    neogeo_sprite_list_drawTextClipped(ui, font, layout.colXposX, headerY, layout.colXposW, "X", headerColor);
    neogeo_sprite_list_drawTextClipped(ui, font, layout.colYposX, headerY, layout.colYposW, "Y", headerColor);
    neogeo_sprite_list_drawTextClipped(ui, font, layout.colRowsX, headerY, layout.colRowsW, "ROWS", headerColor);
    neogeo_sprite_list_drawTextClipped(ui, font, layout.colWidthX, headerY, layout.colWidthW, "W", headerColor);
    neogeo_sprite_list_drawTextClipped(ui, font, layout.colShrinkX, headerY, layout.colShrinkW, "SHRINK", headerColor);
    neogeo_sprite_list_drawTextClipped(ui, font, layout.colPaletteX, headerY, layout.colPaletteW, "PAL", headerColor);
    neogeo_sprite_list_drawTextClipped(ui, font, layout.colScb1X, headerY, layout.colScb1W, "SCB1", headerColor);
    neogeo_sprite_list_drawTextClipped(ui, font, layout.colScbX, headerY, layout.colScbW, "SCB2 SCB3 SCB4", headerColor);

    int topRow = 0;
    if (viewRect.y > self->bounds.y + layout.lineHeight) {
        topRow = (viewRect.y - self->bounds.y) / layout.lineHeight - 1;
    }
    if (topRow < 0) {
        topRow = 0;
    }
    for (int rowIndex = 0; rowIndex < NEOGEO_SPRITE_LIST_MAX_SPRITES; ++rowIndex) {
        e9ui_component_t *checkbox = ui->rowCheckboxes[rowIndex];
        if (checkbox && checkbox->layout) {
            checkbox->layout(checkbox, ctx, (e9ui_rect_t){ 0, 0, 0, 0 });
        }
    }
    int visibleRows = viewRect.h / layout.lineHeight + 2;
    for (int filteredRow = topRow; filteredRow < ui->filteredCount; ++filteredRow) {
        int y = self->bounds.y + (filteredRow + 1) * layout.lineHeight;
        if (y >= viewRect.y + viewRect.h) {
            break;
        }
        if (y + layout.lineHeight <= viewRect.y) {
            continue;
        }
        int rowIndex = ui->filteredIndexes[filteredRow];
        const neogeo_sprite_list_row_t *row = &ui->rows[rowIndex];
        if (ui->selectedRowIndex == rowIndex) {
            SDL_SetRenderDrawColor(ctx->renderer, 46, 55, 68, 255);
            SDL_Rect rowBg = { self->bounds.x, y, self->bounds.w, layout.lineHeight };
            SDL_RenderFillRect(ctx->renderer, &rowBg);
        } else if ((filteredRow & 1) != 0) {
            SDL_SetRenderDrawColor(ctx->renderer, 16, 16, 20, 255);
            SDL_Rect rowBg = { self->bounds.x, y, self->bounds.w, layout.lineHeight };
            SDL_RenderFillRect(ctx->renderer, &rowBg);
        }

        const neogeo_sprite_list_display_row_t *display = &ui->displayRows[rowIndex];
        e9ui_component_t *checkbox = ui->rowCheckboxes[rowIndex];
        if (checkbox && checkbox->layout) {
            if (neogeo_sprite_list_rowCheckboxVisible(ui, rowIndex)) {
                e9ui_rect_t checkboxBounds = {
                    layout.colSelectX,
                    y,
                    layout.colSelectW,
                    layout.lineHeight
                };
                checkbox->layout(checkbox, ctx, checkboxBounds);
                neogeo_sprite_list_syncRowCheckbox(ui, ctx, rowIndex);
                if (checkbox->render) {
                    checkbox->render(checkbox, ctx);
                }
            } else {
                checkbox->layout(checkbox, ctx, (e9ui_rect_t){ 0, 0, 0, 0 });
            }
        }

        SDL_Color statusColor = row->active ? (row->visible ? visibleColor : rowColor) : inactiveColor;
        if (row->hasAnimBits) {
            statusColor = animColor;
        } else if (row->sticky) {
            statusColor = chainColor;
        }
        SDL_Color textColor = row->active ? rowColor : inactiveColor;
        neogeo_sprite_list_drawTextClipped(ui, font, layout.colIndexX, y, layout.colIndexW, display->index, textColor);
        neogeo_sprite_list_drawTextClipped(ui, font, layout.colRootX, y, layout.colRootW, display->root, chainColor);
        neogeo_sprite_list_drawTextClipped(ui, font, layout.colStatusX, y, layout.colStatusW, display->status, statusColor);
        neogeo_sprite_list_drawTextClipped(ui, font, layout.colXposX, y, layout.colXposW, display->xpos, textColor);
        neogeo_sprite_list_drawTextClipped(ui, font, layout.colYposX, y, layout.colYposW, display->ypos, textColor);
        neogeo_sprite_list_drawTextClipped(ui, font, layout.colRowsX, y, layout.colRowsW, display->rows, textColor);
        neogeo_sprite_list_drawTextClipped(ui, font, layout.colWidthX, y, layout.colWidthW, display->width, textColor);
        neogeo_sprite_list_drawTextClipped(ui, font, layout.colShrinkX, y, layout.colShrinkW, display->shrink, textColor);
        neogeo_sprite_list_drawTextClipped(ui, font, layout.colPaletteX, y, layout.colPaletteW, display->palette, textColor);
        neogeo_sprite_list_drawTextClipped(ui, font, layout.colScb1X, y, layout.colScb1W, display->scb1, textColor);
        neogeo_sprite_list_drawTextClipped(ui, font, layout.colScbX, y, layout.colScbW, display->scb, textColor);

        visibleRows--;
        if (visibleRows <= 0) {
            break;
        }
    }
}

static void
neogeo_sprite_list_canvasDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self || !self->state) {
        return;
    }
    alloc_free(self->state);
    self->state = NULL;
}

static e9ui_component_t *
neogeo_sprite_list_makeCanvas(void)
{
    e9ui_component_t *canvas = (e9ui_component_t *)alloc_calloc(1, sizeof(*canvas));
    neogeo_sprite_list_canvas_state_t *st =
        (neogeo_sprite_list_canvas_state_t *)alloc_calloc(1, sizeof(*st));
    if (!canvas || !st) {
        alloc_free(canvas);
        alloc_free(st);
        return NULL;
    }
    canvas->name = "neogeo_sprite_list_canvas";
    canvas->state = st;
    canvas->preferredHeight = neogeo_sprite_list_canvasPreferredHeight;
    canvas->layout = neogeo_sprite_list_canvasLayout;
    canvas->render = neogeo_sprite_list_canvasRender;
    canvas->handleEvent = neogeo_sprite_list_canvasHandleEvent;
    canvas->dtor = neogeo_sprite_list_canvasDtor;
    neogeo_sprite_list_makeRowCheckboxes(&neogeo_sprite_list_state, canvas);
    return canvas;
}

static void
neogeo_sprite_list_centerSelectedRow(neogeo_sprite_list_state_t *ui)
{
    int selectedIndex = ui ? ui->selectedRowIndex : -1;
    if (!ui || !ui->windowState.open || !ui->listScroll || selectedIndex < 0) {
        return;
    }

    neogeo_sprite_list_rebuildFilterCache(ui);
    int displayIndex = -1;
    for (int i = 0; i < ui->filteredCount; ++i) {
        if (ui->filteredIndexes[i] == selectedIndex) {
            displayIndex = i;
            break;
        }
    }
    if (displayIndex < 0) {
        return;
    }

    int lineHeight = neogeo_sprite_list_measureLineHeight();
    if (lineHeight <= 0) {
        return;
    }

    int scrollX = 0;
    int scrollY = 0;
    e9ui_scroll_getScrollPx(ui->listScroll, &scrollX, &scrollY);

    int rowCenterY = (displayIndex + 1) * lineHeight + lineHeight / 2;
    int viewH = ui->listScroll->bounds.h;
    int targetScrollY = rowCenterY - viewH / 2;
    e9ui_scroll_setScrollPx(ui->listScroll, scrollX, targetScrollY);
}

static void
neogeo_sprite_list_filterTextboxChanged(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    neogeo_sprite_list_filter_cb_t *cb = (neogeo_sprite_list_filter_cb_t *)user;
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;
    if (!cb || cb->filterIndex < 0 || cb->filterIndex >= NEOGEO_SPRITE_LIST_FILTER_COUNT) {
        return;
    }
    const char *text = e9ui_textbox_getText(ui->filterTextboxes[cb->filterIndex]);
    if (!text) {
        text = "";
    }
    if (strncmp(ui->filters[cb->filterIndex], text, NEOGEO_SPRITE_LIST_FILTER_TEXT_MAX) != 0) {
        snprintf(ui->filters[cb->filterIndex], NEOGEO_SPRITE_LIST_FILTER_TEXT_MAX, "%s", text);
        ui->filterCacheDirty = 1;
        if (ui->listScroll) {
            e9ui_scroll_setScrollPx(ui->listScroll, 0, 0);
        }
    }
}

static void
neogeo_sprite_list_activeOnlyChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    (void)user;
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;
    ui->activeOnly = selected ? 1 : 0;
    ui->filterCacheDirty = 1;
    config_saveConfig();
    if (ui->listScroll) {
        e9ui_scroll_setScrollPx(ui->listScroll, 0, 0);
    }
}

static void
neogeo_sprite_list_highlightChainChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    (void)user;
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;
    ui->highlightChain = selected ? 1 : 0;
    if (ui->selectedSpriteIndex >= 0) {
        neogeo_sprite_list_setSelectedSprite(ui, ui->selectedSpriteIndex);
    }
    ui->filterCacheDirty = 1;
    neogeo_sprite_list_applySelection(ui);
    neogeo_sprite_list_refreshPausedSelectionFrame();
    config_saveConfig();
}

static void
neogeo_sprite_list_collapseChainChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    (void)user;
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;
    ui->collapseChain = selected ? 1 : 0;
    if (ui->selectedSpriteIndex >= 0) {
        neogeo_sprite_list_setSelectedSprite(ui, ui->selectedSpriteIndex);
    }
    ui->filterCacheDirty = 1;
    config_saveConfig();
    if (ui->listScroll) {
        e9ui_scroll_setScrollPx(ui->listScroll, 0, 0);
    }
}

static void
neogeo_sprite_list_grayscaleSelectionChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    (void)user;
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;
    ui->grayscaleSelection = selected ? 1 : 0;
    neogeo_sprite_list_applySelection(ui);
    neogeo_sprite_list_refreshPausedSelectionFrame();
    config_saveConfig();
}

static void
neogeo_sprite_list_invertSelectionChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    (void)user;
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;
    ui->invertSelection = selected ? 1 : 0;
    neogeo_sprite_list_applySelection(ui);
    neogeo_sprite_list_refreshPausedSelectionFrame();
    config_saveConfig();
}

static void
neogeo_sprite_list_hideSelectionChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    (void)user;
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;
    ui->hideSelection = selected ? 1 : 0;
    neogeo_sprite_list_applySelection(ui);
    neogeo_sprite_list_refreshPausedSelectionFrame();
    config_saveConfig();
}

static void
neogeo_sprite_list_clearFilterClicked(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;
    for (int i = 0; i < NEOGEO_SPRITE_LIST_FILTER_COUNT; ++i) {
        ui->filters[i][0] = '\0';
        if (ui->filterTextboxes[i]) {
            e9ui_textbox_setText(ui->filterTextboxes[i], "");
        }
    }
    memset(ui->selectedMask, 0, sizeof(ui->selectedMask));
    ui->suppressCheckboxCallback = 1;
    for (int i = 0; i < NEOGEO_SPRITE_LIST_MAX_SPRITES; ++i) {
        if (ui->rowCheckboxes[i]) {
            e9ui_checkbox_setSelected(ui->rowCheckboxes[i], 0, ctx);
        }
    }
    ui->suppressCheckboxCallback = 0;
    neogeo_sprite_list_applySelection(ui);
    neogeo_sprite_list_refreshPausedSelectionFrame();
    ui->filterCacheDirty = 1;
}

static e9ui_component_t *
neogeo_sprite_list_buildFilterRoot(void)
{
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;
    e9ui_component_t *root = e9ui_stack_makeVertical();
    e9ui_component_t *controlsRow = e9ui_hstack_make();
    e9ui_component_t *filterRow = e9ui_hstack_make();
    e9ui_component_t *activeOnly = e9ui_checkbox_make("Active",
                                                       ui->activeOnly,
                                                       neogeo_sprite_list_activeOnlyChanged,
                                                       NULL);
    e9ui_component_t *highlightChain = e9ui_checkbox_make("Chain",
                                                          ui->highlightChain,
                                                          neogeo_sprite_list_highlightChainChanged,
                                                          NULL);
    e9ui_component_t *collapseChain = e9ui_checkbox_make("Collapse",
                                                         ui->collapseChain,
                                                         neogeo_sprite_list_collapseChainChanged,
                                                         NULL);
    e9ui_component_t *grayscaleSelection = e9ui_checkbox_make("Identify",
                                                              ui->grayscaleSelection,
                                                              neogeo_sprite_list_grayscaleSelectionChanged,
                                                              NULL);
    e9ui_component_t *invertSelection = e9ui_checkbox_make("Invert",
                                                           ui->invertSelection,
                                                           neogeo_sprite_list_invertSelectionChanged,
                                                           NULL);
    e9ui_component_t *hideSelection = e9ui_checkbox_make("Hide",
                                                         ui->hideSelection,
                                                         neogeo_sprite_list_hideSelectionChanged,
                                                         NULL);
    e9ui_component_t *clearButton = e9ui_button_make("Clear", neogeo_sprite_list_clearFilterClicked, NULL);
    if (!root || !controlsRow || !filterRow) {
        return NULL;
    }
    if (clearButton) {
        e9ui_button_setMini(clearButton, 1);
    }
    int activeW = 74;
    int chainW = 74;
    int collapseW = 94;
    int grayW = 74;
    int invertW = 74;
    int hideW = 74;
    int clearW = 60;
    if (activeOnly) {
        e9ui_checkbox_measure(activeOnly, &ui->ctx, &activeW, NULL);
    }
    if (highlightChain) {
        e9ui_checkbox_measure(highlightChain, &ui->ctx, &chainW, NULL);
    }
    if (collapseChain) {
        e9ui_checkbox_measure(collapseChain, &ui->ctx, &collapseW, NULL);
    }
    if (grayscaleSelection) {
        e9ui_checkbox_measure(grayscaleSelection, &ui->ctx, &grayW, NULL);
    }
    if (invertSelection) {
        e9ui_checkbox_measure(invertSelection, &ui->ctx, &invertW, NULL);
    }
    if (hideSelection) {
        e9ui_checkbox_measure(hideSelection, &ui->ctx, &hideW, NULL);
    }
    if (clearButton) {
        e9ui_button_measure(clearButton, &ui->ctx, &clearW, NULL);
    }
    e9ui_hstack_addFixed(controlsRow, activeOnly, activeW);
    e9ui_hstack_addFixed(controlsRow, e9ui_spacer_make(NEOGEO_SPRITE_LIST_GAP), NEOGEO_SPRITE_LIST_GAP);
    e9ui_hstack_addFixed(controlsRow, highlightChain, chainW);
    e9ui_hstack_addFixed(controlsRow, e9ui_spacer_make(NEOGEO_SPRITE_LIST_GAP), NEOGEO_SPRITE_LIST_GAP);
    e9ui_hstack_addFixed(controlsRow, collapseChain, collapseW);
    e9ui_hstack_addFixed(controlsRow, e9ui_spacer_make(NEOGEO_SPRITE_LIST_GAP), NEOGEO_SPRITE_LIST_GAP);
    e9ui_hstack_addFixed(controlsRow, grayscaleSelection, grayW);
    e9ui_hstack_addFixed(controlsRow, e9ui_spacer_make(NEOGEO_SPRITE_LIST_GAP), NEOGEO_SPRITE_LIST_GAP);
    e9ui_hstack_addFixed(controlsRow, invertSelection, invertW);
    e9ui_hstack_addFixed(controlsRow, e9ui_spacer_make(NEOGEO_SPRITE_LIST_GAP), NEOGEO_SPRITE_LIST_GAP);
    e9ui_hstack_addFixed(controlsRow, hideSelection, hideW);
    e9ui_hstack_addFlex(controlsRow, e9ui_spacer_make(1));
    e9ui_hstack_addFixed(controlsRow, clearButton, clearW);

    TTF_Font *font = ui->ctx.font ? ui->ctx.font : e9ui->ctx.font;
    if (!ui->measurementsValid || ui->measuredFont != font) {
        neogeo_sprite_list_refreshDisplayMeasurements(ui, font);
    }
    neogeo_sprite_list_layout_t layout;
    neogeo_sprite_list_computeLayout(&ui->ctx, 0, &layout);
    if (layout.colIndexX > 0) {
        e9ui_hstack_addFixed(filterRow, e9ui_spacer_make(layout.colIndexX), layout.colIndexX);
    }
    for (int i = 0; i < NEOGEO_SPRITE_LIST_FILTER_COUNT; ++i) {
        ui->filterCbs[i].filterIndex = i;
        e9ui_component_t *textbox = e9ui_textbox_make(NEOGEO_SPRITE_LIST_FILTER_TEXT_MAX - 1,
                                                      NULL,
                                                      neogeo_sprite_list_filterTextboxChanged,
                                                      &ui->filterCbs[i]);
        if (textbox) {
            e9ui_textbox_setPlaceholder(textbox, neogeo_sprite_list_filterLabel(i));
            e9ui_textbox_setText(textbox, ui->filters[i]);
        }
        ui->filterTextboxes[i] = textbox;
        e9ui_hstack_addFixed(filterRow, textbox, neogeo_sprite_list_filterColumnWidth(ui, i));
        if (i + 1 < NEOGEO_SPRITE_LIST_FILTER_COUNT) {
            e9ui_hstack_addFixed(filterRow,
                                 e9ui_spacer_make(NEOGEO_SPRITE_LIST_COL_GAP),
                                 NEOGEO_SPRITE_LIST_COL_GAP);
        }
    }

    e9ui_stack_addFixed(root, controlsRow);
    e9ui_stack_addFixed(root, e9ui_vspacer_make(NEOGEO_SPRITE_LIST_FILTER_GAP_Y));
    e9ui_stack_addFixed(root, filterRow);
    return root;
}

static e9ui_component_t *
neogeo_sprite_list_wrapRowWithSidePadding(e9ui_component_t *child, int leftPad, int rightPad)
{
    if (!child) {
        return NULL;
    }
    e9ui_component_t *row = e9ui_hstack_make();
    if (leftPad > 0) {
        e9ui_hstack_addFixed(row, e9ui_spacer_make(leftPad), leftPad);
    }
    e9ui_hstack_addFlex(row, child);
    if (rightPad > 0) {
        e9ui_hstack_addFixed(row, e9ui_spacer_make(rightPad), rightPad);
    }
    return row;
}

static e9ui_component_t *
neogeo_sprite_list_buildRoot(void)
{
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;
    e9ui_component_t *root = e9ui_stack_makeVertical();
    e9ui_component_t *filterRoot = neogeo_sprite_list_buildFilterRoot();
    e9ui_component_t *canvas = neogeo_sprite_list_makeCanvas();
    e9ui_component_t *listScroll = e9ui_scroll_make(canvas);
    if (!root || !filterRoot || !canvas || !listScroll) {
        return NULL;
    }
    ui->filterRoot = filterRoot;
    ui->listCanvas = canvas;
    ui->listScroll = listScroll;
    neogeo_sprite_list_layout_t layout;
    neogeo_sprite_list_computeLayout(&ui->ctx, 0, &layout);
    e9ui_scroll_setContentWidthPx(listScroll, layout.contentWidth + 12);

    const int filterPad = NEOGEO_SPRITE_LIST_PAD + 6;
    e9ui_stack_addFixed(root, e9ui_vspacer_make(NEOGEO_SPRITE_LIST_PAD));
    e9ui_stack_addFixed(root, neogeo_sprite_list_wrapRowWithSidePadding(filterRoot, filterPad, filterPad));
    e9ui_stack_addFixed(root, e9ui_vspacer_make(NEOGEO_SPRITE_LIST_FILTER_GAP_Y));
    e9ui_stack_addFlex(root, neogeo_sprite_list_wrapRowWithSidePadding(listScroll, NEOGEO_SPRITE_LIST_PAD, NEOGEO_SPRITE_LIST_PAD));
    e9ui_stack_addFixed(root, e9ui_vspacer_make(NEOGEO_SPRITE_LIST_PAD));
    return root;
}

static void
neogeo_sprite_list_overlayBodyLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self || !ctx) {
        return;
    }
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;
    if (!ui->root) {
        return;
    }
    self->bounds = bounds;
    ui->ctx = *ctx;
    ui->ctx.window = ctx->window;
    ui->ctx.renderer = ctx->renderer;
    ui->ctx.font = e9ui->ctx.font;
    ui->ctx.winW = bounds.w;
    ui->ctx.winH = bounds.h;
    ui->ctx.focusRoot = ui->root;
    ui->ctx.focusFullscreen = NULL;
    if (ui->root->layout) {
        ui->root->layout(ui->root, &ui->ctx, bounds);
    }
}

static void
neogeo_sprite_list_overlayBodyRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) {
        return;
    }
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;
    if (!ui->windowState.open || !ui->root) {
        return;
    }
    int hadClip = SDL_RenderIsClipEnabled(ctx->renderer) ? 1 : 0;
    SDL_Rect prevClip = { 0, 0, 0, 0 };
    if (hadClip) {
        SDL_RenderGetClipRect(ctx->renderer, &prevClip);
    }
    SDL_Rect clip = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    if (hadClip) {
        SDL_Rect clipped;
        if (SDL_IntersectRect(&prevClip, &clip, &clipped)) {
            SDL_RenderSetClipRect(ctx->renderer, &clipped);
        } else {
            SDL_Rect empty = { 0, 0, 0, 0 };
            SDL_RenderSetClipRect(ctx->renderer, &empty);
        }
    } else {
        SDL_RenderSetClipRect(ctx->renderer, &clip);
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
    ui->renderer = ctx->renderer;
    ui->window = ctx->window;

    if (ui->root->render) {
        ui->root->render(ui->root, &ui->ctx);
    }
    if (hadClip) {
        SDL_RenderSetClipRect(ctx->renderer, &prevClip);
    } else {
        SDL_RenderSetClipRect(ctx->renderer, NULL);
    }
}

static void
neogeo_sprite_list_overlayBodyDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self || !self->state) {
        return;
    }
    alloc_free(self->state);
    self->state = NULL;
}

static e9ui_component_t *
neogeo_sprite_list_makeOverlayBodyHost(void)
{
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;
    if (!ui->root) {
        return NULL;
    }
    e9ui_component_t *host = (e9ui_component_t *)alloc_calloc(1, sizeof(*host));
    neogeo_sprite_list_overlay_body_state_t *st =
        (neogeo_sprite_list_overlay_body_state_t *)alloc_calloc(1, sizeof(*st));
    if (!host || !st) {
        alloc_free(host);
        alloc_free(st);
        return NULL;
    }
    host->name = "neogeo_sprite_list_overlay_body";
    host->state = st;
    host->layout = neogeo_sprite_list_overlayBodyLayout;
    host->render = neogeo_sprite_list_overlayBodyRender;
    host->dtor = neogeo_sprite_list_overlayBodyDtor;
    e9ui_child_add(host, ui->root, alloc_strdup("neogeo_sprite_list_root"));
    return host;
}

static void
neogeo_sprite_list_overlayWindowCloseRequested(e9ui_window_t *window, void *user)
{
    (void)window;
    (void)user;
    neogeo_sprite_list_toggle();
}

void
neogeo_sprite_list_toggle(void)
{
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;
    if (!ui->windowState.open) {
        ui->windowState.windowHost = e9ui_windowCreate(neogeo_sprite_list_windowBackend());
        if (!ui->windowState.windowHost) {
            return;
        }
        ui->ctx = e9ui->ctx;
        ui->root = neogeo_sprite_list_buildRoot();
        e9ui_component_t *overlayBodyHost = neogeo_sprite_list_makeOverlayBodyHost();
        if (!ui->root || !overlayBodyHost) {
            if (ui->windowState.windowHost) {
                e9ui_windowDestroy(ui->windowState.windowHost);
                ui->windowState.windowHost = NULL;
            }
            ui->root = NULL;
            ui->filterRoot = NULL;
            ui->listScroll = NULL;
            ui->listCanvas = NULL;
            return;
        }
        e9ui_rect_t rect = e9ui_windowResolveStateOpenRect(&e9ui->ctx,
                                                           neogeo_sprite_list_windowDefaultRect(&e9ui->ctx),
                                                           &ui->windowState);
        e9ui_windowOpen(ui->windowState.windowHost,
                        "SPRITE LIST",
                        rect,
                        overlayBodyHost,
                        neogeo_sprite_list_overlayWindowCloseRequested,
                        NULL,
                        &e9ui->ctx);
        ui->window = e9ui->ctx.window;
        ui->renderer = e9ui->ctx.renderer;
        ui->windowState.open = 1;
        ui->filterCacheDirty = 1;
    } else {
        (void)e9ui_windowCaptureStateRectSnapshot(&ui->windowState, &e9ui->ctx);
        config_saveConfig();
        e9ui_text_cache_clearRenderer(ui->renderer);
        if (ui->windowState.windowHost) {
            e9ui_windowDestroy(ui->windowState.windowHost);
            ui->windowState.windowHost = NULL;
        }
        ui->root = NULL;
        ui->filterRoot = NULL;
        ui->listScroll = NULL;
        ui->listCanvas = NULL;
        for (int i = 0; i < NEOGEO_SPRITE_LIST_FILTER_COUNT; ++i) {
            ui->filterTextboxes[i] = NULL;
        }
        for (int i = 0; i < NEOGEO_SPRITE_LIST_MAX_SPRITES; ++i) {
            ui->rowCheckboxes[i] = NULL;
        }
        ui->window = NULL;
        ui->renderer = NULL;
        ui->windowState.open = 0;
        neogeo_sprite_list_setSelectedSprite(ui, -1);
        neogeo_sprite_debug_setSelection(-1, -1, 0);
        e9k_debug_sprite_grayscale_selection_t selection = {0};
        (void)libretro_host_neogeo_setSpriteGrayscaleSelection(&selection);
    }
}

int
neogeo_sprite_list_isOpen(void)
{
    return neogeo_sprite_list_state.windowState.open ? 1 : 0;
}

int
neogeo_sprite_list_getSelectedSprite(void)
{
    return neogeo_sprite_list_state.selectedSpriteIndex;
}

void
neogeo_sprite_list_selectSprite(int spriteIndex)
{
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;
    if (ui->hasLastState) {
        ui->rowsValid = neogeo_sprite_list_decodeRows(ui, &ui->lastState);
    }
    neogeo_sprite_list_setSelectedSprite(ui, spriteIndex);
    neogeo_sprite_list_applySelection(ui);
    neogeo_sprite_list_refreshPausedSelectionFrame();
    neogeo_sprite_list_centerSelectedRow(ui);
}

void
neogeo_sprite_list_selectSpriteSingleCheckbox(int spriteIndex)
{
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;
    if (ui->hasLastState) {
        ui->rowsValid = neogeo_sprite_list_decodeRows(ui, &ui->lastState);
    }
    if (spriteIndex < 0 || spriteIndex >= NEOGEO_SPRITE_LIST_MAX_SPRITES) {
        neogeo_sprite_list_setSelectedSprite(ui, -1);
        memset(ui->selectedMask, 0, sizeof(ui->selectedMask));
    } else {
        neogeo_sprite_list_setSelectedSprite(ui, spriteIndex);
        neogeo_sprite_list_selectSingleCheckbox(ui, &ui->ctx, spriteIndex);
    }
    neogeo_sprite_list_applySelection(ui);
    neogeo_sprite_list_refreshPausedSelectionFrame();
    neogeo_sprite_list_centerSelectedRow(ui);
}

void
neogeo_sprite_list_render(const e9k_debug_sprite_state_t *st)
{
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;
    if (st) {
        ui->lastState = *st;
        ui->hasLastState = 1;
        int rowsDecoded = neogeo_sprite_list_decodeRows(ui, st);
        ui->rowsValid = rowsDecoded;
        if (ui->selectedSpriteIndex >= 0) {
            neogeo_sprite_list_setSelectedSprite(ui, ui->selectedSpriteIndex);
        }
        if (rowsDecoded && (ui->selectedSpriteIndex >= 0 ||
                            neogeo_sprite_list_maskHasBits(ui->selectedMask))) {
            neogeo_sprite_list_applySelection(ui);
        }
    }
    if (!ui->windowState.open) {
        return;
    }
    if (e9ui_windowCaptureStateRectChanged(&ui->windowState, &e9ui->ctx)) {
        config_saveConfig();
    }
}

void
neogeo_sprite_list_persistConfig(FILE *file)
{
    if (!file) {
        return;
    }
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;
    e9ui_windowPersistStateRect(file, "comp.sprite_list", &ui->windowState, &e9ui->ctx);
    fprintf(file, "comp.sprite_list.active_only=%d\n", ui->activeOnly ? 1 : 0);
    fprintf(file, "comp.sprite_list.highlight_chain=%d\n", ui->highlightChain ? 1 : 0);
    fprintf(file, "comp.sprite_list.collapse_chain=%d\n", ui->collapseChain ? 1 : 0);
    fprintf(file, "comp.sprite_list.grayscale_selection=%d\n", ui->grayscaleSelection ? 1 : 0);
    fprintf(file, "comp.sprite_list.invert_selection=%d\n", ui->invertSelection ? 1 : 0);
    fprintf(file, "comp.sprite_list.hide_selection=%d\n", ui->hideSelection ? 1 : 0);
}

int
neogeo_sprite_list_loadConfigProperty(const char *prop, const char *value)
{
    neogeo_sprite_list_state_t *ui = &neogeo_sprite_list_state;
    if (!prop || !value) {
        return 0;
    }
    int intValue = 0;
    if (strcmp(prop, "win_x") == 0) {
        if (!neogeo_sprite_list_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winX = intValue;
    } else if (strcmp(prop, "win_y") == 0) {
        if (!neogeo_sprite_list_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winY = intValue;
    } else if (strcmp(prop, "win_w") == 0) {
        if (!neogeo_sprite_list_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winW = intValue;
    } else if (strcmp(prop, "win_h") == 0) {
        if (!neogeo_sprite_list_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winH = intValue;
    } else if (strcmp(prop, "active_only") == 0) {
        if (!neogeo_sprite_list_parseInt(value, &intValue)) {
            return 0;
        }
        ui->activeOnly = intValue ? 1 : 0;
        ui->filterCacheDirty = 1;
    } else if (strcmp(prop, "highlight_chain") == 0) {
        if (!neogeo_sprite_list_parseInt(value, &intValue)) {
            return 0;
        }
        ui->highlightChain = intValue ? 1 : 0;
        ui->filterCacheDirty = 1;
    } else if (strcmp(prop, "collapse_chain") == 0) {
        if (!neogeo_sprite_list_parseInt(value, &intValue)) {
            return 0;
        }
        ui->collapseChain = intValue ? 1 : 0;
        ui->filterCacheDirty = 1;
    } else if (strcmp(prop, "grayscale_selection") == 0) {
        if (!neogeo_sprite_list_parseInt(value, &intValue)) {
            return 0;
        }
        ui->grayscaleSelection = intValue ? 1 : 0;
    } else if (strcmp(prop, "invert_selection") == 0) {
        if (!neogeo_sprite_list_parseInt(value, &intValue)) {
            return 0;
        }
        ui->invertSelection = intValue ? 1 : 0;
    } else if (strcmp(prop, "hide_selection") == 0) {
        if (!neogeo_sprite_list_parseInt(value, &intValue)) {
            return 0;
        }
        ui->hideSelection = intValue ? 1 : 0;
    } else {
        return 0;
    }
    ui->windowState.winHasSaved =
        e9ui_windowHasSavedPosition(ui->windowState.winX, ui->windowState.winY);
    return 1;
}
