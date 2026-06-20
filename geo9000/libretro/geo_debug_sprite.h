#pragma once

#include <stddef.h>
#include <stdint.h>
#include "e9k-geo.h"
#ifdef __cplusplus
extern "C" {
#endif
  
size_t
e9k_debug_neogeo_get_sprite_state(e9k_debug_sprite_state_t *out, size_t cap);

void
e9k_debug_neogeo_set_sprite_grayscale_selection(const e9k_debug_sprite_grayscale_selection_t *selection);

void
e9k_debug_neogeo_set_palette_grayscale_mask(const e9k_debug_palette_grayscale_mask_t *mask);

size_t
e9k_debug_neogeo_get_palette_grayscale_mask(e9k_debug_palette_grayscale_mask_t *out, size_t cap);

#ifdef __cplusplus
}
#endif
