#pragma once

#include <stddef.h>
#include <stdint.h>
#include "e9k-lib.h"

void
e9k_checkpoint_reset(void);

void
e9k_checkpoint_resetHard(void);

void
e9k_checkpoint_setEnabled(int enabled);

int
e9k_checkpoint_isEnabled(void);

void
e9k_checkpoint_stateSave(uint8_t *st);

void
e9k_checkpoint_stateLoad(uint8_t *st);

void
e9k_checkpoint_write(uint8_t index, uint32_t scanline);

void
e9k_checkpoint_setName(uint8_t index, const char *name);

void
e9k_checkpoint_tick(uint64_t ticks);

size_t
e9k_checkpoint_read(e9k_debug_checkpoint_t *out, size_t cap);
