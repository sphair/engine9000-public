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
#include "inline_edit_pause.h"
#include "source.h"
#include "source_pane.h"
#include "source_pane_internal.h"
#include "source_cpr.h"
#include "source_z80.h"
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

typedef struct source_pane_step_buttons_action_ctx {
    e9ui_component_t *self;
    e9ui_context_t *ctx;
    source_pane_state_t *st;
    source_pane_mode_t mode;
} source_pane_step_buttons_action_ctx_t;

typedef struct source_pane_asm_spans {
    syntax_highlight_span_t *items;
    int count;
    int cap;
} source_pane_asm_spans_t;

static void
source_pane_followCurrent(source_pane_state_t *st);

void
source_pane_freeFrozenAsm(source_pane_state_t *st);

static void
source_pane_setModeInternal(e9ui_component_t *comp, source_pane_mode_t mode, int enforceElfValid);

static void
source_pane_clearFunctionScrollLock(source_pane_state_t *st);

static void
source_pane_lockToggle(e9ui_context_t *ctx, void *user);

static void
source_pane_asmSymbolSelectChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *value, void *user);

static void
source_pane_asmAddressSubmitted(e9ui_context_t *ctx, void *user);

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
source_pane_isSourceLikeMode(source_pane_mode_t mode);

int
source_pane_shouldFreezeAsmWhileRunning(const source_pane_state_t *st);

static int
source_pane_areAsmViewStepButtonsEnabled(const source_pane_state_t *st);

static int
source_pane_isCpuAsmLikeMode(source_pane_mode_t mode);

static void
source_pane_selectSymbolsCache(source_pane_state_t *st, source_pane_mode_t mode);

static int
source_pane_beginGutterPress(e9ui_component_t *self, e9ui_context_t *ctx, source_pane_state_t *st,
                             source_pane_mode_t mode, int mx, int my);

void
source_pane_inlineEditCancel(source_pane_state_t *st, e9ui_context_t *ctx);

static int
source_pane_inlineEditCommit(source_pane_state_t *st, e9ui_context_t *ctx);

static void
source_pane_inlineEditSubmitted(e9ui_context_t *ctx, void *user);

static void
source_pane_inlineEditOptionSelected(e9ui_context_t *ctx, e9ui_component_t *comp, const char *value, void *user);

static int
source_pane_inlineEditKey(e9ui_context_t *ctx, SDL_Keycode key, SDL_Keymod mods, void *user);

static int
source_pane_beginGutterPress(e9ui_component_t *self, e9ui_context_t *ctx, source_pane_state_t *st,
                             source_pane_mode_t mode, int mx, int my)
{
    if (!self || !ctx || !st) {
        return 0;
    }
    if (mode == source_pane_mode_c || mode == source_pane_mode_z80s) {
        return source_pane_source_view_beginSourceGutterPress(self, ctx, st, mx, my);
    }
    return source_pane_asm_view_beginGutterPress(self, ctx, st, mode, mx, my);
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
    return source_pane_asm_view_isAsmLikeMode(mode);
}

static int
source_pane_isSourceLikeMode(source_pane_mode_t mode)
{
    return (mode == source_pane_mode_c ||
            mode == source_pane_mode_z80s) ? 1 : 0;
}

int
source_pane_shouldFreezeAsmWhileRunning(const source_pane_state_t *st)
{
    return source_pane_asm_view_shouldFreezeWhileRunning(st);
}

static int
source_pane_areAsmViewStepButtonsEnabled(const source_pane_state_t *st)
{
    return source_pane_asm_view_areStepButtonsEnabled(st);
}

static int
source_pane_isCpuAsmLikeMode(source_pane_mode_t mode)
{
    return source_pane_asm_view_isCpuAsmLikeMode(mode);
}

static void
source_pane_selectSymbolsCache(source_pane_state_t *st, source_pane_mode_t mode)
{
    if (!st) {
        return;
    }
    if (mode == source_pane_mode_z80 ||
        mode == source_pane_mode_z80s) {
        st->activeSymbols = &st->z80Symbols;
        return;
    }
    st->activeSymbols = &st->primarySymbols;
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

void
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

int
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

int
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

int
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

static void
source_pane_clearFunctionScrollLock(source_pane_state_t *st)
{
    if (!st) {
        return;
    }
    st->functionScrollLock = 0;
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
source_pane_adjustScroll(source_pane_state_t *st, source_pane_mode_t mode, int delta)
{
    if (!st || delta == 0) {
        return;
    }
    if (st->inlineEditActive) {
        source_pane_inlineEditCancel(st, NULL);
    }
    if (mode == source_pane_mode_c || mode == source_pane_mode_z80s) {
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
    if (mode == source_pane_mode_z80) {
        uint64_t destAddr = source_z80_getCurrentAddr(st);
        if (st->scrollLocked && st->scrollAnchorValid) {
            destAddr = st->scrollAnchorAddr;
        }
        if (delta < 0) {
            uint64_t step = (uint64_t)(-delta);
            destAddr = destAddr > step ? destAddr - step : 0;
        } else {
            destAddr += (uint64_t)delta;
        }
        destAddr = source_z80_resolveAnchorAddr(destAddr);
        st->scrollIndex = (int)destAddr;
        st->scrollAnchorAddr = destAddr;
        st->scrollAnchorValid = 1;
        st->scrollLocked = 1;
        st->gutterPending = 0;
        source_pane_freeFrozenAsm(st);
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
    if (mode == source_pane_mode_c || mode == source_pane_mode_z80s) {
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
    if (mode == source_pane_mode_z80) {
        st->scrollIndex = 0;
        st->scrollAnchorAddr = 0;
        st->scrollAnchorValid = 1;
        st->scrollLocked = 1;
        st->gutterPending = 0;
        source_pane_freeFrozenAsm(st);
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
    if (mode == source_pane_mode_c || mode == source_pane_mode_z80s) {
        source_pane_source_view_updateSourceLocation(st, 0);
        int total = 0;
        const char *path = st->curSrcPath;
        if (st->manualSrcActive && st->manualSrcPath && st->manualSrcPath[0]) {
            path = st->manualSrcPath;
        }
        if (path && path[0]) {
            total = source_getTotalLines(path);
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
    if (mode == source_pane_mode_z80) {
        uint64_t destAddr = 0xffffu;
        if (maxLines > 1) {
            uint64_t back = (uint64_t)(maxLines - 1);
            destAddr = destAddr > back ? destAddr - back : 0;
        }
        destAddr = source_z80_resolveAnchorAddr(destAddr);
        st->scrollIndex = (int)destAddr;
        st->scrollAnchorAddr = destAddr;
        st->scrollAnchorValid = 1;
        st->scrollLocked = 1;
        st->gutterPending = 0;
        source_pane_freeFrozenAsm(st);
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
    if (!source_pane_isSourceLikeMode(st->viewMode)) {
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

    source_pane_source_view_updateSourceLocation(st, 0);
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

void
source_pane_trackPosition(source_pane_state_t *st)
{
    if (!st) {
        return;
    }
    if (st->overrideActive) {
        return;
    }
    uint64_t curAddr = 0;
    if (st->viewMode == source_pane_mode_z80 ||
        st->viewMode == source_pane_mode_z80s) {
        curAddr = source_z80_getCurrentAddr(st);
    } else {
        unsigned long primaryPc = 0;
        (void)machine_findReg(&debugger.machine, "PC", &primaryPc);
        curAddr = (uint64_t)(primaryPc & 0x00ffffffu);
    }
    if (curAddr != st->lastPcAddr) {
        if (!st->scrollLocked) {
            source_pane_clearFunctionScrollLock(st);
            st->manualSrcActive = 0;
        }
    }
    st->lastPcAddr = curAddr;
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
    if (mode == source_pane_mode_c || mode == source_pane_mode_sym) {
        return "c";
    }
    if (mode == source_pane_mode_cpr) {
        return "cpr";
    }
    if (mode == source_pane_mode_z80) {
        return "z80";
    }
    if (mode == source_pane_mode_z80s) {
        return "z80s";
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
    if (strcmp(value, "z80") == 0 ||
        strcmp(value, "z80a") == 0) {
        return source_pane_mode_z80;
    }
    if (strcmp(value, "z80s") == 0) {
        return source_pane_mode_z80s;
    }
    if (strcmp(value, "hex") == 0) {
        return source_pane_mode_h;
    }
    return source_pane_mode_a;
}

static int
source_pane_modePersistValue(source_pane_mode_t mode)
{
    if (mode == source_pane_mode_c || mode == source_pane_mode_sym) {
        return 0;
    }
    if (mode == source_pane_mode_cpr) {
        return 4;
    }
    if (mode == source_pane_mode_z80) {
        return 6;
    }
    if (mode == source_pane_mode_z80s) {
        return 7;
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
    static const e9ui_textbox_option_t modeOptionsNeoGeo[] = {
        { .value = "c",   .label = "SRC" },
        { .value = "asm", .label = "ASM" },
        { .value = "hex", .label = "HEX" },
        { .value = "z80", .label = "Z8A" },
        { .value = "z80s", .label = "Z8S" },
    };

    if (!comp || !st || !st->toggleBtnMeta) {
        return;
    }
    e9ui_component_t *select = e9ui_child_find(comp, st->toggleBtnMeta);
    if (!select) {
        return;
    }

    int optionsKind = 0;
    if (source_cpr_isModeAvailable()) {
        optionsKind = 1;
    } else if (source_z80_isModeAvailable()) {
        optionsKind = 2;
    }
    if (st->modeOptionsKind != optionsKind) {
        if (optionsKind == 1) {
            e9ui_textbox_setOptions(select,
                                    modeOptionsAmiga,
                                    (int)(sizeof(modeOptionsAmiga) / sizeof(modeOptionsAmiga[0])));
        } else if (optionsKind == 2) {
            e9ui_textbox_setOptions(select,
                                    modeOptionsNeoGeo,
                                    (int)(sizeof(modeOptionsNeoGeo) / sizeof(modeOptionsNeoGeo[0])));
        } else {
            e9ui_textbox_setOptions(select,
                                    modeOptionsBase,
                                    (int)(sizeof(modeOptionsBase) / sizeof(modeOptionsBase[0])));
        }
        st->modeOptionsKind = optionsKind;
    }
    e9ui_textbox_setSelectedValue(select, source_pane_modeValue(st->viewMode));
}

static void
source_pane_forceRefreshModeOptions(e9ui_component_t *comp, source_pane_state_t *st)
{
    if (st) {
        st->modeOptionsKind = -1;
    }
    source_pane_refreshModeOptions(comp, st);
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
      } else if (m == 5) {
          mode = source_pane_mode_sym;
      } else if (m == 4) {
          mode = source_pane_mode_cpr;
      } else if (m == 6) {
          mode = source_pane_mode_z80;
      } else if (m == 7) {
          mode = source_pane_mode_z80s;
      } else if (m == 3) {
          mode = source_pane_mode_h;
      }
      source_pane_setModeInternal(self, mode, 0);
  }
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

static e9ui_component_t *
source_pane_inlineTextboxComponent(source_pane_state_t *st)
{
    if (!st || !st->ownerPane || !st->inlineEditMeta) {
        return NULL;
    }
    return e9ui_child_find(st->ownerPane, st->inlineEditMeta);
}

static e9ui_component_t *
source_pane_inlineDataEditComponent(source_pane_state_t *st)
{
    if (!st || !st->ownerPane || !st->inlineDataEditMeta) {
        return NULL;
    }
    return e9ui_child_find(st->ownerPane, st->inlineDataEditMeta);
}

static e9ui_component_t *
source_pane_inlineEditComponentForKind(source_pane_state_t *st, source_pane_inline_edit_kind_t kind)
{
    if (!st) {
        return NULL;
    }
    if (kind == source_pane_inline_edit_hex_bytes ||
        kind == source_pane_inline_edit_cpr_words ||
        kind == source_pane_inline_edit_cpr_value) {
        return source_pane_inlineDataEditComponent(st);
    }
    return source_pane_inlineTextboxComponent(st);
}

static e9ui_component_t *
source_pane_inlineEditComponent(source_pane_state_t *st)
{
    if (!st) {
        return NULL;
    }
    return source_pane_inlineEditComponentForKind(st, st->inlineEditKind);
}

void
source_pane_inlineEditCancel(source_pane_state_t *st, e9ui_context_t *ctx)
{
    e9ui_component_t *editor = NULL;

    if (!st) {
        return;
    }

    e9ui_component_t *textboxEditor = source_pane_inlineTextboxComponent(st);
    e9ui_component_t *dataEditor = source_pane_inlineDataEditComponent(st);
    if (textboxEditor) {
        e9ui_textbox_setOptions(textboxEditor, NULL, 0);
        e9ui_textbox_setText(textboxEditor, "");
        e9ui_setHidden(textboxEditor, 1);
    }
    editor = dataEditor;
    st->inlineEditActive = 0;
    st->inlineEditKind = source_pane_inline_edit_none;
    st->inlineEditMode = source_pane_mode_c;
    st->inlineEditAddr = 0;
    st->inlineEditByteCount = 0;
    st->inlineEditWord1 = 0;
    st->inlineEditWord2 = 0;
    st->inlineEditRect = (SDL_Rect){0, 0, 0, 0};
    if (editor) {
        e9ui_data_edit_setText(editor, "");
        e9ui_setHidden(editor, 1);
    }
    if (ctx && (e9ui_getFocus(ctx) == textboxEditor || e9ui_getFocus(ctx) == dataEditor)) {
        e9ui_setFocus(ctx, st->ownerPane);
    }
    inline_edit_pauseEnd(&st->inlineEditAutoResume);
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
source_pane_writeHexBytes(source_pane_mode_t mode, uint32_t addr, const uint8_t *bytes, int count)
{
    int i = 0;
    int byteWritesOnly = mode == source_pane_mode_z80 ? 1 : 0;

    if (!bytes || count <= 0) {
        return 0;
    }

    while (i < count) {
        if (!byteWritesOnly && (i + 1) < count) {
            uint16_t word = (uint16_t)(((uint16_t)bytes[i] << 8) | (uint16_t)bytes[i + 1]);
            if (!libretro_host_debugWriteMemory(addr + (uint32_t)i, (uint32_t)word, 2)) {
                return 0;
            }
            i += 2;
            continue;
        }
        if (mode == source_pane_mode_z80) {
            if (!libretro_host_debugWriteProcessorMemory(source_z80_processorId(),
                                                         addr + (uint32_t)i,
                                                         (uint32_t)bytes[i],
                                                         1)) {
                return 0;
            }
        } else if (!libretro_host_debugWriteMemory(addr + (uint32_t)i, (uint32_t)bytes[i], 1)) {
            return 0;
        }
        i += 1;
    }
    return 1;
}

static int
source_pane_verifyHexBytes(source_pane_mode_t mode, uint32_t addr, const uint8_t *bytes, int count)
{
    uint8_t check[16];

    if (!bytes || count <= 0 || count > (int)sizeof(check)) {
        return 0;
    }
    memset(check, 0, sizeof(check));
    if (mode == source_pane_mode_z80) {
        if (!libretro_host_debugReadProcessorMemory(source_z80_processorId(), addr, check, (size_t)count)) {
            return 0;
        }
    } else if (!libretro_host_debugReadMemory(addr, check, (size_t)count)) {
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
    editor = source_pane_inlineEditComponent(st);
    if (!editor) {
        source_pane_inlineEditCancel(st, ctx);
        return 0;
    }
    if (st->inlineEditKind == source_pane_inline_edit_hex_bytes ||
        st->inlineEditKind == source_pane_inline_edit_cpr_words ||
        st->inlineEditKind == source_pane_inline_edit_cpr_value) {
        text = e9ui_data_edit_getText(editor);
    } else {
        text = e9ui_textbox_getText(editor);
    }

    cprResult = source_cpr_commitInlineEdit(st, ctx, editor, text);
    if (cprResult != 0) {
        return cprResult > 0 ? 1 : 0;
    }

    if (st->inlineEditKind == source_pane_inline_edit_hex_bytes) {
        uint8_t bytes[16];
        memset(bytes, 0, sizeof(bytes));
        if (!source_pane_parseInlineHexBytes(text, bytes, st->inlineEditByteCount)) {
            e9ui_showTransientMessage("INVALID HEX FORMAT");
            e9ui_data_edit_selectAllExternal(editor);
            return 0;
        }
        if (!source_pane_writeHexBytes(st->inlineEditMode,
                                       st->inlineEditAddr,
                                       bytes,
                                       st->inlineEditByteCount)) {
            e9ui_showTransientMessage("WRITE FAILED - NO CORE SUPPORT?");
            e9ui_data_edit_selectAllExternal(editor);
            return 0;
        }
        if (!source_pane_verifyHexBytes(st->inlineEditMode,
                                        st->inlineEditAddr,
                                        bytes,
                                        st->inlineEditByteCount)) {
            e9ui_showTransientMessage("UNABLE TO WRITE DATA - ROM ?");
            e9ui_data_edit_selectAllExternal(editor);
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

static void
source_pane_inlineEditOptionSelected(e9ui_context_t *ctx, e9ui_component_t *comp, const char *value, void *user)
{
    source_pane_state_t *st = (source_pane_state_t *)user;
    (void)comp;
    (void)value;

    if (!st || !st->inlineEditActive || st->inlineEditKind != source_pane_inline_edit_cpr_reg) {
        return;
    }
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
source_pane_dataEditCursorForPoint(TTF_Font *font, const char *text, e9ui_data_edit_mode_t mode,
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
    fullWidth = source_pane_measureSegment(font, text, 0, len);
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
        startWidth = source_pane_measureSegment(font, text, 0, i);
        endWidth = source_pane_measureSegment(font, text, 0, i + 1);
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

int
source_pane_beginInlineEdit(source_pane_state_t *st, e9ui_context_t *ctx, source_pane_mode_t mode,
                            source_pane_inline_edit_kind_t kind, uint32_t addr, int byteCount,
                            uint16_t word1, uint16_t word2, const char *text, SDL_Rect rect,
                            int initialCursor)
{
    e9ui_component_t *editor = NULL;
    int autoResume = 0;

    if (!st || !ctx || !text || byteCount <= 0) {
        return 0;
    }

    editor = source_pane_inlineEditComponentForKind(st, kind);
    if (!editor) {
        return 0;
    }
    if (!inline_edit_pauseBegin(&autoResume)) {
        return 0;
    }

    st->inlineEditActive = 1;
    st->inlineEditAutoResume = autoResume;
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
    if (kind == source_pane_inline_edit_hex_bytes ||
        kind == source_pane_inline_edit_cpr_words ||
        kind == source_pane_inline_edit_cpr_value) {
        if (kind == source_pane_inline_edit_hex_bytes) {
            e9ui_data_edit_setCellCount(editor, byteCount);
            e9ui_data_edit_setMode(editor, e9ui_data_edit_mode_hex_bytes);
        } else {
            int wordCount = byteCount / 2;
            if (wordCount <= 0) {
                wordCount = 1;
            }
            e9ui_data_edit_setCellCount(editor, wordCount);
            e9ui_data_edit_setMode(editor, e9ui_data_edit_mode_hex_words16);
        }
        e9ui_data_edit_setText(editor, text);
        e9ui_data_edit_setCursor(editor, initialCursor);
    } else {
        if (kind == source_pane_inline_edit_cpr_reg && source_cpr_buildRegisterOptions(st)) {
            e9ui_textbox_setOptions(editor, st->cprRegisterOptions, st->cprRegisterOptionCount);
        } else {
            e9ui_textbox_setOptions(editor, NULL, 0);
        }
        e9ui_textbox_setText(editor, text);
    }
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
source_pane_renderAsm(e9ui_component_t *self, e9ui_context_t *ctx)
{
    source_pane_asm_view_renderAsm(self, ctx);
}

static int
source_pane_beginInlineHexEditAtPoint(e9ui_component_t *self, e9ui_context_t *ctx, source_pane_state_t *st,
                                      int mx, int my)
{
    return source_pane_asm_view_beginInlineHexEditAtPoint(self, ctx, st, mx, my);
}

static void
source_pane_renderHex(e9ui_component_t *self, e9ui_context_t *ctx)
{
    source_pane_asm_view_renderHex(self, ctx);
}

static void
source_pane_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    TTF_Font *useFont = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    SDL_Rect area = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    source_pane_state_t *st = (source_pane_state_t*)self->state;
    if (st) {
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
        source_pane_symbols_refreshAsmSymbols(self, st);
        source_pane_renderAsm(self, ctx);
        goto done;
    }
    if (st && st->viewMode == source_pane_mode_sym) {
        source_pane_symbols_refreshAsmSymbols(self, st);
        source_pane_renderAsm(self, ctx);
        goto done;
    }
    if (st && st->viewMode == source_pane_mode_h) {
        source_pane_symbols_refreshAsmSymbols(self, st);
        source_pane_renderHex(self, ctx);
        goto done;
    }
    if (st && st->viewMode == source_pane_mode_cpr) {
        source_pane_symbols_refreshAsmSymbols(self, st);
        source_cpr_render(self, ctx);
        goto done;
    }
    if (st && st->viewMode == source_pane_mode_z80) {
        source_pane_symbols_refreshAsmSymbols(self, st);
        source_pane_renderAsm(self, ctx);
        goto done;
    }
    if (st && st->viewMode == source_pane_mode_z80s) {
        source_pane_source_view_render(self, ctx);
        goto done;
    }
    if (st) {
        source_pane_source_view_render(self, ctx);
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
      e9ui_component_t* inlineEdit = source_pane_inlineEditComponent(st);
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
          int showSourceControls = source_pane_isSourceLikeMode(mode);
          e9ui_setHidden(functionSelect, showSourceControls ? 0 : 1);
          if (showSourceControls && functionW > 0) {
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
          int showSourceControls = source_pane_isSourceLikeMode(mode);
          e9ui_setHidden(fileSelect, showSourceControls ? 0 : 1);
          if (showSourceControls && sourceW > 0) {
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
          int showSearch = (source_pane_isSourceLikeMode(mode) && st && st->searchActive) ? 1 : 0;
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
    if (st && st->inlineEditActive &&
        ev->type == SDL_KEYDOWN &&
        ev->key.keysym.sym == SDLK_ESCAPE) {
        source_pane_inlineEditCancel(st, ctx);
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
    if (st && ctx &&
        (ev->type == SDL_MOUSEMOTION || ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP) &&
        source_pane_source_view_handleCScrollEvent(self, ctx, st, ev)) {
        return 1;
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
        source_pane_source_view_updateHoverTooltip(self, ctx, st, ev);
        return 0;
    }
    if (ev->type == SDL_MOUSEBUTTONUP && ev->button.button == SDL_BUTTON_LEFT) {
        if (!st || !st->gutterPending) {
        } else {
            st->gutterPending = 0;
            int slop = ctx ? e9ui_scale_px(ctx, 4) : 4;
            int dx = ev->button.x - st->gutterDownX;
            int dy = ev->button.y - st->gutterDownY;
            if (dx * dx + dy * dy >= slop * slop) {
                return 0;
            }
            if (st->gutterMode == source_pane_mode_c ||
                st->gutterMode == source_pane_mode_z80s) {
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
                if (st->gutterMode == source_pane_mode_z80s) {
                    uint16_t addr = 0;
                    if (!source_z80_resolveSourceLineAddress(path, lineNo, &addr)) {
                        return 0;
                    }
                    uint32_t processorId = source_z80_processorId();
                    machine_breakpoint_t *existing =
                        machine_findProcessorBreakpointByAddr(&debugger.machine, processorId, addr);
                    if (existing) {
                        if (machine_removeProcessorBreakpointByAddr(&debugger.machine, processorId, addr)) {
                            libretro_host_debugRemoveProcessorBreakpoint(processorId, addr);
                            breakpoints_markDirty();
                        }
                        return 1;
                    }
                    machine_breakpoint_t *bp = machine_addProcessorBreakpoint(&debugger.machine,
                                                                              processorId,
                                                                              addr,
                                                                              1);
                    if (bp) {
                        strncpy(bp->file, path, sizeof(bp->file) - 1);
                        bp->file[sizeof(bp->file) - 1] = '\0';
                        bp->line = lineNo;
                        libretro_host_debugAddProcessorBreakpoint(processorId, addr);
                        breakpoints_markDirty();
                        return 1;
                    }
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
                if (source_pane_fileline_removeBreakpointsForLine(path, lineNo, bps, bp_count)) {
                    breakpoints_markDirty();
                    return 1;
                }
                if (source_pane_fileline_addBreakpointsForLine(path, lineNo)) {
                    breakpoints_markDirty();
                    return 1;
                }
                return 0;
            }
            if (source_pane_isCpuAsmLikeMode(st->gutterMode)) {
                uint32_t addr = st->gutterAddr;
                uint32_t processorId = st->gutterMode == source_pane_mode_z80
                    ? source_z80_processorId()
                    : MACHINE_PROCESSOR_PRIMARY;
                machine_breakpoint_t *existing = machine_findProcessorBreakpointByAddr(&debugger.machine,
                                                                                        processorId,
                                                                                        addr);
                if (existing) {
                    if (machine_removeProcessorBreakpointByAddr(&debugger.machine, processorId, addr)) {
                        libretro_host_debugRemoveProcessorBreakpoint(processorId, addr);
                        breakpoints_markDirty();
                    }
                    return 1;
                }
                machine_breakpoint_t *bp = machine_addProcessorBreakpoint(&debugger.machine,
                                                                          processorId,
                                                                          addr,
                                                                          1);
                if (bp) {
                    breakpoints_resolveLocation(bp);
                    libretro_host_debugAddProcessorBreakpoint(processorId, addr);
                    breakpoints_markDirty();
                    return 1;
                }
                return 0;
            }
        }
    }
    if (ev->type == SDL_MOUSEWHEEL) {
        int mx = e9ui->mouseX;
        int my = e9ui->mouseY;
        if (source_pane_pointInBounds(self, mx, my)) {
            int wheelX = ev->wheel.x;
            int wheelY = ev->wheel.y;
            int handledCScroll = 0;
            if (ctx) {
                handledCScroll = source_pane_source_view_handleCScrollEvent(self, ctx, st, ev);
            }
            if (wheelY != 0) {
                const int linesPerTick = 1;
                int delta = wheelY * linesPerTick;
                source_pane_adjustScroll(st, mode, delta);
            }
            if (handledCScroll || wheelX != 0 || wheelY != 0) {
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
        if (st) {
            st->inlineEditPending = 0;
            if (!st->inlineEditActive && ev->button.clicks >= 2) {
                st->inlineEditPending = 1;
            }
        }
        if (source_pane_beginGutterPress(self, ctx, st, mode, mx, my)) {
            return 1;
        }
    }
    if (ev->type == SDL_MOUSEBUTTONUP && ev->button.button == SDL_BUTTON_LEFT) {
        int mx = ev->button.x;
        int my = ev->button.y;
        if (!source_pane_pointInBounds(self, mx, my)) {
            return 0;
        }
        if (st && st->inlineEditPending) {
            st->inlineEditPending = 0;
        } else {
            return 0;
        }
        if (st && !st->inlineEditActive) {
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
        if (source_pane_isSourceLikeMode(mode) && accel && kc == SDLK_s) {
            source_pane_searchOpen(st, ctx);
            source_pane_searchFind(st, self, ctx, 1, 1);
            return 1;
        }
        if (source_pane_isSourceLikeMode(mode) && accel && kc == SDLK_r) {
            source_pane_searchOpen(st, ctx);
            source_pane_searchFind(st, self, ctx, -1, 1);
            return 1;
        }
        if (source_pane_isSourceLikeMode(mode) && kc == SDLK_ESCAPE && st->searchActive) {
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
            if (source_pane_isSourceLikeMode(st->viewMode)) {
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
    source_pane_asm_view_symbolSelectChanged(ctx, comp, value, user);
}

static void
source_pane_asmAddressSubmitted(e9ui_context_t *ctx, void *user)
{
    source_pane_asm_view_addressSubmitted(ctx, user);
}

void
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
    source_pane_symbols_clearSourceFunctions(st);
    if (st->ownerPane) {
        if (st->manualSrcPath[0]) {
            source_pane_symbols_refreshSourceFunctions(st->ownerPane, st, st->manualSrcPath);
        } else {
            source_pane_symbols_refreshSourceFunctions(st->ownerPane, st, NULL);
        }
    }
    source_pane_symbols_syncFunctionSelect(st->ownerPane, st);
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
    source_pane_symbols_clearAllCaches(st);
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
    st->inlineDataEditMeta = NULL;
    st->functionSelectMeta = NULL;
}

e9ui_component_t *
source_pane_make(void)
{
  e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
  c->name = "source_pane";
  source_pane_state_t *st = (source_pane_state_t*)alloc_calloc(1, sizeof(source_pane_state_t));
  st->viewMode = source_pane_mode_c;
  st->activeSymbols = &st->primarySymbols;
  st->scrollLine = 1;
  st->scrollIndex = 0;
  st->scrollLocked = 0;
  st->modeOptionsKind = -1;
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
      e9ui_textbox_setOnOptionSelected(inlineEdit, source_pane_inlineEditOptionSelected, st);
      st->inlineEditMeta = alloc_strdup("inline_edit");
      e9ui_child_add(c, inlineEdit, st->inlineEditMeta);
      e9ui_setHidden(inlineEdit, 1);
  }
  e9ui_component_t *inlineDataEdit = e9ui_data_edit_make(16, source_pane_inlineEditSubmitted, st);
  if (inlineDataEdit) {
      e9ui_data_edit_setKeyHandler(inlineDataEdit, source_pane_inlineEditKey, st);
      st->inlineDataEditMeta = alloc_strdup("inline_data_edit");
      e9ui_child_add(c, inlineDataEdit, st->inlineDataEditMeta);
      e9ui_setHidden(inlineDataEdit, 1);
  }

  source_pane_forceRefreshModeOptions(c, st);
  
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
        mode != source_pane_mode_sym &&
        mode != source_pane_mode_h &&
        mode != source_pane_mode_cpr &&
        mode != source_pane_mode_z80 &&
        mode != source_pane_mode_z80s) {
        mode = source_pane_mode_a;
    }
    if (mode == source_pane_mode_c &&
        debugger.symbolFileKind == DEBUGGER_SYMBOL_FILE_KIND_TEXT_MAP) {
        mode = source_pane_mode_sym;
    }
    if (mode == source_pane_mode_sym &&
        debugger.symbolFileKind != DEBUGGER_SYMBOL_FILE_KIND_TEXT_MAP) {
        mode = source_pane_mode_a;
    }
    if (mode == source_pane_mode_cpr && !source_cpr_isModeAvailable()) {
        mode = source_pane_mode_h;
    }
    if ((mode == source_pane_mode_z80 || mode == source_pane_mode_z80s) &&
        !source_z80_isModeAvailable()) {
        mode = source_pane_mode_a;
    }
    if (enforceElfValid && !debugger.elfValid && mode == source_pane_mode_c) {
        mode = source_pane_mode_a;
    }

    if (prevMode != mode &&
        (prevMode == source_pane_mode_cpr || mode == source_pane_mode_cpr ||
         prevMode == source_pane_mode_z80 || mode == source_pane_mode_z80 ||
         prevMode == source_pane_mode_z80s || mode == source_pane_mode_z80s)) {
        st->frozenActive = 0;
        source_pane_freeFrozenAsm(st);
        st->scrollAnchorValid = 0;
    }

    int enteringAsmLikeFromSource = (!source_pane_isAsmLikeMode(prevMode) &&
                                     source_pane_isAsmLikeMode(mode)) ? 1 : 0;

    st->viewMode = mode;
    source_pane_selectSymbolsCache(st, mode);
    st->gutterPending = 0;
    st->scrollAnchorValid = 0;
    source_pane_inlineEditCancel(st, NULL);
    source_pane_source_view_clearHover(comp, st);
    source_pane_clearFunctionScrollLock(st);
    if (!source_pane_isSourceLikeMode(mode)) {
        source_pane_searchClose(st, NULL);
    }
    if (!source_pane_isSourceLikeMode(mode) ||
        (source_pane_isSourceLikeMode(prevMode) && prevMode != mode)) {
        st->manualSrcActive = 0;
    }

    if (!source_pane_isAsmLikeMode(mode)) {
        st->frozenActive = 0;
        source_pane_freeFrozenAsm(st);
    }

    if (enteringAsmLikeFromSource) {
        source_pane_followCurrent(st);
    }

    source_pane_refreshModeOptions(comp, st);
}

void
source_pane_setMode(e9ui_component_t *comp, source_pane_mode_t mode)
{
    source_pane_setModeInternal(comp, mode, 1);
}

source_pane_mode_t
source_pane_getMode(e9ui_component_t *comp)
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

void
source_pane_markNeedsRefresh(e9ui_component_t *comp)
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
    source_pane_source_view_updateSourceLocation(st, 1);
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
    if (!st || !source_pane_isSourceLikeMode(st->viewMode)) {
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
        source_pane_source_view_updateSourceLocation(st, 0);
    }
    if (!st->curSrcPath[0]) {
        return 0;
    }
    strncpy(out, st->curSrcPath, cap - 1);
    out[cap - 1] = '\0';
    return 1;
}
