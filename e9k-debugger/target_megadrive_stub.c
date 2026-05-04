/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <stdlib.h>
#include <string.h>

#include "target.h"
#include "debugger.h"
#include "debug.h"
#include "emu_mega.h"
#include "mega_sprite_debug.h"
#include "megadrive_core_options.h"

static void
target_megadrive_stubSetConfigDefaults(e9k_system_config_t *config)
{
    if (!config) {
        return;
    }
    memset(&config->megadrive, 0, sizeof(config->megadrive));
}

static const char *
target_megadrive_stubDefaultCorePath(void)
{
    debug_error("BUG: target_megadrive_stubDefaultCorePath called with E9K_ENABLE_MEGADRIVE=0");
    abort();
    return NULL;
}

static void
target_megadrive_stubSetActiveDefaultsFromCurrentSystem(void)
{
    debug_error("BUG: target_megadrive_stubSetActiveDefaultsFromCurrentSystem called with E9K_ENABLE_MEGADRIVE=0");
    abort();
}

static void
target_megadrive_stubApplyActiveSettingsToCurrentSystem(void)
{
    debug_error("BUG: target_megadrive_stubApplyActiveSettingsToCurrentSystem called with E9K_ENABLE_MEGADRIVE=0");
    abort();
}

static int
target_megadrive_stubConfigIsOk(void)
{
    debug_error("BUG: target_megadrive_stubConfigIsOk called with E9K_ENABLE_MEGADRIVE=0");
    abort();
    return 0;
}

static int
target_megadrive_stubNeedsRestart(void)
{
    debug_error("BUG: target_megadrive_stubNeedsRestart called with E9K_ENABLE_MEGADRIVE=0");
    abort();
    return 0;
}

static int
target_megadrive_stubSettingsSaveButtonDisabled(void)
{
    debug_error("BUG: target_megadrive_stubSettingsSaveButtonDisabled called with E9K_ENABLE_MEGADRIVE=0");
    abort();
    return 1;
}

static void
target_megadrive_stubValidateSettings(void)
{
    debug_error("BUG: target_megadrive_stubValidateSettings called with E9K_ENABLE_MEGADRIVE=0");
    abort();
}

static void
target_megadrive_stubSettingsDefaults(void)
{
    debug_error("BUG: target_megadrive_stubSettingsDefaults called with E9K_ENABLE_MEGADRIVE=0");
    abort();
}

static void
target_megadrive_stubApplyRomConfigForSelection(settings_romselect_state_t *st, const char **saveDirP, const char **romPathP)
{
    (void)st;
    (void)saveDirP;
    (void)romPathP;
    debug_error("BUG: target_megadrive_stubApplyRomConfigForSelection called with E9K_ENABLE_MEGADRIVE=0");
    abort();
}

static void
target_megadrive_stubSettingsSetConfigPaths(int hasElf,
                                            const char *elfPath,
                                            int hasSource,
                                            const char *sourceDir,
                                            int hasToolchain,
                                            const char *toolchainPrefix)
{
    (void)hasElf;
    (void)elfPath;
    (void)hasSource;
    (void)sourceDir;
    (void)hasToolchain;
    (void)toolchainPrefix;
    debug_error("BUG: target_megadrive_stubSettingsSetConfigPaths called with E9K_ENABLE_MEGADRIVE=0");
    abort();
}

static void
target_megadrive_stubSettingsRomPathChanged(settings_romselect_state_t *st)
{
    (void)st;
    debug_error("BUG: target_megadrive_stubSettingsRomPathChanged called with E9K_ENABLE_MEGADRIVE=0");
    abort();
}

static void
target_megadrive_stubSettingsRomFolderChanged(void)
{
    debug_error("BUG: target_megadrive_stubSettingsRomFolderChanged called with E9K_ENABLE_MEGADRIVE=0");
    abort();
}

static void
target_megadrive_stubSettingsCoreChanged(void)
{
    debug_error("BUG: target_megadrive_stubSettingsCoreChanged called with E9K_ENABLE_MEGADRIVE=0");
    abort();
}

static void
target_megadrive_stubSettingsClearOptions(void)
{
    debug_error("BUG: target_megadrive_stubSettingsClearOptions called with E9K_ENABLE_MEGADRIVE=0");
    abort();
}

static void
target_megadrive_stubSettingsLoadOptions(struct e9k_system_config *st)
{
    (void)st;
    debug_error("BUG: target_megadrive_stubSettingsLoadOptions called with E9K_ENABLE_MEGADRIVE=0");
    abort();
}

static void
target_megadrive_stubSettingsBuildModal(e9ui_context_t *ctx, target_settings_modal_t *out)
{
    (void)ctx;
    (void)out;
    debug_error("BUG: target_megadrive_stubSettingsBuildModal called with E9K_ENABLE_MEGADRIVE=0");
    abort();
}

static const struct e9k_libretro_config *
target_megadrive_stubSelectLibretroConfig(const struct e9k_system_config *cfg)
{
    (void)cfg;
    debug_error("BUG: target_megadrive_stubSelectLibretroConfig called with E9K_ENABLE_MEGADRIVE=0");
    abort();
    return NULL;
}

static int
target_megadrive_stubCoreOptionsHasGeneral(const struct core_options_modal_state *st)
{
    (void)st;
    debug_error("BUG: target_megadrive_stubCoreOptionsHasGeneral called with E9K_ENABLE_MEGADRIVE=0");
    abort();
    return 0;
}

static void
target_megadrive_stubCoreOptionsSaveClicked(e9ui_context_t *ctx, struct core_options_modal_state *st)
{
    (void)ctx;
    (void)st;
    debug_error("BUG: target_megadrive_stubCoreOptionsSaveClicked called with E9K_ENABLE_MEGADRIVE=0");
    abort();
}

static const char *
target_megadrive_stubCoreOptionGetValue(const char *key)
{
    (void)key;
    debug_error("BUG: target_megadrive_stubCoreOptionGetValue called with E9K_ENABLE_MEGADRIVE=0");
    abort();
    return NULL;
}

static struct e9k_libretro_config *
target_megadrive_stubGetLibretroCliConfig(void)
{
    debug_error("BUG: target_megadrive_stubGetLibretroCliConfig called with E9K_ENABLE_MEGADRIVE=0");
    abort();
    return NULL;
}

static void
target_megadrive_stubOnCoreStarted(void)
{
    debug_error("BUG: target_megadrive_stubOnCoreStarted called with E9K_ENABLE_MEGADRIVE=0");
    abort();
}

static void
target_megadrive_stubOnVblank(void)
{
    debug_error("BUG: target_megadrive_stubOnVblank called with E9K_ENABLE_MEGADRIVE=0");
    abort();
}

static void
target_megadrive_stubLibretroSelectConfig(void)
{
    debug_error("BUG: target_megadrive_stubLibretroSelectConfig called with E9K_ENABLE_MEGADRIVE=0");
    abort();
}

static void
target_megadrive_stubPickElfToolchainPaths(const char **rawElf, const char **toolchainPrefix)
{
    (void)rawElf;
    (void)toolchainPrefix;
    debug_error("BUG: target_megadrive_stubPickElfToolchainPaths called with E9K_ENABLE_MEGADRIVE=0");
    abort();
}

static void
target_megadrive_stubApplyCoreOptions(void)
{
    debug_error("BUG: target_megadrive_stubApplyCoreOptions called with E9K_ENABLE_MEGADRIVE=0");
    abort();
}

static void
target_megadrive_stubValidateAPI(void)
{
    debug_error("BUG: target_megadrive_stubValidateAPI called with E9K_ENABLE_MEGADRIVE=0");
    abort();
}

static int
target_megadrive_stubAudioEnabled(void)
{
    debug_error("BUG: target_megadrive_stubAudioEnabled called with E9K_ENABLE_MEGADRIVE=0");
    abort();
    return 0;
}

static void
target_megadrive_stubAudioEnable(int enabled)
{
    (void)enabled;
    debug_error("BUG: target_megadrive_stubAudioEnable called with E9K_ENABLE_MEGADRIVE=0");
    abort();
}

static int
target_megadrive_stubMemoryGetLimits(uint32_t *outMinAddr, uint32_t *outMaxAddr)
{
    (void)outMinAddr;
    (void)outMaxAddr;
    debug_error("BUG: target_megadrive_stubMemoryGetLimits called with E9K_ENABLE_MEGADRIVE=0");
    abort();
    return 0;
}

static int
target_megadrive_stubMemoryTrackGetRanges(target_memory_range_t *outRanges, size_t cap, size_t *outCount)
{
    (void)outRanges;
    (void)cap;
    (void)outCount;
    debug_error("BUG: target_megadrive_stubMemoryTrackGetRanges called with E9K_ENABLE_MEGADRIVE=0");
    abort();
    return 0;
}

static int
target_megadrive_stubMemoryGetSpaces(target_memory_space_t *outSpaces, size_t cap, size_t *outCount)
{
    (void)outSpaces;
    (void)cap;
    (void)outCount;
    debug_error("BUG: target_megadrive_stubMemoryGetSpaces called with E9K_ENABLE_MEGADRIVE=0");
    abort();
    return 0;
}

static int
target_megadrive_stubRegistersReadExtra(const char **outTitle, e9k_debug_processor_reg_t *outRegs, size_t cap, size_t *outCount)
{
    (void)outRegs;
    (void)cap;
    if (outTitle) {
        *outTitle = NULL;
    }
    if (outCount) {
        *outCount = 0;
    }
    return 1;
}

static SDL_Texture *
target_megadrive_stubGetBadgeTexture(SDL_Renderer *renderer, target_iface_t *t, int *outW, int *outH)
{
    (void)renderer;
    (void)t;
    (void)outW;
    (void)outH;
    debug_error("BUG: target_megadrive_stubGetBadgeTexture called with E9K_ENABLE_MEGADRIVE=0");
    abort();
    return NULL;
}

static void
target_megadrive_stubConfigControllerPorts(void)
{
    debug_error("BUG: target_megadrive_stubConfigControllerPorts called with E9K_ENABLE_MEGADRIVE=0");
    abort();
}

static int
target_megadrive_stubControllerMapButton(SDL_GameControllerButton button, unsigned *outId)
{
    (void)button;
    (void)outId;
    debug_error("BUG: target_megadrive_stubControllerMapButton called with E9K_ENABLE_MEGADRIVE=0");
    abort();
    return 0;
}

static target_iface_t target_megadrive_stubTarget = {
    .name = "MEGA DRIVE (DISABLED)",
    .defaultCorePath = target_megadrive_stubDefaultCorePath,
    .setConfigDefaults = target_megadrive_stubSetConfigDefaults,
    .setActiveDefaultsFromCurrentSystem = target_megadrive_stubSetActiveDefaultsFromCurrentSystem,
    .applyActiveSettingsToCurrentSystem = target_megadrive_stubApplyActiveSettingsToCurrentSystem,
    .configIsOk = target_megadrive_stubConfigIsOk,
    .needsRestart = target_megadrive_stubNeedsRestart,
    .settingsSaveButtonDisabled = target_megadrive_stubSettingsSaveButtonDisabled,
    .validateSettings = target_megadrive_stubValidateSettings,
    .settingsDefaults = target_megadrive_stubSettingsDefaults,
    .applyRomConfigForSelection = target_megadrive_stubApplyRomConfigForSelection,
    .settingsSetConfigPaths = target_megadrive_stubSettingsSetConfigPaths,
    .settingsRomPathChanged = target_megadrive_stubSettingsRomPathChanged,
    .settingsRomFolderChanged = target_megadrive_stubSettingsRomFolderChanged,
    .settingsCoreChanged = target_megadrive_stubSettingsCoreChanged,
    .settingsClearOptions = target_megadrive_stubSettingsClearOptions,
    .settingsLoadOptions = target_megadrive_stubSettingsLoadOptions,
    .settingsBuildModal = target_megadrive_stubSettingsBuildModal,
    .selectLibretroConfig = target_megadrive_stubSelectLibretroConfig,
    .coreOptionsHasGeneral = target_megadrive_stubCoreOptionsHasGeneral,
    .coreOptionsSaveClicked = target_megadrive_stubCoreOptionsSaveClicked,
    .coreOptionGetValue = target_megadrive_stubCoreOptionGetValue,
    .getLibretroCliConfig = target_megadrive_stubGetLibretroCliConfig,
    .onCoreStarted = target_megadrive_stubOnCoreStarted,
    .onVblank = target_megadrive_stubOnVblank,
    .libretroSelectConfig = target_megadrive_stubLibretroSelectConfig,
    .pickElfToolchainPaths = target_megadrive_stubPickElfToolchainPaths,
    .applyCoreOptions = target_megadrive_stubApplyCoreOptions,
    .validateAPI = target_megadrive_stubValidateAPI,
    .audioEnabled = target_megadrive_stubAudioEnabled,
    .audioEnable = target_megadrive_stubAudioEnable,
    .coreIndex = TARGET_MEGADRIVE,
    .mousePort = -1,
    .memoryGetLimits = target_megadrive_stubMemoryGetLimits,
    .memoryGetSpaces = target_megadrive_stubMemoryGetSpaces,
    .memoryTrackGetRanges = target_megadrive_stubMemoryTrackGetRanges,
    .registersReadExtra = target_megadrive_stubRegistersReadExtra,
    .getBadgeTexture = target_megadrive_stubGetBadgeTexture,
    .configControllerPorts = target_megadrive_stubConfigControllerPorts,
    .controllerMapButton = target_megadrive_stubControllerMapButton,
};

target_iface_t *
target_megadrive(void)
{
    return &target_megadrive_stubTarget;
}

void
emu_mega_setSpriteState(const e9k_debug_mega_sprite_state_t *state, int ready)
{
    (void)state;
    (void)ready;
}

const emu_system_iface_t emu_mega_iface = {0};

void
mega_sprite_debug_toggle(void)
{
}

int
mega_sprite_debug_is_open(void)
{
    return 0;
}

void
mega_sprite_debug_render(const e9k_debug_mega_sprite_state_t *st)
{
    (void)st;
}

int
mega_sprite_debug_handleKeydown(const SDL_KeyboardEvent *kev)
{
    (void)kev;
    return 0;
}

void
mega_sprite_debug_setMainWindowFocused(int focused)
{
    (void)focused;
}

void
mega_sprite_debug_persistConfig(FILE *file)
{
    (void)file;
}

int
mega_sprite_debug_loadConfigProperty(const char *prop, const char *value)
{
    (void)prop;
    (void)value;
    return 0;
}

int
megadrive_coreOptionsDirty(void)
{
    return 0;
}

void
megadrive_coreOptionsClear(void)
{
}

const char *
megadrive_coreOptionsGetValue(const char *key)
{
    (void)key;
    return NULL;
}

void
megadrive_coreOptionsSetValue(const char *key, const char *value)
{
    (void)key;
    (void)value;
}

int
megadrive_coreOptionsBuildPath(char *out, size_t cap, const char *saveDir, const char *romPath)
{
    (void)out;
    (void)cap;
    (void)saveDir;
    (void)romPath;
    return 0;
}

int
megadrive_coreOptionsLoadFromFile(const char *saveDir, const char *romPath)
{
    (void)saveDir;
    (void)romPath;
    return 0;
}

int
megadrive_coreOptionsWriteToFile(const char *saveDir, const char *romPath)
{
    (void)saveDir;
    (void)romPath;
    return 0;
}

int
megadrive_coreOptionsApplyFileToHost(const char *saveDir, const char *romPath)
{
    (void)saveDir;
    (void)romPath;
    return 0;
}
