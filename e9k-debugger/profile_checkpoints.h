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
profile_checkpoints_makeComponent(void);

void
profile_checkpoints_toggle(void);

void
profile_checkpoints_reset(void);

void
profile_checkpoints_dump(void);

void
profile_checkpoints_refreshHotkeyTooltips(void);

void
profile_checkpoints_persistConfig(FILE *file);

int
profile_checkpoints_loadConfigProperty(const char *prop, const char *value);

void
profile_checkpoints_renderScanlineOverlay(e9ui_context_t *ctx,
                                          const SDL_Rect *dst,
                                          const SDL_Rect *clipRect,
                                          uint64_t scanlineCount,
                                          uint64_t videoStartScanline,
                                          uint64_t videoScanlineCount);
