/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <SDL.h>
#include <stdio.h>

void
mega_palette_debug_toggle(void);

int
mega_palette_debug_isOpen(void);

int
mega_palette_debug_handleKeydown(const SDL_KeyboardEvent *kev);

void
mega_palette_debug_setMainWindowFocused(int focused);

void
mega_palette_debug_render(void);

void
mega_palette_debug_persistConfig(FILE *file);

int
mega_palette_debug_loadConfigProperty(const char *prop, const char *value);
