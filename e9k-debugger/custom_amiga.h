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
#include <stdint.h>

int
custom_amiga_init(void);

void
custom_amiga_shutdown(void);

void
custom_amiga_toggle(void);

int
custom_amiga_isOpen(void);

uint32_t
custom_amiga_getWindowId(void);

void
custom_amiga_handleEvent(SDL_Event *ev);

void
custom_amiga_setMainWindowFocused(int focused);

void
custom_amiga_render(void);

void
custom_amiga_persistConfig(FILE *file);

int
custom_amiga_loadConfigProperty(const char *prop, const char *value);
