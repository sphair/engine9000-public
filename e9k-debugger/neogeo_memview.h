/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stdio.h>

void
neogeo_memview_toggle(void);

int
neogeo_memview_isOpen(void);

void
neogeo_memview_setMainWindowFocused(int focused);

void
neogeo_memview_render(void);

void
neogeo_memview_persistConfig(FILE *file);

int
neogeo_memview_loadConfigProperty(const char *prop, const char *value);
