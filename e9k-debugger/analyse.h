/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <limits.h>
#include <stddef.h>

#define ANALYSE_LOCATION_TEXT_CAP 128
#define ANALYSE_SOURCE_TEXT_CAP 256

typedef struct {
    unsigned int pc;
    unsigned long long samples;
    unsigned long long cycles;
    char location[ANALYSE_LOCATION_TEXT_CAP];
    char source[ANALYSE_SOURCE_TEXT_CAP];
    char file[PATH_MAX];
    int line;
} analyse_profile_sample_entry;

typedef struct {
    char *function;
    char *file;
    int line;
    char *loc;
} analyse_frame;

typedef struct {
    char address[16];
    unsigned long long samples;
    unsigned long long cycles;
    analyse_frame *frames;
    size_t frameCount;
    char *chain;
    char *source;
    const char *topFile;
    int topLine;
    char fallbackFile[PATH_MAX];
    int fallbackLine;
} analyse_resolved_entry;

int
analyse_init(void);

void
analyse_shutdown(void);

int
analyse_reset(void);

int
analyse_handlePacket(const char *line, size_t len);

int
analyse_writeFinalJson(const char *jsonPath);

int
analyse_profileSnapshot(analyse_profile_sample_entry **out, size_t *count);

void
analyse_profileSnapshotFree(analyse_profile_sample_entry *entries);

void
analyse_populateSampleLocations(analyse_profile_sample_entry *entries, size_t count);

analyse_frame *
analyse_buildFramesFromLines(char **lines, size_t count, size_t *outCount);

int
analyse_platformResolveFramesBatch(const char *elf, analyse_resolved_entry *entries, size_t count);
