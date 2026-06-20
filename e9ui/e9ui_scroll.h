/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stdio.h>

#include "e9ui.h"

e9ui_component_t *
e9ui_scroll_make(e9ui_component_t *child);

void
e9ui_scroll_setContentHeightPx(e9ui_component_t *scroll, int contentHeight_px);

void
e9ui_scroll_setContentWidthPx(e9ui_component_t *scroll, int contentWidth_px);

void
e9ui_scroll_getScrollPx(e9ui_component_t *scroll, int *outScrollX, int *outScrollY);

void
e9ui_scroll_setScrollPx(e9ui_component_t *scroll, int scrollX, int scrollY);

void
e9ui_scroll_setPersistKey(e9ui_component_t *scroll, const char *persistKey);

void
e9ui_scroll_loadPersistedPx(e9ui_component_t *scroll, int scrollX, int scrollY);

void
e9ui_scroll_persistConfig(FILE *file, e9ui_component_t *scroll);

int
e9ui_scroll_pointInContentPx(e9ui_component_t *scroll,
                             e9ui_context_t *ctx,
                             int contentW,
                             int contentH,
                             int mouseX,
                             int mouseY);
