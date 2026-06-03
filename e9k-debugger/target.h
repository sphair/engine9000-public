/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "emu.h"
#include "dasm.h"
#include "settings.h"
#include "e9k-lib.h"

#define TARGET_AMIGA  0
#define TARGET_NEOGEO 1
#define TARGET_MEGADRIVE 2

struct core_options_modal_state;
struct e9k_system_config;
struct retro_core_option_v2_definition;

typedef struct target_settings_modal
{
    e9ui_component_t *body;
    e9ui_component_t *footerWarning;
} target_settings_modal_t;

typedef struct target_memory_range
{
    uint32_t baseAddr;
    uint32_t size;
} target_memory_range_t;

typedef struct target_memory_space
{
    const char *value;
    const char *label;
    uint32_t minAddr;
    uint32_t maxAddr;
    int addressDigits;
    int processorMemory;
    uint32_t processorId;
} target_memory_space_t;

typedef struct target_memory_panel_options
{
    const char *spaceKey;
    const char *addressKey;
    const char *searchKey;
} target_memory_panel_options_t;

typedef struct target_iface
{
    const char *name;
    const emu_system_iface_t *emu;
    const dasm_iface_t *dasm;
    char bootSaveDir[PATH_MAX];
    char bootSystemDir[PATH_MAX];

    const char *(*defaultCorePath)(void);
  
    void (*setActiveDefaultsFromCurrentSystem)(void);
    void (*applyActiveSettingsToCurrentSystem)(void);
    void (*setConfigDefaults)(struct e9k_system_config *config);
    int (*configIsOk)(void);
    int (*needsRestart)(void);
    int (*settingsSaveButtonDisabled)(void);
    void (*validateSettings)(void);
    void (*settingsDefaults)(void);
    void (*applyRomConfigForSelection)(settings_romselect_state_t *st, const char **saveDirP, const char **romPathP);
    void (*settingsSetConfigPaths)(int hasElf,
                                  const char *elfPath,
                                  int hasSource,
                                  const char *sourceDir,
                                  int hasToolchain,
                                  const char *toolchainPrefix);
    void (*settingsRomPathChanged)(settings_romselect_state_t *st);
    void (*settingsRomFolderChanged)(void);
    void (*settingsCoreChanged)(void);
    void (*settingsClearOptions)(void);
    void (*settingsLoadOptions)(struct e9k_system_config *st);
    void (*settingsBuildModal)(e9ui_context_t *ctx, target_settings_modal_t *out);
    const struct e9k_libretro_config *(*selectLibretroConfig)(const struct e9k_system_config *cfg);
    int (*coreOptionsHasGeneral)(const struct core_options_modal_state *st);
    void (*coreOptionsSaveClicked)(e9ui_context_t *ctx, struct core_options_modal_state *st);
    const char *(*coreOptionGetValue)(const char *key);
    struct e9k_libretro_config *(*getLibretroCliConfig)(void);
    void (*onCoreStarted)(void);
    void (*onVblank)(void);
    void (*libretroSelectConfig)(void);
    void (*pickElfToolchainPaths)(const char **rawElf, const char **toolchainPrefix);
    void (*applyCoreOptions)(void);
    void (*validateAPI)(void);
    int (*audioEnabled)(void);
    void (*audioEnable)(int enabled);
    int coreIndex;
    int mousePort;
    SDL_Renderer *badgeRenderer;
    SDL_Texture *badge;
    int badgeW;
    int badgeH;
    int (*memoryGetLimits)(uint32_t *outMinAddr, uint32_t *outMaxAddr);
    int (*memoryGetSpaces)(target_memory_space_t *outSpaces, size_t cap, size_t *outCount);
    int (*memoryTrackGetRanges)(target_memory_range_t *outRanges, size_t cap, size_t *outCount);
    int (*memoryPanelOptions)(target_memory_panel_options_t *outOptions);
    int (*registersReadExtra)(const char **outTitle, e9k_debug_processor_reg_t *outRegs, size_t cap, size_t *outCount);
    SDL_Texture *(*getBadgeTexture)(SDL_Renderer *renderer, struct target_iface *t, int *outW, int *outH);
    void (*configControllerPorts)(void);
    int (*controllerMapButton)(SDL_GameControllerButton button, unsigned *outId);
    size_t (*romConfigCustomOptionCount)(void);
    const char *(*romConfigCustomOptionKeyAt)(size_t index);
    const char *(*romConfigGetActiveCustomOptionValue)(const char *key);
    void (*romConfigSetActiveCustomOptionValue)(const char *key, const char *value);
    void (*romConfigClearActiveCustomOptions)(void);
    int (*coreOptionsHideOptionKey)(const char *key);
    int (*coreOptionsIsSyntheticOptionKey)(const char *key);
    size_t (*coreOptionsSyntheticDefCount)(void);
    const struct retro_core_option_v2_definition *(*coreOptionsSyntheticDefAt)(size_t index);
    size_t (*coreOptionsMapDebuggerInputSpecIndex)(size_t specIndex);
    const char *(*coreOptionsDebuggerInputLabel)(const char *optionKey, const char *defaultLabel);
} target_iface_t;

void
target_nextTarget(void);

void
target_setTarget(target_iface_t *newTarget);

void
target_setTargetIndex(int index);

void
target_settingsClearAllOptions(void);

void
target_setConfigDefaults(void);

target_iface_t *
target_getByIndex(int index);

int
target_firstEnabledIndex(void);

target_iface_t *
target_firstEnabled(void);

int
target_coreOptionsIsSyntheticOptionKey(const char *key);

target_iface_t *target_amiga(void);

target_iface_t *target_neogeo(void);

target_iface_t *target_megadrive(void);

void target_ctor(void);

void
target_releaseUiResources(void);

extern target_iface_t *target;
