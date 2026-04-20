/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "alloc.h"
#include "amiga_blit_info.h"
#include "amiga_memview.h"
#include "aux_window.h"
#include "config.h"
#include "debugger.h"
#include "e9ui.h"
#include "e9ui_box.h"
#include "e9ui_button.h"
#include "e9ui_flow.h"
#include "e9ui_hstack.h"
#include "e9ui_link.h"
#include "e9ui_spacer.h"
#include "e9ui_stack.h"
#include "e9ui_theme.h"
#include "e9ui_text.h"
#include "ui.h"

#define AMIGA_BLIT_INFO_TITLE "ENGINE9000 DEBUGGER - BLIT INFO"
#define AMIGA_BLIT_INFO_LABEL_W 168
#define AMIGA_BLIT_INFO_MAX_OVERLAPS 8

typedef struct amiga_blit_info_overlay_body_state
{
    struct amiga_blit_info_state *ui;
} amiga_blit_info_overlay_body_state_t;

typedef struct amiga_blit_info_button_user
{
    struct amiga_blit_info_state *ui;
    size_t index;
} amiga_blit_info_button_user_t;

typedef enum amiga_blit_info_pending_action
{
    amiga_blit_info_pendingAction_none = 0,
    amiga_blit_info_pendingAction_showSource,
    amiga_blit_info_pendingAction_showRam
} amiga_blit_info_pending_action_t;

typedef struct amiga_blit_info_state
{
    e9ui_window_state_t windowState;
    e9ui_context_t ctx;
    SDL_Window *window;
    SDL_Renderer *renderer;
    e9ui_component_t *root;
    e9ui_component_t *triggerLink;
    e9ui_component_t *sizeText;
    e9ui_component_t *flagsText;
    e9ui_component_t *overlapRow;
    e9ui_component_t *overlapFlow;
    e9ui_component_t *channelARow;
    e9ui_component_t *channelBRow;
    e9ui_component_t *channelCRow;
    e9ui_component_t *channelDRow;
    e9ui_component_t *channelALink;
    e9ui_component_t *channelBLink;
    e9ui_component_t *channelCLink;
    e9ui_component_t *channelDLink;
    e9ui_component_t *overlapButtons[AMIGA_BLIT_INFO_MAX_OVERLAPS];
    amiga_blit_info_button_user_t overlapButtonUsers[AMIGA_BLIT_INFO_MAX_OVERLAPS];
    e9k_debug_ami_blitter_vis_point_t overlapHits[AMIGA_BLIT_INFO_MAX_OVERLAPS];
    size_t overlapCount;
    size_t overlapSelectedIndex;
    e9k_debug_ami_blitter_vis_point_t current;
    amiga_blit_info_pending_action_t pendingAction;
    uint32_t pendingAddr;
    uint32_t pendingRowBytes;
    int hasCurrent;
    int dirty;
} amiga_blit_info_state_t;

static amiga_blit_info_state_t amiga_blit_info_state = {
    .windowState.winX = E9UI_WINDOW_COORD_UNSET,
    .windowState.winY = E9UI_WINDOW_COORD_UNSET,
    .windowState.openMinWidthPx = 200,
    .windowState.openMinHeightPx = 100,
    .windowState.openCenterWhenNoSaved = 1,
};

static const aux_window_ops_t amiga_blit_info_auxWindowOps = {
    .setFocus = amiga_blit_info_setMainWindowFocused,
    .render = amiga_blit_info_render,
};

static e9ui_window_backend_t
amiga_blit_info_windowBackend(void)
{
    return e9ui_window_backend_overlay;
}

static e9ui_rect_t
amiga_blit_info_windowDefaultRect(const e9ui_context_t *ctx)
{
    e9ui_rect_t rect = {
        e9ui_scale_px(ctx, 120),
        e9ui_scale_px(ctx, 120),
        e9ui_scale_px(ctx, 687),
        e9ui_scale_px(ctx, 320)
    };
    return rect;
}

static int
amiga_blit_info_parseInt(const char *value, int *out)
{
    char *end = NULL;
    long parsed = 0;

    if (!value || !out) {
        return 0;
    }
    parsed = strtol(value, &end, 10);
    if (!end || end == value) {
        return 0;
    }
    *out = (int)parsed;
    return 1;
}

static void
amiga_blit_info_formatAddr(char *out, size_t cap, uint32_t addr)
{
    if (!out || cap == 0u) {
        return;
    }
    if (addr == 0u) {
        snprintf(out, cap, "-");
        return;
    }
    snprintf(out, cap, "%06x", (unsigned)(addr & 0x00ffffffu));
}

static void
amiga_blit_info_formatTrigger(char *out, size_t cap, const e9k_debug_ami_blitter_vis_point_t *info)
{
    char addr[32];

    if (!out || cap == 0u) {
        return;
    }
    if (!info || info->sourceAddr == 0u) {
        snprintf(out, cap, "-");
        return;
    }
    amiga_blit_info_formatAddr(addr, sizeof(addr), info->sourceAddr);
    snprintf(out, cap, "%s %s", info->sourceIsCopper ? "Copper" : "CPU", addr);
}

static void
amiga_blit_info_formatSize(char *out, size_t cap, const e9k_debug_ami_blitter_vis_point_t *info)
{
    if (!out || cap == 0u) {
        return;
    }
    if (!info) {
        snprintf(out, cap, "-");
        return;
    }
    snprintf(out,
             cap,
             "width=%u words  row=%u bytes  modulo=%d",
             (unsigned)info->widthWords,
             (unsigned)info->sourceRowBytes,
             (int)info->sourceModulo);
}

static void
amiga_blit_info_formatFlags(char *out, size_t cap, const e9k_debug_ami_blitter_vis_point_t *info)
{
    if (!out || cap == 0u) {
        return;
    }
    if (!info) {
        snprintf(out, cap, "-");
        return;
    }
    snprintf(out,
             cap,
             "line=%s  descending=%s  source=%s",
             info->lineMode ? "yes" : "no",
             info->sourceDescending ? "yes" : "no",
             info->sourceIsCopper ? "copper" : "cpu");
}

static void
amiga_blit_info_formatOverlapLabel(char *out, size_t cap, const e9k_debug_ami_blitter_vis_point_t *info, size_t index)
{
    char addr[32];

    if (!out || cap == 0u) {
        return;
    }
    if (!info) {
        snprintf(out, cap, "%u", (unsigned)(index + 1u));
        return;
    }
    if (info->sourceAddr != 0u) {
        amiga_blit_info_formatAddr(addr, sizeof(addr), info->sourceAddr);
        snprintf(out, cap, "%s %s", info->sourceIsCopper ? "CPR" : "CPU", addr);
        return;
    }
    if (info->blitId != 0u) {
        snprintf(out, cap, "#%u", (unsigned)info->blitId);
        return;
    }
    snprintf(out, cap, "%u", (unsigned)(index + 1u));
}

static void
amiga_blit_info_queueShowSource(amiga_blit_info_state_t *ui)
{
    if (!ui || !ui->hasCurrent || ui->current.sourceAddr == 0u) {
        e9ui_showTransientMessage("No source address");
        return;
    }
    ui->pendingAction = amiga_blit_info_pendingAction_showSource;
    ui->pendingAddr = ui->current.sourceAddr;
    ui->pendingRowBytes = 0u;
}

static void
amiga_blit_info_queueShowRam(amiga_blit_info_state_t *ui, uint32_t addr, uint32_t rowBytes)
{
    if (!ui || !ui->hasCurrent || addr == 0u) {
        e9ui_showTransientMessage("No RAM source address");
        return;
    }
    if (rowBytes == 0u) {
        rowBytes = (uint32_t)ui->current.widthWords * 2u;
    }
    ui->pendingAction = amiga_blit_info_pendingAction_showRam;
    ui->pendingAddr = addr;
    ui->pendingRowBytes = rowBytes;
}

static void
amiga_blit_info_flushPendingAction(amiga_blit_info_state_t *ui)
{
    amiga_blit_info_pending_action_t action = amiga_blit_info_pendingAction_none;
    uint32_t addr = 0u;
    uint32_t rowBytes = 0u;

    if (!ui || ui->pendingAction == amiga_blit_info_pendingAction_none) {
        return;
    }

    action = ui->pendingAction;
    addr = ui->pendingAddr;
    rowBytes = ui->pendingRowBytes;
    ui->pendingAction = amiga_blit_info_pendingAction_none;
    ui->pendingAddr = 0u;
    ui->pendingRowBytes = 0u;

    if (action == amiga_blit_info_pendingAction_showSource) {
        if (addr == 0u) {
            e9ui_showTransientMessage("No source address");
            return;
        }
        if (ui->current.sourceIsCopper) {
            ui_centerCprSourceOnAddress(addr);
        } else {
            ui_centerSourceOnAddress(addr);
        }
        return;
    }

    if (action == amiga_blit_info_pendingAction_showRam) {
        if (addr == 0u) {
            e9ui_showTransientMessage("No RAM source address");
            return;
        }
        if (!amiga_memview_isOpen()) {
            if (!amiga_memview_init()) {
                e9ui_showTransientMessage("Unable to open RAM view");
                return;
            }
        }
        amiga_memview_setViewIfOpen(addr, rowBytes, 1);
    }
}

static void
amiga_blit_info_selectHit(amiga_blit_info_state_t *ui, size_t index)
{
    if (!ui) {
        return;
    }
    if (index >= ui->overlapCount) {
        return;
    }
    ui->current = ui->overlapHits[index];
    ui->overlapSelectedIndex = index;
    ui->hasCurrent = 1;
    ui->dirty = 1;
}

static void
amiga_blit_info_syncState(amiga_blit_info_state_t *ui)
{
    char text[128];
    char buttonText[32];

    if (!ui || !ui->dirty) {
        return;
    }

    if (!ui->hasCurrent) {
        e9ui_link_setText(ui->triggerLink, "-");
        e9ui_text_setText(ui->sizeText, "-");
        e9ui_text_setText(ui->flagsText, "-");
        e9ui_link_setText(ui->channelALink, "-");
        e9ui_link_setText(ui->channelBLink, "-");
        e9ui_link_setText(ui->channelCLink, "-");
        e9ui_link_setText(ui->channelDLink, "-");
        e9ui_setHidden(ui->channelARow, 0);
        e9ui_setHidden(ui->channelBRow, 0);
        e9ui_setHidden(ui->channelCRow, 0);
        e9ui_setHidden(ui->channelDRow, 0);
        e9ui_setHidden(ui->overlapRow, 1);
        e9ui_setDisabled(ui->triggerLink, 1);
        e9ui_setDisabled(ui->channelALink, 1);
        e9ui_setDisabled(ui->channelBLink, 1);
        e9ui_setDisabled(ui->channelCLink, 1);
        e9ui_setDisabled(ui->channelDLink, 1);
        for (size_t i = 0u; i < AMIGA_BLIT_INFO_MAX_OVERLAPS; ++i) {
            if (ui->overlapButtons[i]) {
                e9ui_button_setLabel(ui->overlapButtons[i], "");
                e9ui_button_clearTheme(ui->overlapButtons[i]);
                e9ui_setDisabled(ui->overlapButtons[i], 0);
                e9ui_setHidden(ui->overlapButtons[i], 1);
            }
        }
        ui->dirty = 0;
        return;
    }

    amiga_blit_info_formatTrigger(text, sizeof(text), &ui->current);
    e9ui_link_setText(ui->triggerLink, text);

    amiga_blit_info_formatSize(text, sizeof(text), &ui->current);
    e9ui_text_setText(ui->sizeText, text);

    amiga_blit_info_formatFlags(text, sizeof(text), &ui->current);
    e9ui_text_setText(ui->flagsText, text);

    amiga_blit_info_formatAddr(text, sizeof(text), ui->current.channelAAddr);
    e9ui_link_setText(ui->channelALink, text);

    amiga_blit_info_formatAddr(text, sizeof(text), ui->current.channelBAddr);
    e9ui_link_setText(ui->channelBLink, text);

    amiga_blit_info_formatAddr(text, sizeof(text), ui->current.channelCAddr);
    e9ui_link_setText(ui->channelCLink, text);

    amiga_blit_info_formatAddr(text, sizeof(text), ui->current.channelDAddr);
    e9ui_link_setText(ui->channelDLink, text);

    e9ui_setHidden(ui->overlapRow, ui->overlapCount <= 1u);
    for (size_t i = 0u; i < AMIGA_BLIT_INFO_MAX_OVERLAPS; ++i) {
        if (!ui->overlapButtons[i]) {
            continue;
        }
        if (i < ui->overlapCount) {
            amiga_blit_info_formatOverlapLabel(buttonText, sizeof(buttonText), &ui->overlapHits[i], i);
            e9ui_button_setLabel(ui->overlapButtons[i], buttonText);
            e9ui_setHidden(ui->overlapButtons[i], 0);
            if (i == ui->overlapSelectedIndex) {
                e9ui_button_setTheme(ui->overlapButtons[i], e9ui_theme_button_preset_profile_active());
                e9ui_setDisabled(ui->overlapButtons[i], 1);
            } else {
                e9ui_button_clearTheme(ui->overlapButtons[i]);
                e9ui_setDisabled(ui->overlapButtons[i], 0);
            }
        } else {
            e9ui_button_setLabel(ui->overlapButtons[i], "");
            e9ui_button_clearTheme(ui->overlapButtons[i]);
            e9ui_setDisabled(ui->overlapButtons[i], 0);
            e9ui_setHidden(ui->overlapButtons[i], 1);
        }
    }
    e9ui_setDisabled(ui->triggerLink, ui->current.sourceAddr == 0u);
    e9ui_setHidden(ui->channelARow, (ui->current.sourceChannelsMask & 0x8u) == 0u);
    e9ui_setHidden(ui->channelBRow, (ui->current.sourceChannelsMask & 0x4u) == 0u);
    e9ui_setHidden(ui->channelCRow, (ui->current.sourceChannelsMask & 0x2u) == 0u);
    e9ui_setHidden(ui->channelDRow, (ui->current.sourceChannelsMask & 0x1u) == 0u);
    e9ui_setDisabled(ui->channelALink, ui->current.channelAAddr == 0u);
    e9ui_setDisabled(ui->channelBLink, ui->current.channelBAddr == 0u);
    e9ui_setDisabled(ui->channelCLink, ui->current.channelCAddr == 0u);
    e9ui_setDisabled(ui->channelDLink, ui->current.channelDAddr == 0u);
    ui->dirty = 0;
}

static void
amiga_blit_info_selectOverlap(e9ui_context_t *ctx, void *user)
{
    amiga_blit_info_button_user_t *buttonUser = (amiga_blit_info_button_user_t *)user;

    (void)ctx;
    if (!buttonUser || !buttonUser->ui) {
        return;
    }
    amiga_blit_info_selectHit(buttonUser->ui, buttonUser->index);
    amiga_blit_info_syncState(buttonUser->ui);
}

static void
amiga_blit_info_showSource(e9ui_context_t *ctx, void *user)
{
    amiga_blit_info_state_t *ui = user;

    (void)ctx;
    amiga_blit_info_queueShowSource(ui);
}

static void
amiga_blit_info_showChannelRam(amiga_blit_info_state_t *ui, uint32_t addr, uint32_t rowBytes)
{
    amiga_blit_info_queueShowRam(ui, addr, rowBytes);
}

static void
amiga_blit_info_showChannelA(e9ui_context_t *ctx, void *user)
{
    amiga_blit_info_state_t *ui = (amiga_blit_info_state_t *)user;
    int rowBytes = 0;

    (void)ctx;
    if (!ui) {
        return;
    }
    rowBytes = (int)ui->current.widthWords * 2 + (int)ui->current.channelAModulo;
    if (rowBytes < 0) {
        rowBytes = -rowBytes;
    }
    amiga_blit_info_showChannelRam(ui, ui->current.channelAAddr, (uint32_t)rowBytes);
}

static void
amiga_blit_info_showChannelB(e9ui_context_t *ctx, void *user)
{
    amiga_blit_info_state_t *ui = (amiga_blit_info_state_t *)user;
    int rowBytes = 0;

    (void)ctx;
    if (!ui) {
        return;
    }
    rowBytes = (int)ui->current.widthWords * 2 + (int)ui->current.channelBModulo;
    if (rowBytes < 0) {
        rowBytes = -rowBytes;
    }
    amiga_blit_info_showChannelRam(ui, ui->current.channelBAddr, (uint32_t)rowBytes);
}

static void
amiga_blit_info_showChannelC(e9ui_context_t *ctx, void *user)
{
    amiga_blit_info_state_t *ui = (amiga_blit_info_state_t *)user;
    int rowBytes = 0;

    (void)ctx;
    if (!ui) {
        return;
    }
    rowBytes = (int)ui->current.widthWords * 2 + (int)ui->current.channelCModulo;
    if (rowBytes < 0) {
        rowBytes = -rowBytes;
    }
    amiga_blit_info_showChannelRam(ui, ui->current.channelCAddr, (uint32_t)rowBytes);
}

static void
amiga_blit_info_showChannelD(e9ui_context_t *ctx, void *user)
{
    amiga_blit_info_state_t *ui = (amiga_blit_info_state_t *)user;
    int rowBytes = 0;

    (void)ctx;
    if (!ui) {
        return;
    }
    rowBytes = (int)ui->current.widthWords * 2 + (int)ui->current.channelDModulo;
    if (rowBytes < 0) {
        rowBytes = -rowBytes;
    }
    amiga_blit_info_showChannelRam(ui, ui->current.channelDAddr, (uint32_t)rowBytes);
}

static e9ui_component_t *
amiga_blit_info_makeRow(const char *label, e9ui_component_t *valueComp)
{
    e9ui_component_t *row = NULL;
    e9ui_component_t *labelText = NULL;
    e9ui_component_t *labelBox = NULL;
    e9ui_component_t *valueBox = NULL;
    int labelW = e9ui_scale_px(&e9ui->ctx, AMIGA_BLIT_INFO_LABEL_W);
    int gap = e9ui_scale_px(&e9ui->ctx, 8);

    row = e9ui_hstack_make();
    labelText = e9ui_text_make(label ? label : "");
    labelBox = e9ui_box_make(labelText);
    valueBox = e9ui_box_make(valueComp);
    if (!row || !labelText || !labelBox || !valueBox || !valueComp) {
        return NULL;
    }

    e9ui_box_setPaddingY(labelBox, e9ui_scale_px(&e9ui->ctx, 2));
    e9ui_box_setPaddingY(valueBox, e9ui_scale_px(&e9ui->ctx, 2));
    e9ui_hstack_addFixed(row, labelBox, labelW);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(gap), gap);
    e9ui_hstack_addFlex(row, valueBox);
    return row;
}

static e9ui_component_t *
amiga_blit_info_buildRoot(amiga_blit_info_state_t *ui)
{
    e9ui_component_t *root = NULL;
    e9ui_component_t *content = NULL;
    e9ui_component_t *overlapFlow = NULL;
    e9ui_component_t *overlapLabel = NULL;
    e9ui_component_t *overlapLabelBox = NULL;
    e9ui_component_t *triggerLink = NULL;
    e9ui_component_t *channelALink = NULL;
    e9ui_component_t *channelBLink = NULL;
    e9ui_component_t *channelCLink = NULL;
    e9ui_component_t *channelDLink = NULL;
    e9ui_component_t *sizeText = NULL;
    e9ui_component_t *flagsText = NULL;
    e9ui_component_t *row = NULL;

    if (!ui) {
        return NULL;
    }

    content = e9ui_stack_makeVertical();
    root = e9ui_box_make(content);
    if (!root || !content) {
        return NULL;
    }
    e9ui_box_setPadding(root, 12);

    overlapFlow = e9ui_flow_make();
    overlapLabel = e9ui_text_make("History");
    overlapLabelBox = e9ui_box_make(overlapLabel);
    triggerLink = e9ui_link_make("-", amiga_blit_info_showSource, ui);
    channelALink = e9ui_link_make("-", amiga_blit_info_showChannelA, ui);
    channelBLink = e9ui_link_make("-", amiga_blit_info_showChannelB, ui);
    channelCLink = e9ui_link_make("-", amiga_blit_info_showChannelC, ui);
    channelDLink = e9ui_link_make("-", amiga_blit_info_showChannelD, ui);
    sizeText = e9ui_text_make("-");
    flagsText = e9ui_text_make("-");
    if (!overlapFlow || !overlapLabel || !overlapLabelBox ||
        !triggerLink || !channelALink || !channelBLink || !channelCLink || !channelDLink ||
        !sizeText || !flagsText) {
        return NULL;
    }
    e9ui_flow_setPadding(overlapFlow, 0);
    e9ui_flow_setSpacing(overlapFlow, 6);
    e9ui_flow_setWrap(overlapFlow, 1);
    ui->triggerLink = triggerLink;
    ui->channelALink = channelALink;
    ui->channelBLink = channelBLink;
    ui->channelCLink = channelCLink;
    ui->channelDLink = channelDLink;
    ui->sizeText = sizeText;
    ui->flagsText = flagsText;
    ui->overlapFlow = overlapFlow;

    row = e9ui_hstack_make();
    if (!row) {
        return NULL;
    }
    e9ui_box_setPaddingY(overlapLabelBox, e9ui_scale_px(&e9ui->ctx, 2));
    e9ui_hstack_addFixed(row, overlapLabelBox, e9ui_scale_px(&e9ui->ctx, AMIGA_BLIT_INFO_LABEL_W));
    e9ui_hstack_addFixed(row, e9ui_spacer_make(e9ui_scale_px(&e9ui->ctx, 8)), e9ui_scale_px(&e9ui->ctx, 8));
    e9ui_hstack_addFlex(row, overlapFlow);
    ui->overlapRow = row;
    e9ui_stack_addFixed(content, row);
    for (size_t i = 0u; i < AMIGA_BLIT_INFO_MAX_OVERLAPS; ++i) {
        e9ui_component_t *button = e9ui_button_make("", amiga_blit_info_selectOverlap, &ui->overlapButtonUsers[i]);

        if (!button) {
            return NULL;
        }
        e9ui_button_setMini(button, 1);
        ui->overlapButtonUsers[i].ui = ui;
        ui->overlapButtonUsers[i].index = i;
        ui->overlapButtons[i] = button;
        e9ui_setHidden(button, 1);
        e9ui_flow_add(overlapFlow, button);
    }

    row = amiga_blit_info_makeRow("BLTSIZE Write", triggerLink);
    if (!row) {
        return NULL;
    }
    e9ui_stack_addFixed(content, row);
    row = amiga_blit_info_makeRow("Size", sizeText);
    if (!row) {
        return NULL;
    }
    e9ui_stack_addFixed(content, row);
    row = amiga_blit_info_makeRow("Flags", flagsText);
    if (!row) {
        return NULL;
    }
    e9ui_stack_addFixed(content, row);
    row = amiga_blit_info_makeRow("Channel A", channelALink);
    if (!row) {
        return NULL;
    }
    ui->channelARow = row;
    e9ui_stack_addFixed(content, row);
    row = amiga_blit_info_makeRow("Channel B", channelBLink);
    if (!row) {
        return NULL;
    }
    ui->channelBRow = row;
    e9ui_stack_addFixed(content, row);
    row = amiga_blit_info_makeRow("Channel C", channelCLink);
    if (!row) {
        return NULL;
    }
    ui->channelCRow = row;
    e9ui_stack_addFixed(content, row);
    row = amiga_blit_info_makeRow("Channel D", channelDLink);
    if (!row) {
        return NULL;
    }
    ui->channelDRow = row;
    e9ui_stack_addFixed(content, row);

    return root;
}

static int
amiga_blit_info_overlayBodyPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)ctx;
    (void)availW;
    return 0;
}

static void
amiga_blit_info_overlayBodyLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    amiga_blit_info_overlay_body_state_t *state = NULL;
    amiga_blit_info_state_t *ui = NULL;

    if (!self || !ctx || !self->state) {
        return;
    }
    self->bounds = bounds;
    state = (amiga_blit_info_overlay_body_state_t*)self->state;
    ui = state ? state->ui : NULL;
    if (!ui || !ui->root || !ui->root->layout) {
        return;
    }
    ui->ctx = *ctx;
    ui->ctx.window = ctx->window;
    ui->ctx.renderer = ctx->renderer;
    ui->ctx.font = e9ui->ctx.font;
    ui->ctx.winW = bounds.w;
    ui->ctx.winH = bounds.h;
    ui->ctx.focusRoot = ui->root;
    ui->ctx.focusFullscreen = NULL;
    ui->root->layout(ui->root, &ui->ctx, bounds);
}

static void
amiga_blit_info_overlayBodyRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    amiga_blit_info_overlay_body_state_t *state = NULL;
    amiga_blit_info_state_t *ui = NULL;

    if (!self || !ctx || !self->state) {
        return;
    }
    state = (amiga_blit_info_overlay_body_state_t*)self->state;
    ui = state ? state->ui : NULL;
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
    ui->ctx.focusFullscreen = NULL;
    amiga_blit_info_flushPendingAction(ui);
    amiga_blit_info_syncState(ui);
    ui->root->render(ui->root, &ui->ctx);
}

static e9ui_component_t *
amiga_blit_info_makeOverlayBodyHost(amiga_blit_info_state_t *ui)
{
    e9ui_component_t *host = NULL;
    amiga_blit_info_overlay_body_state_t *state = NULL;

    if (!ui || !ui->root) {
        return NULL;
    }
    host = (e9ui_component_t *)alloc_calloc(1, sizeof(*host));
    state = (amiga_blit_info_overlay_body_state_t *)alloc_calloc(1, sizeof(*state));
    if (!host || !state) {
        alloc_free(host);
        alloc_free(state);
        return NULL;
    }
    state->ui = ui;
    host->name = "amiga_blit_info_overlay_body";
    host->state = state;
    host->preferredHeight = amiga_blit_info_overlayBodyPreferredHeight;
    host->layout = amiga_blit_info_overlayBodyLayout;
    host->render = amiga_blit_info_overlayBodyRender;
    e9ui_child_add(host, ui->root, alloc_strdup("amiga_blit_info_root"));
    return host;
}

static void
amiga_blit_info_overlayWindowCloseRequested(e9ui_window_t *window, void *user)
{
    (void)window;
    (void)user;
    amiga_blit_info_shutdown();
}

int
amiga_blit_info_init(void)
{
    amiga_blit_info_state_t *ui = &amiga_blit_info_state;
    e9ui_rect_t rect;
    e9ui_component_t *overlayBodyHost = NULL;

    if (ui->windowState.open) {
        return 1;
    }
    ui->ctx = e9ui->ctx;
    ui->windowState.windowHost = e9ui_windowCreate(amiga_blit_info_windowBackend());
    if (!ui->windowState.windowHost) {
        return 0;
    }
    ui->root = amiga_blit_info_buildRoot(ui);
    if (!ui->root) {
        e9ui_windowDestroy(ui->windowState.windowHost);
        ui->windowState.windowHost = NULL;
        return 0;
    }
    rect = e9ui_windowResolveStateOpenRect(&e9ui->ctx,
                                           amiga_blit_info_windowDefaultRect(&e9ui->ctx),
                                           &ui->windowState);
    overlayBodyHost = amiga_blit_info_makeOverlayBodyHost(ui);
    if (!overlayBodyHost) {
        e9ui_windowDestroy(ui->windowState.windowHost);
        ui->windowState.windowHost = NULL;
        ui->root = NULL;
        return 0;
    }
    e9ui_windowOpen(ui->windowState.windowHost,
                    AMIGA_BLIT_INFO_TITLE,
                    rect,
                    overlayBodyHost,
                    amiga_blit_info_overlayWindowCloseRequested,
                    ui,
                    &e9ui->ctx);
    ui->window = e9ui->ctx.window;
    ui->renderer = e9ui->ctx.renderer;
    ui->ctx = e9ui->ctx;
    ui->windowState.open = 1;
    aux_window_register(&amiga_blit_info_auxWindowOps, ui);
    ui->dirty = 1;
    amiga_blit_info_syncState(ui);
    return 1;
}

void
amiga_blit_info_shutdown(void)
{
    amiga_blit_info_state_t *ui = &amiga_blit_info_state;

    if (!ui->windowState.open) {
        return;
    }
    aux_window_unregister(&amiga_blit_info_auxWindowOps, ui);
    (void)e9ui_windowCaptureStateRectSnapshot(&ui->windowState, &e9ui->ctx);
    config_saveConfig();
    if (ui->windowState.windowHost) {
        e9ui_windowDestroy(ui->windowState.windowHost);
        ui->windowState.windowHost = NULL;
    }
    ui->window = NULL;
    ui->renderer = NULL;
    ui->root = NULL;
    ui->windowState.open = 0;
    ui->pendingAction = amiga_blit_info_pendingAction_none;
    ui->pendingAddr = 0u;
    ui->pendingRowBytes = 0u;
    memset(&ui->ctx, 0, sizeof(ui->ctx));
}

void
amiga_blit_info_toggle(void)
{
    if (amiga_blit_info_isOpen()) {
        amiga_blit_info_shutdown();
        return;
    }
    (void)amiga_blit_info_init();
}

int
amiga_blit_info_isOpen(void)
{
    return amiga_blit_info_state.windowState.open ? 1 : 0;
}

void
amiga_blit_info_show(const e9k_debug_ami_blitter_vis_point_t *info)
{
    amiga_blit_info_showHits(info, info ? 1u : 0u, 0u);
}

void
amiga_blit_info_showHits(const e9k_debug_ami_blitter_vis_point_t *hits, size_t hitCount, size_t selectedIndex)
{
    amiga_blit_info_state_t *ui = &amiga_blit_info_state;

    if (hitCount > AMIGA_BLIT_INFO_MAX_OVERLAPS) {
        hitCount = AMIGA_BLIT_INFO_MAX_OVERLAPS;
    }
    ui->overlapCount = hitCount;
    if (hitCount > 0u && selectedIndex >= hitCount) {
        selectedIndex = 0u;
    }
    if (hits && hitCount > 0u) {
        memcpy(ui->overlapHits, hits, hitCount * sizeof(*hits));
        if (hitCount < AMIGA_BLIT_INFO_MAX_OVERLAPS) {
            memset(ui->overlapHits + hitCount,
                   0,
                   (AMIGA_BLIT_INFO_MAX_OVERLAPS - hitCount) * sizeof(*ui->overlapHits));
        }
        amiga_blit_info_selectHit(ui, selectedIndex);
    } else {
        memset(ui->overlapHits, 0, sizeof(ui->overlapHits));
        memset(&ui->current, 0, sizeof(ui->current));
        ui->overlapSelectedIndex = 0u;
        ui->hasCurrent = 0;
        ui->dirty = 1;
    }
    if (!ui->windowState.open) {
        (void)amiga_blit_info_init();
        return;
    }
    amiga_blit_info_syncState(ui);
}

void
amiga_blit_info_setMainWindowFocused(int focused)
{
    (void)focused;
}

void
amiga_blit_info_render(void)
{
    amiga_blit_info_state_t *ui = &amiga_blit_info_state;

    if (!ui->windowState.open) {
        return;
    }
    if (e9ui_windowCaptureStateRectChanged(&ui->windowState, &e9ui->ctx)) {
        config_saveConfig();
    }
}

void
amiga_blit_info_persistConfig(FILE *file)
{
    amiga_blit_info_state_t *ui = &amiga_blit_info_state;

    if (!file) {
        return;
    }
    e9ui_windowPersistStateRect(file, "comp.amiga_blit_info", &ui->windowState, &e9ui->ctx);
}

int
amiga_blit_info_loadConfigProperty(const char *prop, const char *value)
{
    amiga_blit_info_state_t *ui = &amiga_blit_info_state;
    int intValue = 0;

    if (!prop || !value) {
        return 0;
    }
    if (strcmp(prop, "win_x") == 0) {
        if (!amiga_blit_info_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winX = intValue;
    } else if (strcmp(prop, "win_y") == 0) {
        if (!amiga_blit_info_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winY = intValue;
    } else if (strcmp(prop, "win_w") == 0) {
        if (!amiga_blit_info_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winW = intValue;
    } else if (strcmp(prop, "win_h") == 0) {
        if (!amiga_blit_info_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winH = intValue;
    } else {
        return 0;
    }
    return 1;
}
