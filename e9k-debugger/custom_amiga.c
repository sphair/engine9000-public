/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "amiga_custom_regs.h"
#include "breakpoints.h"
#include "config.h"
#include "custom_amiga.h"
#include "debugger.h"
#include "e9ui_box.h"
#include "e9ui.h"
#include "e9ui_checkbox.h"
#include "e9ui_hstack.h"
#include "e9ui_scroll.h"
#include "e9ui_spacer.h"
#include "e9ui_stack.h"
#include "e9ui_text.h"
#include "e9ui_textbox.h"
#include "libretro_host.h"
#include "protect.h"
#include "trainer.h"
#include "ui.h"

#define CUSTOM_AMIGA_TITLE "ENGINE9000 DEBUGGER - AMIGA CUSTOM CHIPSET"
#define CUSTOM_AMIGA_REG_COUNT 256
#define CUSTOM_AMIGA_VALUE_TEXT_MAX 16
#define CUSTOM_AMIGA_DETAIL_COL_W 560
#define CUSTOM_AMIGA_COL_GAP 12
#define CUSTOM_AMIGA_FILTER_TEXT_MAX 128
#define CUSTOM_AMIGA_TOOLTIP_TEXT_MAX 1024
#define CUSTOM_AMIGA_TEXT_CELL_PAD_X 8

typedef enum custom_amiga_filter_index {
    custom_amiga_filter_address = 0,
    custom_amiga_filter_name = 1,
    custom_amiga_filter_value = 2,
    custom_amiga_filter_src = 3,
    custom_amiga_filter_src_addr = 4,
    custom_amiga_filter_desc = 5,
    CUSTOM_AMIGA_FILTER_COUNT = 6
} custom_amiga_filter_index_t;

typedef struct custom_amiga_state custom_amiga_state_t;

typedef struct custom_amiga_overlay_body_state {
    custom_amiga_state_t *ui;
} custom_amiga_overlay_body_state_t;

typedef struct custom_amiga_row_cb {
    custom_amiga_state_t *ui;
    size_t regIndex;
} custom_amiga_row_cb_t;

typedef struct custom_amiga_filter_cb {
    custom_amiga_state_t *ui;
    int filterIndex;
} custom_amiga_filter_cb_t;

typedef struct custom_amiga_row_state {
    uint16_t regOffset;
    e9ui_component_t *row;
    e9ui_component_t *addressText;
    e9ui_component_t *nameText;
    e9ui_component_t *valueTextbox;
    e9ui_component_t *sourceText;
    e9ui_component_t *sourceAddrText;
    e9ui_component_t *watchCheckbox;
    e9ui_component_t *protectCheckbox;
    e9ui_component_t *detailText;
    char valueTooltip[CUSTOM_AMIGA_TOOLTIP_TEXT_MAX];
    uint16_t lastValue;
    uint32_t lastPc;
    SDL_Color lastSourceAddrColor;
    int lastHidden;
    int snapshotValid;
} custom_amiga_row_state_t;

struct custom_amiga_state {
    int open;
    e9ui_window_t *windowHost;
    e9ui_context_t ctx;
    e9ui_component_t *root;
    e9ui_component_t *contentScroll;
    e9ui_component_t *filterRoot;
    e9ui_component_t *tableScroll;
    e9ui_component_t *filterTextboxes[CUSTOM_AMIGA_FILTER_COUNT];
    char filters[CUSTOM_AMIGA_FILTER_COUNT][CUSTOM_AMIGA_FILTER_TEXT_MAX];
    custom_amiga_filter_cb_t filterCbs[CUSTOM_AMIGA_FILTER_COUNT];
    custom_amiga_row_state_t rows[CUSTOM_AMIGA_REG_COUNT];
    custom_amiga_row_cb_t rowCb[CUSTOM_AMIGA_REG_COUNT];
    int winX;
    int winY;
    int winW;
    int winH;
    int winHasSaved;
    int syncingWatchCheckboxes;
    int syncingProtectCheckboxes;
};

static custom_amiga_state_t custom_amiga_state = {0};

static int
custom_amiga_colorsEqual(SDL_Color a, SDL_Color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static const char *
custom_amiga_filterLabel(int index);

static e9ui_window_backend_t
custom_amiga_windowBackend(void)
{
    return e9ui_window_backend_overlay;
}

static e9ui_rect_t
custom_amiga_windowDefaultRect(const e9ui_context_t *ctx)
{
    e9ui_rect_t rect = {
        e9ui_scale_px(ctx, 112),
        e9ui_scale_px(ctx, 96),
        e9ui_scale_px(ctx, 1400),
        e9ui_scale_px(ctx, 620)
    };
    return rect;
}

static int
custom_amiga_parseInt(const char *value, int *out)
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
custom_amiga_parseHex16(const char *text, uint16_t *outValue)
{
    if (!text || !*text || !outValue) {
        return 0;
    }
    const char *cursor = text;
    if (cursor[0] == '$') {
        cursor++;
    } else if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
        cursor += 2;
    }
    if (!*cursor) {
        return 0;
    }
    char *end = NULL;
    unsigned long parsed = strtoul(cursor, &end, 16);
    if (!end || end == cursor || *end != '\0' || parsed > 0xfffful) {
        return 0;
    }
    *outValue = (uint16_t)parsed;
    return 1;
}

static void
custom_amiga_formatValueText(char *dst, size_t dstCap, uint16_t value)
{
    if (!dst || dstCap == 0) {
        return;
    }
    snprintf(dst, dstCap, "%04X", (unsigned)value);
}

static void
custom_amiga_formatAddressText(char *dst, size_t dstCap, uint16_t regOffset)
{
    if (!dst || dstCap == 0) {
        return;
    }
    snprintf(dst, dstCap, "%06X", (unsigned)amiga_custom_regs_addressFromOffset(regOffset));
}

static void
custom_amiga_formatSourceText(char *dst, size_t dstCap, uint32_t pc)
{
    if (!dst || dstCap == 0) {
        return;
    }
    if (!pc) {
        snprintf(dst, dstCap, "-");
        return;
    }
    if (pc & 1u) {
        snprintf(dst, dstCap, "COP");
        return;
    }
    snprintf(dst, dstCap, "CPU");
}

static void
custom_amiga_formatSourceAddrText(char *dst, size_t dstCap, uint32_t pc)
{
    if (!dst || dstCap == 0) {
        return;
    }
    if (!pc) {
        snprintf(dst, dstCap, "-");
        return;
    }
    snprintf(dst, dstCap, "%06x", (unsigned)(pc & 0x00fffffeu));
}

static SDL_Color
custom_amiga_colorFromRgb(uint32_t rgb)
{
    SDL_Color color = {
        (uint8_t)((rgb >> 16) & 0xffu),
        (uint8_t)((rgb >> 8) & 0xffu),
        (uint8_t)(rgb & 0xffu),
        0xff
    };
    return color;
}

static SDL_Color
custom_amiga_rowColor(void)
{
    SDL_Color color = { 220, 220, 224, 255 };
    return color;
}

static SDL_Color
custom_amiga_sourceColor(uint32_t pc)
{
    if (pc & 1u) {
        SDL_Color color = { 110, 212, 118, 255 };
        return color;
    }
    if (pc) {
        SDL_Color color = { 222, 92, 92, 255 };
        return color;
    }

    return custom_amiga_rowColor();
}

static void
custom_amiga_copyText(char *dst, size_t dstCap, const char *src)
{
    if (!dst || dstCap == 0) {
        return;
    }

    size_t copyLen = 0;
    if (src) {
        while (src[copyLen] && copyLen + 1 < dstCap) {
            dst[copyLen] = src[copyLen];
            copyLen++;
        }
    }
    dst[copyLen] = '\0';
}

static const char *
custom_amiga_filterLabel(int index)
{
    switch (index) {
    case custom_amiga_filter_address:
        return "ADDRESS";
    case custom_amiga_filter_name:
        return "REGISTER";
    case custom_amiga_filter_value:
        return "VALUE";
    case custom_amiga_filter_src:
        return "SRC";
    case custom_amiga_filter_src_addr:
        return "ADDR";
    case custom_amiga_filter_desc:
        return "DESC";
    default:
        break;
    }
    return "";
}

static int
custom_amiga_filterColumnWidth(int index,
                               int addressColW,
                               int nameColW,
                               int valueColW,
                               int srcColW,
                               int sourceAddrColW)
{
    switch (index) {
    case custom_amiga_filter_address:
        return addressColW;
    case custom_amiga_filter_name:
        return nameColW;
    case custom_amiga_filter_value:
        return valueColW;
    case custom_amiga_filter_src:
        return srcColW;
    case custom_amiga_filter_src_addr:
        return sourceAddrColW;
    default:
        break;
    }
    return 0;
}

static int
custom_amiga_isProtected(uint32_t addr24,
                         uint32_t sizeBits,
                         const e9k_debug_protect_t *protects,
                         size_t protectCount,
                         uint64_t enabledMask)
{
    addr24 &= 0x00ffffffu;

    for (size_t protectIndex = 0; protectIndex < protectCount && protectIndex < E9K_PROTECT_COUNT; ++protectIndex) {
        if (((enabledMask >> protectIndex) & 1ull) == 0ull) {
            continue;
        }

        const e9k_debug_protect_t *protect = &protects[protectIndex];
        if (protect->sizeBits != sizeBits) {
            continue;
        }
        if ((addr24 & protect->addrMask) != (protect->addr & protect->addrMask)) {
            continue;
        }
        return 1;
    }

    return 0;
}

static int
custom_amiga_watchpointMatchesRow(const e9k_debug_watchpoint_t *watchpoint, uint32_t addr24)
{
    const uint32_t expectedOpMask = E9K_WATCH_OP_WRITE | E9K_WATCH_OP_ACCESS_SIZE | E9K_WATCH_OP_ADDR_COMPARE_MASK;

    if (!watchpoint) {
        return 0;
    }
    if (watchpoint->op_mask != expectedOpMask) {
        return 0;
    }
    if ((watchpoint->addr & 0x00ffffffu) != (addr24 & 0x00ffffffu)) {
        return 0;
    }
    if (watchpoint->size_operand != 16u) {
        return 0;
    }
    if ((watchpoint->addr_mask_operand & 0x00ffffffu) != 0x00ffffffu) {
        return 0;
    }
    return 1;
}

static int
custom_amiga_isWriteWatched(uint32_t addr24,
                            const e9k_debug_watchpoint_t *watchpoints,
                            size_t watchpointCount,
                            uint64_t enabledMask)
{
    addr24 &= 0x00ffffffu;

    for (size_t watchpointIndex = 0; watchpointIndex < watchpointCount && watchpointIndex < E9K_WATCHPOINT_COUNT; ++watchpointIndex) {
        if (((enabledMask >> watchpointIndex) & 1ull) == 0ull) {
            continue;
        }
        if (custom_amiga_watchpointMatchesRow(&watchpoints[watchpointIndex], addr24)) {
            return 1;
        }
    }

    return 0;
}

static int
custom_amiga_addWriteWatchpoint(uint32_t addr24)
{
    uint32_t index = 0;

    return libretro_host_debugAddWatchpoint(addr24 & 0x00ffffffu,
                                            E9K_WATCH_OP_WRITE | E9K_WATCH_OP_ACCESS_SIZE | E9K_WATCH_OP_ADDR_COMPARE_MASK,
                                            0,
                                            0,
                                            0,
                                            16,
                                            0x00ffffffu,
                                            &index) ? 1 : 0;
}

static int
custom_amiga_removeWriteWatchpoint(uint32_t addr24)
{
    e9k_debug_watchpoint_t watchpoints[E9K_WATCHPOINT_COUNT];
    size_t watchpointCount = 0;
    uint64_t enabledMask = 0;

    if (!libretro_host_debugReadWatchpoints(watchpoints, E9K_WATCHPOINT_COUNT, &watchpointCount)) {
        return 0;
    }
    if (!libretro_host_debugGetWatchpointEnabledMask(&enabledMask)) {
        return 0;
    }

    addr24 &= 0x00ffffffu;
    for (uint32_t watchpointIndex = 0; watchpointIndex < (uint32_t)watchpointCount && watchpointIndex < E9K_WATCHPOINT_COUNT; ++watchpointIndex) {
        if (((enabledMask >> watchpointIndex) & 1ull) == 0ull) {
            continue;
        }
        if (!custom_amiga_watchpointMatchesRow(&watchpoints[watchpointIndex], addr24)) {
            continue;
        }
        return libretro_host_debugRemoveWatchpoint(watchpointIndex) ? 1 : 0;
    }

    return 0;
}

static int
custom_amiga_measureTitleColumnWidth(const e9ui_context_t *ctx, const char *titleText, int extraPadding_px)
{
    TTF_Font *font = NULL;
    if (e9ui) {
        font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : e9ui->ctx.font;
    }

    int textWidth = 0;
    if (font && titleText && *titleText) {
        TTF_SizeText(font, titleText, &textWidth, NULL);
    }

    int width = textWidth + e9ui_scale_px(ctx, extraPadding_px);
    return width;
}

static e9ui_component_t *
custom_amiga_wrapTextCell(e9ui_component_t *child)
{
    if (!child) {
        return NULL;
    }

    e9ui_component_t *box = e9ui_box_make(child);
    if (!box) {
        return NULL;
    }

    e9ui_box_setPaddingSides(box, CUSTOM_AMIGA_TEXT_CELL_PAD_X, 0, CUSTOM_AMIGA_TEXT_CELL_PAD_X, 0);
    return box;
}

static int
custom_amiga_minContentWidth(const e9ui_context_t *ctx)
{
    if (!ctx) {
        return 0;
    }

    const int colGap = e9ui_scale_px(ctx, CUSTOM_AMIGA_COL_GAP);
    const int addressColW =
        custom_amiga_measureTitleColumnWidth(ctx, custom_amiga_filterLabel(custom_amiga_filter_address), 36);
    const int nameColW =
        custom_amiga_measureTitleColumnWidth(ctx, custom_amiga_filterLabel(custom_amiga_filter_name), 28);
    const int valueColW =
        custom_amiga_measureTitleColumnWidth(ctx, custom_amiga_filterLabel(custom_amiga_filter_value), 28);
    const int srcColW =
        custom_amiga_measureTitleColumnWidth(ctx, custom_amiga_filterLabel(custom_amiga_filter_src), 24);
    const int sourceAddrColW =
        custom_amiga_measureTitleColumnWidth(ctx, custom_amiga_filterLabel(custom_amiga_filter_src_addr), 28);
    const int watchColW =
        custom_amiga_measureTitleColumnWidth(ctx, "W", 20);
    const int protectColW =
        custom_amiga_measureTitleColumnWidth(ctx, "P", 20);
    const int detailColMinW = e9ui_scale_px(ctx, CUSTOM_AMIGA_DETAIL_COL_W);

    return addressColW +
           colGap +
           nameColW +
           colGap +
           valueColW +
           colGap +
           srcColW +
           colGap +
           sourceAddrColW +
           colGap +
           watchColW +
           colGap +
           protectColW +
           colGap +
           detailColMinW;
}

static custom_amiga_row_state_t *
custom_amiga_findRowByComponent(custom_amiga_state_t *ui, const e9ui_component_t *comp)
{
    if (!ui || !comp) {
        return NULL;
    }

    for (size_t regIndex = 0; regIndex < CUSTOM_AMIGA_REG_COUNT; ++regIndex) {
        custom_amiga_row_state_t *row = &ui->rows[regIndex];
        if (row->valueTextbox == comp || row->sourceAddrText == comp) {
            return row;
        }
    }
    return NULL;
}

static void
custom_amiga_addOrEnableBreakpoint(uint32_t addr)
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
custom_amiga_sourceAddrClicked(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouse_ev)
{
    (void)ctx;
    (void)mouse_ev;
    custom_amiga_row_state_t *row = custom_amiga_findRowByComponent(&custom_amiga_state, self);
    if (!row || !row->snapshotValid || !row->lastPc) {
        return;
    }

    uint32_t sourceAddr = row->lastPc & 0x00fffffeu;
    if (row->lastPc & 1u) {
        ui_centerCprSourceOnAddress(sourceAddr);
        return;
    }

    custom_amiga_addOrEnableBreakpoint(sourceAddr);
}

static uint8_t
custom_amiga_upperAscii(uint8_t value)
{
    if (value >= 'a' && value <= 'z') {
        return (uint8_t)(value - ('a' - 'A'));
    }
    return value;
}

static int
custom_amiga_containsAsciiInsensitive(const char *haystack, const char *needle)
{
    if (!needle || !needle[0]) {
        return 1;
    }
    if (!haystack) {
        return 0;
    }

    size_t needleLen = strlen(needle);
    if (needleLen == 0) {
        return 1;
    }

    for (size_t startIndex = 0; haystack[startIndex]; ++startIndex) {
        size_t matchIndex = 0;
        while (matchIndex < needleLen && haystack[startIndex + matchIndex]) {
            uint8_t hay = custom_amiga_upperAscii((uint8_t)haystack[startIndex + matchIndex]);
            uint8_t need = custom_amiga_upperAscii((uint8_t)needle[matchIndex]);
            if (hay != need) {
                break;
            }
            matchIndex++;
        }
        if (matchIndex == needleLen) {
            return 1;
        }
    }
    return 0;
}

static int
custom_amiga_rowMatchesFilters(const custom_amiga_state_t *ui, uint16_t regOffset, uint16_t value, uint32_t pc)
{
    if (!ui) {
        return 1;
    }

    if (ui->filters[custom_amiga_filter_address][0]) {
        char addressText[16];
        custom_amiga_formatAddressText(addressText, sizeof(addressText), regOffset);
        if (!custom_amiga_containsAsciiInsensitive(addressText, ui->filters[custom_amiga_filter_address])) {
            return 0;
        }
    }

    if (ui->filters[custom_amiga_filter_name][0]) {
        const char *name = amiga_custom_regs_nameForOffset(regOffset);
        if (!custom_amiga_containsAsciiInsensitive(name ? name : "", ui->filters[custom_amiga_filter_name])) {
            return 0;
        }
    }

    if (ui->filters[custom_amiga_filter_value][0]) {
        char valueText[CUSTOM_AMIGA_VALUE_TEXT_MAX];
        custom_amiga_formatValueText(valueText, sizeof(valueText), value);
        if (!custom_amiga_containsAsciiInsensitive(valueText, ui->filters[custom_amiga_filter_value])) {
            return 0;
        }
    }

    if (ui->filters[custom_amiga_filter_src][0]) {
        char sourceText[32];
        custom_amiga_formatSourceText(sourceText, sizeof(sourceText), pc);
        if (!custom_amiga_containsAsciiInsensitive(sourceText, ui->filters[custom_amiga_filter_src])) {
            return 0;
        }
    }

    if (ui->filters[custom_amiga_filter_src_addr][0]) {
        char sourceAddrText[32];
        custom_amiga_formatSourceAddrText(sourceAddrText, sizeof(sourceAddrText), pc);
        if (!custom_amiga_containsAsciiInsensitive(sourceAddrText, ui->filters[custom_amiga_filter_src_addr])) {
            return 0;
        }
    }

    if (ui->filters[custom_amiga_filter_desc][0]) {
        const char *description = amiga_custom_regs_descriptionForOffset(regOffset);
        if (!custom_amiga_containsAsciiInsensitive(description ? description : "", ui->filters[custom_amiga_filter_desc])) {
            return 0;
        }
    }

    return 1;
}

static void
custom_amiga_syncRows(custom_amiga_state_t *ui)
{
    if (!ui) {
        return;
    }

    const e9k_debug_ami_custom_reg_state_t *regs = libretro_host_debugAmiGetCustomRegs();
    e9ui_component_t *focused = e9ui ? e9ui_getFocus(&e9ui->ctx) : e9ui_getFocus(&ui->ctx);

    e9k_debug_protect_t protects[E9K_PROTECT_COUNT];
    size_t protectCount = 0;
    uint64_t enabledMask = 0;
    int haveProtects = libretro_host_debugReadProtects(protects, E9K_PROTECT_COUNT, &protectCount) &&
                       libretro_host_debugGetProtectEnabledMask(&enabledMask);
    e9k_debug_watchpoint_t watchpoints[E9K_WATCHPOINT_COUNT];
    size_t watchpointCount = 0;
    uint64_t watchEnabledMask = 0;
    int haveWatchpoints = libretro_host_debugReadWatchpoints(watchpoints, E9K_WATCHPOINT_COUNT, &watchpointCount) &&
                          libretro_host_debugGetWatchpointEnabledMask(&watchEnabledMask);

    for (size_t regIndex = 0; regIndex < CUSTOM_AMIGA_REG_COUNT; ++regIndex) {
        custom_amiga_row_state_t *row = &ui->rows[regIndex];
        if (!row->row || !row->valueTextbox || !row->sourceText || !row->sourceAddrText || !row->watchCheckbox || !row->protectCheckbox) {
            continue;
        }

        uint16_t value = regs ? regs[regIndex].value : 0u;
        uint32_t pc = regs ? regs[regIndex].pc : 0u;
        int hadSnapshot = row->snapshotValid ? 1 : 0;
        int valueTextboxFocused = focused == row->valueTextbox ? 1 : 0;
        int valueTextboxHovered = row->valueTextbox->mouseInside ? 1 : 0;
        int valueTooltipActive = valueTextboxFocused || valueTextboxHovered;

        if (!valueTextboxFocused) {
            if (!hadSnapshot || row->lastValue != value) {
                char valueText[CUSTOM_AMIGA_VALUE_TEXT_MAX];
                custom_amiga_formatValueText(valueText, sizeof(valueText), value);
                e9ui_textbox_setText(row->valueTextbox, valueText);
            }
        }
        const char *textboxText = e9ui_textbox_getText(row->valueTextbox);
        if (!hadSnapshot || row->lastPc != pc) {
            char sourceText[32];
            char sourceAddrText[32];
            custom_amiga_formatSourceText(sourceText, sizeof(sourceText), pc);
            custom_amiga_formatSourceAddrText(sourceAddrText, sizeof(sourceAddrText), pc);
            e9ui_text_setText(row->sourceText, sourceText);
            e9ui_text_setText(row->sourceAddrText, sourceAddrText);
            e9ui_text_setColor(row->sourceText, custom_amiga_sourceColor(pc));
        }
        {
            SDL_Color sourceAddrColor = custom_amiga_rowColor();
            if (pc && !(pc & 1u)) {
                machine_breakpoint_t *bp = machine_findBreakpointByAddr(&debugger.machine, pc & 0x00fffffeu);
                if (bp && bp->enabled) {
                    sourceAddrColor = custom_amiga_sourceColor(1u);
                }
            }
            if (!hadSnapshot || !custom_amiga_colorsEqual(row->lastSourceAddrColor, sourceAddrColor)) {
                e9ui_text_setColor(row->sourceAddrText, sourceAddrColor);
                row->lastSourceAddrColor = sourceAddrColor;
            }
        }
        row->lastValue = value;
        row->lastPc = pc;
        row->snapshotValid = 1;
        {
            int hidden = custom_amiga_rowMatchesFilters(ui, row->regOffset, value, pc) ? 0 : 1;
            if (!hadSnapshot || row->lastHidden != hidden) {
                e9ui_setHidden(row->row, hidden);
                row->lastHidden = hidden;
            }
        }
        if (haveWatchpoints) {
            uint32_t addr = amiga_custom_regs_addressFromOffset(row->regOffset);
            int isWriteWatched = custom_amiga_isWriteWatched(addr, watchpoints, watchpointCount, watchEnabledMask);
            if (e9ui_checkbox_isSelected(row->watchCheckbox) != isWriteWatched) {
                ui->syncingWatchCheckboxes = 1;
                e9ui_checkbox_setSelected(row->watchCheckbox, isWriteWatched, &ui->ctx);
                ui->syncingWatchCheckboxes = 0;
            }
        }
        if (haveProtects) {
            uint32_t addr = amiga_custom_regs_addressFromOffset(row->regOffset);
            int isProtected = custom_amiga_isProtected(addr, 16, protects, protectCount, enabledMask);
            if (e9ui_checkbox_isSelected(row->protectCheckbox) != isProtected) {
                ui->syncingProtectCheckboxes = 1;
                e9ui_checkbox_setSelected(row->protectCheckbox, isProtected, &ui->ctx);
                ui->syncingProtectCheckboxes = 0;
            }
        }

        if (valueTooltipActive) {
            uint16_t tooltipValue = value;
            if (valueTextboxFocused && textboxText && *textboxText) {
                uint16_t parsedValue = 0;
                if (custom_amiga_parseHex16(textboxText, &parsedValue)) {
                    tooltipValue = parsedValue;
                }
            }
            char tooltipText[CUSTOM_AMIGA_TOOLTIP_TEXT_MAX];
            custom_amiga_copyText(tooltipText,
                                  sizeof(tooltipText),
                                  amiga_custom_regs_valueTooltipForOffset(row->regOffset, tooltipValue));
            if (!hadSnapshot || strcmp(row->valueTooltip, tooltipText) != 0) {
                custom_amiga_copyText(row->valueTooltip, sizeof(row->valueTooltip), tooltipText);
                e9ui_setTooltip(row->valueTextbox, row->valueTooltip[0] ? row->valueTooltip : NULL);
            }
        }
    }
}

static void
custom_amiga_filterTextboxChanged(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    custom_amiga_filter_cb_t *cb = (custom_amiga_filter_cb_t *)user;
    if (!cb || !cb->ui) {
        return;
    }
    if (cb->filterIndex < 0 || cb->filterIndex >= CUSTOM_AMIGA_FILTER_COUNT) {
        return;
    }

    e9ui_component_t *textbox = cb->ui->filterTextboxes[cb->filterIndex];
    const char *text = e9ui_textbox_getText(textbox);
    custom_amiga_copyText(cb->ui->filters[cb->filterIndex],
                          sizeof(cb->ui->filters[cb->filterIndex]),
                          text ? text : "");
    custom_amiga_syncRows(cb->ui);
}

static e9ui_component_t *
custom_amiga_buildFilterTextbox(custom_amiga_state_t *ui, int filterIndex)
{
    if (!ui || filterIndex < 0 || filterIndex >= CUSTOM_AMIGA_FILTER_COUNT) {
        return NULL;
    }

    ui->filterCbs[filterIndex].ui = ui;
    ui->filterCbs[filterIndex].filterIndex = filterIndex;

    e9ui_component_t *textbox =
        e9ui_textbox_make(CUSTOM_AMIGA_FILTER_TEXT_MAX - 1, NULL, custom_amiga_filterTextboxChanged, &ui->filterCbs[filterIndex]);
    if (!textbox) {
        return NULL;
    }

    e9ui_textbox_setPlaceholder(textbox, custom_amiga_filterLabel(filterIndex));
    e9ui_textbox_setText(textbox, ui->filters[filterIndex]);
    ui->filterTextboxes[filterIndex] = textbox;
    return textbox;
}

static e9ui_component_t *
custom_amiga_buildFilterRoot(custom_amiga_state_t *ui,
                             int addressColW,
                             int nameColW,
                             int valueColW,
                             int srcColW,
                             int sourceAddrColW,
                             int watchColW,
                             int protectColW)
{
    if (!ui) {
        return NULL;
    }

    const int colGap = e9ui_scale_px(&ui->ctx, CUSTOM_AMIGA_COL_GAP);
    e9ui_component_t *row = e9ui_hstack_make();
    if (!row) {
        return NULL;
    }

    for (int i = 0; i <= custom_amiga_filter_src_addr; ++i) {
        e9ui_component_t *textbox = custom_amiga_buildFilterTextbox(ui, i);
        if (!textbox) {
            e9ui_childDestroy(row, &ui->ctx);
            return NULL;
        }
        e9ui_hstack_addFixed(row,
                             textbox,
                             custom_amiga_filterColumnWidth(i,
                                                            addressColW,
                                                            nameColW,
                                                            valueColW,
                                                            srcColW,
                                                            sourceAddrColW));
        e9ui_hstack_addFixed(row, e9ui_spacer_make(colGap), colGap);
    }

    e9ui_component_t *watchLabel = e9ui_text_make("W");
    if (!watchLabel) {
        e9ui_childDestroy(row, &ui->ctx);
        return NULL;
    }
    e9ui_text_setColor(watchLabel, custom_amiga_rowColor());
    e9ui_setTooltip(watchLabel, "Watchpoint");
    e9ui_hstack_addFixed(row, watchLabel, watchColW);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(colGap), colGap);

    e9ui_component_t *protectLabel = e9ui_text_make("P");
    if (!protectLabel) {
        e9ui_childDestroy(row, &ui->ctx);
        return NULL;
    }
    e9ui_text_setColor(protectLabel, custom_amiga_rowColor());
    e9ui_setTooltip(protectLabel, "Protect register");
    e9ui_hstack_addFixed(row, protectLabel, protectColW);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(colGap), colGap);

    e9ui_component_t *descTextbox = custom_amiga_buildFilterTextbox(ui, custom_amiga_filter_desc);
    if (!descTextbox) {
        e9ui_childDestroy(row, &ui->ctx);
        return NULL;
    }
    e9ui_hstack_addFlex(row, descTextbox);

    return row;
}

static void
custom_amiga_watchCheckboxChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    custom_amiga_row_cb_t *cb = (custom_amiga_row_cb_t *)user;
    if (!cb || !cb->ui || cb->regIndex >= CUSTOM_AMIGA_REG_COUNT) {
        return;
    }

    custom_amiga_state_t *ui = cb->ui;
    if (ui->syncingWatchCheckboxes) {
        return;
    }

    custom_amiga_row_state_t *row = &ui->rows[cb->regIndex];
    uint32_t addr = amiga_custom_regs_addressFromOffset(row->regOffset);
    int ok = selected ? custom_amiga_addWriteWatchpoint(addr) : custom_amiga_removeWriteWatchpoint(addr);
    if (!ok) {
        ui->syncingWatchCheckboxes = 1;
        e9ui_checkbox_setSelected(row->watchCheckbox, selected ? 0 : 1, &ui->ctx);
        ui->syncingWatchCheckboxes = 0;
        return;
    }
    custom_amiga_syncRows(ui);
}

static void
custom_amiga_protectCheckboxChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    custom_amiga_row_cb_t *cb = (custom_amiga_row_cb_t *)user;
    if (!cb || !cb->ui || cb->regIndex >= CUSTOM_AMIGA_REG_COUNT) {
        return;
    }

    custom_amiga_state_t *ui = cb->ui;
    if (ui->syncingProtectCheckboxes) {
        return;
    }

    custom_amiga_row_state_t *row = &ui->rows[cb->regIndex];
    uint32_t addr = amiga_custom_regs_addressFromOffset(row->regOffset);
    int ok = selected ? protect_addBlock(addr, 16) : protect_remove(addr, 16);
    if (!ok) {
        ui->syncingProtectCheckboxes = 1;
        e9ui_checkbox_setSelected(row->protectCheckbox, selected ? 0 : 1, &ui->ctx);
        ui->syncingProtectCheckboxes = 0;
        return;
    }
    trainer_markDirty();
    custom_amiga_syncRows(ui);
}

static void
custom_amiga_valueSubmitted(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    custom_amiga_row_cb_t *cb = (custom_amiga_row_cb_t *)user;
    if (!cb || !cb->ui || cb->regIndex >= CUSTOM_AMIGA_REG_COUNT) {
        return;
    }

    custom_amiga_state_t *ui = cb->ui;
    custom_amiga_row_state_t *row = &ui->rows[cb->regIndex];
    if (!row->valueTextbox) {
        return;
    }

    const char *text = e9ui_textbox_getText(row->valueTextbox);
    uint16_t parsedValue = 0;
    if (!custom_amiga_parseHex16(text, &parsedValue)) {
        if (row->snapshotValid) {
            char valueText[CUSTOM_AMIGA_VALUE_TEXT_MAX];
            custom_amiga_formatValueText(valueText, sizeof(valueText), row->lastValue);
            e9ui_textbox_setText(row->valueTextbox, valueText);
        }
        return;
    }

    uint32_t addr = amiga_custom_regs_addressFromOffset(row->regOffset);
    if (!libretro_host_debugWriteMemory(addr, parsedValue, 2)) {
        if (row->snapshotValid) {
            char valueText[CUSTOM_AMIGA_VALUE_TEXT_MAX];
            custom_amiga_formatValueText(valueText, sizeof(valueText), row->lastValue);
            e9ui_textbox_setText(row->valueTextbox, valueText);
        }
        return;
    }

    char valueText[CUSTOM_AMIGA_VALUE_TEXT_MAX];
    custom_amiga_formatValueText(valueText, sizeof(valueText), parsedValue);
    e9ui_textbox_setText(row->valueTextbox, valueText);
    custom_amiga_syncRows(ui);
}

static e9ui_component_t *
custom_amiga_buildRoot(custom_amiga_state_t *ui)
{
    if (!ui) {
        return NULL;
    }

    const int colGap = e9ui_scale_px(&ui->ctx, CUSTOM_AMIGA_COL_GAP);
    const int addressColW =
        custom_amiga_measureTitleColumnWidth(&ui->ctx, custom_amiga_filterLabel(custom_amiga_filter_address), 36);
    const int nameColW =
        custom_amiga_measureTitleColumnWidth(&ui->ctx, custom_amiga_filterLabel(custom_amiga_filter_name), 28);
    const int valueColW =
        custom_amiga_measureTitleColumnWidth(&ui->ctx, custom_amiga_filterLabel(custom_amiga_filter_value), 28);
    const int srcColW =
        custom_amiga_measureTitleColumnWidth(&ui->ctx, custom_amiga_filterLabel(custom_amiga_filter_src), 24);
    const int sourceAddrColW =
        custom_amiga_measureTitleColumnWidth(&ui->ctx, custom_amiga_filterLabel(custom_amiga_filter_src_addr), 28);
    const int watchColW =
        custom_amiga_measureTitleColumnWidth(&ui->ctx, "W", 20);
    const int protectColW =
        custom_amiga_measureTitleColumnWidth(&ui->ctx, "P", 20);
    const int contentWidth = custom_amiga_minContentWidth(&ui->ctx);

    e9ui_component_t *root = e9ui_stack_makeVertical();
    if (!root) {
        return NULL;
    }

    e9ui_component_t *contentStack = e9ui_stack_makeVertical();
    if (!contentStack) {
        e9ui_childDestroy(root, &ui->ctx);
        return NULL;
    }

    e9ui_component_t *filterRoot =
        custom_amiga_buildFilterRoot(ui, addressColW, nameColW, valueColW, srcColW, sourceAddrColW, watchColW, protectColW);
    if (!filterRoot) {
        e9ui_childDestroy(contentStack, &ui->ctx);
        e9ui_childDestroy(root, &ui->ctx);
        return NULL;
    }
    ui->filterRoot = filterRoot;
    e9ui_stack_addFixed(contentStack, filterRoot);

    e9ui_component_t *tableRoot = e9ui_stack_makeVertical();
    if (!tableRoot) {
        e9ui_childDestroy(contentStack, &ui->ctx);
        e9ui_childDestroy(root, &ui->ctx);
        return NULL;
    }

    e9ui_component_t *rowsStack = e9ui_stack_makeVertical();
    if (!rowsStack) {
        e9ui_childDestroy(tableRoot, &ui->ctx);
        e9ui_childDestroy(contentStack, &ui->ctx);
        e9ui_childDestroy(root, &ui->ctx);
        return NULL;
    }

    for (size_t regIndex = 0; regIndex < CUSTOM_AMIGA_REG_COUNT; ++regIndex) {
        uint16_t regOffset = (uint16_t)(regIndex << 1);
        custom_amiga_row_state_t *row = &ui->rows[regIndex];
        custom_amiga_row_cb_t *cb = &ui->rowCb[regIndex];
        memset(row, 0, sizeof(*row));
        memset(cb, 0, sizeof(*cb));
        row->regOffset = regOffset;
        cb->ui = ui;
        cb->regIndex = regIndex;

        e9ui_component_t *rowComp = e9ui_hstack_make();
        if (!rowComp) {
            return NULL;
        }

        char addressText[16];
        custom_amiga_formatAddressText(addressText, sizeof(addressText), regOffset);
        e9ui_component_t *addressComp = e9ui_text_make(addressText);
        if (!addressComp) {
            return NULL;
        }
        e9ui_component_t *addressBox = custom_amiga_wrapTextCell(addressComp);
        if (!addressBox) {
            return NULL;
        }
        e9ui_text_setColor(addressComp, custom_amiga_rowColor());

        const char *name = amiga_custom_regs_nameForOffset(regOffset);
        e9ui_component_t *nameComp = e9ui_text_make(name ? name : "UNKNOWN");
        if (!nameComp) {
            return NULL;
        }
        e9ui_component_t *nameBox = custom_amiga_wrapTextCell(nameComp);
        if (!nameBox) {
            return NULL;
        }
        SDL_Color regColor = custom_amiga_colorFromRgb(amiga_custom_regs_colorForOffset(regOffset));
        e9ui_text_setColor(nameComp, regColor);

        e9ui_component_t *valueTextbox =
            e9ui_textbox_make(CUSTOM_AMIGA_VALUE_TEXT_MAX - 1, custom_amiga_valueSubmitted, NULL, cb);
        if (!valueTextbox) {
            return NULL;
        }
        e9ui_textbox_setPlaceholder(valueTextbox, "0000");
        e9ui_textbox_setEnterMovesToNextTextbox(valueTextbox, 1);
        e9ui_textbox_setTextColor(valueTextbox, 1, custom_amiga_rowColor());
        e9ui_textbox_setText(valueTextbox, "0000");

        e9ui_component_t *sourceComp = e9ui_text_make("-");
        if (!sourceComp) {
            return NULL;
        }
        e9ui_component_t *sourceBox = custom_amiga_wrapTextCell(sourceComp);
        if (!sourceBox) {
            return NULL;
        }
        e9ui_text_setColor(sourceComp, custom_amiga_rowColor());

        e9ui_component_t *sourceAddrComp = e9ui_text_make("-");
        if (!sourceAddrComp) {
            return NULL;
        }
        e9ui_component_t *sourceAddrBox = custom_amiga_wrapTextCell(sourceAddrComp);
        if (!sourceAddrBox) {
            return NULL;
        }
        e9ui_text_setColor(sourceAddrComp, custom_amiga_rowColor());
        sourceAddrComp->onClick = custom_amiga_sourceAddrClicked;

        e9ui_component_t *watchCheckbox = e9ui_checkbox_make(NULL, 0, custom_amiga_watchCheckboxChanged, cb);
        if (!watchCheckbox) {
            return NULL;
        }

        e9ui_component_t *protectCheckbox = e9ui_checkbox_make(NULL, 0, custom_amiga_protectCheckboxChanged, cb);
        if (!protectCheckbox) {
            return NULL;
        }

        const char *description = amiga_custom_regs_descriptionForOffset(regOffset);
        e9ui_component_t *detailComp = e9ui_text_make(description ? description : "");
        if (!detailComp) {
            return NULL;
        }
        e9ui_component_t *detailBox = custom_amiga_wrapTextCell(detailComp);
        if (!detailBox) {
            return NULL;
        }
        e9ui_text_setColor(detailComp, regColor);

        row->addressText = addressComp;
        row->nameText = nameComp;
        row->valueTextbox = valueTextbox;
        row->sourceText = sourceComp;
        row->sourceAddrText = sourceAddrComp;
        row->watchCheckbox = watchCheckbox;
        row->protectCheckbox = protectCheckbox;
        row->detailText = detailComp;
        row->row = rowComp;

        e9ui_hstack_addFixed(rowComp, addressBox, addressColW);
        e9ui_hstack_addFixed(rowComp, e9ui_spacer_make(colGap), colGap);
        e9ui_hstack_addFixed(rowComp, nameBox, nameColW);
        e9ui_hstack_addFixed(rowComp, e9ui_spacer_make(colGap), colGap);
        e9ui_hstack_addFixed(rowComp, valueTextbox, valueColW);
        e9ui_hstack_addFixed(rowComp, e9ui_spacer_make(colGap), colGap);
        e9ui_hstack_addFixed(rowComp, sourceBox, srcColW);
        e9ui_hstack_addFixed(rowComp, e9ui_spacer_make(colGap), colGap);
        e9ui_hstack_addFixed(rowComp, sourceAddrBox, sourceAddrColW);
        e9ui_hstack_addFixed(rowComp, e9ui_spacer_make(colGap), colGap);
        e9ui_hstack_addFixed(rowComp, watchCheckbox, watchColW);
        e9ui_hstack_addFixed(rowComp, e9ui_spacer_make(colGap), colGap);
        e9ui_hstack_addFixed(rowComp, protectCheckbox, protectColW);
        e9ui_hstack_addFixed(rowComp, e9ui_spacer_make(colGap), colGap);
        e9ui_hstack_addFlex(rowComp, detailBox);
        e9ui_stack_addFixed(rowsStack, rowComp);
    }

    e9ui_stack_addFixed(tableRoot, rowsStack);

    e9ui_component_t *tableScroll = e9ui_scroll_make(tableRoot);
    if (!tableScroll) {
        e9ui_childDestroy(contentStack, &ui->ctx);
        e9ui_childDestroy(root, &ui->ctx);
        return NULL;
    }
    e9ui_scroll_setContentWidthPx(tableScroll, contentWidth);
    ui->tableScroll = tableScroll;
    e9ui_stack_addFlex(contentStack, tableScroll);

    e9ui_component_t *contentScroll = e9ui_scroll_make(contentStack);
    if (!contentScroll) {
        e9ui_childDestroy(contentStack, &ui->ctx);
        e9ui_childDestroy(root, &ui->ctx);
        ui->tableScroll = NULL;
        return NULL;
    }
    e9ui_scroll_setContentWidthPx(contentScroll, contentWidth);
    ui->contentScroll = contentScroll;
    e9ui_stack_addFlex(root, contentScroll);
    return root;
}

static int
custom_amiga_overlayBodyPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)ctx;
    (void)availW;
    return 0;
}

static void
custom_amiga_overlayBodyLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self || !ctx || !self->state) {
        return;
    }

    self->bounds = bounds;
    custom_amiga_overlay_body_state_t *st = (custom_amiga_overlay_body_state_t *)self->state;
    custom_amiga_state_t *ui = st ? st->ui : NULL;
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

    int contentWidth = custom_amiga_minContentWidth(&ui->ctx);
    if (bounds.w > contentWidth) {
        contentWidth = bounds.w;
    }

    if (ui->contentScroll) {
        e9ui_scroll_setContentWidthPx(ui->contentScroll, contentWidth);
        e9ui_scroll_setContentHeightPx(ui->contentScroll, bounds.h);
    }
    if (ui->tableScroll) {
        e9ui_scroll_setContentWidthPx(ui->tableScroll, contentWidth);
    }

    if (ui->root && ui->root->layout) {
        ui->root->layout(ui->root, &ui->ctx, bounds);
    }
}

static void
custom_amiga_overlayBodyRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !self->state) {
        return;
    }

    custom_amiga_overlay_body_state_t *st = (custom_amiga_overlay_body_state_t *)self->state;
    custom_amiga_state_t *ui = st ? st->ui : NULL;
    if (!ui || !ui->open) {
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

    custom_amiga_syncRows(ui);

    if (ui->root && ui->root->render) {
        ui->root->render(ui->root, &ui->ctx);
    }
}

static void
custom_amiga_overlayBodyDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    custom_amiga_overlay_body_state_t *st = (custom_amiga_overlay_body_state_t *)self->state;
    alloc_free(st);
    self->state = NULL;
}

static e9ui_component_t *
custom_amiga_makeOverlayBodyHost(custom_amiga_state_t *ui)
{
    if (!ui || !ui->root) {
        return NULL;
    }

    e9ui_component_t *host = (e9ui_component_t *)alloc_calloc(1, sizeof(*host));
    if (!host) {
        return NULL;
    }

    custom_amiga_overlay_body_state_t *st =
        (custom_amiga_overlay_body_state_t *)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(host);
        return NULL;
    }

    st->ui = ui;
    host->name = "custom_amiga_overlay_body";
    host->state = st;
    host->preferredHeight = custom_amiga_overlayBodyPreferredHeight;
    host->layout = custom_amiga_overlayBodyLayout;
    host->render = custom_amiga_overlayBodyRender;
    host->dtor = custom_amiga_overlayBodyDtor;
    e9ui_child_add(host, ui->root, alloc_strdup("custom_amiga_root"));
    return host;
}

static void
custom_amiga_overlayWindowCloseRequested(e9ui_window_t *window, void *user)
{
    (void)window;
    custom_amiga_state_t *ui = (custom_amiga_state_t *)user;
    if (!ui) {
        return;
    }
    custom_amiga_shutdown();
}

int
custom_amiga_init(void)
{
    custom_amiga_state_t *ui = &custom_amiga_state;
    if (ui->open) {
        return 1;
    }

    memset(ui->rows, 0, sizeof(ui->rows));
    memset(ui->rowCb, 0, sizeof(ui->rowCb));
    memset(ui->filterCbs, 0, sizeof(ui->filterCbs));
    ui->contentScroll = NULL;
    ui->tableScroll = NULL;
    ui->filterRoot = NULL;
    memset(ui->filterTextboxes, 0, sizeof(ui->filterTextboxes));
    ui->root = NULL;
    memset(ui->filters, 0, sizeof(ui->filters));
    ui->ctx = e9ui->ctx;

    ui->windowHost = e9ui_windowCreate(custom_amiga_windowBackend());
    if (!ui->windowHost) {
        return 0;
    }

    ui->root = custom_amiga_buildRoot(ui);
    if (!ui->root) {
        e9ui_windowDestroy(ui->windowHost);
        ui->windowHost = NULL;
        return 0;
    }

    int hasSavedSize = (ui->winHasSaved && ui->winW > 0 && ui->winH > 0) ? 1 : 0;
    e9ui_rect_t rect = e9ui_windowResolveOpenRect(&e9ui->ctx,
                                                  custom_amiga_windowDefaultRect(&e9ui->ctx),
                                                  hasSavedSize ? 0 : 1300,
                                                  hasSavedSize ? 0 : 420,
                                                  1,
                                                  ui->winHasSaved ? 1 : 0,
                                                  hasSavedSize,
                                                  ui->winX,
                                                  ui->winY,
                                                  ui->winW,
                                                  ui->winH);
    e9ui_component_t *overlayBodyHost = custom_amiga_makeOverlayBodyHost(ui);
    if (!overlayBodyHost) {
        e9ui_windowDestroy(ui->windowHost);
        ui->windowHost = NULL;
        ui->root = NULL;
        return 0;
    }

    if (!e9ui_windowOpen(ui->windowHost,
                         CUSTOM_AMIGA_TITLE,
                         rect,
                         overlayBodyHost,
                         custom_amiga_overlayWindowCloseRequested,
                         ui,
                         &e9ui->ctx)) {
        e9ui_childDestroy(overlayBodyHost, &e9ui->ctx);
        e9ui_windowDestroy(ui->windowHost);
        ui->windowHost = NULL;
        ui->root = NULL;
        return 0;
    }

    ui->ctx = e9ui->ctx;
    ui->open = 1;
    custom_amiga_syncRows(ui);
    return 1;
}

void
custom_amiga_shutdown(void)
{
    custom_amiga_state_t *ui = &custom_amiga_state;
    if (!ui->open) {
        return;
    }

    (void)e9ui_windowCaptureRectSnapshot(ui->windowHost,
                                         (e9ui ? &e9ui->ctx : &ui->ctx),
                                         &ui->winHasSaved,
                                         &ui->winX,
                                         &ui->winY,
                                         &ui->winW,
                                         &ui->winH);
    config_saveConfig();

    if (ui->windowHost) {
        e9ui_windowDestroy(ui->windowHost);
        ui->windowHost = NULL;
    }

    ui->root = NULL;
    ui->contentScroll = NULL;
    ui->filterRoot = NULL;
    ui->tableScroll = NULL;
    memset(ui->filterTextboxes, 0, sizeof(ui->filterTextboxes));
    ui->open = 0;
    memset(&ui->ctx, 0, sizeof(ui->ctx));
    memset(ui->rows, 0, sizeof(ui->rows));
    memset(ui->rowCb, 0, sizeof(ui->rowCb));
    memset(ui->filterCbs, 0, sizeof(ui->filterCbs));
}

void
custom_amiga_toggle(void)
{
    if (custom_amiga_isOpen()) {
        custom_amiga_shutdown();
        return;
    }
    (void)custom_amiga_init();
}

int
custom_amiga_isOpen(void)
{
    return custom_amiga_state.open ? 1 : 0;
}

uint32_t
custom_amiga_getWindowId(void)
{
    return 0;
}

void
custom_amiga_handleEvent(SDL_Event *ev)
{
    (void)ev;
}

void
custom_amiga_setMainWindowFocused(int focused)
{
    (void)focused;
}

void
custom_amiga_render(void)
{
    custom_amiga_state_t *ui = &custom_amiga_state;
    if (!ui->open) {
        return;
    }
    if (e9ui_windowCaptureRectChanged(ui->windowHost,
                                      (e9ui ? &e9ui->ctx : &ui->ctx),
                                      &ui->winHasSaved,
                                      &ui->winX,
                                      &ui->winY,
                                      &ui->winW,
                                      &ui->winH)) {
        config_saveConfig();
    }
}

void
custom_amiga_persistConfig(FILE *file)
{
    custom_amiga_state_t *ui = &custom_amiga_state;
    if (!file) {
        return;
    }

    if (ui->open) {
        (void)e9ui_windowCaptureRectSnapshot(ui->windowHost,
                                             (e9ui ? &e9ui->ctx : &ui->ctx),
                                             &ui->winHasSaved,
                                             &ui->winX,
                                             &ui->winY,
                                             &ui->winW,
                                             &ui->winH);
    }
    if (!ui->winHasSaved) {
        return;
    }

    fprintf(file, "comp.custom_amiga.win_x=%d\n", ui->winX);
    fprintf(file, "comp.custom_amiga.win_y=%d\n", ui->winY);
    fprintf(file, "comp.custom_amiga.win_w=%d\n", ui->winW);
    fprintf(file, "comp.custom_amiga.win_h=%d\n", ui->winH);
}

int
custom_amiga_loadConfigProperty(const char *prop, const char *value)
{
    custom_amiga_state_t *ui = &custom_amiga_state;
    if (!prop || !value) {
        return 0;
    }

    int intValue = 0;
    if (strcmp(prop, "win_x") == 0) {
        if (!custom_amiga_parseInt(value, &intValue)) {
            return 0;
        }
        ui->winX = intValue;
    } else if (strcmp(prop, "win_y") == 0) {
        if (!custom_amiga_parseInt(value, &intValue)) {
            return 0;
        }
        ui->winY = intValue;
    } else if (strcmp(prop, "win_w") == 0) {
        if (!custom_amiga_parseInt(value, &intValue)) {
            return 0;
        }
        ui->winW = intValue;
    } else if (strcmp(prop, "win_h") == 0) {
        if (!custom_amiga_parseInt(value, &intValue)) {
            return 0;
        }
        ui->winH = intValue;
    } else {
        return 0;
    }

    ui->winHasSaved = 1;
    return 1;
}
