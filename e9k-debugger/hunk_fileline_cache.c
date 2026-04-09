/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hunk_fileline_cache.h"
#include "addr2line.h"
#include "alloc.h"
#include "base_map.h"
#include "debug.h"
#include "debugger.h"
#include "file.h"
#include "strutil.h"

typedef struct hunk_fileline_cache_entry {
    char *path;
    int line;
    uint32_t addr;
    size_t sectionIndex;
    int hasSectionIndex;
} hunk_fileline_cache_entry_t;

typedef struct hunk_fileline_cache_state {
    hunk_fileline_cache_entry_t *entries;
    int entryCount;
    int entryCap;
    int cacheReady;
    int disassemblyScanned;
    int sectionSweepScanned;
    char elfPath[PATH_MAX];
    char toolchainPrefix[PATH_MAX];
} hunk_fileline_cache_state_t;

static hunk_fileline_cache_state_t hunk_fileline_cache_state;

typedef struct hunk_fileline_cache_text_section {
    size_t index;
    uint32_t size;
} hunk_fileline_cache_text_section_t;

static int
hunk_fileline_cache_parseTextSectionIndex(const char *sectionName, size_t fallbackIndex, size_t *outIndex);

static const char *
hunk_fileline_cache_basename(const char *path)
{
    if (!path) {
        return NULL;
    }
    const char *slash = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    const char *best = slash > back ? slash : back;
    return best ? best + 1 : path;
}

static int
hunk_fileline_cache_isAbsolutePath(const char *path)
{
    if (!path || !path[0]) {
        return 0;
    }
    if (path[0] == '/' || path[0] == '\\') {
        return 1;
    }
    if (isalpha((unsigned char)path[0]) && path[1] == ':' &&
        (path[2] == '\\' || path[2] == '/')) {
        return 1;
    }
    return 0;
}

static void
hunk_fileline_cache_resolveSourcePath(const char *input, char *out, size_t outCap)
{
    if (!out || outCap == 0) {
        return;
    }
    out[0] = '\0';
    if (!input || !input[0]) {
        return;
    }
    if (!hunk_fileline_cache_isAbsolutePath(input) && debugger.libretro.sourceDir[0]) {
        strutil_pathJoinTrunc(out, outCap, debugger.libretro.sourceDir, input);
    } else {
        strutil_strlcpy(out, outCap, input);
    }
}

static int
hunk_fileline_cache_fileMatches(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    if (strcmp(a, b) == 0) {
        return 1;
    }
    const char *ba = hunk_fileline_cache_basename(a);
    const char *bb = hunk_fileline_cache_basename(b);
    if (ba && bb && strcmp(ba, bb) == 0) {
        return 1;
    }
    const char *src = debugger.libretro.sourceDir;
    if (src && *src) {
        size_t srcLen = strlen(src);
        if (strncmp(a, src, srcLen) == 0) {
            const char *rest = a + srcLen;
            if (*rest == '/' || *rest == '\\') {
                rest++;
            }
            if (strcmp(rest, b) == 0) {
                return 1;
            }
        }
        if (strncmp(b, src, srcLen) == 0) {
            const char *rest = b + srcLen;
            if (*rest == '/' || *rest == '\\') {
                rest++;
            }
            if (strcmp(rest, a) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

void
hunk_fileline_cache_clear(void)
{
    for (int i = 0; i < hunk_fileline_cache_state.entryCount; ++i) {
        alloc_free(hunk_fileline_cache_state.entries[i].path);
        hunk_fileline_cache_state.entries[i].path = NULL;
    }
    alloc_free(hunk_fileline_cache_state.entries);
    hunk_fileline_cache_state.entries = NULL;
    hunk_fileline_cache_state.entryCount = 0;
    hunk_fileline_cache_state.entryCap = 0;
    hunk_fileline_cache_state.cacheReady = 0;
    hunk_fileline_cache_state.disassemblyScanned = 0;
    hunk_fileline_cache_state.sectionSweepScanned = 0;
    hunk_fileline_cache_state.elfPath[0] = '\0';
    hunk_fileline_cache_state.toolchainPrefix[0] = '\0';
}

static int
hunk_fileline_cache_ensureCapacity(int minCap)
{
    if (hunk_fileline_cache_state.entryCap >= minCap) {
        return 1;
    }
    int nextCap = hunk_fileline_cache_state.entryCap > 0 ? hunk_fileline_cache_state.entryCap : 512;
    while (nextCap < minCap) {
        nextCap *= 2;
    }
    hunk_fileline_cache_entry_t *nextEntries =
        (hunk_fileline_cache_entry_t *)alloc_realloc(hunk_fileline_cache_state.entries,
                                                     sizeof(*nextEntries) * (size_t)nextCap);
    if (!nextEntries) {
        return 0;
    }
    for (int i = hunk_fileline_cache_state.entryCap; i < nextCap; ++i) {
        nextEntries[i].path = NULL;
        nextEntries[i].line = 0;
        nextEntries[i].addr = 0;
        nextEntries[i].sectionIndex = 0u;
        nextEntries[i].hasSectionIndex = 0;
    }
    hunk_fileline_cache_state.entries = nextEntries;
    hunk_fileline_cache_state.entryCap = nextCap;
    return 1;
}

static int
hunk_fileline_cache_addEntry(const char *path, int line, uint32_t addr, size_t sectionIndex, int hasSectionIndex)
{
    if (!path || !path[0] || line <= 0) {
        return 0;
    }
    char resolved[PATH_MAX];
    hunk_fileline_cache_resolveSourcePath(path, resolved, sizeof(resolved));
    if (!resolved[0]) {
        return 0;
    }
    for (int i = 0; i < hunk_fileline_cache_state.entryCount; ++i) {
        hunk_fileline_cache_entry_t *entry = &hunk_fileline_cache_state.entries[i];
        if (entry->line != line) {
            continue;
        }
        if (!hunk_fileline_cache_fileMatches(entry->path, resolved)) {
            continue;
        }
        if (entry->addr == addr &&
            entry->sectionIndex == sectionIndex &&
            entry->hasSectionIndex == hasSectionIndex) {
            return 1;
        }
    }
    if (!hunk_fileline_cache_ensureCapacity(hunk_fileline_cache_state.entryCount + 1)) {
        return 0;
    }
    char *pathDup = alloc_strdup(resolved);
    if (!pathDup) {
        return 0;
    }
    hunk_fileline_cache_entry_t *entry = &hunk_fileline_cache_state.entries[hunk_fileline_cache_state.entryCount++];
    entry->path = pathDup;
    entry->line = line;
    entry->addr = addr;
    entry->sectionIndex = sectionIndex;
    entry->hasSectionIndex = hasSectionIndex ? 1 : 0;
    return 1;
}

static int
hunk_fileline_cache_parseTextSectionToken(const char *sectionName, size_t *outIndex, int *outHasSectionIndex)
{
    if (outIndex) {
        *outIndex = 0u;
    }
    if (outHasSectionIndex) {
        *outHasSectionIndex = 0;
    }
    if (!sectionName || !*sectionName) {
        return 0;
    }
    if (strcmp(sectionName, ".text") == 0) {
        if (outHasSectionIndex) {
            *outHasSectionIndex = 1;
        }
        return 1;
    }
    size_t sectionIndex = 0u;
    if (!hunk_fileline_cache_parseTextSectionIndex(sectionName, 0u, &sectionIndex)) {
        return 0;
    }
    if (outIndex) {
        *outIndex = sectionIndex;
    }
    if (outHasSectionIndex) {
        *outHasSectionIndex = 1;
    }
    return 1;
}

static uint32_t
hunk_fileline_cache_mapEntryAddr(const hunk_fileline_cache_entry_t *entry)
{
    if (!entry) {
        return 0u;
    }

    uint32_t resolvedAddr = entry->addr & 0x00ffffffu;
    if (entry->hasSectionIndex && base_map_getMode() == BASE_MAP_MODE_STACK) {
        uint32_t runtimeAddr = resolvedAddr;
        if (base_map_debugToRuntimeWithIndex(entry->sectionIndex, resolvedAddr, &runtimeAddr)) {
            return runtimeAddr & 0x00ffffffu;
        }
    }
    return resolvedAddr;
}

static int
hunk_fileline_cache_parseHex64(const char *s, uint64_t *out)
{
    if (out) {
        *out = 0;
    }
    if (!s || !*s || !out) {
        return 0;
    }
    const char *p = s;
    while (*p) {
        if (!isxdigit((unsigned char)*p)) {
            return 0;
        }
        p++;
    }
    unsigned long long value = strtoull(s, NULL, 16);
    *out = (uint64_t)value;
    return 1;
}

static int
hunk_fileline_cache_parseAddressBeforeColon(const char *line, uint32_t *outAddr)
{
    if (outAddr) {
        *outAddr = 0;
    }
    if (!line || !outAddr) {
        return 0;
    }

    const char *p = line;
    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }
    if (!isxdigit((unsigned char)*p)) {
        return 0;
    }

    const char *start = p;
    while (*p && isxdigit((unsigned char)*p)) {
        ++p;
    }
    if (p == start || *p != ':') {
        return 0;
    }

    size_t len = (size_t)(p - start);
    if (len == 0 || len >= 24u) {
        return 0;
    }
    char buf[24];
    memcpy(buf, start, len);
    buf[len] = '\0';
    uint64_t value = 0;
    if (!hunk_fileline_cache_parseHex64(buf, &value)) {
        return 0;
    }
    *outAddr = (uint32_t)(value & 0x00ffffffu);
    return 1;
}

static int
hunk_fileline_cache_parseTextSectionIndex(const char *sectionName, size_t fallbackIndex, size_t *outIndex)
{
    if (outIndex) {
        *outIndex = 0u;
    }
    if (!sectionName || !*sectionName || !outIndex) {
        return 0;
    }
    if (strcmp(sectionName, ".text") == 0) {
        *outIndex = fallbackIndex;
        return 1;
    }
    if (strncmp(sectionName, ".text.", 6) != 0) {
        return 0;
    }
    const char *suffix = sectionName + 6;
    if (!*suffix) {
        return 0;
    }
    for (const char *p = suffix; *p; ++p) {
        if (!isdigit((unsigned char)*p)) {
            return 0;
        }
    }
    unsigned long parsed = strtoul(suffix, NULL, 10);
    *outIndex = (size_t)parsed;
    return 1;
}

static int
hunk_fileline_cache_resolveAddressForSection(size_t sectionIndex, uint32_t sectionAddr,
                                             char *outFile, size_t outFileCap, int *outLine)
{
    if (outFile && outFileCap > 0) {
        outFile[0] = '\0';
    }
    if (outLine) {
        *outLine = 0;
    }
    if (!outFile || outFileCap == 0 || !outLine) {
        return 0;
    }

    uint32_t queryAddr = sectionAddr & 0x00ffffffu;
    if (!base_map_debugToRuntimeWithIndex(sectionIndex, queryAddr, &queryAddr) && sectionIndex != 0u) {
        return 0;
    }

    return addr2line_resolve((uint64_t)queryAddr, outFile, outFileCap, outLine);
}

static int
hunk_fileline_cache_collectTextSections(const char *elfPath,
                                        hunk_fileline_cache_text_section_t *outSections,
                                        int maxSections,
                                        int *outCount)
{
    if (outCount) {
        *outCount = 0;
    }
    if (!elfPath || !*elfPath || !outSections || maxSections <= 0 || !outCount) {
        return 0;
    }

    char objdumpBin[PATH_MAX];
    if (!debugger_toolchainBuildBinary(objdumpBin, sizeof(objdumpBin), "objdump")) {
        return 0;
    }
    char objdumpExe[PATH_MAX];
    if (!file_findInPath(objdumpBin, objdumpExe, sizeof(objdumpExe))) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), objdumpExe, "-h", elfPath, 1)) {
        return 0;
    }
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return 0;
    }

    char lineBuf[2048];
    int count = 0;
    while (fgets(lineBuf, sizeof(lineBuf), fp)) {
        char *tokens[8];
        int tokenCount = 0;
        char *cursor = lineBuf;
        while (*cursor && isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (!*cursor || !isdigit((unsigned char)*cursor)) {
            continue;
        }
        while (tokenCount < (int)(sizeof(tokens) / sizeof(tokens[0]))) {
            while (*cursor && isspace((unsigned char)*cursor)) {
                ++cursor;
            }
            if (!*cursor) {
                break;
            }
            tokens[tokenCount++] = cursor;
            while (*cursor && !isspace((unsigned char)*cursor)) {
                ++cursor;
            }
            if (*cursor) {
                *cursor++ = '\0';
            }
        }
        if (tokenCount < 3) {
            continue;
        }

        size_t fallbackIndex = 0u;
        if (tokens[0] && tokens[0][0]) {
            fallbackIndex = (size_t)strtoul(tokens[0], NULL, 10);
        }

        size_t sectionIndex = 0u;
        if (!hunk_fileline_cache_parseTextSectionIndex(tokens[1], fallbackIndex, &sectionIndex)) {
            continue;
        }

        uint64_t size64 = 0;
        if (!hunk_fileline_cache_parseHex64(tokens[2], &size64) || size64 == 0u) {
            continue;
        }
        if (count >= maxSections) {
            break;
        }
        outSections[count].index = sectionIndex;
        outSections[count].size = (uint32_t)(size64 & 0xffffffffu);
        count++;
    }

    pclose(fp);
    *outCount = count;
    return count > 0 ? 1 : 0;
}

static int
hunk_fileline_cache_buildFromSectionSweep(const char *elfPath)
{
    if (!elfPath || !*elfPath) {
        return 0;
    }
    hunk_fileline_cache_text_section_t textSections[256];
    int textSectionCount = 0;
    if (!hunk_fileline_cache_collectTextSections(elfPath,
                                                 textSections,
                                                 (int)(sizeof(textSections) / sizeof(textSections[0])),
                                                 &textSectionCount)) {
        return 0;
    }
    if (!addr2line_start(elfPath)) {
        return 0;
    }

    for (int i = 0; i < textSectionCount; ++i) {
        const hunk_fileline_cache_text_section_t *section = &textSections[i];
        uint32_t offset = 0u;
        while (offset < section->size) {
            char resolvedFile[PATH_MAX];
            int resolvedLine = 0;
            if (hunk_fileline_cache_resolveAddressForSection(section->index,
                                                             offset,
                                                             resolvedFile,
                                                             sizeof(resolvedFile),
                                                             &resolvedLine) &&
                resolvedLine > 0 &&
                resolvedFile[0]) {
                (void)hunk_fileline_cache_addEntry(resolvedFile,
                                                  resolvedLine,
                                                  offset,
                                                  section->index,
                                                  1);
            }
            if (offset > 0x00fffffdu) {
                break;
            }
            offset += 2u;
        }
    }

    return 1;
}

static int
hunk_fileline_cache_buildFromSymbols(const char *elfPath)
{
    char objdumpBin[PATH_MAX];
    if (!debugger_toolchainBuildBinary(objdumpBin, sizeof(objdumpBin), "objdump")) {
        debug_error("break: failed to resolve objdump");
        return 0;
    }
    char objdumpExe[PATH_MAX];
    if (!file_findInPath(objdumpBin, objdumpExe, sizeof(objdumpExe))) {
        debug_error("break: objdump not found in PATH: %s", objdumpBin);
        return 0;
    }
    char cmd[PATH_MAX * 2];
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), objdumpExe, "-t", elfPath, 1)) {
        return 0;
    }
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        debug_error("break: failed to run objdump");
        return 0;
    }
    if (!addr2line_start(elfPath)) {
        pclose(fp);
        return 0;
    }

    char lineBuf[2048];
    while (fgets(lineBuf, sizeof(lineBuf), fp)) {
        char *tokens[16];
        int count = 0;
        size_t sectionIndex = 0u;
        int hasSectionIndex = 0;
        char *cursor = lineBuf;
        while (*cursor && isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (!*cursor || !isxdigit((unsigned char)*cursor)) {
            continue;
        }
        while (count < (int)(sizeof(tokens) / sizeof(tokens[0]))) {
            while (*cursor && isspace((unsigned char)*cursor)) {
                ++cursor;
            }
            if (!*cursor) {
                break;
            }
            tokens[count++] = cursor;
            while (*cursor && !isspace((unsigned char)*cursor)) {
                ++cursor;
            }
            if (*cursor) {
                *cursor++ = '\0';
            }
        }
        if (count < 4) {
            continue;
        }
        uint64_t symAddr = 0;
        if (!hunk_fileline_cache_parseHex64(tokens[0], &symAddr)) {
            continue;
        }
        int textSymbol = 0;
        for (int i = 1; i < count; ++i) {
            if (hunk_fileline_cache_parseTextSectionToken(tokens[i], &sectionIndex, &hasSectionIndex)) {
                textSymbol = 1;
                break;
            }
        }
        if (!textSymbol) {
            continue;
        }
        const char *symbolName = tokens[count - 1];
        if (!symbolName || !*symbolName || symbolName[0] == '.') {
            continue;
        }
        uint32_t addr24 = (uint32_t)(symAddr & 0x00ffffffu);
        uint32_t queryAddr = addr24;
        if (hasSectionIndex && base_map_getMode() == BASE_MAP_MODE_STACK) {
            (void)base_map_debugToRuntimeWithIndex(sectionIndex, addr24, &queryAddr);
        }
        char resolvedFile[PATH_MAX];
        int resolvedLine = 0;
        if (!addr2line_resolve((uint64_t)queryAddr, resolvedFile, sizeof(resolvedFile), &resolvedLine)) {
            continue;
        }
        if (resolvedLine <= 0 || !resolvedFile[0]) {
            continue;
        }
        (void)hunk_fileline_cache_addEntry(resolvedFile,
                                          resolvedLine,
                                          addr24,
                                          sectionIndex,
                                          hasSectionIndex);
    }
    pclose(fp);
    return 1;
}

static int
hunk_fileline_cache_buildFromDisassembly(const char *elfPath)
{
    char objdumpBin[PATH_MAX];
    if (!debugger_toolchainBuildBinary(objdumpBin, sizeof(objdumpBin), "objdump")) {
        debug_error("break: failed to resolve objdump");
        return 0;
    }
    char objdumpExe[PATH_MAX];
    if (!file_findInPath(objdumpBin, objdumpExe, sizeof(objdumpExe))) {
        debug_error("break: objdump not found in PATH: %s", objdumpBin);
        return 0;
    }
    char cmd[PATH_MAX * 2];
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), objdumpExe, "-d", elfPath, 1)) {
        return 0;
    }
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        debug_error("break: failed to run objdump");
        return 0;
    }
    if (!addr2line_start(elfPath)) {
        pclose(fp);
        return 0;
    }

    char lineBuf[2048];
    size_t currentSectionIndex = 0u;
    int haveCurrentSectionIndex = 0;
    while (fgets(lineBuf, sizeof(lineBuf), fp)) {
        if (strncmp(lineBuf, "Disassembly of section ", 23) == 0) {
            char sectionName[64];
            const char *name = lineBuf + 23;
            size_t len = 0u;
            while (name[len] && name[len] != ':' && name[len] != '\n' &&
                   len + 1u < sizeof(sectionName)) {
                sectionName[len] = name[len];
                len++;
            }
            sectionName[len] = '\0';
            haveCurrentSectionIndex = 0;
            (void)hunk_fileline_cache_parseTextSectionToken(sectionName,
                                                            &currentSectionIndex,
                                                            &haveCurrentSectionIndex);
            continue;
        }
        uint32_t addr24 = 0;
        if (!hunk_fileline_cache_parseAddressBeforeColon(lineBuf, &addr24)) {
            continue;
        }
        uint32_t queryAddr = addr24;
        if (haveCurrentSectionIndex && base_map_getMode() == BASE_MAP_MODE_STACK) {
            (void)base_map_debugToRuntimeWithIndex(currentSectionIndex, addr24, &queryAddr);
        }
        char resolvedFile[PATH_MAX];
        int resolvedLine = 0;
        if (!addr2line_resolve((uint64_t)queryAddr, resolvedFile, sizeof(resolvedFile), &resolvedLine)) {
            continue;
        }
        if (resolvedLine <= 0 || !resolvedFile[0]) {
            continue;
        }
        (void)hunk_fileline_cache_addEntry(resolvedFile,
                                          resolvedLine,
                                          addr24,
                                          currentSectionIndex,
                                          haveCurrentSectionIndex);
    }

    pclose(fp);
    return 1;
}

static int
hunk_fileline_cache_compareAddrs(const void *a, const void *b)
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
hunk_fileline_cache_findAll(const char *filePath, int lineNo, uint32_t **outAddrs, int *outCount)
{
    if (outAddrs) {
        *outAddrs = NULL;
    }
    if (outCount) {
        *outCount = 0;
    }
    if (!filePath || !*filePath || lineNo <= 0 || !outAddrs || !outCount) {
        return 0;
    }

    int matchCount = 0;
    for (int i = 0; i < hunk_fileline_cache_state.entryCount; ++i) {
        const hunk_fileline_cache_entry_t *entry = &hunk_fileline_cache_state.entries[i];
        if (entry->line != lineNo) {
            continue;
        }
        if (!hunk_fileline_cache_fileMatches(entry->path, filePath)) {
            continue;
        }
        matchCount++;
    }
    if (matchCount <= 0) {
        return 0;
    }

    uint32_t *matches = (uint32_t *)alloc_alloc(sizeof(*matches) * (size_t)matchCount);
    if (!matches) {
        return 0;
    }

    int writeIndex = 0;
    for (int i = 0; i < hunk_fileline_cache_state.entryCount; ++i) {
        const hunk_fileline_cache_entry_t *entry = &hunk_fileline_cache_state.entries[i];
        if (entry->line != lineNo) {
            continue;
        }
        if (!hunk_fileline_cache_fileMatches(entry->path, filePath)) {
            continue;
        }
        matches[writeIndex++] = hunk_fileline_cache_mapEntryAddr(entry);
    }

    qsort(matches, (size_t)writeIndex, sizeof(*matches), hunk_fileline_cache_compareAddrs);

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

    *outAddrs = matches;
    *outCount = uniqueCount;
    return 1;
}

static int
hunk_fileline_cache_ensure(const char *elfPath)
{
    const char *toolchain = debugger.libretro.toolchainPrefix;
    if (hunk_fileline_cache_state.cacheReady &&
        strcmp(hunk_fileline_cache_state.elfPath, elfPath) == 0 &&
        strcmp(hunk_fileline_cache_state.toolchainPrefix, toolchain) == 0) {
        return 1;
    }

    hunk_fileline_cache_clear();
    if (!hunk_fileline_cache_buildFromSymbols(elfPath)) {
        return 0;
    }
    debugger_copyPath(hunk_fileline_cache_state.elfPath, sizeof(hunk_fileline_cache_state.elfPath), elfPath);
    debugger_copyPath(hunk_fileline_cache_state.toolchainPrefix,
                      sizeof(hunk_fileline_cache_state.toolchainPrefix),
                      toolchain);
    hunk_fileline_cache_state.cacheReady = 1;
    hunk_fileline_cache_state.disassemblyScanned = 0;
    hunk_fileline_cache_state.sectionSweepScanned = 0;
    return 1;
}

int
hunk_fileline_cache_resolveFileLine(const char *elfPath, const char *filePath, int lineNo, uint32_t *outAddr)
{
    if (outAddr) {
        *outAddr = 0;
    }
    if (!elfPath || !*elfPath || !filePath || !*filePath || lineNo <= 0 || !outAddr) {
        return 0;
    }
    uint32_t *matches = NULL;
    int matchCount = 0;
    if (!hunk_fileline_cache_resolveFileLineAll(elfPath, filePath, lineNo, &matches, &matchCount)) {
        return 0;
    }
    *outAddr = matches[0];
    alloc_free(matches);
    return 1;
}

int
hunk_fileline_cache_resolveFileLineAll(const char *elfPath, const char *filePath, int lineNo,
                                       uint32_t **outAddrs, int *outCount)
{
    if (outAddrs) {
        *outAddrs = NULL;
    }
    if (outCount) {
        *outCount = 0;
    }
    if (!elfPath || !*elfPath || !filePath || !*filePath || lineNo <= 0 || !outAddrs || !outCount) {
        return 0;
    }
    if (!hunk_fileline_cache_ensure(elfPath)) {
        return 0;
    }
    if (hunk_fileline_cache_findAll(filePath, lineNo, outAddrs, outCount)) {
        return 1;
    }

    if (!hunk_fileline_cache_state.disassemblyScanned) {
        hunk_fileline_cache_state.disassemblyScanned = 1;
        (void)hunk_fileline_cache_buildFromDisassembly(elfPath);
        if (hunk_fileline_cache_findAll(filePath, lineNo, outAddrs, outCount)) {
            return 1;
        }
    }
    if (!hunk_fileline_cache_state.sectionSweepScanned) {
        hunk_fileline_cache_state.sectionSweepScanned = 1;
        (void)hunk_fileline_cache_buildFromSectionSweep(elfPath);
        if (hunk_fileline_cache_findAll(filePath, lineNo, outAddrs, outCount)) {
            return 1;
        }
    }
    return 0;
}
