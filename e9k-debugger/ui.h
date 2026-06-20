/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stdint.h>

#include "e9ui.h"

void
ui_build(void);

void
ui_configureE9uiHost(void);

void
ui_updateSourceTitle(void);

void
ui_updateWindowTitle(void);

void
ui_refreshOnPause(void);

void
ui_centerSourceOnAddress(uint32_t addr);

void
ui_openSourceLocation(const char *file, int line, uint32_t fallbackAddr);

void
ui_centerCprSourceOnAddress(uint32_t addr);

void
ui_applySourcePaneElfMode(void);

void
ui_copyFramebufferToClipboard(void);

void
ui_refreshSpeedButton(void);

void
ui_refreshRecordButton(void);

void
ui_refreshHotkeyTooltips(void);

void
ui_toggleRollingSavePauseResume(void);

int
ui_runFullscreenTransition(e9ui_context_t *ctx,
                           int entering,
                           e9ui_component_t *from,
                           e9ui_component_t *to,
                           int width,
                           int height);

void
ui_setMainWindowFocused(e9ui_context_t *ctx, int focused);

int
ui_handleGlobalKeydown(e9ui_context_t *ctx, const SDL_KeyboardEvent *kev);

void
ui_shutdownHostUi(e9ui_context_t *ctx);

void
ui_prepareMainWindow(e9ui_context_t *ctx,
                     int cliOverrideWindowSize,
                     int startHidden,
                     int *wantX,
                     int *wantY,
                     int *wantW,
                     int *wantH,
                     Uint32 *winFlags);

int
ui_shouldUseVsync(e9ui_context_t *ctx);

void
ui_finalizeMainWindow(e9ui_context_t *ctx,
                      SDL_Window *window,
                      SDL_Renderer *renderer,
                      int wantW,
                      int wantH);
