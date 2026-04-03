/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <SDL_ttf.h>

#include "help.h"
#include "debugger.h"
#include "e9ui.h"
#include "e9ui_link.h"
#include "e9ui_scroll.h"
#include "e9ui_text.h"
#include "hotkeys.h"
#include "debugger_input_bindings.h"
#include "lib9000/file.h"

typedef struct help_collapsible_state {
    const char *label;
    int collapsed;
    int sectionHeight;
    e9ui_component_t *section;
    e9ui_component_t *title;
} help_collapsible_state_t;

typedef struct help_sections_state {
    int leftHeight;
    int rightExpandedHeight;
    e9ui_component_t *scroll;
    help_collapsible_state_t amiga;
    help_collapsible_state_t megaDrive;
    help_collapsible_state_t neoGeo;
} help_sections_state_t;

static help_sections_state_t help_sections = {0};
static SDL_Cursor *help_cursorHand = NULL;
static SDL_Cursor *help_cursorArrow = NULL;

static void
help_ensureCursors(void)
{
    if (!help_cursorHand) {
        help_cursorHand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    }
    if (!help_cursorArrow) {
        help_cursorArrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    }
}

static const char *
help_baseName(const char *path)
{
    if (!path || !*path) {
        return NULL;
    }
    const char *slash = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    const char *best = slash > back ? slash : back;
    return best ? best + 1 : path;
}

static void
help_getReleaseVersionText(char *text, size_t textCap)
{
    if (!text || textCap == 0) {
        return;
    }

    int currentMinor = 0;
    char path[PATH_MAX];
    if (file_getAssetPath("assets/release_minor.txt", path, sizeof(path))) {
        FILE *fp = fopen(path, "r");
        if (fp) {
            char buf[64];
            if (fgets(buf, sizeof(buf), fp)) {
                char *end = NULL;
                long value = strtol(buf, &end, 10);
                if (end != buf && value >= 0 && value <= INT_MAX) {
                    currentMinor = (int)value;
                }
            }
            fclose(fp);
            snprintf(text, textCap, "Version v0.%d", currentMinor + 1);
            return;
        }
    }

    snprintf(text, textCap, "Version v0.%d", currentMinor + 1);
}

static void
help_closeModal(void)
{
    if (!e9ui->helpModal) {
        return;
    }
    e9ui_setHidden(e9ui->helpModal, 1);
    if (!e9ui->pendingRemove) {
        e9ui->pendingRemove = e9ui->helpModal;
    }
    memset(&help_sections, 0, sizeof(help_sections));
    e9ui->helpModal = NULL;
}

static void
help_uiClosed(e9ui_component_t *modal, void *user)
{
    (void)modal;
    (void)user;
    help_closeModal();
}

static void
help_uiClose(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    help_closeModal();
}

static void
help_openProjectPage(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    SDL_OpenURL("https://github.com/alpine9000/engine9000-public");
}

static void
help_openYoutubeChannel(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    SDL_OpenURL("https://www.youtube.com/channel/UCrmZyw0kDbA2QjWqjIRD_zw");
}

static void
help_openFreeSoftware(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    SDL_OpenURL("https://raw.githubusercontent.com/alpine9000/engine9000-public/refs/heads/main/COPYING");
}

static void
help_updateSectionTitle(help_collapsible_state_t *section)
{
    if (!section || !section->title || !section->label) {
        return;
    }
    char title[96];
    snprintf(title, sizeof(title), "%s  [%c]", section->label, section->collapsed ? '+' : '-');
    e9ui_text_setText(section->title, title);
    if (section->collapsed) {
        e9ui_text_setColor(section->title, (SDL_Color){150, 150, 150, 255});
    } else {
        e9ui_text_setColor(section->title, (SDL_Color){235, 235, 235, 255});
    }
}

static void
help_refreshCollapsedContentHeight(void)
{
    if (!help_sections.scroll) {
        return;
    }
    int rightH = help_sections.rightExpandedHeight;
    help_collapsible_state_t *sections[] = {
        &help_sections.amiga,
        &help_sections.megaDrive,
        &help_sections.neoGeo
    };
    for (int i = 0; i < 3; ++i) {
        if (sections[i]->collapsed) {
            rightH -= sections[i]->sectionHeight;
        }
    }
    if (rightH < 0) {
        rightH = 0;
    }
    int contentH = help_sections.leftHeight > rightH ? help_sections.leftHeight : rightH;
    e9ui_scroll_setContentHeightPx(help_sections.scroll, contentH);
}

static void
help_setSectionCollapsed(help_collapsible_state_t *section, int collapsed)
{
    if (!section) {
        return;
    }
    section->collapsed = collapsed ? 1 : 0;
    if (section->section) {
        e9ui_setHidden(section->section, section->collapsed);
    }
    help_updateSectionTitle(section);
}

static void
help_toggleSection(help_collapsible_state_t *section)
{
    if (!section) {
        return;
    }
    help_setSectionCollapsed(section, !section->collapsed);
    help_refreshCollapsedContentHeight();
}

#if E9K_ENABLE_AMIGA
static void
help_amigaTitleClick(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouse_ev)
{
    (void)self;
    (void)ctx;
    if (!mouse_ev || mouse_ev->button != E9UI_MOUSE_BUTTON_LEFT) {
        return;
    }
    help_toggleSection(&help_sections.amiga);
}
#endif

#if E9K_ENABLE_MEGADRIVE
static void
help_megaDriveTitleClick(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouse_ev)
{
    (void)self;
    (void)ctx;
    if (!mouse_ev || mouse_ev->button != E9UI_MOUSE_BUTTON_LEFT) {
        return;
    }
    help_toggleSection(&help_sections.megaDrive);
}
#endif

#if E9K_ENABLE_NEOGEO
static void
help_neoGeoTitleClick(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouse_ev)
{
    (void)self;
    (void)ctx;
    if (!mouse_ev || mouse_ev->button != E9UI_MOUSE_BUTTON_LEFT) {
        return;
    }
    help_toggleSection(&help_sections.neoGeo);
}
#endif

static void
help_sectionTitleHover(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouse_ev)
{
    (void)self;
    (void)mouse_ev;
    help_ensureCursors();
    if (help_cursorHand) {
        SDL_SetCursor(help_cursorHand);
        if (ctx) {
            ctx->cursorOverride = 1;
        }
    }
}

static void
help_sectionTitleMove(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouse_ev)
{
    (void)self;
    (void)mouse_ev;
    help_ensureCursors();
    if (help_cursorHand) {
        SDL_SetCursor(help_cursorHand);
        if (ctx) {
            ctx->cursorOverride = 1;
        }
    }
}

static void
help_sectionTitleLeave(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouse_ev)
{
    (void)self;
    (void)ctx;
    (void)mouse_ev;
    help_ensureCursors();
    if (help_cursorArrow) {
        SDL_SetCursor(help_cursorArrow);
    }
}

static e9ui_component_t *
help_makeCollapsibleHeading(e9ui_component_t *title,
                            void (*onClick)(e9ui_component_t *, e9ui_context_t *, const e9ui_mouse_event_t *))
{
    if (!title) {
        return NULL;
    }
    e9ui_component_t *row = e9ui_hstack_make();
    e9ui_hstack_addFlex(row, title);
    row->onClick = onClick;
    row->onHover = help_sectionTitleHover;
    row->onMouseMove = help_sectionTitleMove;
    row->onLeave = help_sectionTitleLeave;
    return row;
}

static int
help_measureKeyWidth(e9ui_context_t *ctx, const char **keys, size_t count)
{
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : (ctx ? ctx->font : NULL);
    int maxW = 0;
    if (!font) {
        return e9ui_scale_px(ctx, 80);
    }
    for (size_t i = 0; i < count; ++i) {
        if (!keys[i]) {
            continue;
        }
        int w = 0;
        if (TTF_SizeText(font, keys[i], &w, NULL) == 0) {
            if (w > maxW) {
                maxW = w;
            }
        }
    }
    return maxW;
}

static int
help_measureTextWidth(e9ui_context_t *ctx, const char *text)
{
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : (ctx ? ctx->font : NULL);
    if (!font || !text || !*text) {
        return 0;
    }
    int w = 0;
    if (TTF_SizeText(font, text, &w, NULL) != 0) {
        return 0;
    }
    return w;
}

#if E9K_ENABLE_AMIGA
static void
help_formatDebuggerInputBindingForTarget(int targetIndex, const char *optionKey, char *out, size_t outCap)
{
    const char *storedValue = NULL;
    char storedDefault[64];

    if (!out || outCap == 0) {
        return;
    }
    out[0] = '\0';
    if (!optionKey || !*optionKey) {
        return;
    }

    if (target && target->coreIndex == targetIndex && target->coreOptionGetValue) {
        storedValue = target->coreOptionGetValue(optionKey);
    }
    if (!storedValue || !*storedValue) {
        SDL_Keycode defKey = debugger_input_bindings_defaultKeyForTarget(targetIndex, optionKey);
        if (debugger_input_bindings_buildStoredValue(defKey, storedDefault, sizeof(storedDefault))) {
            storedValue = storedDefault;
        }
    }

    debugger_input_bindings_formatDisplayValue(storedValue, out, outCap);
    if (strcmp(out, "Unbound") == 0) {
        out[0] = '\0';
    }
}

static void
help_formatAmigaDpadSummary(char *out, size_t outCap)
{
    char up[32];
    char down[32];
    char left[32];
    char right[32];

    if (!out || outCap == 0) {
        return;
    }
    out[0] = '\0';

    help_formatDebuggerInputBindingForTarget(TARGET_AMIGA, "e9k_debugger_input_dpad_up", up, sizeof(up));
    help_formatDebuggerInputBindingForTarget(TARGET_AMIGA, "e9k_debugger_input_dpad_down", down, sizeof(down));
    help_formatDebuggerInputBindingForTarget(TARGET_AMIGA, "e9k_debugger_input_dpad_left", left, sizeof(left));
    help_formatDebuggerInputBindingForTarget(TARGET_AMIGA, "e9k_debugger_input_dpad_right", right, sizeof(right));

    if (strcmp(up, "Up") == 0 &&
        strcmp(down, "Down") == 0 &&
        strcmp(left, "Left") == 0 &&
        strcmp(right, "Right") == 0) {
        snprintf(out, outCap, "Arrows");
        out[outCap - 1] = '\0';
        return;
    }

    snprintf(out, outCap, "%s/%s/%s/%s",
             up[0] ? up : "-",
             down[0] ? down : "-",
             left[0] ? left : "-",
             right[0] ? right : "-");
    out[outCap - 1] = '\0';
}
#endif

static e9ui_component_t *
help_makeRow(const char *key, const char *value, int keyW, int gap, SDL_Color keyColor, SDL_Color valueColor)
{
    e9ui_component_t *row = e9ui_hstack_make();
    e9ui_component_t *keyText = e9ui_text_make(key);
    e9ui_component_t *valueText = e9ui_text_make(value);
    e9ui_text_setColor(keyText, keyColor);
    e9ui_text_setColor(valueText, valueColor);
    e9ui_hstack_addFixed(row, keyText, keyW);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(gap), gap);
    e9ui_hstack_addFlex(row, valueText);
    return row;
}

static void
help_add(e9ui_component_t *stack, e9ui_component_t *item, e9ui_context_t *ctx, int colW, int *totalH)
{
    e9ui_stack_addFixed(stack, item);
    if (item->preferredHeight) {
        *totalH += item->preferredHeight(item, ctx, colW);
    }
}

static void
help_addSpacer(e9ui_component_t *stack, int height, e9ui_context_t *ctx, int colW, int *totalH)
{
    if (!stack) {
        return;
    }
    e9ui_component_t *spacer = e9ui_vspacer_make(height);
    e9ui_stack_addFixed(stack, spacer);
    if (spacer->preferredHeight) {
        *totalH += spacer->preferredHeight(spacer, ctx, colW);
    }
}

void
help_cancelModal(void)
{
    help_closeModal();
}

void
help_showModal(e9ui_context_t *ctx)
{
    if (!ctx) {
        return;
    }
    if (e9ui->helpModal) {
        return;
    }
    int margin = e9ui_scale_px(ctx, 32);
    int w = ctx->winW - margin * 2;
    int h = ctx->winH - margin * 2;
    if (w < 1) {
        w = 1;
    }
    if (h < 1) {
        h = 1;
    }
    e9ui_rect_t rect = { margin, margin, w, h };
    e9ui->helpModal = e9ui_modal_show(ctx, "HELP", rect, help_uiClosed, NULL);

    int baseText = e9ui->theme.text.fontSize > 0 ? e9ui->theme.text.fontSize : E9UI_THEME_TEXT_FONT_SIZE;
    int headingSize = baseText + 2;
    SDL_Color headingColor = (SDL_Color){235, 235, 235, 255};
    SDL_Color bodyColor = (SDL_Color){210, 210, 210, 255};

    e9ui_component_t *stackLeft = e9ui_stack_makeVertical();
    e9ui_component_t *stackRight = e9ui_stack_makeVertical();
    int gap = 10;
    int gapSmall = 6;
    int colGap = e9ui_scale_px(ctx, 16);
    int colW = e9ui_scale_px(ctx, 384);
    int columnGap = e9ui_scale_px(ctx, 32);
    int contentHLeft = 0;
    int contentHRight = 0;
    e9ui_component_t *titleShortcuts = e9ui_text_make("DEBUGGER HOTKEYS");
    e9ui_text_setBold(titleShortcuts, 1);
    e9ui_text_setFontSize(titleShortcuts, headingSize);
    e9ui_text_setColor(titleShortcuts, headingColor);

    char shortcutKeyBufs[31][64];
    const char *shortcutKeys[31] = {0};
    const char *shortcutVals[] = { "Help",
                                   "Screenshot to clipboard",
                                   "Cycle core + restart",
                                   "Toggle record",
                                   "Warp",
                                   "Toggle audio",
                                   "Save state",
                                   "Restore state",
                                   "Restart",
                                   "Reset core",
                                   "Toggle hotkeys",				   
                                   "Settings",
                                   "Hex converter",
                                   "In-place hex convert",
                                   "Toggle fullscreen",
                                   "Release mouse capture",
                                   "Close modal",
                                   "Activate console",
                                   "Continue",
                                   "Pause",
                                   "Step",
                                   "Next",
                                   "Step inst",
                                   "Breakpoint add current",
                                   "Frame step back",
                                   "Frame step",
                                   "Frame continue",
                                   "Copy selection",
                                   "Checkpoint profile",
                                   "Checkpoint reset",
                                   "Checkpoint dump" };
    const char *shortcutActionIds[] = {
        "help",
        "screenshot",
        "cycle_core_restart",
        "rolling_save_toggle",
        "warp",
        "audio_toggle",
        "save_state",
        "restore_state",
        "restart",
        "reset_core",
        "hotkeys_toggle",
        "settings",
        "hex_convert",
        "hex_convert_inline",
        "fullscreen",
        "mouse_release",
        NULL,
        "prompt_focus",
        "continue",
        "pause",
        "step",
        "next",
        "step_inst",
        "breakpoint_add_current",
        "frame_back",
        "frame_step",
        "frame_continue",
        NULL,
        "checkpoint_prev",
        "checkpoint_reset",
        "checkpoint_next"
    };
    const char *shortcutFixedKeys[] = {
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        "ESC",
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        "CTRL+c",
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    };
    size_t shortcutCount = sizeof(shortcutVals) / sizeof(shortcutVals[0]);
    for (size_t i = 0; i < shortcutCount; ++i) {
        if (shortcutActionIds[i]) {
            if (!hotkeys_formatActionBindingDisplay(shortcutActionIds[i], shortcutKeyBufs[i],
                                                    sizeof(shortcutKeyBufs[i]))) {
                snprintf(shortcutKeyBufs[i], sizeof(shortcutKeyBufs[i]), "?");
            }
        } else if (shortcutFixedKeys[i]) {
            snprintf(shortcutKeyBufs[i], sizeof(shortcutKeyBufs[i]), "%s", shortcutFixedKeys[i]);
        } else {
            snprintf(shortcutKeyBufs[i], sizeof(shortcutKeyBufs[i]), "?");
        }
        shortcutKeys[i] = shortcutKeyBufs[i];
    }
    int shortcutKeyW = help_measureKeyWidth(ctx, shortcutKeys, sizeof(shortcutKeys) / sizeof(shortcutKeys[0]));

    e9ui_component_t *titleSourcePane = e9ui_text_make("SOURCE PANE");
    e9ui_text_setBold(titleSourcePane, 1);
    e9ui_text_setFontSize(titleSourcePane, headingSize);
    e9ui_text_setColor(titleSourcePane, headingColor);
    const char *sourcePaneKeys[] = {
        "CTRL+s",
        "CTRL+r",
        "ESC",
        "UP/DOWN",
        "SHIFT+UP/DOWN",
        "CTRL+UP/DOWN",
        "ALT+UP/DOWN",
        "PAGEUP/PAGEDOWN",
        "HOME/END"
    };
    const char *sourcePaneVals[] = {
        "Search",
        "Reverse search",
        "Cancel search",
        "Scroll 1 line",
        "Scroll 4 lines",
        "Scroll half page",
        "Scroll 16 lines",
        "Scroll one page",
        "Jump to start/end"
    };
    int sourcePaneKeyW = help_measureKeyWidth(ctx, sourcePaneKeys,
                                              sizeof(sourcePaneKeys) / sizeof(sourcePaneKeys[0]));

#if E9K_ENABLE_MEGADRIVE
    e9ui_component_t *titleMegaDrive = e9ui_text_make("MEGA DRIVE SHORTCUTS");
    e9ui_text_setBold(titleMegaDrive, 1);
    e9ui_text_setFontSize(titleMegaDrive, headingSize);
    e9ui_text_setColor(titleMegaDrive, headingColor);
    e9ui_component_t *headingMegaDrive = help_makeCollapsibleHeading(titleMegaDrive, help_megaDriveTitleClick);

    e9ui_component_t *titleMegaDriveKeyboard = e9ui_text_make("MEGA DRIVE KEYBOARD");
    e9ui_text_setBold(titleMegaDriveKeyboard, 1);
    e9ui_text_setColor(titleMegaDriveKeyboard, headingColor);
    const char *megaKbKeys[] = { "Arrows", "L/R aLT", "L/R CTRL", "L/R SHIFT", "SPACE", "1", "5" };
    const char *megaKbVals[] = { "D-pad", "A", "B", "C", "D", "START", "SELECT" };
    int megaKbKeyW = help_measureKeyWidth(ctx, megaKbKeys, sizeof(megaKbKeys) / sizeof(megaKbKeys[0]));

    e9ui_component_t *titleMegaDriveController = e9ui_text_make("MEGA DRIVE JOYSTICK CONTROLS");
    e9ui_text_setBold(titleMegaDriveController, 1);
    e9ui_text_setColor(titleMegaDriveController, headingColor);
    const char *megaPadKeys[] = { "Left stick / D-PAD", "A", "B", "X", "Y", "LB", "RB", "START", "BACK" };
    const char *megaPadVals[] = { "Directions", "A", "B", "C", "D", "L", "R", "START", "SELECT" };
    int megaPadKeyW = help_measureKeyWidth(ctx, megaPadKeys, sizeof(megaPadKeys) / sizeof(megaPadKeys[0]));
#endif

#if E9K_ENABLE_NEOGEO
    e9ui_component_t *titleNeoGeo = e9ui_text_make("NEO GEO SHORTCUTS");
    e9ui_text_setBold(titleNeoGeo, 1);
    e9ui_text_setFontSize(titleNeoGeo, headingSize);
    e9ui_text_setColor(titleNeoGeo, headingColor);
    e9ui_component_t *headingNeoGeo = help_makeCollapsibleHeading(titleNeoGeo, help_neoGeoTitleClick);

    e9ui_component_t *titleNeoGeoKeyboard = e9ui_text_make("NEO GEO KEYBOARD");
    e9ui_text_setBold(titleNeoGeoKeyboard, 1);
    e9ui_text_setColor(titleNeoGeoKeyboard, headingColor);
    const char *neoKbKeys[] = { "Arrows", "L/R ALT", "L/R CTRL", "L/R SHIFT", "SPACE", "1", "5" };
    const char *neoKbVals[] = { "D-pad", "A", "B", "C", "D", "START", "SELECT" };
    int neoKbKeyW = help_measureKeyWidth(ctx, neoKbKeys, sizeof(neoKbKeys) / sizeof(neoKbKeys[0]));

    e9ui_component_t *titleNeoGeoController = e9ui_text_make("NEO GEO JOYSTICK CONTROLS");
    e9ui_text_setBold(titleNeoGeoController, 1);
    e9ui_text_setColor(titleNeoGeoController, headingColor);
    const char *neoPadKeys[] = { "Left stick / D-PAD", "A", "B", "X", "Y", "LB", "RB", "START", "BACK" };
    const char *neoPadVals[] = { "Directions", "A", "B", "C", "D", "L", "R", "START", "SELECT" };
    int neoPadKeyW = help_measureKeyWidth(ctx, neoPadKeys, sizeof(neoPadKeys) / sizeof(neoPadKeys[0]));
#endif

    help_add(stackLeft, titleShortcuts, ctx, colW, &contentHLeft);
    help_addSpacer(stackLeft, gapSmall, ctx, colW, &contentHLeft);
    for (size_t i = 0; i < sizeof(shortcutKeys) / sizeof(shortcutKeys[0]); ++i) {
        e9ui_component_t *row = help_makeRow(shortcutKeys[i], shortcutVals[i], shortcutKeyW, colGap, bodyColor, bodyColor);
        help_add(stackLeft, row, ctx, colW, &contentHLeft);
    }

    help_add(stackRight, titleSourcePane, ctx, colW, &contentHRight);
    help_addSpacer(stackRight, gapSmall, ctx, colW, &contentHRight);
    for (size_t i = 0; i < sizeof(sourcePaneKeys) / sizeof(sourcePaneKeys[0]); ++i) {
        e9ui_component_t *row = help_makeRow(sourcePaneKeys[i], sourcePaneVals[i], sourcePaneKeyW, colGap,
                                             bodyColor, bodyColor);
        help_add(stackRight, row, ctx, colW, &contentHRight);
    }

#if E9K_ENABLE_MEGADRIVE
    e9ui_component_t *megaDriveSection = e9ui_stack_makeVertical();
    int megaDriveHeight = 0;
    help_addSpacer(megaDriveSection, gapSmall, ctx, colW, &megaDriveHeight);
    help_add(megaDriveSection, titleMegaDriveKeyboard, ctx, colW, &megaDriveHeight);
    help_addSpacer(megaDriveSection, gapSmall, ctx, colW, &megaDriveHeight);
    for (size_t i = 0; i < sizeof(megaKbKeys) / sizeof(megaKbKeys[0]); ++i) {
        e9ui_component_t *row = help_makeRow(megaKbKeys[i], megaKbVals[i], megaKbKeyW, colGap, bodyColor, bodyColor);
        help_add(megaDriveSection, row, ctx, colW, &megaDriveHeight);
    }
    help_addSpacer(megaDriveSection, gap, ctx, colW, &megaDriveHeight);
    help_add(megaDriveSection, titleMegaDriveController, ctx, colW, &megaDriveHeight);
    help_addSpacer(megaDriveSection, gapSmall, ctx, colW, &megaDriveHeight);
    for (size_t i = 0; i < sizeof(megaPadKeys) / sizeof(megaPadKeys[0]); ++i) {
        e9ui_component_t *row = help_makeRow(megaPadKeys[i], megaPadVals[i], megaPadKeyW, colGap, bodyColor, bodyColor);
        help_add(megaDriveSection, row, ctx, colW, &megaDriveHeight);
    }
#endif

#if E9K_ENABLE_NEOGEO
    e9ui_component_t *neoGeoSection = e9ui_stack_makeVertical();
    int neoGeoHeight = 0;
    help_addSpacer(neoGeoSection, gapSmall, ctx, colW, &neoGeoHeight);
    help_add(neoGeoSection, titleNeoGeoKeyboard, ctx, colW, &neoGeoHeight);
    help_addSpacer(neoGeoSection, gapSmall, ctx, colW, &neoGeoHeight);
    for (size_t i = 0; i < sizeof(neoKbKeys) / sizeof(neoKbKeys[0]); ++i) {
        e9ui_component_t *row = help_makeRow(neoKbKeys[i], neoKbVals[i], neoKbKeyW, colGap, bodyColor, bodyColor);
        help_add(neoGeoSection, row, ctx, colW, &neoGeoHeight);
    }
    help_addSpacer(neoGeoSection, gap, ctx, colW, &neoGeoHeight);
    help_add(neoGeoSection, titleNeoGeoController, ctx, colW, &neoGeoHeight);
    help_addSpacer(neoGeoSection, gapSmall, ctx, colW, &neoGeoHeight);
    for (size_t i = 0; i < sizeof(neoPadKeys) / sizeof(neoPadKeys[0]); ++i) {
        e9ui_component_t *row = help_makeRow(neoPadKeys[i], neoPadVals[i], neoPadKeyW, colGap, bodyColor, bodyColor);
        help_add(neoGeoSection, row, ctx, colW, &neoGeoHeight);
    }
#endif

#if E9K_ENABLE_AMIGA
    e9ui_component_t *titleAmiga = e9ui_text_make("AMIGA SHORTCUTS");
    e9ui_text_setBold(titleAmiga, 1);
    e9ui_text_setFontSize(titleAmiga, headingSize);
    e9ui_text_setColor(titleAmiga, headingColor);
    e9ui_component_t *headingAmiga = help_makeCollapsibleHeading(titleAmiga, help_amigaTitleClick);

    e9ui_component_t *titleAmigaKeyboard = e9ui_text_make("AMIGA KEYBOARD");
    e9ui_text_setBold(titleAmigaKeyboard, 1);
    e9ui_text_setColor(titleAmigaKeyboard, headingColor);
    char amigaKbKeyBufDpad[96];
    char amigaKbKeyBufFire1[64];
    char amigaKbKeyBufFire2[64];
    const char *amigaKbKeys[] = { amigaKbKeyBufDpad, amigaKbKeyBufFire1, amigaKbKeyBufFire2 };
    const char *amigaKbVals[] = { "D-pad", "Fire 1", "Fire 2" };
    help_formatAmigaDpadSummary(amigaKbKeyBufDpad, sizeof(amigaKbKeyBufDpad));
    help_formatDebuggerInputBindingForTarget(TARGET_AMIGA, "e9k_debugger_input_button_b",
                                             amigaKbKeyBufFire1, sizeof(amigaKbKeyBufFire1));
    help_formatDebuggerInputBindingForTarget(TARGET_AMIGA, "e9k_debugger_input_button_a",
                                             amigaKbKeyBufFire2, sizeof(amigaKbKeyBufFire2));
    int amigaKbKeyW = help_measureKeyWidth(ctx, amigaKbKeys, sizeof(amigaKbKeys) / sizeof(amigaKbKeys[0]));

    e9ui_component_t *titleAmigaController = e9ui_text_make("AMIGA JOYSTICK CONTROLS");
    e9ui_text_setBold(titleAmigaController, 1);
    e9ui_text_setColor(titleAmigaController, headingColor);
    const char *amigaPadKeys[] = { "Left stick / D-pad", "A", "B" };
    const char *amigaPadVals[] = { "Directions", "Fire 1", "Fire 2" };
    int amigaPadKeyW = help_measureKeyWidth(ctx, amigaPadKeys, sizeof(amigaPadKeys) / sizeof(amigaPadKeys[0]));

    e9ui_component_t *amigaSection = e9ui_stack_makeVertical();
    int amigaHeight = 0;
    help_addSpacer(amigaSection, gapSmall, ctx, colW, &amigaHeight);
    help_add(amigaSection, titleAmigaKeyboard, ctx, colW, &amigaHeight);
    help_addSpacer(amigaSection, gapSmall, ctx, colW, &amigaHeight);
    for (size_t i = 0; i < sizeof(amigaKbKeys) / sizeof(amigaKbKeys[0]); ++i) {
        e9ui_component_t *row = help_makeRow(amigaKbKeys[i], amigaKbVals[i], amigaKbKeyW, colGap, bodyColor, bodyColor);
        help_add(amigaSection, row, ctx, colW, &amigaHeight);
    }
    help_addSpacer(amigaSection, gap, ctx, colW, &amigaHeight);
    help_add(amigaSection, titleAmigaController, ctx, colW, &amigaHeight);
    help_addSpacer(amigaSection, gapSmall, ctx, colW, &amigaHeight);
    for (size_t i = 0; i < sizeof(amigaPadKeys) / sizeof(amigaPadKeys[0]); ++i) {
        e9ui_component_t *row = help_makeRow(amigaPadKeys[i], amigaPadVals[i], amigaPadKeyW, colGap, bodyColor, bodyColor);
        help_add(amigaSection, row, ctx, colW, &amigaHeight);
    }

    help_addSpacer(stackRight, gap, ctx, colW, &contentHRight);
    help_add(stackRight, headingAmiga, ctx, colW, &contentHRight);
    help_addSpacer(stackRight, gapSmall, ctx, colW, &contentHRight);
    help_add(stackRight, amigaSection, ctx, colW, &contentHRight);
#endif

#if E9K_ENABLE_MEGADRIVE
    help_addSpacer(stackRight, gap, ctx, colW, &contentHRight);
    help_add(stackRight, headingMegaDrive, ctx, colW, &contentHRight);
    help_addSpacer(stackRight, gapSmall, ctx, colW, &contentHRight);
    help_add(stackRight, megaDriveSection, ctx, colW, &contentHRight);
#endif

#if E9K_ENABLE_NEOGEO
    help_addSpacer(stackRight, gap, ctx, colW, &contentHRight);
    help_add(stackRight, headingNeoGeo, ctx, colW, &contentHRight);
    help_addSpacer(stackRight, gapSmall, ctx, colW, &contentHRight);
    help_add(stackRight, neoGeoSection, ctx, colW, &contentHRight);
#endif

    e9ui_component_t *titleCli = e9ui_text_make("COMMAND LINE");
    e9ui_text_setBold(titleCli, 1);
    e9ui_text_setFontSize(titleCli, headingSize);
    e9ui_text_setColor(titleCli, headingColor);

    const char *prog = debugger.argv0[0] ? help_baseName(debugger.argv0) : "e9k-debugger";
    char cliCmd[PATH_MAX + 16];
    snprintf(cliCmd, sizeof(cliCmd), "%s --help", prog);
    e9ui_component_t *lineCliPrefix = e9ui_text_make("Use");
    e9ui_component_t *lineCliCmd = e9ui_text_make(cliCmd);
    e9ui_component_t *lineCliSuffix = e9ui_text_make("for options");
    e9ui_text_setColor(lineCliPrefix, bodyColor);
    e9ui_text_setColor(lineCliCmd, headingColor);
    e9ui_text_setColor(lineCliSuffix, bodyColor);

    int cliGap = e9ui_scale_px(ctx, 6);
    int prefixW = help_measureTextWidth(ctx, "Use");
    int cmdW = help_measureTextWidth(ctx, cliCmd);
    e9ui_component_t *rowCli = e9ui_hstack_make();
    e9ui_hstack_addFixed(rowCli, lineCliPrefix, prefixW);
    e9ui_hstack_addFixed(rowCli, e9ui_spacer_make(cliGap), cliGap);
    e9ui_hstack_addFixed(rowCli, lineCliCmd, cmdW);
    e9ui_hstack_addFixed(rowCli, e9ui_spacer_make(cliGap), cliGap);
    e9ui_hstack_addFlex(rowCli, lineCliSuffix);

    help_addSpacer(stackLeft, gap, ctx, colW, &contentHLeft);
    help_add(stackLeft, titleCli, ctx, colW, &contentHLeft);
    help_addSpacer(stackLeft, gapSmall, ctx, colW, &contentHLeft);
    help_add(stackLeft, rowCli, ctx, colW, &contentHLeft);

    int contentH = contentHLeft > contentHRight ? contentHLeft : contentHRight;
    e9ui_component_t *columns = e9ui_hstack_make();
    e9ui_hstack_addFixed(columns, stackLeft, colW);
    e9ui_hstack_addFixed(columns, e9ui_spacer_make(columnGap), columnGap);
    e9ui_hstack_addFixed(columns, stackRight, colW);

    int contentW = colW * 2 + columnGap;
    e9ui_component_t *scroll = e9ui_scroll_make(columns);
    e9ui_scroll_setContentHeightPx(scroll, contentH);
    memset(&help_sections, 0, sizeof(help_sections));
    help_sections.leftHeight = contentHLeft;
    help_sections.rightExpandedHeight = contentHRight;
    help_sections.scroll = scroll;

#if E9K_ENABLE_AMIGA
    help_sections.amiga.label = "AMIGA SHORTCUTS";
    help_sections.amiga.section = amigaSection;
    help_sections.amiga.title = titleAmiga;
    help_sections.amiga.sectionHeight = amigaHeight;
#endif

#if E9K_ENABLE_MEGADRIVE
    help_sections.megaDrive.label = "MEGA DRIVE SHORTCUTS";
    help_sections.megaDrive.section = megaDriveSection;
    help_sections.megaDrive.title = titleMegaDrive;
    help_sections.megaDrive.sectionHeight = megaDriveHeight;
#endif

#if E9K_ENABLE_NEOGEO
    help_sections.neoGeo.label = "NEO GEO SHORTCUTS";
    help_sections.neoGeo.section = neoGeoSection;
    help_sections.neoGeo.title = titleNeoGeo;
    help_sections.neoGeo.sectionHeight = neoGeoHeight;
#endif

#if E9K_ENABLE_AMIGA
    help_setSectionCollapsed(&help_sections.amiga, 1);
#endif
#if E9K_ENABLE_MEGADRIVE
    help_setSectionCollapsed(&help_sections.megaDrive, 1);
#endif
#if E9K_ENABLE_NEOGEO
    help_setSectionCollapsed(&help_sections.neoGeo, 1);
#endif
    help_refreshCollapsedContentHeight();
    e9ui_component_t *center = e9ui_center_make(scroll);
    int centerW = e9ui_unscale_px(ctx, contentW);
    e9ui_center_setSize(center, centerW, 0);
    e9ui_component_t *btnClose = e9ui_button_make("Close", help_uiClose, NULL);
    e9ui_button_setTheme(btnClose, e9ui_theme_button_preset_green());
    e9ui_component_t *overlayClose = e9ui_overlay_make(center, btnClose);
    e9ui_overlay_setAnchor(overlayClose, e9ui_anchor_bottom_right);
    e9ui_overlay_setMargin(overlayClose, 12);

    char versionText[64];
    help_getReleaseVersionText(versionText, sizeof(versionText));
    e9ui_component_t *versionLabel = e9ui_text_make(versionText);
    e9ui_text_setColor(versionLabel, (SDL_Color){180, 180, 180, 255});
    e9ui_component_t *footerMeta = e9ui_hstack_make();
    SDL_Color sepColor = bodyColor;
    e9ui_component_t *sep1 = e9ui_text_make(" | ");
    e9ui_component_t *sep2 = e9ui_text_make(" | ");
    e9ui_component_t *sep3 = e9ui_text_make(" | ");
    e9ui_component_t *freeLink = e9ui_link_make("Free Software", help_openFreeSoftware, NULL);
    e9ui_component_t *srcLink = e9ui_link_make("Source Code", help_openProjectPage, NULL);
    e9ui_component_t *ytLink = e9ui_link_make("Youtube Channel", help_openYoutubeChannel, NULL);
    e9ui_text_setColor(sep1, sepColor);
    e9ui_text_setColor(sep2, sepColor);
    e9ui_text_setColor(sep3, sepColor);
    e9ui_hstack_addFixed(footerMeta, versionLabel, help_measureTextWidth(ctx, versionText));
    e9ui_hstack_addFixed(footerMeta, sep1, help_measureTextWidth(ctx, " | "));
    e9ui_hstack_addFixed(footerMeta, freeLink, help_measureTextWidth(ctx, "Free Software"));
    e9ui_hstack_addFixed(footerMeta, sep2, help_measureTextWidth(ctx, " | "));
    e9ui_hstack_addFixed(footerMeta, srcLink, help_measureTextWidth(ctx, "Source Code"));
    e9ui_hstack_addFixed(footerMeta, sep3, help_measureTextWidth(ctx, " | "));
    e9ui_hstack_addFixed(footerMeta, ytLink, help_measureTextWidth(ctx, "Youtube Channel"));

    e9ui_component_t *body = overlayClose;
    e9ui_component_t *overlayVersion = e9ui_overlay_make(body, footerMeta);
    e9ui_overlay_setAnchor(overlayVersion, e9ui_anchor_bottom_left);
    e9ui_overlay_setMargin(overlayVersion, 12);

    e9ui_modal_setBodyChild(e9ui->helpModal, overlayVersion, ctx);
    e9ui_setFocus(ctx, btnClose);
}
