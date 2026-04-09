/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

int
hunk_fileline_cache_resolveFileLine(const char *elfPath, const char *filePath, int lineNo, uint32_t *outAddr);

int
hunk_fileline_cache_resolveFileLineAll(const char *elfPath, const char *filePath, int lineNo,
                                       uint32_t **outAddrs, int *outCount);

void
hunk_fileline_cache_clear(void);
