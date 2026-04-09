/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>

#include "debugger.h"
#include "source_pane_internal.h"
#include "base_map.h"
#include "breakpoints.h"
#include "libretro_host.h"
#include "debug.h"
#include "file.h"
#include "hunk_fileline_cache.h"

typedef struct source_pane_fileline_cache_entry {
    char *path;
    int line;
    uint32_t addr;
} source_pane_fileline_cache_entry_t;

typedef struct source_pane_fileline_cache_state {
    source_pane_fileline_cache_entry_t *entries;
    int entryCount;
    int entryCap;
    int cacheReady;
    char elfPath[PATH_MAX];
    char toolchainPrefix[PATH_MAX];
    char sourceDir[PATH_MAX];
} source_pane_fileline_cache_state_t;

static source_pane_fileline_cache_state_t source_pane_fileline_cache_state;

static int
source_pane_fileline_resolveFileLineAll(const char *elf, const char *file, int line_no,
                                        uint32_t **out_addrs, int *out_count);

int
source_pane_fileline_resolveFileLine(const char *elf, const char *file, int line_no, uint32_t *out_addr)
{
    uint32_t *addrs = NULL;
    int count = 0;

    if (out_addr) {
        *out_addr = 0;
    }
    if (!source_pane_fileline_resolveFileLineAll(elf, file, line_no, &addrs, &count)) {
        return 0;
    }

    *out_addr = addrs[0];
    alloc_free(addrs);
    return 1;
}

static void
source_pane_fileline_clearCache(void)
{
    for (int i = 0; i < source_pane_fileline_cache_state.entryCount; ++i) {
        alloc_free(source_pane_fileline_cache_state.entries[i].path);
        source_pane_fileline_cache_state.entries[i].path = NULL;
    }
    alloc_free(source_pane_fileline_cache_state.entries);
    source_pane_fileline_cache_state.entries = NULL;
    source_pane_fileline_cache_state.entryCount = 0;
    source_pane_fileline_cache_state.entryCap = 0;
    source_pane_fileline_cache_state.cacheReady = 0;
    source_pane_fileline_cache_state.elfPath[0] = '\0';
    source_pane_fileline_cache_state.toolchainPrefix[0] = '\0';
    source_pane_fileline_cache_state.sourceDir[0] = '\0';
}

static int
source_pane_fileline_ensureCapacity(int minCap)
{
    if (source_pane_fileline_cache_state.entryCap >= minCap) {
        return 1;
    }

    int nextCap = source_pane_fileline_cache_state.entryCap > 0 ? source_pane_fileline_cache_state.entryCap : 1024;
    while (nextCap < minCap) {
        nextCap *= 2;
    }

    source_pane_fileline_cache_entry_t *nextEntries =
        (source_pane_fileline_cache_entry_t*)alloc_realloc(source_pane_fileline_cache_state.entries,
                                                           sizeof(*nextEntries) * (size_t)nextCap);
    if (!nextEntries) {
        return 0;
    }
    for (int i = source_pane_fileline_cache_state.entryCap; i < nextCap; ++i) {
        nextEntries[i].path = NULL;
        nextEntries[i].line = 0;
        nextEntries[i].addr = 0;
    }

    source_pane_fileline_cache_state.entries = nextEntries;
    source_pane_fileline_cache_state.entryCap = nextCap;
    return 1;
}

static int
source_pane_fileline_addEntry(const char *path, int line, uint32_t addr)
{
    if (!path || !path[0] || line <= 0) {
        return 0;
    }

    char resolved[PATH_MAX];
    source_pane_resolveSourcePath(path, resolved, sizeof(resolved));
    if (!resolved[0]) {
        return 0;
    }

    for (int i = 0; i < source_pane_fileline_cache_state.entryCount; ++i) {
        source_pane_fileline_cache_entry_t *entry = &source_pane_fileline_cache_state.entries[i];
        if (entry->line != line) {
            continue;
        }
        if (entry->addr != addr) {
            continue;
        }
        if (!source_pane_fileMatches(entry->path, resolved)) {
            continue;
        }
        return 1;
    }

    if (!source_pane_fileline_ensureCapacity(source_pane_fileline_cache_state.entryCount + 1)) {
        return 0;
    }

    char *pathDup = alloc_strdup(resolved);
    if (!pathDup) {
        return 0;
    }

    source_pane_fileline_cache_entry_t *entry =
        &source_pane_fileline_cache_state.entries[source_pane_fileline_cache_state.entryCount++];
    entry->path = pathDup;
    entry->line = line;
    entry->addr = addr;
    return 1;
}

machine_breakpoint_t *
source_pane_fileline_findBreakpointForLine(const char *path, int line,
                                           const machine_breakpoint_t *bps, int count)
{
    if (!path || line <= 0) {
        return NULL;
    }
    for (int i = 0; i < count; ++i) {
        machine_breakpoint_t *bp = (machine_breakpoint_t*)&bps[i];
        if (bp->line == line && source_pane_fileMatches(bp->file, path)) {
            return bp;
        }
    }
    return NULL;
}

int
source_pane_fileline_removeBreakpointsForLine(const char *path, int line,
                                              const machine_breakpoint_t *bps, int count)
{
    if (!path || line <= 0 || !bps || count <= 0) {
        return 0;
    }

    int matchCount = 0;
    for (int i = 0; i < count; ++i) {
        const machine_breakpoint_t *bp = &bps[i];
        if (bp->line == line && source_pane_fileMatches(bp->file, path)) {
            matchCount++;
        }
    }
    if (matchCount <= 0) {
        return 0;
    }

    uint32_t *addrs = (uint32_t*)alloc_alloc(sizeof(*addrs) * (size_t)matchCount);
    if (!addrs) {
        return 0;
    }

    int writeIndex = 0;
    for (int i = 0; i < count; ++i) {
        const machine_breakpoint_t *bp = &bps[i];
        if (bp->line == line && source_pane_fileMatches(bp->file, path)) {
            addrs[writeIndex++] = (uint32_t)bp->addr;
        }
    }

    int removedAny = 0;
    for (int i = 0; i < writeIndex; ++i) {
        uint32_t addr = addrs[i];
        if (machine_removeBreakpointByAddr(&debugger.machine, addr)) {
            libretro_host_debugRemoveBreakpoint(addr);
            removedAny = 1;
        }
    }

    alloc_free(addrs);
    return removedAny;
}

static int
source_pane_fileline_compareBreakpointAddr(const void *a, const void *b)
{
    const uint32_t av = *(const uint32_t *)a;
    const uint32_t bv = *(const uint32_t *)b;

    if (av < bv) {
        return -1;
    }
    if (av > bv) {
        return 1;
    }
    return 0;
}

static int
source_pane_fileline_buildCacheFromDisassembly(const char *elf)
{
    if (!elf || !elf[0]) {
        return 0;
    }

    char cmd[PATH_MAX * 2];
    char objdump[PATH_MAX];
    if (!debugger_toolchainBuildBinary(objdump, sizeof(objdump), "objdump")) {
        debug_error("break: failed to resolve objdump");
        return 0;
    }
    char objdumpExe[PATH_MAX];
    if (!file_findInPath(objdump, objdumpExe, sizeof(objdumpExe))) {
        debug_error("break: objdump not found in PATH: %s", objdump);
        return 0;
    }
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), objdumpExe, "-l -d", elf, 0)) {
        return 0;
    }
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        debug_error("break: failed to run objdump");
        return 0;
    }

    char lineBuf[1024];
    int wantAddr = 0;
    int currentLine = 0;
    char currentFile[PATH_MAX];
    currentFile[0] = '\0';
    while (fgets(lineBuf, sizeof(lineBuf), fp)) {
        char *nl = strchr(lineBuf, '\n');
        if (nl) {
            *nl = '\0';
        }
        if (lineBuf[0] == '\0') {
            wantAddr = 0;
            continue;
        }
        if (lineBuf[0] != ' ') {
            const char *colon = strrchr(lineBuf, ':');
            if (!colon || !colon[1]) {
                wantAddr = 0;
                currentLine = 0;
                currentFile[0] = '\0';
                continue;
            }

            int gotLine = atoi(colon + 1);
            if (gotLine <= 0) {
                wantAddr = 0;
                currentLine = 0;
                currentFile[0] = '\0';
                continue;
            }

            size_t len = (size_t)(colon - lineBuf);
            if (len >= sizeof(currentFile)) {
                len = sizeof(currentFile) - 1;
            }
            memcpy(currentFile, lineBuf, len);
            currentFile[len] = '\0';
            currentLine = gotLine;
            wantAddr = 1;
            continue;
        }

        if (!wantAddr || !currentFile[0] || currentLine <= 0) {
            continue;
        }

        char addrBuf[32];
        const char *p = lineBuf;
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        size_t i = 0;
        while (*p && !isspace((unsigned char)*p) && i + 1 < sizeof(addrBuf)) {
            addrBuf[i++] = *p++;
        }
        addrBuf[i] = '\0';

        uint32_t addr = 0;
        if (!source_pane_parseHex(addrBuf, &addr)) {
            continue;
        }
        if (!source_pane_fileline_addEntry(currentFile, currentLine, addr)) {
            pclose(fp);
            return 0;
        }
    }

    pclose(fp);
    return 1;
}

static int
source_pane_fileline_findAll(const char *file, int line_no, uint32_t **out_addrs, int *out_count)
{
    if (out_addrs) {
        *out_addrs = NULL;
    }
    if (out_count) {
        *out_count = 0;
    }
    if (!file || !file[0] || line_no <= 0 || !out_addrs || !out_count) {
        return 0;
    }

    int matchCount = 0;
    for (int i = 0; i < source_pane_fileline_cache_state.entryCount; ++i) {
        const source_pane_fileline_cache_entry_t *entry = &source_pane_fileline_cache_state.entries[i];
        if (entry->line != line_no) {
            continue;
        }
        if (!source_pane_fileMatches(entry->path, file)) {
            continue;
        }
        matchCount++;
    }
    if (matchCount <= 0) {
        return 0;
    }

    uint32_t *matches = (uint32_t*)alloc_alloc(sizeof(*matches) * (size_t)matchCount);
    if (!matches) {
        return 0;
    }

    int writeIndex = 0;
    for (int i = 0; i < source_pane_fileline_cache_state.entryCount; ++i) {
        const source_pane_fileline_cache_entry_t *entry = &source_pane_fileline_cache_state.entries[i];
        if (entry->line != line_no) {
            continue;
        }
        if (!source_pane_fileMatches(entry->path, file)) {
            continue;
        }
        matches[writeIndex++] = entry->addr;
    }

    qsort(matches, (size_t)writeIndex, sizeof(*matches), source_pane_fileline_compareBreakpointAddr);

    int uniqueCount = 0;
    for (int i = 0; i < writeIndex; ++i) {
        if (uniqueCount > 0 && matches[uniqueCount - 1] == matches[i]) {
            continue;
        }
        matches[uniqueCount++] = matches[i];
    }
    if (uniqueCount <= 0) {
        alloc_free(matches);
        return 0;
    }

    *out_addrs = matches;
    *out_count = uniqueCount;
    return 1;
}

static int
source_pane_fileline_ensureCache(const char *elf)
{
    const char *toolchain = debugger.libretro.toolchainPrefix;
    const char *sourceDir = debugger.libretro.sourceDir;

    if (source_pane_fileline_cache_state.cacheReady &&
        strcmp(source_pane_fileline_cache_state.elfPath, elf) == 0 &&
        strcmp(source_pane_fileline_cache_state.toolchainPrefix, toolchain) == 0 &&
        strcmp(source_pane_fileline_cache_state.sourceDir, sourceDir) == 0) {
        return 1;
    }

    source_pane_fileline_clearCache();
    if (!source_pane_fileline_buildCacheFromDisassembly(elf)) {
        return 0;
    }

    debugger_copyPath(source_pane_fileline_cache_state.elfPath,
                      sizeof(source_pane_fileline_cache_state.elfPath),
                      elf);
    debugger_copyPath(source_pane_fileline_cache_state.toolchainPrefix,
                      sizeof(source_pane_fileline_cache_state.toolchainPrefix),
                      toolchain);
    debugger_copyPath(source_pane_fileline_cache_state.sourceDir,
                      sizeof(source_pane_fileline_cache_state.sourceDir),
                      sourceDir);
    source_pane_fileline_cache_state.cacheReady = 1;
    return 1;
}

static int
source_pane_fileline_resolveFileLineAll(const char *elf, const char *file, int line_no,
                                        uint32_t **out_addrs, int *out_count)
{
    if (out_addrs) {
        *out_addrs = NULL;
    }
    if (out_count) {
        *out_count = 0;
    }
    if (!elf || !elf[0] || !debugger.elfValid || !file || !file[0] || line_no <= 0 ||
        !out_addrs || !out_count) {
        return 0;
    }
    if (debugger_toolchainUsesHunkAddr2line()) {
        return hunk_fileline_cache_resolveFileLineAll(elf, file, line_no, out_addrs, out_count);
    }
    if (!source_pane_fileline_ensureCache(elf)) {
        return 0;
    }
    return source_pane_fileline_findAll(file, line_no, out_addrs, out_count);
}

int
source_pane_fileline_addBreakpointsForLine(const char *path, int lineNo)
{
    if (!path || lineNo <= 0) {
        return 0;
    }

    uint32_t *resolvedAddrs = NULL;
    int resolvedCount = 0;
    if (!source_pane_fileline_resolveFileLineAll(debugger.libretro.exePath,
                                                 path,
                                                 lineNo,
                                                 &resolvedAddrs,
                                                 &resolvedCount)) {
        return 0;
    }

    int changed = 0;
    for (int i = 0; i < resolvedCount; ++i) {
        uint32_t runtimeAddr = resolvedAddrs[i];
        if (!(debugger_toolchainUsesHunkAddr2line() &&
              base_map_getMode() == BASE_MAP_MODE_STACK)) {
            (void)base_map_debugToRuntime(BASE_MAP_SECTION_TEXT, runtimeAddr, &runtimeAddr);
        }

        machine_breakpoint_t *bp = machine_findBreakpointByAddr(&debugger.machine, runtimeAddr);
        if (!bp) {
            bp = machine_addBreakpoint(&debugger.machine, runtimeAddr, 1);
            if (bp) {
                libretro_host_debugAddBreakpoint(runtimeAddr);
            }
        }
        if (!bp) {
            continue;
        }

        strncpy(bp->file, path, sizeof(bp->file) - 1);
        bp->file[sizeof(bp->file) - 1] = '\0';
        bp->line = lineNo;
        changed = 1;
    }

    alloc_free(resolvedAddrs);
    return changed;
}
