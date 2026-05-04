/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

bool
amiga_uaeLoadUaeOptions(const char *uaePath);

void
amiga_uaeClearPuaeOptions(void);

int
amiga_uaeUaeOptionsDirty(void);

int
amiga_uaeHasRestartRequiredDirty(void);

int
amiga_uaeHasFloppyDirty(void);

const char *
amiga_uaeGetPuaeOptionValue(const char *key);

void
amiga_uaeSetPuaeOptionValue(const char *key, const char *value);

const char *
amiga_uaeGetFloppyPath(int drive);

void
amiga_uaeSetFloppyPath(int drive, const char *path);

void
amiga_uaeClearFloppyDirty(int drive);

const char *
amiga_uaeGetHardDriveFolderPath(void);

const char *
amiga_uaeGetHardDriveFolderPathForUnit(int unit);

void
amiga_uaeSetHardDriveFolderPath(const char *path);

void
amiga_uaeSetHardDriveFolderPathForUnit(int unit, const char *path);

const char *
amiga_uaeGetHardDriveHdfPath(void);

const char *
amiga_uaeGetHardDriveHdfPathForUnit(int unit);

void
amiga_uaeSetHardDriveHdfPath(const char *path);

void
amiga_uaeSetHardDriveHdfPathForUnit(int unit, const char *path);

bool
amiga_uaeWriteUaeOptionsToFile(const char *uaePath);

bool
amiga_uaeApplyPuaeOptionsToHost(const char *uaePath);
