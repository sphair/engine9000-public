/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stdint.h>

#include "source_pane_internal.h"

int
source_z80_isModeAvailable(void);

uint32_t
source_z80_processorId(void);

uint64_t
source_z80_resolveAnchorAddr(uint64_t addr);

uint64_t
source_z80_getCurrentAddr(source_pane_state_t *st);

int
source_z80_getWindow(source_pane_state_t *st, int maxLines, uint64_t *outCurAddr,
                     const char ***outLines, const uint64_t **outAddrs, int *outCount);
