/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"
#include "e9ui_scrollbar.h"
#include "debugger.h"

#include <string.h>

typedef struct e9ui_console_state {
    int bucketConsole;
    e9ui_scrollbar_state_t scrollbar;
} console_state_t;

typedef struct console_wrap_metrics {
    int visualLineCount;
    int topVisualIndex;
} console_wrap_metrics_t;

static int
console_getTopIndex(int count, int visibleLines);

static int
console_utf8ByteOffsetForChars(const char *text, int chars)
{
    int count = 0;
    int offset = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (p && *p && count < chars) {
        if ((*p & 0xc0u) != 0x80u) {
            count++;
        }
        p++;
        offset++;
    }
    return offset;
}

static int
console_utf8CharCount(const char *text)
{
    int count = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (p && *p) {
        if ((*p & 0xc0u) != 0x80u) {
            count++;
        }
        p++;
    }
    return count;
}

static int
console_measureUtf8(TTF_Font *font, const char *text, int measureWidth, int *extent, int *count)
{
    if (extent) {
        *extent = 0;
    }
    if (count) {
        *count = 0;
    }
    if (!font || !text) {
        return -1;
    }
    if (!*text) {
        return 0;
    }
    if (measureWidth <= 0) {
        return 0;
    }

    int fullW = 0;
    if (TTF_SizeUTF8(font, text, &fullW, NULL) == 0 && fullW <= measureWidth) {
        if (extent) {
            *extent = fullW;
        }
        if (count) {
            *count = console_utf8CharCount(text);
        }
        return 0;
    }

    int totalChars = console_utf8CharCount(text);
    int lo = 1;
    int hi = totalChars;
    int bestChars = 0;
    int bestExtent = 0;

    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int byteCount = console_utf8ByteOffsetForChars(text, mid);
        if (byteCount <= 0) {
            hi = mid - 1;
            continue;
        }

        char *scratch = alloc_alloc((size_t)byteCount + 1);
        if (!scratch) {
            return -1;
        }
        memcpy(scratch, text, (size_t)byteCount);
        scratch[byteCount] = '\0';

        int w = 0;
        int ok = (TTF_SizeUTF8(font, scratch, &w, NULL) == 0) ? 1 : 0;
        alloc_free(scratch);
        if (!ok) {
            return -1;
        }

        if (w <= measureWidth) {
            bestChars = mid;
            bestExtent = w;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    if (extent) {
        *extent = bestExtent;
    }
    if (count) {
        *count = bestChars;
    }
    return 0;
}

static int
console_wrapSegmentBytes(TTF_Font *font, const char *text, int wrapWidth)
{
    size_t textLen = 0;
    int fitChars = 0;
    int fitBytes = 0;
    int bestBreak = -1;

    if (!text || !*text) {
        return 0;
    }
    if (!font || wrapWidth <= 0) {
        return (int)strlen(text);
    }

    textLen = strlen(text);
    if (console_measureUtf8(font, text, wrapWidth, NULL, &fitChars) != 0) {
        return (int)textLen;
    }
    if (fitChars <= 0) {
        fitChars = 1;
    }

    fitBytes = console_utf8ByteOffsetForChars(text, fitChars);
    if (fitBytes <= 0) {
        fitBytes = 1;
    }
    if ((size_t)fitBytes >= textLen) {
        return (int)textLen;
    }

    for (int i = 0; i < fitBytes; i++) {
        if (text[i] == ' ' || text[i] == '\t') {
            bestBreak = i;
        }
    }
    if (bestBreak > 0) {
        return bestBreak;
    }

    return fitBytes;
}

static const char *
console_skipWrapWhitespace(const char *text)
{
    const char *p = text;
    while (p && (*p == ' ' || *p == '\t')) {
        p++;
    }
    return p;
}

static int
console_countWrappedLine(TTF_Font *font, const char *text, int wrapWidth)
{
    int count = 0;
    const char *p = text;

    if (!p || !*p) {
        return 1;
    }

    while (*p) {
        int segmentBytes = console_wrapSegmentBytes(font, p, wrapWidth);
        if (segmentBytes <= 0) {
            segmentBytes = 1;
        }
        count++;
        p += segmentBytes;
        p = console_skipWrapWhitespace(p);
    }

    return count > 0 ? count : 1;
}

static console_wrap_metrics_t
console_getWrapMetrics(TTF_Font *font, int wrapWidth, int visibleLines)
{
    console_wrap_metrics_t metrics = { 0, 0 };

    for (int i = 0; i < debugger.console.n; ++i) {
        int phys = linebuf_phys_index(&debugger.console, i);
        const char *line = debugger.console.lines[phys];
        metrics.visualLineCount += console_countWrappedLine(font, line ? line : "", wrapWidth);
    }

    if (metrics.visualLineCount < 1) {
        metrics.visualLineCount = 1;
    }

    metrics.topVisualIndex = console_getTopIndex(metrics.visualLineCount, visibleLines);
    return metrics;
}

static int
console_lineHeightPx(e9ui_context_t *ctx, TTF_Font *font)
{
    TTF_Font *useFont = font ? font : (e9ui->theme.text.console ? e9ui->theme.text.console : (ctx ? ctx->font : NULL));
    int lineHeight = useFont ? TTF_FontHeight(useFont) : 0;
    if (lineHeight <= 0) {
        lineHeight = 16;
    }
    return lineHeight;
}

static int
console_visibleLines(e9ui_component_t *self, e9ui_context_t *ctx, TTF_Font *font)
{
    if (!self) {
        return 1;
    }
    int lineHeight = console_lineHeightPx(ctx, font);
    int pad = 10;
    int availH = self->bounds.h - pad - 10;
    if (availH < lineHeight) {
        availH = lineHeight;
    }
    int visibleLines = availH / lineHeight;
    if (visibleLines < 1) {
        visibleLines = 1;
    }
    return visibleLines;
}

static int
console_getTopIndex(int count, int visibleLines)
{
    int maxTop = e9ui_scrollbar_maxScroll(count, visibleLines);
    int topIndex = maxTop - debugger.consoleScrollLines;
    if (topIndex < 0) {
        topIndex = 0;
    }
    if (topIndex > maxTop) {
        topIndex = maxTop;
    }
    return topIndex;
}

static void
console_setTopIndex(int count, int visibleLines, int topIndex)
{
    int maxTop = e9ui_scrollbar_maxScroll(count, visibleLines);
    if (topIndex < 0) {
        topIndex = 0;
    }
    if (topIndex > maxTop) {
        topIndex = maxTop;
    }
    debugger.consoleScrollLines = maxTop - topIndex;
    if (debugger.consoleScrollLines < 0) {
        debugger.consoleScrollLines = 0;
    }
}

static int
console_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self; (void)availW; (void)ctx; return 0;
}

static void
console_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx; self->bounds = bounds;
}

static void
console_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    console_state_t *st = (console_state_t*)self->state;
    (void)st;
    TTF_Font *useFont = e9ui->theme.text.console ? e9ui->theme.text.console : (ctx?ctx->font:NULL);
    if (!useFont) {
        return; // no TTF font available; skip console text rendering
    }
    int lh = TTF_FontHeight(useFont); if (lh <= 0) lh = 16;
    
    int pad = 10;
    int textX = self->bounds.x + pad;
    int hitW = self->bounds.w - pad * 2;
    if (hitW < 0) {
        hitW = 0;
    }
    int wrapWidth = hitW > 0 ? hitW : 1;
    int y = self->bounds.y + 4;
    int availH = self->bounds.h - pad - 10;
    if (availH < lh) {
        availH = lh;
    }
    int visLines = availH / lh;
    if (visLines < 1) {
        visLines = 1;
    }
    console_wrap_metrics_t metrics = console_getWrapMetrics(useFont, wrapWidth, visLines);
    int renderStart = metrics.topVisualIndex;
    int renderEnd = renderStart + visLines;
    int visualIndex = 0;

    for (int i = 0; i < debugger.console.n; ++i) {
        int phys = linebuf_phys_index(&debugger.console, i);
        const char *ln = debugger.console.lines[phys];
        unsigned char iserr = debugger.console.is_err[phys];
        SDL_Color colc = iserr ? (SDL_Color){220,120,120,255} : (SDL_Color){200,200,200,255};
        const char *line = ln ? ln : "";
        const char *p = line;

        if (!*p) {
            if (visualIndex >= renderStart && visualIndex < renderEnd) {
                e9ui_drawSelectableText(ctx, self, useFont, "", colc, textX, y, lh,
                                        hitW, &st->bucketConsole, 0, 1);
                y += lh;
            }
            visualIndex++;
            if (visualIndex >= renderEnd || y > self->bounds.y + self->bounds.h - 10) {
                break;
            }
            continue;
        }

        while (*p) {
            int segmentBytes = console_wrapSegmentBytes(useFont, p, wrapWidth);
            if (segmentBytes <= 0) {
                segmentBytes = 1;
            }

            if (visualIndex >= renderStart && visualIndex < renderEnd) {
                char *segment = alloc_alloc((size_t)segmentBytes + 1);
                if (!segment) {
                    return;
                }
                memcpy(segment, p, (size_t)segmentBytes);
                segment[segmentBytes] = '\0';
                e9ui_drawSelectableText(ctx, self, useFont, segment, colc, textX, y, lh,
                                        hitW, &st->bucketConsole, 0, 1);
                alloc_free(segment);
                y += lh;
            }

            visualIndex++;
            p += segmentBytes;
            p = console_skipWrapWhitespace(p);

            if (visualIndex >= renderEnd || y > self->bounds.y + self->bounds.h - 10) {
                break;
            }
        }

        if (visualIndex >= renderEnd || y > self->bounds.y + self->bounds.h - 10) {
            break;
        }
    }

    e9ui_scrollbar_render(self,
                          ctx,
                          self->bounds,
                          1,
                          visLines,
                          1,
                          metrics.visualLineCount,
                          0,
                          metrics.topVisualIndex);
}

static int
console_handleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    (void)self; (void)ctx;
    console_state_t *st = self ? (console_state_t*)self->state : NULL;

    if (self && ctx && st &&
        (ev->type == SDL_MOUSEMOTION || ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP)) {
        TTF_Font *useFont = e9ui->theme.text.console ? e9ui->theme.text.console : (ctx ? ctx->font : NULL);
        int visibleLines = console_visibleLines(self, ctx, NULL);
        int pad = 10;
        int hitW = self->bounds.w - pad * 2;
        if (hitW < 1) {
            hitW = 1;
        }
        console_wrap_metrics_t metrics = console_getWrapMetrics(useFont, hitW, visibleLines);
        int topIndex = metrics.topVisualIndex;
        int scrollX = 0;
        if (e9ui_scrollbar_handleEvent(self,
                                       ctx,
                                       ev,
                                       self->bounds,
                                       1,
                                       visibleLines,
                                       1,
                                       metrics.visualLineCount,
                                       &scrollX,
                                       &topIndex,
                                       &st->scrollbar)) {
            console_setTopIndex(metrics.visualLineCount, visibleLines, topIndex);
            return 1;
        }
    }

    if (ev->type == SDL_KEYDOWN) {
        SDL_Keycode kc = ev->key.keysym.sym;
        if (kc == SDLK_PAGEUP) { debugger.consoleScrollLines += 8; return 1; }
        if (kc == SDLK_PAGEDOWN) { debugger.consoleScrollLines -= 8; if (debugger.consoleScrollLines < 0) debugger.consoleScrollLines = 0; return 1; }
        if (kc == SDLK_HOME) {
            TTF_Font *useFont = e9ui->theme.text.console ? e9ui->theme.text.console : (ctx ? ctx->font : NULL);
            int pad = 10;
            int hitW = self->bounds.w - pad * 2;
            if (hitW < 1) {
                hitW = 1;
            }
            debugger.consoleScrollLines = console_getWrapMetrics(useFont, hitW, 1).visualLineCount;
            return 1;
        }
        if (kc == SDLK_END) { debugger.consoleScrollLines = 0; return 1; }
    } else if (ev->type == SDL_MOUSEWHEEL) {
        if (!ctx) {
            return 0;
        }
        int mx = ctx->mouseX;
        int my = ctx->mouseY;
        if (mx < self->bounds.x || mx >= self->bounds.x + self->bounds.w ||
            my < self->bounds.y || my >= self->bounds.y + self->bounds.h) {
            return 0;
        }
        int linesPerWheel = 3;
        int visibleLines = console_visibleLines(self, ctx, NULL);
        TTF_Font *useFont = e9ui->theme.text.console ? e9ui->theme.text.console : (ctx ? ctx->font : NULL);
        int pad = 10;
        int hitW = self->bounds.w - pad * 2;
        if (hitW < 1) {
            hitW = 1;
        }
        console_wrap_metrics_t metrics = console_getWrapMetrics(useFont, hitW, visibleLines);
        int topIndex = metrics.topVisualIndex;
        topIndex += linesPerWheel * ev->wheel.y;
        console_setTopIndex(metrics.visualLineCount, visibleLines, topIndex);
        return 1;
    }
    return 0;
}

static void
console_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
  (void)ctx;
  (void)self;
}

e9ui_component_t *
console_makeComponent(void)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    c->name = "e9ui_console";
    console_state_t *st = (console_state_t*)alloc_calloc(1, sizeof(console_state_t));
    c->state = st;
    c->preferredHeight = console_preferredHeight;
    c->layout = console_layout;
    c->render = console_render;
    c->handleEvent = console_handleEvent;
    c->dtor = console_dtor;
    return c;
}
