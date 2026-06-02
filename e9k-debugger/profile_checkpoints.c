/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "profile_checkpoints.h"
#include "profile_list.h"
#include "libretro_host.h"
#include "e9ui.h"
#include "e9ui_scroll.h"
#include "hotkeys.h"

typedef struct profile_checkpoints_state {
    e9k_debug_checkpoint_t entries[E9K_CHECKPOINT_COUNT];
    size_t entryCount;
    size_t visibleCount;
    int enabled;
    e9ui_component_t *profileButton;
    e9ui_component_t *listScroll;
} profile_checkpoints_state_t;

static e9ui_component_t *profile_checkpoints_btnProfile = NULL;
static e9ui_component_t *profile_checkpoints_btnReset = NULL;
static e9ui_component_t *profile_checkpoints_btnDump = NULL;
static e9ui_component_t *profile_checkpoints_btnMode = NULL;
static e9ui_component_t *profile_checkpoints_btnOverlay = NULL;
static char profile_checkpoints_tipProfile[96];
static char profile_checkpoints_tipReset[96];
static char profile_checkpoints_tipDump[96];
static int profile_checkpoints_showScanlines = 0;
static int profile_checkpoints_showScanlineOverlay = 0;
static uint64_t profile_checkpoints_overlaySelectedMask = 0;
static const uint8_t profile_checkpoints_overlayColors[][3] = {
    {255, 0, 0},
    {0, 180, 255},
    {255, 230, 0},
    {0, 255, 0},
    {255, 0, 255},
    {255, 128, 0},
    {0, 255, 255},
    {160, 0, 255},
    {255, 255, 255},
    {0, 0, 255},
    {180, 255, 120},
    {255, 80, 160}
};

static e9ui_component_t *profile_checkpoints_list_makeComponent(void);
static void profile_checkpoints_componentDtor(e9ui_component_t *self, e9ui_context_t *ctx);
static void profile_checkpoints_listDtor(e9ui_component_t *self, e9ui_context_t *ctx);
static void profile_checkpoints_refresh(profile_checkpoints_state_t *st);
static int  profile_checkpoints_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW);
static void profile_checkpoints_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds);
static void profile_checkpoints_render(e9ui_component_t *self, e9ui_context_t *ctx);
static void profile_checkpoints_onListClick(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouseEv);
static void profile_checkpoints_onToggle(e9ui_context_t *ctx, void *user);
static void profile_checkpoints_onReset(e9ui_context_t *ctx, void *user);
static void profile_checkpoints_onDump(e9ui_context_t *ctx, void *user);
static void profile_checkpoints_onToggleMode(e9ui_context_t *ctx, void *user);
static void profile_checkpoints_onToggleOverlay(e9ui_context_t *ctx, void *user);
static void profile_checkpoints_contentSize(profile_checkpoints_state_t *st, e9ui_context_t *ctx, int *contentWidth, int *contentHeight);
static const char *profile_checkpoints_entryDisplayName(const e9k_debug_checkpoint_t *entries, size_t entryCount, size_t index);
static int profile_checkpoints_entryIsVisible(const e9k_debug_checkpoint_t *entries, size_t entryCount, size_t index);
static int profile_checkpoints_entryHasDisplayName(const e9k_debug_checkpoint_t *entries, size_t entryCount, size_t index);
static int profile_checkpoints_entryHasData(const e9k_debug_checkpoint_t *entries, size_t entryCount, size_t index);
static int profile_checkpoints_entriesHaveNames(const e9k_debug_checkpoint_t *entries, size_t entryCount);
static int profile_checkpoints_overlayEntryIsSelected(size_t index);
static void profile_checkpoints_drawSwatchCheck(e9ui_context_t *ctx, SDL_Rect swatch);

static const char *
profile_checkpoints_entryDisplayName(const e9k_debug_checkpoint_t *entries, size_t entryCount, size_t index)
{
    if (!entries || index + 1 >= entryCount || index >= E9K_CHECKPOINT_COUNT) {
        return "";
    }
    return entries[index + 1].name;
}

static int
profile_checkpoints_entryIsVisible(const e9k_debug_checkpoint_t *entries, size_t entryCount, size_t index)
{
    if (!entries || index >= entryCount || index + 1 >= entryCount) {
        return 0;
    }
    if (profile_checkpoints_entriesHaveNames(entries, entryCount)) {
        return profile_checkpoints_entryHasDisplayName(entries, entryCount, index);
    }
    return profile_checkpoints_entryHasData(entries, entryCount, index);
}

static int
profile_checkpoints_entryHasDisplayName(const e9k_debug_checkpoint_t *entries, size_t entryCount, size_t index)
{
    const char *displayName = profile_checkpoints_entryDisplayName(entries, entryCount, index);
    return displayName[0] != '\0';
}

static int
profile_checkpoints_entryHasData(const e9k_debug_checkpoint_t *entries, size_t entryCount, size_t index)
{
    if (!entries || index >= entryCount) {
        return 0;
    }
    return entries[index].count != 0 || entries[index].scanlineCount != 0;
}

static int
profile_checkpoints_entriesHaveNames(const e9k_debug_checkpoint_t *entries, size_t entryCount)
{
    if (!entries) {
        return 0;
    }
    for (size_t i = 0; i < entryCount; ++i) {
        if (entries[i].name[0] != '\0') {
            return 1;
        }
    }
    return 0;
}

static int
profile_checkpoints_overlayEntryIsSelected(size_t index)
{
    if (index >= 64) {
        return 0;
    }
    return (profile_checkpoints_overlaySelectedMask & (UINT64_C(1) << index)) ? 1 : 0;
}

static void
profile_checkpoints_drawSwatchCheck(e9ui_context_t *ctx, SDL_Rect swatch)
{
    if (!ctx || !ctx->renderer) {
        return;
    }
    int innerPad = e9ui_scale_px(ctx, 2);
    if (innerPad <= 0) {
        innerPad = 2;
    }
    SDL_Rect outline = {
        swatch.x + innerPad,
        swatch.y + innerPad,
        swatch.w - (innerPad * 2),
        swatch.h - (innerPad * 2)
    };
    if (outline.w > 0 && outline.h > 0) {
        SDL_SetRenderDrawColor(ctx->renderer, 120, 220, 120, 255);
        SDL_RenderDrawRect(ctx->renderer, &outline);
    }
}

static void
profile_checkpoints_refreshModeButton(void)
{
    if (!profile_checkpoints_btnMode) {
        return;
    }
    if (profile_checkpoints_showScanlines) {
        e9ui_button_setLabel(profile_checkpoints_btnMode, "Scanlines");
    } else {
        e9ui_button_setLabel(profile_checkpoints_btnMode, "Cycles");
    }
}

static void
profile_checkpoints_refreshOverlayButton(void)
{
    if (!profile_checkpoints_btnOverlay) {
        return;
    }
    if (profile_checkpoints_showScanlineOverlay) {
        e9ui_button_setLabel(profile_checkpoints_btnOverlay, "Overlay On");
        e9ui_button_setTheme(profile_checkpoints_btnOverlay, e9ui_theme_button_preset_profile_active());
    } else {
        profile_checkpoints_overlaySelectedMask = 0;
        e9ui_button_setLabel(profile_checkpoints_btnOverlay, "Overlay Off");
        e9ui_button_clearTheme(profile_checkpoints_btnOverlay);
    }
}

static void
profile_checkpoints_setTooltipForAction(e9ui_component_t *button,
                                        const char *baseLabel,
                                        const char *actionId,
                                        char *out,
                                        size_t outCap)
{
    if (!button || !baseLabel || !out || outCap == 0) {
        return;
    }
    char binding[96];
    binding[0] = '\0';
    if (hotkeys_formatActionBindingDisplay(actionId, binding, sizeof(binding)) && binding[0]) {
        snprintf(out, outCap, "%s - %s", baseLabel, binding);
    } else {
        snprintf(out, outCap, "%s", baseLabel);
    }
    out[outCap - 1] = '\0';
    e9ui_setTooltip(button, out);
}

void
profile_checkpoints_refreshHotkeyTooltips(void)
{
    profile_checkpoints_setTooltipForAction(profile_checkpoints_btnProfile,
                                            "Profile",
                                            "checkpoint_prev",
                                            profile_checkpoints_tipProfile,
                                            sizeof(profile_checkpoints_tipProfile));
    profile_checkpoints_setTooltipForAction(profile_checkpoints_btnReset,
                                            "Reset",
                                            "checkpoint_reset",
                                            profile_checkpoints_tipReset,
                                            sizeof(profile_checkpoints_tipReset));
    profile_checkpoints_setTooltipForAction(profile_checkpoints_btnDump,
                                            "Dump",
                                            "checkpoint_next",
                                            profile_checkpoints_tipDump,
                                            sizeof(profile_checkpoints_tipDump));
}

static void
profile_checkpoints_componentDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)self;
    (void)ctx;
    profile_checkpoints_btnProfile = NULL;
    profile_checkpoints_btnReset = NULL;
    profile_checkpoints_btnDump = NULL;
    profile_checkpoints_btnMode = NULL;
    profile_checkpoints_btnOverlay = NULL;
}

static void
profile_checkpoints_listDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    if (self->state) {
        alloc_free(self->state);
        self->state = NULL;
    }
}

static void
profile_checkpoints_refresh(profile_checkpoints_state_t *st)
{
    if (!st) {
        return;
    }

    libretro_host_debugGetCheckpointEnabled(&st->enabled);
    if (st->profileButton) {
        if (st->enabled) {
            e9ui_button_setTheme(st->profileButton, e9ui_theme_button_preset_profile_active());
        } else {
            e9ui_button_clearTheme(st->profileButton);
        }
    }
    size_t bytes = libretro_host_debugReadCheckpoints(st->entries, sizeof(st->entries));
    st->entryCount = bytes / sizeof(st->entries[0]);
    if (st->entryCount > E9K_CHECKPOINT_COUNT) {
        st->entryCount = E9K_CHECKPOINT_COUNT;
    }
    st->visibleCount = 0;
    for (size_t i = 0; i < st->entryCount; ++i) {
        if (profile_checkpoints_entryIsVisible(st->entries, st->entryCount, i)) {
            st->visibleCount++;
        }
    }
}

static void
profile_checkpoints_contentSize(profile_checkpoints_state_t *st, e9ui_context_t *ctx, int *contentWidth, int *contentHeight)
{
    if (contentWidth) {
        *contentWidth = 0;
    }
    if (contentHeight) {
        *contentHeight = 0;
    }
    if (!st || !ctx) {
        return;
    }

    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    int lineHeight = font ? TTF_FontHeight(font) : 16;
    if (lineHeight <= 0) {
        lineHeight = 16;
    }
    int padX = e9ui_scale_px(ctx, PROFILE_LIST_PADDING_X);
    int padY = e9ui_scale_px(ctx, PROFILE_LIST_PADDING_Y);
    int swatchSize = lineHeight - 4;
    if (swatchSize < 6) {
        swatchSize = 6;
    }
    int lines = (int)(st->visibleCount > 0 ? st->visibleCount : 1);
    int maxLineWidth = 0;

    if (font) {
        if (st->visibleCount == 0) {
            int tw = 0;
            int th = 0;
            if (TTF_SizeUTF8(font, "No checkpoints", &tw, &th) == 0) {
                maxLineWidth = tw;
            }
        } else {
            for (size_t i = 0; i < st->entryCount; ++i) {
                if (!profile_checkpoints_entryIsVisible(st->entries, st->entryCount, i)) {
                    continue;
                }

                uint64_t avg = st->entries[i].average;
                uint64_t min = st->entries[i].minimum;
                uint64_t max = st->entries[i].maximum;
                uint64_t live = st->entries[i].scanlineLast;
                if (profile_checkpoints_showScanlines) {
                    avg = st->entries[i].scanlineAverage;
                    min = st->entries[i].scanlineMinimum;
                    max = st->entries[i].scanlineMaximum;
                }

                char line[160];
                if (profile_checkpoints_showScanlines) {
                    snprintf(line, sizeof(line),
                             "%02zu %-16.16s live:%llu avg:%llu min:%llu max:%llu",
                             i,
                             profile_checkpoints_entryDisplayName(st->entries, st->entryCount, i),
                             (unsigned long long)live,
                             (unsigned long long)avg,
                             (unsigned long long)min,
                             (unsigned long long)max);
                } else {
                    snprintf(line, sizeof(line),
                             "%02zu %-16.16s avg:%llu min:%llu max:%llu",
                             i,
                             profile_checkpoints_entryDisplayName(st->entries, st->entryCount, i),
                             (unsigned long long)avg,
                             (unsigned long long)min,
                             (unsigned long long)max);
                }

                int tw = 0;
                int th = 0;
                if (TTF_SizeUTF8(font, line, &tw, &th) == 0 && tw > maxLineWidth) {
                    maxLineWidth = tw;
                }
            }
        }
    }

    if (contentWidth) {
        *contentWidth = padX * 2 + swatchSize + 6 + maxLineWidth;
    }
    if (contentHeight) {
        *contentHeight = padY * 2 + lines * lineHeight;
    }
}

static int
profile_checkpoints_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    if (!self || !ctx || !self->state) {
        return 0;
    }

    profile_checkpoints_state_t *st = (profile_checkpoints_state_t*)self->state;
    profile_checkpoints_refresh(st);

    int contentWidth = 0;
    int contentHeight = 0;
    profile_checkpoints_contentSize(st, ctx, &contentWidth, &contentHeight);
    if (st->listScroll) {
        e9ui_scroll_setContentWidthPx(st->listScroll, contentWidth);
        e9ui_scroll_setContentHeightPx(st->listScroll, contentHeight);
    }
    return contentHeight;
}

static void
profile_checkpoints_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static void
profile_checkpoints_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer || !self->state) {
        return;
    }

    profile_checkpoints_state_t *st = (profile_checkpoints_state_t*)self->state;
    profile_checkpoints_refresh(st);
    int contentWidth = 0;
    int contentHeight = 0;
    profile_checkpoints_contentSize(st, ctx, &contentWidth, &contentHeight);
    if (st->listScroll) {
        e9ui_scroll_setContentWidthPx(st->listScroll, contentWidth);
        e9ui_scroll_setContentHeightPx(st->listScroll, contentHeight);
    }

    SDL_Rect bg = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_SetRenderDrawColor(ctx->renderer, 16, 16, 20, 255);
    SDL_RenderFillRect(ctx->renderer, &bg);

    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    if (!font) {
        return;
    }

    int padX = e9ui_scale_px(ctx, PROFILE_LIST_PADDING_X);
    int padY = e9ui_scale_px(ctx, PROFILE_LIST_PADDING_Y);
    int lineHeight = TTF_FontHeight(font);
    if (lineHeight <= 0) {
        lineHeight = 16;
    }

    SDL_Color textColor = { 200, 200, 200, 255 };
    int lineIndex = 0;

    if (st->visibleCount == 0) {
        const char *empty = "No checkpoints";
        int tw = 0;
        int th = 0;
        SDL_Texture *tex = e9ui_text_cache_getUTF8(ctx->renderer, font, empty, textColor, &tw, &th);
        if (tex) {
            SDL_Rect rect = { self->bounds.x + padX, self->bounds.y + padY, tw, th };
            SDL_RenderCopy(ctx->renderer, tex, NULL, &rect);
        }
        return;
    }

    for (size_t i = 0; i < st->entryCount; ++i) {
        if (!profile_checkpoints_entryIsVisible(st->entries, st->entryCount, i)) {
            continue;
        }

        int y = self->bounds.y + padY + lineIndex * lineHeight;
        uint64_t avg = st->entries[i].average;
        uint64_t min = st->entries[i].minimum;
        uint64_t max = st->entries[i].maximum;
        uint64_t live = st->entries[i].scanlineLast;
        if (profile_checkpoints_showScanlines) {
            avg = st->entries[i].scanlineAverage;
            min = st->entries[i].scanlineMinimum;
            max = st->entries[i].scanlineMaximum;
        }

        char line[160];
        if (profile_checkpoints_showScanlines) {
            snprintf(line, sizeof(line),
                     "%02zu %-16.16s live:%llu avg:%llu min:%llu max:%llu",
                     i,
                     profile_checkpoints_entryDisplayName(st->entries, st->entryCount, i),
                     (unsigned long long)live,
                     (unsigned long long)avg,
                     (unsigned long long)min,
                     (unsigned long long)max);
        } else {
            snprintf(line, sizeof(line),
                     "%02zu %-16.16s avg:%llu min:%llu max:%llu",
                     i,
                     profile_checkpoints_entryDisplayName(st->entries, st->entryCount, i),
                     (unsigned long long)avg,
                     (unsigned long long)min,
                     (unsigned long long)max);
        }

        int tw = 0;
        int th = 0;
        SDL_Texture *tex = e9ui_text_cache_getUTF8(ctx->renderer, font, line, textColor, &tw, &th);
        if (tex) {
            int textX = self->bounds.x + padX;
            int swatchSize = lineHeight - 4;
            if (swatchSize < 6) {
                swatchSize = 6;
            }
            int swatchY = y + (lineHeight - swatchSize) / 2;
            const uint8_t *color = profile_checkpoints_overlayColors[i % (sizeof(profile_checkpoints_overlayColors) / sizeof(profile_checkpoints_overlayColors[0]))];
            SDL_SetRenderDrawColor(ctx->renderer, color[0], color[1], color[2], 220);
            SDL_Rect swatch = { textX, swatchY, swatchSize, swatchSize };
            SDL_RenderFillRect(ctx->renderer, &swatch);
            SDL_SetRenderDrawColor(ctx->renderer, 8, 8, 8, 255);
            SDL_RenderDrawRect(ctx->renderer, &swatch);
            if (profile_checkpoints_showScanlineOverlay && profile_checkpoints_overlayEntryIsSelected(i)) {
                profile_checkpoints_drawSwatchCheck(ctx, swatch);
            }
            textX += swatchSize + 6;
            SDL_Rect rect = { textX, y, tw, th };
            SDL_RenderCopy(ctx->renderer, tex, NULL, &rect);
        }

        lineIndex++;
    }
}

e9ui_component_t *
profile_checkpoints_makeComponent(void)
{
    e9ui_component_t *list = profile_checkpoints_list_makeComponent();
    if (!list) {
        return NULL;
    }

    profile_checkpoints_state_t *st = (profile_checkpoints_state_t*)list->state;

    e9ui_component_t *toolbar = e9ui_flow_make();
    e9ui_flow_setWrap(toolbar, 0);
    e9ui_flow_setSpacing(toolbar, 6);
    e9ui_flow_setPadding(toolbar, 6);

    e9ui_component_t *btn_profile = e9ui_button_make("Profile", profile_checkpoints_onToggle, st);
    e9ui_button_setMini(btn_profile, 1);
    profile_checkpoints_btnProfile = btn_profile;
    st->profileButton = btn_profile;
    if (st->enabled) {
        e9ui_button_setTheme(btn_profile, e9ui_theme_button_preset_profile_active());
    } else {
        e9ui_button_clearTheme(btn_profile);
    }
    e9ui_flow_add(toolbar, btn_profile);

    e9ui_component_t *btn_reset = e9ui_button_make("Reset", profile_checkpoints_onReset, st);
    e9ui_button_setMini(btn_reset, 1);
    e9ui_button_setTheme(btn_reset, e9ui_theme_button_preset_red());
    profile_checkpoints_btnReset = btn_reset;
    e9ui_flow_add(toolbar, btn_reset);

    e9ui_component_t *btn_dump = e9ui_button_make("Dump", profile_checkpoints_onDump, st);
    e9ui_button_setMini(btn_dump, 1);
    profile_checkpoints_btnDump = btn_dump;
    e9ui_flow_add(toolbar, btn_dump);

    e9ui_component_t *btn_mode = e9ui_button_make("Cycles", profile_checkpoints_onToggleMode, st);
    e9ui_button_setMini(btn_mode, 1);
    profile_checkpoints_btnMode = btn_mode;
    profile_checkpoints_refreshModeButton();
    e9ui_flow_add(toolbar, btn_mode);

    e9ui_component_t *btn_overlay = e9ui_button_make("Overlay Off", profile_checkpoints_onToggleOverlay, st);
    e9ui_button_setMini(btn_overlay, 1);
    profile_checkpoints_btnOverlay = btn_overlay;
    profile_checkpoints_refreshOverlayButton();
    e9ui_flow_add(toolbar, btn_overlay);

    profile_checkpoints_refreshHotkeyTooltips();

    e9ui_component_t *listScroll = e9ui_scroll_make(list);
    if (!listScroll) {
        profile_checkpoints_listDtor(list, NULL);
        alloc_free(list);
        return NULL;
    }
    st->listScroll = listScroll;

    e9ui_component_t *stack = e9ui_stack_makeVertical();
    e9ui_stack_addFixed(stack, toolbar);
    e9ui_stack_addFlex(stack, listScroll);
    stack->name = "profile_checkpoints";
    stack->dtor = profile_checkpoints_componentDtor;

    return stack;
}

static e9ui_component_t *
profile_checkpoints_list_makeComponent(void)
{
    e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    if (!comp) {
        return NULL;
    }

    profile_checkpoints_state_t *st = (profile_checkpoints_state_t*)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(comp);
        return NULL;
    }

    comp->name = "profile_checkpoints";
    comp->state = st;
    comp->preferredHeight = profile_checkpoints_preferredHeight;
    comp->layout = profile_checkpoints_layout;
    comp->render = profile_checkpoints_render;
    comp->onClick = profile_checkpoints_onListClick;
    comp->dtor = profile_checkpoints_listDtor;

    return comp;
}

static void
profile_checkpoints_onListClick(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouseEv)
{
    if (!self || !ctx || !self->state || !mouseEv || mouseEv->button != E9UI_MOUSE_BUTTON_LEFT) {
        return;
    }
    profile_checkpoints_state_t *st = (profile_checkpoints_state_t*)self->state;
    profile_checkpoints_refresh(st);
    if (st->visibleCount == 0) {
        return;
    }

    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    int lineHeight = font ? TTF_FontHeight(font) : 16;
    if (lineHeight <= 0) {
        lineHeight = 16;
    }
    int padX = e9ui_scale_px(ctx, PROFILE_LIST_PADDING_X);
    int padY = e9ui_scale_px(ctx, PROFILE_LIST_PADDING_Y);
    int swatchSize = lineHeight - 4;
    if (swatchSize < 6) {
        swatchSize = 6;
    }
    int swatchX = self->bounds.x + padX;
    int lineIndex = 0;

    for (size_t i = 0; i < st->entryCount && i < 64; ++i) {
        if (!profile_checkpoints_entryIsVisible(st->entries, st->entryCount, i)) {
            continue;
        }
        int y = self->bounds.y + padY + lineIndex * lineHeight;
        int swatchY = y + (lineHeight - swatchSize) / 2;
        if (mouseEv->x >= swatchX && mouseEv->x < swatchX + swatchSize &&
            mouseEv->y >= swatchY && mouseEv->y < swatchY + swatchSize) {
            if (!profile_checkpoints_showScanlineOverlay) {
                profile_checkpoints_showScanlineOverlay = 1;
                profile_checkpoints_refreshOverlayButton();
            }
            uint64_t bit = UINT64_C(1) << i;
            if (profile_checkpoints_overlaySelectedMask & bit) {
                profile_checkpoints_overlaySelectedMask &= ~bit;
            } else {
                profile_checkpoints_overlaySelectedMask |= bit;
            }
            return;
        }
        lineIndex++;
    }
}

static void
profile_checkpoints_onToggle(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    profile_checkpoints_toggle();
}

static void
profile_checkpoints_onReset(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    profile_checkpoints_reset();
}

static void
profile_checkpoints_onDump(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    profile_checkpoints_dump();
}

static void
profile_checkpoints_onToggleMode(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    profile_checkpoints_showScanlines = profile_checkpoints_showScanlines ? 0 : 1;
    profile_checkpoints_refreshModeButton();
}

static void
profile_checkpoints_onToggleOverlay(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    profile_checkpoints_showScanlineOverlay = profile_checkpoints_showScanlineOverlay ? 0 : 1;
    if (!profile_checkpoints_showScanlineOverlay) {
        profile_checkpoints_overlaySelectedMask = 0;
    }
    profile_checkpoints_refreshOverlayButton();
}

void
profile_checkpoints_toggle(void)
{
    int enabled = 0;
    libretro_host_debugGetCheckpointEnabled(&enabled);
    libretro_host_debugSetCheckpointEnabled(enabled ? 0 : 1);
}

void
profile_checkpoints_reset(void)
{
    libretro_host_debugResetCheckpoints();
}

void
profile_checkpoints_dump(void)
{
    e9k_debug_checkpoint_t entries[E9K_CHECKPOINT_COUNT];
    size_t entryCount = 0;
    size_t bytes = libretro_host_debugReadCheckpoints(entries, sizeof(entries));
    entryCount = bytes / sizeof(entries[0]);
    if (entryCount > E9K_CHECKPOINT_COUNT) {
        entryCount = E9K_CHECKPOINT_COUNT;
    }
    printf("Profiler checkpoints\n");
    printf("%-3s %-16s | %-32s | %-32s\n",
           "",
           "",
           "Cycles",
           "Scan Lines");
    printf("--- ---------------- | ---------- ---------- ---------- | ---------- ---------- ----------\n");
    printf("%-3s %-16s | %10s %10s %10s | %10s %10s %10s\n",
           "id",
           "desc",
           "avg",
           "min",
           "max",
           "avg",
           "min",
           "max");
    printf("--- ---------------- | ---------- ---------- ---------- | ---------- ---------- ----------\n");
    for (size_t i = 0; i < entryCount; ++i) {
        if (!profile_checkpoints_entryIsVisible(entries, entryCount, i)) {
            continue;
        }
        printf("%02zu  %-16.16s | %10llu %10llu %10llu | %10llu %10llu %10llu\n",
               i,
               profile_checkpoints_entryDisplayName(entries, entryCount, i),
               (unsigned long long)entries[i].average,
               (unsigned long long)entries[i].minimum,
               (unsigned long long)entries[i].maximum,
               (unsigned long long)entries[i].scanlineAverage,
               (unsigned long long)entries[i].scanlineMinimum,
               (unsigned long long)entries[i].scanlineMaximum);
    }
    fflush(stdout);
}

void
profile_checkpoints_renderScanlineOverlay(e9ui_context_t *ctx, const SDL_Rect *dst, uint64_t scanlineCount)
{
    const int totalRasterLines = 256;
    const int preActiveLines = 16;
    typedef struct profile_checkpoint_overlay_boundary {
        size_t checkpointIndex;
        uint64_t scanline;
    } profile_checkpoint_overlay_boundary_t;

    if (!profile_checkpoints_showScanlineOverlay || !ctx || !ctx->renderer || !dst) {
        return;
    }
    if (dst->w <= 0 || dst->h <= 0) {
        return;
    }
    if (scanlineCount == 0) {
        return;
    }

    e9k_debug_checkpoint_t entries[E9K_CHECKPOINT_COUNT];
    size_t entryCount = 0;
    size_t bytes = libretro_host_debugReadCheckpoints(entries, sizeof(entries));
    entryCount = bytes / sizeof(entries[0]);
    if (entryCount > E9K_CHECKPOINT_COUNT) {
        entryCount = E9K_CHECKPOINT_COUNT;
    }
    if (entryCount == 0) {
        return;
    }

    profile_checkpoint_overlay_boundary_t boundaries[E9K_CHECKPOINT_COUNT];
    size_t boundaryCount = 0;
    for (size_t i = 0; i < entryCount; ++i) {
        const e9k_debug_checkpoint_t *entry = &entries[i];
        if (entry->scanlineCount == 0) {
            continue;
        }
        boundaries[boundaryCount].checkpointIndex = i;
        boundaries[boundaryCount].scanline = entry->scanlineLast % scanlineCount;
        boundaryCount++;
    }
    if (boundaryCount < 2) {
        return;
    }

    int visibleStartLine = 0;
    int visibleHeightLines = (int)scanlineCount;
    if (scanlineCount == 264u) {
        int cropT = 8;
        int cropB = 8;
        e9k_debug_sprite_state_t spriteState;
        if (libretro_host_neogeo_getSpriteState(&spriteState)) {
            cropT = spriteState.crop_t;
            cropB = spriteState.crop_b;
        }
        if (cropT < 0) {
            cropT = 0;
        }
        if (cropB < 0) {
            cropB = 0;
        }
        if (cropT > totalRasterLines) {
            cropT = totalRasterLines;
        }
        if (cropB > totalRasterLines) {
            cropB = totalRasterLines;
        }

        visibleStartLine = preActiveLines + cropT;
        visibleHeightLines = totalRasterLines - (preActiveLines + cropT + cropB);
        if (visibleHeightLines <= 0) {
            visibleStartLine = 24;
            visibleHeightLines = 224;
        }
    }

    SDL_BlendMode prevBlend = SDL_BLENDMODE_BLEND;
    SDL_GetRenderDrawBlendMode(ctx->renderer, &prevBlend);
    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);

    int clipEnabled = SDL_RenderIsClipEnabled(ctx->renderer);
    SDL_Rect prevClip = {0, 0, 0, 0};
    SDL_RenderGetClipRect(ctx->renderer, &prevClip);
    SDL_RenderSetClipRect(ctx->renderer, NULL);

    for (size_t i = 0; i < boundaryCount; ++i) {
        size_t next = (i + 1) % boundaryCount;
        uint64_t startLine = boundaries[i].scanline;
        uint64_t endLine = boundaries[next].scanline;
        if (endLine == startLine) {
            continue;
        }

        size_t checkpointIndex = boundaries[i].checkpointIndex;
        if (!profile_checkpoints_entryIsVisible(entries, entryCount, checkpointIndex)) {
            continue;
        }
        if (profile_checkpoints_overlaySelectedMask != 0 && !profile_checkpoints_overlayEntryIsSelected(checkpointIndex)) {
            continue;
        }
        const uint8_t *color = profile_checkpoints_overlayColors[checkpointIndex % (sizeof(profile_checkpoints_overlayColors) / sizeof(profile_checkpoints_overlayColors[0]))];
        SDL_SetRenderDrawColor(ctx->renderer, color[0], color[1], color[2], 128);

        if (endLine > startLine) {
            int y0 = dst->y + (int)((((int64_t)startLine - (int64_t)visibleStartLine) * (int64_t)dst->h) / (int64_t)visibleHeightLines);
            int y1 = dst->y + (int)((((int64_t)endLine - (int64_t)visibleStartLine) * (int64_t)dst->h) / (int64_t)visibleHeightLines);
            if (y1 <= y0) {
                y1 = y0 + 1;
            }
            SDL_Rect band = { dst->x, y0, dst->w, y1 - y0 };
            SDL_RenderFillRect(ctx->renderer, &band);
        } else {
            int yBottom0 = dst->y + (int)((((int64_t)startLine - (int64_t)visibleStartLine) * (int64_t)dst->h) / (int64_t)visibleHeightLines);
            int yBottom1 = dst->y + (int)((((int64_t)scanlineCount - (int64_t)visibleStartLine) * (int64_t)dst->h) / (int64_t)visibleHeightLines);
            if (yBottom1 > yBottom0) {
                SDL_Rect bandBottom = { dst->x, yBottom0, dst->w, yBottom1 - yBottom0 };
                SDL_RenderFillRect(ctx->renderer, &bandBottom);
            }

            int yTop0 = dst->y + (int)((((int64_t)0 - (int64_t)visibleStartLine) * (int64_t)dst->h) / (int64_t)visibleHeightLines);
            int yTop1 = dst->y + (int)((((int64_t)endLine - (int64_t)visibleStartLine) * (int64_t)dst->h) / (int64_t)visibleHeightLines);
            if (yTop1 > yTop0) {
                SDL_Rect bandTop = { dst->x, yTop0, dst->w, yTop1 - yTop0 };
                SDL_RenderFillRect(ctx->renderer, &bandTop);
            }
        }
    }

    for (size_t i = 0; i < boundaryCount; ++i) {
        uint64_t borderLine = boundaries[i].scanline;
        int y = dst->y + (int)((((int64_t)borderLine - (int64_t)visibleStartLine) * (int64_t)dst->h) / (int64_t)visibleHeightLines);
        size_t checkpointIndex = boundaries[i].checkpointIndex;
        if (!profile_checkpoints_entryIsVisible(entries, entryCount, checkpointIndex)) {
            continue;
        }
        if (profile_checkpoints_overlaySelectedMask != 0 && !profile_checkpoints_overlayEntryIsSelected(checkpointIndex)) {
            continue;
        }
        const uint8_t *color = profile_checkpoints_overlayColors[checkpointIndex % (sizeof(profile_checkpoints_overlayColors) / sizeof(profile_checkpoints_overlayColors[0]))];
        SDL_SetRenderDrawColor(ctx->renderer, color[0], color[1], color[2], 240);
        SDL_RenderDrawLine(ctx->renderer, dst->x, y, dst->x + dst->w - 1, y);
    }

    if (clipEnabled) {
        SDL_RenderSetClipRect(ctx->renderer, &prevClip);
    } else {
        SDL_RenderSetClipRect(ctx->renderer, NULL);
    }
    SDL_SetRenderDrawBlendMode(ctx->renderer, prevBlend);
}
