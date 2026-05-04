/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */


#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "amiga_uae_options.h"
#include "alloc.h"
#include "debugger.h"
#include "libretro_host.h"

typedef struct amiga_uae_kv {
    char *key;
    char *value;
} amiga_uae_kv_t;

static amiga_uae_kv_t *amiga_uae_entries = NULL;
static size_t amiga_uae_entryCount = 0;
static size_t amiga_uae_entryCap = 0;
enum {
    AMIGA_UAE_DIRTY_FLOPPY0 = 1 << 0,
    AMIGA_UAE_DIRTY_FLOPPY1 = 1 << 1,
    AMIGA_UAE_DIRTY_HD0 = 1 << 2,
    AMIGA_UAE_DIRTY_PUAE = 1 << 3,
    AMIGA_UAE_DIRTY_HD1 = 1 << 4
};
static int amiga_uae_dirtyMask = 0;
static char amiga_uae_loadedPath[PATH_MAX];
static char amiga_uae_floppy0[PATH_MAX];
static char amiga_uae_floppy1[PATH_MAX];
static char amiga_uae_hdFolder[2][PATH_MAX];
static char amiga_uae_hdHdf[2][PATH_MAX];

static void
amiga_uaeTrimRight(char *s)
{
    if (!s) {
        return;
    }
    size_t len = strlen(s);
    while (len > 0) {
        char c = s[len - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            s[len - 1] = '\0';
            len--;
            continue;
        }
        break;
    }
}

static char *
amiga_uaeTrimLeft(char *s)
{
    if (!s) {
        return NULL;
    }
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

static const char *
amiga_uaeTrimLeftConst(const char *s)
{
    if (!s) {
        return NULL;
    }
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

static int
amiga_uaeKeyIsPuae(const char *key)
{
    if (!key) {
        return 0;
    }
    return strncmp(key, "puae_", 5) == 0;
}

static int
amiga_uaeKeyIsFloppy(const char *key, int *out_drive)
{
    if (out_drive) {
        *out_drive = -1;
    }
    if (!key) {
        return 0;
    }
    if (strcmp(key, "floppy0") == 0) {
        if (out_drive) {
            *out_drive = 0;
        }
        return 1;
    }
    if (strcmp(key, "floppy1") == 0) {
        if (out_drive) {
            *out_drive = 1;
        }
        return 1;
    }
    return 0;
}

static int
amiga_uaeKeyIsFloppyType(const char *key, int *out_drive)
{
    if (out_drive) {
        *out_drive = -1;
    }
    if (!key || !*key) {
        return 0;
    }
    if (strncmp(key, "floppy", 6) != 0) {
        return 0;
    }
    const char *p = key + 6;
    if (*p < '0' || *p > '9') {
        return 0;
    }
    int drive = *p - '0';
    p++;
    if (strcmp(p, "type") != 0) {
        return 0;
    }
    if (out_drive) {
        *out_drive = drive;
    }
    return 1;
}

static int
amiga_uaeParseFilesystem2DhFolder(const char *value, int unit, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!value || !*value) {
        return 0;
    }
    const char *comma1 = strchr(value, ',');
    if (!comma1) {
        return 0;
    }
    const char *rest = comma1 + 1;
    if (!*rest) {
        return 0;
    }
    const char *commaLast = strrchr(rest, ',');
    size_t midLen = commaLast ? (size_t)(commaLast - rest) : strlen(rest);
    if (midLen == 0) {
        return 0;
    }
    if (midLen >= 1024) {
        midLen = 1023;
    }
    char mid[1024];
    memcpy(mid, rest, midLen);
    mid[midLen] = '\0';

    char device[8];
    snprintf(device, sizeof(device), "DH%d:", unit);
    char *p = amiga_uaeTrimLeft(mid);
    if (!p || strncmp(p, device, strlen(device)) != 0) {
        return 0;
    }
    const char *colon1 = strchr(p, ':');
    if (!colon1) {
        return 0;
    }
    const char *colon2 = strchr(colon1 + 1, ':');
    if (!colon2) {
        return 0;
    }
    const char *path = colon2 + 1;
    if (!*path) {
        return 0;
    }
    if (path[0] == '"' || path[0] == '\'') {
        char quote = path[0];
        path++;
        const char *end = strrchr(path, quote);
        if (end && end > path) {
            size_t outPos = 0;
            for (const char *p = path; p < end && outPos + 1 < cap; ++p) {
                if (*p == '\\' && (p + 1) < end) {
                    p++;
                }
                out[outPos++] = *p;
            }
            out[outPos] = '\0';
            return 1;
        }
    }
    strncpy(out, path, cap - 1);
    out[cap - 1] = '\0';
    return 1;
}

static int
amiga_uaeFilesystem2IsDhLine(const char *value, int unit)
{
    char tmp[8];
    return amiga_uaeParseFilesystem2DhFolder(value, unit, tmp, sizeof(tmp)) ? 1 : 0;
}

static int
amiga_uaeFilesystem2IsManagedDhLine(const char *value, int unit)
{
    if (unit == 0) {
        return amiga_uaeFilesystem2IsDhLine(value, unit);
    }
    if (!value || strncmp(value, "rw,", 3) != 0) {
        return 0;
    }
    const char *rest = value + 3;
    rest = amiga_uaeTrimLeftConst(rest);
    const char *prefix = "DH1:Work1:";
    if (strncmp(rest, prefix, strlen(prefix)) != 0) {
        return 0;
    }
    const char *commaLast = strrchr(rest, ',');
    if (!commaLast || strcmp(commaLast, ",0") != 0) {
        return 0;
    }
    char tmp[8];
    return amiga_uaeParseFilesystem2DhFolder(value, unit, tmp, sizeof(tmp)) ? 1 : 0;
}

static int
amiga_uaeParseHardfile2DhPath(const char *value, int unit, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!value || !*value) {
        return 0;
    }
    const char *comma1 = strchr(value, ',');
    if (!comma1) {
        return 0;
    }
    const char *rest = comma1 + 1;
    char device[8];
    snprintf(device, sizeof(device), "DH%d:", unit);
    if (strncmp(rest, device, strlen(device)) != 0) {
        return 0;
    }
    const char *path = rest + strlen(device);
    if (!*path) {
        return 0;
    }
    if (path[0] == '"' || path[0] == '\'') {
        char quote = path[0];
        path++;
        const char *end = strrchr(path, quote);
        if (end && end > path) {
            size_t outPos = 0;
            for (const char *p = path; p < end && outPos + 1 < cap; ++p) {
                if (*p == '\\' && (p + 1) < end) {
                    p++;
                }
                out[outPos++] = *p;
            }
            out[outPos] = '\0';
            return 1;
        }
    }
    const char *end = strchr(path, ',');
    size_t len = end ? (size_t)(end - path) : strlen(path);
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(out, path, len);
    out[len] = '\0';
    return 1;
}

static int
amiga_uaeHardfile2IsDhLine(const char *value, int unit)
{
    char tmp[8];
    return amiga_uaeParseHardfile2DhPath(value, unit, tmp, sizeof(tmp)) ? 1 : 0;
}

static const char *
amiga_uaeHardfile2TailAfterPath(const char *path)
{
    if (!path || !*path) {
        return NULL;
    }
    if (path[0] == '"' || path[0] == '\'') {
        char quote = path[0];
        const char *end = strrchr(path + 1, quote);
        if (!end || end == path + 1) {
            return NULL;
        }
        return end + 1;
    }
    return strchr(path, ',');
}

static int
amiga_uaeHardfile2HasGeneratedTail(const char *tail)
{
    if (!tail || tail[0] != ',') {
        return 0;
    }
    if (strcmp(tail, ",0,0,0,512,0,,uae0") == 0) {
        return 1;
    }
    const char *p = tail + 1;
    if (strncmp(p, "32,", 3) != 0) {
        return 0;
    }
    p += 3;
    if (*p < '0' || *p > '9') {
        return 0;
    }
    while (*p >= '0' && *p <= '9') {
        p++;
    }
    return strcmp(p, ",2,512,0,,uae0") == 0 ? 1 : 0;
}

static int
amiga_uaeHardfile2IsManagedDhLine(const char *value, int unit)
{
    if (unit == 0) {
        return amiga_uaeHardfile2IsDhLine(value, unit);
    }
    if (!value || strncmp(value, "rw,DH1:", 7) != 0) {
        return 0;
    }
    const char *path = value + 7;
    const char *tail = amiga_uaeHardfile2TailAfterPath(path);
    if (!amiga_uaeHardfile2HasGeneratedTail(tail)) {
        return 0;
    }
    char tmp[8];
    return amiga_uaeParseHardfile2DhPath(value, unit, tmp, sizeof(tmp)) ? 1 : 0;
}

static void
amiga_uaeWriteQuotedFilesystemPath(FILE *out, const char *path)
{
    if (!out) {
        return;
    }
    fputc('"', out);
    if (path) {
        for (const char *p = path; *p; ++p) {
            if (*p == '\\') {
                fputc('\\', out);
            }
            fputc(*p, out);
        }
    }
    fputc('"', out);
}

static int
amiga_uaeHdfIsRdb(const char *path)
{
    char header[4];
    FILE *f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    size_t count = fread(header, 1, 3, f);
    fclose(f);
    if (count != 3) {
        return 0;
    }
    header[3] = '\0';
    return strcmp(header, "RDS") == 0 ? 1 : 0;
}

static int
amiga_uaeHdfGuessSurfaces(const char *path)
{
    struct stat st;
    if (!path || stat(path, &st) != 0) {
        return 1;
    }
    return (st.st_size / 1024) >= (1024 * 1024) ? 16 : 1;
}

static int
amiga_uaePathIsDirectory(const char *path)
{
    struct stat st;

    if (!path || !*path) {
        return 0;
    }
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

static int
amiga_uaeHardDriveDirtyBit(int unit)
{
    if (unit == 0) {
        return AMIGA_UAE_DIRTY_HD0;
    }
    if (unit == 1) {
        return AMIGA_UAE_DIRTY_HD1;
    }
    return 0;
}

static int
amiga_uaeParseKeyValue(const char *line, char *outKey, size_t keyCap, char *outValue, size_t valueCap)
{
    if (!line || !outKey || keyCap == 0 || !outValue || valueCap == 0) {
        return 0;
    }
    outKey[0] = '\0';
    outValue[0] = '\0';

    const char *p = line;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '\0' || *p == ';' || *p == '#') {
        return 0;
    }
    const char *eq = strchr(p, '=');
    if (!eq) {
        return 0;
    }

    size_t keyLen = (size_t)(eq - p);
    if (keyLen >= keyCap) {
        keyLen = keyCap - 1;
    }
    memcpy(outKey, p, keyLen);
    outKey[keyLen] = '\0';
    amiga_uaeTrimRight(outKey);
    char *k = amiga_uaeTrimLeft(outKey);
    if (k != outKey) {
        memmove(outKey, k, strlen(k) + 1);
    }

    const char *v = eq + 1;
    while (*v == ' ' || *v == '\t') {
        v++;
    }
    strncpy(outValue, v, valueCap - 1);
    outValue[valueCap - 1] = '\0';
    amiga_uaeTrimRight(outValue);
    return outKey[0] ? 1 : 0;
}

static amiga_uae_kv_t *
amiga_uaeFindEntry(const char *key)
{
    if (!key || !*key) {
        return NULL;
    }
    for (size_t i = 0; i < amiga_uae_entryCount; ++i) {
        if (amiga_uae_entries[i].key && strcmp(amiga_uae_entries[i].key, key) == 0) {
            return &amiga_uae_entries[i];
        }
    }
    return NULL;
}

static amiga_uae_kv_t *
amiga_uaeGetOrAddEntry(const char *key)
{
    if (!key || !*key) {
        return NULL;
    }
    amiga_uae_kv_t *existing = amiga_uaeFindEntry(key);
    if (existing) {
        return existing;
    }
    if (amiga_uae_entryCount >= amiga_uae_entryCap) {
        size_t nextCap = amiga_uae_entryCap ? amiga_uae_entryCap * 2 : 64;
        amiga_uae_kv_t *next = (amiga_uae_kv_t *)alloc_realloc(amiga_uae_entries, nextCap * sizeof(*next));
        if (!next) {
            return NULL;
        }
        amiga_uae_entries = next;
        amiga_uae_entryCap = nextCap;
    }
    amiga_uae_kv_t *ent = &amiga_uae_entries[amiga_uae_entryCount++];
    memset(ent, 0, sizeof(*ent));
    ent->key = alloc_strdup(key);
    ent->value = alloc_strdup("");
    return ent;
}

static void
amiga_uaeRemoveEntry(const char *key)
{
    if (!key || !*key || !amiga_uae_entries) {
        return;
    }
    for (size_t i = 0; i < amiga_uae_entryCount; ++i) {
        if (amiga_uae_entries[i].key && strcmp(amiga_uae_entries[i].key, key) == 0) {
            alloc_free(amiga_uae_entries[i].key);
            alloc_free(amiga_uae_entries[i].value);
            for (size_t j = i + 1; j < amiga_uae_entryCount; ++j) {
                amiga_uae_entries[j - 1] = amiga_uae_entries[j];
            }
            amiga_uae_entryCount--;
            return;
        }
    }
}

static int
amiga_uaeCompareEntriesByKey(const void *a, const void *b)
{
    const amiga_uae_kv_t *ea = (const amiga_uae_kv_t *)a;
    const amiga_uae_kv_t *eb = (const amiga_uae_kv_t *)b;
    const char *ka = ea && ea->key ? ea->key : "";
    const char *kb = eb && eb->key ? eb->key : "";
    return strcmp(ka, kb);
}

static int
amiga_uaeWriteAtomically(const char *dstPath, const char *tmpPath)
{
    return debugger_platform_replaceFile(tmpPath, dstPath);
}

void
amiga_uaeClearPuaeOptions(void)
{
    if (amiga_uae_entries) {
        for (size_t i = 0; i < amiga_uae_entryCount; ++i) {
            alloc_free(amiga_uae_entries[i].key);
            alloc_free(amiga_uae_entries[i].value);
        }
        alloc_free(amiga_uae_entries);
    }
    amiga_uae_entries = NULL;
    amiga_uae_entryCount = 0;
    amiga_uae_entryCap = 0;
    amiga_uae_dirtyMask = 0;
    amiga_uae_loadedPath[0] = '\0';
    amiga_uae_floppy0[0] = '\0';
    amiga_uae_floppy1[0] = '\0';
    for (int unit = 0; unit < 2; ++unit) {
        amiga_uae_hdFolder[unit][0] = '\0';
        amiga_uae_hdHdf[unit][0] = '\0';
    }
}

int
amiga_uaeUaeOptionsDirty(void)
{
    return amiga_uae_dirtyMask ? 1 : 0;
}

int
amiga_uaeHasRestartRequiredDirty(void)
{
    const int restartMask = AMIGA_UAE_DIRTY_HD0 | AMIGA_UAE_DIRTY_HD1;
    return (amiga_uae_dirtyMask & restartMask) ? 1 : 0;
}

int
amiga_uaeHasFloppyDirty(void)
{
    const int floppyMask = AMIGA_UAE_DIRTY_FLOPPY0 | AMIGA_UAE_DIRTY_FLOPPY1;
    return (amiga_uae_dirtyMask & floppyMask) ? 1 : 0;
}

bool
amiga_uaeLoadUaeOptions(const char *uaePath)
{
    amiga_uaeClearPuaeOptions();
    if (!uaePath || !*uaePath) {
        return true;
    }
    strncpy(amiga_uae_loadedPath, uaePath, sizeof(amiga_uae_loadedPath) - 1);
    amiga_uae_loadedPath[sizeof(amiga_uae_loadedPath) - 1] = '\0';

    FILE *f = fopen(uaePath, "r");
    if (!f) {
        amiga_uae_dirtyMask = 0;
        return true;
    }
    char line[8192];
    while (fgets(line, sizeof(line), f)) {
        char key[1024];
        char value[7168];
        if (!amiga_uaeParseKeyValue(line, key, sizeof(key), value, sizeof(value))) {
            continue;
        }
        int drive = -1;
        if (amiga_uaeKeyIsFloppy(key, &drive)) {
            if (drive == 0) {
                strncpy(amiga_uae_floppy0, value, sizeof(amiga_uae_floppy0) - 1);
                amiga_uae_floppy0[sizeof(amiga_uae_floppy0) - 1] = '\0';
            } else if (drive == 1) {
                strncpy(amiga_uae_floppy1, value, sizeof(amiga_uae_floppy1) - 1);
                amiga_uae_floppy1[sizeof(amiga_uae_floppy1) - 1] = '\0';
            }
            continue;
        }
        if (strcmp(key, "filesystem2") == 0) {
            char folder[PATH_MAX];
            for (int unit = 0; unit < 2; ++unit) {
                if (amiga_uaeFilesystem2IsManagedDhLine(value, unit) &&
                    amiga_uaeParseFilesystem2DhFolder(value, unit, folder, sizeof(folder))) {
                    strncpy(amiga_uae_hdFolder[unit], folder, sizeof(amiga_uae_hdFolder[unit]) - 1);
                    amiga_uae_hdFolder[unit][sizeof(amiga_uae_hdFolder[unit]) - 1] = '\0';
                    amiga_uae_hdHdf[unit][0] = '\0';
                    break;
                }
            }
            continue;
        }
        if (strcmp(key, "hardfile2") == 0) {
            char hdf[PATH_MAX];
            for (int unit = 0; unit < 2; ++unit) {
                if (amiga_uaeHardfile2IsManagedDhLine(value, unit) &&
                    amiga_uaeParseHardfile2DhPath(value, unit, hdf, sizeof(hdf))) {
                    if (amiga_uaePathIsDirectory(hdf)) {
                        strncpy(amiga_uae_hdFolder[unit], hdf, sizeof(amiga_uae_hdFolder[unit]) - 1);
                        amiga_uae_hdFolder[unit][sizeof(amiga_uae_hdFolder[unit]) - 1] = '\0';
                        amiga_uae_hdHdf[unit][0] = '\0';
                    } else {
                        strncpy(amiga_uae_hdHdf[unit], hdf, sizeof(amiga_uae_hdHdf[unit]) - 1);
                        amiga_uae_hdHdf[unit][sizeof(amiga_uae_hdHdf[unit]) - 1] = '\0';
                        amiga_uae_hdFolder[unit][0] = '\0';
                    }
                    break;
                }
            }
            continue;
        }
        if (!amiga_uaeKeyIsPuae(key)) {
            continue;
        }
        amiga_uae_kv_t *ent = amiga_uaeGetOrAddEntry(key);
        if (!ent) {
            continue;
        }
        alloc_free(ent->value);
        ent->value = alloc_strdup(value);
    }
    fclose(f);
    amiga_uae_dirtyMask = 0;
    return true;
}

const char *
amiga_uaeGetPuaeOptionValue(const char *key)
{
    if (!key || !*key) {
        return NULL;
    }
    amiga_uae_kv_t *ent = amiga_uaeFindEntry(key);
    return ent ? ent->value : NULL;
}

void
amiga_uaeSetPuaeOptionValue(const char *key, const char *value)
{
    if (!key || !*key) {
        return;
    }
    if (!amiga_uaeKeyIsPuae(key)) {
        return;
    }
    if (!value) {
        amiga_uaeRemoveEntry(key);
        amiga_uae_dirtyMask |= AMIGA_UAE_DIRTY_PUAE;
        return;
    }
    amiga_uae_kv_t *ent = amiga_uaeGetOrAddEntry(key);
    if (!ent) {
        return;
    }
    alloc_free(ent->value);
    ent->value = alloc_strdup(value);
    amiga_uae_dirtyMask |= AMIGA_UAE_DIRTY_PUAE;
}

const char *
amiga_uaeGetFloppyPath(int drive)
{
    if (drive == 0) {
        return amiga_uae_floppy0[0] ? amiga_uae_floppy0 : NULL;
    }
    if (drive == 1) {
        return amiga_uae_floppy1[0] ? amiga_uae_floppy1 : NULL;
    }
    return NULL;
}

const char *
amiga_uaeGetHardDriveFolderPathForUnit(int unit)
{
    if (unit < 0 || unit >= 2) {
        return NULL;
    }
    return amiga_uae_hdFolder[unit][0] ? amiga_uae_hdFolder[unit] : NULL;
}

void
amiga_uaeSetHardDriveFolderPathForUnit(int unit, const char *path)
{
    if (unit < 0 || unit >= 2) {
        return;
    }
    const char *src = path ? path : "";
    strncpy(amiga_uae_hdFolder[unit], src, sizeof(amiga_uae_hdFolder[unit]) - 1);
    amiga_uae_hdFolder[unit][sizeof(amiga_uae_hdFolder[unit]) - 1] = '\0';
    if (src[0]) {
        amiga_uae_hdHdf[unit][0] = '\0';
    }
    amiga_uae_dirtyMask |= amiga_uaeHardDriveDirtyBit(unit);
}

const char *
amiga_uaeGetHardDriveHdfPathForUnit(int unit)
{
    if (unit < 0 || unit >= 2) {
        return NULL;
    }
    return amiga_uae_hdHdf[unit][0] ? amiga_uae_hdHdf[unit] : NULL;
}

void
amiga_uaeSetHardDriveHdfPathForUnit(int unit, const char *path)
{
    if (unit < 0 || unit >= 2) {
        return;
    }
    const char *src = path ? path : "";
    strncpy(amiga_uae_hdHdf[unit], src, sizeof(amiga_uae_hdHdf[unit]) - 1);
    amiga_uae_hdHdf[unit][sizeof(amiga_uae_hdHdf[unit]) - 1] = '\0';
    if (src[0]) {
        amiga_uae_hdFolder[unit][0] = '\0';
    }
    amiga_uae_dirtyMask |= amiga_uaeHardDriveDirtyBit(unit);
}

const char *
amiga_uaeGetHardDriveFolderPath(void)
{
    return amiga_uaeGetHardDriveFolderPathForUnit(0);
}

void
amiga_uaeSetHardDriveFolderPath(const char *path)
{
    amiga_uaeSetHardDriveFolderPathForUnit(0, path);
}

const char *
amiga_uaeGetHardDriveHdfPath(void)
{
    return amiga_uaeGetHardDriveHdfPathForUnit(0);
}

void
amiga_uaeSetHardDriveHdfPath(const char *path)
{
    amiga_uaeSetHardDriveHdfPathForUnit(0, path);
}

void
amiga_uaeSetFloppyPath(int drive, const char *path)
{
    if (drive != 0 && drive != 1) {
        return;
    }
    const char *src = path ? path : "";
    if (drive == 0) {
        strncpy(amiga_uae_floppy0, src, sizeof(amiga_uae_floppy0) - 1);
        amiga_uae_floppy0[sizeof(amiga_uae_floppy0) - 1] = '\0';
        amiga_uae_dirtyMask |= AMIGA_UAE_DIRTY_FLOPPY0;
    } else {
        strncpy(amiga_uae_floppy1, src, sizeof(amiga_uae_floppy1) - 1);
        amiga_uae_floppy1[sizeof(amiga_uae_floppy1) - 1] = '\0';
        amiga_uae_dirtyMask |= AMIGA_UAE_DIRTY_FLOPPY1;
    }
}

void
amiga_uaeClearFloppyDirty(int drive)
{
    if (drive == 0) {
        amiga_uae_dirtyMask &= ~AMIGA_UAE_DIRTY_FLOPPY0;
    } else if (drive == 1) {
        amiga_uae_dirtyMask &= ~AMIGA_UAE_DIRTY_FLOPPY1;
    }
}

bool
amiga_uaeWriteUaeOptionsToFile(const char *uaePath)
{
    if (!uaePath || !*uaePath) {
        return false;
    }
    char tmpPath[PATH_MAX];
    int tmpWritten = snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", uaePath);
    if (tmpWritten < 0 || tmpWritten >= (int)sizeof(tmpPath)) {
        return false;
    }

    FILE *in = fopen(uaePath, "r");
    FILE *out = fopen(tmpPath, "w");
    if (!out) {
        if (in) {
            fclose(in);
        }
        return false;
    }

    int lastWrittenHadNewline = 1;
    if (in) {
        char line[8192];
        while (fgets(line, sizeof(line), in)) {
            char key[1024];
            char value[7168];
            int isManaged = 0;
            if (amiga_uaeParseKeyValue(line, key, sizeof(key), value, sizeof(value))) {
                int drive = -1;
                if (amiga_uaeKeyIsPuae(key)) {
                    isManaged = 1;
                } else if (amiga_uaeKeyIsFloppy(key, &drive)) {
                    isManaged = 1;
                } else if (amiga_uaeKeyIsFloppyType(key, &drive)) {
                    isManaged = 1;
                } else if (strcmp(key, "nr_floppies") == 0) {
                    isManaged = 1;
                } else if (strcmp(key, "filesystem2") == 0) {
                    for (int unit = 0; unit < 2; ++unit) {
                        if (amiga_uaeFilesystem2IsManagedDhLine(value, unit)) {
                            isManaged = 1;
                            break;
                        }
                    }
                } else if (strcmp(key, "hardfile2") == 0) {
                    for (int unit = 0; unit < 2; ++unit) {
                        if (amiga_uaeHardfile2IsManagedDhLine(value, unit)) {
                            isManaged = 1;
                            break;
                        }
                    }
                }
            }
            if (isManaged) {
                continue;
            }
            fputs(line, out);
            size_t len = strlen(line);
            lastWrittenHadNewline = (len > 0 && line[len - 1] == '\n') ? 1 : 0;
        }
        fclose(in);
    }

    if (!lastWrittenHadNewline) {
        fputc('\n', out);
    }

    int nrFloppies = 1;
    if (amiga_uae_floppy1[0]) {
        nrFloppies = 2;
    } else if (amiga_uae_floppy0[0]) {
        nrFloppies = 1;
    }
    fprintf(out, "nr_floppies=%d\n", nrFloppies);

    if (amiga_uae_floppy0[0]) {
        fprintf(out, "floppy0=%s\n", amiga_uae_floppy0);
    }
    if (amiga_uae_floppy1[0]) {
        fprintf(out, "floppy1=%s\n", amiga_uae_floppy1);
        fprintf(out, "floppy1type=0\n");
    }

    for (int unit = 0; unit < 2; ++unit) {
        if (amiga_uae_hdHdf[unit][0]) {
            fprintf(out, "hardfile2=rw,DH%d:", unit);
            amiga_uaeWriteQuotedFilesystemPath(out, amiga_uae_hdHdf[unit]);
            if (amiga_uaeHdfIsRdb(amiga_uae_hdHdf[unit])) {
                fputs(",0,0,0,512,0,,uae0\n", out);
            } else {
                fprintf(out, ",32,%d,2,512,0,,uae0\n", amiga_uaeHdfGuessSurfaces(amiga_uae_hdHdf[unit]));
            }
        } else if (amiga_uae_hdFolder[unit][0]) {
            char sep = '/';
            if (strchr(amiga_uae_hdFolder[unit], '\\')) {
                sep = '\\';
            }
            char folder[PATH_MAX + 2];
            strncpy(folder, amiga_uae_hdFolder[unit], sizeof(folder) - 1);
            folder[sizeof(folder) - 1] = '\0';
            size_t len = strlen(folder);
            if (len > 0 && folder[len - 1] != '/' && folder[len - 1] != '\\') {
                if (len + 2 <= sizeof(folder)) {
                    folder[len] = sep;
                    folder[len + 1] = '\0';
                }
            }
            if (unit == 0) {
                fputs("filesystem2=rw,DH0:Work:", out);
            } else {
                fputs("filesystem2=rw,DH1:Work1:", out);
            }
            amiga_uaeWriteQuotedFilesystemPath(out, folder);
            fputs(",0\n", out);
        }
    }

    if (amiga_uae_entryCount > 1) {
        qsort(amiga_uae_entries, amiga_uae_entryCount, sizeof(*amiga_uae_entries), amiga_uaeCompareEntriesByKey);
    }

    for (size_t i = 0; i < amiga_uae_entryCount; ++i) {
        const char *k = amiga_uae_entries[i].key;
        const char *v = amiga_uae_entries[i].value;
        if (!k || !*k) {
            continue;
        }
        if (!amiga_uaeKeyIsPuae(k)) {
            continue;
        }
        if (!v) {
            v = "";
        }
        fprintf(out, "%s=%s\n", k, v);
    }

    fclose(out);
    if (!amiga_uaeWriteAtomically(uaePath, tmpPath)) {
        remove(tmpPath);
        return false;
    }
    amiga_uae_dirtyMask = 0;
    return true;
}

bool
amiga_uaeApplyPuaeOptionsToHost(const char *uaePath)
{
    if (!uaePath || !*uaePath) {
        return false;
    }
    FILE *f = fopen(uaePath, "r");
    if (!f) {
        return false;
    }
    char line[8192];
    while (fgets(line, sizeof(line), f)) {
        char key[1024];
        char value[7168];
        if (!amiga_uaeParseKeyValue(line, key, sizeof(key), value, sizeof(value))) {
            continue;
        }
        if (!amiga_uaeKeyIsPuae(key)) {
            continue;
        }
        libretro_host_setCoreOption(key, value);
    }
    fclose(f);
    return true;
}
