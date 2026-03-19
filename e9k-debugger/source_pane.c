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
#include <unistd.h>
#include <limits.h>
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>

#include "e9ui.h"
#include "e9ui_scrollbar.h"
#include "e9ui_step_buttons.h"
#include "config.h"
#include "debugger.h"
#include "source.h"
#include "source_pane.h"
#include "source_pane_internal.h"
#include "source_cpr.h"
#include "dasm.h"
#include "addr2line.h"
#include "machine.h"
#include "base_map.h"
#include "breakpoints.h"
#include "libretro_host.h"
#include "debug.h"
#include "file.h"
#include "strutil.h"
#include "syntax_highlight.h"
#include "syntax_highlight_asm.h"
#include "print_eval.h"
#include "hunk_fileline_cache.h"

/* Set to 0 to restore the old snapshot-while-running behavior for ASM/HEX. */
static int source_pane_enableLiveAsmViews = 0;

static void
source_pane_updateSourceLocation(source_pane_state_t *st, int allowWhileRunning);

static void
source_pane_followCurrent(source_pane_state_t *st);

void
source_pane_freeFrozenAsm(source_pane_state_t *st);

static void
source_pane_setModeInternal(e9ui_component_t *comp, source_pane_mode_t mode, int enforceElfValid);

static void
source_pane_refreshSourceFiles(e9ui_component_t *comp, source_pane_state_t *st);

static void
source_pane_refreshSourceFunctions(e9ui_component_t *comp, source_pane_state_t *st, const char *source_file);

static void
source_pane_syncFileSelect(e9ui_component_t *comp, source_pane_state_t *st);

static void
source_pane_syncFunctionSelect(e9ui_component_t *comp, source_pane_state_t *st);

static void
source_pane_clearFunctionScrollLock(source_pane_state_t *st);

static void
source_pane_trackCurrentFunction(e9ui_component_t *comp, source_pane_state_t *st, const char *path, int line);

static void
source_pane_lockToggle(e9ui_context_t *ctx, void *user);

static void
source_pane_syncLockButtonVisual(source_pane_state_t *st);

static void
source_pane_asmSymbolSelectChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *value, void *user);

static void
source_pane_asmAddressSubmitted(e9ui_context_t *ctx, void *user);

static void
source_pane_clearAsmSymbols(source_pane_state_t *st);

static int
source_pane_addAsmSymbol(source_pane_state_t *st, const char *name, uint64_t addr, const char *valueOverride);

static int
source_pane_collectAsmSymbols(source_pane_state_t *st, const char *elf_path);

static int
source_pane_collectObjdumpTextAsmSymbols(source_pane_state_t *st, const char *elf_path);

static int
source_pane_collectObjdumpTextFiles(source_pane_state_t *st, const char *elf_path);

static void
source_pane_refreshAsmSymbols(e9ui_component_t *comp, source_pane_state_t *st);

static void
source_pane_setAsmAnchorLocked(source_pane_state_t *st, uint64_t addr);

static uint64_t
source_pane_resolveAsmAnchorAddr(uint64_t addr);

static void
source_pane_searchClose(source_pane_state_t *st, e9ui_context_t *ctx);

static int
source_pane_searchFind(source_pane_state_t *st, e9ui_component_t *self, e9ui_context_t *ctx, int direction, int advance);

static void
source_pane_searchTextChanged(e9ui_context_t *ctx, void *user);

static int
source_pane_searchTextboxKey(e9ui_context_t *ctx, SDL_Keycode key, SDL_Keymod mods, void *user);

static void
source_pane_searchOpen(source_pane_state_t *st, e9ui_context_t *ctx);

static void
source_pane_adjustScroll(source_pane_state_t *st, source_pane_mode_t mode, int delta);

source_pane_line_metrics_t
source_pane_computeLineMetrics(e9ui_component_t *self, e9ui_context_t *ctx, TTF_Font *font, int padPx);

TTF_Font *
source_pane_resolveFont(const e9ui_context_t *ctx);

SDL_Rect
source_pane_getContentArea(e9ui_component_t *self, e9ui_context_t *ctx, int padPx);

static int
source_pane_isAsmLikeMode(source_pane_mode_t mode);

static int
source_pane_isAsmViewLiveUpdateEnabled(void);

int
source_pane_shouldFreezeAsmWhileRunning(const source_pane_state_t *st);

static int
source_pane_areAsmViewStepButtonsEnabled(const source_pane_state_t *st);

static int
source_pane_isCpuAsmLikeMode(source_pane_mode_t mode);

static uint64_t
source_pane_resolveAsmLikeAnchorAddr(source_pane_state_t *st, uint64_t addr);

void
source_pane_inlineEditCancel(source_pane_state_t *st, e9ui_context_t *ctx);

static int
source_pane_inlineEditCommit(source_pane_state_t *st, e9ui_context_t *ctx);

static void
source_pane_inlineEditSubmitted(e9ui_context_t *ctx, void *user);

static int
source_pane_inlineEditKey(e9ui_context_t *ctx, SDL_Keycode key, SDL_Keymod mods, void *user);

static int
source_pane_getCScrollbarModel(e9ui_component_t *self,
                               e9ui_context_t *ctx,
                               source_pane_state_t *st,
                               int *outTotalLines,
                               int *outVisibleLines,
                               int *outTopIndex)
{
    if (!self || !ctx || !st || !outTotalLines || !outVisibleLines || !outTopIndex) {
        return 0;
    }
    if (st->viewMode != source_pane_mode_c) {
        return 0;
    }
    TTF_Font *useFont = source_pane_resolveFont(ctx);
    if (!useFont) {
        return 0;
    }
    const int padPx = 10;

    source_pane_updateSourceLocation(st, 0);

    int manualView = st->manualSrcActive && st->manualSrcPath;
    const char *path = manualView ? st->manualSrcPath : st->curSrcPath;
    int curLine = manualView ? 0 : st->curSrcLine;
    if (!path || !*path || (!manualView && curLine <= 0)) {
        return 0;
    }

    source_pane_line_metrics_t metrics = source_pane_computeLineMetrics(self, ctx, useFont, padPx);
    if (metrics.innerHeight <= 0) {
        return 0;
    }
    int maxLines = metrics.maxLines;
    if (maxLines <= 0) {
        maxLines = 1;
    }
    int drawMaxLines = maxLines + 1;
    int start = 1;
    if (!manualView) {
        start = curLine - (maxLines / 2);
        if (start < 1) {
            start = 1;
        }
    }
    if (st->scrollLocked) {
        start = st->scrollLine;
        if (start < 1) {
            start = 1;
        }
    }
    int end = start + drawMaxLines - 1;

    const char **lines = NULL;
    int count = 0;
    int first = 0;
    int total = 0;
    if (!source_getRange(path, start, end, &lines, &count, &first, &total)) {
        return 0;
    }
    if (count < drawMaxLines && total > 0) {
        int missing = drawMaxLines - count;
        int altStart = first - missing;
        if (altStart < 1) {
            altStart = 1;
        }
        int altEnd = altStart + drawMaxLines - 1;
        if (altEnd > total) {
            altEnd = total;
        }
        (void)source_getRange(path, altStart, altEnd, &lines, &count, &first, &total);
    }
    (void)lines;

    *outTotalLines = total > 0 ? total : count;
    *outVisibleLines = maxLines;
    *outTopIndex = first > 0 ? (first - 1) : 0;
    return 1;
}

static int
source_pane_overlayTopRowHeight(e9ui_component_t *self, e9ui_context_t *ctx, source_pane_state_t *st)
{
    int rowW = self ? self->bounds.w : 0;
    int rowH = e9ui_scale_px(ctx, 30);
    if (rowH < 1) {
        rowH = 1;
    }
    if (st && st->toggleBtnMeta) {
        e9ui_component_t *overlay = e9ui_child_find(self, st->toggleBtnMeta);
        if (overlay && overlay->preferredHeight) {
            int ph = overlay->preferredHeight(overlay, ctx, rowW);
            if (ph > rowH) {
                rowH = ph;
            }
        }
    }
    if (st && st->lockBtnMeta) {
        e9ui_component_t *lockButton = e9ui_child_find(self, st->lockBtnMeta);
        if (lockButton) {
            int lockMeasureH = 0;
            e9ui_button_measure(lockButton, ctx, NULL, &lockMeasureH);
            if (lockMeasureH > rowH) {
                rowH = lockMeasureH;
            }
        }
    }
    return rowH;
}

static int
source_pane_stepButtonPageAmount(e9ui_component_t *self, e9ui_context_t *ctx)
{
    TTF_Font *useFont = source_pane_resolveFont(ctx);
    int maxLines = 1;
    if (useFont) {
        source_pane_line_metrics_t metrics = source_pane_computeLineMetrics(self, ctx, useFont, 10);
        if (metrics.maxLines > 0) {
            maxLines = metrics.maxLines;
        }
    }
    if (maxLines <= 0) {
        maxLines = 1;
    }
    return maxLines;
}

typedef struct source_pane_step_buttons_action_ctx {
    e9ui_component_t *self;
    e9ui_context_t *ctx;
    source_pane_state_t *st;
    source_pane_mode_t mode;
} source_pane_step_buttons_action_ctx_t;

static int
source_pane_stepButtonsOnAction(void *user, e9ui_step_buttons_action_t action)
{
    source_pane_step_buttons_action_ctx_t *actionCtx = (source_pane_step_buttons_action_ctx_t*)user;
    if (!actionCtx || !actionCtx->self || !actionCtx->ctx || !actionCtx->st) {
        return 0;
    }
    int delta = 0;
    switch (action) {
    case e9ui_step_buttons_action_page_up:
        delta = -source_pane_stepButtonPageAmount(actionCtx->self, actionCtx->ctx);
        break;
    case e9ui_step_buttons_action_line_up:
        delta = -1;
        break;
    case e9ui_step_buttons_action_line_down:
        delta = 1;
        break;
    case e9ui_step_buttons_action_page_down:
        delta = source_pane_stepButtonPageAmount(actionCtx->self, actionCtx->ctx);
        break;
    default:
        break;
    }
    if (delta == 0) {
        return 0;
    }
    source_pane_adjustScroll(actionCtx->st, actionCtx->mode, delta);
    return 1;
}

static int
source_pane_isAsmLikeMode(source_pane_mode_t mode)
{
    return (mode == source_pane_mode_a ||
            mode == source_pane_mode_h ||
            mode == source_pane_mode_cpr) ? 1 : 0;
}

static int
source_pane_isAsmViewLiveUpdateEnabled(void)
{
    return source_pane_enableLiveAsmViews ? 1 : 0;
}

int
source_pane_shouldFreezeAsmWhileRunning(const source_pane_state_t *st)
{
    if (!st) {
        return 0;
    }
    if (st->viewMode == source_pane_mode_cpr) {
        return 0;
    }
    if (source_pane_isAsmViewLiveUpdateEnabled()) {
        return 0;
    }
    if (st->overrideActive) {
        return 0;
    }
    return machine_getRunning(debugger.machine) ? 1 : 0;
}

static int
source_pane_areAsmViewStepButtonsEnabled(const source_pane_state_t *st)
{
    if (!st || !source_pane_isAsmLikeMode(st->viewMode)) {
        return 0;
    }
    return 1;
}

static int
source_pane_isCpuAsmLikeMode(source_pane_mode_t mode)
{
    return (mode == source_pane_mode_a ||
            mode == source_pane_mode_h) ? 1 : 0;
}

static const char *
source_pane_basename(const char *path)
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
source_pane_isAbsolutePath(const char *path)
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

static void
source_pane_copyPath(char *out, size_t out_cap, const char *path)
{
    if (!out || out_cap == 0) {
        return;
    }
    out[0] = '\0';
    if (!path || !path[0]) {
        return;
    }
    strutil_strlcpy(out, out_cap, path);
}

static int
source_pane_pathExistsFile(const char *path)
{
    if (!path || !path[0]) {
        return 0;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    fclose(f);
    return 1;
}

static void
source_pane_resolveSourcePath(const char *path, char *out, size_t out_cap)
{
    if (!out || out_cap == 0) {
        return;
    }
    out[0] = '\0';
    if (!path || !path[0]) {
        return;
    }
    const char *src = debugger.libretro.sourceDir;
    if (source_pane_isAbsolutePath(path)) {
        if (source_pane_pathExistsFile(path) || !src || !src[0]) {
            source_pane_copyPath(out, out_cap, path);
            return;
        }

        const char *base = source_pane_basename(path);
        if (base && base[0]) {
            char candidate[PATH_MAX];
            strutil_pathJoinTrunc(candidate, sizeof(candidate), src, base);
            if (source_pane_pathExistsFile(candidate)) {
                source_pane_copyPath(out, out_cap, candidate);
                return;
            }
        }
        source_pane_copyPath(out, out_cap, path);
        return;
    }
    if (!src || !src[0]) {
        source_pane_copyPath(out, out_cap, path);
        return;
    }
    strutil_pathJoinTrunc(out, out_cap, src, path);
}

static int
source_pane_parseHex(const char *s, uint32_t *out)
{
    if (!s || !*s || !out) {
        return 0;
    }
    char buf[32];
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        len--;
    }
    if (len > 0 && s[len - 1] == ':') {
        len--;
    }
    if (len == 0 || len >= sizeof(buf)) {
        return 0;
    }
    memcpy(buf, s, len);
    buf[len] = '\0';
    const char *p = buf;
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
source_pane_parseHex64(const char *s, uint64_t *out)
{
    if (!s || !*s || !out) {
        return 0;
    }
    char buf[32];
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        len--;
    }
    if (len > 0 && s[len - 1] == ':') {
        len--;
    }
    if (len == 0 || len >= sizeof(buf)) {
        return 0;
    }
    memcpy(buf, s, len);
    buf[len] = '\0';
    const char *p = buf;
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
    unsigned long long v = strtoull(p, NULL, 16);
    if (errno != 0) {
        return 0;
    }
    *out = (uint64_t)v;
    return 1;
}

static const char *
source_pane_findTextInsensitive(const char *text, const char *needle, int start_col)
{
    if (!text || !needle) {
        return NULL;
    }
    int needle_len = (int)strlen(needle);
    if (needle_len <= 0) {
        return NULL;
    }
    int text_len = (int)strlen(text);
    if (start_col < 0) {
        start_col = 0;
    }
    for (int i = start_col; i + needle_len <= text_len; ++i) {
        int match = 1;
        for (int j = 0; j < needle_len; ++j) {
            int a = tolower((unsigned char)text[i + j]);
            int b = tolower((unsigned char)needle[j]);
            if (a != b) {
                match = 0;
                break;
            }
        }
        if (match) {
            return text + i;
        }
    }
    return NULL;
}

static int
source_pane_findTextInsensitiveReverse(const char *text, const char *needle, int start_col)
{
    if (!text || !needle) {
        return -1;
    }
    int needle_len = (int)strlen(needle);
    if (needle_len <= 0) {
        return -1;
    }
    int text_len = (int)strlen(text);
    if (text_len < needle_len) {
        return -1;
    }
    int max_col = text_len - needle_len;
    if (start_col > max_col) {
        start_col = max_col;
    }
    for (int i = start_col; i >= 0; --i) {
        int match = 1;
        for (int j = 0; j < needle_len; ++j) {
            int a = tolower((unsigned char)text[i + j]);
            int b = tolower((unsigned char)needle[j]);
            if (a != b) {
                match = 0;
                break;
            }
        }
        if (match) {
            return i;
        }
    }
    return -1;
}

static int
source_pane_fileMatches(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    if (strcmp(a, b) == 0) {
        return 1;
    }
    const char *ba = source_pane_basename(a);
    const char *bb = source_pane_basename(b);
    if (ba && bb && strcmp(ba, bb) == 0) {
        return 1;
    }
    const char *src = debugger.libretro.sourceDir;
    if (src && *src) {
        size_t src_len = strlen(src);
        if (strncmp(a, src, src_len) == 0) {
            const char *rest = a + src_len;
            if (*rest == '/' || *rest == '\\') {
                rest++;
            }
            if (strcmp(rest, b) == 0) {
                return 1;
            }
        }
        if (strncmp(b, src, src_len) == 0) {
            const char *rest = b + src_len;
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

static int
source_pane_hasCSourceExtension(const char *path)
{
    if (!path || !*path) {
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
    return strcmp(dot, ".c") == 0 || strcmp(dot, ".cc") == 0 ||
           strcmp(dot, ".cpp") == 0 || strcmp(dot, ".cxx") == 0;
}

static char *
source_pane_parseValueAfterColon(const char *line)
{
    if (!line) {
        return NULL;
    }
    const char *colon = strrchr(line, ':');
    if (!colon || !colon[1]) {
        return NULL;
    }
    const char *start = colon + 1;
    while (*start && isspace((unsigned char)*start)) {
        start++;
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

static void
source_pane_clearSourceFiles(source_pane_state_t *st)
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

static void
source_pane_clearSourceFunctions(source_pane_state_t *st)
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

static void
source_pane_clearAsmSymbols(source_pane_state_t *st)
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
    st->asmSymbolsElf[0] = '\0';
    st->asmSymbolsToolchain[0] = '\0';
}

static int
source_pane_parseFunctionValue(const char *value, int *out_line, const char **out_file)
{
    if (!value || !*value || !out_line) {
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
    char line_buf[16];
    memcpy(line_buf, value, len);
    line_buf[len] = '\0';
    int line = (int)strtol(line_buf, NULL, 10);
    if (line <= 0) {
        return 0;
    }
    *out_line = line;
    if (out_file) {
        const char *file = sep + 1;
        *out_file = strchr(file, '|') ? file : NULL;
    }
    return 1;
}

static int
source_pane_addSourceFunction(source_pane_state_t *st, const char *filePath, const char *name, int line)
{
    if (!st || !filePath || !*filePath || !name || !*name || line <= 0) {
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
        int next_cap = st->sourceFunctionCap > 0 ? st->sourceFunctionCap * 2 : 64;
        char **next_names = (char**)alloc_calloc((size_t)next_cap, sizeof(*next_names));
        char **next_files = (char**)alloc_calloc((size_t)next_cap, sizeof(*next_files));
        char **next_labels = (char**)alloc_calloc((size_t)next_cap, sizeof(*next_labels));
        char **next_values = (char**)alloc_calloc((size_t)next_cap, sizeof(*next_values));
        int *next_lines = (int*)alloc_calloc((size_t)next_cap, sizeof(*next_lines));
        e9ui_textbox_option_t *next_options =
            (e9ui_textbox_option_t*)alloc_calloc((size_t)next_cap, sizeof(*next_options));
        if (!next_names || !next_files || !next_labels || !next_values || !next_lines || !next_options) {
            alloc_free(next_names);
            alloc_free(next_files);
            alloc_free(next_labels);
            alloc_free(next_values);
            alloc_free(next_lines);
            alloc_free(next_options);
            return 0;
        }
        if (st->sourceFunctionCount > 0) {
            size_t count = (size_t)st->sourceFunctionCount;
            memcpy(next_names, st->sourceFunctionNames, sizeof(*next_names) * count);
            memcpy(next_files, st->sourceFunctionFiles, sizeof(*next_files) * count);
            memcpy(next_labels, st->sourceFunctionLabels, sizeof(*next_labels) * count);
            memcpy(next_values, st->sourceFunctionValues, sizeof(*next_values) * count);
            memcpy(next_lines, st->sourceFunctionLines, sizeof(*next_lines) * count);
            memcpy(next_options, st->sourceFunctionOptions, sizeof(*next_options) * count);
        }
        alloc_free(st->sourceFunctionNames);
        alloc_free(st->sourceFunctionFiles);
        alloc_free(st->sourceFunctionLabels);
        alloc_free(st->sourceFunctionValues);
        alloc_free(st->sourceFunctionLines);
        alloc_free(st->sourceFunctionOptions);
        st->sourceFunctionNames = next_names;
        st->sourceFunctionFiles = next_files;
        st->sourceFunctionLabels = next_labels;
        st->sourceFunctionValues = next_values;
        st->sourceFunctionLines = next_lines;
        st->sourceFunctionOptions = next_options;
        st->sourceFunctionCap = next_cap;
    }

    char value_buf[PATH_MAX + 64];
    snprintf(value_buf, sizeof(value_buf), "%d|%s|%s", line, filePath, name);
    char *name_dup = alloc_strdup(name);
    char *file_dup = alloc_strdup(filePath);
    char *label_dup = alloc_strdup(name);
    char *value_dup = alloc_strdup(value_buf);
    if (!name_dup || !file_dup || !label_dup || !value_dup) {
        alloc_free(name_dup);
        alloc_free(file_dup);
        alloc_free(label_dup);
        alloc_free(value_dup);
        return 0;
    }

    int insert_at = st->sourceFunctionCount;
    while (insert_at > 0) {
        int prev = insert_at - 1;
        int prev_line = st->sourceFunctionLines[prev];
        int cmp = strcasecmp(st->sourceFunctionNames[prev], name_dup);
        if (cmp < 0 || (cmp == 0 && prev_line <= line)) {
            break;
        }
        st->sourceFunctionNames[insert_at] = st->sourceFunctionNames[prev];
        st->sourceFunctionFiles[insert_at] = st->sourceFunctionFiles[prev];
        st->sourceFunctionLabels[insert_at] = st->sourceFunctionLabels[prev];
        st->sourceFunctionValues[insert_at] = st->sourceFunctionValues[prev];
        st->sourceFunctionLines[insert_at] = st->sourceFunctionLines[prev];
        st->sourceFunctionOptions[insert_at] = st->sourceFunctionOptions[prev];
        insert_at--;
    }

    st->sourceFunctionNames[insert_at] = name_dup;
    st->sourceFunctionFiles[insert_at] = file_dup;
    st->sourceFunctionLabels[insert_at] = label_dup;
    st->sourceFunctionValues[insert_at] = value_dup;
    st->sourceFunctionLines[insert_at] = line;
    st->sourceFunctionOptions[insert_at].value = value_dup;
    st->sourceFunctionOptions[insert_at].label = label_dup;
    st->sourceFunctionCount++;
    return 1;
}

static int
source_pane_addSourceFile(source_pane_state_t *st, const char *path)
{
    if (!st || !path || !*path) {
        return 0;
    }
    char resolved[PATH_MAX];
    source_pane_resolveSourcePath(path, resolved, sizeof(resolved));
    if (!source_pane_hasCSourceExtension(resolved)) {
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
    const char *base = source_pane_basename(resolved);
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

static void
source_pane_prependBlankSourceOption(source_pane_state_t *st)
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
source_pane_collectReadelfFiles(source_pane_state_t *st, const char *elfPath)
{
    if (!st || !elfPath || !*elfPath) {
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
                    if (cuDir[0] && !source_pane_isAbsolutePath(cuName)) {
                        snprintf(fullPath, sizeof(fullPath), "%s/%s", cuDir, cuName);
                    } else {
                        strncpy(fullPath, cuName, sizeof(fullPath) - 1);
                        fullPath[sizeof(fullPath) - 1] = '\0';
                    }
                    added += source_pane_addSourceFile(st, fullPath);
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
            char *name = source_pane_parseValueAfterColon(line);
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
            char *dir = source_pane_parseValueAfterColon(line);
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
        if (cuDir[0] && !source_pane_isAbsolutePath(cuName)) {
            snprintf(fullPath, sizeof(fullPath), "%s/%s", cuDir, cuName);
        } else {
            strncpy(fullPath, cuName, sizeof(fullPath) - 1);
            fullPath[sizeof(fullPath) - 1] = '\0';
        }
        added += source_pane_addSourceFile(st, fullPath);
    }

    pclose(fp);
    return added;
}

static int
source_pane_collectStabsFiles(source_pane_state_t *st, const char *elfPath)
{
    if (!st || !elfPath || !*elfPath) {
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
        if (!stabType || !stabStr || !*stabStr) {
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
        if (!source_pane_isAbsolutePath(stabStr) && currentDir[0]) {
            snprintf(fullPath, sizeof(fullPath), "%s%s", currentDir, stabStr);
        } else {
            strncpy(fullPath, stabStr, sizeof(fullPath) - 1);
            fullPath[sizeof(fullPath) - 1] = '\0';
        }
        added += source_pane_addSourceFile(st, fullPath);
    }
    pclose(fp);
    return added;
}

static void
source_pane_syncFileSelect(e9ui_component_t *comp, source_pane_state_t *st)
{
    if (!comp || !st || !st->fileSelectMeta) {
        return;
    }
    e9ui_component_t *select = e9ui_child_find(comp, st->fileSelectMeta);
    if (!select) {
        return;
    }
    e9ui_setHidden(select, st->viewMode == source_pane_mode_c ? 0 : 1);
    if (st->viewMode != source_pane_mode_c) {
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
    if (!displayPath || !*displayPath) {
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

static void
source_pane_syncFunctionSelect(e9ui_component_t *comp, source_pane_state_t *st)
{
    if (!comp || !st || !st->functionSelectMeta) {
        return;
    }
    e9ui_component_t *select = e9ui_child_find(comp, st->functionSelectMeta);
    if (!select) {
        return;
    }
    e9ui_setHidden(select, st->viewMode == source_pane_mode_c ? 0 : 1);
    if (st->viewMode != source_pane_mode_c) {
        return;
    }
    int editingSelect = (e9ui && e9ui_getFocus(&e9ui->ctx) == select) ? 1 : 0;
    if (!editingSelect) {
        e9ui_textbox_setOptions(select, st->sourceFunctionOptions, st->sourceFunctionCount);
    }
    select->disabled = st->sourceFunctionCount <= 0 ? 1 : 0;
}

static void
source_pane_clearFunctionScrollLock(source_pane_state_t *st)
{
    if (!st) {
        return;
    }
    st->functionScrollLock = 0;
}

static void
source_pane_trackCurrentFunction(e9ui_component_t *comp, source_pane_state_t *st, const char *path, int line)
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
source_pane_addAsmSymbol(source_pane_state_t *st, const char *name, uint64_t addr, const char *valueOverride)
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
        int next_cap = st->asmSymbolCap > 0 ? st->asmSymbolCap * 2 : 128;
        char **next_names = (char**)alloc_calloc((size_t)next_cap, sizeof(*next_names));
        char **next_labels = (char**)alloc_calloc((size_t)next_cap, sizeof(*next_labels));
        char **next_values = (char**)alloc_calloc((size_t)next_cap, sizeof(*next_values));
        uint64_t *next_addrs = (uint64_t*)alloc_calloc((size_t)next_cap, sizeof(*next_addrs));
        e9ui_textbox_option_t *next_options =
            (e9ui_textbox_option_t*)alloc_calloc((size_t)next_cap, sizeof(*next_options));
        if (!next_names || !next_labels || !next_values || !next_addrs || !next_options) {
            alloc_free(next_names);
            alloc_free(next_labels);
            alloc_free(next_values);
            alloc_free(next_addrs);
            alloc_free(next_options);
            return 0;
        }
        if (st->asmSymbolCount > 0) {
            size_t count = (size_t)st->asmSymbolCount;
            memcpy(next_names, st->asmSymbolNames, sizeof(*next_names) * count);
            memcpy(next_labels, st->asmSymbolLabels, sizeof(*next_labels) * count);
            memcpy(next_values, st->asmSymbolValues, sizeof(*next_values) * count);
            memcpy(next_addrs, st->asmSymbolAddrs, sizeof(*next_addrs) * count);
            memcpy(next_options, st->asmSymbolOptions, sizeof(*next_options) * count);
        }
        alloc_free(st->asmSymbolNames);
        alloc_free(st->asmSymbolLabels);
        alloc_free(st->asmSymbolValues);
        alloc_free(st->asmSymbolAddrs);
        alloc_free(st->asmSymbolOptions);
        st->asmSymbolNames = next_names;
        st->asmSymbolLabels = next_labels;
        st->asmSymbolValues = next_values;
        st->asmSymbolAddrs = next_addrs;
        st->asmSymbolOptions = next_options;
        st->asmSymbolCap = next_cap;
    }

    char value_buf[32];
    if (!valueOverride || !valueOverride[0]) {
        snprintf(value_buf, sizeof(value_buf), "%llX", (unsigned long long)(addr & 0x00ffffffull));
    }
    char *name_dup = alloc_strdup(name);
    char *label_dup = alloc_strdup(name);
    char *value_dup = alloc_strdup((valueOverride && valueOverride[0]) ? valueOverride : value_buf);
    if (!name_dup || !label_dup || !value_dup) {
        alloc_free(name_dup);
        alloc_free(label_dup);
        alloc_free(value_dup);
        return 0;
    }

    int insert_at = st->asmSymbolCount;
    while (insert_at > 0) {
        int prev = insert_at - 1;
        int cmp = strcasecmp(st->asmSymbolNames[prev], name_dup);
        if (cmp < 0 || (cmp == 0 && st->asmSymbolAddrs[prev] <= addr)) {
            break;
        }
        st->asmSymbolNames[insert_at] = st->asmSymbolNames[prev];
        st->asmSymbolLabels[insert_at] = st->asmSymbolLabels[prev];
        st->asmSymbolValues[insert_at] = st->asmSymbolValues[prev];
        st->asmSymbolAddrs[insert_at] = st->asmSymbolAddrs[prev];
        st->asmSymbolOptions[insert_at] = st->asmSymbolOptions[prev];
        insert_at--;
    }
    st->asmSymbolNames[insert_at] = name_dup;
    st->asmSymbolLabels[insert_at] = label_dup;
    st->asmSymbolValues[insert_at] = value_dup;
    st->asmSymbolAddrs[insert_at] = addr;
    st->asmSymbolOptions[insert_at].value = value_dup;
    st->asmSymbolOptions[insert_at].label = label_dup;
    st->asmSymbolCount++;
    return 1;
}

static int
source_pane_collectAsmSymbols(source_pane_state_t *st, const char *elf_path)
{
    if (!st || !elf_path || !elf_path[0] || !debugger.elfValid) {
        return 0;
    }

    int added = 0;
    char **completions = NULL;
    int completionCount = 0;
    if (print_eval_complete("", &completions, &completionCount)) {
        for (int i = 0; i < completionCount; ++i) {
            const char *name = completions[i];
            uint32_t addr = 0;
            size_t size = 0;
            if (!name || !name[0]) {
                continue;
            }
            if (!print_eval_resolveSymbol(name, &addr, &size)) {
                continue;
            }
            if (addr == 0) {
                continue;
            }
            added += source_pane_addAsmSymbol(st, name, (uint64_t)(addr & 0x00ffffffu), NULL);
        }
        print_eval_freeCompletions(completions, completionCount);
        completions = NULL;
        completionCount = 0;
    }

    if (st->ownerPane) {
        source_pane_refreshSourceFunctions(st->ownerPane, st, NULL);
        for (int i = 0; i < st->sourceFunctionCount; ++i) {
            const char *name = st->sourceFunctionNames ? st->sourceFunctionNames[i] : NULL;
            const char *value = st->sourceFunctionValues ? st->sourceFunctionValues[i] : NULL;
            if (!name || !name[0] || !value || !value[0]) {
                continue;
            }
            added += source_pane_addAsmSymbol(st, name, 0, value);
        }
    }

    if (added > 0) {
        return added;
    }

    if (debugger_toolchainUsesHunkAddr2line()) {
        return source_pane_collectObjdumpTextAsmSymbols(st, elf_path);
    }

    char readelf[PATH_MAX];
    if (!debugger_toolchainBuildBinary(readelf, sizeof(readelf), "readelf")) {
        return 0;
    }
    char readelf_exe[PATH_MAX];
    if (!file_findInPath(readelf, readelf_exe, sizeof(readelf_exe))) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), readelf_exe, "-Ws", elf_path, 1)) {
        return 0;
    }
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return 0;
    }

    added = 0;
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
        const char *symbol_name = tokens[7];
        if (!symbol_name || !symbol_name[0] || strcmp(symbol_name, "<null>") == 0) {
            continue;
        }
        uint64_t symbol_addr = 0;
        if (!source_pane_parseHex64(tokens[1], &symbol_addr) || symbol_addr == 0) {
            continue;
        }
        added += source_pane_addAsmSymbol(st, symbol_name, symbol_addr, NULL);
    }
    pclose(fp);
    if (added == 0) {
        added += source_pane_collectObjdumpTextAsmSymbols(st, elf_path);
    }
    return added;
}

static int
source_pane_collectObjdumpTextAsmSymbols(source_pane_state_t *st, const char *elf_path)
{
    if (!st || !elf_path || !elf_path[0] || !debugger.elfValid) {
        return 0;
    }
    char objdump[PATH_MAX];
    if (!debugger_toolchainBuildBinary(objdump, sizeof(objdump), "objdump")) {
        return 0;
    }
    char objdump_exe[PATH_MAX];
    if (!file_findInPath(objdump, objdump_exe, sizeof(objdump_exe))) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), objdump_exe, "-t", elf_path, 1)) {
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
        uint64_t symbol_addr = 0;
        if (!source_pane_parseHex64(tokens[0], &symbol_addr) || symbol_addr == 0) {
            continue;
        }
        int text_symbol = 0;
        for (int i = 1; i < count; ++i) {
            if (strcmp(tokens[i], ".text") == 0 || strncmp(tokens[i], ".text.", 6) == 0) {
                text_symbol = 1;
                break;
            }
        }
        if (!text_symbol) {
            continue;
        }
        const char *symbol_name = tokens[count - 1];
        if (!symbol_name || !symbol_name[0] || symbol_name[0] == '.') {
            continue;
        }
        added += source_pane_addAsmSymbol(st, symbol_name, symbol_addr, NULL);
    }

    pclose(fp);
    return added;
}

static void
source_pane_refreshAsmSymbols(e9ui_component_t *comp, source_pane_state_t *st)
{
    if (!comp || !st || !st->asmSymbolSelectMeta) {
        return;
    }
    e9ui_component_t *select = e9ui_child_find(comp, st->asmSymbolSelectMeta);
    if (!select) {
        return;
    }
    int asmLikeMode = source_pane_isAsmLikeMode(st->viewMode);
    e9ui_setHidden(select, asmLikeMode ? 0 : 1);
    if (!asmLikeMode) {
        return;
    }
    int editingSelect = (e9ui && e9ui_getFocus(&e9ui->ctx) == select) ? 1 : 0;

    const char *elf = debugger.libretro.exePath;
    const char *toolchain = debugger.libretro.toolchainPrefix;
    if (!elf || !elf[0] || !debugger.elfValid) {
        e9ui_textbox_setOptions(select, NULL, 0);
        source_pane_clearAsmSymbols(st);
        select->disabled = 1;
        return;
    }
    if (st->asmSymbolsLoaded &&
        strcmp(st->asmSymbolsElf, elf) == 0 &&
        strcmp(st->asmSymbolsToolchain, toolchain ? toolchain : "") == 0) {
        if (!editingSelect) {
            e9ui_textbox_setOptions(select, st->asmSymbolOptions, st->asmSymbolCount);
        }
        select->disabled = st->asmSymbolCount <= 0 ? 1 : 0;
        return;
    }

    e9ui_textbox_setOptions(select, NULL, 0);
    source_pane_clearAsmSymbols(st);
    (void)source_pane_collectAsmSymbols(st, elf);
    st->asmSymbolsLoaded = 1;
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

static void
source_pane_setAsmAnchorLocked(source_pane_state_t *st, uint64_t addr)
{
    if (!st) {
        return;
    }
    uint64_t a = source_pane_resolveAsmLikeAnchorAddr(st, addr);
    if (st->viewMode == source_pane_mode_cpr) {
        st->scrollIndex = (int)(a >> 2);
        st->scrollAnchorAddr = a;
        st->scrollAnchorValid = 1;
        st->scrollLocked = 1;
        st->gutterPending = 0;
        source_pane_syncLockButtonVisual(st);
        return;
    }
    int idx = 0;
    if (!dasm_findIndexForAddr(a, &idx)) {
        return;
    }
    idx -= 1;
    if (idx < 0 && !(dasm_getFlags() & DASM_IFACE_FLAG_STREAMING)) {
        idx = 0;
    }
    st->scrollIndex = idx;
    st->scrollAnchorAddr = 0;
    st->scrollAnchorValid = 0;
    st->scrollLocked = 1;
    st->gutterPending = 0;
    source_pane_syncLockButtonVisual(st);
}

static uint64_t
source_pane_resolveAsmAnchorAddr(uint64_t addr)
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
source_pane_resolveAsmLikeAnchorAddr(source_pane_state_t *st, uint64_t addr)
{
    if (st && st->viewMode == source_pane_mode_cpr) {
        return source_cpr_resolveAnchorAddr(addr);
    }
    return source_pane_resolveAsmAnchorAddr(addr);
}

static int
source_pane_collectFunctionSymbols(source_pane_state_t *st, const char *elf_path, const char *source_file)
{
    if (!st || !elf_path || !*elf_path || !debugger.elfValid) {
        return 0;
    }
    if (debugger_toolchainUsesHunkAddr2line()) {
        return 0;
    }

    char readelf[PATH_MAX];
    if (!debugger_toolchainBuildBinary(readelf, sizeof(readelf), "readelf")) {
        return 0;
    }
    char readelf_exe[PATH_MAX];
    if (!file_findInPath(readelf, readelf_exe, sizeof(readelf_exe))) {
        return 0;
    }

    char cmd[PATH_MAX * 2];
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), readelf_exe, "-Ws", elf_path, 1)) {
        return 0;
    }
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return 0;
    }

    int added = 0;
    char line[2048];
    char resolved_source[PATH_MAX];
    if (source_file && *source_file) {
        source_pane_resolveSourcePath(source_file, resolved_source, sizeof(resolved_source));
    } else {
        resolved_source[0] = '\0';
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
        if (strcmp(tokens[3], "FUNC") != 0) {
            continue;
        }
        if (strcmp(tokens[6], "UND") == 0) {
            continue;
        }
        const char *symbol_name = tokens[7];
        if (!symbol_name || !*symbol_name || strcmp(symbol_name, "<null>") == 0) {
            continue;
        }
        uint64_t symbol_addr = 0;
        if (!source_pane_parseHex64(tokens[1], &symbol_addr) || symbol_addr == 0) {
            continue;
        }

        char resolved_file[PATH_MAX];
        char function_name[1024];
        int function_line = 0;
        if (!addr2line_resolveDetailed(symbol_addr, resolved_file, sizeof(resolved_file),
                                       &function_line, function_name, sizeof(function_name))) {
            continue;
        }
        if (function_line <= 0 || !resolved_file[0]) {
            continue;
        }
        char resolved_path[PATH_MAX];
        source_pane_resolveSourcePath(resolved_file, resolved_path, sizeof(resolved_path));
        if (resolved_source[0] && !source_pane_fileMatches(resolved_path, resolved_source)) {
            continue;
        }
        const char *display_name = function_name[0] ? function_name : symbol_name;
        if (strcmp(display_name, "??") == 0) {
            continue;
        }
        added += source_pane_addSourceFunction(st, resolved_path, display_name, function_line);
    }

    pclose(fp);
    return added;
}

static int
source_pane_parseStabStringName(const char *stab_str, char *out_name, size_t cap)
{
    if (!stab_str || !*stab_str || !out_name || cap == 0) {
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
source_pane_collectStabsFunctions(source_pane_state_t *st, const char *elf_path, const char *source_file)
{
    if (!st || !elf_path || !*elf_path || !debugger.elfValid) {
        return 0;
    }
    if (debugger_toolchainUsesHunkAddr2line()) {
        return 0;
    }
    char objdump[PATH_MAX];
    if (!debugger_toolchainBuildBinary(objdump, sizeof(objdump), "objdump")) {
        return 0;
    }
    char objdump_exe[PATH_MAX];
    if (!file_findInPath(objdump, objdump_exe, sizeof(objdump_exe))) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), objdump_exe, "-G", elf_path, 1)) {
        return 0;
    }
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return 0;
    }

    int added = 0;
    char line[2048];
    char resolved_source[PATH_MAX];
    if (source_file && *source_file) {
        source_pane_resolveSourcePath(source_file, resolved_source, sizeof(resolved_source));
    } else {
        resolved_source[0] = '\0';
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
        const char *stab_type = tokens[1];
        const char *stab_str = tokens[count - 1];
        if (!stab_type || !stab_str || strcmp(stab_type, "FUN") != 0) {
            continue;
        }
        char parsed_name[1024];
        if (!source_pane_parseStabStringName(stab_str, parsed_name, sizeof(parsed_name))) {
            continue;
        }

        uint64_t n_value = 0;
        if (!source_pane_parseHex64(tokens[4], &n_value) || n_value == 0) {
            continue;
        }
        char resolved_file[PATH_MAX];
        char function_name[1024];
        int function_line = 0;
        if (!addr2line_resolveDetailed(n_value, resolved_file, sizeof(resolved_file),
                                       &function_line, function_name, sizeof(function_name))) {
            continue;
        }
        if (function_line <= 0 || !resolved_file[0]) {
            continue;
        }
        char resolved_path[PATH_MAX];
        source_pane_resolveSourcePath(resolved_file, resolved_path, sizeof(resolved_path));
        if (resolved_source[0] && !source_pane_fileMatches(resolved_path, resolved_source)) {
            continue;
        }
        const char *display_name = function_name[0] ? function_name : parsed_name;
        if (!display_name[0] || strcmp(display_name, "??") == 0) {
            continue;
        }
        added += source_pane_addSourceFunction(st, resolved_path, display_name, function_line);
    }

    pclose(fp);
    return added;
}

static int
source_pane_collectObjdumpTextFunctions(source_pane_state_t *st, const char *elf_path, const char *source_file)
{
    if (!st || !elf_path || !*elf_path || !debugger.elfValid) {
        return 0;
    }
    char objdump[PATH_MAX];
    if (!debugger_toolchainBuildBinary(objdump, sizeof(objdump), "objdump")) {
        return 0;
    }
    char objdump_exe[PATH_MAX];
    if (!file_findInPath(objdump, objdump_exe, sizeof(objdump_exe))) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), objdump_exe, "-t", elf_path, 1)) {
        return 0;
    }
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return 0;
    }

    int added = 0;
    char line[2048];
    char resolved_source[PATH_MAX];
    if (source_file && *source_file) {
        source_pane_resolveSourcePath(source_file, resolved_source, sizeof(resolved_source));
    } else {
        resolved_source[0] = '\0';
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
        uint64_t symbol_addr = 0;
        if (!source_pane_parseHex64(tokens[0], &symbol_addr) || symbol_addr == 0) {
            continue;
        }
        int text_symbol = 0;
        for (int i = 1; i < count; ++i) {
            if (strcmp(tokens[i], ".text") == 0 || strncmp(tokens[i], ".text.", 6) == 0) {
                text_symbol = 1;
                break;
            }
        }
        if (!text_symbol) {
            continue;
        }
        const char *symbol_name = tokens[count - 1];
        if (!symbol_name || !*symbol_name || symbol_name[0] == '.') {
            continue;
        }

        char resolved_file[PATH_MAX];
        char function_name[1024];
        int function_line = 0;
        if (!addr2line_resolveDetailed(symbol_addr, resolved_file, sizeof(resolved_file),
                                       &function_line, function_name, sizeof(function_name))) {
            continue;
        }
        if (function_line <= 0 || !resolved_file[0]) {
            continue;
        }
        char resolved_path[PATH_MAX];
        source_pane_resolveSourcePath(resolved_file, resolved_path, sizeof(resolved_path));
        if (resolved_source[0] && !source_pane_fileMatches(resolved_path, resolved_source)) {
            continue;
        }
        const char *display_name = function_name[0] ? function_name : symbol_name;
        if (!display_name[0] || strcmp(display_name, "??") == 0) {
            continue;
        }
        added += source_pane_addSourceFunction(st, resolved_path, display_name, function_line);
    }

    pclose(fp);
    return added;
}

static int
source_pane_collectObjdumpTextFiles(source_pane_state_t *st, const char *elf_path)
{
    if (!st || !elf_path || !*elf_path || !debugger.elfValid) {
        return 0;
    }
    char objdump[PATH_MAX];
    if (!debugger_toolchainBuildBinary(objdump, sizeof(objdump), "objdump")) {
        return 0;
    }
    char objdump_exe[PATH_MAX];
    if (!file_findInPath(objdump, objdump_exe, sizeof(objdump_exe))) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    if (!debugger_platform_formatToolCommand(cmd, sizeof(cmd), objdump_exe, "-t", elf_path, 1)) {
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
        uint64_t symbol_addr = 0;
        if (!source_pane_parseHex64(tokens[0], &symbol_addr) || symbol_addr == 0) {
            continue;
        }
        int text_symbol = 0;
        for (int i = 1; i < count; ++i) {
            if (strcmp(tokens[i], ".text") == 0 || strncmp(tokens[i], ".text.", 6) == 0) {
                text_symbol = 1;
                break;
            }
        }
        if (!text_symbol) {
            continue;
        }
        const char *symbol_name = tokens[count - 1];
        if (!symbol_name || !*symbol_name || symbol_name[0] == '.') {
            continue;
        }

        char resolved_file[PATH_MAX];
        int function_line = 0;
        if (!addr2line_resolve(symbol_addr, resolved_file, sizeof(resolved_file), &function_line)) {
            continue;
        }
        if (function_line <= 0 || !resolved_file[0]) {
            continue;
        }
        char resolved_path[PATH_MAX];
        source_pane_resolveSourcePath(resolved_file, resolved_path, sizeof(resolved_path));
        added += source_pane_addSourceFile(st, resolved_path);
    }

    pclose(fp);
    return added;
}

static void
source_pane_refreshSourceFunctions(e9ui_component_t *comp, source_pane_state_t *st, const char *source_file)
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
    char resolved_source_file[PATH_MAX];
    resolved_source_file[0] = '\0';
    if (source_file && *source_file) {
        source_pane_resolveSourcePath(source_file, resolved_source_file, sizeof(resolved_source_file));
    }
    if (!debugger.elfValid || !elf || !*elf) {
        if (select) {
            e9ui_textbox_setOptions(select, NULL, 0);
        }
        source_pane_clearSourceFunctions(st);
        source_pane_syncFunctionSelect(comp, st);
        return;
    }

    if (st->sourceFunctionsLoaded &&
        strcmp(st->sourceFunctionsElf, elf) == 0 &&
        strcmp(st->sourceFunctionsToolchain, toolchain ? toolchain : "") == 0 &&
        strcmp(st->sourceFunctionsFile, resolved_source_file) == 0) {
        source_pane_syncFunctionSelect(comp, st);
        return;
    }

    if (select) {
        e9ui_textbox_setOptions(select, NULL, 0);
    }
    source_pane_clearSourceFunctions(st);
    int added = source_pane_collectFunctionSymbols(st, elf, source_file);
    if (added == 0) {
        added += source_pane_collectStabsFunctions(st, elf, source_file);
    }
    if (added == 0) {
        added += source_pane_collectObjdumpTextFunctions(st, elf, source_file);
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
    if (resolved_source_file[0]) {
        strncpy(st->sourceFunctionsFile, resolved_source_file, sizeof(st->sourceFunctionsFile) - 1);
        st->sourceFunctionsFile[sizeof(st->sourceFunctionsFile) - 1] = '\0';
    } else {
        st->sourceFunctionsFile[0] = '\0';
    }
    source_pane_syncFunctionSelect(comp, st);
}

static void
source_pane_refreshSourceFiles(e9ui_component_t *comp, source_pane_state_t *st)
{
    if (!comp || !st) {
        return;
    }
    const char *elf = debugger.libretro.exePath;
    const char *toolchain = debugger.libretro.toolchainPrefix;
    e9ui_component_t *select = NULL;
    if (st->fileSelectMeta) {
        select = e9ui_child_find(comp, st->fileSelectMeta);
    }
    if (!debugger.elfValid || !elf || !*elf) {
        if (select) {
            e9ui_textbox_setOptions(select, NULL, 0);
        }
        source_pane_clearSourceFiles(st);
        source_pane_syncFileSelect(comp, st);
        return;
    }
    if (st->sourceFilesLoaded &&
        strcmp(st->sourceFilesElf, elf) == 0 &&
        strcmp(st->sourceFilesToolchain, toolchain ? toolchain : "") == 0) {
        if (select) {
            e9ui_setHidden(select, st->viewMode == source_pane_mode_c ? 0 : 1);
            if (st->viewMode == source_pane_mode_c) {
                if (e9ui && e9ui_getFocus(&e9ui->ctx) == select) {
                    return;
                }
                const char *displayPath = NULL;
                if (st->manualSrcActive && st->manualSrcPath) {
                    displayPath = st->manualSrcPath;
                } else if (st->curSrcPath[0]) {
                    displayPath = st->curSrcPath;
                }
                if (!displayPath || !*displayPath) {
                    e9ui_textbox_setSelectedValue(select, "");
                } else {
                    for (int i = 0; i < st->sourceFileCount; ++i) {
                        if (source_pane_fileMatches(st->sourceFiles[i], displayPath)) {
                            e9ui_textbox_setSelectedValue(select, st->sourceFiles[i]);
                            break;
                        }
                    }
                }
            }
        }
        return;
    }

    if (select) {
        e9ui_textbox_setOptions(select, NULL, 0);
    }
    source_pane_clearSourceFiles(st);
    (void)source_pane_collectReadelfFiles(st, elf);
    (void)source_pane_collectStabsFiles(st, elf);
    if (st->sourceFileCount <= 0 && debugger_toolchainUsesHunkAddr2line()) {
        (void)source_pane_collectObjdumpTextFiles(st, elf);
    }
    int foundSourceFiles = st->sourceFileCount;
    if (foundSourceFiles <= 0) {
        debug_error("source_pane: no source files collected (elf='%s', sourceDir='%s', toolchain='%s')",
                    elf,
                    debugger.libretro.sourceDir,
                    debugger.libretro.toolchainPrefix);
    }
    source_pane_prependBlankSourceOption(st);
    st->sourceFilesLoaded = 1;
    strncpy(st->sourceFilesElf, elf, sizeof(st->sourceFilesElf) - 1);
    st->sourceFilesElf[sizeof(st->sourceFilesElf) - 1] = '\0';
    if (toolchain) {
        strncpy(st->sourceFilesToolchain, toolchain, sizeof(st->sourceFilesToolchain) - 1);
        st->sourceFilesToolchain[sizeof(st->sourceFilesToolchain) - 1] = '\0';
    } else {
        st->sourceFilesToolchain[0] = '\0';
    }
    source_pane_syncFileSelect(comp, st);
}

static int
source_pane_resolveFileLineAll(const char *elf, const char *file, int line_no,
                               uint32_t **out_addrs, int *out_count);

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
source_pane_resolveFileLine(const char *elf, const char *file, int line_no, uint32_t *out_addr)
{
    uint32_t *addrs = NULL;
    int count = 0;

    if (out_addr) {
        *out_addr = 0;
    }
    if (!source_pane_resolveFileLineAll(elf, file, line_no, &addrs, &count)) {
        return 0;
    }

    *out_addr = addrs[0];
    alloc_free(addrs);
    return 1;
}

static void
source_pane_fileline_cache_clear(void)
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
source_pane_fileline_cache_ensureCapacity(int minCap)
{
    if (source_pane_fileline_cache_state.entryCap >= minCap) {
        return 1;
    }

    int nextCap = source_pane_fileline_cache_state.entryCap > 0 ? source_pane_fileline_cache_state.entryCap : 1024;
    while (nextCap < minCap) {
        nextCap *= 2;
    }

    source_pane_fileline_cache_entry_t *nextEntries =
        (source_pane_fileline_cache_entry_t *)alloc_realloc(source_pane_fileline_cache_state.entries,
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
source_pane_fileline_cache_addEntry(const char *path, int line, uint32_t addr)
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

    if (!source_pane_fileline_cache_ensureCapacity(source_pane_fileline_cache_state.entryCount + 1)) {
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

static machine_breakpoint_t *
source_pane_findBreakpointForLine(const char *path, int line,
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

static int
source_pane_removeBreakpointsForLine(const char *path, int line,
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

    uint32_t *addrs = (uint32_t *)alloc_alloc(sizeof(*addrs) * (size_t)matchCount);
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
source_pane_compareBreakpointAddr(const void *a, const void *b)
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
source_pane_fileline_cache_buildFromDisassembly(const char *elf)
{
    if (!elf || !*elf) {
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
        if (!source_pane_fileline_cache_addEntry(currentFile, currentLine, addr)) {
            pclose(fp);
            return 0;
        }
    }

    pclose(fp);
    return 1;
}

static int
source_pane_fileline_cache_findAll(const char *file, int line_no, uint32_t **out_addrs, int *out_count)
{
    if (out_addrs) {
        *out_addrs = NULL;
    }
    if (out_count) {
        *out_count = 0;
    }
    if (!file || !*file || line_no <= 0 || !out_addrs || !out_count) {
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

    uint32_t *matches = (uint32_t *)alloc_alloc(sizeof(*matches) * (size_t)matchCount);
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

    qsort(matches, (size_t)writeIndex, sizeof(*matches), source_pane_compareBreakpointAddr);

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
source_pane_fileline_cache_ensure(const char *elf)
{
    const char *toolchain = debugger.libretro.toolchainPrefix;
    const char *sourceDir = debugger.libretro.sourceDir;

    if (source_pane_fileline_cache_state.cacheReady &&
        strcmp(source_pane_fileline_cache_state.elfPath, elf) == 0 &&
        strcmp(source_pane_fileline_cache_state.toolchainPrefix, toolchain) == 0 &&
        strcmp(source_pane_fileline_cache_state.sourceDir, sourceDir) == 0) {
        return 1;
    }

    source_pane_fileline_cache_clear();
    if (!source_pane_fileline_cache_buildFromDisassembly(elf)) {
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
source_pane_resolveFileLineAll(const char *elf, const char *file, int line_no,
                               uint32_t **out_addrs, int *out_count)
{
    if (out_addrs) {
        *out_addrs = NULL;
    }
    if (out_count) {
        *out_count = 0;
    }
    if (!elf || !*elf || !debugger.elfValid || !file || !*file || line_no <= 0 || !out_addrs || !out_count) {
        return 0;
    }
    if (debugger_toolchainUsesHunkAddr2line()) {
        return hunk_fileline_cache_resolveFileLineAll(elf, file, line_no, out_addrs, out_count);
    }

    if (!source_pane_fileline_cache_ensure(elf)) {
        return 0;
    }
    return source_pane_fileline_cache_findAll(file, line_no, out_addrs, out_count);
}

static int
source_pane_addBreakpointsForLine(const char *path, int lineNo)
{
    if (!path || lineNo <= 0) {
        return 0;
    }

    uint32_t *resolvedAddrs = NULL;
    int resolvedCount = 0;
    if (!source_pane_resolveFileLineAll(debugger.libretro.exePath,
                                        path,
                                        lineNo,
                                        &resolvedAddrs,
                                        &resolvedCount)) {
        return 0;
    }

    int changed = 0;
    for (int i = 0; i < resolvedCount; ++i) {
        uint32_t runtimeAddr = resolvedAddrs[i];
        (void)base_map_debugToRuntime(BASE_MAP_SECTION_TEXT, runtimeAddr, &runtimeAddr);

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


source_pane_line_metrics_t
source_pane_computeLineMetrics(e9ui_component_t *self, e9ui_context_t *ctx, TTF_Font *font, int padPx)
{
    source_pane_line_metrics_t out = {0};
    if (!self || !font) {
        out.lineHeight = 16;
        out.maxLines = 1;
        return out;
    }
    out.lineHeight = TTF_FontHeight(font);
    if (out.lineHeight <= 0) {
        out.lineHeight = 16;
    }
    SDL_Rect contentArea = source_pane_getContentArea(self, ctx, padPx);
    out.innerHeight = contentArea.h - padPx * 2;
    if (out.innerHeight <= 0) {
        out.maxLines = 0;
        return out;
    }
    out.maxLines = out.innerHeight / out.lineHeight;
    if (out.maxLines <= 0) {
        out.maxLines = 1;
    }
    return out;
}

SDL_Rect
source_pane_getContentArea(e9ui_component_t *self, e9ui_context_t *ctx, int padPx)
{
    SDL_Rect area = {0};
    if (!self) {
        return area;
    }

    area.x = self->bounds.x;
    area.y = self->bounds.y;
    area.w = self->bounds.w;
    area.h = self->bounds.h;

    source_pane_state_t *st = (source_pane_state_t *)self->state;
    int topInset = source_pane_overlayTopRowHeight(self, ctx, st);
    if (topInset < 0) {
        topInset = 0;
    }
    if (topInset > 0) {
        area.y += topInset;
        area.h -= topInset;
    }
    if (area.h < padPx * 2) {
        area.h = padPx * 2;
    }
    return area;
}

TTF_Font *
source_pane_resolveFont(const e9ui_context_t *ctx)
{
    if (e9ui->theme.text.source) {
        return e9ui->theme.text.source;
    }
    if (ctx) {
        return ctx->font;
    }
    return NULL;
}

static int
source_pane_pointInBounds(const e9ui_component_t *comp, int x, int y)
{
    if (!comp) {
        return 0;
    }
    return x >= comp->bounds.x && x < comp->bounds.x + comp->bounds.w &&
           y >= comp->bounds.y && y < comp->bounds.y + comp->bounds.h;
}

static void
source_pane_pushRenderClip(e9ui_context_t *ctx, const SDL_Rect *clipRect, SDL_bool *hadClip, SDL_Rect *prevClip)
{
    if (!ctx || !ctx->renderer || !clipRect || !hadClip || !prevClip) {
        return;
    }
    *hadClip = SDL_RenderIsClipEnabled(ctx->renderer);
    if (*hadClip) {
        SDL_RenderGetClipRect(ctx->renderer, prevClip);
    }
    if (clipRect->w > 0 && clipRect->h > 0) {
        SDL_RenderSetClipRect(ctx->renderer, clipRect);
    } else {
        SDL_RenderSetClipRect(ctx->renderer, NULL);
    }
}

static void
source_pane_popRenderClip(e9ui_context_t *ctx, SDL_bool hadClip, const SDL_Rect *prevClip)
{
    if (!ctx || !ctx->renderer) {
        return;
    }
    if (hadClip && prevClip) {
        SDL_RenderSetClipRect(ctx->renderer, prevClip);
    } else {
        SDL_RenderSetClipRect(ctx->renderer, NULL);
    }
}

void
source_pane_renderStatusMessage(e9ui_context_t *ctx, TTF_Font *font, SDL_Rect area, int padPx,
                                SDL_Color color, const char *msg)
{
    if (!ctx || !ctx->renderer || !font || !msg || !msg[0]) {
        return;
    }
    int tw = 0;
    int th = 0;
    SDL_Texture *t = e9ui_text_cache_getText(ctx->renderer, font, msg, color, &tw, &th);
    if (!t) {
        return;
    }
    int y = area.y + area.h - padPx - th;
    if (y < area.y + padPx) {
        y = area.y + padPx;
    }
    SDL_Rect r = {area.x + padPx, y, tw, th};
    SDL_RenderCopy(ctx->renderer, t, NULL, &r);
}

static SDL_Color
source_pane_syntaxColor(syntax_highlight_kind_t kind)
{
    switch (kind) {
        case syntax_highlight_kind_keyword:
            return (SDL_Color){220, 170, 90, 255};
        case syntax_highlight_kind_type:
            return (SDL_Color){120, 190, 255, 255};
        case syntax_highlight_kind_string:
            return (SDL_Color){200, 210, 120, 255};
        case syntax_highlight_kind_comment:
            return (SDL_Color){120, 150, 120, 255};
        case syntax_highlight_kind_number:
            return (SDL_Color){210, 150, 120, 255};
        case syntax_highlight_kind_preproc:
            return (SDL_Color){175, 145, 220, 255};
        case syntax_highlight_kind_function:
            return (SDL_Color){110, 215, 195, 255};
        default:
            return (SDL_Color){220, 220, 220, 255};
    }
}

static int
source_pane_copySegment(const char *line, int start, int len, char *stackBuf, int stackCap, char **outBuf)
{
    if (!outBuf || !line || start < 0 || len <= 0) {
        return 0;
    }
    *outBuf = NULL;
    if (len + 1 <= stackCap) {
        memcpy(stackBuf, line + start, (size_t)len);
        stackBuf[len] = '\0';
        *outBuf = stackBuf;
        return 1;
    }
    char *heap = (char*)alloc_alloc((size_t)len + 1);
    if (!heap) {
        return 0;
    }
    memcpy(heap, line + start, (size_t)len);
    heap[len] = '\0';
    *outBuf = heap;
    return 1;
}

int
source_pane_measureSegment(TTF_Font *font, const char *line, int start, int len)
{
    if (!font || !line || start < 0 || len <= 0) {
        return 0;
    }
    char stackBuf[256];
    char *seg = NULL;
    if (!source_pane_copySegment(line, start, len, stackBuf, (int)sizeof(stackBuf), &seg)) {
        return 0;
    }
    int w = 0;
    int h = 0;
    TTF_SizeText(font, seg, &w, &h);
    if (seg != stackBuf) {
        alloc_free(seg);
    }
    return w;
}

static void
source_pane_drawSegment(e9ui_context_t *ctx, e9ui_component_t *owner, TTF_Font *font, const char *line,
                        int start, int len, SDL_Color color, int x, int y, int lineHeight)
{
    if (!ctx || !owner || !font || !line || len <= 0) {
        return;
    }
    char stackBuf[256];
    char *seg = NULL;
    if (!source_pane_copySegment(line, start, len, stackBuf, (int)sizeof(stackBuf), &seg)) {
        return;
    }
    e9ui_text_select_drawText(ctx, owner, font, seg, color, x, y, lineHeight, 0, owner, 0, 0);
    if (seg != stackBuf) {
        alloc_free(seg);
    }
}

typedef struct source_pane_asm_spans {
    syntax_highlight_span_t *items;
    int count;
    int cap;
} source_pane_asm_spans_t;

static void
source_pane_freeAsmSpans(source_pane_asm_spans_t *spans)
{
    if (!spans) {
        return;
    }
    if (spans->items) {
        alloc_free(spans->items);
    }
    memset(spans, 0, sizeof(*spans));
}

static int
source_pane_addAsmSpan(void *user, int lineIndex, int startColumn, int length, syntax_highlight_kind_t kind)
{
    if (!user || lineIndex != 0 || startColumn < 0 || length <= 0) {
        return 0;
    }
    source_pane_asm_spans_t *spans = (source_pane_asm_spans_t*)user;
    if (spans->count >= spans->cap) {
        int nextCap = spans->cap ? spans->cap * 2 : 8;
        syntax_highlight_span_t *nextItems = (syntax_highlight_span_t*)alloc_realloc(
            spans->items, (size_t)nextCap * sizeof(*nextItems));
        if (!nextItems) {
            return 0;
        }
        spans->items = nextItems;
        spans->cap = nextCap;
    }
    syntax_highlight_span_t *dst = &spans->items[spans->count++];
    dst->startColumn = startColumn;
    dst->length = length;
    dst->kind = kind;
    return 1;
}

static int
source_pane_buildAsmLineSpans(const char *line, source_pane_asm_spans_t *outSpans)
{
    if (!line || !outSpans) {
        return 0;
    }
    memset(outSpans, 0, sizeof(*outSpans));
    int lineLength = (int)strlen(line);
    if (lineLength <= 0) {
        return 1;
    }
    if (!syntax_highlight_asm_buildLineSpans(line, lineLength, 0, source_pane_addAsmSpan, outSpans)) {
        source_pane_freeAsmSpans(outSpans);
        return 0;
    }
    return 1;
}

void
source_pane_renderAsmLineHighlighted(e9ui_context_t *ctx, e9ui_component_t *owner, TTF_Font *font,
                                     const char *line, int highlightOffset, SDL_Color baseColor, int textX, int y,
                                     int lineHeight, int hitW, void *sourceBucket)
{
    if (!ctx || !owner || !font || !line) {
        return;
    }
    e9ui_text_select_drawText(ctx, owner, font, line, baseColor, textX, y,
                              lineHeight, hitW, sourceBucket, 0, 1);
    if (highlightOffset < 0) {
        highlightOffset = 0;
    }
    int lineLen = (int)strlen(line);
    if (highlightOffset > lineLen) {
        highlightOffset = lineLen;
    }
    source_pane_asm_spans_t spans;
    if (!source_pane_buildAsmLineSpans(line + highlightOffset, &spans)) {
        return;
    }
    if (!spans.items || spans.count <= 0) {
        source_pane_freeAsmSpans(&spans);
        return;
    }
    int cursorIndex = 0;
    int cursorX = textX;
    if (highlightOffset > 0) {
        cursorX += source_pane_measureSegment(font, line, 0, highlightOffset);
        cursorIndex = highlightOffset;
    }
    for (int i = 0; i < spans.count; ++i) {
        int start = spans.items[i].startColumn + highlightOffset;
        int len = spans.items[i].length;
        if (start < cursorIndex) {
            int trim = cursorIndex - start;
            if (trim >= len) {
                continue;
            }
            start += trim;
            len -= trim;
        }
        if (start >= lineLen || len <= 0) {
            continue;
        }
        if (start > cursorIndex) {
            int gap = source_pane_measureSegment(font, line, cursorIndex, start - cursorIndex);
            cursorX += gap;
            cursorIndex = start;
        }
        if (start + len > lineLen) {
            len = lineLen - start;
        }
        if (len <= 0) {
            continue;
        }
        SDL_Color color = source_pane_syntaxColor(spans.items[i].kind);
        source_pane_drawSegment(ctx, owner, font, line, start, len, color, cursorX, y, lineHeight);
        int segw = source_pane_measureSegment(font, line, start, len);
        cursorX += segw;
        cursorIndex = start + len;
    }
    source_pane_freeAsmSpans(&spans);
}

static void
source_pane_renderHighlightedLine(e9ui_context_t *ctx, e9ui_component_t *owner, TTF_Font *font,
                                  const char *path, int lineNo, const char *line, SDL_Color baseColor,
                                  int textX, int y, int lineHeight, int hitW, void *sourceBucket)
{
    if (!ctx || !owner || !font || !line) {
        return;
    }
    e9ui_text_select_drawText(ctx, owner, font, line, baseColor, textX, y,
                              lineHeight, hitW, sourceBucket, 0, 1);
    const syntax_highlight_span_t *spans = NULL;
    int spanCount = 0;
    if (!syntax_highlight_getLineSpans(path, lineNo, &spans, &spanCount) || !spans || spanCount <= 0) {
        return;
    }
    int lineLen = (int)strlen(line);
    int cursorIndex = 0;
    int cursorX = textX;
    for (int i = 0; i < spanCount; ++i) {
        int start = spans[i].startColumn;
        int len = spans[i].length;
        if (start < cursorIndex) {
            int trim = cursorIndex - start;
            if (trim >= len) {
                continue;
            }
            start += trim;
            len -= trim;
        }
        if (start >= lineLen || len <= 0) {
            continue;
        }
        if (start > cursorIndex) {
            int gap = source_pane_measureSegment(font, line, cursorIndex, start - cursorIndex);
            cursorX += gap;
            cursorIndex = start;
        }
        if (start + len > lineLen) {
            len = lineLen - start;
        }
        if (len <= 0) {
            continue;
        }
        SDL_Color color = source_pane_syntaxColor(spans[i].kind);
        source_pane_drawSegment(ctx, owner, font, line, start, len, color, cursorX, y, lineHeight);
        int segw = source_pane_measureSegment(font, line, start, len);
        cursorX += segw;
        cursorIndex = start + len;
    }
}

static int
source_pane_resolveCurrentHighlightLine(const source_pane_state_t *st, const char *viewPath)
{
    if (!st || !viewPath || !viewPath[0]) {
        return 0;
    }
    if (!st->curSrcPath[0] || st->curSrcLine <= 0) {
        return 0;
    }
    if (!source_pane_fileMatches(viewPath, st->curSrcPath)) {
        return 0;
    }
    return st->curSrcLine;
}

int
source_pane_findCurrentAddrRow(const uint64_t *addrs, int count, uint64_t curAddr)
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

static void
source_pane_clearHover(e9ui_component_t *self, source_pane_state_t *st)
{
    if (!self || !st) {
        return;
    }
    st->hoverExpr[0] = '\0';
    st->hoverTip[0] = '\0';
    st->hoverLine = 0;
    st->hoverCol = 0;
    st->hoverPc = 0;
    st->hoverTick = 0;
    st->hoverActive = 0;
    e9ui_setTooltip(self, NULL);
}

static int
source_pane_columnFromX(TTF_Font *font, const char *line, int relX)
{
    if (!font || !line) {
        return 0;
    }
    if (relX <= 0) {
        return 0;
    }
    int lineLen = (int)strlen(line);
    if (lineLen <= 0) {
        return 0;
    }
    int bestCol = lineLen;
    for (int i = 1; i <= lineLen; ++i) {
        int w = source_pane_measureSegment(font, line, 0, i);
        if (w >= relX) {
            bestCol = i - 1;
            break;
        }
    }
    if (bestCol < 0) {
        bestCol = 0;
    }
    if (bestCol > lineLen) {
        bestCol = lineLen;
    }
    return bestCol;
}

static int
source_pane_extractHoverExprFallback(const char *line, int column, char *outExpr, int outCap)
{
    if (!line || !outExpr || outCap <= 1 || column < 0) {
        return 0;
    }
    outExpr[0] = '\0';
    int lineLen = (int)strlen(line);
    if (lineLen <= 0) {
        return 0;
    }
    int pivot = column;
    if (pivot >= lineLen) {
        pivot = lineLen - 1;
    }
    if (pivot < 0) {
        return 0;
    }
    if (!(isalnum((unsigned char)line[pivot]) || line[pivot] == '_')) {
        if (pivot > 0 && (isalnum((unsigned char)line[pivot - 1]) || line[pivot - 1] == '_')) {
            pivot -= 1;
        } else {
            return 0;
        }
    }
    int start = pivot;
    while (start > 0 && (isalnum((unsigned char)line[start - 1]) || line[start - 1] == '_')) {
        start--;
    }
    int end = pivot + 1;
    while (end < lineLen && (isalnum((unsigned char)line[end]) || line[end] == '_')) {
        end++;
    }
    int len = end - start;
    if (len <= 0) {
        return 0;
    }
    if (len >= outCap) {
        len = outCap - 1;
    }
    memcpy(outExpr, line + start, (size_t)len);
    outExpr[len] = '\0';
    return 1;
}

static int
source_pane_isIdentifierExpr(const char *expr)
{
    if (!expr || !*expr) {
        return 0;
    }
    const unsigned char *p = (const unsigned char *)expr;
    if (!(isalpha(*p) || *p == '_')) {
        return 0;
    }
    ++p;
    while (*p) {
        if (!(isalnum(*p) || *p == '_')) {
            return 0;
        }
        ++p;
    }
    return 1;
}

static void
source_pane_sanitizeTooltipLine(char *text)
{
    if (!text) {
        return;
    }
    for (char *p = text; *p; ++p) {
        if (*p == '\n' || *p == '\r') {
            *p = ' ';
        }
    }
}

static void
source_pane_updateHoverTooltip(e9ui_component_t *self, e9ui_context_t *ctx, source_pane_state_t *st,
                               const e9ui_event_t *ev)
{
    if (!self || !ctx || !st) {
        return;
    }
    if (ev && ev->type != SDL_MOUSEMOTION) {
        return;
    }
    if (st->viewMode != source_pane_mode_c) {
        source_pane_clearHover(self, st);
        return;
    }
    if (machine_getRunning(debugger.machine) && !st->frozenActive) {
        source_pane_clearHover(self, st);
        return;
    }
    int mx = ev ? ev->motion.x : ctx->mouseX;
    int my = ev ? ev->motion.y : ctx->mouseY;
    if (!source_pane_pointInBounds(self, mx, my)) {
        source_pane_clearHover(self, st);
        return;
    }
    TTF_Font *useFont = source_pane_resolveFont(ctx);
    if (!useFont) {
        source_pane_clearHover(self, st);
        return;
    }
    const int padPx = 10;
    source_pane_updateSourceLocation(st, 0);
    int manualView = st->manualSrcActive && st->manualSrcPath;
    const char *path = manualView ? st->manualSrcPath : st->curSrcPath;
    int curLine = manualView ? 0 : st->curSrcLine;
    if (!path || !path[0] || (!manualView && curLine <= 0)) {
        source_pane_clearHover(self, st);
        return;
    }
    source_pane_line_metrics_t metrics = source_pane_computeLineMetrics(self, ctx, useFont, padPx);
    if (metrics.innerHeight <= 0 || metrics.lineHeight <= 0) {
        source_pane_clearHover(self, st);
        return;
    }
    int maxLines = metrics.maxLines > 0 ? metrics.maxLines : 1;
    int start = 1;
    if (!manualView) {
        start = curLine - (maxLines / 2);
        if (start < 1) {
            start = 1;
        }
    }
    if (st->scrollLocked) {
        start = st->scrollLine;
        if (start < 1) {
            start = 1;
        }
    }
    int end = start + maxLines - 1;
    const char **lines = NULL;
    int count = 0;
    int first = 0;
    int total = 0;
    if (!source_getRange(path, start, end, &lines, &count, &first, &total) || count <= 0) {
        source_pane_clearHover(self, st);
        return;
    }
    if (count < maxLines && total > 0) {
        int missing = maxLines - count;
        int altStart = first - missing;
        if (altStart < 1) {
            altStart = 1;
        }
        int altEnd = altStart + maxLines - 1;
        if (altEnd > total) {
            altEnd = total;
        }
        source_getRange(path, altStart, altEnd, &lines, &count, &first, &total);
    }
    int digits = 1;
    int tmpTotal = (total > 0) ? total : (first + count - 1);
    for (int v = tmpTotal; v >= 10; v /= 10) {
        digits++;
    }
    if (digits < 3) {
        digits = 3;
    }
    char zeros[16];
    if (digits >= (int)sizeof(zeros)) {
        digits = (int)sizeof(zeros) - 1;
    }
    for (int i = 0; i < digits; ++i) {
        zeros[i] = '8';
    }
    zeros[digits] = '\0';
    int gutterW = 0;
    int th = 0;
    TTF_SizeText(useFont, zeros, &gutterW, &th);
    int gutterPad = e9ui_scale_px(ctx, 16);
    SDL_Rect contentArea = source_pane_getContentArea(self, ctx, padPx);
    int textX = contentArea.x + padPx + gutterW + gutterPad - st->cScrollX;
    int relY = my - (contentArea.y + padPx);
    if (relY < 0) {
        source_pane_clearHover(self, st);
        return;
    }
    int row = relY / metrics.lineHeight;
    if (row < 0 || row >= count) {
        source_pane_clearHover(self, st);
        return;
    }
    if (mx < textX) {
        source_pane_clearHover(self, st);
        return;
    }
    const char *line = lines[row] ? lines[row] : "";
    int relX = mx - textX;
    int lineLen = (int)strlen(line);
    int lineWidth = source_pane_measureSegment(useFont, line, 0, lineLen);
    if (relX > lineWidth) {
        source_pane_clearHover(self, st);
        return;
    }
    int lineNo = first + row;
    int column = source_pane_columnFromX(useFont, line, relX);
    char expr[256];
    expr[0] = '\0';
    if (!syntax_highlight_getHoverExpr(path, lineNo, column, expr, sizeof(expr))) {
        if (!source_pane_extractHoverExprFallback(line, column, expr, (int)sizeof(expr))) {
            source_pane_clearHover(self, st);
            return;
        }
    }
    unsigned long pcReg = 0;
    (void)machine_findReg(&debugger.machine, "PC", &pcReg);
    uint32_t pc = (uint32_t)pcReg;
    int sameTarget = st->hoverActive &&
                     st->hoverLine == lineNo &&
                     strcmp(st->hoverExpr, expr) == 0;
    char tip[1024];
    tip[0] = '\0';
    if (!print_eval_eval(expr, tip, sizeof(tip))) {
        source_pane_clearHover(self, st);
        return;
    }
    source_pane_sanitizeTooltipLine(tip);
    if (debugger_toolchainUsesHunkAddr2line() && source_pane_isIdentifierExpr(expr)) {
        char derefExpr[288];
        if (snprintf(derefExpr, sizeof(derefExpr), "*%s", expr) > 0) {
            char derefTip[512];
            derefTip[0] = '\0';
            if (print_eval_eval(derefExpr, derefTip, sizeof(derefTip))) {
                source_pane_sanitizeTooltipLine(derefTip);
                if (derefTip[0]) {
                    char combinedTip[1024];
                    snprintf(combinedTip, sizeof(combinedTip), "%s\n%s", tip, derefTip);
                    combinedTip[sizeof(combinedTip) - 1] = '\0';
                    strncpy(tip, combinedTip, sizeof(tip) - 1);
                    tip[sizeof(tip) - 1] = '\0';
                }
            }
        }
    }
    strncpy(st->hoverExpr, expr, sizeof(st->hoverExpr) - 1);
    st->hoverExpr[sizeof(st->hoverExpr) - 1] = '\0';
    strncpy(st->hoverTip, tip, sizeof(st->hoverTip) - 1);
    st->hoverTip[sizeof(st->hoverTip) - 1] = '\0';
    st->hoverLine = lineNo;
    st->hoverCol = column;
    st->hoverPc = pc;
    st->hoverTick = SDL_GetTicks();
    st->hoverActive = 1;
    if (!sameTarget) {
        e9ui_setTooltip(self, st->hoverTip);
    }
}

static void
source_pane_adjustScroll(source_pane_state_t *st, source_pane_mode_t mode, int delta)
{
    if (!st || delta == 0) {
        return;
    }
    if (st->inlineEditActive) {
        source_pane_inlineEditCancel(st, NULL);
    }
    if (mode == source_pane_mode_c) {
        int dest = st->scrollLine + delta;
        if (dest < 1) {
            dest = 1;
        }
        st->scrollLine = dest;
        st->scrollLocked = 1;
        st->gutterPending = 0;
        source_pane_syncLockButtonVisual(st);
        return;
    }
    if (mode == source_pane_mode_cpr) {
        uint64_t destAddr = source_cpr_getCurrentAddr(st);
        if (st->scrollLocked && st->scrollAnchorValid) {
            destAddr = st->scrollAnchorAddr;
        }
        if (delta < 0) {
            uint64_t step = (uint64_t)(-delta) * 4ull;
            destAddr = destAddr > step ? destAddr - step : 0ull;
        } else {
            destAddr += (uint64_t)delta * 4ull;
        }
        destAddr = source_cpr_resolveAnchorAddr(destAddr);
        st->scrollIndex = (int)(destAddr >> 2);
        st->scrollAnchorAddr = destAddr;
        st->scrollAnchorValid = 1;
        st->scrollLocked = 1;
        st->gutterPending = 0;
        source_pane_syncLockButtonVisual(st);
        return;
    }
    int dest = st->scrollIndex + delta;
    int streaming = (dasm_getFlags() & DASM_IFACE_FLAG_STREAMING) ? 1 : 0;
    if (st->scrollLocked && st->scrollAnchorValid) {
        int anchorIndex = 0;
        if (dasm_findIndexForAddr(st->scrollAnchorAddr, &anchorIndex)) {
            dest = anchorIndex + delta;
        }
    }
    if (dest < 0 && !streaming) {
        dest = 0;
    }
    if (!streaming) {
        int total = dasm_getTotal();
        if (total > 0 && dest >= total) {
            dest = total - 1;
        }
    }
    const char **lines = NULL;
    const uint64_t *addrs = NULL;
    int first = 0;
    int count = 0;
    if (dasm_getRangeByIndex(dest, dest, &lines, &addrs, &first, &count) && count > 0) {
        st->scrollIndex = first;
        st->scrollAnchorAddr = addrs[0];
        st->scrollAnchorValid = 1;
    } else {
        st->scrollIndex = dest;
        st->scrollAnchorValid = 0;
    }
    st->scrollLocked = 1;
    st->gutterPending = 0;
    source_pane_syncLockButtonVisual(st);
}

static void
source_pane_scrollToStart(source_pane_state_t *st, source_pane_mode_t mode)
{
    if (!st) {
        return;
    }
    if (mode == source_pane_mode_c) {
        st->scrollLine = 1;
        st->scrollLocked = 1;
        st->gutterPending = 0;
        source_pane_syncLockButtonVisual(st);
        return;
    }
    if (mode == source_pane_mode_cpr) {
        st->scrollIndex = 0;
        st->scrollAnchorAddr = 0;
        st->scrollAnchorValid = 1;
        st->scrollLocked = 1;
        st->gutterPending = 0;
        source_pane_syncLockButtonVisual(st);
        return;
    }
    if (dasm_getFlags() & DASM_IFACE_FLAG_STREAMING) {
        source_pane_followCurrent(st);
        return;
    }
    st->scrollIndex = 0;
    st->scrollLocked = 1;
    st->scrollAnchorValid = 0;
    st->gutterPending = 0;
    source_pane_syncLockButtonVisual(st);
}

static void
source_pane_scrollToEnd(source_pane_state_t *st, source_pane_mode_t mode, int maxLines)
{
    if (!st) {
        return;
    }
    if (maxLines <= 0) {
        maxLines = 1;
    }
    if (mode == source_pane_mode_c) {
        source_pane_updateSourceLocation(st, 0);
        int total = 0;
        if (st->curSrcPath[0]) {
            total = source_getTotalLines(st->curSrcPath);
        }
        if (total <= 0) {
            st->scrollLine = 1;
        } else {
            int dest = total - maxLines + 1;
            if (dest < 1) {
                dest = 1;
            }
            st->scrollLine = dest;
        }
        st->scrollLocked = 1;
        st->gutterPending = 0;
        source_pane_syncLockButtonVisual(st);
        return;
    }
    if (mode == source_pane_mode_cpr) {
        uint64_t destAddr = 0x00fffffcu;
        if (maxLines > 1) {
            uint64_t back = (uint64_t)(maxLines - 1) * 4ull;
            destAddr = destAddr > back ? destAddr - back : 0ull;
        }
        destAddr = source_cpr_resolveAnchorAddr(destAddr);
        st->scrollIndex = (int)(destAddr >> 2);
        st->scrollAnchorAddr = destAddr;
        st->scrollAnchorValid = 1;
        st->scrollLocked = 1;
        st->gutterPending = 0;
        source_pane_syncLockButtonVisual(st);
        return;
    }
    if (dasm_getFlags() & DASM_IFACE_FLAG_STREAMING) {
        return;
    }
    int total = dasm_getTotal();
    int dest = total - maxLines;
    if (dest < 0) {
        dest = 0;
    }
    st->scrollIndex = dest;
    st->scrollLocked = 1;
    st->scrollAnchorValid = 0;
    st->gutterPending = 0;
    source_pane_syncLockButtonVisual(st);
}

static int
source_pane_arrowScrollAmount(SDL_Keymod mods, int maxLines)
{
    int amount = 1;
    if ((mods & KMOD_CTRL) != 0 || (mods & KMOD_GUI) != 0) {
        amount = maxLines / 2;
        if (amount < 1) {
            amount = 1;
        }
    } else if ((mods & KMOD_SHIFT) != 0) {
        amount = 4;
    } else if ((mods & KMOD_ALT) != 0) {
        amount = 16;
    }
    return amount;
}

static void
source_pane_followCurrent(source_pane_state_t *st)
{
    if (!st) {
        return;
    }
    source_pane_clearFunctionScrollLock(st);
    st->scrollLocked = 0;
    st->scrollAnchorValid = 0;
    source_pane_freeFrozenAsm(st);
    st->overrideActive = 0;
    st->gutterPending = 0;
    st->manualSrcActive = 0;
    source_pane_syncLockButtonVisual(st);
}

static void
source_pane_searchClearMatch(source_pane_state_t *st)
{
    if (!st) {
        return;
    }
    st->searchMatchValid = 0;
    st->searchMatchLine = 0;
    st->searchMatchColumn = 0;
    st->searchMatchLength = 0;
}

static void
source_pane_searchOpen(source_pane_state_t *st, e9ui_context_t *ctx)
{
    if (!st || !st->ownerPane || !st->searchBoxMeta) {
        return;
    }
    e9ui_component_t *search_box = e9ui_child_find(st->ownerPane, st->searchBoxMeta);
    if (!search_box) {
        return;
    }
    st->searchActive = 1;
    e9ui_setHidden(search_box, 0);
    if (ctx) {
        e9ui_setFocus(ctx, search_box);
    }
}

static void
source_pane_searchClose(source_pane_state_t *st, e9ui_context_t *ctx)
{
    if (!st || !st->ownerPane || !st->searchBoxMeta) {
        return;
    }
    e9ui_component_t *search_box = e9ui_child_find(st->ownerPane, st->searchBoxMeta);
    st->searchActive = 0;
    source_pane_searchClearMatch(st);
    if (search_box) {
        e9ui_textbox_setText(search_box, "");
        e9ui_setHidden(search_box, 1);
    }
    if (ctx && st->ownerPane) {
        e9ui_setFocus(ctx, st->ownerPane);
    }
}

static int
source_pane_searchFind(source_pane_state_t *st, e9ui_component_t *self, e9ui_context_t *ctx, int direction, int advance)
{
    if (!st || !self || !st->ownerPane || !st->searchBoxMeta) {
        return 0;
    }
    if (st->viewMode != source_pane_mode_c) {
        return 0;
    }
    e9ui_component_t *search_box = e9ui_child_find(st->ownerPane, st->searchBoxMeta);
    if (!search_box) {
        return 0;
    }
    const char *needle = e9ui_textbox_getText(search_box);
    if (!needle || !needle[0]) {
        source_pane_searchClearMatch(st);
        return 0;
    }

    source_pane_updateSourceLocation(st, 0);
    int manual_view = st->manualSrcActive && st->manualSrcPath;
    const char *path = manual_view ? st->manualSrcPath : st->curSrcPath;
    int cur_line = manual_view ? 0 : st->curSrcLine;
    if (!path || !path[0] || (!manual_view && cur_line <= 0)) {
        source_pane_searchClearMatch(st);
        return 0;
    }

    int total = source_getTotalLines(path);
    if (total <= 0) {
        source_pane_searchClearMatch(st);
        return 0;
    }
    const char **lines = NULL;
    int count = 0;
    int first = 0;
    if (!source_getRange(path, 1, total, &lines, &count, &first, &total) || count <= 0 || first != 1) {
        source_pane_searchClearMatch(st);
        return 0;
    }

    int needle_len = (int)strlen(needle);
    int start_line = st->searchMatchValid ? st->searchMatchLine : (cur_line > 0 ? cur_line : st->scrollLine);
    if (start_line < 1 || start_line > total) {
        start_line = 1;
    }

    int found_line = 0;
    int found_col = -1;
    if (direction >= 0) {
        int line = start_line;
        int col = advance && st->searchMatchValid ? st->searchMatchColumn + 1 : 0;
        for (int pass = 0; pass < 2 && found_col < 0; ++pass) {
            for (int ln = line; ln <= total; ++ln) {
                const char *text = lines[ln - 1] ? lines[ln - 1] : "";
                int local_col = (ln == line) ? col : 0;
                const char *hit = source_pane_findTextInsensitive(text, needle, local_col);
                if (hit) {
                    found_line = ln;
                    found_col = (int)(hit - text);
                    break;
                }
            }
            if (!advance) {
                break;
            }
            line = 1;
            col = 0;
        }
    } else {
        int line = start_line;
        int col = INT_MAX;
        if (advance && st->searchMatchValid) {
            col = st->searchMatchColumn - 1;
        }
        for (int pass = 0; pass < 2 && found_col < 0; ++pass) {
            for (int ln = line; ln >= 1; --ln) {
                const char *text = lines[ln - 1] ? lines[ln - 1] : "";
                int text_len = (int)strlen(text);
                int local_col = (ln == line) ? col : (text_len - needle_len);
                int hit_col = source_pane_findTextInsensitiveReverse(text, needle, local_col);
                if (hit_col >= 0) {
                    found_line = ln;
                    found_col = hit_col;
                    break;
                }
            }
            if (!advance) {
                break;
            }
            line = total;
            col = INT_MAX;
        }
    }

    if (found_col < 0 || found_line <= 0) {
        source_pane_searchClearMatch(st);
        return 0;
    }

    st->searchMatchValid = 1;
    st->searchMatchLine = found_line;
    st->searchMatchColumn = found_col;
    st->searchMatchLength = needle_len;

    TTF_Font *use_font = source_pane_resolveFont(ctx);
    int max_lines = 1;
    if (use_font) {
        source_pane_line_metrics_t metrics = source_pane_computeLineMetrics(self, ctx, use_font, 10);
        if (metrics.maxLines > 0) {
            max_lines = metrics.maxLines;
        }
    }
    int start = found_line - (max_lines / 2);
    if (start < 1) {
        start = 1;
    }
    int max_start = total - max_lines + 1;
    if (max_start < 1) {
        max_start = 1;
    }
    if (start > max_start) {
        start = max_start;
    }
    st->scrollLine = start;
    st->scrollLocked = 1;
    st->gutterPending = 0;
    source_pane_syncLockButtonVisual(st);
    return 1;
}

static void
source_pane_searchTextChanged(e9ui_context_t *ctx, void *user)
{
    source_pane_state_t *st = (source_pane_state_t*)user;
    if (!st || !st->ownerPane) {
        return;
    }
    source_pane_searchFind(st, st->ownerPane, ctx, 1, 0);
}

static int
source_pane_searchTextboxKey(e9ui_context_t *ctx, SDL_Keycode key, SDL_Keymod mods, void *user)
{
    source_pane_state_t *st = (source_pane_state_t*)user;
    if (!st || !st->ownerPane) {
        return 0;
    }
    if (key == SDLK_ESCAPE) {
        source_pane_searchClose(st, ctx);
        return 1;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        source_pane_searchFind(st, st->ownerPane, ctx, 1, 1);
        return 1;
    }
    if (key == SDLK_UP || key == SDLK_DOWN || key == SDLK_PAGEUP || key == SDLK_PAGEDOWN ||
        key == SDLK_HOME || key == SDLK_END) {
        e9ui_component_t *pane = st->ownerPane;
        TTF_Font *useFont = source_pane_resolveFont(ctx);
        int maxLines = 1;
        if (useFont) {
            source_pane_line_metrics_t metrics = source_pane_computeLineMetrics(pane, ctx, useFont, 10);
            if (metrics.maxLines > 0) {
                maxLines = metrics.maxLines;
            }
        }
        if (maxLines <= 0) {
            maxLines = 1;
        }
        source_pane_mode_t mode = st->viewMode;
        switch (key) {
        case SDLK_UP: {
            int amount = source_pane_arrowScrollAmount(mods, maxLines);
            source_pane_adjustScroll(st, mode, -amount);
            return 1;
        }
        case SDLK_DOWN: {
            int amount = source_pane_arrowScrollAmount(mods, maxLines);
            source_pane_adjustScroll(st, mode, amount);
            return 1;
        }
        case SDLK_PAGEUP:
            source_pane_adjustScroll(st, mode, -maxLines);
            return 1;
        case SDLK_PAGEDOWN:
            source_pane_adjustScroll(st, mode, maxLines);
            return 1;
        case SDLK_HOME:
            source_pane_scrollToStart(st, mode);
            return 1;
        case SDLK_END:
            source_pane_scrollToEnd(st, mode, maxLines);
            return 1;
        default:
            break;
        }
    }
    if ((mods & KMOD_CTRL) != 0 || (mods & KMOD_GUI) != 0) {
        if (key == SDLK_s) {
            source_pane_searchFind(st, st->ownerPane, ctx, 1, 1);
            return 1;
        }
        if (key == SDLK_r) {
            source_pane_searchFind(st, st->ownerPane, ctx, -1, 1);
            return 1;
        }
    }
    return 0;
}

void
source_pane_freeFrozenAsm(source_pane_state_t *st)
{
    if (!st) {
        return;
    }
    if (st->frozenAsmLines) {
        for (int i = 0; i < st->frozenAsmCount; ++i) {
            alloc_free(st->frozenAsmLines[i]);
        }
        alloc_free(st->frozenAsmLines);
    }
    alloc_free(st->frozenAsmAddrs);
    st->frozenAsmLines = NULL;
    st->frozenAsmAddrs = NULL;
    st->frozenAsmCount = 0;
    st->frozenAsmAnchorAddr = 0;
    st->frozenAsmStartIndex = 0;
    st->frozenAsmMaxLines = 0;
}

static void
source_pane_trackPosition(source_pane_state_t *st)
{
    if (!st) {
        return;
    }
    if (st->overrideActive) {
        return;
    }
    unsigned long curAddr = 0;
    (void)machine_findReg(&debugger.machine, "PC", &curAddr);
    curAddr &= 0x00ffffffu;
    if (curAddr != st->lastPcAddr) {
        if (!st->scrollLocked) {
            source_pane_clearFunctionScrollLock(st);
            st->manualSrcActive = 0;
        }
    }
    st->lastPcAddr = curAddr;
}

static void
source_pane_updateSourceLocation(source_pane_state_t *st, int allowWhileRunning)
{
    if (!st) {
        return;
    }
    if (!allowWhileRunning && !st->overrideActive && machine_getRunning(debugger.machine)) {
        return;
    }
    unsigned long pc = 0;
    if (st->overrideActive) {
        pc = (unsigned long)st->overrideAddr;
    } else {
        (void)machine_findReg(&debugger.machine, "PC", &pc);
    }
    pc &= 0x00ffffffu;
    if (st->lastResolvedPc == (uint64_t)pc && st->curSrcLine > 0 && st->curSrcPath[0]) {
        return;
    }
    st->lastResolvedPc = (uint64_t)pc;
    st->curSrcLine = 0;
    st->curSrcPath[0] = '\0';

    const char *elf = debugger.libretro.exePath;
    if (!elf || !*elf || !debugger.elfValid) {
        return;
    }
    if (!addr2line_start(elf)) {
        return;
    }
    int line = 0;
    char path[PATH_MAX];
    if (addr2line_resolve((uint64_t)pc, path, sizeof(path), &line)) {
        source_pane_resolveSourcePath(path, st->curSrcPath, sizeof(st->curSrcPath));
        st->curSrcLine = line;
    }
}

static int
source_pane_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self; (void)ctx; (void)availW;
    return 0;
}

static void
source_pane_layoutComp(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static const char *
source_pane_modeValue(source_pane_mode_t mode)
{
    if (mode == source_pane_mode_c) {
        return "c";
    }
    if (mode == source_pane_mode_cpr) {
        return "cpr";
    }
    if (mode == source_pane_mode_h) {
        return "hex";
    }
    return "asm";
}

static source_pane_mode_t
source_pane_modeFromValue(const char *value)
{
    if (!value || !*value) {
        return source_pane_mode_a;
    }
    if (strcmp(value, "c") == 0) {
        return source_pane_mode_c;
    }
    if (strcmp(value, "cpr") == 0) {
        return source_pane_mode_cpr;
    }
    if (strcmp(value, "hex") == 0) {
        return source_pane_mode_h;
    }
    return source_pane_mode_a;
}

static int
source_pane_modePersistValue(source_pane_mode_t mode)
{
    if (mode == source_pane_mode_c) {
        return 0;
    }
    if (mode == source_pane_mode_cpr) {
        return 4;
    }
    if (mode == source_pane_mode_h) {
        return 3;
    }
    return 2;
}

static void
source_pane_refreshModeOptions(e9ui_component_t *comp, source_pane_state_t *st)
{
    static const e9ui_textbox_option_t modeOptionsBase[] = {
        { .value = "c",   .label = "SRC" },
        { .value = "asm", .label = "ASM" },
        { .value = "hex", .label = "HEX" },
    };
    static const e9ui_textbox_option_t modeOptionsAmiga[] = {
        { .value = "c",   .label = "SRC" },
        { .value = "asm", .label = "ASM" },
        { .value = "hex", .label = "HEX" },
        { .value = "cpr", .label = "CPR" },
    };

    if (!comp || !st || !st->toggleBtnMeta) {
        return;
    }
    e9ui_component_t *select = e9ui_child_find(comp, st->toggleBtnMeta);
    if (!select) {
        return;
    }

    if (source_cpr_isModeAvailable()) {
        e9ui_textbox_setOptions(select,
                                modeOptionsAmiga,
                                (int)(sizeof(modeOptionsAmiga) / sizeof(modeOptionsAmiga[0])));
    } else {
        e9ui_textbox_setOptions(select,
                                modeOptionsBase,
                                (int)(sizeof(modeOptionsBase) / sizeof(modeOptionsBase[0])));
    }
    e9ui_textbox_setSelectedValue(select, source_pane_modeValue(st->viewMode));
}

static void
source_pane_persistSave(e9ui_component_t *self, e9ui_context_t *ctx, FILE *f)
{
  (void)ctx;
  if (!self || !self->persist_id) {
    return;
  }
  source_pane_state_t *st = (source_pane_state_t*)self->state;
  int m = st ? source_pane_modePersistValue(st->viewMode) : 0;
  fprintf(f, "comp.%s.mode=%d\n", self->persist_id, m);
}

static void
source_pane_persistLoad(e9ui_component_t *self, e9ui_context_t *ctx, const char *key, const char *value)
{
  (void)ctx;
  if (!self || !key || !value) {
    return;
  }
  if (strcmp(key, "mode") == 0) {
      int m = (int)strtol(value, NULL, 10);
      source_pane_mode_t mode = source_pane_mode_a;
      if (m == 0) {
          mode = source_pane_mode_c;
      } else if (m == 4) {
          mode = source_pane_mode_cpr;
      } else if (m == 3) {
          mode = source_pane_mode_h;
      }
      source_pane_setModeInternal(self, mode, 0);
  }
}

static int
source_pane_getAsmWindow(source_pane_state_t *st, int maxLines, uint64_t *out_curAddr,
                         const char ***out_lines, const uint64_t **out_addrs, int *out_count)
{
    if (out_curAddr) {
        *out_curAddr = 0;
    }
    if (out_lines) {
        *out_lines = NULL;
    }
    if (out_addrs) {
        *out_addrs = NULL;
    }
    if (out_count) {
        *out_count = 0;
    }

    if (!st || maxLines <= 0 || !out_lines || !out_addrs || !out_count || !out_curAddr) {
        return 0;
    }

    int streaming = (dasm_getFlags() & DASM_IFACE_FLAG_STREAMING) ? 1 : 0;
    int total = dasm_getTotal();
    if (!streaming && total <= 0) {
        return 0;
    }

    int freezeWhileRunning = source_pane_shouldFreezeAsmWhileRunning(st);
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
    *out_curAddr = curAddr;

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
        lines = (const char**)st->frozenAsmLines;
        addrs = (const uint64_t*)st->frozenAsmAddrs;
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
            st->frozenAsmLines = (char**)alloc_calloc((size_t)count, sizeof(*st->frozenAsmLines));
            st->frozenAsmAddrs = (uint64_t*)alloc_calloc((size_t)count, sizeof(*st->frozenAsmAddrs));
            if (st->frozenAsmLines && st->frozenAsmAddrs) {
                for (int i = 0; i < count; ++i) {
                    st->frozenAsmLines[i] = alloc_strdup(lines[i] ? lines[i] : "");
                    st->frozenAsmAddrs[i] = addrs[i];
                }
                st->frozenAsmCount = count;
                st->frozenAsmStartIndex = first;
                st->frozenAsmMaxLines = maxLines;
                st->frozenAsmAnchorAddr = windowAnchorAddr;
                lines = (const char**)st->frozenAsmLines;
                addrs = (const uint64_t*)st->frozenAsmAddrs;
                first = st->frozenAsmStartIndex;
                count = st->frozenAsmCount;
            }
        }
    }

    *out_lines = lines;
    *out_addrs = addrs;
    *out_count = count;
    (void)first;
    return 1;
}

static int
source_pane_parseHexNibble(char c)
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
source_pane_parseInlineHexBytes(const char *text, uint8_t *outBytes, int wantCount)
{
    int byteIndex = 0;
    int hiNibble = -1;

    if (!text || !outBytes || wantCount <= 0) {
        return 0;
    }

    for (const char *p = text; *p; ++p) {
        if (isspace((unsigned char)*p)) {
            continue;
        }
        int nibble = source_pane_parseHexNibble(*p);
        if (nibble < 0) {
            return 0;
        }
        if (hiNibble < 0) {
            hiNibble = nibble;
            continue;
        }
        if (byteIndex >= wantCount) {
            return 0;
        }
        outBytes[byteIndex++] = (uint8_t)((hiNibble << 4) | nibble);
        hiNibble = -1;
    }

    if (hiNibble >= 0) {
        return 0;
    }
    return byteIndex == wantCount ? 1 : 0;
}

int
source_pane_parseInlineHexWord(const char *text, uint16_t *outWord)
{
    char digits[5];
    int count = 0;

    if (!text || !outWord) {
        return 0;
    }

    for (const char *p = text; *p; ++p) {
        if (isspace((unsigned char)*p)) {
            continue;
        }
        int nibble = source_pane_parseHexNibble(*p);
        if (nibble < 0 || count >= 4) {
            return 0;
        }
        digits[count++] = *p;
    }

    if (count != 4) {
        return 0;
    }
    digits[4] = '\0';
    *outWord = (uint16_t)strtoul(digits, NULL, 16);
    return 1;
}

static void
source_pane_formatInlineHexBytes(char *dst, size_t cap, const uint8_t *bytes, int count)
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

static e9ui_component_t *
source_pane_inlineEditComponent(source_pane_state_t *st)
{
    if (!st || !st->ownerPane || !st->inlineEditMeta) {
        return NULL;
    }
    return e9ui_child_find(st->ownerPane, st->inlineEditMeta);
}

void
source_pane_inlineEditCancel(source_pane_state_t *st, e9ui_context_t *ctx)
{
    e9ui_component_t *editor = NULL;

    if (!st) {
        return;
    }

    editor = source_pane_inlineEditComponent(st);
    st->inlineEditActive = 0;
    st->inlineEditKind = source_pane_inline_edit_none;
    st->inlineEditMode = source_pane_mode_c;
    st->inlineEditAddr = 0;
    st->inlineEditByteCount = 0;
    st->inlineEditWord1 = 0;
    st->inlineEditWord2 = 0;
    st->inlineEditRect = (SDL_Rect){0, 0, 0, 0};
    if (editor) {
        e9ui_textbox_setOptions(editor, NULL, 0);
        e9ui_textbox_setText(editor, "");
        e9ui_setHidden(editor, 1);
    }
    if (ctx && e9ui_getFocus(ctx) == editor) {
        e9ui_setFocus(ctx, st->ownerPane);
    }
}

void
source_pane_inlineEditRefreshAfterWrite(source_pane_state_t *st)
{
    if (!st) {
        return;
    }
    st->frozenActive = 0;
    source_pane_freeFrozenAsm(st);
}

static int
source_pane_writeHexBytes(uint32_t addr, const uint8_t *bytes, int count)
{
    int i = 0;

    if (!bytes || count <= 0) {
        return 0;
    }

    while (i < count) {
        if ((i + 1) < count) {
            uint16_t word = (uint16_t)(((uint16_t)bytes[i] << 8) | (uint16_t)bytes[i + 1]);
            if (!libretro_host_debugWriteMemory(addr + (uint32_t)i, (uint32_t)word, 2)) {
                return 0;
            }
            i += 2;
            continue;
        }
        if (!libretro_host_debugWriteMemory(addr + (uint32_t)i, (uint32_t)bytes[i], 1)) {
            return 0;
        }
        i += 1;
    }
    return 1;
}

static int
source_pane_verifyHexBytes(uint32_t addr, const uint8_t *bytes, int count)
{
    uint8_t check[16];

    if (!bytes || count <= 0 || count > (int)sizeof(check)) {
        return 0;
    }
    memset(check, 0, sizeof(check));
    if (!libretro_host_debugReadMemory(addr, check, (size_t)count)) {
        return 0;
    }
    return memcmp(check, bytes, (size_t)count) == 0 ? 1 : 0;
}

static int
source_pane_inlineEditCommit(source_pane_state_t *st, e9ui_context_t *ctx)
{
    e9ui_component_t *editor = NULL;
    const char *text = NULL;
    int cprResult = 0;

    if (!st || !st->inlineEditActive) {
        return 0;
    }
    if (machine_getRunning(debugger.machine)) {
        e9ui_showTransientMessage("PAUSE TO EDIT MEMORY");
        return 0;
    }

    editor = source_pane_inlineEditComponent(st);
    if (!editor) {
        source_pane_inlineEditCancel(st, ctx);
        return 0;
    }
    text = e9ui_textbox_getText(editor);

    cprResult = source_cpr_commitInlineEdit(st, ctx, editor, text);
    if (cprResult != 0) {
        return cprResult > 0 ? 1 : 0;
    }

    if (st->inlineEditKind == source_pane_inline_edit_hex_bytes) {
        uint8_t bytes[16];
        memset(bytes, 0, sizeof(bytes));
        if (!source_pane_parseInlineHexBytes(text, bytes, st->inlineEditByteCount)) {
            e9ui_showTransientMessage("INVALID HEX FORMAT");
            e9ui_textbox_selectAllExternal(editor);
            return 0;
        }
        if (!source_pane_writeHexBytes(st->inlineEditAddr, bytes, st->inlineEditByteCount)) {
            e9ui_showTransientMessage("WRITE FAILED - NO CORE SUPPORT?");
            e9ui_textbox_selectAllExternal(editor);
            return 0;
        }
        if (!source_pane_verifyHexBytes(st->inlineEditAddr, bytes, st->inlineEditByteCount)) {
            e9ui_showTransientMessage("UNABLE TO WRITE DATA - ROM ?");
            e9ui_textbox_selectAllExternal(editor);
            return 0;
        }
        source_pane_inlineEditRefreshAfterWrite(st);
        source_pane_inlineEditCancel(st, ctx);
        return 1;
    }

    source_pane_inlineEditCancel(st, ctx);
    return 0;
}

static void
source_pane_inlineEditSubmitted(e9ui_context_t *ctx, void *user)
{
    source_pane_state_t *st = (source_pane_state_t *)user;
    (void)source_pane_inlineEditCommit(st, ctx);
}

static int
source_pane_inlineEditKey(e9ui_context_t *ctx, SDL_Keycode key, SDL_Keymod mods, void *user)
{
    source_pane_state_t *st = (source_pane_state_t *)user;
    (void)mods;

    if (!st || !st->inlineEditActive) {
        return 0;
    }
    if (key == SDLK_ESCAPE) {
        source_pane_inlineEditCancel(st, ctx);
        return 1;
    }
    return 0;
}

int
source_pane_beginInlineEdit(source_pane_state_t *st, e9ui_context_t *ctx, source_pane_mode_t mode,
                            source_pane_inline_edit_kind_t kind, uint32_t addr, int byteCount,
                            uint16_t word1, uint16_t word2, const char *text, SDL_Rect rect)
{
    e9ui_component_t *editor = NULL;

    if (!st || !ctx || !text || byteCount <= 0) {
        return 0;
    }
    if (machine_getRunning(debugger.machine)) {
        e9ui_showTransientMessage("PAUSE TO EDIT MEMORY");
        return 0;
    }

    editor = source_pane_inlineEditComponent(st);
    if (!editor) {
        return 0;
    }

    st->inlineEditActive = 1;
    st->inlineEditKind = kind;
    st->inlineEditMode = mode;
    st->inlineEditAddr = addr;
    st->inlineEditByteCount = byteCount;
    st->inlineEditWord1 = word1;
    st->inlineEditWord2 = word2;
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
    if (kind == source_pane_inline_edit_cpr_reg && source_cpr_buildRegisterOptions(st)) {
        e9ui_textbox_setOptions(editor, st->cprRegisterOptions, st->cprRegisterOptionCount);
    } else {
        e9ui_textbox_setOptions(editor, NULL, 0);
    }
    e9ui_textbox_setText(editor, text);
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
    e9ui_textbox_selectAllExternal(editor);
    return 1;
}

static void
source_pane_renderAsm(e9ui_component_t *self, e9ui_context_t *ctx)
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

    source_pane_state_t *st = (source_pane_state_t*)self->state;
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
    if (!source_pane_getAsmWindow(st, drawMaxLines, &curAddr, &lines, &addrs, &count)) {
        SDL_Color icol = (SDL_Color){200,160,160,255};
        source_pane_renderStatusMessage(ctx, useFont, contentArea, padPx, icol, "No disassembly available");
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

    SDL_Color txt = (SDL_Color){220,220,220,255};
    SDL_Color lno = (SDL_Color){160,160,200,255};
    SDL_Color lno_bp_on = (SDL_Color){120,200,120,255};
    SDL_Color lno_bp_off = (SDL_Color){200,140,60,255};
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
        snprintf(abuf, sizeof(abuf), "%0*llX", hexw, (unsigned long long)a);
        int nw = 0;
        int nh = 0;
        TTF_SizeText(useFont, abuf, &nw, &nh);
        int lnx = contentArea.x + padPx + (gutterW - nw);
        SDL_Color use_col = lno;
        machine_breakpoint_t *bp = machine_findBreakpointByAddr(&debugger.machine, (uint32_t)a);
        if (bp) {
            use_col = bp->enabled ? lno_bp_on : lno_bp_off;
        }
        void *addrBucket = st ? (void*)&st->bucketAddr : (void*)self;
        void *sourceBucket = st ? (void*)&st->bucketSource : (void*)self;
        e9ui_text_select_drawText(ctx, self, useFont, abuf, use_col, lnx, y,
                                  metrics.lineHeight, 0, addrBucket, 1, 1);
        source_pane_renderAsmLineHighlighted(ctx, self, useFont, ins, 0, txt, textX, y,
                                             metrics.lineHeight, hitW, sourceBucket);
        y += metrics.lineHeight;
        if (y > clipBottom) {
            break;
        }
    }
}

static int
source_pane_beginInlineHexEditAtPoint(e9ui_component_t *self, e9ui_context_t *ctx, source_pane_state_t *st,
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
    if (!source_pane_getAsmWindow(st, drawMaxLines, &curAddr, &lines, &addrs, &count)) {
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
            if (libretro_host_debugDisassembleQuick((uint32_t)a, tmp, sizeof(tmp), &len) && len > 0 && len <= 64) {
                wantBytes = len;
            }
        }
        if (wantBytes > 16) {
            wantBytes = 16;
        }
        if (!libretro_host_debugReadMemory((uint32_t)a, bytes, wantBytes)) {
            y += metrics.lineHeight;
            continue;
        }

        source_pane_formatInlineHexBytes(editText, sizeof(editText), bytes, (int)wantBytes);
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
                                               source_pane_mode_h,
                                               source_pane_inline_edit_hex_bytes,
                                               (uint32_t)a,
                                               (int)wantBytes,
                                               0,
                                               0,
                                               editText,
                                               rect);
        }
        y += metrics.lineHeight;
    }

    return 0;
}

static void
source_pane_renderHex(e9ui_component_t *self, e9ui_context_t *ctx)
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

    source_pane_state_t *st = (source_pane_state_t*)self->state;
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
    if (!source_pane_getAsmWindow(st, drawMaxLines, &curAddr, &lines, &addrs, &count)) {
        SDL_Color icol = (SDL_Color){200,160,160,255};
        source_pane_renderStatusMessage(ctx, useFont, contentArea, padPx, icol, "No disassembly available");
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

    SDL_Color txt = (SDL_Color){220,220,220,255};
    SDL_Color lno = (SDL_Color){160,160,200,255};
    SDL_Color lno_bp_on = (SDL_Color){120,200,120,255};
    SDL_Color lno_bp_off = (SDL_Color){200,140,60,255};
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
        snprintf(abuf, sizeof(abuf), "%0*llX", hexw, (unsigned long long)a);
        int nw = 0;
        int nh = 0;
        TTF_SizeText(useFont, abuf, &nw, &nh);
        int lnx = contentArea.x + padPx + (gutterW - nw);
        SDL_Color use_col = lno;
        machine_breakpoint_t *bp = machine_findBreakpointByAddr(&debugger.machine, (uint32_t)a);
        if (bp) {
            use_col = bp->enabled ? lno_bp_on : lno_bp_off;
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
            if (libretro_host_debugDisassembleQuick((uint32_t)a, tmp, sizeof(tmp), &len) && len > 0 && len <= 64) {
                wantBytes = len;
            }
        }
        if (wantBytes > 16) {
            wantBytes = 16;
        }

        uint8_t bytes[16];
        memset(bytes, 0, sizeof(bytes));
        int gotBytes = libretro_host_debugReadMemory((uint32_t)a, bytes, wantBytes) ? 1 : 0;

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

        void *addrBucket = st ? (void*)&st->bucketAddr : (void*)self;
        void *sourceBucket = st ? (void*)&st->bucketSource : (void*)self;
        e9ui_text_select_drawText(ctx, self, useFont, abuf, use_col, lnx, y,
                                  metrics.lineHeight, 0, addrBucket, 1, 1);
        source_pane_renderAsmLineHighlighted(ctx, self, useFont, linebuf, (int)strlen(hexbuf), txt,
                                             textX, y, metrics.lineHeight, hitW, sourceBucket);

        y += metrics.lineHeight;
        if (y > clipBottom) {
            break;
        }
    }
}

static void
source_pane_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    TTF_Font *useFont = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    SDL_Rect area = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    source_pane_state_t *st = (source_pane_state_t*)self->state;
    if (st) {
        if (st->viewMode == source_pane_mode_cpr && !source_cpr_isModeAvailable()) {
            source_pane_setModeInternal(self, source_pane_mode_h, 0);
        }
        source_pane_refreshModeOptions(self, st);
        source_pane_mode_t stepMode = st->viewMode;
        int stepEnabled = source_pane_areAsmViewStepButtonsEnabled(st);
        source_pane_step_buttons_action_ctx_t actionCtx = {
            self,
            ctx,
            st,
            stepMode
        };
        int topInsetPx = source_pane_overlayTopRowHeight(self, ctx, st);
        e9ui_step_buttons_tick(ctx,
                               self->bounds,
                               topInsetPx,
                               stepEnabled,
                               &st->asmStepButtons,
                               &actionCtx,
                               source_pane_stepButtonsOnAction);
    }
    int padPx = 10;
    SDL_Rect contentArea = source_pane_getContentArea(self, ctx, padPx);
    SDL_bool hadClip = SDL_FALSE;
    SDL_Rect prevClip = {0};
    source_pane_pushRenderClip(ctx, &area, &hadClip, &prevClip);
    SDL_SetRenderDrawColor(ctx->renderer, 20, 20, 20, 255);
    SDL_RenderFillRect(ctx->renderer, &area);

    if (!useFont) {
        goto done;
    }
    int freezeWhileRunning = st ? source_pane_shouldFreezeAsmWhileRunning(st) : 0;
    if (st && st->frozenActive && !freezeWhileRunning && !st->scrollLocked) {
        st->frozenActive = 0;
        source_pane_freeFrozenAsm(st);
    }
    if (st && !source_pane_isAsmLikeMode(st->viewMode) &&
        (st->frozenActive || st->frozenAsmLines)) {
        st->frozenActive = 0;
        source_pane_freeFrozenAsm(st);
    }
    if (st && st->viewMode == source_pane_mode_a) {
        source_pane_refreshAsmSymbols(self, st);
        source_pane_renderAsm(self, ctx);
        goto done;
    }
    if (st && st->viewMode == source_pane_mode_h) {
        source_pane_refreshAsmSymbols(self, st);
        source_pane_renderHex(self, ctx);
        goto done;
    }
    if (st && st->viewMode == source_pane_mode_cpr) {
        source_pane_refreshAsmSymbols(self, st);
        source_cpr_render(self, ctx);
        goto done;
    }
    if (st && !freezeWhileRunning) {
        source_pane_trackPosition(st);
    }
    if (st) {
        source_pane_updateSourceLocation(st, st->scrollLocked ? 1 : 0);
        source_pane_refreshSourceFiles(self, st);
    }
    int manualView = st && st->manualSrcActive && st->manualSrcPath;
    const char *path = manualView ? st->manualSrcPath : (st ? st->curSrcPath : NULL);
    int curLine = manualView ? 0 : (st ? st->curSrcLine : 0);
    int pcHighlightLine = source_pane_resolveCurrentHighlightLine(st, path);
    if (st) {
        const char *functionPath = path;
        if (st->manualSrcActive && st->manualSrcPath && st->manualSrcPath[0] == '\0') {
            functionPath = NULL;
        }
        source_pane_refreshSourceFunctions(self, st, functionPath);
        if (!st->functionScrollLock && !manualView) {
            source_pane_trackCurrentFunction(self, st, path, curLine);
        }
    }
    if (!path || !*path || (!manualView && curLine <= 0)) {
        SDL_Color icol = (SDL_Color){200,160,160,255};
        source_pane_renderStatusMessage(ctx, useFont, contentArea, padPx, icol, "No source code available");
        goto done;
    }
    source_pane_line_metrics_t metrics = source_pane_computeLineMetrics(self, ctx, useFont, padPx);
    if (metrics.innerHeight <= 0) {
        goto done;
    }
    int maxLines = metrics.maxLines;
    if (maxLines <= 0) {
        maxLines = 1;
    }
    int drawMaxLines = maxLines + 1;
    int start = 1;
    if (!manualView) {
        start = curLine - (maxLines / 2);
        if (start < 1) {
            start = 1;
        }
    }
    if (st && st->scrollLocked) {
        start = st->scrollLine;
        if (start < 1) {
            start = 1;
        }
    }
    int end = start + drawMaxLines - 1;

    const char **lines = NULL;
    int count = 0;
    int first = 0;
    int total = 0;
    if (!source_getRange(path, start, end, &lines, &count, &first, &total)) {
        SDL_Color icol = (SDL_Color){200,160,160,255};
        source_pane_renderStatusMessage(ctx, useFont, contentArea, padPx, icol, "Failed to load source code");
        goto done;
    }
    // Adjust start if near end of file for better centering
    if (count < drawMaxLines && total > 0) {
        int missing = drawMaxLines - count;
        int altStart = first - missing;
        if (altStart < 1) {
            altStart = 1;
        }
        int altEnd = altStart + drawMaxLines - 1;
        if (altEnd > total) {
            altEnd = total;
        }
        source_getRange(path, altStart, altEnd, &lines, &count, &first, &total);
    }

    if (st) {
        st->scrollLine = first;
    }

    // Compute gutter width based on total line count
    int digits = 1;
    int tmp_total = (total > 0) ? total : (first + count - 1);
    for (int v = tmp_total; v >= 10; v /= 10) {
        digits++;
    }
    if (digits < 3) {
        digits = 3;
    }
    char zeros[16];
    if (digits >= (int)sizeof(zeros)) {
        digits = (int)sizeof(zeros) - 1;
    }
    for (int i=0; i<digits; ++i) {
        zeros[i] = '8';
    }
    zeros[digits] = '\0';
    int gutterW = 0, th = 0;
    TTF_SizeText(useFont, zeros, &gutterW, &th);
    int gutterPad = e9ui_scale_px(ctx, 16);
    // Draw gutter background
    SDL_SetRenderDrawColor(ctx->renderer, 26, 26, 30, 255);
    SDL_Rect gutter = { contentArea.x, contentArea.y, padPx + gutterW + gutterPad, contentArea.h };
    SDL_RenderFillRect(ctx->renderer, &gutter);

    int y = contentArea.y + padPx;
    int lineHeight = metrics.lineHeight;
    int clipBottom = contentArea.y + contentArea.h + lineHeight;
    SDL_Color txt = (SDL_Color){220,220,220,255};
    SDL_Color lno = (SDL_Color){160,160,180,255};
    SDL_Color lno_bp_on = (SDL_Color){120,200,120,255};
    SDL_Color lno_bp_off = (SDL_Color){200,140,60,255};
    int textX = contentArea.x + padPx + gutterW + gutterPad;
    int hitW = contentArea.x + contentArea.w - textX - padPx;
    if (hitW < 0) {
        hitW = 0;
    }
    if (st) {
        int scrollY = 0;
        int viewW = hitW > 0 ? hitW : 1;
        e9ui_scrollbar_clamp(viewW, 1, st->cContentPixelWidth, 1, &st->cScrollX, &scrollY);
        st->cContentPixelWidth = 0;
    }
    SDL_Rect textClip = { textX, contentArea.y, hitW, contentArea.h };
    int textDrawX = textX - (st ? st->cScrollX : 0);
    const machine_breakpoint_t *bps = NULL;
    int bp_count = 0;
    if (machine_getBreakpoints(&debugger.machine, &bps, &bp_count)) {
        for (int i = 0; i < bp_count; ++i) {
            machine_breakpoint_t *bp = (machine_breakpoint_t*)&bps[i];
            if (bp->line <= 0 || !bp->file[0]) {
                breakpoints_resolveLocation(bp);
            }
        }
    } else {
        bps = NULL;
        bp_count = 0;
    }
    for (int i=0; i<count; ++i) {
        const char *ln = lines[i] ? lines[i] : "";
        int lineNo = first + i;
        // Highlight current line (blue shade)
        if (pcHighlightLine > 0 && lineNo == pcHighlightLine) {
            SDL_SetRenderDrawColor(ctx->renderer, 40, 72, 138, 255);
            SDL_Rect hl = { area.x + 2, y - 2, area.w - 4, lineHeight + 4 };
            SDL_RenderFillRect(ctx->renderer, &hl);
        }
        if (st && st->searchActive && st->searchMatchValid && lineNo == st->searchMatchLine) {
            SDL_bool hadTextClip = SDL_FALSE;
            SDL_Rect prevTextClip = {0};
            source_pane_pushRenderClip(ctx, &textClip, &hadTextClip, &prevTextClip);
            int line_len = (int)strlen(ln);
            int start_col = st->searchMatchColumn;
            int match_len = st->searchMatchLength;
            if (start_col < 0) {
                start_col = 0;
            }
            if (start_col > line_len) {
                start_col = line_len;
            }
            if (match_len < 0) {
                match_len = 0;
            }
            if (start_col + match_len > line_len) {
                match_len = line_len - start_col;
            }
            if (match_len > 0) {
                int hx = textDrawX + source_pane_measureSegment(useFont, ln, 0, start_col);
                int hw = source_pane_measureSegment(useFont, ln, start_col, match_len);
                SDL_SetRenderDrawColor(ctx->renderer, 186, 152, 62, 196);
                SDL_Rect mhl = { hx, y - 1, hw, lineHeight + 2 };
                SDL_RenderFillRect(ctx->renderer, &mhl);
            }
            source_pane_popRenderClip(ctx, hadTextClip, &prevTextClip);
        }
        // Line number (right-aligned in gutter)
        char numbuf[16];
        snprintf(numbuf, sizeof(numbuf), "%d", lineNo);
        int nw = 0, nh = 0;
        if (useFont) {
            TTF_SizeText(useFont, numbuf, &nw, &nh);
        }
        int lnx = contentArea.x + padPx + (gutterW - nw);
        if (useFont) {
            int nsw = 0, nsh = 0;
            SDL_Color use_col = lno;
            machine_breakpoint_t *bp = source_pane_findBreakpointForLine(path, lineNo, bps, bp_count);
            if (bp) {
                use_col = bp->enabled ? lno_bp_on : lno_bp_off;
            }
            SDL_Texture *nt = e9ui_text_cache_getText(ctx->renderer, useFont, numbuf, use_col, &nsw, &nsh);
            if (nt) {
                SDL_Rect nr = { lnx, y, nsw, nsh };
                SDL_RenderCopy(ctx->renderer, nt, NULL, &nr);
            }
        }
        if (st) {
            int linePixelW = source_pane_measureSegment(useFont, ln, 0, (int)strlen(ln));
            if (linePixelW > st->cContentPixelWidth) {
                st->cContentPixelWidth = linePixelW;
            }
        }
        void *sourceBucket = st ? (void*)&st->bucketSource : (void*)self;
        SDL_bool hadTextClip = SDL_FALSE;
        SDL_Rect prevTextClip = {0};
        source_pane_pushRenderClip(ctx, &textClip, &hadTextClip, &prevTextClip);
        source_pane_renderHighlightedLine(ctx, self, useFont, path, lineNo, ln, txt,
                                          textDrawX, y, lineHeight, hitW, sourceBucket);
        source_pane_popRenderClip(ctx, hadTextClip, &prevTextClip);
        y += lineHeight;
        if (y > clipBottom) {
            break;
        }
    }
    if (st && st->hoverActive) {
        source_pane_updateHoverTooltip(self, ctx, st, NULL);
    }
    if (st && total > 0) {
        int topIndex = first > 0 ? (first - 1) : 0;
        int scrollY = 0;
        int viewW = hitW > 0 ? hitW : 1;
        e9ui_scrollbar_clamp(viewW, 1, st->cContentPixelWidth, 1, &st->cScrollX, &scrollY);
        e9ui_scrollbar_render(self,
                              ctx,
                              self->bounds,
                              viewW,
                              maxLines,
                              st->cContentPixelWidth,
                              total,
                              st->cScrollX,
                              topIndex);
    }

 done:
    {
      e9ui_component_t* overlay = e9ui_child_find(self, st->toggleBtnMeta);
      e9ui_component_t* lockButton = st && st->lockBtnMeta ? e9ui_child_find(self, st->lockBtnMeta) : NULL;
      e9ui_component_t* fileSelect = st && st->fileSelectMeta ? e9ui_child_find(self, st->fileSelectMeta) : NULL;
      e9ui_component_t* functionSelect = st && st->functionSelectMeta ? e9ui_child_find(self, st->functionSelectMeta) : NULL;
      e9ui_component_t* searchBox = st && st->searchBoxMeta ? e9ui_child_find(self, st->searchBoxMeta) : NULL;
      e9ui_component_t* asmSymbolSelect = st && st->asmSymbolSelectMeta ? e9ui_child_find(self, st->asmSymbolSelectMeta) : NULL;
      e9ui_component_t* asmAddress = st && st->asmAddressMeta ? e9ui_child_find(self, st->asmAddressMeta) : NULL;
      e9ui_component_t* inlineEdit = st && st->inlineEditMeta ? e9ui_child_find(self, st->inlineEditMeta) : NULL;
      source_pane_mode_t mode = source_pane_getMode(self);
      int rowX = self->bounds.x;
      int rowY = self->bounds.y;
      int rowW = self->bounds.w;
      int rowH = e9ui_scale_px(ctx, 30);
      int modeW = e9ui_scale_px(ctx, 24);
      int lockW = rowH;
      if (overlay && overlay->preferredHeight) {
          int ph = overlay->preferredHeight(overlay, ctx, rowW);
          if (ph > 0) {
              rowH = ph;
          }
      }
      {
          TTF_Font *modeFont = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
          int hexW = 0;
          if (modeFont && TTF_SizeText(modeFont, "HEX", &hexW, NULL) == 0) {
              int padW = e9ui_scale_px(ctx, 16);
              modeW = hexW + padW;
          }
      }
      if (lockButton) {
          int lockMeasureH = 0;
          e9ui_button_measure(lockButton, ctx, NULL, &lockMeasureH);
          if (lockMeasureH > rowH) {
              rowH = lockMeasureH;
          }
          lockW = rowH;
      }
      if (modeW + lockW > rowW) {
          int over = modeW + lockW - rowW;
          if (modeW > over) {
              modeW -= over;
          } else {
              modeW = 0;
          }
      }
      int middleW = rowW - modeW - lockW;
      if (middleW < 0) {
          middleW = 0;
      }
      int sourceW = middleW / 2;
      int functionW = middleW - sourceW;
      int asmSymbolW = (middleW * 3) / 4;
      int asmAddressW = middleW - asmSymbolW;

      if (overlay && modeW > 0) {
          e9ui_rect_t bounds = {
              rowX + rowW - modeW,
              rowY,
              modeW,
              rowH
          };
          if (overlay->layout) {
              overlay->layout(overlay, ctx, bounds);
          } else {
              overlay->bounds = bounds;
          }
          overlay->render(overlay, ctx);
      }
      if (lockButton && lockW > 0) {
          e9ui_rect_t bounds = {
              rowX,
              rowY,
              lockW,
              rowH
          };
          if (lockButton->layout) {
              lockButton->layout(lockButton, ctx, bounds);
          } else {
              lockButton->bounds = bounds;
          }
          lockButton->render(lockButton, ctx);
      }
      if (functionSelect) {
          e9ui_setHidden(functionSelect, mode == source_pane_mode_c ? 0 : 1);
          if (mode == source_pane_mode_c && functionW > 0) {
              e9ui_rect_t bounds = {
                  rowX + lockW + sourceW,
                  rowY,
                  functionW,
                  rowH
              };
              if (functionSelect->layout) {
                  functionSelect->layout(functionSelect, ctx, bounds);
              } else {
                  functionSelect->bounds = bounds;
              }
              functionSelect->render(functionSelect, ctx);
          }
      }
      if (fileSelect) {
          e9ui_setHidden(fileSelect, mode == source_pane_mode_c ? 0 : 1);
          if (mode == source_pane_mode_c && sourceW > 0) {
              e9ui_rect_t bounds = {
                  rowX + lockW,
                  rowY,
                  sourceW,
                  rowH
              };
              if (fileSelect->layout) {
                  fileSelect->layout(fileSelect, ctx, bounds);
              } else {
                  fileSelect->bounds = bounds;
              }
              fileSelect->render(fileSelect, ctx);
          }
      }
      if (asmSymbolSelect) {
          int showAsmControls = source_pane_isAsmLikeMode(mode);
          e9ui_setHidden(asmSymbolSelect, showAsmControls ? 0 : 1);
          if (showAsmControls && asmSymbolW > 0) {
              e9ui_rect_t bounds = {
                  rowX + lockW,
                  rowY,
                  asmSymbolW,
                  rowH
              };
              if (asmSymbolSelect->layout) {
                  asmSymbolSelect->layout(asmSymbolSelect, ctx, bounds);
              } else {
                  asmSymbolSelect->bounds = bounds;
              }
              asmSymbolSelect->render(asmSymbolSelect, ctx);
          }
      }
      if (asmAddress) {
          int showAsmControls = source_pane_isAsmLikeMode(mode);
          e9ui_setHidden(asmAddress, showAsmControls ? 0 : 1);
          if (showAsmControls && asmAddressW > 0) {
              e9ui_rect_t bounds = {
                  rowX + lockW + asmSymbolW,
                  rowY,
                  asmAddressW,
                  rowH
              };
              if (asmAddress->layout) {
                  asmAddress->layout(asmAddress, ctx, bounds);
              } else {
                  asmAddress->bounds = bounds;
              }
              asmAddress->render(asmAddress, ctx);
          }
      }
      if (searchBox) {
          int showSearch = (mode == source_pane_mode_c && st && st->searchActive) ? 1 : 0;
          e9ui_setHidden(searchBox, showSearch ? 0 : 1);
          if (showSearch) {
              e9ui_rect_t bounds = {
                  self->bounds.x,
                  self->bounds.y + self->bounds.h - rowH,
                  self->bounds.w,
                  rowH
              };
              if (searchBox->layout) {
                  searchBox->layout(searchBox, ctx, bounds);
              } else {
                  searchBox->bounds = bounds;
              }
              searchBox->render(searchBox, ctx);
          }
      }
      if (source_pane_areAsmViewStepButtonsEnabled(st)) {
          int topInsetPx = source_pane_overlayTopRowHeight(self, ctx, st);
          e9ui_step_buttons_render(ctx,
                                   self->bounds,
                                   topInsetPx,
                                   1,
                                   &st->asmStepButtons);
      }
      if (inlineEdit) {
          int showInlineEdit = st && st->inlineEditActive ? 1 : 0;
          e9ui_setHidden(inlineEdit, showInlineEdit ? 0 : 1);
          if (showInlineEdit) {
              e9ui_rect_t bounds = {
                  st->inlineEditRect.x,
                  st->inlineEditRect.y,
                  st->inlineEditRect.w,
                  st->inlineEditRect.h
              };
              if (inlineEdit->layout) {
                  inlineEdit->layout(inlineEdit, ctx, bounds);
              } else {
                  inlineEdit->bounds = bounds;
              }
              inlineEdit->render(inlineEdit, ctx);
          }
      }
    }
    source_pane_popRenderClip(ctx, hadClip, &prevClip);
}

static int
source_pane_handleEventComp(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    e9ui_component_t *inlineEdit = NULL;

    if (!self || !ev) {
        return 0;
    }
    source_pane_state_t *st = (source_pane_state_t*)self->state;
    if (st && st->viewMode == source_pane_mode_cpr && !source_cpr_isModeAvailable()) {
        source_pane_setModeInternal(self, source_pane_mode_h, 0);
    }
    source_pane_mode_t mode = st ? st->viewMode : source_pane_mode_c;
    inlineEdit = source_pane_inlineEditComponent(st);
    if (st && st->inlineEditActive && ctx && inlineEdit &&
        e9ui_getFocus(ctx) != inlineEdit &&
        e9ui_getFocus(ctx) != self) {
        source_pane_inlineEditCancel(st, ctx);
    }
    if (st && st->inlineEditActive && inlineEdit &&
        ev->type == SDL_MOUSEBUTTONDOWN &&
        ev->button.button == SDL_BUTTON_LEFT &&
        !source_pane_pointInBounds(inlineEdit, ev->button.x, ev->button.y) &&
        source_pane_pointInBounds(self, ev->button.x, ev->button.y)) {
        if (source_pane_inlineEditCommit(st, ctx)) {
            return 1;
        }
        return 1;
    }
    if (st) {
        int mx = 0;
        int my = 0;
        int hasPoint = 0;
        if (ev->type == SDL_MOUSEMOTION) {
            mx = ev->motion.x;
            my = ev->motion.y;
            hasPoint = 1;
        } else if (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP) {
            mx = ev->button.x;
            my = ev->button.y;
            hasPoint = 1;
        } else if (ev->type == SDL_MOUSEWHEEL) {
            mx = e9ui->mouseX;
            my = e9ui->mouseY;
            hasPoint = 1;
        }
        if (hasPoint) {
            e9ui_component_t *searchBox = st->searchBoxMeta ? e9ui_child_find(self, st->searchBoxMeta) : NULL;
            e9ui_component_t *controls[8] = {
                st->lockBtnMeta ? e9ui_child_find(self, st->lockBtnMeta) : NULL,
                st->toggleBtnMeta ? e9ui_child_find(self, st->toggleBtnMeta) : NULL,
                st->fileSelectMeta ? e9ui_child_find(self, st->fileSelectMeta) : NULL,
                st->functionSelectMeta ? e9ui_child_find(self, st->functionSelectMeta) : NULL,
                st->searchBoxMeta ? e9ui_child_find(self, st->searchBoxMeta) : NULL,
                st->asmSymbolSelectMeta ? e9ui_child_find(self, st->asmSymbolSelectMeta) : NULL,
                st->asmAddressMeta ? e9ui_child_find(self, st->asmAddressMeta) : NULL,
                inlineEdit
            };
            for (int i = 0; i < (int)(sizeof(controls) / sizeof(controls[0])); ++i) {
                if (!controls[i] || e9ui_getHidden(controls[i])) {
                    continue;
                }
                if (source_pane_pointInBounds(controls[i], mx, my)) {
                    if (ev->type == SDL_MOUSEWHEEL && controls[i] == searchBox) {
                        continue;
                    }
                    if (ev->type == SDL_MOUSEBUTTONDOWN && st->gutterPending) {
                        st->gutterPending = 0;
                    }
                    return 0;
                }
            }
        }
    }
    if (st && mode == source_pane_mode_c && ctx &&
        (ev->type == SDL_MOUSEMOTION || ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP)) {
        int totalLines = 0;
        int visibleLines = 0;
        int topIndex = 0;
        int scrollX = st->cScrollX;
        if (source_pane_getCScrollbarModel(self, ctx, st, &totalLines, &visibleLines, &topIndex)) {
            TTF_Font *useFont = source_pane_resolveFont(ctx);
            int viewW = 1;
            if (useFont) {
                const int padPx = 10;
                source_pane_line_metrics_t metrics = source_pane_computeLineMetrics(self, ctx, useFont, padPx);
                (void)metrics;
                int digits = 3;
                int tmp_total = totalLines > 0 ? totalLines : 1;
                for (int v = tmp_total; v >= 10; v /= 10) {
                    digits++;
                }
                char zeros[16];
                if (digits >= (int)sizeof(zeros)) {
                    digits = (int)sizeof(zeros) - 1;
                }
                for (int i = 0; i < digits; ++i) {
                    zeros[i] = '8';
                }
                zeros[digits] = '\0';
                int gutterW = 0;
                int th = 0;
                TTF_SizeText(useFont, zeros, &gutterW, &th);
                int gutterPad = e9ui_scale_px(ctx, 16);
                int textX = self->bounds.x + padPx + gutterW + gutterPad;
                viewW = self->bounds.x + self->bounds.w - textX - padPx;
                if (viewW < 1) {
                    viewW = 1;
                }
            }
            if (e9ui_scrollbar_handleEvent(self,
                                           ctx,
                                           ev,
                                           self->bounds,
                                           viewW,
                                           visibleLines,
                                           st->cContentPixelWidth,
                                           totalLines,
                                           &scrollX,
                                           &topIndex,
                                           &st->cScrollbar)) {
                st->cScrollX = scrollX;
                st->scrollLine = topIndex + 1;
                st->scrollLocked = 1;
                st->gutterPending = 0;
                source_pane_syncLockButtonVisual(st);
                return 1;
            }
        }
    }
    if (st && ctx && source_pane_isAsmLikeMode(mode)) {
        source_pane_step_buttons_action_ctx_t actionCtx = {
            self,
            ctx,
            st,
            mode
        };
        int topInsetPx = source_pane_overlayTopRowHeight(self, ctx, st);
        int stepEnabled = source_pane_areAsmViewStepButtonsEnabled(st);
        if (e9ui_step_buttons_handleEvent(ctx,
                                          ev,
                                          self->bounds,
                                          topInsetPx,
                                          stepEnabled,
                                          &st->asmStepButtons,
                                          &actionCtx,
                                          source_pane_stepButtonsOnAction)) {
            return 1;
        }
    }
    if (ev->type == SDL_MOUSEMOTION) {
        if (st && st->gutterPending) {
            int slop = ctx ? e9ui_scale_px(ctx, 4) : 4;
            int dx = ev->motion.x - st->gutterDownX;
            int dy = ev->motion.y - st->gutterDownY;
            if (dx * dx + dy * dy >= slop * slop) {
                st->gutterPending = 0;
            }
        }
        source_pane_updateHoverTooltip(self, ctx, st, ev);
        return 0;
    }
    if (ev->type == SDL_MOUSEBUTTONUP && ev->button.button == SDL_BUTTON_LEFT) {
        if (!st || !st->gutterPending) {
            return 0;
        }
        st->gutterPending = 0;
        int slop = ctx ? e9ui_scale_px(ctx, 4) : 4;
        int dx = ev->button.x - st->gutterDownX;
        int dy = ev->button.y - st->gutterDownY;
        if (dx * dx + dy * dy >= slop * slop) {
            return 0;
        }
        if (st->gutterMode == source_pane_mode_c) {
            const char *path = NULL;
            if (st->manualSrcActive && st->manualSrcPath) {
                path = st->manualSrcPath;
            } else {
                path = st->curSrcPath;
            }
            int lineNo = st->gutterLine;
            if (!path || !path[0] || lineNo <= 0) {
                return 0;
            }
            const machine_breakpoint_t *bps = NULL;
            int bp_count = 0;
            if (machine_getBreakpoints(&debugger.machine, &bps, &bp_count)) {
                for (int i = 0; i < bp_count; ++i) {
                    machine_breakpoint_t *bp = (machine_breakpoint_t*)&bps[i];
                    if (bp->line <= 0 || !bp->file[0]) {
                        breakpoints_resolveLocation(bp);
                    }
                }
            } else {
                bps = NULL;
                bp_count = 0;
            }
            if (source_pane_removeBreakpointsForLine(path, lineNo, bps, bp_count)) {
                breakpoints_markDirty();
                return 1;
            }
            if (source_pane_addBreakpointsForLine(path, lineNo)) {
                breakpoints_markDirty();
                return 1;
            }
            return 0;
        }
        if (source_pane_isCpuAsmLikeMode(st->gutterMode)) {
            uint32_t addr = st->gutterAddr;
            machine_breakpoint_t *existing = machine_findBreakpointByAddr(&debugger.machine, addr);
            if (existing) {
                if (machine_removeBreakpointByAddr(&debugger.machine, addr)) {
                    libretro_host_debugRemoveBreakpoint(addr);
                    breakpoints_markDirty();
                }
                return 1;
            }
            machine_breakpoint_t *bp = machine_addBreakpoint(&debugger.machine, addr, 1);
            if (bp) {
                breakpoints_resolveLocation(bp);
                libretro_host_debugAddBreakpoint(addr);
                breakpoints_markDirty();
                return 1;
            }
            return 0;
        }
        return 0;
    }
    if (ev->type == SDL_MOUSEWHEEL) {
        int mx = e9ui->mouseX;
        int my = e9ui->mouseY;
        if (source_pane_pointInBounds(self, mx, my)) {
            int wheelX = ev->wheel.x;
            int wheelY = ev->wheel.y;
            if (ev->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                wheelX = -wheelX;
                wheelY = -wheelY;
            }
            if (mode == source_pane_mode_c && wheelX != 0 && ctx) {
                TTF_Font *useFont = source_pane_resolveFont(ctx);
                if (useFont) {
                    const int padPx = 10;
                    int totalLines = 0;
                    int visibleLines = 0;
                    int topIndex = 0;
                    if (source_pane_getCScrollbarModel(self, ctx, st, &totalLines, &visibleLines, &topIndex)) {
                        int digits = 3;
                        int tmp_total = totalLines > 0 ? totalLines : 1;
                        for (int v = tmp_total; v >= 10; v /= 10) {
                            digits++;
                        }
                        char zeros[16];
                        if (digits >= (int)sizeof(zeros)) {
                            digits = (int)sizeof(zeros) - 1;
                        }
                        for (int i = 0; i < digits; ++i) {
                            zeros[i] = '8';
                        }
                        zeros[digits] = '\0';
                        int gutterW = 0;
                        int th = 0;
                        TTF_SizeText(useFont, zeros, &gutterW, &th);
                        int gutterPad = e9ui_scale_px(ctx, 16);
                        int textX = self->bounds.x + padPx + gutterW + gutterPad;
                        int viewW = self->bounds.x + self->bounds.w - textX - padPx;
                        if (viewW < 1) {
                            viewW = 1;
                        }
                        st->cScrollX -= wheelX * e9ui_scale_px(ctx, 24);
                        {
                            int scrollY = 0;
                            e9ui_scrollbar_clamp(viewW, 1, st->cContentPixelWidth, 1, &st->cScrollX, &scrollY);
                        }
                    }
                }
            }
            if (wheelY != 0) {
                const int linesPerTick = 1;
                int delta = wheelY * linesPerTick;
                source_pane_adjustScroll(st, mode, delta);
            }
            if (wheelX != 0 || wheelY != 0) {
                return 1;
            }
            return 0;
        }
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_LEFT) {
        int mx = ev->button.x;
        int my = ev->button.y;
        if (!source_pane_pointInBounds(self, mx, my)) {
            return 0;
        }
        if (st && !st->inlineEditActive && ev->button.clicks >= 2) {
            if (mode == source_pane_mode_h &&
                source_pane_beginInlineHexEditAtPoint(self, ctx, st, mx, my)) {
                return 1;
            }
            if (mode == source_pane_mode_cpr &&
                source_cpr_beginInlineRegisterEditAtPoint(self, ctx, st, mx, my)) {
                return 1;
            }
            if (mode == source_pane_mode_cpr &&
                source_cpr_beginInlineValueEditAtPoint(self, ctx, st, mx, my)) {
                return 1;
            }
            if (mode == source_pane_mode_cpr &&
                source_cpr_beginInlineWordsEditAtPoint(self, ctx, st, mx, my)) {
                return 1;
            }
        }
        TTF_Font *useFont = source_pane_resolveFont(ctx);
        if (!useFont) {
            return 0;
        }
        const int padPx = 10;
        if (mode == source_pane_mode_c) {
            source_pane_updateSourceLocation(st, 0);
            int manualView = st && st->manualSrcActive && st->manualSrcPath;
            const char *path = manualView ? st->manualSrcPath : (st ? st->curSrcPath : NULL);
            int curLine = manualView ? 0 : (st ? st->curSrcLine : 0);
            if (!path || !path[0] || (!manualView && curLine <= 0)) {
                return 0;
            }
            source_pane_line_metrics_t metrics = source_pane_computeLineMetrics(self, ctx, useFont, padPx);
            if (metrics.innerHeight <= 0) {
                return 0;
            }
            int maxLines = metrics.maxLines > 0 ? metrics.maxLines : 1;
            int start = 1;
            if (!manualView) {
                start = curLine - (maxLines / 2);
                if (start < 1) {
                    start = 1;
                }
            }
            if (st && st->scrollLocked) {
                start = st->scrollLine;
                if (start < 1) {
                    start = 1;
                }
            }
            int end = start + maxLines - 1;
            const char **lines = NULL;
            int count = 0;
            int first = 0;
            int total = 0;
            if (!source_getRange(path, start, end, &lines, &count, &first, &total)) {
                return 0;
            }
            if (count < maxLines && total > 0) {
                int missing = maxLines - count;
                int altStart = first - missing;
                if (altStart < 1) {
                    altStart = 1;
                }
                int altEnd = altStart + maxLines - 1;
                if (altEnd > total) {
                    altEnd = total;
                }
                source_getRange(path, altStart, altEnd, &lines, &count, &first, &total);
            }

            int digits = 1;
            int tmp_total = (total > 0) ? total : (first + count - 1);
            for (int v = tmp_total; v >= 10; v /= 10) {
                digits++;
            }
            if (digits < 3) {
                digits = 3;
            }
            char zeros[16];
            if (digits >= (int)sizeof(zeros)) {
                digits = (int)sizeof(zeros) - 1;
            }
            for (int i = 0; i < digits; ++i) {
                zeros[i] = '8';
            }
            zeros[digits] = '\0';
            int gutterW = 0;
            int th = 0;
            TTF_SizeText(useFont, zeros, &gutterW, &th);
            int gutterPad = e9ui_scale_px(ctx, 16);
            SDL_Rect contentArea = source_pane_getContentArea(self, ctx, padPx);
            int gutterRight = contentArea.x + padPx + gutterW + gutterPad;
            if (mx >= gutterRight) {
                return 0;
            }
            int row = (my - (contentArea.y + padPx)) / metrics.lineHeight;
            if (row < 0 || row >= count) {
                return 0;
            }
            int lineNo = first + row;
            st->gutterPending = 1;
            st->gutterMode = source_pane_mode_c;
            st->gutterLine = lineNo;
            st->gutterDownX = mx;
            st->gutterDownY = my;
            return 1;
        }
        if (source_pane_isCpuAsmLikeMode(mode)) {
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
            if (!source_pane_getAsmWindow(st, drawMaxLines, &curAddr, &lines, &addrs, &count)) {
                return 0;
            }
            (void)curAddr;
            (void)lines;
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
            SDL_Rect contentArea = source_pane_getContentArea(self, ctx, padPx);
            int gutterRight = contentArea.x + padPx + gutterW + gutterPad;
            if (mx >= gutterRight) {
                return 0;
            }
            int row = (my - (contentArea.y + padPx)) / metrics.lineHeight;
            if (row < 0 || row >= count) {
                return 0;
            }
            st->gutterPending = 1;
            st->gutterMode = mode;
            st->gutterAddr = (uint32_t)(addrs[row] & 0x00ffffffu);
            st->gutterDownX = mx;
            st->gutterDownY = my;
            return 1;
        }
    }
    if (ev->type == SDL_KEYDOWN && ctx && e9ui_getFocus(ctx) == self) {
        const int padPx = 10;
        TTF_Font *useFont = source_pane_resolveFont(ctx);
        int maxLines = 1;
        if (useFont) {
            source_pane_line_metrics_t metrics = source_pane_computeLineMetrics(self, ctx, useFont, padPx);
            maxLines = metrics.maxLines;
        }
        if (maxLines <= 0) {
            maxLines = 1;
        }
        SDL_Keycode kc = ev->key.keysym.sym;
        SDL_Keymod mods = ev->key.keysym.mod;
        int accel = ((mods & KMOD_CTRL) != 0 || (mods & KMOD_GUI) != 0) ? 1 : 0;
        if (mode == source_pane_mode_c && accel && kc == SDLK_s) {
            source_pane_searchOpen(st, ctx);
            source_pane_searchFind(st, self, ctx, 1, 1);
            return 1;
        }
        if (mode == source_pane_mode_c && accel && kc == SDLK_r) {
            source_pane_searchOpen(st, ctx);
            source_pane_searchFind(st, self, ctx, -1, 1);
            return 1;
        }
        if (mode == source_pane_mode_c && kc == SDLK_ESCAPE && st->searchActive) {
            source_pane_searchClose(st, ctx);
            return 1;
        }
        switch (kc) {
        case SDLK_PAGEUP:
            source_pane_adjustScroll(st, mode, -maxLines);
            return 1;
        case SDLK_PAGEDOWN:
            source_pane_adjustScroll(st, mode, maxLines);
            return 1;
        case SDLK_UP: {
            int amount = source_pane_arrowScrollAmount(mods, maxLines);
            source_pane_adjustScroll(st, mode, -amount);
            return 1;
        }
        case SDLK_DOWN: {
            int amount = source_pane_arrowScrollAmount(mods, maxLines);
            source_pane_adjustScroll(st, mode, amount);
            return 1;
        }
        case SDLK_HOME:
            source_pane_scrollToStart(st, mode);
            return 1;
        case SDLK_END:
            source_pane_scrollToEnd(st, mode, maxLines);
            return 1;
        default:
            break;
        }
    }
    return 0;
}

static void
source_pane_modeSelectChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *value, void *user)
{
    (void)ctx;
    (void)comp;
    e9ui_component_t *pane = (e9ui_component_t*)user;
    if (!pane || !value || !*value) {
        return;
    }
    source_pane_setMode(pane, source_pane_modeFromValue(value));
    config_saveConfig();
}

static void
source_pane_lockToggle(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    source_pane_state_t *st = (source_pane_state_t*)user;
    if (!st) {
        return;
    }
    if (st->inlineEditActive) {
        source_pane_inlineEditCancel(st, NULL);
    }
    int wasLocked = st->scrollLocked ? 1 : 0;
    st->scrollLocked = st->scrollLocked ? 0 : 1;
    st->scrollAnchorValid = 0;
    if (!st->scrollLocked) {
        source_pane_followCurrent(st);
        if (st->ownerPane) {
            if (st->viewMode == source_pane_mode_c) {
                e9ui_component_t *file_select = st->fileSelectMeta ? e9ui_child_find(st->ownerPane, st->fileSelectMeta) : NULL;
                e9ui_component_t *function_select = st->functionSelectMeta ? e9ui_child_find(st->ownerPane, st->functionSelectMeta) : NULL;
                if (file_select) {
                    e9ui_textbox_setText(file_select, "");
                }
                if (function_select) {
                    e9ui_textbox_setText(function_select, "");
                }
            } else if (source_pane_isAsmLikeMode(st->viewMode)) {
                e9ui_component_t *symbol_select = st->asmSymbolSelectMeta ? e9ui_child_find(st->ownerPane, st->asmSymbolSelectMeta) : NULL;
                e9ui_component_t *address_box = st->asmAddressMeta ? e9ui_child_find(st->ownerPane, st->asmAddressMeta) : NULL;
                if (symbol_select) {
                    e9ui_textbox_setOptions(symbol_select, NULL, 0);
                    e9ui_textbox_setText(symbol_select, "");
                    if (st->asmSymbolOptions && st->asmSymbolCount > 0) {
                        e9ui_textbox_setOptions(symbol_select, st->asmSymbolOptions, st->asmSymbolCount);
                    }
                }
                if (address_box) {
                    e9ui_textbox_setText(address_box, "");
                }
            }
        }
    } else if (!wasLocked) {
        source_pane_freeFrozenAsm(st);
    }
    if (!st->ownerPane || !st->lockBtnMeta) {
        return;
    }
    source_pane_syncLockButtonVisual(st);
}

static void
source_pane_asmSymbolSelectChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *value, void *user)
{
    (void)ctx;
    (void)comp;
    source_pane_state_t *st = (source_pane_state_t*)user;
    if (!st || !value || !value[0]) {
        return;
    }
    uint64_t addr = 0;
    if (!source_pane_parseHex64(value, &addr) || addr == 0) {
        int line = 0;
        const char *selected_file = NULL;
        if (!source_pane_parseFunctionValue(value, &line, &selected_file)) {
            return;
        }
        if (!selected_file || !selected_file[0] || line <= 0) {
            return;
        }
        const char *end = strchr(selected_file, '|');
        if (!end) {
            return;
        }
        size_t len = (size_t)(end - selected_file);
        if (len == 0 || len >= PATH_MAX) {
            return;
        }
        char file_path[PATH_MAX];
        memcpy(file_path, selected_file, len);
        file_path[len] = '\0';
        uint32_t runtime = 0;
        if (!source_pane_resolveFileLine(debugger.libretro.exePath, file_path, line, &runtime)) {
            return;
        }
        (void)base_map_debugToRuntime(BASE_MAP_SECTION_TEXT, runtime, &runtime);
        addr = (uint64_t)runtime;
    }
    uint64_t resolved = source_pane_resolveAsmLikeAnchorAddr(st, addr);
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
        e9ui_component_t *addr_box = e9ui_child_find(st->ownerPane, st->asmAddressMeta);
        if (addr_box) {
            int hexw = dasm_getAddrHexWidth();
            if (hexw < 6) {
                hexw = 6;
            }
            if (hexw > 16) {
                hexw = 16;
            }
            char buf[32];
            snprintf(buf, sizeof(buf), "%0*llX", hexw, (unsigned long long)(resolved & 0x00ffffffull));
            e9ui_textbox_setText(addr_box, buf);
        }
    }
    source_pane_setAsmAnchorLocked(st, resolved);
}

static void
source_pane_asmAddressSubmitted(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    source_pane_state_t *st = (source_pane_state_t*)user;
    if (!st || !st->ownerPane || !st->asmAddressMeta) {
        return;
    }
    e9ui_component_t *addr_box = e9ui_child_find(st->ownerPane, st->asmAddressMeta);
    if (!addr_box) {
        return;
    }
    const char *text = e9ui_textbox_getText(addr_box);
    uint64_t addr = 0;
    if (!source_pane_parseHex64(text, &addr) || addr == 0) {
        return;
    }
    uint64_t resolved = source_pane_resolveAsmLikeAnchorAddr(st, addr);
    int hexw = dasm_getAddrHexWidth();
    if (hexw < 6) {
        hexw = 6;
    }
    if (hexw > 16) {
        hexw = 16;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%0*llX", hexw, (unsigned long long)(resolved & 0x00ffffffull));
    e9ui_textbox_setText(addr_box, buf);

    if (st->asmSymbolSelectMeta) {
        e9ui_component_t *symbol_select = e9ui_child_find(st->ownerPane, st->asmSymbolSelectMeta);
        if (symbol_select) {
            const char *match_value = NULL;
            for (int i = 0; i < st->asmSymbolCount; ++i) {
                const char *value = st->asmSymbolValues ? st->asmSymbolValues[i] : NULL;
                uint64_t symbol_addr = st->asmSymbolAddrs ? (st->asmSymbolAddrs[i] & 0x00ffffffull) : 0ull;
                if (!value || !value[0] || symbol_addr == 0) {
                    continue;
                }
                symbol_addr = source_pane_resolveAsmLikeAnchorAddr(st, symbol_addr);
                if ((symbol_addr & 0x00ffffffull) == (resolved & 0x00ffffffull)) {
                    match_value = value;
                    break;
                }
            }
            if (match_value) {
                e9ui_textbox_setSelectedValue(symbol_select, match_value);
            } else {
                e9ui_textbox_setOptions(symbol_select, NULL, 0);
                e9ui_textbox_setText(symbol_select, "");
                if (st->asmSymbolOptions && st->asmSymbolCount > 0) {
                    e9ui_textbox_setOptions(symbol_select, st->asmSymbolOptions, st->asmSymbolCount);
                }
            }
        }
    }

    source_pane_setAsmAnchorLocked(st, resolved);
}

static void
source_pane_syncLockButtonVisual(source_pane_state_t *st)
{
    if (!st || !st->ownerPane || !st->lockBtnMeta) {
        return;
    }
    e9ui_component_t *button = e9ui_child_find(st->ownerPane, st->lockBtnMeta);
    if (!button) {
        return;
    }
    e9ui_button_setLabel(button, "");
    e9ui_button_setIconAsset(button,
                             st->scrollLocked ? "assets/icons/locked.png"
                                              : "assets/icons/unlocked.png");
}

static void
source_pane_sourceSelectChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *value, void *user)
{
    (void)ctx;
    (void)comp;
    source_pane_state_t *st = (source_pane_state_t*)user;
    if (!st || !value) {
        return;
    }
    alloc_free(st->manualSrcPath);
    st->manualSrcPath = alloc_strdup(value);
    if (!st->manualSrcPath) {
        st->manualSrcActive = 0;
        return;
    }
    st->manualSrcActive = 1;
    st->scrollLine = 1;
    st->scrollLocked = 1;
    st->gutterPending = 0;
    source_pane_clearFunctionScrollLock(st);
    if (st->ownerPane && st->functionSelectMeta) {
        e9ui_component_t *function_select = e9ui_child_find(st->ownerPane, st->functionSelectMeta);
        if (function_select) {
            e9ui_textbox_setText(function_select, "");
        }
    }
    source_pane_clearSourceFunctions(st);
    if (st->ownerPane) {
        if (st->manualSrcPath[0]) {
            source_pane_refreshSourceFunctions(st->ownerPane, st, st->manualSrcPath);
        } else {
            source_pane_refreshSourceFunctions(st->ownerPane, st, NULL);
        }
    }
    source_pane_syncFunctionSelect(st->ownerPane, st);
    source_pane_syncLockButtonVisual(st);
}

static void
source_pane_functionSelectChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *value, void *user)
{
    (void)comp;
    source_pane_state_t *st = (source_pane_state_t*)user;
    if (!st || !value || !*value) {
        return;
    }
    int line = 0;
    const char *selected_file = NULL;
    if (!source_pane_parseFunctionValue(value, &line, &selected_file)) {
        return;
    }
    if (selected_file && selected_file[0]) {
        const char *end = strchr(selected_file, '|');
        if (end) {
            size_t len = (size_t)(end - selected_file);
            if (len > 0 && len < PATH_MAX) {
                char file_path[PATH_MAX];
                memcpy(file_path, selected_file, len);
                file_path[len] = '\0';
                alloc_free(st->manualSrcPath);
                st->manualSrcPath = alloc_strdup(file_path);
                if (st->manualSrcPath) {
                    st->manualSrcActive = 1;
                }
            }
        }
    }
    int max_lines = 1;
    if (ctx && st->ownerPane) {
        TTF_Font *use_font = source_pane_resolveFont(ctx);
        if (use_font) {
            source_pane_line_metrics_t metrics = source_pane_computeLineMetrics(st->ownerPane, ctx, use_font, 10);
            if (metrics.maxLines > 0) {
                max_lines = metrics.maxLines;
            }
        }
    }
    int start = line - (max_lines / 2);
    if (start < 1) {
        start = 1;
    }
    st->scrollLine = start;
    st->scrollLocked = 1;
    st->gutterPending = 0;
    st->functionScrollLock = 1;
    source_pane_syncLockButtonVisual(st);
}

static void
source_pane_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self || !self->state) {
        return;
    }
    source_pane_state_t *st = (source_pane_state_t*)self->state;
    source_pane_freeFrozenAsm(st);
    source_pane_clearSourceFiles(st);
    source_pane_clearSourceFunctions(st);
    source_pane_clearAsmSymbols(st);
    if (st->cprRegisterOptions) {
        alloc_free(st->cprRegisterOptions);
        st->cprRegisterOptions = NULL;
    }
    st->cprRegisterOptionCount = 0;
    alloc_free(st->manualSrcPath);
    st->manualSrcPath = NULL;
    // Child metadata keys are owned/freed by e9ui child container teardown.
    st->toggleBtnMeta = NULL;
    st->lockBtnMeta = NULL;
    st->fileSelectMeta = NULL;
    st->searchBoxMeta = NULL;
    st->asmSymbolSelectMeta = NULL;
    st->asmAddressMeta = NULL;
    st->inlineEditMeta = NULL;
    st->functionSelectMeta = NULL;
}

e9ui_component_t *
source_pane_make(void)
{
  e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
  c->name = "source_pane";
  source_pane_state_t *st = (source_pane_state_t*)alloc_calloc(1, sizeof(source_pane_state_t));
  st->viewMode = source_pane_mode_c;
  st->scrollLine = 1;
  st->scrollIndex = 0;
  st->scrollLocked = 0;
  st->ownerPane = c;
  c->state = st;
  c->focusable = 1;
  c->preferredHeight = source_pane_preferredHeight;
  c->layout = source_pane_layoutComp;
  c->render = source_pane_render;
  c->handleEvent = source_pane_handleEventComp;
  c->persistSave = source_pane_persistSave;
  c->persistLoad = source_pane_persistLoad;
  c->dtor = source_pane_dtor;

  e9ui_component_t *modeSelect = e9ui_textbox_make(16, NULL, NULL, NULL);
  if (modeSelect) {
      e9ui_textbox_setFocusBorderVisible(modeSelect, 0);
      e9ui_textbox_setReadOnly(modeSelect, 1);
      e9ui_textbox_setOnOptionSelected(modeSelect, source_pane_modeSelectChanged, c);
      st->toggleBtnMeta = alloc_strdup("toggle");
      e9ui_child_add(c, modeSelect, st->toggleBtnMeta);
  }

  e9ui_component_t *lockButton = e9ui_button_make("", source_pane_lockToggle, st);
  if (lockButton) {
      e9ui_button_setMini(lockButton, 1);
      st->lockBtnMeta = alloc_strdup("scroll_lock");
      e9ui_child_add(c, lockButton, st->lockBtnMeta);
      source_pane_syncLockButtonVisual(st);
  }

  e9ui_component_t *fileSelect = e9ui_textbox_make(512, NULL, NULL, NULL);
  if (fileSelect) {
      e9ui_textbox_setFocusBorderVisible(fileSelect, 0);
      e9ui_textbox_setPlaceholder(fileSelect, "source file");
      e9ui_textbox_setOnOptionSelected(fileSelect, source_pane_sourceSelectChanged, st);
      st->fileSelectMeta = alloc_strdup("source_select");
      e9ui_child_add(c, fileSelect, st->fileSelectMeta);
  }
  e9ui_component_t *functionSelect = e9ui_textbox_make(1024, NULL, NULL, NULL);
  if (functionSelect) {
      e9ui_textbox_setFocusBorderVisible(functionSelect, 0);
      e9ui_textbox_setPlaceholder(functionSelect, "function");
      e9ui_textbox_setOnOptionSelected(functionSelect, source_pane_functionSelectChanged, st);
      st->functionSelectMeta = alloc_strdup("function_select");
      e9ui_child_add(c, functionSelect, st->functionSelectMeta);
  }
  e9ui_component_t *searchBox = e9ui_textbox_make(512, NULL, source_pane_searchTextChanged, st);
  if (searchBox) {
      e9ui_textbox_setFocusBorderVisible(searchBox, 0);
      e9ui_textbox_setPlaceholder(searchBox, "search");
      e9ui_textbox_setKeyHandler(searchBox, source_pane_searchTextboxKey, st);
      st->searchBoxMeta = alloc_strdup("search_box");
      e9ui_child_add(c, searchBox, st->searchBoxMeta);
      e9ui_setHidden(searchBox, 1);
  }
  e9ui_component_t *asmSymbolSelect = e9ui_textbox_make(1024, NULL, NULL, NULL);
  if (asmSymbolSelect) {
      e9ui_textbox_setFocusBorderVisible(asmSymbolSelect, 0);
      e9ui_textbox_setPlaceholder(asmSymbolSelect, "symbol");
      e9ui_textbox_setOnOptionSelected(asmSymbolSelect, source_pane_asmSymbolSelectChanged, st);
      st->asmSymbolSelectMeta = alloc_strdup("asm_symbol_select");
      e9ui_child_add(c, asmSymbolSelect, st->asmSymbolSelectMeta);
  }
  e9ui_component_t *asmAddress = e9ui_textbox_make(32, source_pane_asmAddressSubmitted, NULL, st);
  if (asmAddress) {
      e9ui_textbox_setFocusBorderVisible(asmAddress, 0);
      e9ui_textbox_setPlaceholder(asmAddress, "address");
      st->asmAddressMeta = alloc_strdup("asm_address");
      e9ui_child_add(c, asmAddress, st->asmAddressMeta);
  }
  e9ui_component_t *inlineEdit = e9ui_textbox_make(64, source_pane_inlineEditSubmitted, NULL, st);
  if (inlineEdit) {
      e9ui_textbox_setPlaceholder(inlineEdit, "hex");
      e9ui_textbox_setKeyHandler(inlineEdit, source_pane_inlineEditKey, st);
      st->inlineEditMeta = alloc_strdup("inline_edit");
      e9ui_child_add(c, inlineEdit, st->inlineEditMeta);
      e9ui_setHidden(inlineEdit, 1);
  }

  source_pane_refreshModeOptions(c, st);
  
  return c;
}

static void
source_pane_setModeInternal(e9ui_component_t *comp, source_pane_mode_t mode, int enforceElfValid)
{
    if (!comp || !comp->state) {
        return;
    }
    source_pane_state_t *st = (source_pane_state_t*)comp->state;
    source_pane_mode_t prevMode = st->viewMode;
    if (mode != source_pane_mode_c &&
        mode != source_pane_mode_a &&
        mode != source_pane_mode_h &&
        mode != source_pane_mode_cpr) {
        mode = source_pane_mode_a;
    }
    if (mode == source_pane_mode_cpr && !source_cpr_isModeAvailable()) {
        mode = source_pane_mode_h;
    }
    if (enforceElfValid && !debugger.elfValid && mode == source_pane_mode_c) {
        mode = source_pane_mode_a;
    }

    if (prevMode != mode &&
        (prevMode == source_pane_mode_cpr || mode == source_pane_mode_cpr)) {
        st->frozenActive = 0;
        source_pane_freeFrozenAsm(st);
        st->scrollAnchorValid = 0;
    }

    st->viewMode = mode;
    st->gutterPending = 0;
    st->scrollAnchorValid = 0;
    source_pane_inlineEditCancel(st, NULL);
    source_pane_clearHover(comp, st);
    source_pane_clearFunctionScrollLock(st);
    if (mode != source_pane_mode_c) {
        source_pane_searchClose(st, NULL);
    }
    if (mode != source_pane_mode_c) {
        st->manualSrcActive = 0;
    }

    if (!source_pane_isAsmLikeMode(mode)) {
        st->frozenActive = 0;
        source_pane_freeFrozenAsm(st);
    }

    source_pane_refreshModeOptions(comp, st);
}

void
source_pane_setMode(e9ui_component_t *comp, source_pane_mode_t mode)
{
    source_pane_setModeInternal(comp, mode, 1);
}

source_pane_mode_t source_pane_getMode(e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return source_pane_mode_c;
    }
    source_pane_state_t *st = (source_pane_state_t*)comp->state;
    return st->viewMode;
}

void
source_pane_setToggleVisible(e9ui_component_t *comp, int visible)
{
    if (!comp || !comp->state) {
        return;
    }
    source_pane_state_t *st = (source_pane_state_t*)comp->state;
    if (!st->toggleBtnMeta) {
        return;
    }
    e9ui_component_t *overlay = e9ui_child_find(comp, st->toggleBtnMeta);
    if (!overlay) {
        return;
    }
    e9ui_setHidden(overlay, visible ? 0 : 1);
}

void source_pane_markNeedsRefresh(e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return;
    }
    source_pane_state_t *st = (source_pane_state_t*)comp->state;
    source_pane_inlineEditCancel(st, NULL);
    if (!st->scrollLocked) {
        st->scrollLine = 1;
        st->scrollIndex = 0;
        st->scrollAnchorValid = 0;
    }
    st->gutterPending = 0;
    if (!st->scrollLocked) {
        source_pane_clearFunctionScrollLock(st);
    }
}

void
source_pane_centerOnAddress(e9ui_component_t *comp, e9ui_context_t *ctx, uint32_t addr)
{
    if (!comp) {
        return;
    }
    source_pane_state_t *st = (source_pane_state_t*)comp->state;
    if (!st) {
        return;
    }
    st->overrideActive = 1;
    st->overrideAddr = (uint64_t)(addr & 0x00ffffffu);
    st->lastResolvedPc = 0;
    st->manualSrcActive = 0;

    TTF_Font *useFont = source_pane_resolveFont(ctx);
    int maxLines = 1;
    if (useFont) {
        source_pane_line_metrics_t metrics = source_pane_computeLineMetrics(comp, ctx, useFont, 10);
        maxLines = metrics.maxLines > 0 ? metrics.maxLines : 1;
    }

    st->curSrcLine = 0;
    st->curSrcPath[0] = '\0';
    source_pane_updateSourceLocation(st, 1);
    if (!st->scrollLocked && st->curSrcLine > 0) {
        int start = st->curSrcLine - (maxLines / 2);
        if (start < 1) {
            start = 1;
        }
        st->scrollLine = start;
        st->scrollLocked = 1;
    }

    if (!st->scrollLocked && st->viewMode == source_pane_mode_cpr) {
        uint64_t startAddr = source_cpr_resolveAnchorAddr((uint64_t)addr);
        if (maxLines > 1) {
            uint64_t back = (uint64_t)(maxLines / 2) * 4ull;
            if (startAddr > back) {
                startAddr -= back;
            } else {
                startAddr = 0;
            }
        }
        st->scrollIndex = (int)(startAddr >> 2);
        st->scrollAnchorAddr = startAddr;
        st->scrollAnchorValid = 1;
        st->scrollLocked = 1;
    } else {
        int idx = 0;
        if (!st->scrollLocked && dasm_findIndexForAddr((uint64_t)addr, &idx)) {
            int start = idx - (maxLines / 2);
            if (start < 0 && !(dasm_getFlags() & DASM_IFACE_FLAG_STREAMING)) {
                start = 0;
            }
            st->scrollIndex = start;
            st->scrollLocked = 1;
            st->scrollAnchorValid = 0;
        }
    }
    st->gutterPending = 0;
    source_pane_syncLockButtonVisual(st);
}

void
source_pane_submitAddress(e9ui_component_t *comp, e9ui_context_t *ctx, uint32_t addr)
{
    if (!comp || !comp->state) {
        return;
    }
    source_pane_state_t *st = (source_pane_state_t*)comp->state;
    if (!st->ownerPane || !st->asmAddressMeta) {
        return;
    }
    e9ui_component_t *addr_box = e9ui_child_find(st->ownerPane, st->asmAddressMeta);
    if (!addr_box) {
        return;
    }

    int hexw = dasm_getAddrHexWidth();
    if (hexw < 6) {
        hexw = 6;
    }
    if (hexw > 16) {
        hexw = 16;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%0*X", hexw, (unsigned)(addr & 0x00ffffffu));
    e9ui_textbox_setText(addr_box, buf);
    source_pane_asmAddressSubmitted(ctx, st);
}

int
source_pane_getCurrentFile(e9ui_component_t *comp, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!comp) {
        return 0;
    }
    source_pane_state_t *st = (source_pane_state_t*)comp->state;
    if (!st || st->viewMode != source_pane_mode_c) {
        return 0;
    }
    if (st->manualSrcActive && st->manualSrcPath && st->manualSrcPath[0]) {
        strncpy(out, st->manualSrcPath, cap - 1);
        out[cap - 1] = '\0';
        return 1;
    }
    if (!st->overrideActive && machine_getRunning(debugger.machine)) {
        if (!st->curSrcPath[0]) {
            return 0;
        }
    } else {
        source_pane_updateSourceLocation(st, 0);
    }
    if (!st->curSrcPath[0]) {
        return 0;
    }
    strncpy(out, st->curSrcPath, cap - 1);
    out[cap - 1] = '\0';
    return 1;
}
