/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "alloc.h"
#include "debugger.h"
#include "libretro_host.h"
#include "source_z80.h"
#include "strutil.h"
#include "target.h"

enum
{
    SOURCE_Z80_MAX_INSN_BYTES = 4,
    SOURCE_Z80_MAX_BANK_NOI_FILES = 16,
    SOURCE_Z80_BASENAME_CAP = 128
};

typedef struct source_z80_symbol {
    uint16_t addr;
    int order;
    char *name;
} source_z80_symbol_t;

typedef struct source_z80_symbol_cache {
    source_z80_symbol_t *symbols;
    int symbolCount;
    int symbolCap;
    int nextOrder;
    int ready;
    char symbolBaseDir[PATH_MAX];
    char symbolBaseName[SOURCE_Z80_BASENAME_CAP];
} source_z80_symbol_cache_t;

typedef struct source_z80_source_entry {
    uint16_t addr;
    int line;
    char *path;
} source_z80_source_entry_t;

typedef struct source_z80_source_cache {
    source_z80_source_entry_t *entries;
    int entryCount;
    int entryCap;
    int ready;
    char symbolBaseDir[PATH_MAX];
    char sourceMapBaseName[SOURCE_Z80_BASENAME_CAP];
} source_z80_source_cache_t;

static source_z80_symbol_cache_t source_z80_symbolCache;
static source_z80_source_cache_t source_z80_sourceCache;
static uint64_t source_z80_sourceMapRevision;
static int source_z80_processorIdCached;
static uint32_t source_z80_cachedProcessorId;

typedef struct source_z80_sidecar_detect {
    const char *symbolBaseDir;
    char singleNoi[SOURCE_Z80_BASENAME_CAP];
    char singleSourceMap[SOURCE_Z80_BASENAME_CAP];
    char singlePair[SOURCE_Z80_BASENAME_CAP];
    int noiCount;
    int sourceMapCount;
    int pairCount;
} source_z80_sidecar_detect_t;

static int
source_z80_findProcessorId(uint32_t *outProcessorId);

static int
source_z80_resolveSymbol(uint64_t addr, const char **outName, uint16_t *outSymbolAddr);

static int
source_z80_resolveExactSymbol(uint16_t addr, const char **outName);

static int
source_z80_isHexDigit(char ch);

static void
source_z80_detectSidecars(const char *symbolBaseDir,
                          char *outSymbolBaseName,
                          size_t symbolBaseNameCap,
                          char *outSourceMapBaseName,
                          size_t sourceMapBaseNameCap);

static int
source_z80_pathExistsDir(const char *path)
{
    struct stat st;

    if (!path || !path[0]) {
        return 0;
    }
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

static int
source_z80_pathExistsRegular(const char *path)
{
    struct stat st;

    if (!path || !path[0]) {
        return 0;
    }
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISREG(st.st_mode) ? 1 : 0;
}

static int
source_z80_hasSuffix(const char *text, const char *suffix)
{
    if (!text || !suffix) {
        return 0;
    }
    size_t textLen = strlen(text);
    size_t suffixLen = strlen(suffix);
    if (suffixLen > textLen) {
        return 0;
    }
    return strcmp(text + textLen - suffixLen, suffix) == 0;
}

static void
source_z80_stemFromLeaf(char *out, size_t cap, const char *leaf, const char *suffix)
{
    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (!leaf || !suffix || !source_z80_hasSuffix(leaf, suffix)) {
        return;
    }
    size_t suffixLen = strlen(suffix);
    size_t stemLen = strlen(leaf) - suffixLen;
    if (stemLen >= cap) {
        stemLen = cap - 1;
    }
    memcpy(out, leaf, stemLen);
    out[stemLen] = '\0';
}

static int
source_z80_makeLeaf(char *out, size_t cap, const char *stem, const char *suffix)
{
    size_t stemLen;
    size_t suffixLen;

    if (!out || cap == 0 || !stem || !stem[0] || !suffix) {
        return 0;
    }
    out[0] = '\0';
    stemLen = strlen(stem);
    suffixLen = strlen(suffix);
    if (stemLen + suffixLen + 1u > cap) {
        return 0;
    }
    memcpy(out, stem, stemLen);
    memcpy(out + stemLen, suffix, suffixLen + 1u);
    return 1;
}

static int
source_z80_isBankNoiStem(const char *stem)
{
    if (!stem) {
        return 0;
    }
    const char *bank = strstr(stem, "_bank");
    if (!bank || !bank[5]) {
        return 0;
    }
    const char *digits = bank + 5;
    for (const char *p = digits; *p; ++p) {
        if (!isdigit((unsigned char)*p)) {
            return 0;
        }
    }
    return digits[0] ? 1 : 0;
}

static int
source_z80_sidecarExists(const char *symbolBaseDir, const char *stem, const char *suffix)
{
    char leaf[SOURCE_Z80_BASENAME_CAP + 16];
    char path[PATH_MAX];

    if (!symbolBaseDir || !symbolBaseDir[0] || !stem || !stem[0] || !suffix) {
        return 0;
    }
    if (!source_z80_makeLeaf(leaf, sizeof(leaf), stem, suffix)) {
        return 0;
    }
    strutil_pathJoinTrunc(path, sizeof(path), symbolBaseDir, leaf);
    return source_z80_pathExistsRegular(path);
}

static const char *
source_z80_basename(const char *path)
{
    const char *slash = path ? strrchr(path, '/') : NULL;
    const char *back = path ? strrchr(path, '\\') : NULL;
    const char *sep = slash;

    if (back && (!sep || back > sep)) {
        sep = back;
    }
    return sep ? sep + 1 : path;
}

static int
source_z80_detectSidecarFile(const char *path, void *user)
{
    source_z80_sidecar_detect_t *detect = (source_z80_sidecar_detect_t *)user;
    const char *leaf = source_z80_basename(path);
    char stem[SOURCE_Z80_BASENAME_CAP];

    if (!detect || !leaf || !leaf[0]) {
        return 1;
    }
    if (source_z80_hasSuffix(leaf, ".noi")) {
        source_z80_stemFromLeaf(stem, sizeof(stem), leaf, ".noi");
        if (stem[0] && !source_z80_isBankNoiStem(stem)) {
            detect->noiCount++;
            strutil_strlcpy(detect->singleNoi, sizeof(detect->singleNoi), stem);
        }
    } else if (source_z80_hasSuffix(leaf, ".z80srcmap")) {
        source_z80_stemFromLeaf(stem, sizeof(stem), leaf, ".z80srcmap");
        if (stem[0]) {
            detect->sourceMapCount++;
            strutil_strlcpy(detect->singleSourceMap, sizeof(detect->singleSourceMap), stem);
            if (source_z80_sidecarExists(detect->symbolBaseDir, stem, ".noi")) {
                detect->pairCount++;
                strutil_strlcpy(detect->singlePair, sizeof(detect->singlePair), stem);
            }
        }
    }
    return 1;
}

static void
source_z80_detectSidecars(const char *symbolBaseDir,
                          char *outSymbolBaseName,
                          size_t symbolBaseNameCap,
                          char *outSourceMapBaseName,
                          size_t sourceMapBaseNameCap)
{
    source_z80_sidecar_detect_t detect;
    memset(&detect, 0, sizeof(detect));
    detect.symbolBaseDir = symbolBaseDir;

    if (outSymbolBaseName && symbolBaseNameCap > 0) {
        outSymbolBaseName[0] = '\0';
    }
    if (outSourceMapBaseName && sourceMapBaseNameCap > 0) {
        outSourceMapBaseName[0] = '\0';
    }

    if (!symbolBaseDir || !symbolBaseDir[0]) {
        return;
    }
    if (!debugger_platform_scanFolder(symbolBaseDir, source_z80_detectSidecarFile, &detect)) {
        return;
    }

    if (detect.pairCount == 1) {
        if (outSymbolBaseName && symbolBaseNameCap > 0) {
            strutil_strlcpy(outSymbolBaseName, symbolBaseNameCap, detect.singlePair);
        }
        if (outSourceMapBaseName && sourceMapBaseNameCap > 0) {
            strutil_strlcpy(outSourceMapBaseName, sourceMapBaseNameCap, detect.singlePair);
        }
        return;
    }
    if (detect.pairCount > 1) {
        printf("source_z80: multiple Z80 sidecar pairs in %s\n", symbolBaseDir);
        return;
    }
    if (detect.noiCount == 1 && outSymbolBaseName && symbolBaseNameCap > 0) {
        strutil_strlcpy(outSymbolBaseName, symbolBaseNameCap, detect.singleNoi);
    } else if (detect.noiCount > 1) {
        printf("source_z80: multiple Z80 .noi files in %s\n", symbolBaseDir);
    }
    if (detect.sourceMapCount == 1 && outSourceMapBaseName && sourceMapBaseNameCap > 0) {
        strutil_strlcpy(outSourceMapBaseName, sourceMapBaseNameCap, detect.singleSourceMap);
    } else if (detect.sourceMapCount > 1) {
        printf("source_z80: multiple Z80 .z80srcmap files in %s\n", symbolBaseDir);
    }
}

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
    if (target == target_neogeo() && source_z80_processorIdCached) {
        if (outProcessorId) {
            *outProcessorId = source_z80_cachedProcessorId;
        }
        return 1;
    }
    if (!libretro_host_debugReadProcessors(processors, countof(processors), &count)) {
        return 0;
    }
    if (count > countof(processors)) {
        count = countof(processors);
    }
    for (size_t i = 0; i < count; ++i) {
        if (strcasecmp(processors[i].name, "Z80") == 0) {
            source_z80_cachedProcessorId = processors[i].id;
            source_z80_processorIdCached = 1;
            if (outProcessorId) {
                *outProcessorId = processors[i].id;
            }
            return 1;
        }
    }
    return 0;
}

void
source_z80_copySymbolBaseDir(char *out, size_t cap)
{
    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';

    const char *sourceDir = debugger.libretro.sourceDir;
    if (sourceDir && sourceDir[0]) {
        char buildDir[PATH_MAX];
        strutil_pathJoinTrunc(buildDir, sizeof(buildDir), sourceDir, "build");
        if (source_z80_pathExistsDir(buildDir)) {
            strutil_strlcpy(out, cap, buildDir);
            return;
        }
        if (source_z80_pathExistsDir(sourceDir)) {
            strutil_strlcpy(out, cap, sourceDir);
            return;
        }
    }

    const char *elf = debugger.libretro.exePath;
    if (!elf || !elf[0]) {
        return;
    }

    const char *slash = strrchr(elf, '/');
    const char *back = strrchr(elf, '\\');
    const char *sep = slash;
    if (back && (!sep || back > sep)) {
        sep = back;
    }
    if (!sep) {
        strutil_strlcpy(out, cap, ".");
        return;
    }

    size_t len = (size_t)(sep - elf);
    if (len == 0) {
        len = 1;
    }
    if (len >= cap) {
        len = cap - 1;
    }
    if (len > 0) {
        memcpy(out, elf, len);
    }
    out[len] = '\0';
}

static void
source_z80_clearSymbols(void)
{
    for (int i = 0; i < source_z80_symbolCache.symbolCount; ++i) {
        alloc_free(source_z80_symbolCache.symbols[i].name);
        source_z80_symbolCache.symbols[i].name = NULL;
    }
    alloc_free(source_z80_symbolCache.symbols);
    source_z80_symbolCache.symbols = NULL;
    source_z80_symbolCache.symbolCount = 0;
    source_z80_symbolCache.symbolCap = 0;
    source_z80_symbolCache.nextOrder = 0;
    source_z80_symbolCache.ready = 0;
    source_z80_symbolCache.symbolBaseDir[0] = '\0';
    source_z80_symbolCache.symbolBaseName[0] = '\0';
}

static int
source_z80_symbolCompare(const void *a, const void *b)
{
    const source_z80_symbol_t *sa = (const source_z80_symbol_t *)a;
    const source_z80_symbol_t *sb = (const source_z80_symbol_t *)b;

    if (sa->addr < sb->addr) {
        return -1;
    }
    if (sa->addr > sb->addr) {
        return 1;
    }
    if (sa->order < sb->order) {
        return -1;
    }
    if (sa->order > sb->order) {
        return 1;
    }
    return 0;
}

static int
source_z80_ensureSymbolCapacity(int minCap)
{
    if (source_z80_symbolCache.symbolCap >= minCap) {
        return 1;
    }

    int nextCap = source_z80_symbolCache.symbolCap > 0 ? source_z80_symbolCache.symbolCap : 256;
    while (nextCap < minCap) {
        nextCap *= 2;
    }

    source_z80_symbol_t *nextSymbols =
        (source_z80_symbol_t *)alloc_realloc(source_z80_symbolCache.symbols,
                                             (size_t)nextCap * sizeof(*nextSymbols));
    if (!nextSymbols) {
        return 0;
    }
    for (int i = source_z80_symbolCache.symbolCap; i < nextCap; ++i) {
        nextSymbols[i].addr = 0;
        nextSymbols[i].order = 0;
        nextSymbols[i].name = NULL;
    }
    source_z80_symbolCache.symbols = nextSymbols;
    source_z80_symbolCache.symbolCap = nextCap;
    return 1;
}

static int
source_z80_addSymbol(uint16_t addr, const char *name)
{
    if (!name || !name[0]) {
        return 0;
    }
    if (!source_z80_ensureSymbolCapacity(source_z80_symbolCache.symbolCount + 1)) {
        return 0;
    }

    char *nameDup = alloc_strdup(name);
    if (!nameDup) {
        return 0;
    }

    source_z80_symbol_t *symbol = &source_z80_symbolCache.symbols[source_z80_symbolCache.symbolCount++];
    symbol->addr = addr;
    symbol->order = source_z80_symbolCache.nextOrder++;
    symbol->name = nameDup;
    return 1;
}

static int
source_z80_parseNoiAddress(const char *text, uint16_t *outAddr)
{
    const char *digits = text;
    char normalized[64];
    size_t pos = 0;

    if (!text || !outAddr) {
        return 0;
    }
    if (digits[0] == '$') {
        digits++;
    } else if (digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
        digits += 2;
    }
    if (!source_z80_isHexDigit(*digits)) {
        return 0;
    }
    while (source_z80_isHexDigit(*digits)) {
        if (pos + 1 >= sizeof(normalized)) {
            return 0;
        }
        normalized[pos++] = *digits++;
    }
    if (*digits != '\0' || pos == 0) {
        return 0;
    }
    normalized[pos] = '\0';

    errno = 0;
    char *end = NULL;
    unsigned long value = strtoul(normalized, &end, 16);
    if (errno != 0 || !end || *end != '\0' || value > 0xfffful) {
        return 0;
    }

    *outAddr = (uint16_t)value;
    return 1;
}

static int
source_z80_parseNoiDefLine(const char *line, uint16_t *outAddr, char *outName, size_t nameCap)
{
    char tag[8];
    char name[256];
    char addrText[64];

    if (!line || !outAddr || !outName || nameCap == 0) {
        return 0;
    }
    tag[0] = '\0';
    name[0] = '\0';
    addrText[0] = '\0';
    if (sscanf(line, "%7s %255s %63s", tag, name, addrText) != 3) {
        return 0;
    }
    if (strcmp(tag, "DEF") != 0 || !name[0]) {
        return 0;
    }

    uint16_t addr = 0;
    if (!source_z80_parseNoiAddress(addrText, &addr)) {
        return 0;
    }

    *outAddr = addr;
    strutil_strlcpy(outName, nameCap, name);
    return 1;
}

static int
source_z80_loadNoiFile(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return 0;
    }

    int added = 0;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        uint16_t addr = 0;
        char name[256];

        if (source_z80_parseNoiDefLine(line, &addr, name, sizeof(name))) {
            if (source_z80_addSymbol(addr, name)) {
                added++;
            }
        }
    }

    fclose(fp);
    return added > 0 ? 1 : 0;
}

static void
source_z80_buildSidecarPath(char *out, size_t cap, const char *leaf)
{
    char symbolBaseDir[PATH_MAX];

    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';
    source_z80_copySymbolBaseDir(symbolBaseDir, sizeof(symbolBaseDir));
    strutil_pathJoinTrunc(out, cap, symbolBaseDir, leaf);
}

static void
source_z80_loadSymbols(void)
{
    char symbolBaseDir[PATH_MAX];
    char symbolBaseName[SOURCE_Z80_BASENAME_CAP];

    source_z80_copySymbolBaseDir(symbolBaseDir, sizeof(symbolBaseDir));
    if (!symbolBaseDir[0]) {
        source_z80_symbolCache.ready = 1;
        return;
    }
    source_z80_detectSidecars(symbolBaseDir, symbolBaseName, sizeof(symbolBaseName), NULL, 0);

    char path[PATH_MAX];
    char leaf[SOURCE_Z80_BASENAME_CAP + 32];
    if (!source_z80_makeLeaf(leaf, sizeof(leaf), symbolBaseName, ".noi")) {
        strutil_strlcpy(source_z80_symbolCache.symbolBaseDir,
                        sizeof(source_z80_symbolCache.symbolBaseDir),
                        symbolBaseDir);
        source_z80_symbolCache.symbolBaseName[0] = '\0';
        source_z80_symbolCache.ready = 1;
        return;
    }
    source_z80_buildSidecarPath(path, sizeof(path), leaf);
    (void)source_z80_loadNoiFile(path);

    for (int bank = 1; bank < SOURCE_Z80_MAX_BANK_NOI_FILES; ++bank) {
        char suffix[32];

        snprintf(suffix, sizeof(suffix), "_bank%d.noi", bank);
        if (!source_z80_makeLeaf(leaf, sizeof(leaf), symbolBaseName, suffix)) {
            continue;
        }
        source_z80_buildSidecarPath(path, sizeof(path), leaf);
        (void)source_z80_loadNoiFile(path);
    }

    if (source_z80_symbolCache.symbolCount > 1) {
        qsort(source_z80_symbolCache.symbols,
              (size_t)source_z80_symbolCache.symbolCount,
              sizeof(*source_z80_symbolCache.symbols),
              source_z80_symbolCompare);
    }
    strutil_strlcpy(source_z80_symbolCache.symbolBaseDir,
                    sizeof(source_z80_symbolCache.symbolBaseDir),
                    symbolBaseDir);
    strutil_strlcpy(source_z80_symbolCache.symbolBaseName,
                    sizeof(source_z80_symbolCache.symbolBaseName),
                    symbolBaseName);
    source_z80_symbolCache.ready = 1;
}

static void
source_z80_ensureSymbols(void)
{
    char symbolBaseDir[PATH_MAX];

    source_z80_copySymbolBaseDir(symbolBaseDir, sizeof(symbolBaseDir));
    if (source_z80_symbolCache.ready &&
        strcmp(source_z80_symbolCache.symbolBaseDir, symbolBaseDir) == 0) {
        return;
    }

    source_z80_clearSymbols();
    source_z80_loadSymbols();
}

static int
source_z80_resolveSymbol(uint64_t addr, const char **outName, uint16_t *outSymbolAddr)
{
    if (outName) {
        *outName = NULL;
    }
    if (outSymbolAddr) {
        *outSymbolAddr = 0;
    }

    source_z80_ensureSymbols();
    if (source_z80_symbolCache.symbolCount <= 0) {
        return 0;
    }

    uint16_t queryAddr = (uint16_t)source_z80_resolveAnchorAddr(addr);
    int lo = 0;
    int hi = source_z80_symbolCache.symbolCount - 1;
    int best = -1;

    while (lo <= hi) {
        int mid = lo + ((hi - lo) / 2);
        uint16_t symbolAddr = source_z80_symbolCache.symbols[mid].addr;

        if (symbolAddr <= queryAddr) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    if (best < 0) {
        return 0;
    }

    while (best > 0 &&
           source_z80_symbolCache.symbols[best - 1].addr == source_z80_symbolCache.symbols[best].addr) {
        best--;
    }

    if (outName) {
        *outName = source_z80_symbolCache.symbols[best].name;
    }
    if (outSymbolAddr) {
        *outSymbolAddr = source_z80_symbolCache.symbols[best].addr;
    }
    return source_z80_symbolCache.symbols[best].name ? 1 : 0;
}

static int
source_z80_resolveExactSymbol(uint16_t addr, const char **outName)
{
    if (outName) {
        *outName = NULL;
    }

    source_z80_ensureSymbols();
    if (source_z80_symbolCache.symbolCount <= 0) {
        return 0;
    }

    int lo = 0;
    int hi = source_z80_symbolCache.symbolCount - 1;
    int best = -1;

    while (lo <= hi) {
        int mid = lo + ((hi - lo) / 2);
        uint16_t symbolAddr = source_z80_symbolCache.symbols[mid].addr;

        if (symbolAddr == addr) {
            best = mid;
            break;
        }
        if (symbolAddr < addr) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    if (best < 0) {
        return 0;
    }
    while (best > 0 &&
           source_z80_symbolCache.symbols[best - 1].addr == source_z80_symbolCache.symbols[best].addr) {
        best--;
    }

    if (!source_z80_symbolCache.symbols[best].name) {
        return 0;
    }
    if (outName) {
        *outName = source_z80_symbolCache.symbols[best].name;
    }
    return 1;
}

int
source_z80_getSymbolCount(void)
{
    source_z80_ensureSymbols();
    return source_z80_symbolCache.symbolCount;
}

int
source_z80_getSymbol(int index, const char **outName, uint16_t *outAddr)
{
    if (outName) {
        *outName = NULL;
    }
    if (outAddr) {
        *outAddr = 0;
    }

    source_z80_ensureSymbols();
    if (index < 0 || index >= source_z80_symbolCache.symbolCount) {
        return 0;
    }

    source_z80_symbol_t *symbol = &source_z80_symbolCache.symbols[index];
    if (!symbol->name) {
        return 0;
    }
    if (outName) {
        *outName = symbol->name;
    }
    if (outAddr) {
        *outAddr = symbol->addr;
    }
    return 1;
}

static void
source_z80_clearSourceMap(void)
{
    for (int i = 0; i < source_z80_sourceCache.entryCount; ++i) {
        alloc_free(source_z80_sourceCache.entries[i].path);
        source_z80_sourceCache.entries[i].path = NULL;
    }
    alloc_free(source_z80_sourceCache.entries);
    source_z80_sourceCache.entries = NULL;
    source_z80_sourceCache.entryCount = 0;
    source_z80_sourceCache.entryCap = 0;
    source_z80_sourceCache.ready = 0;
    source_z80_sourceCache.symbolBaseDir[0] = '\0';
    source_z80_sourceCache.sourceMapBaseName[0] = '\0';
}

static int
source_z80_sourceCompare(const void *a, const void *b)
{
    const source_z80_source_entry_t *ea = (const source_z80_source_entry_t *)a;
    const source_z80_source_entry_t *eb = (const source_z80_source_entry_t *)b;

    if (ea->addr < eb->addr) {
        return -1;
    }
    if (ea->addr > eb->addr) {
        return 1;
    }
    if (ea->line < eb->line) {
        return -1;
    }
    if (ea->line > eb->line) {
        return 1;
    }
    return strcmp(ea->path ? ea->path : "", eb->path ? eb->path : "");
}

static int
source_z80_ensureSourceCapacity(int minCap)
{
    if (source_z80_sourceCache.entryCap >= minCap) {
        return 1;
    }

    int nextCap = source_z80_sourceCache.entryCap > 0 ? source_z80_sourceCache.entryCap : 512;
    while (nextCap < minCap) {
        nextCap *= 2;
    }

    source_z80_source_entry_t *nextEntries =
        (source_z80_source_entry_t *)alloc_realloc(source_z80_sourceCache.entries,
                                                   (size_t)nextCap * sizeof(*nextEntries));
    if (!nextEntries) {
        return 0;
    }
    for (int i = source_z80_sourceCache.entryCap; i < nextCap; ++i) {
        nextEntries[i].addr = 0;
        nextEntries[i].line = 0;
        nextEntries[i].path = NULL;
    }
    source_z80_sourceCache.entries = nextEntries;
    source_z80_sourceCache.entryCap = nextCap;
    return 1;
}

static int
source_z80_addSourceEntry(uint16_t addr, const char *path, int line)
{
    if (!path || !path[0] || line <= 0) {
        return 0;
    }
    if (!source_z80_ensureSourceCapacity(source_z80_sourceCache.entryCount + 1)) {
        return 0;
    }

    char *pathDup = alloc_strdup(path);
    if (!pathDup) {
        return 0;
    }

    source_z80_source_entry_t *entry = &source_z80_sourceCache.entries[source_z80_sourceCache.entryCount++];
    entry->addr = addr;
    entry->line = line;
    entry->path = pathDup;
    return 1;
}

static int
source_z80_parseSourceMapLine(const char *line, uint16_t *outAddr, char *outPath, size_t pathCap, int *outLine)
{
    if (outAddr) {
        *outAddr = 0;
    }
    if (outPath && pathCap > 0) {
        outPath[0] = '\0';
    }
    if (outLine) {
        *outLine = 0;
    }
    if (!line || !outAddr || !outPath || pathCap == 0 || !outLine || line[0] == '#') {
        return 0;
    }

    const char *tab1 = strchr(line, '\t');
    if (!tab1) {
        return 0;
    }
    const char *tab2 = strchr(tab1 + 1, '\t');
    if (!tab2) {
        return 0;
    }

    char addrText[16];
    size_t addrLen = (size_t)(tab1 - line);
    if (addrLen == 0 || addrLen >= sizeof(addrText)) {
        return 0;
    }
    memcpy(addrText, line, addrLen);
    addrText[addrLen] = '\0';

    uint16_t addr = 0;
    if (!source_z80_parseNoiAddress(addrText, &addr)) {
        return 0;
    }

    size_t pathLen = (size_t)(tab2 - (tab1 + 1));
    if (pathLen == 0) {
        return 0;
    }
    if (pathLen >= pathCap) {
        pathLen = pathCap - 1;
    }
    memcpy(outPath, tab1 + 1, pathLen);
    outPath[pathLen] = '\0';

    int sourceLine = atoi(tab2 + 1);
    if (sourceLine <= 0) {
        return 0;
    }
    *outAddr = addr;
    *outLine = sourceLine;
    return 1;
}

static void
source_z80_loadSourceMap(void)
{
    char symbolBaseDir[PATH_MAX];
    char sourceMapBaseName[SOURCE_Z80_BASENAME_CAP];
    char path[PATH_MAX];

    source_z80_copySymbolBaseDir(symbolBaseDir, sizeof(symbolBaseDir));
    if (!symbolBaseDir[0]) {
        source_z80_sourceCache.ready = 1;
        source_z80_sourceMapRevision++;
        return;
    }
    source_z80_detectSidecars(symbolBaseDir, NULL, 0, sourceMapBaseName, sizeof(sourceMapBaseName));
    char leaf[SOURCE_Z80_BASENAME_CAP + 16];
    if (!source_z80_makeLeaf(leaf, sizeof(leaf), sourceMapBaseName, ".z80srcmap")) {
        strutil_strlcpy(source_z80_sourceCache.symbolBaseDir,
                        sizeof(source_z80_sourceCache.symbolBaseDir),
                        symbolBaseDir);
        source_z80_sourceCache.sourceMapBaseName[0] = '\0';
        source_z80_sourceCache.ready = 1;
        source_z80_sourceMapRevision++;
        return;
    }
    strutil_pathJoinTrunc(path, sizeof(path), symbolBaseDir, leaf);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        strutil_strlcpy(source_z80_sourceCache.symbolBaseDir,
                        sizeof(source_z80_sourceCache.symbolBaseDir),
                        symbolBaseDir);
        strutil_strlcpy(source_z80_sourceCache.sourceMapBaseName,
                        sizeof(source_z80_sourceCache.sourceMapBaseName),
                        sourceMapBaseName);
        source_z80_sourceCache.ready = 1;
        source_z80_sourceMapRevision++;
        return;
    }

    char raw[PATH_MAX + 64];
    while (fgets(raw, sizeof(raw), fp)) {
        size_t len = strlen(raw);
        while (len > 0 && (raw[len - 1] == '\n' || raw[len - 1] == '\r')) {
            raw[--len] = '\0';
        }
        uint16_t addr = 0;
        char sourcePath[PATH_MAX];
        int sourceLine = 0;
        if (source_z80_parseSourceMapLine(raw, &addr, sourcePath, sizeof(sourcePath), &sourceLine)) {
            (void)source_z80_addSourceEntry(addr, sourcePath, sourceLine);
        }
    }
    fclose(fp);

    if (source_z80_sourceCache.entryCount > 1) {
        qsort(source_z80_sourceCache.entries,
              (size_t)source_z80_sourceCache.entryCount,
              sizeof(*source_z80_sourceCache.entries),
              source_z80_sourceCompare);
    }
    strutil_strlcpy(source_z80_sourceCache.symbolBaseDir,
                    sizeof(source_z80_sourceCache.symbolBaseDir),
                    symbolBaseDir);
    strutil_strlcpy(source_z80_sourceCache.sourceMapBaseName,
                    sizeof(source_z80_sourceCache.sourceMapBaseName),
                    sourceMapBaseName);
    source_z80_sourceCache.ready = 1;
    source_z80_sourceMapRevision++;
    printf("source_z80: loaded %d source map rows from %s\n", source_z80_sourceCache.entryCount, path);
}

static void
source_z80_ensureSourceMap(void)
{
    char symbolBaseDir[PATH_MAX];

    source_z80_copySymbolBaseDir(symbolBaseDir, sizeof(symbolBaseDir));
    if (source_z80_sourceCache.ready &&
        strcmp(source_z80_sourceCache.symbolBaseDir, symbolBaseDir) == 0) {
        return;
    }

    source_z80_clearSourceMap();
    source_z80_loadSourceMap();
}

int
source_z80_resolveSourceLocation(uint16_t addr, char *outPath, size_t pathCap, int *outLine)
{
    if (outPath && pathCap > 0) {
        outPath[0] = '\0';
    }
    if (outLine) {
        *outLine = 0;
    }
    if (!outPath || pathCap == 0 || !outLine) {
        return 0;
    }

    source_z80_ensureSourceMap();
    if (source_z80_sourceCache.entryCount <= 0) {
        return 0;
    }

    int lo = 0;
    int hi = source_z80_sourceCache.entryCount - 1;
    int best = -1;
    while (lo <= hi) {
        int mid = lo + ((hi - lo) / 2);
        uint16_t entryAddr = source_z80_sourceCache.entries[mid].addr;
        if (entryAddr == addr) {
            best = mid;
            break;
        }
        if (entryAddr < addr) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    if (best < 0) {
        return 0;
    }
    while (best > 0 &&
           source_z80_sourceCache.entries[best - 1].addr == source_z80_sourceCache.entries[best].addr) {
        best--;
    }

    source_z80_source_entry_t *entry = &source_z80_sourceCache.entries[best];
    if (!entry->path || !entry->path[0] || entry->line <= 0) {
        return 0;
    }
    strutil_strlcpy(outPath, pathCap, entry->path);
    *outLine = entry->line;
    return 1;
}

int
source_z80_resolveSourceLineAddress(const char *path, int line, uint16_t *outAddr)
{
    if (outAddr) {
        *outAddr = 0;
    }
    if (!path || !path[0] || line <= 0 || !outAddr) {
        return 0;
    }

    source_z80_ensureSourceMap();
    for (int i = 0; i < source_z80_sourceCache.entryCount; ++i) {
        source_z80_source_entry_t *entry = &source_z80_sourceCache.entries[i];
        if (entry->line != line || !entry->path || !entry->path[0]) {
            continue;
        }
        if (!source_pane_fileMatches(entry->path, path)) {
            continue;
        }
        *outAddr = entry->addr;
        return 1;
    }
    return 0;
}

int
source_z80_getSourceLocationCount(void)
{
    source_z80_ensureSourceMap();
    return source_z80_sourceCache.entryCount;
}

int
source_z80_getSourceLocation(int index, uint16_t *outAddr, const char **outPath, int *outLine)
{
    source_z80_ensureSourceMap();
    if (outAddr) {
        *outAddr = 0;
    }
    if (outPath) {
        *outPath = NULL;
    }
    if (outLine) {
        *outLine = 0;
    }
    if (index < 0 || index >= source_z80_sourceCache.entryCount) {
        return 0;
    }
    source_z80_source_entry_t *entry = &source_z80_sourceCache.entries[index];
    if (outAddr) {
        *outAddr = entry->addr;
    }
    if (outPath) {
        *outPath = entry->path;
    }
    if (outLine) {
        *outLine = entry->line;
    }
    return 1;
}

uint64_t
source_z80_getSourceMapRevision(void)
{
    source_z80_ensureSourceMap();
    return source_z80_sourceMapRevision;
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

static void
source_z80_appendText(char *out, size_t cap, size_t *pos, const char *text)
{
    if (!text) {
        return;
    }
    for (size_t i = 0; text[i]; ++i) {
        if (!source_z80_appendChar(out, cap, pos, text[i])) {
            return;
        }
    }
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

static int
source_z80_isTargetMnemonic(const char *line)
{
    if (!line) {
        return 0;
    }
    return strncasecmp(line, "call", 4) == 0 ||
           strncasecmp(line, "jp", 2) == 0 ||
           strncasecmp(line, "jr", 2) == 0 ||
           strncasecmp(line, "djnz", 4) == 0;
}

static int
source_z80_parseHexOperand(const char *text, uint16_t *outAddr)
{
    const char *p = text;

    if (!text || !outAddr) {
        return 0;
    }
    while (*p && *p != '$') {
        p++;
    }
    if (*p != '$') {
        return 0;
    }
    p++;
    if (!source_z80_isHexDigit(*p)) {
        return 0;
    }

    unsigned long value = 0;
    int digits = 0;
    while (source_z80_isHexDigit(*p)) {
        value <<= 4;
        if (*p >= '0' && *p <= '9') {
            value += (unsigned long)(*p - '0');
        } else {
            value += (unsigned long)(toupper((unsigned char)*p) - 'A' + 10);
        }
        digits++;
        p++;
    }
    if (digits <= 0 || value > 0xfffful) {
        return 0;
    }

    *outAddr = (uint16_t)value;
    return 1;
}

static void
source_z80_annotateTargetSymbol(char *out, size_t cap, const char *line)
{
    uint16_t targetAddr = 0;
    const char *symbolName = NULL;
    char addrText[16];
    size_t pos = 0;

    if (!out || cap == 0) {
        return;
    }
    strutil_strlcpy(out, cap, line ? line : "");
    if (!line || !source_z80_isTargetMnemonic(line)) {
        return;
    }
    if (!source_z80_parseHexOperand(line, &targetAddr)) {
        return;
    }
    if (!source_z80_resolveExactSymbol(targetAddr, &symbolName) || !symbolName) {
        return;
    }

    snprintf(addrText, sizeof(addrText), "$%04X", (unsigned int)targetAddr);
    out[0] = '\0';
    source_z80_appendText(out, cap, &pos, line);
    source_z80_appendText(out, cap, &pos, " ; ");
    source_z80_appendText(out, cap, &pos, symbolName);
    source_z80_appendText(out, cap, &pos, " (");
    source_z80_appendText(out, cap, &pos, addrText);
    source_z80_appendText(out, cap, &pos, ")");
}

static size_t
source_z80_disassembleLine(uint64_t addr, char *out, size_t cap)
{
    char disasm[256];
    char formatted[256];
    char annotated[320];
    size_t insnLen = 0;

    if (!out || cap == 0) {
        return 1;
    }
    out[0] = '\0';
    disasm[0] = '\0';
    formatted[0] = '\0';
    annotated[0] = '\0';

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
    source_z80_formatDisassembly(formatted, sizeof(formatted), disasm);
    source_z80_annotateTargetSymbol(annotated, sizeof(annotated), formatted);

    const char *symbolName = NULL;
    uint16_t symbolAddr = 0;
    if (source_z80_resolveSymbol(addr, &symbolName, &symbolAddr) && symbolName) {
        uint16_t resolvedAddr = (uint16_t)source_z80_resolveAnchorAddr(addr);

        if (resolvedAddr == symbolAddr) {
            size_t pos = 0;

            out[0] = '\0';
            source_z80_appendText(out, cap, &pos, symbolName);
            source_z80_appendText(out, cap, &pos, ": ");
            source_z80_appendText(out, cap, &pos, annotated);
        } else {
            strutil_strlcpy(out, cap, annotated);
        }
    } else {
        strutil_strlcpy(out, cap, annotated);
    }

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
