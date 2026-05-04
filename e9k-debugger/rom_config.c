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
#include <sys/stat.h>

#include "rom_config.h"
#include "alloc.h"
#include "breakpoints.h"
#include "debugger.h"
#include "debugger_input_bindings.h"
#include "json.h"
#include "libretro_host.h"
#include "machine.h"
#include "protect.h"
#include "trainer.h"
#include "ui_test.h"
#include "smoke_test.h"
#include "strutil.h"

typedef struct rom_config_bp_entry {
    uint32_t addr;
    int enabled;
} rom_config_bp_entry_t;

typedef struct rom_config_protect_entry {
    uint32_t addr;
    uint32_t sizeBits;
    uint32_t mode;
    uint32_t value;
    int enabled;
} rom_config_protect_entry_t;

typedef struct rom_config_input_binding_entry {
    char *key;
    char *value;
} rom_config_input_binding_entry_t;

typedef struct rom_config_data {
    uint64_t romChecksum;
    rom_config_bp_entry_t *breakpoints;
    size_t breakpointCount;
    int breakpointsTextRelative;
    rom_config_protect_entry_t *protects;
    size_t protectCount;
    char elfPath[PATH_MAX];
    char sourceDir[PATH_MAX];
    char toolchainPrefix[PATH_MAX];
    int hasElf;
    int hasSource;
    int hasToolchain;
    rom_config_input_binding_entry_t *inputBindings;
    size_t inputBindingCount;
    rom_config_input_binding_entry_t *targetOptions;
    size_t targetOptionCount;
} rom_config_data_t;

char rom_config_activeElfPath[PATH_MAX];
char rom_config_activeSourceDir[PATH_MAX];
char rom_config_activeToolchainPrefix[PATH_MAX];
int rom_config_activeInit = 0;
static rom_config_input_binding_entry_t *rom_config_activeInputBindings = NULL;
static size_t rom_config_activeInputBindingCount = 0;

static void
rom_config_setActiveDefaultsFromCurrentSystem(void);

static void
rom_config_freeInputBindingEntries(rom_config_input_binding_entry_t **entriesPtr, size_t *countPtr)
{
    if (!entriesPtr || !countPtr) {
        return;
    }
    rom_config_input_binding_entry_t *entries = *entriesPtr;
    size_t count = *countPtr;
    if (entries) {
        for (size_t i = 0; i < count; ++i) {
            alloc_free(entries[i].key);
            alloc_free(entries[i].value);
        }
        alloc_free(entries);
    }
    *entriesPtr = NULL;
    *countPtr = 0;
}

static const char *
rom_config_findInputBindingValue(const rom_config_input_binding_entry_t *entries, size_t count, const char *key)
{
    if (!entries || !key || !*key) {
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        if (entries[i].key && strcmp(entries[i].key, key) == 0) {
            return entries[i].value;
        }
    }
    return NULL;
}

static int
rom_config_setInputBindingValueInList(rom_config_input_binding_entry_t **entriesPtr,
                                      size_t *countPtr,
                                      const char *key,
                                      const char *value)
{
    if (!entriesPtr || !countPtr || !key || !*key) {
        return 0;
    }

    rom_config_input_binding_entry_t *entries = *entriesPtr;
    size_t count = *countPtr;

    for (size_t i = 0; i < count; ++i) {
        if (!entries[i].key || strcmp(entries[i].key, key) != 0) {
            continue;
        }
        if (!value || !*value) {
            alloc_free(entries[i].key);
            alloc_free(entries[i].value);
            for (size_t j = i + 1; j < count; ++j) {
                entries[j - 1] = entries[j];
            }
            count--;
            if (count == 0) {
                alloc_free(entries);
                entries = NULL;
            } else {
                rom_config_input_binding_entry_t *shrunk =
                    (rom_config_input_binding_entry_t *)alloc_realloc(entries, count * sizeof(*entries));
                if (shrunk) {
                    entries = shrunk;
                }
            }
            *entriesPtr = entries;
            *countPtr = count;
            return 1;
        }
        alloc_free(entries[i].value);
        entries[i].value = alloc_strdup(value);
        *entriesPtr = entries;
        *countPtr = count;
        return 1;
    }

    if (!value || !*value) {
        return 1;
    }

    rom_config_input_binding_entry_t *next =
        (rom_config_input_binding_entry_t *)alloc_realloc(entries, (count + 1) * sizeof(*entries));
    if (!next) {
        return 0;
    }
    entries = next;
    memset(&entries[count], 0, sizeof(entries[count]));
    entries[count].key = alloc_strdup(key);
    entries[count].value = alloc_strdup(value);
    *entriesPtr = entries;
    *countPtr = count + 1;
    return 1;
}

static void
rom_config_applyParsedInputBindingsToActive(const rom_config_data_t *data)
{
    rom_config_clearActiveInputBindings();
    if (!data || !data->inputBindings || data->inputBindingCount == 0) {
        return;
    }
    for (size_t i = 0; i < data->inputBindingCount; ++i) {
        const char *key = data->inputBindings[i].key;
        const char *value = data->inputBindings[i].value;
        if (!key || !*key || !value) {
            continue;
        }
        rom_config_setActiveInputBindingValue(key, value);
    }
}

const char *
rom_config_getActiveInputBindingValue(const char *key)
{
    return rom_config_findInputBindingValue(rom_config_activeInputBindings,
                                            rom_config_activeInputBindingCount,
                                            key);
}

void
rom_config_setActiveInputBindingValue(const char *key, const char *value)
{
    if (!debugger_input_bindings_isOptionKey(key)) {
        return;
    }
    if (!rom_config_activeInit) {
        rom_config_setActiveDefaultsFromCurrentSystem();
    }
    (void)rom_config_setInputBindingValueInList(&rom_config_activeInputBindings,
                                                &rom_config_activeInputBindingCount,
                                                key,
                                                value);
}

void
rom_config_clearActiveInputBindings(void)
{
    rom_config_freeInputBindingEntries(&rom_config_activeInputBindings,
                                       &rom_config_activeInputBindingCount);
}

static void
rom_config_clearActiveTargetCustomOptions(target_iface_t *targetIface)
{
    if (!targetIface || !targetIface->romConfigClearActiveCustomOptions) {
        return;
    }
    targetIface->romConfigClearActiveCustomOptions();
}

static void
rom_config_applyParsedTargetOptionsToActive(const rom_config_data_t *data, target_iface_t *targetIface)
{
    rom_config_clearActiveTargetCustomOptions(targetIface);
    if (!data || !targetIface || !targetIface->romConfigSetActiveCustomOptionValue ||
        !data->targetOptions || data->targetOptionCount == 0) {
        return;
    }
    for (size_t i = 0; i < data->targetOptionCount; ++i) {
        const char *key = data->targetOptions[i].key;
        const char *value = data->targetOptions[i].value;
        if (!key || !*key || !value) {
            continue;
        }
        targetIface->romConfigSetActiveCustomOptionValue(key, value);
    }
}

static void
rom_config_collectActiveTargetOptions(rom_config_data_t *data, target_iface_t *targetIface)
{
    if (!data || !targetIface || !targetIface->romConfigCustomOptionCount ||
        !targetIface->romConfigCustomOptionKeyAt || !targetIface->romConfigGetActiveCustomOptionValue) {
        return;
    }
    for (size_t i = 0; i < targetIface->romConfigCustomOptionCount(); ++i) {
        const char *key = targetIface->romConfigCustomOptionKeyAt(i);
        const char *value = key ? targetIface->romConfigGetActiveCustomOptionValue(key) : NULL;
        if (!key || !*key || !value || !*value) {
            continue;
        }
        (void)rom_config_setInputBindingValueInList(&data->targetOptions,
                                                    &data->targetOptionCount,
                                                    key,
                                                    value);
    }
}

static const char *
rom_config_basename(const char *path)
{
    if (!path || !*path) {
        return NULL;
    }
    const char *slash = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    const char *best = slash > back ? slash : back;
    return best ? best + 1 : path;
}

static int
rom_config_pathExistsFile(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    struct stat sb;
    if (stat(path, &sb) != 0) {
        return 0;
    }
    return S_ISREG(sb.st_mode) ? 1 : 0;
}

static int
rom_config_pathExistsDir(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    struct stat sb;
    if (stat(path, &sb) != 0) {
        return 0;
    }
    return S_ISDIR(sb.st_mode) ? 1 : 0;
}

static const char *
rom_config_bootSaveDir(void)
{
  if (target->bootSaveDir[0]) {
    return target->bootSaveDir;
  }
  
  if (target->bootSystemDir[0]) {
    return target->bootSystemDir;
  }
  return NULL;
}

static int
rom_config_copyFile(const char *srcPath, const char *dstPath)
{
    if (!srcPath || !*srcPath || !dstPath || !*dstPath) {
        return 0;
    }
    FILE *src = fopen(srcPath, "rb");
    if (!src) {
        return 0;
    }
    FILE *dst = fopen(dstPath, "wb");
    if (!dst) {
        fclose(src);
        return 0;
    }

    uint8_t buf[16384];
    int ok = 1;
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) {
            ok = 0;
            break;
        }
    }
    if (ferror(src)) {
        ok = 0;
    }
    if (fclose(dst) != 0) {
        ok = 0;
    }
    fclose(src);
    return ok ? 1 : 0;
}

static const char *
rom_config_saveDir(const char *fallbackSystemDir)
{
    const char *hostSaveDir = libretro_host_getSaveDir();
    if (hostSaveDir && *hostSaveDir) {
        return hostSaveDir;
    }
    if (debugger.libretro.saveDir[0]) {
        return debugger.libretro.saveDir;
    }
    if (fallbackSystemDir && *fallbackSystemDir) {
        return fallbackSystemDir;
    }
    const char *hostSystemDir = libretro_host_getSystemDir();
    if (hostSystemDir && *hostSystemDir) {
        return hostSystemDir;
    }
    if (debugger.libretro.systemDir[0]) {
        return debugger.libretro.systemDir;
    }
    return NULL;
}

static const char *
rom_config_activeRomPath(void)
{
    const char *activeRom = libretro_host_getRomPath();
    if (activeRom) {
        return activeRom;
    }
    if (debugger.libretro.romPath[0]) {
        return debugger.libretro.romPath;
    }
    return NULL;
}

static int
rom_config_buildJsonPathCore(char *out, size_t cap, const char *saveDir, const char *romPath)
{
    if (!out || cap == 0 || !saveDir || !romPath) {
        return 0;
    }
    const char *base = rom_config_basename(romPath);
    if (!base || !*base) {
        return 0;
    }
    size_t dirLen = strlen(saveDir);
    int needsSlash = (dirLen > 0 && saveDir[dirLen - 1] != '/' && saveDir[dirLen - 1] != '\\');
    int written = snprintf(out, cap, "%s%s%s.json", saveDir, needsSlash ? "/" : "", base);
    if (written < 0 || (size_t)written >= cap) {
        if (cap > 0) {
            out[0] = '\0';
        }
        return 0;
    }
    return 1;
}

static int
rom_config_findExistingPath(char *out, size_t cap, const char *saveDir, const char *romPath)
{
    if (!out || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!saveDir || !*saveDir || !romPath || !*romPath || !rom_config_pathExistsDir(saveDir)) {
        return 0;
    }
    char jsonPath[PATH_MAX];
    if (!rom_config_buildJsonPathCore(jsonPath, sizeof(jsonPath), saveDir, romPath)) {
        return 0;
    }
    if (rom_config_pathExistsFile(jsonPath)) {
        strutil_strlcpy(out, cap, jsonPath);
        return 1;
    }
    return 0;
}

static const char *
rom_config_uiTestBaseSaveDir(void)
{
    if (ui_test_getMode() == UI_TEST_MODE_NONE) {
        return NULL;
    }
    const char *folder = ui_test_getFolder();
    if (!folder || !*folder || !rom_config_pathExistsDir(folder)) {
        return NULL;
    }
    return folder;
}

static int
rom_config_makeDir(const char *path)
{
    return debugger_platform_makeDir(path);
}

static const char *
rom_config_uiTestTempSaveDir(void)
{
    static char pathbuf[PATH_MAX];
    const char *tempCfgPath = debugger_configTempPath();
    if (!tempCfgPath || !*tempCfgPath) {
        return NULL;
    }
    int written = snprintf(pathbuf, sizeof(pathbuf), "%s.rom", tempCfgPath);
    if (written < 0 || (size_t)written >= sizeof(pathbuf)) {
        return NULL;
    }
    if (!rom_config_pathExistsDir(pathbuf) && !rom_config_makeDir(pathbuf)) {
        return NULL;
    }
    return pathbuf;
}

static const char *
rom_config_uiTestSaveDir(void)
{
    if (ui_test_getMode() == UI_TEST_MODE_NONE) {
        return NULL;
    }
    if (debugger_getLoadTestTempConfig()) {
        const char *tempDir = rom_config_uiTestTempSaveDir();
        if (tempDir && *tempDir) {
            return tempDir;
        }
    }
    return rom_config_uiTestBaseSaveDir();
}

static int
rom_config_copyIntoSaveDir(char *outPath, size_t outCap, const char *dstSaveDir,
                           const char *romPath, const char *srcPath)
{
    if (!outPath || outCap == 0) {
        return 0;
    }
    outPath[0] = '\0';
    if (!dstSaveDir || !*dstSaveDir || !romPath || !*romPath || !srcPath || !*srcPath) {
        return 0;
    }
    char dstPath[PATH_MAX];
    if (!rom_config_buildJsonPathCore(dstPath, sizeof(dstPath), dstSaveDir, romPath)) {
        return 0;
    }
    if (!rom_config_copyFile(srcPath, dstPath)) {
        return 0;
    }
    if (!rom_config_pathExistsFile(dstPath)) {
        return 0;
    }
    strutil_strlcpy(outPath, outCap, dstPath);
    return 1;
}

static uint64_t
rom_config_hashFNV1a(uint64_t hash, const uint8_t *data, size_t len)
{
    const uint64_t prime = 1099511628211ull;
    for (size_t i = 0; i < len; ++i) {
        hash ^= (uint64_t)data[i];
        hash *= prime;
    }
    return hash;
}

static int
rom_config_computeRomChecksum(const char *romPath, uint64_t *outChecksum)
{
    if (!outChecksum) {
        return 0;
    }
    *outChecksum = 0;
    if (!romPath || !rom_config_pathExistsFile(romPath)) {
        return 0;
    }
    FILE *f = fopen(romPath, "rb");
    if (!f) {
        return 0;
    }
    uint8_t buf[8192];
    uint64_t hash = 1469598103934665603ull;
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        hash = rom_config_hashFNV1a(hash, buf, n);
    }
    fclose(f);
    *outChecksum = hash;
    return 1;
}

static void *
rom_config_jsonAlloc(void *userData, size_t size)
{
    (void)userData;
    return alloc_alloc(size);
}

static struct json_value_s *
rom_config_jsonObjectFind(struct json_object_s *object, const char *name)
{
    if (!object || !name) {
        return NULL;
    }
    size_t nameLen = strlen(name);
    for (struct json_object_element_s *elem = object->start; elem; elem = elem->next) {
        if (!elem->name || !elem->name->string) {
            continue;
        }
        if (elem->name->string_size == nameLen &&
            strncmp(elem->name->string, name, nameLen) == 0) {
            return elem->value;
        }
    }
    return NULL;
}

static int
rom_config_jsonGetU64(struct json_value_s *value, uint64_t *outValue)
{
    if (outValue) {
        *outValue = 0;
    }
    if (!value || !outValue) {
        return 0;
    }
    struct json_number_s *num = json_value_as_number(value);
    if (!num || !num->number || num->number_size == 0) {
        return 0;
    }
    char stackBuf[64];
    char *buf = stackBuf;
    if (num->number_size + 1 > sizeof(stackBuf)) {
        buf = (char*)alloc_alloc(num->number_size + 1);
        if (!buf) {
            return 0;
        }
    }
    memcpy(buf, num->number, num->number_size);
    buf[num->number_size] = '\0';
    char *end = NULL;
    unsigned long long v = strtoull(buf, &end, 10);
    if (buf != stackBuf) {
        alloc_free(buf);
    }
    if (!end || *end != '\0') {
        return 0;
    }
    *outValue = (uint64_t)v;
    return 1;
}

static int
rom_config_jsonGetU32(struct json_value_s *value, uint32_t *outValue)
{
    if (outValue) {
        *outValue = 0;
    }
    uint64_t v = 0;
    if (!rom_config_jsonGetU64(value, &v)) {
        return 0;
    }
    if (v > 0xffffffffULL) {
        return 0;
    }
    if (outValue) {
        *outValue = (uint32_t)v;
    }
    return 1;
}

static int
rom_config_jsonGetBool(struct json_value_s *value, int *outValue)
{
    if (outValue) {
        *outValue = 0;
    }
    if (!value || !outValue) {
        return 0;
    }
    if (json_value_is_true(value)) {
        *outValue = 1;
        return 1;
    }
    if (json_value_is_false(value)) {
        *outValue = 0;
        return 1;
    }
    uint32_t v = 0;
    if (rom_config_jsonGetU32(value, &v)) {
        *outValue = v ? 1 : 0;
        return 1;
    }
    return 0;
}

static int
rom_config_jsonGetString(struct json_value_s *value, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!value) {
        return 0;
    }
    struct json_string_s *str = json_value_as_string(value);
    if (!str || !str->string) {
        return 0;
    }
    size_t n = str->string_size;
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(out, str->string, n);
    out[n] = '\0';
    return 1;
}

static void
rom_config_freeData(rom_config_data_t *data)
{
    if (!data) {
        return;
    }
    rom_config_freeInputBindingEntries(&data->inputBindings, &data->inputBindingCount);
    rom_config_freeInputBindingEntries(&data->targetOptions, &data->targetOptionCount);
    alloc_free(data->breakpoints);
    alloc_free(data->protects);
    memset(data, 0, sizeof(*data));
}

static int
rom_config_parseFile(const char *path, rom_config_data_t *outData)
{
    if (!outData) {
        return 0;
    }
    memset(outData, 0, sizeof(*outData));
    if (!path || !*path || !rom_config_pathExistsFile(path)) {
        return 0;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    long fileSize = ftell(f);
    if (fileSize <= 0) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    size_t bufSize = (size_t)fileSize;
    char *buf = (char*)alloc_alloc(bufSize + 1);
    if (!buf) {
        fclose(f);
        return 0;
    }
    size_t read = fread(buf, 1, bufSize, f);
    fclose(f);
    if (read != bufSize) {
        alloc_free(buf);
        return 0;
    }
    buf[bufSize] = '\0';

    struct json_parse_result_s result = {0};
    struct json_value_s *root = json_parse_ex(buf, bufSize, json_parse_flags_default,
                                              rom_config_jsonAlloc, NULL, &result);
    alloc_free(buf);
    if (!root) {
        return 0;
    }
    struct json_object_s *object = json_value_as_object(root);
    if (!object) {
        alloc_free(root);
        return 0;
    }

    uint64_t checksum = 0;
    (void)rom_config_jsonGetU64(rom_config_jsonObjectFind(object, "rom_checksum"), &checksum);
    outData->romChecksum = checksum;
    char breakpointAddrMode[64];
    breakpointAddrMode[0] = '\0';
    if (rom_config_jsonGetString(rom_config_jsonObjectFind(object, "breakpoint_addr_mode"),
                                 breakpointAddrMode, sizeof(breakpointAddrMode)) &&
        strcmp(breakpointAddrMode, "text_relative") == 0) {
        outData->breakpointsTextRelative = 1;
    }

    struct json_object_s *cfgObj = json_value_as_object(rom_config_jsonObjectFind(object, "config"));
    if (cfgObj) {
        if (rom_config_jsonGetString(rom_config_jsonObjectFind(cfgObj, "elf"), outData->elfPath, sizeof(outData->elfPath)) &&
            outData->elfPath[0]) {
            outData->hasElf = 1;
        }
        if (rom_config_jsonGetString(rom_config_jsonObjectFind(cfgObj, "source"), outData->sourceDir, sizeof(outData->sourceDir)) &&
            outData->sourceDir[0]) {
            outData->hasSource = 1;
        }
        if (rom_config_jsonGetString(rom_config_jsonObjectFind(cfgObj, "toolchain_prefix"), outData->toolchainPrefix, sizeof(outData->toolchainPrefix)) &&
            outData->toolchainPrefix[0]) {
            outData->hasToolchain = 1;
        }
    }

    {
        struct json_value_s *inputValue = rom_config_jsonObjectFind(object, "input_bindings");
        struct json_object_s *inputObj = inputValue ? json_value_as_object(inputValue) : NULL;
        if (inputObj) {
            for (struct json_object_element_s *elem = inputObj->start; elem; elem = elem->next) {
                if (!elem->name || !elem->name->string || elem->name->string_size == 0) {
                    continue;
                }
                size_t keyLen = elem->name->string_size;
                char keyBuf[128];
                if (keyLen >= sizeof(keyBuf)) {
                    continue;
                }
                memcpy(keyBuf, elem->name->string, keyLen);
                keyBuf[keyLen] = '\0';
                if (!debugger_input_bindings_isOptionKey(keyBuf)) {
                    continue;
                }
                char valueBuf[128];
                if (!rom_config_jsonGetString(elem->value, valueBuf, sizeof(valueBuf))) {
                    continue;
                }
                if (!valueBuf[0]) {
                    continue;
                }
                (void)rom_config_setInputBindingValueInList(&outData->inputBindings,
                                                            &outData->inputBindingCount,
                                                            keyBuf,
                                                            valueBuf);
            }
        }
    }
    {
        struct json_value_s *targetValue = rom_config_jsonObjectFind(object, "target_options");
        struct json_object_s *targetObj = targetValue ? json_value_as_object(targetValue) : NULL;
        if (targetObj) {
            for (struct json_object_element_s *elem = targetObj->start; elem; elem = elem->next) {
                if (!elem->name || !elem->name->string || elem->name->string_size == 0) {
                    continue;
                }
                size_t keyLen = elem->name->string_size;
                char keyBuf[128];
                if (keyLen >= sizeof(keyBuf)) {
                    continue;
                }
                memcpy(keyBuf, elem->name->string, keyLen);
                keyBuf[keyLen] = '\0';
                char valueBuf[128];
                if (!rom_config_jsonGetString(elem->value, valueBuf, sizeof(valueBuf))) {
                    continue;
                }
                if (!valueBuf[0]) {
                    continue;
                }
                (void)rom_config_setInputBindingValueInList(&outData->targetOptions,
                                                            &outData->targetOptionCount,
                                                            keyBuf,
                                                            valueBuf);
            }
        }
    }

    struct json_array_s *bpsArray = json_value_as_array(rom_config_jsonObjectFind(object, "breakpoints"));
    if (bpsArray) {
        size_t count = 0;
        for (struct json_array_element_s *el = bpsArray->start; el; el = el->next) {
            (void)el;
            count++;
        }
        if (count > 0) {
            outData->breakpoints = (rom_config_bp_entry_t*)alloc_calloc(count, sizeof(*outData->breakpoints));
            if (outData->breakpoints) {
                size_t writeIndex = 0;
                for (struct json_array_element_s *el = bpsArray->start; el; el = el->next) {
                    struct json_object_s *bpObj = json_value_as_object(el->value);
                    if (!bpObj) {
                        continue;
                    }
                    uint32_t addr = 0;
                    int enabled = 0;
                    if (!rom_config_jsonGetU32(rom_config_jsonObjectFind(bpObj, "addr"), &addr)) {
                        continue;
                    }
                    (void)rom_config_jsonGetBool(rom_config_jsonObjectFind(bpObj, "enabled"), &enabled);
                    outData->breakpoints[writeIndex].addr = addr;
                    outData->breakpoints[writeIndex].enabled = enabled ? 1 : 0;
                    writeIndex++;
                }
                outData->breakpointCount = writeIndex;
            }
        }
    }

    struct json_array_s *protectsArray = json_value_as_array(rom_config_jsonObjectFind(object, "protects"));
    if (protectsArray) {
        size_t count = 0;
        for (struct json_array_element_s *el = protectsArray->start; el; el = el->next) {
            (void)el;
            count++;
        }
        if (count > 0) {
            outData->protects = (rom_config_protect_entry_t*)alloc_calloc(count, sizeof(*outData->protects));
            if (outData->protects) {
                size_t writeIndex = 0;
                for (struct json_array_element_s *el = protectsArray->start; el; el = el->next) {
                    struct json_object_s *pObj = json_value_as_object(el->value);
                    if (!pObj) {
                        continue;
                    }
                    uint32_t addr = 0;
                    uint32_t sizeBits = 0;
                    uint32_t mode = 0;
                    uint32_t value = 0;
                    int enabled = 0;
                    if (!rom_config_jsonGetU32(rom_config_jsonObjectFind(pObj, "addr"), &addr)) {
                        continue;
                    }
                    if (!rom_config_jsonGetU32(rom_config_jsonObjectFind(pObj, "size_bits"), &sizeBits)) {
                        continue;
                    }
                    if (!rom_config_jsonGetU32(rom_config_jsonObjectFind(pObj, "mode"), &mode)) {
                        continue;
                    }
                    (void)rom_config_jsonGetU32(rom_config_jsonObjectFind(pObj, "value"), &value);
                    (void)rom_config_jsonGetBool(rom_config_jsonObjectFind(pObj, "enabled"), &enabled);
                    outData->protects[writeIndex].addr = addr;
                    outData->protects[writeIndex].sizeBits = sizeBits;
                    outData->protects[writeIndex].mode = mode;
                    outData->protects[writeIndex].value = value;
                    outData->protects[writeIndex].enabled = enabled ? 1 : 0;
                    writeIndex++;
                }
                outData->protectCount = writeIndex;
            }
        }
    }

    alloc_free(root);
    return 1;
}

static void
rom_config_writeJsonString(FILE *f, const char *text)
{
    if (!f) {
        return;
    }

    fputc('"', f);
    if (text) {
        for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
            unsigned char c = *p;
            if (c == '"' || c == '\\') {
                fputc('\\', f);
                fputc((int)c, f);
            } else if (c == '\b') {
                fputs("\\b", f);
            } else if (c == '\f') {
                fputs("\\f", f);
            } else if (c == '\n') {
                fputs("\\n", f);
            } else if (c == '\r') {
                fputs("\\r", f);
            } else if (c == '\t') {
                fputs("\\t", f);
            } else if (c < 0x20) {
                fprintf(f, "\\u%04X", (unsigned)c);
            } else {
                fputc((int)c, f);
            }
        }
    }
    fputc('"', f);
}

static void
rom_config_writeJsonFile(const char *path, const char *romPath, const rom_config_data_t *data)
{
    if (!path || !*path || !romPath || !*romPath || !data) {
        return;
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        return;
    }

    const char *base = rom_config_basename(romPath);
    char jsonName[PATH_MAX];
    if (base && *base) {
        snprintf(jsonName, sizeof(jsonName), "%s.json", base);
    } else {
        snprintf(jsonName, sizeof(jsonName), "unknown.json");
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"rom_checksum\": %llu,\n", (unsigned long long)data->romChecksum);
    fprintf(f, "  \"rom_filename\": ");
    rom_config_writeJsonString(f, jsonName);
    fprintf(f, ",\n");
    fprintf(f, "  \"breakpoint_addr_mode\": ");
    rom_config_writeJsonString(f, data->breakpointsTextRelative ? "text_relative" : "absolute");
    fprintf(f, ",\n");

    fprintf(f, "  \"config\": {\n");
    fprintf(f, "    \"elf\": ");
    rom_config_writeJsonString(f, data->hasElf ? data->elfPath : "");
    fprintf(f, ",\n");
    fprintf(f, "    \"source\": ");
    rom_config_writeJsonString(f, data->hasSource ? data->sourceDir : "");
    fprintf(f, ",\n");
    fprintf(f, "    \"toolchain_prefix\": ");
    rom_config_writeJsonString(f, data->hasToolchain ? data->toolchainPrefix : "");
    fprintf(f, "\n");
    fprintf(f, "  },\n");

    fprintf(f, "  \"input_bindings\": {\n");
    {
        int wroteAny = 0;
        for (size_t i = 0; i < data->inputBindingCount; ++i) {
            const rom_config_input_binding_entry_t *entry = &data->inputBindings[i];
            if (!entry->key || !entry->value || !entry->key[0] || !entry->value[0]) {
                continue;
            }
            if (!debugger_input_bindings_isOptionKey(entry->key)) {
                continue;
            }
            if (wroteAny) {
                fprintf(f, ",\n");
            }
            fprintf(f, "    ");
            rom_config_writeJsonString(f, entry->key);
            fprintf(f, ": ");
            rom_config_writeJsonString(f, entry->value);
            wroteAny = 1;
        }
        if (wroteAny) {
            fprintf(f, "\n");
        }
    }
    fprintf(f, "  },\n");
    fprintf(f, "  \"target_options\": {\n");
    {
        int wroteAny = 0;
        for (size_t i = 0; i < data->targetOptionCount; ++i) {
            const rom_config_input_binding_entry_t *entry = &data->targetOptions[i];
            if (!entry->key || !entry->value || !entry->key[0] || !entry->value[0]) {
                continue;
            }
            if (wroteAny) {
                fprintf(f, ",\n");
            }
            fprintf(f, "    ");
            rom_config_writeJsonString(f, entry->key);
            fprintf(f, ": ");
            rom_config_writeJsonString(f, entry->value);
            wroteAny = 1;
        }
        if (wroteAny) {
            fprintf(f, "\n");
        }
    }
    fprintf(f, "  },\n");

    fprintf(f, "  \"breakpoints\": [\n");
    for (size_t i = 0; i < data->breakpointCount; ++i) {
        const rom_config_bp_entry_t *bp = &data->breakpoints[i];
        fprintf(f, "    {\"addr\": %u, \"enabled\": %s}%s\n",
                (unsigned)(bp->addr & 0x00ffffffu),
                bp->enabled ? "true" : "false",
                (i + 1 < data->breakpointCount) ? "," : "");
    }
    fprintf(f, "  ],\n");

    fprintf(f, "  \"protects\": [\n");
    for (size_t i = 0; i < data->protectCount; ++i) {
        const rom_config_protect_entry_t *p = &data->protects[i];
        fprintf(f, "    {\"addr\": %u, \"size_bits\": %u, \"mode\": %u, \"value\": %u, \"enabled\": %s}%s\n",
                (unsigned)(p->addr & 0x00ffffffu),
                (unsigned)p->sizeBits,
                (unsigned)p->mode,
                (unsigned)p->value,
                p->enabled ? "true" : "false",
                (i + 1 < data->protectCount) ? "," : "");
    }
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");

    fclose(f);
}

static void
rom_config_setActiveDefaultsFromCurrentSystem(void)
{
    target->setActiveDefaultsFromCurrentSystem();
    rom_config_activeElfPath[sizeof(rom_config_activeElfPath) - 1] = '\0';
    rom_config_activeSourceDir[sizeof(rom_config_activeSourceDir) - 1] = '\0';
    rom_config_activeToolchainPrefix[sizeof(rom_config_activeToolchainPrefix) - 1] = '\0';
    rom_config_clearActiveInputBindings();
    rom_config_clearActiveTargetCustomOptions(target);
    rom_config_activeInit = 1;
}

void
rom_config_syncActiveFromCurrentSystem(void)
{
    target->setActiveDefaultsFromCurrentSystem();
    rom_config_activeElfPath[sizeof(rom_config_activeElfPath) - 1] = '\0';
    rom_config_activeSourceDir[sizeof(rom_config_activeSourceDir) - 1] = '\0';
    rom_config_activeToolchainPrefix[sizeof(rom_config_activeToolchainPrefix) - 1] = '\0';
    rom_config_activeInit = 1;
}

static void
rom_config_applyActiveSettingsToCurrentSystem(void)
{
    if (!rom_config_activeInit) {
        return;
    }
    target->applyActiveSettingsToCurrentSystem();

    debugger_libretroSelectConfig();
}

static void
rom_config_clearBreakpointsCore(void)
{
    const machine_breakpoint_t *bps = NULL;
    int count = 0;
    machine_getBreakpoints(&debugger.machine, &bps, &count);
    if (bps && count > 0) {
        for (int i = 0; i < count; ++i) {
            uint32_t addr = bps[i].processorId == MACHINE_PROCESSOR_PRIMARY
                ? (uint32_t)(bps[i].addr & 0x00ffffffu)
                : (uint32_t)(bps[i].addr & 0x0000ffffu);
            libretro_host_debugRemoveProcessorBreakpoint(bps[i].processorId, addr);
        }
    }
    machine_clearBreakpoints(&debugger.machine);
}

void
rom_config_loadSettingsForSelectedRom(void)
{
    const char *romPath = debugger.libretro.romPath[0] ? debugger.libretro.romPath : NULL;
    if (!romPath || !*romPath) {
        rom_config_setActiveDefaultsFromCurrentSystem();
        return;
    }
    ui_test_mode_t testMode = ui_test_getMode();
    const char *testBaseSaveDir = rom_config_uiTestBaseSaveDir();
    const char *testSaveDir = rom_config_uiTestSaveDir();
    const char *saveDir = rom_config_saveDir(NULL);
    char selectedPath[PATH_MAX];
    selectedPath[0] = '\0';

    if (testMode == UI_TEST_MODE_COMPARE || testMode == UI_TEST_MODE_REMAKE) {
        if (!testSaveDir || !rom_config_findExistingPath(selectedPath, sizeof(selectedPath), testSaveDir, romPath)) {
            if (testBaseSaveDir && *testBaseSaveDir &&
                (!testSaveDir || strcmp(testSaveDir, testBaseSaveDir) != 0)) {
                (void)rom_config_findExistingPath(selectedPath, sizeof(selectedPath), testBaseSaveDir, romPath);
            }
        }
        if (!selectedPath[0]) {
            rom_config_setActiveDefaultsFromCurrentSystem();
            return;
        }
    } else if (testMode == UI_TEST_MODE_RECORD) {
        if (!testSaveDir) {
            rom_config_setActiveDefaultsFromCurrentSystem();
            return;
        }
        if (!rom_config_findExistingPath(selectedPath, sizeof(selectedPath), testSaveDir, romPath)) {
            char sourcePath[PATH_MAX];
            sourcePath[0] = '\0';
            if (testBaseSaveDir && *testBaseSaveDir &&
                strcmp(testBaseSaveDir, testSaveDir) != 0) {
                (void)rom_config_findExistingPath(sourcePath, sizeof(sourcePath), testBaseSaveDir, romPath);
            }
            if (saveDir && strcmp(saveDir, testSaveDir) != 0) {
                (void)rom_config_findExistingPath(sourcePath, sizeof(sourcePath), saveDir, romPath);
            }
            if (!sourcePath[0]) {
                const char *bootSaveDir = rom_config_bootSaveDir();
                if (bootSaveDir && *bootSaveDir &&
                    strcmp(bootSaveDir, testSaveDir) != 0 &&
                    (!saveDir || strcmp(bootSaveDir, saveDir) != 0)) {
                    (void)rom_config_findExistingPath(sourcePath, sizeof(sourcePath), bootSaveDir, romPath);
                }
            }
            if (sourcePath[0]) {
                if (!rom_config_copyIntoSaveDir(selectedPath, sizeof(selectedPath), testSaveDir, romPath, sourcePath)) {
                    strutil_strlcpy(selectedPath, sizeof(selectedPath), sourcePath);
                }
            }
        }
    } else {
        if (saveDir) {
            (void)rom_config_findExistingPath(selectedPath, sizeof(selectedPath), saveDir, romPath);
        }
    }

    if (!selectedPath[0]) {
        rom_config_setActiveDefaultsFromCurrentSystem();
        return;
    }

    rom_config_data_t data;
    if (!rom_config_parseFile(selectedPath, &data)) {
        rom_config_setActiveDefaultsFromCurrentSystem();
        return;
    }

    rom_config_setActiveDefaultsFromCurrentSystem();
    if (data.hasElf) {
        strutil_strlcpy(rom_config_activeElfPath, sizeof(rom_config_activeElfPath), data.elfPath);
    }
    if (data.hasSource) {
        strutil_strlcpy(rom_config_activeSourceDir, sizeof(rom_config_activeSourceDir), data.sourceDir);
    }
    if (data.hasToolchain) {
        strutil_strlcpy(rom_config_activeToolchainPrefix, sizeof(rom_config_activeToolchainPrefix), data.toolchainPrefix);
    }
    rom_config_applyParsedInputBindingsToActive(&data);
    rom_config_applyParsedTargetOptionsToActive(&data, target);
    rom_config_applyActiveSettingsToCurrentSystem();
    rom_config_freeData(&data);
}

int
rom_config_loadSettingsForRom(const char *saveDir, const char *romPath,
                              target_iface_t *targetIface,
                              char *outElfPath, size_t elfCap,
                              char *outSourceDir, size_t sourceCap,
                              char *outToolchainPrefix, size_t toolchainCap,
                              int *outHasElf, int *outHasSource, int *outHasToolchain)
{
    rom_config_clearActiveInputBindings();
    rom_config_clearActiveTargetCustomOptions(targetIface);
    if (outHasElf) {
        *outHasElf = 0;
    }
    if (outHasSource) {
        *outHasSource = 0;
    }
    if (outHasToolchain) {
        *outHasToolchain = 0;
    }
    if (outElfPath && elfCap > 0) {
        outElfPath[0] = '\0';
    }
    if (outSourceDir && sourceCap > 0) {
        outSourceDir[0] = '\0';
    }
    if (outToolchainPrefix && toolchainCap > 0) {
        outToolchainPrefix[0] = '\0';
    }
    const char *effectiveSaveDir = saveDir;
    const char *testBaseSaveDir = NULL;
    if (ui_test_getMode() != UI_TEST_MODE_NONE) {
        effectiveSaveDir = rom_config_uiTestSaveDir();
        testBaseSaveDir = rom_config_uiTestBaseSaveDir();
    }
    if (!effectiveSaveDir || !*effectiveSaveDir || !romPath || !*romPath || !rom_config_pathExistsDir(effectiveSaveDir)) {
        return 0;
    }
    char jsonPath[PATH_MAX];
    if (!rom_config_buildJsonPathCore(jsonPath, sizeof(jsonPath), effectiveSaveDir, romPath)) {
        return 0;
    }
    char fallbackJsonPath[PATH_MAX];
    fallbackJsonPath[0] = '\0';
    if (testBaseSaveDir && *testBaseSaveDir && strcmp(testBaseSaveDir, effectiveSaveDir) != 0) {
        (void)rom_config_buildJsonPathCore(fallbackJsonPath, sizeof(fallbackJsonPath), testBaseSaveDir, romPath);
    }
    const char *pathToRead = NULL;
    if (rom_config_pathExistsFile(jsonPath)) {
        pathToRead = jsonPath;
    } else if (fallbackJsonPath[0] && rom_config_pathExistsFile(fallbackJsonPath)) {
        pathToRead = fallbackJsonPath;
    }
    if (!pathToRead) {
        return 0;
    }

    rom_config_data_t data;
    if (!rom_config_parseFile(pathToRead, &data)) {
        return 0;
    }
    if (outHasElf) {
        *outHasElf = data.hasElf ? 1 : 0;
    }
    if (outHasSource) {
        *outHasSource = data.hasSource ? 1 : 0;
    }
    if (outHasToolchain) {
        *outHasToolchain = data.hasToolchain ? 1 : 0;
    }
    if (data.hasElf && outElfPath && elfCap > 0) {
        strutil_strlcpy(outElfPath, elfCap, data.elfPath);
    }
    if (data.hasSource && outSourceDir && sourceCap > 0) {
        strutil_strlcpy(outSourceDir, sourceCap, data.sourceDir);
    }
    if (data.hasToolchain && outToolchainPrefix && toolchainCap > 0) {
        strutil_strlcpy(outToolchainPrefix, toolchainCap, data.toolchainPrefix);
    }

    rom_config_applyParsedInputBindingsToActive(&data);
    rom_config_applyParsedTargetOptionsToActive(&data, targetIface);

    rom_config_freeData(&data);
    return 1;
}

void
rom_config_loadRuntimeStateOnBoot(void)
{
    const char *romPath = rom_config_activeRomPath();
    if (!romPath) {
        return;
    }
    const char *testBaseSaveDir = rom_config_uiTestBaseSaveDir();
    const char *saveDir = NULL;
    if (ui_test_getMode() != UI_TEST_MODE_NONE) {
        saveDir = rom_config_uiTestSaveDir();
    }
    if (!saveDir) {
        saveDir = rom_config_saveDir(NULL);
    }
    char selectedPath[PATH_MAX];
    selectedPath[0] = '\0';
    if (saveDir) {
        (void)rom_config_findExistingPath(selectedPath, sizeof(selectedPath), saveDir, romPath);
        if (!selectedPath[0] && testBaseSaveDir && *testBaseSaveDir &&
            strcmp(saveDir, testBaseSaveDir) != 0) {
            (void)rom_config_findExistingPath(selectedPath, sizeof(selectedPath), testBaseSaveDir, romPath);
        }
    }
    if (!selectedPath[0]) {
        return;
    }

    rom_config_data_t data;
    if (!rom_config_parseFile(selectedPath, &data)) {
        return;
    }

    uint64_t romChecksum = 0;
    if (!rom_config_computeRomChecksum(romPath, &romChecksum)) {
        rom_config_freeData(&data);
        return;
    }

    if (data.romChecksum != 0 && data.romChecksum != romChecksum) {
        rom_config_clearBreakpointsCore();
        protect_clear();
        breakpoints_markDirty();
        trainer_markDirty();
        rom_config_freeData(&data);
        return;
    }

    rom_config_applyParsedInputBindingsToActive(&data);
    rom_config_applyParsedTargetOptionsToActive(&data, target);

    rom_config_clearBreakpointsCore();
    protect_clear();

    for (size_t i = 0; i < data.breakpointCount; ++i) {
        const rom_config_bp_entry_t *bp = &data.breakpoints[i];
        uint32_t runtimeAddr = bp->addr & 0x00ffffffu;
        if (data.breakpointsTextRelative) {
            runtimeAddr = machine_textBaseFromRelativeAddr(&debugger.machine, runtimeAddr);
        }
        machine_breakpoint_t *added = machine_addBreakpoint(&debugger.machine, runtimeAddr, bp->enabled);
        if (added) {
            breakpoints_resolveLocation(added);
        }
        if (bp->enabled) {
            libretro_host_debugAddBreakpoint(runtimeAddr & 0x00ffffffu);
        }
    }

    uint64_t enabledMask = 0;
    for (size_t i = 0; i < data.protectCount; ++i) {
        const rom_config_protect_entry_t *p = &data.protects[i];
        uint32_t index = 0;
        if (!libretro_host_debugAddProtect(p->addr & 0x00ffffffu, p->sizeBits, p->mode, p->value, &index)) {
            continue;
        }
        if (p->enabled) {
            enabledMask |= (1ull << index);
        }
    }
    if (data.protectCount > 0) {
        libretro_host_debugSetProtectEnabledMask(enabledMask);
    }

    breakpoints_markDirty();
    trainer_markDirty();

    rom_config_freeData(&data);
}

void
rom_config_saveOnExit(void)
{
    if (debugger.smokeTestMode == SMOKE_TEST_MODE_COMPARE ||
        debugger.smokeTestMode == SMOKE_TEST_MODE_REMAKE) {
        return;
    }
    
    if (ui_test_getMode() != UI_TEST_MODE_NONE) {
        return;
    }
    const char *romPath = rom_config_activeRomPath();
    const char *saveDir = rom_config_saveDir(NULL);
    if (!romPath || !saveDir || !rom_config_pathExistsDir(saveDir)) {
        return;
    }
    char jsonPath[PATH_MAX];
    if (!rom_config_buildJsonPathCore(jsonPath, sizeof(jsonPath), saveDir, romPath)) {
        return;
    }
    uint64_t romChecksum = 0;
    if (!rom_config_computeRomChecksum(romPath, &romChecksum)) {
        return;
    }

    rom_config_data_t data;
    int loaded = rom_config_parseFile(jsonPath, &data);
    if (!loaded) {
        memset(&data, 0, sizeof(data));
    }
    data.romChecksum = romChecksum;

    const machine_breakpoint_t *bps = NULL;
    int bpCount = 0;
    if (data.breakpoints) {
        alloc_free(data.breakpoints);
        data.breakpoints = NULL;
        data.breakpointCount = 0;
    }
    if (data.protects) {
        alloc_free(data.protects);
        data.protects = NULL;
        data.protectCount = 0;
    }
    machine_getBreakpoints(&debugger.machine, &bps, &bpCount);
    data.breakpointsTextRelative = 1;
    if (bps && bpCount > 0) {
        int primaryBpCount = 0;
        for (int i = 0; i < bpCount; ++i) {
            if (bps[i].processorId == MACHINE_PROCESSOR_PRIMARY) {
                primaryBpCount++;
            }
        }
        data.breakpoints = (rom_config_bp_entry_t*)alloc_calloc((size_t)primaryBpCount, sizeof(*data.breakpoints));
        if (data.breakpoints) {
            data.breakpointCount = (size_t)primaryBpCount;
            size_t writeIndex = 0;
            for (int i = 0; i < bpCount; ++i) {
                if (bps[i].processorId != MACHINE_PROCESSOR_PRIMARY) {
                    continue;
                }
                data.breakpoints[writeIndex].addr =
                    machine_textBaseToRelativeAddr(&debugger.machine, (uint32_t)(bps[i].addr & 0x00ffffffu));
                data.breakpoints[writeIndex].enabled = bps[i].enabled ? 1 : 0;
                writeIndex++;
            }
        }
    }

    e9k_debug_protect_t protects[E9K_PROTECT_COUNT];
    size_t protectCount = 0;
    uint64_t enabledMask = 0;
    libretro_host_debugReadProtects(protects, E9K_PROTECT_COUNT, &protectCount);
    libretro_host_debugGetProtectEnabledMask(&enabledMask);
    if (protectCount > 0) {
        data.protects = (rom_config_protect_entry_t*)alloc_calloc(protectCount, sizeof(*data.protects));
        if (data.protects) {
            size_t written = 0;
            for (size_t i = 0; i < protectCount; ++i) {
                const e9k_debug_protect_t *p = &protects[i];
                if (p->sizeBits == 0) {
                    continue;
                }
                int enabled = ((enabledMask >> i) & 1ull) ? 1 : 0;
                data.protects[written].addr = (uint32_t)(p->addr & 0x00ffffffu);
                data.protects[written].sizeBits = (uint32_t)p->sizeBits;
                data.protects[written].mode = (uint32_t)p->mode;
                data.protects[written].value = (uint32_t)p->value;
                data.protects[written].enabled = enabled;
                written++;
            }
            data.protectCount = written;
        }
    }

    rom_config_writeJsonFile(jsonPath, romPath, &data);
    rom_config_freeData(&data);
}

void
rom_config_saveCurrentRomSettings(void)
{
    const char *romPath = rom_config_activeRomPath();
    const char *saveDir = rom_config_saveDir(NULL);
    if (!romPath || !*romPath || !saveDir || !*saveDir || !rom_config_pathExistsDir(saveDir)) {
        return;
    }
    if (!rom_config_activeInit) {
        rom_config_setActiveDefaultsFromCurrentSystem();
    }
    rom_config_saveSettingsForRom(saveDir,
                                  romPath,
                                  target,
                                  rom_config_activeElfPath[0] ? rom_config_activeElfPath : NULL,
                                  rom_config_activeSourceDir[0] ? rom_config_activeSourceDir : NULL,
                                  rom_config_activeToolchainPrefix[0] ? rom_config_activeToolchainPrefix : NULL);
}

void
rom_config_saveSettingsForRom(const char *saveDir, const char *romPath,
                              target_iface_t *targetIface,
                              const char *elfPath, const char *sourceDir,
                              const char *toolchainPrefix)
{
    const char *effectiveSaveDir = saveDir;
    if (ui_test_getMode() != UI_TEST_MODE_NONE) {
        effectiveSaveDir = rom_config_uiTestTempSaveDir();
    }
    if (!effectiveSaveDir || !*effectiveSaveDir || !romPath || !*romPath || !rom_config_pathExistsDir(effectiveSaveDir)) {
        return;
    }
    char jsonPath[PATH_MAX];
    if (!rom_config_buildJsonPathCore(jsonPath, sizeof(jsonPath), effectiveSaveDir, romPath)) {
        return;
    }
    rom_config_data_t data;
    int loaded = rom_config_parseFile(jsonPath, &data);
    if (!loaded) {
        memset(&data, 0, sizeof(data));
    }

    uint64_t romChecksum = 0;
    if (rom_config_computeRomChecksum(romPath, &romChecksum)) {
        data.romChecksum = romChecksum;
    }

    data.hasElf = 0;
    data.hasSource = 0;
    data.hasToolchain = 0;
    data.elfPath[0] = '\0';
    data.sourceDir[0] = '\0';
    data.toolchainPrefix[0] = '\0';

    if (elfPath && *elfPath) {
        strutil_strlcpy(data.elfPath, sizeof(data.elfPath), elfPath);
        data.hasElf = 1;
    }
    if (sourceDir && *sourceDir) {
        strutil_strlcpy(data.sourceDir, sizeof(data.sourceDir), sourceDir);
        data.hasSource = 1;
    }
    if (toolchainPrefix && *toolchainPrefix) {
        strutil_strlcpy(data.toolchainPrefix, sizeof(data.toolchainPrefix), toolchainPrefix);
        data.hasToolchain = 1;
    }
    rom_config_freeInputBindingEntries(&data.inputBindings, &data.inputBindingCount);
    rom_config_freeInputBindingEntries(&data.targetOptions, &data.targetOptionCount);
    for (size_t i = 0; i < debugger_input_bindings_specCount(); ++i) {
        const debugger_input_bindings_spec_t *spec = debugger_input_bindings_specAt(i);
        if (!spec || !spec->optionKey) {
            continue;
        }
        const char *value = rom_config_getActiveInputBindingValue(spec->optionKey);
        if (!value || !*value) {
            continue;
        }
        (void)rom_config_setInputBindingValueInList(&data.inputBindings,
                                                    &data.inputBindingCount,
                                                    spec->optionKey,
                                                    value);
    }
    rom_config_collectActiveTargetOptions(&data, targetIface ? targetIface : target);
    const char *activeRom = rom_config_activeRomPath();
    if (activeRom && strcmp(activeRom, romPath) == 0) {
        strutil_strlcpy(rom_config_activeElfPath, sizeof(rom_config_activeElfPath), data.elfPath);
        strutil_strlcpy(rom_config_activeSourceDir, sizeof(rom_config_activeSourceDir), data.sourceDir);
        strutil_strlcpy(rom_config_activeToolchainPrefix, sizeof(rom_config_activeToolchainPrefix), data.toolchainPrefix);
        rom_config_activeInit = 1;
    }
    rom_config_writeJsonFile(jsonPath, romPath, &data);
    rom_config_freeData(&data);
}
