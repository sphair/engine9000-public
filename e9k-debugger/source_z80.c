/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "alloc.h"
#include "debugger.h"
#include "libretro_host.h"
#include "source_z80.h"
#include "strutil.h"
#include "target.h"

enum
{
    SOURCE_Z80_MAX_INSN_BYTES = 4
};

static int
source_z80_findProcessorId(uint32_t *outProcessorId);

int
source_z80_isModeAvailable(void)
{
    uint32_t processorId = 0;

    if (target != target_neogeo()) {
        return 0;
    }
    return source_z80_findProcessorId(&processorId);
}

static int
source_z80_findProcessorId(uint32_t *outProcessorId)
{
    e9k_debug_processor_info_t processors[8];
    size_t count = 0;

    if (outProcessorId) {
        *outProcessorId = 0;
    }
    if (!libretro_host_debugReadProcessors(processors, countof(processors), &count)) {
        return 0;
    }
    if (count > countof(processors)) {
        count = countof(processors);
    }
    for (size_t i = 0; i < count; ++i) {
        if (strcasecmp(processors[i].name, "Z80") == 0) {
            if (outProcessorId) {
                *outProcessorId = processors[i].id;
            }
            return 1;
        }
    }
    return 0;
}

uint32_t
source_z80_processorId(void)
{
    uint32_t processorId = 0;

    (void)source_z80_findProcessorId(&processorId);
    return processorId;
}

uint64_t
source_z80_resolveAnchorAddr(uint64_t addr)
{
    return addr & 0xffffull;
}

static int
source_z80_readPc(uint64_t *outAddr)
{
    e9k_debug_processor_reg_t regs[32];
    size_t count = 0;

    if (!outAddr) {
        return 0;
    }
    if (!libretro_host_debugReadProcessorRegs(source_z80_processorId(), regs, countof(regs), &count)) {
        return 0;
    }
    for (size_t i = 0; i < count; ++i) {
        if (strcasecmp(regs[i].name, "PC") == 0) {
            *outAddr = source_z80_resolveAnchorAddr(regs[i].value);
            return 1;
        }
    }
    return 0;
}

uint64_t
source_z80_getCurrentAddr(source_pane_state_t *st)
{
    uint64_t addr = 0;

    if (st && st->overrideActive) {
        return source_z80_resolveAnchorAddr(st->overrideAddr);
    }
    if (source_z80_readPc(&addr)) {
        return addr;
    }
    if (st && st->scrollAnchorValid) {
        return source_z80_resolveAnchorAddr(st->scrollAnchorAddr);
    }
    return 0;
}

static size_t
source_z80_appendChar(char *out, size_t cap, size_t *pos, char ch)
{
    if (!out || cap == 0 || !pos || *pos + 1 >= cap) {
        return 0;
    }
    out[*pos] = ch;
    (*pos)++;
    out[*pos] = '\0';
    return 1;
}

static int
source_z80_isHexDigit(char ch)
{
    return isxdigit((unsigned char)ch) ? 1 : 0;
}

static int
source_z80_isHexSuffixLiteral(const char *text, size_t start, size_t end)
{
    if (!text || end <= start + 1 || text[end - 1] != 'h') {
        return 0;
    }
    if (!isdigit((unsigned char)text[start])) {
        return 0;
    }
    for (size_t i = start; i + 1 < end; ++i) {
        if (!source_z80_isHexDigit(text[i])) {
            return 0;
        }
    }
    return 1;
}

static void
source_z80_formatDisassembly(char *out, size_t cap, const char *text)
{
    size_t pos = 0;
    size_t i = 0;

    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (!text) {
        return;
    }

    while (text[i] && pos + 1 < cap) {
        if (isdigit((unsigned char)text[i])) {
            size_t start = i;
            while (source_z80_isHexDigit(text[i])) {
                i++;
            }
            if (text[i] == 'h') {
                i++;
                if (source_z80_isHexSuffixLiteral(text, start, i)) {
                    size_t digitStart = start;
                    if (!source_z80_appendChar(out, cap, &pos, '$')) {
                        return;
                    }
                    if (digitStart + 2 < i &&
                        text[digitStart] == '0' &&
                        isalpha((unsigned char)text[digitStart + 1])) {
                        digitStart++;
                    }
                    for (size_t j = digitStart; j + 1 < i; ++j) {
                        if (!source_z80_appendChar(out, cap, &pos, (char)toupper((unsigned char)text[j]))) {
                            return;
                        }
                    }
                    continue;
                }
            }
            for (size_t j = start; j < i && pos + 1 < cap; ++j) {
                if (!source_z80_appendChar(out, cap, &pos, text[j])) {
                    return;
                }
            }
            continue;
        }
        if (!source_z80_appendChar(out, cap, &pos, text[i])) {
            return;
        }
        i++;
    }
}

static size_t
source_z80_disassembleLine(uint64_t addr, char *out, size_t cap)
{
    char disasm[256];
    size_t insnLen = 0;

    if (!out || cap == 0) {
        return 1;
    }
    out[0] = '\0';
    disasm[0] = '\0';

    if (!libretro_host_debugDisassembleProcessorQuick(source_z80_processorId(),
                                                      (uint32_t)source_z80_resolveAnchorAddr(addr),
                                                      disasm,
                                                      sizeof(disasm),
                                                      &insnLen) ||
        insnLen == 0) {
        strutil_strlcpy(out, cap, "??");
        return 1;
    }
    if (insnLen > SOURCE_Z80_MAX_INSN_BYTES) {
        insnLen = SOURCE_Z80_MAX_INSN_BYTES;
    }
    source_z80_formatDisassembly(out, cap, disasm);

    return insnLen == 0 ? 1 : insnLen;
}

int
source_z80_getWindow(source_pane_state_t *st, int maxLines, uint64_t *outCurAddr,
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
        st->frozenPcAddr = source_z80_getCurrentAddr(st);
        st->frozenActive = 1;
        st->frozenAsmStartIndex = -1;
        st->frozenAsmMaxLines = 0;
        source_pane_freeFrozenAsm(st);
    }

    uint64_t curAddr = freezeWhileRunning ? st->frozenPcAddr : source_z80_getCurrentAddr(st);
    curAddr = source_z80_resolveAnchorAddr(curAddr);
    *outCurAddr = curAddr;

    uint64_t startAddr = curAddr;
    if (st->scrollLocked) {
        if (st->scrollAnchorValid) {
            startAddr = st->scrollAnchorAddr;
        } else if (st->scrollIndex >= 0) {
            startAddr = (uint64_t)(uint32_t)st->scrollIndex;
        }
    } else if (maxLines > 1) {
        uint64_t back = (uint64_t)(maxLines / 2);
        startAddr = curAddr > back ? curAddr - back : 0;
    }
    startAddr = source_z80_resolveAnchorAddr(startAddr);
    int startIndex = (int)startAddr;

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

    uint64_t addr = startAddr;
    for (int i = 0; i < maxLines; ++i) {
        char line[320];
        size_t insnLen = source_z80_disassembleLine(addr, line, sizeof(line));
        st->frozenAsmLines[i] = alloc_strdup(line);
        if (!st->frozenAsmLines[i]) {
            break;
        }
        st->frozenAsmAddrs[i] = source_z80_resolveAnchorAddr(addr);
        st->frozenAsmCount++;
        if (insnLen == 0) {
            insnLen = 1;
        }
        addr = source_z80_resolveAnchorAddr(addr + insnLen);
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
