#pragma once

#include <stddef.h>
#include <stdint.h>
#include "e9k-geo.h"

size_t
e9k_debug_neogeo_get_p1_rom(e9k_debug_rom_region_t *out, size_t cap);

size_t
e9k_debug_neogeo_get_c_rom(e9k_debug_rom_region_t *out, size_t cap);

size_t
e9k_debug_neogeo_get_fix_rom(e9k_debug_rom_region_t *out, size_t cap);

size_t
e9k_debug_neogeo_get_palette_state(e9k_debug_palette_state_t *out, size_t cap);

size_t
e9k_debug_disassemble_quick(uint32_t pc, char *out, size_t cap);
