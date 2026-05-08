/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "e9ui.h"
#include "addr2line.h"
#include "breakpoints.h"
#include "debugger.h"
#include "machine.h"
#include "print_eval.h"
#include "source.h"
#include "source_pane_internal.h"
#include "source_z80.h"
#include "syntax_highlight.h"

typedef struct source_pane_source_window {
    TTF_Font *font;
    const char *path;
    const char **lines;
    source_pane_line_metrics_t metrics;
    SDL_Rect contentArea;
    int manualView;
    int curLine;
    int count;
    int first;
    int total;
    int maxLines;
    int padPx;
    int gutterW;
    int gutterPad;
    int textX;
    int textDrawX;
    int hitW;
    int loaded;
} source_pane_source_window_t;

static SDL_Color
source_pane_source_view_syntaxColor(syntax_highlight_kind_t kind)
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
source_pane_source_view_copySegment(const char *line, int start, int len,
                                    char *stackBuf, int stackCap, char **outBuf)
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

    char *heapBuf = (char *)malloc((size_t)len + 1);
    if (!heapBuf) {
        return 0;
    }
    memcpy(heapBuf, line + start, (size_t)len);
    heapBuf[len] = '\0';
    *outBuf = heapBuf;
    return 1;
}

static void
source_pane_source_view_drawSegment(e9ui_context_t *ctx, e9ui_component_t *owner, TTF_Font *font,
                                    const char *line, int start, int len, SDL_Color color,
                                    int x, int y, int lineHeight, int hitW, void *sourceBucket)
{
    if (!ctx || !owner || !font || !line || len <= 0) {
        return;
    }

    char stackBuf[256];
    char *seg = NULL;
    if (!source_pane_source_view_copySegment(line, start, len, stackBuf, (int)sizeof(stackBuf), &seg)) {
        return;
    }

    e9ui_text_select_drawText(ctx, owner, font, seg, color, x, y,
                              lineHeight, hitW, sourceBucket, 0, 0);
    if (seg != stackBuf) {
        free(seg);
    }
}

static void
source_pane_source_view_pushRenderClip(e9ui_context_t *ctx, const SDL_Rect *clipRect,
                                       SDL_bool *hadClip, SDL_Rect *prevClip)
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
source_pane_source_view_popRenderClip(e9ui_context_t *ctx, SDL_bool hadClip, const SDL_Rect *prevClip)
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

static int
source_pane_source_view_pointInBounds(const e9ui_component_t *comp, int x, int y)
{
    if (!comp) {
        return 0;
    }
    return x >= comp->bounds.x && x < comp->bounds.x + comp->bounds.w &&
           y >= comp->bounds.y && y < comp->bounds.y + comp->bounds.h;
}

static int
source_pane_source_view_hoverSupportedMode(source_pane_mode_t mode)
{
    return (mode == source_pane_mode_c ||
            mode == source_pane_mode_z80s) ? 1 : 0;
}

static int
source_pane_source_view_getTarget(source_pane_state_t *st,
                                  int allowWhileRunning,
                                  const char **outPath,
                                  int *outManualView,
                                  int *outCurLine)
{
    if (!st || !outPath || !outManualView || !outCurLine) {
        return 0;
    }

    *outPath = NULL;
    *outManualView = 0;
    *outCurLine = 0;

    source_pane_source_view_updateSourceLocation(st, allowWhileRunning);

    int manualView = st->manualSrcActive && st->manualSrcPath;
    const char *path = manualView ? st->manualSrcPath : st->curSrcPath;
    int curLine = manualView ? 0 : st->curSrcLine;
    if (!path || !path[0] || (!manualView && curLine <= 0)) {
        return 0;
    }

    *outPath = path;
    *outManualView = manualView;
    *outCurLine = curLine;
    return 1;
}

static void
source_pane_source_view_loadWindow(e9ui_component_t *self,
                                   e9ui_context_t *ctx,
                                   source_pane_state_t *st,
                                   int allowWhileRunning,
                                   int extraLines,
                                   source_pane_source_window_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));

    if (!self || !ctx || !st ||
        (st->viewMode != source_pane_mode_c && st->viewMode != source_pane_mode_z80s)) {
        return;
    }

    out->font = source_pane_resolveFont(ctx);
    if (!out->font) {
        return;
    }

    out->padPx = 10;
    if (!source_pane_source_view_getTarget(st, allowWhileRunning, &out->path, &out->manualView, &out->curLine)) {
        return;
    }

    out->metrics = source_pane_computeLineMetrics(self, ctx, out->font, out->padPx);
    if (out->metrics.innerHeight <= 0) {
        return;
    }

    out->maxLines = out->metrics.maxLines > 0 ? out->metrics.maxLines : 1;
    int drawMaxLines = out->maxLines + extraLines;
    if (drawMaxLines < out->maxLines) {
        drawMaxLines = out->maxLines;
    }

    int start = 1;
    if (!out->manualView) {
        start = out->curLine - (out->maxLines / 2);
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
    if (!source_getRange(out->path, start, end, &out->lines, &out->count, &out->first, &out->total)) {
        return;
    }

    if (out->count < drawMaxLines && out->total > 0) {
        int missing = drawMaxLines - out->count;
        int altStart = out->first - missing;
        if (altStart < 1) {
            altStart = 1;
        }
        int altEnd = altStart + drawMaxLines - 1;
        if (altEnd > out->total) {
            altEnd = out->total;
        }
        (void)source_getRange(out->path, altStart, altEnd, &out->lines, &out->count, &out->first, &out->total);
    }

    int digits = 1;
    int tmpTotal = out->total > 0 ? out->total : (out->first + out->count - 1);
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

    int th = 0;
    TTF_SizeText(out->font, zeros, &out->gutterW, &th);

    out->gutterPad = e9ui_scale_px(ctx, 16);
    out->contentArea = source_pane_getContentArea(self, ctx, out->padPx);
    out->textX = out->contentArea.x + out->padPx + out->gutterW + out->gutterPad;
    out->hitW = out->contentArea.x + out->contentArea.w - out->textX - out->padPx;
    if (out->hitW < 0) {
        out->hitW = 0;
    }
    out->textDrawX = out->textX - st->cScrollX;
    out->loaded = 1;
}

static void
source_pane_source_view_renderHighlightedLine(e9ui_context_t *ctx,
                                              e9ui_component_t *owner,
                                              TTF_Font *font,
                                              const char *path,
                                              int lineNo,
                                              const char *line,
                                              SDL_Color baseColor,
                                              int textX,
                                              int y,
                                              int lineHeight,
                                              int hitW,
                                              void *sourceBucket)
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
        SDL_Color color = source_pane_source_view_syntaxColor(spans[i].kind);
        source_pane_source_view_drawSegment(ctx, owner, font, line, start, len, color,
                                            cursorX, y, lineHeight, hitW, sourceBucket);
        int segw = source_pane_measureSegment(font, line, start, len);
        cursorX += segw;
        cursorIndex = start + len;
    }
}

static int
source_pane_source_view_resolveCurrentHighlightLine(const source_pane_state_t *st, const char *viewPath)
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

static int
source_pane_source_view_columnFromX(TTF_Font *font, const char *line, int relX)
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
source_pane_source_view_extractHoverExprFallback(const char *line, int column, char *outExpr, int outCap)
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
source_pane_source_view_isIdentifierExpr(const char *expr)
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
source_pane_source_view_sanitizeTooltipLine(char *text)
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

void
source_pane_source_view_updateSourceLocation(source_pane_state_t *st, int allowWhileRunning)
{
    if (!st) {
        return;
    }
    if (!allowWhileRunning && !st->overrideActive && machine_getRunning(debugger.machine)) {
        return;
    }

    uint64_t pc = 0;
    if (st->viewMode == source_pane_mode_z80s) {
        pc = st->overrideActive ? source_z80_resolveAnchorAddr(st->overrideAddr) : source_z80_getCurrentAddr(st);
    } else {
        unsigned long primaryPc = 0;
        if (st->overrideActive) {
            primaryPc = (unsigned long)st->overrideAddr;
        } else {
            (void)machine_findReg(&debugger.machine, "PC", &primaryPc);
        }
        pc = (uint64_t)(primaryPc & 0x00ffffffu);
    }
    if (st->lastResolvedPc == (uint64_t)pc &&
        st->lastResolvedMode == st->viewMode &&
        st->curSrcLine > 0 &&
        st->curSrcPath[0]) {
        return;
    }

    st->lastResolvedPc = (uint64_t)pc;
    st->lastResolvedMode = st->viewMode;
    st->curSrcLine = 0;
    st->curSrcPath[0] = '\0';

    if (st->viewMode == source_pane_mode_z80s) {
        char path[PATH_MAX];
        int line = 0;
        if (source_z80_resolveSourceLocation((uint16_t)pc, path, sizeof(path), &line)) {
            source_pane_resolveSourcePath(path, st->curSrcPath, sizeof(st->curSrcPath));
            st->curSrcLine = line;
        }
        return;
    }

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

int
source_pane_source_view_beginSourceGutterPress(e9ui_component_t *self, e9ui_context_t *ctx,
                                               source_pane_state_t *st, int mx, int my)
{
    if (!self || !ctx || !st) {
        return 0;
    }

    source_pane_source_window_t view;
    source_pane_source_view_loadWindow(self, ctx, st, 0, 0, &view);
    if (!view.loaded || view.metrics.lineHeight <= 0) {
        return 0;
    }

    int gutterRight = view.contentArea.x + view.padPx + view.gutterW + view.gutterPad;
    if (mx >= gutterRight) {
        return 0;
    }

    int row = (my - (view.contentArea.y + view.padPx)) / view.metrics.lineHeight;
    if (row < 0 || row >= view.count) {
        return 0;
    }

    st->gutterPending = 1;
    st->gutterMode = st->viewMode;
    st->gutterLine = view.first + row;
    st->gutterDownX = mx;
    st->gutterDownY = my;
    return 1;
}

int
source_pane_source_view_handleCScrollEvent(e9ui_component_t *self, e9ui_context_t *ctx,
                                           source_pane_state_t *st, const e9ui_event_t *ev)
{
    if (!self || !ctx || !st || !ev ||
        (st->viewMode != source_pane_mode_c && st->viewMode != source_pane_mode_z80s)) {
        return 0;
    }

    if (ev->type == SDL_MOUSEMOTION || ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP) {
        source_pane_source_window_t view;
        source_pane_source_view_loadWindow(self, ctx, st, 0, 1, &view);
        if (!view.loaded) {
            return 0;
        }

        int totalLines = view.total > 0 ? view.total : view.count;
        int visibleLines = view.maxLines;
        int topIndex = view.first > 0 ? (view.first - 1) : 0;
        int scrollX = st->cScrollX;
        int viewW = view.hitW > 0 ? view.hitW : 1;
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
        return 0;
    }

    if (ev->type == SDL_MOUSEWHEEL) {
        int mx = e9ui->mouseX;
        int my = e9ui->mouseY;
        if (!source_pane_source_view_pointInBounds(self, mx, my)) {
            return 0;
        }

        int wheelX = ev->wheel.x;
        if (wheelX == 0) {
            return 0;
        }

        source_pane_source_window_t view;
        source_pane_source_view_loadWindow(self, ctx, st, 0, 1, &view);
        if (!view.loaded) {
            return 0;
        }

        st->cScrollX -= wheelX * e9ui_scale_px(ctx, 24);
        int scrollY = 0;
        int viewW = view.hitW > 0 ? view.hitW : 1;
        e9ui_scrollbar_clamp(viewW, 1, st->cContentPixelWidth, 1, &st->cScrollX, &scrollY);
        return 1;
    }

    return 0;
}

void
source_pane_source_view_clearHover(e9ui_component_t *self, source_pane_state_t *st)
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

void
source_pane_source_view_updateHoverTooltip(e9ui_component_t *self, e9ui_context_t *ctx,
                                           source_pane_state_t *st, const e9ui_event_t *ev)
{
    if (!self || !ctx || !st) {
        return;
    }
    if (ev && ev->type != SDL_MOUSEMOTION) {
        return;
    }
    if (!source_pane_source_view_hoverSupportedMode(st->viewMode)) {
        source_pane_source_view_clearHover(self, st);
        return;
    }
    if (machine_getRunning(debugger.machine) && !st->frozenActive) {
        source_pane_source_view_clearHover(self, st);
        return;
    }

    int mx = ev ? ev->motion.x : ctx->mouseX;
    int my = ev ? ev->motion.y : ctx->mouseY;
    if (!source_pane_source_view_pointInBounds(self, mx, my)) {
        source_pane_source_view_clearHover(self, st);
        return;
    }

    source_pane_source_window_t view;
    source_pane_source_view_loadWindow(self, ctx, st, 0, 0, &view);
    if (!view.loaded || view.metrics.lineHeight <= 0) {
        source_pane_source_view_clearHover(self, st);
        return;
    }

    int relY = my - (view.contentArea.y + view.padPx);
    if (relY < 0) {
        source_pane_source_view_clearHover(self, st);
        return;
    }

    int row = relY / view.metrics.lineHeight;
    if (row < 0 || row >= view.count) {
        source_pane_source_view_clearHover(self, st);
        return;
    }
    if (mx < view.textX) {
        source_pane_source_view_clearHover(self, st);
        return;
    }

    const char *line = view.lines[row] ? view.lines[row] : "";
    int relX = mx - view.textX;
    int lineLen = (int)strlen(line);
    int lineWidth = source_pane_measureSegment(view.font, line, 0, lineLen);
    if (relX > lineWidth) {
        source_pane_source_view_clearHover(self, st);
        return;
    }

    int lineNo = view.first + row;
    int column = source_pane_source_view_columnFromX(view.font, line, relX);
    char expr[256];
    expr[0] = '\0';
    if (!syntax_highlight_getHoverExpr(view.path, lineNo, column, expr, sizeof(expr))) {
        if (!source_pane_source_view_extractHoverExprFallback(line, column, expr, (int)sizeof(expr))) {
            source_pane_source_view_clearHover(self, st);
            return;
        }
    }

    uint32_t pc = 0;
    if (st->viewMode == source_pane_mode_z80s) {
        pc = (uint32_t)source_z80_getCurrentAddr(st);
    } else {
        unsigned long pcReg = 0;
        (void)machine_findReg(&debugger.machine, "PC", &pcReg);
        pc = (uint32_t)pcReg;
    }
    int sameTarget = st->hoverActive &&
                     st->hoverLine == lineNo &&
                     st->hoverCol == column &&
                     st->hoverPc == pc &&
                     strcmp(st->hoverExpr, expr) == 0;

    char tip[1024];
    tip[0] = '\0';
    if (!print_eval_eval(expr, tip, sizeof(tip))) {
        source_pane_source_view_clearHover(self, st);
        return;
    }
    source_pane_source_view_sanitizeTooltipLine(tip);

    if (debugger_toolchainUsesHunkAddr2line() && source_pane_source_view_isIdentifierExpr(expr)) {
        char derefExpr[288];
        if (snprintf(derefExpr, sizeof(derefExpr), "*%s", expr) > 0) {
            char derefTip[512];
            derefTip[0] = '\0';
            if (print_eval_eval(derefExpr, derefTip, sizeof(derefTip))) {
                source_pane_source_view_sanitizeTooltipLine(derefTip);
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

void
source_pane_source_view_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !self->state) {
        return;
    }

    source_pane_state_t *st = (source_pane_state_t *)self->state;
    TTF_Font *useFont = source_pane_resolveFont(ctx);
    if (!useFont) {
        return;
    }

    int freezeWhileRunning = source_pane_shouldFreezeAsmWhileRunning(st);
    if (!freezeWhileRunning) {
        source_pane_trackPosition(st);
    }

    source_pane_source_view_updateSourceLocation(st, st->scrollLocked ? 1 : 0);
    if (st->viewMode == source_pane_mode_c ||
        st->viewMode == source_pane_mode_z80s) {
        source_pane_symbols_refreshSourceFiles(self, st);
    }

    int manualView = st->manualSrcActive && st->manualSrcPath;
    const char *path = manualView ? st->manualSrcPath : st->curSrcPath;
    int curLine = manualView ? 0 : st->curSrcLine;
    int pcHighlightLine = source_pane_source_view_resolveCurrentHighlightLine(st, path);

    const char *functionPath = path;
    if (st->manualSrcActive && st->manualSrcPath && st->manualSrcPath[0] == '\0') {
        functionPath = NULL;
    }
    if (st->viewMode == source_pane_mode_c ||
        st->viewMode == source_pane_mode_z80s) {
        source_pane_symbols_refreshSourceFunctions(self, st, functionPath);
    }
    if ((st->viewMode == source_pane_mode_c ||
         st->viewMode == source_pane_mode_z80s) && !st->functionScrollLock && !manualView) {
        source_pane_symbols_trackCurrentFunction(self, st, path, curLine);
    }

    const int padPx = 10;
    SDL_Rect contentArea = source_pane_getContentArea(self, ctx, padPx);
    if (!path || !path[0] || (!manualView && curLine <= 0)) {
        SDL_Color icol = (SDL_Color){200, 160, 160, 255};
        source_pane_renderStatusMessage(ctx, useFont, contentArea, padPx, icol, "No source code available");
        return;
    }

    source_pane_source_window_t view;
    source_pane_source_view_loadWindow(self, ctx, st, st->scrollLocked ? 1 : 0, 1, &view);
    if (view.metrics.innerHeight <= 0) {
        return;
    }
    if (!view.loaded) {
        SDL_Color icol = (SDL_Color){200, 160, 160, 255};
        source_pane_renderStatusMessage(ctx, useFont, contentArea, padPx, icol, "Failed to load source code");
        return;
    }

    st->scrollLine = view.first;

    SDL_SetRenderDrawColor(ctx->renderer, 26, 26, 30, 255);
    SDL_Rect gutter = { view.contentArea.x, view.contentArea.y,
                        view.padPx + view.gutterW + view.gutterPad, view.contentArea.h };
    SDL_RenderFillRect(ctx->renderer, &gutter);

    int y = view.contentArea.y + view.padPx;
    int lineHeight = view.metrics.lineHeight;
    int clipBottom = view.contentArea.y + view.contentArea.h + lineHeight;
    SDL_Color txt = (SDL_Color){220, 220, 220, 255};
    SDL_Color lno = (SDL_Color){160, 160, 180, 255};
    SDL_Color lnoBpOn = (SDL_Color){120, 200, 120, 255};
    SDL_Color lnoBpOff = (SDL_Color){200, 140, 60, 255};
    if (st) {
        int scrollY = 0;
        int viewW = view.hitW > 0 ? view.hitW : 1;
        e9ui_scrollbar_clamp(viewW, 1, st->cContentPixelWidth, 1, &st->cScrollX, &scrollY);
        st->cContentPixelWidth = 0;
        view.textDrawX = view.textX - st->cScrollX;
    }

    SDL_Rect textClip = { view.textX, view.contentArea.y, view.hitW, view.contentArea.h };
    const machine_breakpoint_t *bps = NULL;
    int bpCount = 0;
    if (machine_getBreakpoints(&debugger.machine, &bps, &bpCount)) {
        for (int i = 0; i < bpCount; ++i) {
            machine_breakpoint_t *bp = (machine_breakpoint_t *)&bps[i];
            if (bp->line <= 0 || !bp->file[0]) {
                breakpoints_resolveLocation(bp);
            }
        }
    } else {
        bps = NULL;
        bpCount = 0;
    }

    for (int i = 0; i < view.count; ++i) {
        const char *ln = view.lines[i] ? view.lines[i] : "";
        int lineNo = view.first + i;
        if (pcHighlightLine > 0 && lineNo == pcHighlightLine) {
            SDL_SetRenderDrawColor(ctx->renderer, 40, 72, 138, 255);
            SDL_Rect hl = { self->bounds.x + 2, y - 2, self->bounds.w - 4, lineHeight + 4 };
            SDL_RenderFillRect(ctx->renderer, &hl);
        }
        if (st->searchActive && st->searchMatchValid && lineNo == st->searchMatchLine) {
            SDL_bool hadTextClip = SDL_FALSE;
            SDL_Rect prevTextClip = {0};
            source_pane_source_view_pushRenderClip(ctx, &textClip, &hadTextClip, &prevTextClip);
            int lineLen = (int)strlen(ln);
            int startCol = st->searchMatchColumn;
            int matchLen = st->searchMatchLength;
            if (startCol < 0) {
                startCol = 0;
            }
            if (startCol > lineLen) {
                startCol = lineLen;
            }
            if (matchLen < 0) {
                matchLen = 0;
            }
            if (startCol + matchLen > lineLen) {
                matchLen = lineLen - startCol;
            }
            if (matchLen > 0) {
                int hx = view.textDrawX + source_pane_measureSegment(useFont, ln, 0, startCol);
                int hw = source_pane_measureSegment(useFont, ln, startCol, matchLen);
                SDL_SetRenderDrawColor(ctx->renderer, 186, 152, 62, 196);
                SDL_Rect mhl = { hx, y - 1, hw, lineHeight + 2 };
                SDL_RenderFillRect(ctx->renderer, &mhl);
            }
            source_pane_source_view_popRenderClip(ctx, hadTextClip, &prevTextClip);
        }

        char numbuf[16];
        snprintf(numbuf, sizeof(numbuf), "%d", lineNo);
        int nw = 0;
        int nh = 0;
        TTF_SizeText(useFont, numbuf, &nw, &nh);
        int lnx = view.contentArea.x + view.padPx + (view.gutterW - nw);

        int nsw = 0;
        int nsh = 0;
        SDL_Color useCol = lno;
        machine_breakpoint_t *bp = source_pane_fileline_findBreakpointForLine(path, lineNo, bps, bpCount);
        if (bp) {
            useCol = bp->enabled ? lnoBpOn : lnoBpOff;
        }
        SDL_Texture *nt = e9ui_text_cache_getText(ctx->renderer, useFont, numbuf, useCol, &nsw, &nsh);
        if (nt) {
            SDL_Rect nr = { lnx, y, nsw, nsh };
            SDL_RenderCopy(ctx->renderer, nt, NULL, &nr);
        }

        int linePixelW = source_pane_measureSegment(useFont, ln, 0, (int)strlen(ln));
        if (linePixelW > st->cContentPixelWidth) {
            st->cContentPixelWidth = linePixelW;
        }

        void *sourceBucket = (void *)&st->bucketSource;
        SDL_bool hadTextClip = SDL_FALSE;
        SDL_Rect prevTextClip = {0};
        source_pane_source_view_pushRenderClip(ctx, &textClip, &hadTextClip, &prevTextClip);
        source_pane_source_view_renderHighlightedLine(ctx, self, useFont, path, lineNo, ln, txt,
                                                      view.textDrawX, y, lineHeight, view.hitW, sourceBucket);
        source_pane_source_view_popRenderClip(ctx, hadTextClip, &prevTextClip);

        y += lineHeight;
        if (y > clipBottom) {
            break;
        }
    }

    if (st->hoverActive) {
        source_pane_source_view_updateHoverTooltip(self, ctx, st, NULL);
    }
    if (view.total > 0) {
        int topIndex = view.first > 0 ? (view.first - 1) : 0;
        int scrollY = 0;
        int viewW = view.hitW > 0 ? view.hitW : 1;
        e9ui_scrollbar_clamp(viewW, 1, st->cContentPixelWidth, 1, &st->cScrollX, &scrollY);
        e9ui_scrollbar_render(self,
                              ctx,
                              self->bounds,
                              viewW,
                              view.maxLines,
                              st->cContentPixelWidth,
                              view.total,
                              st->cScrollX,
                              topIndex);
    }
}
