/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <ctype.h>

#include "e9ui.h"
#include "debugger.h"
#include "source_pane_internal.h"
#include "debug.h"
#include "file.h"
#include "strutil.h"
#include "addr2line.h"
#include "source_z80.h"
#include "symbol_text_map.h"

void
source_pane_symbols_refreshSourceFunctions(e9ui_component_t *comp, source_pane_state_t *st,
                                           const char *source_file);

static int
source_pane_symbols_addAsmSymbol(source_pane_state_t *st, const char *name, uint64_t addr,
                                 const char *valueOverride);

static int
source_pane_symbols_isAsmLikeMode(source_pane_mode_t mode)
{
    return (mode == source_pane_mode_a ||
            mode == source_pane_mode_sym ||
            mode == source_pane_mode_h ||
            mode == source_pane_mode_cpr ||
            mode == source_pane_mode_z80) ? 1 : 0;
}

static int
source_pane_symbols_isSourceLikeMode(source_pane_mode_t mode)
{
    return (mode == source_pane_mode_c ||
            mode == source_pane_mode_z80s) ? 1 : 0;
}

static void
source_pane_symbols_copyZ80SourceToolchainKey(char *out, size_t cap)
{
    if (!out || cap == 0) {
        return;
    }
    unsigned long long revision = (unsigned long long)source_z80_getSourceMapRevision();
    snprintf(out, cap, "z80s:%llu", revision);
    out[cap - 1] = '\0';
}

static const char *
source_pane_symbols_basename(const char *path)
{
    if (!path || !path[0]) {
        return NULL;
    }
    const char *slash = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    const char *best = slash > back ? slash : back;
    return best ? best + 1 : path;
}

static int
source_pane_symbols_isAbsolutePath(const char *path)
{
    if (!path || !path[0]) {
        return 0;
    }
    if (path[0] == '/' || path[0] == '\\') {
        return 1;
    }
    if (isalpha((unsigned char)path[0]) && path[1] == ':') {
        return 1;
    }
    return 0;
}

static int
source_pane_symbols_hasCSourceExtension(const char *path)
{
    if (!path || !path[0]) {
        return 0;
    }
    const char *dot = strrchr(path, '.');
    if (!dot || !dot[1]) {
        return 0;
    }
    if (strcmp(dot, ".s") == 0 || strcmp(dot, ".S") == 0 ||
        strcmp(dot, ".asm") == 0 || strcmp(dot, ".ASM") == 0) {
        return 1;
    }
    if (strcmp(dot, ".inc") == 0 || strcmp(dot, ".INC") == 0) {
        return 1;
    }
    return strcmp(dot, ".c") == 0 || strcmp(dot, ".cc") == 0 ||
           strcmp(dot, ".cpp") == 0 || strcmp(dot, ".cxx") == 0;
}

static char *
source_pane_symbols_parseValueAfterColon(const char *line)
{
    if (!line) {
        return NULL;
    }
    const char *attr = strstr(line, "DW_AT_");
    if (!attr) {
        return NULL;
    }
    const char *colon = strchr(attr, ':');
    if (!colon || !colon[1]) {
        return NULL;
    }
    const char *start = colon + 1;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    if (*start == '(') {
        int depth = 0;
        const char *p = start;
        while (*p) {
            if (*p == '(') {
                depth++;
            } else if (*p == ')') {
                depth--;
                if (depth == 0) {
                    p++;
                    while (*p && isspace((unsigned char)*p)) {
                        p++;
                    }
                    if (*p == ':') {
                        start = p + 1;
                        while (*start && isspace((unsigned char)*start)) {
                            start++;
                        }
                    }
                    break;
                }
            }
            p++;
        }
    }
    if (!*start) {
        return NULL;
    }
    size_t len = strlen(start);
    while (len > 0 && isspace((unsigned char)start[len - 1])) {
        len--;
    }
    if (len == 0) {
        return NULL;
    }
    char *out = (char*)alloc_calloc(len + 1, 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

void
source_pane_symbols_clearSourceFiles(source_pane_state_t *st)
{
    if (!st) {
        return;
    }
    if (st->sourceFiles) {
        for (int i = 0; i < st->sourceFileCount; ++i) {
            alloc_free(st->sourceFiles[i]);
            alloc_free(st->sourceLabels[i]);
        }
    }
    alloc_free(st->sourceFiles);
    alloc_free(st->sourceLabels);
    alloc_free(st->sourceOptions);
    st->sourceFiles = NULL;
    st->sourceLabels = NULL;
    st->sourceOptions = NULL;
    st->sourceFileCount = 0;
    st->sourceFileCap = 0;
    st->sourceFilesLoaded = 0;
    st->sourceFilesElf[0] = '\0';
    st->sourceFilesToolchain[0] = '\0';
}

void
source_pane_symbols_clearSourceFunctions(source_pane_state_t *st)
{
    if (!st) {
        return;
    }
    if (st->sourceFunctionNames) {
        for (int i = 0; i < st->sourceFunctionCount; ++i) {
            alloc_free(st->sourceFunctionNames[i]);
            alloc_free(st->sourceFunctionFiles[i]);
            alloc_free(st->sourceFunctionLabels[i]);
            alloc_free(st->sourceFunctionValues[i]);
        }
    }
    alloc_free(st->sourceFunctionNames);
    alloc_free(st->sourceFunctionFiles);
    alloc_free(st->sourceFunctionLabels);
    alloc_free(st->sourceFunctionValues);
    alloc_free(st->sourceFunctionLines);
    alloc_free(st->sourceFunctionOptions);
    st->sourceFunctionNames = NULL;
    st->sourceFunctionFiles = NULL;
    st->sourceFunctionLabels = NULL;
    st->sourceFunctionValues = NULL;
    st->sourceFunctionLines = NULL;
    st->sourceFunctionOptions = NULL;
    st->sourceFunctionCount = 0;
    st->sourceFunctionCap = 0;
    st->sourceFunctionsLoaded = 0;
    st->sourceFunctionsElf[0] = '\0';
    st->sourceFunctionsToolchain[0] = '\0';
    st->sourceFunctionsFile[0] = '\0';
}

void
source_pane_symbols_clearAsmSymbols(source_pane_state_t *st)
{
    if (!st) {
        return;
    }
    if (st->asmSymbolNames) {
        for (int i = 0; i < st->asmSymbolCount; ++i) {
            alloc_free(st->asmSymbolNames[i]);
            alloc_free(st->asmSymbolLabels[i]);
            alloc_free(st->asmSymbolValues[i]);
        }
    }
    alloc_free(st->asmSymbolNames);
    alloc_free(st->asmSymbolLabels);
    alloc_free(st->asmSymbolValues);
    alloc_free(st->asmSymbolAddrs);
    alloc_free(st->asmSymbolOptions);
    st->asmSymbolNames = NULL;
    st->asmSymbolLabels = NULL;
    st->asmSymbolValues = NULL;
    st->asmSymbolAddrs = NULL;
    st->asmSymbolOptions = NULL;
    st->asmSymbolCount = 0;
    st->asmSymbolCap = 0;
    st->asmSymbolsLoaded = 0;
    st->asmSymbolsTextMapRevision = 0;
    st->asmSymbolsElf[0] = '\0';
    st->asmSymbolsToolchain[0] = '\0';
}

void
source_pane_symbols_clearAllCaches(source_pane_state_t *st)
{
    if (!st) {
        return;
    }

    source_pane_symbols_cache_t *prev = st->activeSymbols;
    st->activeSymbols = &st->primarySymbols;
    source_pane_symbols_clearSourceFiles(st);
    source_pane_symbols_clearSourceFunctions(st);
    source_pane_symbols_clearAsmSymbols(st);
    st->activeSymbols = &st->z80Symbols;
    source_pane_symbols_clearSourceFiles(st);
    source_pane_symbols_clearSourceFunctions(st);
    source_pane_symbols_clearAsmSymbols(st);
    st->activeSymbols = prev;
}

static int
source_pane_symbols_addSourceFunction(source_pane_state_t *st, const char *filePath, const char *name, int line)
{
    if (!st || !filePath || !filePath[0] || !name || !name[0] || line <= 0) {
        return 0;
    }
    for (int i = 0; i < st->sourceFunctionCount; ++i) {
        if (st->sourceFunctionLines[i] == line &&
            strcmp(st->sourceFunctionFiles[i], filePath) == 0 &&
            strcmp(st->sourceFunctionNames[i], name) == 0) {
            return 0;
        }
    }
    if (st->sourceFunctionCount >= st->sourceFunctionCap) {
        int nextCap = st->sourceFunctionCap > 0 ? st->sourceFunctionCap * 2 : 64;
        char **nextNames = (char**)alloc_calloc((size_t)nextCap, sizeof(*nextNames));
        char **nextFiles = (char**)alloc_calloc((size_t)nextCap, sizeof(*nextFiles));
        char **nextLabels = (char**)alloc_calloc((size_t)nextCap, sizeof(*nextLabels));
        char **nextValues = (char**)alloc_calloc((size_t)nextCap, sizeof(*nextValues));
        int *nextLines = (int*)alloc_calloc((size_t)nextCap, sizeof(*nextLines));
        e9ui_textbox_option_t *nextOptions =
            (e9ui_textbox_option_t*)alloc_calloc((size_t)nextCap, sizeof(*nextOptions));
        if (!nextNames || !nextFiles || !nextLabels || !nextValues || !nextLines || !nextOptions) {
            alloc_free(nextNames);
            alloc_free(nextFiles);
            alloc_free(nextLabels);
            alloc_free(nextValues);
            alloc_free(nextLines);
            alloc_free(nextOptions);
            return 0;
        }
        if (st->sourceFunctionCount > 0) {
            size_t count = (size_t)st->sourceFunctionCount;
            memcpy(nextNames, st->sourceFunctionNames, sizeof(*nextNames) * count);
            memcpy(nextFiles, st->sourceFunctionFiles, sizeof(*nextFiles) * count);
            memcpy(nextLabels, st->sourceFunctionLabels, sizeof(*nextLabels) * count);
            memcpy(nextValues, st->sourceFunctionValues, sizeof(*nextValues) * count);
            memcpy(nextLines, st->sourceFunctionLines, sizeof(*nextLines) * count);
            memcpy(nextOptions, st->sourceFunctionOptions, sizeof(*nextOptions) * count);
        }
        alloc_free(st->sourceFunctionNames);
        alloc_free(st->sourceFunctionFiles);
        alloc_free(st->sourceFunctionLabels);
        alloc_free(st->sourceFunctionValues);
        alloc_free(st->sourceFunctionLines);
        alloc_free(st->sourceFunctionOptions);
        st->sourceFunctionNames = nextNames;
        st->sourceFunctionFiles = nextFiles;
        st->sourceFunctionLabels = nextLabels;
        st->sourceFunctionValues = nextValues;
        st->sourceFunctionLines = nextLines;
        st->sourceFunctionOptions = nextOptions;
        st->sourceFunctionCap = nextCap;
    }

    char valueBuf[PATH_MAX + 64];
    snprintf(valueBuf, sizeof(valueBuf), "%d|%s|%s", line, filePath, name);
    char *nameDup = alloc_strdup(name);
    char *fileDup = alloc_strdup(filePath);
    char *labelDup = alloc_strdup(name);
    char *valueDup = alloc_strdup(valueBuf);
    if (!nameDup || !fileDup || !labelDup || !valueDup) {
        alloc_free(nameDup);
        alloc_free(fileDup);
        alloc_free(labelDup);
        alloc_free(valueDup);
        return 0;
    }

    int insertAt = st->sourceFunctionCount;
    while (insertAt > 0) {
        int prev = insertAt - 1;
        int prevLine = st->sourceFunctionLines[prev];
        int cmp = strcasecmp(st->sourceFunctionNames[prev], nameDup);
        if (cmp < 0 || (cmp == 0 && prevLine <= line)) {
            break;
        }
        st->sourceFunctionNames[insertAt] = st->sourceFunctionNames[prev];
        st->sourceFunctionFiles[insertAt] = st->sourceFunctionFiles[prev];
        st->sourceFunctionLabels[insertAt] = st->sourceFunctionLabels[prev];
        st->sourceFunctionValues[insertAt] = st->sourceFunctionValues[prev];
        st->sourceFunctionLines[insertAt] = st->sourceFunctionLines[prev];
        st->sourceFunctionOptions[insertAt] = st->sourceFunctionOptions[prev];
        insertAt--;
    }

    st->sourceFunctionNames[insertAt] = nameDup;
    st->sourceFunctionFiles[insertAt] = fileDup;
    st->sourceFunctionLabels[insertAt] = labelDup;
    st->sourceFunctionValues[insertAt] = valueDup;
    st->sourceFunctionLines[insertAt] = line;
    st->sourceFunctionOptions[insertAt].value = valueDup;
    st->sourceFunctionOptions[insertAt].label = labelDup;
    st->sourceFunctionCount++;
    return 1;
}

static int
source_pane_symbols_addSourceFile(source_pane_state_t *st, const char *path)
{
    if (!st || !path || !path[0]) {
        return 0;
    }
    char resolved[PATH_MAX];
    source_pane_resolveSourcePath(path, resolved, sizeof(resolved));
    if (!source_pane_symbols_hasCSourceExtension(resolved)) {
        return 0;
    }
    for (int i = 0; i < st->sourceFileCount; ++i) {
        if (source_pane_fileMatches(st->sourceFiles[i], resolved)) {
            return 0;
        }
    }
    if (st->sourceFileCount >= st->sourceFileCap) {
        int nextCap = st->sourceFileCap > 0 ? st->sourceFileCap * 2 : 32;
        char **nextFiles = (char**)alloc_calloc((size_t)nextCap, sizeof(*nextFiles));
        char **nextLabels = (char**)alloc_calloc((size_t)nextCap, sizeof(*nextLabels));
        e9ui_textbox_option_t *nextOptions =
            (e9ui_textbox_option_t*)alloc_calloc((size_t)nextCap, sizeof(*nextOptions));
        if (!nextFiles || !nextLabels || !nextOptions) {
            alloc_free(nextFiles);
            alloc_free(nextLabels);
            alloc_free(nextOptions);
            return 0;
        }
        if (st->sourceFileCount > 0) {
            memcpy(nextFiles, st->sourceFiles, sizeof(*nextFiles) * (size_t)st->sourceFileCount);
            memcpy(nextLabels, st->sourceLabels, sizeof(*nextLabels) * (size_t)st->sourceFileCount);
            memcpy(nextOptions, st->sourceOptions, sizeof(*nextOptions) * (size_t)st->sourceFileCount);
        }
        alloc_free(st->sourceFiles);
        alloc_free(st->sourceLabels);
        alloc_free(st->sourceOptions);
        st->sourceFiles = nextFiles;
        st->sourceLabels = nextLabels;
        st->sourceOptions = nextOptions;
        st->sourceFileCap = nextCap;
    }

    char *pathDup = alloc_strdup(resolved);
    if (!pathDup) {
        return 0;
    }
    const char *base = source_pane_symbols_basename(resolved);
    char *labelDup = alloc_strdup(base && *base ? base : resolved);
    if (!labelDup) {
        alloc_free(pathDup);
        return 0;
    }

    int idx = st->sourceFileCount++;
    st->sourceFiles[idx] = pathDup;
    st->sourceLabels[idx] = labelDup;
    st->sourceOptions[idx].value = st->sourceFiles[idx];
    st->sourceOptions[idx].label = st->sourceLabels[idx];
    return 1;
}

static int
source_pane_symbols_addUniqueRawPath(const char ***paths, int *count, int *cap, const char *path)
{
    if (!paths || !count || !cap || !path || !path[0]) {
        return 0;
    }
    for (int i = 0; i < *count; ++i) {
        if ((*paths)[i] && strcmp((*paths)[i], path) == 0) {
            return 0;
        }
    }
    if (*count >= *cap) {
        int nextCap = *cap > 0 ? *cap * 2 : 64;
        const char **nextPaths = (const char **)alloc_realloc((void *)*paths,
                                                              (size_t)nextCap * sizeof(*nextPaths));
        if (!nextPaths) {
            return 0;
        }
        *paths = nextPaths;
        *cap = nextCap;
    }
    (*paths)[(*count)++] = path;
    return 1;
}

static void
source_pane_symbols_prependBlankSourceOption(source_pane_state_t *st)
{
    if (!st) {
        return;
    }
    if (st->sourceFileCount > 0 && st->sourceFiles && st->sourceFiles[0] && st->sourceFiles[0][0] == '\0') {
        return;
    }
    if (!st->sourceFiles || !st->sourceOptions || !st->sourceLabels) {
        int cap = st->sourceFileCap > 0 ? st->sourceFileCap : 4;
        st->sourceFiles = (char**)alloc_calloc((size_t)cap, sizeof(*st->sourceFiles));
        st->sourceLabels = (char**)alloc_calloc((size_t)cap, sizeof(*st->sourceLabels));
        st->sourceOptions = (e9ui_textbox_option_t*)alloc_calloc((size_t)cap, sizeof(*st->sourceOptions));
        if (!st->sourceFiles || !st->sourceLabels || !st->sourceOptions) {
            alloc_free(st->sourceFiles);
            alloc_free(st->sourceLabels);
            alloc_free(st->sourceOptions);
            st->sourceFiles = NULL;
            st->sourceLabels = NULL;
            st->sourceOptions = NULL;
            st->sourceFileCount = 0;
            st->sourceFileCap = 0;
            return;
        }
        st->sourceFileCap = cap;
    } else if (st->sourceFileCount >= st->sourceFileCap) {
        int nextCap = st->sourceFileCap > 0 ? st->sourceFileCap * 2 : 32;
        char **nextFiles = (char**)alloc_calloc((size_t)nextCap, sizeof(*nextFiles));
        char **nextLabels = (char**)alloc_calloc((size_t)nextCap, sizeof(*nextLabels));
        e9ui_textbox_option_t *nextOptions =
            (e9ui_textbox_option_t*)alloc_calloc((size_t)nextCap, sizeof(*nextOptions));
        if (!nextFiles || !nextLabels || !nextOptions) {
            alloc_free(nextFiles);
            alloc_free(nextLabels);
            alloc_free(nextOptions);
            return;
        }
        memcpy(nextFiles, st->sourceFiles, sizeof(*nextFiles) * (size_t)st->sourceFileCount);
        memcpy(nextLabels, st->sourceLabels, sizeof(*nextLabels) * (size_t)st->sourceFileCount);
        memcpy(nextOptions, st->sourceOptions, sizeof(*nextOptions) * (size_t)st->sourceFileCount);
        alloc_free(st->sourceFiles);
        alloc_free(st->sourceLabels);
        alloc_free(st->sourceOptions);
        st->sourceFiles = nextFiles;
        st->sourceLabels = nextLabels;
        st->sourceOptions = nextOptions;
        st->sourceFileCap = nextCap;
    }
    for (int i = st->sourceFileCount; i > 0; --i) {
        st->sourceFiles[i] = st->sourceFiles[i - 1];
        st->sourceLabels[i] = st->sourceLabels[i - 1];
        st->sourceOptions[i] = st->sourceOptions[i - 1];
    }
    st->sourceFiles[0] = alloc_strdup("");
    st->sourceLabels[0] = alloc_strdup("");
    if (!st->sourceFiles[0] || !st->sourceLabels[0]) {
        alloc_free(st->sourceFiles[0]);
        alloc_free(st->sourceLabels[0]);
        for (int i = 0; i < st->sourceFileCount; ++i) {
            st->sourceFiles[i] = st->sourceFiles[i + 1];
            st->sourceLabels[i] = st->sourceLabels[i + 1];
            st->sourceOptions[i] = st->sourceOptions[i + 1];
        }
        return;
    }
    st->sourceOptions[0].value = st->sourceFiles[0];
    st->sourceOptions[0].label = st->sourceLabels[0];
    st->sourceFileCount++;
}

static int
source_pane_symbols_collectReadelfFiles(source_pane_state_t *st, const char *elfPath)
{
    if (!st || !elfPath || !elfPath[0]) {
        return 0;
    }
    if (debugger_toolchainUsesHunkAddr2line()) {
        return 0;
    }
    char readelf[PATH_MAX];
    if (!debugger_toolchainBuildBinary(readelf, sizeof(readelf), "readelf")) {
        debug_error("source_pane: failed to build readelf tool name (prefix='%s')",
                    debugger.libretro.toolchainPrefix);
        return 0;
    }
    char readelfExe[PATH_MAX];
    if (!file_findInPath(readelf, readelfExe, sizeof(readelfExe))) {
        debug_error("source_pane: readelf not found: '%s' (prefix='%s')",
                    readelf, debugger.libretro.toolchainPrefix);
        return 0;
    }
    char cmd[PATH_MAX * 2];
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), readelfExe, "--debug-dump=info", elfPath, 1)) {
        return 0;
    }
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return 0;
    }

    int added = 0;
    char line[1024];
    int cuOpen = 0;
    int cuDepth = 0;
    char cuName[PATH_MAX];
    char cuDir[PATH_MAX];
    cuName[0] = '\0';
    cuDir[0] = '\0';

    while (fgets(line, sizeof(line), fp)) {
        int depth = -1;
        if (sscanf(line, " <%d><", &depth) == 1) {
            int isCompileUnit = strstr(line, "DW_TAG_compile_unit") != NULL;
            if (cuOpen && (isCompileUnit || depth <= cuDepth)) {
                if (cuName[0]) {
                    char fullPath[PATH_MAX];
                    if (cuDir[0] && !source_pane_symbols_isAbsolutePath(cuName)) {
                        snprintf(fullPath, sizeof(fullPath), "%s/%s", cuDir, cuName);
                    } else {
                        strncpy(fullPath, cuName, sizeof(fullPath) - 1);
                        fullPath[sizeof(fullPath) - 1] = '\0';
                    }
                    added += source_pane_symbols_addSourceFile(st, fullPath);
                }
                cuOpen = 0;
                cuName[0] = '\0';
                cuDir[0] = '\0';
            }
            if (isCompileUnit) {
                cuOpen = 1;
                cuDepth = depth;
                cuName[0] = '\0';
                cuDir[0] = '\0';
                continue;
            }
        }
        if (!cuOpen) {
            continue;
        }
        if (!cuName[0] && strstr(line, "DW_AT_name")) {
            char *name = source_pane_symbols_parseValueAfterColon(line);
            if (name) {
                if (strcmp(name, "<artificial>") != 0) {
                    strncpy(cuName, name, sizeof(cuName) - 1);
                    cuName[sizeof(cuName) - 1] = '\0';
                }
                alloc_free(name);
            }
            continue;
        }
        if (!cuDir[0] && strstr(line, "DW_AT_comp_dir")) {
            char *dir = source_pane_symbols_parseValueAfterColon(line);
            if (dir) {
                strncpy(cuDir, dir, sizeof(cuDir) - 1);
                cuDir[sizeof(cuDir) - 1] = '\0';
                alloc_free(dir);
            }
            continue;
        }
    }

    if (cuOpen && cuName[0]) {
        char fullPath[PATH_MAX];
        if (cuDir[0] && !source_pane_symbols_isAbsolutePath(cuName)) {
            snprintf(fullPath, sizeof(fullPath), "%s/%s", cuDir, cuName);
        } else {
            strncpy(fullPath, cuName, sizeof(fullPath) - 1);
            fullPath[sizeof(fullPath) - 1] = '\0';
        }
        added += source_pane_symbols_addSourceFile(st, fullPath);
    }

    pclose(fp);
    return added;
}

static int
source_pane_symbols_collectStabsFiles(source_pane_state_t *st, const char *elfPath)
{
    if (!st || !elfPath || !elfPath[0]) {
        return 0;
    }
    if (debugger_toolchainUsesHunkAddr2line()) {
        return 0;
    }
    char objdump[PATH_MAX];
    if (!debugger_toolchainBuildBinary(objdump, sizeof(objdump), "objdump")) {
        debug_error("source_pane: failed to build objdump tool name (prefix='%s')",
                    debugger.libretro.toolchainPrefix);
        return 0;
    }
    char objdumpExe[PATH_MAX];
    if (!file_findInPath(objdump, objdumpExe, sizeof(objdumpExe))) {
        debug_error("source_pane: objdump not found: '%s' (prefix='%s')",
                    objdump, debugger.libretro.toolchainPrefix);
        return 0;
    }
    char cmd[PATH_MAX * 2];
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), objdumpExe, "-G", elfPath, 1)) {
        return 0;
    }
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return 0;
    }

    int added = 0;
    char currentDir[PATH_MAX];
    currentDir[0] = '\0';
    char line[2048];
    while (fgets(line, sizeof(line), fp)) {
        char *tokens[12];
        int count = 0;
        char *cursor = line;
        while (*cursor && isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (!*cursor) {
            continue;
        }
        while (count < (int)(sizeof(tokens) / sizeof(tokens[0]))) {
            while (*cursor && isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (!*cursor) {
                break;
            }
            tokens[count++] = cursor;
            while (*cursor && !isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (*cursor) {
                *cursor++ = '\0';
            }
        }
        if (count < 7) {
            continue;
        }
        const char *stabType = tokens[1];
        const char *stabStr = tokens[count - 1];
        if (!stabType || !stabStr || !stabStr[0]) {
            continue;
        }
        if (strcmp(stabType, "SO") != 0 && strcmp(stabType, "SOL") != 0) {
            continue;
        }
        if (strcmp(stabStr, "./") == 0 || strcmp(stabStr, ".\\") == 0) {
            strncpy(currentDir, stabStr, sizeof(currentDir) - 1);
            currentDir[sizeof(currentDir) - 1] = '\0';
            continue;
        }
        size_t len = strlen(stabStr);
        if (strcmp(stabType, "SO") == 0 && len > 0 &&
            (stabStr[len - 1] == '/' || stabStr[len - 1] == '\\')) {
            strncpy(currentDir, stabStr, sizeof(currentDir) - 1);
            currentDir[sizeof(currentDir) - 1] = '\0';
            continue;
        }
        char fullPath[PATH_MAX];
        if (!source_pane_symbols_isAbsolutePath(stabStr) && currentDir[0]) {
            snprintf(fullPath, sizeof(fullPath), "%s%s", currentDir, stabStr);
        } else {
            strncpy(fullPath, stabStr, sizeof(fullPath) - 1);
            fullPath[sizeof(fullPath) - 1] = '\0';
        }
        added += source_pane_symbols_addSourceFile(st, fullPath);
    }
    pclose(fp);
    return added;
}

static int
source_pane_symbols_collectZ80SourceFiles(source_pane_state_t *st)
{
    if (!st) {
        return 0;
    }

    int added = 0;
    int count = source_z80_getSourceLocationCount();
    int uniqueCount = 0;
    int uniqueCap = 0;
    const char **uniquePaths = NULL;
    for (int i = 0; i < count; ++i) {
        const char *path = NULL;
        if (!source_z80_getSourceLocation(i, NULL, &path, NULL)) {
            continue;
        }
        if (!source_pane_symbols_hasCSourceExtension(path)) {
            continue;
        }
        (void)source_pane_symbols_addUniqueRawPath(&uniquePaths, &uniqueCount, &uniqueCap, path);
    }
    for (int i = 0; i < uniqueCount; ++i) {
        added += source_pane_symbols_addSourceFile(st, uniquePaths[i]);
    }
    alloc_free(uniquePaths);
    return added;
}

void
source_pane_symbols_syncFileSelect(e9ui_component_t *comp, source_pane_state_t *st)
{
    if (!comp || !st || !st->fileSelectMeta) {
        return;
    }
    e9ui_component_t *select = e9ui_child_find(comp, st->fileSelectMeta);
    if (!select) {
        return;
    }
    e9ui_setHidden(select, source_pane_symbols_isSourceLikeMode(st->viewMode) ? 0 : 1);
    if (!source_pane_symbols_isSourceLikeMode(st->viewMode)) {
        return;
    }
    int editingSelect = (e9ui && e9ui_getFocus(&e9ui->ctx) == select) ? 1 : 0;
    if (!editingSelect) {
        e9ui_textbox_setOptions(select, st->sourceOptions, st->sourceFileCount);
    }
    select->disabled = st->sourceFileCount <= 1 ? 1 : 0;

    const char *displayPath = NULL;
    if (st->manualSrcActive && st->manualSrcPath) {
        displayPath = st->manualSrcPath;
    } else if (st->curSrcPath[0]) {
        displayPath = st->curSrcPath;
    }
    if (!displayPath || !displayPath[0]) {
        e9ui_textbox_setSelectedValue(select, "");
        return;
    }
    if (editingSelect) {
        return;
    }
    for (int i = 0; i < st->sourceFileCount; ++i) {
        if (source_pane_fileMatches(st->sourceFiles[i], displayPath)) {
            e9ui_textbox_setSelectedValue(select, st->sourceFiles[i]);
            return;
        }
    }
    e9ui_textbox_setSelectedValue(select, "");
}

void
source_pane_symbols_syncFunctionSelect(e9ui_component_t *comp, source_pane_state_t *st)
{
    if (!comp || !st || !st->functionSelectMeta) {
        return;
    }
    e9ui_component_t *select = e9ui_child_find(comp, st->functionSelectMeta);
    if (!select) {
        return;
    }
    e9ui_setHidden(select, source_pane_symbols_isSourceLikeMode(st->viewMode) ? 0 : 1);
    if (!source_pane_symbols_isSourceLikeMode(st->viewMode)) {
        return;
    }
    int editingSelect = (e9ui && e9ui_getFocus(&e9ui->ctx) == select) ? 1 : 0;
    if (!editingSelect) {
        e9ui_textbox_setOptions(select, st->sourceFunctionOptions, st->sourceFunctionCount);
    }
    select->disabled = st->sourceFunctionCount <= 0 ? 1 : 0;
}

void
source_pane_symbols_trackCurrentFunction(e9ui_component_t *comp, source_pane_state_t *st,
                                         const char *path, int line)
{
    if (!comp || !st || !path || !path[0] || line <= 0 || !st->functionSelectMeta) {
        return;
    }
    if (st->sourceFunctionCount <= 0) {
        return;
    }
    if (st->sourceFunctionsFile[0] && !source_pane_fileMatches(st->sourceFunctionsFile, path)) {
        return;
    }
    e9ui_component_t *select = e9ui_child_find(comp, st->functionSelectMeta);
    if (!select) {
        return;
    }
    if (e9ui && e9ui_getFocus(&e9ui->ctx) == select) {
        return;
    }

    int best = -1;
    for (int i = 0; i < st->sourceFunctionCount; ++i) {
        if (!source_pane_fileMatches(st->sourceFunctionFiles[i], path)) {
            continue;
        }
        if (st->sourceFunctionLines[i] <= line) {
            if (best < 0 || st->sourceFunctionLines[i] >= st->sourceFunctionLines[best]) {
                best = i;
            }
        }
    }
    if (best < 0) {
        for (int i = 0; i < st->sourceFunctionCount; ++i) {
            if (source_pane_fileMatches(st->sourceFunctionFiles[i], path)) {
                best = i;
                break;
            }
        }
    }
    if (best < 0) {
        for (int i = 0; i < st->sourceFunctionCount; ++i) {
            best = i;
            if (st->sourceFunctionLines[i] <= line) {
                if (st->sourceFunctionLines[i] >= st->sourceFunctionLines[best]) {
                    best = i;
                }
            }
        }
    }
    if (best < 0) {
        return;
    }
    e9ui_textbox_setSelectedValue(select, st->sourceFunctionValues[best]);
}

static int
source_pane_symbols_addAsmSymbol(source_pane_state_t *st, const char *name, uint64_t addr,
                                 const char *valueOverride)
{
    if (!st || !name || !name[0]) {
        return 0;
    }
    if (addr == 0 && (!valueOverride || !valueOverride[0])) {
        return 0;
    }
    for (int i = 0; i < st->asmSymbolCount; ++i) {
        if (strcmp(st->asmSymbolNames[i], name) == 0) {
            if (valueOverride && valueOverride[0]) {
                if (st->asmSymbolValues[i] && strcmp(st->asmSymbolValues[i], valueOverride) == 0) {
                    return 0;
                }
            } else if (st->asmSymbolAddrs[i] == addr) {
                return 0;
            }
        }
    }
    if (st->asmSymbolCount >= st->asmSymbolCap) {
        int nextCap = st->asmSymbolCap > 0 ? st->asmSymbolCap * 2 : 128;
        char **nextNames = (char**)alloc_calloc((size_t)nextCap, sizeof(*nextNames));
        char **nextLabels = (char**)alloc_calloc((size_t)nextCap, sizeof(*nextLabels));
        char **nextValues = (char**)alloc_calloc((size_t)nextCap, sizeof(*nextValues));
        uint64_t *nextAddrs = (uint64_t*)alloc_calloc((size_t)nextCap, sizeof(*nextAddrs));
        e9ui_textbox_option_t *nextOptions =
            (e9ui_textbox_option_t*)alloc_calloc((size_t)nextCap, sizeof(*nextOptions));
        if (!nextNames || !nextLabels || !nextValues || !nextAddrs || !nextOptions) {
            alloc_free(nextNames);
            alloc_free(nextLabels);
            alloc_free(nextValues);
            alloc_free(nextAddrs);
            alloc_free(nextOptions);
            return 0;
        }
        if (st->asmSymbolCount > 0) {
            size_t count = (size_t)st->asmSymbolCount;
            memcpy(nextNames, st->asmSymbolNames, sizeof(*nextNames) * count);
            memcpy(nextLabels, st->asmSymbolLabels, sizeof(*nextLabels) * count);
            memcpy(nextValues, st->asmSymbolValues, sizeof(*nextValues) * count);
            memcpy(nextAddrs, st->asmSymbolAddrs, sizeof(*nextAddrs) * count);
            memcpy(nextOptions, st->asmSymbolOptions, sizeof(*nextOptions) * count);
        }
        alloc_free(st->asmSymbolNames);
        alloc_free(st->asmSymbolLabels);
        alloc_free(st->asmSymbolValues);
        alloc_free(st->asmSymbolAddrs);
        alloc_free(st->asmSymbolOptions);
        st->asmSymbolNames = nextNames;
        st->asmSymbolLabels = nextLabels;
        st->asmSymbolValues = nextValues;
        st->asmSymbolAddrs = nextAddrs;
        st->asmSymbolOptions = nextOptions;
        st->asmSymbolCap = nextCap;
    }

    char valueBuf[32];
    if (!valueOverride || !valueOverride[0]) {
        snprintf(valueBuf, sizeof(valueBuf), "%llX", (unsigned long long)(addr & 0x00ffffffull));
    }
    char *nameDup = alloc_strdup(name);
    char *labelDup = alloc_strdup(name);
    char *valueDup = alloc_strdup((valueOverride && valueOverride[0]) ? valueOverride : valueBuf);
    if (!nameDup || !labelDup || !valueDup) {
        alloc_free(nameDup);
        alloc_free(labelDup);
        alloc_free(valueDup);
        return 0;
    }

    int insertAt = st->asmSymbolCount;
    while (insertAt > 0) {
        int prev = insertAt - 1;
        int cmp = strcasecmp(st->asmSymbolNames[prev], nameDup);
        if (cmp < 0 || (cmp == 0 && st->asmSymbolAddrs[prev] <= addr)) {
            break;
        }
        st->asmSymbolNames[insertAt] = st->asmSymbolNames[prev];
        st->asmSymbolLabels[insertAt] = st->asmSymbolLabels[prev];
        st->asmSymbolValues[insertAt] = st->asmSymbolValues[prev];
        st->asmSymbolAddrs[insertAt] = st->asmSymbolAddrs[prev];
        st->asmSymbolOptions[insertAt] = st->asmSymbolOptions[prev];
        insertAt--;
    }
    st->asmSymbolNames[insertAt] = nameDup;
    st->asmSymbolLabels[insertAt] = labelDup;
    st->asmSymbolValues[insertAt] = valueDup;
    st->asmSymbolAddrs[insertAt] = addr;
    st->asmSymbolOptions[insertAt].value = valueDup;
    st->asmSymbolOptions[insertAt].label = labelDup;
    st->asmSymbolCount++;
    return 1;
}

static int
source_pane_symbols_collectTextMapAsmSymbols(source_pane_state_t *st, const char *elf_path)
{
    const symbol_text_map_entry_t *entries = NULL;
    int count = 0;
    if (!st || !elf_path || !elf_path[0] || !debugger.symbolValid ||
        debugger.symbolFileKind != DEBUGGER_SYMBOL_FILE_KIND_TEXT_MAP) {
        return 0;
    }
    if (!symbol_text_map_getEntries(elf_path, &entries, &count) || !entries || count <= 0) {
        return 0;
    }

    int added = 0;
    for (int i = 0; i < count; ++i) {
        const symbol_text_map_entry_t *entry = &entries[i];
        added += source_pane_symbols_addAsmSymbol(st, entry->name, (uint64_t)entry->addr, NULL);
    }
    return added;
}

static int
source_pane_symbols_collectObjdumpTextAsmSymbols(source_pane_state_t *st, const char *elf_path)
{
    if (!st || !elf_path || !elf_path[0] || !debugger.elfValid) {
        return 0;
    }
    char objdump[PATH_MAX];
    if (!debugger_toolchainBuildBinary(objdump, sizeof(objdump), "objdump")) {
        return 0;
    }
    char objdumpExe[PATH_MAX];
    if (!file_findInPath(objdump, objdumpExe, sizeof(objdumpExe))) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), objdumpExe, "-t", elf_path, 1)) {
        return 0;
    }
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return 0;
    }

    int added = 0;
    char line[2048];
    while (fgets(line, sizeof(line), fp)) {
        char *tokens[16];
        int count = 0;
        char *cursor = line;
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
        uint64_t symbolAddr = 0;
        if (!source_pane_parseHex64(tokens[0], &symbolAddr) || symbolAddr == 0) {
            continue;
        }
        int textSymbol = 0;
        for (int i = 1; i < count; ++i) {
            if (strcmp(tokens[i], ".text") == 0 || strncmp(tokens[i], ".text.", 6) == 0) {
                textSymbol = 1;
                break;
            }
        }
        if (!textSymbol) {
            continue;
        }
        const char *symbolName = tokens[count - 1];
        if (!symbolName || !symbolName[0] || symbolName[0] == '.') {
            continue;
        }
        added += source_pane_symbols_addAsmSymbol(st, symbolName, symbolAddr, NULL);
    }

    pclose(fp);
    return added;
}

static int
source_pane_symbols_collectAsmSymbols(source_pane_state_t *st, const char *elf_path)
{
    if (!st || !elf_path || !elf_path[0] || !debugger.symbolValid) {
        return 0;
    }
    if (debugger.symbolFileKind == DEBUGGER_SYMBOL_FILE_KIND_TEXT_MAP) {
        return source_pane_symbols_collectTextMapAsmSymbols(st, elf_path);
    }

    if (debugger_toolchainUsesHunkAddr2line()) {
        return source_pane_symbols_collectObjdumpTextAsmSymbols(st, elf_path);
    }

    char readelf[PATH_MAX];
    if (!debugger_toolchainBuildBinary(readelf, sizeof(readelf), "readelf")) {
        return 0;
    }
    char readelfExe[PATH_MAX];
    if (!file_findInPath(readelf, readelfExe, sizeof(readelfExe))) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), readelfExe, "-Ws", elf_path, 1)) {
        return 0;
    }
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return 0;
    }

    int added = 0;
    char line[2048];
    while (fgets(line, sizeof(line), fp)) {
        char *tokens[12];
        int count = 0;
        char *cursor = line;
        while (*cursor && isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (!*cursor || !isdigit((unsigned char)*cursor)) {
            continue;
        }
        while (count < (int)(sizeof(tokens) / sizeof(tokens[0]))) {
            while (*cursor && isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (!*cursor) {
                break;
            }
            tokens[count++] = cursor;
            while (*cursor && !isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (*cursor) {
                *cursor++ = '\0';
            }
        }
        if (count < 8) {
            continue;
        }
        if (strcmp(tokens[3], "FUNC") != 0 || strcmp(tokens[6], "UND") == 0) {
            continue;
        }
        const char *symbolName = tokens[7];
        if (!symbolName || !symbolName[0] || strcmp(symbolName, "<null>") == 0) {
            continue;
        }
        uint64_t symbolAddr = 0;
        if (!source_pane_parseHex64(tokens[1], &symbolAddr) || symbolAddr == 0) {
            continue;
        }
        added += source_pane_symbols_addAsmSymbol(st, symbolName, symbolAddr, NULL);
    }
    pclose(fp);
    if (added == 0) {
        added += source_pane_symbols_collectObjdumpTextAsmSymbols(st, elf_path);
    }
    return added;
}

static int
source_pane_symbols_collectZ80AsmSymbols(source_pane_state_t *st)
{
    if (!st) {
        return 0;
    }

    int added = 0;
    int count = source_z80_getSymbolCount();
    for (int i = 0; i < count; ++i) {
        const char *name = NULL;
        uint16_t addr = 0;

        if (!source_z80_getSymbol(i, &name, &addr)) {
            continue;
        }
        added += source_pane_symbols_addAsmSymbol(st, name, (uint64_t)addr, NULL);
    }
    return added;
}

void
source_pane_symbols_refreshAsmSymbols(e9ui_component_t *comp, source_pane_state_t *st)
{
    if (!comp || !st || !st->asmSymbolSelectMeta) {
        return;
    }
    e9ui_component_t *select = e9ui_child_find(comp, st->asmSymbolSelectMeta);
    if (!select) {
        return;
    }
    int asmLikeMode = source_pane_symbols_isAsmLikeMode(st->viewMode);
    e9ui_setHidden(select, asmLikeMode ? 0 : 1);
    if (!asmLikeMode) {
        return;
    }
    int editingSelect = (e9ui && e9ui_getFocus(&e9ui->ctx) == select) ? 1 : 0;

    if (st->viewMode == source_pane_mode_z80) {
        char symbolBaseDir[PATH_MAX];

        source_z80_copySymbolBaseDir(symbolBaseDir, sizeof(symbolBaseDir));
        if (st->asmSymbolsLoaded &&
            strcmp(st->asmSymbolsElf, symbolBaseDir) == 0 &&
            strcmp(st->asmSymbolsToolchain, "z80-noi") == 0) {
            if (!editingSelect) {
                e9ui_textbox_setOptions(select, st->asmSymbolOptions, st->asmSymbolCount);
            }
            select->disabled = st->asmSymbolCount <= 0 ? 1 : 0;
            return;
        }

        e9ui_textbox_setOptions(select, NULL, 0);
        source_pane_symbols_clearAsmSymbols(st);
        (void)source_pane_symbols_collectZ80AsmSymbols(st);
        st->asmSymbolsLoaded = 1;
        st->asmSymbolsTextMapRevision = 0;
        strutil_strlcpy(st->asmSymbolsElf, sizeof(st->asmSymbolsElf), symbolBaseDir);
        strutil_strlcpy(st->asmSymbolsToolchain, sizeof(st->asmSymbolsToolchain), "z80-noi");
        if (!editingSelect) {
            e9ui_textbox_setOptions(select, st->asmSymbolOptions, st->asmSymbolCount);
        }
        select->disabled = st->asmSymbolCount <= 0 ? 1 : 0;
        return;
    }

    const char *elf = debugger.libretro.exePath;
    const char *toolchain = debugger.libretro.toolchainPrefix;
    uint64_t textMapRevision = 0;
    if (debugger.symbolFileKind == DEBUGGER_SYMBOL_FILE_KIND_TEXT_MAP) {
        textMapRevision = symbol_text_map_revision();
    }
    if (!elf || !elf[0] || !debugger.symbolValid) {
        e9ui_textbox_setOptions(select, NULL, 0);
        source_pane_symbols_clearAsmSymbols(st);
        select->disabled = 1;
        return;
    }
    if (st->asmSymbolsLoaded &&
        strcmp(st->asmSymbolsElf, elf) == 0 &&
        strcmp(st->asmSymbolsToolchain, toolchain ? toolchain : "") == 0 &&
        st->asmSymbolsTextMapRevision == textMapRevision) {
        if (!editingSelect) {
            e9ui_textbox_setOptions(select, st->asmSymbolOptions, st->asmSymbolCount);
        }
        select->disabled = st->asmSymbolCount <= 0 ? 1 : 0;
        return;
    }

    e9ui_textbox_setOptions(select, NULL, 0);
    source_pane_symbols_clearAsmSymbols(st);
    (void)source_pane_symbols_collectAsmSymbols(st, elf);
    st->asmSymbolsLoaded = 1;
    st->asmSymbolsTextMapRevision = textMapRevision;
    strncpy(st->asmSymbolsElf, elf, sizeof(st->asmSymbolsElf) - 1);
    st->asmSymbolsElf[sizeof(st->asmSymbolsElf) - 1] = '\0';
    if (toolchain && toolchain[0]) {
        strncpy(st->asmSymbolsToolchain, toolchain, sizeof(st->asmSymbolsToolchain) - 1);
        st->asmSymbolsToolchain[sizeof(st->asmSymbolsToolchain) - 1] = '\0';
    } else {
        st->asmSymbolsToolchain[0] = '\0';
    }
    if (!editingSelect) {
        e9ui_textbox_setOptions(select, st->asmSymbolOptions, st->asmSymbolCount);
    }
    select->disabled = st->asmSymbolCount <= 0 ? 1 : 0;
}

static int
source_pane_symbols_collectFunctionSymbols(source_pane_state_t *st, const char *elf_path,
                                           const char *source_file)
{
    if (!st || !elf_path || !elf_path[0] || !debugger.elfValid) {
        return 0;
    }
    if (debugger_toolchainUsesHunkAddr2line()) {
        return 0;
    }

    char readelf[PATH_MAX];
    if (!debugger_toolchainBuildBinary(readelf, sizeof(readelf), "readelf")) {
        return 0;
    }
    char readelfExe[PATH_MAX];
    if (!file_findInPath(readelf, readelfExe, sizeof(readelfExe))) {
        return 0;
    }

    char cmd[PATH_MAX * 2];
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), readelfExe, "-Ws", elf_path, 1)) {
        return 0;
    }
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return 0;
    }

    int added = 0;
    char line[2048];
    char resolvedSource[PATH_MAX];
    if (source_file && source_file[0]) {
        source_pane_resolveSourcePath(source_file, resolvedSource, sizeof(resolvedSource));
    } else {
        resolvedSource[0] = '\0';
    }
    if (!addr2line_start(elf_path)) {
        pclose(fp);
        return 0;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *tokens[12];
        int count = 0;
        char *cursor = line;
        while (*cursor && isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (!*cursor || !isdigit((unsigned char)*cursor)) {
            continue;
        }
        while (count < (int)(sizeof(tokens) / sizeof(tokens[0]))) {
            while (*cursor && isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (!*cursor) {
                break;
            }
            tokens[count++] = cursor;
            while (*cursor && !isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (*cursor) {
                *cursor++ = '\0';
            }
        }
        if (count < 8) {
            continue;
        }
        if (strcmp(tokens[3], "FUNC") != 0 || strcmp(tokens[6], "UND") == 0) {
            continue;
        }
        const char *symbolName = tokens[7];
        if (!symbolName || !symbolName[0] || strcmp(symbolName, "<null>") == 0) {
            continue;
        }
        uint64_t symbolAddr = 0;
        if (!source_pane_parseHex64(tokens[1], &symbolAddr) || symbolAddr == 0) {
            continue;
        }

        char resolvedFile[PATH_MAX];
        char functionName[1024];
        int functionLine = 0;
        if (!addr2line_resolveDetailed(symbolAddr, resolvedFile, sizeof(resolvedFile),
                                       &functionLine, functionName, sizeof(functionName))) {
            continue;
        }
        if (functionLine <= 0 || !resolvedFile[0]) {
            continue;
        }
        char resolvedPath[PATH_MAX];
        source_pane_resolveSourcePath(resolvedFile, resolvedPath, sizeof(resolvedPath));
        if (resolvedSource[0] && !source_pane_fileMatches(resolvedPath, resolvedSource)) {
            continue;
        }
        const char *displayName = functionName[0] ? functionName : symbolName;
        if (strcmp(displayName, "??") == 0) {
            continue;
        }
        added += source_pane_symbols_addSourceFunction(st, resolvedPath, displayName, functionLine);
    }

    pclose(fp);
    return added;
}

static int
source_pane_symbols_parseStabStringName(const char *stab_str, char *out_name, size_t cap)
{
    if (!stab_str || !stab_str[0] || !out_name || cap == 0) {
        return 0;
    }
    out_name[0] = '\0';
    const char *end = strchr(stab_str, ':');
    if (!end) {
        end = stab_str + strlen(stab_str);
    }
    size_t len = (size_t)(end - stab_str);
    if (len == 0) {
        return 0;
    }
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(out_name, stab_str, len);
    out_name[len] = '\0';
    return out_name[0] != '\0';
}

static int
source_pane_symbols_collectStabsFunctions(source_pane_state_t *st, const char *elf_path,
                                          const char *source_file)
{
    if (!st || !elf_path || !elf_path[0] || !debugger.elfValid) {
        return 0;
    }
    if (debugger_toolchainUsesHunkAddr2line()) {
        return 0;
    }
    char objdump[PATH_MAX];
    if (!debugger_toolchainBuildBinary(objdump, sizeof(objdump), "objdump")) {
        return 0;
    }
    char objdumpExe[PATH_MAX];
    if (!file_findInPath(objdump, objdumpExe, sizeof(objdumpExe))) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), objdumpExe, "-G", elf_path, 1)) {
        return 0;
    }
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return 0;
    }

    int added = 0;
    char line[2048];
    char resolvedSource[PATH_MAX];
    if (source_file && source_file[0]) {
        source_pane_resolveSourcePath(source_file, resolvedSource, sizeof(resolvedSource));
    } else {
        resolvedSource[0] = '\0';
    }
    if (!addr2line_start(elf_path)) {
        pclose(fp);
        return 0;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *tokens[12];
        int count = 0;
        char *cursor = line;
        while (*cursor && isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (!*cursor) {
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
        if (count < 7) {
            continue;
        }
        const char *stabType = tokens[1];
        const char *stabStr = tokens[count - 1];
        if (!stabType || !stabStr || strcmp(stabType, "FUN") != 0) {
            continue;
        }
        char parsedName[1024];
        if (!source_pane_symbols_parseStabStringName(stabStr, parsedName, sizeof(parsedName))) {
            continue;
        }

        uint64_t nValue = 0;
        if (!source_pane_parseHex64(tokens[4], &nValue) || nValue == 0) {
            continue;
        }
        char resolvedFile[PATH_MAX];
        char functionName[1024];
        int functionLine = 0;
        if (!addr2line_resolveDetailed(nValue, resolvedFile, sizeof(resolvedFile),
                                       &functionLine, functionName, sizeof(functionName))) {
            continue;
        }
        if (functionLine <= 0 || !resolvedFile[0]) {
            continue;
        }
        char resolvedPath[PATH_MAX];
        source_pane_resolveSourcePath(resolvedFile, resolvedPath, sizeof(resolvedPath));
        if (resolvedSource[0] && !source_pane_fileMatches(resolvedPath, resolvedSource)) {
            continue;
        }
        const char *displayName = functionName[0] ? functionName : parsedName;
        if (!displayName[0] || strcmp(displayName, "??") == 0) {
            continue;
        }
        added += source_pane_symbols_addSourceFunction(st, resolvedPath, displayName, functionLine);
    }

    pclose(fp);
    return added;
}

static int
source_pane_symbols_collectObjdumpTextFunctions(source_pane_state_t *st, const char *elf_path,
                                                const char *source_file)
{
    if (!st || !elf_path || !elf_path[0] || !debugger.elfValid) {
        return 0;
    }
    char objdump[PATH_MAX];
    if (!debugger_toolchainBuildBinary(objdump, sizeof(objdump), "objdump")) {
        return 0;
    }
    char objdumpExe[PATH_MAX];
    if (!file_findInPath(objdump, objdumpExe, sizeof(objdumpExe))) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), objdumpExe, "-t", elf_path, 1)) {
        return 0;
    }
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return 0;
    }

    int added = 0;
    char line[2048];
    char resolvedSource[PATH_MAX];
    if (source_file && source_file[0]) {
        source_pane_resolveSourcePath(source_file, resolvedSource, sizeof(resolvedSource));
    } else {
        resolvedSource[0] = '\0';
    }
    if (!addr2line_start(elf_path)) {
        pclose(fp);
        return 0;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *tokens[16];
        int count = 0;
        char *cursor = line;
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
        uint64_t symbolAddr = 0;
        if (!source_pane_parseHex64(tokens[0], &symbolAddr) || symbolAddr == 0) {
            continue;
        }
        int textSymbol = 0;
        for (int i = 1; i < count; ++i) {
            if (strcmp(tokens[i], ".text") == 0 || strncmp(tokens[i], ".text.", 6) == 0) {
                textSymbol = 1;
                break;
            }
        }
        if (!textSymbol) {
            continue;
        }
        const char *symbolName = tokens[count - 1];
        if (!symbolName || !symbolName[0] || symbolName[0] == '.') {
            continue;
        }

        char resolvedFile[PATH_MAX];
        char functionName[1024];
        int functionLine = 0;
        if (!addr2line_resolveDetailed(symbolAddr, resolvedFile, sizeof(resolvedFile),
                                       &functionLine, functionName, sizeof(functionName))) {
            continue;
        }
        if (functionLine <= 0 || !resolvedFile[0]) {
            continue;
        }
        char resolvedPath[PATH_MAX];
        source_pane_resolveSourcePath(resolvedFile, resolvedPath, sizeof(resolvedPath));
        if (resolvedSource[0] && !source_pane_fileMatches(resolvedPath, resolvedSource)) {
            continue;
        }
        const char *displayName = functionName[0] ? functionName : symbolName;
        if (!displayName[0] || strcmp(displayName, "??") == 0) {
            continue;
        }
        added += source_pane_symbols_addSourceFunction(st, resolvedPath, displayName, functionLine);
    }

    pclose(fp);
    return added;
}

static int
source_pane_symbols_collectObjdumpTextFiles(source_pane_state_t *st, const char *elf_path)
{
    if (!st || !elf_path || !elf_path[0] || !debugger.elfValid) {
        return 0;
    }
    char objdump[PATH_MAX];
    if (!debugger_toolchainBuildBinary(objdump, sizeof(objdump), "objdump")) {
        return 0;
    }
    char objdumpExe[PATH_MAX];
    if (!file_findInPath(objdump, objdumpExe, sizeof(objdumpExe))) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), objdumpExe, "-t", elf_path, 1)) {
        return 0;
    }
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return 0;
    }
    if (!addr2line_start(elf_path)) {
        pclose(fp);
        return 0;
    }

    int added = 0;
    char line[2048];
    while (fgets(line, sizeof(line), fp)) {
        char *tokens[16];
        int count = 0;
        char *cursor = line;
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
        uint64_t symbolAddr = 0;
        if (!source_pane_parseHex64(tokens[0], &symbolAddr) || symbolAddr == 0) {
            continue;
        }
        int textSymbol = 0;
        for (int i = 1; i < count; ++i) {
            if (strcmp(tokens[i], ".text") == 0 || strncmp(tokens[i], ".text.", 6) == 0) {
                textSymbol = 1;
                break;
            }
        }
        if (!textSymbol) {
            continue;
        }
        const char *symbolName = tokens[count - 1];
        if (!symbolName || !symbolName[0] || symbolName[0] == '.') {
            continue;
        }

        char resolvedFile[PATH_MAX];
        int functionLine = 0;
        if (!addr2line_resolve(symbolAddr, resolvedFile, sizeof(resolvedFile), &functionLine)) {
            continue;
        }
        if (functionLine <= 0 || !resolvedFile[0]) {
            continue;
        }
        char resolvedPath[PATH_MAX];
        source_pane_resolveSourcePath(resolvedFile, resolvedPath, sizeof(resolvedPath));
        added += source_pane_symbols_addSourceFile(st, resolvedPath);
    }

    pclose(fp);
    return added;
}

static int
source_pane_symbols_collectZ80SourceFunctions(source_pane_state_t *st, const char *sourceFile)
{
    if (!st) {
        return 0;
    }

    char resolvedSourceFile[PATH_MAX];
    resolvedSourceFile[0] = '\0';
    if (sourceFile && sourceFile[0]) {
        source_pane_resolveSourcePath(sourceFile, resolvedSourceFile, sizeof(resolvedSourceFile));
    }

    int added = 0;
    int count = source_z80_getSymbolCount();
    for (int i = 0; i < count; ++i) {
        const char *name = NULL;
        uint16_t addr = 0;
        if (!source_z80_getSymbol(i, &name, &addr) || !name || !name[0]) {
            continue;
        }
        char path[PATH_MAX];
        int line = 0;
        if (!source_z80_resolveSourceLocation(addr, path, sizeof(path), &line)) {
            continue;
        }
        char resolvedPath[PATH_MAX];
        source_pane_resolveSourcePath(path, resolvedPath, sizeof(resolvedPath));
        if (resolvedSourceFile[0] && !source_pane_fileMatches(resolvedSourceFile, resolvedPath)) {
            continue;
        }
        added += source_pane_symbols_addSourceFunction(st, resolvedPath, name, line);
    }
    return added;
}

void
source_pane_symbols_refreshSourceFunctions(e9ui_component_t *comp, source_pane_state_t *st,
                                           const char *source_file)
{
    if (!comp || !st) {
        return;
    }
    e9ui_component_t *select = NULL;
    if (st->functionSelectMeta) {
        select = e9ui_child_find(comp, st->functionSelectMeta);
    }
    const char *elf = debugger.libretro.exePath;
    const char *toolchain = debugger.libretro.toolchainPrefix;
    char z80Key[PATH_MAX];
    char z80ToolchainKey[64];
    z80Key[0] = '\0';
    z80ToolchainKey[0] = '\0';
    if (st->viewMode == source_pane_mode_z80s) {
        source_z80_copySymbolBaseDir(z80Key, sizeof(z80Key));
        elf = z80Key;
        source_pane_symbols_copyZ80SourceToolchainKey(z80ToolchainKey, sizeof(z80ToolchainKey));
        toolchain = z80ToolchainKey;
    }
    char resolvedSourceFile[PATH_MAX];
    resolvedSourceFile[0] = '\0';
    if (source_file && source_file[0]) {
        source_pane_resolveSourcePath(source_file, resolvedSourceFile, sizeof(resolvedSourceFile));
    }
    if ((st->viewMode == source_pane_mode_c && !debugger.elfValid) || !elf || !elf[0]) {
        if (select) {
            e9ui_textbox_setOptions(select, NULL, 0);
        }
        source_pane_symbols_clearSourceFunctions(st);
        source_pane_symbols_syncFunctionSelect(comp, st);
        return;
    }

    if (st->sourceFunctionsLoaded &&
        strcmp(st->sourceFunctionsElf, elf) == 0 &&
        strcmp(st->sourceFunctionsToolchain, toolchain ? toolchain : "") == 0 &&
        strcmp(st->sourceFunctionsFile, resolvedSourceFile) == 0) {
        source_pane_symbols_syncFunctionSelect(comp, st);
        return;
    }

    if (select) {
        e9ui_textbox_setOptions(select, NULL, 0);
    }
    source_pane_symbols_clearSourceFunctions(st);
    int added = 0;
    if (st->viewMode == source_pane_mode_z80s) {
        added += source_pane_symbols_collectZ80SourceFunctions(st, source_file);
    } else {
        added = source_pane_symbols_collectFunctionSymbols(st, elf, source_file);
        if (added == 0) {
            added += source_pane_symbols_collectStabsFunctions(st, elf, source_file);
        }
        if (added == 0) {
            added += source_pane_symbols_collectObjdumpTextFunctions(st, elf, source_file);
        }
    }
    st->sourceFunctionsLoaded = 1;
    strncpy(st->sourceFunctionsElf, elf, sizeof(st->sourceFunctionsElf) - 1);
    st->sourceFunctionsElf[sizeof(st->sourceFunctionsElf) - 1] = '\0';
    if (toolchain) {
        strncpy(st->sourceFunctionsToolchain, toolchain, sizeof(st->sourceFunctionsToolchain) - 1);
        st->sourceFunctionsToolchain[sizeof(st->sourceFunctionsToolchain) - 1] = '\0';
    } else {
        st->sourceFunctionsToolchain[0] = '\0';
    }
    if (resolvedSourceFile[0]) {
        strncpy(st->sourceFunctionsFile, resolvedSourceFile, sizeof(st->sourceFunctionsFile) - 1);
        st->sourceFunctionsFile[sizeof(st->sourceFunctionsFile) - 1] = '\0';
    } else {
        st->sourceFunctionsFile[0] = '\0';
    }
    source_pane_symbols_syncFunctionSelect(comp, st);
}

void
source_pane_symbols_refreshSourceFiles(e9ui_component_t *comp, source_pane_state_t *st)
{
    if (!comp || !st) {
        return;
    }
    const char *elf = debugger.libretro.exePath;
    const char *toolchain = debugger.libretro.toolchainPrefix;
    char z80Key[PATH_MAX];
    char z80ToolchainKey[64];
    z80Key[0] = '\0';
    z80ToolchainKey[0] = '\0';
    if (st->viewMode == source_pane_mode_z80s) {
        source_z80_copySymbolBaseDir(z80Key, sizeof(z80Key));
        elf = z80Key;
        source_pane_symbols_copyZ80SourceToolchainKey(z80ToolchainKey, sizeof(z80ToolchainKey));
        toolchain = z80ToolchainKey;
    }
    e9ui_component_t *select = NULL;
    if (st->fileSelectMeta) {
        select = e9ui_child_find(comp, st->fileSelectMeta);
    }
    if ((st->viewMode == source_pane_mode_c && !debugger.elfValid) || !elf || !elf[0]) {
        if (select) {
            e9ui_textbox_setOptions(select, NULL, 0);
        }
        source_pane_symbols_clearSourceFiles(st);
        source_pane_symbols_syncFileSelect(comp, st);
        return;
    }
    if (st->sourceFilesLoaded &&
        strcmp(st->sourceFilesElf, elf) == 0 &&
        strcmp(st->sourceFilesToolchain, toolchain ? toolchain : "") == 0) {
        source_pane_symbols_syncFileSelect(comp, st);
        return;
    }

    if (select) {
        e9ui_textbox_setOptions(select, NULL, 0);
    }
    source_pane_symbols_clearSourceFiles(st);
    if (st->viewMode == source_pane_mode_z80s) {
        (void)source_pane_symbols_collectZ80SourceFiles(st);
    } else {
        (void)source_pane_symbols_collectReadelfFiles(st, elf);
        (void)source_pane_symbols_collectStabsFiles(st, elf);
        if (st->sourceFileCount <= 0 && debugger_toolchainUsesHunkAddr2line()) {
            (void)source_pane_symbols_collectObjdumpTextFiles(st, elf);
        }
    }
    if (st->sourceFileCount <= 0 && st->viewMode == source_pane_mode_c) {
        debug_error("source_pane: no source files collected (elf='%s', sourceDir='%s', toolchain='%s')",
                    elf,
                    debugger.libretro.sourceDir,
                    debugger.libretro.toolchainPrefix);
    }
    source_pane_symbols_prependBlankSourceOption(st);
    st->sourceFilesLoaded = 1;
    strncpy(st->sourceFilesElf, elf, sizeof(st->sourceFilesElf) - 1);
    st->sourceFilesElf[sizeof(st->sourceFilesElf) - 1] = '\0';
    if (toolchain) {
        strncpy(st->sourceFilesToolchain, toolchain, sizeof(st->sourceFilesToolchain) - 1);
        st->sourceFilesToolchain[sizeof(st->sourceFilesToolchain) - 1] = '\0';
    } else {
        st->sourceFilesToolchain[0] = '\0';
    }
    source_pane_symbols_syncFileSelect(comp, st);
}
