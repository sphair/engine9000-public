/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "addr2line.h"
#include "debugger.h"
#include "base_map.h"
#include "file.h"

typedef struct {
    HANDLE processHandle;
    HANDLE threadHandle;
    HANDLE inWrite;
    HANDLE outRead;
    char elf[PATH_MAX];
    char *pending;
    char buf[4096];
    size_t bufLen;
    int expectFunc;
    int expectFile;
    int useHunkQuery;
} addr2line_t;

static addr2line_t addr2line = {
    .processHandle = INVALID_HANDLE_VALUE,
    .threadHandle = INVALID_HANDLE_VALUE,
    .inWrite = INVALID_HANDLE_VALUE,
    .outRead = INVALID_HANDLE_VALUE,
};

static char addr2line_missingTool[PATH_MAX];

static void
addr2line_clearPending(void)
{
    if (addr2line.pending) {
        free(addr2line.pending);
        addr2line.pending = NULL;
    }
}

static int
addr2line_parseHexComponent(const char *line, const char **outEnd, uint64_t *outValue)
{
    if (outEnd) {
        *outEnd = NULL;
    }
    if (outValue) {
        *outValue = 0;
    }
    if (!line || !outEnd || !outValue) {
        return 0;
    }
    const char *p = line;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }
    if (!isxdigit((unsigned char)*p)) {
        return 0;
    }
    const char *end = p;
    while (isxdigit((unsigned char)*end)) {
        ++end;
    }
    if (end <= p) {
        return 0;
    }
    size_t len = (size_t)(end - p);
    if (len >= 24u) {
        return 0;
    }
    char buf[24];
    memcpy(buf, p, len);
    buf[len] = '\0';
    errno = 0;
    unsigned long long value = strtoull(buf, NULL, 16);
    if (errno != 0) {
        return 0;
    }
    *outEnd = end;
    *outValue = (uint64_t)value;
    return 1;
}

static int
addr2line_parseAddressLine(const char *line, uint64_t *outAddr, size_t *outIndex, int *outHasIndex)
{
    if (outAddr) {
        *outAddr = 0;
    }
    if (outIndex) {
        *outIndex = 0;
    }
    if (outHasIndex) {
        *outHasIndex = 0;
    }
    if (!line) {
        return 0;
    }

    const char *p = line;
    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }

    const char *end = NULL;
    uint64_t first = 0;
    if (!addr2line_parseHexComponent(p, &end, &first)) {
        return 0;
    }
    p = end;
    if (*p == ':') {
        ++p;
        uint64_t second = 0;
        if (!addr2line_parseHexComponent(p, &end, &second)) {
            return 0;
        }
        p = end;
        while (*p && isspace((unsigned char)*p)) {
            ++p;
        }
        if (*p != '\0') {
            return 0;
        }
        if (outHasIndex) {
            *outHasIndex = 1;
        }
        if (outIndex) {
            *outIndex = (size_t)first;
        }
        if (outAddr) {
            *outAddr = second;
        }
        return 1;
    }

    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }
    if (*p != '\0') {
        return 0;
    }
    if (outAddr) {
        *outAddr = first;
    }
    return 1;
}

static int
addr2line_isAddressLine(const char *line)
{
    return addr2line_parseAddressLine(line, NULL, NULL, NULL);
}

static int
addr2line_isHunkTool(const char *toolPath)
{
    if (!toolPath || !*toolPath) {
        return 0;
    }
    const char *name = toolPath;
    const char *slash = strrchr(toolPath, '/');
    if (slash && slash[1]) {
        name = slash + 1;
    }
    const char *backslash = strrchr(name, '\\');
    if (backslash && backslash[1]) {
        name = backslash + 1;
    }
    if (strstr(name, "hunk-addr2line")) {
        return 1;
    }
    if (strstr(name, "hunk_addr2line")) {
        return 1;
    }
    return 0;
}

static void
addr2line_closeStreams(void)
{
    if (addr2line.inWrite != INVALID_HANDLE_VALUE) {
        CloseHandle(addr2line.inWrite);
        addr2line.inWrite = INVALID_HANDLE_VALUE;
    }
    if (addr2line.outRead != INVALID_HANDLE_VALUE) {
        CloseHandle(addr2line.outRead);
        addr2line.outRead = INVALID_HANDLE_VALUE;
    }
}

static void
addr2line_closeProcessHandles(void)
{
    if (addr2line.threadHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(addr2line.threadHandle);
        addr2line.threadHandle = INVALID_HANDLE_VALUE;
    }
    if (addr2line.processHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(addr2line.processHandle);
        addr2line.processHandle = INVALID_HANDLE_VALUE;
    }
}

static void
addr2line_resetState(void)
{
    addr2line.elf[0] = '\0';
    addr2line.bufLen = 0;
    addr2line.expectFunc = 0;
    addr2line.expectFile = 0;
    addr2line.useHunkQuery = 0;
}

static int
addr2line_processIsAlive(void)
{
    if (addr2line.processHandle == INVALID_HANDLE_VALUE) {
        return 0;
    }
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(addr2line.processHandle, &exitCode)) {
        return 0;
    }
    if (exitCode == STILL_ACTIVE) {
        return 1;
    }
    addr2line_closeStreams();
    addr2line_closeProcessHandles();
    addr2line_resetState();
    return 0;
}

static int
addr2line_readLine(char **out)
{
    if (!out) {
        return 0;
    }
    if (addr2line.pending) {
        *out = addr2line.pending;
        addr2line.pending = NULL;
        return 1;
    }
    if (addr2line.outRead == INVALID_HANDLE_VALUE) {
        return 0;
    }
    const DWORD waitStepMs = 1;
    const DWORD waitMaxMs = 2000;
    DWORD waitedMs = 0;
    for (;;) {
        for (size_t i = 0; i < addr2line.bufLen; ++i) {
            if (addr2line.buf[i] == '\n') {
                size_t len = i;
                if (len > 0 && addr2line.buf[len - 1] == '\r') {
                    len--;
                }
                char *line = (char*)malloc(len + 1);
                if (!line) {
                    return 0;
                }
                memcpy(line, addr2line.buf, len);
                line[len] = '\0';
                size_t remain = addr2line.bufLen - (i + 1);
                memmove(addr2line.buf, addr2line.buf + i + 1, remain);
                addr2line.bufLen = remain;
                *out = line;
                return 1;
            }
        }
        if (addr2line.bufLen >= sizeof(addr2line.buf) - 1) {
            addr2line.bufLen = 0;
        }
        DWORD bytesAvailable = 0;
        if (!PeekNamedPipe(addr2line.outRead, NULL, 0, NULL, &bytesAvailable, NULL)) {
            return 0;
        }
        if (bytesAvailable == 0) {
            if (!addr2line_processIsAlive()) {
                return 0;
            }
            if (waitedMs >= waitMaxMs) {
                return 0;
            }
            Sleep(waitStepMs);
            waitedMs += waitStepMs;
            continue;
        }
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(
            addr2line.outRead,
            addr2line.buf + addr2line.bufLen,
            (DWORD)(sizeof(addr2line.buf) - 1 - addr2line.bufLen),
            &bytesRead,
            NULL
        );
        if (!ok || bytesRead == 0) {
            return 0;
        }
        addr2line.bufLen += (size_t)bytesRead;
        waitedMs = 0;
    }
}

static int
addr2line_writeQuery(uint64_t addr, size_t index)
{
    if (addr2line.inWrite == INVALID_HANDLE_VALUE) {
        return 0;
    }
    char query[64];
    int len = 0;
    if (addr2line.useHunkQuery) {
        len = snprintf(query, sizeof(query), "%zu:%llx\n", index, (unsigned long long)addr);
    } else {
        len = snprintf(query, sizeof(query), "0x%llx\n", (unsigned long long)addr);
    }
    if (len <= 0 || (size_t)len >= sizeof(query)) {
        return 0;
    }
    DWORD written = 0;
    if (!WriteFile(addr2line.inWrite, query, (DWORD)len, &written, NULL) || written != (DWORD)len) {
        addr2line_stop();
        return 0;
    }
    return 1;
}

static int
addr2line_startProcess(const char *exe, const char *elfPath)
{
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE childStdoutReadTmp = INVALID_HANDLE_VALUE;
    HANDLE childStdoutWrite = INVALID_HANDLE_VALUE;
    HANDLE childStdinRead = INVALID_HANDLE_VALUE;
    HANDLE childStdinWriteTmp = INVALID_HANDLE_VALUE;

    if (!CreatePipe(&childStdoutReadTmp, &childStdoutWrite, &sa, 0)) {
        return 0;
    }
    if (!SetHandleInformation(childStdoutReadTmp, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(childStdoutReadTmp);
        CloseHandle(childStdoutWrite);
        return 0;
    }
    if (!CreatePipe(&childStdinRead, &childStdinWriteTmp, &sa, 0)) {
        CloseHandle(childStdoutReadTmp);
        CloseHandle(childStdoutWrite);
        return 0;
    }
    if (!SetHandleInformation(childStdinWriteTmp, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(childStdoutReadTmp);
        CloseHandle(childStdoutWrite);
        CloseHandle(childStdinRead);
        CloseHandle(childStdinWriteTmp);
        return 0;
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = childStdinRead;
    si.hStdOutput = childStdoutWrite;
    si.hStdError = childStdoutWrite;

    char cmdLine[PATH_MAX * 3];
    snprintf(cmdLine, sizeof(cmdLine), "\"%s\" -e \"%s\" -a -f -C", exe, elfPath);
    BOOL created = CreateProcessA(
        exe,
        cmdLine,
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &pi
    );

    CloseHandle(childStdinRead);
    CloseHandle(childStdoutWrite);
    if (!created) {
        CloseHandle(childStdoutReadTmp);
        CloseHandle(childStdinWriteTmp);
        return 0;
    }

    addr2line.processHandle = pi.hProcess;
    addr2line.threadHandle = pi.hThread;
    addr2line.outRead = childStdoutReadTmp;
    addr2line.inWrite = childStdinWriteTmp;
    return 1;
}

int
addr2line_start(const char *elf_path)
{
    if (!elf_path || !*elf_path) {
        return 0;
    }
    if (addr2line.processHandle != INVALID_HANDLE_VALUE &&
        strcmp(addr2line.elf, elf_path) == 0 &&
        addr2line_processIsAlive()) {
        return 1;
    }
    addr2line_stop();

    char bin[PATH_MAX];
    if (!debugger_toolchainBuildBinary(bin, sizeof(bin), "addr2line")) {
        return 0;
    }
    char exe[PATH_MAX];
    if (!file_findInPath(bin, exe, sizeof(exe))) {
        if (addr2line_missingTool[0] == '\0' || strcmp(addr2line_missingTool, bin) != 0) {
            strncpy(addr2line_missingTool, bin, sizeof(addr2line_missingTool) - 1);
            addr2line_missingTool[sizeof(addr2line_missingTool) - 1] = '\0';
            debug_error("addr2line: not found in PATH: %s", bin);
        }
        return 0;
    }
    addr2line_missingTool[0] = '\0';
    addr2line.useHunkQuery = addr2line_isHunkTool(exe);
    if (!addr2line_startProcess(exe, elf_path)) {
        addr2line.useHunkQuery = 0;
        return 0;
    }
    strncpy(addr2line.elf, elf_path, sizeof(addr2line.elf) - 1);
    addr2line.elf[sizeof(addr2line.elf) - 1] = '\0';
    addr2line_clearPending();
    addr2line.bufLen = 0;
    addr2line.expectFunc = 0;
    addr2line.expectFile = 0;
    return 1;
}

void
addr2line_stop(void)
{
    addr2line_clearPending();
    addr2line_closeStreams();
    if (addr2line.processHandle != INVALID_HANDLE_VALUE) {
        TerminateProcess(addr2line.processHandle, 1);
        WaitForSingleObject(addr2line.processHandle, 200);
    }
    addr2line_closeProcessHandles();
    addr2line_resetState();
}

int
addr2line_resolve(uint64_t addr, char *out_file, size_t file_cap, int *out_line)
{
    return addr2line_resolveDetailed(addr, out_file, file_cap, out_line, NULL, 0);
}

int
addr2line_resolveDetailed(uint64_t addr, char *out_file, size_t file_cap, int *out_line,
                          char *out_function, size_t function_cap)
{
    if (out_file && file_cap > 0) {
        out_file[0] = '\0';
    }
    if (out_line) {
        *out_line = 0;
    }
    if (out_function && function_cap > 0) {
        out_function[0] = '\0';
    }
    if (addr2line.inWrite == INVALID_HANDLE_VALUE || addr2line.outRead == INVALID_HANDLE_VALUE) {
        return 0;
    }
    if (!addr2line_processIsAlive()) {
        return 0;
    }
    uint64_t queryAddr = addr;
    size_t queryIndex = 0;
    uint32_t queryAddr24 = (uint32_t)(addr & 0x00ffffffu);
    if (!base_map_runtimeToDebugWithIndex(BASE_MAP_SECTION_TEXT, queryAddr24, &queryAddr24, &queryIndex)) {
        queryIndex = 0;
    }
    queryAddr = (uint64_t)queryAddr24;
    if (!addr2line_writeQuery(queryAddr, queryIndex)) {
        return 0;
    }

    char *line = NULL;
    int ok = 0;
    int gotAddr = 0;
    for (int i = 0; i < 128; ++i) {
        if (!addr2line_readLine(&line)) {
            addr2line_stop();
            break;
        }
        if (addr2line_isAddressLine(line)) {
            uint64_t gotValue = 0;
            size_t gotIndex = 0;
            int hasIndex = 0;
            if (addr2line_parseAddressLine(line, &gotValue, &gotIndex, &hasIndex) &&
                addr2line.useHunkQuery) {
                if (hasIndex) {
                    gotAddr = (gotIndex == queryIndex && gotValue == queryAddr) ? 1 : 0;
                } else {
                    gotAddr = (gotValue == queryAddr) ? 1 : 0;
                }
            } else if (addr2line_parseAddressLine(line, &gotValue, &gotIndex, &hasIndex) &&
                       !hasIndex &&
                       gotValue == queryAddr) {
                gotAddr = 1;
            } else {
                gotAddr = 0;
            }
            addr2line.expectFunc = gotAddr ? 1 : 0;
            addr2line.expectFile = 0;
            free(line);
            line = NULL;
            continue;
        }
        if (addr2line.expectFunc) {
            addr2line.expectFunc = 0;
            addr2line.expectFile = 1;
            if (gotAddr && out_function && function_cap > 0 &&
                strcmp(line, "??") != 0) {
                strncpy(out_function, line, function_cap - 1);
                out_function[function_cap - 1] = '\0';
            }
            free(line);
            line = NULL;
            continue;
        }
        if (addr2line.expectFile && gotAddr && out_file && file_cap > 0) {
            char *colon = strrchr(line, ':');
            if (colon && colon[1]) {
                int lineNo = atoi(colon + 1);
                if (lineNo > 0) {
                    *colon = '\0';
                    strncpy(out_file, line, file_cap - 1);
                    out_file[file_cap - 1] = '\0';
                    if (out_line) {
                        *out_line = lineNo;
                    }
                    ok = 1;
                }
            }
            addr2line.expectFile = 0;
            free(line);
            line = NULL;
            break;
        }
        free(line);
        line = NULL;
        if (ok) {
            break;
        }
    }
    if (line) {
        free(line);
    }
    return ok;
}
