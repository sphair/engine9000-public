/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "debugger.h"

static inline int
platform_scanFolder(const char *folder, int (*cb)(const char *path, void *user), void *user)
{
    return debugger_platform_scanFolder(folder, cb, user);
}

static inline int
platform_caseInsensitivePaths(void)
{
    return debugger_platform_caseInsensitivePaths();
}

static inline char
platform_preferredPathSeparator(void)
{
    return debugger_platform_preferredPathSeparator();
}

static inline int
platform_getHomeDir(char *out, size_t cap)
{
    return debugger_platform_getHomeDir(out, cap);
}

static inline int
platform_getCurrentDir(char *out, size_t cap)
{
    return debugger_platform_getCurrentDir(out, cap);
}

static inline const char *
platform_selectFolderDialog(const char *title, const char *defaultPath)
{
    return debugger_platform_selectFolderDialog(title, defaultPath);
}

static inline const char *
platform_openFileDialog(const char *title,
                        const char *defaultPathAndFile,
                        int numOfFilterPatterns,
                        const char * const *filterPatterns,
                        const char *singleFilterDescription,
                        int allowMultipleSelects)
{
    return debugger_platform_openFileDialog(title,
                                            defaultPathAndFile,
                                            numOfFilterPatterns,
                                            filterPatterns,
                                            singleFilterDescription,
                                            allowMultipleSelects);
}

static inline const char *
platform_saveFileDialog(const char *title,
                        const char *defaultPathAndFile,
                        int numOfFilterPatterns,
                        const char * const *filterPatterns,
                        const char *singleFilterDescription)
{
    return debugger_platform_saveFileDialog(title,
                                            defaultPathAndFile,
                                            numOfFilterPatterns,
                                            filterPatterns,
                                            singleFilterDescription);
}
