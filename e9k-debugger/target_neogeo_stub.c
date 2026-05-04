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
#include "emu_geo.h"
#include "neogeo_core_options.h"
#include "neogeo_audio_vis.h"
#include "neogeo_sprite_debug.h"
#include "neogeo_register_log.h"

static void
target_neogeo_stubSetConfigDefaults(e9k_system_config_t *config)
{
    if (!config) {
        return;
    }
    memset(&config->neogeo, 0, sizeof(config->neogeo));
}

static const char *
target_neogeo_stubDefaultCorePath(void)
{
    debug_error("BUG: target_neogeo_stubDefaultCorePath called with E9K_ENABLE_NEOGEO=0");
    abort();
    return NULL;
}

static void
target_neogeo_stubSetActiveDefaultsFromCurrentSystem(void)
{
    debug_error("BUG: target_neogeo_stubSetActiveDefaultsFromCurrentSystem called with E9K_ENABLE_NEOGEO=0");
    abort();
}

static void
target_neogeo_stubApplyActiveSettingsToCurrentSystem(void)
{
    debug_error("BUG: target_neogeo_stubApplyActiveSettingsToCurrentSystem called with E9K_ENABLE_NEOGEO=0");
    abort();
}

static int
target_neogeo_stubConfigIsOk(void)
{
    debug_error("BUG: target_neogeo_stubConfigIsOk called with E9K_ENABLE_NEOGEO=0");
    abort();
    return 0;
}

static int
target_neogeo_stubNeedsRestart(void)
{
    debug_error("BUG: target_neogeo_stubNeedsRestart called with E9K_ENABLE_NEOGEO=0");
    abort();
    return 0;
}

static int
target_neogeo_stubSettingsSaveButtonDisabled(void)
{
    debug_error("BUG: target_neogeo_stubSettingsSaveButtonDisabled called with E9K_ENABLE_NEOGEO=0");
    abort();
    return 1;
}

static void
target_neogeo_stubValidateSettings(void)
{
    debug_error("BUG: target_neogeo_stubValidateSettings called with E9K_ENABLE_NEOGEO=0");
    abort();
}

static void
target_neogeo_stubSettingsDefaults(void)
{
    debug_error("BUG: target_neogeo_stubSettingsDefaults called with E9K_ENABLE_NEOGEO=0");
    abort();
}

static void
target_neogeo_stubApplyRomConfigForSelection(settings_romselect_state_t *st, const char **saveDirP, const char **romPathP)
{
    (void)st;
    (void)saveDirP;
    (void)romPathP;
    debug_error("BUG: target_neogeo_stubApplyRomConfigForSelection called with E9K_ENABLE_NEOGEO=0");
    abort();
}

static void
target_neogeo_stubSettingsSetConfigPaths(int hasElf,
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
    debug_error("BUG: target_neogeo_stubSettingsSetConfigPaths called with E9K_ENABLE_NEOGEO=0");
    abort();
}

static void
target_neogeo_stubSettingsRomPathChanged(settings_romselect_state_t *st)
{
    (void)st;
    debug_error("BUG: target_neogeo_stubSettingsRomPathChanged called with E9K_ENABLE_NEOGEO=0");
    abort();
}

static void
target_neogeo_stubSettingsRomFolderChanged(void)
{
    debug_error("BUG: target_neogeo_stubSettingsRomFolderChanged called with E9K_ENABLE_NEOGEO=0");
    abort();
}

static void
target_neogeo_stubSettingsCoreChanged(void)
{
    debug_error("BUG: target_neogeo_stubSettingsCoreChanged called with E9K_ENABLE_NEOGEO=0");
    abort();
}

static void
target_neogeo_stubSettingsClearOptions(void)
{
    debug_error("BUG: target_neogeo_stubSettingsClearOptions called with E9K_ENABLE_NEOGEO=0");
    abort();
}

static void
target_neogeo_stubSettingsLoadOptions(struct e9k_system_config *st)
{
    (void)st;
    debug_error("BUG: target_neogeo_stubSettingsLoadOptions called with E9K_ENABLE_NEOGEO=0");
    abort();
}

static void
target_neogeo_stubSettingsBuildModal(e9ui_context_t *ctx, target_settings_modal_t *out)
{
    (void)ctx;
    (void)out;
    debug_error("BUG: target_neogeo_stubSettingsBuildModal called with E9K_ENABLE_NEOGEO=0");
    abort();
}

static const struct e9k_libretro_config *
target_neogeo_stubSelectLibretroConfig(const struct e9k_system_config *cfg)
{
    (void)cfg;
    debug_error("BUG: target_neogeo_stubSelectLibretroConfig called with E9K_ENABLE_NEOGEO=0");
    abort();
    return NULL;
}

static int
target_neogeo_stubCoreOptionsHasGeneral(const struct core_options_modal_state *st)
{
    (void)st;
    debug_error("BUG: target_neogeo_stubCoreOptionsHasGeneral called with E9K_ENABLE_NEOGEO=0");
    abort();
    return 0;
}

static void
target_neogeo_stubCoreOptionsSaveClicked(e9ui_context_t *ctx, struct core_options_modal_state *st)
{
    (void)ctx;
    (void)st;
    debug_error("BUG: target_neogeo_stubCoreOptionsSaveClicked called with E9K_ENABLE_NEOGEO=0");
    abort();
}

static const char *
target_neogeo_stubCoreOptionGetValue(const char *key)
{
    (void)key;
    debug_error("BUG: target_neogeo_stubCoreOptionGetValue called with E9K_ENABLE_NEOGEO=0");
    abort();
    return NULL;
}

static struct e9k_libretro_config *
target_neogeo_stubGetLibretroCliConfig(void)
{
    debug_error("BUG: target_neogeo_stubGetLibretroCliConfig called with E9K_ENABLE_NEOGEO=0");
    abort();
    return NULL;
}

static void
target_neogeo_stubOnCoreStarted(void)
{
    debug_error("BUG: target_neogeo_stubOnCoreStarted called with E9K_ENABLE_NEOGEO=0");
    abort();
}

static void
target_neogeo_stubOnVblank(void)
{
    debug_error("BUG: target_neogeo_stubOnVblank called with E9K_ENABLE_NEOGEO=0");
    abort();
}

static void
target_neogeo_stubLibretroSelectConfig(void)
{
    debug_error("BUG: target_neogeo_stubLibretroSelectConfig called with E9K_ENABLE_NEOGEO=0");
    abort();
}

static void
target_neogeo_stubPickElfToolchainPaths(const char **rawElf, const char **toolchainPrefix)
{
    (void)rawElf;
    (void)toolchainPrefix;
    debug_error("BUG: target_neogeo_stubPickElfToolchainPaths called with E9K_ENABLE_NEOGEO=0");
    abort();
}

static void
target_neogeo_stubApplyCoreOptions(void)
{
    debug_error("BUG: target_neogeo_stubApplyCoreOptions called with E9K_ENABLE_NEOGEO=0");
    abort();
}

static void
target_neogeo_stubValidateAPI(void)
{
    debug_error("BUG: target_neogeo_stubValidateAPI called with E9K_ENABLE_NEOGEO=0");
    abort();
}

static int
target_neogeo_stubAudioEnabled(void)
{
    debug_error("BUG: target_neogeo_stubAudioEnabled called with E9K_ENABLE_NEOGEO=0");
    abort();
    return 0;
}

static void
target_neogeo_stubAudioEnable(int enabled)
{
    (void)enabled;
    debug_error("BUG: target_neogeo_stubAudioEnable called with E9K_ENABLE_NEOGEO=0");
    abort();
}

static int
target_neogeo_stubMemoryGetLimits(uint32_t *outMinAddr, uint32_t *outMaxAddr)
{
    (void)outMinAddr;
    (void)outMaxAddr;
    debug_error("BUG: target_neogeo_stubMemoryGetLimits called with E9K_ENABLE_NEOGEO=0");
    abort();
    return 0;
}

static int
target_neogeo_stubMemoryTrackGetRanges(target_memory_range_t *outRanges, size_t cap, size_t *outCount)
{
    (void)outRanges;
    (void)cap;
    (void)outCount;
    debug_error("BUG: target_neogeo_stubMemoryTrackGetRanges called with E9K_ENABLE_NEOGEO=0");
    abort();
    return 0;
}

static int
target_neogeo_stubMemoryGetSpaces(target_memory_space_t *outSpaces, size_t cap, size_t *outCount)
{
    (void)outSpaces;
    (void)cap;
    (void)outCount;
    debug_error("BUG: target_neogeo_stubMemoryGetSpaces called with E9K_ENABLE_NEOGEO=0");
    abort();
    return 0;
}

static int
target_neogeo_stubRegistersReadExtra(const char **outTitle, e9k_debug_processor_reg_t *outRegs, size_t cap, size_t *outCount)
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
target_neogeo_stubGetBadgeTexture(SDL_Renderer *renderer, target_iface_t *t, int *outW, int *outH)
{
    (void)renderer;
    (void)t;
    (void)outW;
    (void)outH;
    debug_error("BUG: target_neogeo_stubGetBadgeTexture called with E9K_ENABLE_NEOGEO=0");
    abort();
    return NULL;
}

static void
target_neogeo_stubConfigControllerPorts(void)
{
    debug_error("BUG: target_neogeo_stubConfigControllerPorts called with E9K_ENABLE_NEOGEO=0");
    abort();
}

static int
target_neogeo_stubControllerMapButton(SDL_GameControllerButton button, unsigned *outId)
{
    (void)button;
    (void)outId;
    debug_error("BUG: target_neogeo_stubControllerMapButton called with E9K_ENABLE_NEOGEO=0");
    abort();
    return 0;
}

static target_iface_t target_neogeo_stubTarget = {
    .name = "NEO GEO (DISABLED)",
    .defaultCorePath = target_neogeo_stubDefaultCorePath,
    .setConfigDefaults = target_neogeo_stubSetConfigDefaults,
    .setActiveDefaultsFromCurrentSystem = target_neogeo_stubSetActiveDefaultsFromCurrentSystem,
    .applyActiveSettingsToCurrentSystem = target_neogeo_stubApplyActiveSettingsToCurrentSystem,
    .configIsOk = target_neogeo_stubConfigIsOk,
    .needsRestart = target_neogeo_stubNeedsRestart,
    .settingsSaveButtonDisabled = target_neogeo_stubSettingsSaveButtonDisabled,
    .validateSettings = target_neogeo_stubValidateSettings,
    .settingsDefaults = target_neogeo_stubSettingsDefaults,
    .applyRomConfigForSelection = target_neogeo_stubApplyRomConfigForSelection,
    .settingsSetConfigPaths = target_neogeo_stubSettingsSetConfigPaths,
    .settingsRomPathChanged = target_neogeo_stubSettingsRomPathChanged,
    .settingsRomFolderChanged = target_neogeo_stubSettingsRomFolderChanged,
    .settingsCoreChanged = target_neogeo_stubSettingsCoreChanged,
    .settingsClearOptions = target_neogeo_stubSettingsClearOptions,
    .settingsLoadOptions = target_neogeo_stubSettingsLoadOptions,
    .settingsBuildModal = target_neogeo_stubSettingsBuildModal,
    .selectLibretroConfig = target_neogeo_stubSelectLibretroConfig,
    .coreOptionsHasGeneral = target_neogeo_stubCoreOptionsHasGeneral,
    .coreOptionsSaveClicked = target_neogeo_stubCoreOptionsSaveClicked,
    .coreOptionGetValue = target_neogeo_stubCoreOptionGetValue,
    .getLibretroCliConfig = target_neogeo_stubGetLibretroCliConfig,
    .onCoreStarted = target_neogeo_stubOnCoreStarted,
    .onVblank = target_neogeo_stubOnVblank,
    .libretroSelectConfig = target_neogeo_stubLibretroSelectConfig,
    .pickElfToolchainPaths = target_neogeo_stubPickElfToolchainPaths,
    .applyCoreOptions = target_neogeo_stubApplyCoreOptions,
    .validateAPI = target_neogeo_stubValidateAPI,
    .audioEnabled = target_neogeo_stubAudioEnabled,
    .audioEnable = target_neogeo_stubAudioEnable,
    .coreIndex = TARGET_NEOGEO,
    .mousePort = -1,
    .memoryGetLimits = target_neogeo_stubMemoryGetLimits,
    .memoryGetSpaces = target_neogeo_stubMemoryGetSpaces,
    .memoryTrackGetRanges = target_neogeo_stubMemoryTrackGetRanges,
    .registersReadExtra = target_neogeo_stubRegistersReadExtra,
    .getBadgeTexture = target_neogeo_stubGetBadgeTexture,
    .configControllerPorts = target_neogeo_stubConfigControllerPorts,
    .controllerMapButton = target_neogeo_stubControllerMapButton,
};

target_iface_t *
target_neogeo(void)
{
    return &target_neogeo_stubTarget;
}

void
emu_geo_setSpriteState(const e9k_debug_sprite_state_t *state, int ready)
{
    (void)state;
    (void)ready;
}

void
emu_geo_setAudioFrame(const e9k_debug_audio_frame_t *frame, int ready)
{
    (void)frame;
    (void)ready;
}

void
emu_geo_shutdown(void)
{
}

const emu_system_iface_t emu_geo_iface = {0};

void
neogeo_sprite_debug_toggle(void)
{
}

int
neogeo_sprite_debug_is_open(void)
{
    return 0;
}

void
neogeo_sprite_debug_render(const e9k_debug_sprite_state_t *st)
{
    (void)st;
}

int
neogeo_sprite_debug_handleKeydown(const SDL_KeyboardEvent *kev)
{
    (void)kev;
    return 0;
}

void
neogeo_sprite_debug_setMainWindowFocused(int focused)
{
    (void)focused;
}

void
neogeo_sprite_debug_persistConfig(FILE *file)
{
    (void)file;
}

int
neogeo_sprite_debug_loadConfigProperty(const char *prop, const char *value)
{
    (void)prop;
    (void)value;
    return 0;
}

void
neogeo_audio_vis_toggle(void)
{
}

int
neogeo_audio_vis_isOpen(void)
{
    return 0;
}

void
neogeo_audio_vis_render(const e9k_debug_audio_frame_t *frame)
{
    (void)frame;
}

int
neogeo_audio_vis_handleKeydown(const SDL_KeyboardEvent *kev)
{
    (void)kev;
    return 0;
}

void
neogeo_audio_vis_setMainWindowFocused(int focused)
{
    (void)focused;
}

void
neogeo_audio_vis_persistConfig(FILE *file)
{
    (void)file;
}

int
neogeo_audio_vis_loadConfigProperty(const char *prop, const char *value)
{
    (void)prop;
    (void)value;
    return 0;
}

void
neogeo_palette_debug_toggle(void)
{
}

int
neogeo_palette_debug_isOpen(void)
{
    return 0;
}

int
neogeo_palette_debug_handleKeydown(const SDL_KeyboardEvent *kev)
{
    (void)kev;
    return 0;
}

void
neogeo_palette_debug_setMainWindowFocused(int focused)
{
    (void)focused;
}

void
neogeo_palette_debug_render(void)
{
}

void
neogeo_palette_debug_persistConfig(FILE *file)
{
    (void)file;
}

int
neogeo_palette_debug_loadConfigProperty(const char *prop, const char *value)
{
    (void)prop;
    (void)value;
    return 0;
}

void
neogeo_memview_toggle(void)
{
}

int
neogeo_memview_isOpen(void)
{
    return 0;
}

void
neogeo_memview_setMainWindowFocused(int focused)
{
    (void)focused;
}

void
neogeo_memview_render(void)
{
}

void
neogeo_memview_persistConfig(FILE *file)
{
    (void)file;
}

int
neogeo_memview_loadConfigProperty(const char *prop, const char *value)
{
    (void)prop;
    (void)value;
    return 0;
}

int
neogeo_register_log_init(void)
{
    return 0;
}

void
neogeo_register_log_shutdown(void)
{
}

void
neogeo_register_log_toggle(void)
{
}

int
neogeo_register_log_isOpen(void)
{
    return 0;
}

void
neogeo_register_log_setMainWindowFocused(int focused)
{
    (void)focused;
}

void
neogeo_register_log_render(void)
{
}

void
neogeo_register_log_persistConfig(FILE *file)
{
    (void)file;
}

int
neogeo_register_log_loadConfigProperty(const char *prop, const char *value)
{
    (void)prop;
    (void)value;
    return 0;
}

void
neogeo_register_log_captureFrame(const e9k_debug_geo_register_log_entry_t *entries,
                                 size_t count,
                                 uint32_t dropped,
                                 uint64_t frameNo)
{
    (void)entries;
    (void)count;
    (void)dropped;
    (void)frameNo;
}

int
neogeo_coreOptionsDirty(void)
{
    return 0;
}

void
neogeo_coreOptionsClear(void)
{
}

const char *
neogeo_coreOptionsGetValue(const char *key)
{
    (void)key;
    return NULL;
}

void
neogeo_coreOptionsSetValue(const char *key, const char *value)
{
    (void)key;
    (void)value;
}

int
neogeo_coreOptionsBuildPath(char *out, size_t cap, const char *saveDir, const char *romPath)
{
    (void)out;
    (void)cap;
    (void)saveDir;
    (void)romPath;
    return 0;
}

int
neogeo_coreOptionsLoadFromFile(const char *saveDir, const char *romPath)
{
    (void)saveDir;
    (void)romPath;
    return 0;
}

int
neogeo_coreOptionsWriteToFile(const char *saveDir, const char *romPath)
{
    (void)saveDir;
    (void)romPath;
    return 0;
}

int
neogeo_coreOptionsApplyFileToHost(const char *saveDir, const char *romPath)
{
    (void)saveDir;
    (void)romPath;
    return 0;
}
