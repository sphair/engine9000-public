/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stdio.h>

#include "e9k-lib.h"

int
amiga_blit_info_init(void);

void
amiga_blit_info_shutdown(void);

void
amiga_blit_info_toggle(void);

int
amiga_blit_info_isOpen(void);

void
amiga_blit_info_show(const e9k_debug_ami_blitter_vis_point_t *info);

void
amiga_blit_info_showHits(const e9k_debug_ami_blitter_vis_point_t *hits, size_t hitCount, size_t selectedIndex);

void
amiga_blit_info_setMainWindowFocused(int focused);

void
amiga_blit_info_render(void);

void
amiga_blit_info_persistConfig(FILE *file);

int
amiga_blit_info_loadConfigProperty(const char *prop, const char *value);
