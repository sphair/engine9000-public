/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"

#include "e9ui_scrollbar.h"

typedef struct e9ui_scrollbar_bar_rects {
    int visible;
    int contentSize;
    int viewSize;
    int maxScroll;
    int thickness;
    SDL_Rect trackRect;
    SDL_Rect thumbRect;
} e9ui_scrollbar_bar_rects_t;

static SDL_Cursor *e9ui_scrollbar_cursorArrow = NULL;
static SDL_Cursor *e9ui_scrollbar_cursorNs = NULL;
static SDL_Cursor *e9ui_scrollbar_cursorEw = NULL;

static void
e9ui_scrollbar_ensureCursors(void)
{
    if (!e9ui_scrollbar_cursorArrow) {
        e9ui_scrollbar_cursorArrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    }
    if (!e9ui_scrollbar_cursorNs) {
        e9ui_scrollbar_cursorNs = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
    }
    if (!e9ui_scrollbar_cursorEw) {
        e9ui_scrollbar_cursorEw = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
    }
}

static SDL_Cursor *
e9ui_scrollbar_cursorForAxis(int axis)
{
    e9ui_scrollbar_ensureCursors();
    if (axis == 2) {
        return e9ui_scrollbar_cursorNs ? e9ui_scrollbar_cursorNs : SDL_GetDefaultCursor();
    }
    if (axis == 1) {
        return e9ui_scrollbar_cursorEw ? e9ui_scrollbar_cursorEw : SDL_GetDefaultCursor();
    }
    return e9ui_scrollbar_cursorArrow ? e9ui_scrollbar_cursorArrow : SDL_GetDefaultCursor();
}

static void
e9ui_scrollbar_setCursorForAxis(e9ui_context_t *ctx, void *owner, int axis, int capture)
{
    SDL_Cursor *cursor = e9ui_scrollbar_cursorForAxis(axis);
    if (!ctx || !cursor) {
        return;
    }
    if (capture) {
        e9ui_cursorCapture(ctx, owner, cursor);
    } else {
        e9ui_cursorRequest(ctx, owner, cursor);
    }
}

int
e9ui_scrollbar_maxScroll(int contentSize, int viewSize)
{
    int maxScroll = contentSize - viewSize;
    if (maxScroll < 0) {
        maxScroll = 0;
    }
    return maxScroll;
}

void
e9ui_scrollbar_clamp(int viewW, int viewH, int contentW, int contentH, int *scrollX, int *scrollY)
{
    if (scrollX) {
        int maxScrollX = e9ui_scrollbar_maxScroll(contentW, viewW);
        if (*scrollX < 0) {
            *scrollX = 0;
        }
        if (*scrollX > maxScrollX) {
            *scrollX = maxScrollX;
        }
    }
    if (scrollY) {
        int maxScrollY = e9ui_scrollbar_maxScroll(contentH, viewH);
        if (*scrollY < 0) {
            *scrollY = 0;
        }
        if (*scrollY > maxScrollY) {
            *scrollY = maxScrollY;
        }
    }
}

static int
e9ui_scrollbar_barThickness(e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    int thickness = e9ui_scale_px(ctx, 8);
    if (thickness < 4) {
        thickness = 4;
    }
    if (thickness >= bounds.w) {
        thickness = bounds.w > 1 ? bounds.w - 1 : 1;
    }
    if (thickness >= bounds.h) {
        thickness = bounds.h > 1 ? bounds.h - 1 : 1;
    }
    if (thickness <= 0) {
        return 0;
    }
    return thickness;
}

static int
e9ui_scrollbar_edgeMargin(e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    int margin = e9ui_scale_px(ctx, 4);
    if (margin < 0) {
        margin = 0;
    }
    if (margin >= bounds.w) {
        margin = bounds.w > 1 ? bounds.w - 1 : 0;
    }
    return margin;
}

static int
e9ui_scrollbar_pointInRect(SDL_Rect rect, int x, int y)
{
    return x >= rect.x && x < rect.x + rect.w &&
           y >= rect.y && y < rect.y + rect.h;
}

static void
e9ui_scrollbar_computeBarRects(e9ui_context_t *ctx,
                               e9ui_rect_t bounds,
                               int vertical,
                               int contentW,
                               int contentH,
                               int viewW,
                               int viewH,
                               int scrollX,
                               int scrollY,
                               e9ui_scrollbar_bar_rects_t *out)
{
    if (!out) {
        return;
    }
    SDL_memset(out, 0, sizeof(*out));
    if (!ctx || bounds.w <= 0 || bounds.h <= 0) {
        return;
    }

    int maxScrollX = e9ui_scrollbar_maxScroll(contentW, viewW);
    int maxScrollY = e9ui_scrollbar_maxScroll(contentH, viewH);
    int maxScroll = vertical ? maxScrollY : maxScrollX;
    if (maxScroll <= 0) {
        return;
    }

    int thickness = e9ui_scrollbar_barThickness(ctx, bounds);
    if (thickness <= 0) {
        return;
    }
    int edgeMargin = e9ui_scrollbar_edgeMargin(ctx, bounds);

    SDL_Rect trackRect;
    if (vertical) {
        trackRect.x = bounds.x + bounds.w - edgeMargin - thickness;
        trackRect.y = bounds.y + edgeMargin;
        trackRect.w = thickness;
        trackRect.h = bounds.h - edgeMargin - edgeMargin - (maxScrollX > 0 ? thickness : 0);
    } else {
        trackRect.x = bounds.x;
        trackRect.y = bounds.y + bounds.h - edgeMargin - thickness;
        trackRect.w = bounds.w - (maxScrollY > 0 ? (thickness + edgeMargin) : 0);
        trackRect.h = thickness;
    }
    if (trackRect.w <= 0 || trackRect.h <= 0) {
        return;
    }

    int contentSize = vertical ? contentH : contentW;
    int viewSize = vertical ? viewH : viewW;
    int scrollPos = vertical ? scrollY : scrollX;
    int trackLen = vertical ? trackRect.h : trackRect.w;
    if (contentSize <= viewSize || viewSize <= 0 || trackLen <= 0) {
        return;
    }

    int minThumb = e9ui_scale_px(ctx, 18);
    if (minThumb < 6) {
        minThumb = 6;
    }
    int thumbLen = (int)(((long long)trackLen * (long long)viewSize) / (long long)contentSize);
    if (thumbLen < minThumb) {
        thumbLen = minThumb;
    }
    if (thumbLen > trackLen) {
        thumbLen = trackLen;
    }

    int thumbTravel = trackLen - thumbLen;
    int thumbOffset = 0;
    if (maxScroll > 0 && thumbTravel > 0) {
        thumbOffset = (int)(((long long)thumbTravel * (long long)scrollPos) / (long long)maxScroll);
    }

    int thumbInset = e9ui_scale_px(ctx, 1);
    if (thumbInset < 1) {
        thumbInset = 1;
    }
    if (thumbInset * 2 >= thickness) {
        thumbInset = 0;
    }

    SDL_Rect thumbRect = trackRect;
    if (vertical) {
        thumbRect.y += thumbOffset;
        thumbRect.h = thumbLen;
        thumbRect.x += thumbInset;
        thumbRect.w -= thumbInset * 2;
    } else {
        thumbRect.x += thumbOffset;
        thumbRect.w = thumbLen;
        thumbRect.y += thumbInset;
        thumbRect.h -= thumbInset * 2;
    }
    if (thumbRect.w <= 0 || thumbRect.h <= 0) {
        return;
    }

    out->visible = 1;
    out->contentSize = contentSize;
    out->viewSize = viewSize;
    out->maxScroll = maxScroll;
    out->thickness = thickness;
    out->trackRect = trackRect;
    out->thumbRect = thumbRect;
}

static int
e9ui_scrollbar_scrollFromBarPointer(const e9ui_scrollbar_bar_rects_t *bar,
                                    int vertical,
                                    int pointerPos,
                                    int thumbGrabOffset)
{
    if (!bar || !bar->visible || bar->maxScroll <= 0) {
        return 0;
    }

    int trackStart = vertical ? bar->trackRect.y : bar->trackRect.x;
    int trackLen = vertical ? bar->trackRect.h : bar->trackRect.w;
    int thumbLen = vertical ? bar->thumbRect.h : bar->thumbRect.w;
    int thumbTravel = trackLen - thumbLen;
    if (thumbTravel <= 0) {
        return 0;
    }

    int thumbStart = pointerPos - thumbGrabOffset;
    if (thumbStart < trackStart) {
        thumbStart = trackStart;
    }
    if (thumbStart > trackStart + thumbTravel) {
        thumbStart = trackStart + thumbTravel;
    }
    return (int)(((long long)(thumbStart - trackStart) * (long long)bar->maxScroll) / (long long)thumbTravel);
}

static void
e9ui_scrollbar_renderBar(e9ui_context_t *ctx,
                         SDL_Rect trackRect,
                         SDL_Rect thumbRect)
{
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 48);
    SDL_RenderFillRect(ctx->renderer, &trackRect);
    SDL_SetRenderDrawColor(ctx->renderer, 255, 255, 255, 132);
    SDL_RenderFillRect(ctx->renderer, &thumbRect);
}

int
e9ui_scrollbar_pointInScrollbarPx(e9ui_context_t *ctx,
                                  e9ui_rect_t bounds,
                                  int viewW,
                                  int viewH,
                                  int contentW,
                                  int contentH,
                                  int mouseX,
                                  int mouseY)
{
    e9ui_scrollbar_bar_rects_t vBar;
    e9ui_scrollbar_bar_rects_t hBar;
    e9ui_scrollbar_computeBarRects(ctx, bounds, 1, contentW, contentH, viewW, viewH, 0, 0, &vBar);
    e9ui_scrollbar_computeBarRects(ctx, bounds, 0, contentW, contentH, viewW, viewH, 0, 0, &hBar);
    if (vBar.visible && e9ui_scrollbar_pointInRect(vBar.trackRect, mouseX, mouseY)) {
        return 1;
    }
    if (hBar.visible && e9ui_scrollbar_pointInRect(hBar.trackRect, mouseX, mouseY)) {
        return 1;
    }
    return 0;
}

void
e9ui_scrollbar_render(void *owner,
                      e9ui_context_t *ctx,
                      e9ui_rect_t bounds,
                      int viewW,
                      int viewH,
                      int contentW,
                      int contentH,
                      int scrollX,
                      int scrollY)
{
    (void)owner;
    if (!ctx || !ctx->renderer || bounds.w <= 0 || bounds.h <= 0) {
        return;
    }

    e9ui_scrollbar_bar_rects_t vBar;
    e9ui_scrollbar_bar_rects_t hBar;
    e9ui_scrollbar_computeBarRects(ctx, bounds, 1, contentW, contentH, viewW, viewH, scrollX, scrollY, &vBar);
    e9ui_scrollbar_computeBarRects(ctx, bounds, 0, contentW, contentH, viewW, viewH, scrollX, scrollY, &hBar);
    if (!vBar.visible && !hBar.visible) {
        return;
    }

    SDL_BlendMode prevBlend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(ctx->renderer, &prevBlend);
    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
    if (vBar.visible) {
        e9ui_scrollbar_renderBar(ctx, vBar.trackRect, vBar.thumbRect);
    }
    if (hBar.visible) {
        e9ui_scrollbar_renderBar(ctx, hBar.trackRect, hBar.thumbRect);
    }
    SDL_SetRenderDrawBlendMode(ctx->renderer, prevBlend);
}

int
e9ui_scrollbar_handleEvent(void *owner,
                           e9ui_context_t *ctx,
                           const e9ui_event_t *ev,
                           e9ui_rect_t bounds,
                           int viewW,
                           int viewH,
                           int contentW,
                           int contentH,
                           int *scrollX,
                           int *scrollY,
                           e9ui_scrollbar_state_t *state)
{
    if (!ctx || !ev || !state) {
        return 0;
    }
    int posX = scrollX ? *scrollX : 0;
    int posY = scrollY ? *scrollY : 0;
    e9ui_scrollbar_clamp(viewW, viewH, contentW, contentH, &posX, &posY);

    if (ev->type == SDL_MOUSEMOTION) {
        e9ui_scrollbar_bar_rects_t vBar;
        e9ui_scrollbar_bar_rects_t hBar;
        int hoverAxis = 0;
        e9ui_scrollbar_computeBarRects(ctx, bounds, 1, contentW, contentH, viewW, viewH, posX, posY, &vBar);
        e9ui_scrollbar_computeBarRects(ctx, bounds, 0, contentW, contentH, viewW, viewH, posX, posY, &hBar);
        if (state->dragAxis == 2 && vBar.visible) {
            hoverAxis = 2;
        } else if (state->dragAxis == 1 && hBar.visible) {
            hoverAxis = 1;
        } else if (vBar.visible && e9ui_scrollbar_pointInRect(vBar.thumbRect, ev->motion.x, ev->motion.y)) {
            hoverAxis = 2;
        } else if (hBar.visible && e9ui_scrollbar_pointInRect(hBar.thumbRect, ev->motion.x, ev->motion.y)) {
            hoverAxis = 1;
        }
        if (hoverAxis != 0) {
            e9ui_scrollbar_setCursorForAxis(ctx, owner, hoverAxis, state->dragAxis != 0);
            state->hoverAxis = hoverAxis;
        } else if (state->hoverAxis != 0) {
            state->hoverAxis = 0;
            if (!state->dragAxis) {
                e9ui_cursorRequest(ctx, owner, e9ui_scrollbar_cursorForAxis(0));
            }
        }
    }

    if (ev->type == SDL_MOUSEBUTTONUP && ev->button.button == SDL_BUTTON_LEFT) {
        if (state->dragAxis != 0) {
            state->dragAxis = 0;
            state->dragThumbOffset = 0;
            e9ui_cursorRelease(ctx, owner);
            if (scrollX) {
                *scrollX = posX;
            }
            if (scrollY) {
                *scrollY = posY;
            }
            return 1;
        }
    }

    if (ev->type == SDL_MOUSEMOTION && state->dragAxis != 0) {
        e9ui_scrollbar_bar_rects_t vBar;
        e9ui_scrollbar_bar_rects_t hBar;
        e9ui_scrollbar_computeBarRects(ctx, bounds, 1, contentW, contentH, viewW, viewH, posX, posY, &vBar);
        e9ui_scrollbar_computeBarRects(ctx, bounds, 0, contentW, contentH, viewW, viewH, posX, posY, &hBar);
        if (state->dragAxis == 2 && vBar.visible && scrollY) {
            posY = e9ui_scrollbar_scrollFromBarPointer(&vBar, 1, ev->motion.y, state->dragThumbOffset);
            e9ui_scrollbar_clamp(viewW, viewH, contentW, contentH, &posX, &posY);
            *scrollY = posY;
            if (scrollX) {
                *scrollX = posX;
            }
            return 1;
        }
        if (state->dragAxis == 1 && hBar.visible && scrollX) {
            posX = e9ui_scrollbar_scrollFromBarPointer(&hBar, 0, ev->motion.x, state->dragThumbOffset);
            e9ui_scrollbar_clamp(viewW, viewH, contentW, contentH, &posX, &posY);
            *scrollX = posX;
            if (scrollY) {
                *scrollY = posY;
            }
            return 1;
        }
        state->dragAxis = 0;
        state->dragThumbOffset = 0;
        e9ui_cursorRelease(ctx, owner);
    }

    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_LEFT) {
        e9ui_scrollbar_bar_rects_t vBar;
        e9ui_scrollbar_bar_rects_t hBar;
        e9ui_scrollbar_computeBarRects(ctx, bounds, 1, contentW, contentH, viewW, viewH, posX, posY, &vBar);
        e9ui_scrollbar_computeBarRects(ctx, bounds, 0, contentW, contentH, viewW, viewH, posX, posY, &hBar);
        if (vBar.visible && scrollY && e9ui_scrollbar_pointInRect(vBar.trackRect, ev->button.x, ev->button.y)) {
            int thumbGrab = ev->button.y - vBar.thumbRect.y;
            if (!e9ui_scrollbar_pointInRect(vBar.thumbRect, ev->button.x, ev->button.y)) {
                thumbGrab = vBar.thumbRect.h / 2;
            }
            if (thumbGrab < 0) {
                thumbGrab = 0;
            }
            if (thumbGrab >= vBar.thumbRect.h) {
                thumbGrab = vBar.thumbRect.h > 0 ? vBar.thumbRect.h - 1 : 0;
            }
            state->dragAxis = 2;
            state->dragThumbOffset = thumbGrab;
            state->hoverAxis = 2;
            e9ui_scrollbar_setCursorForAxis(ctx, owner, 2, 1);
            posY = e9ui_scrollbar_scrollFromBarPointer(&vBar, 1, ev->button.y, thumbGrab);
            e9ui_scrollbar_clamp(viewW, viewH, contentW, contentH, &posX, &posY);
            *scrollY = posY;
            if (scrollX) {
                *scrollX = posX;
            }
            return 1;
        }
        if (hBar.visible && scrollX && e9ui_scrollbar_pointInRect(hBar.trackRect, ev->button.x, ev->button.y)) {
            int thumbGrab = ev->button.x - hBar.thumbRect.x;
            if (!e9ui_scrollbar_pointInRect(hBar.thumbRect, ev->button.x, ev->button.y)) {
                thumbGrab = hBar.thumbRect.w / 2;
            }
            if (thumbGrab < 0) {
                thumbGrab = 0;
            }
            if (thumbGrab >= hBar.thumbRect.w) {
                thumbGrab = hBar.thumbRect.w > 0 ? hBar.thumbRect.w - 1 : 0;
            }
            state->dragAxis = 1;
            state->dragThumbOffset = thumbGrab;
            state->hoverAxis = 1;
            e9ui_scrollbar_setCursorForAxis(ctx, owner, 1, 1);
            posX = e9ui_scrollbar_scrollFromBarPointer(&hBar, 0, ev->button.x, thumbGrab);
            e9ui_scrollbar_clamp(viewW, viewH, contentW, contentH, &posX, &posY);
            *scrollX = posX;
            if (scrollY) {
                *scrollY = posY;
            }
            return 1;
        }
    }

    if (scrollX) {
        *scrollX = posX;
    }
    if (scrollY) {
        *scrollY = posY;
    }
    return 0;
}
