/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"
#include "e9ui_scrollbar.h"

typedef struct e9ui_scroll_state {
    e9ui_component_t *child;
    int scrollX;
    int scrollY;
    int contentW;
    int contentH;
    int contentWidthPx;
    int contentHeightPx;
    int lineHeight;
    e9ui_scrollbar_state_t scrollbar;
} e9ui_scroll_state_t;

static int
scroll_measureLineHeight(e9ui_context_t *ctx)
{
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : (ctx ? ctx->font : NULL);
    int lineHeight = font ? TTF_FontHeight(font) : 0;
    if (lineHeight <= 0) {
        lineHeight = 16;
    }
    return lineHeight;
}

static void
scroll_clamp(e9ui_scroll_state_t *st, int viewW, int viewH)
{
    if (!st) {
        return;
    }
    e9ui_scrollbar_clamp(viewW, viewH, st->contentW, st->contentH, &st->scrollX, &st->scrollY);
}

static int
scroll_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    e9ui_scroll_state_t *st = (e9ui_scroll_state_t*)self->state;
    if (st && st->contentHeightPx > 0) {
        return st->contentHeightPx;
    }
    if (!st || !st->child || !st->child->preferredHeight) {
        return 0;
    }
    return st->child->preferredHeight(st->child, ctx, availW);
}

static void
scroll_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    self->bounds = bounds;
    e9ui_scroll_state_t *st = (e9ui_scroll_state_t*)self->state;
    if (!st || !st->child || !st->child->layout) {
        return;
    }
    int contentW = bounds.w;
    if (st->contentWidthPx > 0) {
        contentW = st->contentWidthPx;
    }
    int contentH = bounds.h;
    if (st->contentHeightPx > 0) {
        contentH = st->contentHeightPx;
    } else if (st->child->preferredHeight) {
        contentH = st->child->preferredHeight(st->child, ctx, bounds.w);
    }
    st->contentW = contentW;
    st->contentH = contentH;
    st->lineHeight = scroll_measureLineHeight(ctx);
    scroll_clamp(st, bounds.w, bounds.h);
    e9ui_rect_t childBounds = { bounds.x - st->scrollX, bounds.y - st->scrollY, st->contentW, st->contentH };
    st->child->layout(st->child, ctx, childBounds);
}

static void
scroll_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) {
        return;
    }
    e9ui_scroll_state_t *st = (e9ui_scroll_state_t*)self->state;
    if (!st || !st->child || !st->child->render) {
        return;
    }
    SDL_Rect prev;
    SDL_bool clipEnabled = SDL_RenderIsClipEnabled(ctx->renderer);
    SDL_RenderGetClipRect(ctx->renderer, &prev);
    SDL_Rect clip = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    if (clipEnabled) {
        SDL_Rect clipped = clip;
        if (SDL_IntersectRect(&prev, &clip, &clipped)) {
            SDL_RenderSetClipRect(ctx->renderer, &clipped);
        } else {
            SDL_RenderSetClipRect(ctx->renderer, &clip);
        }
    } else {
        SDL_RenderSetClipRect(ctx->renderer, &clip);
    }
    st->child->render(st->child, ctx);
    e9ui_scrollbar_render(self,
                          ctx,
                          self->bounds,
                          self->bounds.w,
                          self->bounds.h,
                          st->contentW,
                          st->contentH,
                          st->scrollX,
                          st->scrollY);
    if (clipEnabled) {
        SDL_RenderSetClipRect(ctx->renderer, &prev);
    } else {
        SDL_RenderSetClipRect(ctx->renderer, NULL);
    }
}

static int
scroll_handleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!self || !ctx || !ev) {
        return 0;
    }
    e9ui_scroll_state_t *st = (e9ui_scroll_state_t*)self->state;
    if (!st) {
        return 0;
    }

    if (e9ui_scrollbar_handleEvent(self,
                                   ctx,
                                   ev,
                                   self->bounds,
                                   self->bounds.w,
                                   self->bounds.h,
                                   st->contentW,
                                   st->contentH,
                                   &st->scrollX,
                                   &st->scrollY,
                                   &st->scrollbar)) {
        return 1;
    }

    if (ev->type == SDL_MOUSEWHEEL) {
        int mx = ctx->mouseX;
        int my = ctx->mouseY;
        if (mx >= self->bounds.x && mx < self->bounds.x + self->bounds.w &&
            my >= self->bounds.y && my < self->bounds.y + self->bounds.h) {
            int consumed = 0;
            int wheelX = ev->wheel.x;
            int wheelY = ev->wheel.y;
            if (ev->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                wheelX = -wheelX;
                wheelY = -wheelY;
            }
            SDL_Keymod mods = ctx->keyMods;
            if (wheelX == 0 && (mods & KMOD_SHIFT)) {
                wheelX = wheelY;
                wheelY = 0;
            }
            if (wheelX != 0) {
                const int step = st->lineHeight > 0 ? st->lineHeight : 16;
                if (e9ui_scrollbar_maxScroll(st->contentW, self->bounds.w) > 0) {
                    st->scrollX -= wheelX * step;
                    consumed = 1;
                }
            }
            if (wheelY != 0) {
                int step = st->lineHeight > 0 ? st->lineHeight : 16;
                if (e9ui_scrollbar_maxScroll(st->contentH, self->bounds.h) > 0) {
                    st->scrollY += wheelY * step;
                    consumed = 1;
                }
            }
            if (consumed) {
                scroll_clamp(st, self->bounds.w, self->bounds.h);
                return 1;
            }
        }
    }
    return 0;
}

static void
scroll_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self || !self->state) {
        return;
    }
    e9ui_scroll_state_t *st = (e9ui_scroll_state_t*)self->state;
    alloc_free(st);
    self->state = NULL;
}

e9ui_component_t *
e9ui_scroll_make(e9ui_component_t *child)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    e9ui_scroll_state_t *st = (e9ui_scroll_state_t*)alloc_calloc(1, sizeof(*st));
    if (!c || !st) {
        alloc_free(c);
        alloc_free(st);
        return NULL;
    }
    st->child = child;
    c->name = "e9ui_scroll";
    c->state = st;
    c->preferredHeight = scroll_preferredHeight;
    c->layout = scroll_layout;
    c->render = scroll_render;
    c->handleEvent = scroll_handleEvent;
    c->dtor = scroll_dtor;
    if (child) {
        e9ui_child_add(c, child, 0);
    }
    return c;
}

void
e9ui_scroll_setContentHeightPx(e9ui_component_t *scroll, int contentHeight_px)
{
    if (!scroll || !scroll->state) {
        return;
    }
    e9ui_scroll_state_t *st = (e9ui_scroll_state_t*)scroll->state;
    st->contentHeightPx = contentHeight_px > 0 ? contentHeight_px : 0;
}

void
e9ui_scroll_setContentWidthPx(e9ui_component_t *scroll, int contentWidth_px)
{
    if (!scroll || !scroll->state) {
        return;
    }
    e9ui_scroll_state_t *st = (e9ui_scroll_state_t*)scroll->state;
    st->contentWidthPx = contentWidth_px > 0 ? contentWidth_px : 0;
}

void
e9ui_scroll_getScrollPx(e9ui_component_t *scroll, int *outScrollX, int *outScrollY)
{
    if (outScrollX) {
        *outScrollX = 0;
    }
    if (outScrollY) {
        *outScrollY = 0;
    }
    if (!scroll || !scroll->state) {
        return;
    }
    e9ui_scroll_state_t *st = (e9ui_scroll_state_t*)scroll->state;
    if (outScrollX) {
        *outScrollX = st->scrollX;
    }
    if (outScrollY) {
        *outScrollY = st->scrollY;
    }
}

void
e9ui_scroll_setScrollPx(e9ui_component_t *scroll, int scrollX, int scrollY)
{
    if (!scroll || !scroll->state) {
        return;
    }
    e9ui_scroll_state_t *st = (e9ui_scroll_state_t*)scroll->state;
    st->scrollX = scrollX;
    st->scrollY = scrollY;
    scroll_clamp(st, scroll->bounds.w, scroll->bounds.h);
}
