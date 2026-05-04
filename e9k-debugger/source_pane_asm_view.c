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

#include "e9ui.h"
#include "base_map.h"
#include "breakpoints.h"
#include "dasm.h"
#include "debugger.h"
#include "hex_byte_color.h"
#include "libretro_host.h"
#include "machine.h"
#include "source_cpr.h"
#include "source_pane_internal.h"
#include "source_z80.h"

/* Set to 0 to restore the old snapshot-while-running behavior for ASM/HEX. */
static int source_pane_asm_view_enableLiveViews = 0;

static int
source_pane_asm_view_isLiveUpdateEnabled(void)
{
    return source_pane_asm_view_enableLiveViews ? 1 : 0;
}

static uint64_t
source_pane_asm_view_resolveAsmAnchorAddr(uint64_t addr)
{
    uint64_t a = addr & 0x00ffffffull;
    a &= ~1ull;

    int idx = 0;
    if (dasm_findIndexForAddr(a, &idx)) {
        return a;
    }

    uint32_t asDebug = (uint32_t)a;
    if (base_map_runtimeToDebug(BASE_MAP_SECTION_TEXT, (uint32_t)a, &asDebug)) {
        uint64_t mapped = (uint64_t)(asDebug & 0x00ffffffu);
        mapped &= ~1ull;
        if (dasm_findIndexForAddr(mapped, &idx)) {
            return mapped;
        }
    }

    uint32_t asRuntime = (uint32_t)a;
    if (base_map_debugToRuntime(BASE_MAP_SECTION_TEXT, (uint32_t)a, &asRuntime)) {
        uint64_t mapped = (uint64_t)(asRuntime & 0x00ffffffu);
        mapped &= ~1ull;
        if (dasm_findIndexForAddr(mapped, &idx)) {
            return mapped;
        }
    }

    return a;
}

static uint64_t
source_pane_asm_view_resolveAsmLikeAnchorAddr(source_pane_state_t *st, uint64_t addr)
{
    if (st && st->viewMode == source_pane_mode_cpr) {
        return source_cpr_resolveAnchorAddr(addr);
    }
    if (st && st->viewMode == source_pane_mode_z80) {
        return source_z80_resolveAnchorAddr(addr);
    }
    return source_pane_asm_view_resolveAsmAnchorAddr(addr);
}

static void
source_pane_asm_view_setAsmAnchorLocked(source_pane_state_t *st, uint64_t addr)
{
    if (!st) {
        return;
    }
    uint64_t a = source_pane_asm_view_resolveAsmLikeAnchorAddr(st, addr);
    if (st->viewMode == source_pane_mode_cpr) {
        st->scrollIndex = (int)(a >> 2);
        st->scrollAnchorAddr = a;
        st->scrollAnchorValid = 1;
        st->scrollLocked = 1;
        st->gutterPending = 0;
        source_pane_syncLockButtonVisual(st);
        return;
    }
    if (st->viewMode == source_pane_mode_h) {
        st->scrollIndex = 0;
        st->scrollAnchorAddr = a;
        st->scrollAnchorValid = 1;
        st->scrollLocked = 1;
        st->gutterPending = 0;
        source_pane_syncLockButtonVisual(st);
        return;
    }
    if (st->viewMode == source_pane_mode_z80) {
        st->scrollIndex = (int)(a & 0xffffull);
        st->scrollAnchorAddr = a;
        st->scrollAnchorValid = 1;
        st->scrollLocked = 1;
        st->gutterPending = 0;
        source_pane_syncLockButtonVisual(st);
        return;
    }
    int idx = 0;
    if (!dasm_findIndexForAddr(a, &idx)) {
        idx = 0;
    }
    if (!(dasm_getFlags() & DASM_IFACE_FLAG_STREAMING)) {
        idx -= 1;
    }
    if (idx < 0) {
        idx = 0;
    }
    st->scrollIndex = idx;
    st->scrollAnchorAddr = a;
    st->scrollAnchorValid = 1;
    st->scrollLocked = 1;
    st->gutterPending = 0;
    source_pane_syncLockButtonVisual(st);
}

static int
source_pane_asm_view_getAsmWindow(source_pane_state_t *st, int maxLines, uint64_t *outCurAddr,
                                  const char ***outLines, const uint64_t **outAddrs,
                                  int *outCount)
{
    if (outCurAddr) {
        *outCurAddr = 0;
    }
    if (outLines) {
        *outLines = NULL;
    }
    if (outAddrs) {
        *outAddrs = NULL;
    }
    if (outCount) {
        *outCount = 0;
    }

    if (!st || maxLines <= 0 || !outLines || !outAddrs || !outCount || !outCurAddr) {
        return 0;
    }
    if (st->viewMode == source_pane_mode_z80) {
        return source_z80_getWindow(st, maxLines, outCurAddr, outLines, outAddrs, outCount);
    }

    int streaming = (dasm_getFlags() & DASM_IFACE_FLAG_STREAMING) ? 1 : 0;
    int total = dasm_getTotal();
    if (!streaming && total <= 0) {
        return 0;
    }

    int freezeWhileRunning = source_pane_asm_view_shouldFreezeWhileRunning(st);
    if (st->frozenActive && !freezeWhileRunning) {
        st->frozenActive = 0;
        source_pane_freeFrozenAsm(st);
    }
    if (freezeWhileRunning && !st->frozenActive) {
        unsigned long pcAddr = 0;
        (void)machine_findReg(&debugger.machine, "PC", &pcAddr);
        pcAddr &= 0x00fffffful;
        pcAddr &= ~1ul;
        st->frozenPcAddr = (uint64_t)pcAddr;
        st->frozenActive = 1;
        st->frozenAsmStartIndex = INT_MIN;
        st->frozenAsmMaxLines = 0;
        source_pane_freeFrozenAsm(st);
    }

    uint64_t curAddr = 0;
    if (freezeWhileRunning) {
        curAddr = st->frozenPcAddr;
    } else {
        unsigned long pcAddr = 0;
        (void)machine_findReg(&debugger.machine, "PC", &pcAddr);
        pcAddr &= 0x00fffffful;
        pcAddr &= ~1ul;
        curAddr = (uint64_t)pcAddr;
    }
    *outCurAddr = curAddr;

    int startIndex = 0;
    uint64_t windowAnchorAddr = curAddr;
    if (st->scrollLocked) {
        if (st->scrollAnchorValid) {
            windowAnchorAddr = st->scrollAnchorAddr;
            int anchorIndex = 0;
            if (dasm_findIndexForAddr(st->scrollAnchorAddr, &anchorIndex)) {
                startIndex = anchorIndex;
            } else {
                startIndex = st->scrollIndex;
            }
        } else {
            startIndex = st->scrollIndex;
        }
    } else {
        int curIndex = 0;
        if (!dasm_findIndexForAddr(curAddr, &curIndex) && !streaming) {
            curIndex = 0;
        }
        startIndex = curIndex - (maxLines / 2);
    }
    if (startIndex < 0 && !streaming) {
        startIndex = 0;
    }
    if (!streaming && startIndex >= total) {
        startIndex = total - 1;
    }
    if (freezeWhileRunning && !st->scrollLocked) {
        st->scrollIndex = startIndex;
    }
    int endIndex = startIndex + maxLines - 1;
    if (!streaming && endIndex >= total) {
        endIndex = total - 1;
    }

    const char **lines = NULL;
    const uint64_t *addrs = NULL;
    int first = 0;
    int count = 0;
    if (freezeWhileRunning && st->frozenActive && st->frozenAsmLines &&
        st->frozenAsmAnchorAddr == windowAnchorAddr &&
        st->frozenAsmStartIndex == startIndex &&
        st->frozenAsmMaxLines == maxLines) {
        lines = (const char **)st->frozenAsmLines;
        addrs = (const uint64_t *)st->frozenAsmAddrs;
        first = st->frozenAsmStartIndex;
        count = st->frozenAsmCount;
    } else {
        if (freezeWhileRunning) {
            if (!st->scrollLocked) {
                int dummy = 0;
                (void)dasm_findIndexForAddr(curAddr, &dummy);
            }
        } else {
            source_pane_trackPosition(st);
        }
        if (!dasm_getRangeByIndex(startIndex, endIndex, &lines, &addrs, &first, &count)) {
            return 0;
        }

        if (!streaming && count < maxLines && total > 0) {
            int missing = maxLines - count;
            int altStart = first - missing;
            if (altStart < 0) {
                altStart = 0;
            }
            int altEnd = altStart + maxLines - 1;
            if (altEnd >= total) {
                altEnd = total - 1;
            }
            dasm_getRangeByIndex(altStart, altEnd, &lines, &addrs, &first, &count);
        }

        if (!st->scrollLocked) {
            st->scrollIndex = first;
        } else if (!st->scrollAnchorValid && count > 0) {
            st->scrollAnchorAddr = addrs[0];
            st->scrollAnchorValid = 1;
        }

        if (freezeWhileRunning && st->frozenActive && (st->frozenAsmStartIndex != first ||
            st->frozenAsmCount != count ||
            st->frozenAsmMaxLines != maxLines ||
            st->frozenAsmAnchorAddr != windowAnchorAddr)) {
            source_pane_freeFrozenAsm(st);
            st->frozenAsmLines = (char **)alloc_calloc((size_t)count, sizeof(*st->frozenAsmLines));
            st->frozenAsmAddrs = (uint64_t *)alloc_calloc((size_t)count, sizeof(*st->frozenAsmAddrs));
            if (st->frozenAsmLines && st->frozenAsmAddrs) {
                for (int i = 0; i < count; ++i) {
                    st->frozenAsmLines[i] = alloc_strdup(lines[i] ? lines[i] : "");
                    st->frozenAsmAddrs[i] = addrs[i];
                }
                st->frozenAsmCount = count;
                st->frozenAsmStartIndex = first;
                st->frozenAsmMaxLines = maxLines;
                st->frozenAsmAnchorAddr = windowAnchorAddr;
                lines = (const char **)st->frozenAsmLines;
                addrs = (const uint64_t *)st->frozenAsmAddrs;
                first = st->frozenAsmStartIndex;
                count = st->frozenAsmCount;
            }
        }
    }

    *outLines = lines;
    *outAddrs = addrs;
    *outCount = count;
    (void)first;
    return 1;
}

static int
source_pane_asm_view_findCurrentAddrRow(const uint64_t *addrs, int count, uint64_t curAddr)
{
    if (!addrs || count <= 0) {
        return -1;
    }
    for (int i = 0; i < count; ++i) {
        if (addrs[i] == curAddr) {
            return i;
        }
    }
    int best = -1;
    for (int i = 0; i < count; ++i) {
        if (addrs[i] <= curAddr) {
            best = i;
        } else {
            break;
        }
    }
    if (best < 0 || best + 1 >= count) {
        return -1;
    }
    if (curAddr < addrs[best + 1]) {
        return best;
    }
    return -1;
}

static int
source_pane_asm_view_parseFunctionValue(const char *value, int *outLine, const char **outFile)
{
    if (!value || !*value || !outLine) {
        return 0;
    }
    const char *sep = strchr(value, '|');
    if (!sep) {
        return 0;
    }
    size_t len = (size_t)(sep - value);
    if (len == 0 || len > 15) {
        return 0;
    }
    char lineBuf[16];
    memcpy(lineBuf, value, len);
    lineBuf[len] = '\0';
    int line = (int)strtol(lineBuf, NULL, 10);
    if (line <= 0) {
        return 0;
    }
    *outLine = line;
    if (outFile) {
        const char *file = sep + 1;
        *outFile = strchr(file, '|') ? file : NULL;
    }
    return 1;
}

static void
source_pane_asm_view_formatInlineHexBytes(char *dst, size_t cap, const uint8_t *bytes, int count)
{
    size_t pos = 0;

    if (!dst || cap == 0) {
        return;
    }
    dst[0] = '\0';
    if (!bytes || count <= 0) {
        return;
    }

    for (int i = 0; i < count && pos + 1 < cap; ++i) {
        int written = snprintf(dst + pos, cap - pos, i == 0 ? "%02X" : " %02X", (unsigned)bytes[i]);
        if (written < 0) {
            break;
        }
        pos += (size_t)written;
        if (pos >= cap) {
            dst[cap - 1] = '\0';
            break;
        }
    }
}

static int
source_pane_asm_view_fontColumnWidth(TTF_Font *font)
{
    int w = 0;

    if (!font) {
        return 0;
    }
    if (TTF_SizeText(font, "0", &w, NULL) != 0 || w <= 0) {
        return 0;
    }
    return w;
}

static int
source_pane_asm_view_getAddrHexWidth(const source_pane_state_t *st)
{
    if (st && st->viewMode == source_pane_mode_z80) {
        return 4;
    }

    int hexw = dasm_getAddrHexWidth();
    if (hexw < 6) {
        hexw = 6;
    }
    if (hexw > 16) {
        hexw = 16;
    }
    return hexw;
}

static int
source_pane_asm_view_debugReadBytes(const source_pane_state_t *st, uint32_t addr, uint8_t *out, size_t size)
{
    if (!out || size == 0) {
        return 0;
    }
    if (st && st->viewMode == source_pane_mode_z80) {
        return libretro_host_debugReadProcessorMemory(source_z80_processorId(), addr, out, size) ? 1 : 0;
    }
    return libretro_host_debugReadMemory(addr, out, size) ? 1 : 0;
}

static int
source_pane_asm_view_debugDisassembleQuick(const source_pane_state_t *st,
                                           uint32_t addr,
                                           char *out,
                                           size_t cap,
                                           size_t *outLen)
{
    if (st && st->viewMode == source_pane_mode_z80) {
        return libretro_host_debugDisassembleProcessorQuick(source_z80_processorId(), addr, out, cap, outLen) ? 1 : 0;
    }
    return libretro_host_debugDisassembleQuick(addr, out, cap, outLen) ? 1 : 0;
}

static void
source_pane_asm_view_drawHexByteColors(e9ui_context_t *ctx,
                                       e9ui_component_t *self,
                                       TTF_Font *font,
                                       const char *hexbuf,
                                       const uint8_t *bytes,
                                       size_t byteCount,
                                       int x,
                                       int y,
                                       int lineHeight,
                                       int columnWidth)
{
    size_t hexLen = 0;

    if (!hex_byte_color_isEnabled() ||
        !ctx || !self || !font || !hexbuf || !bytes || byteCount == 0) {
        return;
    }
    (void)self;
    (void)lineHeight;
    if (columnWidth <= 0) {
        return;
    }
    hexLen = strlen(hexbuf);

    for (size_t i = 0; i < byteCount; ++i) {
        int start = (int)i * 3;
        if ((size_t)start + 2u > hexLen) {
            break;
        }
        hex_byte_color_drawHexByte(ctx->renderer, font, bytes[i], x + start * columnWidth, y);
    }
}

int
source_pane_asm_view_isAsmLikeMode(source_pane_mode_t mode)
{
    return (mode == source_pane_mode_a ||
            mode == source_pane_mode_sym ||
            mode == source_pane_mode_h ||
            mode == source_pane_mode_cpr ||
            mode == source_pane_mode_z80) ? 1 : 0;
}

int
source_pane_asm_view_isCpuAsmLikeMode(source_pane_mode_t mode)
{
    return (mode == source_pane_mode_a ||
            mode == source_pane_mode_sym ||
            mode == source_pane_mode_h ||
            mode == source_pane_mode_z80) ? 1 : 0;
}

static int
source_pane_asm_view_countSymbolLabelsForAddr(const source_pane_state_t *st, uint64_t addr)
{
    if (!st || st->viewMode != source_pane_mode_sym) {
        return 0;
    }
    if (!st->asmSymbolNames || !st->asmSymbolAddrs || st->asmSymbolCount <= 0) {
        return 0;
    }

    int count = 0;
    uint64_t addr24 = addr & 0x00ffffffull;
    for (int i = 0; i < st->asmSymbolCount; ++i) {
        const char *name = st->asmSymbolNames[i];
        uint64_t symbolAddr = st->asmSymbolAddrs[i] & 0x00ffffffull;
        if (!name || !name[0] || symbolAddr != addr24) {
            continue;
        }
        count++;
    }
    return count;
}

static int
source_pane_asm_view_resolveGutterRowAddr(const source_pane_state_t *st,
                                          const uint64_t *addrs,
                                          int count,
                                          int row,
                                          uint32_t *outAddr)
{
    if (!st || !addrs || count <= 0 || row < 0 || !outAddr) {
        return 0;
    }

    int visualRow = 0;
    for (int i = 0; i < count; ++i) {
        int labelCount = source_pane_asm_view_countSymbolLabelsForAddr(st, addrs[i]);
        if (row < visualRow + labelCount + 1) {
            *outAddr = (uint32_t)(addrs[i] & 0x00ffffffu);
            return 1;
        }
        visualRow += labelCount + 1;
    }
    return 0;
}

static int
source_pane_asm_view_renderSymbolLabels(e9ui_context_t *ctx,
                                        e9ui_component_t *self,
                                        TTF_Font *font,
                                        source_pane_state_t *st,
                                        uint64_t addr,
                                        int labelX,
                                        int *ioY,
                                        int lineHeight,
                                        int hitW,
                                        int clipBottom)
{
    if (!ctx || !self || !font || !st || !ioY || st->viewMode != source_pane_mode_sym) {
        return 0;
    }
    if (!st->asmSymbolNames || !st->asmSymbolAddrs || st->asmSymbolCount <= 0) {
        return 0;
    }

    int added = 0;
    SDL_Color labelColor = (SDL_Color){196, 172, 108, 255};
    void *sourceBucket = (void *)&st->bucketSource;
    uint64_t addr24 = addr & 0x00ffffffull;
    for (int i = 0; i < st->asmSymbolCount; ++i) {
        const char *name = st->asmSymbolNames[i];
        uint64_t symbolAddr = st->asmSymbolAddrs[i] & 0x00ffffffull;
        if (!name || !name[0] || symbolAddr != addr24) {
            continue;
        }

        char label[1024];
        int written = snprintf(label, sizeof(label), "%s:", name);
        if (written < 0 || (size_t)written >= sizeof(label)) {
            continue;
        }

        source_pane_renderAsmLineHighlighted(ctx,
                                             self,
                                             font,
                                             label,
                                             0,
                                             labelColor,
                                             labelX,
                                             *ioY,
                                             lineHeight,
                                             hitW,
                                             sourceBucket);
        *ioY += lineHeight;
        added++;
        if (*ioY > clipBottom) {
            break;
        }
    }
    return added;
}

int
source_pane_asm_view_shouldFreezeWhileRunning(const source_pane_state_t *st)
{
    if (!st) {
        return 0;
    }
    if (st->viewMode == source_pane_mode_cpr) {
        return 0;
    }
    if (source_pane_asm_view_isLiveUpdateEnabled()) {
        return 0;
    }
    if (st->overrideActive) {
        return 0;
    }
    return machine_getRunning(debugger.machine) ? 1 : 0;
}

int
source_pane_asm_view_areStepButtonsEnabled(const source_pane_state_t *st)
{
    if (!st || !source_pane_asm_view_isAsmLikeMode(st->viewMode)) {
        return 0;
    }
    if (st->viewMode == source_pane_mode_z80) {
        return 0;
    }
    return 1;
}

int
source_pane_asm_view_beginGutterPress(e9ui_component_t *self, e9ui_context_t *ctx,
                                      source_pane_state_t *st, source_pane_mode_t mode,
                                      int mx, int my)
{
    if (!self || !ctx || !st || !source_pane_asm_view_isCpuAsmLikeMode(mode)) {
        return 0;
    }

    TTF_Font *useFont = source_pane_resolveFont(ctx);
    if (!useFont) {
        return 0;
    }

    const int padPx = 10;
    source_pane_line_metrics_t metrics = source_pane_computeLineMetrics(self, ctx, useFont, padPx);
    if (metrics.innerHeight <= 0) {
        return 0;
    }
    int maxLines = metrics.maxLines > 0 ? metrics.maxLines : 1;
    int drawMaxLines = maxLines + 1;
    uint64_t curAddr = 0;
    const char **lines = NULL;
    const uint64_t *addrs = NULL;
    int count = 0;
    if (!source_pane_asm_view_getAsmWindow(st, drawMaxLines, &curAddr, &lines, &addrs, &count)) {
        return 0;
    }
    (void)curAddr;
    (void)lines;

    int hexw = source_pane_asm_view_getAddrHexWidth(st);
    char sample[32];
    for (int i = 0; i < hexw; ++i) {
        sample[i] = 'F';
    }
    sample[hexw] = '\0';
    int gutterW = 0;
    int th = 0;
    TTF_SizeText(useFont, sample, &gutterW, &th);
    int gutterPad = e9ui_scale_px(ctx, 16);
    SDL_Rect contentArea = source_pane_getContentArea(self, ctx, padPx);
    int gutterRight = contentArea.x + padPx + gutterW + gutterPad;
    if (mx >= gutterRight) {
        return 0;
    }
    int row = (my - (contentArea.y + padPx)) / metrics.lineHeight;
    uint32_t gutterAddr = 0;
    if (!source_pane_asm_view_resolveGutterRowAddr(st, addrs, count, row, &gutterAddr)) {
        return 0;
    }
    st->gutterPending = 1;
    st->gutterMode = mode;
    st->gutterAddr = gutterAddr;
    st->gutterDownX = mx;
    st->gutterDownY = my;
    return 1;
}

void
source_pane_asm_view_renderAsm(e9ui_component_t *self, e9ui_context_t *ctx)
{
    TTF_Font *useFont = source_pane_resolveFont(ctx);
    SDL_Rect area = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    const int padPx = 10;
    SDL_Rect contentArea = source_pane_getContentArea(self, ctx, padPx);
    SDL_SetRenderDrawColor(ctx->renderer, 20, 20, 24, 255);
    SDL_RenderFillRect(ctx->renderer, &area);
    if (!useFont) {
        return;
    }

    source_pane_state_t *st = (source_pane_state_t *)self->state;
    source_pane_line_metrics_t metrics = source_pane_computeLineMetrics(self, ctx, useFont, padPx);
    if (metrics.innerHeight <= 0) {
        return;
    }
    int maxLines = metrics.maxLines;
    if (maxLines <= 0) {
        maxLines = 1;
    }
    int drawMaxLines = maxLines + 1;

    const char **lines = NULL;
    const uint64_t *addrs = NULL;
    int count = 0;
    uint64_t curAddr = 0;
    if (!source_pane_asm_view_getAsmWindow(st, drawMaxLines, &curAddr, &lines, &addrs, &count)) {
        SDL_Color icol = (SDL_Color){200, 160, 160, 255};
        source_pane_renderStatusMessage(ctx, useFont, contentArea, padPx, icol, "No disassembly available");
        return;
    }
    int curRow = source_pane_asm_view_findCurrentAddrRow(addrs, count, curAddr);

    int hexw = source_pane_asm_view_getAddrHexWidth(st);
    char sample[32];
    for (int i = 0; i < hexw; ++i) {
        sample[i] = 'F';
    }
    sample[hexw] = '\0';
    int gutterW = 0;
    int th = 0;
    TTF_SizeText(useFont, sample, &gutterW, &th);
    int gutterPad = e9ui_scale_px(ctx, 16);
    SDL_SetRenderDrawColor(ctx->renderer, 26, 26, 30, 255);
    SDL_Rect gutter = { contentArea.x, contentArea.y, padPx + gutterW + gutterPad, contentArea.h };
    SDL_RenderFillRect(ctx->renderer, &gutter);

    SDL_Color txt = (SDL_Color){220, 220, 220, 255};
    SDL_Color lno = (SDL_Color){160, 160, 200, 255};
    SDL_Color lnoBpOn = (SDL_Color){120, 200, 120, 255};
    SDL_Color lnoBpOff = (SDL_Color){200, 140, 60, 255};
    int textX = contentArea.x + padPx + gutterW + gutterPad;
    int labelX = contentArea.x + padPx;
    int hitW = contentArea.x + contentArea.w - textX - padPx;
    int labelHitW = contentArea.x + contentArea.w - labelX - padPx;
    if (hitW < 0) {
        hitW = 0;
    }
    if (labelHitW < 0) {
        labelHitW = 0;
    }
    int y = contentArea.y + padPx;
    int clipBottom = contentArea.y + contentArea.h + metrics.lineHeight;
    for (int i = 0; i < count; ++i) {
        uint64_t a = addrs[i];
        const char *ins = lines[i] ? lines[i] : "";
        (void)source_pane_asm_view_renderSymbolLabels(ctx,
                                                      self,
                                                      useFont,
                                                      st,
                                                      a,
                                                      labelX,
                                                      &y,
                                                      metrics.lineHeight,
                                                      labelHitW,
                                                      clipBottom);
        if (y > clipBottom) {
            break;
        }
        if (i == curRow) {
            SDL_SetRenderDrawColor(ctx->renderer, 40, 72, 138, 255);
            SDL_Rect hl = { area.x + 2, y - 2, area.w - 4, metrics.lineHeight + 4 };
            SDL_RenderFillRect(ctx->renderer, &hl);
        }
        char abuf[32];
        snprintf(abuf, sizeof(abuf), "%0*llX", hexw, (unsigned long long)a);
        int nw = 0;
        int nh = 0;
        TTF_SizeText(useFont, abuf, &nw, &nh);
        int lnx = contentArea.x + padPx + (gutterW - nw);
        SDL_Color useCol = lno;
        uint32_t processorId = st && st->viewMode == source_pane_mode_z80
            ? source_z80_processorId()
            : MACHINE_PROCESSOR_PRIMARY;
        machine_breakpoint_t *bp = machine_findProcessorBreakpointByAddr(&debugger.machine,
                                                                          processorId,
                                                                          (uint32_t)a);
        if (bp) {
            useCol = bp->enabled ? lnoBpOn : lnoBpOff;
        }
        void *addrBucket = st ? (void *)&st->bucketAddr : (void *)self;
        void *sourceBucket = st ? (void *)&st->bucketSource : (void *)self;
        e9ui_text_select_drawText(ctx, self, useFont, abuf, useCol, lnx, y,
                                  metrics.lineHeight, 0, addrBucket, 1, 1);
        source_pane_renderAsmLineHighlighted(ctx, self, useFont, ins, 0, txt, textX, y,
                                             metrics.lineHeight, hitW, sourceBucket);
        y += metrics.lineHeight;
        if (y > clipBottom) {
            break;
        }
    }
}

int
source_pane_asm_view_beginInlineHexEditAtPoint(e9ui_component_t *self, e9ui_context_t *ctx,
                                                source_pane_state_t *st, int mx, int my)
{
    TTF_Font *useFont = NULL;
    const int padPx = 10;
    source_pane_line_metrics_t metrics;
    int maxLines = 1;
    int drawMaxLines = 0;
    const char **lines = NULL;
    const uint64_t *addrs = NULL;
    int count = 0;
    uint64_t curAddr = 0;
    int hexw = 0;
    char sample[32];
    int gutterW = 0;
    int th = 0;
    int gutterPad = 0;
    SDL_Rect contentArea;
    int textX = 0;
    int y = 0;

    if (!self || !ctx || !st) {
        return 0;
    }

    useFont = source_pane_resolveFont(ctx);
    if (!useFont) {
        return 0;
    }

    metrics = source_pane_computeLineMetrics(self, ctx, useFont, padPx);
    if (metrics.innerHeight <= 0) {
        return 0;
    }
    maxLines = metrics.maxLines > 0 ? metrics.maxLines : 1;
    drawMaxLines = maxLines + 1;
    if (!source_pane_asm_view_getAsmWindow(st, drawMaxLines, &curAddr, &lines, &addrs, &count)) {
        return 0;
    }
    (void)curAddr;
    (void)lines;

    hexw = source_pane_asm_view_getAddrHexWidth(st);
    for (int i = 0; i < hexw; ++i) {
        sample[i] = 'F';
    }
    sample[hexw] = '\0';
    TTF_SizeText(useFont, sample, &gutterW, &th);
    gutterPad = e9ui_scale_px(ctx, 16);
    contentArea = source_pane_getContentArea(self, ctx, padPx);
    textX = contentArea.x + padPx + gutterW + gutterPad;
    y = contentArea.y + padPx;

    for (int i = 0; i < count; ++i) {
        uint64_t a = addrs[i];
        size_t wantBytes = 2;
        uint8_t bytes[16];
        char editText[64];
        int fieldW = 0;
        SDL_Rect rect;

        if (i + 1 < count) {
            uint64_t diff = addrs[i + 1] - addrs[i];
            if (diff > 0 && diff <= 64) {
                wantBytes = (size_t)diff;
            }
        } else {
            char tmp[64];
            size_t len = 0;
            if (source_pane_asm_view_debugDisassembleQuick(st, (uint32_t)a, tmp, sizeof(tmp), &len) &&
                len > 0 && len <= 64) {
                wantBytes = len;
            }
        }
        if (wantBytes > 16) {
            wantBytes = 16;
        }
        if (!source_pane_asm_view_debugReadBytes(st, (uint32_t)a, bytes, wantBytes)) {
            y += metrics.lineHeight;
            continue;
        }

        source_pane_asm_view_formatInlineHexBytes(editText, sizeof(editText), bytes, (int)wantBytes);
        fieldW = source_pane_measureSegment(useFont, editText, 0, (int)strlen(editText));
        rect = (SDL_Rect){
            textX - e9ui_scale_px(ctx, 4),
            y - e9ui_scale_px(ctx, 2),
            fieldW + e9ui_scale_px(ctx, 12),
            metrics.lineHeight + e9ui_scale_px(ctx, 4)
        };
        if (mx >= rect.x && mx < rect.x + rect.w &&
            my >= rect.y && my < rect.y + rect.h) {
            if (source_pane_beginInlineEdit(st,
                                            ctx,
                                            st->viewMode == source_pane_mode_z80 ? source_pane_mode_z80 : source_pane_mode_h,
                                            source_pane_inline_edit_hex_bytes,
                                            (uint32_t)a,
                                            (int)wantBytes,
                                            0,
                                            0,
                                            editText,
                                            rect,
                                            source_pane_dataEditCursorForPoint(useFont,
                                                                               editText,
                                                                               e9ui_data_edit_mode_hex_bytes,
                                                                               textX,
                                                                               mx))) {
                return 1;
            }
            return 0;
        }
        y += metrics.lineHeight;
    }

    return 0;
}

void
source_pane_asm_view_renderHex(e9ui_component_t *self, e9ui_context_t *ctx)
{
    TTF_Font *useFont = source_pane_resolveFont(ctx);
    SDL_Rect area = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_Rect contentArea = source_pane_getContentArea(self, ctx, 10);
    const int padPx = 10;
    SDL_SetRenderDrawColor(ctx->renderer, 20, 20, 24, 255);
    SDL_RenderFillRect(ctx->renderer, &area);
    if (!useFont) {
        return;
    }

    source_pane_state_t *st = (source_pane_state_t *)self->state;
    source_pane_line_metrics_t metrics = source_pane_computeLineMetrics(self, ctx, useFont, padPx);
    if (metrics.innerHeight <= 0) {
        return;
    }
    int maxLines = metrics.maxLines;
    if (maxLines <= 0) {
        maxLines = 1;
    }
    int drawMaxLines = maxLines + 1;

    const char **lines = NULL;
    const uint64_t *addrs = NULL;
    int count = 0;
    uint64_t curAddr = 0;
    if (!source_pane_asm_view_getAsmWindow(st, drawMaxLines, &curAddr, &lines, &addrs, &count)) {
        SDL_Color icol = (SDL_Color){200, 160, 160, 255};
        source_pane_renderStatusMessage(ctx, useFont, contentArea, padPx, icol, "No disassembly available");
        return;
    }
    int curRow = source_pane_asm_view_findCurrentAddrRow(addrs, count, curAddr);

    int hexw = source_pane_asm_view_getAddrHexWidth(st);
    char sample[32];
    for (int i = 0; i < hexw; ++i) {
        sample[i] = 'F';
    }
    sample[hexw] = '\0';
    int gutterW = 0;
    int th = 0;
    TTF_SizeText(useFont, sample, &gutterW, &th);
    int gutterPad = e9ui_scale_px(ctx, 16);
    SDL_SetRenderDrawColor(ctx->renderer, 26, 26, 30, 255);
    SDL_Rect gutter = { contentArea.x, contentArea.y, padPx + gutterW + gutterPad, contentArea.h };
    SDL_RenderFillRect(ctx->renderer, &gutter);

    SDL_Color txt = (SDL_Color){220, 220, 220, 255};
    SDL_Color lno = (SDL_Color){160, 160, 200, 255};
    SDL_Color lnoBpOn = (SDL_Color){120, 200, 120, 255};
    SDL_Color lnoBpOff = (SDL_Color){200, 140, 60, 255};
    int textX = contentArea.x + padPx + gutterW + gutterPad;
    int columnWidth = source_pane_asm_view_fontColumnWidth(useFont);
    int hitW = contentArea.x + contentArea.w - textX - padPx;
    if (hitW < 0) {
        hitW = 0;
    }

    int y = contentArea.y + padPx;
    int clipBottom = contentArea.y + contentArea.h + metrics.lineHeight;
    for (int i = 0; i < count; ++i) {
        uint64_t a = addrs[i];
        const char *ins = lines[i] ? lines[i] : "";
        if (i == curRow) {
            SDL_SetRenderDrawColor(ctx->renderer, 40, 72, 138, 255);
            SDL_Rect hl = { area.x + 2, y - 2, area.w - 4, metrics.lineHeight + 4 };
            SDL_RenderFillRect(ctx->renderer, &hl);
        }

        char abuf[32];
        snprintf(abuf, sizeof(abuf), "%0*llX", hexw, (unsigned long long)a);
        int nw = 0;
        int nh = 0;
        TTF_SizeText(useFont, abuf, &nw, &nh);
        int lnx = contentArea.x + padPx + (gutterW - nw);
        SDL_Color useCol = lno;
        uint32_t processorId = st && st->viewMode == source_pane_mode_z80
            ? source_z80_processorId()
            : MACHINE_PROCESSOR_PRIMARY;
        machine_breakpoint_t *bp = machine_findProcessorBreakpointByAddr(&debugger.machine,
                                                                          processorId,
                                                                          (uint32_t)a);
        if (bp) {
            useCol = bp->enabled ? lnoBpOn : lnoBpOff;
        }

        size_t wantBytes = 2;
        if (i + 1 < count) {
            uint64_t diff = addrs[i + 1] - addrs[i];
            if (diff > 0 && diff <= 64) {
                wantBytes = (size_t)diff;
            }
        } else {
            char tmp[64];
            size_t len = 0;
            if (source_pane_asm_view_debugDisassembleQuick(st, (uint32_t)a, tmp, sizeof(tmp), &len) &&
                len > 0 && len <= 64) {
                wantBytes = len;
            }
        }
        if (wantBytes > 16) {
            wantBytes = 16;
        }

        uint8_t bytes[16];
        memset(bytes, 0, sizeof(bytes));
        int gotBytes = source_pane_asm_view_debugReadBytes(st, (uint32_t)a, bytes, wantBytes) ? 1 : 0;

        const int padBytes = 12;
        char hexbuf[padBytes * 3 + 1];
        size_t pos = 0;
        for (size_t b = 0; b < (size_t)padBytes; ++b) {
            if (b < wantBytes && gotBytes) {
                pos += (size_t)snprintf(hexbuf + pos, sizeof(hexbuf) - pos, "%02X ", (unsigned)bytes[b]);
            } else {
                pos += (size_t)snprintf(hexbuf + pos, sizeof(hexbuf) - pos, "   ");
            }
            if (pos >= sizeof(hexbuf)) {
                break;
            }
        }
        hexbuf[sizeof(hexbuf) - 1] = '\0';

        char linebuf[512];
        snprintf(linebuf, sizeof(linebuf), "%s%s", hexbuf, ins);
        linebuf[sizeof(linebuf) - 1] = '\0';

        void *addrBucket = st ? (void *)&st->bucketAddr : (void *)self;
        void *sourceBucket = st ? (void *)&st->bucketSource : (void *)self;
        e9ui_text_select_drawText(ctx, self, useFont, abuf, useCol, lnx, y,
                                  metrics.lineHeight, 0, addrBucket, 1, 1);
        source_pane_renderAsmLineHighlighted(ctx, self, useFont, linebuf, (int)strlen(hexbuf), txt,
                                             textX, y, metrics.lineHeight, hitW, sourceBucket);
        if (gotBytes) {
            size_t colorBytes = wantBytes < (size_t)padBytes ? wantBytes : (size_t)padBytes;
            source_pane_asm_view_drawHexByteColors(ctx,
                                                   self,
                                                   useFont,
                                                   hexbuf,
                                                   bytes,
                                                   colorBytes,
                                                   textX,
                                                   y,
                                                   metrics.lineHeight,
                                                   columnWidth);
        }

        y += metrics.lineHeight;
        if (y > clipBottom) {
            break;
        }
    }
}

void
source_pane_asm_view_symbolSelectChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *value, void *user)
{
    (void)ctx;
    (void)comp;
    source_pane_state_t *st = (source_pane_state_t *)user;
    if (!st || !value || !value[0]) {
        return;
    }
    uint64_t addr = 0;
    if (!source_pane_parseHex64(value, &addr) || addr == 0) {
        int line = 0;
        const char *selectedFile = NULL;
        if (!source_pane_asm_view_parseFunctionValue(value, &line, &selectedFile)) {
            return;
        }
        if (!selectedFile || !selectedFile[0] || line <= 0) {
            return;
        }
        const char *end = strchr(selectedFile, '|');
        if (!end) {
            return;
        }
        size_t len = (size_t)(end - selectedFile);
        if (len == 0 || len >= PATH_MAX) {
            return;
        }
        char filePath[PATH_MAX];
        memcpy(filePath, selectedFile, len);
        filePath[len] = '\0';
        uint32_t runtime = 0;
        if (!source_pane_fileline_resolveFileLine(debugger.libretro.exePath, filePath, line, &runtime)) {
            return;
        }
        (void)base_map_debugToRuntime(BASE_MAP_SECTION_TEXT, runtime, &runtime);
        addr = (uint64_t)runtime;
    }
    uint64_t resolved = source_pane_asm_view_resolveAsmLikeAnchorAddr(st, addr);
    if (st->asmSymbolValues && st->asmSymbolAddrs) {
        for (int i = 0; i < st->asmSymbolCount; ++i) {
            const char *v = st->asmSymbolValues[i];
            if (v && strcmp(v, value) == 0) {
                st->asmSymbolAddrs[i] = resolved & 0x00ffffffull;
                break;
            }
        }
    }
    if (st->ownerPane && st->asmAddressMeta) {
        e9ui_component_t *addrBox = e9ui_child_find(st->ownerPane, st->asmAddressMeta);
        if (addrBox) {
            int hexw = source_pane_asm_view_getAddrHexWidth(st);
            char buf[32];
            snprintf(buf, sizeof(buf), "%0*llX", hexw, (unsigned long long)(resolved & 0x00ffffffull));
            e9ui_textbox_setText(addrBox, buf);
        }
    }
    source_pane_asm_view_setAsmAnchorLocked(st, resolved);
}

void
source_pane_asm_view_addressSubmitted(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    source_pane_state_t *st = (source_pane_state_t *)user;
    if (!st || !st->ownerPane || !st->asmAddressMeta) {
        return;
    }
    e9ui_component_t *addrBox = e9ui_child_find(st->ownerPane, st->asmAddressMeta);
    if (!addrBox) {
        return;
    }
    const char *text = e9ui_textbox_getText(addrBox);
    uint64_t addr = 0;
    if (!source_pane_parseHex64(text, &addr)) {
        return;
    }
    uint64_t resolved = source_pane_asm_view_resolveAsmLikeAnchorAddr(st, addr);
    int hexw = source_pane_asm_view_getAddrHexWidth(st);
    char buf[32];
    snprintf(buf, sizeof(buf), "%0*llX", hexw, (unsigned long long)(resolved & 0x00ffffffull));
    e9ui_textbox_setText(addrBox, buf);

    if (st->asmSymbolSelectMeta) {
        e9ui_component_t *symbolSelect = e9ui_child_find(st->ownerPane, st->asmSymbolSelectMeta);
        if (symbolSelect) {
            const char *matchValue = NULL;
            for (int i = 0; i < st->asmSymbolCount; ++i) {
                const char *value = st->asmSymbolValues ? st->asmSymbolValues[i] : NULL;
                uint64_t symbolAddr = st->asmSymbolAddrs ? (st->asmSymbolAddrs[i] & 0x00ffffffull) : 0ull;
                if (!value || !value[0] || symbolAddr == 0) {
                    continue;
                }
                symbolAddr = source_pane_asm_view_resolveAsmLikeAnchorAddr(st, symbolAddr);
                if ((symbolAddr & 0x00ffffffull) == (resolved & 0x00ffffffull)) {
                    matchValue = value;
                    break;
                }
            }
            if (matchValue) {
                e9ui_textbox_setSelectedValue(symbolSelect, matchValue);
            } else {
                e9ui_textbox_setOptions(symbolSelect, NULL, 0);
                e9ui_textbox_setText(symbolSelect, "");
                if (st->asmSymbolOptions && st->asmSymbolCount > 0) {
                    e9ui_textbox_setOptions(symbolSelect, st->asmSymbolOptions, st->asmSymbolCount);
                }
            }
        }
    }

    source_pane_asm_view_setAsmAnchorLocked(st, resolved);
}
