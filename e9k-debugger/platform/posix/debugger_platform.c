/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zlib.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#endif

#include "debugger.h"
#include "tinyfiledialogs.h"
#include "ui_test.h"
#include "strutil.h"

static const char debugger_testConfigName[] = ".e9k-debugger.cfg";
static const char debugger_testTempConfigPrefix[] = "e9k-debugger-test-";
static const char debugger_testTempConfigSuffix[] = ".cfg";
static int debugger_platform_tinyfdInitialized = 0;

#ifdef __linux__
static int
debugger_platform_zenityRun(const char *command, char *outPath, size_t outCap)
{
    FILE *pipe;
    int status;

    if (!command || !*command || !outPath || outCap == 0) {
        return 0;
    }
    outPath[0] = '\0';
    pipe = popen(command, "r");
    if (!pipe) {
        return 0;
    }
    if (fgets(outPath, (int)outCap, pipe)) {
        size_t len = strlen(outPath);
        while (len > 0 && (outPath[len - 1] == '\n' || outPath[len - 1] == '\r')) {
            outPath[--len] = '\0';
        }
    }
    status = pclose(pipe);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
        outPath[0] = '\0';
        return 0;
    }
    return 1;
}
#endif

static void
debugger_platform_tinyfdInit(void)
{
    if (debugger_platform_tinyfdInitialized) {
        return;
    }
    debugger_platform_tinyfdInitialized = 1;
    tinyfd_allowCursesDialogs = 0;
    tinyfd_forceConsole = 0;

    const char *verboseEnv = getenv("E9K_TINYFD_VERBOSE");
    if (verboseEnv && *verboseEnv && strcmp(verboseEnv, "0") != 0) {
        tinyfd_verbose = 1;
        tinyfd_silent = 0;
    }
}

static int
debugger_platform_configExists(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISREG(st.st_mode) ? 1 : 0;
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
    uLongf destSize = 0;

    if (!dest || !inOutDestSize || !source) {
        return Z_BUF_ERROR;
    }

    destSize = (uLongf)*inOutDestSize;
    int zResult = uncompress((Bytef *)dest,
                             &destSize,
                             (const Bytef *)source,
                             (uLong)sourceSize);
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
        out[pos++] = '/';
    }
    memcpy(out + pos, name, nlen);
    out[pos + nlen] = '\0';
    return 1;
}

int
debugger_platform_pathIsAbsolute(const char *path)
{
    if (!path || !path[0]) {
        return 0;
    }
    return path[0] == '/' ? 1 : 0;
}

int
debugger_platform_expandHomePath(char *out, size_t cap, const char *path)
{
    if (!out || cap == 0) {
        return 0;
    }
    if (!path || !path[0]) {
        out[0] = '\0';
        return 0;
    }
    if (path[0] != '~' || (path[1] != '/' && path[1] != '\0')) {
        strutil_strlcpy(out, cap, path);
        return 1;
    }
    const char *home = getenv("HOME");
    if (!home || !home[0]) {
        strutil_strlcpy(out, cap, path);
        return 0;
    }
    strutil_join2Trunc(out, cap, home, path + 1);
    return 1;
}

int
debugger_platform_scanFolder(const char *folder, int (*cb)(const char *path, void *user), void *user)
{
    if (!folder || !*folder || !cb) {
        return 0;
    }
    DIR *dir = opendir(folder);
    if (!dir) {
        return 0;
    }
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        char full[PATH_MAX];
        if (!debugger_platform_pathJoin(full, sizeof(full), folder, ent->d_name)) {
            continue;
        }
        if (!cb(full, user)) {
            closedir(dir);
            return 0;
        }
    }
    closedir(dir);
    return 1;
}

int
debugger_platform_caseInsensitivePaths(void)
{
    return 0;
}

char
debugger_platform_preferredPathSeparator(void)
{
    return '/';
}

void
debugger_platform_normalizePathSeparators(char *path)
{
    (void)path;
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
    if (args) {
        if (suppressStderr) {
            snprintf(out, cap, "%s %s '%s' 2>/dev/null", toolPath, args, targetPath);
        } else {
            snprintf(out, cap, "%s %s '%s'", toolPath, args, targetPath);
        }
    } else {
        if (suppressStderr) {
            snprintf(out, cap, "%s '%s' 2>/dev/null", toolPath, targetPath);
        } else {
            snprintf(out, cap, "%s '%s'", toolPath, targetPath);
        }
    }
    return out[0] != '\0';
}

int
debugger_platform_finalizeToolBinary(char *toolPath, size_t cap)
{
    (void)cap;
    if (!toolPath || !*toolPath) {
        return 0;
    }
    return 1;
}

int
debugger_platform_getExeDir(char *out, size_t cap)
{
    size_t len;
    char path[PATH_MAX];
    char resolvedPath[PATH_MAX];
    const char *fullPath;

    if (!out || cap == 0) {
        return 0;
    }

    out[0] = '\0';
    fullPath = NULL;

    path[0] = '\0';
    resolvedPath[0] = '\0';

#ifdef __APPLE__
    uint32_t sz = (uint32_t)sizeof(path);
    if (_NSGetExecutablePath(path, &sz) == 0) {
        const char *resolved = realpath(path, resolvedPath);
        fullPath = resolved ? resolved : path;
    }
#else
    ssize_t count = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (count <= 0) {
        count = readlink("/proc/curproc/file", path, sizeof(path) - 1);
    }
    if (count <= 0) {
        count = readlink("/proc/curproc/exe", path, sizeof(path) - 1);
    }
    if (count > 0) {
        path[count] = '\0';
        fullPath = path;
    }
#endif

    if (!fullPath || !*fullPath) {
        if (debugger.argv0[0]) {
            const char *resolved = realpath(debugger.argv0, resolvedPath);
            if (resolved && *resolved) {
                fullPath = resolved;
            } else if (debugger.argv0[0] == '/') {
                fullPath = debugger.argv0;
            } else {
                char cwd[PATH_MAX];
                if (getcwd(cwd, sizeof(cwd))) {
                    strutil_pathJoinTrunc(path, sizeof(path), cwd, debugger.argv0);
                    fullPath = path;
                }
            }
        }
    }

    if (!fullPath || !*fullPath) {
        return 0;
    }

    len = strlen(fullPath);
    while (len > 0 && fullPath[len - 1] != '/') {
        len--;
    }
    if (len == 0) {
        return 0;
    }
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(out, fullPath, len);
    out[len] = '\0';
    return 1;
}

int
debugger_platform_isExecutableFile(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    if (!S_ISREG(st.st_mode)) {
        return 0;
    }
    if (access(path, X_OK) != 0) {
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
    const char *home = getenv("HOME");
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
    if (!getcwd(out, cap)) {
        out[0] = '\0';
        return 0;
    }
    return 1;
}

char
debugger_platform_pathListSeparator(void)
{
    return ':';
}

int
debugger_platform_makeDir(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    if (mkdir(path, 0755) == 0) {
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
    return rmdir(path) == 0 ? 1 : 0;
}

int
debugger_platform_makeTempFilePath(char *out, size_t cap, const char *prefix, const char *suffix)
{
    if (!out || cap == 0) {
        return 0;
    }
    const char *tmpDir = getenv("TMPDIR");
    if (!tmpDir || !*tmpDir) {
        tmpDir = "/tmp";
    }
    const char *filePrefix = (prefix && *prefix) ? prefix : "e9k";
    const char *fileSuffix = suffix ? suffix : "";
    char templatePath[PATH_MAX];
    int written = snprintf(templatePath, sizeof(templatePath), "%s/%s-XXXXXX", tmpDir, filePrefix);
    if (written <= 0 || (size_t)written >= sizeof(templatePath)) {
        return 0;
    }
    int fd = mkstemp(templatePath);
    if (fd < 0) {
        return 0;
    }
    close(fd);
    if (!fileSuffix[0]) {
        written = snprintf(out, cap, "%s", templatePath);
        if (written <= 0 || (size_t)written >= cap) {
            unlink(templatePath);
            return 0;
        }
        return 1;
    }

    char finalPath[PATH_MAX];
    written = snprintf(finalPath, sizeof(finalPath), "%s%s", templatePath, fileSuffix);
    if (written <= 0 || (size_t)written >= sizeof(finalPath)) {
        unlink(templatePath);
        return 0;
    }
    if (rename(templatePath, finalPath) != 0) {
        unlink(templatePath);
        return 0;
    }
    written = snprintf(out, cap, "%s", finalPath);
    if (written <= 0 || (size_t)written >= cap) {
        unlink(finalPath);
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
    return rename(srcPath, dstPath) == 0 ? 1 : 0;
}

int
debugger_platform_glCompositeNeedsOpenGLHint(void)
{
#ifdef __APPLE__
    int vmmPresent = 0;
    size_t vmmSize = sizeof(vmmPresent);
    if (sysctlbyname("kern.hv_vmm_present", &vmmPresent, &vmmSize, NULL, 0) == 0 &&
        vmmSize == sizeof(vmmPresent) && vmmPresent != 0) {
        return 0;
    }

    size_t featSize = 0;
    if (sysctlbyname("machdep.cpu.features", NULL, &featSize, NULL, 0) == 0 && featSize > 1) {
        char *features = (char *)malloc(featSize);
        if (features) {
            if (sysctlbyname("machdep.cpu.features", features, &featSize, NULL, 0) == 0 &&
                strstr(features, "VMM") != NULL) {
                free(features);
                return 0;
            }
            free(features);
        }
    }
    return 1;
#else
    return 0;
#endif
}

const char *
debugger_platform_windowIconAssetPath(void)
{
#ifdef __APPLE__
    return "assets/icons/osx/engine9000.png";
#else
    return "assets/icons/osx/engine9000.png";
#endif
}

const char *
debugger_platform_selectFolderDialog(const char *title, const char *defaultPath)
{
#ifdef __linux__
    static char zenityPath[PATH_MAX];
    if (debugger_platform_zenityRun("zenity --file-selection --directory 2>/dev/null", zenityPath, sizeof(zenityPath))) {
        return zenityPath[0] ? zenityPath : NULL;
    }
#endif
    debugger_platform_tinyfdInit();
    return tinyfd_selectFolderDialog(title, defaultPath);
}

const char *
debugger_platform_openFileDialog(const char *title,
                                 const char *defaultPathAndFile,
                                 int numOfFilterPatterns,
                                 const char * const *filterPatterns,
                                 const char *singleFilterDescription,
                                 int allowMultipleSelects)
{
#ifdef __linux__
    static char zenityPath[PATH_MAX];
    (void)numOfFilterPatterns;
    (void)filterPatterns;
    (void)singleFilterDescription;
    (void)allowMultipleSelects;
    if (debugger_platform_zenityRun("zenity --file-selection 2>/dev/null", zenityPath, sizeof(zenityPath))) {
        return zenityPath[0] ? zenityPath : NULL;
    }
#endif
    debugger_platform_tinyfdInit();
    return tinyfd_openFileDialog(title,
                                 defaultPathAndFile,
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
#ifdef __linux__
    static char zenityPath[PATH_MAX];
    (void)numOfFilterPatterns;
    (void)filterPatterns;
    (void)singleFilterDescription;
    if (debugger_platform_zenityRun("zenity --file-selection --save --confirm-overwrite 2>/dev/null", zenityPath, sizeof(zenityPath))) {
        return zenityPath[0] ? zenityPath : NULL;
    }
#endif
    debugger_platform_tinyfdInit();
    return tinyfd_saveFileDialog(title,
                                 defaultPathAndFile,
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
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

void
debugger_platform_closeSharedLibrary(void *handle)
{
    if (!handle) {
        return;
    }
    dlclose(handle);
}

void *
debugger_platform_loadSharedSymbol(void *handle, const char *name)
{
    if (!handle || !name || !*name) {
        return NULL;
    }
    dlerror();
    return dlsym(handle, name);
}

ssize_t
debugger_platform_getline(char **lineptr, size_t *n, FILE *stream)
{
    return getline(lineptr, n, stream);
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
    const char *home = getenv("HOME");
    if (!home || !*home) {
        return NULL;
    }
    snprintf(pathbuf, sizeof(pathbuf), "%s/.e9k-debugger.cfg", home);
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
    const char *tmpDir = getenv("TMPDIR");
    if (!tmpDir || !*tmpDir) {
        tmpDir = "/tmp";
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
