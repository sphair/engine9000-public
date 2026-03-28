/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "target.h"
#include "amiga_memview.h"
#include "custom_amiga.h"
#include "custom_log.h"
#include "custom_ui.h"
#include "debugger.h"
#include "debug.h"
#include "source_cpr.h"

#include <stdlib.h>
#include <string.h>

static void
target_amiga_stubSetConfigDefaults(e9k_system_config_t *config)
{
    if (!config) {
        return;
    }
    memset(&config->amiga, 0, sizeof(config->amiga));
}

static const char *
target_amiga_stubDefaultCorePath(void)
{
    debug_error("BUG: target_amiga_stubDefaultCorePath called with E9K_ENABLE_AMIGA=0");
    abort();
    return NULL;
}

static void
target_amiga_stubSetActiveDefaultsFromCurrentSystem(void)
{
    debug_error("BUG: target_amiga_stubSetActiveDefaultsFromCurrentSystem called with E9K_ENABLE_AMIGA=0");
    abort();
}

static void
target_amiga_stubApplyActiveSettingsToCurrentSystem(void)
{
    debug_error("BUG: target_amiga_stubApplyActiveSettingsToCurrentSystem called with E9K_ENABLE_AMIGA=0");
    abort();
}

static int
target_amiga_stubConfigIsOk(void)
{
    debug_error("BUG: target_amiga_stubConfigIsOk called with E9K_ENABLE_AMIGA=0");
    abort();
    return 0;
}

static int
target_amiga_stubNeedsRestart(void)
{
    debug_error("BUG: target_amiga_stubNeedsRestart called with E9K_ENABLE_AMIGA=0");
    abort();
    return 0;
}

static int
target_amiga_stubSettingsSaveButtonDisabled(void)
{
    debug_error("BUG: target_amiga_stubSettingsSaveButtonDisabled called with E9K_ENABLE_AMIGA=0");
    abort();
    return 1;
}

static void
target_amiga_stubValidateSettings(void)
{
    debug_error("BUG: target_amiga_stubValidateSettings called with E9K_ENABLE_AMIGA=0");
    abort();
}

static void
target_amiga_stubSettingsDefaults(void)
{
    debug_error("BUG: target_amiga_stubSettingsDefaults called with E9K_ENABLE_AMIGA=0");
    abort();
}

static void
target_amiga_stubApplyRomConfigForSelection(settings_romselect_state_t *st, const char **saveDirP, const char **romPathP)
{
    (void)st;
    (void)saveDirP;
    (void)romPathP;
    debug_error("BUG: target_amiga_stubApplyRomConfigForSelection called with E9K_ENABLE_AMIGA=0");
    abort();
}

static void
target_amiga_stubSettingsSetConfigPaths(int hasElf,
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
    debug_error("BUG: target_amiga_stubSettingsSetConfigPaths called with E9K_ENABLE_AMIGA=0");
    abort();
}

static void
target_amiga_stubSettingsRomPathChanged(settings_romselect_state_t *st)
{
    (void)st;
    debug_error("BUG: target_amiga_stubSettingsRomPathChanged called with E9K_ENABLE_AMIGA=0");
    abort();
}

static void
target_amiga_stubSettingsRomFolderChanged(void)
{
    debug_error("BUG: target_amiga_stubSettingsRomFolderChanged called with E9K_ENABLE_AMIGA=0");
    abort();
}

static void
target_amiga_stubSettingsCoreChanged(void)
{
    debug_error("BUG: target_amiga_stubSettingsCoreChanged called with E9K_ENABLE_AMIGA=0");
    abort();
}

static void
target_amiga_stubSettingsClearOptions(void)
{
    debug_error("BUG: target_amiga_stubSettingsClearOptions called with E9K_ENABLE_AMIGA=0");
    abort();
}

static void
target_amiga_stubSettingsLoadOptions(struct e9k_system_config *st)
{
    (void)st;
    debug_error("BUG: target_amiga_stubSettingsLoadOptions called with E9K_ENABLE_AMIGA=0");
    abort();
}

static void
target_amiga_stubSettingsBuildModal(e9ui_context_t *ctx, target_settings_modal_t *out)
{
    (void)ctx;
    (void)out;
    debug_error("BUG: target_amiga_stubSettingsBuildModal called with E9K_ENABLE_AMIGA=0");
    abort();
}

static const struct e9k_libretro_config *
target_amiga_stubSelectLibretroConfig(const struct e9k_system_config *cfg)
{
    (void)cfg;
    debug_error("BUG: target_amiga_stubSelectLibretroConfig called with E9K_ENABLE_AMIGA=0");
    abort();
    return NULL;
}

static int
target_amiga_stubCoreOptionsHasGeneral(const struct core_options_modal_state *st)
{
    (void)st;
    debug_error("BUG: target_amiga_stubCoreOptionsHasGeneral called with E9K_ENABLE_AMIGA=0");
    abort();
    return 0;
}

static void
target_amiga_stubCoreOptionsSaveClicked(e9ui_context_t *ctx, struct core_options_modal_state *st)
{
    (void)ctx;
    (void)st;
    debug_error("BUG: target_amiga_stubCoreOptionsSaveClicked called with E9K_ENABLE_AMIGA=0");
    abort();
}

static const char *
target_amiga_stubCoreOptionGetValue(const char *key)
{
    (void)key;
    debug_error("BUG: target_amiga_stubCoreOptionGetValue called with E9K_ENABLE_AMIGA=0");
    abort();
    return NULL;
}

static struct e9k_libretro_config *
target_amiga_stubGetLibretroCliConfig(void)
{
    debug_error("BUG: target_amiga_stubGetLibretroCliConfig called with E9K_ENABLE_AMIGA=0");
    abort();
    return NULL;
}

static void
target_amiga_stubOnVblank(void)
{
    debug_error("BUG: target_amiga_stubOnVblank called with E9K_ENABLE_AMIGA=0");
    abort();
}

static void
target_amiga_stubLibretroSelectConfig(void)
{
    debug_error("BUG: target_amiga_stubLibretroSelectConfig called with E9K_ENABLE_AMIGA=0");
    abort();
}

static void
target_amiga_stubPickElfToolchainPaths(const char **rawElf, const char **toolchainPrefix)
{
    (void)rawElf;
    (void)toolchainPrefix;
    debug_error("BUG: target_amiga_stubPickElfToolchainPaths called with E9K_ENABLE_AMIGA=0");
    abort();
}

static void
target_amiga_stubApplyCoreOptions(void)
{
    debug_error("BUG: target_amiga_stubApplyCoreOptions called with E9K_ENABLE_AMIGA=0");
    abort();
}

static void
target_amiga_stubValidateAPI(void)
{
    debug_error("BUG: target_amiga_stubValidateAPI called with E9K_ENABLE_AMIGA=0");
    abort();
}

static int
target_amiga_stubAudioEnabled(void)
{
    debug_error("BUG: target_amiga_stubAudioEnabled called with E9K_ENABLE_AMIGA=0");
    abort();
    return 0;
}

static void
target_amiga_stubAudioEnable(int enabled)
{
    (void)enabled;
    debug_error("BUG: target_amiga_stubAudioEnable called with E9K_ENABLE_AMIGA=0");
    abort();
}

static int
target_amiga_stubMemoryGetLimits(uint32_t *outMinAddr, uint32_t *outMaxAddr)
{
    (void)outMinAddr;
    (void)outMaxAddr;
    debug_error("BUG: target_amiga_stubMemoryGetLimits called with E9K_ENABLE_AMIGA=0");
    abort();
    return 0;
}

static int
target_amiga_stubMemoryTrackGetRanges(target_memory_range_t *outRanges, size_t cap, size_t *outCount)
{
    (void)outRanges;
    (void)cap;
    (void)outCount;
    debug_error("BUG: target_amiga_stubMemoryTrackGetRanges called with E9K_ENABLE_AMIGA=0");
    abort();
    return 0;
}

static SDL_Texture *
target_amiga_stubGetBadgeTexture(SDL_Renderer *renderer, target_iface_t *t, int *outW, int *outH)
{
    (void)renderer;
    (void)t;
    (void)outW;
    (void)outH;
    debug_error("BUG: target_amiga_stubGetBadgeTexture called with E9K_ENABLE_AMIGA=0");
    abort();
    return NULL;
}

static void
target_amiga_stubConfigControllerPorts(void)
{
    debug_error("BUG: target_amiga_stubConfigControllerPorts called with E9K_ENABLE_AMIGA=0");
    abort();
}

static int
target_amiga_stubControllerMapButton(SDL_GameControllerButton button, unsigned *outId)
{
    (void)button;
    (void)outId;
    debug_error("BUG: target_amiga_stubControllerMapButton called with E9K_ENABLE_AMIGA=0");
    abort();
    return 0;
}

int
custom_amiga_init(void)
{
    return 0;
}

void
custom_amiga_shutdown(void)
{
}

void
custom_amiga_toggle(void)
{
}

int
custom_amiga_isOpen(void)
{
    return 0;
}

void
custom_amiga_setMainWindowFocused(int focused)
{
    (void)focused;
}

void
custom_amiga_render(void)
{
}

void
custom_amiga_persistConfig(FILE *file)
{
    (void)file;
}

int
custom_amiga_loadConfigProperty(const char *prop, const char *value)
{
    (void)prop;
    (void)value;
    return 0;
}

int
amiga_memview_init(void)
{
    return 0;
}

void
amiga_memview_shutdown(void)
{
}

void
amiga_memview_toggle(void)
{
}

int
amiga_memview_isOpen(void)
{
    return 0;
}

void
amiga_memview_setMainWindowFocused(int focused)
{
    (void)focused;
}

void
amiga_memview_render(void)
{
}

void
amiga_memview_persistConfig(FILE *file)
{
    (void)file;
}

int
amiga_memview_loadConfigProperty(const char *prop, const char *value)
{
    (void)prop;
    (void)value;
    return 0;
}

int
custom_log_init(void)
{
    return 0;
}

void
custom_log_shutdown(void)
{
}

void
custom_log_toggle(void)
{
}

int
custom_log_isOpen(void)
{
    return 0;
}

void
custom_log_setMainWindowFocused(int focused)
{
    (void)focused;
}

void
custom_log_render(void)
{
}

void
custom_log_persistConfig(FILE *file)
{
    (void)file;
}

int
custom_log_loadConfigProperty(const char *prop, const char *value)
{
    (void)prop;
    (void)value;
    return 0;
}

void
custom_log_captureFrame(const e9k_debug_ami_custom_log_entry_t *entries,
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
custom_ui_init(void)
{
    return 0;
}

void
custom_ui_shutdown(void)
{
}

void
custom_ui_toggle(void)
{
}

int
custom_ui_isOpen(void)
{
    return 0;
}

int
custom_ui_getBlitterVisDecay(void)
{
    return 0;
}

int
custom_ui_getEstimateFpsEnabled(void)
{
    return 0;
}

int
custom_ui_getCopperLimitEnabled(void)
{
    return 0;
}

int
custom_ui_getCopperLimitRange(int *outStart, int *outEnd)
{
    (void)outStart;
    (void)outEnd;
    return 0;
}

void
custom_ui_setCopperLimitRange(int start, int end)
{
    (void)start;
    (void)end;
}

int
custom_ui_getBplptrBlockEnabled(void)
{
    return 0;
}

int
custom_ui_getBplptrLineLimitRange(int *outStart, int *outEnd)
{
    (void)outStart;
    (void)outEnd;
    return 0;
}

void
custom_ui_setBplptrLineLimitRange(int start, int end)
{
    (void)start;
    (void)end;
}

void
custom_ui_setMainWindowFocused(int focused)
{
    (void)focused;
}

void
custom_ui_render(void)
{
}

void
custom_ui_persistConfig(FILE *file)
{
    (void)file;
}

int
custom_ui_loadConfigProperty(const char *prop, const char *value)
{
    (void)prop;
    (void)value;
    return 0;
}

int
source_cpr_isModeAvailable(void)
{
    return 0;
}

uint64_t
source_cpr_resolveAnchorAddr(uint64_t addr)
{
    uint64_t aligned = addr & 0x00ffffffull;
    aligned &= ~3ull;
    return aligned;
}

uint64_t
source_cpr_getCurrentAddr(source_pane_state_t *st)
{
    if (st && st->overrideActive) {
        return source_cpr_resolveAnchorAddr(st->overrideAddr);
    }
    if (st) {
        return source_cpr_resolveAnchorAddr(st->scrollAnchorAddr);
    }
    return 0;
}

int
source_cpr_getWindow(source_pane_state_t *st, int maxLines, uint64_t *outCurAddr,
                     const char ***outLines, const uint64_t **outAddrs, int *outCount)
{
    (void)st;
    (void)maxLines;
    if (outCurAddr) {
        *outCurAddr = 0;
    }
    if (outLines) {
        *outLines = NULL;
    }
    if (outAddrs) {
        *outAddrs = NULL;
    }
    if (outCount) {
        *outCount = 0;
    }
    return 0;
}

int
source_cpr_buildRegisterOptions(source_pane_state_t *st)
{
    (void)st;
    return 0;
}

int
source_cpr_commitInlineEdit(source_pane_state_t *st, e9ui_context_t *ctx, e9ui_component_t *editor,
                            const char *text)
{
    (void)st;
    (void)ctx;
    (void)editor;
    (void)text;
    return 0;
}

int
source_cpr_beginInlineWordsEditAtPoint(e9ui_component_t *self, e9ui_context_t *ctx, source_pane_state_t *st,
                                       int mx, int my)
{
    (void)self;
    (void)ctx;
    (void)st;
    (void)mx;
    (void)my;
    return 0;
}

int
source_cpr_beginInlineRegisterEditAtPoint(e9ui_component_t *self, e9ui_context_t *ctx, source_pane_state_t *st,
                                          int mx, int my)
{
    (void)self;
    (void)ctx;
    (void)st;
    (void)mx;
    (void)my;
    return 0;
}

int
source_cpr_beginInlineValueEditAtPoint(e9ui_component_t *self, e9ui_context_t *ctx, source_pane_state_t *st,
                                       int mx, int my)
{
    (void)self;
    (void)ctx;
    (void)st;
    (void)mx;
    (void)my;
    return 0;
}

void
source_cpr_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)self;
    (void)ctx;
}

static target_iface_t target_amiga_stubTarget = {
    .name = "AMIGA (DISABLED)",
    .defaultCorePath = target_amiga_stubDefaultCorePath,
    .setConfigDefaults = target_amiga_stubSetConfigDefaults,
    .setActiveDefaultsFromCurrentSystem = target_amiga_stubSetActiveDefaultsFromCurrentSystem,
    .applyActiveSettingsToCurrentSystem = target_amiga_stubApplyActiveSettingsToCurrentSystem,
    .configIsOk = target_amiga_stubConfigIsOk,
    .needsRestart = target_amiga_stubNeedsRestart,
    .settingsSaveButtonDisabled = target_amiga_stubSettingsSaveButtonDisabled,
    .validateSettings = target_amiga_stubValidateSettings,
    .settingsDefaults = target_amiga_stubSettingsDefaults,
    .applyRomConfigForSelection = target_amiga_stubApplyRomConfigForSelection,
    .settingsSetConfigPaths = target_amiga_stubSettingsSetConfigPaths,
    .settingsRomPathChanged = target_amiga_stubSettingsRomPathChanged,
    .settingsRomFolderChanged = target_amiga_stubSettingsRomFolderChanged,
    .settingsCoreChanged = target_amiga_stubSettingsCoreChanged,
    .settingsClearOptions = target_amiga_stubSettingsClearOptions,
    .settingsLoadOptions = target_amiga_stubSettingsLoadOptions,
    .settingsBuildModal = target_amiga_stubSettingsBuildModal,
    .selectLibretroConfig = target_amiga_stubSelectLibretroConfig,
    .coreOptionsHasGeneral = target_amiga_stubCoreOptionsHasGeneral,
    .coreOptionsSaveClicked = target_amiga_stubCoreOptionsSaveClicked,
    .coreOptionGetValue = target_amiga_stubCoreOptionGetValue,
    .getLibretroCliConfig = target_amiga_stubGetLibretroCliConfig,
    .onVblank = target_amiga_stubOnVblank,
    .libretroSelectConfig = target_amiga_stubLibretroSelectConfig,
    .pickElfToolchainPaths = target_amiga_stubPickElfToolchainPaths,
    .applyCoreOptions = target_amiga_stubApplyCoreOptions,
    .validateAPI = target_amiga_stubValidateAPI,
    .audioEnabled = target_amiga_stubAudioEnabled,
    .audioEnable = target_amiga_stubAudioEnable,
    .coreIndex = TARGET_AMIGA,
    .mousePort = -1,
    .memoryGetLimits = target_amiga_stubMemoryGetLimits,
    .memoryTrackGetRanges = target_amiga_stubMemoryTrackGetRanges,
    .getBadgeTexture = target_amiga_stubGetBadgeTexture,
    .configControllerPorts = target_amiga_stubConfigControllerPorts,
    .controllerMapButton = target_amiga_stubControllerMapButton,
};

target_iface_t *
target_amiga(void)
{
    return &target_amiga_stubTarget;
}
