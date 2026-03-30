/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>

#include "alloc.h"
#include "amiga_custom_ui_internal.h"
#include "config.h"
#include "emu_ami.h"
#include "e9ui_labeled_textbox.h"
#include "e9ui_scroll.h"
#include "e9ui_seek_bar.h"
#include "e9ui_text.h"
#include "e9ui_textbox.h"
#include "libretro_host.h"

amiga_custom_ui_state_t amiga_custom_ui_state = {
    .windowState.winX = E9UI_WINDOW_COORD_UNSET,
    .windowState.winY = E9UI_WINDOW_COORD_UNSET,
    .windowState.openMinWidthPx = 420,
    .windowState.openMinHeightPx = 420,
    .windowState.openCenterWhenNoSaved = 1,
    .blitterEnabled = 1,
    .copperVisualiserEnabled = 0,
    .paletteVisualiserEnabled = 0,
    .blitterDebugEnabled = 0,
    .spriteVisEnabled = 0,
    .blitterVisMode = AMIGA_CUSTOM_UI_BLITTER_VIS_MODE_COLLECT,
    .blitterVisDecay = 5,
    .dmaStatsEnabled = 0,
    .estimateFpsEnabled = 0,
    .copperLimitEnabled = 0,
    .copperLimitStart = 52,
    .copperLimitEnd = 308,
    .bplptrBlockAllEnabled = 0,
    .bplptrBlockEnabled = { 0, 0, 0, 0, 0, 0 },
    .bplptrLineLimitStart = 52,
    .bplptrLineLimitEnd = 308,
    .bplcon1DelayScrollEnabled = 1,
    .spritesEnabled = 1,
    .spriteEnabled = { 1, 1, 1, 1, 1, 1, 1, 1 },
    .bitplanesEnabled = 1,
    .bitplaneEnabled = { 1, 1, 1, 1, 1, 1, 1, 1 },
    .audiosEnabled = 1,
    .audioEnabled = { 1, 1, 1, 1 }
};

static const aux_window_ops_t amiga_custom_ui_auxWindowOps = {
    .setFocus = amiga_custom_ui_setMainWindowFocused,
    .render = amiga_custom_ui_render,
};

static int
amiga_custom_ui_parseInt(const char *value, int *out)
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

static e9ui_window_backend_t
amiga_custom_ui_windowBackend(void)
{
    return e9ui_window_backend_overlay;
}

static e9ui_rect_t
amiga_custom_ui_windowDefaultRect(const e9ui_context_t *ctx)
{
    e9ui_rect_t rect = {
        e9ui_scale_px(ctx, 64),
        e9ui_scale_px(ctx, 64),
        e9ui_scale_px(ctx, 560),
        e9ui_scale_px(ctx, 560)
    };
    return rect;
}

static void
amiga_custom_ui_applyAllOptions(void)
{
    amiga_custom_ui_blitter_applyOption();
    amiga_custom_ui_blitter_applyVisDecayOption();
    amiga_custom_ui_blitter_applyVisModeOption();
    amiga_custom_ui_sprite_applyVisOption();
    amiga_custom_ui_bitplane_applyPaletteVisOption();
    amiga_custom_ui_bitplane_applyBplcon1DelayScrollOption();
    amiga_custom_ui_copper_applyLimitEnabledOption();
    amiga_custom_ui_copper_applyLimitStartOption();
    amiga_custom_ui_copper_applyLimitEndOption();
    amiga_custom_ui_bplptr_applyBlockAllOption();
    amiga_custom_ui_bplptr_applyLineLimitStartOption();
    amiga_custom_ui_bplptr_applyLineLimitEndOption();
    for (int bplptrIndex = 0; bplptrIndex < AMIGA_CUSTOM_UI_AMIGA_BPLPTR_COUNT; ++bplptrIndex) {
        amiga_custom_ui_bplptr_applyBlockOption(bplptrIndex);
    }
    for (int spriteIndex = 0; spriteIndex < AMIGA_CUSTOM_UI_AMIGA_SPRITE_COUNT; ++spriteIndex) {
        amiga_custom_ui_sprite_applyOption(spriteIndex);
    }
    for (int bitplaneIndex = 0; bitplaneIndex < AMIGA_CUSTOM_UI_AMIGA_BITPLANE_COUNT; ++bitplaneIndex) {
        amiga_custom_ui_bitplane_applyOption(bitplaneIndex);
    }
    for (int audioChannelIndex = 0; audioChannelIndex < AMIGA_CUSTOM_UI_AMIGA_AUDIO_CHANNEL_COUNT; ++audioChannelIndex) {
        amiga_custom_ui_audio_applyOption(audioChannelIndex);
    }
}

static void
amiga_custom_ui_syncEstimateFpsDisplay(amiga_custom_ui_state_t *ui);

void
amiga_custom_ui_estimateFpsChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    amiga_custom_ui_state_t *ui = (amiga_custom_ui_state_t *)user;
    if (!ui) {
        return;
    }
    ui->estimateFpsEnabled = selected ? 1 : 0;
    libretro_host_setEstimateFpsEnabled(ui->estimateFpsEnabled);
    amiga_custom_ui_syncEstimateFpsDisplay(ui);
}
static e9ui_component_t *
amiga_custom_ui_buildRoot(amiga_custom_ui_state_t *ui)
{
    e9ui_component_t *rootStack = e9ui_stack_makeVertical();
    e9ui_stack_addFixed(rootStack, e9ui_vspacer_make(12));
    e9ui_component_t *columns = e9ui_hstack_make();
    e9ui_component_t *leftColumn = e9ui_stack_makeVertical();
    e9ui_component_t *cbBplcon1DelayScroll = e9ui_checkbox_make("BPLCON1 Scroll",
                                                                 ui->bplcon1DelayScrollEnabled,
                                                                 amiga_custom_ui_bitplane_bplcon1DelayScrollChanged,
                                                                 ui);
    e9ui_checkbox_setLeftMargin(cbBplcon1DelayScroll, 12);
    e9ui_stack_addFixed(leftColumn, cbBplcon1DelayScroll);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(8));

    e9ui_component_t *cbSprites = e9ui_checkbox_make("Sprites",
                                                     ui->spritesEnabled,
                                                     amiga_custom_ui_sprite_masterChanged,
                                                     ui);
    ui->spritesCheckbox = cbSprites;
    e9ui_checkbox_setLeftMargin(cbSprites, 12);
    e9ui_stack_addFixed(leftColumn, cbSprites);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(6));

    e9ui_component_t *spriteColumns = e9ui_hstack_make();
    e9ui_component_t *spriteColumnLeft = e9ui_stack_makeVertical();
    e9ui_component_t *spriteColumnRight = e9ui_stack_makeVertical();

    e9ui_hstack_addFlex(spriteColumns, spriteColumnLeft);
    e9ui_hstack_addFixed(spriteColumns, e9ui_spacer_make(6), 6);
    e9ui_hstack_addFlex(spriteColumns, spriteColumnRight);

    for (int spriteIndex = 0; spriteIndex < AMIGA_CUSTOM_UI_AMIGA_SPRITE_COUNT; ++spriteIndex) {
        char label[32];
        snprintf(label, sizeof(label), "Spr %d", spriteIndex);
        ui->spriteCb[spriteIndex].ui = ui;
        ui->spriteCb[spriteIndex].spriteIndex = spriteIndex;
        e9ui_component_t *cbSprite = e9ui_checkbox_make(label,
                                                        ui->spriteEnabled[spriteIndex],
                                                        amiga_custom_ui_sprite_changed,
                                                        &ui->spriteCb[spriteIndex]);
        ui->spriteCheckboxes[spriteIndex] = cbSprite;
        if ((spriteIndex & 1) == 0) {
            e9ui_stack_addFixed(spriteColumnLeft, cbSprite);
        } else {
            e9ui_stack_addFixed(spriteColumnRight, cbSprite);
        }
    }
    e9ui_component_t *spriteColumnsInsetRow = amiga_custom_ui_common_insetRowMake(spriteColumns, 24, 0);
    e9ui_stack_addFixed(leftColumn, spriteColumnsInsetRow);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(8));

    e9ui_component_t *cbBitplanes = e9ui_checkbox_make("Bitplanes",
                                                       ui->bitplanesEnabled,
                                                       amiga_custom_ui_bitplane_masterChanged,
                                                       ui);
    ui->bitplanesCheckbox = cbBitplanes;
    e9ui_checkbox_setLeftMargin(cbBitplanes, 12);
    e9ui_stack_addFixed(leftColumn, cbBitplanes);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(6));

    e9ui_component_t *bitplaneColumns = e9ui_hstack_make();
    e9ui_component_t *bitplaneColumnLeft = e9ui_stack_makeVertical();
    e9ui_component_t *bitplaneColumnRight = e9ui_stack_makeVertical();

    e9ui_hstack_addFlex(bitplaneColumns, bitplaneColumnLeft);
    e9ui_hstack_addFixed(bitplaneColumns, e9ui_spacer_make(6), 6);
    e9ui_hstack_addFlex(bitplaneColumns, bitplaneColumnRight);

    for (int bitplaneIndex = 0; bitplaneIndex < AMIGA_CUSTOM_UI_AMIGA_BITPLANE_COUNT; ++bitplaneIndex) {
        char label[32];
        snprintf(label, sizeof(label), "Bpl %d", bitplaneIndex);
        ui->bitplaneCb[bitplaneIndex].ui = ui;
        ui->bitplaneCb[bitplaneIndex].bitplaneIndex = bitplaneIndex;
        e9ui_component_t *cbBitplane = e9ui_checkbox_make(label,
                                                          ui->bitplaneEnabled[bitplaneIndex],
                                                          amiga_custom_ui_bitplane_changed,
                                                          &ui->bitplaneCb[bitplaneIndex]);
        ui->bitplaneCheckboxes[bitplaneIndex] = cbBitplane;
        if ((bitplaneIndex & 1) == 0) {
            e9ui_stack_addFixed(bitplaneColumnLeft, cbBitplane);
        } else {
            e9ui_stack_addFixed(bitplaneColumnRight, cbBitplane);
        }
    }
    e9ui_component_t *bitplaneColumnsInsetRow = amiga_custom_ui_common_insetRowMake(bitplaneColumns, 24, 0);
    e9ui_stack_addFixed(leftColumn, bitplaneColumnsInsetRow);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(8));

    e9ui_component_t *cbBplptrBlockAll = e9ui_checkbox_make("Bitplane Ptr Block",
                                                            ui->bplptrBlockAllEnabled,
                                                            amiga_custom_ui_bplptr_blockAllChanged,
                                                            ui);
    ui->bplptrBlockAllCheckbox = cbBplptrBlockAll;
    e9ui_checkbox_setLeftMargin(cbBplptrBlockAll, 12);
    e9ui_stack_addFixed(leftColumn, cbBplptrBlockAll);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(6));

    char bplptrLineLimitStartText[16];
    snprintf(bplptrLineLimitStartText, sizeof(bplptrLineLimitStartText), "%d", amiga_custom_ui_common_clampCopperLine(ui->bplptrLineLimitStart));
    e9ui_component_t *bplptrLineLimitStartRow = e9ui_labeled_textbox_make("Start",
                                                                           78,
                                                                           0,
                                                                           amiga_custom_ui_bplptr_lineLimitStartChanged,
                                                                           ui);

    e9ui_labeled_textbox_setText(bplptrLineLimitStartRow, bplptrLineLimitStartText);
    e9ui_component_t *bplptrLineLimitStartTextbox = e9ui_labeled_textbox_getTextbox(bplptrLineLimitStartRow);
    e9ui_textbox_setNumericOnly(bplptrLineLimitStartTextbox, 1);
    e9ui_textbox_setKeyHandler(bplptrLineLimitStartTextbox, amiga_custom_ui_bplptr_lineLimitStartTextboxKey, ui);

    ui->bplptrLineLimitStartRow = bplptrLineLimitStartRow;
    ui->bplptrLineLimitStartTextbox = bplptrLineLimitStartTextbox;
    e9ui_component_t *bplptrLineLimitStartInsetRow = amiga_custom_ui_common_insetRowMake(bplptrLineLimitStartRow, 0, 14);

    e9ui_stack_addFixed(leftColumn, bplptrLineLimitStartInsetRow);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(6));

    char bplptrLineLimitEndText[16];
    snprintf(bplptrLineLimitEndText, sizeof(bplptrLineLimitEndText), "%d", amiga_custom_ui_common_clampCopperLine(ui->bplptrLineLimitEnd));
    e9ui_component_t *bplptrLineLimitEndRow = e9ui_labeled_textbox_make("End",
                                                                         78,
                                                                         0,
                                                                         amiga_custom_ui_bplptr_lineLimitEndChanged,
                                                                         ui);
    
    e9ui_labeled_textbox_setText(bplptrLineLimitEndRow, bplptrLineLimitEndText);
    e9ui_component_t *bplptrLineLimitEndTextbox = e9ui_labeled_textbox_getTextbox(bplptrLineLimitEndRow);
    
    e9ui_textbox_setNumericOnly(bplptrLineLimitEndTextbox, 1);
    e9ui_textbox_setKeyHandler(bplptrLineLimitEndTextbox, amiga_custom_ui_bplptr_lineLimitEndTextboxKey, ui);

    ui->bplptrLineLimitEndRow = bplptrLineLimitEndRow;
    ui->bplptrLineLimitEndTextbox = bplptrLineLimitEndTextbox;
    e9ui_component_t *bplptrLineLimitEndInsetRow = amiga_custom_ui_common_insetRowMake(bplptrLineLimitEndRow, 0, 14);

    e9ui_stack_addFixed(leftColumn, bplptrLineLimitEndInsetRow);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(6));

    for (int bplptrIndex = 0; bplptrIndex < AMIGA_CUSTOM_UI_AMIGA_BPLPTR_COUNT; ++bplptrIndex) {
        char label[32];
        snprintf(label, sizeof(label), "BPL%dPT", bplptrIndex + 1);
        ui->bplptrCb[bplptrIndex].ui = ui;
        ui->bplptrCb[bplptrIndex].bplptrIndex = bplptrIndex;
        e9ui_component_t *cbBplptrBlock = e9ui_checkbox_make(label,
                                                             ui->bplptrBlockEnabled[bplptrIndex],
                                                             amiga_custom_ui_bplptr_blockChanged,
                                                             &ui->bplptrCb[bplptrIndex]);

        ui->bplptrBlockCheckboxes[bplptrIndex] = cbBplptrBlock;
        e9ui_checkbox_setLeftMargin(cbBplptrBlock, 28);
        e9ui_stack_addFixed(leftColumn, cbBplptrBlock);
    }
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(8));

    e9ui_component_t *cbCopperLimit = e9ui_checkbox_make("Copper Block",
                                                         ui->copperLimitEnabled,
                                                         amiga_custom_ui_copper_limitChanged,
                                                         ui);

    ui->copperLimitCheckbox = cbCopperLimit;
    e9ui_checkbox_setLeftMargin(cbCopperLimit, 12);
    e9ui_stack_addFixed(leftColumn, cbCopperLimit);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(6));

    char copperLimitStartText[16];
    snprintf(copperLimitStartText, sizeof(copperLimitStartText), "%d", amiga_custom_ui_common_clampCopperLine(ui->copperLimitStart));
    e9ui_component_t *copperLimitStartRow = e9ui_labeled_textbox_make("Start",
                                                                       78,
                                                                       0,
                                                                       amiga_custom_ui_copper_limitStartChanged,
                                                                       ui);
    e9ui_labeled_textbox_setText(copperLimitStartRow, copperLimitStartText);
    e9ui_component_t *copperLimitStartTextbox = e9ui_labeled_textbox_getTextbox(copperLimitStartRow);
    e9ui_textbox_setNumericOnly(copperLimitStartTextbox, 1);
    e9ui_textbox_setKeyHandler(copperLimitStartTextbox, amiga_custom_ui_copper_limitStartTextboxKey, ui);

    ui->copperLimitStartRow = copperLimitStartRow;
    ui->copperLimitStartTextbox = copperLimitStartTextbox;
    e9ui_component_t *copperLimitStartInsetRow = amiga_custom_ui_common_insetRowMake(copperLimitStartRow, 0, 14);

    e9ui_stack_addFixed(leftColumn, copperLimitStartInsetRow);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(6));

    char copperLimitEndText[16];
    snprintf(copperLimitEndText, sizeof(copperLimitEndText), "%d", amiga_custom_ui_common_clampCopperLine(ui->copperLimitEnd));
    e9ui_component_t *copperLimitEndRow = e9ui_labeled_textbox_make("End",
                                                                     78,
                                                                     0,
                                                                     amiga_custom_ui_copper_limitEndChanged,
                                                                     ui);
    
    e9ui_labeled_textbox_setText(copperLimitEndRow, copperLimitEndText);
    e9ui_component_t *copperLimitEndTextbox = e9ui_labeled_textbox_getTextbox(copperLimitEndRow);
    
    ui->copperLimitEndRow = copperLimitEndRow;
    ui->copperLimitEndTextbox = copperLimitEndTextbox;
    e9ui_component_t *copperLimitEndInsetRow = amiga_custom_ui_common_insetRowMake(copperLimitEndRow, 0, 14);
    
    e9ui_stack_addFixed(leftColumn, copperLimitEndInsetRow);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(8));

    amiga_custom_ui_copper_syncLimitSuboptions(ui);

    e9ui_component_t *cbBlitter = e9ui_checkbox_make("Blitter",
                                                     ui->blitterEnabled,
                                                     amiga_custom_ui_blitter_changed,
                                                     ui);
    e9ui_checkbox_setLeftMargin(cbBlitter, 12);
    e9ui_stack_addFixed(leftColumn, cbBlitter);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(8));

    e9ui_component_t *cbEstimateFps = e9ui_checkbox_make("Analyse Screen Data",
                                                         ui->estimateFpsEnabled,
                                                         amiga_custom_ui_estimateFpsChanged,
                                                         ui);

    ui->estimateFpsCheckbox = cbEstimateFps;
    e9ui_checkbox_setLeftMargin(cbEstimateFps, 12);

    e9ui_component_t *estimateFpsText = e9ui_text_make("FPS: --");

    e9ui_text_setColor(estimateFpsText, (SDL_Color){ 196, 214, 232, 255 });
    ui->estimateFpsText = estimateFpsText;

    e9ui_component_t *estimateFpsColorsText = e9ui_text_make("COLORS: --");

    e9ui_text_setColor(estimateFpsColorsText, (SDL_Color){ 196, 214, 232, 255 });
    ui->estimateFpsColorsText = estimateFpsColorsText;

    e9ui_component_t *estimateFpsVisibleAreaText = e9ui_text_make("VISIBLE AREA: --");

    e9ui_text_setColor(estimateFpsVisibleAreaText, (SDL_Color){ 196, 214, 232, 255 });
    ui->estimateFpsVisibleAreaText = estimateFpsVisibleAreaText;

    e9ui_component_t *estimateFpsDetails = e9ui_stack_makeVertical();

    e9ui_stack_addFixed(estimateFpsDetails, estimateFpsText);
    e9ui_stack_addFixed(estimateFpsDetails, e9ui_vspacer_make(2));
    e9ui_stack_addFixed(estimateFpsDetails, estimateFpsColorsText);
    e9ui_stack_addFixed(estimateFpsDetails, e9ui_vspacer_make(2));
    e9ui_stack_addFixed(estimateFpsDetails, estimateFpsVisibleAreaText);

    e9ui_component_t *estimateFpsDetailsRow = amiga_custom_ui_common_insetRowMake(estimateFpsDetails, 28, 0);

    ui->estimateFpsDetailsRow = estimateFpsDetailsRow;
    e9ui_stack_addFixed(leftColumn, cbEstimateFps);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(4));
    e9ui_stack_addFixed(leftColumn, estimateFpsDetailsRow);
    amiga_custom_ui_syncEstimateFpsDisplay(ui);

    e9ui_component_t *rightColumn = e9ui_stack_makeVertical();
    e9ui_component_t *cbCopperVisualiser = e9ui_checkbox_make("Copper Visualiser",
                                                              ui->copperVisualiserEnabled,
                                                              amiga_custom_ui_copper_visualiserChanged,
                                                              ui);
    ui->copperVisualiserCheckbox = cbCopperVisualiser;
    e9ui_checkbox_setLeftMargin(cbCopperVisualiser, 12);
    e9ui_stack_addFixed(rightColumn, cbCopperVisualiser);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(8));

    e9ui_component_t *cbPaletteVisualiser = e9ui_checkbox_make("Palette Visualiser",
                                                               ui->paletteVisualiserEnabled,
                                                               amiga_custom_ui_bitplane_paletteVisualiserChanged,
                                                               ui);
    ui->paletteVisualiserCheckbox = cbPaletteVisualiser;
    e9ui_checkbox_setLeftMargin(cbPaletteVisualiser, 12);
    e9ui_stack_addFixed(rightColumn, cbPaletteVisualiser);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(8));

    e9ui_component_t *cbSpriteVis = e9ui_checkbox_make("Sprite Visualiser",
                                                       ui->spriteVisEnabled,
                                                       amiga_custom_ui_sprite_visChanged,
                                                       ui);
    ui->spriteVisCheckbox = cbSpriteVis;
    e9ui_checkbox_setLeftMargin(cbSpriteVis, 12);
    e9ui_stack_addFixed(rightColumn, cbSpriteVis);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(8));

    e9ui_component_t *cbBlitterDebug = e9ui_checkbox_make("Blitter Visualiser",
                                                           ui->blitterDebugEnabled,
                                                           amiga_custom_ui_blitter_debugChanged,
                                                           ui);
    ui->blitterDebugCheckbox = cbBlitterDebug;
    e9ui_checkbox_setLeftMargin(cbBlitterDebug, 12);
    e9ui_stack_addFixed(rightColumn, cbBlitterDebug);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(8));

    e9ui_component_t *cbBlitterVisCollect = e9ui_checkbox_make("Overlay",
                                                               (ui->blitterVisMode & AMIGA_CUSTOM_UI_BLITTER_VIS_MODE_COLLECT) != 0,
                                                               amiga_custom_ui_blitter_visCollectChanged,
                                                               ui);

    ui->blitterVisCollectCheckbox = cbBlitterVisCollect;
    e9ui_checkbox_setLeftMargin(cbBlitterVisCollect, 28);

    char decayText[16];
    snprintf(decayText, sizeof(decayText), "%d", ui->blitterVisDecay);
    e9ui_component_t *blitterVisDecayTextboxRow = e9ui_labeled_textbox_make("Decay",
                                                                             78,
                                                                             0,
                                                                             amiga_custom_ui_blitter_visDecayChanged,
                                                                             ui);
    e9ui_labeled_textbox_setText(blitterVisDecayTextboxRow, decayText);
    e9ui_component_t *blitterVisDecayTextbox = e9ui_labeled_textbox_getTextbox(blitterVisDecayTextboxRow);
    e9ui_textbox_setKeyHandler(blitterVisDecayTextbox, amiga_custom_ui_blitter_visDecayTextboxKey, ui);
    ui->blitterVisDecayRow = blitterVisDecayTextboxRow;
    ui->blitterVisDecayTextbox = blitterVisDecayTextbox;
    e9ui_component_t *blitterVisDecayInsetRow = amiga_custom_ui_common_insetRowMake(blitterVisDecayTextboxRow, 0, 14);
    
    e9ui_stack_addFixed(rightColumn, blitterVisDecayInsetRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(4));

    e9ui_component_t *blitterVisDecaySeekBar = NULL;
    e9ui_component_t *blitterVisDecaySeekRow = amiga_custom_ui_common_blitterVisDecaySeekRowMake(&blitterVisDecaySeekBar);

    e9ui_seek_bar_setCallback(blitterVisDecaySeekBar, amiga_custom_ui_blitter_visDecaySeekChanged, ui);
    e9ui_seek_bar_setTooltipCallback(blitterVisDecaySeekBar, amiga_custom_ui_blitter_visDecaySeekTooltip, ui);

    ui->blitterVisDecaySeekRow = blitterVisDecaySeekRow;
    ui->blitterVisDecaySeekBar = blitterVisDecaySeekBar;
    amiga_custom_ui_blitter_syncVisDecaySeekBar(ui);
    e9ui_stack_addFixed(rightColumn, blitterVisDecaySeekRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(8));

    amiga_custom_ui_blitter_syncDebugSuboptions(ui);

    e9ui_component_t *blitterVisStatsChart = amiga_custom_ui_blitter_statsChartMake();

    ui->blitterVisStatsChart = blitterVisStatsChart;
    e9ui_component_t *blitterVisStatsChartInsetRow = amiga_custom_ui_common_insetRowMake(blitterVisStatsChart, 16, 0);

    e9ui_stack_addFixed(rightColumn, blitterVisStatsChartInsetRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(8));

    e9ui_component_t *cbAudios = e9ui_checkbox_make("Audio",
                                                    ui->audiosEnabled,
                                                    amiga_custom_ui_audio_masterChanged,
                                                    ui);

    ui->audiosCheckbox = cbAudios;
    e9ui_checkbox_setLeftMargin(cbAudios, 12);
    e9ui_stack_addFixed(rightColumn, cbAudios);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(6));

    for (int audioChannelIndex = 0; audioChannelIndex < AMIGA_CUSTOM_UI_AMIGA_AUDIO_CHANNEL_COUNT; ++audioChannelIndex) {
        char label[32];
        snprintf(label, sizeof(label), "Audio %d", audioChannelIndex);
        ui->audioCb[audioChannelIndex].ui = ui;
        ui->audioCb[audioChannelIndex].audioChannelIndex = audioChannelIndex;
        e9ui_component_t *cbAudio = e9ui_checkbox_make(label,
                                                       ui->audioEnabled[audioChannelIndex],
                                                       amiga_custom_ui_audio_changed,
                                                       &ui->audioCb[audioChannelIndex]);

        ui->audioCheckboxes[audioChannelIndex] = cbAudio;
        e9ui_checkbox_setLeftMargin(cbAudio, 28);
        e9ui_stack_addFixed(rightColumn, cbAudio);
    }

    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(8));

    e9ui_component_t *cbDmaStats = e9ui_checkbox_make("DMA Stats",
                                                      ui->dmaStatsEnabled,
                                                      amiga_custom_ui_dma_statsChanged,
                                                      ui);
    ui->dmaStatsCheckbox = cbDmaStats;

    e9ui_component_t *dmaStatsHintText = e9ui_text_make("NEEDS CYCLE EXACT!");

    e9ui_text_setColor(dmaStatsHintText, (SDL_Color){ 196, 164, 92, 255 });
    ui->dmaStatsHintText = dmaStatsHintText;
    e9ui_component_t *dmaStatsHintTextInsetRow = amiga_custom_ui_common_insetRowMake(dmaStatsHintText, 0, 0);

    ui->dmaStatsHintTextRow = dmaStatsHintTextInsetRow;

    e9ui_component_t *dmaStatsRow = amiga_custom_ui_common_dmaStatsHeaderRowMake(cbDmaStats, dmaStatsHintTextInsetRow, 12, 8);

    e9ui_stack_addFixed(rightColumn, dmaStatsRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(6));

    e9ui_component_t *copperStatsChart = amiga_custom_ui_copper_statsChartMake();

    ui->copperStatsChart = copperStatsChart;
    e9ui_component_t *copperStatsChartInsetRow = amiga_custom_ui_common_insetRowMake(copperStatsChart, 16, 0);

    ui->copperStatsChartRow = copperStatsChartInsetRow;
    e9ui_stack_addFixed(rightColumn, copperStatsChartInsetRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(4));

    e9ui_component_t *blitterDmaStatsChart = amiga_custom_ui_dma_blitterStatsChartMake();

    ui->blitterDmaStatsChart = blitterDmaStatsChart;
    e9ui_component_t *blitterDmaStatsChartInsetRow = amiga_custom_ui_common_insetRowMake(blitterDmaStatsChart, 16, 0);

    ui->blitterDmaStatsChartRow = blitterDmaStatsChartInsetRow;
    e9ui_stack_addFixed(rightColumn, blitterDmaStatsChartInsetRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(4));

    e9ui_component_t *cpuDmaStatsChart = amiga_custom_ui_dma_cpuStatsChartMake();

    ui->cpuDmaStatsChart = cpuDmaStatsChart;
    e9ui_component_t *cpuDmaStatsChartInsetRow = amiga_custom_ui_common_insetRowMake(cpuDmaStatsChart, 16, 0);

    ui->cpuDmaStatsChartRow = cpuDmaStatsChartInsetRow;
    e9ui_stack_addFixed(rightColumn, cpuDmaStatsChartInsetRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(4));

    e9ui_component_t *bitplaneDmaStatsChart = amiga_custom_ui_dma_bitplaneStatsChartMake();

    ui->bitplaneDmaStatsChart = bitplaneDmaStatsChart;
    e9ui_component_t *bitplaneDmaStatsChartInsetRow = amiga_custom_ui_common_insetRowMake(bitplaneDmaStatsChart, 16, 0);

    ui->bitplaneDmaStatsChartRow = bitplaneDmaStatsChartInsetRow;
    e9ui_stack_addFixed(rightColumn, bitplaneDmaStatsChartInsetRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(4));

    e9ui_component_t *spriteDmaStatsChart = amiga_custom_ui_dma_spriteStatsChartMake();

    ui->spriteDmaStatsChart = spriteDmaStatsChart;
    e9ui_component_t *spriteDmaStatsChartInsetRow = amiga_custom_ui_common_insetRowMake(spriteDmaStatsChart, 16, 0);

    ui->spriteDmaStatsChartRow = spriteDmaStatsChartInsetRow;
    e9ui_stack_addFixed(rightColumn, spriteDmaStatsChartInsetRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(4));

    e9ui_component_t *diskDmaStatsChart = amiga_custom_ui_dma_diskStatsChartMake();

    ui->diskDmaStatsChart = diskDmaStatsChart;
    e9ui_component_t *diskDmaStatsChartInsetRow = amiga_custom_ui_common_insetRowMake(diskDmaStatsChart, 16, 0);

    ui->diskDmaStatsChartRow = diskDmaStatsChartInsetRow;
    e9ui_stack_addFixed(rightColumn, diskDmaStatsChartInsetRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(4));

    e9ui_component_t *audioDmaStatsChart = amiga_custom_ui_dma_audioStatsChartMake();

    ui->audioDmaStatsChart = audioDmaStatsChart;
    e9ui_component_t *audioDmaStatsChartInsetRow = amiga_custom_ui_common_insetRowMake(audioDmaStatsChart, 16, 0);

    ui->audioDmaStatsChartRow = audioDmaStatsChartInsetRow;
    e9ui_stack_addFixed(rightColumn, audioDmaStatsChartInsetRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(4));

    e9ui_component_t *otherDmaStatsChart = amiga_custom_ui_dma_otherStatsChartMake();

    ui->otherDmaStatsChart = otherDmaStatsChart;
    e9ui_component_t *otherDmaStatsChartInsetRow = amiga_custom_ui_common_insetRowMake(otherDmaStatsChart, 16, 0);

    ui->otherDmaStatsChartRow = otherDmaStatsChartInsetRow;
    e9ui_stack_addFixed(rightColumn, otherDmaStatsChartInsetRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(4));

    e9ui_component_t *idleDmaStatsChart = amiga_custom_ui_dma_idleStatsChartMake();

    ui->idleDmaStatsChart = idleDmaStatsChart;
    e9ui_component_t *idleDmaStatsChartInsetRow = amiga_custom_ui_common_insetRowMake(idleDmaStatsChart, 16, 0);

    ui->idleDmaStatsChartRow = idleDmaStatsChartInsetRow;
    e9ui_stack_addFixed(rightColumn, idleDmaStatsChartInsetRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(4));

    e9ui_component_t *dmaTotalMixChart = amiga_custom_ui_dma_totalMixChartMake();

    ui->dmaTotalMixChart = dmaTotalMixChart;
    e9ui_component_t *dmaTotalMixChartInsetRow = amiga_custom_ui_common_insetRowMake(dmaTotalMixChart, 16, 0);

    ui->dmaTotalMixChartRow = dmaTotalMixChartInsetRow;
    e9ui_stack_addFixed(rightColumn, dmaTotalMixChartInsetRow);

    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(8));
    amiga_custom_ui_dma_syncStatsSuboptions(ui);

    e9ui_hstack_addFlex(columns, leftColumn);
    e9ui_hstack_addFixed(columns, e9ui_spacer_make(16), 16);
    e9ui_hstack_addFlex(columns, rightColumn);
    e9ui_stack_addFlex(rootStack, columns);

    e9ui_component_t *scrollRoot = e9ui_scroll_make(rootStack);

    return scrollRoot;
}

static void
amiga_custom_ui_syncEstimateFpsDisplay(amiga_custom_ui_state_t *ui)
{
    if (!ui) {
        return;
    }
    if (ui->estimateFpsDetailsRow) {
        e9ui_setHidden(ui->estimateFpsDetailsRow, ui->estimateFpsEnabled ? 0 : 1);
    }
    if (!ui->estimateFpsText) {
        return;
    }
    if (!ui->estimateFpsEnabled) {
        e9ui_text_setText(ui->estimateFpsText, "FPS: --");
        if (ui->estimateFpsColorsText) {
            e9ui_text_setText(ui->estimateFpsColorsText, "COLORS: --");
        }
        if (ui->estimateFpsVisibleAreaText) {
            e9ui_text_setText(ui->estimateFpsVisibleAreaText, "VISIBLE AREA: --");
        }
        return;
    }
    double estimatedFps = libretro_host_getEstimatedVideoFps();
    if (estimatedFps > 0.0) {
        char text[32];
        snprintf(text, sizeof(text), "FPS: %.1f", estimatedFps);
        e9ui_text_setText(ui->estimateFpsText, text);
    } else {
        e9ui_text_setText(ui->estimateFpsText, "FPS: ...");
    }
    if (ui->estimateFpsColorsText) {
        unsigned distinctColors = libretro_host_getEstimatedVideoDistinctColors();
        if (distinctColors > 0u) {
            char text[32];
            snprintf(text, sizeof(text), "COLORS: %u", distinctColors);
            e9ui_text_setText(ui->estimateFpsColorsText, text);
        } else {
            e9ui_text_setText(ui->estimateFpsColorsText, "COLORS: ...");
        }
    }
    if (ui->estimateFpsVisibleAreaText) {
        unsigned visibleWidth = 0u;
        unsigned visibleHeight = 0u;
        if (libretro_host_getEstimatedVideoVisibleArea(&visibleWidth, &visibleHeight)) {
            char text[48];
            snprintf(text, sizeof(text), "VISIBLE AREA: %ux%u", visibleWidth, visibleHeight);
            e9ui_text_setText(ui->estimateFpsVisibleAreaText, text);
        } else {
            e9ui_text_setText(ui->estimateFpsVisibleAreaText, "VISIBLE AREA: ...");
        }
    }
}

static void
amiga_custom_ui_prepareFrame(amiga_custom_ui_state_t *ui, const e9ui_context_t *frameCtx)
{
    if (!ui) {
        return;
    }
    if (frameCtx) {
        ui->ctx = *frameCtx;
    }
    if (ui->pendingRemove && ui->root) {
        e9ui_childRemove(ui->root, ui->pendingRemove, &ui->ctx);
        ui->pendingRemove = NULL;
    }
    amiga_custom_ui_blitter_syncDebugCheckbox(ui);
    amiga_custom_ui_sprite_syncVisCheckbox(ui);
    amiga_custom_ui_copper_syncVisualiserCheckbox(ui);
    amiga_custom_ui_blitter_tickVisDecayTextbox(ui);
    amiga_custom_ui_copper_tickLimitTextboxes(ui);
    amiga_custom_ui_bplptr_tickLineLimitTextboxes(ui);
    amiga_custom_ui_dma_updateStatsCharts(ui);
    amiga_custom_ui_common_syncDmaStatsCycleExactHint(ui);
    amiga_custom_ui_syncEstimateFpsDisplay(ui);
}

static int
amiga_custom_ui_overlayBodyPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)ctx;
    (void)availW;
    return 0;
}

static void
amiga_custom_ui_overlayBodyLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self) {
        return;
    }
    self->bounds = bounds;
    amiga_custom_ui_overlay_body_state_t *st = (amiga_custom_ui_overlay_body_state_t *)self->state;
    if (!st || !st->ui || !st->ui->root || !st->ui->root->layout) {
        return;
    }
    st->ui->root->layout(st->ui->root, ctx, bounds);
}

static void
amiga_custom_ui_overlayBodyRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx) {
        return;
    }
    amiga_custom_ui_overlay_body_state_t *st = (amiga_custom_ui_overlay_body_state_t *)self->state;
    if (!st || !st->ui || !st->ui->root) {
        return;
    }
    amiga_custom_ui_prepareFrame(st->ui, ctx);
    if (st->ui->root->render) {
        st->ui->root->render(st->ui->root, ctx);
    }
}

static e9ui_component_t *
amiga_custom_ui_makeOverlayBodyHost(amiga_custom_ui_state_t *ui)
{
    if (!ui || !ui->root) {
        return NULL;
    }
    e9ui_component_t *host = (e9ui_component_t *)alloc_calloc(1, sizeof(*host));
    if (!host) {
        return NULL;
    }
    amiga_custom_ui_overlay_body_state_t *st = (amiga_custom_ui_overlay_body_state_t *)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(host);
        return NULL;
    }
    st->ui = ui;
    host->name = "amiga_custom_ui_overlay_body";
    host->state = st;
    host->preferredHeight = amiga_custom_ui_overlayBodyPreferredHeight;
    host->layout = amiga_custom_ui_overlayBodyLayout;
    host->render = amiga_custom_ui_overlayBodyRender;
    e9ui_child_add(host, ui->root, alloc_strdup("amiga_custom_ui_root"));
    return host;
}

static void
amiga_custom_ui_overlayWindowCloseRequested(e9ui_window_t *window, void *user)
{
    (void)window;
    amiga_custom_ui_state_t *ui = (amiga_custom_ui_state_t *)user;
    if (!ui) {
        return;
    }
    amiga_custom_ui_shutdown();
}

int
amiga_custom_ui_init(void)
{
    amiga_custom_ui_state_t *ui = &amiga_custom_ui_state;
    if (ui->windowState.open) {
        return 1;
    }

    ui->windowState.windowHost = e9ui_windowCreate(amiga_custom_ui_windowBackend());
    if (!ui->windowState.windowHost) {
        return 0;
    }
    ui->warnedMissingOption = 0;
    ui->suppressBlitterDebugCallbacks = 0;
    ui->suppressSpriteVisCallbacks = 0;
    ui->suppressSpriteCallbacks = 0;
    ui->suppressBitplaneCallbacks = 0;
    ui->suppressAudioCallbacks = 0;
    ui->suppressBplptrBlockCallbacks = 0;
    ui->spritesCheckbox = NULL;
    ui->bitplanesCheckbox = NULL;
    ui->bplptrBlockAllCheckbox = NULL;
    ui->audiosCheckbox = NULL;
    ui->copperVisualiserCheckbox = NULL;
    ui->paletteVisualiserCheckbox = NULL;
    ui->copperLimitCheckbox = NULL;
    ui->copperLimitStartRow = NULL;
    ui->copperLimitStartTextbox = NULL;
    ui->copperLimitStartTextboxHadFocus = 0;
    ui->copperLimitEndRow = NULL;
    ui->copperLimitEndTextbox = NULL;
    ui->copperLimitEndTextboxHadFocus = 0;
    ui->bplptrLineLimitStartRow = NULL;
    ui->bplptrLineLimitStartTextbox = NULL;
    ui->bplptrLineLimitStartTextboxHadFocus = 0;
    ui->bplptrLineLimitEndRow = NULL;
    ui->bplptrLineLimitEndTextbox = NULL;
    ui->bplptrLineLimitEndTextboxHadFocus = 0;
    ui->blitterDebugCheckbox = NULL;
    ui->spriteVisCheckbox = NULL;
    ui->blitterVisCollectCheckbox = NULL;
    ui->blitterVisDecayRow = NULL;
    ui->blitterVisDecayTextbox = NULL;
    ui->blitterVisDecaySeekRow = NULL;
    ui->blitterVisDecaySeekBar = NULL;
    ui->dmaStatsCheckbox = NULL;
    ui->estimateFpsCheckbox = NULL;
    ui->estimateFpsText = NULL;
    ui->estimateFpsColorsText = NULL;
    ui->estimateFpsVisibleAreaText = NULL;
    ui->estimateFpsDetailsRow = NULL;
    ui->dmaStatsHintText = NULL;
    ui->dmaStatsHintTextRow = NULL;
    ui->copperStatsChart = NULL;
    ui->copperStatsChartRow = NULL;
    ui->blitterDmaStatsChart = NULL;
    ui->blitterDmaStatsChartRow = NULL;
    ui->cpuDmaStatsChart = NULL;
    ui->cpuDmaStatsChartRow = NULL;
    ui->bitplaneDmaStatsChart = NULL;
    ui->bitplaneDmaStatsChartRow = NULL;
    ui->spriteDmaStatsChart = NULL;
    ui->spriteDmaStatsChartRow = NULL;
    ui->diskDmaStatsChart = NULL;
    ui->diskDmaStatsChartRow = NULL;
    ui->audioDmaStatsChart = NULL;
    ui->audioDmaStatsChartRow = NULL;
    ui->otherDmaStatsChart = NULL;
    ui->otherDmaStatsChartRow = NULL;
    ui->idleDmaStatsChart = NULL;
    ui->idleDmaStatsChartRow = NULL;
    ui->dmaTotalMixChart = NULL;
    ui->dmaTotalMixChartRow = NULL;
    ui->blitterVisStatsChart = NULL;
    ui->dmaDebugAutoEnabled = 0;
    ui->dmaDebugAutoPrevValue = 0;
    ui->blitterVisDecayTextboxHadFocus = 0;
    for (int spriteIndex = 0; spriteIndex < AMIGA_CUSTOM_UI_AMIGA_SPRITE_COUNT; ++spriteIndex) {
        ui->spriteCheckboxes[spriteIndex] = NULL;
    }
    for (int bitplaneIndex = 0; bitplaneIndex < AMIGA_CUSTOM_UI_AMIGA_BITPLANE_COUNT; ++bitplaneIndex) {
        ui->bitplaneCheckboxes[bitplaneIndex] = NULL;
    }
    for (int bplptrIndex = 0; bplptrIndex < AMIGA_CUSTOM_UI_AMIGA_BPLPTR_COUNT; ++bplptrIndex) {
        ui->bplptrBlockCheckboxes[bplptrIndex] = NULL;
    }
    for (int audioChannelIndex = 0; audioChannelIndex < AMIGA_CUSTOM_UI_AMIGA_AUDIO_CHANNEL_COUNT; ++audioChannelIndex) {
        ui->audioCheckboxes[audioChannelIndex] = NULL;
    }
    amiga_custom_ui_sprite_syncMasterCheckbox(ui);
    amiga_custom_ui_bitplane_syncMasterCheckbox(ui);
    amiga_custom_ui_bplptr_syncBlockMasterCheckbox(ui);
    amiga_custom_ui_audio_syncMasterCheckbox(ui);
    amiga_custom_ui_blitter_syncDebugCheckbox(ui);
    amiga_custom_ui_sprite_syncVisCheckbox(ui);
    libretro_host_setEstimateFpsEnabled(ui->estimateFpsEnabled);

    ui->root = amiga_custom_ui_buildRoot(ui);
    if (!ui->root) {
        amiga_custom_ui_shutdown();
        return 0;
    }
    {
        e9ui_rect_t rect = e9ui_windowResolveStateOpenRect(&e9ui->ctx,
                                                           amiga_custom_ui_windowDefaultRect(&e9ui->ctx),
                                                           &ui->windowState);
        e9ui_component_t *overlayBodyHost = amiga_custom_ui_makeOverlayBodyHost(ui);
        if (!overlayBodyHost) {
            amiga_custom_ui_shutdown();
            return 0;
        }
        e9ui_windowOpen(ui->windowState.windowHost,
                                     AMIGA_CUSTOM_UI_TITLE,
                                     rect,
                                     overlayBodyHost,
                                     amiga_custom_ui_overlayWindowCloseRequested,
                                     ui,
			             &e9ui->ctx);
        ui->ctx = e9ui->ctx;
    }

    amiga_custom_ui_applyAllOptions();
    if (ui->dmaStatsEnabled) {
        amiga_custom_ui_dma_enableDebugForCopperStats(ui);
    }
    ui->windowState.open = 1;
    aux_window_register(&amiga_custom_ui_auxWindowOps, ui);
    return 1;
}

void
amiga_custom_ui_shutdown(void)
{
    amiga_custom_ui_state_t *ui = &amiga_custom_ui_state;
    amiga_custom_ui_dma_restoreDebugForCopperStats(ui);
    if (!ui->windowState.open) {
        return;
    }
    aux_window_unregister(&amiga_custom_ui_auxWindowOps, ui);
    if (ui->windowState.windowHost) {
        e9ui_windowDestroy(ui->windowState.windowHost);
        ui->windowState.windowHost = NULL;
    }
    ui->root = NULL;
    ui->fullscreen = NULL;
    ui->windowState.open = 0;
    ui->warnedMissingOption = 0;
    ui->pendingRemove = NULL;
    memset(&ui->ctx, 0, sizeof(ui->ctx));
}

void
amiga_custom_ui_toggle(void)
{
    if (amiga_custom_ui_isOpen()) {
        amiga_custom_ui_shutdown();
        return;
    }
    (void)amiga_custom_ui_init();
}

int
amiga_custom_ui_isOpen(void)
{
    return amiga_custom_ui_state.windowState.open ? 1 : 0;
}

void
amiga_custom_ui_setMainWindowFocused(int focused)
{
    (void)focused;
}

int
amiga_custom_ui_getBlitterVisDecay(void)
{
    return amiga_custom_ui_state.blitterVisDecay;
}

int
amiga_custom_ui_getEstimateFpsEnabled(void)
{
    return amiga_custom_ui_state.estimateFpsEnabled ? 1 : 0;
}

int
amiga_custom_ui_getCopperLimitEnabled(void)
{
    return amiga_custom_ui_state.copperLimitEnabled ? 1 : 0;
}

int
amiga_custom_ui_getCopperLimitRange(int *outStart, int *outEnd)
{
    amiga_custom_ui_state_t *ui = &amiga_custom_ui_state;
    if (outStart) {
        *outStart = ui->copperLimitStart;
    }
    if (outEnd) {
        *outEnd = ui->copperLimitEnd;
    }
    return 1;
}

void
amiga_custom_ui_setCopperLimitRange(int start, int end)
{
    amiga_custom_ui_state_t *ui = &amiga_custom_ui_state;
    int nextStart = amiga_custom_ui_common_clampCopperLine(start);
    int nextEnd = amiga_custom_ui_common_clampCopperLine(end);
    if (nextEnd < nextStart) {
        int temp = nextStart;
        nextStart = nextEnd;
        nextEnd = temp;
    }
    int changedStart = (nextStart != ui->copperLimitStart) ? 1 : 0;
    int changedEnd = (nextEnd != ui->copperLimitEnd) ? 1 : 0;
    if (!changedStart && !changedEnd) {
        return;
    }
    ui->copperLimitStart = nextStart;
    ui->copperLimitEnd = nextEnd;
    amiga_custom_ui_copper_applyLimitStartOption();
    amiga_custom_ui_copper_applyLimitEndOption();
    if (ui->copperLimitStartRow) {
        char text[16];
        snprintf(text, sizeof(text), "%d", ui->copperLimitStart);
        e9ui_labeled_textbox_setText(ui->copperLimitStartRow, text);
    }
    if (ui->copperLimitEndRow) {
        char text[16];
        snprintf(text, sizeof(text), "%d", ui->copperLimitEnd);
        e9ui_labeled_textbox_setText(ui->copperLimitEndRow, text);
    }
}

int
amiga_custom_ui_getBplptrBlockEnabled(void)
{
    amiga_custom_ui_state_t *ui = &amiga_custom_ui_state;
    for (int bplptrIndex = 0; bplptrIndex < AMIGA_CUSTOM_UI_AMIGA_BPLPTR_COUNT; ++bplptrIndex) {
        if (ui->bplptrBlockEnabled[bplptrIndex]) {
            return 1;
        }
    }
    return ui->bplptrBlockAllEnabled ? 1 : 0;
}

int
amiga_custom_ui_getBplptrLineLimitRange(int *outStart, int *outEnd)
{
    amiga_custom_ui_state_t *ui = &amiga_custom_ui_state;
    if (outStart) {
        *outStart = ui->bplptrLineLimitStart;
    }
    if (outEnd) {
        *outEnd = ui->bplptrLineLimitEnd;
    }
    return 1;
}

void
amiga_custom_ui_setBplptrLineLimitRange(int start, int end)
{
    amiga_custom_ui_state_t *ui = &amiga_custom_ui_state;
    int nextStart = amiga_custom_ui_common_clampCopperLine(start);
    int nextEnd = amiga_custom_ui_common_clampCopperLine(end);
    if (nextEnd < nextStart) {
        int temp = nextStart;
        nextStart = nextEnd;
        nextEnd = temp;
    }
    int changedStart = (nextStart != ui->bplptrLineLimitStart) ? 1 : 0;
    int changedEnd = (nextEnd != ui->bplptrLineLimitEnd) ? 1 : 0;
    if (!changedStart && !changedEnd) {
        return;
    }
    ui->bplptrLineLimitStart = nextStart;
    ui->bplptrLineLimitEnd = nextEnd;
    amiga_custom_ui_bplptr_applyLineLimitStartOption();
    amiga_custom_ui_bplptr_applyLineLimitEndOption();
    if (ui->bplptrLineLimitStartRow) {
        char text[16];
        snprintf(text, sizeof(text), "%d", ui->bplptrLineLimitStart);
        e9ui_labeled_textbox_setText(ui->bplptrLineLimitStartRow, text);
    }
    if (ui->bplptrLineLimitEndRow) {
        char text[16];
        snprintf(text, sizeof(text), "%d", ui->bplptrLineLimitEnd);
        e9ui_labeled_textbox_setText(ui->bplptrLineLimitEndRow, text);
    }
}

void
amiga_custom_ui_render(void)
{
    amiga_custom_ui_state_t *ui = &amiga_custom_ui_state;
    if (!ui->windowState.open || !ui->root) {
        return;
    }
    if (e9ui_windowCaptureStateRectChanged(&ui->windowState, &e9ui->ctx)) {
        config_saveConfig();
    }
}

void
amiga_custom_ui_persistConfig(FILE *file)
{
    amiga_custom_ui_state_t *ui = &amiga_custom_ui_state;
    if (!file) {
        return;
    }
    e9ui_windowPersistStateRect(file, "comp.custom_ui", &ui->windowState, &e9ui->ctx);
    fprintf(file, "comp.custom_ui.estimate_fps=%d\n", ui->estimateFpsEnabled ? 1 : 0);
}

int
amiga_custom_ui_loadConfigProperty(const char *prop, const char *value)
{
    amiga_custom_ui_state_t *ui = &amiga_custom_ui_state;
    if (!prop || !value) {
        return 0;
    }
    int intValue = 0;
    if (strcmp(prop, "win_x") == 0) {
        if (!amiga_custom_ui_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winX = intValue;
    } else if (strcmp(prop, "win_y") == 0) {
        if (!amiga_custom_ui_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winY = intValue;
    } else if (strcmp(prop, "win_w") == 0) {
        if (!amiga_custom_ui_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winW = intValue;
    } else if (strcmp(prop, "win_h") == 0) {
        if (!amiga_custom_ui_parseInt(value, &intValue)) {
            return 0;
        }
        ui->windowState.winH = intValue;
    } else if (strcmp(prop, "estimate_fps") == 0) {
        if (!amiga_custom_ui_parseInt(value, &intValue)) {
            return 0;
        }
        ui->estimateFpsEnabled = intValue ? 1 : 0;
        return 1;
    } else {
        return 0;
    }
    ui->windowState.winHasSaved = e9ui_windowHasSavedPosition(ui->windowState.winX, ui->windowState.winY);
    return 1;
}
