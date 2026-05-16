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

#include "e9k-mega.h"

void
mega_audio_vis_toggle(void);

int
mega_audio_vis_isOpen(void);

void
mega_audio_vis_render(const e9k_debug_mega_audio_frame_t *frame);

void
mega_audio_vis_persistConfig(FILE *file);

int
mega_audio_vis_loadConfigProperty(const char *prop, const char *value);
