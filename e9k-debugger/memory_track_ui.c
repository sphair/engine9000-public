/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL.h>
#include <SDL_ttf.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "aux_window.h"
#include "memory_track_ui.h"
#include "alloc.h"
#include "config.h"
#include "debug.h"
#include "debugger.h"
#include "e9ui.h"
#include "e9ui_button.h"
#include "e9ui_hstack.h"
#include "e9ui_link.h"
#include "e9ui_scroll.h"
#include "e9ui_spacer.h"
#include "e9ui_stack.h"
#include "e9ui_text.h"
#include "e9ui_text_cache.h"
#include "e9ui_textbox.h"
#include "e9ui_theme.h"
#include "libretro_host.h"
#include "protect.h"
#include "state_buffer.h"
#include "state_wrap.h"
#include "trainer.h"

#define MEMORY_TRACK_UI_REGION_BASE_NEOGEO 0x00100000u
#define MEMORY_TRACK_UI_REGION_SIZE_DEFAULT 0x10000u
#define MEMORY_TRACK_UI_MAX_RANGES 8
#define MEMORY_TRACK_UI_TREND_ALL 0
#define MEMORY_TRACK_UI_TREND_INC 1
#define MEMORY_TRACK_UI_TREND_DEC 2

typedef struct memory_track_entry {
    uint32_t address;
    uint32_t value;
} memory_track_entry_t;

typedef struct memory_track_frame_data {
    uint64_t frameNo;
    memory_track_entry_t *entries;
    size_t entryCount;
} memory_track_frame_data_t;

typedef struct memory_track_marker_state {
    uint64_t frameNo;
    uint8_t *wrappedState;
    size_t wrappedStateSize;
} memory_track_marker_state_t;

typedef struct memory_track_ui memory_track_ui_t;

typedef struct memory_track_table_state {
    memory_track_ui_t *ui;
} memory_track_table_state_t;

typedef struct memory_track_overlay_body_state {
    memory_track_ui_t *ui;
} memory_track_overlay_body_state_t;

typedef struct memory_track_ranges {
    target_memory_range_t ranges[MEMORY_TRACK_UI_MAX_RANGES];
    size_t count;
    size_t totalSize;
} memory_track_ranges_t;

struct memory_track_ui {
    e9ui_window_state_t windowState;
    SDL_Window *window;
    SDL_Renderer *renderer;
    e9ui_context_t ctx;
    e9ui_component_t *root;
    e9ui_component_t *overlayBodyHost;
    e9ui_component_t *pendingRemove;
    e9ui_component_t *protectModal;
    e9ui_component_t *protectCbBlock;
    e9ui_component_t *protectCbSet;
    e9ui_component_t *protectValueBox;
    uint32_t protectAddress;
    int protectAccessSize;
    int protectRadioUpdating;
    e9ui_component_t *fullscreen;
    e9ui_component_t *headerRow;
    e9ui_component_t *hscroll;
    e9ui_component_t *scroll;
    e9ui_component_t *table;
    e9ui_component_t *modeBtn8;
    e9ui_component_t *modeBtn16;
    e9ui_component_t *modeBtn32;
    e9ui_component_t *filterBtn;
    e9ui_component_t *trendBtnAll;
    e9ui_component_t *trendBtnInc;
    e9ui_component_t *trendBtnDec;
    e9ui_component_t **frameInputs;
    size_t frameInputsCap;
    size_t frameInputsCount;
    char **frameTexts;
    size_t frameTextsCap;
    size_t frameTextsCount;
    memory_track_marker_state_t *markerStates;
    size_t markerStatesCap;
    e9ui_component_t **filterInputs;
    size_t filterInputsCap;
    size_t filterInputsCount;
    char **filterTexts;
    size_t filterTextsCap;
    size_t filterTextsCount;
    char **filterParseTexts;
    size_t filterParseCap;
    uint32_t *filterParseValues;
    int *filterParseActive;
    int *filterParseOk;
    e9ui_component_t **addressLinks;
    size_t addressLinksCap;
    size_t addressLinksCount;
    uint8_t *addressLinkProtectState;
    size_t addressLinkProtectStateCap;
    memory_track_frame_data_t *frames;
    size_t frameCount;
    uint32_t *addresses;
    size_t addressCount;
    size_t addressCap;
    uint32_t *baseAddresses;
    size_t baseAddressCount;
    size_t baseAddressCap;
    uint8_t *addressSeen;
    size_t addressSeenCap;
    uint8_t *scratchBase;
    uint8_t *scratchCur;
    uint8_t *scratchRef;
    size_t scratchSize;
    uint64_t *scratchFrameNos;
    int *scratchFrameActive;
    size_t scratchFrameCap;
    size_t *frameIndices;
    size_t frameIndicesCap;
    uint64_t *cachedFrameNos;
    int *cachedFrameActive;
    size_t cachedFrameCap;
    int cachedColumnCount;
    int cachedAccessSize;
    size_t cachedRangeCount;
    target_memory_range_t cachedRanges[MEMORY_TRACK_UI_MAX_RANGES];
    int cacheValid;
    uint64_t cachedBuildFrameNo;
    int columnCount;
    int columnWidth;
    int addressWidth;
    int columnGap;
    int modeButtonWidth;
    int modeButtonGap;
    int modeWidth;
    int filterButtonWidth;
    int filterButtonGap;
    int trendButtonWidth;
    int trendButtonGap;
    int trendWidth;
    int padding;
    int rowHeight;
    int headerHeight;
    int contentHeight;
    int hasActiveFrames;
    int accessSize;
    int requireAllColumns;
    int trendFilterMode;
    int needsRebuild;
    int needsRefresh;
    char error[128];
};

static memory_track_ui_t memory_track_ui_state = {
    .windowState.winX = E9UI_WINDOW_COORD_UNSET,
    .windowState.winY = E9UI_WINDOW_COORD_UNSET,
    .windowState.openMinWidthPx = 520,
    .windowState.openMinHeightPx = 420,
    .windowState.openCenterWhenNoSaved = 1,
};

static const aux_window_ops_t memory_track_ui_auxWindowOps = {
    .setFocus = memory_track_ui_setMainWindowFocused,
    .render = memory_track_ui_render,
};

static e9ui_window_backend_t
memory_track_ui_windowBackend(void)
{
    return e9ui_window_backend_overlay;
}

static int
memory_track_ui_isOverlayBackend(const memory_track_ui_t *ui)
{
    return ui && ui->windowState.windowHost ? 1 : 0;
}

static e9ui_rect_t
memory_track_ui_windowDefaultRect(const e9ui_context_t *ctx)
{
    e9ui_rect_t rect = {
        e9ui_scale_px(ctx, 96),
        e9ui_scale_px(ctx, 96),
        e9ui_scale_px(ctx, 900),
        e9ui_scale_px(ctx, 600)
    };
    return rect;
}

static void
memory_track_ui_destroyAddressLinks(memory_track_ui_t *ui);
static void
memory_track_ui_resetAddressLinks(memory_track_ui_t *ui);

static void
memory_track_ui_clearFrameMarkersInternal(memory_track_ui_t *ui);

static void
memory_track_ui_clearFrameMarkers(e9ui_context_t *ctx, void *user);

static int
memory_track_ui_parseFrameValue(const char *text, uint64_t *outFrame, int *outEmpty);

static int
memory_track_ui_captureWrappedCurrentState(uint8_t **outState, size_t *outSize);

static int
memory_track_ui_restoreWrappedState(const uint8_t *wrappedState, size_t wrappedStateSize, uint64_t frameNo);

static int
memory_track_ui_readRangesFromWrappedState(const uint8_t *wrappedState, size_t wrappedStateSize,
                                           uint64_t frameNo, const memory_track_ranges_t *ranges,
                                           uint8_t *out, size_t size);

static int
memory_track_ui_readFrameRangesAtIndex(memory_track_ui_t *ui, int preferredIndex, uint64_t frameNo,
                                       const memory_track_ranges_t *ranges, uint8_t *out, size_t size);

static void
memory_track_ui_sortRanges(memory_track_ranges_t *ranges);

static void
memory_track_ui_setError(memory_track_ui_t *ui, const char *fmt, ...)
{
    if (!ui) {
        return;
    }
    ui->error[0] = '\0';
    if (!fmt || !*fmt) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ui->error, sizeof(ui->error), fmt, ap);
    va_end(ap);
    ui->error[sizeof(ui->error) - 1] = '\0';
}

static void
memory_track_ui_sortRanges(memory_track_ranges_t *ranges)
{
    if (!ranges || ranges->count < 2) {
        return;
    }
    for (size_t i = 1; i < ranges->count; ++i) {
        target_memory_range_t cur = ranges->ranges[i];
        size_t j = i;
        while (j > 0) {
            target_memory_range_t prev = ranges->ranges[j - 1];
            if (prev.baseAddr < cur.baseAddr) {
                break;
            }
            if (prev.baseAddr == cur.baseAddr && prev.size <= cur.size) {
                break;
            }
            ranges->ranges[j] = prev;
            --j;
        }
        ranges->ranges[j] = cur;
    }
}

static int
memory_track_ui_parseFrameValue(const char *text, uint64_t *outFrame, int *outEmpty)
{
    if (outFrame) {
        *outFrame = 0;
    }
    if (outEmpty) {
        *outEmpty = 0;
    }
    if (!outFrame) {
        return 0;
    }
    if (!text || !*text) {
        if (outEmpty) {
            *outEmpty = 1;
        }
        return 1;
    }
    char *end = NULL;
    unsigned long long val = strtoull(text, &end, 0);
    if (!end || end == text) {
        return 0;
    }
    while (*end && isspace((unsigned char)*end)) {
        ++end;
    }
    if (*end) {
        return 0;
    }
    *outFrame = (uint64_t)val;
    return 1;
}

static int
memory_track_ui_ensureMarkerStateCap(memory_track_ui_t *ui, size_t count)
{
    if (!ui) {
        return 0;
    }
    if (count <= ui->markerStatesCap) {
        return 1;
    }
    size_t newCap = ui->markerStatesCap ? ui->markerStatesCap : 4;
    while (newCap < count) {
        newCap *= 2;
    }
    memory_track_marker_state_t *next =
        (memory_track_marker_state_t*)alloc_realloc(ui->markerStates, newCap * sizeof(*next));
    if (!next) {
        return 0;
    }
    for (size_t i = ui->markerStatesCap; i < newCap; ++i) {
        memset(&next[i], 0, sizeof(next[i]));
    }
    ui->markerStates = next;
    ui->markerStatesCap = newCap;
    return 1;
}

static void
memory_track_ui_clearMarkerStateAtIndex(memory_track_ui_t *ui, size_t index)
{
    if (!ui || !ui->markerStates || index >= ui->markerStatesCap) {
        return;
    }
    alloc_free(ui->markerStates[index].wrappedState);
    ui->markerStates[index].wrappedState = NULL;
    ui->markerStates[index].wrappedStateSize = 0;
    ui->markerStates[index].frameNo = 0;
}

static void
memory_track_ui_clearAllMarkerStates(memory_track_ui_t *ui)
{
    if (!ui || !ui->markerStates) {
        return;
    }
    for (size_t i = 0; i < ui->markerStatesCap; ++i) {
        memory_track_ui_clearMarkerStateAtIndex(ui, i);
    }
    ui->cacheValid = 0;
}

static int
memory_track_ui_setMarkerStateAtIndex(memory_track_ui_t *ui, size_t index, uint64_t frameNo,
                                      uint8_t *wrappedState, size_t wrappedStateSize)
{
    if (!ui || !wrappedState || wrappedStateSize == 0) {
        return 0;
    }
    if (!memory_track_ui_ensureMarkerStateCap(ui, index + 1)) {
        return 0;
    }
    memory_track_ui_clearMarkerStateAtIndex(ui, index);
    ui->markerStates[index].frameNo = frameNo;
    ui->markerStates[index].wrappedState = wrappedState;
    ui->markerStates[index].wrappedStateSize = wrappedStateSize;
    ui->cacheValid = 0;
    return 1;
}

static void
memory_track_ui_syncMarkerStateForText(memory_track_ui_t *ui, size_t index, const char *text)
{
    if (!ui || !ui->markerStates || index >= ui->markerStatesCap) {
        return;
    }
    memory_track_marker_state_t *marker = &ui->markerStates[index];
    if (!marker->wrappedState || marker->wrappedStateSize == 0) {
        return;
    }
    uint64_t frameNo = 0;
    int empty = 0;
    if (!memory_track_ui_parseFrameValue(text, &frameNo, &empty) || empty || frameNo != marker->frameNo) {
        memory_track_ui_clearMarkerStateAtIndex(ui, index);
        ui->cacheValid = 0;
    }
}

static int
memory_track_ui_findMarkerState(const memory_track_ui_t *ui, int preferredIndex, uint64_t frameNo,
                                const uint8_t **outState, size_t *outSize)
{
    if (outState) {
        *outState = NULL;
    }
    if (outSize) {
        *outSize = 0;
    }
    if (!ui || !ui->markerStates || frameNo == 0) {
        return 0;
    }
    if (preferredIndex >= 0 && (size_t)preferredIndex < ui->markerStatesCap) {
        const memory_track_marker_state_t *marker = &ui->markerStates[preferredIndex];
        if (marker->wrappedState && marker->wrappedStateSize > 0 && marker->frameNo == frameNo) {
            if (outState) {
                *outState = marker->wrappedState;
            }
            if (outSize) {
                *outSize = marker->wrappedStateSize;
            }
            return 1;
        }
    }
    for (size_t i = 0; i < ui->markerStatesCap; ++i) {
        if ((int)i == preferredIndex) {
            continue;
        }
        const memory_track_marker_state_t *marker = &ui->markerStates[i];
        if (!marker->wrappedState || marker->wrappedStateSize == 0 || marker->frameNo != frameNo) {
            continue;
        }
        if (outState) {
            *outState = marker->wrappedState;
        }
        if (outSize) {
            *outSize = marker->wrappedStateSize;
        }
        return 1;
    }
    return 0;
}

static int
memory_track_ui_canResolveFrame(const memory_track_ui_t *ui, int preferredIndex, uint64_t frameNo)
{
    if (frameNo == 0) {
        return 0;
    }
    if (memory_track_ui_findMarkerState(ui, preferredIndex, frameNo, NULL, NULL)) {
        return 1;
    }
    return state_buffer_hasFrameNo(frameNo);
}

static int
memory_track_ui_captureWrappedCurrentState(uint8_t **outState, size_t *outSize)
{
    if (outState) {
        *outState = NULL;
    }
    if (outSize) {
        *outSize = 0;
    }
    size_t payloadSize = 0;
    if (!outState || !outSize || !libretro_host_getSerializeSize(&payloadSize) || payloadSize == 0) {
        return 0;
    }
    size_t wrappedSize = state_wrap_wrappedSize(payloadSize);
    uint8_t *wrappedState = (uint8_t*)alloc_alloc(wrappedSize);
    if (!wrappedState) {
        return 0;
    }
    size_t headerSize = state_wrap_headerSize();
    if (!libretro_host_serializeTo(wrappedState + headerSize, payloadSize) ||
        !state_wrap_writeHeader(wrappedState, wrappedSize, payloadSize, &debugger.machine)) {
        alloc_free(wrappedState);
        return 0;
    }
    *outState = wrappedState;
    *outSize = wrappedSize;
    return 1;
}

static int
memory_track_ui_restoreWrappedState(const uint8_t *wrappedState, size_t wrappedStateSize, uint64_t frameNo)
{
    if (!wrappedState || wrappedStateSize == 0) {
        return 0;
    }
    state_wrap_info_t info;
    if (!state_wrap_parse(wrappedState, wrappedStateSize, &info)) {
        return 0;
    }
    debugger_applyStateWrapBases(&info);
    if (!libretro_host_unserializeFrom(info.payload, info.payloadSize)) {
        return 0;
    }
    debugger.frameCounter = frameNo;
    state_buffer_setCurrentFrameNo(frameNo);
    return 1;
}

static int
memory_track_ui_readRangesFromWrappedState(const uint8_t *wrappedState, size_t wrappedStateSize,
                                           uint64_t frameNo, const memory_track_ranges_t *ranges,
                                           uint8_t *out, size_t size)
{
    if (!ranges || !out || size != ranges->totalSize) {
        return 0;
    }
    if (!memory_track_ui_restoreWrappedState(wrappedState, wrappedStateSize, frameNo)) {
        return 0;
    }
    size_t offset = 0;
    for (size_t i = 0; i < ranges->count; ++i) {
        uint32_t base = ranges->ranges[i].baseAddr;
        size_t rangeSize = (size_t)ranges->ranges[i].size;
        if (!libretro_host_debugReadMemory(base, out + offset, rangeSize)) {
            return 0;
        }
        offset += rangeSize;
    }
    return 1;
}

static int
memory_track_ui_parseFrameText(memory_track_ui_t *ui, const char *text, uint64_t *outFrame, int *outEmpty)
{
    if (!ui || !outFrame) {
        return 0;
    }
    if (!memory_track_ui_parseFrameValue(text, outFrame, outEmpty)) {
        memory_track_ui_setError(ui, "Invalid frame: \"%s\"", text);
        return 0;
    }
    return 1;
}

static void
memory_track_ui_getRanges(memory_track_ranges_t *outRanges)
{
    if (!outRanges) {
        return;
    }
    memset(outRanges, 0, sizeof(*outRanges));
    outRanges->count = 1;
    outRanges->ranges[0].baseAddr = MEMORY_TRACK_UI_REGION_BASE_NEOGEO;
    outRanges->ranges[0].size = MEMORY_TRACK_UI_REGION_SIZE_DEFAULT;
    if (target && target->memoryTrackGetRanges) {
        target_memory_range_t targetRanges[MEMORY_TRACK_UI_MAX_RANGES];
        size_t targetCount = 0;
        memset(targetRanges, 0, sizeof(targetRanges));
        if (target->memoryTrackGetRanges(targetRanges, MEMORY_TRACK_UI_MAX_RANGES, &targetCount)) {
            if (targetCount > MEMORY_TRACK_UI_MAX_RANGES) {
                targetCount = MEMORY_TRACK_UI_MAX_RANGES;
            }
            size_t writeIndex = 0;
            for (size_t i = 0; i < targetCount; ++i) {
                if (targetRanges[i].size == 0) {
                    continue;
                }
                outRanges->ranges[writeIndex++] = targetRanges[i];
            }
            if (writeIndex > 0) {
                outRanges->count = writeIndex;
            }
        }
    }
    outRanges->totalSize = 0;
    for (size_t i = 0; i < outRanges->count; ++i) {
        outRanges->totalSize += (size_t)outRanges->ranges[i].size;
    }
    memory_track_ui_sortRanges(outRanges);
}

static int
memory_track_ui_readFrameRanges(uint64_t frameNo, const memory_track_ranges_t *ranges, uint8_t *out, size_t size)
{
    if (!ranges || !out || size != ranges->totalSize) {
        return 0;
    }
    if (!state_buffer_restoreFrameNo(frameNo)) {
        return 0;
    }
    debugger.frameCounter = frameNo;
    state_buffer_setCurrentFrameNo(frameNo);
    size_t offset = 0;
    for (size_t i = 0; i < ranges->count; ++i) {
        uint32_t base = ranges->ranges[i].baseAddr;
        size_t rangeSize = (size_t)ranges->ranges[i].size;
        if (!libretro_host_debugReadMemory(base, out + offset, rangeSize)) {
            return 0;
        }
        offset += rangeSize;
    }
    return 1;
}

static int
memory_track_ui_readFrameRangesAtIndex(memory_track_ui_t *ui, int preferredIndex, uint64_t frameNo,
                                       const memory_track_ranges_t *ranges, uint8_t *out, size_t size)
{
    const uint8_t *wrappedState = NULL;
    size_t wrappedStateSize = 0;
    if (memory_track_ui_findMarkerState(ui, preferredIndex, frameNo, &wrappedState, &wrappedStateSize)) {
        return memory_track_ui_readRangesFromWrappedState(wrappedState, wrappedStateSize, frameNo,
                                                          ranges, out, size);
    }
    return memory_track_ui_readFrameRanges(frameNo, ranges, out, size);
}

static int
memory_track_ui_slotToAddress(const memory_track_ranges_t *ranges, int accessSize, size_t slot, uint32_t *outAddress)
{
    if (!ranges || accessSize <= 0 || !outAddress) {
        return 0;
    }
    size_t slotBase = 0;
    for (size_t rangeIndex = 0; rangeIndex < ranges->count; ++rangeIndex) {
        uint32_t rangeSize = ranges->ranges[rangeIndex].size;
        size_t slotsInRange = (size_t)rangeSize / (size_t)accessSize;
        if (slot < slotBase + slotsInRange) {
            size_t slotInRange = slot - slotBase;
            *outAddress = ranges->ranges[rangeIndex].baseAddr +
                          (uint32_t)(slotInRange * (size_t)accessSize);
            return 1;
        }
        slotBase += slotsInRange;
    }
    return 0;
}

static int
memory_track_ui_addressToBufferOffset(const memory_track_ranges_t *ranges, int accessSize, uint32_t address, size_t *outOffset)
{
    if (!ranges || accessSize <= 0 || !outOffset) {
        return 0;
    }
    size_t byteBase = 0;
    for (size_t rangeIndex = 0; rangeIndex < ranges->count; ++rangeIndex) {
        uint32_t rangeBase = ranges->ranges[rangeIndex].baseAddr;
        uint32_t rangeSize = ranges->ranges[rangeIndex].size;
        if (address >= rangeBase && address < rangeBase + rangeSize) {
            uint32_t offset = address - rangeBase;
            if ((offset % (uint32_t)accessSize) != 0u) {
                return 0;
            }
            *outOffset = byteBase + (size_t)offset;
            return 1;
        }
        byteBase += (size_t)rangeSize;
    }
    return 0;
}

static uint32_t
memory_track_ui_readValueBE(const uint8_t *data, int size)
{
    if (!data || size <= 0) {
        return 0;
    }
    if (size == 1) {
        return (uint32_t)data[0];
    }
    if (size == 2) {
        return ((uint32_t)data[0] << 8) | (uint32_t)data[1];
    }
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static int
memory_track_ui_buildVisibleAddresses(memory_track_ui_t *ui, int columnCount, const int *frameActive)
{
    if (!ui) {
        return 0;
    }
    ui->addressCount = 0;
    if (ui->baseAddressCount == 0) {
        return 1;
    }
    if (ui->addressCap < ui->baseAddressCount) {
        uint32_t *next = (uint32_t*)alloc_realloc(ui->addresses,
                                                  ui->baseAddressCount * sizeof(uint32_t));
        if (!next) {
            return 0;
        }
        ui->addresses = next;
        ui->addressCap = ui->baseAddressCount;
    }
    for (size_t i = 0; i < ui->baseAddressCount; ++i) {
        ui->addresses[i] = ui->baseAddresses[i];
    }
    ui->addressCount = ui->baseAddressCount;

    if (ui->requireAllColumns && ui->addressCount > 0) {
        size_t activeColumns = 0;
        for (int frameIndex = 1; frameIndex < columnCount; ++frameIndex) {
            if (frameActive && frameActive[frameIndex]) {
                activeColumns++;
            }
        }
        if (activeColumns == 0) {
            ui->addressCount = 0;
        } else {
            size_t *indices = (size_t*)alloc_calloc((size_t)columnCount, sizeof(size_t));
            if (!indices) {
                return 0;
            }
            size_t writeIndex = 0;
            for (size_t addrIndex = 0; addrIndex < ui->addressCount; ++addrIndex) {
                uint32_t address = ui->addresses[addrIndex];
                int allMatch = 1;
                for (int frameIndex = 1; frameIndex < columnCount; ++frameIndex) {
                    if (!frameActive || !frameActive[frameIndex]) {
                        continue;
                    }
                    memory_track_frame_data_t *frame = &ui->frames[frameIndex];
                    size_t idx = indices[frameIndex];
                    while (idx < frame->entryCount && frame->entries[idx].address < address) {
                        idx++;
                    }
                    indices[frameIndex] = idx;
                    if (idx >= frame->entryCount || frame->entries[idx].address != address) {
                        allMatch = 0;
                        break;
                    }
                }
                if (allMatch) {
                    ui->addresses[writeIndex++] = address;
                }
            }
            ui->addressCount = writeIndex;
            alloc_free(indices);
        }
    }

    int anyFilter = 0;
    for (int frameIndex = 0; frameIndex < columnCount; ++frameIndex) {
        if (ui->filterParseActive && ui->filterParseActive[frameIndex]) {
            anyFilter = 1;
            break;
        }
    }
    if (anyFilter && ui->addressCount > 0) {
        size_t *indices = (size_t*)alloc_calloc((size_t)columnCount, sizeof(size_t));
        if (!indices) {
            return 0;
        }
        size_t writeIndex = 0;
        for (size_t addrIndex = 0; addrIndex < ui->addressCount; ++addrIndex) {
            uint32_t address = ui->addresses[addrIndex];
            int matches = 1;
            for (int frameIndex = 0; frameIndex < columnCount; ++frameIndex) {
                if (!ui->filterParseActive || !ui->filterParseActive[frameIndex]) {
                    continue;
                }
                memory_track_frame_data_t *frame = &ui->frames[frameIndex];
                size_t idx = indices[frameIndex];
                while (idx < frame->entryCount && frame->entries[idx].address < address) {
                    idx++;
                }
                indices[frameIndex] = idx;
                if (idx >= frame->entryCount || frame->entries[idx].address != address) {
                    matches = 0;
                    break;
                }
                if (frame->entries[idx].value != ui->filterParseValues[frameIndex]) {
                    matches = 0;
                    break;
                }
            }
            if (matches) {
                ui->addresses[writeIndex++] = address;
            }
        }
        ui->addressCount = writeIndex;
        alloc_free(indices);
    }

    if (ui->trendFilterMode != MEMORY_TRACK_UI_TREND_ALL && ui->addressCount > 0) {
        size_t *indices = (size_t*)alloc_calloc((size_t)columnCount, sizeof(size_t));
        if (!indices) {
            return 0;
        }
        size_t writeIndex = 0;
        for (size_t addrIndex = 0; addrIndex < ui->addressCount; ++addrIndex) {
            uint32_t address = ui->addresses[addrIndex];
            int trendSampleCount = 0;
            int trendIncreasing = 1;
            int trendDecreasing = 1;
            uint32_t trendPrevValue = 0;
            int trendHasPrev = 0;
            for (int frameIndex = 0; frameIndex < columnCount; ++frameIndex) {
                memory_track_frame_data_t *frame = &ui->frames[frameIndex];
                size_t idx = indices[frameIndex];
                while (idx < frame->entryCount && frame->entries[idx].address < address) {
                    idx++;
                }
                indices[frameIndex] = idx;
                if (idx < frame->entryCount && frame->entries[idx].address == address) {
                    uint32_t currentValue = frame->entries[idx].value;
                    if (trendHasPrev) {
                        if (currentValue <= trendPrevValue) {
                            trendIncreasing = 0;
                        }
                        if (currentValue >= trendPrevValue) {
                            trendDecreasing = 0;
                        }
                    } else {
                        trendHasPrev = 1;
                    }
                    trendPrevValue = currentValue;
                    trendSampleCount++;
                }
            }
            int keep = 0;
            if (trendSampleCount >= 2) {
                if (ui->trendFilterMode == MEMORY_TRACK_UI_TREND_INC && trendIncreasing) {
                    keep = 1;
                } else if (ui->trendFilterMode == MEMORY_TRACK_UI_TREND_DEC && trendDecreasing) {
                    keep = 1;
                }
            }
            if (keep) {
                ui->addresses[writeIndex++] = address;
            }
        }
        ui->addressCount = writeIndex;
        alloc_free(indices);
    }
    return 1;
}

static void
memory_track_ui_clearData(memory_track_ui_t *ui)
{
    if (!ui) {
        return;
    }
    if (ui->frames) {
        for (size_t frameIndex = 0; frameIndex < ui->frameCount; ++frameIndex) {
            alloc_free(ui->frames[frameIndex].entries);
        }
        alloc_free(ui->frames);
    }
    alloc_free(ui->addresses);
    alloc_free(ui->baseAddresses);
    alloc_free(ui->frameIndices);
    ui->frames = NULL;
    ui->frameCount = 0;
    ui->addresses = NULL;
    ui->addressCount = 0;
    ui->addressCap = 0;
    ui->baseAddresses = NULL;
    ui->baseAddressCount = 0;
    ui->baseAddressCap = 0;
    ui->frameIndices = NULL;
    ui->frameIndicesCap = 0;
    ui->hasActiveFrames = 0;
    ui->cachedRangeCount = 0;
    memset(ui->cachedRanges, 0, sizeof(ui->cachedRanges));
    ui->cacheValid = 0;
}

static int
memory_track_ui_isEmptyText(const char *text)
{
    return (!text || !text[0]) ? 1 : 0;
}

static int
memory_track_ui_ensureFrameTextsCap(memory_track_ui_t *ui, size_t count)
{
    if (!ui) {
        return 0;
    }
    if (count <= ui->frameTextsCap) {
        return 1;
    }
    size_t newCap = ui->frameTextsCap ? ui->frameTextsCap : 4;
    while (newCap < count) {
        newCap *= 2;
    }
    char **next = (char**)alloc_realloc(ui->frameTexts, newCap * sizeof(char*));
    if (!next) {
        return 0;
    }
    ui->frameTexts = next;
    ui->frameTextsCap = newCap;
    return 1;
}

static int
memory_track_ui_ensureFilterTextsCap(memory_track_ui_t *ui, size_t count)
{
    if (!ui) {
        return 0;
    }
    if (count <= ui->filterTextsCap) {
        return 1;
    }
    size_t newCap = ui->filterTextsCap ? ui->filterTextsCap : 4;
    while (newCap < count) {
        newCap *= 2;
    }
    char **next = (char**)alloc_realloc(ui->filterTexts, newCap * sizeof(char*));
    if (!next) {
        return 0;
    }
    ui->filterTexts = next;
    ui->filterTextsCap = newCap;
    return 1;
}

static void
memory_track_ui_setStoredFrameText(memory_track_ui_t *ui, size_t index, const char *text)
{
    if (!ui) {
        return;
    }
    size_t needed = index + 1;
    if (!memory_track_ui_ensureFrameTextsCap(ui, needed)) {
        return;
    }
    if (needed > ui->frameTextsCount) {
        for (size_t i = ui->frameTextsCount; i < needed; ++i) {
            ui->frameTexts[i] = NULL;
        }
        ui->frameTextsCount = needed;
    }
    if (index < ui->frameTextsCount) {
        alloc_free(ui->frameTexts[index]);
        ui->frameTexts[index] = NULL;
        if (text && text[0]) {
            ui->frameTexts[index] = alloc_strdup(text);
        }
        memory_track_ui_syncMarkerStateForText(ui, index, text);
    }
}

static void
memory_track_ui_storeFrameTexts(memory_track_ui_t *ui)
{
    if (!ui) {
        return;
    }
    size_t count = (ui->frameInputs && ui->frameInputsCount) ? ui->frameInputsCount : 0;
    if (count == 0) {
        return;
    }
    if (!memory_track_ui_ensureFrameTextsCap(ui, count)) {
        return;
    }
    if (count > ui->frameTextsCount) {
        for (size_t textIndex = ui->frameTextsCount; textIndex < count; ++textIndex) {
            ui->frameTexts[textIndex] = NULL;
        }
        ui->frameTextsCount = count;
    }
    for (size_t textIndex = 0; textIndex < count; ++textIndex) {
        const char *text = e9ui_textbox_getText(ui->frameInputs[textIndex]);
        memory_track_ui_setStoredFrameText(ui, textIndex, text);
    }
}

static void
memory_track_ui_storeFilterTexts(memory_track_ui_t *ui)
{
    if (!ui) {
        return;
    }
    size_t count = (ui->filterInputs && ui->filterInputsCount) ? ui->filterInputsCount : 0;
    if (count == 0) {
        return;
    }
    if (!memory_track_ui_ensureFilterTextsCap(ui, count)) {
        return;
    }
    if (count > ui->filterTextsCount) {
        for (size_t textIndex = ui->filterTextsCount; textIndex < count; ++textIndex) {
            ui->filterTexts[textIndex] = NULL;
        }
        ui->filterTextsCount = count;
    }
    for (size_t textIndex = 0; textIndex < count; ++textIndex) {
        const char *text = e9ui_textbox_getText(ui->filterInputs[textIndex]);
        alloc_free(ui->filterTexts[textIndex]);
        ui->filterTexts[textIndex] = text ? alloc_strdup(text) : NULL;
    }
}

static size_t
memory_track_ui_findEmptyFrameIndex(memory_track_ui_t *ui)
{
    if (!ui) {
        return 0;
    }
    if (ui->frameInputs && ui->frameInputsCount) {
        for (size_t i = 0; i < ui->frameInputsCount; ++i) {
            const char *text = e9ui_textbox_getText(ui->frameInputs[i]);
            if (memory_track_ui_isEmptyText(text)) {
                return i;
            }
        }
    }
    for (size_t i = 0; i < ui->frameTextsCount; ++i) {
        if (memory_track_ui_isEmptyText(ui->frameTexts[i])) {
            return i;
        }
    }
    return ui->frameTextsCount;
}

static void
memory_track_ui_setFrameTextAtIndex(memory_track_ui_t *ui, size_t index, const char *text)
{
    if (!ui) {
        return;
    }
    if (ui->frameInputs && index < ui->frameInputsCount && ui->frameInputs[index]) {
        e9ui_textbox_setText(ui->frameInputs[index], text ? text : "");
    }
    memory_track_ui_setStoredFrameText(ui, index, text);
}

static void
memory_track_ui_clearFrameMarkersInternal(memory_track_ui_t *ui)
{
    if (!ui) {
        return;
    }
    size_t count = ui->frameInputsCount;
    if (ui->frameTextsCount > count) {
        count = ui->frameTextsCount;
    }
    for (size_t i = 0; i < count; ++i) {
        memory_track_ui_setFrameTextAtIndex(ui, i, "");
    }
    memory_track_ui_clearAllMarkerStates(ui);
    ui->needsRefresh = 1;
}

static void
memory_track_ui_clearFrameMarkers(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_track_ui_clearFrameMarkersInternal((memory_track_ui_t*)user);
}

static void
memory_track_ui_destroyAddressLinks(memory_track_ui_t *ui)
{
    if (!ui || !ui->addressLinks) {
        if (ui) {
            ui->addressLinksCount = 0;
        }
        return;
    }
    size_t count = ui->addressLinksCount;
    if (count > ui->addressLinksCap) {
        count = ui->addressLinksCap;
        ui->addressLinksCount = count;
    }
    for (size_t i = 0; i < count; ++i) {
        if (ui->addressLinks[i]) {
            e9ui_childDestroy(ui->addressLinks[i], &ui->ctx);
            ui->addressLinks[i] = NULL;
        }
    }
    ui->addressLinksCount = 0;
}

static void
memory_track_ui_resetAddressLinks(memory_track_ui_t *ui)
{
    if (!ui) {
        return;
    }
    ui->addressLinksCount = 0;
}

static void
memory_track_ui_protectClose(memory_track_ui_t *ui)
{
    if (!ui || !ui->protectModal) {
        return;
    }
    e9ui_setHidden(ui->protectModal, 1);
    ui->pendingRemove = ui->protectModal;
    ui->protectModal = NULL;
    ui->protectCbBlock = NULL;
    ui->protectCbSet = NULL;
    ui->protectValueBox = NULL;
}

static void
memory_track_ui_protectCancel(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_track_ui_protectClose((memory_track_ui_t*)user);
}

static void
memory_track_ui_protectClosed(e9ui_component_t *modal, void *user)
{
    (void)modal;
    memory_track_ui_protectClose((memory_track_ui_t*)user);
}

static int
memory_track_ui_parseU32Strict(const char *s, uint32_t *out)
{
    if (out) {
        *out = 0;
    }
    if (!s || !*s || !out) {
        return 0;
    }
    const char *p = s;
    if (*p == '$') {
        ++p;
    } else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }
    if (!*p) {
        return 0;
    }
    for (const char *q = p; *q; ++q) {
        if (!isxdigit((unsigned char)*q)) {
            return 0;
        }
    }
    errno = 0;
    unsigned long long v = strtoull(p, NULL, 16);
    if (errno != 0 || v > 0xffffffffULL) {
        return 0;
    }
    *out = (uint32_t)v;
    return 1;
}

static void
memory_track_ui_protectRadioChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    memory_track_ui_t *ui = (memory_track_ui_t*)user;
    if (!ui || ui->protectRadioUpdating) {
        return;
    }
    ui->protectRadioUpdating = 1;
    if (self == ui->protectCbBlock) {
        if (ui->protectCbSet) {
            e9ui_checkbox_setSelected(ui->protectCbSet, selected ? 0 : 1, ctx);
        }
    } else if (self == ui->protectCbSet) {
        if (ui->protectCbBlock) {
            e9ui_checkbox_setSelected(ui->protectCbBlock, selected ? 0 : 1, ctx);
        }
    }
    ui->protectRadioUpdating = 0;
}

static void
memory_track_ui_protectApply(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_track_ui_t *ui = (memory_track_ui_t*)user;
    if (!ui) {
        return;
    }
    uint32_t size_bits = 0;
    if (ui->protectAccessSize == 1) {
        size_bits = 8;
    } else if (ui->protectAccessSize == 2) {
        size_bits = 16;
    } else if (ui->protectAccessSize == 4) {
        size_bits = 32;
    }
    if (size_bits == 0) {
        debug_error("protect: invalid size");
        return;
    }
    int mode_block = 0;
    int mode_set = 0;
    if (ui->protectCbBlock) {
        mode_block = e9ui_checkbox_isSelected(ui->protectCbBlock);
    }
    if (ui->protectCbSet) {
        mode_set = e9ui_checkbox_isSelected(ui->protectCbSet);
    }
    if (mode_block == mode_set) {
        debug_error("protect: choose either block or set");
        return;
    }
    uint32_t addr = ui->protectAddress & 0x00ffffffu;
    int ok = 0;
    if (mode_block) {
        ok = protect_addBlock(addr, size_bits);
    } else {
        const char *text = NULL;
        if (ui->protectValueBox) {
            text = e9ui_textbox_getText(ui->protectValueBox);
        }
        uint32_t value = 0;
        if (!memory_track_ui_parseU32Strict(text, &value)) {
            debug_error("protect: invalid set value '%s' (expected hex: 0x..., $..., or bare hex)", text ? text : "");
            return;
        }
        ok = protect_addSet(addr, value, size_bits);
    }
    if (!ok) {
        debug_error("protect: failed (core protect API missing?)");
        return;
    }
    debug_printf("protect: added\n");
    trainer_markDirty();
    ui->needsRefresh = 1;
    memory_track_ui_protectClose(ui);
}

static void
memory_track_ui_showProtectModal(memory_track_ui_t *ui, uint32_t address)
{
    if (!ui || !ui->root) {
        return;
    }
    if (ui->protectModal) {
        memory_track_ui_protectClose(ui);
    }
    int winW = 0;
    int winH = 0;
    if (ui->renderer) {
        SDL_GetRendererOutputSize(ui->renderer, &winW, &winH);
    }
    int modalW = e9ui_scale_px(&ui->ctx, 520);
    int modalH = e9ui_scale_px(&ui->ctx, 240);
    if (modalW < 1) {
        modalW = 1;
    }
    if (modalH < 1) {
        modalH = 1;
    }
    int x = (winW - modalW) / 2;
    int y = (winH - modalH) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    char title[80];
    int bits = 0;
    if (ui->accessSize == 1) {
        bits = 8;
    } else if (ui->accessSize == 2) {
        bits = 16;
    } else if (ui->accessSize == 4) {
        bits = 32;
    }
    snprintf(title, sizeof(title), "PROTECT - %06X (%d bits)", address & 0x00FFFFFFu, bits);
    e9ui_rect_t rect = { x, y, modalW, modalH };
    ui->protectAddress = address;
    ui->protectAccessSize = ui->accessSize;
    e9ui_component_t *modal = e9ui_modal_make(title, rect, memory_track_ui_protectClosed, ui);
    ui->protectModal = modal;
    if (ui->root->name && strcmp(ui->root->name, "e9ui_stack") == 0) {
        e9ui_stack_addFixed(ui->root, modal);
    } else {
        e9ui_child_add(ui->root, modal, alloc_strdup("protect_modal"));
    }

    e9ui_component_t *cb_block = e9ui_checkbox_make("Block", 1, memory_track_ui_protectRadioChanged, ui);
    e9ui_component_t *cb_set = e9ui_checkbox_make("Set Value", 0, memory_track_ui_protectRadioChanged, ui);
    e9ui_component_t *value_box = e9ui_textbox_make(16, NULL, NULL, NULL);
    ui->protectCbBlock = cb_block;
    ui->protectCbSet = cb_set;
    ui->protectValueBox = value_box;
    e9ui_textbox_setPlaceholder(value_box, "Value");
    e9ui_component_t *set_row = e9ui_hstack_make();
    int checkboxW = e9ui_scale_px(&ui->ctx, 140);
    int valueW = e9ui_scale_px(&ui->ctx, 140);
    int rowGap = e9ui_scale_px(&ui->ctx, 8);
    e9ui_hstack_addFixed(set_row, cb_set, checkboxW);
    e9ui_hstack_addFixed(set_row, e9ui_spacer_make(rowGap), rowGap);
    e9ui_hstack_addFixed(set_row, value_box, valueW);
    e9ui_hstack_addFlex(set_row, e9ui_spacer_make(1));

    e9ui_component_t *stack = e9ui_stack_makeVertical();
    e9ui_stack_addFixed(stack, cb_block);
    e9ui_stack_addFixed(stack, e9ui_vspacer_make(8));
    e9ui_stack_addFixed(stack, set_row);
    e9ui_component_t *content_box = e9ui_box_make(stack);
    e9ui_box_setPadding(content_box, 12);
    e9ui_component_t *center = e9ui_center_make(content_box);
    e9ui_center_setSize(center, 420, 120);

    e9ui_component_t *btn_protect = e9ui_button_make("Protect", memory_track_ui_protectApply, ui);
    e9ui_component_t *btn_cancel = e9ui_button_make("Cancel", memory_track_ui_protectCancel, ui);
    e9ui_component_t *footer = e9ui_flow_make();
    e9ui_flow_setPadding(footer, 0);
    e9ui_flow_setSpacing(footer, 8);
    e9ui_flow_setWrap(footer, 0);
    e9ui_button_setTheme(btn_protect, e9ui_theme_button_preset_green());
    e9ui_button_setGlowPulse(btn_protect, 1);
    e9ui_flow_add(footer, btn_protect);
    e9ui_button_setTheme(btn_cancel, e9ui_theme_button_preset_red());
    e9ui_button_setGlowPulse(btn_cancel, 1);
    e9ui_flow_add(footer, btn_cancel);
    e9ui_component_t *overlay = e9ui_overlay_make(center, footer);
    e9ui_overlay_setAnchor(overlay, e9ui_anchor_bottom_right);
    e9ui_overlay_setMargin(overlay, 12);
    e9ui_modal_setBodyChild(modal, overlay, &ui->ctx);
}

static void
memory_track_ui_addressLinkClicked(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_track_ui_t *ui = &memory_track_ui_state;
    uint32_t address = (uint32_t)(uintptr_t)user & 0x00ffffffu;
    if (!ui) {
        return;
    }

    uint32_t sizeBits = 8;
    if (ui->accessSize == 2) {
        sizeBits = 16;
    } else if (ui->accessSize == 4) {
        sizeBits = 32;
    }

    e9k_debug_protect_t protects[E9K_PROTECT_COUNT];
    size_t protectCount = 0;
    uint64_t enabledMask = 0;
    if (!libretro_host_debugReadProtects(protects, E9K_PROTECT_COUNT, &protectCount) ||
        !libretro_host_debugGetProtectEnabledMask(&enabledMask)) {
        memory_track_ui_showProtectModal(ui, address);
        return;
    }

    int matchedEnabledIndex = -1;
    int matchedDisabledIndex = -1;
    for (size_t i = 0; i < protectCount && i < E9K_PROTECT_COUNT; ++i) {
        const e9k_debug_protect_t *p = &protects[i];
        if (p->sizeBits != sizeBits) {
            continue;
        }
        if ((address & p->addrMask) != (p->addr & p->addrMask)) {
            continue;
        }
        if (((enabledMask >> i) & 1ull) != 0ull) {
            matchedEnabledIndex = (int)i;
            break;
        }
        if (matchedDisabledIndex < 0) {
            matchedDisabledIndex = (int)i;
        }
    }

    if (matchedEnabledIndex >= 0) {
        if (!libretro_host_debugRemoveProtect((uint32_t)matchedEnabledIndex)) {
            debug_error("protect: failed to remove");
            return;
        }
        trainer_markDirty();
        ui->needsRefresh = 1;
        return;
    }

    if (matchedDisabledIndex >= 0) {
        uint64_t nextMask = enabledMask | (1ull << (uint32_t)matchedDisabledIndex);
        if (!libretro_host_debugSetProtectEnabledMask(nextMask)) {
            debug_error("protect: failed to enable");
            return;
        }
        trainer_markDirty();
        ui->needsRefresh = 1;
        return;
    }

    memory_track_ui_showProtectModal(ui, address);
}

static void
memory_track_ui_updateButtonTheme(e9ui_component_t *button, int active, const e9k_theme_button_t *activeTheme)
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
memory_track_ui_updateModeButtons(memory_track_ui_t *ui)
{
    if (!ui) {
        return;
    }
    const e9k_theme_button_t *activeTheme = e9ui_theme_button_preset_profile_active();
    memory_track_ui_updateButtonTheme(ui->modeBtn8, ui->accessSize == 1, activeTheme);
    memory_track_ui_updateButtonTheme(ui->modeBtn16, ui->accessSize == 2, activeTheme);
    memory_track_ui_updateButtonTheme(ui->modeBtn32, ui->accessSize == 4, activeTheme);
}

static void
memory_track_ui_updateFilterButton(memory_track_ui_t *ui)
{
    if (!ui || !ui->filterBtn) {
        return;
    }
    const e9k_theme_button_t *activeTheme = e9ui_theme_button_preset_profile_active();
    memory_track_ui_updateButtonTheme(ui->filterBtn, ui->requireAllColumns, activeTheme);
}

static void
memory_track_ui_updateTrendButtons(memory_track_ui_t *ui)
{
    if (!ui) {
        return;
    }
    const e9k_theme_button_t *activeTheme = e9ui_theme_button_preset_profile_active();
    memory_track_ui_updateButtonTheme(ui->trendBtnAll, ui->trendFilterMode == MEMORY_TRACK_UI_TREND_ALL, activeTheme);
    memory_track_ui_updateButtonTheme(ui->trendBtnInc, ui->trendFilterMode == MEMORY_TRACK_UI_TREND_INC, activeTheme);
    memory_track_ui_updateButtonTheme(ui->trendBtnDec, ui->trendFilterMode == MEMORY_TRACK_UI_TREND_DEC, activeTheme);
}

static void
memory_track_ui_setTrendFilter(memory_track_ui_t *ui, int mode)
{
    if (!ui) {
        return;
    }
    if (mode != MEMORY_TRACK_UI_TREND_ALL &&
        mode != MEMORY_TRACK_UI_TREND_INC &&
        mode != MEMORY_TRACK_UI_TREND_DEC) {
        return;
    }
    if (ui->trendFilterMode == mode) {
        return;
    }
    ui->trendFilterMode = mode;
    memory_track_ui_updateTrendButtons(ui);
    ui->needsRefresh = 1;
}

static void
memory_track_ui_setTrendAll(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_track_ui_setTrendFilter((memory_track_ui_t*)user, MEMORY_TRACK_UI_TREND_ALL);
}

static void
memory_track_ui_setTrendInc(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_track_ui_setTrendFilter((memory_track_ui_t*)user, MEMORY_TRACK_UI_TREND_INC);
}

static void
memory_track_ui_setTrendDec(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_track_ui_setTrendFilter((memory_track_ui_t*)user, MEMORY_TRACK_UI_TREND_DEC);
}

static void
memory_track_ui_setAccessSize(memory_track_ui_t *ui, int size)
{
    if (!ui) {
        return;
    }
    if (size != 1 && size != 2 && size != 4) {
        return;
    }
    if (ui->accessSize == size) {
        return;
    }
    ui->accessSize = size;
    memory_track_ui_updateModeButtons(ui);
    ui->needsRefresh = 1;
}

static void
memory_track_ui_toggleRequireAll(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_track_ui_t *ui = (memory_track_ui_t*)user;
    if (!ui) {
        return;
    }
    ui->requireAllColumns = ui->requireAllColumns ? 0 : 1;
    memory_track_ui_updateFilterButton(ui);
    ui->needsRefresh = 1;
}

static void
memory_track_ui_access8(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_track_ui_setAccessSize((memory_track_ui_t*)user, 1);
}

static void
memory_track_ui_access16(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_track_ui_setAccessSize((memory_track_ui_t*)user, 2);
}

static void
memory_track_ui_access32(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_track_ui_setAccessSize((memory_track_ui_t*)user, 4);
}

static int
memory_track_ui_buildAddressLinks(memory_track_ui_t *ui)
{
    if (!ui) {
        return 0;
    }
    size_t prevCount = ui->addressLinksCount;
    memory_track_ui_resetAddressLinks(ui);
    if (ui->addressCount == 0) {
        return 1;
    }
    if (ui->addressCount > ui->addressLinksCap) {
        size_t newCap = ui->addressLinksCap ? ui->addressLinksCap : 64;
        while (newCap < ui->addressCount) {
            newCap *= 2;
        }
        e9ui_component_t **next = (e9ui_component_t**)alloc_realloc(ui->addressLinks,
                                                                     newCap * sizeof(e9ui_component_t*));
        if (!next) {
            return 0;
        }
        ui->addressLinks = next;
        ui->addressLinksCap = newCap;
    }
    if (ui->addressCount > ui->addressLinkProtectStateCap) {
        size_t newCap = ui->addressLinkProtectStateCap ? ui->addressLinkProtectStateCap : 64;
        while (newCap < ui->addressCount) {
            newCap *= 2;
        }
        uint8_t *next = (uint8_t*)alloc_realloc(ui->addressLinkProtectState, newCap * sizeof(uint8_t));
        if (!next) {
            return 0;
        }
        ui->addressLinkProtectState = next;
        ui->addressLinkProtectStateCap = newCap;
    }
    if (prevCount > ui->addressLinksCap) {
        prevCount = ui->addressLinksCap;
    }
    uint32_t sizeBits = 8;
    if (ui->accessSize == 2) {
        sizeBits = 16;
    } else if (ui->accessSize == 4) {
        sizeBits = 32;
    }
    e9k_debug_protect_t protects[E9K_PROTECT_COUNT];
    size_t protectCount = 0;
    uint64_t enabledMask = 0;
    int haveProtects = libretro_host_debugReadProtects(protects, E9K_PROTECT_COUNT, &protectCount) &&
                       libretro_host_debugGetProtectEnabledMask(&enabledMask);
    for (size_t i = 0; i < ui->addressCount; ++i) {
        char text[16];
        uint32_t address = ui->addresses[i];
        snprintf(text, sizeof(text), "0x%06X", address & 0x00FFFFFFu);
        e9ui_component_t *link = NULL;
        if (i < prevCount) {
            link = ui->addressLinks[i];
        }
        if (!link) {
            link = e9ui_link_make(text, memory_track_ui_addressLinkClicked,
                                  (void*)(uintptr_t)address);
        } else {
            e9ui_link_setText(link, text);
            e9ui_link_setUser(link, (void*)(uintptr_t)address);
        }
        ui->addressLinks[i] = link;
        if (!link) {
            ui->addressLinksCount = i;
            return 0;
        }
        int protectState = 0;
        if (haveProtects) {
            for (size_t pi = 0; pi < protectCount && pi < E9K_PROTECT_COUNT; ++pi) {
                const e9k_debug_protect_t *p = &protects[pi];
                if (p->sizeBits != sizeBits) {
                    continue;
                }
                if ((address & p->addrMask) != (p->addr & p->addrMask)) {
                    continue;
                }
                if (((enabledMask >> pi) & 1ull) != 0ull) {
                    protectState = 1;
                    break;
                }
                if (protectState == 0) {
                    protectState = 2;
                }
            }
        }
        if (protectState == 1) {
            ui->addressLinkProtectState[i] = 1;
        } else if (protectState == 2) {
            ui->addressLinkProtectState[i] = 2;
        } else {
            ui->addressLinkProtectState[i] = 0;
        }
    }
    ui->addressLinksCount = ui->addressCount;
    return 1;
}

static int
memory_track_ui_collectData(memory_track_ui_t *ui, int columnCount)
{
    if (!ui || columnCount <= 0) {
        return 0;
    }

    uint64_t restoreFrame = state_buffer_getCurrentFrameNo();
    uint64_t currentFrameNo = restoreFrame;
    uint8_t *restoreState = NULL;
    size_t restoreStateSize = 0;
    memory_track_ranges_t ranges = {0};
    memory_track_ui_getRanges(&ranges);
    size_t regionSize = ranges.totalSize;
    if (ui->scratchSize != regionSize) {
        alloc_free(ui->scratchBase);
        alloc_free(ui->scratchCur);
        alloc_free(ui->scratchRef);
        ui->scratchBase = NULL;
        ui->scratchCur = NULL;
        ui->scratchRef = NULL;
        ui->scratchSize = 0;
    }
    if (!ui->scratchBase) {
        ui->scratchBase = (uint8_t*)alloc_alloc(regionSize);
    }
    if (!ui->scratchCur) {
        ui->scratchCur = (uint8_t*)alloc_alloc(regionSize);
    }
    if (!ui->scratchRef) {
        ui->scratchRef = (uint8_t*)alloc_alloc(regionSize);
    }
    ui->scratchSize = regionSize;
    if (ui->scratchFrameCap < (size_t)columnCount) {
        size_t newCap = ui->scratchFrameCap ? ui->scratchFrameCap : 4;
        while (newCap < (size_t)columnCount) {
            newCap *= 2;
        }
        uint64_t *nextNos = (uint64_t*)alloc_realloc(ui->scratchFrameNos, newCap * sizeof(uint64_t));
        int *nextActive = (int*)alloc_realloc(ui->scratchFrameActive, newCap * sizeof(int));
        if (!nextNos || !nextActive) {
            alloc_free(nextNos);
            alloc_free(nextActive);
        } else {
            ui->scratchFrameNos = nextNos;
            ui->scratchFrameActive = nextActive;
            ui->scratchFrameCap = newCap;
        }
    }

    uint8_t *refBytes = ui->scratchRef;
    uint8_t *baseBytes = ui->scratchBase;
    uint8_t *curBytes = ui->scratchCur;
    uint64_t *frameNos = ui->scratchFrameNos;
    int *frameActive = ui->scratchFrameActive;
    if (!baseBytes || !curBytes || !refBytes || !frameNos || !frameActive) {
        memory_track_ui_setError(ui, "Out of memory");
        memory_track_ui_clearData(ui);
        return 0;
    }
    if (!memory_track_ui_captureWrappedCurrentState(&restoreState, &restoreStateSize)) {
        memory_track_ui_setError(ui, "Failed to capture current state");
        memory_track_ui_clearData(ui);
        return 0;
    }
    memset(frameNos, 0, (size_t)columnCount * sizeof(uint64_t));
    memset(frameActive, 0, (size_t)columnCount * sizeof(int));

    int success = 0;
    int usedCache = 0;
    int hasActive = 0;
    int accessSize = ui->accessSize > 0 ? ui->accessSize : 1;

    for (int frameIndex = 0; frameIndex < columnCount; ++frameIndex) {
        if (!ui->frameInputs || frameIndex >= (int)ui->frameInputsCount) {
            continue;
        }
        const char *text = e9ui_textbox_getText(ui->frameInputs[frameIndex]);
        uint64_t frameNo = 0;
        int empty = 0;
        if (!memory_track_ui_parseFrameText(ui, text, &frameNo, &empty)) {
            goto cleanup;
        }
        if (empty) {
            continue;
        }
        if (!memory_track_ui_canResolveFrame(ui, frameIndex, frameNo)) {
            memory_track_ui_setError(ui, "Frame %llu not available", (unsigned long long)frameNo);
            goto cleanup;
        }
        frameActive[frameIndex] = 1;
        frameNos[frameIndex] = frameNo;
        if (frameIndex > 0) {
            hasActive = 1;
        }
    }

    if (ui->filterParseCap < (size_t)columnCount) {
        size_t newCap = ui->filterParseCap ? ui->filterParseCap : 4;
        while (newCap < (size_t)columnCount) {
            newCap *= 2;
        }
        char **nextTexts = (char**)alloc_calloc(newCap, sizeof(char*));
        uint32_t *nextValues = (uint32_t*)alloc_calloc(newCap, sizeof(uint32_t));
        int *nextActive = (int*)alloc_calloc(newCap, sizeof(int));
        int *nextOk = (int*)alloc_calloc(newCap, sizeof(int));
        if (!nextTexts || !nextValues || !nextActive || !nextOk) {
            alloc_free(nextTexts);
            alloc_free(nextValues);
            alloc_free(nextActive);
            alloc_free(nextOk);
            memory_track_ui_setError(ui, "Out of memory");
            goto cleanup;
        }
        for (size_t i = 0; i < ui->filterParseCap; ++i) {
            nextTexts[i] = ui->filterParseTexts ? ui->filterParseTexts[i] : NULL;
            nextValues[i] = ui->filterParseValues ? ui->filterParseValues[i] : 0;
            nextActive[i] = ui->filterParseActive ? ui->filterParseActive[i] : 0;
            nextOk[i] = ui->filterParseOk ? ui->filterParseOk[i] : 1;
        }
        for (size_t i = ui->filterParseCap; i < newCap; ++i) {
            nextOk[i] = 1;
        }
        alloc_free(ui->filterParseTexts);
        alloc_free(ui->filterParseValues);
        alloc_free(ui->filterParseActive);
        alloc_free(ui->filterParseOk);
        ui->filterParseTexts = nextTexts;
        ui->filterParseValues = nextValues;
        ui->filterParseActive = nextActive;
        ui->filterParseOk = nextOk;
        ui->filterParseCap = newCap;
    }

    for (int frameIndex = 0; frameIndex < columnCount; ++frameIndex) {
        const char *text = NULL;
        if (ui->filterInputs && frameIndex < (int)ui->filterInputsCount && ui->filterInputs[frameIndex]) {
            text = e9ui_textbox_getText(ui->filterInputs[frameIndex]);
        } else if (ui->filterTexts && frameIndex < (int)ui->filterTextsCount) {
            text = ui->filterTexts[frameIndex];
        }
        if (!text) {
            text = "";
        }
        int changed = 1;
        if (ui->filterParseTexts && ui->filterParseTexts[frameIndex]) {
            if (strcmp(ui->filterParseTexts[frameIndex], text) == 0) {
                changed = 0;
            }
        } else if (text[0] == '\0') {
            changed = 0;
        }
        if (!changed && text[0] == '\0') {
            ui->filterParseOk[frameIndex] = 1;
            ui->filterParseActive[frameIndex] = 0;
            ui->filterParseValues[frameIndex] = 0;
            continue;
        }
        if (changed) {
            if (ui->filterParseTexts && ui->filterParseTexts[frameIndex]) {
                alloc_free(ui->filterParseTexts[frameIndex]);
                ui->filterParseTexts[frameIndex] = NULL;
            }
            if (text[0] != '\0') {
                ui->filterParseTexts[frameIndex] = alloc_strdup(text);
            }
            ui->filterParseActive[frameIndex] = 0;
            ui->filterParseValues[frameIndex] = 0;
            ui->filterParseOk[frameIndex] = 1;
            const char *p = text;
            while (*p && isspace((unsigned char)*p)) {
                p++;
            }
            if (p[0] != '\0') {
                if (p[0] == '$') {
                    p += 1;
                } else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
                    p += 2;
                }
                char *end = NULL;
                unsigned long long val = strtoull(p, &end, 16);
                if (!end || end == p) {
                    ui->filterParseOk[frameIndex] = 0;
                } else {
                    while (*end && isspace((unsigned char)*end)) {
                        end++;
                    }
                    if (*end) {
                        ui->filterParseOk[frameIndex] = 0;
                    }
                }
                if (!ui->filterParseOk[frameIndex]) {
                    memory_track_ui_setError(ui, "Invalid filter: \"%s\"", p);
                    goto cleanup;
                }
                ui->filterParseValues[frameIndex] = (uint32_t)val;
                ui->filterParseActive[frameIndex] = 1;
            } else {
                ui->filterParseOk[frameIndex] = 1;
                ui->filterParseActive[frameIndex] = 0;
            }
        }
        if (ui->filterParseOk && !ui->filterParseOk[frameIndex]) {
            memory_track_ui_setError(ui, "Invalid filter");
            goto cleanup;
        }
    }

    int cacheUsable = 0;
    if (ui->cacheValid && ui->frames && ui->baseAddresses &&
        ui->cachedAccessSize == accessSize &&
        ui->cachedColumnCount == columnCount &&
        ui->cachedRangeCount == ranges.count) {
        cacheUsable = 1;
        for (size_t i = 0; i < ranges.count; ++i) {
            if (ui->cachedRanges[i].baseAddr != ranges.ranges[i].baseAddr ||
                ui->cachedRanges[i].size != ranges.ranges[i].size) {
                cacheUsable = 0;
                break;
            }
        }
        if (currentFrameNo <= ui->cachedBuildFrameNo) {
            cacheUsable = 0;
        }
        for (int i = 0; i < columnCount; ++i) {
            if (!ui->cachedFrameActive || !ui->cachedFrameNos) {
                cacheUsable = 0;
                break;
            }
            if (ui->cachedFrameActive[i] != frameActive[i] ||
                ui->cachedFrameNos[i] != frameNos[i]) {
                cacheUsable = 0;
                break;
            }
        }
    }

    if (cacheUsable) {
        usedCache = 1;
        ui->frameCount = (size_t)columnCount;
        if (!memory_track_ui_buildVisibleAddresses(ui, columnCount, frameActive)) {
            memory_track_ui_setError(ui, "Out of memory");
            goto cleanup;
        }
        if (!memory_track_ui_buildAddressLinks(ui)) {
            memory_track_ui_setError(ui, "Out of memory");
            goto cleanup;
        }
        ui->hasActiveFrames = hasActive;
        memory_track_ui_setError(ui, NULL);
        success = 1;
        goto cleanup;
    }

    memory_track_ui_destroyAddressLinks(ui);
    memory_track_ui_clearData(ui);
    ui->frames = (memory_track_frame_data_t*)alloc_calloc((size_t)columnCount, sizeof(*ui->frames));
    if (!ui->frames) {
        memory_track_ui_setError(ui, "Out of memory");
        return 0;
    }

    size_t addressSlots = 0;
    for (size_t rangeIndex = 0; rangeIndex < ranges.count; ++rangeIndex) {
        addressSlots += (size_t)ranges.ranges[rangeIndex].size / (size_t)accessSize;
    }
    if (addressSlots > ui->addressSeenCap) {
        uint8_t *next = (uint8_t*)alloc_realloc(ui->addressSeen, addressSlots);
        if (!next) {
            memory_track_ui_setError(ui, "Out of memory");
            goto cleanup;
        }
        ui->addressSeen = next;
        ui->addressSeenCap = addressSlots;
    }
    if (addressSlots > 0) {
        memset(ui->addressSeen, 0, addressSlots);
    }
    size_t uniqueAddressCount = 0;
    uint64_t prevActiveFrameNo = 0;
    int hasPrevActive = 0;
    int curBytesValid = 0;
    for (int frameIndex = 0; frameIndex < columnCount; ++frameIndex) {
        ui->frames[frameIndex].frameNo = 0;
        ui->frames[frameIndex].entries = NULL;
        ui->frames[frameIndex].entryCount = 0;
        if (!frameActive[frameIndex]) {
            continue;
        }
        uint64_t frameNo = frameNos[frameIndex];
        ui->frames[frameIndex].frameNo = frameNo;
    }

    if (frameActive[0]) {
        if (!memory_track_ui_readFrameRangesAtIndex(ui, 0, frameNos[0], &ranges, refBytes, regionSize)) {
            memory_track_ui_setError(ui, "Failed to read frame %llu",
                                     (unsigned long long)frameNos[0]);
            goto cleanup;
        }
        prevActiveFrameNo = frameNos[0];
        hasPrevActive = 1;
    }

    for (int frameIndex = 1; frameIndex < columnCount; ++frameIndex) {
        if (!frameActive[frameIndex]) {
            continue;
        }
        uint64_t frameNo = frameNos[frameIndex];
        uint64_t baseFrameNo = 0;
        if (hasPrevActive) {
            baseFrameNo = prevActiveFrameNo;
        } else {
            if (frameNo == 0 || !memory_track_ui_canResolveFrame(ui, -1, frameNo - 1)) {
                memory_track_ui_setError(ui, "Previous frame %llu not available",
                                         (unsigned long long)(frameNo - 1));
                goto cleanup;
            }
            baseFrameNo = frameNo - 1;
        }
        if (hasPrevActive && baseFrameNo == prevActiveFrameNo) {
            if (curBytesValid) {
                uint8_t *tmp = baseBytes;
                baseBytes = curBytes;
                curBytes = tmp;
            } else if (frameActive[0] && frameNos[0] == prevActiveFrameNo) {
                memcpy(baseBytes, refBytes, regionSize);
            } else {
                if (!memory_track_ui_readFrameRangesAtIndex(ui, -1, baseFrameNo, &ranges, baseBytes, regionSize)) {
                    memory_track_ui_setError(ui, "Failed to read frame %llu",
                                             (unsigned long long)baseFrameNo);
                    goto cleanup;
                }
            }
        } else {
            if (!memory_track_ui_readFrameRangesAtIndex(ui, -1, baseFrameNo, &ranges, baseBytes, regionSize)) {
                memory_track_ui_setError(ui, "Failed to read frame %llu",
                                         (unsigned long long)baseFrameNo);
                goto cleanup;
            }
        }
        if (!memory_track_ui_readFrameRangesAtIndex(ui, frameIndex, frameNo, &ranges, curBytes, regionSize)) {
            memory_track_ui_setError(ui, "Failed to read frame %llu", (unsigned long long)frameNo);
            goto cleanup;
        }
        memory_track_entry_t *entries = NULL;
        size_t maxEntries = addressSlots;
        size_t entryCount = 0;
        if (maxEntries > 0) {
            entries = (memory_track_entry_t*)alloc_alloc(maxEntries * sizeof(*entries));
            if (!entries) {
                memory_track_ui_setError(ui, "Out of memory");
                goto cleanup;
            }
        }
        if (entries) {
            size_t slot = 0;
            size_t rangeOffset = 0;
            for (size_t rangeIndex = 0; rangeIndex < ranges.count; ++rangeIndex) {
                uint32_t rangeBase = ranges.ranges[rangeIndex].baseAddr;
                size_t rangeSize = (size_t)ranges.ranges[rangeIndex].size;
                for (size_t offset = 0; offset + (size_t)accessSize <= rangeSize; offset += (size_t)accessSize) {
                    size_t byteOffset = rangeOffset + offset;
                    uint32_t baseValue = memory_track_ui_readValueBE(baseBytes + byteOffset, accessSize);
                    uint32_t curValue = memory_track_ui_readValueBE(curBytes + byteOffset, accessSize);
                    if (baseValue != curValue) {
                        entries[entryCount].address = rangeBase + (uint32_t)offset;
                        entries[entryCount].value = curValue;
                        entryCount++;
                        if (slot < addressSlots && !ui->addressSeen[slot]) {
                            ui->addressSeen[slot] = 1;
                            uniqueAddressCount++;
                        }
                    }
                    slot++;
                }
                rangeOffset += rangeSize;
            }
        }
        if (entryCount == 0) {
            alloc_free(entries);
            entries = NULL;
        } else if (entryCount < maxEntries) {
            memory_track_entry_t *shrunk =
                (memory_track_entry_t*)alloc_realloc(entries, entryCount * sizeof(*entries));
            if (shrunk) {
                entries = shrunk;
            }
        }
        ui->frames[frameIndex].frameNo = frameNo;
        ui->frames[frameIndex].entries = entries;
        ui->frames[frameIndex].entryCount = entryCount;
        hasActive = 1;
        prevActiveFrameNo = frameNo;
        hasPrevActive = 1;
        curBytesValid = 1;
    }

    if (uniqueAddressCount > 0) {
        ui->baseAddresses = (uint32_t*)alloc_alloc(uniqueAddressCount * sizeof(uint32_t));
        if (!ui->baseAddresses) {
            memory_track_ui_setError(ui, "Out of memory");
            goto cleanup;
        }
        ui->baseAddressCap = uniqueAddressCount;
        for (size_t slot = 0; slot < addressSlots; ++slot) {
            if (!ui->addressSeen[slot]) {
                continue;
            }
            uint32_t address = 0;
            if (!memory_track_ui_slotToAddress(&ranges, accessSize, slot, &address)) {
                continue;
            }
            ui->baseAddresses[ui->baseAddressCount++] = address;
        }
    }

    if (frameActive[0] && ui->baseAddressCount > 0) {
        size_t entryCount = ui->baseAddressCount;
        memory_track_entry_t *entries = (memory_track_entry_t*)alloc_alloc(entryCount * sizeof(*entries));
        if (!entries) {
            memory_track_ui_setError(ui, "Out of memory");
            goto cleanup;
        }
        for (size_t entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
            uint32_t address = ui->baseAddresses[entryIndex];
            size_t offset = 0;
            if (!memory_track_ui_addressToBufferOffset(&ranges, accessSize, address, &offset)) {
                continue;
            }
            entries[entryIndex].address = address;
            entries[entryIndex].value = memory_track_ui_readValueBE(refBytes + offset, accessSize);
        }
        ui->frames[0].entries = entries;
        ui->frames[0].entryCount = entryCount;
    }

    if (!memory_track_ui_buildVisibleAddresses(ui, columnCount, frameActive)) {
        memory_track_ui_setError(ui, "Out of memory");
        goto cleanup;
    }
    if (!memory_track_ui_buildAddressLinks(ui)) {
        memory_track_ui_setError(ui, "Out of memory");
        goto cleanup;
    }

    ui->frameCount = (size_t)columnCount;
    ui->hasActiveFrames = hasActive;
    if (ui->cachedFrameCap < (size_t)columnCount) {
        size_t newCap = ui->cachedFrameCap ? ui->cachedFrameCap : 4;
        while (newCap < (size_t)columnCount) {
            newCap *= 2;
        }
        uint64_t *nextNos = (uint64_t*)alloc_realloc(ui->cachedFrameNos, newCap * sizeof(uint64_t));
        int *nextActive = (int*)alloc_realloc(ui->cachedFrameActive, newCap * sizeof(int));
        if (!nextNos || !nextActive) {
            alloc_free(nextNos);
            alloc_free(nextActive);
            memory_track_ui_setError(ui, "Out of memory");
            goto cleanup;
        }
        ui->cachedFrameNos = nextNos;
        ui->cachedFrameActive = nextActive;
        ui->cachedFrameCap = newCap;
    }
    for (int i = 0; i < columnCount; ++i) {
        ui->cachedFrameNos[i] = frameNos[i];
        ui->cachedFrameActive[i] = frameActive[i];
    }
    ui->cachedAccessSize = accessSize;
    ui->cachedColumnCount = columnCount;
    ui->cachedRangeCount = ranges.count;
    memset(ui->cachedRanges, 0, sizeof(ui->cachedRanges));
    for (size_t i = 0; i < ranges.count; ++i) {
        ui->cachedRanges[i] = ranges.ranges[i];
    }
    ui->cacheValid = 1;
    ui->cachedBuildFrameNo = currentFrameNo;
    memory_track_ui_setError(ui, NULL);
    success = 1;

cleanup:
    if (restoreState && restoreStateSize > 0) {
        (void)memory_track_ui_restoreWrappedState(restoreState, restoreStateSize, restoreFrame);
    } else {
        (void)state_buffer_restoreFrameNo(restoreFrame);
        debugger.frameCounter = restoreFrame;
        state_buffer_setCurrentFrameNo(restoreFrame);
    }
    alloc_free(restoreState);
    if (!success && !usedCache) {
        memory_track_ui_clearData(ui);
    }
    return success;
}

static int
memory_track_ui_measureTextWidth(e9ui_context_t *ctx, const char *text)
{
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : (ctx ? ctx->font : NULL);
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
memory_track_ui_updateMetrics(memory_track_ui_t *ui, int winW)
{
    if (!ui) {
        return;
    }
    int prevColumns = ui->columnCount;
    int prevAddressWidth = ui->addressWidth;
    int prevColumnWidth = ui->columnWidth;
    int pad = e9ui_scale_px(&ui->ctx, 8);
    int gap = e9ui_scale_px(&ui->ctx, 10);
    int addrW = memory_track_ui_measureTextWidth(&ui->ctx, "0x00FFFF");
    int frameW = memory_track_ui_measureTextWidth(&ui->ctx, "F000000");
    if (addrW <= 0) {
        addrW = e9ui_scale_px(&ui->ctx, 96);
    }
    if (frameW <= 0) {
        frameW = e9ui_scale_px(&ui->ctx, 60);
    }
    ui->padding = pad;
    ui->columnGap = gap;
    ui->addressWidth = addrW + e9ui_scale_px(&ui->ctx, 12);
    int minColumnW = e9ui_scale_px(&ui->ctx, 72);
    int desiredColumnW = frameW + e9ui_scale_px(&ui->ctx, 10);
    ui->columnWidth = desiredColumnW > minColumnW ? desiredColumnW : minColumnW;

    ui->modeButtonWidth = e9ui_scale_px(&ui->ctx, 34);
    ui->modeButtonGap = e9ui_scale_px(&ui->ctx, 4);
    ui->modeWidth = ui->modeButtonWidth * 3 + ui->modeButtonGap * 2;
    int filterW = memory_track_ui_measureTextWidth(&ui->ctx, "Show All");
    if (filterW <= 0) {
        filterW = e9ui_scale_px(&ui->ctx, 60);
    }
    ui->filterButtonWidth = filterW + e9ui_scale_px(&ui->ctx, 16);
    ui->filterButtonGap = e9ui_scale_px(&ui->ctx, 6);
    int trendW = memory_track_ui_measureTextWidth(&ui->ctx, "Dec");
    if (trendW <= 0) {
        trendW = e9ui_scale_px(&ui->ctx, 44);
    }
    ui->trendButtonWidth = trendW + e9ui_scale_px(&ui->ctx, 16);
    ui->trendButtonGap = e9ui_scale_px(&ui->ctx, 4);
    ui->trendWidth = ui->trendButtonWidth * 3 + ui->trendButtonGap * 2;

    int columns = 1;
    if (memory_track_ui_isOverlayBackend(ui)) {
        int widthColumns = 1;
        int availableW = winW - pad * 2 - ui->addressWidth - gap;
        if (availableW > ui->columnWidth) {
            widthColumns = 1 + (availableW - ui->columnWidth) / (ui->columnWidth + gap);
        }
        if (widthColumns < 1) {
            widthColumns = 1;
        }
        int logicalColumns = widthColumns;
        if ((int)ui->frameTextsCount > logicalColumns) {
            logicalColumns = (int)ui->frameTextsCount;
        }
        if ((int)ui->filterTextsCount > logicalColumns) {
            logicalColumns = (int)ui->filterTextsCount;
        }
        if (prevColumns > 0) {
            columns = prevColumns;
        } else {
            columns = logicalColumns;
        }
    } else {
        int availableW = winW - pad * 2 - ui->addressWidth - gap;
        if (availableW > ui->columnWidth) {
            columns = 1 + (availableW - ui->columnWidth) / (ui->columnWidth + gap);
        }
        if (columns < 1) {
            columns = 1;
        }
        if (prevColumns > 0 && columns < prevColumns) {
            columns = prevColumns;
        }
    }
    ui->columnCount = columns;
    if (columns != prevColumns || ui->addressWidth != prevAddressWidth || ui->columnWidth != prevColumnWidth) {
        ui->needsRebuild = 1;
        ui->needsRefresh = 1;
    }
}

static int
memory_track_ui_contentWidthPx(memory_track_ui_t *ui)
{
    if (!ui) {
        return 0;
    }
    int gap = ui->columnGap;
    int dataColsW = 0;
    if (ui->columnCount > 0) {
        dataColsW = ui->columnCount * ui->columnWidth + (ui->columnCount - 1) * gap;
    }
    int rowDataW = ui->addressWidth + gap + dataColsW;
    int resetW = e9ui_scale_px(&ui->ctx, 80);
    int controlsW = ui->addressWidth + gap + ui->modeWidth + gap +
                    ui->filterButtonWidth + ui->filterButtonGap +
                    ui->trendWidth + gap + resetW;
    int tableW = ui->padding * 2 + rowDataW;
    int controlsBoxW = controlsW + ui->padding * 2;
    int maxW = rowDataW;
    if (controlsBoxW > maxW) {
        maxW = controlsBoxW;
    }
    if (tableW > maxW) {
        maxW = tableW;
    }
    return maxW;
}

static void
memory_track_ui_updateContentHeight(memory_track_ui_t *ui, TTF_Font *font)
{
    if (!ui) {
        return;
    }
    int lineHeight = font ? TTF_FontHeight(font) : 0;
    if (lineHeight <= 0) {
        lineHeight = 16;
    }
    ui->rowHeight = lineHeight + e9ui_scale_px(&ui->ctx, 2);
    ui->headerHeight = 0;
    if (ui->error[0]) {
        ui->contentHeight = ui->padding * 2 + ui->rowHeight * 2;
    } else if (ui->addressCount == 0) {
        ui->contentHeight = ui->padding * 2 + ui->rowHeight * 2;
    } else {
        ui->contentHeight = ui->padding * 2 + (int)(ui->rowHeight * (int)ui->addressCount);
    }
    if (ui->scroll) {
        e9ui_scroll_setContentHeightPx(ui->scroll, ui->contentHeight);
    }
}

static size_t
memory_track_ui_frameLowerBound(const memory_track_frame_data_t *frame, uint32_t address)
{
    if (!frame || !frame->entries || frame->entryCount == 0) {
        return 0;
    }
    size_t lo = 0;
    size_t hi = frame->entryCount;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (frame->entries[mid].address < address) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

static int
memory_track_ui_tablePreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)ctx;
    (void)availW;
    memory_track_table_state_t *st = (memory_track_table_state_t*)self->state;
    if (!st || !st->ui) {
        return 0;
    }
    return st->ui->contentHeight;
}

static void
memory_track_ui_tableLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
    memory_track_table_state_t *st = (memory_track_table_state_t*)self->state;
    memory_track_ui_t *ui = st ? st->ui : NULL;
    if (!ui || !ui->addressLinks || ui->addressLinksCount == 0) {
        return;
    }
    int pad = ui->padding;
    int startX = bounds.x + pad;
    int startY = bounds.y + pad;
    size_t linkCount = ui->addressLinksCount;
    if (linkCount > ui->addressLinksCap) {
        linkCount = ui->addressLinksCap;
    }
    for (size_t rowIndex = 0; rowIndex < linkCount; ++rowIndex) {
        e9ui_component_t *link = ui->addressLinks[rowIndex];
        if (link && link->layout) {
            e9ui_rect_t r = { startX, startY, ui->addressWidth, ui->rowHeight };
            link->layout(link, ctx, r);
        }
        startY += ui->rowHeight;
    }
}

static void
memory_track_ui_tableRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) {
        return;
    }
    memory_track_table_state_t *st = (memory_track_table_state_t*)self->state;
    memory_track_ui_t *ui = st ? st->ui : NULL;
    if (!ui) {
        return;
    }
    SDL_Rect rect = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_SetRenderDrawColor(ctx->renderer, 18, 18, 18, 255);
    SDL_RenderFillRect(ctx->renderer, &rect);

    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    if (!font) {
        return;
    }
    SDL_Color addrColor = { 180, 200, 180, 255 };
    SDL_Color addrColorNormal = { 170, 190, 230, 255 };
    SDL_Color addrColorProtected = { 112, 226, 128, 255 };
    SDL_Color addrColorProtectedDisabled = { 156, 156, 156, 255 };
    SDL_Color valueColor = { 200, 220, 200, 255 };
    SDL_Color valueColorInc = { 255, 80, 80, 255 };
    SDL_Color valueColorDec = { 80, 255, 80, 255 };
    SDL_Color errorColor = { 220, 80, 80, 255 };

    int pad = ui->padding;
    int startX = rect.x + pad;
    int startY = rect.y + pad;
    SDL_Rect clipRect = { 0, 0, 0, 0 };
    SDL_bool clipEnabled = SDL_RenderIsClipEnabled(ctx->renderer);
    if (clipEnabled) {
        SDL_RenderGetClipRect(ctx->renderer, &clipRect);
    } else {
        clipRect = rect;
    }
    int clipTop = clipRect.y;
    int clipBottom = clipRect.y + clipRect.h;
    if (ui->error[0]) {
        int textW = 0;
        int textH = 0;
        SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, ui->error, errorColor,
                                                   &textW, &textH);
        if (tex) {
            SDL_Rect tr = { startX, startY, textW, textH };
            SDL_RenderCopy(ctx->renderer, tex, NULL, &tr);
        }
        return;
    }

    int columnsX = startX + ui->addressWidth + ui->columnGap;
    if (ui->addressCount == 0) {
        int textW = 0;
        int textH = 0;
        const char *emptyText = ui->hasActiveFrames ? "No changes in selected frames"
                                                    : "Enter frame numbers above";
        SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, emptyText,
                                                   addrColor, &textW, &textH);
        if (tex) {
            SDL_Rect tr = { startX, startY, textW, textH };
            SDL_RenderCopy(ctx->renderer, tex, NULL, &tr);
        }
        return;
    }

    if (ui->frameCount > ui->frameIndicesCap) {
        size_t newCap = ui->frameIndicesCap ? ui->frameIndicesCap : 8;
        while (newCap < ui->frameCount) {
            newCap *= 2;
        }
        size_t *next = (size_t*)alloc_realloc(ui->frameIndices, newCap * sizeof(size_t));
        if (!next) {
            return;
        }
        ui->frameIndices = next;
        ui->frameIndicesCap = newCap;
    }
    for (size_t frameIndex = 0; frameIndex < ui->frameCount; ++frameIndex) {
        ui->frameIndices[frameIndex] = 0;
    }

    if (ui->rowHeight <= 0) {
        return;
    }

    int firstVisibleRow = 0;
    int lastVisibleRowExclusive = (int)ui->addressCount;
    if (clipTop > startY) {
        firstVisibleRow = (clipTop - startY) / ui->rowHeight;
    }
    if (clipBottom > startY) {
        lastVisibleRowExclusive = (clipBottom - startY + ui->rowHeight - 1) / ui->rowHeight;
    } else {
        lastVisibleRowExclusive = 0;
    }
    if (firstVisibleRow < 0) {
        firstVisibleRow = 0;
    }
    if (lastVisibleRowExclusive < 0) {
        lastVisibleRowExclusive = 0;
    }
    if (firstVisibleRow > (int)ui->addressCount) {
        firstVisibleRow = (int)ui->addressCount;
    }
    if (lastVisibleRowExclusive > (int)ui->addressCount) {
        lastVisibleRowExclusive = (int)ui->addressCount;
    }
    if (firstVisibleRow >= lastVisibleRowExclusive) {
        return;
    }

    uint32_t firstVisibleAddress = ui->addresses[(size_t)firstVisibleRow];
    for (size_t colIndex = 0; colIndex < ui->frameCount; ++colIndex) {
        memory_track_frame_data_t *frame = &ui->frames[colIndex];
        ui->frameIndices[colIndex] = memory_track_ui_frameLowerBound(frame, firstVisibleAddress);
    }

    startY += firstVisibleRow * ui->rowHeight;
    for (size_t rowIndex = (size_t)firstVisibleRow; rowIndex < (size_t)lastVisibleRowExclusive; ++rowIndex) {
        uint32_t address = ui->addresses[rowIndex];
        SDL_Color rowAddrColor = addrColor;
        if (ui->addressLinks && rowIndex < ui->addressLinksCount &&
            rowIndex < ui->addressLinksCap) {
            rowAddrColor = addrColorNormal;
            if (ui->addressLinkProtectState &&
                rowIndex < ui->addressLinkProtectStateCap) {
                uint8_t state = ui->addressLinkProtectState[rowIndex];
                if (state == 1) {
                    rowAddrColor = addrColorProtected;
                } else if (state == 2) {
                    rowAddrColor = addrColorProtectedDisabled;
                }
            }
        }
        char addrText[16];
        snprintf(addrText, sizeof(addrText), "0x%06X", address & 0x00FFFFFFu);
        int addrW = 0;
        int addrH = 0;
        SDL_Texture *addrTex = e9ui_text_cache_getText(ctx->renderer, font, addrText, rowAddrColor,
                                                       &addrW, &addrH);
        if (addrTex) {
            SDL_Rect tr = { startX, startY, addrW, addrH };
            SDL_RenderCopy(ctx->renderer, addrTex, NULL, &tr);
        }

        int trendSampleCount = 0;
        int trendIncreasing = 1;
        int trendDecreasing = 1;
        uint32_t trendPrevValue = 0;
        int trendHasPrev = 0;
        for (size_t colIndex = 0; colIndex < ui->frameCount; ++colIndex) {
            memory_track_frame_data_t *frame = &ui->frames[colIndex];
            size_t idx = ui->frameIndices[colIndex];
            while (idx < frame->entryCount && frame->entries[idx].address < address) {
                idx++;
            }
            ui->frameIndices[colIndex] = idx;
            if (idx < frame->entryCount && frame->entries[idx].address == address) {
                uint32_t currentValue = frame->entries[idx].value;
                if (trendHasPrev) {
                    if (currentValue <= trendPrevValue) {
                        trendIncreasing = 0;
                    }
                    if (currentValue >= trendPrevValue) {
                        trendDecreasing = 0;
                    }
                } else {
                    trendHasPrev = 1;
                }
                trendPrevValue = currentValue;
                trendSampleCount++;
            }
        }

        SDL_Color rowValueColor = valueColor;
        if (trendSampleCount >= 2) {
            if (trendIncreasing) {
                rowValueColor = valueColorInc;
            } else if (trendDecreasing) {
                rowValueColor = valueColorDec;
            }
        }

        for (size_t colIndex = 0; colIndex < ui->frameCount; ++colIndex) {
            memory_track_frame_data_t *frame = &ui->frames[colIndex];
            size_t idx = ui->frameIndices[colIndex];
            if (idx < frame->entryCount && frame->entries[idx].address == address) {
                char valText[16];
                unsigned digits = (unsigned)(ui->accessSize * 2);
                if (digits < 2) {
                    digits = 2;
                }
                snprintf(valText, sizeof(valText), "%0*X", (int)digits, frame->entries[idx].value);
                int valW = 0;
                int valH = 0;
                SDL_Texture *valTex = e9ui_text_cache_getText(ctx->renderer, font, valText, rowValueColor,
                                                              &valW, &valH);
                if (valTex) {
                    int colX = columnsX + (int)colIndex * (ui->columnWidth + ui->columnGap);
                    SDL_Rect tr = { colX, startY, valW, valH };
                    SDL_RenderCopy(ctx->renderer, valTex, NULL, &tr);
                }
            }
        }
        startY += ui->rowHeight;
    }
}

static int
memory_track_ui_tableHandleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!self || !ctx || !ev) {
        return 0;
    }
    memory_track_table_state_t *st = (memory_track_table_state_t*)self->state;
    memory_track_ui_t *ui = st ? st->ui : NULL;
    if (!ui || !ui->addressLinks || ui->addressLinksCount == 0) {
        return 0;
    }
    size_t linkCount = ui->addressLinksCount;
    if (linkCount > ui->addressLinksCap) {
        linkCount = ui->addressLinksCap;
    }
    for (size_t i = 0; i < linkCount; ++i) {
        e9ui_component_t *link = ui->addressLinks[i];
        if (link && e9ui_event_process(link, ctx, ev)) {
            return 1;
        }
    }
    return 0;
}

static void
memory_track_ui_tableDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    memory_track_table_state_t *st = (memory_track_table_state_t*)self->state;
    alloc_free(st);
    self->state = NULL;
}

static e9ui_component_t *
memory_track_ui_tableMake(memory_track_ui_t *ui)
{
    e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    memory_track_table_state_t *st = (memory_track_table_state_t*)alloc_calloc(1, sizeof(*st));
    if (!comp || !st) {
        alloc_free(comp);
        alloc_free(st);
        return NULL;
    }
    st->ui = ui;
    comp->name = "memory_track_table";
    comp->state = st;
    comp->preferredHeight = memory_track_ui_tablePreferredHeight;
    comp->layout = memory_track_ui_tableLayout;
    comp->render = memory_track_ui_tableRender;
    comp->handleEvent = memory_track_ui_tableHandleEvent;
    comp->dtor = memory_track_ui_tableDtor;
    return comp;
}

static void
memory_track_ui_onFrameSubmit(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_track_ui_t *ui = (memory_track_ui_t*)user;
    if (!ui) {
        return;
    }
    memory_track_ui_storeFrameTexts(ui);
    ui->needsRefresh = 1;
}

static void
memory_track_ui_onFilterSubmit(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_track_ui_t *ui = (memory_track_ui_t*)user;
    if (!ui) {
        return;
    }
    memory_track_ui_storeFilterTexts(ui);
    ui->needsRefresh = 1;
}

static e9ui_component_t *
memory_track_ui_buildFrameRow(memory_track_ui_t *ui)
{
    if (!ui) {
        return NULL;
    }
    e9ui_component_t *row = e9ui_hstack_make();
    e9ui_component_t *label = e9ui_text_make("Address");
    SDL_Color labelColor = { 200, 200, 200, 255 };
    e9ui_text_setColor(label, labelColor);
    int gap = ui->columnGap;
    e9ui_hstack_addFixed(row, label, ui->addressWidth);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(gap), gap);

    if (ui->columnCount > (int)ui->frameInputsCap) {
        size_t newCap = ui->frameInputsCap ? ui->frameInputsCap : 4;
        while (newCap < (size_t)ui->columnCount) {
            newCap *= 2;
        }
        e9ui_component_t **next = (e9ui_component_t**)alloc_realloc(ui->frameInputs,
                                                                     newCap * sizeof(e9ui_component_t*));
        ui->frameInputs = next;
        ui->frameInputsCap = newCap;
    }
    ui->frameInputsCount = (size_t)ui->columnCount;

    for (int columnIndex = 0; columnIndex < ui->columnCount; ++columnIndex) {
        e9ui_component_t *textbox = e9ui_textbox_make(16, memory_track_ui_onFrameSubmit, NULL, ui);
        e9ui_textbox_setPlaceholder(textbox, "Frame");
        e9ui_textbox_setNumericOnly(textbox, 1);
        if (ui->frameTexts && columnIndex < (int)ui->frameTextsCount && ui->frameTexts[columnIndex]) {
            e9ui_textbox_setText(textbox, ui->frameTexts[columnIndex]);
        }
        ui->frameInputs[columnIndex] = textbox;
        e9ui_hstack_addFixed(row, textbox, ui->columnWidth);
        if (columnIndex + 1 < ui->columnCount) {
            e9ui_hstack_addFixed(row, e9ui_spacer_make(gap), gap);
        }
    }
    e9ui_hstack_addFlex(row, e9ui_spacer_make(1));
    memory_track_ui_updateModeButtons(ui);
    memory_track_ui_updateFilterButton(ui);
    return row;
}

static e9ui_component_t *
memory_track_ui_buildControlRow(memory_track_ui_t *ui)
{
    if (!ui) {
        return NULL;
    }
    e9ui_component_t *row = e9ui_hstack_make();
    int gap = ui->columnGap;
    e9ui_hstack_addFixed(row, e9ui_spacer_make(ui->addressWidth), ui->addressWidth);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(gap), gap);

    ui->modeBtn8 = e9ui_button_make("8", memory_track_ui_access8, ui);
    ui->modeBtn16 = e9ui_button_make("16", memory_track_ui_access16, ui);
    ui->modeBtn32 = e9ui_button_make("32", memory_track_ui_access32, ui);
    e9ui_hstack_addFixed(row, ui->modeBtn8, ui->modeButtonWidth);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(ui->modeButtonGap), ui->modeButtonGap);
    e9ui_hstack_addFixed(row, ui->modeBtn16, ui->modeButtonWidth);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(ui->modeButtonGap), ui->modeButtonGap);
    e9ui_hstack_addFixed(row, ui->modeBtn32, ui->modeButtonWidth);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(gap), gap);

    ui->filterBtn = e9ui_button_make("Show All", memory_track_ui_toggleRequireAll, ui);
    e9ui_hstack_addFixed(row, ui->filterBtn, ui->filterButtonWidth);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(ui->filterButtonGap), ui->filterButtonGap);
    ui->trendBtnAll = e9ui_button_make("All", memory_track_ui_setTrendAll, ui);
    ui->trendBtnInc = e9ui_button_make("Inc", memory_track_ui_setTrendInc, ui);
    ui->trendBtnDec = e9ui_button_make("Dec", memory_track_ui_setTrendDec, ui);
    e9ui_hstack_addFixed(row, ui->trendBtnAll, ui->trendButtonWidth);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(ui->trendButtonGap), ui->trendButtonGap);
    e9ui_hstack_addFixed(row, ui->trendBtnInc, ui->trendButtonWidth);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(ui->trendButtonGap), ui->trendButtonGap);
    e9ui_hstack_addFixed(row, ui->trendBtnDec, ui->trendButtonWidth);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(gap), gap);
    e9ui_component_t *btn_reset = e9ui_button_make("Reset", memory_track_ui_clearFrameMarkers, ui);
    int resetW = e9ui_scale_px(&ui->ctx, 80);
    e9ui_hstack_addFixed(row, btn_reset, resetW);
    e9ui_hstack_addFlex(row, e9ui_spacer_make(1));
    memory_track_ui_updateModeButtons(ui);
    memory_track_ui_updateFilterButton(ui);
    memory_track_ui_updateTrendButtons(ui);
    return row;
}

static e9ui_component_t *
memory_track_ui_buildFilterRow(memory_track_ui_t *ui)
{
    if (!ui) {
        return NULL;
    }
    e9ui_component_t *row = e9ui_hstack_make();
    int gap = ui->columnGap;
    e9ui_hstack_addFixed(row, e9ui_spacer_make(ui->addressWidth), ui->addressWidth);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(gap), gap);
    if (ui->columnCount > (int)ui->filterInputsCap) {
        size_t newCap = ui->filterInputsCap ? ui->filterInputsCap : 4;
        while (newCap < (size_t)ui->columnCount) {
            newCap *= 2;
        }
        e9ui_component_t **next = (e9ui_component_t**)alloc_realloc(ui->filterInputs,
                                                                     newCap * sizeof(e9ui_component_t*));
        ui->filterInputs = next;
        ui->filterInputsCap = newCap;
    }
    ui->filterInputsCount = (size_t)ui->columnCount;

    for (int columnIndex = 0; columnIndex < ui->columnCount; ++columnIndex) {
        e9ui_component_t *textbox = e9ui_textbox_make(16, memory_track_ui_onFilterSubmit, NULL, ui);
        e9ui_textbox_setPlaceholder(textbox, "Filter");
        if (ui->filterTexts && columnIndex < (int)ui->filterTextsCount && ui->filterTexts[columnIndex]) {
            e9ui_textbox_setText(textbox, ui->filterTexts[columnIndex]);
        }
        ui->filterInputs[columnIndex] = textbox;
        e9ui_hstack_addFixed(row, textbox, ui->columnWidth);
        if (columnIndex + 1 < ui->columnCount) {
            e9ui_hstack_addFixed(row, e9ui_spacer_make(gap), gap);
        }
    }
    e9ui_hstack_addFlex(row, e9ui_spacer_make(1));
    return row;
}

static e9ui_component_t *
memory_track_ui_buildRoot(memory_track_ui_t *ui)
{
    e9ui_component_t *root = e9ui_stack_makeVertical();
    e9ui_component_t *stack = e9ui_stack_makeVertical();
    e9ui_component_t *controls = memory_track_ui_buildControlRow(ui);
    e9ui_component_t *controls_box = e9ui_box_make(controls);
    e9ui_box_setPadding(controls_box, ui->padding);
    e9ui_component_t *filters = memory_track_ui_buildFilterRow(ui);
    e9ui_component_t *frames = memory_track_ui_buildFrameRow(ui);
    e9ui_component_t *table = memory_track_ui_tableMake(ui);
    ui->table = table;
    ui->scroll = e9ui_scroll_make(table);
    ui->hscroll = e9ui_scroll_make(stack);
    e9ui_scroll_setContentWidthPx(ui->hscroll, memory_track_ui_contentWidthPx(ui));
    ui->headerRow = controls_box;
    e9ui_stack_addFixed(stack, controls_box);
    e9ui_stack_addFixed(stack, filters);
    e9ui_stack_addFixed(stack, frames);
    e9ui_stack_addFlex(stack, ui->scroll);
    e9ui_stack_addFlex(root, ui->hscroll);
    return root;
}

static void
memory_track_ui_rebuildRoot(memory_track_ui_t *ui)
{
    if (!ui) {
        return;
    }
    memory_track_ui_storeFrameTexts(ui);
    memory_track_ui_storeFilterTexts(ui);
    if (ui->root) {
        if (memory_track_ui_isOverlayBackend(ui) && ui->overlayBodyHost) {
            e9ui_childRemove(ui->overlayBodyHost, ui->root, &ui->ctx);
        } else {
            e9ui_childDestroy(ui->root, &ui->ctx);
        }
    }
    ui->root = NULL;
    ui->headerRow = NULL;
    ui->hscroll = NULL;
    ui->scroll = NULL;
    ui->table = NULL;
    ui->root = memory_track_ui_buildRoot(ui);
    if (ui->root && memory_track_ui_isOverlayBackend(ui) && ui->overlayBodyHost) {
        e9ui_child_add(ui->overlayBodyHost, ui->root, alloc_strdup("memory_track_ui_root"));
    }
    ui->needsRebuild = 0;
    ui->needsRefresh = 1;
}

static int
memory_track_ui_overlayBodyPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)ctx;
    (void)availW;
    return 0;
}

static void
memory_track_ui_overlayBodyLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self) {
        return;
    }
    self->bounds = bounds;
    memory_track_overlay_body_state_t *st = (memory_track_overlay_body_state_t *)self->state;
    memory_track_ui_t *ui = st ? st->ui : NULL;
    if (!ui) {
        return;
    }
    if (ctx) {
        ui->ctx = *ctx;
        ui->ctx.window = ctx->window;
        ui->ctx.renderer = ctx->renderer;
        ui->ctx.font = e9ui->ctx.font;
        ui->ctx.winW = bounds.w;
        ui->ctx.winH = bounds.h;
    }
    memory_track_ui_updateMetrics(ui, bounds.w);
    if (ui->needsRebuild) {
        memory_track_ui_rebuildRoot(ui);
    }
    e9ui_component_t *root = ui->fullscreen ? ui->fullscreen : ui->root;
    if (root && root->layout) {
        root->layout(root, ctx, bounds);
    }
}

static void
memory_track_ui_overlayBodyDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    memory_track_overlay_body_state_t *st = (memory_track_overlay_body_state_t *)self->state;
    alloc_free(st);
    self->state = NULL;
}

static void
memory_track_ui_deferredShutdown(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    memory_track_ui_shutdown();
}

static void
memory_track_ui_requestClose(memory_track_ui_t *ui, e9ui_context_t *ctx)
{
    if (!ui) {
        return;
    }
    (void)e9ui_defer(ctx ? ctx : &e9ui->ctx,
                     memory_track_ui_deferredShutdown,
                     ui);
}

static void
memory_track_ui_overlayBodyRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !self->state) {
        return;
    }
    memory_track_overlay_body_state_t *st = (memory_track_overlay_body_state_t *)self->state;
    memory_track_ui_t *ui = st ? st->ui : NULL;
    if (!ui || !ui->windowState.open) {
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
    ui->ctx.focusFullscreen = ui->fullscreen;

    if (ui->pendingRemove && ui->root) {
        e9ui_childRemove(ui->root, ui->pendingRemove, &ui->ctx);
        ui->pendingRemove = NULL;
    }
    memory_track_ui_updateMetrics(ui, self->bounds.w);
    if (ui->needsRebuild) {
        memory_track_ui_rebuildRoot(ui);
        if (!ui->root) {
            memory_track_ui_requestClose(ui, ctx);
            return;
        }
    }
    if (ui->needsRefresh) {
        (void)memory_track_ui_collectData(ui, ui->columnCount);
        ui->needsRefresh = 0;
    }
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ui->ctx.font;
    memory_track_ui_updateContentHeight(ui, font);

    e9ui_component_t *root = ui->fullscreen ? ui->fullscreen : ui->root;
    if (root && root->layout) {
        root->layout(root, &ui->ctx, self->bounds);
    }
    if (root && root->render) {
        root->render(root, &ui->ctx);
    }
}

static e9ui_component_t *
memory_track_ui_makeOverlayBodyHost(memory_track_ui_t *ui)
{
    if (!ui || !ui->root) {
        return NULL;
    }
    e9ui_component_t *host = (e9ui_component_t *)alloc_calloc(1, sizeof(*host));
    if (!host) {
        return NULL;
    }
    memory_track_overlay_body_state_t *st =
        (memory_track_overlay_body_state_t *)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(host);
        return NULL;
    }
    st->ui = ui;
    host->name = "memory_track_ui_overlay_body";
    host->state = st;
    host->preferredHeight = memory_track_ui_overlayBodyPreferredHeight;
    host->layout = memory_track_ui_overlayBodyLayout;
    host->render = memory_track_ui_overlayBodyRender;
    host->dtor = memory_track_ui_overlayBodyDtor;
    e9ui_child_add(host, ui->root, alloc_strdup("memory_track_ui_root"));
    return host;
}

static void
memory_track_ui_overlayWindowCloseRequested(e9ui_window_t *window, void *user)
{
    (void)window;
    memory_track_ui_t *ui = (memory_track_ui_t *)user;
    if (!ui) {
        return;
    }
    memory_track_ui_shutdown();
}

int
memory_track_ui_init(void)
{
    memory_track_ui_t *ui = &memory_track_ui_state;
    if (ui->windowState.open) {
        return 1;
    }
    ui->windowState.winX = e9ui->layout.memTrackWinX;
    ui->windowState.winY = e9ui->layout.memTrackWinY;
    ui->windowState.winW = e9ui->layout.memTrackWinW;
    ui->windowState.winH = e9ui->layout.memTrackWinH;
    ui->windowState.winHasSaved =
        e9ui_windowHasSavedPosition(ui->windowState.winX, ui->windowState.winY);
    ui->windowState.windowHost = e9ui_windowCreate(memory_track_ui_windowBackend());
    if (!ui->windowState.windowHost) {
        return 0;
    }
    ui->needsRefresh = 1;
    ui->accessSize = 2;
    ui->requireAllColumns = 1;
    ui->trendFilterMode = MEMORY_TRACK_UI_TREND_ALL;
    ui->window = NULL;
    ui->renderer = NULL;
    ui->ctx.font = e9ui->ctx.font;
    e9ui_rect_t overlayRect = { 0, 0, 0, 0 };
    overlayRect = e9ui_windowResolveStateOpenRect(&e9ui->ctx,
                                                  memory_track_ui_windowDefaultRect(&e9ui->ctx),
                                                  &ui->windowState);
    ui->ctx = e9ui->ctx;
    ui->ctx.font = e9ui->ctx.font;
    int estimatedBodyW = overlayRect.w - e9ui_scale_px(&e9ui->ctx, 8);
    if (estimatedBodyW <= 0) {
        estimatedBodyW = overlayRect.w;
    }
    memory_track_ui_updateMetrics(ui, estimatedBodyW);
    ui->needsRebuild = 0;
    ui->root = memory_track_ui_buildRoot(ui);
    if (!ui->root) {
        memory_track_ui_shutdown();
        return 0;
    }
    {
        e9ui_rect_t rect = overlayRect;
        ui->overlayBodyHost = memory_track_ui_makeOverlayBodyHost(ui);
        e9ui_windowOpen(ui->windowState.windowHost,
                                     "MEMORY TRACKER",
                                     rect,
                                     ui->overlayBodyHost,
                                     memory_track_ui_overlayWindowCloseRequested,
                                     ui,
			             &e9ui->ctx);
        ui->window = e9ui->ctx.window;
        ui->renderer = e9ui->ctx.renderer;
        ui->ctx = e9ui->ctx;
    }
    ui->needsRebuild = 0;
    memory_track_ui_setError(ui, NULL);
    ui->windowState.open = 1;
    aux_window_register(&memory_track_ui_auxWindowOps, ui);
    return 1;
}

void
memory_track_ui_shutdown(void)
{
    memory_track_ui_t *ui = &memory_track_ui_state;
    if (!ui->windowState.open) {
        return;
    }
    aux_window_unregister(&memory_track_ui_auxWindowOps, ui);
    if (ui->frameInputs && ui->frameInputsCount) {
        memory_track_ui_storeFrameTexts(ui);
    }
    if (ui->filterInputs && ui->filterInputsCount) {
        memory_track_ui_storeFilterTexts(ui);
    }
    (void)e9ui_windowCaptureStateRectSnapshot(&ui->windowState, &e9ui->ctx);
    e9ui->layout.memTrackWinX = ui->windowState.winX;
    e9ui->layout.memTrackWinY = ui->windowState.winY;
    e9ui->layout.memTrackWinW = ui->windowState.winW;
    e9ui->layout.memTrackWinH = ui->windowState.winH;
    config_saveConfig();
    e9ui_text_cache_clearRenderer(ui->renderer);
    ui->root = NULL;
    ui->overlayBodyHost = NULL;
    if (ui->windowState.windowHost) {
        e9ui_windowDestroy(ui->windowState.windowHost);
        ui->windowState.windowHost = NULL;
    }
    ui->renderer = NULL;
    ui->window = NULL;
    ui->overlayBodyHost = NULL;
    memory_track_ui_clearData(ui);
    alloc_free(ui->frameInputs);
    ui->frameInputs = NULL;
    ui->frameInputsCap = 0;
    ui->frameInputsCount = 0;
    alloc_free(ui->filterInputs);
    ui->filterInputs = NULL;
    ui->filterInputsCap = 0;
    ui->filterInputsCount = 0;
    alloc_free(ui->addressLinks);
    ui->addressLinks = NULL;
    ui->addressLinksCap = 0;
    ui->addressLinksCount = 0;
    alloc_free(ui->addressLinkProtectState);
    ui->addressLinkProtectState = NULL;
    ui->addressLinkProtectStateCap = 0;
    alloc_free(ui->addressSeen);
    ui->addressSeen = NULL;
    ui->addressSeenCap = 0;
    if (ui->filterParseTexts) {
        for (size_t i = 0; i < ui->filterParseCap; ++i) {
            if (ui->filterParseTexts[i]) {
                alloc_free(ui->filterParseTexts[i]);
            }
        }
    }
    alloc_free(ui->filterParseTexts);
    ui->filterParseTexts = NULL;
    ui->filterParseCap = 0;
    alloc_free(ui->filterParseValues);
    ui->filterParseValues = NULL;
    alloc_free(ui->filterParseActive);
    ui->filterParseActive = NULL;
    alloc_free(ui->filterParseOk);
    ui->filterParseOk = NULL;
    alloc_free(ui->scratchBase);
    ui->scratchBase = NULL;
    alloc_free(ui->scratchCur);
    ui->scratchCur = NULL;
    alloc_free(ui->scratchRef);
    ui->scratchRef = NULL;
    ui->scratchSize = 0;
    alloc_free(ui->scratchFrameNos);
    ui->scratchFrameNos = NULL;
    alloc_free(ui->scratchFrameActive);
    ui->scratchFrameActive = NULL;
    ui->scratchFrameCap = 0;
    alloc_free(ui->cachedFrameNos);
    ui->cachedFrameNos = NULL;
    alloc_free(ui->cachedFrameActive);
    ui->cachedFrameActive = NULL;
    ui->cachedFrameCap = 0;
    ui->cachedBuildFrameNo = 0;
    ui->windowState.open = 0;
    ui->headerRow = NULL;
    ui->hscroll = NULL;
    ui->scroll = NULL;
    ui->table = NULL;
    ui->columnCount = 0;
    ui->needsRebuild = 0;
    ui->needsRefresh = 0;
    memset(&ui->ctx, 0, sizeof(ui->ctx));
}

int
memory_track_ui_isOpen(void)
{
    return memory_track_ui_state.windowState.open ? 1 : 0;
}

void
memory_track_ui_setMainWindowFocused(int focused)
{
    (void)focused;
}

void
memory_track_ui_render(void)
{
    memory_track_ui_t *ui = &memory_track_ui_state;
    if (!ui->windowState.open) {
        return;
    }
    if (e9ui_windowCaptureStateRectChanged(&ui->windowState, &e9ui->ctx)) {
        e9ui->layout.memTrackWinX = ui->windowState.winX;
        e9ui->layout.memTrackWinY = ui->windowState.winY;
        e9ui->layout.memTrackWinW = ui->windowState.winW;
        e9ui->layout.memTrackWinH = ui->windowState.winH;
        config_saveConfig();
    }
}

void
memory_track_ui_addFrameMarker(uint64_t frameNo)
{
    memory_track_ui_t *ui = &memory_track_ui_state;
    uint8_t *wrappedState = NULL;
    size_t wrappedStateSize = 0;
    char text[64];
    snprintf(text, sizeof(text), "%llu", (unsigned long long)frameNo);
    size_t index = memory_track_ui_findEmptyFrameIndex(ui);
    if (!memory_track_ui_captureWrappedCurrentState(&wrappedState, &wrappedStateSize)) {
        debug_error("memory_track_ui: failed to capture marker state for frame %llu",
                    (unsigned long long)frameNo);
    } else if (!memory_track_ui_setMarkerStateAtIndex(ui, index, frameNo, wrappedState, wrappedStateSize)) {
        debug_error("memory_track_ui: failed to store marker state for frame %llu",
                    (unsigned long long)frameNo);
        alloc_free(wrappedState);
    }
    memory_track_ui_setFrameTextAtIndex(ui, index, text);
    ui->cacheValid = 0;
    ui->needsRefresh = 1;
}

void
memory_track_ui_clearMarkers(void)
{
    memory_track_ui_clearFrameMarkersInternal(&memory_track_ui_state);
}

size_t
memory_track_ui_getMarkerCount(void)
{
    memory_track_ui_t *ui = &memory_track_ui_state;
    if (!ui) {
        return 0;
    }
    size_t count = 0;
    if (ui->frameInputs && ui->frameInputsCount) {
        for (size_t i = 0; i < ui->frameInputsCount; ++i) {
            const char *text = e9ui_textbox_getText(ui->frameInputs[i]);
            uint64_t frameNo = 0;
            int empty = 0;
            if (memory_track_ui_parseFrameValue(text, &frameNo, &empty) && !empty && frameNo > 0) {
                count++;
            }
        }
        return count;
    }
    for (size_t i = 0; i < ui->frameTextsCount; ++i) {
        const char *text = ui->frameTexts[i];
        uint64_t frameNo = 0;
        int empty = 0;
        if (memory_track_ui_parseFrameValue(text, &frameNo, &empty) && !empty && frameNo > 0) {
            count++;
        }
    }
    return count;
}
