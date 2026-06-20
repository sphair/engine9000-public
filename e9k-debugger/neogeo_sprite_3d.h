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
#include "e9ui.h"

typedef enum neogeo_sprite_3d_view_mode {
    neogeo_sprite_3d_view_mode_normal = 0,
    neogeo_sprite_3d_view_mode_shrink = 1,
    neogeo_sprite_3d_view_mode_palette = 2,
    neogeo_sprite_3d_view_mode_chain = 3,
    neogeo_sprite_3d_view_mode_count = 4
} neogeo_sprite_3d_view_mode_t;

typedef struct neogeo_sprite_3d_source {
    const e9k_debug_sprite_state_t *lastState;
    int hasLastState;
    neogeo_sprite_3d_view_mode_t viewMode;
    int selectedSpriteIndex;
    int selectedChainRootIndex;
    int highlightSelectionChain;
} neogeo_sprite_3d_source_t;

void
neogeo_sprite_3d_setSource(const neogeo_sprite_3d_source_t *source);

void
neogeo_sprite_3d_toggle(e9ui_context_t *ctx, void *user);

void
neogeo_sprite_3d_close(void);

void
neogeo_sprite_3d_persistConfig(FILE *file);

int
neogeo_sprite_3d_loadConfigProperty(const char *prop, const char *value);
