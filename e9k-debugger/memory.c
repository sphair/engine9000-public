/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

#include "memory.h"
#include "e9ui_context.h"
#include "e9ui_scale.h"
#include "e9ui_stack.h"
#include "e9ui_hstack.h"
#include "e9ui_textbox.h"
#include "e9ui_labeled_select.h"
#include "e9ui_data_edit.h"
#include "e9ui_text_cache.h"
#include "e9ui_step_buttons.h"
#include "e9ui_scrollbar.h"
#include "target.h"
#include "hex_byte_color.h"
#include "inline_edit_pause.h"
#include "libretro_host.h"

#define MEMORY_SEARCH_MAX_PATTERN 128
#define MEMORY_SEARCH_MAX_RANGES 64
#define MEMORY_MAX_SPACES 8
#define MEMORY_SEARCH_CHUNK 4096
#define MEMORY_BYTES_PER_ROW 16u
#define MEMORY_DEFAULT_VIEW_ROWS 32
#define MEMORY_ROW_HEX_START_COL 10
#define MEMORY_ROW_ASCII_START_COL (MEMORY_ROW_HEX_START_COL + ((int)MEMORY_BYTES_PER_ROW * 3) + 1)

typedef struct memory_search_pattern {
    uint8_t ascii[MEMORY_SEARCH_MAX_PATTERN];
    int asciiLen;
    uint8_t hex[MEMORY_SEARCH_MAX_PATTERN];
    int hexLen;
} memory_search_pattern_t;

typedef enum memory_inline_edit_mode {
    memory_inline_edit_mode_none = 0,
    memory_inline_edit_mode_hex,
    memory_inline_edit_mode_ascii
} memory_inline_edit_mode_t;

typedef struct memory_view_state {
    unsigned int   base;
    int            rowsPerView;
    e9ui_component_t *ownerView;
    void           *inlineEditMeta;
    e9ui_component_t *spaceSelect;
    e9ui_component_t *addressBox;
    e9ui_component_t *searchBox;
    uint32_t       searchMatchAddr;
    int            searchMatchLen;
    int            searchMatchValid;
    char           error[128];
    e9ui_step_buttons_state_t stepButtons;
    e9ui_scrollbar_state_t hScrollbar;
    int            scrollX;
    int            contentPixelWidth;
    int            inlineEditPending;
    int            inlineEditActive;
    int            inlineEditAutoResume;
    uint32_t       inlineEditAddr;
    int            inlineEditByteCount;
    memory_inline_edit_mode_t inlineEditMode;
    uint8_t        inlineEditOriginalBytes[MEMORY_BYTES_PER_ROW];
    SDL_Rect       inlineEditRect;
    char           spaceValue[16];
} memory_view_state_t;

typedef struct memory_step_buttons_action_ctx {
    memory_view_state_t *st;
} memory_step_buttons_action_ctx_t;

static int
memory_buildSearchPattern(const char *text, memory_search_pattern_t *outPattern);

static int
memory_findNextMatch(memory_view_state_t *st, const memory_search_pattern_t *pattern,
                     uint32_t startAddr, uint32_t *outAddr, int *outLen);

static int
memory_findPrevMatch(memory_view_state_t *st, const memory_search_pattern_t *pattern,
                     uint32_t startAddr, uint32_t *outAddr, int *outLen);

static void
memory_inlineEditCancel(memory_view_state_t *st, e9ui_context_t *ctx);

static int
memory_inlineEditCommit(memory_view_state_t *st, e9ui_context_t *ctx);

static int
memory_beginInlineEditAtPoint(e9ui_component_t *self, e9ui_context_t *ctx, memory_view_state_t *st, int mx, int my);

static int
memory_parseU64SmartHex(const char *text, unsigned long long *outValue, char **outEnd)
{
    if (outValue) {
        *outValue = 0;
    }
    if (outEnd) {
        *outEnd = NULL;
    }
    if (!text || !outValue) {
        return 0;
    }
    const char *p = text;
    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }
    int base = 0;
    const char *parseStart = p;
    if (*parseStart == '$') {
        ++parseStart;
        base = 16;
    } else if (!(parseStart[0] == '0' && (parseStart[1] == 'x' || parseStart[1] == 'X'))) {
        for (const char *q = parseStart; *q; ++q) {
            if (isspace((unsigned char)*q)) {
                break;
            }
            if (((*q >= 'a') && (*q <= 'f')) || ((*q >= 'A') && (*q <= 'F'))) {
                base = 16;
                break;
            }
        }
    }
    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(parseStart, &end, base);
    if (outEnd) {
        *outEnd = end;
    }
    if (!end || end == parseStart || errno != 0) {
        return 0;
    }
    *outValue = value;
    return 1;
}

static int
memory_getSpaces(target_memory_space_t *outSpaces, size_t cap, size_t *outCount)
{
    if (outCount) {
        *outCount = 0;
    }
    if (target && target->memoryGetSpaces) {
        return target->memoryGetSpaces(outSpaces, cap, outCount);
    }

    if (outCount) {
        *outCount = 1;
    }
    if (!outSpaces || cap == 0) {
        return 1;
    }

    uint32_t minAddr = 0;
    uint32_t maxAddr = 0x00ffffffu;
    if (target && target->memoryGetLimits) {
        (void)target->memoryGetLimits(&minAddr, &maxAddr);
    }
    outSpaces[0] = (target_memory_space_t){
        .value = "main",
        .label = "Main",
        .minAddr = minAddr,
        .maxAddr = maxAddr,
        .addressDigits = 6,
        .processorMemory = 0,
        .processorId = 0
    };
    return 1;
}

static int
memory_getSelectedSpace(const memory_view_state_t *st, target_memory_space_t *outSpace)
{
    target_memory_space_t spaces[MEMORY_MAX_SPACES];
    size_t count = 0;
    size_t spaceCap = sizeof(spaces) / sizeof(spaces[0]);

    if (!outSpace) {
        return 0;
    }
    memset(spaces, 0, sizeof(spaces));
    if (!memory_getSpaces(spaces, spaceCap, &count) || count == 0) {
        return 0;
    }
    if (count > spaceCap) {
        count = spaceCap;
    }
    const char *want = (st && st->spaceValue[0]) ? st->spaceValue : spaces[0].value;
    for (size_t i = 0; i < count; ++i) {
        if (spaces[i].value && want && strcmp(spaces[i].value, want) == 0) {
            *outSpace = spaces[i];
            return 1;
        }
    }
    *outSpace = spaces[0];
    return 1;
}

static int
memory_getSelectedAddressLimits(const memory_view_state_t *st, uint32_t *outMinAddr, uint32_t *outMaxAddr)
{
    target_memory_space_t space;

    if (outMinAddr) {
        *outMinAddr = 0;
    }
    if (outMaxAddr) {
        *outMaxAddr = 0x00ffffffu;
    }
    if (!memory_getSelectedSpace(st, &space)) {
        return 0;
    }
    if (outMinAddr) {
        *outMinAddr = space.minAddr;
    }
    if (outMaxAddr) {
        *outMaxAddr = space.maxAddr;
    }
    return 1;
}

static void
memory_syncTextboxFromBase(memory_view_state_t *st)
{
    if (!st || !st->addressBox) {
        return;
    }
    target_memory_space_t space;
    int digits = 6;
    uint32_t maxAddr = 0x00ffffffu;
    if (memory_getSelectedSpace(st, &space)) {
        digits = space.addressDigits > 0 ? space.addressDigits : 6;
        maxAddr = space.maxAddr;
    }
    char addrText[32];
    snprintf(addrText, sizeof(addrText), "0x%0*X", digits, st->base & maxAddr);
    e9ui_textbox_setText(st->addressBox, addrText);
}

static unsigned int
memory_viewByteCount(const memory_view_state_t *st)
{
    unsigned int rows = MEMORY_DEFAULT_VIEW_ROWS;

    if (st && st->rowsPerView > 0) {
        rows = (unsigned int)st->rowsPerView;
    }
    return rows * MEMORY_BYTES_PER_ROW;
}

static e9ui_component_t *
memory_inlineEditComponent(memory_view_state_t *st)
{
    if (!st || !st->ownerView || !st->inlineEditMeta) {
        return NULL;
    }
    return e9ui_child_find(st->ownerView, st->inlineEditMeta);
}

static int
memory_pointInBounds(const e9ui_component_t *comp, int x, int y)
{
    if (!comp) {
        return 0;
    }
    return x >= comp->bounds.x && x < comp->bounds.x + comp->bounds.w &&
           y >= comp->bounds.y && y < comp->bounds.y + comp->bounds.h;
}

static uint32_t
memory_clampBaseForView(memory_view_state_t *st, uint32_t base)
{
    if (!st) {
        return base;
    }
    uint32_t minAddr = 0;
    uint32_t maxAddr = 0x00ffffffu;
    if (!memory_getSelectedAddressLimits(st, &minAddr, &maxAddr)) {
        minAddr = 0;
        maxAddr = 0x00ffffffu;
    }
    uint64_t viewBytes = memory_viewByteCount(st);
    uint64_t span = viewBytes > 0 ? (uint64_t)(viewBytes - 1u) : 0ull;
    uint32_t maxBase = maxAddr;
    if ((uint64_t)maxAddr >= span) {
        maxBase = (uint32_t)((uint64_t)maxAddr - span);
    } else {
        maxBase = minAddr;
    }
    if (maxBase < minAddr) {
        maxBase = minAddr;
    }
    uint32_t clamped = base;
    if (clamped < minAddr) {
        clamped = minAddr;
    }
    if (clamped > maxBase) {
        clamped = maxBase;
    }
    return clamped;
}

static int
memory_pageRows(const memory_view_state_t *st)
{
    if (st && st->rowsPerView > 0) {
        return st->rowsPerView;
    }
    return MEMORY_DEFAULT_VIEW_ROWS;
}

static void
memory_scrollRows(memory_view_state_t *st, int rows)
{
    if (!st || rows == 0) {
        return;
    }
    if (st->inlineEditActive) {
        memory_inlineEditCancel(st, NULL);
    }
    int64_t delta = (int64_t)rows * 16ll;
    int64_t rawBase = (int64_t)(uint32_t)st->base + delta;
    if (rawBase < 0) {
        rawBase = 0;
    }
    uint32_t clamped = memory_clampBaseForView(st, (uint32_t)rawBase);
    if (clamped == st->base) {
        return;
    }
    st->base = clamped;
    memory_syncTextboxFromBase(st);
}

static void
memory_setError(memory_view_state_t *panel, const char *fmt, ...)
{
    if (!panel) {
        return;
    }
    panel->error[0] = '\0';
    if (!fmt || !*fmt) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(panel->error, sizeof(panel->error), fmt, ap);
    va_end(ap);
    panel->error[sizeof(panel->error) - 1] = '\0';
}

static int
memory_parseHexNibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

static int
memory_parseInlineHexBytes(const char *text, uint8_t *outBytes, int wantCount)
{
    int highNibble = -1;
    int count = 0;

    if (!text || !outBytes || wantCount <= 0) {
        return 0;
    }

    while (*text) {
        if (isspace((unsigned char)*text)) {
            ++text;
            continue;
        }
        int nibble = memory_parseHexNibble(*text);
        if (nibble < 0) {
            return 0;
        }
        if (highNibble < 0) {
            highNibble = nibble;
        } else {
            if (count >= wantCount) {
                return 0;
            }
            outBytes[count++] = (uint8_t)((highNibble << 4) | nibble);
            highNibble = -1;
        }
        ++text;
    }

    if (highNibble >= 0 || count != wantCount) {
        return 0;
    }
    return 1;
}

static void
memory_formatInlineHexBytes(char *dst, size_t cap, const uint8_t *bytes, int count)
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

static void
memory_formatInlineAsciiBytes(char *dst, size_t cap, const uint8_t *bytes, int count)
{
    int limit = 0;

    if (!dst || cap == 0) {
        return;
    }
    dst[0] = '\0';
    if (!bytes || count <= 0) {
        return;
    }

    limit = count;
    if ((size_t)limit >= cap) {
        limit = (int)cap - 1;
    }
    if (limit < 0) {
        limit = 0;
    }
    for (int i = 0; i < limit; ++i) {
        unsigned char c = bytes[i];
        dst[i] = (c >= 32 && c <= 126) ? (char)c : '.';
    }
    dst[limit] = '\0';
}

static int
memory_parseInlineAsciiBytes(const char *text, const uint8_t *originalBytes, uint8_t *outBytes, int count)
{
    char originalText[MEMORY_BYTES_PER_ROW + 1];
    size_t textLen = 0;

    if (!text || !originalBytes || !outBytes || count <= 0 || count > (int)MEMORY_BYTES_PER_ROW) {
        return 0;
    }

    textLen = strlen(text);
    if (textLen > (size_t)count) {
        return 0;
    }

    memcpy(outBytes, originalBytes, (size_t)count);
    memory_formatInlineAsciiBytes(originalText, sizeof(originalText), originalBytes, count);

    if (textLen == (size_t)count) {
        for (int i = 0; i < count; ++i) {
            unsigned char c = (unsigned char)text[i];
            if (c < 32 || c > 126) {
                return 0;
            }
            if (c == (unsigned char)originalText[i] &&
                !(originalBytes[i] >= 32 && originalBytes[i] <= 126)) {
                continue;
            }
            outBytes[i] = c;
        }
        return 1;
    }

    for (size_t i = 0; i < textLen; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c < 32 || c > 126) {
            return 0;
        }
        outBytes[i] = c;
    }
    return 1;
}

static int
memory_readRaw(memory_view_state_t *st, uint32_t addr, void *out, size_t size)
{
    target_memory_space_t space;

    if (!out || size == 0) {
        return 0;
    }
    if (!memory_getSelectedSpace(st, &space)) {
        return libretro_host_debugReadMemory(addr, out, size) ? 1 : 0;
    }
    if (space.processorMemory) {
        return libretro_host_debugReadProcessorMemory(space.processorId, addr, out, size) ? 1 : 0;
    }
    return libretro_host_debugReadMemory(addr, out, size) ? 1 : 0;
}

static int
memory_writeRaw(memory_view_state_t *st, uint32_t addr, uint32_t value, size_t size)
{
    target_memory_space_t space;

    if (!memory_getSelectedSpace(st, &space)) {
        return libretro_host_debugWriteMemory(addr, value, size) ? 1 : 0;
    }
    if (space.processorMemory) {
        return libretro_host_debugWriteProcessorMemory(space.processorId, addr, value, size) ? 1 : 0;
    }
    return libretro_host_debugWriteMemory(addr, value, size) ? 1 : 0;
}

static int
memory_parseAddress(memory_view_state_t *st, unsigned int *out_addr)
{
    if (!st || !st->addressBox || !out_addr) {
        return 0;
    }
    const char *t = e9ui_textbox_getText(st->addressBox);
    if (!t || !*t) {
        memory_setError(st, "Invalid address: empty input");
        return 0;
    }
    char *end = NULL;
    unsigned long long val = 0;
    if (!memory_parseU64SmartHex(t, &val, &end)) {
        memory_setError(st, "Invalid address: \"%s\"", t);
        return 0;
    }
    while (*end && isspace((unsigned char)*end)) {
        ++end;
    }
    if (*end) {
        memory_setError(st, "Invalid address: \"%s\"", t);
        return 0;
    }
    uint32_t minAddr = 0;
    uint32_t maxAddr = 0x00ffffffu;
    int hasLimits = memory_getSelectedAddressLimits(st, &minAddr, &maxAddr);
    if (val > UINT32_MAX || (hasLimits && ((uint32_t)val < minAddr || (uint32_t)val > maxAddr))) {
        memory_setError(st, "Address outside range (0x%X-0x%X)", minAddr, maxAddr);
        return 0;
    }
    *out_addr = (unsigned int)val;
    return 1;
}

static void
memory_readRange(memory_view_state_t *st, uint32_t base, uint8_t *data, unsigned int size)
{
    if (!st || !data || size == 0) {
        return;
    }
    memset(data, 0, size);
    uint32_t minAddr = 0;
    uint32_t maxAddr = 0x00ffffffu;
    int hasLimits = memory_getSelectedAddressLimits(st, &minAddr, &maxAddr);
    memory_setError(st, NULL);
    uint32_t clampedMin = hasLimits ? minAddr : 0u;
    uint32_t clampedMax = hasLimits ? maxAddr : 0x00ffffffu;
    uint64_t rangeStart = (uint64_t)base;
    uint64_t rangeEnd = rangeStart + (uint64_t)size - 1ull;
    int rangeError = 0;

    if (rangeEnd > (uint64_t)clampedMax) {
        rangeEnd = clampedMax;
        rangeError = 1;
    }
    if (rangeStart < (uint64_t)clampedMin || rangeEnd > (uint64_t)clampedMax) {
        rangeError = 1;
    }

    uint64_t readStart = rangeStart;
    if (readStart < (uint64_t)clampedMin) {
        readStart = clampedMin;
    }
    uint64_t readEnd = rangeEnd;
    if (readEnd > (uint64_t)clampedMax) {
        readEnd = clampedMax;
    }

    if (readEnd >= readStart) {
        unsigned int dstOffset = (unsigned int)(readStart - rangeStart);
        unsigned int readSize = (unsigned int)(readEnd - readStart + 1ull);
        if (!memory_readRaw(st, (uint32_t)readStart, data + dstOffset, readSize)) {
            rangeError = 1;
        }
    }

    if (rangeError) {
        if (hasLimits) {
            memory_setError(st, "Range exceeds limits (0x%X-0x%X)", minAddr, maxAddr);
        } else {
            memory_setError(st, "Range exceeds 24-bit address space (0x000000-0xFFFFFF)");
        }
    }
}

static int
memory_readEditableRange(memory_view_state_t *st, uint32_t base, uint8_t *data, int size)
{
    uint32_t minAddr = 0;
    uint32_t maxAddr = 0x00ffffffu;

    if (!data || size <= 0) {
        return 0;
    }
    if (!memory_getSelectedAddressLimits(st, &minAddr, &maxAddr)) {
        minAddr = 0;
        maxAddr = 0x00ffffffu;
    }
    if (base < minAddr) {
        return 0;
    }
    if ((uint64_t)base + (uint64_t)size - 1ull > (uint64_t)maxAddr) {
        return 0;
    }
    return memory_readRaw(st, base, data, (size_t)size) ? 1 : 0;
}

static int
memory_writeHexBytes(memory_view_state_t *st, uint32_t addr, const uint8_t *bytes, int count)
{
    int i = 0;
    target_memory_space_t space;
    int byteWritesOnly = 0;

    if (!bytes || count <= 0) {
        return 0;
    }
    if (memory_getSelectedSpace(st, &space) && space.processorMemory) {
        byteWritesOnly = 1;
    }

    while (i < count) {
        if (!byteWritesOnly && (i + 1) < count) {
            uint16_t word = (uint16_t)(((uint16_t)bytes[i] << 8) | (uint16_t)bytes[i + 1]);
            if (!memory_writeRaw(st, addr + (uint32_t)i, (uint32_t)word, 2)) {
                return 0;
            }
            i += 2;
            continue;
        }
        if (!memory_writeRaw(st, addr + (uint32_t)i, (uint32_t)bytes[i], 1)) {
            return 0;
        }
        i += 1;
    }
    return 1;
}

static int
memory_verifyHexBytes(memory_view_state_t *st, uint32_t addr, const uint8_t *bytes, int count)
{
    uint8_t check[MEMORY_BYTES_PER_ROW];

    if (!bytes || count <= 0 || count > (int)sizeof(check)) {
        return 0;
    }
    memset(check, 0, sizeof(check));
    if (!memory_readRaw(st, addr, check, (size_t)count)) {
        return 0;
    }
    return memcmp(check, bytes, (size_t)count) == 0 ? 1 : 0;
}

static void
memory_onAddressSubmit(e9ui_context_t *ctx, void *user)
{
    memory_view_state_t *st = (memory_view_state_t*)user;
    if (!st || !st->addressBox) {
        return;
    }
    if (st->inlineEditActive) {
        memory_inlineEditCancel(st, ctx);
    }
    unsigned int addr = 0;
    if (!memory_parseAddress(st, &addr)) {
        return;
    }
    st->base = memory_clampBaseForView(st, addr);
    memory_syncTextboxFromBase(st);
}

static void
memory_onSpaceChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *value, void *user)
{
    memory_view_state_t *st = (memory_view_state_t*)user;

    (void)comp;
    if (!st || !value) {
        return;
    }
    if (st->inlineEditActive) {
        memory_inlineEditCancel(st, ctx);
    }
    size_t len = strlen(value);
    if (len >= sizeof(st->spaceValue)) {
        len = sizeof(st->spaceValue) - 1;
    }
    memcpy(st->spaceValue, value, len);
    st->spaceValue[len] = '\0';
    st->searchMatchValid = 0;
    st->searchMatchAddr = 0;
    st->searchMatchLen = 0;
    uint32_t minAddr = 0;
    uint32_t maxAddr = 0x00ffffffu;
    if (!memory_getSelectedAddressLimits(st, &minAddr, &maxAddr)) {
        minAddr = 0;
        maxAddr = 0x00ffffffu;
    }
    (void)maxAddr;
    st->base = memory_clampBaseForView(st, minAddr);
    memory_syncTextboxFromBase(st);
}

static int
memory_collectSearchRanges(memory_view_state_t *st, target_memory_range_t *outRanges, size_t cap, size_t *outCount)
{
    if (!outRanges || cap == 0 || !outCount) {
        return 0;
    }
    *outCount = 0;
    target_memory_space_t space;
    if (memory_getSelectedSpace(st, &space) && space.processorMemory) {
        outRanges[0].baseAddr = space.minAddr;
        outRanges[0].size = (space.maxAddr - space.minAddr) + 1u;
        *outCount = 1;
        return 1;
    }
    if (target && target->memoryTrackGetRanges) {
        size_t count = 0;
        if (target->memoryTrackGetRanges(outRanges, cap, &count) && count > 0) {
            size_t write = 0;
            for (size_t i = 0; i < count && write < cap; ++i) {
                if (outRanges[i].size == 0) {
                    continue;
                }
                outRanges[write++] = outRanges[i];
            }
            if (write > 0) {
                *outCount = write;
                return 1;
            }
        }
    }
    uint32_t minAddr = 0;
    uint32_t maxAddr = 0x00ffffffu;
    if (!memory_getSelectedAddressLimits(st, &minAddr, &maxAddr)) {
        minAddr = 0;
        maxAddr = 0x00ffffffu;
    }
    if (maxAddr < minAddr) {
        return 0;
    }
    outRanges[0].baseAddr = minAddr;
    outRanges[0].size = (maxAddr - minAddr) + 1u;
    *outCount = 1;
    return 1;
}

static int
memory_patternMatchesAt(const uint8_t *bytes, int avail, const memory_search_pattern_t *pattern, int *outLen)
{
    int bestLen = 0;
    if (!bytes || !pattern || avail <= 0) {
        return 0;
    }
    if (pattern->asciiLen > 0 && pattern->asciiLen <= avail) {
        if (memcmp(bytes, pattern->ascii, (size_t)pattern->asciiLen) == 0) {
            bestLen = pattern->asciiLen;
        }
    }
    if (pattern->hexLen > 0 && pattern->hexLen <= avail) {
        if (memcmp(bytes, pattern->hex, (size_t)pattern->hexLen) == 0) {
            if (pattern->hexLen > bestLen) {
                bestLen = pattern->hexLen;
            }
        }
    }
    if (bestLen <= 0) {
        return 0;
    }
    if (outLen) {
        *outLen = bestLen;
    }
    return 1;
}

static int
memory_scanRangeForMatch(memory_view_state_t *st, uint32_t scanStart, uint32_t scanEndInclusive,
                         const memory_search_pattern_t *pattern,
                         uint32_t *outAddr, int *outLen)
{
    if (!pattern || scanEndInclusive < scanStart) {
        return 0;
    }
    int maxPatternLen = pattern->asciiLen > pattern->hexLen ? pattern->asciiLen : pattern->hexLen;
    if (maxPatternLen <= 0) {
        return 0;
    }
    uint8_t chunk[MEMORY_SEARCH_CHUNK];
    uint32_t addr = scanStart;
    while (addr <= scanEndInclusive) {
        uint32_t remaining = scanEndInclusive - addr + 1u;
        size_t want = remaining < MEMORY_SEARCH_CHUNK ? (size_t)remaining : (size_t)MEMORY_SEARCH_CHUNK;
        if (want < (size_t)maxPatternLen && remaining >= (uint32_t)maxPatternLen) {
            want = (size_t)maxPatternLen;
        }
        if (want > MEMORY_SEARCH_CHUNK) {
            want = MEMORY_SEARCH_CHUNK;
        }
        if (!memory_readRaw(st, addr, chunk, want)) {
            uint32_t step = (uint32_t)want;
            if (step == 0) {
                step = 1;
            }
            if (addr > scanEndInclusive - step + 1u) {
                break;
            }
            addr += step;
            continue;
        }
        for (int i = 0; i < (int)want; ++i) {
            int matchLen = 0;
            if (memory_patternMatchesAt(chunk + i, (int)want - i, pattern, &matchLen)) {
                if (outAddr) {
                    *outAddr = addr + (uint32_t)i;
                }
                if (outLen) {
                    *outLen = matchLen;
                }
                return 1;
            }
        }
        if (remaining <= (uint32_t)maxPatternLen) {
            break;
        }
        uint32_t step = (uint32_t)want - (uint32_t)maxPatternLen + 1u;
        if (step == 0) {
            step = 1;
        }
        if (addr > scanEndInclusive - step + 1u) {
            break;
        }
        addr += step;
    }
    return 0;
}

static int
memory_scanRangeForLastMatch(memory_view_state_t *st, uint32_t scanStart, uint32_t scanEndInclusive,
                             const memory_search_pattern_t *pattern,
                             uint32_t *outAddr, int *outLen)
{
    if (!pattern || scanEndInclusive < scanStart) {
        return 0;
    }
    int maxPatternLen = pattern->asciiLen > pattern->hexLen ? pattern->asciiLen : pattern->hexLen;
    if (maxPatternLen <= 0) {
        return 0;
    }
    uint8_t chunk[MEMORY_SEARCH_CHUNK];
    uint32_t addr = scanStart;
    uint32_t lastAddr = 0;
    int lastLen = 0;
    int found = 0;
    while (addr <= scanEndInclusive) {
        uint32_t remaining = scanEndInclusive - addr + 1u;
        size_t want = remaining < MEMORY_SEARCH_CHUNK ? (size_t)remaining : (size_t)MEMORY_SEARCH_CHUNK;
        if (want < (size_t)maxPatternLen && remaining >= (uint32_t)maxPatternLen) {
            want = (size_t)maxPatternLen;
        }
        if (want > MEMORY_SEARCH_CHUNK) {
            want = MEMORY_SEARCH_CHUNK;
        }
        if (!memory_readRaw(st, addr, chunk, want)) {
            uint32_t step = (uint32_t)want;
            if (step == 0) {
                step = 1;
            }
            if (addr > scanEndInclusive - step + 1u) {
                break;
            }
            addr += step;
            continue;
        }
        for (int i = 0; i < (int)want; ++i) {
            int matchLen = 0;
            if (memory_patternMatchesAt(chunk + i, (int)want - i, pattern, &matchLen)) {
                lastAddr = addr + (uint32_t)i;
                lastLen = matchLen;
                found = 1;
            }
        }
        if (remaining <= (uint32_t)maxPatternLen) {
            break;
        }
        uint32_t step = (uint32_t)want - (uint32_t)maxPatternLen + 1u;
        if (step == 0) {
            step = 1;
        }
        if (addr > scanEndInclusive - step + 1u) {
            break;
        }
        addr += step;
    }
    if (!found) {
        return 0;
    }
    if (outAddr) {
        *outAddr = lastAddr;
    }
    if (outLen) {
        *outLen = lastLen;
    }
    return 1;
}

static int
memory_findNextMatch(memory_view_state_t *st, const memory_search_pattern_t *pattern,
                     uint32_t startAddr, uint32_t *outAddr, int *outLen)
{
    target_memory_range_t ranges[MEMORY_SEARCH_MAX_RANGES];
    size_t rangeCount = 0;
    if (!memory_collectSearchRanges(st, ranges, MEMORY_SEARCH_MAX_RANGES, &rangeCount) || rangeCount == 0) {
        return 0;
    }
    uint32_t maxAddr = 0x00ffffffu;
    (void)memory_getSelectedAddressLimits(st, NULL, &maxAddr);
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < rangeCount; ++i) {
            uint32_t base = ranges[i].baseAddr;
            uint32_t size = ranges[i].size;
            if (size == 0) {
                continue;
            }
            uint32_t end = base + size - 1u;
            if (end < base) {
                end = maxAddr;
            }
            uint32_t scanStart = base;
            uint32_t scanEnd = end;
            if (pass == 0) {
                if (startAddr > end) {
                    continue;
                }
                if (startAddr > base) {
                    scanStart = startAddr;
                }
            } else {
                if (startAddr <= base) {
                    continue;
                }
                if (startAddr <= end) {
                    scanEnd = startAddr - 1u;
                }
            }
            if (scanEnd < scanStart) {
                continue;
            }
            if (memory_scanRangeForMatch(st, scanStart, scanEnd, pattern, outAddr, outLen)) {
                return 1;
            }
        }
    }
    return 0;
}

static int
memory_findPrevMatch(memory_view_state_t *st, const memory_search_pattern_t *pattern,
                     uint32_t startAddr, uint32_t *outAddr, int *outLen)
{
    target_memory_range_t ranges[MEMORY_SEARCH_MAX_RANGES];
    size_t rangeCount = 0;
    if (!memory_collectSearchRanges(st, ranges, MEMORY_SEARCH_MAX_RANGES, &rangeCount) || rangeCount == 0) {
        return 0;
    }
    uint32_t maxAddr = 0x00ffffffu;
    (void)memory_getSelectedAddressLimits(st, NULL, &maxAddr);
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = (int)rangeCount - 1; i >= 0; --i) {
            uint32_t base = ranges[i].baseAddr;
            uint32_t size = ranges[i].size;
            if (size == 0) {
                continue;
            }
            uint32_t end = base + size - 1u;
            if (end < base) {
                end = maxAddr;
            }
            uint32_t scanStart = base;
            uint32_t scanEnd = end;
            if (pass == 0) {
                if (startAddr < base) {
                    continue;
                }
                if (startAddr < end) {
                    scanEnd = startAddr;
                }
            } else {
                if (startAddr >= end) {
                    continue;
                }
                if (startAddr >= base) {
                    scanStart = startAddr + 1u;
                }
            }
            if (scanEnd < scanStart) {
                continue;
            }
            if (memory_scanRangeForLastMatch(st, scanStart, scanEnd, pattern, outAddr, outLen)) {
                return 1;
            }
        }
    }
    return 0;
}

static int
memory_buildSearchPattern(const char *text, memory_search_pattern_t *outPattern)
{
    if (!outPattern) {
        return 0;
    }
    memset(outPattern, 0, sizeof(*outPattern));
    if (!text) {
        return 0;
    }
    int textLen = (int)strlen(text);
    if (textLen <= 0) {
        return 0;
    }
    int asciiLen = textLen;
    if (asciiLen > MEMORY_SEARCH_MAX_PATTERN) {
        asciiLen = MEMORY_SEARCH_MAX_PATTERN;
    }
    if (asciiLen > 0) {
        memcpy(outPattern->ascii, text, (size_t)asciiLen);
        outPattern->asciiLen = asciiLen;
    }

    char hexDigits[MEMORY_SEARCH_MAX_PATTERN * 2 + 1];
    int hexCount = 0;
    for (int i = 0; text[i] && hexCount < (int)sizeof(hexDigits) - 1; ++i) {
        if (isxdigit((unsigned char)text[i])) {
            hexDigits[hexCount++] = text[i];
        }
    }
    if (hexCount >= 2 && (hexCount % 2) == 0) {
        int bytes = hexCount / 2;
        if (bytes > MEMORY_SEARCH_MAX_PATTERN) {
            bytes = MEMORY_SEARCH_MAX_PATTERN;
        }
        for (int i = 0; i < bytes; ++i) {
            char pair[3];
            pair[0] = hexDigits[i * 2];
            pair[1] = hexDigits[i * 2 + 1];
            pair[2] = '\0';
            outPattern->hex[i] = (uint8_t)strtoul(pair, NULL, 16);
        }
        outPattern->hexLen = bytes;
    }
    return outPattern->asciiLen > 0 || outPattern->hexLen > 0;
}

static void
memory_runSearch(memory_view_state_t *st, int direction, int advance)
{
    if (!st || !st->searchBox) {
        return;
    }
    if (st->inlineEditActive) {
        memory_inlineEditCancel(st, NULL);
    }
    const char *text = e9ui_textbox_getText(st->searchBox);
    memory_search_pattern_t pattern;
    if (!memory_buildSearchPattern(text, &pattern)) {
        st->searchMatchValid = 0;
        st->searchMatchAddr = 0;
        st->searchMatchLen = 0;
        return;
    }
    uint32_t minAddr = 0;
    uint32_t maxAddr = 0x00ffffffu;
    (void)memory_getSelectedAddressLimits(st, &minAddr, &maxAddr);
    uint32_t startAddr = st->base;
    if (advance && st->searchMatchValid) {
        if (direction >= 0) {
            startAddr = st->searchMatchAddr < maxAddr ? st->searchMatchAddr + 1u : minAddr;
        } else {
            startAddr = st->searchMatchAddr > minAddr ? st->searchMatchAddr - 1u : maxAddr;
        }
    }
    uint32_t hitAddr = 0;
    int hitLen = 0;
    int found = 0;
    if (direction >= 0) {
        found = memory_findNextMatch(st, &pattern, startAddr, &hitAddr, &hitLen);
    } else {
        found = memory_findPrevMatch(st, &pattern, startAddr, &hitAddr, &hitLen);
    }
    if (!found) {
        st->searchMatchValid = 0;
        st->searchMatchAddr = 0;
        st->searchMatchLen = 0;
        return;
    }
    st->searchMatchValid = 1;
    st->searchMatchAddr = hitAddr;
    st->searchMatchLen = hitLen;

    uint32_t wantBase = memory_clampBaseForView(st, hitAddr & ~0x0fu);
    if (wantBase != st->base) {
        st->base = wantBase;
        memory_syncTextboxFromBase(st);
    }
}

static void
memory_onSearchChange(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_view_state_t *st = (memory_view_state_t*)user;
    memory_runSearch(st, 1, 0);
}

static void
memory_onSearchSubmit(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_view_state_t *st = (memory_view_state_t*)user;
    memory_runSearch(st, 1, 1);
}

static int
memory_onSearchKey(e9ui_context_t *ctx, SDL_Keycode key, SDL_Keymod mods, void *user)
{
    (void)ctx;
    memory_view_state_t *st = (memory_view_state_t*)user;
    if (!st) {
        return 0;
    }
    if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && (mods & KMOD_SHIFT) != 0) {
        memory_runSearch(st, -1, 1);
        return 1;
    }
    return 0;
}

static int
memory_stepButtonsGutterWidth(e9ui_context_t *ctx, e9ui_component_t *self)
{
    if (!ctx || !self) {
        return 0;
    }
    int thickness = e9ui_scale_px(ctx, 8);
    if (thickness < 4) {
        thickness = 4;
    }
    if (self->bounds.w > 0 && thickness >= self->bounds.w) {
        thickness = self->bounds.w > 1 ? self->bounds.w - 1 : 1;
    }
    if (thickness <= 0) {
        return 0;
    }
    int buttonW = thickness * 2;
    if (buttonW > self->bounds.w) {
        buttonW = self->bounds.w;
    }
    int margin = e9ui_scale_px(ctx, 4);
    if (margin < 0) {
        margin = 0;
    }
    int gutter = buttonW + margin;
    if (gutter > self->bounds.w) {
        gutter = self->bounds.w;
    }
    return gutter > 0 ? gutter : 0;
}

static int
memory_hscrollContentWidthEstimate(memory_view_state_t *st, TTF_Font *font)
{
    const int contentPad = 8;
    if (!font) {
        return 0;
    }
    int w = 0;
    int sampleLen = 0;
    char sample[256];
    sampleLen = snprintf(sample, sizeof(sample),
                         "00FFFFFF: FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................");
    if (sampleLen < 0) {
        sample[0] = '\0';
    }
    (void)TTF_SizeText(font, sample, &w, NULL);
    if (st && st->error[0]) {
        int ew = 0;
        (void)TTF_SizeText(font, st->error, &ew, NULL);
        if (ew > w) {
            w = ew;
        }
    }
    return w + contentPad * 2;
}

static e9ui_rect_t
memory_hscrollBounds(e9ui_context_t *ctx, e9ui_component_t *self)
{
    e9ui_rect_t bounds = {0, 0, 0, 0};
    if (!ctx || !self) {
        return bounds;
    }
    int rightGutter = memory_stepButtonsGutterWidth(ctx, self);
    if (rightGutter < 0) {
        rightGutter = 0;
    }
    if (rightGutter > self->bounds.w) {
        rightGutter = self->bounds.w;
    }
    bounds.x = self->bounds.x;
    bounds.y = self->bounds.y;
    bounds.w = self->bounds.w - rightGutter;
    bounds.h = self->bounds.h;
    if (bounds.w < 0) {
        bounds.w = 0;
    }
    return bounds;
}


static int
memory_stepButtonsOnAction(void *user, e9ui_step_buttons_action_t action)
{
    memory_step_buttons_action_ctx_t *actionCtx = (memory_step_buttons_action_ctx_t*)user;
    if (!actionCtx || !actionCtx->st) {
        return 0;
    }
    int rows = 0;
    switch (action) {
    case e9ui_step_buttons_action_page_up:
        rows = -memory_pageRows(actionCtx->st);
        break;
    case e9ui_step_buttons_action_line_up:
        rows = -1;
        break;
    case e9ui_step_buttons_action_line_down:
        rows = 1;
        break;
    case e9ui_step_buttons_action_page_down:
        rows = memory_pageRows(actionCtx->st);
        break;
    default:
        break;
    }
    if (rows == 0) {
        return 0;
    }
    memory_scrollRows(actionCtx->st, rows);
    return 1;
}

static int
memory_handleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    e9ui_component_t *inlineEdit = NULL;

    if (!self || !ctx || !ev) {
        return 0;
    }
    memory_view_state_t *st = (memory_view_state_t*)self->state;
    if (!st) {
        return 0;
    }
    inlineEdit = memory_inlineEditComponent(st);
    if (st->inlineEditActive && ctx && inlineEdit &&
        e9ui_getFocus(ctx) != inlineEdit &&
        e9ui_getFocus(ctx) != self) {
        memory_inlineEditCancel(st, ctx);
    }
    if (st->inlineEditActive && inlineEdit &&
        ev->type == SDL_MOUSEBUTTONDOWN &&
        ev->button.button == SDL_BUTTON_LEFT &&
        !memory_pointInBounds(inlineEdit, ev->button.x, ev->button.y) &&
        memory_pointInBounds(self, ev->button.x, ev->button.y)) {
        if (memory_inlineEditCommit(st, ctx)) {
            return 1;
        }
        return 1;
    }
    if (st->inlineEditActive &&
        ev->type == SDL_KEYDOWN &&
        ev->key.keysym.sym == SDLK_ESCAPE) {
        memory_inlineEditCancel(st, ctx);
        return 1;
    }

    if (ctx &&
        (ev->type == SDL_MOUSEMOTION || ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP)) {
        e9ui_rect_t sbBounds = memory_hscrollBounds(ctx, self);
        int viewW = sbBounds.w;
        if (viewW < 1) {
            viewW = 1;
        }
        TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
        int contentW = st->contentPixelWidth > 0 ? st->contentPixelWidth : memory_hscrollContentWidthEstimate(st, font);
        if (contentW < 0) {
            contentW = 0;
        }
        int scrollX = st->scrollX;
        int scrollY = 0;
        if (e9ui_scrollbar_handleEvent(self,
                                       ctx,
                                       ev,
                                       sbBounds,
                                       viewW,
                                       1,
                                       contentW,
                                       1,
                                       &scrollX,
                                       &scrollY,
                                       &st->hScrollbar)) {
            st->scrollX = scrollX;
            return 1;
        }
    }

    if (ctx) {
        memory_step_buttons_action_ctx_t actionCtx = { st };
        int stepEnabled = 1;
        if (e9ui_step_buttons_handleEvent(ctx,
                                          ev,
                                          self->bounds,
                                          0,
                                          stepEnabled,
                                          &st->stepButtons,
                                          &actionCtx,
                                          memory_stepButtonsOnAction)) {
            return 1;
        }
    }

    if (ev->type == SDL_MOUSEWHEEL) {
        int mx = ctx->mouseX;
        int my = ctx->mouseY;
        if (mx < self->bounds.x || mx >= self->bounds.x + self->bounds.w ||
            my < self->bounds.y || my >= self->bounds.y + self->bounds.h) {
            return 0;
        }
        int wheelX = ev->wheel.x;
        int wheelY = ev->wheel.y;
        if (wheelX != 0) {
            e9ui_rect_t sbBounds = memory_hscrollBounds(ctx, self);
            int viewW = sbBounds.w > 0 ? sbBounds.w : 1;
            int scrollY = 0;
            st->scrollX -= wheelX * e9ui_scale_px(ctx, 24);
            e9ui_scrollbar_clamp(viewW, 1, st->contentPixelWidth, 1, &st->scrollX, &scrollY);
        }
        if (wheelY != 0) {
            memory_scrollRows(st, wheelY * 3);
        }
        if (wheelX != 0 || wheelY != 0) {
            return 1;
        }
        return 0;
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_LEFT) {
        int mx = ev->button.x;
        int my = ev->button.y;

        st->inlineEditPending = 0;
        if (!memory_pointInBounds(self, mx, my)) {
            return 0;
        }
        if (!st->inlineEditActive && ev->button.clicks >= 2) {
            st->inlineEditPending = 1;
        }
        return 0;
    }
    if (ev->type == SDL_MOUSEBUTTONUP && ev->button.button == SDL_BUTTON_LEFT) {
        int mx = ev->button.x;
        int my = ev->button.y;

        if (!st->inlineEditPending) {
            return 0;
        }
        st->inlineEditPending = 0;
        if (!memory_pointInBounds(self, mx, my)) {
            return 0;
        }
        if (!st->inlineEditActive &&
            memory_beginInlineEditAtPoint(self, ctx, st, mx, my)) {
            return 1;
        }
    }
    if (ev->type == SDL_KEYDOWN && ctx && e9ui_getFocus(ctx) == self) {
        SDL_Keycode kc = ev->key.keysym.sym;
        if (kc == SDLK_PAGEUP) {
            memory_scrollRows(st, -memory_pageRows(st));
            return 1;
        }
        if (kc == SDLK_PAGEDOWN) {
            memory_scrollRows(st, memory_pageRows(st));
            return 1;
        }
        if (kc == SDLK_UP) {
            memory_scrollRows(st, -1);
            return 1;
        }
        if (kc == SDLK_DOWN) {
            memory_scrollRows(st, 1);
            return 1;
        }
    }
    return 0;
}

static int
memory_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self; (void)ctx; (void)availW;
    return 0;
}

static void
memory_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static int
memory_measureSegment(TTF_Font *font, const char *line, int start, int len)
{
    if (!font || !line || len <= 0) {
        return 0;
    }
    int lineLen = (int)strlen(line);
    if (start < 0) {
        start = 0;
    }
    if (start > lineLen) {
        start = lineLen;
    }
    if (start + len > lineLen) {
        len = lineLen - start;
    }
    if (len <= 0) {
        return 0;
    }
    char tmp[512];
    if (len >= (int)sizeof(tmp)) {
        len = (int)sizeof(tmp) - 1;
    }
    memcpy(tmp, line + start, (size_t)len);
    tmp[len] = '\0';
    int w = 0;
    (void)TTF_SizeText(font, tmp, &w, NULL);
    return w;
}

static int
memory_fontColumnWidth(TTF_Font *font)
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
memory_drawRowLine(e9ui_context_t *ctx,
                   TTF_Font *font,
                   const char *line,
                   int baseX,
                   int y)
{
    SDL_Color baseColor = {200, 220, 200, 255};
    SDL_Color addressColor = {160, 160, 200, 255};
    char addressText[9];
    int lineW = 0;
    int lineH = 0;
    int addressW = 0;
    int addressH = 0;

    if (!ctx || !ctx->renderer || !font || !line) {
        return 0;
    }

    SDL_Texture *lineTexture = e9ui_text_cache_getText(ctx->renderer, font, line, baseColor, &lineW, &lineH);
    if (lineTexture) {
        SDL_Rect dst = { baseX, y, lineW, lineH };
        SDL_RenderCopy(ctx->renderer, lineTexture, NULL, &dst);
    }

    memcpy(addressText, line, 8);
    addressText[8] = '\0';
    SDL_Texture *addressTexture = e9ui_text_cache_getText(ctx->renderer,
                                                          font,
                                                          addressText,
                                                          addressColor,
                                                          &addressW,
                                                          &addressH);
    if (addressTexture) {
        SDL_Rect dst = { baseX, y, addressW, addressH };
        SDL_RenderCopy(ctx->renderer, addressTexture, NULL, &dst);
    }

    return lineW;
}

static void
memory_drawHexColorRow(e9ui_context_t *ctx,
                       TTF_Font *font,
                       const uint8_t *rowData,
                       int baseX,
                       int y,
                       int columnWidth)
{
    if (!hex_byte_color_isEnabled() || !rowData || columnWidth <= 0) {
        return;
    }

    int hexX = baseX + MEMORY_ROW_HEX_START_COL * columnWidth;
    if (!hex_byte_color_drawHexByteRow(ctx->renderer,
                                       font,
                                       rowData,
                                       (int)MEMORY_BYTES_PER_ROW,
                                       hexX,
                                       y,
                                       columnWidth)) {
        for (unsigned int i = 0; i < MEMORY_BYTES_PER_ROW; ++i) {
            int hexCol = MEMORY_ROW_HEX_START_COL + (int)i * 3;
            hex_byte_color_drawHexByte(ctx->renderer, font, rowData[i], baseX + hexCol * columnWidth, y);
        }
    }
}

static int
memory_dataEditCursorForPoint(TTF_Font *font, const char *text, e9ui_data_edit_mode_t mode,
                              int textX, int mx)
{
    int target = 0;
    int len = 0;
    int bestCursor = 0;
    int bestDist = INT_MAX;
    int fullWidth = 0;
    int groupDigits = 0;

    if (!font || !text) {
        return 0;
    }

    target = mx - textX;
    if (target <= 0) {
        return 0;
    }
    len = (int)strlen(text);
    fullWidth = memory_measureSegment(font, text, 0, len);
    if (target >= fullWidth) {
        return len;
    }

    switch (mode) {
    case e9ui_data_edit_mode_hex_words16:
        groupDigits = 4;
        break;
    case e9ui_data_edit_mode_hex_bytes:
        groupDigits = 2;
        break;
    case e9ui_data_edit_mode_ascii_fixed:
    default:
        groupDigits = 0;
        break;
    }

    for (int i = 0; i < len; ++i) {
        int startWidth = 0;
        int endWidth = 0;
        int dist = 0;

        if (groupDigits > 0 && (i % (groupDigits + 1)) == groupDigits) {
            continue;
        }
        startWidth = memory_measureSegment(font, text, 0, i);
        endWidth = memory_measureSegment(font, text, 0, i + 1);
        if (target >= startWidth && target < endWidth) {
            return i;
        }
        dist = startWidth - target;
        if (dist < 0) {
            dist = -dist;
        }
        if (dist < bestDist) {
            bestDist = dist;
            bestCursor = i;
        }
    }

    return bestCursor;
}

static void
memory_markVisibleMatches(memory_view_state_t *st, const memory_search_pattern_t *pattern,
                          const uint8_t *viewData, unsigned int viewByteCount,
                          uint8_t *allMask, uint8_t *currentMask)
{
    if (!st || !pattern || !viewData || !allMask || !currentMask) {
        return;
    }
    memset(allMask, 0, viewByteCount);
    memset(currentMask, 0, viewByteCount);
    int maxLen = pattern->asciiLen > pattern->hexLen ? pattern->asciiLen : pattern->hexLen;
    if (maxLen <= 0) {
        return;
    }
    for (unsigned int i = 0; i < viewByteCount; ++i) {
        int matchLen = 0;
        if (!memory_patternMatchesAt(viewData + i, (int)(viewByteCount - i), pattern, &matchLen)) {
            continue;
        }
        for (int j = 0; j < matchLen && i + (unsigned int)j < viewByteCount; ++j) {
            allMask[i + (unsigned int)j] = 1;
        }
    }
    if (st->searchMatchValid && st->searchMatchLen > 0) {
        uint32_t viewStart = st->base;
        uint32_t viewEnd = viewStart + viewByteCount - 1u;
        if (st->searchMatchAddr >= viewStart && st->searchMatchAddr <= viewEnd) {
            unsigned int start = (unsigned int)(st->searchMatchAddr - viewStart);
            for (int i = 0; i < st->searchMatchLen && start + (unsigned int)i < viewByteCount; ++i) {
                currentMask[start + (unsigned int)i] = 1;
            }
        }
    }
}

static void
memory_formatRowLine(char *line, size_t cap, uint32_t rowAddr, const uint8_t *rowData)
{
    int n = 0;

    if (!line || cap == 0) {
        return;
    }

    n = snprintf(line, cap, "%08X  ", rowAddr);
    if (n < 0 || (size_t)n >= cap) {
        line[cap - 1] = '\0';
        return;
    }
    for (unsigned int i = 0; i < MEMORY_BYTES_PER_ROW; ++i) {
        n += snprintf(line + n, cap - (size_t)n, "%02X ", (unsigned)(rowData ? rowData[i] : 0u));
        if ((size_t)n >= cap) {
            line[cap - 1] = '\0';
            return;
        }
    }
    n += snprintf(line + n, cap - (size_t)n, " ");
    if ((size_t)n >= cap) {
        line[cap - 1] = '\0';
        return;
    }
    for (unsigned int i = 0; i < MEMORY_BYTES_PER_ROW && (size_t)n + 1 < cap; ++i) {
        unsigned char c = rowData ? rowData[i] : 0;
        line[n++] = (c >= 32 && c <= 126) ? (char)c : '.';
    }
    line[n] = '\0';
}

static void
memory_inlineEditCancel(memory_view_state_t *st, e9ui_context_t *ctx)
{
    e9ui_component_t *editor = NULL;

    if (!st) {
        return;
    }

    editor = memory_inlineEditComponent(st);
    st->inlineEditActive = 0;
    st->inlineEditAddr = 0;
    st->inlineEditByteCount = 0;
    st->inlineEditMode = memory_inline_edit_mode_none;
    memset(st->inlineEditOriginalBytes, 0, sizeof(st->inlineEditOriginalBytes));
    st->inlineEditRect = (SDL_Rect){0, 0, 0, 0};
    if (editor) {
        e9ui_data_edit_setText(editor, "");
        e9ui_setHidden(editor, 1);
    }
    if (ctx && editor && e9ui_getFocus(ctx) == editor) {
        e9ui_setFocus(ctx, st->ownerView);
    }
    inline_edit_pauseEnd(&st->inlineEditAutoResume);
}

static int
memory_inlineEditCommit(memory_view_state_t *st, e9ui_context_t *ctx)
{
    e9ui_component_t *editor = NULL;
    const char *text = NULL;
    uint8_t bytes[MEMORY_BYTES_PER_ROW];

    if (!st || !st->inlineEditActive) {
        return 0;
    }
    editor = memory_inlineEditComponent(st);
    if (!editor) {
        memory_inlineEditCancel(st, ctx);
        return 0;
    }

    text = e9ui_data_edit_getText(editor);
    memset(bytes, 0, sizeof(bytes));
    switch (st->inlineEditMode) {
    case memory_inline_edit_mode_ascii:
        if (!memory_parseInlineAsciiBytes(text,
                                          st->inlineEditOriginalBytes,
                                          bytes,
                                          st->inlineEditByteCount)) {
            e9ui_showTransientMessage("STRING TOO LONG");
            e9ui_data_edit_selectAllExternal(editor);
            return 0;
        }
        break;
    case memory_inline_edit_mode_hex:
        if (!memory_parseInlineHexBytes(text, bytes, st->inlineEditByteCount)) {
            e9ui_showTransientMessage("INVALID HEX FORMAT");
            e9ui_data_edit_selectAllExternal(editor);
            return 0;
        }
        break;
    default:
        memory_inlineEditCancel(st, ctx);
        return 0;
    }
    if (!memory_writeHexBytes(st, st->inlineEditAddr, bytes, st->inlineEditByteCount)) {
        e9ui_showTransientMessage("WRITE FAILED - NO CORE SUPPORT?");
        e9ui_data_edit_selectAllExternal(editor);
        return 0;
    }
    if (!memory_verifyHexBytes(st, st->inlineEditAddr, bytes, st->inlineEditByteCount)) {
        e9ui_showTransientMessage("UNABLE TO WRITE DATA - ROM ?");
        e9ui_data_edit_selectAllExternal(editor);
        return 0;
    }

    memory_inlineEditCancel(st, ctx);
    return 1;
}

static void
memory_inlineEditSubmitted(e9ui_context_t *ctx, void *user)
{
    memory_view_state_t *st = (memory_view_state_t*)user;

    (void)memory_inlineEditCommit(st, ctx);
}

static int
memory_inlineEditKey(e9ui_context_t *ctx, SDL_Keycode key, SDL_Keymod mods, void *user)
{
    memory_view_state_t *st = (memory_view_state_t*)user;

    (void)mods;
    if (!st || !st->inlineEditActive) {
        return 0;
    }
    if (key == SDLK_ESCAPE) {
        memory_inlineEditCancel(st, ctx);
        return 1;
    }
    return 0;
}

static int
memory_beginInlineEditAtPoint(e9ui_component_t *self, e9ui_context_t *ctx, memory_view_state_t *st, int mx, int my)
{
    TTF_Font *font = NULL;
    int rightGutter = 0;
    SDL_Rect contentClip;
    int pad = 8;
    int lineHeight = 0;
    int y = 0;
    int availableRows = 0;
    int row = 0;
    unsigned int off = 0;
    uint32_t rowAddr = 0;
    uint8_t rowData[MEMORY_BYTES_PER_ROW];
    char editText[64];
    char asciiText[MEMORY_BYTES_PER_ROW + 1];
    char line[256];
    int baseX = 0;
    int hexStartX = 0;
    int hexWidth = 0;
    int asciiStartX = 0;
    int asciiWidth = 0;
    int initialCursor = 0;
    SDL_Rect rect;
    e9ui_component_t *editor = NULL;
    memory_inline_edit_mode_t mode = memory_inline_edit_mode_none;
    int autoResume = 0;

    if (!self || !ctx || !st) {
        return 0;
    }
    font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    if (!font) {
        return 0;
    }

    rightGutter = memory_stepButtonsGutterWidth(ctx, self);
    if (rightGutter < 0) {
        rightGutter = 0;
    }
    if (rightGutter > self->bounds.w) {
        rightGutter = self->bounds.w;
    }
    contentClip = (SDL_Rect){ self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    contentClip.w -= rightGutter;
    if (contentClip.w <= 0) {
        return 0;
    }

    lineHeight = TTF_FontHeight(font);
    if (lineHeight <= 0) {
        lineHeight = 16;
    }
    y = self->bounds.y + pad;
    if (st->error[0]) {
        y += lineHeight;
    }
    availableRows = (self->bounds.h - pad - (y - self->bounds.y)) / lineHeight;
    if (availableRows < 1) {
        availableRows = 1;
    }
    if (mx < contentClip.x || mx >= contentClip.x + contentClip.w || my < y) {
        return 0;
    }

    row = (my - y) / lineHeight;
    if (row < 0 || row >= availableRows) {
        return 0;
    }

    off = (unsigned int)row * MEMORY_BYTES_PER_ROW;
    rowAddr = st->base + off;
    if (!memory_readEditableRange(st, rowAddr, rowData, (int)MEMORY_BYTES_PER_ROW)) {
        return 0;
    }

    memory_formatInlineHexBytes(editText, sizeof(editText), rowData, (int)MEMORY_BYTES_PER_ROW);
    memory_formatInlineAsciiBytes(asciiText, sizeof(asciiText), rowData, (int)MEMORY_BYTES_PER_ROW);
    memory_formatRowLine(line, sizeof(line), rowAddr, rowData);
    baseX = self->bounds.x + pad - st->scrollX;
    hexStartX = baseX + memory_measureSegment(font, line, 0, MEMORY_ROW_HEX_START_COL);
    hexWidth = memory_measureSegment(font, line, MEMORY_ROW_HEX_START_COL, (int)strlen(editText));
    asciiStartX = baseX + memory_measureSegment(font, line, 0, MEMORY_ROW_ASCII_START_COL);
    asciiWidth = memory_measureSegment(font, line, MEMORY_ROW_ASCII_START_COL, (int)strlen(asciiText));
    if (mx >= hexStartX - e9ui_scale_px(ctx, 4) &&
        mx < hexStartX + hexWidth + e9ui_scale_px(ctx, 8)) {
        mode = memory_inline_edit_mode_hex;
        initialCursor = memory_dataEditCursorForPoint(font,
                                                      editText,
                                                      e9ui_data_edit_mode_hex_bytes,
                                                      hexStartX,
                                                      mx);
        rect = (SDL_Rect){
            hexStartX - e9ui_scale_px(ctx, 4),
            y + row * lineHeight - e9ui_scale_px(ctx, 2),
            hexWidth + e9ui_scale_px(ctx, 12),
            lineHeight + e9ui_scale_px(ctx, 4)
        };
    } else if (mx >= asciiStartX - e9ui_scale_px(ctx, 4) &&
               mx < asciiStartX + asciiWidth + e9ui_scale_px(ctx, 8)) {
        mode = memory_inline_edit_mode_ascii;
        initialCursor = memory_dataEditCursorForPoint(font,
                                                      asciiText,
                                                      e9ui_data_edit_mode_ascii_fixed,
                                                      asciiStartX,
                                                      mx);
        rect = (SDL_Rect){
            asciiStartX - e9ui_scale_px(ctx, 4),
            y + row * lineHeight - e9ui_scale_px(ctx, 2),
            asciiWidth + e9ui_scale_px(ctx, 12),
            lineHeight + e9ui_scale_px(ctx, 4)
        };
    } else {
        return 0;
    }

    editor = memory_inlineEditComponent(st);
    if (!editor) {
        return 0;
    }
    if (!inline_edit_pauseBegin(&autoResume)) {
        return 0;
    }
    st->inlineEditActive = 1;
    st->inlineEditAutoResume = autoResume;
    st->inlineEditAddr = rowAddr;
    st->inlineEditByteCount = (int)MEMORY_BYTES_PER_ROW;
    st->inlineEditMode = mode;
    memcpy(st->inlineEditOriginalBytes, rowData, sizeof(st->inlineEditOriginalBytes));
    st->inlineEditRect = rect;
    if (st->inlineEditRect.w < e9ui_scale_px(ctx, 80)) {
        st->inlineEditRect.w = e9ui_scale_px(ctx, 80);
    }
    if (editor->preferredHeight) {
        int preferredH = editor->preferredHeight(editor, ctx, st->inlineEditRect.w);
        if (st->inlineEditRect.h < preferredH) {
            st->inlineEditRect.h = preferredH;
        }
    }
    if (mode == memory_inline_edit_mode_ascii) {
        e9ui_data_edit_setMode(editor, e9ui_data_edit_mode_ascii_fixed);
        e9ui_data_edit_setText(editor, asciiText);
    } else {
        e9ui_data_edit_setMode(editor, e9ui_data_edit_mode_hex_bytes);
        e9ui_data_edit_setText(editor, editText);
    }
    e9ui_data_edit_setCursor(editor, initialCursor);
    e9ui_setHidden(editor, 0);
    if (editor->layout) {
        editor->layout(editor, ctx, (e9ui_rect_t){
            st->inlineEditRect.x,
            st->inlineEditRect.y,
            st->inlineEditRect.w,
            st->inlineEditRect.h
        });
    } else {
        editor->bounds = (e9ui_rect_t){
            st->inlineEditRect.x,
            st->inlineEditRect.y,
            st->inlineEditRect.w,
            st->inlineEditRect.h
        };
    }
    e9ui_setFocus(ctx, editor);
    return 1;
}

static void
memory_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) {
        return;
    }
    memory_view_state_t *st = (memory_view_state_t*)self->state;
    if (!st) {
        return;
    }
    e9ui_component_t *inlineEdit = memory_inlineEditComponent(st);
    if (st->inlineEditActive && ctx && inlineEdit &&
        e9ui_getFocus(ctx) != inlineEdit &&
        e9ui_getFocus(ctx) != self) {
        memory_inlineEditCancel(st, ctx);
    }
    memory_step_buttons_action_ctx_t actionCtx = { st };
    {
        int stepEnabled = 1;
        e9ui_step_buttons_tick(ctx,
                               self->bounds,
                               0,
                               stepEnabled,
                               &st->stepButtons,
                               &actionCtx,
                               memory_stepButtonsOnAction);
    }
    SDL_Rect r = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_SetRenderDrawColor(ctx->renderer, 20, 22, 20, 255);
    SDL_RenderFillRect(ctx->renderer, &r);
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    if (!font) {
        return;
    }
    int stepEnabled = 1;
    int rightGutter = memory_stepButtonsGutterWidth(ctx, self);
    if (rightGutter < 0) {
        rightGutter = 0;
    }
    if (rightGutter > r.w) {
        rightGutter = r.w;
    }
    SDL_bool hadClip = SDL_RenderIsClipEnabled(ctx->renderer);
    SDL_Rect prevClip = {0};
    if (hadClip) {
        SDL_RenderGetClipRect(ctx->renderer, &prevClip);
    }
    SDL_Rect contentClip = r;
    contentClip.w -= rightGutter;
    if (contentClip.w < 0) {
        contentClip.w = 0;
    }
    if (contentClip.w > 0 && contentClip.h > 0) {
        if (hadClip) {
            SDL_Rect clipped;
            if (SDL_IntersectRect(&prevClip, &contentClip, &clipped)) {
                SDL_RenderSetClipRect(ctx->renderer, &clipped);
            } else {
                SDL_RenderSetClipRect(ctx->renderer, &contentClip);
            }
        } else {
            SDL_RenderSetClipRect(ctx->renderer, &contentClip);
        }
    } else {
        SDL_RenderSetClipRect(ctx->renderer, NULL);
    }
    st->contentPixelWidth = memory_hscrollContentWidthEstimate(st, font);
    if (st->contentPixelWidth < 0) {
        st->contentPixelWidth = 0;
    }
    {
        int scrollY = 0;
        int viewW = contentClip.w > 0 ? contentClip.w : 1;
        e9ui_scrollbar_clamp(viewW, 1, st->contentPixelWidth, 1, &st->scrollX, &scrollY);
    }
    int lh = TTF_FontHeight(font);
    if (lh <= 0) {
        lh = 16;
    }
    int pad = 8;
    int y = r.y + pad;
    if (st->error[0]) {
        y += lh;
    }
    int availableRows = (r.h - pad - (y - r.y)) / lh;
    if (availableRows < 1) {
        availableRows = 1;
    }
    st->rowsPerView = availableRows;
    unsigned int viewByteCount = (unsigned int)availableRows * MEMORY_BYTES_PER_ROW;
    int maxPatternLen = 0;
    memory_search_pattern_t pattern;
    int havePattern = 0;
    if (st->searchBox) {
        const char *searchText = e9ui_textbox_getText(st->searchBox);
        havePattern = memory_buildSearchPattern(searchText, &pattern);
        if (havePattern) {
            maxPatternLen = pattern.asciiLen > pattern.hexLen ? pattern.asciiLen : pattern.hexLen;
        }
    }
    unsigned int extraBytes = maxPatternLen > 1 ? (unsigned int)(maxPatternLen - 1) : 0u;
    unsigned int viewReadCount = viewByteCount + extraBytes;
    uint8_t *viewData = NULL;
    uint8_t *allMask = NULL;
    uint8_t *currentMask = NULL;
    if (viewReadCount > 0) {
        viewData = (uint8_t*)alloc_alloc(viewReadCount);
    }
    if (viewByteCount > 0 && havePattern) {
        allMask = (uint8_t*)alloc_alloc(viewByteCount);
        currentMask = (uint8_t*)alloc_alloc(viewByteCount);
    }
    if (viewData && viewReadCount > 0) {
        memory_readRange(st, st->base, viewData, viewReadCount);
    }
    if (viewByteCount > 0 && allMask && currentMask) {
        memory_markVisibleMatches(st, &pattern, viewData, viewByteCount, allMask, currentMask);
    }
    int columnWidth = memory_fontColumnWidth(font);
    y = r.y + pad;
    if (st->error[0]) {
        SDL_Color err = {220, 80, 80, 255};
        int tw = 0, th = 0;
        SDL_Texture *t = e9ui_text_cache_getText(ctx->renderer, font, st->error, err, &tw, &th);
        if (t) {
            int drawX = r.x + pad - st->scrollX;
            SDL_Rect tr = { drawX, y, tw, th };
            SDL_RenderCopy(ctx->renderer, t, NULL, &tr);
            if (tw + pad * 2 > st->contentPixelWidth) {
                st->contentPixelWidth = tw + pad * 2;
            }
        }
        y += lh;
    }
    char line[256];
    for (int row = 0; row < availableRows; ++row) {
        unsigned int off = (unsigned int)row * MEMORY_BYTES_PER_ROW;
        uint32_t rowAddr = st->base + off;
        const uint8_t *rowData = viewData ? viewData + off : NULL;
        memory_formatRowLine(line, sizeof(line), rowAddr, rowData);
        int baseX = r.x + pad - st->scrollX;
        for (unsigned int i = 0; i < MEMORY_BYTES_PER_ROW; ++i) {
            unsigned int idx = off + i;
            if (!allMask || !currentMask || idx >= viewByteCount || !allMask[idx]) {
                continue;
            }
            int hexCol = MEMORY_ROW_HEX_START_COL + (int)i * 3;
            int asciiCol = MEMORY_ROW_ASCII_START_COL + (int)i;
            int hexX = baseX + memory_measureSegment(font, line, 0, hexCol);
            int asciiX = baseX + memory_measureSegment(font, line, 0, asciiCol);
            int hexW = memory_measureSegment(font, line, hexCol, 2);
            int asciiW = memory_measureSegment(font, line, asciiCol, 1);
            SDL_Color hl = currentMask[idx] ? (SDL_Color){64, 112, 188, 220}
                                            : (SDL_Color){120, 100, 48, 170};
            SDL_SetRenderDrawColor(ctx->renderer, hl.r, hl.g, hl.b, hl.a);
            SDL_Rect hexRect = { hexX, y - 1, hexW, lh + 2 };
            SDL_Rect asciiRect = { asciiX, y - 1, asciiW, lh + 2 };
            SDL_RenderFillRect(ctx->renderer, &hexRect);
            SDL_RenderFillRect(ctx->renderer, &asciiRect);
        }
        int lineW = memory_drawRowLine(ctx, font, line, r.x + pad - st->scrollX, y);
        memory_drawHexColorRow(ctx, font, rowData, r.x + pad - st->scrollX, y, columnWidth);
        if (lineW + pad * 2 > st->contentPixelWidth) {
            st->contentPixelWidth = lineW + pad * 2;
        }
        y += lh;
        if (y > r.y + r.h - pad) {
            break;
        }
    }
    if (viewData) {
        alloc_free(viewData);
    }
    if (allMask) {
        alloc_free(allMask);
    }
    if (currentMask) {
        alloc_free(currentMask);
    }
    if (hadClip) {
        SDL_RenderSetClipRect(ctx->renderer, &prevClip);
    } else {
        SDL_RenderSetClipRect(ctx->renderer, NULL);
    }
    {
        e9ui_rect_t sbBounds = memory_hscrollBounds(ctx, self);
        int viewW = sbBounds.w > 0 ? sbBounds.w : 1;
        e9ui_scrollbar_render(self,
                              ctx,
                              sbBounds,
                              viewW,
                              1,
                              st->contentPixelWidth,
                              1,
                              st->scrollX,
                              0);
    }
    e9ui_step_buttons_render(ctx,
                             self->bounds,
                             0,
                             stepEnabled,
                             &st->stepButtons);
    if (inlineEdit && !st->inlineEditActive) {
        e9ui_setHidden(inlineEdit, 1);
    }
    if (inlineEdit && st->inlineEditActive) {
        e9ui_setHidden(inlineEdit, 0);
        if (inlineEdit->layout) {
            inlineEdit->layout(inlineEdit, ctx, (e9ui_rect_t){
                st->inlineEditRect.x,
                st->inlineEditRect.y,
                st->inlineEditRect.w,
                st->inlineEditRect.h
            });
        } else {
            inlineEdit->bounds = (e9ui_rect_t){
                st->inlineEditRect.x,
                st->inlineEditRect.y,
                st->inlineEditRect.w,
                st->inlineEditRect.h
            };
        }
        inlineEdit->render(inlineEdit, ctx);
    }
}

static void
memory_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
  (void)ctx;
  (void)self;
}

e9ui_component_t *
memory_makeComponent(void)
{
    e9ui_component_t *stack = e9ui_stack_makeVertical();
    e9ui_component_t *row = e9ui_hstack_make();
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    if (!c || !row) {
        alloc_free(c);
        return NULL;
    }
    memory_view_state_t *st = (memory_view_state_t*)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(c);
        return NULL;
    }
    c->name = "memory_view";
    c->state = st;
    c->preferredHeight = memory_preferredHeight;
    c->layout = memory_layout;
    c->render = memory_render;
    c->handleEvent = memory_handleEvent;
    c->dtor = memory_dtor;
    c->focusable = 1;
    st->ownerView = c;

    target_memory_space_t spaces[MEMORY_MAX_SPACES];
    e9ui_select_option_t options[MEMORY_MAX_SPACES];
    size_t spaceCount = 0;
    size_t spaceCap = sizeof(spaces) / sizeof(spaces[0]);
    memset(spaces, 0, sizeof(spaces));
    memset(options, 0, sizeof(options));
    if (!memory_getSpaces(spaces, spaceCap, &spaceCount) || spaceCount == 0) {
        spaces[0] = (target_memory_space_t){
            .value = "main",
            .label = "Main",
            .minAddr = 0x00000000u,
            .maxAddr = 0x00ffffffu,
            .addressDigits = 6,
            .processorMemory = 0,
            .processorId = 0
        };
        spaceCount = 1;
    }
    if (spaceCount > spaceCap) {
        spaceCount = spaceCap;
    }
    for (size_t i = 0; i < spaceCount; ++i) {
        options[i].value = spaces[i].value ? spaces[i].value : "main";
        options[i].label = spaces[i].label ? spaces[i].label : options[i].value;
    }
    if (spaces[0].value) {
        size_t len = strlen(spaces[0].value);
        if (len >= sizeof(st->spaceValue)) {
            len = sizeof(st->spaceValue) - 1;
        }
        memcpy(st->spaceValue, spaces[0].value, len);
        st->spaceValue[len] = '\0';
    }
    
    uint32_t minAddr = 0;
    uint32_t maxAddr = 0x00ffffffu;
    if (memory_getSelectedAddressLimits(st, &minAddr, &maxAddr)) {
        st->base = minAddr;
    } else {
        st->base = 0;
    }
    st->base = memory_clampBaseForView(st, st->base);
    st->rowsPerView = MEMORY_DEFAULT_VIEW_ROWS;
    memory_setError(st, NULL);

    if (spaceCount > 1) {
        st->spaceSelect = e9ui_labeled_select_make(NULL,
                                                   0,
                                                   88,
                                                   options,
                                                   (int)spaceCount,
                                                   st->spaceValue,
                                                   memory_onSpaceChanged,
                                                   st);
    }
    st->addressBox = e9ui_textbox_make(32, memory_onAddressSubmit, NULL, st);
    st->searchBox = e9ui_textbox_make(128, memory_onSearchSubmit, memory_onSearchChange, st);
    e9ui_component_t *inlineEdit = e9ui_data_edit_make((int)MEMORY_BYTES_PER_ROW, memory_inlineEditSubmitted, st);
    e9ui_textbox_setFocusBorderVisible(st->addressBox, 0);
    e9ui_textbox_setFocusBorderVisible(st->searchBox, 0);
    e9ui_textbox_setKeyHandler(st->searchBox, memory_onSearchKey, st);
    e9ui_data_edit_setKeyHandler(inlineEdit, memory_inlineEditKey, st);
    st->inlineEditMeta = alloc_strdup("inline_edit");

    e9ui_child_add(c, inlineEdit, st->inlineEditMeta);
    e9ui_setHidden(inlineEdit, 1);

    e9ui_textbox_setPlaceholder(st->addressBox, "Base address (hex)");
    e9ui_textbox_setPlaceholder(st->searchBox, "Search (hex/ascii)");
    memory_syncTextboxFromBase(st);

    if (st->spaceSelect) {
        e9ui_hstack_addFixed(row, st->spaceSelect, 88);
    }
    e9ui_hstack_addFlex(row, st->addressBox);
    e9ui_hstack_addFlex(row, st->searchBox);

    e9ui_stack_addFixed(stack, row);

    e9ui_stack_addFlex(stack, c);

    return stack;
}
