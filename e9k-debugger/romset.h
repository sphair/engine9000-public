/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stddef.h>

int
romset_buildNeoFromFolder(const char *folder, char *outPath, size_t capacity);

int
romset_buildNeoFromZip(const char *zipPath, char *outPath, size_t capacity);

int
romset_buildNeoOutputPath(const char *inputPath,
                          int isZip,
                          const char *saveDir,
                          const char *systemDir,
                          char *outPath,
                          size_t capacity);
