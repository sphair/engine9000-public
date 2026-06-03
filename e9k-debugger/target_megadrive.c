/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "debugger.h"
#include "target.h"
#include "emu_mega.h"
#include "rom_config.h"
#include "debugger_input_bindings.h"
#include "megadrive_core_options.h"
#include "core_options.h"
#include "libretro_host.h"
#include "mega_audio_vis.h"
#include "system_badge.h"
#include "alloc.h"
#include "file.h"
#include "strutil.h"

#define TARGET_MEGADRIVE_Z80_PROCESSOR_ID 1u

static char target_megadrive_activeRomMemorySpace[16];
static char target_megadrive_activeRomMemoryAddress[32];
static char target_megadrive_activeRomMemorySearch[128];

static const char *
target_megadrive_defaultCorePath(void);

static const char *
target_megadrive_memorySpaceOptionKey(void)
{
    return "e9k_debugger_megadrive_memory_space";
}

static const char *
target_megadrive_memoryAddressOptionKey(void)
{
    return "e9k_debugger_megadrive_memory_address";
}

static const char *
target_megadrive_memorySearchOptionKey(void)
{
    return "e9k_debugger_megadrive_memory_search";
}

static void
target_megadrive_setConfigDefaults(e9k_system_config_t *config)
{
    if (!config) {
        return;
    }
#if E9K_ENABLE_MEGADRIVE
    snprintf(config->megadrive.libretro.systemDir, sizeof(config->megadrive.libretro.systemDir), "./system");
    snprintf(config->megadrive.libretro.saveDir, sizeof(config->megadrive.libretro.saveDir), "./saves");
    config->megadrive.libretro.sourceDir[0] = '\0';
    snprintf(config->megadrive.libretro.toolchainPrefix, sizeof(config->megadrive.libretro.toolchainPrefix), "m68k-elf");
    config->megadrive.libretro.audioBufferMs = 250;
    config->megadrive.libretro.exePath[0] = '\0';
    config->megadrive.romFolder[0] = '\0';
#else
    memset(&config->megadrive, 0, sizeof(config->megadrive));
#endif
}

static void
target_megadrive_settingsClearOptions(void)
{
    megadrive_coreOptionsClear();
}

static void
target_megadrive_settingsBuildModal(e9ui_context_t *ctx, target_settings_modal_t *out)
{
    if (!out || !ctx) {
        return;
    }
    out->body = NULL;
    out->footerWarning = NULL;

    const char *romExts[] = { "*.bin", "*.gen", "*.md", "*.smd", "*.sms", "*.zip" };
    const char *elfExts[] = { "*.elf", "*.txt" };

    settings_romselect_state_t *romState = (settings_romselect_state_t *)alloc_calloc(1, sizeof(*romState));
    if (romState) {
        romState->romPath = debugger.settingsEdit.megadrive.libretro.romPath;
    }

    e9ui_component_t *fsRom = e9ui_fileSelect_make("ROM", 120, 600, "...", romExts, (int)countof(romExts), E9UI_FILESELECT_FILE);
    e9ui_component_t *fsElf = e9ui_fileSelect_make("ELF", 120, 600, "...", elfExts, (int)countof(elfExts), E9UI_FILESELECT_FILE);
    e9ui_component_t *fsBios = e9ui_fileSelect_make("BIOS FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
    e9ui_component_t *fsSaves = e9ui_fileSelect_make("SAVES FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
    e9ui_component_t *fsSource = e9ui_fileSelect_make("SOURCE FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);

    if (fsRom) {
        e9ui_fileSelect_setText(fsRom, debugger.settingsEdit.megadrive.libretro.romPath);
        e9ui_fileSelect_setOnChange(fsRom, settings_romPathChanged, romState);
    }
    if (fsElf) {
        e9ui_fileSelect_setAllowEmpty(fsElf, 1);
        e9ui_fileSelect_setText(fsElf, debugger.settingsEdit.megadrive.libretro.exePath);
        e9ui_fileSelect_setOnChange(fsElf, settings_pathChanged, debugger.settingsEdit.megadrive.libretro.exePath);
    }
    if (fsBios) {
        e9ui_fileSelect_setText(fsBios, debugger.settingsEdit.megadrive.libretro.systemDir);
        e9ui_fileSelect_setOnChange(fsBios, settings_pathChanged, debugger.settingsEdit.megadrive.libretro.systemDir);
    }
    if (fsSaves) {
        e9ui_fileSelect_setText(fsSaves, debugger.settingsEdit.megadrive.libretro.saveDir);
        e9ui_fileSelect_setOnChange(fsSaves, settings_pathChanged, debugger.settingsEdit.megadrive.libretro.saveDir);
    }
    if (fsSource) {
        e9ui_fileSelect_setAllowEmpty(fsSource, 1);
        e9ui_fileSelect_setText(fsSource, debugger.settingsEdit.megadrive.libretro.sourceDir);
        e9ui_fileSelect_setOnChange(fsSource, settings_pathChanged, debugger.settingsEdit.megadrive.libretro.sourceDir);
    }
    e9ui_component_t *ltToolchain = e9ui_labeled_textbox_make("TOOLCHAIN PREFIX",
                                                               120,
                                                               600,
                                                               settings_toolchainPrefixChanged,
                                                               debugger.settingsEdit.megadrive.libretro.toolchainPrefix);
    if (ltToolchain) {
        e9ui_labeled_textbox_setText(ltToolchain, debugger.settingsEdit.megadrive.libretro.toolchainPrefix);
    }

    e9ui_component_t *ltAudio = e9ui_labeled_textbox_make("AUDIO BUFFER MS",
                                                           120,
                                                           600,
                                                           settings_audioChanged,
                                                           &debugger.settingsEdit.megadrive.libretro.audioBufferMs);
    if (ltAudio) {
        char buf[32];
        int audioValue = debugger.settingsEdit.megadrive.libretro.audioBufferMs;
        if (audioValue > 0) {
            snprintf(buf, sizeof(buf), "%d", audioValue);
            e9ui_labeled_textbox_setText(ltAudio, buf);
        } else {
            e9ui_labeled_textbox_setText(ltAudio, "");
        }
        e9ui_component_t *tbComp = e9ui_labeled_textbox_getTextbox(ltAudio);
        if (tbComp) {
            e9ui_textbox_setNumericOnly(tbComp, 1);
        }
    }

    if (romState) {
        romState->romSelect = fsRom;
        romState->elfSelect = fsElf;
        romState->sourceSelect = fsSource;
        romState->toolchainSelect = ltToolchain;
        settings_romSelectUpdateAllowEmpty(romState);
        settings_romSelectRefreshRecents(romState);
    }

    e9ui_component_t *body = e9ui_stack_makeVertical();
    if (body) {
        int first = 1;
        if (fsRom) {
            e9ui_stack_addFixed(body, fsRom);
            first = 0;
        }
        if (fsElf) {
            if (!first) {
                e9ui_stack_addFixed(body, e9ui_vspacer_make(12));
            }
            e9ui_stack_addFixed(body, fsElf);
            first = 0;
        }
        if (ltToolchain) {
            if (!first) {
                e9ui_stack_addFixed(body, e9ui_vspacer_make(12));
            }
            e9ui_stack_addFixed(body, ltToolchain);
            first = 0;
        }
        if (fsSource) {
            if (!first) {
                e9ui_stack_addFixed(body, e9ui_vspacer_make(12));
            }
            e9ui_stack_addFixed(body, fsSource);
            first = 0;
        }
        if (fsBios) {
            if (!first) {
                e9ui_stack_addFixed(body, e9ui_vspacer_make(12));
            }
            e9ui_stack_addFixed(body, fsBios);
            first = 0;
        }
        if (fsSaves) {
            if (!first) {
                e9ui_stack_addFixed(body, e9ui_vspacer_make(12));
            }
            e9ui_stack_addFixed(body, fsSaves);
            first = 0;
        }
        if (ltAudio) {
            if (!first) {
                e9ui_stack_addFixed(body, e9ui_vspacer_make(12));
            }
            e9ui_stack_addFixed(body, ltAudio);
            first = 0;
        }
    }
    out->body = body;
}

static void
target_megadrive_applyActiveSettingsToCurrentSystem(void)
{
    strutil_strlcpy(debugger.config.megadrive.libretro.exePath,
                      sizeof(debugger.config.megadrive.libretro.exePath),
                      rom_config_activeElfPath);
    strutil_strlcpy(debugger.config.megadrive.libretro.sourceDir,
                      sizeof(debugger.config.megadrive.libretro.sourceDir),
                      rom_config_activeSourceDir);
    strutil_strlcpy(debugger.config.megadrive.libretro.toolchainPrefix,
                      sizeof(debugger.config.megadrive.libretro.toolchainPrefix),
                      rom_config_activeToolchainPrefix);
}

static void
target_megadrive_setActiveDefaultsFromCurrentSystem(void)
{
    strutil_strlcpy(rom_config_activeElfPath, sizeof(rom_config_activeElfPath),
                      debugger.config.megadrive.libretro.exePath);
    strutil_strlcpy(rom_config_activeSourceDir, sizeof(rom_config_activeSourceDir),
                      debugger.config.megadrive.libretro.sourceDir);
    strutil_strlcpy(rom_config_activeToolchainPrefix, sizeof(rom_config_activeToolchainPrefix),
                      debugger.config.megadrive.libretro.toolchainPrefix);
}

static int
target_megadrive_configMissingPaths(const e9k_megadrive_config_t *cfg)
{
    if (!cfg) {
        return 1;
    }
    const char *corePath = target_megadrive_defaultCorePath();
    if (!corePath || !*corePath ||
        !cfg->libretro.romPath[0] ||
        !cfg->libretro.systemDir[0] ||
        !cfg->libretro.saveDir[0] ||
        !settings_pathExistsFile(corePath) ||
        !settings_pathExistsFile(cfg->libretro.romPath) ||
        !settings_pathExistsDir(cfg->libretro.systemDir) ||
        !settings_pathExistsDir(cfg->libretro.saveDir)) {
        return 1;
    }
    if (cfg->libretro.exePath[0] && !settings_pathExistsFile(cfg->libretro.exePath)) {
        return 1;
    }
    if (cfg->libretro.sourceDir[0] && !settings_pathExistsDir(cfg->libretro.sourceDir)) {
        return 1;
    }
    return 0;
}

static int
target_megadrive_configIsOk(void)
{
    return target_megadrive_configMissingPaths(&debugger.config.megadrive) ? 0 : 1;
}

static int
target_megadrive_configIsOkFor(const e9k_megadrive_config_t *cfg)
{
    return target_megadrive_configMissingPaths(cfg) ? 0 : 1;
}

static int
target_megadrive_restartNeededForMegaDrive(const e9k_megadrive_config_t *before, const e9k_megadrive_config_t *after)
{
    if (!before || !after) {
        return 1;
    }
    int romChanged = strcmp(before->libretro.romPath, after->libretro.romPath) != 0;
    int elfChanged = strcmp(before->libretro.exePath, after->libretro.exePath) != 0;
    int toolchainChanged = strcmp(before->libretro.toolchainPrefix, after->libretro.toolchainPrefix) != 0;
    int biosChanged = strcmp(before->libretro.systemDir, after->libretro.systemDir) != 0;
    int savesChanged = strcmp(before->libretro.saveDir, after->libretro.saveDir) != 0;
    int sourceChanged = strcmp(before->libretro.sourceDir, after->libretro.sourceDir) != 0;
    int audioBefore = settings_audioBufferNormalized(before->libretro.audioBufferMs);
    int audioAfter = settings_audioBufferNormalized(after->libretro.audioBufferMs);
    int audioChanged = audioBefore != audioAfter;
    return romChanged || elfChanged || toolchainChanged || biosChanged || savesChanged || sourceChanged || audioChanged;
}

static int
target_megadrive_needsRestart(void)
{
    int configChanged = target_megadrive_restartNeededForMegaDrive(&debugger.config.megadrive, &debugger.settingsEdit.megadrive);
    if (settings_coreOptionsDirty) {
        configChanged = 1;
    }
    int okBefore = target_megadrive_configIsOkFor(&debugger.config.megadrive);
    int okAfter = target_megadrive_configIsOkFor(&debugger.settingsEdit.megadrive);
    return configChanged || (!okBefore && okAfter);
}

static int
target_megadrive_settingsSaveButtonDisabled(void)
{
    return 0;
}

static void
target_megadrive_validateSettings(void)
{
    if (debugger.settingsEdit.megadrive.libretro.audioBufferMs <= 0) {
        debugger.settingsEdit.megadrive.libretro.audioBufferMs = 50;
    }
    const char *saveDir = debugger.settingsEdit.megadrive.libretro.saveDir[0] ?
        debugger.settingsEdit.megadrive.libretro.saveDir : debugger.settingsEdit.megadrive.libretro.systemDir;
    const char *romPath = debugger.settingsEdit.megadrive.libretro.romPath;
    if (romPath && *romPath) {
        if (!megadrive_coreOptionsWriteToFile(saveDir, romPath)) {
            e9ui_showTransientMessage("CORE OPTIONS SAVE FAILED");
            return;
        }
    }
    megadrive_coreOptionsClear();
    rom_config_saveSettingsForRom(saveDir, romPath, target_megadrive(),
                                  debugger.settingsEdit.megadrive.libretro.exePath,
                                  debugger.settingsEdit.megadrive.libretro.sourceDir,
                                  debugger.settingsEdit.megadrive.libretro.toolchainPrefix);
}

static void
target_megadrive_settingsDefault(void)
{
    char romPath[PATH_MAX];
    char elfPath[PATH_MAX];
    settings_copyPath(romPath, sizeof(romPath), debugger.settingsEdit.megadrive.libretro.romPath);
    settings_copyPath(elfPath, sizeof(elfPath), debugger.settingsEdit.megadrive.libretro.exePath);
    int audioEnabled = debugger.settingsEdit.megadrive.libretro.audioEnabled;
    target_megadrive_setConfigDefaults(&debugger.settingsEdit);
    debugger.settingsEdit.megadrive.libretro.audioEnabled = audioEnabled;
    settings_copyPath(debugger.settingsEdit.megadrive.libretro.romPath, sizeof(debugger.settingsEdit.megadrive.libretro.romPath), romPath);
    settings_copyPath(debugger.settingsEdit.megadrive.libretro.exePath, sizeof(debugger.settingsEdit.megadrive.libretro.exePath), elfPath);
}

static void
target_megadrive_applyRomConfigForSelection(settings_romselect_state_t *st, const char **saveDirP, const char **romPathP)
{
    (void)st;
    *saveDirP = debugger.settingsEdit.megadrive.libretro.saveDir[0] ?
        debugger.settingsEdit.megadrive.libretro.saveDir : debugger.settingsEdit.megadrive.libretro.systemDir;
    *romPathP = debugger.settingsEdit.megadrive.libretro.romPath;
}

static void
target_megadrive_settingsSetConfigPaths(int hasElf, const char *elfPath, int hasSource, const char *sourceDir, int hasToolchain, const char *toolchainPrefix)
{
    settings_config_setPath(debugger.settingsEdit.megadrive.libretro.exePath, PATH_MAX, hasElf ? elfPath : "");
    settings_config_setPath(debugger.settingsEdit.megadrive.libretro.sourceDir, PATH_MAX, hasSource ? sourceDir : "");
    settings_config_setValue(debugger.settingsEdit.megadrive.libretro.toolchainPrefix, PATH_MAX, hasToolchain ? toolchainPrefix : "");
}

static const char *
target_megadrive_defaultCorePath(void)
{
    static char corePath[PATH_MAX];
    static char fallbackPath[PATH_MAX];
    char relPath[PATH_MAX];
#if defined(_WIN32)
    const char *ext = "dll";
#elif defined(__APPLE__)
    const char *ext = "dylib";
#else
    const char *ext = "so";
#endif
    snprintf(relPath, sizeof(relPath), "system/mega9000.%s", ext);
    if (file_getAssetPath(relPath, corePath, sizeof(corePath))) {
        return corePath;
    }
    snprintf(fallbackPath, sizeof(fallbackPath), "./system/mega9000.%s", ext);
    return fallbackPath;
}

static void
target_megadrive_settingsRomPathChanged(settings_romselect_state_t *st)
{
    const char *saveDir = debugger.settingsEdit.megadrive.libretro.saveDir[0] ?
        debugger.settingsEdit.megadrive.libretro.saveDir : debugger.settingsEdit.megadrive.libretro.systemDir;
    if (!st || !st->romPath || !st->romPath[0]) {
        megadrive_coreOptionsClear();
        return;
    }
    megadrive_coreOptionsLoadFromFile(saveDir, st->romPath);
}

static void
target_megadrive_settingsFolderChanged(void)
{

}

static void
target_megadrive_settingsCoreChanged(void)
{
    const char *saveDir = debugger.settingsEdit.megadrive.libretro.saveDir[0] ?
        debugger.settingsEdit.megadrive.libretro.saveDir : debugger.settingsEdit.megadrive.libretro.systemDir;
    if (debugger.settingsEdit.megadrive.libretro.romPath[0]) {
        megadrive_coreOptionsLoadFromFile(saveDir, debugger.settingsEdit.megadrive.libretro.romPath);
    } else {
        megadrive_coreOptionsClear();
    }
}

static void
target_megadrive_settingsLoadOptions(e9k_system_config_t *st)
{
    (void)st;
    const char *saveDir = debugger.settingsEdit.megadrive.libretro.saveDir[0] ?
        debugger.settingsEdit.megadrive.libretro.saveDir : debugger.settingsEdit.megadrive.libretro.systemDir;
    if (debugger.settingsEdit.megadrive.libretro.romPath[0]) {
        megadrive_coreOptionsLoadFromFile(saveDir, debugger.settingsEdit.megadrive.libretro.romPath);
    }
}

const e9k_libretro_config_t *
target_megadrive_selectLibretroConfig(const e9k_system_config_t *cfg)
{
    return &cfg->megadrive.libretro;
}

static int
target_megadrive_coreOptionsHasGeneral(const core_options_modal_state_t *st)
{
    (void)st;
    return 0;
}

static void
target_megadrive_coreOptionsSaveClicked(e9ui_context_t *ctx, core_options_modal_state_t *st)
{
    (void)ctx;
    int anyChange = 0;
    int anyRomConfigBindingChange = 0;
    int anyCoreOptionChange = 0;

    for (size_t i = 0; i < st->entryCount; ++i) {
        const char *key = st->entries[i].key;
        const char *value = st->entries[i].value;
        if (!key || !*key) {
            continue;
        }
        if (debugger_input_bindings_isOptionKey(key)) {
            const char *existingBinding = rom_config_getActiveInputBindingValue(key);
            const char *desiredBinding = (value && *value) ? value : NULL;
            if (desiredBinding) {
                const char *defValue = core_options_findDefaultValue(st, key);
                if (defValue && strcmp(defValue, desiredBinding) == 0) {
                    desiredBinding = NULL;
                }
            }
            if (desiredBinding) {
                if (!existingBinding || !core_options_stringsEqual(existingBinding, desiredBinding)) {
                    rom_config_setActiveInputBindingValue(key, desiredBinding);
                    anyChange = 1;
                    anyRomConfigBindingChange = 1;
                }
            } else if (existingBinding) {
                rom_config_setActiveInputBindingValue(key, NULL);
                anyChange = 1;
                anyRomConfigBindingChange = 1;
            }
            continue;
        }
        const char *defValue = core_options_findDefaultValue(st, key);
        const char *desired = NULL;
        if (!defValue || !value || strcmp(defValue, value) != 0) {
            desired = value ? value : "";
        }
        const char *existing = megadrive_coreOptionsGetValue(key);
        if (!desired) {
            if (!existing) {
                continue;
            }
            megadrive_coreOptionsSetValue(key, NULL);
            anyChange = 1;
            anyCoreOptionChange = 1;
        } else {
            if (existing && core_options_stringsEqual(existing, desired)) {
                continue;
            }
            megadrive_coreOptionsSetValue(key, desired);
            anyChange = 1;
            anyCoreOptionChange = 1;
        }
    }
    if (anyChange) {
        settings_markCoreOptionsDirtyWithRestart(anyCoreOptionChange ? 1 : 0);
    }
    if (anyRomConfigBindingChange && (!e9ui || !e9ui->settingsModal)) {
        rom_config_saveCurrentRomSettings();
    }
    settings_refreshSaveLabel();
    if (anyChange) {
        e9ui_showTransientMessage((!e9ui || !e9ui->settingsModal) ? "CORE OPTIONS APPLIED" : "CORE OPTIONS STAGED");
    } else {
        e9ui_showTransientMessage("CORE OPTIONS: NO CHANGES");
    }
    core_options_closeModal();
}

const char *
target_megadrive_coreOptionGetValue(const char *key)
{
    if (debugger_input_bindings_isOptionKey(key)) {
        return rom_config_getActiveInputBindingValue(key);
    }
    return megadrive_coreOptionsGetValue(key);
}

static e9k_libretro_config_t *
target_megadrive_getLibretroCliConfig(void)
{
    return &debugger.cliConfig.megadrive.libretro;
}

static void
target_megadrive_onCoreStarted(void)
{
}

static void
target_megadrive_onVblank(void)
{
    e9k_debug_mega_sprite_state_t spriteState;
    if (libretro_host_megadrive_getSpriteState(&spriteState)) {
        emu_mega_setSpriteState(&spriteState, 1);
    } else {
        emu_mega_setSpriteState(NULL, 0);
    }
    if (mega_audio_vis_isOpen()) {
        e9k_debug_mega_audio_frame_t audioFrame;
        if (libretro_host_megadrive_getAudioFrame(&audioFrame)) {
            emu_mega_setAudioFrame(&audioFrame, 1);
        } else {
            emu_mega_setAudioFrame(NULL, 0);
        }
    } else {
        emu_mega_setAudioFrame(NULL, 0);
    }
}

static void
target_megadrive_libretroSelectConfig(void)
{
    debugger.libretro.audioBufferMs = debugger.config.megadrive.libretro.audioBufferMs;
    debugger.libretro.audioEnabled = debugger.config.megadrive.libretro.audioEnabled;
    debugger_copyPath(debugger.libretro.sourceDir, sizeof(debugger.libretro.sourceDir), debugger.config.megadrive.libretro.sourceDir);
    debugger_copyPath(debugger.libretro.exePath, sizeof(debugger.libretro.exePath), debugger.config.megadrive.libretro.exePath);
    debugger_copyPath(debugger.libretro.toolchainPrefix, sizeof(debugger.libretro.toolchainPrefix), debugger.config.megadrive.libretro.toolchainPrefix);
    debugger_copyPath(debugger.libretro.romPath, sizeof(debugger.libretro.romPath), debugger.config.megadrive.libretro.romPath);
    debugger_copyPath(debugger.libretro.systemDir, sizeof(debugger.libretro.systemDir), debugger.config.megadrive.libretro.systemDir);
    debugger_copyPath(debugger.libretro.saveDir, sizeof(debugger.libretro.saveDir), debugger.config.megadrive.libretro.saveDir);
}

static void
target_megadrive_pickElfToolchainPaths(const char **rawElf, const char **toolchainPrefix)
{
    *rawElf = debugger.config.megadrive.libretro.exePath;
    *toolchainPrefix = debugger.config.megadrive.libretro.toolchainPrefix;
}

static void
target_megadrive_applyCoreOptions(void)
{
    const char *romPath = debugger.libretro.romPath[0] ? debugger.libretro.romPath : debugger.config.megadrive.libretro.romPath;
    const char *saveDir = debugger.libretro.saveDir[0] ? debugger.libretro.saveDir : debugger.config.megadrive.libretro.saveDir;
    if (romPath && *romPath && saveDir && *saveDir) {
        megadrive_coreOptionsApplyFileToHost(saveDir, romPath);
    }
}

static void
target_megadrive_validateAPI(void)
{
    emu_mega_setSpriteState(NULL, 0);
    emu_mega_setAudioFrame(NULL, 0);
    libretro_host_megadrive_bindApis();
    libretro_host_neogeo_unbindApis();
}

static int
target_megadrive_audioEnabled(void)
{
    return debugger.config.megadrive.libretro.audioEnabled;
}

static void
target_megadrive_audioEnable(int enabled)
{
    debugger.config.megadrive.libretro.audioEnabled = enabled;
}

static SDL_Texture *
target_megadrive_getBadgeTexture(SDL_Renderer *renderer, target_iface_t *t, int *outW, int *outH)
{
    if (t->badge && t->badgeRenderer != renderer) {
        SDL_DestroyTexture(t->badge);
        t->badge = NULL;
    }
    t->badgeRenderer = renderer;
    if (!t->badge) {
        t->badge = system_badge_loadTexture(renderer, "assets/mega.png", &t->badgeW, &t->badgeH);
    }

    if (t->badge) {
        if (outW) {
            *outW = t->badgeW;
        }
        if (outH) {
            *outH = t->badgeH;
        }
        return t->badge;
    }

    return t->badge;
}

static void
target_megadrive_configControllerPorts(void)
{

}

static int
target_megadrive_memoryGetLimits(uint32_t *outMinAddr, uint32_t *outMaxAddr)
{
    if (outMinAddr) {
        *outMinAddr = 0x00000000u;
    }
    if (outMaxAddr) {
        *outMaxAddr = 0x00ffffffu;
    }
    return 1;
}

static int
target_megadrive_memoryGetSpaces(target_memory_space_t *outSpaces, size_t cap, size_t *outCount)
{
    if (outCount) {
        *outCount = 2;
    }
    if (!outSpaces || cap == 0) {
        return 1;
    }
    outSpaces[0] = (target_memory_space_t){
        .value = "68k",
        .label = "68K",
        .minAddr = 0x00000000u,
        .maxAddr = 0x00ffffffu,
        .addressDigits = 6,
        .processorMemory = 0,
        .processorId = 0
    };
    if (cap > 1) {
        outSpaces[1] = (target_memory_space_t){
            .value = "z80",
            .label = "Z80",
            .minAddr = 0x0000u,
            .maxAddr = 0xffffu,
            .addressDigits = 4,
            .processorMemory = 1,
            .processorId = TARGET_MEGADRIVE_Z80_PROCESSOR_ID
        };
    }
    return 1;
}

static int
target_megadrive_memoryTrackGetRanges(target_memory_range_t *outRanges, size_t cap, size_t *outCount)
{
    if (outCount) {
        *outCount = 1;
    }
    if (!outRanges || cap == 0) {
        return 1;
    }
    outRanges[0].baseAddr = 0x00ff0000u;
    outRanges[0].size = 0x00010000u;
    return 1;
}

static int
target_megadrive_registersReadExtra(const char **outTitle, e9k_debug_processor_reg_t *outRegs, size_t cap, size_t *outCount)
{
    size_t count = 0;

    if (outTitle) {
        *outTitle = "Z80";
    }
    if (outCount) {
        *outCount = 0;
    }
    if (!outRegs || cap == 0) {
        return 1;
    }
    if (!libretro_host_debugReadProcessorRegs(TARGET_MEGADRIVE_Z80_PROCESSOR_ID, outRegs, cap, &count)) {
        if (outTitle) {
            *outTitle = NULL;
        }
        return 0;
    }
    if (outCount) {
        *outCount = count;
    }
    return 1;
}

static int
target_megadrive_controllerMapButton(SDL_GameControllerButton button, unsigned *outId)
{
    switch (button) {
    case SDL_CONTROLLER_BUTTON_A: *outId = RETRO_DEVICE_ID_JOYPAD_B; return 1;
    case SDL_CONTROLLER_BUTTON_B: *outId = RETRO_DEVICE_ID_JOYPAD_A; return 1;
    case SDL_CONTROLLER_BUTTON_X: *outId = RETRO_DEVICE_ID_JOYPAD_Y; return 1;
    case SDL_CONTROLLER_BUTTON_Y: *outId = RETRO_DEVICE_ID_JOYPAD_X; return 1;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: *outId = RETRO_DEVICE_ID_JOYPAD_L; return 1;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: *outId = RETRO_DEVICE_ID_JOYPAD_R; return 1;
    case SDL_CONTROLLER_BUTTON_START: *outId = RETRO_DEVICE_ID_JOYPAD_START; return 1;
    case SDL_CONTROLLER_BUTTON_BACK: *outId = RETRO_DEVICE_ID_JOYPAD_SELECT; return 1;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: *outId = RETRO_DEVICE_ID_JOYPAD_UP; return 1;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: *outId = RETRO_DEVICE_ID_JOYPAD_DOWN; return 1;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: *outId = RETRO_DEVICE_ID_JOYPAD_LEFT; return 1;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: *outId = RETRO_DEVICE_ID_JOYPAD_RIGHT; return 1;
    default:
        break;
    }

    return 0;
}

static size_t
target_megadrive_romConfigCustomOptionCount(void)
{
    return 3;
}

static const char *
target_megadrive_romConfigCustomOptionKeyAt(size_t index)
{
    if (index == 0) {
        return target_megadrive_memorySpaceOptionKey();
    }
    if (index == 1) {
        return target_megadrive_memoryAddressOptionKey();
    }
    if (index == 2) {
        return target_megadrive_memorySearchOptionKey();
    }
    return NULL;
}

static const char *
target_megadrive_romConfigGetActiveCustomOptionValue(const char *key)
{
    if (!key) {
        return NULL;
    }
    if (strcmp(key, target_megadrive_memorySpaceOptionKey()) == 0) {
        return target_megadrive_activeRomMemorySpace[0] ? target_megadrive_activeRomMemorySpace : NULL;
    }
    if (strcmp(key, target_megadrive_memoryAddressOptionKey()) == 0) {
        return target_megadrive_activeRomMemoryAddress[0] ? target_megadrive_activeRomMemoryAddress : NULL;
    }
    if (strcmp(key, target_megadrive_memorySearchOptionKey()) == 0) {
        return target_megadrive_activeRomMemorySearch[0] ? target_megadrive_activeRomMemorySearch : NULL;
    }
    return NULL;
}

static void
target_megadrive_romConfigSetActiveCustomOptionValue(const char *key, const char *value)
{
    if (!key) {
        return;
    }
    if (strcmp(key, target_megadrive_memorySpaceOptionKey()) == 0) {
        strutil_strlcpy(target_megadrive_activeRomMemorySpace, sizeof(target_megadrive_activeRomMemorySpace), value ? value : "");
        return;
    }
    if (strcmp(key, target_megadrive_memoryAddressOptionKey()) == 0) {
        strutil_strlcpy(target_megadrive_activeRomMemoryAddress, sizeof(target_megadrive_activeRomMemoryAddress), value ? value : "");
        return;
    }
    if (strcmp(key, target_megadrive_memorySearchOptionKey()) == 0) {
        strutil_strlcpy(target_megadrive_activeRomMemorySearch, sizeof(target_megadrive_activeRomMemorySearch), value ? value : "");
    }
}

static void
target_megadrive_romConfigClearActiveCustomOptions(void)
{
    target_megadrive_activeRomMemorySpace[0] = '\0';
    target_megadrive_activeRomMemoryAddress[0] = '\0';
    target_megadrive_activeRomMemorySearch[0] = '\0';
}

static int
target_megadrive_memoryPanelOptions(target_memory_panel_options_t *outOptions)
{
    if (!outOptions) {
        return 0;
    }
    outOptions->spaceKey = target_megadrive_memorySpaceOptionKey();
    outOptions->addressKey = target_megadrive_memoryAddressOptionKey();
    outOptions->searchKey = target_megadrive_memorySearchOptionKey();
    return 1;
}

static target_iface_t _target_megadrive = {
    .name = "MEGA DRIVE",
    .dasm = &dasm_ami_iface,
    .emu = &emu_mega_iface,
    .setConfigDefaults = target_megadrive_setConfigDefaults,
    .setActiveDefaultsFromCurrentSystem = target_megadrive_setActiveDefaultsFromCurrentSystem,
    .applyActiveSettingsToCurrentSystem = target_megadrive_applyActiveSettingsToCurrentSystem,
    .configIsOk = target_megadrive_configIsOk,
    .needsRestart = target_megadrive_needsRestart,
    .settingsSaveButtonDisabled = target_megadrive_settingsSaveButtonDisabled,
    .validateSettings = target_megadrive_validateSettings,
    .settingsDefaults = target_megadrive_settingsDefault,
    .applyRomConfigForSelection = target_megadrive_applyRomConfigForSelection,
    .settingsSetConfigPaths = target_megadrive_settingsSetConfigPaths,
    .defaultCorePath = target_megadrive_defaultCorePath,
    .settingsRomPathChanged = target_megadrive_settingsRomPathChanged,
    .settingsRomFolderChanged = target_megadrive_settingsFolderChanged,
    .settingsCoreChanged = target_megadrive_settingsCoreChanged,
    .settingsClearOptions = target_megadrive_settingsClearOptions,
    .settingsLoadOptions = target_megadrive_settingsLoadOptions,
    .settingsBuildModal = target_megadrive_settingsBuildModal,
    .selectLibretroConfig = target_megadrive_selectLibretroConfig,
    .coreOptionsHasGeneral = target_megadrive_coreOptionsHasGeneral,
    .coreOptionsSaveClicked = target_megadrive_coreOptionsSaveClicked,
    .coreOptionGetValue = target_megadrive_coreOptionGetValue,
    .getLibretroCliConfig = target_megadrive_getLibretroCliConfig,
    .onCoreStarted = target_megadrive_onCoreStarted,
    .onVblank = target_megadrive_onVblank,
    .coreIndex = TARGET_MEGADRIVE,
    .libretroSelectConfig = target_megadrive_libretroSelectConfig,
    .pickElfToolchainPaths = target_megadrive_pickElfToolchainPaths,
    .applyCoreOptions = target_megadrive_applyCoreOptions,
    .validateAPI = target_megadrive_validateAPI,
    .audioEnabled = target_megadrive_audioEnabled,
    .audioEnable = target_megadrive_audioEnable,
    .mousePort = -1,
    .memoryGetLimits = target_megadrive_memoryGetLimits,
    .memoryGetSpaces = target_megadrive_memoryGetSpaces,
    .memoryTrackGetRanges = target_megadrive_memoryTrackGetRanges,
    .memoryPanelOptions = target_megadrive_memoryPanelOptions,
    .registersReadExtra = target_megadrive_registersReadExtra,
    .getBadgeTexture = target_megadrive_getBadgeTexture,
    .configControllerPorts = target_megadrive_configControllerPorts,
    .controllerMapButton = target_megadrive_controllerMapButton,
    .romConfigCustomOptionCount = target_megadrive_romConfigCustomOptionCount,
    .romConfigCustomOptionKeyAt = target_megadrive_romConfigCustomOptionKeyAt,
    .romConfigGetActiveCustomOptionValue = target_megadrive_romConfigGetActiveCustomOptionValue,
    .romConfigSetActiveCustomOptionValue = target_megadrive_romConfigSetActiveCustomOptionValue,
    .romConfigClearActiveCustomOptions = target_megadrive_romConfigClearActiveCustomOptions,
};

target_iface_t *
target_megadrive(void)
{
    return &_target_megadrive;
}
