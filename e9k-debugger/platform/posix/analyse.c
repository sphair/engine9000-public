/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "analyse.h"
#include "debug.h"
#include "debugger.h"
#include "file.h"
#include "alloc.h"

int
analyse_platformResolveFramesBatch(const char *elf, analyse_resolved_entry *entries, size_t count)
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

    int toChild[2];
    int fromChild[2];
    if (pipe(toChild) != 0) {
        debug_error("profile: failed to open addr2line stdin: %s", strerror(errno));
        return 0;
    }
    if (pipe(fromChild) != 0) {
        close(toChild[0]);
        close(toChild[1]);
        debug_error("profile: failed to open addr2line stdout: %s", strerror(errno));
        return 0;
    }

    pid_t pid = fork();
    if (pid == 0) {
        dup2(toChild[0], STDIN_FILENO);
        dup2(fromChild[1], STDOUT_FILENO);
        dup2(fromChild[1], STDERR_FILENO);
        close(toChild[0]);
        close(toChild[1]);
        close(fromChild[0]);
        close(fromChild[1]);
        char *const argv[] = {
            exe,
            (char*)"-e",
            (char*)elf,
            (char*)"-a",
            (char*)"-f",
            (char*)"-C",
            (char*)"-i",
            NULL
        };
        execv(exe, argv);
        _exit(127);
    }
    if (pid < 0) {
        close(toChild[0]);
        close(toChild[1]);
        close(fromChild[0]);
        close(fromChild[1]);
        debug_error("profile: failed to spawn addr2line: %s", strerror(errno));
        return 0;
    }

    close(toChild[0]);
    close(fromChild[1]);
    FILE *input = fdopen(toChild[1], "w");
    FILE *pipeOut = fdopen(fromChild[0], "r");
    if (!input || !pipeOut) {
        if (input) {
            fclose(input);
        } else {
            close(toChild[1]);
        }
        if (pipeOut) {
            fclose(pipeOut);
        } else {
            close(fromChild[0]);
        }
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        debug_error("profile: failed to open addr2line pipes");
        return 0;
    }

    struct sigaction oldAct;
    struct sigaction ignAct;
    memset(&ignAct, 0, sizeof(ignAct));
    ignAct.sa_handler = SIG_IGN;
    sigemptyset(&ignAct.sa_mask);
    sigaction(SIGPIPE, &ignAct, &oldAct);

    int writeOk = 1;
    for (size_t i = 0; i < count; ++i) {
        if (fprintf(input, "%s\n", entries[i].address) < 0) {
            writeOk = 0;
            break;
        }
    }
    if (writeOk && fflush(input) != 0) {
        writeOk = 0;
    }
    if (fclose(input) != 0) {
        writeOk = 0;
    }
    sigaction(SIGPIPE, &oldAct, NULL);

    if (!writeOk) {
        fclose(pipeOut);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        debug_error("profile: addr2line write failed");
        return 0;
    }

    char *line = NULL;
    size_t cap = 0;
    ssize_t read = 0;
    char **currentLines = NULL;
    size_t currentCount = 0;
    size_t entryIdx = 0;
    int ok = 1;
    int entryStarted = 0;
    while (ok && (read = getline(&line, &cap, pipeOut)) != -1) {
        while (read > 0 && (line[read - 1] == '\n' || line[read - 1] == '\r')) {
            line[--read] = '\0';
        }
        if (read == 0) {
            continue;
        }
        if (read >= 2 && line[0] == '0' && (line[1] == 'x' || line[1] == 'X')) {
            if (!entryStarted) {
                entryStarted = 1;
            } else {
                if (entryIdx >= count) {
                    ok = 0;
                    break;
                }
                size_t frameCount = 0;
                if (currentCount > 0) {
                    entries[entryIdx].frames = analyse_buildFramesFromLines(currentLines, currentCount, &frameCount);
                    entries[entryIdx].frameCount = frameCount;
                } else {
                    entries[entryIdx].frames = NULL;
                    entries[entryIdx].frameCount = 0;
                }
                currentLines = NULL;
                currentCount = 0;
                entryIdx++;
            }
            continue;
        }
        if (!entryStarted) {
            continue;
        }
        char *dup = alloc_strdup(line);
        if (!dup) {
            ok = 0;
            break;
        }
        char **tmpLines = alloc_realloc(currentLines, (currentCount + 1) * sizeof(char *));
        if (!tmpLines) {
            alloc_free(dup);
            ok = 0;
            break;
        }
        currentLines = tmpLines;
        currentLines[currentCount++] = dup;
    }

    if (ok && entryStarted) {
        if (entryIdx >= count) {
            ok = 0;
        } else {
            size_t frameCount = 0;
            if (currentCount > 0) {
                entries[entryIdx].frames = analyse_buildFramesFromLines(currentLines, currentCount, &frameCount);
                entries[entryIdx].frameCount = frameCount;
            } else {
                entries[entryIdx].frames = NULL;
                entries[entryIdx].frameCount = 0;
            }
            currentLines = NULL;
            currentCount = 0;
            entryIdx++;
        }
    }

    if (currentLines) {
        for (size_t i = 0; i < currentCount; ++i) {
            alloc_free(currentLines[i]);
        }
        alloc_free(currentLines);
    }
    free(line);
    fclose(pipeOut);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        ok = 0;
    } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        ok = 0;
    }
    if (entryIdx < count) {
        ok = 0;
    }
    return ok;
}
