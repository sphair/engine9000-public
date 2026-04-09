/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "addr2line.h"
#include "debugger.h"
#include "base_map.h"
#include "file.h"

typedef struct {
  pid_t pid;
  FILE *in;
  int outFD;
  char elf[PATH_MAX];
  char *pending;
  char buf[4096];
  size_t bufLen;
  int expectFunc;
  int expectFile;
  int useHunkQuery;
} addr2line_t;

static addr2line_t addr2line = {
    .pid = -1,
    .outFD = -1,
};

static char addr2line_missingTool[PATH_MAX];

int
addr2line_resolveDetailed(uint64_t addr, char *out_file, size_t file_cap, int *out_line,
                          char *out_function, size_t function_cap);

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
    if (strstr(name, "hunk-addr2line")) {
        return 1;
    }
    if (strstr(name, "hunk_addr2line")) {
        return 1;
    }
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
    if (addr2line.outFD < 0) {
        return 0;
    }
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
        if (addr2line.bufLen > sizeof(addr2line.buf) - 1) {
            addr2line.bufLen = 0;
        }
        size_t avail = (sizeof(addr2line.buf) - 1) - addr2line.bufLen;
        if (avail == 0) {
            addr2line.bufLen = 0;
            avail = sizeof(addr2line.buf) - 1;
        }
        ssize_t n = read(addr2line.outFD,
                         addr2line.buf + addr2line.bufLen,
                         avail);
        if (n > 0) {
            addr2line.bufLen += (size_t)n;
            continue;
        }
        if (n == 0) {
            return 0;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return 0;
    }
}

static void
addr2line_closeStreams(void)
{
    if (addr2line.in) {
        fclose(addr2line.in);
        addr2line.in = NULL;
    }
    if (addr2line.outFD >= 0) {
        close(addr2line.outFD);
        addr2line.outFD = -1;
    }
}

static int
addr2line_processIsAlive(void)
{
    if (addr2line.pid <= 0) {
        return 0;
    }
    int status = 0;
    pid_t r = waitpid(addr2line.pid, &status, WNOHANG);
    if (r == 0) {
        return 1;
    }
    if (r == addr2line.pid) {
        addr2line_closeStreams();
        addr2line.pid = -1;
        addr2line.elf[0] = '\0';
        addr2line.bufLen = 0;
        addr2line.expectFunc = 0;
        addr2line.expectFile = 0;
        addr2line.useHunkQuery = 0;
        return 0;
    }
    return 1;
}

static int
addr2line_writeQuery(uint64_t addr, size_t index)
{
    if (!addr2line.in) {
        return 0;
    }

    struct sigaction oldAct;
    struct sigaction ignAct;
    memset(&ignAct, 0, sizeof(ignAct));
    ignAct.sa_handler = SIG_IGN;
    sigemptyset(&ignAct.sa_mask);
    sigaction(SIGPIPE, &ignAct, &oldAct);

    errno = 0;
    int ok = 1;
    if (addr2line.useHunkQuery) {
        if (fprintf(addr2line.in, "%zu:%llx\n", index, (unsigned long long)addr) < 0) {
            ok = 0;
        }
    } else if (fprintf(addr2line.in, "0x%llx\n", (unsigned long long)addr) < 0) {
        ok = 0;
    }
    if (ok && fflush(addr2line.in) != 0) {
        ok = 0;
    }

    sigaction(SIGPIPE, &oldAct, NULL);

    if (!ok) {
        addr2line_stop();
        return 0;
    }
    return 1;
}

int
addr2line_start(const char *elf_path)
{
    if (!elf_path || !*elf_path) {
        return 0;
    }
    if (addr2line.pid > 0 && strcmp(addr2line.elf, elf_path) == 0) {
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

    int to_child[2];
    int from_child[2];
    if (pipe(to_child) != 0) {
        return 0;
    }
    if (pipe(from_child) != 0) {
        close(to_child[0]);
        close(to_child[1]);
        return 0;
    }

    pid_t pid = fork();
    if (pid == 0) {
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);
        dup2(from_child[1], STDERR_FILENO);
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        char *const argv[] = {
            exe,
            (char*)"-e",
            (char*)elf_path,
            (char*)"-a",
            (char*)"-f",
            (char*)"-C",
            NULL
        };
        execv(exe, argv);
        _exit(127);
    }
    if (pid < 0) {
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        return 0;
    }

    close(to_child[0]);
    close(from_child[1]);
    addr2line.in = fdopen(to_child[1], "w");
    addr2line.outFD = from_child[0];
    if (!addr2line.in || addr2line.outFD < 0) {
        addr2line_closeStreams();
        close(to_child[1]);
        if (addr2line.outFD >= 0) {
            close(addr2line.outFD);
            addr2line.outFD = -1;
        }
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        addr2line.useHunkQuery = 0;
        return 0;
    }
    setvbuf(addr2line.in, NULL, _IOLBF, 0);
    addr2line.pid = pid;
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
    if (addr2line.pid > 0) {
        kill(addr2line.pid, SIGTERM);
        waitpid(addr2line.pid, NULL, 0);
        addr2line.pid = -1;
    }
    addr2line.elf[0] = '\0';
    addr2line.bufLen = 0;
    addr2line.expectFunc = 0;
    addr2line.expectFile = 0;
    addr2line.useHunkQuery = 0;
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
    if (!addr2line.in || addr2line.outFD < 0) {
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
    int got_addr = 0;
    for (int i = 0; i < 128; ++i) {
        if (!addr2line_readLine(&line)) {
            addr2line_stop();
            break;
        }
        if (addr2line_isAddressLine(line)) {
            uint64_t gotAddr = 0;
            size_t gotIndex = 0;
            int hasIndex = 0;
            if (addr2line_parseAddressLine(line, &gotAddr, &gotIndex, &hasIndex) &&
                addr2line.useHunkQuery) {
                if (hasIndex) {
                    got_addr = (gotIndex == queryIndex && gotAddr == queryAddr) ? 1 : 0;
                } else {
                    got_addr = (gotAddr == queryAddr) ? 1 : 0;
                }
            } else if (addr2line_parseAddressLine(line, &gotAddr, &gotIndex, &hasIndex) &&
                       !hasIndex &&
                       gotAddr == queryAddr) {
                got_addr = 1;
            } else {
                got_addr = 0;
            }
            addr2line.expectFunc = got_addr ? 1 : 0;
            addr2line.expectFile = 0;
            free(line);
            line = NULL;
            continue;
        }
        if (addr2line.expectFunc) {
            addr2line.expectFunc = 0;
            addr2line.expectFile = 1;
            if (got_addr && out_function && function_cap > 0 &&
                strcmp(line, "??") != 0) {
                strncpy(out_function, line, function_cap - 1);
                out_function[function_cap - 1] = '\0';
            }
            free(line);
            line = NULL;
            continue;
        }
        if (addr2line.expectFile && got_addr && out_file && file_cap > 0) {
            char *colon = strrchr(line, ':');
            if (colon && colon[1]) {
                int line_no = atoi(colon + 1);
                if (line_no > 0) {
                    *colon = '\0';
                    strncpy(out_file, line, file_cap - 1);
                    out_file[file_cap - 1] = '\0';
                    if (out_line) {
                        *out_line = line_no;
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
