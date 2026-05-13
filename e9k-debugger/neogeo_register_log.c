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
#include "neogeo_register_log.h"
#include "debug.h"
#include "debugger.h"
#include "e9ui.h"
#include "e9ui_scroll.h"
#include "libretro_host.h"
#include "neogeo_register_log_regs.h"
#include "ui.h"

#define NEOGEO_REGISTER_LOG_PAD 10
#define NEOGEO_REGISTER_LOG_ROW_PAD_Y 2
#define NEOGEO_REGISTER_LOG_COL_GAP 12
#define NEOGEO_REGISTER_LOG_LINE_COL_W 112
#define NEOGEO_REGISTER_LOG_NAME_COL_W 128
#define NEOGEO_REGISTER_LOG_SRC_COL_W 75
#define NEOGEO_REGISTER_LOG_ADDR_COL_W 120
#define NEOGEO_REGISTER_LOG_VALUE_COL_W 109
#define NEOGEO_REGISTER_LOG_FILTER_COUNT 6
#define NEOGEO_REGISTER_LOG_FILTER_TEXT_MAX 128
#define NEOGEO_REGISTER_LOG_FILTER_VPAD 3
#define NEOGEO_REGISTER_LOG_FILTER_GAP_Y 6

typedef enum neogeo_register_log_filter_index {
    neogeo_register_log_filter_line = 0,
    neogeo_register_log_filter_name = 1,
    neogeo_register_log_filter_src = 2,
    neogeo_register_log_filter_addr = 3,
    neogeo_register_log_filter_value = 4,
    neogeo_register_log_filter_desc = 5
} neogeo_register_log_filter_index_t;

typedef struct neogeo_register_log_state neogeo_register_log_state_t;

typedef struct neogeo_register_log_filter_cb {
    neogeo_register_log_state_t *ui;
    int filterIndex;
} neogeo_register_log_filter_cb_t;

typedef struct neogeo_register_log_overlay_body_state {
    neogeo_register_log_state_t *ui;
} neogeo_register_log_overlay_body_state_t;

typedef struct neogeo_register_log_canvas_state {
    neogeo_register_log_state_t *ui;
} neogeo_register_log_canvas_state_t;

typedef struct neogeo_register_log_layout {
    int lineHeight;
    int colX[NEOGEO_REGISTER_LOG_FILTER_COUNT];
    int colW[NEOGEO_REGISTER_LOG_FILTER_COUNT];
} neogeo_register_log_layout_t;

typedef struct neogeo_register_log_row_hit {
    SDL_Rect nameRect;
    SDL_Rect addrRect;
    SDL_Rect valueRect;
    uint32_t sourceAddr;
    uint32_t reg;
    uint16_t regValue;
    uint8_t sourceKind;
} neogeo_register_log_row_hit_t;

typedef struct neogeo_register_log_scroll_model {
    int visibleRows;
    int filteredCount;
    int topFilteredRow;
    int topRawRow;
} neogeo_register_log_scroll_model_t;

struct neogeo_register_log_state {
    e9ui_window_state_t windowState;
    int callbackBound;
    int preserveLog;
    SDL_Window *window;
    SDL_Renderer *renderer;
    e9ui_context_t ctx;
    e9ui_component_t *root;
    e9ui_component_t *filterRoot;
    e9ui_component_t *listScroll;
    e9ui_component_t *listCanvas;
    e9ui_component_t *preserveLogCheckbox;
    e9ui_component_t *filterTextboxes[NEOGEO_REGISTER_LOG_FILTER_COUNT];
    neogeo_register_log_filter_cb_t filterCbs[NEOGEO_REGISTER_LOG_FILTER_COUNT];
    e9k_debug_geo_register_log_entry_t *entries;
    size_t entryCount;
    size_t entryCap;
    uint32_t dropped;
    uint64_t frameNo;
    uint64_t framesCaptured;
    uint64_t allocFailures;
    char filters[NEOGEO_REGISTER_LOG_FILTER_COUNT][NEOGEO_REGISTER_LOG_FILTER_TEXT_MAX];
    neogeo_register_log_row_hit_t *rowHits;
    size_t rowHitCount;
    size_t rowHitCap;
};

static neogeo_register_log_state_t neogeo_register_log_state = {
    .windowState.winX = E9UI_WINDOW_COORD_UNSET,
    .windowState.winY = E9UI_WINDOW_COORD_UNSET,
    .windowState.openMinWidthPx = 420,
    .windowState.openMinHeightPx = 260,
    .windowState.openCenterWhenNoSaved = 1,
};

static const aux_window_ops_t neogeo_register_log_auxWindowOps = {
    .setFocus = neogeo_register_log_setMainWindowFocused,
    .render = neogeo_register_log_render,
};

static e9ui_window_backend_t
neogeo_register_log_windowBackend(void)
{
    return e9ui_window_backend_overlay;
}

static e9ui_component_t *
neogeo_register_log_makeOverlayBodyHost(neogeo_register_log_state_t *ui);

static e9ui_component_t *
neogeo_register_log_buildRoot(neogeo_register_log_state_t *ui);

static void
neogeo_register_log_overlayWindowCloseRequested(e9ui_window_t *window, void *user);

static void
neogeo_register_log_overlayBodyDtor(e9ui_component_t *self, e9ui_context_t *ctx);

static int
neogeo_register_log_entryMatchesFilters(const neogeo_register_log_state_t *ui, const e9k_debug_geo_register_log_entry_t *entry);

static void
neogeo_register_log_onFrame(const e9k_debug_geo_register_log_entry_t *entries,
                            size_t count,
                            uint32_t dropped,
                            uint64_t frameNo,
                            void *user);

static void
neogeo_register_log_clearEntries(neogeo_register_log_state_t *ui);

static uint32_t
neogeo_register_log_normalizeAddress(uint32_t addr);

static int
neogeo_register_log_ensureRowHitCapacity(neogeo_register_log_state_t *ui, size_t needed);

static int
neogeo_register_log_findAddressHit(const neogeo_register_log_state_t *ui, int x, int y, neogeo_register_log_row_hit_t *outHit);

static int
neogeo_register_log_findNameHit(const neogeo_register_log_state_t *ui, int x, int y, neogeo_register_log_row_hit_t *outHit);

static int
neogeo_register_log_findValueHit(const neogeo_register_log_state_t *ui, int x, int y, neogeo_register_log_row_hit_t *outHit);

static void
neogeo_register_log_addOrEnableBreakpoint(uint32_t addr);

static void
neogeo_register_log_drawTextClipped(neogeo_register_log_state_t *ui,
                                    TTF_Font *font,
                                    int x,
                                    int y,
                                    int width,
                                    const char *text,
                                    SDL_Color color);

static void
neogeo_register_log_drawTooltip(neogeo_register_log_state_t *ui,
                                TTF_Font *font,
                                int mouseX,
                                int mouseY,
                                const char *text,
                                int showSwatch,
                                SDL_Color swatchColor);

static int
neogeo_register_log_parseInt(const char *value, int *out)
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
neogeo_register_log_measureLineHeight(void)
{
    TTF_Font *font = e9ui->ctx.font;
    int lineHeight = font ? TTF_FontHeight(font) : 0;
    if (lineHeight <= 0) {
        lineHeight = 16;
    }
    return lineHeight;
}

static e9ui_rect_t
neogeo_register_log_windowDefaultRect(const e9ui_context_t *ctx)
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
neogeo_register_log_computeLayout(const neogeo_register_log_state_t *ui, int originX, int width, neogeo_register_log_layout_t *out)
{
    if (!ui || !out) {
        return 0;
    }
    if (width <= 0) {
        return 0;
    }
    int lineHeight = neogeo_register_log_measureLineHeight();
    int colLineW = NEOGEO_REGISTER_LOG_LINE_COL_W;
    int colNameW = NEOGEO_REGISTER_LOG_NAME_COL_W;
    int colSrcW = NEOGEO_REGISTER_LOG_SRC_COL_W;
    int colAddrW = NEOGEO_REGISTER_LOG_ADDR_COL_W;
    int colValueW = NEOGEO_REGISTER_LOG_VALUE_COL_W;
    int colDescW = width - colLineW - colNameW - colSrcW - colAddrW - colValueW - NEOGEO_REGISTER_LOG_COL_GAP * 5;
    if (colDescW < 48) {
        colDescW = 48;
    }

    out->lineHeight = lineHeight;
    out->colX[neogeo_register_log_filter_line] = originX;
    out->colW[neogeo_register_log_filter_line] = colLineW;
    out->colX[neogeo_register_log_filter_name] = out->colX[neogeo_register_log_filter_line] + colLineW + NEOGEO_REGISTER_LOG_COL_GAP;
    out->colW[neogeo_register_log_filter_name] = colNameW;
    out->colX[neogeo_register_log_filter_src] = out->colX[neogeo_register_log_filter_name] + colNameW + NEOGEO_REGISTER_LOG_COL_GAP;
    out->colW[neogeo_register_log_filter_src] = colSrcW;
    out->colX[neogeo_register_log_filter_addr] = out->colX[neogeo_register_log_filter_src] + colSrcW + NEOGEO_REGISTER_LOG_COL_GAP;
    out->colW[neogeo_register_log_filter_addr] = colAddrW;
    out->colX[neogeo_register_log_filter_value] = out->colX[neogeo_register_log_filter_addr] + colAddrW + NEOGEO_REGISTER_LOG_COL_GAP;
    out->colW[neogeo_register_log_filter_value] = colValueW;
    out->colX[neogeo_register_log_filter_desc] = out->colX[neogeo_register_log_filter_value] + colValueW + NEOGEO_REGISTER_LOG_COL_GAP;
    out->colW[neogeo_register_log_filter_desc] = colDescW;
    return 1;
}

static int
neogeo_register_log_hasActiveFilters(const neogeo_register_log_state_t *ui)
{
    if (!ui) {
        return 0;
    }
    for (int i = 0; i < NEOGEO_REGISTER_LOG_FILTER_COUNT; ++i) {
        if (ui->filters[i][0] != '\0') {
            return 1;
        }
    }
    return 0;
}

static int
neogeo_register_log_filteredCount(const neogeo_register_log_state_t *ui)
{
    if (!ui || ui->entryCount == 0) {
        return 0;
    }
    if (!neogeo_register_log_hasActiveFilters(ui)) {
        if (ui->entryCount > (size_t)INT_MAX) {
            return INT_MAX;
        }
        return (int)ui->entryCount;
    }
    int count = 0;
    for (size_t i = 0; i < ui->entryCount; ++i) {
        if (neogeo_register_log_entryMatchesFilters(ui, &ui->entries[i])) {
            if (count < INT_MAX) {
                count++;
            }
        }
    }
    return count;
}

static const char *
neogeo_register_log_filterLabel(int index)
{
    switch (index) {
    case neogeo_register_log_filter_line: return "LINE";
    case neogeo_register_log_filter_name: return "NAME";
    case neogeo_register_log_filter_src: return "SRC";
    case neogeo_register_log_filter_addr: return "ADDR";
    case neogeo_register_log_filter_value: return "VALUE";
    case neogeo_register_log_filter_desc: return "DESC";
    default:
        break;
    }
    return "";
}

static int
neogeo_register_log_filterColumnWidth(int index)
{
    switch (index) {
    case neogeo_register_log_filter_line:
        return NEOGEO_REGISTER_LOG_LINE_COL_W;
    case neogeo_register_log_filter_name:
        return NEOGEO_REGISTER_LOG_NAME_COL_W;
    case neogeo_register_log_filter_src:
        return NEOGEO_REGISTER_LOG_SRC_COL_W;
    case neogeo_register_log_filter_addr:
        return NEOGEO_REGISTER_LOG_ADDR_COL_W;
    case neogeo_register_log_filter_value:
        return NEOGEO_REGISTER_LOG_VALUE_COL_W;
    case neogeo_register_log_filter_desc:
    default:
        break;
    }
    return -1;
}

static void
neogeo_register_log_filterTextboxChanged(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    neogeo_register_log_filter_cb_t *cb = (neogeo_register_log_filter_cb_t *)user;
    if (!cb || !cb->ui) {
        return;
    }
    if (cb->filterIndex < 0 || cb->filterIndex >= NEOGEO_REGISTER_LOG_FILTER_COUNT) {
        return;
    }
    e9ui_component_t *textbox = cb->ui->filterTextboxes[cb->filterIndex];
    const char *text = e9ui_textbox_getText(textbox);
    if (!text) {
        text = "";
    }
    snprintf(cb->ui->filters[cb->filterIndex], NEOGEO_REGISTER_LOG_FILTER_TEXT_MAX, "%s", text);
}

static void
neogeo_register_log_preserveLogChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    neogeo_register_log_state_t *ui = (neogeo_register_log_state_t *)user;
    if (!ui) {
        return;
    }
    ui->preserveLog = selected ? 1 : 0;
    config_saveConfig();
}

static void
neogeo_register_log_clearClicked(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    neogeo_register_log_state_t *ui = (neogeo_register_log_state_t *)user;
    if (!ui) {
        return;
    }
    neogeo_register_log_clearEntries(ui);
}

static e9ui_component_t *
neogeo_register_log_buildFilterRoot(neogeo_register_log_state_t *ui)
{
    if (!ui) {
        return NULL;
    }
    e9ui_component_t *root = e9ui_stack_makeVertical();
    e9ui_component_t *controlsRow = e9ui_hstack_make();
    e9ui_component_t *filterRow = e9ui_hstack_make();
    e9ui_component_t *preserveLogCheckbox = e9ui_checkbox_make("Preserve log",
                                                               ui->preserveLog,
                                                               neogeo_register_log_preserveLogChanged,
                                                               ui);
    e9ui_component_t *clearButton = e9ui_button_make("Clear", neogeo_register_log_clearClicked, ui);
    e9ui_button_setMini(clearButton, 1);
    ui->preserveLogCheckbox = preserveLogCheckbox;
    int preserveLogCheckboxW = 0;
    int clearButtonW = 0;
    e9ui_checkbox_measure(preserveLogCheckbox, &ui->ctx, &preserveLogCheckboxW, NULL);
    e9ui_button_measure(clearButton, &ui->ctx, &clearButtonW, NULL);
    e9ui_hstack_addFixed(controlsRow, preserveLogCheckbox, preserveLogCheckboxW);
    e9ui_hstack_addFixed(controlsRow, e9ui_spacer_make(NEOGEO_REGISTER_LOG_COL_GAP), NEOGEO_REGISTER_LOG_COL_GAP);
    e9ui_hstack_addFixed(controlsRow, clearButton, clearButtonW);
    e9ui_hstack_addFlex(controlsRow, e9ui_spacer_make(1));
    e9ui_stack_addFixed(root, controlsRow);
    e9ui_stack_addFixed(root, e9ui_vspacer_make(NEOGEO_REGISTER_LOG_FILTER_GAP_Y));
    for (int i = 0; i < NEOGEO_REGISTER_LOG_FILTER_COUNT; ++i) {
        ui->filterCbs[i].ui = ui;
        ui->filterCbs[i].filterIndex = i;
        e9ui_component_t *textbox = e9ui_textbox_make(NEOGEO_REGISTER_LOG_FILTER_TEXT_MAX - 1,
                                                      NULL,
                                                      neogeo_register_log_filterTextboxChanged,
                                                      &ui->filterCbs[i]);

        e9ui_textbox_setPlaceholder(textbox, neogeo_register_log_filterLabel(i));
        e9ui_textbox_setText(textbox, ui->filters[i]);
        ui->filterTextboxes[i] = textbox;
        int width = neogeo_register_log_filterColumnWidth(i);
        if (width > 0) {
            e9ui_hstack_addFixed(filterRow, textbox, width);
        } else {
            e9ui_hstack_addFlex(filterRow, textbox);
        }
        if (i + 1 < NEOGEO_REGISTER_LOG_FILTER_COUNT) {
            e9ui_component_t *gap = e9ui_spacer_make(NEOGEO_REGISTER_LOG_COL_GAP);
            e9ui_hstack_addFixed(filterRow, gap, NEOGEO_REGISTER_LOG_COL_GAP);
        }
    }
    e9ui_stack_addFixed(root, filterRow);
    return root;
}

static e9ui_component_t *
neogeo_register_log_wrapRowWithSidePadding(e9ui_component_t *child, int leftPad, int rightPad)
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

static int
neogeo_register_log_canvasPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)availW;
    neogeo_register_log_state_t *ui = &neogeo_register_log_state;
    int lineHeight = 16;
    if (ctx && ctx->font) {
        lineHeight = TTF_FontHeight(ctx->font);
        if (lineHeight <= 0) {
            lineHeight = 16;
        }
    }
    int filteredCount = neogeo_register_log_filteredCount(ui);
    if (filteredCount <= 0) {
        return lineHeight;
    }
    return filteredCount * lineHeight;
}

static void
neogeo_register_log_canvasLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static int
neogeo_register_log_canvasHandleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    (void)self;
    (void)ctx;
    if (!ev) {
        return 0;
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_LEFT) {
        neogeo_register_log_row_hit_t hitRow;
        if (neogeo_register_log_findAddressHit(&neogeo_register_log_state, ev->button.x, ev->button.y, &hitRow)) {
            uint32_t sourceAddr = neogeo_register_log_normalizeAddress(hitRow.sourceAddr);
            if (neogeo_register_log_regs_sourceCanBreakpoint(hitRow.sourceKind)) {
                neogeo_register_log_addOrEnableBreakpoint(sourceAddr);
            }
            return 1;
        }
    }
    return 0;
}

static void
neogeo_register_log_canvasRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) {
        return;
    }
    neogeo_register_log_canvas_state_t *st = (neogeo_register_log_canvas_state_t *)self->state;
    neogeo_register_log_state_t *ui = st ? st->ui : NULL;
    if (!ui || !ui->windowState.open) {
        return;
    }

    ui->ctx = *ctx;
    ui->ctx.window = ctx->window;
    ui->ctx.renderer = ctx->renderer;
    ui->ctx.font = e9ui->ctx.font;
    ui->renderer = ctx->renderer;
    ui->window = ctx->window;

    TTF_Font *font = e9ui->ctx.font;
    if (!font) {
        return;
    }

    neogeo_register_log_layout_t layout;
    if (!neogeo_register_log_computeLayout(ui, self->bounds.x + 6, self->bounds.w - 12, &layout)) {
        return;
    }

    SDL_SetRenderDrawColor(ctx->renderer, 12, 12, 12, 255);
    SDL_Rect bgRect = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_RenderFillRect(ctx->renderer, &bgRect);

    int scrollY = 0;
    if (ui->listScroll) {
        e9ui_scroll_getScrollPx(ui->listScroll, NULL, &scrollY);
    }
    int topFilteredRow = 0;
    if (layout.lineHeight > 0 && scrollY > 0) {
        topFilteredRow = scrollY / layout.lineHeight;
    }
    int visibleRows = self->bounds.h / layout.lineHeight + 2;
    if (visibleRows < 1) {
        visibleRows = 1;
    }

    ui->rowHitCount = 0;
    (void)neogeo_register_log_ensureRowHitCapacity(ui, (size_t)visibleRows);

    SDL_Color rowColor = { 220, 220, 224, 255 };
    int filteredIndex = 0;
    int drawnRows = 0;
    for (size_t entryIndex = 0; entryIndex < ui->entryCount; ++entryIndex) {
        const e9k_debug_geo_register_log_entry_t *entry = &ui->entries[entryIndex];
        if (!neogeo_register_log_entryMatchesFilters(ui, entry)) {
            continue;
        }
        if (filteredIndex < topFilteredRow) {
            filteredIndex++;
            continue;
        }

        int y = self->bounds.y + filteredIndex * layout.lineHeight;
        if (y >= self->bounds.y + self->bounds.h) {
            break;
        }
        if (y + layout.lineHeight <= self->bounds.y) {
            filteredIndex++;
            continue;
        }

        if ((filteredIndex & 1) != 0) {
            SDL_SetRenderDrawColor(ctx->renderer, 16, 16, 20, 255);
            SDL_Rect rowBg = { self->bounds.x, y, self->bounds.w, layout.lineHeight };
            SDL_RenderFillRect(ctx->renderer, &rowBg);
        }

        const char *name = neogeo_register_log_regs_nameForReg(entry->reg, entry->sourceKind);
        const char *desc = neogeo_register_log_regs_descriptionForReg(entry->reg, entry->sourceKind);
        uint32_t regColorRgb = neogeo_register_log_regs_colorForReg(entry->reg, entry->sourceKind);
        SDL_Color regColor = {
            (uint8_t)((regColorRgb >> 16) & 0xffu),
            (uint8_t)((regColorRgb >> 8) & 0xffu),
            (uint8_t)(regColorRgb & 0xffu),
            255
        };
        SDL_Color srcColor = entry->sourceKind == E9K_DEBUG_GEO_REGISTER_LOG_SOURCE_Z80
            ? (SDL_Color){ 120, 211, 140, 255 }
            : (SDL_Color){ 108, 190, 255, 255 };
        uint32_t normalizedAddr = neogeo_register_log_normalizeAddress(entry->sourceAddr);
        machine_breakpoint_t *bp = neogeo_register_log_regs_sourceCanBreakpoint(entry->sourceKind)
            ? machine_findBreakpointByAddr(&debugger.machine, normalizedAddr)
            : NULL;
        SDL_Color addrColor = (bp && bp->enabled)
            ? (SDL_Color){ 110, 212, 118, 255 }
            : rowColor;
        const char *src = neogeo_register_log_regs_sourceLabel(entry->sourceKind);
        if (!name) {
            name = "";
        }
        if (!desc) {
            desc = "";
        }

        char lineBuf[16];
        char addrBuf[16];
        char valueBuf[16];
        snprintf(lineBuf, sizeof(lineBuf), "%03u", (unsigned)entry->line);
        snprintf(addrBuf, sizeof(addrBuf), "%06x", (unsigned)normalizedAddr);
        snprintf(valueBuf, sizeof(valueBuf), "%04x", (unsigned)(entry->value & 0xffffu));

        neogeo_register_log_drawTextClipped(ui, font, layout.colX[neogeo_register_log_filter_line], y, layout.colW[neogeo_register_log_filter_line], lineBuf, rowColor);
        neogeo_register_log_drawTextClipped(ui, font, layout.colX[neogeo_register_log_filter_name], y, layout.colW[neogeo_register_log_filter_name], name, regColor);
        neogeo_register_log_drawTextClipped(ui, font, layout.colX[neogeo_register_log_filter_src], y, layout.colW[neogeo_register_log_filter_src], src, srcColor);
        neogeo_register_log_drawTextClipped(ui, font, layout.colX[neogeo_register_log_filter_addr], y, layout.colW[neogeo_register_log_filter_addr], addrBuf, addrColor);
        neogeo_register_log_drawTextClipped(ui, font, layout.colX[neogeo_register_log_filter_value], y, layout.colW[neogeo_register_log_filter_value], valueBuf, rowColor);
        neogeo_register_log_drawTextClipped(ui, font, layout.colX[neogeo_register_log_filter_desc], y, layout.colW[neogeo_register_log_filter_desc], desc, regColor);

        if (ui->rowHitCount < ui->rowHitCap) {
            neogeo_register_log_row_hit_t *hit = &ui->rowHits[ui->rowHitCount++];
            hit->nameRect.x = layout.colX[neogeo_register_log_filter_name];
            hit->nameRect.y = y;
            hit->nameRect.w = layout.colW[neogeo_register_log_filter_name];
            hit->nameRect.h = layout.lineHeight;
            hit->addrRect.x = layout.colX[neogeo_register_log_filter_addr];
            hit->addrRect.y = y;
            hit->addrRect.w = layout.colW[neogeo_register_log_filter_addr];
            hit->addrRect.h = layout.lineHeight;
            hit->valueRect.x = layout.colX[neogeo_register_log_filter_value];
            hit->valueRect.y = y;
            hit->valueRect.w = layout.colW[neogeo_register_log_filter_value];
            hit->valueRect.h = layout.lineHeight;
            hit->sourceAddr = normalizedAddr;
            hit->reg = entry->reg;
            hit->regValue = (uint16_t)(entry->value & 0xffffu);
            hit->sourceKind = entry->sourceKind;
        }

        filteredIndex++;
        drawnRows++;
        if (drawnRows >= visibleRows) {
            break;
        }
    }

    neogeo_register_log_row_hit_t hoverRow;
    if (neogeo_register_log_findValueHit(ui, ctx->mouseX, ctx->mouseY, &hoverRow)) {
        const char *tooltip = neogeo_register_log_regs_valueTooltipForReg(hoverRow.reg, hoverRow.sourceKind, hoverRow.regValue);
        neogeo_register_log_drawTooltip(ui, font, ctx->mouseX, ctx->mouseY, tooltip, 0, (SDL_Color){ 0, 0, 0, 255 });
    } else if (neogeo_register_log_findNameHit(ui, ctx->mouseX, ctx->mouseY, &hoverRow)) {
        char tooltip[64];
        neogeo_register_log_regs_formatRegAddress(hoverRow.reg, hoverRow.sourceKind, tooltip, sizeof(tooltip));
        neogeo_register_log_drawTooltip(ui, font, ctx->mouseX, ctx->mouseY, tooltip, 0, (SDL_Color){ 0, 0, 0, 255 });
    }
}

static void
neogeo_register_log_canvasDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self || !self->state) {
        return;
    }
    alloc_free(self->state);
    self->state = NULL;
}

static e9ui_component_t *
neogeo_register_log_makeCanvas(neogeo_register_log_state_t *ui)
{
    if (!ui) {
        return NULL;
    }
    e9ui_component_t *canvas = (e9ui_component_t *)alloc_calloc(1, sizeof(*canvas));
    neogeo_register_log_canvas_state_t *st =
        (neogeo_register_log_canvas_state_t *)alloc_calloc(1, sizeof(*st));
    if (!canvas || !st) {
        alloc_free(canvas);
        alloc_free(st);
        return NULL;
    }
    st->ui = ui;
    canvas->name = "neogeo_register_log_canvas";
    canvas->state = st;
    canvas->preferredHeight = neogeo_register_log_canvasPreferredHeight;
    canvas->layout = neogeo_register_log_canvasLayout;
    canvas->render = neogeo_register_log_canvasRender;
    canvas->handleEvent = neogeo_register_log_canvasHandleEvent;
    canvas->dtor = neogeo_register_log_canvasDtor;
    return canvas;
}

static e9ui_component_t *
neogeo_register_log_buildRoot(neogeo_register_log_state_t *ui)
{
    if (!ui) {
        return NULL;
    }

    e9ui_component_t *root = e9ui_stack_makeVertical();
    e9ui_component_t *filterRoot = neogeo_register_log_buildFilterRoot(ui);
    e9ui_component_t *canvas = neogeo_register_log_makeCanvas(ui);
    e9ui_component_t *listScroll = e9ui_scroll_make(canvas);
    if (!canvas) {
        return NULL;
    }

    ui->filterRoot = filterRoot;
    ui->listCanvas = canvas;
    ui->listScroll = listScroll;

    const int filterPad = NEOGEO_REGISTER_LOG_PAD + 6;
    e9ui_stack_addFixed(root, e9ui_vspacer_make(NEOGEO_REGISTER_LOG_PAD));
    e9ui_stack_addFixed(root, neogeo_register_log_wrapRowWithSidePadding(filterRoot, filterPad, filterPad));
    e9ui_stack_addFixed(root, e9ui_vspacer_make(NEOGEO_REGISTER_LOG_FILTER_GAP_Y));
    e9ui_stack_addFlex(root, neogeo_register_log_wrapRowWithSidePadding(listScroll, NEOGEO_REGISTER_LOG_PAD, NEOGEO_REGISTER_LOG_PAD));
    e9ui_stack_addFixed(root, e9ui_vspacer_make(NEOGEO_REGISTER_LOG_PAD));
    return root;
}

static int
neogeo_register_log_containsInsensitive(const char *text, const char *needle)
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
neogeo_register_log_normalizeAddress(uint32_t addr)
{
    return addr & 0x00ffffffu;
}

static int
neogeo_register_log_entryMatchesFilters(const neogeo_register_log_state_t *ui, const e9k_debug_geo_register_log_entry_t *entry)
{
    if (!ui || !entry) {
        return 0;
    }

    const char *name = neogeo_register_log_regs_nameForReg(entry->reg, entry->sourceKind);
    const char *desc = neogeo_register_log_regs_descriptionForReg(entry->reg, entry->sourceKind);
    const char *src = neogeo_register_log_regs_sourceLabel(entry->sourceKind);
    if (!name) {
        name = "";
    }
    if (!desc) {
        desc = "";
    }
    char lineBuf[16];
    char addrBuf[16];
    char valueBuf[16];
    snprintf(lineBuf, sizeof(lineBuf), "%03u", (unsigned)entry->line);
    snprintf(addrBuf, sizeof(addrBuf), "%06x", (unsigned)neogeo_register_log_normalizeAddress(entry->sourceAddr));
    snprintf(valueBuf, sizeof(valueBuf), "%04x", (unsigned)(entry->value & 0xffffu));

    if (!neogeo_register_log_containsInsensitive(lineBuf, ui->filters[neogeo_register_log_filter_line])) {
        return 0;
    }
    if (!neogeo_register_log_containsInsensitive(name, ui->filters[neogeo_register_log_filter_name])) {
        return 0;
    }
    if (!neogeo_register_log_containsInsensitive(src, ui->filters[neogeo_register_log_filter_src])) {
        return 0;
    }
    if (!neogeo_register_log_containsInsensitive(addrBuf, ui->filters[neogeo_register_log_filter_addr])) {
        return 0;
    }
    if (!neogeo_register_log_containsInsensitive(valueBuf, ui->filters[neogeo_register_log_filter_value])) {
        return 0;
    }
    if (!neogeo_register_log_containsInsensitive(desc, ui->filters[neogeo_register_log_filter_desc])) {
        return 0;
    }
    return 1;
}

static int
neogeo_register_log_ensureRowHitCapacity(neogeo_register_log_state_t *ui, size_t needed)
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
    neogeo_register_log_row_hit_t *next = (neogeo_register_log_row_hit_t *)realloc(ui->rowHits, nextCap * sizeof(*next));
    if (!next) {
        return 0;
    }
    ui->rowHits = next;
    ui->rowHitCap = nextCap;
    return 1;
}

static int
neogeo_register_log_findAddressHit(const neogeo_register_log_state_t *ui, int x, int y, neogeo_register_log_row_hit_t *outHit)
{
    if (!ui) {
        return 0;
    }
    for (size_t i = 0; i < ui->rowHitCount; ++i) {
        const neogeo_register_log_row_hit_t *hit = &ui->rowHits[i];
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
neogeo_register_log_findNameHit(const neogeo_register_log_state_t *ui, int x, int y, neogeo_register_log_row_hit_t *outHit)
{
    if (!ui) {
        return 0;
    }
    for (size_t i = 0; i < ui->rowHitCount; ++i) {
        const neogeo_register_log_row_hit_t *hit = &ui->rowHits[i];
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
neogeo_register_log_findValueHit(const neogeo_register_log_state_t *ui, int x, int y, neogeo_register_log_row_hit_t *outHit)
{
    if (!ui) {
        return 0;
    }
    for (size_t i = 0; i < ui->rowHitCount; ++i) {
        const neogeo_register_log_row_hit_t *hit = &ui->rowHits[i];
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

static void
neogeo_register_log_addOrEnableBreakpoint(uint32_t addr)
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
neogeo_register_log_drawTextClipped(neogeo_register_log_state_t *ui,
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

static void
neogeo_register_log_drawTooltip(neogeo_register_log_state_t *ui,
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

    enum { NEOGEO_REGISTER_LOG_TOOLTIP_MAX_LINES = 64 };
    const char *lines[NEOGEO_REGISTER_LOG_TOOLTIP_MAX_LINES];
    int lineCount = 0;
    const char *cursor = text;
    while (*cursor && lineCount < NEOGEO_REGISTER_LOG_TOOLTIP_MAX_LINES) {
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

    int lineHeight = neogeo_register_log_measureLineHeight();
    if (lineHeight <= 0) {
        lineHeight = 16;
    }

    int maxTextW = 0;
    int lineWidths[NEOGEO_REGISTER_LOG_TOOLTIP_MAX_LINES];
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
        neogeo_register_log_drawTextClipped(ui,
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
neogeo_register_log_onFrame(const e9k_debug_geo_register_log_entry_t *entries,
                            size_t count,
                            uint32_t dropped,
                            uint64_t frameNo,
                            void *user)
{
    (void)user;
    neogeo_register_log_captureFrame(entries, count, dropped, frameNo);
}

static void
neogeo_register_log_clearEntries(neogeo_register_log_state_t *ui)
{
    if (!ui) {
        return;
    }
    ui->entryCount = 0;
    if (ui->listScroll) {
        e9ui_scroll_setScrollPx(ui->listScroll, 0, 0);
    }
}

int
neogeo_register_log_init(void)
{
    neogeo_register_log_state_t *ui = &neogeo_register_log_state;
    if (ui->windowState.open) {
        return 1;
    }

    ui->windowState.windowHost = e9ui_windowCreate(neogeo_register_log_windowBackend());
    if (!ui->windowState.windowHost) {
        return 0;
    }
    ui->ctx = e9ui->ctx;
    ui->root = NULL;
    ui->filterRoot = NULL;
    ui->listScroll = NULL;
    ui->listCanvas = NULL;
    ui->preserveLogCheckbox = NULL;
    memset(ui->filters, 0, sizeof(ui->filters));
    for (int i = 0; i < NEOGEO_REGISTER_LOG_FILTER_COUNT; ++i) {
        ui->filterTextboxes[i] = NULL;
    }
    ui->root = neogeo_register_log_buildRoot(ui);
    if (!ui->root) {
        e9ui_windowDestroy(ui->windowState.windowHost);
        ui->windowState.windowHost = NULL;
        ui->renderer = NULL;
        ui->window = NULL;
        memset(&ui->ctx, 0, sizeof(ui->ctx));
        return 0;
    }
    {
        e9ui_rect_t rect = e9ui_windowResolveStateOpenRect(&e9ui->ctx,
                                                           neogeo_register_log_windowDefaultRect(&e9ui->ctx),
                                                           &ui->windowState);
        e9ui_component_t *overlayBodyHost = neogeo_register_log_makeOverlayBodyHost(ui);
        if (!overlayBodyHost) {
            e9ui_windowDestroy(ui->windowState.windowHost);
            ui->windowState.windowHost = NULL;
            ui->root = NULL;
            ui->filterRoot = NULL;
            ui->listScroll = NULL;
            ui->listCanvas = NULL;
            ui->renderer = NULL;
            ui->window = NULL;
            memset(&ui->ctx, 0, sizeof(ui->ctx));
            return 0;
        }
        e9ui_windowOpen(ui->windowState.windowHost,
                                     "REGISTER LOG",
                                     rect,
                                     overlayBodyHost,
                                     neogeo_register_log_overlayWindowCloseRequested,
                                     ui,
			             &e9ui->ctx);        
        ui->window = e9ui->ctx.window;
        ui->renderer = e9ui->ctx.renderer;
        ui->ctx = e9ui->ctx;
    }

    ui->windowState.open = 1;
    aux_window_register(&neogeo_register_log_auxWindowOps, ui);
    ui->callbackBound = libretro_host_neogeo_setRegisterLogFrameCallback(neogeo_register_log_onFrame, NULL) ? 1 : 0;
    if (!ui->callbackBound) {
        debug_error("register log: core does not expose Neo Geo register log callback");
    }
    return 1;
}

void
neogeo_register_log_shutdown(void)
{
    neogeo_register_log_state_t *ui = &neogeo_register_log_state;
    if (!ui->windowState.open) {
        return;
    }

    aux_window_unregister(&neogeo_register_log_auxWindowOps, ui);
    (void)e9ui_windowCaptureStateRectSnapshot(&ui->windowState, &e9ui->ctx);
    config_saveConfig();
    if (ui->callbackBound) {
        (void)libretro_host_neogeo_setRegisterLogFrameCallback(NULL, NULL);
        ui->callbackBound = 0;
    }
    ui->root = NULL;
    ui->filterRoot = NULL;
    ui->listScroll = NULL;
    ui->listCanvas = NULL;
    ui->preserveLogCheckbox = NULL;
    for (int i = 0; i < NEOGEO_REGISTER_LOG_FILTER_COUNT; ++i) {
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
    ui->callbackBound = 0;
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
neogeo_register_log_toggle(void)
{
    if (neogeo_register_log_isOpen()) {
        neogeo_register_log_shutdown();
        return;
    }
    (void)neogeo_register_log_init();
}

int
neogeo_register_log_isOpen(void)
{
    return neogeo_register_log_state.windowState.open ? 1 : 0;
}

void
neogeo_register_log_setMainWindowFocused(int focused)
{
    (void)focused;
}

static int
neogeo_register_log_overlayBodyPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)ctx;
    (void)availW;
    return 0;
}

static void
neogeo_register_log_overlayBodyLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self || !ctx || !self->state) {
        return;
    }
    neogeo_register_log_overlay_body_state_t *st = (neogeo_register_log_overlay_body_state_t *)self->state;
    neogeo_register_log_state_t *ui = st ? st->ui : NULL;
    if (!ui || !ui->root) {
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

    if (ui->root && ui->root->layout) {
        ui->root->layout(ui->root, &ui->ctx, bounds);
    }
}

static void
neogeo_register_log_overlayBodyRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !self->state || !ctx->renderer) {
        return;
    }
    neogeo_register_log_overlay_body_state_t *st = (neogeo_register_log_overlay_body_state_t *)self->state;
    neogeo_register_log_state_t *ui = st ? st->ui : NULL;
    if (!ui || !ui->windowState.open) {
        return;
    }
    int hadClip = SDL_RenderIsClipEnabled(ctx->renderer) ? 1 : 0;
    SDL_Rect prevClip = { 0, 0, 0, 0 };
    if (hadClip) {
        SDL_RenderGetClipRect(ctx->renderer, &prevClip);
        SDL_Rect clipped;
        if (SDL_IntersectRect(&prevClip, (const SDL_Rect *)&self->bounds, &clipped)) {
            SDL_RenderSetClipRect(ctx->renderer, &clipped);
        } else {
            SDL_Rect empty = { 0, 0, 0, 0 };
            SDL_RenderSetClipRect(ctx->renderer, &empty);
        }
    } else {
        SDL_Rect clip = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
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

    if (ui->root && ui->root->render) {
        ui->root->render(ui->root, &ui->ctx);
    }
    if (hadClip) {
        SDL_RenderSetClipRect(ctx->renderer, &prevClip);
    } else {
        SDL_RenderSetClipRect(ctx->renderer, NULL);
    }
}

static void
neogeo_register_log_overlayBodyDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self || !self->state) {
        return;
    }
    alloc_free(self->state);
    self->state = NULL;
}

static e9ui_component_t *
neogeo_register_log_makeOverlayBodyHost(neogeo_register_log_state_t *ui)
{
    if (!ui || !ui->root) {
        return NULL;
    }
    e9ui_component_t *host = (e9ui_component_t *)alloc_calloc(1, sizeof(*host));
    if (!host) {
        return NULL;
    }
    neogeo_register_log_overlay_body_state_t *st = (neogeo_register_log_overlay_body_state_t *)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(host);
        return NULL;
    }
    st->ui = ui;
    host->name = "neogeo_register_log_overlay_body";
    host->state = st;
    host->preferredHeight = neogeo_register_log_overlayBodyPreferredHeight;
    host->layout = neogeo_register_log_overlayBodyLayout;
    host->render = neogeo_register_log_overlayBodyRender;
    host->dtor = neogeo_register_log_overlayBodyDtor;
    e9ui_child_add(host, ui->root, alloc_strdup("neogeo_register_log_root"));
    return host;
}

static void
neogeo_register_log_overlayWindowCloseRequested(e9ui_window_t *window, void *user)
{
    (void)window;
    neogeo_register_log_state_t *ui = (neogeo_register_log_state_t *)user;
    if (!ui) {
        return;
    }
    neogeo_register_log_shutdown();
}

void
neogeo_register_log_render(void)
{
    neogeo_register_log_state_t *ui = &neogeo_register_log_state;
    if (!ui->windowState.open) {
        return;
    }
    if (!ui->callbackBound) {
        ui->callbackBound = libretro_host_neogeo_setRegisterLogFrameCallback(neogeo_register_log_onFrame, NULL) ? 1 : 0;
    }
    if (e9ui_windowCaptureStateRectChanged(&ui->windowState, &e9ui->ctx)) {
        config_saveConfig();
    }
}

void
neogeo_register_log_persistConfig(FILE *file)
{
    neogeo_register_log_state_t *ui = &neogeo_register_log_state;
    if (!file) {
        return;
    }
    e9ui_windowPersistStateRect(file, "comp.register_log", &ui->windowState, &e9ui->ctx);
    fprintf(file, "comp.register_log.preserve_log=%d\n", ui->preserveLog ? 1 : 0);
}

int
neogeo_register_log_loadConfigProperty(const char *prop, const char *value)
{
    neogeo_register_log_state_t *ui = &neogeo_register_log_state;
    if (!prop || !value) {
        return 0;
    }
    int intValue = 0;
    if (strcmp(prop, "win_x") == 0) {
        if (!neogeo_register_log_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winX = intValue;
    } else if (strcmp(prop, "win_y") == 0) {
        if (!neogeo_register_log_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winY = intValue;
    } else if (strcmp(prop, "win_w") == 0) {
        if (!neogeo_register_log_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winW = intValue;
    } else if (strcmp(prop, "win_h") == 0) {
        if (!neogeo_register_log_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winH = intValue;
    } else if (strcmp(prop, "preserve_log") == 0) {
        if (!neogeo_register_log_parseInt(value, &intValue)) {
            return 0;
        }
        ui->preserveLog = intValue ? 1 : 0;
    } else {
        return 0;
    }
    ui->windowState.winHasSaved =
        e9ui_windowHasSavedPosition(ui->windowState.winX, ui->windowState.winY);
    return 1;
}

void
neogeo_register_log_captureFrame(const e9k_debug_geo_register_log_entry_t *entries,
                        size_t count,
                        uint32_t dropped,
                        uint64_t frameNo)
{
    neogeo_register_log_state_t *ui = &neogeo_register_log_state;
    if (ui->preserveLogCheckbox) {
        ui->preserveLog = e9ui_checkbox_isSelected(ui->preserveLogCheckbox) ? 1 : 0;
    }
    ui->frameNo = frameNo;
    ui->dropped = dropped;
    ui->framesCaptured++;

    if (!entries || count == 0) {
        if (!ui->preserveLog) {
            neogeo_register_log_clearEntries(ui);
        }
        return;
    }

    if (!ui->preserveLog) {
        if (count > ui->entryCap) {
            e9k_debug_geo_register_log_entry_t *next =
                (e9k_debug_geo_register_log_entry_t *)realloc(ui->entries, count * sizeof(*next));
            if (!next) {
                ui->allocFailures++;
                neogeo_register_log_clearEntries(ui);
                return;
            }
            ui->entries = next;
            ui->entryCap = count;
        }
        memcpy(ui->entries, entries, count * sizeof(*entries));
        ui->entryCount = count;
        return;
    }

    if (count > (SIZE_MAX - ui->entryCount)) {
        ui->allocFailures++;
        return;
    }

    size_t nextCount = ui->entryCount + count;
    if (nextCount > ui->entryCap) {
        size_t nextCap = ui->entryCap ? ui->entryCap : 16u;
        while (nextCap < nextCount) {
            if (nextCap > (SIZE_MAX / 2u)) {
                nextCap = nextCount;
                break;
            }
            nextCap *= 2u;
        }
        e9k_debug_geo_register_log_entry_t *next =
            (e9k_debug_geo_register_log_entry_t *)realloc(ui->entries, nextCap * sizeof(*next));
        if (!next) {
            ui->allocFailures++;
            return;
        }
        ui->entries = next;
        ui->entryCap = nextCap;
    }

    memcpy(ui->entries + ui->entryCount, entries, count * sizeof(*entries));
    ui->entryCount = nextCount;
}
