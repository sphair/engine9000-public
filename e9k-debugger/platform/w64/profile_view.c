/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <stdio.h>
#include <windows.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "profile_view.h"
#include "debug.h"
#include "debugger.h"
#include "file.h"
#include "strutil.h"

#define PROFILE_VIEWER_PYTHON_ENV "E9K_PROFILE_VIEWER_PYTHON"
#define PROFILE_VIEWER_SCRIPT_ENV "E9K_PROFILE_VIEWER_SCRIPT"
#define PROFILE_VIEWER_DEFAULT_SCRIPT "tools/profileui/build_viewer.py"

static int profile_viewer_try_env_path(const char *env, char *out, size_t cap);
static int profile_viewer_is_executable(const char *path);
static int profile_viewer_python_works(const char *python);
static int profile_viewer_make_temp_dir(char *out, size_t cap);
static int profile_viewer_index_path(const char *out_dir, char *out, size_t cap);
static int profile_viewer_file_exists(const char *path);
static int profile_viewer_append(char *out, size_t cap, size_t *used, const char *text);
static int profile_viewer_append_quoted(char *out, size_t cap, size_t *used, const char *text);
static void profile_viewer_python_path_arg(char *out, size_t cap, const char *path);

static int
profile_viewer_resolve_python(const char *env, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    if (profile_viewer_try_env_path(env, out, cap)) {
        if (!profile_viewer_is_executable(out)) {
            debug_error("profile: python env path %s not executable; falling back to PATH", out);
        } else if (!profile_viewer_python_works(out)) {
            debug_error("profile: python env path %s did not execute; falling back to PATH", out);
        } else {
            return 1;
        }
    }
    const char *candidates[] = {
        "python.exe",
        "python3.exe",
        "py.exe",
        "python",
        "python3",
        NULL
    };
    for (int i = 0; candidates[i]; ++i) {
        if (!file_findInPath(candidates[i], out, cap)) {
            continue;
        }
        if (profile_viewer_python_works(out)) {
            return 1;
        }
        debug_error("profile: python candidate %s did not execute: %s", candidates[i], out);
    }
    return 0;
}

static int
profile_viewer_resolve_script(const char *env, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    if (profile_viewer_try_env_path(env, out, cap)) {
        if (profile_viewer_file_exists(out)) {
            return 1;
        }
        debug_error("profile: viewer script env path %s invalid; falling back to assets", out);
    }
    return file_getAssetPath(PROFILE_VIEWER_DEFAULT_SCRIPT, out, cap);
}

static int
profile_viewer_try_env_path(const char *env, char *out, size_t cap)
{
    if (!env || !*env || !out || cap == 0) {
        return 0;
    }
    size_t len = strlen(env);
    if (len >= cap) {
        return 0;
    }
    memcpy(out, env, len + 1);
    return 1;
}

static int
profile_viewer_is_executable(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return 0;
    }
    return (attr & FILE_ATTRIBUTE_DIRECTORY) ? 0 : 1;
}

static int
profile_viewer_python_works(const char *python)
{
    if (!python || !*python) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    size_t used = 0;
    if (!profile_viewer_append_quoted(cmd, sizeof(cmd), &used, python) ||
        !profile_viewer_append(cmd, sizeof(cmd), &used, " -c ") ||
        !profile_viewer_append_quoted(cmd, sizeof(cmd), &used, "import sys")) {
        return 0;
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    BOOL created = CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    if (!created) {
        return 0;
    }

    DWORD waitResult = WaitForSingleObject(pi.hProcess, 5000);
    DWORD exitCode = 1;
    if (waitResult == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi.hProcess, &exitCode);
    } else {
        TerminateProcess(pi.hProcess, 1);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return waitResult == WAIT_OBJECT_0 && exitCode == 0;
}

static int
profile_viewer_make_temp_dir(char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    char base[MAX_PATH];
    DWORD base_len = GetTempPathA((DWORD)sizeof(base), base);
    if (base_len == 0 || base_len >= (DWORD)sizeof(base)) {
        return 0;
    }
    char temp_file[MAX_PATH];
    if (!GetTempFileNameA(base, "e9k", 0, temp_file)) {
        return 0;
    }
    DeleteFileA(temp_file);
    if (!CreateDirectoryA(temp_file, NULL)) {
        return 0;
    }
    size_t len = strlen(temp_file);
    if (len >= cap) len = cap - 1;
    memcpy(out, temp_file, len);
    out[len] = '\0';
    return 1;
}

static int
profile_viewer_index_path(const char *out_dir, char *out, size_t cap)
{
    if (!out_dir || !*out_dir || !out || cap == 0) {
        return 0;
    }
    strutil_pathJoinTrunc(out, cap, out_dir, "index.html");
    return out[0] ? 1 : 0;
}

static int
profile_viewer_file_exists(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return 0;
    }
    return (attr & FILE_ATTRIBUTE_DIRECTORY) ? 0 : 1;
}

static int
profile_viewer_append(char *out, size_t cap, size_t *used, const char *text)
{
    if (!out || cap == 0 || !used || !text) {
        return 0;
    }
    size_t pos = *used;
    if (pos >= cap) {
        return 0;
    }
    while (*text) {
        if (pos + 1 >= cap) {
            out[cap - 1] = '\0';
            return 0;
        }
        out[pos++] = *text++;
    }
    out[pos] = '\0';
    *used = pos;
    return 1;
}

static int
profile_viewer_append_quoted(char *out, size_t cap, size_t *used, const char *text)
{
    if (!profile_viewer_append(out, cap, used, "\"")) {
        return 0;
    }
    size_t slashCount = 0;
    while (text && *text) {
        if (*text == '\\') {
            ++slashCount;
            ++text;
            continue;
        }
        if (*text == '"') {
            for (size_t i = 0; i < slashCount * 2 + 1; ++i) {
                if (!profile_viewer_append(out, cap, used, "\\")) {
                    return 0;
                }
            }
            slashCount = 0;
            if (!profile_viewer_append(out, cap, used, "\"")) {
                return 0;
            }
            ++text;
            continue;
        }
        for (size_t i = 0; i < slashCount; ++i) {
            if (!profile_viewer_append(out, cap, used, "\\")) {
                return 0;
            }
        }
        slashCount = 0;
        char ch[2] = { *text, '\0' };
        if (!profile_viewer_append(out, cap, used, ch)) {
            return 0;
        }
        ++text;
    }
    for (size_t i = 0; i < slashCount * 2; ++i) {
        if (!profile_viewer_append(out, cap, used, "\\")) {
            return 0;
        }
    }
    return profile_viewer_append(out, cap, used, "\"");
}

static void
profile_viewer_python_path_arg(char *out, size_t cap, const char *path)
{
    strutil_strlcpy(out, cap, path);
    for (char *p = out; p && *p; ++p) {
        if (*p == '\\') {
            *p = '/';
        }
    }
}

static int
profile_viewer_build_cmd(char *out, size_t cap, const char *python, const char *script,
                         const char *json_path, const char *out_dir)
{
    if (!out || cap == 0) {
        return 0;
    }
    char scriptArg[PATH_MAX];
    char jsonArg[PATH_MAX];
    char outArg[PATH_MAX];
    char elfArg[PATH_MAX];
    char srcArg[PATH_MAX];
    char toolchainArg[PATH_MAX];
    profile_viewer_python_path_arg(scriptArg, sizeof(scriptArg), script);
    profile_viewer_python_path_arg(jsonArg, sizeof(jsonArg), json_path);
    profile_viewer_python_path_arg(outArg, sizeof(outArg), out_dir);
    profile_viewer_python_path_arg(elfArg, sizeof(elfArg), debugger.libretro.exePath);
    profile_viewer_python_path_arg(srcArg, sizeof(srcArg), debugger.libretro.sourceDir);
    profile_viewer_python_path_arg(toolchainArg, sizeof(toolchainArg), debugger.libretro.toolchainPrefix);

    size_t used = 0;
    out[0] = '\0';
    if (!profile_viewer_append_quoted(out, cap, &used, python) ||
        !profile_viewer_append(out, cap, &used, " ") ||
        !profile_viewer_append_quoted(out, cap, &used, scriptArg) ||
        !profile_viewer_append(out, cap, &used, " --input ") ||
        !profile_viewer_append_quoted(out, cap, &used, jsonArg) ||
        !profile_viewer_append(out, cap, &used, " --out ") ||
        !profile_viewer_append_quoted(out, cap, &used, outArg)) {
        return 0;
    }
    if (debugger.libretro.toolchainPrefix[0]) {
        if (!profile_viewer_append(out, cap, &used, " --toolchain-prefix ") ||
            !profile_viewer_append_quoted(out, cap, &used, toolchainArg)) {
            return 0;
        }
    }
    if (debugger.libretro.exePath[0]) {
        if (!profile_viewer_append(out, cap, &used, " --elf ") ||
            !profile_viewer_append_quoted(out, cap, &used, elfArg)) {
            return 0;
        }
    }
    if (debugger.libretro.sourceDir[0]) {
        if (!profile_viewer_append(out, cap, &used, " --src-base ") ||
            !profile_viewer_append_quoted(out, cap, &used, srcArg)) {
            return 0;
        }
    }
    if (debugger.machine.textBaseAddr) {
        char addr[32];
        int n = snprintf(addr, sizeof(addr), "0x%08X", (unsigned)debugger.machine.textBaseAddr);
        if (n <= 0 || (size_t)n >= sizeof(addr) ||
            !profile_viewer_append(out, cap, &used, " --text-base ") ||
            !profile_viewer_append(out, cap, &used, addr)) {
            return 0;
        }
    }
    if (debugger.machine.dataBaseAddr) {
        char addr[32];
        int n = snprintf(addr, sizeof(addr), "0x%08X", (unsigned)debugger.machine.dataBaseAddr);
        if (n <= 0 || (size_t)n >= sizeof(addr) ||
            !profile_viewer_append(out, cap, &used, " --data-base ") ||
            !profile_viewer_append(out, cap, &used, addr)) {
            return 0;
        }
    }
    if (debugger.machine.bssBaseAddr) {
        char addr[32];
        int n = snprintf(addr, sizeof(addr), "0x%08X", (unsigned)debugger.machine.bssBaseAddr);
        if (n <= 0 || (size_t)n >= sizeof(addr) ||
            !profile_viewer_append(out, cap, &used, " --bss-base ") ||
            !profile_viewer_append(out, cap, &used, addr)) {
            return 0;
        }
    }
    return 1;
}

int
profile_viewer_run(const char *json_path)
{
    if (!json_path || !*json_path) {
        return 0;
    }
    char pythonPath[PATH_MAX];
    if (!profile_viewer_resolve_python(getenv(PROFILE_VIEWER_PYTHON_ENV), pythonPath, sizeof(pythonPath))) {
        debug_error("profile: unable to locate a working Windows python interpreter");
        return 0;
    }
    char scriptPath[PATH_MAX];
    if (!profile_viewer_resolve_script(getenv(PROFILE_VIEWER_SCRIPT_ENV), scriptPath, sizeof(scriptPath))) {
        debug_error("profile: unable to locate viewer script (%s)", PROFILE_VIEWER_DEFAULT_SCRIPT);
        return 0;
    }
    char outDir[PATH_MAX];
    if (!profile_viewer_make_temp_dir(outDir, sizeof(outDir))) {
        debug_error("profile: unable to create viewer temp dir");
        return 0;
    }
    char indexPath[PATH_MAX];
    if (!profile_viewer_index_path(outDir, indexPath, sizeof(indexPath))) {
        debug_error("profile: unable to build viewer index path");
        return 0;
    }
    char cmd[PATH_MAX * 4];
    if (!profile_viewer_build_cmd(cmd, sizeof(cmd), pythonPath, scriptPath, json_path, outDir)) {
        debug_error("profile: unable to build viewer command line");
        return 0;
    }
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        debug_error("profile: unable to launch viewer process");
        return 0;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (exitCode != 0) {
        debug_error("profile: viewer process failed (exit=%lu)", (unsigned long)exitCode);
        return 0;
    }
    if (!profile_viewer_file_exists(indexPath)) {
        DWORD err = GetLastError();
        debug_error("profile: viewer process exited successfully but did not create %s (GetLastError=%lu)",
                    indexPath,
                    (unsigned long)err);
        return 0;
    }
    debug_printf("Profile viewer generated at %s\n", indexPath);
    return 1;
}
