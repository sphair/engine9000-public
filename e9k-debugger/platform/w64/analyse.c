/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "analyse.h"
#include "debug.h"
#include "debugger.h"
#include "file.h"
#include "strutil.h"
#include "alloc.h"

#define ANALYSE_W64_ADDR2LINE_BATCH 64

static int
analyse_w64AppendText(char *out, size_t cap, size_t *pos, const char *text)
{
    if (!out || cap == 0 || !pos || !text) {
        return 0;
    }
    size_t len = strlen(text);
    if (*pos >= cap || len >= cap - *pos) {
        out[cap - 1] = '\0';
        return 0;
    }
    memcpy(out + *pos, text, len);
    *pos += len;
    out[*pos] = '\0';
    return 1;
}

static int
analyse_w64AppendQuoted(char *out, size_t cap, size_t *pos, const char *text)
{
    if (!analyse_w64AppendText(out, cap, pos, "\"")) {
        return 0;
    }
    size_t slashCount = 0;
    while (text && *text) {
        if (*text == '\\') {
            slashCount++;
            text++;
            continue;
        }
        if (*text == '"') {
            for (size_t i = 0; i < slashCount * 2 + 1; i++) {
                if (!analyse_w64AppendText(out, cap, pos, "\\")) {
                    return 0;
                }
            }
            slashCount = 0;
            if (!analyse_w64AppendText(out, cap, pos, "\"")) {
                return 0;
            }
            text++;
            continue;
        }
        for (size_t i = 0; i < slashCount; i++) {
            if (!analyse_w64AppendText(out, cap, pos, "\\")) {
                return 0;
            }
        }
        slashCount = 0;
        char ch[2] = { *text, '\0' };
        if (!analyse_w64AppendText(out, cap, pos, ch)) {
            return 0;
        }
        text++;
    }
    for (size_t i = 0; i < slashCount * 2; i++) {
        if (!analyse_w64AppendText(out, cap, pos, "\\")) {
            return 0;
        }
    }
    return analyse_w64AppendText(out, cap, pos, "\"");
}

static int
analyse_w64BuildAddr2lineCommand(char *out, size_t cap, const char *exe, const char *elf)
{
    if (!out || cap == 0 || !exe || !elf) {
        return 0;
    }
    size_t pos = 0;
    out[0] = '\0';
    return analyse_w64AppendQuoted(out, cap, &pos, exe) &&
           analyse_w64AppendText(out, cap, &pos, " -e ") &&
           analyse_w64AppendQuoted(out, cap, &pos, elf) &&
           analyse_w64AppendText(out, cap, &pos, " -a -f -C -i");
}

static int
analyse_w64FinishEntry(analyse_resolved_entry *entries,
                       size_t count,
                       size_t *entryIdx,
                       char ***currentLines,
                       size_t *currentCount)
{
    if (!entries || !entryIdx || !currentLines || !currentCount || *entryIdx >= count) {
        return 0;
    }
    size_t frameCount = 0;
    if (*currentCount > 0) {
        entries[*entryIdx].frames = analyse_buildFramesFromLines(*currentLines, *currentCount, &frameCount);
        entries[*entryIdx].frameCount = frameCount;
    } else {
        entries[*entryIdx].frames = NULL;
        entries[*entryIdx].frameCount = 0;
    }
    *currentLines = NULL;
    *currentCount = 0;
    (*entryIdx)++;
    return 1;
}

static int
analyse_w64ProcessLine(char *line,
                       size_t lineLen,
                       analyse_resolved_entry *entries,
                       size_t count,
                       size_t *entryIdx,
                       int *entryStarted,
                       char ***currentLines,
                       size_t *currentCount)
{
    if (!line || !entries || !entryIdx || !entryStarted || !currentLines || !currentCount) {
        return 0;
    }
    while (lineLen > 0 && (line[lineLen - 1] == '\n' || line[lineLen - 1] == '\r')) {
        line[--lineLen] = '\0';
    }
    if (lineLen == 0) {
        return 1;
    }
    if (lineLen >= 2 && line[0] == '0' && (line[1] == 'x' || line[1] == 'X')) {
        if (!*entryStarted) {
            *entryStarted = 1;
            return 1;
        }
        return analyse_w64FinishEntry(entries, count, entryIdx, currentLines, currentCount);
    }
    if (!*entryStarted) {
        return 1;
    }
    char *dup = alloc_strdup(line);
    if (!dup) {
        return 0;
    }
    char **tmpLines = alloc_realloc(*currentLines, (*currentCount + 1) * sizeof(char *));
    if (!tmpLines) {
        alloc_free(dup);
        return 0;
    }
    *currentLines = tmpLines;
    (*currentLines)[(*currentCount)++] = dup;
    return 1;
}

static void
analyse_w64FreePendingLines(char **currentLines, size_t currentCount)
{
    if (!currentLines) {
        return;
    }
    for (size_t i = 0; i < currentCount; ++i) {
        alloc_free(currentLines[i]);
    }
    alloc_free(currentLines);
}

static int
analyse_w64ResolveFramesBatchProcess(const char *elf, analyse_resolved_entry *entries, size_t count)
{
    if (!elf || !entries) {
        return 0;
    }
    if (count == 0) {
        return 1;
    }

    char bin[PATH_MAX];
    if (!debugger_toolchainBuildBinary(bin, sizeof(bin), "addr2line")) {
        debug_error("profile: failed to resolve addr2line binary");
        return 0;
    }

    char exe[PATH_MAX];
    if (!file_findInPath(bin, exe, sizeof(exe))) {
        debug_error("profile: addr2line not found in PATH: %s", bin);
        return 0;
    }

    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE childStdoutRead = INVALID_HANDLE_VALUE;
    HANDLE childStdoutWrite = INVALID_HANDLE_VALUE;
    HANDLE childStdinRead = INVALID_HANDLE_VALUE;
    HANDLE childStdinWrite = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&childStdoutRead, &childStdoutWrite, &sa, 0)) {
        debug_error("profile: failed to open addr2line stdout");
        return 0;
    }
    if (!SetHandleInformation(childStdoutRead, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(childStdoutRead);
        CloseHandle(childStdoutWrite);
        debug_error("profile: failed to configure addr2line stdout");
        return 0;
    }
    if (!CreatePipe(&childStdinRead, &childStdinWrite, &sa, 0)) {
        CloseHandle(childStdoutRead);
        CloseHandle(childStdoutWrite);
        debug_error("profile: failed to open addr2line stdin");
        return 0;
    }
    if (!SetHandleInformation(childStdinWrite, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(childStdoutRead);
        CloseHandle(childStdoutWrite);
        CloseHandle(childStdinRead);
        CloseHandle(childStdinWrite);
        debug_error("profile: failed to configure addr2line stdin");
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
    if (!analyse_w64BuildAddr2lineCommand(cmdLine, sizeof(cmdLine), exe, elf)) {
        CloseHandle(childStdoutRead);
        CloseHandle(childStdoutWrite);
        CloseHandle(childStdinRead);
        CloseHandle(childStdinWrite);
        debug_error("profile: failed to build addr2line command");
        return 0;
    }

    BOOL created = CreateProcessA(exe, cmdLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(childStdinRead);
    CloseHandle(childStdoutWrite);
    if (!created) {
        CloseHandle(childStdoutRead);
        CloseHandle(childStdinWrite);
        debug_error("profile: failed to spawn addr2line");
        return 0;
    }

    int writeOk = 1;
    for (size_t i = 0; i < count; ++i) {
        char query[32];
        strutil_join2Trunc(query, sizeof(query), entries[i].address, "\n");
        size_t queryLen = strlen(query);
        DWORD written = 0;
        if (queryLen == 0 || queryLen >= sizeof(query) ||
            !WriteFile(childStdinWrite, query, (DWORD)queryLen, &written, NULL) ||
            written != (DWORD)queryLen) {
            writeOk = 0;
            break;
        }
    }
    CloseHandle(childStdinWrite);
    if (!writeOk) {
        CloseHandle(childStdoutRead);
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        debug_error("profile: addr2line write failed");
        return 0;
    }

    char *line = NULL;
    size_t lineCap = 0;
    size_t lineLen = 0;
    char **currentLines = NULL;
    size_t currentCount = 0;
    size_t entryIdx = 0;
    int entryStarted = 0;
    int ok = 1;
    char readBuf[4096];
    DWORD bytesRead = 0;
    while (ok && ReadFile(childStdoutRead, readBuf, (DWORD)sizeof(readBuf), &bytesRead, NULL) && bytesRead > 0) {
        for (DWORD i = 0; i < bytesRead; ++i) {
            char ch = readBuf[i];
            if (ch != '\n') {
                if (lineLen + 1 >= lineCap) {
                    size_t next = lineCap ? lineCap * 2 : 128;
                    char *tmp = alloc_realloc(line, next);
                    if (!tmp) {
                        ok = 0;
                        break;
                    }
                    line = tmp;
                    lineCap = next;
                }
                line[lineLen++] = ch;
                continue;
            }
            if (lineLen + 1 >= lineCap) {
                size_t next = lineCap ? lineCap * 2 : 128;
                char *tmp = alloc_realloc(line, next);
                if (!tmp) {
                    ok = 0;
                    break;
                }
                line = tmp;
                lineCap = next;
            }
            line[lineLen] = '\0';
            if (!analyse_w64ProcessLine(line, lineLen, entries, count, &entryIdx, &entryStarted, &currentLines, &currentCount)) {
                ok = 0;
                break;
            }
            lineLen = 0;
        }
    }
    if (ok && lineLen > 0) {
        if (lineLen + 1 >= lineCap) {
            char *tmp = alloc_realloc(line, lineLen + 1);
            if (!tmp) {
                ok = 0;
            } else {
                line = tmp;
                lineCap = lineLen + 1;
            }
        }
        if (ok) {
            line[lineLen] = '\0';
            ok = analyse_w64ProcessLine(line, lineLen, entries, count, &entryIdx, &entryStarted, &currentLines, &currentCount);
        }
    }
    if (ok && entryStarted) {
        ok = analyse_w64FinishEntry(entries, count, &entryIdx, &currentLines, &currentCount);
    }
    analyse_w64FreePendingLines(currentLines, currentCount);
    alloc_free(line);
    CloseHandle(childStdoutRead);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (exitCode != 0) {
        ok = 0;
    }
    if (entryIdx < count) {
        ok = 0;
    }
    return ok;
}

int
analyse_platformResolveFramesBatch(const char *elf, analyse_resolved_entry *entries, size_t count)
{
    if (!elf || !entries) {
        return 0;
    }
    for (size_t base = 0; base < count; base += ANALYSE_W64_ADDR2LINE_BATCH) {
        size_t batchCount = count - base;
        if (batchCount > ANALYSE_W64_ADDR2LINE_BATCH) {
            batchCount = ANALYSE_W64_ADDR2LINE_BATCH;
        }
        if (!analyse_w64ResolveFramesBatchProcess(elf, entries + base, batchCount)) {
            return 0;
        }
    }
    return 1;
}
