/*
 * Z80 disassembly adapter for the Engine 9000 debugger.
 *
 * The opcode data and decoding behaviour are derived from z80dasm 1.1.6:
 * Copyright (C) 1994-2007 Jan Panteltje
 * Copyright (C) 2007-2019 Tomaz Solc
 *
 * z80dasm is licensed under the GNU General Public License version 2 or
 * later.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

size_t geo_z80_dasmDisassemble(const uint8_t *bytes, uint32_t pc, char *out, size_t cap);
