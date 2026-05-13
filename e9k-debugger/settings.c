/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

#include "settings.h"
#include "alloc.h"
#include "crt.h"
#include "core_options.h"
#include "debugger.h"
#include "config.h"
#include "hotkeys.h"
#include "list.h"
#include "system_badge.h"
#include "rom_config.h"
#include "e9ui_flow.h"
#include "e9ui_scroll.h"

#define SETTINGS_ROM_RECENTS_MAX 16

static int settings_pendingRebuild = 0;
int settings_coreOptionsDirty = 0;
static int settings_coreOptionsRestartDirty = 0;
static const char settings_romRecentClearLabel[] = "<CLEAR RECENTS>";
static const char settings_romRecentClearValue[] = "__e9k_clear_recents__";

typedef struct settings_romrecent_list
{
    char entries[SETTINGS_ROM_RECENTS_MAX][PATH_MAX];
    int count;
} settings_romrecent_list_t;

static settings_romrecent_list_t settings_romRecents[TARGET_MEGADRIVE + 1];
static settings_romrecent_list_t settings_romFolderRecents[TARGET_MEGADRIVE + 1];

static void
settings_rebuildModalBody(e9ui_context_t *ctx);

static e9ui_component_t *
settings_makeSystemBadge(e9ui_context_t *ctx, target_iface_t* system);

void
settings_markCoreOptionsDirtyWithRestart(int restartRequired);

static void
settings_uiDebuggerHotkeys(e9ui_context_t *ctx, void *user)
{
    (void)user;
    hotkeys_showConfigModal(ctx);
}

static e9ui_component_t *
settings_findFirstVisibleTextbox(e9ui_component_t *comp)
{
    if (!comp) {
        return NULL;
    }
    if (!e9ui_getHidden(comp) && !comp->disabled && !comp->collapsed) {
        if (comp->name && strcmp(comp->name, "e9ui_textbox") == 0) {
            return comp;
        }
        e9ui_child_iterator iter;
        if (e9ui_child_iterateChildren(comp, &iter)) {
            for (e9ui_child_iterator *it = e9ui_child_interateNext(&iter);
                 it;
                 it = e9ui_child_interateNext(&iter)) {
                if (!it->child) {
                    continue;
                }
                e9ui_component_t *found = settings_findFirstVisibleTextbox(it->child);
                if (found) {
                    return found;
                }
            }
        }
    }
    return NULL;
}

static void
settings_focusFirstVisibleTextbox(e9ui_context_t *ctx)
{
    if (!ctx || !e9ui->settingsModal) {
        return;
    }
    e9ui_component_t *firstTextbox = settings_findFirstVisibleTextbox(e9ui->settingsModal);
    if (firstTextbox) {
        e9ui_setFocus(ctx, firstTextbox);
    }
}

static void
settings_enableTextboxEnterNavigation(e9ui_component_t *comp)
{
    if (!comp) {
        return;
    }
    if (comp->name && strcmp(comp->name, "e9ui_textbox") == 0) {
        e9ui_textbox_setEnterMovesToNextTextbox(comp, 1);
    }
    e9ui_child_iterator iter;
    if (!e9ui_child_iterateChildren(comp, &iter)) {
        return;
    }
    for (e9ui_child_iterator *it = e9ui_child_interateNext(&iter);
         it;
         it = e9ui_child_interateNext(&iter)) {
        if (!it->child) {
            continue;
        }
        settings_enableTextboxEnterNavigation(it->child);
    }
}


static int
settings_romRecentsTargetIndexForIface(const target_iface_t *system)
{
    if (!system) {
        return -1;
    }
    if (!target_getByIndex(system->coreIndex)) {
        return -1;
    }
    if (system->coreIndex < TARGET_AMIGA || system->coreIndex > TARGET_MEGADRIVE) {
        return -1;
    }
    return system->coreIndex;
}

static int
settings_romRecentsCurrentTargetIndex(void)
{
    target_iface_t *selectedTarget = debugger.settingsEdit.target ? debugger.settingsEdit.target : target;
    return settings_romRecentsTargetIndexForIface(selectedTarget);
}

static const char *
settings_romRecentsTargetKeyName(int targetIndex)
{
#if E9K_ENABLE_AMIGA
    if (targetIndex == TARGET_AMIGA) {
        return "amiga";
    }
#endif
#if E9K_ENABLE_NEOGEO
    if (targetIndex == TARGET_NEOGEO) {
        return "neogeo";
    }
#endif
#if E9K_ENABLE_MEGADRIVE
    if (targetIndex == TARGET_MEGADRIVE) {
        return "megadrive";
    }
#endif
    return NULL;
}

static int
settings_romRecentsTargetIndexFromKey(const char *name)
{
    if (!name) {
        return -1;
    }
#if E9K_ENABLE_AMIGA
    if (strcmp(name, "amiga") == 0) {
        return TARGET_AMIGA;
    }
#endif
#if E9K_ENABLE_NEOGEO
    if (strcmp(name, "neogeo") == 0) {
        return TARGET_NEOGEO;
    }
#endif
#if E9K_ENABLE_MEGADRIVE
    if (strcmp(name, "megadrive") == 0) {
        return TARGET_MEGADRIVE;
    }
#endif
    return -1;
}

static settings_romrecent_list_t *
settings_romRecentsListForTargetIndex(int targetIndex)
{
    if (targetIndex < TARGET_AMIGA || targetIndex > TARGET_MEGADRIVE) {
        return NULL;
    }
    return &settings_romRecents[targetIndex];
}

static settings_romrecent_list_t *
settings_romFolderRecentsListForTargetIndex(int targetIndex)
{
    if (targetIndex < TARGET_AMIGA || targetIndex > TARGET_MEGADRIVE) {
        return NULL;
    }
    return &settings_romFolderRecents[targetIndex];
}

static void
settings_romRecentsClearList(settings_romrecent_list_t *list)
{
    if (!list) {
        return;
    }
    memset(list, 0, sizeof(*list));
}

static void
settings_romRecentsAddPath(settings_romrecent_list_t *list, const char *path)
{
    if (!list || !path || !*path) {
        return;
    }

    int existingIndex = -1;
    for (int i = 0; i < list->count; ++i) {
        if (strcmp(list->entries[i], path) == 0) {
            existingIndex = i;
            break;
        }
    }

    if (existingIndex == 0) {
        return;
    }

    if (existingIndex > 0) {
        char keep[PATH_MAX];
        settings_config_setPath(keep, sizeof(keep), list->entries[existingIndex]);
        for (int i = existingIndex; i > 0; --i) {
            settings_config_setPath(list->entries[i], sizeof(list->entries[i]), list->entries[i - 1]);
        }
        settings_config_setPath(list->entries[0], sizeof(list->entries[0]), keep);
        return;
    }

    int limit = list->count;
    if (limit >= SETTINGS_ROM_RECENTS_MAX) {
        limit = SETTINGS_ROM_RECENTS_MAX - 1;
    } else {
        list->count++;
    }

    for (int i = limit; i > 0; --i) {
        settings_config_setPath(list->entries[i], sizeof(list->entries[i]), list->entries[i - 1]);
    }
    settings_config_setPath(list->entries[0], sizeof(list->entries[0]), path);
}

static void
settings_romRecentsAddTargetPath(int targetIndex, const char *path)
{
    settings_romrecent_list_t *list = settings_romRecentsListForTargetIndex(targetIndex);
    settings_romRecentsAddPath(list, path);
}

static void
settings_romFolderRecentsAddTargetPath(int targetIndex, const char *path)
{
    settings_romrecent_list_t *list = settings_romFolderRecentsListForTargetIndex(targetIndex);
    settings_romRecentsAddPath(list, path);
}

static void
settings_romRecentsSetLoadedListEntry(settings_romrecent_list_t *list, int index, const char *value)
{
    if (!list || !value || !*value) {
        return;
    }
    if (index < 0 || index >= SETTINGS_ROM_RECENTS_MAX) {
        return;
    }
    settings_config_setPath(list->entries[index], sizeof(list->entries[index]), value);
    if (list->count < index + 1) {
        list->count = index + 1;
    }
}

static void
settings_romRecentsSetLoadedEntry(int targetIndex, int index, const char *value)
{
    settings_romrecent_list_t *list = settings_romRecentsListForTargetIndex(targetIndex);
    settings_romRecentsSetLoadedListEntry(list, index, value);
}

static void
settings_romFolderRecentsSetLoadedEntry(int targetIndex, int index, const char *value)
{
    settings_romrecent_list_t *list = settings_romFolderRecentsListForTargetIndex(targetIndex);
    settings_romRecentsSetLoadedListEntry(list, index, value);
}

static void
settings_romRecentsAddFromSettingsSave(target_iface_t *selectedTarget)
{
    int targetIndex = settings_romRecentsTargetIndexForIface(selectedTarget);
    if (targetIndex < 0) {
        return;
    }

    const char *romPath = NULL;
    const char *romFolder = NULL;
#if E9K_ENABLE_AMIGA
    if (targetIndex == TARGET_AMIGA) {
        romPath = debugger.settingsEdit.amiga.libretro.romPath;
    } else
#endif
#if E9K_ENABLE_NEOGEO
    if (targetIndex == TARGET_NEOGEO) {
        romPath = debugger.settingsEdit.neogeo.libretro.romPath;
        romFolder = debugger.settingsEdit.neogeo.romFolder;
    } else
#endif
#if E9K_ENABLE_MEGADRIVE
    if (targetIndex == TARGET_MEGADRIVE) {
        romPath = debugger.settingsEdit.megadrive.libretro.romPath;
    }
#endif

    settings_romRecentsAddTargetPath(targetIndex, romPath);
    settings_romFolderRecentsAddTargetPath(targetIndex, romFolder);
}

static int
settings_romSelectHandleClearRecentsFor(e9ui_component_t *fileSelect,
                                        settings_romrecent_list_t *list,
                                        const char *restorePath,
                                        const char *text)
{
    if (!fileSelect) {
        return 0;
    }

    const char *selectedValue = e9ui_fileSelect_getSelectedValue(fileSelect);
    if (!selectedValue ||
        strcmp(selectedValue, settings_romRecentClearValue) != 0 ||
        !text ||
        (strcmp(text, settings_romRecentClearLabel) != 0 &&
         strcmp(text, settings_romRecentClearValue) != 0)) {
        return 0;
    }

    settings_romRecentsClearList(list);
    e9ui_fileSelect_setOptions(fileSelect, NULL, 0);
    e9ui_fileSelect_setText(fileSelect, restorePath ? restorePath : "");
    return 1;
}

static int
settings_romSelectHandleClearRecents(settings_romselect_state_t *st, const char *text)
{
    if (!st || !st->romSelect) {
        return 0;
    }

    char restorePath[PATH_MAX];
    settings_config_setPath(restorePath, sizeof(restorePath), st->romPath ? st->romPath : "");

    int targetIndex = settings_romRecentsCurrentTargetIndex();
    settings_romrecent_list_t *list = settings_romRecentsListForTargetIndex(targetIndex);
    st->suppress = 1;
    int handled = settings_romSelectHandleClearRecentsFor(st->romSelect, list, restorePath, text);
    st->suppress = 0;
    if (!handled) {
        return 0;
    }

    st->suppress = 1;
    settings_romSelectRefreshRecents(st);
    st->suppress = 0;
    return 1;
}

static int
settings_romFolderSelectHandleClearRecents(settings_romselect_state_t *st, const char *text)
{
    if (!st || !st->folderSelect) {
        return 0;
    }

    char restorePath[PATH_MAX];
    settings_config_setPath(restorePath, sizeof(restorePath), st->romFolder ? st->romFolder : "");

    int targetIndex = settings_romRecentsCurrentTargetIndex();
    settings_romrecent_list_t *list = settings_romFolderRecentsListForTargetIndex(targetIndex);
    st->suppress = 1;
    int handled = settings_romSelectHandleClearRecentsFor(st->folderSelect, list, restorePath, text);
    st->suppress = 0;
    if (!handled) {
        return 0;
    }

    st->suppress = 1;
    settings_romSelectRefreshRecents(st);
    st->suppress = 0;
    return 1;
}

void
settings_markCoreOptionsDirty(void)
{
    settings_markCoreOptionsDirtyWithRestart(1);
}

void
settings_markCoreOptionsDirtyWithRestart(int restartRequired)
{
    settings_coreOptionsDirty = 1;
    if (restartRequired) {
        settings_coreOptionsRestartDirty = 1;
    }
}

void
settings_clearCoreOptionsDirty(void)
{
    settings_coreOptionsDirty = 0;
    settings_coreOptionsRestartDirty = 0;
}

int
settings_coreOptionsNeedsRestart(void)
{
    return settings_coreOptionsRestartDirty ? 1 : 0;
}

// TODO
//static
int
settings_pathExistsFile(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    struct stat statBuffer;
    if (stat(path, &statBuffer) != 0) {
        return 0;
    }
    return S_ISREG(statBuffer.st_mode) ? 1 : 0;
}

int
settings_pathExistsDir(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    struct stat statBuffer;
    if (stat(path, &statBuffer) != 0) {
        return 0;
    }
    return S_ISDIR(statBuffer.st_mode) ? 1 : 0;
}


void
settings_copyPath(char *dest, size_t capacity, const char *src)
{
    if (!dest || capacity == 0) {
        return;
    }
    if (!src || !*src) {
        dest[0] = '\0';
        return;
    }
    if (src[0] == '~' && (src[1] == '/' || src[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home && *home) {
            int written = snprintf(dest, capacity, "%s%s", home, src + 1);
            if (written < 0 || (size_t)written >= capacity) {
                dest[capacity - 1] = '\0';
            }
            return;
        }
    }
    strncpy(dest, src, capacity - 1);
    dest[capacity - 1] = '\0';
}

void
settings_config_setPath(char *dest, size_t capacity, const char *value)
{
    if (!dest || capacity == 0) {
        return;
    }
    if (!value || !*value) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, value, capacity - 1);
    dest[capacity - 1] = '\0';
}


void
settings_config_setValue(char *dest, size_t capacity, const char *value)
{
    if (!dest || capacity == 0) {
        return;
    }
    if (!value || !*value) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, value, capacity - 1);
    dest[capacity - 1] = '\0';
}

static void
settings_copyConfig(e9k_system_config_t *dest, const e9k_system_config_t *src)
{
    if (!dest || !src) {
        return;
    }
    memcpy(dest, src, sizeof(e9k_system_config_t));
}

static void
settings_closeModal(void)
{
    if (!e9ui->settingsModal) {
        return;
    }
    settings_clearCoreOptionsDirty();
    settings_pendingRebuild = 0;
    e9ui_setHidden(e9ui->settingsModal, 1);
    if (!e9ui->pendingRemove) {
        e9ui->pendingRemove = e9ui->settingsModal;
    }
    e9ui->settingsModal = NULL;
    e9ui->settingsSaveButton = NULL;
}

void
settings_cancelModal(void)
{
    if (!e9ui->settingsModal) {
        return;
    }
    settings_copyConfig(&debugger.settingsEdit, &debugger.config);
    target_settingsClearAllOptions();
    settings_clearCoreOptionsDirty();
    settings_closeModal();
}

void
settings_updateButton(int settingsOk)
{
    if (!e9ui->settingsButton) {
        return;
    }
    if (!settingsOk) {
        e9ui_button_setTheme(e9ui->settingsButton, e9ui_theme_button_preset_red());
        e9ui_button_setGlowPulse(e9ui->settingsButton, 1);
    } else {
        e9ui_button_clearTheme(e9ui->settingsButton);
        e9ui_button_setGlowPulse(e9ui->settingsButton, 0);
    }
}


int
settings_configIsOk(void)
{
  return target->configIsOk();
}

int
settings_audioBufferNormalized(int value)
{
    return value > 0 ? value : 50;
}

static int
settings_needsRestart(void)
{
    target_iface_t *selectedTarget = debugger.settingsEdit.target ? debugger.settingsEdit.target : target;
    int coreSystemChanged = (target != selectedTarget);
    if (!selectedTarget) {
        return 1;
    }

    return coreSystemChanged || selectedTarget->needsRestart();
}

static int
settings_modalBodyViewportHeight(e9ui_context_t *ctx)
{
    if (!ctx) {
        return 0;
    }
    int margin = e9ui_scale_px(ctx, 32);
    int modalHeight = ctx->winH - margin * 2;
    if (modalHeight < 1) {
        modalHeight = 1;
    }
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    int textH = font ? TTF_FontHeight(font) : 16;
    if (textH <= 0) {
        textH = 16;
    }
    int titlePadY = e9ui_scale_px(ctx, 4);
    int titleH = textH + titlePadY * 2;
    int bodyH = modalHeight - titleH;
    if (bodyH < 0) {
        bodyH = 0;
    }
    return bodyH;
}

void
settings_updateSaveLabel(void)
{
    if (!e9ui->settingsSaveButton) {
        return;
    }
    target_iface_t *selectedTarget = debugger.settingsEdit.target ? debugger.settingsEdit.target : target;
    const char *label = settings_needsRestart() ? "Save and Restart" : "Save";
    e9ui_button_setLabel(e9ui->settingsSaveButton, label);
    if (selectedTarget) {
        e9ui->settingsSaveButton->disabled = selectedTarget->settingsSaveButtonDisabled();
    }

}



void
settings_refreshSaveLabel(void)
{
    settings_updateSaveLabel();
}

void
settings_applyToolbarMode(void)
{
    if (!e9ui->toolbar || !e9ui->settingsButton) {
        return;
    }
    if (target->configIsOk()) {
        return;
    }
    int childCount = list_count(e9ui->toolbar->children);
    if (childCount <= 0) {
        return;
    }
    e9ui_component_t **kids = (e9ui_component_t**)alloc_calloc((size_t)childCount, sizeof(*kids));
    if (!kids) {
        return;
    }
    int childTotal = e9ui_child_enumerateREMOVETHIS(e9ui->toolbar, &e9ui->ctx, kids, childCount);
    for (int childIndex = 0; childIndex < childTotal; ++childIndex) {
        e9ui_component_t *child = kids[childIndex];
        if (!child) {
            continue;
        }
        if (child == e9ui->settingsButton) {
            continue;
        }
        if (child->name && strcmp(child->name, "e9ui_button") != 0) {
            continue;
        }
        e9ui_childRemove(e9ui->toolbar, child, &e9ui->ctx);
    }
    alloc_free(kids);
    e9ui->profileButton = NULL;
    e9ui->analyseButton = NULL;
    e9ui->speedButton = NULL;
    e9ui->restartButton = NULL;
    e9ui->resetButton = NULL;
}

static int
settings_checkboxMeasureWidth(e9ui_component_t *checkbox, e9ui_context_t *ctx)
{
    int width = 0;
    int height = 0;
    if (!checkbox || !ctx) {
        return 0;
    }
    e9ui_checkbox_measure(checkbox, ctx, &width, &height);
    return width > 0 ? width : 0;
}

static int
settings_componentPreferredHeight(e9ui_component_t *comp, e9ui_context_t *ctx, int availW)
{
    if (!comp || !comp->preferredHeight) {
        return 0;
    }
    int h = comp->preferredHeight(comp, ctx, availW);
    return h > 0 ? h : 0;
}

static int
settings_measureTargetBodyHeight(target_iface_t *system, e9ui_context_t *ctx, int availW)
{
    if (!system || !system->settingsBuildModal || !ctx) {
        return 0;
    }
    target_settings_modal_t modal = {0};
    system->settingsBuildModal(ctx, &modal);
    int height = settings_componentPreferredHeight(modal.body, ctx, availW);
    return height;
}

static void
settings_cancel(void)
{
    settings_copyConfig(&debugger.settingsEdit, &debugger.config);
    target_settingsClearAllOptions();
    settings_clearCoreOptionsDirty();
    settings_closeModal();
}

static void
settings_save(void)
{
    int needsRestart = settings_needsRestart();
    target_iface_t *previousTarget = target;

    target_iface_t *selectedTarget = debugger.settingsEdit.target ? debugger.settingsEdit.target : target;
    if (selectedTarget) {
        selectedTarget->validateSettings();
    }
    settings_copyConfig(&debugger.config, &debugger.settingsEdit);
    target_setTarget(debugger.settingsEdit.target);
    if (previousTarget != target) {
        rom_config_clearActiveInputBindings();
        if (target && target->romConfigClearActiveCustomOptions) {
            target->romConfigClearActiveCustomOptions();
        }
    }
    crt_setEnabled(debugger.config.crtEnabled ? 1 : 0);
    debugger_libretroSelectConfig();
    rom_config_syncActiveFromCurrentSystem();
    debugger_applyCoreOptions();
    debugger_refreshElfValid();
    debugger.settingsOk = settings_configIsOk();
    settings_updateButton(debugger.settingsOk);
    settings_applyToolbarMode();
    settings_romRecentsAddFromSettingsSave(selectedTarget);
    config_saveConfig();
    if (needsRestart) {
        debugger.restartRequested = 1;
    }
    settings_closeModal();
}

static void
settings_uiClosed(e9ui_component_t *modal, void *user)
{
    (void)modal;
    (void)user;
    settings_cancel();
}

static void
settings_uiCancel(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    settings_cancel();
}

static void
settings_uiSave(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    settings_save();
}

static void
settings_uiDefaults(e9ui_context_t *ctx, void *user)
{
    (void)user;
    if (!ctx || !e9ui->settingsModal) {
        return;
    }
    target_iface_t *selectedTarget = debugger.settingsEdit.target ? debugger.settingsEdit.target : target;
    if (selectedTarget) {
        selectedTarget->settingsDefaults();
    }
    settings_clearCoreOptionsDirty();
    target_settingsClearAllOptions();
    if (selectedTarget) {
        selectedTarget->settingsLoadOptions(&debugger.settingsEdit);
    }
    settings_pendingRebuild = 1;
    e9ui_showTransientMessage("DEFAULTS RESTORED");
}

void
settings_pathChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *text, void *user)
{
    (void)ctx;
    (void)comp;
    char *dest = (char*)user;
    if (!dest) {
        return;
    }
    settings_config_setPath(dest, PATH_MAX, text);
    settings_updateSaveLabel();
}

void
settings_romSelectUpdateAllowEmpty(settings_romselect_state_t *st)
{
    if (!st) {
        return;
    }
    int hasRom = st->romPath && st->romPath[0];
    int hasFolder = st->romFolder && st->romFolder[0];
    int allowRomEmpty = hasFolder ? 1 : 0;
    int allowFolderEmpty = hasRom ? 1 : 0;
    if (st->romSelect) {
        e9ui_fileSelect_setAllowEmpty(st->romSelect, allowRomEmpty);
    }
    if (st->folderSelect) {
        e9ui_fileSelect_setAllowEmpty(st->folderSelect, allowFolderEmpty);
    }
}

static void
settings_romSelectRefreshRecentsFor(e9ui_component_t *fileSelect, settings_romrecent_list_t *list)
{
    if (!fileSelect) {
        return;
    }

    if (!list) {
        e9ui_fileSelect_setOptions(fileSelect, NULL, 0);
        return;
    }

    e9ui_textbox_option_t options[SETTINGS_ROM_RECENTS_MAX + 1];
    int optionCount = 0;
    for (int i = 0; i < list->count && optionCount < SETTINGS_ROM_RECENTS_MAX; ++i) {
        if (!list->entries[i][0]) {
            continue;
        }
        options[optionCount].value = list->entries[i];
        options[optionCount].label = list->entries[i];
        optionCount++;
    }
    options[optionCount].value = settings_romRecentClearValue;
    options[optionCount].label = settings_romRecentClearLabel;
    optionCount++;

    e9ui_fileSelect_setOptions(fileSelect, options, optionCount);
}

void
settings_romSelectRefreshRecents(settings_romselect_state_t *st)
{
    if (!st) {
        return;
    }

    int targetIndex = settings_romRecentsCurrentTargetIndex();
    settings_romSelectRefreshRecentsFor(st->romSelect, settings_romRecentsListForTargetIndex(targetIndex));
    settings_romSelectRefreshRecentsFor(st->folderSelect, settings_romFolderRecentsListForTargetIndex(targetIndex));
}

static void
settings_clearActiveRomConfigSelectionState(target_iface_t *selectedTarget)
{
    rom_config_clearActiveInputBindings();
    if (selectedTarget && selectedTarget->romConfigClearActiveCustomOptions) {
        selectedTarget->romConfigClearActiveCustomOptions();
    }
}

static int
settings_loadActiveRomConfigForSelection(target_iface_t *selectedTarget, const char *saveDir, const char *romPath)
{
    char elfPath[PATH_MAX];
    char sourceDir[PATH_MAX];
    char toolchainPrefix[PATH_MAX];
    int hasElf = 0;
    int hasSource = 0;
    int hasToolchain = 0;

    if (!selectedTarget) {
        return 0;
    }
    if (!romPath || !*romPath || !saveDir || !*saveDir) {
        settings_clearActiveRomConfigSelectionState(selectedTarget);
        return 0;
    }
    if (!rom_config_loadSettingsForRom(saveDir, romPath,
                                       selectedTarget,
                                       elfPath, sizeof(elfPath),
                                       sourceDir, sizeof(sourceDir),
                                       toolchainPrefix, sizeof(toolchainPrefix),
                                       &hasElf, &hasSource, &hasToolchain)) {
        settings_clearActiveRomConfigSelectionState(selectedTarget);
        return 0;
    }
    return 1;
}

void
settings_syncActiveRomConfigForSelection(void)
{
    target_iface_t *selectedTarget = debugger.settingsEdit.target ? debugger.settingsEdit.target : target;
    const char *saveDir = NULL;
    const char *romPath = NULL;

    if (!selectedTarget || !selectedTarget->applyRomConfigForSelection) {
        return;
    }
    selectedTarget->applyRomConfigForSelection(NULL, &saveDir, &romPath);
    (void)settings_loadActiveRomConfigForSelection(selectedTarget, saveDir, romPath);
}

static void
settings_applyRomConfigForSelection(settings_romselect_state_t *st)
{
    if (!st) {
        return;
    }
    target_iface_t *selectedTarget = debugger.settingsEdit.target ? debugger.settingsEdit.target : target;
    if (!selectedTarget) {
        return;
    }
    const char *saveDir = NULL;
    const char *romPath = NULL;

    selectedTarget->applyRomConfigForSelection(st, &saveDir, &romPath);
    
    if (!romPath || !*romPath || !saveDir || !*saveDir) {
        settings_clearActiveRomConfigSelectionState(selectedTarget);
        return;
    }
    char elfPath[PATH_MAX];
    char sourceDir[PATH_MAX];
    char toolchainPrefix[PATH_MAX];
    int hasElf = 0;
    int hasSource = 0;
    int hasToolchain = 0;
    if (!rom_config_loadSettingsForRom(saveDir, romPath,
                                       selectedTarget,
                                       elfPath, sizeof(elfPath),
                                       sourceDir, sizeof(sourceDir),
                                       toolchainPrefix, sizeof(toolchainPrefix),
                                       &hasElf, &hasSource, &hasToolchain)) {
        settings_clearActiveRomConfigSelectionState(selectedTarget);
        return;
    }
    selectedTarget->settingsSetConfigPaths(hasElf, elfPath, hasSource, sourceDir, hasToolchain, toolchainPrefix);
    
    if (st->elfSelect) {
        e9ui_fileSelect_setText(st->elfSelect, hasElf ? elfPath : "");
    }
    if (st->sourceSelect) {
        e9ui_fileSelect_setText(st->sourceSelect, hasSource ? sourceDir : "");
    }
    if (st->toolchainSelect) {
        e9ui_labeled_textbox_setText(st->toolchainSelect, hasToolchain ? toolchainPrefix : "");
    }
}


void
settings_toolchainPrefixChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *text, void *user)
{
    (void)ctx;
    (void)comp;
    char *prefix = (char*)user;
    if (!prefix) {
        return;
    }
    settings_config_setValue(prefix, PATH_MAX, text ? text : "");
    settings_updateSaveLabel();
}

void
settings_romPathChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *text, void *user)
{
    (void)ctx;
    (void)comp;
    settings_romselect_state_t *st = (settings_romselect_state_t *)user;
    if (!st || st->suppress) {
        return;
    }
    if (!st->romPath) {
        return;
    }
    if (settings_romSelectHandleClearRecents(st, text)) {
        return;
    }
    settings_config_setPath(st->romPath, PATH_MAX, text);
    if (text && *text) {
        st->suppress = 1;
        if (st->romFolder) {
            settings_config_setPath(st->romFolder, PATH_MAX, "");
            if (st->folderSelect) {
                e9ui_fileSelect_setText(st->folderSelect, "");
            }
        }
        st->suppress = 0;
    }
    settings_romSelectUpdateAllowEmpty(st);
    settings_applyRomConfigForSelection(st);
    settings_updateSaveLabel();

    {
        target_iface_t *selectedTarget = debugger.settingsEdit.target ? debugger.settingsEdit.target : target;
        if (selectedTarget) {
            selectedTarget->settingsRomPathChanged(st);
        }
    }

}

void
settings_romFolderChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *text, void *user)
{
    (void)ctx;
    (void)comp;
    settings_romselect_state_t *st = (settings_romselect_state_t *)user;
    if (!st || st->suppress) {
        return;
    }
    if (!st->romFolder) {
        return;
    }
    if (settings_romFolderSelectHandleClearRecents(st, text)) {
        return;
    }
    settings_config_setPath(st->romFolder, PATH_MAX, text);
    if (text && *text) {
        st->suppress = 1;
        if (st->romPath) {
            settings_config_setPath(st->romPath, PATH_MAX, "");
            if (st->romSelect) {
                e9ui_fileSelect_setText(st->romSelect, "");
            }
        }
        st->suppress = 0;
    }
    settings_romSelectUpdateAllowEmpty(st);
    settings_applyRomConfigForSelection(st);
    settings_updateSaveLabel();
    {
        target_iface_t *selectedTarget = debugger.settingsEdit.target ? debugger.settingsEdit.target : target;
        if (selectedTarget) {
            selectedTarget->settingsRomFolderChanged();
        }
    }
}

void
settings_audioChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *text, void *user)
{
    (void)ctx;
    (void)comp;
    int *dest = (int*)user;
    if (!dest) {
        return;
    }
    if (!text || !*text) {
        *dest = 0;
        settings_updateSaveLabel();
        return;
    }
    char *end = NULL;
    long ms = strtol(text, &end, 10);
    if (!end || end == text) {
        *dest = 0;
        settings_updateSaveLabel();
        return;
    }
    if (ms < 0) {
        ms = 0;
    }
    if (ms > INT_MAX) {
        ms = INT_MAX;
    }
    *dest = (int)ms;
    settings_updateSaveLabel();
}

void
settings_persistConfig(FILE *file)
{
    if (!file) {
        return;
    }

    for (int targetIndex = TARGET_AMIGA; targetIndex <= TARGET_MEGADRIVE; ++targetIndex) {
        const char *targetName = settings_romRecentsTargetKeyName(targetIndex);
        settings_romrecent_list_t *list = settings_romRecentsListForTargetIndex(targetIndex);
        if (!targetName || !list) {
            continue;
        }
        for (int i = 0; i < list->count; ++i) {
            if (!list->entries[i][0]) {
                continue;
            }
            fprintf(file, "comp.settings.recent.%s.rom.%d=%s\n", targetName, i, list->entries[i]);
        }

        list = settings_romFolderRecentsListForTargetIndex(targetIndex);
        if (!list) {
            continue;
        }
        for (int i = 0; i < list->count; ++i) {
            if (!list->entries[i][0]) {
                continue;
            }
            fprintf(file, "comp.settings.recent.%s.romFolder.%d=%s\n", targetName, i, list->entries[i]);
        }
    }
}

int
settings_loadConfigProperty(const char *prop, const char *value)
{
    if (!prop || !value) {
        return 0;
    }

    if (strncmp(prop, "recent.", 7) != 0) {
        return 0;
    }

    const char *targetName = prop + 7;
    const char *dot = strchr(targetName, '.');
    if (!dot) {
        return 0;
    }

    char targetBuf[32];
    size_t targetLen = (size_t)(dot - targetName);
    if (targetLen == 0 || targetLen >= sizeof(targetBuf)) {
        return 0;
    }
    memcpy(targetBuf, targetName, targetLen);
    targetBuf[targetLen] = '\0';

    int targetIndex = settings_romRecentsTargetIndexFromKey(targetBuf);
    if (targetIndex < 0) {
        return 0;
    }

    const char *rest = dot + 1;
    int isRom = strncmp(rest, "rom.", 4) == 0;
    int isRomFolder = strncmp(rest, "romFolder.", 10) == 0;
    if (!isRom && !isRomFolder) {
        return 0;
    }

    const char *indexText = rest + (isRom ? 4 : 10);
    if (!*indexText) {
        return 0;
    }
    for (const char *p = indexText; *p; ++p) {
        if (!isdigit((unsigned char)*p)) {
            return 0;
        }
    }

    int index = atoi(indexText);
    if (isRom) {
        settings_romRecentsSetLoadedEntry(targetIndex, index, value);
    } else {
        settings_romFolderRecentsSetLoadedEntry(targetIndex, index, value);
    }
    return 1;
}

static void
settings_coreSystemSync(settings_coresystem_state_t *st, target_iface_t* system, e9ui_context_t *ctx)
{
    if (!st || !system) {
        return;
    }
    st->updating = 1;
    target_iface_t* current = st->target;
    int systemChanged = (current != system);
    st->target = system;
    debugger.settingsEdit.target = system;

    system->settingsCoreChanged();

#if E9K_ENABLE_AMIGA
    int amigaSelected = (system == target_amiga());
#else
    int amigaSelected = 0;
#endif
    int neogeoSelected = (system == target_neogeo());
#if E9K_ENABLE_MEGADRIVE
    int megadriveSelected = (system == target_megadrive());
#endif
    if (st->allowRebuild && systemChanged) {
        st->updating = 0;
        settings_pendingRebuild = 1;
        return;
    }
    if (st->neogeoCheckbox) {
        e9ui_checkbox_setSelected(st->neogeoCheckbox, neogeoSelected, ctx);
    }
    if (st->amigaCheckbox) {
        e9ui_checkbox_setSelected(st->amigaCheckbox, amigaSelected, ctx);
    }
    if (st->megadriveCheckbox) {
#if E9K_ENABLE_MEGADRIVE
        e9ui_checkbox_setSelected(st->megadriveCheckbox, megadriveSelected, ctx);
#else
        e9ui_checkbox_setSelected(st->megadriveCheckbox, 0, ctx);
#endif
    }
    st->updating = 0;
    settings_updateSaveLabel();
}

#if E9K_ENABLE_NEOGEO
static void
settings_coreSystemNeoGeoChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    settings_coresystem_state_t *st = (settings_coresystem_state_t *)user;
    if (!st || st->updating) {
        return;
    }
    if (selected) {
        settings_coreSystemSync(st, target_neogeo(), ctx);
    } else if (st->target == target_neogeo()) {
        settings_coreSystemSync(st, target_neogeo(), ctx);
    }
}
#endif

#if E9K_ENABLE_AMIGA
static void
settings_coreSystemAmigaChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    settings_coresystem_state_t *st = (settings_coresystem_state_t *)user;
    if (!st || st->updating) {
        return;
    }
    if (selected) {
        settings_coreSystemSync(st, target_amiga(), ctx);
    } else if (st->target == target_amiga()) {
        settings_coreSystemSync(st, target_amiga(), ctx);
    }
}
#endif

#if E9K_ENABLE_MEGADRIVE
static void
settings_coreSystemMegaDriveChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    settings_coresystem_state_t *st = (settings_coresystem_state_t *)user;
    if (!st || st->updating) {
        return;
    }
    if (selected) {
        settings_coreSystemSync(st, target_megadrive(), ctx);
    } else if (st->target == target_megadrive()) {
        settings_coreSystemSync(st, target_megadrive(), ctx);
    }
}
#endif

static e9ui_component_t *
settings_makeSystemBadge(e9ui_context_t *ctx, target_iface_t* system)
{
    if (!ctx || !ctx->renderer) {
        return NULL;
    }
    int w = 0;
    int h = 0;
    SDL_Texture *tex = system_badge_getTexture(ctx->renderer, system, &w, &h);
    if (!tex) {
        return NULL;
    }
    e9ui_component_t *img = e9ui_image_makeFromTexture(tex, w, h);
    if (!img) {
        return NULL;
    }
    e9ui_component_t *box = e9ui_box_make(img);
    if (!box) {
        return img;
    }
    e9ui_box_setWidth(box, e9ui_dim_fixed, 139);
    e9ui_box_setHeight(box, e9ui_dim_fixed, 48);
    return box;
}

static e9ui_component_t *
settings_buildModalBody(e9ui_context_t *ctx)
{
    if (!ctx) {
        return NULL;
    }
    int contentWidth = e9ui_scale_px(ctx, 640);
    int formWidth = e9ui_scale_px(ctx, 600);
    target_iface_t *selectedTarget = debugger.settingsEdit.target ? debugger.settingsEdit.target : target;
    if (selectedTarget && !target_getByIndex(selectedTarget->coreIndex)) {
        selectedTarget = NULL;
    }
    if (!selectedTarget) {
        selectedTarget = target_firstEnabled();
    }
#if E9K_ENABLE_AMIGA
    int amigaSelected = (selectedTarget == target_amiga()) ? 1 : 0;
#endif
#if E9K_ENABLE_NEOGEO
    int neogeoSelected = (selectedTarget == target_neogeo()) ? 1 : 0;
#endif
#if E9K_ENABLE_MEGADRIVE
    int megadriveSelected = (selectedTarget == target_megadrive()) ? 1 : 0;
#endif

    settings_coresystem_state_t *coreState = (settings_coresystem_state_t *)alloc_calloc(1, sizeof(*coreState));
#if E9K_ENABLE_NEOGEO
    e9ui_component_t *cbNeogeo = e9ui_checkbox_make("NEO GEO", neogeoSelected, settings_coreSystemNeoGeoChanged, coreState);
#else
    e9ui_component_t *cbNeogeo = NULL;
#endif
#if E9K_ENABLE_AMIGA
    e9ui_component_t *cbAmiga = e9ui_checkbox_make("AMIGA", amigaSelected, settings_coreSystemAmigaChanged, coreState);
#else
    e9ui_component_t *cbAmiga = NULL;
#endif
    e9ui_component_t *cbMegaDrive = NULL;
#if E9K_ENABLE_MEGADRIVE
    cbMegaDrive = e9ui_checkbox_make("MEGA DRIVE", megadriveSelected, settings_coreSystemMegaDriveChanged, coreState);
#endif
    e9ui_component_t *rowCore = e9ui_hstack_make();
    e9ui_component_t *rowCoreCenter = rowCore ? e9ui_center_make(rowCore) : NULL;
    e9ui_component_t *btnCoreOptionsTop = e9ui_button_make("Core Options", core_options_uiOpen, NULL);
    e9ui_setTooltip(btnCoreOptionsTop, "Libretro core options");

    e9ui_component_t *badge = settings_makeSystemBadge(ctx, selectedTarget ? selectedTarget : target);
    e9ui_component_t *rowHeader = NULL;
    int rowHeaderBadgeWPx = e9ui_scale_px(ctx, 139);
    int rowHeaderGapPx = e9ui_scale_px(ctx, 12);
    rowHeader = e9ui_hstack_make();
    e9ui_hstack_addFixed(rowHeader, badge, rowHeaderBadgeWPx);
    e9ui_hstack_addFixed(rowHeader, e9ui_spacer_make(rowHeaderGapPx), rowHeaderGapPx);
    e9ui_hstack_addFlex(rowHeader, rowCoreCenter);
    e9ui_component_t *btnHotkeys = e9ui_button_make("Debugger Options", settings_uiDebuggerHotkeys, NULL);
    e9ui_component_t *rowGlobal = e9ui_hstack_make();
    e9ui_component_t *rowGlobalCenter = rowGlobal ? e9ui_center_make(rowGlobal) : NULL;

    if (coreState) {
        coreState->neogeoCheckbox = cbNeogeo;
        coreState->amigaCheckbox = cbAmiga;
        coreState->megadriveCheckbox = cbMegaDrive;
        coreState->target = selectedTarget;
        coreState->allowRebuild = 0;
        settings_coreSystemSync(coreState, selectedTarget ? selectedTarget : target, ctx);
        coreState->allowRebuild = 1;
    }
    target_settings_modal_t targetModal = {0};
    int targetModalFooterInGlobalRow = 0;
    if (selectedTarget && selectedTarget->settingsBuildModal) {
        selectedTarget->settingsBuildModal(ctx, &targetModal);
    }

    if (rowCore && ctx) {
        int gap = e9ui_scale_px(ctx, 8);
        int wNeogeo = cbNeogeo ? settings_checkboxMeasureWidth(cbNeogeo, ctx) : 0;
        int wAmiga = cbAmiga ? settings_checkboxMeasureWidth(cbAmiga, ctx) : 0;
        int wMegaDrive = cbMegaDrive ? settings_checkboxMeasureWidth(cbMegaDrive, ctx) : 0;
        int wCoreOptions = 0;
        int hCoreOptions = 0;
        if (btnCoreOptionsTop) {
            e9ui_button_measure(btnCoreOptionsTop, ctx, &wCoreOptions, &hCoreOptions);
            (void)hCoreOptions;
        }
        int leftW = 0;
        if (cbNeogeo) {
            e9ui_hstack_addFixed(rowCore, cbNeogeo, wNeogeo);
            leftW += wNeogeo;
        }
        if (cbAmiga) {
            if (leftW > 0) {
                e9ui_hstack_addFixed(rowCore, e9ui_spacer_make(gap), gap);
                leftW += gap;
            }
            e9ui_hstack_addFixed(rowCore, cbAmiga, wAmiga);
            leftW += wAmiga;
        }
        if (cbMegaDrive) {
            if (leftW > 0) {
                e9ui_hstack_addFixed(rowCore, e9ui_spacer_make(gap), gap);
                leftW += gap;
            }
            e9ui_hstack_addFixed(rowCore, cbMegaDrive, wMegaDrive);
            leftW += wMegaDrive;
        }
        if (btnCoreOptionsTop && wCoreOptions > 0) {
            if (leftW > 0) {
                e9ui_hstack_addFlex(rowCore, e9ui_spacer_make(1));
            }
        }
        if (btnCoreOptionsTop && wCoreOptions > 0) {
            e9ui_hstack_addFixed(rowCore, btnCoreOptionsTop, wCoreOptions);
        }
        if (rowCoreCenter) {
            int rowCoreWidth = formWidth;
            if (rowHeader) {
                rowCoreWidth = formWidth - rowHeaderBadgeWPx - rowHeaderGapPx;
            }
            if (rowCoreWidth < 1) {
                rowCoreWidth = 1;
            }
            e9ui_center_setSize(rowCoreCenter, e9ui_unscale_px(ctx, rowCoreWidth), 0);
        }
    }
    if (rowGlobal && ctx) {
        int gap = e9ui_scale_px(ctx, 40);
        int wHotkeys = 0;
        int hHotkeys = 0;
        int hasGlobalContent = 0;
        if (btnHotkeys) {
            e9ui_button_measure(btnHotkeys, ctx, &wHotkeys, &hHotkeys);
            (void)hHotkeys;
        }
        if (selectedTarget == target_neogeo() && targetModal.footerWarning) {
            int neogeoOptionsW = 0;
            int neogeoOptionsH = 0;
            e9ui_flow_measure(targetModal.footerWarning, ctx, &neogeoOptionsW, &neogeoOptionsH);
            (void)neogeoOptionsH;
            if (neogeoOptionsW > 0) {
                hasGlobalContent = 1;
                targetModalFooterInGlobalRow = 1;
                e9ui_hstack_addFlex(rowGlobal, e9ui_spacer_make(1));
                e9ui_hstack_addFixed(rowGlobal, targetModal.footerWarning, neogeoOptionsW);
            }
        }
        if (btnHotkeys && wHotkeys > 0) {
            if (!hasGlobalContent) {
                e9ui_hstack_addFlex(rowGlobal, e9ui_spacer_make(1));
                hasGlobalContent = 1;
            }
            if (targetModalFooterInGlobalRow) {
                e9ui_hstack_addFixed(rowGlobal, e9ui_spacer_make(gap), gap);
            }
            e9ui_hstack_addFixed(rowGlobal, btnHotkeys, wHotkeys);
        }
        if (rowGlobalCenter) {
            e9ui_center_setSize(rowGlobalCenter, e9ui_unscale_px(ctx, formWidth), 0);
        }
    }

    e9ui_component_t *stack = e9ui_stack_makeVertical();
    if (rowHeader) {
        e9ui_stack_addFixed(stack, rowHeader);
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
    } else if (rowCoreCenter) {
        e9ui_stack_addFixed(stack, rowCoreCenter);
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
    } else if (badge) {
        e9ui_stack_addFixed(stack, badge);
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
    }
    if (targetModal.body) {
        e9ui_stack_addFixed(stack, targetModal.body);
    }
    if (rowGlobalCenter) {
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
        e9ui_stack_addFixed(stack, rowGlobalCenter);
    }
    int selectedBodyHeight = settings_componentPreferredHeight(targetModal.body, ctx, contentWidth);
    int maxBodyHeight = selectedBodyHeight;
#if E9K_ENABLE_AMIGA
    {
        int h = settings_measureTargetBodyHeight(target_amiga(), ctx, contentWidth);
        if (h > maxBodyHeight) {
            maxBodyHeight = h;
        }
    }
#endif
#if E9K_ENABLE_NEOGEO
    {
        int h = settings_measureTargetBodyHeight(target_neogeo(), ctx, contentWidth);
        if (h > maxBodyHeight) {
            maxBodyHeight = h;
        }
    }
#endif
#if E9K_ENABLE_MEGADRIVE
    {
        int h = settings_measureTargetBodyHeight(target_megadrive(), ctx, contentWidth);
        if (h > maxBodyHeight) {
            maxBodyHeight = h;
        }
    }
#endif

    e9ui_component_t *center = e9ui_center_make(stack);
    int centerFixedHeight = 0;
    if (center) {
        int stackHeight = settings_componentPreferredHeight(stack, ctx, contentWidth);
        if (stackHeight > 0 && selectedBodyHeight > 0 && maxBodyHeight > selectedBodyHeight) {
            centerFixedHeight = stackHeight + (maxBodyHeight - selectedBodyHeight);
            if (centerFixedHeight < stackHeight) {
                centerFixedHeight = stackHeight;
            }
        }
        e9ui_center_setSize(center, 640,
                            centerFixedHeight > 0 ? e9ui_unscale_px(ctx, centerFixedHeight) : 0);
    }
    e9ui_component_t *scrollContent = center;
    int centerContentHeight = center ? settings_componentPreferredHeight(center, ctx, contentWidth) : 0;
    int bodyViewportHeight = settings_modalBodyViewportHeight(ctx);
    if (center && centerContentHeight > 0 && bodyViewportHeight > centerContentHeight) {
        int topSpacerPx = (bodyViewportHeight - centerContentHeight) / 2;
        e9ui_component_t *scrollStack = e9ui_stack_makeVertical();
        if (scrollStack) {
            if (topSpacerPx > 0) {
                e9ui_stack_addFixed(scrollStack, e9ui_vspacer_make(e9ui_unscale_px(ctx, topSpacerPx)));
            }
            e9ui_stack_addFixed(scrollStack, center);
            scrollContent = scrollStack;
        }
    }
    e9ui_component_t *centerScroll = center ? e9ui_scroll_make(scrollContent) : NULL;
    if (centerScroll) {
        int scrollContentHeight = settings_componentPreferredHeight(scrollContent, ctx, contentWidth);
        e9ui_scroll_setContentHeightPx(centerScroll, scrollContentHeight);
        scrollContent = centerScroll;
    }
    e9ui_component_t *btnDefaults = e9ui_button_make("Defaults", settings_uiDefaults, NULL);
    e9ui_component_t *btnSave = e9ui_button_make("Save", settings_uiSave, NULL);
    e9ui_component_t *btnCancel = e9ui_button_make("Cancel", settings_uiCancel, NULL);
    e9ui->settingsSaveButton = btnSave;
    settings_updateSaveLabel();
    e9ui_component_t *buttons = e9ui_flow_make();
    e9ui_flow_setPadding(buttons, 0);
    e9ui_flow_setSpacing(buttons, 8);
    e9ui_flow_setWrap(buttons, 0);
    if (btnSave) {
        e9ui_button_setTheme(btnSave, e9ui_theme_button_preset_green());
        e9ui_flow_add(buttons, btnSave);
    }
    if (btnDefaults) {
        e9ui_flow_add(buttons, btnDefaults);
    }
    if (btnCancel) {
        e9ui_button_setTheme(btnCancel, e9ui_theme_button_preset_red());
        e9ui_flow_add(buttons, btnCancel);
    }
    e9ui_component_t *footer = e9ui_stack_makeVertical();
    if (targetModal.footerWarning && !targetModalFooterInGlobalRow) {
        e9ui_stack_addFixed(footer, targetModal.footerWarning);
    }
    if (buttons) {
        e9ui_stack_addFixed(footer, buttons);
    }
    e9ui_component_t *overlay = e9ui_overlay_make(scrollContent, footer);
    e9ui_overlay_setAnchor(overlay, e9ui_anchor_bottom_right);
    e9ui_overlay_setMargin(overlay, 12);
    return overlay;
}

static void
settings_rebuildModalBody(e9ui_context_t *ctx)
{
    if (!e9ui->settingsModal || !ctx) {
        return;
    }
    e9ui_component_t *overlay = settings_buildModalBody(ctx);
    if (overlay) {
        e9ui_modal_setBodyChild(e9ui->settingsModal, overlay, ctx);
        settings_enableTextboxEnterNavigation(overlay);
        settings_focusFirstVisibleTextbox(ctx);
    }
}

void
settings_pollRebuild(e9ui_context_t *ctx)
{
    if (!settings_pendingRebuild) {
        return;
    }
    settings_pendingRebuild = 0;
    if (!e9ui->settingsModal || !ctx) {
        return;
    }
    if (e9ui->pendingRemove == e9ui->settingsModal) {
        return;
    }
    settings_rebuildModalBody(ctx);
}

void
settings_uiOpen(e9ui_context_t *ctx, void *user)
{
    (void)user;
    if (!ctx) {
        return;
    }
    if (e9ui->settingsModal) {
        return;
    }
    settings_clearCoreOptionsDirty();
    int margin = e9ui_scale_px(ctx, 32);
    int modalWidth = ctx->winW - margin * 2;
    int modalHeight = ctx->winH - margin * 2;
    if (modalWidth < 1) modalWidth = 1;
    if (modalHeight < 1) modalHeight = 1;
    e9ui_rect_t rect = { margin, margin, modalWidth, modalHeight };
    settings_copyConfig(&debugger.settingsEdit, &debugger.config);

    debugger.settingsEdit.target = target;
    target_settingsClearAllOptions();
    if (debugger.settingsEdit.target) {
        debugger.settingsEdit.target->settingsLoadOptions(&debugger.settingsEdit);
    }
    
    e9ui->settingsModal = e9ui_modal_show(ctx, "Settings", rect, settings_uiClosed, NULL);
    if (e9ui->settingsModal) {
        e9ui_component_t *overlay = settings_buildModalBody(ctx);
        if (overlay) {
            e9ui_modal_setBodyChild(e9ui->settingsModal, overlay, ctx);
            settings_enableTextboxEnterNavigation(overlay);
            settings_focusFirstVisibleTextbox(ctx);
        }
    }
}
