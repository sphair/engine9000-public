/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stdio.h>

int
amiga_memview_init(void);

void
amiga_memview_shutdown(void);

void
amiga_memview_toggle(void);

int
amiga_memview_isOpen(void);

void
amiga_memview_setMainWindowFocused(int focused);

void
amiga_memview_render(void);

void
amiga_memview_persistConfig(FILE *file);

int
amiga_memview_loadConfigProperty(const char *prop, const char *value);
