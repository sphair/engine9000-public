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
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "aux_window.h"
#include "config.h"
#include "breakpoints.h"
#include "amiga_custom_log.h"
#include "debug.h"
#include "debugger.h"
#include "e9ui.h"
#include "e9ui_scrollbar.h"
#include "libretro_host.h"
#include "amiga_custom_regs.h"
#include "ui.h"

#define AMIGA_CUSTOM_LOG_PAD 10
#define AMIGA_CUSTOM_LOG_ROW_PAD_Y 2
#define AMIGA_CUSTOM_LOG_COL_GAP 12
#define AMIGA_CUSTOM_LOG_LINE_COL_W 112
#define AMIGA_CUSTOM_LOG_NAME_COL_W 128
#define AMIGA_CUSTOM_LOG_SRC_COL_W 75
#define AMIGA_CUSTOM_LOG_ADDR_COL_W 120
#define AMIGA_CUSTOM_LOG_VALUE_COL_W 109
#define AMIGA_CUSTOM_LOG_FILTER_COUNT 6
#define AMIGA_CUSTOM_LOG_FILTER_TEXT_MAX 128
#define AMIGA_CUSTOM_LOG_FILTER_VPAD 3
#define AMIGA_CUSTOM_LOG_FILTER_GAP_Y 6

typedef enum amiga_custom_log_filter_index {
    amiga_custom_log_filter_line = 0,
    amiga_custom_log_filter_name = 1,
    amiga_custom_log_filter_src = 2,
    amiga_custom_log_filter_addr = 3,
    amiga_custom_log_filter_value = 4,
    amiga_custom_log_filter_desc = 5
} amiga_custom_log_filter_index_t;

typedef struct amiga_custom_log_state amiga_custom_log_state_t;

typedef struct amiga_custom_log_filter_cb {
    amiga_custom_log_state_t *ui;
    int filterIndex;
} amiga_custom_log_filter_cb_t;

typedef struct amiga_custom_log_overlay_body_state {
    amiga_custom_log_state_t *ui;
} amiga_custom_log_overlay_body_state_t;

typedef struct amiga_custom_log_layout {
    int winW;
    int winH;
    int lineHeight;
    int filterHeight;
    int rowsY;
    int rowsH;
    int colX[AMIGA_CUSTOM_LOG_FILTER_COUNT];
    int colW[AMIGA_CUSTOM_LOG_FILTER_COUNT];
} amiga_custom_log_layout_t;

typedef struct amiga_custom_log_row_hit {
    SDL_Rect nameRect;
    SDL_Rect addrRect;
    SDL_Rect valueRect;
    uint32_t sourceAddr;
    uint16_t regOffset;
    uint16_t regValue;
    uint8_t sourceIsCopper;
} amiga_custom_log_row_hit_t;

typedef struct amiga_custom_log_scroll_model {
    int visibleRows;
    int filteredCount;
    int topFilteredRow;
    int topRawRow;
} amiga_custom_log_scroll_model_t;

struct amiga_custom_log_state {
    e9ui_window_state_t windowState;
    int warnedMissingOption;
    SDL_Window *window;
    SDL_Renderer *renderer;
    e9ui_context_t ctx;
    e9ui_component_t *filterRoot;
    e9ui_component_t *filterTextboxes[AMIGA_CUSTOM_LOG_FILTER_COUNT];
    amiga_custom_log_filter_cb_t filterCbs[AMIGA_CUSTOM_LOG_FILTER_COUNT];
    int scrollRow;
    e9k_debug_ami_custom_log_entry_t *entries;
    size_t entryCount;
    size_t entryCap;
    uint32_t dropped;
    uint64_t frameNo;
    uint64_t framesCaptured;
    uint64_t allocFailures;
    char filters[AMIGA_CUSTOM_LOG_FILTER_COUNT][AMIGA_CUSTOM_LOG_FILTER_TEXT_MAX];
    amiga_custom_log_row_hit_t *rowHits;
    size_t rowHitCount;
    size_t rowHitCap;
    e9ui_scrollbar_state_t scrollbar;
};

static amiga_custom_log_state_t amiga_custom_log_state = {
    .windowState.winX = E9UI_WINDOW_COORD_UNSET,
    .windowState.winY = E9UI_WINDOW_COORD_UNSET,
    .windowState.openMinWidthPx = 420,
    .windowState.openMinHeightPx = 260,
    .windowState.openCenterWhenNoSaved = 1,
};

static const aux_window_ops_t amiga_custom_log_auxWindowOps = {
    .setFocus = amiga_custom_log_setMainWindowFocused,
    .render = amiga_custom_log_render,
};

static e9ui_window_backend_t
amiga_custom_log_windowBackend(void)
{
    return e9ui_window_backend_overlay;
}

static e9ui_component_t *
amiga_custom_log_makeOverlayBodyHost(amiga_custom_log_state_t *ui);

static void
amiga_custom_log_overlayWindowCloseRequested(e9ui_window_t *window, void *user);

static int
amiga_custom_log_entryMatchesFilters(const amiga_custom_log_state_t *ui, const e9k_debug_ami_custom_log_entry_t *entry);

static int
amiga_custom_log_parseInt(const char *value, int *out)
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
amiga_custom_log_measureLineHeight(void)
{
    TTF_Font *font = e9ui->ctx.font;
    int lineHeight = font ? TTF_FontHeight(font) : 0;
    if (lineHeight <= 0) {
        lineHeight = 16;
    }
    return lineHeight;
}

static e9ui_rect_t
amiga_custom_log_windowDefaultRect(const e9ui_context_t *ctx)
{
    e9ui_rect_t rect = {
        e9ui_scale_px(ctx, 96),
        e9ui_scale_px(ctx, 96),
        e9ui_scale_px(ctx, 540),
        e9ui_scale_px(ctx, 360)
    };
    return rect;
}

static int
amiga_custom_log_componentContains(const e9ui_component_t *root, const e9ui_component_t *needle)
{
    if (!root || !needle) {
        return 0;
    }
    if (root == needle) {
        return 1;
    }
    for (list_t *ptr = root->children; ptr; ptr = ptr->next) {
        e9ui_component_child_t *container = (e9ui_component_child_t *)ptr->data;
        if (!container || !container->component) {
            continue;
        }
        if (amiga_custom_log_componentContains(container->component, needle)) {
            return 1;
        }
    }
    return 0;
}

static int
amiga_custom_log_measureFilterHeight(const amiga_custom_log_state_t *ui, int contentW)
{
    if (ui && ui->filterRoot && ui->filterRoot->preferredHeight) {
        int height = ui->filterRoot->preferredHeight(ui->filterRoot, (e9ui_context_t *)&ui->ctx, contentW);
        if (height > 0) {
            return height;
        }
    }
    return amiga_custom_log_measureLineHeight() + AMIGA_CUSTOM_LOG_FILTER_VPAD * 2;
}

static int
amiga_custom_log_computeLayout(const amiga_custom_log_state_t *ui, amiga_custom_log_layout_t *out)
{
    if (!ui || !ui->renderer || !out) {
        return 0;
    }
    int winW = 0;
    int winH = 0;
    if (ui->ctx.winW > 0 && ui->ctx.winH > 0) {
        winW = ui->ctx.winW;
        winH = ui->ctx.winH;
    } else {
        SDL_GetRendererOutputSize(ui->renderer, &winW, &winH);
    }
    if (winW <= 0 || winH <= 0) {
        return 0;
    }
    int lineHeight = amiga_custom_log_measureLineHeight();
    int contentX = AMIGA_CUSTOM_LOG_PAD + 6;
    int contentW = winW - AMIGA_CUSTOM_LOG_PAD * 2 - 12;
    int filterHeight = amiga_custom_log_measureFilterHeight(ui, contentW);
    int rowsY = AMIGA_CUSTOM_LOG_PAD + filterHeight + AMIGA_CUSTOM_LOG_FILTER_GAP_Y;
    int rowsH = winH - AMIGA_CUSTOM_LOG_PAD - rowsY;
    if (rowsH < lineHeight) {
        rowsH = lineHeight;
    }
    int colLineW = AMIGA_CUSTOM_LOG_LINE_COL_W;
    int colNameW = AMIGA_CUSTOM_LOG_NAME_COL_W;
    int colSrcW = AMIGA_CUSTOM_LOG_SRC_COL_W;
    int colAddrW = AMIGA_CUSTOM_LOG_ADDR_COL_W;
    int colValueW = AMIGA_CUSTOM_LOG_VALUE_COL_W;
    int colDescW = contentW - colLineW - colNameW - colSrcW - colAddrW - colValueW - AMIGA_CUSTOM_LOG_COL_GAP * 5;
    if (colDescW < 48) {
        colDescW = 48;
    }

    out->winW = winW;
    out->winH = winH;
    out->lineHeight = lineHeight;
    out->filterHeight = filterHeight;
    out->rowsY = rowsY;
    out->rowsH = rowsH;
    out->colX[amiga_custom_log_filter_line] = contentX;
    out->colW[amiga_custom_log_filter_line] = colLineW;
    out->colX[amiga_custom_log_filter_name] = out->colX[amiga_custom_log_filter_line] + colLineW + AMIGA_CUSTOM_LOG_COL_GAP;
    out->colW[amiga_custom_log_filter_name] = colNameW;
    out->colX[amiga_custom_log_filter_src] = out->colX[amiga_custom_log_filter_name] + colNameW + AMIGA_CUSTOM_LOG_COL_GAP;
    out->colW[amiga_custom_log_filter_src] = colSrcW;
    out->colX[amiga_custom_log_filter_addr] = out->colX[amiga_custom_log_filter_src] + colSrcW + AMIGA_CUSTOM_LOG_COL_GAP;
    out->colW[amiga_custom_log_filter_addr] = colAddrW;
    out->colX[amiga_custom_log_filter_value] = out->colX[amiga_custom_log_filter_addr] + colAddrW + AMIGA_CUSTOM_LOG_COL_GAP;
    out->colW[amiga_custom_log_filter_value] = colValueW;
    out->colX[amiga_custom_log_filter_desc] = out->colX[amiga_custom_log_filter_value] + colValueW + AMIGA_CUSTOM_LOG_COL_GAP;
    out->colW[amiga_custom_log_filter_desc] = colDescW;
    return 1;
}

static int
amiga_custom_log_computeVisibleRows(const amiga_custom_log_state_t *ui)
{
    amiga_custom_log_layout_t layout;
    if (!amiga_custom_log_computeLayout(ui, &layout)) {
        return 1;
    }
    int rows = layout.rowsH / layout.lineHeight;
    if (rows <= 0) {
        rows = 1;
    }
    return rows;
}

static int
amiga_custom_log_hasActiveFilters(const amiga_custom_log_state_t *ui)
{
    if (!ui) {
        return 0;
    }
    for (int i = 0; i < AMIGA_CUSTOM_LOG_FILTER_COUNT; ++i) {
        if (ui->filters[i][0] != '\0') {
            return 1;
        }
    }
    return 0;
}

static int
amiga_custom_log_filteredCount(const amiga_custom_log_state_t *ui)
{
    if (!ui || ui->entryCount == 0) {
        return 0;
    }
    if (!amiga_custom_log_hasActiveFilters(ui)) {
        if (ui->entryCount > (size_t)INT_MAX) {
            return INT_MAX;
        }
        return (int)ui->entryCount;
    }
    int count = 0;
    for (size_t i = 0; i < ui->entryCount; ++i) {
        if (amiga_custom_log_entryMatchesFilters(ui, &ui->entries[i])) {
            if (count < INT_MAX) {
                count++;
            }
        }
    }
    return count;
}

static int
amiga_custom_log_rawRowForFilteredIndex(const amiga_custom_log_state_t *ui, int filteredIndex)
{
    if (!ui || ui->entryCount == 0) {
        return 0;
    }
    if (filteredIndex < 0) {
        filteredIndex = 0;
    }
    if (!amiga_custom_log_hasActiveFilters(ui)) {
        int maxIndex = (ui->entryCount > 0) ? (int)(ui->entryCount - 1) : 0;
        if (filteredIndex > maxIndex) {
            filteredIndex = maxIndex;
        }
        return filteredIndex;
    }
    int matchIndex = 0;
    int lastMatch = -1;
    for (size_t i = 0; i < ui->entryCount; ++i) {
        if (!amiga_custom_log_entryMatchesFilters(ui, &ui->entries[i])) {
            continue;
        }
        lastMatch = (int)i;
        if (matchIndex == filteredIndex) {
            return (int)i;
        }
        matchIndex++;
    }
    return lastMatch >= 0 ? lastMatch : 0;
}

static int
amiga_custom_log_filteredIndexForRawRow(const amiga_custom_log_state_t *ui, int rawRow)
{
    if (!ui || ui->entryCount == 0) {
        return 0;
    }
    if (rawRow < 0) {
        rawRow = 0;
    }
    if ((size_t)rawRow > ui->entryCount) {
        rawRow = (int)ui->entryCount;
    }
    if (!amiga_custom_log_hasActiveFilters(ui)) {
        int maxIndex = (ui->entryCount > 0) ? (int)(ui->entryCount - 1) : 0;
        if (rawRow > maxIndex) {
            rawRow = maxIndex;
        }
        return rawRow;
    }
    int filteredIndex = 0;
    for (size_t i = 0; i < ui->entryCount; ++i) {
        if (!amiga_custom_log_entryMatchesFilters(ui, &ui->entries[i])) {
            continue;
        }
        if ((int)i >= rawRow) {
            return filteredIndex;
        }
        filteredIndex++;
    }
    return filteredIndex;
}

static void
amiga_custom_log_buildScrollModel(const amiga_custom_log_state_t *ui, int visibleRows, amiga_custom_log_scroll_model_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!ui) {
        out->visibleRows = 1;
        return;
    }
    if (visibleRows <= 0) {
        visibleRows = 1;
    }
    out->visibleRows = visibleRows;
    out->filteredCount = amiga_custom_log_filteredCount(ui);
    if (out->filteredCount <= 0) {
        out->topFilteredRow = 0;
        out->topRawRow = 0;
        return;
    }
    int topFiltered = amiga_custom_log_filteredIndexForRawRow(ui, ui->scrollRow);
    int maxTop = e9ui_scrollbar_maxScroll(out->filteredCount, visibleRows);
    if (topFiltered < 0) {
        topFiltered = 0;
    }
    if (topFiltered > maxTop) {
        topFiltered = maxTop;
    }
    out->topFilteredRow = topFiltered;
    out->topRawRow = amiga_custom_log_rawRowForFilteredIndex(ui, topFiltered);
}

static void
amiga_custom_log_setTopFilteredRow(amiga_custom_log_state_t *ui, int visibleRows, int topFilteredRow)
{
    if (!ui) {
        return;
    }
    amiga_custom_log_scroll_model_t model;
    amiga_custom_log_buildScrollModel(ui, visibleRows, &model);
    if (model.filteredCount <= 0) {
        ui->scrollRow = 0;
        return;
    }
    int maxTop = e9ui_scrollbar_maxScroll(model.filteredCount, model.visibleRows);
    if (topFilteredRow < 0) {
        topFilteredRow = 0;
    }
    if (topFilteredRow > maxTop) {
        topFilteredRow = maxTop;
    }
    ui->scrollRow = amiga_custom_log_rawRowForFilteredIndex(ui, topFilteredRow);
}

static void
amiga_custom_log_clampScroll(amiga_custom_log_state_t *ui, int visibleRows)
{
    if (!ui) {
        return;
    }
    if (ui->scrollRow < 0) {
        ui->scrollRow = 0;
    }
    amiga_custom_log_scroll_model_t model;
    amiga_custom_log_buildScrollModel(ui, visibleRows, &model);
    ui->scrollRow = model.topRawRow;
}

static void
amiga_custom_log_adjustScroll(amiga_custom_log_state_t *ui, int deltaRows)
{
    if (!ui || deltaRows == 0) {
        return;
    }
    int visibleRows = amiga_custom_log_computeVisibleRows(ui);
    amiga_custom_log_scroll_model_t model;
    amiga_custom_log_buildScrollModel(ui, visibleRows, &model);
    long next = (long)model.topFilteredRow + (long)deltaRows;
    if (next < 0) {
        next = 0;
    }
    if (next > INT_MAX) {
        next = INT_MAX;
    }
    amiga_custom_log_setTopFilteredRow(ui, visibleRows, (int)next);
}

static int
amiga_custom_log_arrowScrollAmount(SDL_Keymod mods, int visibleRows)
{
    int amount = 1;
    if ((mods & KMOD_CTRL) != 0 || (mods & KMOD_GUI) != 0) {
        amount = visibleRows / 2;
        if (amount < 1) {
            amount = 1;
        }
    } else if ((mods & KMOD_SHIFT) != 0) {
        amount = 4;
    } else if ((mods & KMOD_ALT) != 0) {
        amount = 16;
    }
    return amount;
}

static const char *
amiga_custom_log_filterLabel(int index)
{
    switch (index) {
    case amiga_custom_log_filter_line: return "LINE";
    case amiga_custom_log_filter_name: return "NAME";
    case amiga_custom_log_filter_src: return "SRC";
    case amiga_custom_log_filter_addr: return "ADDR";
    case amiga_custom_log_filter_value: return "VALUE";
    case amiga_custom_log_filter_desc: return "DESC";
    default:
        break;
    }
    return "";
}

static int
amiga_custom_log_filterColumnWidth(int index)
{
    switch (index) {
    case amiga_custom_log_filter_line:
        return AMIGA_CUSTOM_LOG_LINE_COL_W;
    case amiga_custom_log_filter_name:
        return AMIGA_CUSTOM_LOG_NAME_COL_W;
    case amiga_custom_log_filter_src:
        return AMIGA_CUSTOM_LOG_SRC_COL_W;
    case amiga_custom_log_filter_addr:
        return AMIGA_CUSTOM_LOG_ADDR_COL_W;
    case amiga_custom_log_filter_value:
        return AMIGA_CUSTOM_LOG_VALUE_COL_W;
    case amiga_custom_log_filter_desc:
    default:
        break;
    }
    return -1;
}

static void
amiga_custom_log_filterTextboxChanged(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    amiga_custom_log_filter_cb_t *cb = (amiga_custom_log_filter_cb_t *)user;
    if (!cb || !cb->ui) {
        return;
    }
    if (cb->filterIndex < 0 || cb->filterIndex >= AMIGA_CUSTOM_LOG_FILTER_COUNT) {
        return;
    }
    e9ui_component_t *textbox = cb->ui->filterTextboxes[cb->filterIndex];
    const char *text = e9ui_textbox_getText(textbox);
    if (!text) {
        text = "";
    }
    snprintf(cb->ui->filters[cb->filterIndex], AMIGA_CUSTOM_LOG_FILTER_TEXT_MAX, "%s", text);
}

static e9ui_component_t *
amiga_custom_log_buildFilterRoot(amiga_custom_log_state_t *ui)
{
    if (!ui) {
        return NULL;
    }
    e9ui_component_t *row = e9ui_hstack_make();
    if (!row) {
        return NULL;
    }
    for (int i = 0; i < AMIGA_CUSTOM_LOG_FILTER_COUNT; ++i) {
        ui->filterCbs[i].ui = ui;
        ui->filterCbs[i].filterIndex = i;
        e9ui_component_t *textbox = e9ui_textbox_make(AMIGA_CUSTOM_LOG_FILTER_TEXT_MAX - 1,
                                                      NULL,
                                                      amiga_custom_log_filterTextboxChanged,
                                                      &ui->filterCbs[i]);

        e9ui_textbox_setPlaceholder(textbox, amiga_custom_log_filterLabel(i));
        e9ui_textbox_setText(textbox, ui->filters[i]);
        ui->filterTextboxes[i] = textbox;
        int width = amiga_custom_log_filterColumnWidth(i);
        if (width > 0) {
            e9ui_hstack_addFixed(row, textbox, width);
        } else {
            e9ui_hstack_addFlex(row, textbox);
        }
        if (i + 1 < AMIGA_CUSTOM_LOG_FILTER_COUNT) {
            e9ui_component_t *gap = e9ui_spacer_make(AMIGA_CUSTOM_LOG_COL_GAP);
            e9ui_hstack_addFixed(row, gap, AMIGA_CUSTOM_LOG_COL_GAP);
        }
    }
    return row;
}

static int
amiga_custom_log_containsInsensitive(const char *text, const char *needle)
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
    for (size_t i = 0; i + needleLen <= textLen; ++i) {
        size_t j = 0;
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

static uint32_t
amiga_custom_log_normalizeAddress(uint32_t addr)
{
    return addr & 0x00ffffffu;
}

static int
amiga_custom_log_entryMatchesFilters(const amiga_custom_log_state_t *ui, const e9k_debug_ami_custom_log_entry_t *entry)
{
    if (!ui || !entry) {
        return 0;
    }

    uint16_t regOffset = (uint16_t)(entry->reg & 0x1feu);
    const char *name = amiga_custom_regs_nameForOffset(regOffset);
    const char *desc = amiga_custom_regs_descriptionForOffset(regOffset);
    const char *src = entry->sourceIsCopper ? "COP" : "CPU";
    if (!name) {
        name = "";
    }
    if (!desc) {
        desc = "";
    }
    char lineBuf[16];
    char addrBuf[16];
    char valueBuf[16];
    snprintf(lineBuf, sizeof(lineBuf), "%03u", (unsigned)entry->vpos);
    snprintf(addrBuf, sizeof(addrBuf), "%06x", (unsigned)amiga_custom_log_normalizeAddress(entry->sourceAddr));
    snprintf(valueBuf, sizeof(valueBuf), "%04x", (unsigned)(entry->value & 0xffffu));

    if (!amiga_custom_log_containsInsensitive(lineBuf, ui->filters[amiga_custom_log_filter_line])) {
        return 0;
    }
    if (!amiga_custom_log_containsInsensitive(name, ui->filters[amiga_custom_log_filter_name])) {
        return 0;
    }
    if (!amiga_custom_log_containsInsensitive(src, ui->filters[amiga_custom_log_filter_src])) {
        return 0;
    }
    if (!amiga_custom_log_containsInsensitive(addrBuf, ui->filters[amiga_custom_log_filter_addr])) {
        return 0;
    }
    if (!amiga_custom_log_containsInsensitive(valueBuf, ui->filters[amiga_custom_log_filter_value])) {
        return 0;
    }
    if (!amiga_custom_log_containsInsensitive(desc, ui->filters[amiga_custom_log_filter_desc])) {
        return 0;
    }
    return 1;
}

static int
amiga_custom_log_ensureRowHitCapacity(amiga_custom_log_state_t *ui, size_t needed)
{
    if (!ui) {
        return 0;
    }
    if (needed <= ui->rowHitCap) {
        return 1;
    }
    size_t nextCap = ui->rowHitCap ? ui->rowHitCap : 16u;
    while (nextCap < needed) {
        if (nextCap > (SIZE_MAX / 2u)) {
            nextCap = needed;
            break;
        }
        nextCap *= 2u;
    }
    amiga_custom_log_row_hit_t *next = (amiga_custom_log_row_hit_t *)realloc(ui->rowHits, nextCap * sizeof(*next));
    if (!next) {
        return 0;
    }
    ui->rowHits = next;
    ui->rowHitCap = nextCap;
    return 1;
}

static int
amiga_custom_log_findAddressHit(const amiga_custom_log_state_t *ui, int x, int y, amiga_custom_log_row_hit_t *outHit)
{
    if (!ui) {
        return 0;
    }
    for (size_t i = 0; i < ui->rowHitCount; ++i) {
        const amiga_custom_log_row_hit_t *hit = &ui->rowHits[i];
        if (x >= hit->addrRect.x && x < hit->addrRect.x + hit->addrRect.w &&
            y >= hit->addrRect.y && y < hit->addrRect.y + hit->addrRect.h) {
            if (outHit) {
                *outHit = *hit;
            }
            return 1;
        }
    }
    return 0;
}

static int
amiga_custom_log_findNameHit(const amiga_custom_log_state_t *ui, int x, int y, amiga_custom_log_row_hit_t *outHit)
{
    if (!ui) {
        return 0;
    }
    for (size_t i = 0; i < ui->rowHitCount; ++i) {
        const amiga_custom_log_row_hit_t *hit = &ui->rowHits[i];
        if (x >= hit->nameRect.x && x < hit->nameRect.x + hit->nameRect.w &&
            y >= hit->nameRect.y && y < hit->nameRect.y + hit->nameRect.h) {
            if (outHit) {
                *outHit = *hit;
            }
            return 1;
        }
    }
    return 0;
}

static int
amiga_custom_log_findValueHit(const amiga_custom_log_state_t *ui, int x, int y, amiga_custom_log_row_hit_t *outHit)
{
    if (!ui) {
        return 0;
    }
    for (size_t i = 0; i < ui->rowHitCount; ++i) {
        const amiga_custom_log_row_hit_t *hit = &ui->rowHits[i];
        if (x >= hit->valueRect.x && x < hit->valueRect.x + hit->valueRect.w &&
            y >= hit->valueRect.y && y < hit->valueRect.y + hit->valueRect.h) {
            if (outHit) {
                *outHit = *hit;
            }
            return 1;
        }
    }
    return 0;
}

static int
amiga_custom_log_isPaletteRegOffset(uint16_t regOffset)
{
    uint16_t normalized = (uint16_t)(regOffset & 0x01feu);
    return normalized >= 0x0180u && normalized <= 0x01beu;
}

static SDL_Color
amiga_custom_log_paletteSwatchColor(uint16_t value)
{
    uint8_t r = (uint8_t)(((value >> 8) & 0x0fu) * 17u);
    uint8_t g = (uint8_t)(((value >> 4) & 0x0fu) * 17u);
    uint8_t b = (uint8_t)((value & 0x0fu) * 17u);
    SDL_Color color = { r, g, b, 255 };
    return color;
}

static void
amiga_custom_log_addOrEnableBreakpoint(uint32_t addr)
{
    uint32_t bpAddr = addr & 0x00ffffffu;
    int changed = 0;
    machine_breakpoint_t *existing = machine_findBreakpointByAddr(&debugger.machine, bpAddr);
    if (existing) {
        if (existing->enabled) {
            if (machine_removeBreakpointByAddr(&debugger.machine, bpAddr)) {
                libretro_host_debugRemoveBreakpoint(bpAddr);
                changed = 1;
            }
        } else {
            uint32_t enabledAddr = 0;
            if (machine_setBreakpointEnabled(&debugger.machine, existing->number, 1, &enabledAddr)) {
                libretro_host_debugAddBreakpoint(enabledAddr);
                changed = 1;
            }
        }
    } else {
        machine_breakpoint_t *bp = machine_addBreakpoint(&debugger.machine, bpAddr, 1);
        if (!bp) {
            return;
        }
        breakpoints_resolveLocation(bp);
        libretro_host_debugAddBreakpoint((uint32_t)bp->addr);
        changed = 1;
    }

    if (changed) {
        breakpoints_markDirty();
        ui_refreshOnPause();
    }
}

static void
amiga_custom_log_drawTextClipped(amiga_custom_log_state_t *ui,
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
    SDL_RenderSetClipRect(ui->renderer, &clip);
    SDL_RenderCopy(ui->renderer, tex, NULL, &dst);
    if (hadClip) {
        SDL_RenderSetClipRect(ui->renderer, &prevClip);
    } else {
        SDL_RenderSetClipRect(ui->renderer, NULL);
    }
}

static void
amiga_custom_log_drawTooltip(amiga_custom_log_state_t *ui,
                       TTF_Font *font,
                       int mouseX,
                       int mouseY,
                       const char *text,
                       int showSwatch,
                       SDL_Color swatchColor)
{
    if (!ui || !ui->renderer || !font || !text || !text[0]) {
        return;
    }

    enum { AMIGA_CUSTOM_LOG_TOOLTIP_MAX_LINES = 64 };
    const char *lines[AMIGA_CUSTOM_LOG_TOOLTIP_MAX_LINES];
    int lineCount = 0;
    const char *cursor = text;
    while (*cursor && lineCount < AMIGA_CUSTOM_LOG_TOOLTIP_MAX_LINES) {
        lines[lineCount++] = cursor;
        const char *next = strchr(cursor, '\n');
        if (!next) {
            break;
        }
        cursor = next + 1;
    }
    if (lineCount <= 0) {
        return;
    }

    int lineHeight = amiga_custom_log_measureLineHeight();
    if (lineHeight <= 0) {
        lineHeight = 16;
    }

    int maxTextW = 0;
    int lineWidths[AMIGA_CUSTOM_LOG_TOOLTIP_MAX_LINES];
    char lineBuf[256];
    for (int i = 0; i < lineCount; ++i) {
        const char *lineStart = lines[i];
        const char *lineEnd = strchr(lineStart, '\n');
        size_t lineLen = lineEnd ? (size_t)(lineEnd - lineStart) : strlen(lineStart);
        if (lineLen >= sizeof(lineBuf)) {
            lineLen = sizeof(lineBuf) - 1u;
        }
        memcpy(lineBuf, lineStart, lineLen);
        lineBuf[lineLen] = '\0';

        int lineW = 0;
        int lineH = 0;
        if (!lineBuf[0]) {
            lineW = 0;
            lineH = lineHeight;
        } else {
            TTF_SizeUTF8(font, lineBuf, &lineW, &lineH);
        }
        lineWidths[i] = lineW;
        if (lineW > maxTextW) {
            maxTextW = lineW;
        }
    }

    const int padX = 8;
    const int padY = 6;
    const int swatchSize = lineHeight - 4;
    const int swatchGap = 6;
    if (showSwatch && lineCount > 1 && swatchSize > 0) {
        int swatchLineWidth = lineWidths[1] + swatchGap + swatchSize;
        if (swatchLineWidth > maxTextW) {
            maxTextW = swatchLineWidth;
        }
    }
    int tooltipW = maxTextW + padX * 2;
    int tooltipH = lineCount * lineHeight + padY * 2;

    int winW = 0;
    int winH = 0;
    SDL_GetRendererOutputSize(ui->renderer, &winW, &winH);
    if (winW <= 0 || winH <= 0) {
        return;
    }

    int tooltipX = mouseX + 14;
    int tooltipY = mouseY + 18;
    if (tooltipX + tooltipW > winW - 4) {
        tooltipX = winW - tooltipW - 4;
    }
    if (tooltipY + tooltipH > winH - 4) {
        tooltipY = winH - tooltipH - 4;
    }
    if (tooltipX < 4) {
        tooltipX = 4;
    }
    if (tooltipY < 4) {
        tooltipY = 4;
    }

    SDL_Rect bg = { tooltipX, tooltipY, tooltipW, tooltipH };
    SDL_SetRenderDrawColor(ui->renderer, 20, 20, 24, 242);
    SDL_RenderFillRect(ui->renderer, &bg);
    SDL_SetRenderDrawColor(ui->renderer, 92, 92, 102, 255);
    SDL_RenderDrawRect(ui->renderer, &bg);

    SDL_Color textColor = { 232, 232, 236, 255 };
    for (int i = 0; i < lineCount; ++i) {
        const char *lineStart = lines[i];
        const char *lineEnd = strchr(lineStart, '\n');
        size_t lineLen = lineEnd ? (size_t)(lineEnd - lineStart) : strlen(lineStart);
        if (lineLen >= sizeof(lineBuf)) {
            lineLen = sizeof(lineBuf) - 1u;
        }
        memcpy(lineBuf, lineStart, lineLen);
        lineBuf[lineLen] = '\0';
        amiga_custom_log_drawTextClipped(ui,
                                   font,
                                   tooltipX + padX,
                                   tooltipY + padY + i * lineHeight,
                                   tooltipW - padX * 2,
                                   lineBuf,
                                   textColor);
    }

    if (showSwatch && lineCount > 1 && swatchSize > 0) {
        int swatchX = tooltipX + padX + lineWidths[1] + swatchGap;
        int swatchY = tooltipY + padY + lineHeight + (lineHeight - swatchSize) / 2;
        SDL_Rect swatchRect = { swatchX, swatchY, swatchSize, swatchSize };
        SDL_SetRenderDrawColor(ui->renderer, swatchColor.r, swatchColor.g, swatchColor.b, 255);
        SDL_RenderFillRect(ui->renderer, &swatchRect);
        SDL_SetRenderDrawColor(ui->renderer, 220, 220, 220, 255);
        SDL_RenderDrawRect(ui->renderer, &swatchRect);
    }
}

static void
amiga_custom_log_applyEnabledOption(int enabled)
{
    amiga_custom_log_state_t *ui = &amiga_custom_log_state;
    if (libretro_host_debugSetDebugOption(E9K_DEBUG_OPTION_AMIGA_CUSTOM_LOGGER,
                                          enabled ? 1u : 0u,
                                          NULL)) {
        ui->warnedMissingOption = 0;
        return;
    }
    if (!ui->warnedMissingOption) {
        debug_error("custom log: core does not expose debug option API");
        ui->warnedMissingOption = 1;
    }
}

int
amiga_custom_log_init(void)
{
    amiga_custom_log_state_t *ui = &amiga_custom_log_state;
    if (ui->windowState.open) {
        return 1;
    }

    ui->windowState.windowHost = e9ui_windowCreate(amiga_custom_log_windowBackend());
    if (!ui->windowState.windowHost) {
        return 0;
    }
    memset(&ui->ctx, 0, sizeof(ui->ctx));
    ui->ctx.font = e9ui->ctx.font;
    ui->warnedMissingOption = 0;
    ui->scrollRow = 0;
    ui->filterRoot = NULL;
    memset(ui->filters, 0, sizeof(ui->filters));
    for (int i = 0; i < AMIGA_CUSTOM_LOG_FILTER_COUNT; ++i) {
        ui->filterTextboxes[i] = NULL;
    }
    ui->filterRoot = amiga_custom_log_buildFilterRoot(ui);
    if (!ui->filterRoot) {
        e9ui_windowDestroy(ui->windowState.windowHost);
        ui->windowState.windowHost = NULL;
        ui->renderer = NULL;
        ui->window = NULL;
        memset(&ui->ctx, 0, sizeof(ui->ctx));
        return 0;
    }
    {
        e9ui_rect_t rect = e9ui_windowResolveStateOpenRect(&e9ui->ctx,
                                                           amiga_custom_log_windowDefaultRect(&e9ui->ctx),
                                                           &ui->windowState);
        e9ui_component_t *overlayBodyHost = amiga_custom_log_makeOverlayBodyHost(ui);
        e9ui_windowOpen(ui->windowState.windowHost,
                                     "CHIPSET LOG",
                                     rect,
                                     overlayBodyHost,
                                     amiga_custom_log_overlayWindowCloseRequested,
                                     ui,
			             &e9ui->ctx);        
        ui->window = e9ui->ctx.window;
        ui->renderer = e9ui->ctx.renderer;
        ui->ctx = e9ui->ctx;
    }

    ui->windowState.open = 1;
    aux_window_register(&amiga_custom_log_auxWindowOps, ui);
    amiga_custom_log_applyEnabledOption(1);
    return 1;
}

void
amiga_custom_log_shutdown(void)
{
    amiga_custom_log_state_t *ui = &amiga_custom_log_state;
    if (!ui->windowState.open) {
        return;
    }

    aux_window_unregister(&amiga_custom_log_auxWindowOps, ui);
    (void)e9ui_windowCaptureStateRectSnapshot(&ui->windowState, &e9ui->ctx);
    config_saveConfig();
    amiga_custom_log_applyEnabledOption(0);
    if (ui->filterRoot) {
        ui->filterRoot = NULL;
    }
    for (int i = 0; i < AMIGA_CUSTOM_LOG_FILTER_COUNT; ++i) {
        ui->filterTextboxes[i] = NULL;
    }
    e9ui_text_cache_clearRenderer(ui->renderer);
    if (ui->windowState.windowHost) {
        e9ui_windowDestroy(ui->windowState.windowHost);
        ui->windowState.windowHost = NULL;
    }
    ui->renderer = NULL;
    ui->window = NULL;
    if (ui->entries) {
        free(ui->entries);
        ui->entries = NULL;
    }
    if (ui->rowHits) {
        free(ui->rowHits);
        ui->rowHits = NULL;
    }

    ui->windowState.open = 0;
    ui->warnedMissingOption = 0;
    ui->scrollRow = 0;
    ui->entryCount = 0;
    ui->entryCap = 0;
    ui->rowHitCount = 0;
    ui->rowHitCap = 0;
    ui->dropped = 0;
    ui->frameNo = 0;
    ui->framesCaptured = 0;
    ui->allocFailures = 0;
    memset(ui->filters, 0, sizeof(ui->filters));
    memset(&ui->ctx, 0, sizeof(ui->ctx));
}

void
amiga_custom_log_toggle(void)
{
    if (amiga_custom_log_isOpen()) {
        amiga_custom_log_shutdown();
        return;
    }
    (void)amiga_custom_log_init();
}

int
amiga_custom_log_isOpen(void)
{
    return amiga_custom_log_state.windowState.open ? 1 : 0;
}

void
amiga_custom_log_setMainWindowFocused(int focused)
{
    (void)focused;
}

static int
amiga_custom_log_overlayBodyPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)ctx;
    (void)availW;
    return 0;
}

static void
amiga_custom_log_overlayBodyLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static int
amiga_custom_log_overlayTranslateEventLocal(e9ui_component_t *self, const e9ui_event_t *ev, e9ui_event_t *out)
{
    if (!self || !ev || !out) {
        return 0;
    }
    *out = *ev;
    if (ev->type == SDL_MOUSEMOTION) {
        out->motion.x -= self->bounds.x;
        out->motion.y -= self->bounds.y;
        return 1;
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP) {
        out->button.x -= self->bounds.x;
        out->button.y -= self->bounds.y;
        return 1;
    }
    if (ev->type == SDL_MOUSEWHEEL) {
        out->wheel.mouseX -= self->bounds.x;
        out->wheel.mouseY -= self->bounds.y;
        return 1;
    }
    if (ev->type == SDL_KEYDOWN || ev->type == SDL_KEYUP) {
        return 1;
    }
    return 0;
}

static void
amiga_custom_log_renderFrame(amiga_custom_log_state_t *ui, int presentFrame)
{
    if (!ui || !ui->renderer) {
        return;
    }
    TTF_Font *font = e9ui->ctx.font;
    if (!font) {
        if (presentFrame) {
            SDL_RenderPresent(ui->renderer);
        }
        return;
    }

    amiga_custom_log_layout_t layout;
    if (!amiga_custom_log_computeLayout(ui, &layout)) {
        if (presentFrame) {
            SDL_RenderPresent(ui->renderer);
        }
        return;
    }

    SDL_Rect bgRect = { 0, 0, layout.winW, layout.winH };
    SDL_SetRenderDrawColor(ui->renderer, 12, 12, 12, 255);
    SDL_RenderFillRect(ui->renderer, &bgRect);

    int winW = layout.winW;
    int lineHeight = layout.lineHeight;
    int visibleRows = amiga_custom_log_computeVisibleRows(ui);
    amiga_custom_log_clampScroll(ui, visibleRows);
    amiga_custom_log_scroll_model_t scrollModel;
    amiga_custom_log_buildScrollModel(ui, visibleRows, &scrollModel);
    ui->rowHitCount = 0;
    if (visibleRows > 0) {
        (void)amiga_custom_log_ensureRowHitCapacity(ui, (size_t)visibleRows);
    }
    ui->ctx.font = font;
    ui->ctx.focusRoot = ui->filterRoot;
    ui->ctx.focusFullscreen = NULL;
    if (ui->filterRoot && ui->filterRoot->layout) {
        int filterX = layout.colX[amiga_custom_log_filter_line];
        int filterW = layout.colX[amiga_custom_log_filter_desc] + layout.colW[amiga_custom_log_filter_desc] - filterX;
        if (filterW < 0) {
            filterW = 0;
        }
        e9ui_rect_t filterBounds = {
            filterX,
            AMIGA_CUSTOM_LOG_PAD,
            filterW,
            layout.filterHeight
        };
        ui->filterRoot->layout(ui->filterRoot, &ui->ctx, filterBounds);
    }
    if (ui->filterRoot && ui->filterRoot->render) {
        ui->filterRoot->render(ui->filterRoot, &ui->ctx);
    }

    SDL_Rect rowsClip = {
        AMIGA_CUSTOM_LOG_PAD,
        layout.rowsY,
        winW - AMIGA_CUSTOM_LOG_PAD * 2,
        layout.rowsH
    };
    if (rowsClip.w > 0 && rowsClip.h > 0) {
        SDL_RenderSetClipRect(ui->renderer, &rowsClip);
    }

    SDL_Color rowColor = { 220, 220, 224, 255 };
    int drawnRows = 0;
    size_t scanIndex = (size_t)ui->scrollRow;
    while (scanIndex < ui->entryCount && drawnRows < visibleRows) {
        const e9k_debug_ami_custom_log_entry_t *entry = &ui->entries[scanIndex];
        scanIndex++;
        if (!amiga_custom_log_entryMatchesFilters(ui, entry)) {
            continue;
        }

        int y = layout.rowsY + drawnRows * lineHeight;
        if ((drawnRows & 1) != 0) {
            SDL_SetRenderDrawColor(ui->renderer, 16, 16, 20, 255);
            SDL_Rect rowBg = { AMIGA_CUSTOM_LOG_PAD, y, winW - AMIGA_CUSTOM_LOG_PAD * 2, lineHeight };
            SDL_RenderFillRect(ui->renderer, &rowBg);
        }
        uint16_t regOffset = (uint16_t)(entry->reg & 0x1feu);
        const char *name = amiga_custom_regs_nameForOffset(regOffset);
        const char *desc = amiga_custom_regs_descriptionForOffset(regOffset);
        uint32_t regColorRgb = amiga_custom_regs_colorForOffset(regOffset);
        SDL_Color regColor = {
            (uint8_t)((regColorRgb >> 16) & 0xffu),
            (uint8_t)((regColorRgb >> 8) & 0xffu),
            (uint8_t)(regColorRgb & 0xffu),
            255
        };
        SDL_Color srcColor = entry->sourceIsCopper
            ? (SDL_Color){ 110, 212, 118, 255 }
            : (SDL_Color){ 222, 92, 92, 255 };
        uint32_t normalizedAddr = amiga_custom_log_normalizeAddress(entry->sourceAddr);
        machine_breakpoint_t *bp = machine_findBreakpointByAddr(&debugger.machine, normalizedAddr);
        SDL_Color addrColor = (bp && bp->enabled)
            ? (SDL_Color){ 110, 212, 118, 255 }
            : rowColor;
        const char *src = entry->sourceIsCopper ? "COP" : "CPU";
        if (!name) {
            name = "";
        }
        if (!desc) {
            desc = "";
        }

        char lineBuf[16];
        char addrBuf[16];
        char valueBuf[16];
        snprintf(lineBuf, sizeof(lineBuf), "%03u", (unsigned)entry->vpos);
        snprintf(addrBuf, sizeof(addrBuf), "%06x", (unsigned)normalizedAddr);
        snprintf(valueBuf, sizeof(valueBuf), "%04x", (unsigned)(entry->value & 0xffffu));

        amiga_custom_log_drawTextClipped(ui, font,
                                   layout.colX[amiga_custom_log_filter_line], y, layout.colW[amiga_custom_log_filter_line],
                                   lineBuf, rowColor);
        amiga_custom_log_drawTextClipped(ui, font,
                                   layout.colX[amiga_custom_log_filter_name], y, layout.colW[amiga_custom_log_filter_name],
                                   name, regColor);
        amiga_custom_log_drawTextClipped(ui, font,
                                   layout.colX[amiga_custom_log_filter_src], y, layout.colW[amiga_custom_log_filter_src],
                                   src, srcColor);
        amiga_custom_log_drawTextClipped(ui, font,
                                   layout.colX[amiga_custom_log_filter_addr], y, layout.colW[amiga_custom_log_filter_addr],
                                   addrBuf, addrColor);
        amiga_custom_log_drawTextClipped(ui, font,
                                   layout.colX[amiga_custom_log_filter_value], y, layout.colW[amiga_custom_log_filter_value],
                                   valueBuf, rowColor);
        amiga_custom_log_drawTextClipped(ui, font,
                                   layout.colX[amiga_custom_log_filter_desc], y, layout.colW[amiga_custom_log_filter_desc],
                                   desc, regColor);

        if (ui->rowHitCount < ui->rowHitCap) {
            amiga_custom_log_row_hit_t *hit = &ui->rowHits[ui->rowHitCount++];
            hit->nameRect.x = layout.colX[amiga_custom_log_filter_name];
            hit->nameRect.y = y;
            hit->nameRect.w = layout.colW[amiga_custom_log_filter_name];
            hit->nameRect.h = lineHeight;
            hit->addrRect.x = layout.colX[amiga_custom_log_filter_addr];
            hit->addrRect.y = y;
            hit->addrRect.w = layout.colW[amiga_custom_log_filter_addr];
            hit->addrRect.h = lineHeight;
            hit->valueRect.x = layout.colX[amiga_custom_log_filter_value];
            hit->valueRect.y = y;
            hit->valueRect.w = layout.colW[amiga_custom_log_filter_value];
            hit->valueRect.h = lineHeight;
            hit->sourceAddr = normalizedAddr;
            hit->regOffset = regOffset;
            hit->regValue = (uint16_t)(entry->value & 0xffffu);
            hit->sourceIsCopper = entry->sourceIsCopper ? 1u : 0u;
        }
        drawnRows++;
    }
    SDL_RenderSetClipRect(ui->renderer, NULL);

    {
        e9ui_rect_t scrollBounds = {
            rowsClip.x,
            rowsClip.y,
            rowsClip.w,
            rowsClip.h
        };
        e9ui_scrollbar_render(ui,
                              &ui->ctx,
                              scrollBounds,
                              1,
                              scrollModel.visibleRows,
                              1,
                              scrollModel.filteredCount,
                              0,
                              scrollModel.topFilteredRow);
    }

    amiga_custom_log_row_hit_t hoverRow;
    if (amiga_custom_log_findValueHit(ui, ui->ctx.mouseX, ui->ctx.mouseY, &hoverRow)) {
        const char *tooltip = amiga_custom_regs_valueTooltipForOffset(hoverRow.regOffset, hoverRow.regValue);
        int showSwatch = amiga_custom_log_isPaletteRegOffset(hoverRow.regOffset);
        SDL_Color swatchColor = amiga_custom_log_paletteSwatchColor(hoverRow.regValue);
        amiga_custom_log_drawTooltip(ui,
                               font,
                               ui->ctx.mouseX,
                               ui->ctx.mouseY,
                               tooltip,
                               showSwatch,
                               swatchColor);
    } else if (amiga_custom_log_findNameHit(ui, ui->ctx.mouseX, ui->ctx.mouseY, &hoverRow)) {
        char tooltip[64];
        uint32_t regAddr = amiga_custom_regs_addressFromOffset(hoverRow.regOffset) & 0x00ffffffu;
        snprintf(tooltip, sizeof(tooltip), "Address: 0x%06X", (unsigned)regAddr);
        amiga_custom_log_drawTooltip(ui,
                               font,
                               ui->ctx.mouseX,
                               ui->ctx.mouseY,
                               tooltip,
                               0,
                               (SDL_Color){ 0, 0, 0, 255 });
    }

    if (presentFrame) {
        SDL_RenderPresent(ui->renderer);
    }
}

static int
amiga_custom_log_overlayBodyHandleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!self || !ctx || !ev || !self->state) {
        return 0;
    }
    amiga_custom_log_overlay_body_state_t *st = (amiga_custom_log_overlay_body_state_t *)self->state;
    amiga_custom_log_state_t *ui = st ? st->ui : NULL;
    if (!ui || !ui->windowState.open) {
        return 0;
    }
    e9ui_event_t localEv;
    if (!amiga_custom_log_overlayTranslateEventLocal(self, ev, &localEv)) {
        return 0;
    }
    ui->ctx = *ctx;
    ui->ctx.window = ctx->window;
    ui->ctx.renderer = ctx->renderer;
    ui->ctx.winW = self->bounds.w;
    ui->ctx.winH = self->bounds.h;
    ui->ctx.mouseX = ctx->mouseX - self->bounds.x;
    ui->ctx.mouseY = ctx->mouseY - self->bounds.y;
    ui->ctx.mousePrevX = ctx->mousePrevX - self->bounds.x;
    ui->ctx.mousePrevY = ctx->mousePrevY - self->bounds.y;

    if (localEv.type == SDL_KEYDOWN) {
        SDL_Keymod mods = localEv.key.keysym.mod;
        int accel = (mods & KMOD_GUI) || (mods & KMOD_CTRL);
        if (!accel && localEv.key.keysym.sym == SDLK_TAB) {
            e9ui_component_t *focus = e9ui_getFocus(ctx);
            int focusInFilters = (focus && amiga_custom_log_componentContains(ui->filterRoot, focus)) ? 1 : 0;
            if (focusInFilters || !focus) {
                int reverse = (mods & KMOD_SHIFT) ? 1 : 0;
                e9ui_component_t *next = e9ui_focusFindNext(ui->filterRoot, focusInFilters ? focus : NULL, reverse);
                if (next) {
                    e9ui_setFocus(ctx, next);
                    return 1;
                }
            }
        }
    }

    if (ui->filterRoot) {
        e9ui_component_t *prevFocusRoot = ctx->focusRoot;
        e9ui_component_t *prevFocusFullscreen = ctx->focusFullscreen;
        ctx->focusRoot = ui->filterRoot;
        ctx->focusFullscreen = NULL;
        if (e9ui_event_process(ui->filterRoot, ctx, &localEv)) {
            ctx->focusRoot = prevFocusRoot;
            ctx->focusFullscreen = prevFocusFullscreen;
            return 1;
        }
        ctx->focusRoot = prevFocusRoot;
        ctx->focusFullscreen = prevFocusFullscreen;
        if (localEv.type == SDL_MOUSEBUTTONDOWN &&
            localEv.button.button == SDL_BUTTON_LEFT &&
            !ctx->focusClickHandled) {
            e9ui_setFocus(ctx, NULL);
        }
    }
    if (localEv.type == SDL_MOUSEMOTION ||
        (localEv.type == SDL_MOUSEBUTTONDOWN && localEv.button.button == SDL_BUTTON_LEFT) ||
        (localEv.type == SDL_MOUSEBUTTONUP && localEv.button.button == SDL_BUTTON_LEFT)) {
        amiga_custom_log_layout_t layout;
        if (amiga_custom_log_computeLayout(ui, &layout)) {
            int visibleRows = amiga_custom_log_computeVisibleRows(ui);
            amiga_custom_log_clampScroll(ui, visibleRows);
            amiga_custom_log_scroll_model_t scrollModel;
            amiga_custom_log_buildScrollModel(ui, visibleRows, &scrollModel);
            e9ui_rect_t scrollBounds = {
                AMIGA_CUSTOM_LOG_PAD,
                layout.rowsY,
                layout.winW - AMIGA_CUSTOM_LOG_PAD * 2,
                layout.rowsH
            };
            int scrollX = 0;
            int scrollY = scrollModel.topFilteredRow;
            if (e9ui_scrollbar_handleEvent(ui,
                                           ctx,
                                           &localEv,
                                           scrollBounds,
                                           1,
                                           scrollModel.visibleRows,
                                           1,
                                           scrollModel.filteredCount,
                                           &scrollX,
                                           &scrollY,
                                           &ui->scrollbar)) {
                amiga_custom_log_setTopFilteredRow(ui, visibleRows, scrollY);
                return 1;
            }
        }
    }
    if (localEv.type == SDL_MOUSEWHEEL) {
        int wheelY = localEv.wheel.y;
        if (wheelY != 0) {
            amiga_custom_log_adjustScroll(ui, wheelY);
        }
        return 1;
    }
    if (localEv.type == SDL_MOUSEBUTTONDOWN && localEv.button.button == SDL_BUTTON_LEFT) {
        amiga_custom_log_row_hit_t hitRow;
        if (amiga_custom_log_findAddressHit(ui, localEv.button.x, localEv.button.y, &hitRow)) {
            uint32_t sourceAddr = amiga_custom_log_normalizeAddress(hitRow.sourceAddr);
            if (hitRow.sourceIsCopper) {
                ui_centerCprSourceOnAddress(sourceAddr);
            } else {
                amiga_custom_log_addOrEnableBreakpoint(sourceAddr);
            }
            return 1;
        }
    }
    if (localEv.type == SDL_KEYDOWN) {
        SDL_Keycode key = localEv.key.keysym.sym;
        SDL_Keymod mods = localEv.key.keysym.mod;
        int visibleRows = amiga_custom_log_computeVisibleRows(ui);
        int step = amiga_custom_log_arrowScrollAmount(mods, visibleRows);
        switch (key) {
        case SDLK_UP:
            amiga_custom_log_adjustScroll(ui, -step);
            return 1;
        case SDLK_DOWN:
            amiga_custom_log_adjustScroll(ui, step);
            return 1;
        case SDLK_PAGEUP:
            amiga_custom_log_adjustScroll(ui, -visibleRows);
            return 1;
        case SDLK_PAGEDOWN:
            amiga_custom_log_adjustScroll(ui, visibleRows);
            return 1;
        case SDLK_HOME:
            ui->scrollRow = 0;
            amiga_custom_log_clampScroll(ui, visibleRows);
            return 1;
        case SDLK_END:
            ui->scrollRow = INT_MAX;
            amiga_custom_log_clampScroll(ui, visibleRows);
            return 1;
        default:
            break;
        }
    }
    return 0;
}

static void
amiga_custom_log_overlayBodyRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !self->state || !ctx->renderer) {
        return;
    }
    amiga_custom_log_overlay_body_state_t *st = (amiga_custom_log_overlay_body_state_t *)self->state;
    amiga_custom_log_state_t *ui = st ? st->ui : NULL;
    if (!ui || !ui->windowState.open) {
        return;
    }
    SDL_Rect prevViewport;
    int hadClip = SDL_RenderIsClipEnabled(ctx->renderer) ? 1 : 0;
    SDL_Rect prevClip = { 0, 0, 0, 0 };
    if (hadClip) {
        SDL_RenderGetClipRect(ctx->renderer, &prevClip);
    }
    SDL_RenderGetViewport(ctx->renderer, &prevViewport);
    SDL_Rect viewport = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_RenderSetViewport(ctx->renderer, &viewport);
    if (hadClip) {
        SDL_Rect localClip = {
            prevClip.x - self->bounds.x,
            prevClip.y - self->bounds.y,
            prevClip.w,
            prevClip.h
        };
        SDL_Rect viewportLocal = { 0, 0, self->bounds.w, self->bounds.h };
        SDL_Rect clipped;
        if (SDL_IntersectRect(&localClip, &viewportLocal, &clipped)) {
            SDL_RenderSetClipRect(ctx->renderer, &clipped);
        } else {
            SDL_Rect empty = { 0, 0, 0, 0 };
            SDL_RenderSetClipRect(ctx->renderer, &empty);
        }
    }

    ui->ctx = *ctx;
    ui->ctx.window = ctx->window;
    ui->ctx.renderer = ctx->renderer;
    ui->ctx.winW = self->bounds.w;
    ui->ctx.winH = self->bounds.h;
    ui->ctx.mouseX = ctx->mouseX - self->bounds.x;
    ui->ctx.mouseY = ctx->mouseY - self->bounds.y;
    ui->ctx.mousePrevX = ctx->mousePrevX - self->bounds.x;
    ui->ctx.mousePrevY = ctx->mousePrevY - self->bounds.y;
    amiga_custom_log_renderFrame(ui, 0);

    SDL_RenderSetViewport(ctx->renderer, &prevViewport);
    if (hadClip) {
        SDL_RenderSetClipRect(ctx->renderer, &prevClip);
    }
}

static e9ui_component_t *
amiga_custom_log_makeOverlayBodyHost(amiga_custom_log_state_t *ui)
{
    if (!ui) {
        return NULL;
    }
    e9ui_component_t *host = (e9ui_component_t *)alloc_calloc(1, sizeof(*host));
    if (!host) {
        return NULL;
    }
    amiga_custom_log_overlay_body_state_t *st = (amiga_custom_log_overlay_body_state_t *)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(host);
        return NULL;
    }
    st->ui = ui;
    host->name = "amiga_custom_log_overlay_body";
    host->state = st;
    host->preferredHeight = amiga_custom_log_overlayBodyPreferredHeight;
    host->layout = amiga_custom_log_overlayBodyLayout;
    host->render = amiga_custom_log_overlayBodyRender;
    host->handleEvent = amiga_custom_log_overlayBodyHandleEvent;
    if (ui->filterRoot) {
        e9ui_child_add(host, ui->filterRoot, alloc_strdup("amiga_custom_log_filter_root"));
    }
    return host;
}

static void
amiga_custom_log_overlayWindowCloseRequested(e9ui_window_t *window, void *user)
{
    (void)window;
    amiga_custom_log_state_t *ui = (amiga_custom_log_state_t *)user;
    if (!ui) {
        return;
    }
    amiga_custom_log_shutdown();
}

void
amiga_custom_log_render(void)
{
    amiga_custom_log_state_t *ui = &amiga_custom_log_state;
    if (!ui->windowState.open) {
        return;
    }
    if (e9ui_windowCaptureStateRectChanged(&ui->windowState, &e9ui->ctx)) {
        config_saveConfig();
    }
}

void
amiga_custom_log_persistConfig(FILE *file)
{
    amiga_custom_log_state_t *ui = &amiga_custom_log_state;
    if (!file) {
        return;
    }
    e9ui_windowPersistStateRect(file, "comp.custom_log", &ui->windowState, &e9ui->ctx);
}

int
amiga_custom_log_loadConfigProperty(const char *prop, const char *value)
{
    amiga_custom_log_state_t *ui = &amiga_custom_log_state;
    if (!prop || !value) {
        return 0;
    }
    int intValue = 0;
    if (strcmp(prop, "win_x") == 0) {
        if (!amiga_custom_log_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winX = intValue;
    } else if (strcmp(prop, "win_y") == 0) {
        if (!amiga_custom_log_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winY = intValue;
    } else if (strcmp(prop, "win_w") == 0) {
        if (!amiga_custom_log_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winW = intValue;
    } else if (strcmp(prop, "win_h") == 0) {
        if (!amiga_custom_log_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winH = intValue;
    } else {
        return 0;
    }
    ui->windowState.winHasSaved =
        e9ui_windowHasSavedPosition(ui->windowState.winX, ui->windowState.winY);
    return 1;
}

void
amiga_custom_log_captureFrame(const e9k_debug_ami_custom_log_entry_t *entries,
                        size_t count,
                        uint32_t dropped,
                        uint64_t frameNo)
{
    amiga_custom_log_state_t *ui = &amiga_custom_log_state;
    ui->frameNo = frameNo;
    ui->dropped = dropped;
    ui->framesCaptured++;

    if (!entries || count == 0) {
        ui->entryCount = 0;
        return;
    }

    if (count > ui->entryCap) {
        e9k_debug_ami_custom_log_entry_t *next =
            (e9k_debug_ami_custom_log_entry_t *)realloc(ui->entries, count * sizeof(*next));
        if (!next) {
            ui->allocFailures++;
            ui->entryCount = 0;
            return;
        }
        ui->entries = next;
        ui->entryCap = count;
    }

    memcpy(ui->entries, entries, count * sizeof(*entries));
    ui->entryCount = count;
}
