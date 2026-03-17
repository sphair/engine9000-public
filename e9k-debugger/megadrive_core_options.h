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
megadrive_coreOptionsDirty(void);

void
megadrive_coreOptionsClear(void);

const char *
megadrive_coreOptionsGetValue(const char *key);

void
megadrive_coreOptionsSetValue(const char *key, const char *value);

int
megadrive_coreOptionsBuildPath(char *out, size_t cap, const char *saveDir, const char *romPath);

int
megadrive_coreOptionsLoadFromFile(const char *saveDir, const char *romPath);

int
megadrive_coreOptionsWriteToFile(const char *saveDir, const char *romPath);

int
megadrive_coreOptionsApplyFileToHost(const char *saveDir, const char *romPath);
