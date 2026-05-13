/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL.h>
#include <SDL_ttf.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aux_window.h"
#include "shader_ui.h"
#include "alloc.h"
#include "crt.h"
#include "debugger.h"
#include "config.h"
#include "e9ui.h"
#include "e9ui_labeled_checkbox.h"
#include "e9ui_vspacer.h"
#include "e9ui_text_cache.h"
#include "e9ui_seek_bar.h"
#include "e9ui_button.h"
#include "e9ui_theme.h"


#define SHADER_UI_LABEL_W 185
#define SHADER_UI_GAP 12
#define SHADER_UI_ROW_PAD 6
#define SHADER_UI_BAR_H 12
#define SHADER_UI_RIGHT_MARGIN 12

typedef struct shader_ui_slider_binding {
    float minValue;
    float maxValue;
    float (*getValue)(void);
    void (*setValue)(float value);
} shader_ui_slider_binding_t;

typedef struct shader_ui_checkbox_binding {
    int (*getValue)(void);
    void (*setValue)(int enabled);
} shader_ui_checkbox_binding_t;

typedef struct shader_ui_slider {
    e9ui_component_t *bar;
    shader_ui_slider_binding_t binding;
    const char *tooltipLabel;
    const char *tooltipUnit;
    int tooltipPrecision;
} shader_ui_slider_t;

typedef struct shader_ui_checkbox {
    e9ui_component_t *checkbox;
    shader_ui_checkbox_binding_t binding;
} shader_ui_checkbox_t;

typedef struct shader_ui_column_state {
    int rowGap;
} shader_ui_column_state_t;

typedef struct shader_ui_action_row_state {
    e9ui_component_t *defaultsButton;
    e9ui_component_t *cancelButton;
    e9ui_component_t *applyButton;
    int gap;
    int padRight;
} shader_ui_action_row_state_t;

typedef struct shader_ui_overlay_body_state {
    struct e9k_shader_ui *ui;
} shader_ui_overlay_body_state_t;

typedef struct e9k_shader_ui {
    e9ui_window_state_t windowState;
    int dirty;
    SDL_Window *window;
    SDL_Renderer *renderer;
    e9ui_context_t ctx;
    e9ui_component_t *root;
    e9ui_component_t *fullscreen;
    shader_ui_checkbox_t crtEnabled;
    shader_ui_checkbox_t geometryEnabled;
    shader_ui_checkbox_t bloomEnabled;
    shader_ui_checkbox_t halationEnabled;
    shader_ui_checkbox_t maskEnabled;
    shader_ui_checkbox_t gammaEnabled;
    shader_ui_checkbox_t chromaEnabled;
    shader_ui_checkbox_t grilleEnabled;
    shader_ui_slider_t scanStrength;
    shader_ui_slider_t halationStrength;
    shader_ui_slider_t halationThreshold;
    shader_ui_slider_t halationRadius;
    shader_ui_slider_t maskStrength;
    shader_ui_slider_t maskScale;
    shader_ui_slider_t beamStrength;
    shader_ui_slider_t beamWidth;
    shader_ui_slider_t curvature;
    shader_ui_slider_t overscan;
    shader_ui_slider_t scanlineBorder;
    int snapshotReady;
    int snapshotCrtEnabled;
    int snapshotGeometryEnabled;
    int snapshotBloomEnabled;
    int snapshotHalationEnabled;
    int snapshotMaskEnabled;
    int snapshotGammaEnabled;
    int snapshotChromaEnabled;
    int snapshotGrilleEnabled;
    float snapshotScanStrength;
    float snapshotHalationStrength;
    float snapshotHalationThreshold;
    float snapshotHalationRadius;
    float snapshotMaskStrength;
    float snapshotMaskScale;
    float snapshotBeamStrength;
    float snapshotBeamWidth;
    float snapshotCurvature;
    float snapshotOverscan;
    float snapshotScanlineBorder;
} e9k_shader_ui_t;

static e9k_shader_ui_t shader_ui_state = {
    .windowState.winX = E9UI_WINDOW_COORD_UNSET,
    .windowState.winY = E9UI_WINDOW_COORD_UNSET,
    .windowState.openMinWidthPx = 420,
    .windowState.openMinHeightPx = 420,
    .windowState.openCenterWhenNoSaved = 1,
};

static const aux_window_ops_t shader_ui_auxWindowOps = {
    .setFocus = shader_ui_setMainWindowFocused,
    .render = shader_ui_render,
};

static e9ui_window_backend_t
shader_ui_windowBackend(void)
{
    return e9ui_window_backend_overlay;
}

static int
shader_ui_parseInt(const char *value, int *out)
{
    if (!value || !out) {
        return 0;
    }
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (!end || end == value) {
        return 0;
    }
    if (parsed < INT_MIN || parsed > INT_MAX) {
        return 0;
    }
    *out = (int)parsed;
    return 1;
}

static e9ui_rect_t
shader_ui_windowDefaultRect(const e9ui_context_t *ctx)
{
    e9ui_rect_t rect = {
        e9ui_scale_px(ctx, 96),
        e9ui_scale_px(ctx, 96),
        e9ui_scale_px(ctx, 520),
        e9ui_scale_px(ctx, 720)
    };
    return rect;
}

static int
shader_ui_overlayTitlebarHeightEstimate(const e9ui_context_t *ctx)
{
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : (ctx ? ctx->font : NULL);
    int textH = font ? TTF_FontHeight(font) : 16;
    if (textH <= 0) {
        textH = 16;
    }
    int padY = e9ui_scale_px(ctx, 4);
    return textH + padY * 2;
}

static int
shader_ui_getCrtEnabled(void)
{
    return crt_isEnabled();
}

static void
shader_ui_setCrtEnabled(int enabled)
{
    crt_setEnabled(enabled ? 1 : 0);
    debugger.config.crtEnabled = crt_isEnabled() ? 1 : 0;
}

static int
shader_ui_getGeometryEnabled(void)
{
    return crt_isGeometryEnabled();
}

static void
shader_ui_setGeometryEnabled(int enabled)
{
    crt_setGeometryEnabled(enabled ? 1 : 0);
}

static int
shader_ui_getBloomEnabled(void)
{
    return crt_isBloomEnabled();
}

static void
shader_ui_setBloomEnabled(int enabled)
{
    crt_setBloomEnabled(enabled ? 1 : 0);
}

static int
shader_ui_getHalationEnabled(void)
{
    return crt_isHalationEnabled();
}

static void
shader_ui_setHalationEnabled(int enabled)
{
    crt_setHalationEnabled(enabled ? 1 : 0);
}

static int
shader_ui_getMaskEnabled(void)
{
    return crt_isMaskEnabled();
}

static void
shader_ui_setMaskEnabled(int enabled)
{
    crt_setMaskEnabled(enabled ? 1 : 0);
}

static int
shader_ui_getGammaEnabled(void)
{
    return crt_isGammaEnabled();
}

static void
shader_ui_setGammaEnabled(int enabled)
{
    crt_setGammaEnabled(enabled ? 1 : 0);
}

static int
shader_ui_getChromaEnabled(void)
{
    return crt_isChromaEnabled();
}

static void
shader_ui_setChromaEnabled(int enabled)
{
    crt_setChromaEnabled(enabled ? 1 : 0);
}

static int
shader_ui_getGrilleEnabled(void)
{
    return crt_isGrilleEnabled();
}

static void
shader_ui_setGrilleEnabled(int enabled)
{
    crt_setGrilleEnabled(enabled ? 1 : 0);
}

static void
shader_ui_snapshot(e9k_shader_ui_t *ui)
{
    if (!ui) {
        return;
    }
    ui->snapshotCrtEnabled = crt_isEnabled();
    ui->snapshotGeometryEnabled = crt_isGeometryEnabled();
    ui->snapshotBloomEnabled = crt_isBloomEnabled();
    ui->snapshotHalationEnabled = crt_isHalationEnabled();
    ui->snapshotMaskEnabled = crt_isMaskEnabled();
    ui->snapshotGammaEnabled = crt_isGammaEnabled();
    ui->snapshotChromaEnabled = crt_isChromaEnabled();
    ui->snapshotGrilleEnabled = crt_isGrilleEnabled();
    ui->snapshotScanStrength = crt_getScanStrength();
    ui->snapshotHalationStrength = crt_getHalationStrength();
    ui->snapshotHalationThreshold = crt_getHalationThreshold();
    ui->snapshotHalationRadius = crt_getHalationRadius();
    ui->snapshotMaskStrength = crt_getMaskStrength();
    ui->snapshotMaskScale = crt_getMaskScale();
    ui->snapshotBeamStrength = crt_getBeamStrength();
    ui->snapshotBeamWidth = crt_getBeamWidth();
    ui->snapshotCurvature = crt_getCurvatureK();
    ui->snapshotOverscan = crt_getOverscan();
    ui->snapshotScanlineBorder = crt_getScanlineBorder();
    ui->snapshotReady = 1;
}

static void
shader_ui_restoreSnapshot(const e9k_shader_ui_t *ui)
{
    if (!ui || !ui->snapshotReady) {
        return;
    }

    shader_ui_setCrtEnabled(ui->snapshotCrtEnabled);
    crt_setGeometryEnabled(ui->snapshotGeometryEnabled);
    crt_setBloomEnabled(ui->snapshotBloomEnabled);
    crt_setHalationEnabled(ui->snapshotHalationEnabled);
    crt_setMaskEnabled(ui->snapshotMaskEnabled);
    crt_setGammaEnabled(ui->snapshotGammaEnabled);
    crt_setChromaEnabled(ui->snapshotChromaEnabled);
    crt_setGrilleEnabled(ui->snapshotGrilleEnabled);
    crt_setScanStrength(ui->snapshotScanStrength);
    crt_setHalationStrength(ui->snapshotHalationStrength);
    crt_setHalationThreshold(ui->snapshotHalationThreshold);
    crt_setHalationRadius(ui->snapshotHalationRadius);
    crt_setMaskStrength(ui->snapshotMaskStrength);
    crt_setMaskScale(ui->snapshotMaskScale);
    crt_setBeamStrength(ui->snapshotBeamStrength);
    crt_setBeamWidth(ui->snapshotBeamWidth);
    crt_setCurvatureK(ui->snapshotCurvature);
    crt_setOverscan(ui->snapshotOverscan);
    crt_setScanlineBorder(ui->snapshotScanlineBorder);
}

static int
shader_ui_columnPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    if (!self || !ctx) {
        return 0;
    }
    shader_ui_column_state_t *st = (shader_ui_column_state_t*)self->state;
    if (!st) {
        return 0;
    }
    int gap = e9ui_scale_px(ctx, st->rowGap);
    int total = 0;
    int visibleCount = 0;
    e9ui_child_iterator iter;
    e9ui_child_iterator *it = e9ui_child_iterateChildren(self, &iter);
    while (e9ui_child_interateNext(it)) {
        e9ui_component_t *child = it->child;
        if (!child || e9ui_getHidden(child)) {
            continue;
        }
        int h = child->preferredHeight ? child->preferredHeight(child, ctx, availW) : 0;
        total += h;
        visibleCount++;
    }
    if (visibleCount > 1) {
        total += gap * (visibleCount - 1);
    }
    return total;
}

static void
shader_ui_columnLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self || !ctx) {
        return;
    }
    shader_ui_column_state_t *st = (shader_ui_column_state_t*)self->state;
    if (!st) {
        return;
    }
    self->bounds = bounds;
    int gap = e9ui_scale_px(ctx, st->rowGap);
    int y = bounds.y;
    e9ui_child_iterator iter;
    e9ui_child_iterator *it = e9ui_child_iterateChildren(self, &iter);
    while (e9ui_child_interateNext(it)) {
        e9ui_component_t *child = it->child;
        if (!child || e9ui_getHidden(child)) {
            continue;
        }
        int h = child->preferredHeight ? child->preferredHeight(child, ctx, bounds.w) : 0;
        if (child->layout) {
            e9ui_rect_t row = (e9ui_rect_t){ bounds.x, y, bounds.w, h };
            child->layout(child, ctx, row);
        }
        y += h + gap;
    }
}

static void
shader_ui_columnRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx) {
        return;
    }
    e9ui_child_iterator iter;
    e9ui_child_iterator *it = e9ui_child_iterateChildren(self, &iter);
    while (e9ui_child_interateNext(it)) {
        e9ui_component_t *child = it->child;
        if (child && child->render) {
            child->render(child, ctx);
        }
    }
}

static e9ui_component_t *
shader_ui_columnMake(void)
{
    e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    shader_ui_column_state_t *st = (shader_ui_column_state_t*)alloc_calloc(1, sizeof(*st));
    if (!comp || !st) {
        if (comp) {
            alloc_free(comp);
        }
        if (st) {
            alloc_free(st);
        }
        return NULL;
    }
    st->rowGap = 4;
    comp->name = "shader_ui_column";
    comp->state = st;
    comp->preferredHeight = shader_ui_columnPreferredHeight;
    comp->layout = shader_ui_columnLayout;
    comp->render = shader_ui_columnRender;
    return comp;
}

static void
shader_ui_actionRowLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self || !ctx || !self->state) {
        return;
    }
    shader_ui_action_row_state_t *st = (shader_ui_action_row_state_t*)self->state;
    self->bounds = bounds;
    int gap = e9ui_scale_px(ctx, st->gap);
    int padRight = e9ui_scale_px(ctx, st->padRight);
    int barH = e9ui_scale_px(ctx, SHADER_UI_BAR_H);
    int inset = barH / 2;
    if (inset < 6) {
        inset = 6;
    }
    padRight += inset;
    int wApply = 0;
    int hApply = 0;
    int wDefaults = 0;
    int hDefaults = 0;
    int wCancel = 0;
    int hCancel = 0;
    if (st->applyButton) {
        e9ui_button_measure(st->applyButton, ctx, &wApply, &hApply);
    }
    if (st->defaultsButton) {
        e9ui_button_measure(st->defaultsButton, ctx, &wDefaults, &hDefaults);
    }
    if (st->cancelButton) {
        e9ui_button_measure(st->cancelButton, ctx, &wCancel, &hCancel);
    }
    int totalW = 0;
    if (st->applyButton) {
        totalW += wApply;
    }
    if (st->defaultsButton) {
        totalW += wDefaults;
    }
    if (st->cancelButton) {
        totalW += wCancel;
    }
    int gapCount = 0;
    if (st->defaultsButton) {
        gapCount++;
    }
    if (st->cancelButton) {
        gapCount++;
    }
    if (st->applyButton) {
        gapCount++;
    }
    if (gapCount > 1) {
        totalW += gap;
    }
    int x = bounds.x + bounds.w - padRight - totalW;
    int y = bounds.y;
    int rowH = bounds.h;
    if (st->applyButton) {
        int bh = hApply > 0 ? hApply : rowH;
        st->applyButton->bounds.x = x;
        st->applyButton->bounds.y = y + (rowH - bh) / 2;
        st->applyButton->bounds.w = wApply;
        st->applyButton->bounds.h = bh;
        x += wApply + gap;
    }
    if (st->defaultsButton) {
        int bh = hDefaults > 0 ? hDefaults : rowH;
        st->defaultsButton->bounds.x = x;
        st->defaultsButton->bounds.y = y + (rowH - bh) / 2;
        st->defaultsButton->bounds.w = wDefaults;
        st->defaultsButton->bounds.h = bh;
        x += wDefaults + gap;
    }
    if (st->cancelButton) {
        int bh = hCancel > 0 ? hCancel : rowH;
        st->cancelButton->bounds.x = x;
        st->cancelButton->bounds.y = y + (rowH - bh) / 2;
        st->cancelButton->bounds.w = wCancel;
        st->cancelButton->bounds.h = bh;
    }
}

static void
shader_ui_actionRowRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !self->state) {
        return;
    }
    shader_ui_action_row_state_t *st = (shader_ui_action_row_state_t*)self->state;
    if (st->defaultsButton && st->defaultsButton->render) {
        st->defaultsButton->render(st->defaultsButton, ctx);
    }
    if (st->cancelButton && st->cancelButton->render) {
        st->cancelButton->render(st->cancelButton, ctx);
    }
    if (st->applyButton && st->applyButton->render) {
        st->applyButton->render(st->applyButton, ctx);
    }
}

static int
shader_ui_actionRowPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    if (!self || !ctx || !self->state) {
        return 0;
    }
    shader_ui_action_row_state_t *st = (shader_ui_action_row_state_t*)self->state;
    int hDefaults = 0;
    int hCancel = 0;
    int hApply = 0;
    if (st->defaultsButton) {
        int w = 0;
        e9ui_button_measure(st->defaultsButton, ctx, &w, &hDefaults);
    }
    if (st->cancelButton) {
        int w = 0;
        e9ui_button_measure(st->cancelButton, ctx, &w, &hCancel);
    }
    if (st->applyButton) {
        int w = 0;
        e9ui_button_measure(st->applyButton, ctx, &w, &hApply);
    }
    int max = hDefaults > hCancel ? hDefaults : hCancel;
    return hApply > max ? hApply : max;
}

static e9ui_component_t *
shader_ui_actionRowMake(e9ui_component_t *defaultsButton, e9ui_component_t *cancelButton,
                        e9ui_component_t *applyButton)
{
    e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    shader_ui_action_row_state_t *st = (shader_ui_action_row_state_t*)alloc_calloc(1, sizeof(*st));
    if (!comp || !st) {
        if (comp) {
            alloc_free(comp);
        }
        if (st) {
            alloc_free(st);
        }
        return NULL;
    }
    st->defaultsButton = defaultsButton;
    st->cancelButton = cancelButton;
    st->applyButton = applyButton;
    st->gap = 10;
    st->padRight = SHADER_UI_RIGHT_MARGIN;
    comp->name = "shader_ui_action_row";
    comp->state = st;
    comp->preferredHeight = shader_ui_actionRowPreferredHeight;
    comp->layout = shader_ui_actionRowLayout;
    comp->render = shader_ui_actionRowRender;
    if (applyButton) {
        e9ui_child_add(comp, applyButton, NULL);
    }
    if (defaultsButton) {
        e9ui_child_add(comp, defaultsButton, NULL);
    }
    if (cancelButton) {
        e9ui_child_add(comp, cancelButton, NULL);
    }
    return comp;
}

static void
shader_ui_checkboxChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    shader_ui_checkbox_binding_t *binding = (shader_ui_checkbox_binding_t*)user;
    if (!binding || !binding->setValue) {
        return;
    }
    binding->setValue(selected ? 1 : 0);
}

static void
shader_ui_sliderChanged(float percent, void *user)
{
    shader_ui_slider_binding_t *binding = (shader_ui_slider_binding_t*)user;
    if (!binding || !binding->setValue) {
        return;
    }
    float range = binding->maxValue - binding->minValue;
    float value = binding->minValue + percent * range;
    binding->setValue(value);
}

static void
shader_ui_sliderTooltip(float percent, char *out, size_t cap, void *user)
{
    shader_ui_slider_t *slider = (shader_ui_slider_t*)user;
    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (!slider) {
        return;
    }
    float range = slider->binding.maxValue - slider->binding.minValue;
    float value = slider->binding.minValue + percent * range;
    int precision = slider->tooltipPrecision > 0 ? slider->tooltipPrecision : 2;
    const char *label = slider->tooltipLabel ? slider->tooltipLabel : "Value";
    const char *unit = slider->tooltipUnit ? slider->tooltipUnit : "";
    snprintf(out, cap, "%s %.2f%s", label, value, unit);
    if (precision != 2) {
        char fmt[32];
        snprintf(fmt, sizeof(fmt), "%%s %%.%df%%s", precision);
        snprintf(out, cap, fmt, label, value, unit);
    }
}

static float
shader_ui_clampPercent(float percent)
{
    if (percent < 0.0f) {
        return 0.0f;
    }
    if (percent > 1.0f) {
        return 1.0f;
    }
    return percent;
}

static void
shader_ui_syncCheckbox(shader_ui_checkbox_t *checkbox, e9ui_context_t *ctx)
{
    if (!checkbox || !checkbox->checkbox || !checkbox->binding.getValue) {
        return;
    }
    int selected = checkbox->binding.getValue();
    e9ui_labeled_checkbox_setSelected(checkbox->checkbox, selected, ctx);
}

static void
shader_ui_syncSlider(shader_ui_slider_t *slider)
{
    if (!slider || !slider->bar || !slider->binding.getValue) {
        return;
    }
    float range = slider->binding.maxValue - slider->binding.minValue;
    if (range <= 0.0f) {
        return;
    }
    float value = slider->binding.getValue();
    float percent = (value - slider->binding.minValue) / range;
    e9ui_seek_bar_setPercent(slider->bar, shader_ui_clampPercent(percent));
}

static e9ui_component_t *
shader_ui_makeCheckbox(const char *label, shader_ui_checkbox_t *slot)
{
    if (!slot) {
        return NULL;
    }
    int selected = slot->binding.getValue ? slot->binding.getValue() : 0;
    e9ui_component_t *comp = e9ui_labeled_checkbox_make(label, SHADER_UI_LABEL_W, 0,
                                                        selected, shader_ui_checkboxChanged,
                                                        &slot->binding);
    slot->checkbox = comp;
    return comp;
}

static e9ui_component_t *
shader_ui_makeSlider(const char *label, shader_ui_slider_t *slot)
{
    if (!slot) {
        return NULL;
    }
    e9ui_component_t *bar = NULL;
    e9ui_component_t *row = e9ui_slider_make(label,
                                             SHADER_UI_LABEL_W,
                                             SHADER_UI_GAP,
                                             SHADER_UI_ROW_PAD,
                                             SHADER_UI_BAR_H,
                                             SHADER_UI_RIGHT_MARGIN,
                                             &bar);
    slot->bar = bar;
    if (!slot->tooltipLabel) {
        slot->tooltipLabel = label;
    }
    if (slot->tooltipPrecision <= 0) {
        slot->tooltipPrecision = 2;
    }
    if (bar) {
        e9ui_seek_bar_setCallback(bar, shader_ui_sliderChanged, &slot->binding);
        e9ui_seek_bar_setTooltipCallback(bar, shader_ui_sliderTooltip, slot);
        e9ui_seek_bar_setHoverMargin(bar, 6);
    }
    return row;
}

static void
shader_ui_syncState(e9k_shader_ui_t *ui)
{
    if (!ui) {
        return;
    }
    shader_ui_syncCheckbox(&ui->crtEnabled, &ui->ctx);
    shader_ui_syncCheckbox(&ui->geometryEnabled, &ui->ctx);
    shader_ui_syncCheckbox(&ui->bloomEnabled, &ui->ctx);
    shader_ui_syncCheckbox(&ui->halationEnabled, &ui->ctx);
    shader_ui_syncCheckbox(&ui->maskEnabled, &ui->ctx);
    shader_ui_syncCheckbox(&ui->gammaEnabled, &ui->ctx);
    shader_ui_syncCheckbox(&ui->chromaEnabled, &ui->ctx);
    shader_ui_syncCheckbox(&ui->grilleEnabled, &ui->ctx);
    shader_ui_syncSlider(&ui->scanStrength);
    shader_ui_syncSlider(&ui->halationStrength);
    shader_ui_syncSlider(&ui->halationThreshold);
    shader_ui_syncSlider(&ui->halationRadius);
    shader_ui_syncSlider(&ui->maskStrength);
    shader_ui_syncSlider(&ui->maskScale);
    shader_ui_syncSlider(&ui->beamStrength);
    shader_ui_syncSlider(&ui->beamWidth);
    shader_ui_syncSlider(&ui->curvature);
    shader_ui_syncSlider(&ui->overscan);
    shader_ui_syncSlider(&ui->scanlineBorder);
}

static void
shader_ui_buildBindings(e9k_shader_ui_t *ui)
{
    ui->crtEnabled.binding.getValue = shader_ui_getCrtEnabled;
    ui->crtEnabled.binding.setValue = shader_ui_setCrtEnabled;
    ui->geometryEnabled.binding.getValue = shader_ui_getGeometryEnabled;
    ui->geometryEnabled.binding.setValue = shader_ui_setGeometryEnabled;
    ui->bloomEnabled.binding.getValue = shader_ui_getBloomEnabled;
    ui->bloomEnabled.binding.setValue = shader_ui_setBloomEnabled;
    ui->halationEnabled.binding.getValue = shader_ui_getHalationEnabled;
    ui->halationEnabled.binding.setValue = shader_ui_setHalationEnabled;
    ui->maskEnabled.binding.getValue = shader_ui_getMaskEnabled;
    ui->maskEnabled.binding.setValue = shader_ui_setMaskEnabled;
    ui->gammaEnabled.binding.getValue = shader_ui_getGammaEnabled;
    ui->gammaEnabled.binding.setValue = shader_ui_setGammaEnabled;
    ui->chromaEnabled.binding.getValue = shader_ui_getChromaEnabled;
    ui->chromaEnabled.binding.setValue = shader_ui_setChromaEnabled;
    ui->grilleEnabled.binding.getValue = shader_ui_getGrilleEnabled;
    ui->grilleEnabled.binding.setValue = shader_ui_setGrilleEnabled;

    ui->scanStrength.binding.minValue = 0.0f;
    ui->scanStrength.binding.maxValue = 1.0f;
    ui->scanStrength.binding.getValue = crt_getScanStrength;
    ui->scanStrength.binding.setValue = crt_setScanStrength;
    ui->scanStrength.tooltipLabel = "Scan Strength";

    ui->halationStrength.binding.minValue = 0.0f;
    ui->halationStrength.binding.maxValue = 1.0f;
    ui->halationStrength.binding.getValue = crt_getHalationStrength;
    ui->halationStrength.binding.setValue = crt_setHalationStrength;
    ui->halationStrength.tooltipLabel = "Halation Strength";

    ui->halationThreshold.binding.minValue = 0.0f;
    ui->halationThreshold.binding.maxValue = 1.0f;
    ui->halationThreshold.binding.getValue = crt_getHalationThreshold;
    ui->halationThreshold.binding.setValue = crt_setHalationThreshold;
    ui->halationThreshold.tooltipLabel = "Halation Threshold";

    ui->halationRadius.binding.minValue = 0.0f;
    ui->halationRadius.binding.maxValue = 64.0f;
    ui->halationRadius.binding.getValue = crt_getHalationRadius;
    ui->halationRadius.binding.setValue = crt_setHalationRadius;
    ui->halationRadius.tooltipLabel = "Halation Radius";
    ui->halationRadius.tooltipUnit = "px";

    ui->maskStrength.binding.minValue = 0.0f;
    ui->maskStrength.binding.maxValue = 1.0f;
    ui->maskStrength.binding.getValue = crt_getMaskStrength;
    ui->maskStrength.binding.setValue = crt_setMaskStrength;
    ui->maskStrength.tooltipLabel = "Mask Strength";

    ui->maskScale.binding.minValue = 0.25f;
    ui->maskScale.binding.maxValue = 32.0f;
    ui->maskScale.binding.getValue = crt_getMaskScale;
    ui->maskScale.binding.setValue = crt_setMaskScale;
    ui->maskScale.tooltipLabel = "Mask Scale";
    ui->maskScale.tooltipUnit = "x";

    ui->beamStrength.binding.minValue = 0.0f;
    ui->beamStrength.binding.maxValue = 1.0f;
    ui->beamStrength.binding.getValue = crt_getBeamStrength;
    ui->beamStrength.binding.setValue = crt_setBeamStrength;
    ui->beamStrength.tooltipLabel = "Beam Strength";

    ui->beamWidth.binding.minValue = 0.25f;
    ui->beamWidth.binding.maxValue = 4.0f;
    ui->beamWidth.binding.getValue = crt_getBeamWidth;
    ui->beamWidth.binding.setValue = crt_setBeamWidth;
    ui->beamWidth.tooltipLabel = "Beam Width";

    ui->curvature.binding.minValue = 0.0f;
    ui->curvature.binding.maxValue = 0.20f;
    ui->curvature.binding.getValue = crt_getCurvatureK;
    ui->curvature.binding.setValue = crt_setCurvatureK;
    ui->curvature.tooltipLabel = "Curvature";
    ui->curvature.tooltipPrecision = 3;

    ui->overscan.binding.minValue = 0.50f;
    ui->overscan.binding.maxValue = 1.50f;
    ui->overscan.binding.getValue = crt_getOverscan;
    ui->overscan.binding.setValue = crt_setOverscan;
    ui->overscan.tooltipLabel = "Overscan";
    ui->overscan.tooltipUnit = "x";

    ui->scanlineBorder.binding.minValue = 0.0f;
    ui->scanlineBorder.binding.maxValue = 0.45f;
    ui->scanlineBorder.binding.getValue = crt_getScanlineBorder;
    ui->scanlineBorder.binding.setValue = crt_setScanlineBorder;
    ui->scanlineBorder.tooltipLabel = "Scanline Border";
}

static void
shader_ui_deferredShutdown(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    shader_ui_shutdown();
}

static void
shader_ui_requestClose(e9ui_context_t *ctx, e9k_shader_ui_t *ui)
{
    if (!ui) {
        return;
    }
    (void)e9ui_defer(ctx ? ctx : &e9ui->ctx,
                     shader_ui_deferredShutdown,
                     ui);
}

static void
shader_ui_cancel(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    e9k_shader_ui_t *ui = (e9k_shader_ui_t*)user;
    if (!ui) {
        return;
    }
    shader_ui_restoreSnapshot(ui);
    shader_ui_requestClose(ctx, ui);
}

static void
shader_ui_defaults(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    crt_setAdvancedDefaults();
    debugger.config.crtEnabled = crt_isEnabled() ? 1 : 0;
}

static void
shader_ui_apply(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    e9k_shader_ui_t *ui = (e9k_shader_ui_t*)user;
    if (!ui) {
        return;
    }
    config_saveConfig();
    shader_ui_requestClose(ctx, ui);
}

static e9ui_component_t *
shader_ui_buildRoot(e9k_shader_ui_t *ui)
{
    e9ui_component_t *stack = e9ui_stack_makeVertical();
    if (!stack) {
        return NULL;
    }

    e9ui_stack_addFixed(stack, e9ui_vspacer_make(SHADER_UI_RIGHT_MARGIN));

    e9ui_component_t *checkboxRow = e9ui_hstack_make();
    e9ui_component_t *leftCol = shader_ui_columnMake();
    e9ui_component_t *rightCol = shader_ui_columnMake();
    e9ui_component_t *row = shader_ui_makeCheckbox("CRT Enabled", &ui->crtEnabled);
    e9ui_child_add(leftCol, row, NULL);
    row = shader_ui_makeCheckbox("Geometry", &ui->geometryEnabled);
    e9ui_child_add(leftCol, row, NULL);
    row = shader_ui_makeCheckbox("Mask", &ui->maskEnabled);
    e9ui_child_add(leftCol, row, NULL);
    row = shader_ui_makeCheckbox("Bloom", &ui->bloomEnabled);
    e9ui_child_add(rightCol, row, NULL);
    row = shader_ui_makeCheckbox("Halation", &ui->halationEnabled);
    e9ui_child_add(rightCol, row, NULL);
    row = shader_ui_makeCheckbox("Gamma", &ui->gammaEnabled);
    e9ui_child_add(rightCol, row, NULL);
    row = shader_ui_makeCheckbox("Chroma", &ui->chromaEnabled);
    e9ui_child_add(rightCol, row, NULL);
    row = shader_ui_makeCheckbox("Grille", &ui->grilleEnabled);
    e9ui_child_add(leftCol, row, NULL);
    e9ui_hstack_addFlex(checkboxRow, leftCol);
    e9ui_hstack_addFixed(checkboxRow, e9ui_spacer_make(24), 24);
    e9ui_hstack_addFlex(checkboxRow, rightCol);
    e9ui_stack_addFixed(stack, checkboxRow);

    e9ui_stack_addFixed(stack, e9ui_vspacer_make(10));

    row = shader_ui_makeSlider("Scan Strength", &ui->scanStrength);
    e9ui_stack_addFixed(stack, row);
    row = shader_ui_makeSlider("Mask Strength", &ui->maskStrength);
    e9ui_stack_addFixed(stack, row);
    row = shader_ui_makeSlider("Mask Scale", &ui->maskScale);
    e9ui_stack_addFixed(stack, row);
    row = shader_ui_makeSlider("Beam Strength", &ui->beamStrength);
    e9ui_stack_addFixed(stack, row);
    row = shader_ui_makeSlider("Beam Width", &ui->beamWidth);
    e9ui_stack_addFixed(stack, row);
    row = shader_ui_makeSlider("Curvature", &ui->curvature);
    e9ui_stack_addFixed(stack, row);

    row = shader_ui_makeSlider("Overscan", &ui->overscan);
    e9ui_stack_addFixed(stack, row);
    row = shader_ui_makeSlider("Scanline Border", &ui->scanlineBorder);
    e9ui_stack_addFixed(stack, row);
    row = shader_ui_makeSlider("Halation Strength", &ui->halationStrength);
    e9ui_stack_addFixed(stack, row);
    row = shader_ui_makeSlider("Halation Threshold", &ui->halationThreshold);
    e9ui_stack_addFixed(stack, row);
    row = shader_ui_makeSlider("Halation Radius", &ui->halationRadius);
    e9ui_stack_addFixed(stack, row);

    e9ui_stack_addFixed(stack, e9ui_vspacer_make(SHADER_UI_RIGHT_MARGIN));
    e9ui_component_t *apply = e9ui_button_make("Apply", shader_ui_apply, ui);
    e9ui_component_t *defaults = e9ui_button_make("Defaults", shader_ui_defaults, ui);
    e9ui_component_t *cancel = e9ui_button_make("Cancel", shader_ui_cancel, ui);
    e9ui_button_setTheme(apply, e9ui_theme_button_preset_green());
    e9ui_button_setTheme(cancel, e9ui_theme_button_preset_red());
    e9ui_component_t *actions = shader_ui_actionRowMake(defaults, cancel, apply);
    e9ui_stack_addFixed(stack, actions);
    e9ui_stack_addFlex(stack, e9ui_vspacer_make(6));
    return stack;
}

static int
shader_ui_measureRootHeight(e9ui_component_t *root, e9ui_context_t *ctx, int availW)
{
    if (!root || !ctx) {
        return 0;
    }
    int innerW = availW;
    int total = 0;
    e9ui_child_iterator iter;
    e9ui_child_iterator *it = e9ui_child_iterateChildren(root, &iter);
    while (e9ui_child_interateNext(it)) {
        e9ui_component_t *child = it->child;
        if (!child || e9ui_getHidden(child)) {
            continue;
        }
        if (child->preferredHeight) {
            total += child->preferredHeight(child, ctx, innerW);
        }
    }
    return total;
}

static int
shader_ui_overlayBodyPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)ctx;
    (void)availW;
    return 0;
}

static void
shader_ui_overlayBodyLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self) {
        return;
    }
    self->bounds = bounds;
    shader_ui_overlay_body_state_t *st = (shader_ui_overlay_body_state_t *)self->state;
    if (!st || !st->ui || !st->ui->root || !st->ui->root->layout) {
        return;
    }
    st->ui->root->layout(st->ui->root, ctx, bounds);
}

static void
shader_ui_overlayBodyRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !self->state) {
        return;
    }
    shader_ui_overlay_body_state_t *st = (shader_ui_overlay_body_state_t *)self->state;
    e9k_shader_ui_t *ui = st ? st->ui : NULL;
    if (!ui || !ui->root) {
        return;
    }
    ui->ctx = *ctx;
    ui->ctx.window = ctx->window;
    ui->ctx.renderer = ctx->renderer;
    ui->ctx.font = e9ui->ctx.font;
    ui->ctx.winW = self->bounds.w;
    ui->ctx.winH = self->bounds.h;
    ui->ctx.mouseX = ctx->mouseX;
    ui->ctx.mouseY = ctx->mouseY;
    ui->ctx.mousePrevX = ctx->mousePrevX;
    ui->ctx.mousePrevY = ctx->mousePrevY;
    ui->ctx.focusRoot = ui->root;
    ui->ctx.focusFullscreen = ui->fullscreen;
    shader_ui_syncState(ui);
    e9ui_component_t *root = ui->fullscreen ? ui->fullscreen : ui->root;
    if (root && root->render) {
        root->render(root, &ui->ctx);
    }
    ui->dirty = 0;
}

static e9ui_component_t *
shader_ui_makeOverlayBodyHost(e9k_shader_ui_t *ui)
{
    if (!ui || !ui->root) {
        return NULL;
    }
    e9ui_component_t *host = (e9ui_component_t *)alloc_calloc(1, sizeof(*host));
    if (!host) {
        return NULL;
    }
    shader_ui_overlay_body_state_t *st = (shader_ui_overlay_body_state_t *)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(host);
        return NULL;
    }
    st->ui = ui;
    host->name = "shader_ui_overlay_body";
    host->state = st;
    host->preferredHeight = shader_ui_overlayBodyPreferredHeight;
    host->layout = shader_ui_overlayBodyLayout;
    host->render = shader_ui_overlayBodyRender;
    e9ui_child_add(host, ui->root, alloc_strdup("shader_ui_root"));
    return host;
}

static void
shader_ui_overlayWindowCloseRequested(e9ui_window_t *window, void *user)
{
    (void)window;
    shader_ui_cancel(&e9ui->ctx, user);
}

int
shader_ui_init(void)
{
    e9k_shader_ui_t *ui = &shader_ui_state;
    if (ui->windowState.open) {
        return 1;
    }
    shader_ui_buildBindings(ui);

    ui->windowState.windowHost = e9ui_windowCreate(shader_ui_windowBackend());
    if (!ui->windowState.windowHost) {
        return 0;
    }
    memset(&ui->ctx, 0, sizeof(ui->ctx));
    ui->ctx.font = e9ui->ctx.font;
    shader_ui_snapshot(ui);
    ui->dirty = 1;

    ui->root = shader_ui_buildRoot(ui);
    if (!ui->root) {
        e9ui_windowDestroy(ui->windowState.windowHost);
        ui->windowState.windowHost = NULL;
        return 0;
    }
    {
        e9ui_rect_t rect = e9ui_windowResolveStateOpenRect(&e9ui->ctx,
                                                           shader_ui_windowDefaultRect(&e9ui->ctx),
                                                           &ui->windowState);
        if (!e9ui_windowHasSavedSize(ui->windowState.winW, ui->windowState.winH)) {
            int desiredRenderH = shader_ui_measureRootHeight(ui->root, &e9ui->ctx, rect.w);
            if (desiredRenderH > 0) {
                rect.h = desiredRenderH +
                         shader_ui_overlayTitlebarHeightEstimate(&e9ui->ctx) +
                         e9ui_scale_px(&e9ui->ctx, 20);
                e9ui_windowClampRectSize(&rect, &e9ui->ctx, 420, 420);
            }
        }
        e9ui_component_t *overlayBodyHost = shader_ui_makeOverlayBodyHost(ui);

        e9ui_windowOpen(ui->windowState.windowHost,
                                     "CRT SETTINGS",
                                     rect,
                                     overlayBodyHost,
                                     shader_ui_overlayWindowCloseRequested,
                                     ui,
			             &e9ui->ctx);

        ui->window = e9ui->ctx.window;
        ui->renderer = e9ui->ctx.renderer;
        ui->ctx = e9ui->ctx;
    }
    ui->windowState.open = 1;
    aux_window_register(&shader_ui_auxWindowOps, ui);
    return 1;
}

void
shader_ui_shutdown(void)
{
    e9k_shader_ui_t *ui = &shader_ui_state;
    if (!ui->windowState.open) {
        return;
    }
    aux_window_unregister(&shader_ui_auxWindowOps, ui);
    (void)e9ui_windowCaptureStateRectSnapshot(&ui->windowState, &e9ui->ctx);
    config_saveConfig();
    e9ui_text_cache_clearRenderer(ui->renderer);
    ui->root = NULL;
    if (ui->windowState.windowHost) {
        e9ui_windowDestroy(ui->windowState.windowHost);
        ui->windowState.windowHost = NULL;
    }
    ui->renderer = NULL;
    ui->window = NULL;
    ui->windowState.open = 0;
    ui->dirty = 0;
    memset(&ui->ctx, 0, sizeof(ui->ctx));
}

int
shader_ui_isOpen(void)
{
    return shader_ui_state.windowState.open ? 1 : 0;
}

void
shader_ui_setMainWindowFocused(int focused)
{
    (void)focused;
}

void
shader_ui_render(void)
{
    e9k_shader_ui_t *ui = &shader_ui_state;
    if (!ui->windowState.open || !ui->root) {
        return;
    }
    if (e9ui_windowCaptureStateRectChanged(&ui->windowState, &e9ui->ctx)) {
        config_saveConfig();
    }
}

void
shader_ui_persistConfig(FILE *file)
{
    if (!file) {
        return;
    }
    e9k_shader_ui_t *ui = &shader_ui_state;
    e9ui_windowPersistStateRect(file,
                                "comp.shader_ui",
                                &ui->windowState,
                                &e9ui->ctx);
}

int
shader_ui_loadConfigProperty(const char *prop, const char *value)
{
    e9k_shader_ui_t *ui = &shader_ui_state;
    int intValue = 0;
    if (strcmp(prop, "win_x") == 0) {
        if (!shader_ui_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winX = intValue;
    } else if (strcmp(prop, "win_y") == 0) {
        if (!shader_ui_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winY = intValue;
    } else if (strcmp(prop, "win_w") == 0) {
        if (!shader_ui_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winW = intValue;
    } else if (strcmp(prop, "win_h") == 0) {
        if (!shader_ui_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winH = intValue;
    } else {
        return 0;
    }
    ui->windowState.winHasSaved =
        e9ui_windowHasSavedPosition(ui->windowState.winX, ui->windowState.winY);
    return 1;
}
