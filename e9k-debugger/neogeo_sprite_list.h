/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stdio.h>

#include "e9k-geo.h"

void
neogeo_sprite_list_toggle(void);

int
neogeo_sprite_list_isOpen(void);

int
neogeo_sprite_list_getSelectedSprite(void);

void
neogeo_sprite_list_selectSprite(int spriteIndex);

void
neogeo_sprite_list_selectSpriteSingleCheckbox(int spriteIndex);

void
neogeo_sprite_list_render(const e9k_debug_sprite_state_t *st);

void
neogeo_sprite_list_persistConfig(FILE *file);

int
neogeo_sprite_list_loadConfigProperty(const char *prop, const char *value);
