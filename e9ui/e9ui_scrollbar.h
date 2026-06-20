/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9ui_context.h"
#include "e9ui_types.h"

typedef struct e9ui_scrollbar_state {
    int dragAxis;
    int dragThumbOffset;
    int hoverAxis;
} e9ui_scrollbar_state_t;

int
e9ui_scrollbar_maxScroll(int contentSize, int viewSize);

void
e9ui_scrollbar_clamp(int viewW, int viewH, int contentW, int contentH, int *scrollX, int *scrollY);

int
e9ui_scrollbar_pointInScrollbarPx(e9ui_context_t *ctx,
                                  e9ui_rect_t bounds,
                                  int viewW,
                                  int viewH,
                                  int contentW,
                                  int contentH,
                                  int mouseX,
                                  int mouseY);

void
e9ui_scrollbar_render(void *owner,
                      e9ui_context_t *ctx,
                      e9ui_rect_t bounds,
                      int viewW,
                      int viewH,
                      int contentW,
                      int contentH,
                      int scrollX,
                      int scrollY);

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
                           e9ui_scrollbar_state_t *state);
