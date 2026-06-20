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
#include <limits.h>
#include <string.h>

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

typedef enum profile_checkpoints_table_mode {
    profile_checkpoints_table_mode_cycles = 0,
    profile_checkpoints_table_mode_scanline,
    profile_checkpoints_table_mode_scanlines
} profile_checkpoints_table_mode_t;

typedef struct profile_checkpoints_table_values {
    uint64_t live;
    uint64_t avg;
    uint64_t min;
    uint64_t max;
} profile_checkpoints_table_values_t;

static e9ui_component_t *profile_checkpoints_btnProfile = NULL;
static e9ui_component_t *profile_checkpoints_btnReset = NULL;
static e9ui_component_t *profile_checkpoints_btnDump = NULL;
static e9ui_component_t *profile_checkpoints_btnMode = NULL;
static e9ui_component_t *profile_checkpoints_btnOverlay = NULL;
static e9ui_component_t *profile_checkpoints_scaleInput = NULL;
static char profile_checkpoints_tipProfile[96];
static char profile_checkpoints_tipReset[96];
static char profile_checkpoints_tipDump[96];
static profile_checkpoints_table_mode_t profile_checkpoints_tableMode = profile_checkpoints_table_mode_cycles;
static int profile_checkpoints_showScanlineOverlay = 0;
static int profile_checkpoints_scanlineScale = 1;
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
static void
profile_checkpoints_onScaleChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *text, void *user);
static void
profile_checkpoints_contentSize(profile_checkpoints_state_t *st, e9ui_context_t *ctx, int *contentWidth, int *contentHeight);
static const char *
profile_checkpoints_entryDisplayName(const e9k_debug_checkpoint_t *entries, size_t entryCount, size_t index, size_t displayIndex, char *fallback, size_t fallbackCap);
static int
profile_checkpoints_entryIsVisible(const e9k_debug_checkpoint_t *entries, size_t entryCount, size_t index);
static int
profile_checkpoints_entryHasData(const e9k_debug_checkpoint_t *entries, size_t entryCount, size_t index);
static int
profile_checkpoints_overlayEntryIsSelected(size_t index);
static const char *
profile_checkpoints_tableModeLabel(void);
static const char *
profile_checkpoints_tableModeConfigValue(void);
static profile_checkpoints_table_values_t
profile_checkpoints_entryTableValues(const e9k_debug_checkpoint_t *entry);
static void
profile_checkpoints_drawSwatchCheck(e9ui_context_t *ctx, SDL_Rect swatch);
static void
profile_checkpoints_buildOverlayLines(const e9k_debug_checkpoint_t *entries,
                                      size_t entryCount,
                                      uint64_t scanlineCount,
                                      int scanlineScale,
                                      int64_t *checkpointLines);
static int
profile_checkpoints_scanlineToOverlayY(const SDL_Rect *dst,
                                       int64_t line,
                                       uint64_t videoStartScanline,
                                       uint64_t videoScanlineCount);
static void
profile_checkpoints_drawScanlineSegment(e9ui_context_t *ctx,
                                        const SDL_Rect *dst,
                                        int64_t startLine,
                                        int64_t endLine,
                                        uint64_t videoStartScanline,
                                        uint64_t videoScanlineCount);

static const char *
profile_checkpoints_entryDisplayName(const e9k_debug_checkpoint_t *entries, size_t entryCount, size_t index, size_t displayIndex, char *fallback, size_t fallbackCap)
{
    if (!fallback || fallbackCap == 0) {
        return "";
    }
    if (entries && index + 1 < entryCount && index < E9K_CHECKPOINT_COUNT && entries[index + 1].name[0] != '\0') {
        return entries[index + 1].name;
    }
    snprintf(fallback, fallbackCap, "checkpoint %zu", displayIndex);
    fallback[fallbackCap - 1] = '\0';
    return fallback;
}

static int
profile_checkpoints_entryIsVisible(const e9k_debug_checkpoint_t *entries, size_t entryCount, size_t index)
{
    if (!entries || index >= entryCount || index + 1 >= entryCount) {
        return 0;
    }
    return profile_checkpoints_entryHasData(entries, entryCount, index);
}

static int
profile_checkpoints_entryHasData(const e9k_debug_checkpoint_t *entries, size_t entryCount, size_t index)
{
    if (!entries || index >= entryCount) {
        return 0;
    }
    return entries[index].count != 0 || entries[index].scanlineSpanCount != 0;
}

static int
profile_checkpoints_overlayEntryIsSelected(size_t index)
{
    if (index >= 64) {
        return 0;
    }
    return (profile_checkpoints_overlaySelectedMask & (UINT64_C(1) << index)) ? 1 : 0;
}

static const char *
profile_checkpoints_tableModeLabel(void)
{
    switch (profile_checkpoints_tableMode) {
        case profile_checkpoints_table_mode_scanline:
            return "Scanline";
        case profile_checkpoints_table_mode_scanlines:
            return "Scanlines";
        case profile_checkpoints_table_mode_cycles:
        default:
            return "Cycles";
    }
}

static const char *
profile_checkpoints_tableModeConfigValue(void)
{
    switch (profile_checkpoints_tableMode) {
        case profile_checkpoints_table_mode_scanline:
            return "scanline";
        case profile_checkpoints_table_mode_scanlines:
            return "scanlines";
        case profile_checkpoints_table_mode_cycles:
        default:
            return "cycles";
    }
}

static profile_checkpoints_table_values_t
profile_checkpoints_entryTableValues(const e9k_debug_checkpoint_t *entry)
{
    switch (profile_checkpoints_tableMode) {
        case profile_checkpoints_table_mode_scanline:
            return (profile_checkpoints_table_values_t) {
                entry->scanlineLast,
                entry->scanlineAverage,
                entry->scanlineMinimum,
                entry->scanlineMaximum
            };
        case profile_checkpoints_table_mode_scanlines:
            return (profile_checkpoints_table_values_t) {
                entry->scanlineSpanLast,
                entry->scanlineSpanAverage,
                entry->scanlineSpanMinimum,
                entry->scanlineSpanMaximum
            };
        case profile_checkpoints_table_mode_cycles:
        default:
            return (profile_checkpoints_table_values_t) {
                entry->current,
                entry->average,
                entry->minimum,
                entry->maximum
            };
    }
}

static void
profile_checkpoints_drawSwatchCheck(e9ui_context_t *ctx, SDL_Rect swatch)
{
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
profile_checkpoints_buildOverlayLines(const e9k_debug_checkpoint_t *entries,
                                      size_t entryCount,
                                      uint64_t scanlineCount,
                                      int scanlineScale,
                                      int64_t *checkpointLines)
{
    uint64_t backwardStepCount = 0;
    uint64_t previousCheckpoint = 0;
    int hasPreviousCheckpoint = 0;
    int initialWrapPending = 0;
    size_t firstCheckpointIndex = 0;
    for (size_t i = 0; i < entryCount; ++i) {
        checkpointLines[i] = 0;
        if (entries[i].scanlineCount == 0) {
            continue;
        }
        uint64_t checkpoint = entries[i].scanlineLast % scanlineCount;
        if (!hasPreviousCheckpoint) {
            initialWrapPending = scanlineScale > 1 && checkpoint >= ((scanlineCount * 3) / 4);
            firstCheckpointIndex = i;
        } else if (checkpoint < previousCheckpoint) {
            if (initialWrapPending) {
                int64_t initialWrapOffset = (int64_t)scanlineCount * (int64_t)(scanlineScale - 1);
                for (size_t j = firstCheckpointIndex; j < i; ++j) {
                    if (entries[j].scanlineCount != 0) {
                        checkpointLines[j] += initialWrapOffset;
                    }
                }
                initialWrapPending = 0;
            } else if (scanlineScale > 1) {
                backwardStepCount++;
            }
        }
        checkpointLines[i] = ((int64_t)scanlineCount * (int64_t)backwardStepCount) + (int64_t)checkpoint;
        previousCheckpoint = checkpoint;
        hasPreviousCheckpoint = 1;
    }
}

static int
profile_checkpoints_scanlineToOverlayY(const SDL_Rect *dst,
                                       int64_t line,
                                       uint64_t videoStartScanline,
                                       uint64_t videoScanlineCount)
{
    if (!dst || videoScanlineCount == 0) {
        return 0;
    }
    int64_t numerator = (line - (int64_t)videoStartScanline) * (int64_t)dst->h;
    return dst->y + (int)(numerator / (int64_t)videoScanlineCount);
}

static void
profile_checkpoints_drawScanlineSegment(e9ui_context_t *ctx,
                                        const SDL_Rect *dst,
                                        int64_t startLine,
                                        int64_t endLine,
                                        uint64_t videoStartScanline,
                                        uint64_t videoScanlineCount)
{
    if (!dst || endLine <= startLine || videoScanlineCount == 0) {
        return;
    }

    int y0 = profile_checkpoints_scanlineToOverlayY(dst,
                                                    startLine,
                                                    videoStartScanline,
                                                    videoScanlineCount);
    int y1 = profile_checkpoints_scanlineToOverlayY(dst,
                                                    endLine,
                                                    videoStartScanline,
                                                    videoScanlineCount);
    if (y1 <= y0) {
        y1 = y0 + 1;
    }
    SDL_Rect band = { dst->x, y0, dst->w, y1 - y0 };
    SDL_RenderFillRect(ctx->renderer, &band);
}

static void
profile_checkpoints_refreshModeButton(void)
{
    if (!profile_checkpoints_btnMode) {
        return;
    }
    e9ui_button_setLabel(profile_checkpoints_btnMode, profile_checkpoints_tableModeLabel());
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
    profile_checkpoints_scaleInput = NULL;
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
    if (!st) {
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
            size_t displayIndex = 0;
            for (size_t i = 0; i < st->entryCount; ++i) {
                if (!profile_checkpoints_entryIsVisible(st->entries, st->entryCount, i)) {
                    continue;
                }

                profile_checkpoints_table_values_t values = profile_checkpoints_entryTableValues(&st->entries[i]);

                char line[160];
                char fallbackName[32];
                const char *displayName = profile_checkpoints_entryDisplayName(st->entries,
                                                                                st->entryCount,
                                                                                i,
                                                                                displayIndex,
                                                                                fallbackName,
                                                                                sizeof(fallbackName));
                if (profile_checkpoints_tableMode != profile_checkpoints_table_mode_cycles) {
                    snprintf(line, sizeof(line),
                             "%02zu %-16.16s live:%llu avg:%llu min:%llu max:%llu",
                             displayIndex,
                             displayName,
                             (unsigned long long)values.live,
                             (unsigned long long)values.avg,
                             (unsigned long long)values.min,
                             (unsigned long long)values.max);
                } else {
                    snprintf(line, sizeof(line),
                             "%02zu %-16.16s avg:%llu min:%llu max:%llu",
                             displayIndex,
                             displayName,
                             (unsigned long long)values.avg,
                             (unsigned long long)values.min,
                             (unsigned long long)values.max);
                }

                int tw = 0;
                int th = 0;
                if (TTF_SizeUTF8(font, line, &tw, &th) == 0 && tw > maxLineWidth) {
                    maxLineWidth = tw;
                }
                displayIndex++;
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
    if (!self || !self->state) {
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
    if (!self || !self->state) {
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
        profile_checkpoints_table_values_t values = profile_checkpoints_entryTableValues(&st->entries[i]);

        char line[160];
        char fallbackName[32];
        const char *displayName = profile_checkpoints_entryDisplayName(st->entries,
                                                                        st->entryCount,
                                                                        i,
                                                                        (size_t)lineIndex,
                                                                        fallbackName,
                                                                        sizeof(fallbackName));
        if (profile_checkpoints_tableMode != profile_checkpoints_table_mode_cycles) {
            snprintf(line, sizeof(line),
                     "%02zu %-16.16s live:%llu avg:%llu min:%llu max:%llu",
                     (size_t)lineIndex,
                     displayName,
                     (unsigned long long)values.live,
                     (unsigned long long)values.avg,
                     (unsigned long long)values.min,
                     (unsigned long long)values.max);
        } else {
            snprintf(line, sizeof(line),
                     "%02zu %-16.16s avg:%llu min:%llu max:%llu",
                     (size_t)lineIndex,
                     displayName,
                     (unsigned long long)values.avg,
                     (unsigned long long)values.min,
                     (unsigned long long)values.max);
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

    e9ui_component_t *scaleInput = e9ui_labeled_textbox_make("Scale", 58, 86, profile_checkpoints_onScaleChanged, st);
    if (scaleInput) {
        char scaleText[32];
        snprintf(scaleText, sizeof(scaleText), "%d", profile_checkpoints_scanlineScale);
        e9ui_labeled_textbox_setText(scaleInput, scaleText);
        e9ui_component_t *scaleTextbox = e9ui_labeled_textbox_getTextbox(scaleInput);
        if (scaleTextbox) {
            e9ui_textbox_setNumericOnly(scaleTextbox, 1);
        }
        e9ui_setTooltip(scaleInput, "Scale scanline overlay height");
        profile_checkpoints_scaleInput = scaleInput;
        e9ui_flow_add(toolbar, scaleInput);
    }

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
    if (!self || !self->state || !mouseEv || mouseEv->button != E9UI_MOUSE_BUTTON_LEFT) {
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
    switch (profile_checkpoints_tableMode) {
        case profile_checkpoints_table_mode_cycles:
            profile_checkpoints_tableMode = profile_checkpoints_table_mode_scanline;
            break;
        case profile_checkpoints_table_mode_scanline:
            profile_checkpoints_tableMode = profile_checkpoints_table_mode_scanlines;
            break;
        case profile_checkpoints_table_mode_scanlines:
        default:
            profile_checkpoints_tableMode = profile_checkpoints_table_mode_cycles;
            break;
    }
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

static void
profile_checkpoints_onScaleChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *text, void *user)
{
    (void)ctx;
    (void)user;
    int scale = 1;
    int normalizeText = 0;
    if (text && text[0] != '\0') {
        char *end = NULL;
        long value = strtol(text, &end, 10);
        if (end != text && value > 0) {
            scale = value > INT_MAX ? INT_MAX : (int)value;
        } else {
            normalizeText = 1;
        }
    }
    profile_checkpoints_scanlineScale = scale;
    if (normalizeText && comp) {
        e9ui_labeled_textbox_setText(comp, "1");
    }
}

void
profile_checkpoints_persistConfig(FILE *file)
{
    if (!file) {
        return;
    }
    fprintf(file, "comp.profile_checkpoints.mode=%s\n", profile_checkpoints_tableModeConfigValue());
    fprintf(file, "comp.profile_checkpoints.scale=%d\n", profile_checkpoints_scanlineScale);
}

int
profile_checkpoints_loadConfigProperty(const char *prop, const char *value)
{
    if (!prop || !value) {
        return 0;
    }
    if (strcmp(prop, "mode") == 0) {
        if (strcmp(value, "scanline") == 0 || strcmp(value, "1") == 0) {
            profile_checkpoints_tableMode = profile_checkpoints_table_mode_scanline;
        } else if (strcmp(value, "scanlines") == 0) {
            profile_checkpoints_tableMode = profile_checkpoints_table_mode_scanlines;
        } else if (strcmp(value, "cycles") == 0 || strcmp(value, "0") == 0) {
            profile_checkpoints_tableMode = profile_checkpoints_table_mode_cycles;
        } else {
            return 0;
        }
        profile_checkpoints_refreshModeButton();
        return 1;
    }
    if (strcmp(prop, "scale") == 0) {
        char *end = NULL;
        long scale = strtol(value, &end, 10);
        if (end == value || scale <= 0 || scale > INT_MAX) {
            return 0;
        }
        profile_checkpoints_scanlineScale = (int)scale;
        if (profile_checkpoints_scaleInput) {
            char scaleText[32];
            snprintf(scaleText, sizeof(scaleText), "%d", profile_checkpoints_scanlineScale);
            e9ui_labeled_textbox_setText(profile_checkpoints_scaleInput, scaleText);
        }
        return 1;
    }
    return 0;
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
    printf("%-3s %-16s | %-32s | %-32s | %-32s\n",
           "",
           "",
           "Cycles",
           "Scanline",
           "Scanlines");
    printf("--- ---------------- | ---------- ---------- ---------- | ---------- ---------- ---------- | ---------- ---------- ----------\n");
    printf("%-3s %-16s | %10s %10s %10s | %10s %10s %10s | %10s %10s %10s\n",
           "id",
           "desc",
           "avg",
           "min",
           "max",
           "avg",
           "min",
           "max",
           "avg",
           "min",
           "max");
    printf("--- ---------------- | ---------- ---------- ---------- | ---------- ---------- ---------- | ---------- ---------- ----------\n");
    size_t displayIndex = 0;
    for (size_t i = 0; i < entryCount; ++i) {
        if (!profile_checkpoints_entryIsVisible(entries, entryCount, i)) {
            continue;
        }
        char fallbackName[32];
        const char *displayName = profile_checkpoints_entryDisplayName(entries,
                                                                       entryCount,
                                                                       i,
                                                                       displayIndex,
                                                                       fallbackName,
                                                                       sizeof(fallbackName));
        printf("%02zu  %-16.16s | %10llu %10llu %10llu | %10llu %10llu %10llu | %10llu %10llu %10llu\n",
               displayIndex,
               displayName,
               (unsigned long long)entries[i].average,
               (unsigned long long)entries[i].minimum,
               (unsigned long long)entries[i].maximum,
               (unsigned long long)entries[i].scanlineAverage,
               (unsigned long long)entries[i].scanlineMinimum,
               (unsigned long long)entries[i].scanlineMaximum,
               (unsigned long long)entries[i].scanlineSpanAverage,
               (unsigned long long)entries[i].scanlineSpanMinimum,
               (unsigned long long)entries[i].scanlineSpanMaximum);
        displayIndex++;
    }
    fflush(stdout);
}

void
profile_checkpoints_renderScanlineOverlay(e9ui_context_t *ctx,
                                          const SDL_Rect *dst,
                                          const SDL_Rect *clipRect,
                                          uint64_t scanlineCount,
                                          uint64_t videoStartScanline,
                                          uint64_t videoScanlineCount)
{
    if (!profile_checkpoints_showScanlineOverlay || !dst) {
        return;
    }
    if (dst->w <= 0 || dst->h <= 0) {
        return;
    }
    if (scanlineCount == 0) {
        return;
    }
    if (videoScanlineCount == 0) {
        return;
    }

    e9k_debug_checkpoint_t entries[E9K_CHECKPOINT_COUNT];
    size_t bytes = libretro_host_debugReadCheckpoints(entries, sizeof(entries));
    size_t entryCount = bytes / sizeof(entries[0]);
    if (entryCount > E9K_CHECKPOINT_COUNT) {
        entryCount = E9K_CHECKPOINT_COUNT;
    }
    if (entryCount == 0) {
        return;
    }

    uint64_t displayVideoScanlineCount = videoScanlineCount * (uint64_t)profile_checkpoints_scanlineScale;
    if (displayVideoScanlineCount == 0) {
        return;
    }
    uint64_t displayFrameScanlineCount = scanlineCount * (uint64_t)profile_checkpoints_scanlineScale;
    if (displayFrameScanlineCount == 0) {
        return;
    }

    int64_t checkpointLines[E9K_CHECKPOINT_COUNT];
    profile_checkpoints_buildOverlayLines(entries,
                                          entryCount,
                                          scanlineCount,
                                          profile_checkpoints_scanlineScale,
                                          checkpointLines);

    SDL_BlendMode prevBlend = SDL_BLENDMODE_BLEND;
    SDL_GetRenderDrawBlendMode(ctx->renderer, &prevBlend);
    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);

    int clipEnabled = SDL_RenderIsClipEnabled(ctx->renderer);
    SDL_Rect prevClip = {0, 0, 0, 0};
    SDL_RenderGetClipRect(ctx->renderer, &prevClip);
    SDL_RenderSetClipRect(ctx->renderer, clipRect);

    for (size_t i = 0; i + 1 < entryCount; ++i) {
        size_t checkpointIndex = i;
        if (!profile_checkpoints_entryIsVisible(entries, entryCount, checkpointIndex)) {
            continue;
        }
        if (profile_checkpoints_overlaySelectedMask != 0 && !profile_checkpoints_overlayEntryIsSelected(checkpointIndex)) {
            continue;
        }
        if (entries[i].scanlineCount == 0) {
            continue;
        }
        size_t nextIndex = i + 1;
        while (nextIndex < entryCount && entries[nextIndex].scanlineCount == 0) {
            nextIndex++;
        }
        if (nextIndex >= entryCount) {
            continue;
        }
        const uint8_t *color = profile_checkpoints_overlayColors[checkpointIndex % (sizeof(profile_checkpoints_overlayColors) / sizeof(profile_checkpoints_overlayColors[0]))];
        SDL_SetRenderDrawColor(ctx->renderer, color[0], color[1], color[2], 128);
        int64_t startLine = checkpointLines[i];
        int64_t endLine = checkpointLines[nextIndex];
        if (endLine < startLine) {
            profile_checkpoints_drawScanlineSegment(ctx,
                                                    dst,
                                                    startLine,
                                                    (int64_t)displayFrameScanlineCount,
                                                    videoStartScanline,
                                                    displayVideoScanlineCount);
            profile_checkpoints_drawScanlineSegment(ctx,
                                                    dst,
                                                    0,
                                                    endLine,
                                                    videoStartScanline,
                                                    displayVideoScanlineCount);
        } else {
            profile_checkpoints_drawScanlineSegment(ctx,
                                                    dst,
                                                    startLine,
                                                    endLine,
                                                    videoStartScanline,
                                                    displayVideoScanlineCount);
        }
    }

    for (size_t i = 0; i < entryCount; ++i) {
        if (entries[i].scanlineCount == 0) {
            continue;
        }
        int64_t scanline = checkpointLines[i];
        int y = profile_checkpoints_scanlineToOverlayY(dst,
                                                       scanline,
                                                       videoStartScanline,
                                                       displayVideoScanlineCount);
        size_t checkpointIndex = i;
        size_t colorIndex = checkpointIndex;
        int isVisibleBoundary = profile_checkpoints_entryIsVisible(entries, entryCount, checkpointIndex);
        if (!isVisibleBoundary && i > 0) {
            size_t previousIndex = i;
            while (previousIndex > 0) {
                previousIndex--;
                if (profile_checkpoints_entryIsVisible(entries, entryCount, previousIndex)) {
                    colorIndex = previousIndex;
                    isVisibleBoundary = 1;
                    break;
                }
            }
        }
        if (!isVisibleBoundary) {
            continue;
        }
        if (profile_checkpoints_overlaySelectedMask != 0 && !profile_checkpoints_overlayEntryIsSelected(colorIndex)) {
            continue;
        }
        const uint8_t *color = profile_checkpoints_overlayColors[colorIndex % (sizeof(profile_checkpoints_overlayColors) / sizeof(profile_checkpoints_overlayColors[0]))];
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
