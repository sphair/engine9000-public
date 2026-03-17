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

typedef enum e9ui_step_buttons_action {
    e9ui_step_buttons_action_none = 0,
    e9ui_step_buttons_action_page_up = 1,
    e9ui_step_buttons_action_line_up = 2,
    e9ui_step_buttons_action_line_down = 3,
    e9ui_step_buttons_action_page_down = 4
} e9ui_step_buttons_action_t;

typedef struct e9ui_step_buttons_state {
    int holdAction;
    uint32_t repeatTick;
    uint32_t uiTick;
} e9ui_step_buttons_state_t;

typedef int (*e9ui_step_buttons_action_fn)(void *user, e9ui_step_buttons_action_t action);

void
e9ui_step_buttons_clearHold(e9ui_step_buttons_state_t *state);

void
e9ui_step_buttons_tick(e9ui_context_t *ctx,
                       e9ui_rect_t bounds,
                       int topInsetPx,
                       int enabled,
                       e9ui_step_buttons_state_t *state,
                       void *user,
                       e9ui_step_buttons_action_fn onAction);

void
e9ui_step_buttons_render(e9ui_context_t *ctx,
                         e9ui_rect_t bounds,
                         int topInsetPx,
                         int enabled,
                         e9ui_step_buttons_state_t *state);

int
e9ui_step_buttons_handleEvent(e9ui_context_t *ctx,
                              const e9ui_event_t *ev,
                              e9ui_rect_t bounds,
                              int topInsetPx,
                              int enabled,
                              e9ui_step_buttons_state_t *state,
                              void *user,
                              e9ui_step_buttons_action_fn onAction);
