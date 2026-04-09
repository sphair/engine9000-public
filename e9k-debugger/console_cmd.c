/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <errno.h>

#include "console_cmd.h"
#include "debugger.h"
#include "config.h"
#include "alloc.h"
#include "breakpoints.h"
#include "libretro_host.h"
#include "transition.h"
#include "machine.h"
#include "base_map.h"
#include "print_eval.h"
#include "train.h"
#include "state_buffer.h"
#include "protect.h"
#include "hunk_fileline_cache.h"
#include "symbol_text_map.h"


typedef int (*console_cmd_handler_t)(int argc, char **argv);
typedef int (*console_cmd_complete_t)(const char *prefix, char ***out_list, int *out_count);

typedef struct console_cmd_entry {
    const char *name;
    const char *shortcut;
    const char *usage;
    const char *help;
    console_cmd_handler_t handler;
    console_cmd_complete_t complete;
} console_cmd_entry_t;

static int console_cmd_help(int argc, char **argv);
static int console_cmd_base(int argc, char **argv);
static int console_cmd_break(int argc, char **argv);
static int console_cmd_completeBreak(const char *prefix, char ***out_list, int *out_count);
static int console_cmd_continue(int argc, char **argv);
static int console_cmd_cls(int argc, char **argv);
static int console_cmd_step(int argc, char **argv);
static int console_cmd_stepi(int argc, char **argv);
static int console_cmd_next(int argc, char **argv);
static int console_cmd_finish(int argc, char **argv);
static int console_cmd_write(int argc, char **argv);
static int console_cmd_completeWrite(const char *prefix, char ***outList, int *outCount);
static int console_cmd_transition(int argc, char **argv);
static int console_cmd_completeTransition(const char *prefix, char ***out_list, int *out_count);
static int console_cmd_print(int argc, char **argv);
static int console_cmd_completePrint(const char *prefix, char ***outList, int *outCount);
static int console_cmd_watch(int argc, char **argv);
static int console_cmd_train(int argc, char **argv);
static int console_cmd_loop(int argc, char **argv);
static int console_cmd_protect(int argc, char **argv);
static int console_cmd_diff(int argc, char **argv);
static int console_cmd_symbols(int argc, char **argv);
static int console_cmd_parseHex(const char *s, uint32_t *out);

static const console_cmd_entry_t console_cmd[] = {
    { "help",  "h", "help [command]", "Show available commands or detailed help.", console_cmd_help, NULL },
    { "base",  NULL, "base [text|data|bss] [addr|clear]", "Set or show section base addresses (subtracted from addresses passed to toolchain tools).", console_cmd_base, NULL },
    { "break", "b", "break <addr|symbol|file:line>", "Set a breakpoint at an address, symbol, or file:line.", console_cmd_break, console_cmd_completeBreak },    
    { "cls",  NULL, "cls", "Clear the console output.", console_cmd_cls, NULL },    
    { "continue", "c", "continue", "Continue execution and defocus the prompt.", console_cmd_continue, NULL },
    { "diff", NULL, "diff <fromFrame> <toFrame> [size=8|16|32]", "Show RAM addresses that differ between two recorded frames.", console_cmd_diff, NULL },    
    { "loop", NULL, "loop <from> <to>\nloop\nloop clear", "Loop between two recorded frame numbers (decimal).", console_cmd_loop, NULL },
    { "print", "p", "print <expr> [size=8|16|32]\nprint addr <expr>", "Print an expression using DWARF + symbol info. Use size=8|16|32 to force a memory read size, or 'print addr <expr>' to show the resolved runtime address.", console_cmd_print, console_cmd_completePrint },   
    { "protect", NULL, "protect\nprotect clear\nprotect del <addr> [size=8|16|32]\nprotect <addr> block [size=8|16|32]\nprotect <addr> set=0x...|$... [size=8|16|32]", "Protect addresses by blocking writes or forcing a value (core-side).", console_cmd_protect, NULL },    
    { "next", "n", "next", "Step over the next line.", console_cmd_next, NULL },    
    { "finish", "f", "finish", "Step out of the current function.", console_cmd_finish, NULL },
    { "step", "s", "step", "Step to next source line.", console_cmd_step, NULL },
    { "stepi", "i", "stepi", "Step one instruction.", console_cmd_stepi, NULL },
    { "symbols", "y", "symbols export <path>\nsymbols load <path>\nsymbols save\nsymbols list\nsymbols show <symbol>\nsymbols lookup <addr>\nsymbols add <symbol> <addr> <function|variable|unknown>\nsymbols delete <symbol>\nsymbols reset", "Export, load, and edit text-map symbols.", console_cmd_symbols, NULL },
    { "train", NULL, "train <from> <to> [size=8|16|32]\ntrain ignore\ntrain clear", "Train by breaking on a value transition (from/to accept decimal or 0x... or $...).", console_cmd_train, NULL },    
    { "transition", NULL, "transition <slide|explode|doom|flip|rbar|random|cycle|none>", "Set the transition mode for startup and fullscreen.", console_cmd_transition, console_cmd_completeTransition },
    { "watch", "wa", "watch [addr|symbol] [r|w|rw] [size=8|16|32] [mask=0x...|$...] [val=0x...|$...] [old=0x...|$...] [diff=0x...|$...]\nwatch del <idx> \nwatch clear", "Set or list watchpoints.", console_cmd_watch, NULL },    
    { "write", NULL, "write <dest> <value>", "Write a hex value to an address or symbol.", console_cmd_write, console_cmd_completeWrite },

};

typedef struct console_completion {
    char **items;
    int count;
    int cap;
} console_completion_t;

typedef struct console_export_symbol {
    char *name;
    uint32_t addr;
    symbol_text_map_symbol_kind_t kind;
} console_export_symbol_t;

static int
console_cmd_exportSymbolKindPriority(symbol_text_map_symbol_kind_t kind)
{
    switch (kind) {
    case SYMBOL_TEXT_MAP_SYMBOL_KIND_FUNCTION:
        return 0;
    case SYMBOL_TEXT_MAP_SYMBOL_KIND_VARIABLE:
        return 1;
    case SYMBOL_TEXT_MAP_SYMBOL_KIND_UNKNOWN:
    default:
        return 2;
    }
}

static const char *
console_cmd_exportSymbolKindName(symbol_text_map_symbol_kind_t kind)
{
    switch (kind) {
    case SYMBOL_TEXT_MAP_SYMBOL_KIND_FUNCTION:
        return "function";
    case SYMBOL_TEXT_MAP_SYMBOL_KIND_VARIABLE:
        return "variable";
    case SYMBOL_TEXT_MAP_SYMBOL_KIND_UNKNOWN:
    default:
        return "unknown";
    }
}

static void
console_cmd_symbolsPrintUsage(void)
{
    debug_printf("Usage: symbols export <path>\n");
    debug_printf("       symbols load <path>\n");
    debug_printf("       symbols save\n");
    debug_printf("       symbols list\n");
    debug_printf("       symbols show <symbol>\n");
    debug_printf("       symbols lookup <addr>\n");
    debug_printf("       symbols add <symbol> <addr> <function|variable|unknown>\n");
    debug_printf("       symbols delete <symbol>\n");
    debug_printf("       symbols reset\n");
}

static int
console_cmd_symbolsParseKind(const char *text, symbol_text_map_symbol_kind_t *outKind)
{
    if (outKind) {
        *outKind = SYMBOL_TEXT_MAP_SYMBOL_KIND_UNKNOWN;
    }
    if (!text || !*text || !outKind) {
        return 0;
    }
    if (strcasecmp(text, "f") == 0 ||
        strcasecmp(text, "func") == 0 ||
        strcasecmp(text, "function") == 0) {
        *outKind = SYMBOL_TEXT_MAP_SYMBOL_KIND_FUNCTION;
        return 1;
    }
    if (strcasecmp(text, "v") == 0 ||
        strcasecmp(text, "var") == 0 ||
        strcasecmp(text, "variable") == 0) {
        *outKind = SYMBOL_TEXT_MAP_SYMBOL_KIND_VARIABLE;
        return 1;
    }
    if (strcasecmp(text, "u") == 0 ||
        strcasecmp(text, "unk") == 0 ||
        strcasecmp(text, "unknown") == 0) {
        *outKind = SYMBOL_TEXT_MAP_SYMBOL_KIND_UNKNOWN;
        return 1;
    }
    return 0;
}

static void
console_cmd_symbolsInvalidateCaches(void)
{
    print_eval_invalidateCache();
    debugger_refreshElfValid();
}

static int
console_cmd_symbolsSetActivePath(const char *path)
{
    if (!path || !path[0]) {
        return 0;
    }

    if (target == target_amiga()) {
        debugger_copyPath(debugger.config.amiga.libretro.exePath,
                          sizeof(debugger.config.amiga.libretro.exePath),
                          path);
    } else if (target == target_neogeo()) {
        debugger_copyPath(debugger.config.neogeo.libretro.exePath,
                          sizeof(debugger.config.neogeo.libretro.exePath),
                          path);
    } else if (target == target_megadrive()) {
        debugger_copyPath(debugger.config.megadrive.libretro.exePath,
                          sizeof(debugger.config.megadrive.libretro.exePath),
                          path);
    } else {
        return 0;
    }

    debugger_copyPath(debugger.libretro.exePath, sizeof(debugger.libretro.exePath), path);
    config_saveConfig();
    return 1;
}

static int
console_cmd_symbolsGetEditablePath(const char **outPath)
{
    const char *path = debugger.libretro.exePath;
    if (outPath) {
        *outPath = NULL;
    }
    if (!path || !path[0]) {
        return 0;
    }
    if (!symbol_text_map_hasActive() && !symbol_text_map_canLoad(path)) {
        return 0;
    }
    if (outPath) {
        *outPath = path;
    }
    return 1;
}

static int
console_cmd_symbolsPrintEntry(const symbol_text_map_entry_t *entry)
{
    if (!entry || !entry->name || !entry->name[0]) {
        return 0;
    }
    debug_printf("0x%X %s %s\n",
                 (unsigned)(entry->addr & 0x00ffffffu),
                 entry->name,
                 console_cmd_exportSymbolKindName(entry->kind));
    return 1;
}

static symbol_text_map_symbol_kind_t
console_cmd_exportSymbolKindFromTokens(char **tokens, int count)
{
    if (!tokens || count <= 0) {
        return SYMBOL_TEXT_MAP_SYMBOL_KIND_UNKNOWN;
    }
    for (int i = 1; i < count - 1; ++i) {
        const char *token = tokens[i];
        if (!token || !token[0]) {
            continue;
        }
        if (strcmp(token, "F") == 0 || strcmp(token, "FUNC") == 0) {
            return SYMBOL_TEXT_MAP_SYMBOL_KIND_FUNCTION;
        }
        if (strcmp(token, "O") == 0 || strcmp(token, "OBJECT") == 0) {
            return SYMBOL_TEXT_MAP_SYMBOL_KIND_VARIABLE;
        }
    }
    return SYMBOL_TEXT_MAP_SYMBOL_KIND_UNKNOWN;
}

static void
console_cmd_exportSymbolsFree(console_export_symbol_t *symbols, int count)
{
    if (!symbols) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        alloc_free(symbols[i].name);
    }
    alloc_free(symbols);
}

static int
console_cmd_exportSymbolsAdd(console_export_symbol_t **symbols,
                             int *count,
                             int *cap,
                             const char *name,
                             uint32_t addr,
                             symbol_text_map_symbol_kind_t kind)
{
    if (!symbols || !count || !cap || !name || !name[0]) {
        return 0;
    }
    for (int i = 0; i < *count; ++i) {
        console_export_symbol_t *existing = &(*symbols)[i];
        if (existing->addr == (addr & 0x00ffffffu) &&
            strcmp(existing->name, name) == 0) {
            if (console_cmd_exportSymbolKindPriority(kind) <
                console_cmd_exportSymbolKindPriority(existing->kind)) {
                existing->kind = kind;
            }
            return 1;
        }
    }
    if (*count >= *cap) {
        int nextCap = *cap ? (*cap * 2) : 256;
        console_export_symbol_t *nextSymbols =
            (console_export_symbol_t *)alloc_realloc(*symbols, sizeof(*nextSymbols) * (size_t)nextCap);
        if (!nextSymbols) {
            return 0;
        }
        *symbols = nextSymbols;
        *cap = nextCap;
    }
    char *nameDup = alloc_strdup(name);
    if (!nameDup) {
        return 0;
    }
    console_export_symbol_t *entry = &(*symbols)[(*count)++];
    entry->name = nameDup;
    entry->addr = addr & 0x00ffffffu;
    entry->kind = kind;
    return 1;
}

static int
console_cmd_exportSymbolsCompare(const void *a, const void *b)
{
    const console_export_symbol_t *sa = (const console_export_symbol_t *)a;
    const console_export_symbol_t *sb = (const console_export_symbol_t *)b;
    if (sa->addr < sb->addr) {
        return -1;
    }
    if (sa->addr > sb->addr) {
        return 1;
    }
    int nameCmp = strcmp(sa->name ? sa->name : "", sb->name ? sb->name : "");
    if (nameCmp != 0) {
        return nameCmp;
    }
    return console_cmd_exportSymbolKindPriority(sa->kind) -
           console_cmd_exportSymbolKindPriority(sb->kind);
}

static int
console_cmd_exportSymbolsFromTextMap(const char *path,
                                     console_export_symbol_t **outSymbols,
                                     int *outCount)
{
    if (outSymbols) {
        *outSymbols = NULL;
    }
    if (outCount) {
        *outCount = 0;
    }
    if (!path || !*path || !outSymbols || !outCount) {
        return 0;
    }
    const symbol_text_map_entry_t *entries = NULL;
    int entryCount = 0;
    if (!symbol_text_map_getEntries(path, &entries, &entryCount) || !entries || entryCount <= 0) {
        return 0;
    }
    console_export_symbol_t *symbols = NULL;
    int count = 0;
    int cap = 0;
    for (int i = 0; i < entryCount; ++i) {
        const symbol_text_map_entry_t *entry = &entries[i];
        if (!console_cmd_exportSymbolsAdd(&symbols, &count, &cap,
                                          entry->name, entry->addr, entry->kind)) {
            console_cmd_exportSymbolsFree(symbols, count);
            return 0;
        }
    }
    *outSymbols = symbols;
    *outCount = count;
    return count > 0 ? 1 : 0;
}

static int
console_cmd_exportSymbolsFromBinary(const char *path,
                                    console_export_symbol_t **outSymbols,
                                    int *outCount)
{
    if (outSymbols) {
        *outSymbols = NULL;
    }
    if (outCount) {
        *outCount = 0;
    }
    if (!path || !*path || !outSymbols || !outCount) {
        return 0;
    }
    char objdump[PATH_MAX];
    if (!debugger_toolchainBuildBinary(objdump, sizeof(objdump), "objdump")) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), objdump, "--syms", path, 0)) {
        return 0;
    }
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return 0;
    }

    console_export_symbol_t *symbols = NULL;
    int count = 0;
    int cap = 0;
    char line[2048];
    while (fgets(line, sizeof(line), fp)) {
        char *tokens[16];
        int tokenCount = 0;
        char *cursor = line;
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
        if (tokenCount < 2) {
            continue;
        }
        uint32_t addr = 0;
        if (!console_cmd_parseHex(tokens[0], &addr)) {
            continue;
        }
        const char *section = NULL;
        for (int i = 1; i < tokenCount - 1; ++i) {
            const char *token = tokens[i];
            if (!token || !token[0]) {
                continue;
            }
            if (token[0] == '.' || token[0] == '*'
                || strcmp(token, "UND") == 0) {
                section = token;
                break;
            }
        }
        const char *name = tokens[tokenCount - 1];
        if (!name || !name[0]) {
            continue;
        }
        symbol_text_map_symbol_kind_t kind = console_cmd_exportSymbolKindFromTokens(tokens, tokenCount);
        uint32_t runtimeAddr = addr;
        if (debugger_toolchainUsesHunkAddr2line()) {
            (void)base_map_symbolToRuntimeHunk(section, addr, &runtimeAddr);
        } else {
            (void)base_map_symbolToRuntime(section, addr, &runtimeAddr);
        }
        if (!console_cmd_exportSymbolsAdd(&symbols, &count, &cap, name, runtimeAddr, kind)) {
            pclose(fp);
            console_cmd_exportSymbolsFree(symbols, count);
            return 0;
        }
    }
    pclose(fp);
    *outSymbols = symbols;
    *outCount = count;
    return count > 0 ? 1 : 0;
}

static int
console_cmd_exportSymbolsSupplementReadelfFunctions(const char *path,
                                                    console_export_symbol_t **ioSymbols,
                                                    int *ioCount,
                                                    int *ioCap)
{
    if (!path || !*path || !ioSymbols || !ioCount || !ioCap) {
        return 0;
    }
    char readelf[PATH_MAX];
    if (!debugger_toolchainBuildBinary(readelf, sizeof(readelf), "readelf")) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), readelf, "-Ws", path, 1)) {
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
        int tokenCount = 0;
        char *cursor = line;
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
        if (tokenCount < 8) {
            continue;
        }
        if (strcmp(tokens[3], "FUNC") != 0 || strcmp(tokens[6], "UND") == 0) {
            continue;
        }
        uint32_t addr = 0;
        if (!console_cmd_parseHex(tokens[1], &addr)) {
            continue;
        }
        const char *name = tokens[7];
        if (!name || !name[0]) {
            continue;
        }
        uint32_t runtimeAddr = addr;
        if (debugger_toolchainUsesHunkAddr2line()) {
            (void)base_map_symbolToRuntimeHunk(tokens[6], addr, &runtimeAddr);
        } else {
            (void)base_map_symbolToRuntime(tokens[6], addr, &runtimeAddr);
        }
        if (console_cmd_exportSymbolsAdd(ioSymbols,
                                         ioCount,
                                         ioCap,
                                         name,
                                         runtimeAddr,
                                         SYMBOL_TEXT_MAP_SYMBOL_KIND_FUNCTION)) {
            added++;
        }
    }
    pclose(fp);
    return added;
}

static int
console_cmd_exportSymbolsSupplementObjdumpTextFunctions(const char *path,
                                                        console_export_symbol_t **ioSymbols,
                                                        int *ioCount,
                                                        int *ioCap)
{
    if (!path || !*path || !ioSymbols || !ioCount || !ioCap) {
        return 0;
    }
    char objdump[PATH_MAX];
    if (!debugger_toolchainBuildBinary(objdump, sizeof(objdump), "objdump")) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), objdump, "-t", path, 1)) {
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
        int tokenCount = 0;
        char *cursor = line;
        while (*cursor && isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (!*cursor || !isxdigit((unsigned char)*cursor)) {
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
        if (tokenCount < 4) {
            continue;
        }
        uint32_t addr = 0;
        if (!console_cmd_parseHex(tokens[0], &addr)) {
            continue;
        }
        int textSymbol = 0;
        for (int i = 1; i < tokenCount; ++i) {
            if (strcmp(tokens[i], ".text") == 0 || strncmp(tokens[i], ".text.", 6) == 0) {
                textSymbol = 1;
                break;
            }
        }
        if (!textSymbol) {
            continue;
        }
        const char *name = tokens[tokenCount - 1];
        if (!name || !name[0]) {
            continue;
        }
        uint32_t runtimeAddr = addr;
        if (debugger_toolchainUsesHunkAddr2line()) {
            (void)base_map_symbolToRuntimeHunk(".text", addr, &runtimeAddr);
        } else {
            (void)base_map_symbolToRuntime(".text", addr, &runtimeAddr);
        }
        if (console_cmd_exportSymbolsAdd(ioSymbols,
                                         ioCount,
                                         ioCap,
                                         name,
                                         runtimeAddr,
                                         SYMBOL_TEXT_MAP_SYMBOL_KIND_FUNCTION)) {
            added++;
        }
    }
    pclose(fp);
    return added;
}

static int
console_cmd_exportSymbolsSupplementStabsFunctions(const char *path,
                                                  console_export_symbol_t **ioSymbols,
                                                  int *ioCount,
                                                  int *ioCap)
{
    if (!path || !*path || !ioSymbols || !ioCount || !ioCap) {
        return 0;
    }
    char objdump[PATH_MAX];
    if (!debugger_toolchainBuildBinary(objdump, sizeof(objdump), "objdump")) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), objdump, "-G", path, 1)) {
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
        int tokenCount = 0;
        char *cursor = line;
        while (*cursor && isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (!*cursor) {
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
        if (tokenCount < 7) {
            continue;
        }
        if (strcmp(tokens[1], "FUN") != 0) {
            continue;
        }
        uint32_t addr = 0;
        if (!console_cmd_parseHex(tokens[4], &addr) || addr == 0) {
            continue;
        }
        const char *stab = tokens[tokenCount - 1];
        if (!stab || !stab[0]) {
            continue;
        }
        const char *end = strchr(stab, ':');
        size_t len = end ? (size_t)(end - stab) : strlen(stab);
        if (len == 0 || len >= 1024) {
            continue;
        }
        char name[1024];
        memcpy(name, stab, len);
        name[len] = '\0';
        if (!name[0]) {
            continue;
        }
        uint32_t runtimeAddr = addr;
        if (debugger_toolchainUsesHunkAddr2line()) {
            (void)base_map_symbolToRuntimeHunk(".text", addr, &runtimeAddr);
        } else {
            (void)base_map_symbolToRuntime(".text", addr, &runtimeAddr);
        }
        if (console_cmd_exportSymbolsAdd(ioSymbols,
                                         ioCount,
                                         ioCap,
                                         name,
                                         runtimeAddr,
                                         SYMBOL_TEXT_MAP_SYMBOL_KIND_FUNCTION)) {
            added++;
        }
    }
    pclose(fp);
    return added;
}

static int
console_cmd_exportSymbolsSupplementFromPrintEval(console_export_symbol_t **ioSymbols,
                                                 int *ioCount,
                                                 int *ioCap)
{
    if (!ioSymbols || !ioCount || !ioCap) {
        return 0;
    }
    char **names = NULL;
    int nameCount = 0;
    if (!print_eval_complete("", &names, &nameCount) || !names || nameCount <= 0) {
        return 0;
    }
    int added = 0;
    for (int i = 0; i < nameCount; ++i) {
        const char *name = names[i];
        uint32_t addr = 0;
        size_t size = 0;
        int isVariable = 0;
        if (!name || !name[0]) {
            continue;
        }
        if (!print_eval_resolveSymbol(name, &addr, &size)) {
            continue;
        }
        if (addr == 0) {
            continue;
        }
        symbol_text_map_symbol_kind_t kind = SYMBOL_TEXT_MAP_SYMBOL_KIND_UNKNOWN;
        if (print_eval_resolveNamedKind(name, &isVariable)) {
            kind = isVariable ? SYMBOL_TEXT_MAP_SYMBOL_KIND_VARIABLE :
                                SYMBOL_TEXT_MAP_SYMBOL_KIND_FUNCTION;
        }
        if (console_cmd_exportSymbolsAdd(ioSymbols,
                                         ioCount,
                                         ioCap,
                                         name,
                                         addr,
                                         kind)) {
            added++;
        }
        (void)size;
    }
    print_eval_freeCompletions(names, nameCount);
    return added;
}

static int
console_cmd_exportSymbolsWriteFile(const char *outPath, console_export_symbol_t *symbols, int count)
{
    if (!outPath || !*outPath || !symbols || count <= 0) {
        return 0;
    }
    qsort(symbols, (size_t)count, sizeof(*symbols), console_cmd_exportSymbolsCompare);
    FILE *fp = fopen(outPath, "w");
    if (!fp) {
        return 0;
    }
    for (int i = 0; i < count; ++i) {
        const console_export_symbol_t *entry = &symbols[i];
        if (!entry->name || !entry->name[0]) {
            continue;
        }
        fprintf(fp, "0x%06X %s %s\n",
                (unsigned)(entry->addr & 0x00ffffffu),
                entry->name,
                console_cmd_exportSymbolKindName(entry->kind));
    }
    fclose(fp);
    return 1;
}

static void
console_cmd_completionAdd(console_completion_t *list, const char *s)
{
    if (!list || !s || !*s) {
        return;
    }
    if (list->count >= list->cap) {
        int next = list->cap ? list->cap * 2 : 32;
        char **tmp = (char**)alloc_realloc(list->items, (size_t)next * sizeof(char*));
        if (!tmp) {
            return;
        }
        list->items = tmp;
        list->cap = next;
    }
    size_t len = strlen(s);
    char *dup = (char*)alloc_alloc(len + 1);
    if (!dup) {
        return;
    }
    memcpy(dup, s, len + 1);
    list->items[list->count++] = dup;
}

static int
console_cmd_tokenize(char *buf, char **argv, int cap)
{
    int argc = 0;
    char *p = buf;
    while (p && *p) {
        while (*p && isspace((unsigned char)*p)) {
            ++p;
        }
        if (!*p) {
            break;
        }
        if (argc < cap) {
            argv[argc++] = p;
        }
        while (*p && !isspace((unsigned char)*p)) {
            ++p;
        }
        if (*p) {
            *p++ = '\0';
        }
    }
    return argc;
}

static const console_cmd_entry_t *
console_cmd_find(const char *name)
{
    if (!name || !*name) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(console_cmd) / sizeof(console_cmd[0]); ++i) {
        const console_cmd_entry_t *cmd = &console_cmd[i];
        if ((cmd->name && strcasecmp(cmd->name, name) == 0) ||
            (cmd->shortcut && strcasecmp(cmd->shortcut, name) == 0)) {
            return cmd;
        }
    }
    return NULL;
}

static int
console_cmd_parseHex(const char *s, uint32_t *out)
{
    if (!s || !*s || !out) {
        return 0;
    }
    const char *p = s;
    if (*p == '$') {
        p += 1;
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
    unsigned long v = strtoul(p, NULL, 16);
    if (errno != 0) {
        return 0;
    }
    *out = (uint32_t)(v & 0x00ffffffu);
    return 1;
}

static int
console_cmd_parseHexStrict(const char *s, uint64_t *out, int *outDigits)
{
    if (out) {
        *out = 0;
    }
    if (outDigits) {
        *outDigits = 0;
    }
    if (!s || !*s || !out || !outDigits) {
        return 0;
    }
    const char *p = s;
    const char *digitsStart = NULL;
    if (*p == '$') {
        p += 1;
    } else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    } else {
        return 0;
    }
    if (!*p) {
        return 0;
    }
    digitsStart = p;
    int digits = 0;
    while (*p) {
        if (!isxdigit((unsigned char)*p)) {
            return 0;
        }
        ++digits;
        ++p;
    }
    errno = 0;
    unsigned long long v = strtoull(digitsStart, NULL, 16);
    if (errno != 0) {
        return 0;
    }
    *out = (uint64_t)v;
    *outDigits = digits;
    return 1;
}

static int
console_cmd_parseU32Strict(const char *s, uint32_t *out)
{
    if (out) {
        *out = 0;
    }
    uint64_t v = 0;
    int digits = 0;
    if (!console_cmd_parseHexStrict(s, &v, &digits)) {
        return 0;
    }
    if (v > 0xffffffffULL) {
        return 0;
    }
    if (out) {
        *out = (uint32_t)v;
    }
    return 1;
}

static int
console_cmd_parseU32Auto(const char *s, uint32_t *out)
{
    if (out) {
        *out = 0;
    }
    if (!s || !*s || !out) {
        return 0;
    }
    if (s[0] == '-') {
        return 0;
    }
    errno = 0;
    char *end = NULL;
    const char *p = s;
    int base = 0;
    if (*p == '$') {
        p += 1;
        base = 16;
    }
    unsigned long long v = strtoull(p, &end, base);
    if (errno != 0 || !end || end == p || *end != '\0') {
        return 0;
    }
    if (v > 0xffffffffULL) {
        return 0;
    }
    *out = (uint32_t)v;
    return 1;
}

static int
console_cmd_parseU64Dec(const char *s, uint64_t *out)
{
    if (out) {
        *out = 0;
    }
    if (!s || !*s || !out) {
        return 0;
    }
    for (const char *p = s; *p; ++p) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
    }
    errno = 0;
    unsigned long long v = strtoull(s, NULL, 10);
    if (errno != 0) {
        return 0;
    }
    *out = (uint64_t)v;
    return 1;
}

static size_t
console_cmd_sizeFromHexDigits(int digits)
{
    if (digits <= 0) {
        return 0;
    }
    if (digits <= 2) {
        return 1;
    }
    if (digits <= 4) {
        return 2;
    }
    if (digits <= 8) {
        return 4;
    }
    return 0;
}

static int
console_cmd_parseSizeBitsOpt(const char *tok, uint32_t *out_size_bits)
{
    if (!tok || !out_size_bits) {
        return 0;
    }
    if (strncasecmp(tok, "size=", 5) != 0) {
        return 0;
    }
    int sz = atoi(tok + 5);
    if (sz != 8 && sz != 16 && sz != 32) {
        return -1;
    }
    *out_size_bits = (uint32_t)sz;
    return 1;
}

static int
console_cmd_readMemoryValueBe(uint32_t addr, size_t sizeBytes, uint32_t *outValue)
{
    if (outValue) {
        *outValue = 0u;
    }
    if (!outValue || (sizeBytes != 1u && sizeBytes != 2u && sizeBytes != 4u)) {
        return 0;
    }

    uint8_t buf[4] = {0, 0, 0, 0};
    if (!libretro_host_debugReadMemory(addr, buf, sizeBytes)) {
        return 0;
    }

    if (sizeBytes == 1u) {
        *outValue = (uint32_t)buf[0];
        return 1;
    }
    if (sizeBytes == 2u) {
        *outValue = ((uint32_t)buf[0] << 8) | (uint32_t)buf[1];
        return 1;
    }

    *outValue = ((uint32_t)buf[0] << 24) |
                ((uint32_t)buf[1] << 16) |
                ((uint32_t)buf[2] << 8) |
                (uint32_t)buf[3];
    return 1;
}

static const char *
console_cmd_basename(const char *path)
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
console_cmd_fileMatches(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    if (strcmp(a, b) == 0) {
        return 1;
    }
    const char *ba = console_cmd_basename(a);
    const char *bb = console_cmd_basename(b);
    if (!ba || !bb) {
        return 0;
    }
    return strcmp(ba, bb) == 0;
}

static int
console_cmd_symbolMatch(const char *name, const char *symbol)
{
    if (!name || !symbol) {
        return 0;
    }
    if (strcmp(name, symbol) == 0) {
        return 1;
    }
    size_t base_len = strlen(symbol);
    if (base_len == 0) {
        return 0;
    }
    if (strncmp(name, symbol, base_len) == 0) {
        char next = name[base_len];
        if (next == '.' || next == '$') {
            return 1;
        }
    }
    if (symbol[0] != '_' && name[0] == '_' && strcmp(name + 1, symbol) == 0) {
        return 1;
    }
    return 0;
}

static int
console_cmd_parseStabStringName(const char *stabStr, char *outName, size_t cap)
{
    if (!stabStr || !outName || cap == 0) {
        return 0;
    }
    outName[0] = '\0';
    const char *colon = strchr(stabStr, ':');
    if (!colon || colon == stabStr) {
        return 0;
    }
    size_t len = (size_t)(colon - stabStr);
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(outName, stabStr, len);
    outName[len] = '\0';
    return outName[0] ? 1 : 0;
}

static int
console_cmd_resolveSymbolStabsFun(const char *elf, const char *symbol, uint32_t *out_addr)
{
    if (!elf || !*elf || !symbol || !*symbol || !out_addr) {
        return 0;
    }
    char objdump[PATH_MAX];
    if (!debugger_toolchainBuildBinary(objdump, sizeof(objdump), "objdump")) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), objdump, "-G", elf, 0)) {
        return 0;
    }
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return 0;
    }
    char line[2048];
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
        if (!stabType || strcmp(stabType, "FUN") != 0) {
            continue;
        }
        const char *nValueStr = tokens[4];
        const char *stabStr = tokens[count - 1];
        if (!nValueStr || !*nValueStr || !stabStr || !*stabStr) {
            continue;
        }
        char name[256];
        if (!console_cmd_parseStabStringName(stabStr, name, sizeof(name))) {
            continue;
        }
        if (!console_cmd_symbolMatch(name, symbol)) {
            continue;
        }
        errno = 0;
        uint32_t nValue = (uint32_t)strtoul(nValueStr, NULL, 16);
        if (errno != 0) {
            continue;
        }
        *out_addr = nValue & 0x00ffffffu;
        pclose(fp);
        return 1;
    }
    pclose(fp);
    return 0;
}

static int
console_cmd_resolveSymbol(const char *elf, const char *symbol, uint32_t *out_addr)
{
    if (!elf || !*elf || !symbol || !*symbol || !out_addr) {
        return 0;
    }
    if (debugger.symbolFileKind == DEBUGGER_SYMBOL_FILE_KIND_TEXT_MAP) {
        const symbol_text_map_entry_t *entry = NULL;
        if (!symbol_text_map_findExact(elf,
                                       symbol,
                                       SYMBOL_TEXT_MAP_SYMBOL_MASK_ALL,
                                       &entry) ||
            !entry) {
            return 0;
        }
        *out_addr = entry->addr & 0x00ffffffu;
        return 1;
    }
    char cmd[PATH_MAX * 2];
    char objdump[PATH_MAX];
    if (!debugger_toolchainBuildBinary(objdump, sizeof(objdump), "objdump")) {
        debug_error("break: failed to resolve objdump");
        return 0;
    }
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), objdump, "--syms", elf, 0)) {
        debug_error("break: failed to build objdump command");
        return 0;
    }
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        debug_error("break: failed to run objdump");
        return 0;
    }
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        char *tokens[8];
        int count = 0;
        char *cursor = line;
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
        if (count < 2) {
            continue;
        }
        const char *name = tokens[count - 1];
        if (!console_cmd_symbolMatch(name, symbol)) {
            continue;
        }
        uint32_t addr = 0;
        if (!console_cmd_parseHex(tokens[0], &addr)) {
            continue;
        }
        if (debugger_toolchainUsesHunkAddr2line() &&
            base_map_getMode() == BASE_MAP_MODE_STACK &&
            count >= 3) {
            uint32_t runtimeAddr = addr;
            if (base_map_symbolToRuntimeHunk(tokens[2], addr, &runtimeAddr)) {
                addr = runtimeAddr;
            }
        }
        *out_addr = addr;
        pclose(fp);
        return 1;
    }
    pclose(fp);
    return console_cmd_resolveSymbolStabsFun(elf, symbol, out_addr);
}

static int
console_cmd_resolveFileLine(const char *elf, const char *file, int line_no, uint32_t *out_addr)
{
    if (!elf || !*elf || !file || !*file || line_no <= 0 || !out_addr) {
        return 0;
    }
    if (debugger_toolchainUsesHunkAddr2line()) {
        return hunk_fileline_cache_resolveFileLine(elf, file, line_no, out_addr);
    }
    char cmd[PATH_MAX * 2];
    char objdump[PATH_MAX];
    if (!debugger_toolchainBuildBinary(objdump, sizeof(objdump), "objdump")) {
        debug_error("break: failed to resolve objdump");
        return 0;
    }
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), objdump, "-l -d", elf, 0)) {
        debug_error("break: failed to build objdump command");
        return 0;
    }
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        debug_error("break: failed to run objdump");
        return 0;
    }
    char line[1024];
    int want_addr = 0;
    while (fgets(line, sizeof(line), fp)) {
        char *nl = strchr(line, '\n');
        if (nl) {
            *nl = '\0';
        }
        char *colon = strrchr(line, ':');
        if (colon && isdigit((unsigned char)colon[1])) {
            int ln = atoi(colon + 1);
            if (ln == line_no) {
                *colon = '\0';
                if (console_cmd_fileMatches(line, file)) {
                    want_addr = 1;
                    continue;
                }
            }
        }
        if (want_addr) {
            char *p = line;
            while (*p && isspace((unsigned char)*p)) {
                ++p;
            }
            if (isxdigit((unsigned char)*p)) {
                char *end = p;
                while (*end && isxdigit((unsigned char)*end)) {
                    ++end;
                }
                if (*end == ':') {
                    char addr_buf[32];
                    size_t len = (size_t)(end - p);
                    if (len < sizeof(addr_buf)) {
                        memcpy(addr_buf, p, len);
                        addr_buf[len] = '\0';
                        uint32_t addr = 0;
                        if (console_cmd_parseHex(addr_buf, &addr)) {
                            *out_addr = addr;
                            pclose(fp);
                            return 1;
                        }
                    }
                }
            }
        }
    }
    pclose(fp);
    return 0;
}

static int
console_cmd_help(int argc, char **argv)
{
    if (argc < 2) {
        debug_printf("Commands:\n");
        for (size_t i = 0; i < sizeof(console_cmd) / sizeof(console_cmd[0]); ++i) {
            const console_cmd_entry_t *cmd = &console_cmd[i];
            if (cmd->shortcut) {
                debug_printf("  %s (%s)\n", cmd->name, cmd->shortcut, cmd->usage);
            } else {
                debug_printf("  %s\n", cmd->name, cmd->usage);
            }
        }
        return 1;
    }
    const console_cmd_entry_t *cmd = console_cmd_find(argv[1]);
    if (!cmd) {
        debug_error("help: unknown command '%s'", argv[1]);
        return 0;
    }
    debug_printf("\n%s\n\n%s\n", cmd->help, cmd->usage);
    return 1;
}

static int
console_cmd_base(int argc, char **argv)
{
    uint32_t textBase = 0;
    uint32_t dataBase = 0;
    uint32_t bssBase = 0;
    base_map_getBasicBases(&textBase, &dataBase, &bssBase);
    if (argc < 2) {
        debug_printf("base: text=0x%08X data=0x%08X bss=0x%08X\n",
                     (unsigned)textBase,
                     (unsigned)dataBase,
                     (unsigned)bssBase);
        return 1;
    }
    base_map_section_t section = BASE_MAP_SECTION_TEXT;
    const char *sectionName = "text";
    int argIndex = 1;

    if (strcasecmp(argv[1], "text") == 0) {
        section = BASE_MAP_SECTION_TEXT;
        sectionName = "text";
        argIndex = 2;
    } else if (strcasecmp(argv[1], "data") == 0) {
        section = BASE_MAP_SECTION_DATA;
        sectionName = "data";
        argIndex = 2;
    } else if (strcasecmp(argv[1], "bss") == 0) {
        section = BASE_MAP_SECTION_BSS;
        sectionName = "bss";
        argIndex = 2;
    } else if (strcasecmp(argv[1], "clear") == 0) {
        debugger_setTextBaseAddress(0);
        debugger_setDataBaseAddress(0);
        debugger_setBssBaseAddress(0);
        debug_printf("base: cleared\n");
        return 1;
    }

    if (argc <= argIndex) {
        debug_printf("base: %s=0x%08X\n", sectionName, (unsigned)base_map_getBasicBase(section));
        return 1;
    }
    if (strcasecmp(argv[argIndex], "clear") == 0) {
        if (section == BASE_MAP_SECTION_TEXT) {
            debugger_setTextBaseAddress(0);
        } else if (section == BASE_MAP_SECTION_DATA) {
            debugger_setDataBaseAddress(0);
        } else {
            debugger_setBssBaseAddress(0);
        }
        debug_printf("base: cleared %s\n", sectionName);
        return 1;
    }
    uint32_t addr = 0;
    if (!console_cmd_parseU32Auto(argv[argIndex], &addr)) {
        debug_error("base: invalid address '%s' (use decimal or 0x... or $...)", argv[argIndex]);
        return 0;
    }
    if (section == BASE_MAP_SECTION_TEXT) {
        debugger_setTextBaseAddress(addr);
    } else if (section == BASE_MAP_SECTION_DATA) {
        debugger_setDataBaseAddress(addr);
    } else {
        debugger_setBssBaseAddress(addr);
    }
    debug_printf("base: set %s to 0x%08X\n", sectionName, (unsigned)base_map_getBasicBase(section));
    return 1;
}

static int
console_cmd_break(int argc, char **argv)
{
    if (argc < 2) {
        debug_printf("Usage: break <addr|symbol|file:line>\n");
        return 0;
    }
    const char *arg = argv[1];
    const char *elf = debugger.libretro.exePath;
    uint32_t addr = 0;
    int ok = 0;
    const char *colon = strrchr(arg, ':');
    if (colon && colon[1]) {
        int line_no = atoi(colon + 1);
        if (line_no > 0) {
            size_t len = (size_t)(colon - arg);
            char file_buf[PATH_MAX];
            if (len < sizeof(file_buf)) {
                memcpy(file_buf, arg, len);
                file_buf[len] = '\0';
                if (!elf || !*elf) {
                    debug_error("break: no debug symbol file configured (set --elf/--hunk/--symbols or Settings)");
                    return 0;
                }
                if (debugger.symbolFileKind == DEBUGGER_SYMBOL_FILE_KIND_TEXT_MAP) {
                    debug_error("break: file:line requires binary debug symbols");
                    return 0;
                }
                ok = console_cmd_resolveFileLine(elf, file_buf, line_no, &addr);
                if (ok) {
                    if (!(debugger_toolchainUsesHunkAddr2line() &&
                          base_map_getMode() == BASE_MAP_MODE_STACK)) {
                        (void)base_map_debugToRuntime(BASE_MAP_SECTION_TEXT, addr, &addr);
                    }
                }
            }
        }
    }
    if (!ok) {
        ok = console_cmd_parseHex(arg, &addr);
    }
    if (ok) {
        machine_breakpoint_t *bp = machine_addBreakpoint(&debugger.machine, addr, 1);
        if (!bp) {
            debug_error("break: failed to add breakpoint");
            return 0;
        }
        breakpoints_resolveLocation(bp);
        libretro_host_debugAddBreakpoint(addr);
        breakpoints_markDirty();
        debug_printf("break: added at 0x%06X\n", addr);
        return 1;
    }
    if (!elf || !*elf) {
        debug_error("break: no debug symbol file configured (set --elf/--hunk/--symbols or Settings)");
        return 0;
    }
    if (!ok) {
        ok = console_cmd_resolveSymbol(elf, arg, &addr);
    }
    if (!ok) {
        debug_error("break: failed to resolve '%s'", arg);
        return 0;
    }
    if (debugger.symbolFileKind != DEBUGGER_SYMBOL_FILE_KIND_TEXT_MAP) {
        if (!(debugger_toolchainUsesHunkAddr2line() &&
              base_map_getMode() == BASE_MAP_MODE_STACK)) {
            (void)base_map_debugToRuntime(BASE_MAP_SECTION_TEXT, addr, &addr);
        }
    }
    machine_breakpoint_t *bp = machine_addBreakpoint(&debugger.machine, addr, 1);
    if (!bp) {
        debug_error("break: failed to add breakpoint");
        return 0;
    }
    breakpoints_resolveLocation(bp);
    libretro_host_debugAddBreakpoint(addr);
    breakpoints_markDirty();
    debug_printf("break: added at 0x%06X\n", addr);
    return 1;
}

static int
console_cmd_watchList(void)
{
    e9k_debug_watchpoint_t wps[E9K_WATCHPOINT_COUNT];
    memset(wps, 0, sizeof(wps));
    size_t count = 0;
    if (!libretro_host_debugReadWatchpoints(wps, E9K_WATCHPOINT_COUNT, &count)) {
        debug_error("watch: libretro core does not expose watchpoints");
        return 0;
    }
    uint64_t enabled = 0;
    (void)libretro_host_debugGetWatchpointEnabledMask(&enabled);

    debug_printf("Watchpoints (enabled=0x%016llX):\n", (unsigned long long)enabled);
    for (size_t i = 0; i < count; ++i) {
        const e9k_debug_watchpoint_t *wp = &wps[i];
        int is_enabled = ((enabled >> i) & 1ull) ? 1 : 0;
        if (!is_enabled && wp->op_mask == 0) {
            continue;
        }

        const char *rw = "";
        if ((wp->op_mask & E9K_WATCH_OP_READ) && (wp->op_mask & E9K_WATCH_OP_WRITE)) {
            rw = "rw";
        } else if (wp->op_mask & E9K_WATCH_OP_READ) {
            rw = "r";
        } else if (wp->op_mask & E9K_WATCH_OP_WRITE) {
            rw = "w";
        }

        debug_printf("  [%02u] %s addr=0x%06X op=0x%08X %s",
                     (unsigned)i, is_enabled ? "on " : "off",
                     (unsigned)(wp->addr & 0x00ffffffu), (unsigned)wp->op_mask, rw);
        if (wp->op_mask & E9K_WATCH_OP_ACCESS_SIZE) {
            debug_printf(" size=%u", (unsigned)wp->size_operand);
        }
        if (wp->op_mask & E9K_WATCH_OP_ADDR_COMPARE_MASK) {
            debug_printf(" mask=0x%08X", (unsigned)wp->addr_mask_operand);
        }
        if (wp->op_mask & E9K_WATCH_OP_VALUE_EQ) {
            debug_printf(" val=0x%08X", (unsigned)wp->value_operand);
        }
        if (wp->op_mask & E9K_WATCH_OP_OLD_VALUE_EQ) {
            debug_printf(" old=0x%08X", (unsigned)wp->old_value_operand);
        }
        if (wp->op_mask & E9K_WATCH_OP_VALUE_NEQ_OLD) {
            debug_printf(" diff=0x%08X", (unsigned)wp->diff_operand);
        }
        debug_printf("\n");
    }

    return 1;
}

static int
console_cmd_watch(int argc, char **argv)
{
    if (argc < 2) {
        return console_cmd_watchList();
    }

    if (strcasecmp(argv[1], "clear") == 0) {
        if (!libretro_host_debugResetWatchpoints()) {
            debug_error("watch: libretro core does not expose watchpoints");
            return 0;
        }
        debug_printf("watch: cleared\n");
        return 1;
    }

    if (strcasecmp(argv[1], "del") == 0 || strcasecmp(argv[1], "rm") == 0 || strcasecmp(argv[1], "remove") == 0) {
        if (argc < 3) {
            debug_printf("Usage: watch del <idx>\n");
            return 0;
        }
        char *end = NULL;
        unsigned long idx = strtoul(argv[2], &end, 0);
        if (!end || *end != '\0') {
            debug_error("watch: invalid index '%s'", argv[2]);
            return 0;
        }
        if (!libretro_host_debugRemoveWatchpoint((uint32_t)idx)) {
            debug_error("watch: remove failed (unsupported?)");
            return 0;
        }
        debug_printf("watch: removed %lu\n", idx);
        return 1;
    }

    uint32_t addr = 0;
    if (!console_cmd_parseHex(argv[1], &addr)) {
        size_t resolvedSize = 0;
        if (!print_eval_resolveAddress(argv[1], &addr, &resolvedSize)) {
            debug_error("watch: expected address or symbol, got '%s'", argv[1]);
            return 0;
        }
        (void)resolvedSize;
    }
    addr &= 0x00ffffffu;

    uint32_t op_mask = 0;
    uint32_t diff_operand = 0;
    uint32_t value_operand = 0;
    uint32_t old_value_operand = 0;
    uint32_t size_operand = 0;
    uint32_t addr_mask_operand = 0;
    int have_rw = 0;

    for (int i = 2; i < argc; ++i) {
        const char *tok = argv[i];
        if (!tok || !*tok) {
            continue;
        }
        if (strcasecmp(tok, "r") == 0 || strcasecmp(tok, "read") == 0) {
            op_mask |= E9K_WATCH_OP_READ;
            have_rw = 1;
            continue;
        }
        if (strcasecmp(tok, "w") == 0 || strcasecmp(tok, "write") == 0) {
            op_mask |= E9K_WATCH_OP_WRITE;
            have_rw = 1;
            continue;
        }
        if (strcasecmp(tok, "rw") == 0 || strcasecmp(tok, "wr") == 0) {
            op_mask |= (E9K_WATCH_OP_READ | E9K_WATCH_OP_WRITE);
            have_rw = 1;
            continue;
        }
        if (strncasecmp(tok, "size=", 5) == 0) {
            int sz = atoi(tok + 5);
            if (sz != 8 && sz != 16 && sz != 32) {
                debug_error("watch: invalid size '%s' (expected 8/16/32)", tok + 5);
                return 0;
            }
            op_mask |= E9K_WATCH_OP_ACCESS_SIZE;
            size_operand = (uint32_t)sz;
            continue;
        }
        if (strncasecmp(tok, "mask=", 5) == 0) {
            uint32_t v = 0;
            if (!console_cmd_parseU32Strict(tok + 5, &v)) {
                debug_error("watch: invalid mask '%s' (expected 0x... or $...)", tok + 5);
                return 0;
            }
            op_mask |= E9K_WATCH_OP_ADDR_COMPARE_MASK;
            addr_mask_operand = v;
            continue;
        }
        if (strncasecmp(tok, "val=", 4) == 0) {
            uint32_t v = 0;
            if (!console_cmd_parseU32Strict(tok + 4, &v)) {
                debug_error("watch: invalid val '%s' (expected 0x... or $...)", tok + 4);
                return 0;
            }
            op_mask |= E9K_WATCH_OP_VALUE_EQ;
            value_operand = v;
            continue;
        }
        if (strncasecmp(tok, "value=", 6) == 0) {
            uint32_t v = 0;
            if (!console_cmd_parseU32Strict(tok + 6, &v)) {
                debug_error("watch: invalid value '%s' (expected 0x... or $...)", tok + 6);
                return 0;
            }
            op_mask |= E9K_WATCH_OP_VALUE_EQ;
            value_operand = v;
            continue;
        }
        if (strncasecmp(tok, "old=", 4) == 0) {
            uint32_t v = 0;
            if (!console_cmd_parseU32Strict(tok + 4, &v)) {
                debug_error("watch: invalid old '%s' (expected 0x... or $...)", tok + 4);
                return 0;
            }
            op_mask |= E9K_WATCH_OP_OLD_VALUE_EQ;
            old_value_operand = v;
            continue;
        }
        if (strncasecmp(tok, "diff=", 5) == 0) {
            uint32_t v = 0;
            if (!console_cmd_parseU32Strict(tok + 5, &v)) {
                debug_error("watch: invalid diff '%s' (expected 0x... or $...)", tok + 5);
                return 0;
            }
            op_mask |= E9K_WATCH_OP_VALUE_NEQ_OLD;
            diff_operand = v;
            continue;
        }
        if (strncasecmp(tok, "neq=", 4) == 0) {
            uint32_t v = 0;
            if (!console_cmd_parseU32Strict(tok + 4, &v)) {
                debug_error("watch: invalid neq '%s' (expected 0x... or $...)", tok + 4);
                return 0;
            }
            op_mask |= E9K_WATCH_OP_VALUE_NEQ_OLD;
            diff_operand = v;
            continue;
        }

        debug_error("watch: unknown option '%s'", tok);
        return 0;
    }

    if (!have_rw) {
        op_mask |= (E9K_WATCH_OP_READ | E9K_WATCH_OP_WRITE);
    }

    uint32_t index = 0;
    if (!libretro_host_debugAddWatchpoint(addr, op_mask, diff_operand, value_operand, old_value_operand, size_operand, addr_mask_operand, &index)) {
        debug_error("watch: failed to add (table full or unsupported)");
        return 0;
    }
    debug_printf("watch: added [%u] at 0x%06X\n", (unsigned)index, (unsigned)addr);
    return 1;
}

static int
console_cmd_train(int argc, char **argv)
{
    if (argc < 2) {
        debug_printf("Usage: train <from> <to> [size=8|16|32] | train ignore | train clear\n");
        return 0;
    }

    if (strcasecmp(argv[1], "clear") == 0) {
        train_clearIgnoreList();
        debug_printf("train: ignore list cleared\n");
        return 1;
    }

    if (strcasecmp(argv[1], "ignore") == 0) {
        uint32_t addr24 = 0;
        if (!train_getLastWatchbreakAddr(&addr24)) {
            debug_error("train: no watchbreak to ignore yet");
            return 0;
        }
        if (!train_addIgnoreAddr(addr24)) {
            debug_error("train: ignore list full");
            return 0;
        }
        debug_printf("train: ignoring 0x%06X\n", (unsigned)addr24);
        return 1;
    }

    if (argc < 3) {
        debug_printf("Usage: train <from> <to> [size=8|16|32]\n");
        return 0;
    }

    uint32_t from = 0;
    uint32_t to = 0;
    if (!console_cmd_parseU32Auto(argv[1], &from)) {
        debug_error("train: invalid from '%s' (expected decimal or 0x... or $...)", argv[1]);
        return 0;
    }
    if (!console_cmd_parseU32Auto(argv[2], &to)) {
        debug_error("train: invalid to '%s' (expected decimal or 0x... or $...)", argv[2]);
        return 0;
    }

    uint32_t op_mask = 0;
    uint32_t size_operand = 0;
    for (int i = 3; i < argc; ++i) {
        const char *tok = argv[i];
        if (!tok || !*tok) {
            continue;
        }
        if (strncasecmp(tok, "size=", 5) == 0) {
            int sz = atoi(tok + 5);
            if (sz != 8 && sz != 16 && sz != 32) {
                debug_error("train: invalid size '%s' (expected 8/16/32)", tok + 5);
                return 0;
            }
            op_mask |= E9K_WATCH_OP_ACCESS_SIZE;
            size_operand = (uint32_t)sz;
            continue;
        }
        debug_error("train: unknown option '%s'", tok);
        return 0;
    }

    // Any address: enable address compare mask with mask=0 (always matches).
    op_mask |= E9K_WATCH_OP_ADDR_COMPARE_MASK;
    op_mask |= E9K_WATCH_OP_WRITE;
    op_mask |= E9K_WATCH_OP_OLD_VALUE_EQ;
    op_mask |= E9K_WATCH_OP_VALUE_EQ;

    uint32_t index = 0;
    if (!libretro_host_debugAddWatchpoint(0, op_mask, 0, to, from, size_operand, 0, &index)) {
        debug_error("train: failed to add watchpoint (table full or unsupported)");
        return 0;
    }
    train_setWatchIndex(index);

    debug_printf("train: watchpoint [%u] old=0x%08X -> val=0x%08X\n", (unsigned)index, (unsigned)from, (unsigned)to);
    return 1;
}

static int
console_cmd_loop(int argc, char **argv)
{
    if (argc < 2) {
        if (!debugger.loopEnabled) {
            debug_printf("loop: disabled\n");
            return 1;
        }
        debug_printf("loop: enabled from=%llu to=%llu\n",
                     (unsigned long long)debugger.loopFrom,
                     (unsigned long long)debugger.loopTo);
        return 1;
    }

    if (strcasecmp(argv[1], "clear") == 0) {
        debugger.loopEnabled = 0;
        debugger.loopFrom = 0;
        debugger.loopTo = 0;
        debug_printf("loop: cleared\n");
        return 1;
    }

    if (argc < 3) {
        debug_printf("Usage: loop <from> <to>\n");
        return 0;
    }

    uint64_t from = 0;
    uint64_t to = 0;
    if (!console_cmd_parseU64Dec(argv[1], &from)) {
        debug_error("loop: invalid from '%s' (expected decimal integer)", argv[1]);
        return 0;
    }
    if (!console_cmd_parseU64Dec(argv[2], &to)) {
        debug_error("loop: invalid to '%s' (expected decimal integer)", argv[2]);
        return 0;
    }
    if (from >= to) {
        debug_error("loop: expected from < to");
        return 0;
    }

    if (!state_buffer_hasFrameNo(from)) {
        debug_error("loop: from frame %llu not in state buffer", (unsigned long long)from);
        return 0;
    }
    if (!state_buffer_hasFrameNo(to)) {
        debug_error("loop: to frame %llu not in state buffer", (unsigned long long)to);
        return 0;
    }

    debugger.loopEnabled = 1;
    debugger.loopFrom = from;
    debugger.loopTo = to;

    return 1;
}

static int
console_cmd_protect(int argc, char **argv)
{
    if (argc < 2) {
        protect_debugList();
        return 1;
    }

    if (strcasecmp(argv[1], "clear") == 0) {
        protect_clear();
        debug_printf("protect: cleared\n");
        return 1;
    }

    if (strcasecmp(argv[1], "del") == 0 || strcasecmp(argv[1], "rm") == 0 || strcasecmp(argv[1], "remove") == 0) {
        if (argc < 3) {
            debug_printf("Usage: protect del <addr> [size=8|16|32]\n");
            return 0;
        }
        uint32_t addr = 0;
        if (!console_cmd_parseHex(argv[2], &addr)) {
            debug_error("protect: invalid address '%s'", argv[2]);
            return 0;
        }
        uint32_t size_bits = 8;
        for (int i = 3; i < argc; ++i) {
            int r = console_cmd_parseSizeBitsOpt(argv[i], &size_bits);
            if (r == 1) {
                continue;
            }
            if (r == -1) {
                debug_error("protect: invalid size '%s'", argv[i]);
                return 0;
            }
            debug_error("protect: unknown option '%s'", argv[i]);
            return 0;
        }
        if (!protect_remove(addr, size_bits)) {
            debug_error("protect: not found");
            return 0;
        }
        debug_printf("protect: removed\n");
        return 1;
    }

    uint32_t addr = 0;
    if (!console_cmd_parseHex(argv[1], &addr)) {
        debug_error("protect: invalid address '%s'", argv[1]);
        return 0;
    }

    uint32_t size_bits = 8;
    int mode_set = 0;
    uint32_t set_value = 0;
    int mode_block = 0;

    for (int i = 2; i < argc; ++i) {
        const char *tok = argv[i];
        if (!tok || !*tok) {
            continue;
        }
        if (strcasecmp(tok, "block") == 0 || strcasecmp(tok, "deny") == 0) {
            mode_block = 1;
            continue;
        }
        if (strncasecmp(tok, "set=", 4) == 0) {
            if (!console_cmd_parseU32Strict(tok + 4, &set_value)) {
                debug_error("protect: invalid set value '%s' (expected 0x... or $...)", tok + 4);
                return 0;
            }
            mode_set = 1;
            continue;
        }
        if (strncasecmp(tok, "value=", 6) == 0) {
            if (!console_cmd_parseU32Strict(tok + 6, &set_value)) {
                debug_error("protect: invalid value '%s' (expected 0x... or $...)", tok + 6);
                return 0;
            }
            mode_set = 1;
            continue;
        }
        int r = console_cmd_parseSizeBitsOpt(tok, &size_bits);
        if (r == 1) {
            continue;
        }
        if (r == -1) {
            debug_error("protect: invalid size '%s'", tok);
            return 0;
        }

        debug_error("protect: unknown option '%s'", tok);
        return 0;
    }

    if (mode_set && mode_block) {
        debug_error("protect: choose either block or set=...");
        return 0;
    }
    if (!mode_set && !mode_block) {
        debug_printf("Usage: protect <addr> block [size=8|16|32]\nprotect <addr> set=0x...|$... [size=8|16|32]\n");
        return 0;
    }

    int ok = 0;
    if (mode_block) {
        ok = protect_addBlock(addr, size_bits);
    } else {
        ok = protect_addSet(addr, set_value, size_bits);
    }
    if (!ok) {
        debug_error("protect: failed (core protect API missing?)");
        return 0;
    }
    debug_printf("protect: added\n");
    return 1;
}

static int
console_cmd_continue(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (libretro_host_debugResume()) {
        machine_setRunning(&debugger.machine, 1);
        e9ui_setFocus(&e9ui->ctx, NULL);
        return 1;
    }
    debug_error("continue: resume failed");
    return 0;
}

static int
console_cmd_cls(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    linebuf_clear(&debugger.console);
    debugger.consoleScrollLines = 0;
    return 1;
}

static int
console_cmd_step(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    debugger_suppressBreakpointAtPC();
    if (libretro_host_debugStepLine()) {
        machine_setRunning(&debugger.machine, 1);
        return 1;
    }
    debug_error("step line: libretro core does not expose debug step line");
    return 0;
}

static int
console_cmd_stepi(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    debugger_suppressBreakpointAtPC();
    if (libretro_host_debugStepInstr()) {
        machine_setRunning(&debugger.machine, 1);
        return 1;
    }
    debug_error("step instruction: libretro core does not expose debug step");
    return 0;
}

static int
console_cmd_next(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    debugger_suppressBreakpointAtPC();
    if (libretro_host_debugStepNext()) {
        machine_setRunning(&debugger.machine, 1);
        return 1;
    }
    debug_error("step next: libretro core does not expose debug next");
    return 0;
}

static int
console_cmd_finish(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    debugger_suppressBreakpointAtPC();
    if (libretro_host_debugStepOut()) {
        machine_setRunning(&debugger.machine, 1);
        return 1;
    }
    debug_error("step out: libretro core does not expose debug step out");
    return 0;
}

static int
console_cmd_write(int argc, char **argv)
{
    if (argc < 3) {
        debug_printf("Usage: write <dest> <value>\n");
        return 0;
    }
    const char *dest = argv[1];
    const char *value_str = argv[2];
    uint64_t value = 0;
    int value_digits = 0;
    if (!console_cmd_parseHexStrict(value_str, &value, &value_digits)) {
        debug_error("write: value must be hex (0x... or $...)");
        return 0;
    }
    size_t value_size = console_cmd_sizeFromHexDigits(value_digits);
    if (value_size == 0) {
        debug_error("write: value too wide (max 32-bit hex)");
        return 0;
    }
    if (dest && ((dest[0] == '$') || (dest[0] == '0' && (dest[1] == 'x' || dest[1] == 'X')))) {
        uint64_t addr64 = 0;
        int addr_digits = 0;
        if (!console_cmd_parseHexStrict(dest, &addr64, &addr_digits)) {
            debug_error("write: address must be hex (0x... or $...)");
            return 0;
        }
        if (addr64 > 0xffffffffu) {
            debug_error("write: address out of range");
            return 0;
        }
        uint32_t addr = (uint32_t)addr64;
        if (!libretro_host_debugWriteMemory(addr, (uint32_t)value, value_size)) {
            debug_error("write: failed to write 0x%llX to 0x%08X",
                        (unsigned long long)value, addr);
            return 0;
        }
        debug_printf("%s = 0x%llX (%zu bits)\n", dest, (unsigned long long)value, value_size * 8);
        return 1;
    }
    uint32_t sym_addr = 0;
    size_t sym_size = 0;
    if (!print_eval_resolveAddress(dest, &sym_addr, &sym_size)) {
        debug_error("write: unknown symbol '%s'", dest);
        return 0;
    }
    if (sym_size > 4) {
        debug_error("write: can't write to %s (size %zu); use \"write 0x%08X %s\" to write the address directly",
                    dest, sym_size, sym_addr, value_str);
        return 0;
    }
    if (value_digits > (int)(sym_size * 2)) {
        debug_error("write: value too large for %s (%zu bytes)", dest, sym_size);
        return 0;
    }
    if (!libretro_host_debugWriteMemory(sym_addr, (uint32_t)value, sym_size)) {
        debug_error("write: failed to write 0x%llX to %s", (unsigned long long)value, dest);
        return 0;
    }
    debug_printf("%s = 0x%llX (%zu bits)\n", dest, (unsigned long long)value, sym_size * 8);
    return 1;
}

static int
console_cmd_transition(int argc, char **argv)
{
    if (argc < 2) {
        debug_printf("transition: %s\n", transition_modeName(e9ui->transition.mode));
        debug_printf("Usage: transition <slide|explode|doom|flip|rbar|random|cycle|none>\n");
        return 1;
    }
    e9k_transition_mode_t mode = e9k_transition_none;
    if (!transition_parseMode(argv[1], &mode)) {
        debug_error("transition: unknown mode '%s'", argv[1]);
        return 0;
    }
    e9ui->transition.mode = mode;
    e9ui->transition.fullscreenModeSet = 0;
    config_saveConfig();
    debug_printf("transition: %s\n", transition_modeName(e9ui->transition.mode));
    return 1;
}

static int
console_cmd_symbols(int argc, char **argv)
{
    if (argc < 2) {
        console_cmd_symbolsPrintUsage();
        return 0;
    }
    if (strcasecmp(argv[1], "export") == 0) {
        if (argc < 3 || !argv[2] || !argv[2][0]) {
            console_cmd_symbolsPrintUsage();
            return 0;
        }

        const char *symbolPath = debugger.libretro.exePath;
        if (!symbolPath || !symbolPath[0] || !debugger.symbolValid) {
            debug_error("symbols: no debug symbol file configured");
            return 0;
        }

        console_export_symbol_t *symbols = NULL;
        int symbolCount = 0;
        int ok = 0;
        if (debugger.symbolFileKind == DEBUGGER_SYMBOL_FILE_KIND_TEXT_MAP) {
            ok = console_cmd_exportSymbolsFromTextMap(symbolPath, &symbols, &symbolCount);
        } else {
            ok = console_cmd_exportSymbolsFromBinary(symbolPath, &symbols, &symbolCount);
            if (!symbols) {
                symbols = NULL;
                symbolCount = 0;
            }
            int symbolCap = symbolCount;
            if (debugger.elfValid) {
                (void)console_cmd_exportSymbolsSupplementReadelfFunctions(symbolPath,
                                                                          &symbols,
                                                                          &symbolCount,
                                                                          &symbolCap);
                (void)console_cmd_exportSymbolsSupplementStabsFunctions(symbolPath,
                                                                        &symbols,
                                                                        &symbolCount,
                                                                        &symbolCap);
                (void)console_cmd_exportSymbolsSupplementObjdumpTextFunctions(symbolPath,
                                                                              &symbols,
                                                                              &symbolCount,
                                                                              &symbolCap);
            }
            if (console_cmd_exportSymbolsSupplementFromPrintEval(&symbols, &symbolCount, &symbolCap) > 0) {
                ok = 1;
            }
        }
        if (!ok || !symbols || symbolCount <= 0) {
            console_cmd_exportSymbolsFree(symbols, symbolCount);
            debug_error("symbols: failed to collect symbols from '%s'", symbolPath);
            return 0;
        }

        if (!console_cmd_exportSymbolsWriteFile(argv[2], symbols, symbolCount)) {
            console_cmd_exportSymbolsFree(symbols, symbolCount);
            debug_error("symbols: failed to write '%s'", argv[2]);
            return 0;
        }

        debug_printf("symbols: exported %d symbols to %s\n", symbolCount, argv[2]);
        console_cmd_exportSymbolsFree(symbols, symbolCount);
        return 1;
    }

    if (strcasecmp(argv[1], "load") == 0) {
        const char *activePath = NULL;
        if (argc < 3 || !argv[2] || !argv[2][0]) {
            console_cmd_symbolsPrintUsage();
            return 0;
        }
        if (console_cmd_symbolsGetEditablePath(&activePath)) {
            if (!symbol_text_map_importFile(argv[2])) {
                debug_error("symbols: failed to merge '%s'", argv[2]);
                return 0;
            }
            console_cmd_symbolsInvalidateCaches();
            debug_printf("symbols: loaded %s (merged)\n", argv[2]);
            return 1;
        }

        if (!symbol_text_map_canLoad(argv[2])) {
            debug_error("symbols: failed to load text symbol file '%s'", argv[2]);
            return 0;
        }
        symbol_text_map_clear();
        if (!console_cmd_symbolsSetActivePath(argv[2])) {
            debug_error("symbols: failed to activate symbol file '%s'", argv[2]);
            return 0;
        }
        console_cmd_symbolsInvalidateCaches();
        debug_printf("symbols: loaded %s\n", argv[2]);
        return 1;
    }

    if (strcasecmp(argv[1], "save") == 0) {
        const char *path = NULL;
        if (argc != 2) {
            console_cmd_symbolsPrintUsage();
            return 0;
        }
        if (!console_cmd_symbolsGetEditablePath(&path)) {
            debug_error("symbols: no text symbol file configured");
            return 0;
        }
        if (!symbol_text_map_saveToPath(path)) {
            debug_error("symbols: failed to write '%s'", path);
            return 0;
        }
        debug_printf("symbols: saved active symbol table to %s\n", path);
        return 1;
    }

    if (strcasecmp(argv[1], "list") == 0) {
        const char *path = NULL;
        const symbol_text_map_entry_t *entries = NULL;
        int count = 0;
        if (argc != 2) {
            console_cmd_symbolsPrintUsage();
            return 0;
        }
        if (!console_cmd_symbolsGetEditablePath(&path)) {
            debug_error("symbols: no text symbol file loaded");
            return 0;
        }
        if (!symbol_text_map_getEntries(path, &entries, &count)) {
            debug_error("symbols: failed to read '%s'", path);
            return 0;
        }
        if (count <= 0) {
            debug_printf("symbols: table is empty\n");
            return 1;
        }
        for (int i = 0; i < count; ++i) {
            (void)console_cmd_symbolsPrintEntry(&entries[i]);
        }
        debug_printf("symbols: listed %d symbol%s\n", count, count == 1 ? "" : "s");
        return 1;
    }

    if (strcasecmp(argv[1], "show") == 0) {
        const char *path = NULL;
        const symbol_text_map_entry_t *entries = NULL;
        int count = 0;
        int matched = 0;
        if (argc < 3 || !argv[2] || !argv[2][0]) {
            console_cmd_symbolsPrintUsage();
            return 0;
        }
        if (!console_cmd_symbolsGetEditablePath(&path)) {
            debug_error("symbols: no text symbol file loaded");
            return 0;
        }
        if (!symbol_text_map_getEntries(path, &entries, &count)) {
            debug_error("symbols: failed to read '%s'", path);
            return 0;
        }
        for (int i = 0; i < count; ++i) {
            if (strcmp(entries[i].name, argv[2]) != 0) {
                continue;
            }
            matched += console_cmd_symbolsPrintEntry(&entries[i]);
        }
        if (matched <= 0) {
            debug_error("symbols: symbol '%s' not found", argv[2]);
            return 0;
        }
        return 1;
    }

    if (strcasecmp(argv[1], "lookup") == 0) {
        const char *path = NULL;
        const symbol_text_map_entry_t *entries = NULL;
        uint32_t addr = 0;
        int count = 0;
        int matched = 0;
        if (argc != 3 || !argv[2] || !argv[2][0]) {
            console_cmd_symbolsPrintUsage();
            return 0;
        }
        if (!console_cmd_symbolsGetEditablePath(&path)) {
            debug_error("symbols: no text symbol file loaded");
            return 0;
        }
        if (!console_cmd_parseHex(argv[2], &addr)) {
            debug_error("symbols: invalid address '%s'", argv[2]);
            return 0;
        }
        if (!symbol_text_map_getEntries(path, &entries, &count)) {
            debug_error("symbols: failed to read '%s'", path);
            return 0;
        }
        for (int i = 0; i < count; ++i) {
            if ((entries[i].addr & 0x00ffffffu) != (addr & 0x00ffffffu)) {
                continue;
            }
            matched += console_cmd_symbolsPrintEntry(&entries[i]);
        }
        if (matched <= 0) {
            debug_error("symbols: no exact match at 0x%X", (unsigned)(addr & 0x00ffffffu));
            return 0;
        }
        return 1;
    }

    if (strcasecmp(argv[1], "add") == 0) {
        uint32_t addr = 0;
        symbol_text_map_symbol_kind_t kind = SYMBOL_TEXT_MAP_SYMBOL_KIND_UNKNOWN;
        if (argc < 5 || !argv[2] || !argv[2][0]) {
            console_cmd_symbolsPrintUsage();
            return 0;
        }
        if (!console_cmd_symbolsGetEditablePath(NULL)) {
            debug_error("symbols: no text symbol file loaded");
            return 0;
        }
        if (!console_cmd_parseHex(argv[3], &addr)) {
            debug_error("symbols: invalid address '%s'", argv[3]);
            return 0;
        }
        if (!console_cmd_symbolsParseKind(argv[4], &kind)) {
            debug_error("symbols: invalid kind '%s' (expected function, variable, or unknown)", argv[4]);
            return 0;
        }
        if (!symbol_text_map_upsert(argv[2], addr, kind)) {
            debug_error("symbols: failed to update '%s'", argv[2]);
            return 0;
        }
        console_cmd_symbolsInvalidateCaches();
        debug_printf("symbols: set %s = 0x%X (%s)\n",
                     argv[2],
                     (unsigned)(addr & 0x00ffffffu),
                     console_cmd_exportSymbolKindName(kind));
        return 1;
    }

    if (strcasecmp(argv[1], "delete") == 0) {
        int removedCount = 0;
        if (argc < 3 || !argv[2] || !argv[2][0]) {
            console_cmd_symbolsPrintUsage();
            return 0;
        }
        if (!console_cmd_symbolsGetEditablePath(NULL)) {
            debug_error("symbols: no text symbol file loaded");
            return 0;
        }
        if (!symbol_text_map_delete(argv[2], &removedCount)) {
            debug_error("symbols: symbol '%s' not found", argv[2]);
            return 0;
        }
        console_cmd_symbolsInvalidateCaches();
        debug_printf("symbols: removed %d entr%s named %s\n",
                     removedCount,
                     removedCount == 1 ? "y" : "ies",
                     argv[2]);
        return 1;
    }

    if (strcasecmp(argv[1], "reset") == 0) {
        if (argc != 2) {
            console_cmd_symbolsPrintUsage();
            return 0;
        }
        if (!console_cmd_symbolsGetEditablePath(NULL)) {
            debug_error("symbols: no text symbol file loaded");
            return 0;
        }
        if (!symbol_text_map_reset()) {
            debug_error("symbols: failed to reset active symbol table");
            return 0;
        }
        console_cmd_symbolsInvalidateCaches();
        debug_printf("symbols: reset active symbol table\n");
        return 1;
    }

    debug_error("symbols: unknown subcommand '%s'", argv[1]);
    return 0;
}

static int
console_cmd_print(int argc, char **argv)
{
    if (argc < 2) {
        debug_printf("Usage: print <expr> [size=8|16|32]\n");
        debug_printf("       print addr <expr>\n");
        return 0;
    }

    int addrMode = 0;
    int startIndex = 1;
    if (argc >= 3 && strcasecmp(argv[1], "addr") == 0) {
        addrMode = 1;
        startIndex = 2;
    }

    char expr[512];
    expr[0] = '\0';
    size_t used = 0;
    uint32_t sizeBitsOpt = 0u;
    for (int i = startIndex; i < argc; ++i) {
        const char *arg = argv[i];
        if (!arg) {
            continue;
        }
        uint32_t parsedSizeBits = 0u;
        int sizeToken = console_cmd_parseSizeBitsOpt(arg, &parsedSizeBits);
        if (sizeToken < 0) {
            debug_error("print: invalid size '%s' (expected size=8|16|32)", arg);
            return 0;
        }
        if (sizeToken > 0) {
            sizeBitsOpt = parsedSizeBits;
            continue;
        }
        size_t len = strlen(arg);
        if (used > 0 && used < sizeof(expr) - 1) {
            expr[used++] = ' ';
            expr[used] = '\0';
        }
        if (used + len >= sizeof(expr)) {
            len = sizeof(expr) - 1 - used;
        }
        memcpy(expr + used, arg, len);
        used += len;
        expr[used] = '\0';
    }
    if (expr[0] == '\0') {
        if (addrMode) {
            debug_printf("Usage: print addr <expr>\n");
        } else {
            debug_printf("Usage: print <expr> [size=8|16|32]\n");
            debug_printf("       print addr <expr>\n");
        }
        return 0;
    }

    if (addrMode) {
        uint32_t addr = 0;
        size_t sizeBytes = 0;
        if (!print_eval_resolveAddress(expr, &addr, &sizeBytes)) {
            debug_error("print: failed to resolve address '%s'", expr);
            return 0;
        }
        uint32_t bits = sizeBitsOpt;
        if (bits == 0u) {
            bits = (uint32_t)(sizeBytes > 0 ? sizeBytes * 8u : 32u);
        }
        debug_printf("%s: 0x%06X (%u bits)\n", expr, (unsigned)(addr & 0x00ffffffu), (unsigned)bits);
        return 1;
    }

    // Fast-path simple numeric expressions so `print *0xADDR` works without an ELF.
    {
        const char *p = expr;
        while (*p && isspace((unsigned char)*p)) {
            ++p;
        }

        int deref = 0;
        if (*p == '*') {
            deref = 1;
            ++p;
        }

        while (*p && isspace((unsigned char)*p)) {
            ++p;
        }

        int paren = 0;
        if (*p == '(') {
            paren = 1;
            ++p;
            while (*p && isspace((unsigned char)*p)) {
                ++p;
            }
        }

        const char *numberStart = p;
        int numberBase = 0;
        if (*numberStart == '$') {
            numberBase = 16;
            ++numberStart;
        }

        errno = 0;
        char *end = NULL;
        unsigned long long number = strtoull(numberStart, &end, numberBase);
        if (errno == 0 && end && end != numberStart) {
            const char *q = end;
            while (*q && isspace((unsigned char)*q)) {
                ++q;
            }
            if (paren) {
                if (*q != ')') {
                    q = NULL;
                } else {
                    ++q;
                    while (*q && isspace((unsigned char)*q)) {
                        ++q;
                    }
                }
            }
            if (q && *q == '\0') {
                if (deref) {
                    uint32_t addr = (uint32_t)number & 0x00ffffffu;
                    size_t sizeBytes = sizeBitsOpt ? (size_t)(sizeBitsOpt / 8u) : 4u;
                    uint32_t val = 0u;
                    if (!console_cmd_readMemoryValueBe(addr, sizeBytes, &val)) {
                        debug_error("print: failed to read memory at 0x%06X", (unsigned)addr);
                        return 0;
                    }
                    debug_printf("*0x%06X: %u (0x%X) [%u bits]\n",
                                 (unsigned)addr,
                                 (unsigned)val,
                                 (unsigned)val,
                                 (unsigned)(sizeBytes * 8u));
                    return 1;
                }
                debug_printf("%s: %llu (0x%llX)\n", expr,
                             (unsigned long long)number,
                             (unsigned long long)number);
                return 1;
            }
        }
    }

    if (sizeBitsOpt != 0u) {
        uint32_t addr = 0u;
        size_t resolvedSizeBytes = 0u;
        if (!print_eval_resolveAddress(expr, &addr, &resolvedSizeBytes)) {
            debug_error("print: size override requires an address expression");
            return 0;
        }
        size_t sizeBytes = (size_t)(sizeBitsOpt / 8u);
        uint32_t val = 0u;
        if (!console_cmd_readMemoryValueBe(addr, sizeBytes, &val)) {
            debug_error("print: failed to read memory at 0x%06X", (unsigned)(addr & 0x00ffffffu));
            return 0;
        }
        (void)resolvedSizeBytes;
        debug_printf("%s: %u (0x%X) [%u bits]\n", expr, (unsigned)val, (unsigned)val, (unsigned)sizeBitsOpt);
        return 1;
    }

    return print_eval_print(expr) ? 1 : 0;
}

static int
console_cmd_completeBreakFromObjdump(const char *prefix,
                                     const char *elf,
                                     const char *objdumpExe,
                                     const char *objdumpArgs,
                                     char ***out_list,
                                     int *out_count)
{
    if (!elf || !*elf || !objdumpExe || !*objdumpExe || !objdumpArgs || !*objdumpArgs || !out_list || !out_count) {
        return 0;
    }
    const char *matchPrefix = prefix ? prefix : "";
    size_t matchPrefixLen = strlen(matchPrefix);

    char cmd[PATH_MAX * 2];
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), objdumpExe, objdumpArgs, elf, 0)) {
        return 0;
    }

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return 0;
    }

    console_completion_t list = {0};
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        char *tokens[8];
        int count = 0;
        char *cursor = line;
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
        if (count < 2) {
            continue;
        }
        const char *name = tokens[count - 1];
        if (matchPrefixLen > 0 && strncmp(name, matchPrefix, matchPrefixLen) != 0) {
            continue;
        }
        console_cmd_completionAdd(&list, name);
    }
    pclose(fp);

    if (list.count == 0) {
        alloc_free(list.items);
        return 0;
    }

    *out_list = list.items;
    *out_count = list.count;
    return 1;
}

static int
console_cmd_completeBreak(const char *prefix, char ***out_list, int *out_count)
{
    if (out_list) {
        *out_list = NULL;
    }
    if (out_count) {
        *out_count = 0;
    }
    if (!out_list || !out_count) {
        return 0;
    }
    const char *completionPrefix = prefix ? prefix : "";

    if (print_eval_complete(completionPrefix, out_list, out_count)) {
        if (*out_count > 0) {
            return 1;
        }
        print_eval_freeCompletions(*out_list, *out_count);
        *out_list = NULL;
        *out_count = 0;
    }

    const char *elf = debugger.libretro.exePath;
    if (!elf || !*elf) {
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

    if (console_cmd_completeBreakFromObjdump(completionPrefix, elf, objdumpExe, "--syms", out_list, out_count)) {
        return 1;
    }

    return console_cmd_completeBreakFromObjdump(completionPrefix, elf, objdumpExe, "-t", out_list, out_count);
}

static int
console_cmd_completeTransition(const char *prefix, char ***out_list, int *out_count)
{
    const char *options[] = { "slide", "explode", "doom", "flip", "rbar", "random", "cycle", "none" };
    console_completion_t list = {0};
    for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); ++i) {
        const char *opt = options[i];
        if (!prefix || !*prefix) {
            console_cmd_completionAdd(&list, opt);
        } else if (strncasecmp(opt, prefix, strlen(prefix)) == 0) {
            console_cmd_completionAdd(&list, opt);
        }
    }
    *out_list = list.items;
    *out_count = list.count;
    return list.count > 0;
}

static int
console_cmd_completePrint(const char *prefix, char ***outList, int *outCount)
{
    return print_eval_complete(prefix, outList, outCount);
}

static int
console_cmd_completeWrite(const char *prefix, char ***outList, int *outCount)
{
    if (!prefix || !*prefix) {
        return 0;
    }
    if (prefix[0] == '0' && (prefix[1] == 'x' || prefix[1] == 'X')) {
        return 0;
    }
    return print_eval_complete(prefix, outList, outCount);
}

static int
console_cmd_completeCommands(const char *prefix, char ***out_list, int *out_count)
{
    if (out_list) {
        *out_list = NULL;
    }
    if (out_count) {
        *out_count = 0;
    }
    if (!out_list || !out_count) {
        return 0;
    }
    console_completion_t list = {0};
    for (size_t i = 0; i < sizeof(console_cmd) / sizeof(console_cmd[0]); ++i) {
        const console_cmd_entry_t *cmd = &console_cmd[i];
        if (!cmd->name) {
            continue;
        }
        if (prefix && *prefix && strncmp(cmd->name, prefix, strlen(prefix)) != 0) {
            continue;
        }
        console_cmd_completionAdd(&list, cmd->name);
        if (cmd->shortcut) {
            if (!prefix || !*prefix || strncmp(cmd->shortcut, prefix, strlen(prefix)) == 0) {
                console_cmd_completionAdd(&list, cmd->shortcut);
            }
        }
    }
    if (list.count == 0) {
        alloc_free(list.items);
        return 0;
    }
    *out_list = list.items;
    *out_count = list.count;
    return 1;
}

int
console_cmd_complete(const char *line, int cursor, char ***out_list, int *out_count, int *out_prefix_pos)
{
    if (out_list) {
        *out_list = NULL;
    }
    if (out_count) {
        *out_count = 0;
    }
    if (out_prefix_pos) {
        *out_prefix_pos = 0;
    }
    if (!line || !out_list || !out_count || !out_prefix_pos) {
        return 0;
    }
    int len = (int)strlen(line);
    if (cursor < 0) {
        cursor = 0;
    }
    if (cursor > len) {
        cursor = len;
    }
    int token_start = cursor;
    while (token_start > 0 && !isspace((unsigned char)line[token_start - 1])) {
        token_start--;
    }
    int token_len = cursor - token_start;
    int cmd_start = 0;
    while (cmd_start < len && isspace((unsigned char)line[cmd_start])) {
        cmd_start++;
    }
    int cmd_end = cmd_start;
    while (cmd_end < len && !isspace((unsigned char)line[cmd_end])) {
        cmd_end++;
    }
    if (cursor <= cmd_end) {
        *out_prefix_pos = token_start;
        char prefix[256];
        if (token_len >= (int)sizeof(prefix)) {
            token_len = (int)sizeof(prefix) - 1;
        }
        memcpy(prefix, line + token_start, (size_t)token_len);
        prefix[token_len] = '\0';
        return console_cmd_completeCommands(prefix, out_list, out_count);
    }
    char cmd_buf[64];
    int cmd_len = cmd_end - cmd_start;
    if (cmd_len <= 0 || cmd_len >= (int)sizeof(cmd_buf)) {
        return 0;
    }
    memcpy(cmd_buf, line + cmd_start, (size_t)cmd_len);
    cmd_buf[cmd_len] = '\0';
    const console_cmd_entry_t *cmd = console_cmd_find(cmd_buf);
    if (!cmd || !cmd->complete) {
        return 0;
    }
    *out_prefix_pos = token_start;
    char prefix[256];
    if (token_len >= (int)sizeof(prefix)) {
        token_len = (int)sizeof(prefix) - 1;
    }
    memcpy(prefix, line + token_start, (size_t)token_len);
    prefix[token_len] = '\0';
    return cmd->complete(prefix, out_list, out_count);
}

void
console_cmd_freeCompletions(char **list, int count)
{
    if (!list || count <= 0) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        alloc_free(list[i]);
    }
    alloc_free(list);
}

void
console_cmd_sendLine(const char *s)
{
    if (!s) {
        return;
    }
    char buf[1024];
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *argv[16];
    int argc = console_cmd_tokenize(buf, argv, (int)(sizeof(argv) / sizeof(argv[0])));
    if (argc <= 0) {
        return;
    }
    const console_cmd_entry_t *cmd = console_cmd_find(argv[0]);
    if (!cmd) {
        debug_error("console: unknown command '%s'", argv[0]);
        return;
    }
    cmd->handler(argc, argv);
}

void
console_cmd_sendInterrupt(void)
{
    if (libretro_host_debugPause()) {
        machine_setRunning(&debugger.machine, 0);
        debugger_clearFrameStep();
        return;
    }
    debug_error("console: interrupt failed");
}

static int
console_cmd_diffReadBytes(uint64_t frame, uint32_t base, uint8_t *out, size_t size)
{
    if (!state_buffer_restoreFrameNo(frame)) {
        return 0;
    }
    if (!libretro_host_debugReadMemory(base, out, size)) {
        return 0;
    }
    return 1;
}

static uint32_t
console_cmd_diffReadValueBE(const uint8_t *p, size_t size)
{
    if (size == 1) {
        return (uint32_t)p[0];
    }
    if (size == 2) {
        return ((uint32_t)p[0] << 8) | (uint32_t)p[1];
    }
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int
console_cmd_diff(int argc, char **argv)
{
    if (argc < 3) {
        debug_error("diff: usage: diff <fromFrame> <toFrame> [size=8|16|32]");
        return 0;
    }
    uint64_t from = 0;
    uint64_t to = 0;
    if (!console_cmd_parseU64Dec(argv[1], &from)) {
        debug_error("diff: invalid fromFrame (decimal expected)");
        return 0;
    }
    if (!console_cmd_parseU64Dec(argv[2], &to)) {
        debug_error("diff: invalid toFrame (decimal expected)");
        return 0;
    }
    size_t accessSize = 1;
    for (int i = 3; i < argc; ++i) {
        const char *tok = argv[i];
        if (!tok || !*tok) {
            continue;
        }
        if (strncmp(tok, "size=", 5) == 0) {
            uint32_t bits = 0;
            if (!console_cmd_parseU32Auto(tok + 5, &bits)) {
                debug_error("diff: invalid size");
                return 0;
            }
            if (bits == 8) {
                accessSize = 1;
            } else if (bits == 16) {
                accessSize = 2;
            } else if (bits == 32) {
                accessSize = 4;
            } else {
                debug_error("diff: size must be 8, 16, or 32");
                return 0;
            }
            continue;
        }
        debug_error("diff: unknown option '%s'", tok);
        return 0;
    }

    if (!state_buffer_hasFrameNo(from)) {
        debug_error("diff: frame %llu not in state buffer", (unsigned long long)from);
        return 0;
    }
    if (!state_buffer_hasFrameNo(to)) {
        debug_error("diff: frame %llu not in state buffer", (unsigned long long)to);
        return 0;
    }

    const uint64_t restoreFrame = state_buffer_getCurrentFrameNo();

    // Neo E9k main + backup RAM (68k map). Keep it simple; we can extend later.
    struct {
        uint32_t base;
        size_t size;
        const char *name;
    } regions[] = {
        { 0x00100000u, 0x10000u, "ram" },
        { 0x00d00000u, 0x10000u, "backup" }
    };

    enum { kMaxLines = 4096 };
    unsigned long long changed = 0;
    unsigned long long printed = 0;

    debug_printf("diff: frames %llu -> %llu (size=%u)\n",
                 (unsigned long long)from, (unsigned long long)to,
                 (unsigned)(accessSize * 8));

    for (size_t r = 0; r < sizeof(regions) / sizeof(regions[0]); ++r) {
        const uint32_t base = regions[r].base;
        const size_t size = regions[r].size;
        uint8_t *a = (uint8_t*)malloc(size);
        uint8_t *b = (uint8_t*)malloc(size);
        if (!a || !b) {
            free(a);
            free(b);
            debug_error("diff: out of memory");
            (void)state_buffer_restoreFrameNo(restoreFrame);
            debugger.frameCounter = restoreFrame;
            state_buffer_setCurrentFrameNo(restoreFrame);
            return 0;
        }

        if (!console_cmd_diffReadBytes(from, base, a, size) ||
            !console_cmd_diffReadBytes(to, base, b, size)) {
            free(a);
            free(b);
            debug_error("diff: failed to read %s memory", regions[r].name);
            (void)state_buffer_restoreFrameNo(restoreFrame);
            debugger.frameCounter = restoreFrame;
            state_buffer_setCurrentFrameNo(restoreFrame);
            return 0;
        }

        for (size_t off = 0; off + accessSize <= size; off += accessSize) {
            uint32_t va = console_cmd_diffReadValueBE(a + off, accessSize);
            uint32_t vb = console_cmd_diffReadValueBE(b + off, accessSize);
            if (va == vb) {
                continue;
            }
            changed++;
            if (printed < kMaxLines) {
                unsigned digits = (unsigned)(accessSize * 2);
                long long delta = (long long)((long long)vb - (long long)va);
                debug_printf("0x%06X: 0x%0*X -> 0x%0*X  delta=%+lld\n",
                             (unsigned)((base + (uint32_t)off) & 0x00ffffffu),
                             (int)digits, (unsigned)va,
                             (int)digits, (unsigned)vb,
                             delta);
                printed++;
            }
        }

        free(a);
        free(b);
    }

    if (changed == 0) {
        debug_printf("diff: no changes\n");
    } else if (changed > printed) {
        debug_printf("diff: %llu changes (showing %llu, truncated)\n",
                     changed, printed);
    } else {
        debug_printf("diff: %llu changes\n", changed);
    }

    (void)state_buffer_restoreFrameNo(restoreFrame);
    debugger.frameCounter = restoreFrame;
    state_buffer_setCurrentFrameNo(restoreFrame);
    return 1;
}
