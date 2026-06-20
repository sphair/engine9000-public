/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include <io.h>
#include <windows.h>
#include <errno.h>
#include <zlib.h>

#include "platform/w64/debugger_platform.h"
#include "debugger.h"
#include "strutil.h"
#include "tinyfiledialogs.h"
#include "ui_test.h"

static const char debugger_testConfigName[] = ".e9k-debugger.cfg";
static const char debugger_testTempConfigPrefix[] = "e9k-debugger-test-";
static const char debugger_testTempConfigSuffix[] = ".cfg";

static int
debugger_platform_configExists(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return 0;
    }
    if ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return 0;
    }
    return 1;
}

static int
debugger_platform_pathIsDirectory(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return 0;
    }
    return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0 ? 1 : 0;
}

static const char *
debugger_platform_dialogDefaultPath(const char *path, char *out, size_t cap)
{
    DWORD fullPathLen = 0;
    char fullPath[PATH_MAX];
    const char *sourcePath = path;
    size_t len = 0;

    if (!path || !*path) {
        return path;
    }
    if (!out || cap < 4) {
        return path;
    }

    fullPathLen = GetFullPathNameA(path, (DWORD)sizeof(fullPath), fullPath, NULL);
    if (fullPathLen > 0 && fullPathLen < sizeof(fullPath)) {
        sourcePath = fullPath;
    }
    len = strlen(sourcePath);
    if (len + 1 > cap) {
        return path;
    }
    memcpy(out, sourcePath, len + 1);
    if (!debugger_platform_pathIsDirectory(out)) {
        return out;
    }
    if (out[len - 1] == '/' || out[len - 1] == '\\') {
        return out;
    }
    if (len + 2 > cap) {
        return out;
    }
    out[len++] = '\\';
    out[len] = '\0';
    return out;
}

static uint64_t
debugger_platform_hashPath(const char *path)
{
    uint64_t hash = 1469598103934665603ull;
    if (!path) {
        return hash;
    }
    while (*path) {
        hash ^= (uint8_t)(*path++);
        hash *= 1099511628211ull;
    }
    return hash;
}

int
debugger_platform_uncompressBuffer(uint8_t *dest, size_t *inOutDestSize, const uint8_t *source, size_t sourceSize)
{
    uint32_t destSize = 0;

    if (!dest || !inOutDestSize || !source) {
        return Z_BUF_ERROR;
    }
    if (*inOutDestSize > UINT32_MAX || sourceSize > UINT32_MAX) {
        return Z_BUF_ERROR;
    }

    destSize = (uint32_t)*inOutDestSize;
    int zResult = uncompress((unsigned char *)dest,
                             &destSize,
                             (const unsigned char *)source,
                             (uint32_t)sourceSize);
    *inOutDestSize = (size_t)destSize;
    return zResult;
}

int
debugger_platform_pathJoin(char *out, size_t cap, const char *dir, const char *name)
{
    if (!out || cap == 0 || !dir || !*dir || !name || !*name) {
        return 0;
    }
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    int need_sep = (dlen > 0 && dir[dlen - 1] != '/' && dir[dlen - 1] != '\\');
    size_t total = dlen + (need_sep ? 1 : 0) + nlen;
    if (total + 1 > cap) {
        return 0;
    }
    memcpy(out, dir, dlen);
    size_t pos = dlen;
    if (need_sep) {
        out[pos++] = '\\';
    }
    memcpy(out + pos, name, nlen);
    out[pos + nlen] = '\0';
    debugger_platform_normalizePathSeparators(out);
    return 1;
}

int
debugger_platform_pathIsAbsolute(const char *path)
{
    if (!path || !path[0]) {
        return 0;
    }
    if (path[0] == '\\' || path[0] == '/') {
        return 1;
    }
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':' &&
        (path[2] == '\\' || path[2] == '/')) {
        return 1;
    }
    return 0;
}

int
debugger_platform_expandHomePath(char *out, size_t cap, const char *path)
{
    char home[PATH_MAX];

    if (!out || cap == 0) {
        return 0;
    }
    if (!path || !path[0]) {
        out[0] = '\0';
        return 0;
    }
    if (path[0] != '~' || (path[1] != '\\' && path[1] != '/' && path[1] != '\0')) {
        strutil_strlcpy(out, cap, path);
        debugger_platform_normalizePathSeparators(out);
        return 1;
    }
    home[0] = '\0';
    if (!debugger_platform_getHomeDir(home, sizeof(home)) || !home[0]) {
        strutil_strlcpy(out, cap, path);
        debugger_platform_normalizePathSeparators(out);
        return 0;
    }
    strutil_join2Trunc(out, cap, home, path + 1);
    debugger_platform_normalizePathSeparators(out);
    return 1;
}

int
debugger_platform_scanFolder(const char *folder, int (*cb)(const char *path, void *user), void *user)
{
    if (!folder || !*folder || !cb) {
        return 0;
    }
    char pattern[PATH_MAX];
    if (!debugger_platform_pathJoin(pattern, sizeof(pattern), folder, "*")) {
        return 0;
    }
    WIN32_FIND_DATAA data;
    HANDLE h = FindFirstFileA(pattern, &data);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        char full[PATH_MAX];
        if (!debugger_platform_pathJoin(full, sizeof(full), folder, data.cFileName)) {
            continue;
        }
        if (!cb(full, user)) {
            FindClose(h);
            return 0;
        }
    } while (FindNextFileA(h, &data));
    FindClose(h);
    return 1;
}

int
debugger_platform_caseInsensitivePaths(void)
{
    return 1;
}

char
debugger_platform_preferredPathSeparator(void)
{
    return '\\';
}

void
debugger_platform_normalizePathSeparators(char *path)
{
    if (!path) {
        return;
    }
    for (size_t i = 0; path[i]; ++i) {
        if (path[i] == '/') {
            path[i] = '\\';
        }
    }
}

int
debugger_platform_formatToolCommand(char *out,
                                    size_t cap,
                                    const char *toolPath,
                                    const char *toolArgs,
                                    const char *targetPath,
                                    int suppressStderr)
{
    if (!out || cap == 0 || !toolPath || !*toolPath || !targetPath || !*targetPath) {
        return 0;
    }
    out[0] = '\0';
    const char *args = (toolArgs && *toolArgs) ? toolArgs : NULL;
    int toolNeedsQuotes = (strchr(toolPath, ' ') != NULL || strchr(toolPath, '\t') != NULL) ? 1 : 0;
    int isTraceTool = 0;
    if (strstr(toolPath, "objdump") != NULL || strstr(toolPath, "addr2line") != NULL) {
        isTraceTool = 1;
    }
    if (args) {
        if (suppressStderr) {
            if (isTraceTool) {
                if (toolNeedsQuotes) {
                    snprintf(out, cap, "\"%s\" %s \"%s\"", toolPath, args, targetPath);
                } else {
                    snprintf(out, cap, "%s %s \"%s\"", toolPath, args, targetPath);
                }
            } else {
                if (toolNeedsQuotes) {
                    snprintf(out, cap, "\"%s\" %s \"%s\" 2>nul", toolPath, args, targetPath);
                } else {
                    snprintf(out, cap, "%s %s \"%s\" 2>nul", toolPath, args, targetPath);
                }
            }
        } else {
            if (toolNeedsQuotes) {
                snprintf(out, cap, "\"%s\" %s \"%s\"", toolPath, args, targetPath);
            } else {
                snprintf(out, cap, "%s %s \"%s\"", toolPath, args, targetPath);
            }
        }
    } else {
        if (suppressStderr) {
            if (isTraceTool) {
                if (toolNeedsQuotes) {
                    snprintf(out, cap, "\"%s\" \"%s\"", toolPath, targetPath);
                } else {
                    snprintf(out, cap, "%s \"%s\"", toolPath, targetPath);
                }
            } else {
                if (toolNeedsQuotes) {
                    snprintf(out, cap, "\"%s\" \"%s\" 2>nul", toolPath, targetPath);
                } else {
                    snprintf(out, cap, "%s \"%s\" 2>nul", toolPath, targetPath);
                }
            }
        } else {
            if (toolNeedsQuotes) {
                snprintf(out, cap, "\"%s\" \"%s\"", toolPath, targetPath);
            } else {
                snprintf(out, cap, "%s \"%s\"", toolPath, targetPath);
            }
        }
    }
    return out[0] != '\0';
}

int
debugger_platform_finalizeToolBinary(char *toolPath, size_t cap)
{
    if (!toolPath || !*toolPath || cap == 0) {
        return 0;
    }

    for (char *p = toolPath; *p; ++p) {
        if (*p == '/') {
            *p = '\\';
        }
    }
    if (toolPath[0] == '.' && toolPath[1] == '\\') {
        // already normalized
    } else if (toolPath[0] == '.' && toolPath[1] == '/' && toolPath[2] != '\0') {
        toolPath[1] = '\\';
    }

    size_t len = strlen(toolPath);
    if (len >= 4) {
        const char *suffix = toolPath + len - 4;
        if (suffix[0] == '.' &&
            (suffix[1] == 'e' || suffix[1] == 'E') &&
            (suffix[2] == 'x' || suffix[2] == 'X') &&
            (suffix[3] == 'e' || suffix[3] == 'E')) {
            return 1;
        }
    }
    if (len + 4 >= cap) {
        toolPath[cap - 1] = '\0';
        return 0;
    }
    memcpy(toolPath + len, ".exe", 5);
    return 1;
}

int
debugger_platform_getExeDir(char *out, size_t cap)
{
    return w64_getExeDir(out, cap);
}

int
debugger_platform_isExecutableFile(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return 0;
    }
    if ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return 0;
    }
    if (_access(path, 0) != 0) {
        return 0;
    }
    return 1;
}

int
debugger_platform_getHomeDir(char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    const char *home = getenv("USERPROFILE");
    if (!home || !*home) {
        home = getenv("APPDATA");
    }
    if (!home || !*home) {
        out[0] = '\0';
        return 0;
    }
    size_t len = strlen(home);
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(out, home, len);
    out[len] = '\0';
    return 1;
}

int
debugger_platform_getCurrentDir(char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    if (!_getcwd(out, (int)cap)) {
        out[0] = '\0';
        return 0;
    }
    return 1;
}

char
debugger_platform_pathListSeparator(void)
{
    return ';';
}

int
debugger_platform_makeDir(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    if (_mkdir(path) == 0) {
        return 1;
    }
    return errno == EEXIST ? 1 : 0;
}

int
debugger_platform_removeDir(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    return RemoveDirectoryA(path) ? 1 : 0;
}

int
debugger_platform_makeTempFilePath(char *out, size_t cap, const char *prefix, const char *suffix)
{
    if (!out || cap == 0) {
        return 0;
    }
    char tempDir[MAX_PATH];
    DWORD dirLen = GetTempPathA((DWORD)sizeof(tempDir), tempDir);
    if (dirLen == 0 || dirLen >= sizeof(tempDir)) {
        return 0;
    }
    char filePath[MAX_PATH];
    const char *filePrefix = (prefix && *prefix) ? prefix : "e9k";
    char shortPrefix[4] = "e9k";
    strutil_strlcpy(shortPrefix, sizeof(shortPrefix), filePrefix);
    if (GetTempFileNameA(tempDir, shortPrefix, 0, filePath) == 0) {
        return 0;
    }
    const char *fileSuffix = suffix ? suffix : "";
    if (!*fileSuffix) {
        int written = snprintf(out, cap, "%s", filePath);
        return (written > 0 && (size_t)written < cap) ? 1 : 0;
    }
    int written = snprintf(out, cap, "%s%s", filePath, fileSuffix);
    if (written <= 0 || (size_t)written >= cap) {
        DeleteFileA(filePath);
        return 0;
    }
    if (!MoveFileExA(filePath, out, MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileA(filePath);
        return 0;
    }
    return 1;
}

int
debugger_platform_replaceFile(const char *srcPath, const char *dstPath)
{
    if (!srcPath || !*srcPath || !dstPath || !*dstPath) {
        return 0;
    }
    if (!MoveFileExA(srcPath, dstPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        return 0;
    }
    return 1;
}

int
debugger_platform_glCompositeNeedsOpenGLHint(void)
{
    return 1;
}

const char *
debugger_platform_windowIconAssetPath(void)
{
    return "assets/icons/w64/engine9000.ico";
}

const char *
debugger_platform_selectFolderDialog(const char *title, const char *defaultPath)
{
    char dialogPath[PATH_MAX];

    return tinyfd_selectFolderDialog(title,
                                     debugger_platform_dialogDefaultPath(defaultPath,
                                                                         dialogPath,
                                                                         sizeof(dialogPath)));
}

const char *
debugger_platform_openFileDialog(const char *title,
                                 const char *defaultPathAndFile,
                                 int numOfFilterPatterns,
                                 const char * const *filterPatterns,
                                 const char *singleFilterDescription,
                                 int allowMultipleSelects)
{
    char dialogPath[PATH_MAX];
    return tinyfd_openFileDialog(title,
                                 debugger_platform_dialogDefaultPath(defaultPathAndFile,
                                                                     dialogPath,
                                                                     sizeof(dialogPath)),
                                 numOfFilterPatterns,
                                 filterPatterns,
                                 singleFilterDescription,
                                 allowMultipleSelects);
}

const char *
debugger_platform_saveFileDialog(const char *title,
                                 const char *defaultPathAndFile,
                                 int numOfFilterPatterns,
                                 const char * const *filterPatterns,
                                 const char *singleFilterDescription)
{
    char dialogPath[PATH_MAX];
    return tinyfd_saveFileDialog(title,
                                 debugger_platform_dialogDefaultPath(defaultPathAndFile,
                                                                     dialogPath,
                                                                     sizeof(dialogPath)),
                                 numOfFilterPatterns,
                                 filterPatterns,
                                 singleFilterDescription);
}

void *
debugger_platform_loadSharedLibrary(const char *path)
{
    if (!path || !*path) {
        return NULL;
    }
    return (void *)LoadLibraryA(path);
}

void
debugger_platform_closeSharedLibrary(void *handle)
{
    if (!handle) {
        return;
    }
    FreeLibrary((HMODULE)handle);
}

void *
debugger_platform_loadSharedSymbol(void *handle, const char *name)
{
    if (!handle || !name || !*name) {
        return NULL;
    }
    FARPROC symbol = GetProcAddress((HMODULE)handle, name);
    return (void *)symbol;
}

ssize_t
debugger_platform_getline(char **lineptr, size_t *n, FILE *stream)
{
    return w64_getline(lineptr, n, stream);
}

char *
debugger_configPath(void)
{
    static char pathbuf[1024];
    ui_test_mode_t mode = ui_test_getMode();
    const char *testTempPath = debugger_configTempPath();
    if (mode != UI_TEST_MODE_NONE && debugger_getLoadTestTempConfig() && debugger_platform_configExists(testTempPath)) {
        return (char *)testTempPath;
    }
    if (mode == UI_TEST_MODE_RECORD || mode == UI_TEST_MODE_COMPARE || mode == UI_TEST_MODE_REMAKE) {
        const char *folder = ui_test_getFolder();
        if (folder && *folder) {
            if (debugger_platform_pathJoin(pathbuf, sizeof(pathbuf), folder, debugger_testConfigName)) {
                return pathbuf;
            }
        }
    }
    return debugger_defaultConfigPath();
}

char *
debugger_defaultConfigPath(void)
{
    static char pathbuf[1024];
    const char *home = getenv("APPDATA");
    if (!home || !*home) {
        home = getenv("USERPROFILE");
    }
    if (!home || !*home) {
        return NULL;
    }
    snprintf(pathbuf, sizeof(pathbuf), "%s\\e9k-debugger.cfg", home);
    return pathbuf;
}

char *
debugger_configTempPath(void)
{
    static char pathbuf[1024];
    static char namebuf[96];
    const char *folder = ui_test_getFolder();
    if (!folder || !*folder) {
        return NULL;
    }
    const char *tmpDir = getenv("TEMP");
    if (!tmpDir || !*tmpDir) {
        tmpDir = getenv("TMP");
    }
    if (!tmpDir || !*tmpDir) {
        tmpDir = getenv("APPDATA");
    }
    if (!tmpDir || !*tmpDir) {
        return NULL;
    }
    uint64_t folderHash = debugger_platform_hashPath(folder);
    snprintf(namebuf, sizeof(namebuf), "%s%016llx%s",
             debugger_testTempConfigPrefix,
             (unsigned long long)folderHash,
             debugger_testTempConfigSuffix);
    if (!debugger_platform_pathJoin(pathbuf, sizeof(pathbuf), tmpDir, namebuf)) {
        return NULL;
    }
    return pathbuf;
}


ssize_t
w64_getline(char **lineptr, size_t *n, FILE *stream)
{
    if (!lineptr || !n || !stream) {
        errno = EINVAL;
        return -1;
    }
    if (!*lineptr || *n == 0) {
        *n = 128;
        *lineptr = (char*)malloc(*n);
        if (!*lineptr) {
            errno = ENOMEM;
            return -1;
        }
    }
    size_t pos = 0;
    int ch;
    while ((ch = fgetc(stream)) != EOF) {
        if (pos + 1 >= *n) {
            size_t new_cap = (*n) * 2;
            char *tmp = (char*)realloc(*lineptr, new_cap);
            if (!tmp) {
                errno = ENOMEM;
                return -1;
            }
            *lineptr = tmp;
            *n = new_cap;
        }
        (*lineptr)[pos++] = (char)ch;
        if (ch == '\n') {
            break;
        }
    }
    if (pos == 0 && ch == EOF) {
        return -1;
    }
    (*lineptr)[pos] = '\0';
    return (ssize_t)pos;
}

int
w64_getExeDir(char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    char path[PATH_MAX];
    DWORD len = GetModuleFileNameA(NULL, path, (DWORD)sizeof(path));
    if (len == 0 || len >= (DWORD)sizeof(path)) {
        return 0;
    }
    size_t slen = (size_t)len;
    while (slen > 0 && path[slen - 1] != '\\' && path[slen - 1] != '/') {
        slen--;
    }
    if (slen == 0) {
        return 0;
    }
    if (slen >= cap) {
        slen = cap - 1;
    }
    memcpy(out, path, slen);
    out[slen] = '\0';
    return 1;
}
