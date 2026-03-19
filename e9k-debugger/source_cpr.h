/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "source_pane_internal.h"

int
source_cpr_isModeAvailable(void);

uint64_t
source_cpr_resolveAnchorAddr(uint64_t addr);

uint64_t
source_cpr_getCurrentAddr(source_pane_state_t *st);

int
source_cpr_getWindow(source_pane_state_t *st, int maxLines, uint64_t *outCurAddr,
                     const char ***outLines, const uint64_t **outAddrs, int *outCount);

int
source_cpr_buildRegisterOptions(source_pane_state_t *st);

int
source_cpr_commitInlineEdit(source_pane_state_t *st, e9ui_context_t *ctx, e9ui_component_t *editor,
                            const char *text);

int
source_cpr_beginInlineWordsEditAtPoint(e9ui_component_t *self, e9ui_context_t *ctx, source_pane_state_t *st,
                                       int mx, int my);

int
source_cpr_beginInlineRegisterEditAtPoint(e9ui_component_t *self, e9ui_context_t *ctx, source_pane_state_t *st,
                                          int mx, int my);

int
source_cpr_beginInlineValueEditAtPoint(e9ui_component_t *self, e9ui_context_t *ctx, source_pane_state_t *st,
                                       int mx, int my);

void
source_cpr_render(e9ui_component_t *self, e9ui_context_t *ctx);
