/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#include "machine.h"

int
source_pane_fileline_resolveAddress(const char *elf, uint32_t addr, char *outFile, size_t fileCap, int *outLine);

int
source_pane_fileline_resolveFileLine(const char *elf, const char *file, int line_no, uint32_t *out_addr);

machine_breakpoint_t *
source_pane_fileline_findBreakpointForLine(const char *path, int line,
                                           const machine_breakpoint_t *bps, int count);

int
source_pane_fileline_removeBreakpointsForLine(const char *path, int line,
                                              const machine_breakpoint_t *bps, int count);

int
source_pane_fileline_addBreakpointsForLine(const char *path, int lineNo);
