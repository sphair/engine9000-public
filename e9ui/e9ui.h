/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <SDL_image.h>
#include <limits.h>

#include "alloc.h"
#include "e9ui_component.h"
#include "e9ui_context.h"
#include "e9ui_stack.h"
#include "e9ui_split.h"
#include "e9ui_split_stack.h"
#include "e9ui_image.h"
#include "e9ui_box.h"
#include "e9ui_hstack.h"
#include "e9ui_button.h"
#include "e9ui_flow.h"
#include "e9ui_header_flow.h"
#include "e9ui_spacer.h"
#include "e9ui_vspacer.h"
#include "e9ui_textbox.h"
#include "e9ui_data_edit.h"
#include "e9ui_separator.h"
#include "e9ui_overlay.h"
#include "e9ui_center.h"
#include "e9ui_fileselect.h"
#include "e9ui_labeled_textbox.h"
#include "e9ui_labeled_checkbox.h"
#include "e9ui_labeled_select.h"
#include "e9ui_range_bar.h"
#include "e9ui_seek_bar.h"
#include "e9ui_slider.h"
#include "e9ui_modal.h"
#include "e9ui_event.h"
#include "e9ui_text_cache.h"
#include "e9ui_text_select.h"
#include "e9ui_scale.h"
#include "e9ui_link.h"
#include "e9ui_checkbox.h"
#include "e9ui_window.h"
#include "e9ui_theme_defaults.h"
#include "e9ui_theme_presets.h"
#include "e9ui_theme.h"
#include "file.h"
#include "debug.h"

typedef struct {
  list_t* _cursor;                       
  e9ui_component_t* child;               
  void* meta;                            
  e9ui_component_child_t* container;     
} e9ui_child_iterator;

typedef struct {
  list_t* head;
  list_t* cursor;
  e9ui_component_t* child;
  void* meta;
  e9ui_component_child_t* container;
} e9ui_child_reverse_iterator;

typedef struct e9k_hotkey_entry {
    int id;
    int key;   // SDL_Keycode
    int mask;  // SDL_Keymod mask
    int value; // SDL_Keymod value
    void (*cb)(e9ui_context_t *ctx, void *user);
    void *user;
    int  active;
} e9k_hotkey_entry_t;

typedef struct e9k_hotkey_registry {
    e9k_hotkey_entry_t *entries;
    int count;
    int cap;
    int next_id;
} e9k_hotkey_registry_t;

#define E9UI_GAMEPAD_GUID_CAP 64
#define E9UI_GAMEPAD_NAME_CAP 256

typedef struct e9ui_gamepad_info {
    char guid[E9UI_GAMEPAD_GUID_CAP];
    char name[E9UI_GAMEPAD_NAME_CAP];
} e9ui_gamepad_info_t;

typedef void (*e9ui_defer_fn_t)(e9ui_context_t *ctx, void *user);

typedef struct e9ui_defer_entry {
    e9ui_defer_fn_t fn;
    void *user;
} e9ui_defer_entry_t;

typedef struct e9k_layout_config {
    float splitSrcConsole;
    float splitUpper;
    float splitRight;
    float splitLr;
    int   winX;
    int   winY;
    int   winW;
    int   winH;
    int   memTrackWinX;
    int   memTrackWinY;
    int   memTrackWinW;
    int   memTrackWinH;
} e9k_layout_config_t;

typedef struct {
  e9k_theme_button_t button;
  e9k_theme_button_t miniButton;
  e9k_theme_button_t microButton;
  e9k_theme_button_t nanoButton;
  e9k_theme_text_t   text;
  e9k_theme_titlebar_t titlebar;
  e9k_theme_checkbox_t checkbox;
  e9k_theme_disabled_t disabled;
} e9ui_theme_t;

typedef enum e9k_transition_mode {
  e9k_transition_none = 0,
  e9k_transition_slide,
  e9k_transition_explode,
  e9k_transition_doom,
  e9k_transition_flip,
  e9k_transition_rbar,
  e9k_transition_random,
  e9k_transition_cycle
} e9k_transition_mode_t;

typedef struct {
  int inTransition;
  e9k_transition_mode_t mode;
  e9k_transition_mode_t fullscreenMode;
  int fullscreenModeSet;  
  int cycleIndex;
} e9ui_transition_state_t;

typedef struct {
  int mouseX;
  int mouseY;
  e9ui_component_t *root;
  e9ui_component_child_t* rootComponent;
  e9ui_context_t    ctx;
  e9ui_component_t *toolbar; 
  e9ui_component_t *profileButton; 
  e9ui_component_t *analyseButton;
  e9ui_component_t *speedButton; 
  e9ui_component_t *restartButton;
  e9ui_component_t *resetButton;
  e9ui_component_t *audioButton; 
  e9ui_component_t *settingsButton;
  e9ui_component_t *settingsModal; 
  e9ui_component_t *settingsSaveButton;
  e9ui_component_t *coreOptionsModal;
  e9ui_component_t *helpModal; 
  e9ui_component_t *prompt; 
  e9ui_component_t *pendingRemove; 
  e9ui_defer_entry_t *deferred;
  int deferredCount;
  int deferredCap;
  e9ui_component_t *sourceBox; 
  e9ui_component_t *fullscreen;
  e9ui_component_t *overlayRoot;
  e9ui_theme_t theme;
  char sourceTitle[PATH_MAX];
  e9k_hotkey_registry_t hotkeys;
  e9k_layout_config_t layout;
  e9ui_transition_state_t transition;
  int currentDisplayIndex;

  int glCompositeEnabled;
  int glCompositeCapture;  
} e9ui_global_t;

extern e9ui_global_t *e9ui;

void
e9ui_loadLayoutComponents(const char* configPath);

void
e9ui_saveLayout(const char* configPath);
  
void
e9ui_loadLayoutWindow(void);

void
e9ui_toggleSrcMode(e9ui_context_t *ctx, void *user);

e9ui_component_t *
e9ui_findById(e9ui_component_t *root, const char *id);

void
e9ui_debugDrawBounds(e9ui_component_t *c, e9ui_context_t *ctx, int depth);

void
e9ui_renderFrame(void);

void
e9ui_renderFrameNoLayout(void);

void
e9ui_renderFrameNoLayoutNoPresent(void);

void
e9ui_renderFrameNoLayoutNoPresentFade(int fadeAlpha);

void
e9ui_renderFrameNoLayoutNoPresentNoClear(void);

int
e9ui_processEvents(void);

size_t
e9ui_gamepadReadAvailable(e9ui_gamepad_info_t *out, size_t cap);

const char *
e9ui_gamepadGetPreferredGuid(void);

void
e9ui_gamepadSetPreferredGuid(const char *guid);

int
e9ui_defer(e9ui_context_t *ctx, e9ui_defer_fn_t fn, void *user);

e9ui_component_t *
e9ui_focusTraversalRoot(e9ui_context_t *ctx, e9ui_component_t *current);

e9ui_component_t *
e9ui_focusFindNext(e9ui_component_t *root, e9ui_component_t *current, int reverse);

void
e9ui_focusAdvance(e9ui_context_t *ctx, e9ui_component_t *current, int reverse);

void
e9ui_drawFocusRingRect(e9ui_context_t *ctx, SDL_Rect rect, int padPx);

void
e9ui_cursorRequest(e9ui_context_t *ctx, void *owner, SDL_Cursor *cursor);

void
e9ui_cursorCapture(e9ui_context_t *ctx, void *owner, SDL_Cursor *cursor);

void
e9ui_cursorRelease(e9ui_context_t *ctx, void *owner);

int
e9ui_cursorIsCapturedBy(const e9ui_context_t *ctx, const void *owner);

void
e9ui_setFullscreenComponent(e9ui_component_t *comp);

void
e9ui_clearFullscreenComponent(void);

void
e9ui_showTransientMessage(const char *message);

int
e9ui_getFpsEnabled(void);

void
e9ui_setFpsEnabled(int enabled);

uint32_t
e9ui_getTicks(e9ui_context_t *ctx);

void
e9ui_setRefreshHz(e9ui_context_t *ctx, int refreshHz);

const char *
e9ui_getWindowIconAssetPath(e9ui_context_t *ctx);

uint64_t
e9ui_getNextUiFrameId(e9ui_context_t *ctx);

void
e9ui_commitUiFrameId(e9ui_context_t *ctx, uint64_t frameId);

int
e9ui_captureUiFrame(e9ui_context_t *ctx, uint64_t frameId, SDL_Renderer *renderer);

int
e9ui_transformTextboxSelection(e9ui_context_t *ctx,
                               const char *actionId,
                               const char *input,
                               char *out,
                               size_t outCap);

int
e9ui_shouldPresentFrame(e9ui_context_t *ctx);

int
e9ui_pollInjectedUiEvent(e9ui_context_t *ctx, uint64_t frameId, SDL_Event *eventValue);

void
e9ui_recordUiEvent(e9ui_context_t *ctx, uint64_t frameId, const SDL_Event *eventValue);

int
e9ui_runFullscreenTransition(e9ui_context_t *ctx,
                             int entering,
                             e9ui_component_t *from,
                             e9ui_component_t *to,
                             int width,
                             int height);

void
e9ui_setMainWindowFocused(e9ui_context_t *ctx, int focused);

int
e9ui_handleGlobalKeydown(e9ui_context_t *ctx, const SDL_KeyboardEvent *kev);

void
e9ui_shutdownHostUi(e9ui_context_t *ctx);

void
e9ui_prepareMainWindow(e9ui_context_t *ctx,
                       int cliOverrideWindowSize,
                       int startHidden,
                       int *wantX,
                       int *wantY,
                       int *wantW,
                       int *wantH,
                       Uint32 *winFlags);

int
e9ui_shouldUseVsync(e9ui_context_t *ctx);

void
e9ui_finalizeMainWindow(e9ui_context_t *ctx,
                        SDL_Window *window,
                        SDL_Renderer *renderer,
                        int wantW,
                        int wantH);

void
e9ui_drawSelectableText(e9ui_context_t *ctx,
                        e9ui_component_t *owner,
                        TTF_Font *font,
                        const char *text,
                        SDL_Color color,
                        int x,
                        int y,
                        int lineHeight,
                        int hitW,
                        void *bucket,
                        int dragOnly,
                        int selectable);

e9ui_component_t *
e9ui_getFullscreenComponent(void);

e9ui_component_t *
e9ui_getOverlayHost(void);

e9ui_component_t *
e9ui_getSelectionScopeForOwner(const e9ui_component_t *owner);

int
e9ui_isFullscreenComponent(const e9ui_component_t *comp);

int
e9ui_ctor(const char* configPath, int cliOverrideWindowSize, int cliWinW, int cliWinH, int startHidden);

void
e9ui_shutdown(void);

void
e9ui_setDisableVariable(e9ui_component_t *comp, const int *stateFlag, int disableWhenTrue);

void
e9ui_setDisabled(e9ui_component_t *comp, int disabled);

void
e9ui_setHiddenVariable(e9ui_component_t *comp, const int *var, int hideWhenTrue);

void
e9ui_setHidden(e9ui_component_t *comp, int visible);

void
e9ui_setAutoHide(e9ui_component_t *comp, int enable, int margin_px);

void
e9ui_setAutoHideClip(e9ui_component_t *comp, const e9ui_rect_t *rect);

void
e9ui_setFocusTarget(e9ui_component_t *comp, e9ui_component_t *target);

void
e9ui_child_add(e9ui_component_t *comp, e9ui_component_t *child, void *meta);

void
e9ui_setTooltip(e9ui_component_t *comp, const char *tooltip);

void
e9ui_drawTooltip(const e9ui_context_t *ctx, const char *text, int baseX, int baseY);

void
e9ui_updateStateTree(e9ui_component_t *root);

e9ui_child_iterator*
e9ui_child_interateNext(e9ui_child_iterator* iter);

e9ui_child_iterator*
e9ui_child_iterateChildren(e9ui_component_t* self, e9ui_child_iterator *iter);

e9ui_child_reverse_iterator*
e9ui_child_iterateChildrenReverse(e9ui_component_t* self, e9ui_child_reverse_iterator *iter);

e9ui_child_reverse_iterator*
e9ui_child_iteratePrev(e9ui_child_reverse_iterator* iter);

void
e9ui_childRemove(e9ui_component_t *self, e9ui_component_t *child,  e9ui_context_t *ctx);

void
e9ui_child_destroyChildren(e9ui_component_t *self, e9ui_context_t *ctx);

void
e9ui_childDestroy(e9ui_component_t *self, e9ui_context_t *ctx);

int    
e9ui_child_enumerateREMOVETHIS(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_component_t **out, int cap);

e9ui_component_child_t *
e9ui_child_findContainer(e9ui_component_t *self, void* meta);

e9ui_component_t *
e9ui_child_find(e9ui_component_t *self, void* meta);

#define e9ui_getHidden(comp) ( comp && (comp->_hidden+0))
