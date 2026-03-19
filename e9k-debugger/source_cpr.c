/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <stdarg.h>

#include "debugger.h"
#include "source_cpr.h"
#include "dasm.h"
#include "libretro_host.h"
#include "debug.h"
#include "strutil.h"
#include "amiga_custom_regs.h"

static int
source_cpr_getCopperListAddr(const char *highName, const char *lowName, uint64_t *outAddr)
{
    unsigned long highValue = 0;
    unsigned long lowValue = 0;

    if (!highName || !lowName || !outAddr) {
        return 0;
    }
    if (!machine_findReg(&debugger.machine, highName, &highValue) ||
        !machine_findReg(&debugger.machine, lowName, &lowValue)) {
        return 0;
    }

    *outAddr = (((uint64_t)highValue & 0x001full) << 16) |
               ((uint64_t)lowValue & 0x0000fffeull);
    *outAddr = source_cpr_resolveAnchorAddr(*outAddr);
    return 1;
}

static void
source_cpr_sanitizeValueTip(char *text)
{
    if (!text) {
        return;
    }
    for (char *p = text; *p; ++p) {
        if (*p == '\r' || *p == '\n' || *p == '\t') {
            *p = ' ';
        }
    }
}

static void
source_cpr_appendDecodeText(char *out, size_t cap, size_t *pos, const char *text)
{
    if (!out || cap == 0 || !pos || !text) {
        return;
    }
    while (*text && *pos + 1 < cap) {
        out[*pos] = *text;
        (*pos)++;
        text++;
    }
    out[*pos] = '\0';
}

static void
source_cpr_appendDecodeFormat(char *out, size_t cap, size_t *pos, const char *fmt, ...)
{
    va_list args;
    char buffer[256];

    if (!out || cap == 0 || !pos || !fmt || *pos >= cap - 1) {
        return;
    }

    va_start(args, fmt);
    int written = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (written < 0) {
        return;
    }

    source_cpr_appendDecodeText(out, cap, pos, buffer);
}

static void
source_cpr_appendWaitCore(uint32_t insn, char *out, size_t cap, size_t *pos)
{
    int vp = (int)((insn & 0xff000000u) >> 24);
    int hp = (int)((insn & 0x00fe0000u) >> 16);
    int ve = (int)((insn & 0x00007f00u) >> 8);
    int he = (int)(insn & 0x000000feu);
    int bfd = (int)((insn & 0x00008000u) >> 15);
    int vMask = vp & (ve | 0x80);
    int hMask = hp & he;
    int didOutput = 0;

    if (vMask > 0) {
        didOutput = 1;
        source_cpr_appendDecodeText(out, cap, pos, "vpos ");
        if (ve != 0x7fu) {
            source_cpr_appendDecodeFormat(out, cap, pos, "& 0x%02x ", ve);
        }
        source_cpr_appendDecodeFormat(out, cap, pos, ">= 0x%02x", vMask);
    }
    if (he > 0) {
        if (vMask > 0) {
            source_cpr_appendDecodeText(out, cap, pos, " and");
        }
        source_cpr_appendDecodeText(out, cap, pos, " hpos ");
        if (he != 0xfe) {
            source_cpr_appendDecodeFormat(out, cap, pos, "& 0x%02x ", he);
        }
        source_cpr_appendDecodeFormat(out, cap, pos, ">= 0x%02x", hMask);
    } else {
        if (didOutput) {
            source_cpr_appendDecodeText(out, cap, pos, ", ");
        }
        source_cpr_appendDecodeText(out, cap, pos, "ignore horizontal");
    }

    source_cpr_appendDecodeFormat(out,
                                  cap,
                                  pos,
                                  " ; VP %02x, VE %02x; HP %02x, HE %02x; BFD %d",
                                  vp,
                                  ve,
                                  hp,
                                  he,
                                  bfd);
}

static void
source_cpr_decodeInstruction(uint16_t w1, uint16_t w2, char *out, size_t cap)
{
    size_t pos = 0;

    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';

    uint32_t insn = ((uint32_t)w1 << 16) | (uint32_t)w2;
    uint32_t insnType = insn & 0x00010001u;
    if (insnType == 0x00010000u || insnType == 0x00010001u) {
        int waitsForBlitterIdle = (w2 & 0x8000u) == 0u;
        int isWaitForever = (w1 == 0xffffu && w2 == 0xfffeu) ? 1 : 0;

        if (isWaitForever) {
            strutil_strlcpy(out, cap, insnType == 0x00010001u ? "SKIP FOREVER" : "WAIT FOREVER");
            return;
        }

        if (insnType == 0x00010001u) {
            source_cpr_appendDecodeText(out, cap, &pos, "Skip if ");
        } else {
            source_cpr_appendDecodeText(out, cap, &pos, "WAIT for ");
        }
        source_cpr_appendWaitCore(insn, out, cap, &pos);
        if (waitsForBlitterIdle) {
            source_cpr_appendDecodeText(out, cap, &pos, " ; BLITTER MUST BE IDLE");
        }
        if (insnType == 0x00010000u && insn == 0xfffffffeu) {
            source_cpr_appendDecodeText(out, cap, &pos, " ; End of Copperlist");
        }
        return;
    }

    uint16_t regOffset = (uint16_t)(w1 & 0x01feu);
    const char *regName = amiga_custom_regs_nameForOffset(regOffset);
    if (!regName || !regName[0]) {
        (void)snprintf(out, cap, "%03X := %04X", (unsigned)regOffset, (unsigned)w2);
        return;
    }

    (void)snprintf(out, cap, "%s := %04X", regName, (unsigned)w2);

    const char *valueTip = amiga_custom_regs_valueTooltipForOffset(regOffset, w2);
    if (!valueTip || !valueTip[0]) {
        return;
    }

    char valueTipBuf[256];
    strutil_strlcpy(valueTipBuf, sizeof(valueTipBuf), valueTip);
    source_cpr_sanitizeValueTip(valueTipBuf);
    size_t len = strlen(out);
    if (len < cap) {
        (void)snprintf(out + len, cap - len, " ; %s", valueTipBuf);
    }
}

static int
source_cpr_caseInsensitiveEquals(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0' ? 1 : 0;
}

static int
source_cpr_resolveRegisterOffset(const char *name, uint16_t *outOffset)
{
    if (!name || !outOffset) {
        return 0;
    }
    for (uint16_t regOffset = 0; regOffset < 0x200u; regOffset += 2u) {
        const char *regName = amiga_custom_regs_nameForOffset(regOffset);
        if (!regName || !regName[0] || strcmp(regName, "RESERVED") == 0) {
            continue;
        }
        if (source_cpr_caseInsensitiveEquals(name, regName)) {
            *outOffset = regOffset;
            return 1;
        }
    }
    return 0;
}

static void
source_cpr_formatInlineWords(char *dst, size_t cap, uint16_t w1, uint16_t w2)
{
    if (!dst || cap == 0) {
        return;
    }
    (void)snprintf(dst, cap, "%04X %04X", (unsigned)w1, (unsigned)w2);
}

int
source_cpr_isModeAvailable(void)
{
    return target == target_amiga() ? 1 : 0;
}

uint64_t
source_cpr_resolveAnchorAddr(uint64_t addr)
{
    uint64_t a = addr & 0x00ffffffull;
    a &= ~3ull;
    return a;
}

uint64_t
source_cpr_getCurrentAddr(source_pane_state_t *st)
{
    uint64_t addr = 0;

    if (st && st->overrideActive) {
        return source_cpr_resolveAnchorAddr(st->overrideAddr);
    }
    if (source_cpr_getCopperListAddr("COP1LCH", "COP1LCL", &addr)) {
        return addr;
    }
    if (source_cpr_getCopperListAddr("COP2LCH", "COP2LCL", &addr)) {
        return addr;
    }
    if (st && st->scrollAnchorValid) {
        return source_cpr_resolveAnchorAddr(st->scrollAnchorAddr);
    }
    return 0;
}

int
source_cpr_getWindow(source_pane_state_t *st, int maxLines, uint64_t *outCurAddr,
                     const char ***outLines, const uint64_t **outAddrs, int *outCount)
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
    if (!st || maxLines <= 0 || !outCurAddr || !outLines || !outAddrs || !outCount) {
        return 0;
    }

    int freezeWhileRunning = source_pane_shouldFreezeAsmWhileRunning(st);
    if (st->frozenActive && !freezeWhileRunning) {
        st->frozenActive = 0;
        source_pane_freeFrozenAsm(st);
    }
    if (freezeWhileRunning && !st->frozenActive) {
        st->frozenPcAddr = source_cpr_getCurrentAddr(st);
        st->frozenActive = 1;
        st->frozenAsmStartIndex = INT_MIN;
        st->frozenAsmMaxLines = 0;
        source_pane_freeFrozenAsm(st);
    }

    uint64_t curAddr = freezeWhileRunning ? st->frozenPcAddr : source_cpr_getCurrentAddr(st);
    curAddr = source_cpr_resolveAnchorAddr(curAddr);
    *outCurAddr = curAddr;

    uint64_t startAddr = curAddr;
    if (st->scrollLocked) {
        if (st->scrollAnchorValid) {
            startAddr = st->scrollAnchorAddr;
        } else {
            startAddr = (uint64_t)(uint32_t)(st->scrollIndex >= 0 ? st->scrollIndex : 0) * 4ull;
        }
    }
    startAddr = source_cpr_resolveAnchorAddr(startAddr);
    int startIndex = (int)(startAddr >> 2);

    if (freezeWhileRunning &&
        st->frozenActive &&
        st->frozenAsmLines &&
        st->frozenAsmAddrs &&
        st->frozenAsmStartIndex == startIndex &&
        st->frozenAsmMaxLines == maxLines) {
        *outLines = (const char **)st->frozenAsmLines;
        *outAddrs = (const uint64_t *)st->frozenAsmAddrs;
        *outCount = st->frozenAsmCount;
        return st->frozenAsmCount > 0 ? 1 : 0;
    }

    source_pane_freeFrozenAsm(st);
    st->frozenAsmLines = (char **)alloc_calloc((size_t)maxLines, sizeof(*st->frozenAsmLines));
    st->frozenAsmAddrs = (uint64_t *)alloc_calloc((size_t)maxLines, sizeof(*st->frozenAsmAddrs));
    if (!st->frozenAsmLines || !st->frozenAsmAddrs) {
        source_pane_freeFrozenAsm(st);
        return 0;
    }

    for (int i = 0; i < maxLines; ++i) {
        uint32_t addr = (uint32_t)((startAddr + ((uint64_t)i * 4ull)) & 0x00fffffcu);
        uint8_t bytes[4];
        if (!libretro_host_debugReadMemory(addr, bytes, sizeof(bytes))) {
            break;
        }

        uint16_t w1 = ((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1];
        uint16_t w2 = ((uint16_t)bytes[2] << 8) | (uint16_t)bytes[3];
        char decoded[320];
        char words[32];
        char lineBuf[384];

        source_cpr_decodeInstruction(w1, w2, decoded, sizeof(decoded));
        (void)snprintf(words, sizeof(words), "%04X %04X  ", (unsigned)w1, (unsigned)w2);
        strutil_join2Trunc(lineBuf, sizeof(lineBuf), words, decoded);

        st->frozenAsmLines[i] = alloc_strdup(lineBuf);
        if (!st->frozenAsmLines[i]) {
            break;
        }
        st->frozenAsmAddrs[i] = (uint64_t)addr;
        st->frozenAsmCount++;
    }

    if (st->frozenAsmCount <= 0) {
        source_pane_freeFrozenAsm(st);
        return 0;
    }

    st->frozenAsmStartIndex = startIndex;
    st->frozenAsmMaxLines = maxLines;

    if (!st->scrollLocked) {
        st->scrollIndex = startIndex;
    } else if (!st->scrollAnchorValid) {
        st->scrollAnchorAddr = startAddr;
        st->scrollAnchorValid = 1;
    }

    *outLines = (const char **)st->frozenAsmLines;
    *outAddrs = (const uint64_t *)st->frozenAsmAddrs;
    *outCount = st->frozenAsmCount;
    return 1;
}

int
source_cpr_buildRegisterOptions(source_pane_state_t *st)
{
    int count = 0;

    if (!st) {
        return 0;
    }
    if (st->cprRegisterOptions) {
        return st->cprRegisterOptionCount > 0 ? 1 : 0;
    }

    for (uint16_t regOffset = 0; regOffset < 0x200u; regOffset += 2u) {
        const char *regName = amiga_custom_regs_nameForOffset(regOffset);
        if (!regName || !regName[0] || strcmp(regName, "RESERVED") == 0) {
            continue;
        }
        count++;
    }
    if (count <= 0) {
        return 0;
    }

    st->cprRegisterOptions = (e9ui_textbox_option_t *)alloc_calloc((size_t)count, sizeof(*st->cprRegisterOptions));
    if (!st->cprRegisterOptions) {
        return 0;
    }

    int index = 0;
    for (uint16_t regOffset = 0; regOffset < 0x200u; regOffset += 2u) {
        const char *regName = amiga_custom_regs_nameForOffset(regOffset);
        if (!regName || !regName[0] || strcmp(regName, "RESERVED") == 0) {
            continue;
        }
        st->cprRegisterOptions[index].value = regName;
        st->cprRegisterOptions[index].label = regName;
        index++;
    }
    st->cprRegisterOptionCount = count;
    return 1;
}

int
source_cpr_commitInlineEdit(source_pane_state_t *st, e9ui_context_t *ctx, e9ui_component_t *editor,
                            const char *text)
{
    if (!st || !editor) {
        return 0;
    }

    if (st->inlineEditKind == source_pane_inline_edit_cpr_words) {
        const char *split = text ? strchr(text, ' ') : NULL;
        char left[16];
        uint16_t w1 = 0;
        uint16_t w2 = 0;
        size_t leftLen = 0;

        if (!split) {
            e9ui_showTransientMessage("CPR EDIT INVALID");
            e9ui_textbox_selectAllExternal(editor);
            return -1;
        }
        leftLen = (size_t)(split - text);
        if (leftLen == 0 || leftLen >= sizeof(left)) {
            e9ui_showTransientMessage("CPR EDIT INVALID");
            e9ui_textbox_selectAllExternal(editor);
            return -1;
        }
        memcpy(left, text, leftLen);
        left[leftLen] = '\0';
        if (!source_pane_parseInlineHexWord(left, &w1) ||
            !source_pane_parseInlineHexWord(split + 1, &w2)) {
            e9ui_showTransientMessage("CPR EDIT INVALID");
            e9ui_textbox_selectAllExternal(editor);
            return -1;
        }
        if (!libretro_host_debugWriteMemory(st->inlineEditAddr, (uint32_t)w1, 2) ||
            !libretro_host_debugWriteMemory(st->inlineEditAddr + 2u, (uint32_t)w2, 2)) {
            e9ui_showTransientMessage("CPR EDIT FAILED");
            e9ui_textbox_selectAllExternal(editor);
            return -1;
        }
        source_pane_inlineEditRefreshAfterWrite(st);
        source_pane_inlineEditCancel(st, ctx);
        return 1;
    }

    if (st->inlineEditKind == source_pane_inline_edit_cpr_reg) {
        char nameBuf[64];
        size_t out = 0;
        uint16_t regOffset = 0;
        uint16_t newWord1 = 0;

        if (!text) {
            return -1;
        }
        for (const char *p = text; *p && out + 1 < sizeof(nameBuf); ++p) {
            if (!isspace((unsigned char)*p)) {
                nameBuf[out++] = *p;
            }
        }
        nameBuf[out] = '\0';
        if (nameBuf[0] == '\0' || !source_cpr_resolveRegisterOffset(nameBuf, &regOffset)) {
            e9ui_showTransientMessage("CPR REGISTER INVALID");
            e9ui_textbox_selectAllExternal(editor);
            return -1;
        }
        newWord1 = (uint16_t)((st->inlineEditWord1 & (uint16_t)~0x01feu) | regOffset);
        if (!libretro_host_debugWriteMemory(st->inlineEditAddr, (uint32_t)newWord1, 2)) {
            e9ui_showTransientMessage("CPR EDIT FAILED");
            e9ui_textbox_selectAllExternal(editor);
            return -1;
        }
        source_pane_inlineEditRefreshAfterWrite(st);
        source_pane_inlineEditCancel(st, ctx);
        return 1;
    }

    if (st->inlineEditKind == source_pane_inline_edit_cpr_value) {
        uint16_t valueWord = 0;
        if (!source_pane_parseInlineHexWord(text, &valueWord)) {
            e9ui_showTransientMessage("CPR VALUE INVALID");
            e9ui_textbox_selectAllExternal(editor);
            return -1;
        }
        if (!libretro_host_debugWriteMemory(st->inlineEditAddr + 2u, (uint32_t)valueWord, 2)) {
            e9ui_showTransientMessage("CPR EDIT FAILED");
            e9ui_textbox_selectAllExternal(editor);
            return -1;
        }
        source_pane_inlineEditRefreshAfterWrite(st);
        source_pane_inlineEditCancel(st, ctx);
        return 1;
    }

    return 0;
}

int
source_cpr_beginInlineWordsEditAtPoint(e9ui_component_t *self, e9ui_context_t *ctx, source_pane_state_t *st,
                                       int mx, int my)
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
    if (!source_cpr_getWindow(st, drawMaxLines, &curAddr, &lines, &addrs, &count)) {
        return 0;
    }
    (void)curAddr;
    (void)lines;

    hexw = dasm_getAddrHexWidth();
    if (hexw < 6) {
        hexw = 6;
    }
    if (hexw > 16) {
        hexw = 16;
    }
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
        uint8_t bytes[4];
        uint16_t w1 = 0;
        uint16_t w2 = 0;
        char editText[32];
        int fieldW = 0;
        SDL_Rect rect;

        if (!libretro_host_debugReadMemory((uint32_t)addrs[i], bytes, sizeof(bytes))) {
            y += metrics.lineHeight;
            continue;
        }
        w1 = (uint16_t)(((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1]);
        w2 = (uint16_t)(((uint16_t)bytes[2] << 8) | (uint16_t)bytes[3]);
        source_cpr_formatInlineWords(editText, sizeof(editText), w1, w2);
        fieldW = source_pane_measureSegment(useFont, editText, 0, (int)strlen(editText));
        rect = (SDL_Rect){
            textX - e9ui_scale_px(ctx, 4),
            y - e9ui_scale_px(ctx, 2),
            fieldW + e9ui_scale_px(ctx, 12),
            metrics.lineHeight + e9ui_scale_px(ctx, 4)
        };
        if (mx >= rect.x && mx < rect.x + rect.w &&
            my >= rect.y && my < rect.y + rect.h) {
            return source_pane_beginInlineEdit(st,
                                               ctx,
                                               source_pane_mode_cpr,
                                               source_pane_inline_edit_cpr_words,
                                               (uint32_t)addrs[i],
                                               4,
                                               w1,
                                               w2,
                                               editText,
                                               rect);
        }
        y += metrics.lineHeight;
    }

    return 0;
}

int
source_cpr_beginInlineRegisterEditAtPoint(e9ui_component_t *self, e9ui_context_t *ctx, source_pane_state_t *st,
                                          int mx, int my)
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
    if (!source_cpr_buildRegisterOptions(st)) {
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
    if (!source_cpr_getWindow(st, drawMaxLines, &curAddr, &lines, &addrs, &count)) {
        return 0;
    }
    (void)curAddr;

    hexw = dasm_getAddrHexWidth();
    if (hexw < 6) {
        hexw = 6;
    }
    if (hexw > 16) {
        hexw = 16;
    }
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
        const char *line = lines[i] ? lines[i] : "";
        int lineLen = (int)strlen(line);
        const char *assign = NULL;
        uint8_t bytes[4];
        uint16_t w1 = 0;
        uint16_t w2 = 0;
        int regStart = 10;
        int regLen = 0;
        int regX = 0;
        int regW = 0;
        SDL_Rect rect;

        if (lineLen > 10) {
            assign = strstr(line + 10, " := ");
        }
        if (!assign) {
            y += metrics.lineHeight;
            continue;
        }
        regLen = (int)(assign - (line + regStart));
        if (regLen <= 0) {
            y += metrics.lineHeight;
            continue;
        }
        if (!libretro_host_debugReadMemory((uint32_t)addrs[i], bytes, sizeof(bytes))) {
            y += metrics.lineHeight;
            continue;
        }
        w1 = (uint16_t)(((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1]);
        w2 = (uint16_t)(((uint16_t)bytes[2] << 8) | (uint16_t)bytes[3]);
        regX = textX + source_pane_measureSegment(useFont, line, 0, regStart);
        regW = source_pane_measureSegment(useFont, line, regStart, regLen);
        rect = (SDL_Rect){
            regX - e9ui_scale_px(ctx, 4),
            y - e9ui_scale_px(ctx, 2),
            regW + e9ui_scale_px(ctx, 12),
            metrics.lineHeight + e9ui_scale_px(ctx, 4)
        };
        if (mx >= rect.x && mx < rect.x + rect.w &&
            my >= rect.y && my < rect.y + rect.h) {
            char regText[64];
            if (regLen >= (int)sizeof(regText)) {
                regLen = (int)sizeof(regText) - 1;
            }
            memcpy(regText, line + regStart, (size_t)regLen);
            regText[regLen] = '\0';
            return source_pane_beginInlineEdit(st,
                                               ctx,
                                               source_pane_mode_cpr,
                                               source_pane_inline_edit_cpr_reg,
                                               (uint32_t)addrs[i],
                                               2,
                                               w1,
                                               w2,
                                               regText,
                                               rect);
        }
        y += metrics.lineHeight;
    }

    return 0;
}

int
source_cpr_beginInlineValueEditAtPoint(e9ui_component_t *self, e9ui_context_t *ctx, source_pane_state_t *st,
                                       int mx, int my)
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
    if (!source_cpr_getWindow(st, drawMaxLines, &curAddr, &lines, &addrs, &count)) {
        return 0;
    }
    (void)curAddr;

    hexw = dasm_getAddrHexWidth();
    if (hexw < 6) {
        hexw = 6;
    }
    if (hexw > 16) {
        hexw = 16;
    }
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
        const char *line = lines[i] ? lines[i] : "";
        const char *assign = NULL;
        const char *valueStart = NULL;
        int lineLen = (int)strlen(line);
        int valueIndex = 0;
        int valueLen = 0;
        int valueX = 0;
        int valueW = 0;
        SDL_Rect rect;
        uint8_t bytes[4];
        uint16_t w1 = 0;
        uint16_t w2 = 0;
        char valueText[16];

        if (lineLen > 10) {
            assign = strstr(line + 10, " := ");
        }
        if (!assign) {
            y += metrics.lineHeight;
            continue;
        }
        valueStart = assign + 4;
        valueIndex = (int)(valueStart - line);
        while (valueStart[valueLen] && isxdigit((unsigned char)valueStart[valueLen])) {
            valueLen++;
        }
        if (valueLen <= 0) {
            y += metrics.lineHeight;
            continue;
        }
        if (!libretro_host_debugReadMemory((uint32_t)addrs[i], bytes, sizeof(bytes))) {
            y += metrics.lineHeight;
            continue;
        }
        w1 = (uint16_t)(((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1]);
        w2 = (uint16_t)(((uint16_t)bytes[2] << 8) | (uint16_t)bytes[3]);
        valueX = textX + source_pane_measureSegment(useFont, line, 0, valueIndex);
        valueW = source_pane_measureSegment(useFont, line, valueIndex, valueLen);
        rect = (SDL_Rect){
            valueX - e9ui_scale_px(ctx, 4),
            y - e9ui_scale_px(ctx, 2),
            valueW + e9ui_scale_px(ctx, 12),
            metrics.lineHeight + e9ui_scale_px(ctx, 4)
        };
        if (mx >= rect.x && mx < rect.x + rect.w &&
            my >= rect.y && my < rect.y + rect.h) {
            if (valueLen >= (int)sizeof(valueText)) {
                valueLen = (int)sizeof(valueText) - 1;
            }
            memcpy(valueText, valueStart, (size_t)valueLen);
            valueText[valueLen] = '\0';
            return source_pane_beginInlineEdit(st,
                                               ctx,
                                               source_pane_mode_cpr,
                                               source_pane_inline_edit_cpr_value,
                                               (uint32_t)addrs[i],
                                               2,
                                               w1,
                                               w2,
                                               valueText,
                                               rect);
        }
        y += metrics.lineHeight;
    }

    return 0;
}

void
source_cpr_render(e9ui_component_t *self, e9ui_context_t *ctx)
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
    if (!source_cpr_isModeAvailable()) {
        SDL_Color icol = (SDL_Color){200, 160, 160, 255};
        source_pane_renderStatusMessage(ctx, useFont, contentArea, padPx, icol, "CPR mode is only available for Amiga");
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
    if (!source_cpr_getWindow(st, drawMaxLines, &curAddr, &lines, &addrs, &count)) {
        SDL_Color icol = (SDL_Color){200, 160, 160, 255};
        source_pane_renderStatusMessage(ctx, useFont, contentArea, padPx, icol, "No copper list data available");
        return;
    }
    int curRow = source_pane_findCurrentAddrRow(addrs, count, curAddr);

    int hexw = dasm_getAddrHexWidth();
    if (hexw < 6) {
        hexw = 6;
    }
    if (hexw > 16) {
        hexw = 16;
    }
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
    int textX = contentArea.x + padPx + gutterW + gutterPad;
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
        (void)snprintf(abuf, sizeof(abuf), "%0*llX", hexw, (unsigned long long)a);
        int nw = 0;
        int nh = 0;
        TTF_SizeText(useFont, abuf, &nw, &nh);
        int lnx = contentArea.x + padPx + (gutterW - nw);

        void *addrBucket = st ? (void *)&st->bucketAddr : (void *)self;
        void *sourceBucket = st ? (void *)&st->bucketSource : (void *)self;
        e9ui_text_select_drawText(ctx, self, useFont, abuf, lno, lnx, y,
                                  metrics.lineHeight, 0, addrBucket, 1, 1);
        source_pane_renderAsmLineHighlighted(ctx, self, useFont, ins, 10, txt,
                                             textX, y, metrics.lineHeight, hitW, sourceBucket);

        y += metrics.lineHeight;
        if (y > clipBottom) {
            break;
        }
    }
}
