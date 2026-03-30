/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <SDL.h>
#include <SDL_ttf.h>

#include "../e9k-lib/e9k-lib.h"
#include "aux_window.h"
#include "amiga_custom_ui.h"
#include "e9ui.h"

#define AMIGA_CUSTOM_UI_TITLE "ENGINE9000 DEBUGGER - VISUALISERS"
#define AMIGA_CUSTOM_UI_AMIGA_SPRITE_COUNT 8
#define AMIGA_CUSTOM_UI_AMIGA_BITPLANE_COUNT 8
#define AMIGA_CUSTOM_UI_AMIGA_AUDIO_CHANNEL_COUNT 4
#define AMIGA_CUSTOM_UI_AMIGA_BPLPTR_COUNT 6
#define AMIGA_CUSTOM_UI_BLITTER_VIS_MODE_COLLECT 0x2
#define AMIGA_CUSTOM_UI_BLITTER_VIS_DECAY_MAX 64
#define AMIGA_CUSTOM_UI_COPPER_LINE_MAX 2047
#define AMIGA_CUSTOM_UI_DMA_RECORD_CPU 2u
#define AMIGA_CUSTOM_UI_DMA_RECORD_COPPER 3u
#define AMIGA_CUSTOM_UI_DMA_RECORD_AUDIO 4u
#define AMIGA_CUSTOM_UI_DMA_RECORD_BLITTER 5u
#define AMIGA_CUSTOM_UI_DMA_RECORD_BITPLANE 6u
#define AMIGA_CUSTOM_UI_DMA_RECORD_SPRITE 7u
#define AMIGA_CUSTOM_UI_DMA_RECORD_DISK 8u
#define AMIGA_CUSTOM_UI_DMA_RECORD_CONFLICT 9u
#define AMIGA_CUSTOM_UI_DMA_RECORD_REFRESH 1u

typedef struct amiga_custom_ui_state {
    e9ui_window_state_t windowState;
    int blitterEnabled;
    int copperVisualiserEnabled;
    int paletteVisualiserEnabled;
    int blitterDebugEnabled;
    int suppressBlitterDebugCallbacks;
    int spriteVisEnabled;
    int suppressSpriteVisCallbacks;
    int blitterVisMode;
    int blitterVisDecay;
    int dmaStatsEnabled;
    int estimateFpsEnabled;
    int copperLimitEnabled;
    int copperLimitStart;
    int copperLimitEnd;
    int bplptrBlockAllEnabled;
    int bplptrBlockEnabled[AMIGA_CUSTOM_UI_AMIGA_BPLPTR_COUNT];
    int bplptrLineLimitStart;
    int bplptrLineLimitEnd;
    int suppressBplptrBlockCallbacks;
    int bplcon1DelayScrollEnabled;
    int spritesEnabled;
    int spriteEnabled[AMIGA_CUSTOM_UI_AMIGA_SPRITE_COUNT];
    int suppressSpriteCallbacks;
    int bitplanesEnabled;
    int bitplaneEnabled[AMIGA_CUSTOM_UI_AMIGA_BITPLANE_COUNT];
    int suppressBitplaneCallbacks;
    int audiosEnabled;
    int suppressAudioCallbacks;
    int audioEnabled[AMIGA_CUSTOM_UI_AMIGA_AUDIO_CHANNEL_COUNT];
    int warnedMissingOption;
    e9ui_context_t ctx;
    e9ui_component_t *root;
    e9ui_component_t *fullscreen;
    e9ui_component_t *pendingRemove;
    e9ui_component_t *spritesCheckbox;
    e9ui_component_t *spriteCheckboxes[AMIGA_CUSTOM_UI_AMIGA_SPRITE_COUNT];
    e9ui_component_t *bitplanesCheckbox;
    e9ui_component_t *bitplaneCheckboxes[AMIGA_CUSTOM_UI_AMIGA_BITPLANE_COUNT];
    e9ui_component_t *audiosCheckbox;
    e9ui_component_t *copperVisualiserCheckbox;
    e9ui_component_t *paletteVisualiserCheckbox;
    e9ui_component_t *blitterDebugCheckbox;
    e9ui_component_t *spriteVisCheckbox;
    e9ui_component_t *blitterVisCollectCheckbox;
    e9ui_component_t *blitterVisDecayRow;
    e9ui_component_t *blitterVisDecayTextbox;
    e9ui_component_t *blitterVisDecaySeekRow;
    e9ui_component_t *blitterVisDecaySeekBar;
    e9ui_component_t *dmaStatsCheckbox;
    e9ui_component_t *estimateFpsCheckbox;
    e9ui_component_t *estimateFpsText;
    e9ui_component_t *estimateFpsColorsText;
    e9ui_component_t *estimateFpsVisibleAreaText;
    e9ui_component_t *estimateFpsDetailsRow;
    e9ui_component_t *dmaStatsHintText;
    e9ui_component_t *dmaStatsHintTextRow;
    e9ui_component_t *copperStatsChart;
    e9ui_component_t *copperStatsChartRow;
    e9ui_component_t *blitterDmaStatsChart;
    e9ui_component_t *blitterDmaStatsChartRow;
    e9ui_component_t *cpuDmaStatsChart;
    e9ui_component_t *cpuDmaStatsChartRow;
    e9ui_component_t *bitplaneDmaStatsChart;
    e9ui_component_t *bitplaneDmaStatsChartRow;
    e9ui_component_t *spriteDmaStatsChart;
    e9ui_component_t *spriteDmaStatsChartRow;
    e9ui_component_t *diskDmaStatsChart;
    e9ui_component_t *diskDmaStatsChartRow;
    e9ui_component_t *audioDmaStatsChart;
    e9ui_component_t *audioDmaStatsChartRow;
    e9ui_component_t *otherDmaStatsChart;
    e9ui_component_t *otherDmaStatsChartRow;
    e9ui_component_t *idleDmaStatsChart;
    e9ui_component_t *idleDmaStatsChartRow;
    e9ui_component_t *dmaTotalMixChart;
    e9ui_component_t *dmaTotalMixChartRow;
    e9ui_component_t *blitterVisStatsChart;
    int dmaStatsCacheValid;
    int dmaStatsCacheFrameSelect;
    int dmaStatsCacheFrameNumber;
    int dmaDebugAutoEnabled;
    int dmaDebugAutoPrevValue;
    int blitterVisDecayTextboxHadFocus;
    e9ui_component_t *copperLimitCheckbox;
    e9ui_component_t *copperLimitStartRow;
    e9ui_component_t *copperLimitStartTextbox;
    int copperLimitStartTextboxHadFocus;
    e9ui_component_t *copperLimitEndRow;
    e9ui_component_t *copperLimitEndTextbox;
    int copperLimitEndTextboxHadFocus;
    e9ui_component_t *bplptrBlockAllCheckbox;
    e9ui_component_t *bplptrLineLimitStartRow;
    e9ui_component_t *bplptrLineLimitStartTextbox;
    int bplptrLineLimitStartTextboxHadFocus;
    e9ui_component_t *bplptrLineLimitEndRow;
    e9ui_component_t *bplptrLineLimitEndTextbox;
    int bplptrLineLimitEndTextboxHadFocus;
    e9ui_component_t *bplptrBlockCheckboxes[AMIGA_CUSTOM_UI_AMIGA_BPLPTR_COUNT];
    e9ui_component_t *audioCheckboxes[AMIGA_CUSTOM_UI_AMIGA_AUDIO_CHANNEL_COUNT];
    struct amiga_custom_ui_sprite_cb {
        struct amiga_custom_ui_state *ui;
        int spriteIndex;
    } spriteCb[AMIGA_CUSTOM_UI_AMIGA_SPRITE_COUNT];
    struct amiga_custom_ui_bitplane_cb {
        struct amiga_custom_ui_state *ui;
        int bitplaneIndex;
    } bitplaneCb[AMIGA_CUSTOM_UI_AMIGA_BITPLANE_COUNT];
    struct amiga_custom_ui_audio_cb {
        struct amiga_custom_ui_state *ui;
        int audioChannelIndex;
    } audioCb[AMIGA_CUSTOM_UI_AMIGA_AUDIO_CHANNEL_COUNT];
    struct amiga_custom_ui_bplptr_cb {
        struct amiga_custom_ui_state *ui;
        int bplptrIndex;
    } bplptrCb[AMIGA_CUSTOM_UI_AMIGA_BPLPTR_COUNT];
} amiga_custom_ui_state_t;

typedef struct amiga_custom_ui_seek_row_state {
    e9ui_component_t *bar;
    int leftInset;
    int rightInset;
    int barHeight;
    int rowPadding;
} amiga_custom_ui_seek_row_state_t;

typedef struct amiga_custom_ui_inset_row_state {
    e9ui_component_t *child;
    int leftInset;
    int rightInset;
} amiga_custom_ui_inset_row_state_t;

typedef struct amiga_custom_ui_dma_stats_header_row_state {
    e9ui_component_t *checkbox;
    e9ui_component_t *hintRow;
    int leftInset;
    int gap;
} amiga_custom_ui_dma_stats_header_row_state_t;

typedef struct amiga_custom_ui_overlay_body_state {
    amiga_custom_ui_state_t *ui;
} amiga_custom_ui_overlay_body_state_t;

typedef struct amiga_custom_ui_blitter_stats_chart_state {
    int leftInset;
    int rightInset;
    int topPadding;
    int bottomPadding;
    int rowGap;
    int labelWidth;
    int labelGap;
    int barHeight;
    int hasStats;
    uint32_t wordsFrame;
    uint32_t maxWordsEstimateFrame;
    uint32_t blitsFrame;
} amiga_custom_ui_blitter_stats_chart_state_t;

typedef struct amiga_custom_ui_copper_stats_chart_state {
    int leftInset;
    int rightInset;
    int topPadding;
    int bottomPadding;
    int rowGap;
    int labelWidth;
    int labelGap;
    int barHeight;
    int hasCopperStats;
    uint32_t copperSlotsFrame;
    uint32_t copperSlotsMaxFrame;
} amiga_custom_ui_copper_stats_chart_state_t;

typedef struct amiga_custom_ui_blitter_dma_stats_chart_state {
    int leftInset;
    int rightInset;
    int topPadding;
    int bottomPadding;
    int rowGap;
    int labelWidth;
    int labelGap;
    int barHeight;
    int hasBlitterDmaStats;
    uint32_t blitterSlotsFrame;
    uint32_t blitterSlotsMaxFrame;
} amiga_custom_ui_blitter_dma_stats_chart_state_t;

typedef struct amiga_custom_ui_cpu_dma_stats_chart_state {
    int leftInset;
    int rightInset;
    int topPadding;
    int bottomPadding;
    int rowGap;
    int labelWidth;
    int labelGap;
    int barHeight;
    int hasCpuDmaStats;
    uint32_t cpuSlotsFrame;
    uint32_t cpuSlotsMaxFrame;
} amiga_custom_ui_cpu_dma_stats_chart_state_t;

typedef struct amiga_custom_ui_bitplane_dma_stats_chart_state {
    int leftInset;
    int rightInset;
    int topPadding;
    int bottomPadding;
    int rowGap;
    int labelWidth;
    int labelGap;
    int barHeight;
    int hasBitplaneDmaStats;
    uint32_t bitplaneSlotsFrame;
    uint32_t bitplaneSlotsMaxFrame;
} amiga_custom_ui_bitplane_dma_stats_chart_state_t;

typedef struct amiga_custom_ui_sprite_dma_stats_chart_state {
    int leftInset;
    int rightInset;
    int topPadding;
    int bottomPadding;
    int rowGap;
    int labelWidth;
    int labelGap;
    int barHeight;
    int hasSpriteDmaStats;
    uint32_t spriteSlotsFrame;
    uint32_t spriteSlotsMaxFrame;
} amiga_custom_ui_sprite_dma_stats_chart_state_t;

typedef struct amiga_custom_ui_disk_dma_stats_chart_state {
    int leftInset;
    int rightInset;
    int topPadding;
    int bottomPadding;
    int rowGap;
    int labelWidth;
    int labelGap;
    int barHeight;
    int hasDiskDmaStats;
    uint32_t diskSlotsFrame;
    uint32_t diskSlotsMaxFrame;
} amiga_custom_ui_disk_dma_stats_chart_state_t;

typedef struct amiga_custom_ui_audio_dma_stats_chart_state {
    int leftInset;
    int rightInset;
    int topPadding;
    int bottomPadding;
    int rowGap;
    int labelWidth;
    int labelGap;
    int barHeight;
    int hasAudioDmaStats;
    uint32_t audioSlotsFrame;
    uint32_t audioSlotsMaxFrame;
} amiga_custom_ui_audio_dma_stats_chart_state_t;

typedef struct amiga_custom_ui_other_dma_stats_chart_state {
    int leftInset;
    int rightInset;
    int topPadding;
    int bottomPadding;
    int rowGap;
    int labelWidth;
    int labelGap;
    int barHeight;
    int hasOtherDmaStats;
    uint32_t otherSlotsFrame;
    uint32_t otherSlotsMaxFrame;
} amiga_custom_ui_other_dma_stats_chart_state_t;

typedef struct amiga_custom_ui_idle_dma_stats_chart_state {
    int leftInset;
    int rightInset;
    int topPadding;
    int bottomPadding;
    int rowGap;
    int labelWidth;
    int labelGap;
    int barHeight;
    int hasIdleDmaStats;
    uint32_t idleSlotsFrame;
    uint32_t idleSlotsMaxFrame;
} amiga_custom_ui_idle_dma_stats_chart_state_t;

typedef struct amiga_custom_ui_dma_total_mix_chart_state {
    int leftInset;
    int rightInset;
    int topPadding;
    int bottomPadding;
    int rowGap;
    int labelWidth;
    int labelGap;
    int barHeight;
    int hasStats;
    uint32_t totalSlotsFrame;
    uint32_t totalSlotsMaxFrame;
    uint32_t cpuSlotsFrame;
    uint32_t copperSlotsFrame;
    uint32_t audioSlotsFrame;
    uint32_t blitterSlotsFrame;
    uint32_t bitplaneSlotsFrame;
    uint32_t spriteSlotsFrame;
    uint32_t diskSlotsFrame;
    uint32_t otherSlotsFrame;
    uint32_t idleSlotsFrame;
} amiga_custom_ui_dma_total_mix_chart_state_t;
extern amiga_custom_ui_state_t amiga_custom_ui_state;

void
amiga_custom_ui_common_applyOption(e9k_debug_option_t option, uint32_t argument);

void
amiga_custom_ui_common_setComponentDisabled(e9ui_component_t *comp, int disabled);

int
amiga_custom_ui_common_clampCopperLine(int line);

int
amiga_custom_ui_common_textboxLikeHeight(e9ui_context_t *ctx);

extern const SDL_Color amiga_custom_ui_common_blitterStatsChartLabelColor;
extern const SDL_Color amiga_custom_ui_common_blitterStatsChartTextColor;
extern const SDL_Color amiga_custom_ui_common_blitterStatsChartTextShadowColor;
extern const SDL_Color amiga_custom_ui_common_dmaColorCpu;
extern const SDL_Color amiga_custom_ui_common_dmaColorCopper;
extern const SDL_Color amiga_custom_ui_common_dmaColorAudio;
extern const SDL_Color amiga_custom_ui_common_dmaColorBlitter;
extern const SDL_Color amiga_custom_ui_common_dmaColorBitplane;
extern const SDL_Color amiga_custom_ui_common_dmaColorSprite;
extern const SDL_Color amiga_custom_ui_common_dmaColorDisk;
extern const SDL_Color amiga_custom_ui_common_dmaColorOther;
extern const SDL_Color amiga_custom_ui_common_dmaColorIdle;

void
amiga_custom_ui_common_blitterStatsChartDrawText(e9ui_context_t *ctx,
                                          TTF_Font *font,
                                          const char *text,
                                          SDL_Color color,
                                          int x,
                                          int y);

void
amiga_custom_ui_common_statsChartMeasureUint(e9ui_context_t *ctx, TTF_Font *font,
                                      uint32_t value, SDL_Color color,
                                      int *outW, int *outH);

void
amiga_custom_ui_common_statsChartMeasureText(e9ui_context_t *ctx, TTF_Font *font,
                                      const char *text, SDL_Color color,
                                      int *outW, int *outH);

void
amiga_custom_ui_common_statsChartDrawValueUsedSuffix(e9ui_context_t *ctx,
                                              TTF_Font *font,
                                              uint32_t valueUsed,
                                              const char *valueSuffix,
                                              SDL_Color color,
                                              int x,
                                              int y);

void
amiga_custom_ui_common_blitterStatsChartFillGradient(e9ui_context_t *ctx,
                                              const SDL_Rect *rect,
                                              int originX,
                                              int originW);

void
amiga_custom_ui_blitter_statsChartRenderBarRow(e9ui_context_t *ctx,
                                       int contentX,
                                       int barX,
                                       int barW,
                                       int barHeight,
                                       int labelGap,
                                       int rowY,
                                       int rowH,
                                       int fontH,
                                       const char *labelText,
                                       int hasData,
                                       uint32_t valueUsed,
                                       uint32_t valueMax,
                                       const char *valueSuffix,
                                       int useGradientFill,
                                       SDL_Color solidFillColor,
                                       int useTextShadow,
                                       int useTextClip);

e9ui_component_t *
amiga_custom_ui_common_blitterVisDecaySeekRowMake(e9ui_component_t **outBar);

e9ui_component_t *
amiga_custom_ui_common_insetRowMake(e9ui_component_t *child, int leftInset, int rightInset);

e9ui_component_t *
amiga_custom_ui_common_dmaStatsHeaderRowMake(e9ui_component_t *checkbox, e9ui_component_t *hintRow,
                                      int leftInset, int gap);

void
amiga_custom_ui_blitter_applyOption(void);

void
amiga_custom_ui_blitter_applyVisDecayOption(void);

void
amiga_custom_ui_blitter_applyVisModeOption(void);

void
amiga_custom_ui_blitter_applyDebugOption(void);

void
amiga_custom_ui_bitplane_applyPaletteVisOption(void);

void
amiga_custom_ui_blitter_syncDebugSuboptions(amiga_custom_ui_state_t *ui);

void
amiga_custom_ui_blitter_syncDebugCheckbox(amiga_custom_ui_state_t *ui);

void
amiga_custom_ui_blitter_changed(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user);

void
amiga_custom_ui_blitter_debugChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user);

void
amiga_custom_ui_bitplane_paletteVisualiserChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user);

void
amiga_custom_ui_blitter_visCollectChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user);

void
amiga_custom_ui_blitter_visDecayChanged(e9ui_context_t *ctx, e9ui_component_t *comp,
                                       const char *value, void *user);

void
amiga_custom_ui_blitter_visDecaySeekChanged(float percent, void *user);

void
amiga_custom_ui_blitter_visDecaySeekTooltip(float percent, char *out, size_t cap, void *user);

int
amiga_custom_ui_blitter_visDecayTextboxKey(e9ui_context_t *ctx, SDL_Keycode key,
                                          SDL_Keymod mods, void *user);

void
amiga_custom_ui_blitter_updateStatsChart(amiga_custom_ui_state_t *ui);

e9ui_component_t *
amiga_custom_ui_blitter_statsChartMake(void);

void
amiga_custom_ui_copper_applyLimitEnabledOption(void);

void
amiga_custom_ui_copper_applyLimitStartOption(void);

void
amiga_custom_ui_copper_applyLimitEndOption(void);

void
amiga_custom_ui_copper_syncLimitSuboptions(amiga_custom_ui_state_t *ui);

void
amiga_custom_ui_copper_syncVisualiserCheckbox(amiga_custom_ui_state_t *ui);

void
amiga_custom_ui_copper_limitChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user);

void
amiga_custom_ui_copper_visualiserChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user);

void
amiga_custom_ui_copper_limitStartChanged(e9ui_context_t *ctx, e9ui_component_t *comp,
                                        const char *value, void *user);

void
amiga_custom_ui_copper_limitEndChanged(e9ui_context_t *ctx, e9ui_component_t *comp,
                                      const char *value, void *user);

int
amiga_custom_ui_copper_limitStartTextboxKey(e9ui_context_t *ctx, SDL_Keycode key,
                                           SDL_Keymod mods, void *user);

int
amiga_custom_ui_copper_limitEndTextboxKey(e9ui_context_t *ctx, SDL_Keycode key,
                                         SDL_Keymod mods, void *user);

void
amiga_custom_ui_copper_tickLimitTextboxes(amiga_custom_ui_state_t *ui);

void
amiga_custom_ui_dma_enableDebugForCopperStats(amiga_custom_ui_state_t *ui);

void
amiga_custom_ui_dma_restoreDebugForCopperStats(amiga_custom_ui_state_t *ui);

void
amiga_custom_ui_dma_statsChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user);

void
amiga_custom_ui_dma_syncStatsSuboptions(amiga_custom_ui_state_t *ui);

void
amiga_custom_ui_common_syncDmaStatsCycleExactHint(amiga_custom_ui_state_t *ui);

void
amiga_custom_ui_copper_statsChartSetValues(e9ui_component_t *comp,
                                          int hasCopperStats,
                                          uint32_t copperSlotsFrame,
                                          uint32_t copperSlotsMaxFrame);

e9ui_component_t *
amiga_custom_ui_copper_statsChartMake(void);

e9ui_component_t *
amiga_custom_ui_dma_blitterStatsChartMake(void);

e9ui_component_t *
amiga_custom_ui_dma_cpuStatsChartMake(void);

e9ui_component_t *
amiga_custom_ui_dma_bitplaneStatsChartMake(void);

e9ui_component_t *
amiga_custom_ui_dma_spriteStatsChartMake(void);

e9ui_component_t *
amiga_custom_ui_dma_diskStatsChartMake(void);

e9ui_component_t *
amiga_custom_ui_dma_audioStatsChartMake(void);

e9ui_component_t *
amiga_custom_ui_dma_otherStatsChartMake(void);

e9ui_component_t *
amiga_custom_ui_dma_idleStatsChartMake(void);

e9ui_component_t *
amiga_custom_ui_dma_totalMixChartMake(void);

void
amiga_custom_ui_sprite_applyVisOption(void);

void
amiga_custom_ui_sprite_applyOption(int spriteIndex);

void
amiga_custom_ui_sprite_syncVisCheckbox(amiga_custom_ui_state_t *ui);

void
amiga_custom_ui_sprite_syncMasterCheckbox(amiga_custom_ui_state_t *ui);

void
amiga_custom_ui_sprite_visChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user);

void
amiga_custom_ui_sprite_masterChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user);

void
amiga_custom_ui_sprite_changed(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user);

void
amiga_custom_ui_bitplane_applyBplcon1DelayScrollOption(void);

void
amiga_custom_ui_bitplane_applyOption(int bitplaneIndex);

void
amiga_custom_ui_bitplane_syncMasterCheckbox(amiga_custom_ui_state_t *ui);

void
amiga_custom_ui_bitplane_bplcon1DelayScrollChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user);

void
amiga_custom_ui_bitplane_masterChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user);

void
amiga_custom_ui_bitplane_changed(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user);

void
amiga_custom_ui_bplptr_applyBlockAllOption(void);

void
amiga_custom_ui_bplptr_applyLineLimitStartOption(void);

void
amiga_custom_ui_bplptr_applyLineLimitEndOption(void);

void
amiga_custom_ui_bplptr_applyBlockOption(int bplptrIndex);

void
amiga_custom_ui_bplptr_syncBlockMasterCheckbox(amiga_custom_ui_state_t *ui);

void
amiga_custom_ui_bplptr_blockAllChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user);

void
amiga_custom_ui_bplptr_blockChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user);

void
amiga_custom_ui_bplptr_lineLimitStartChanged(e9ui_context_t *ctx, e9ui_component_t *comp,
                                            const char *value, void *user);

void
amiga_custom_ui_bplptr_lineLimitEndChanged(e9ui_context_t *ctx, e9ui_component_t *comp,
                                          const char *value, void *user);

int
amiga_custom_ui_bplptr_lineLimitStartTextboxKey(e9ui_context_t *ctx, SDL_Keycode key,
                                               SDL_Keymod mods, void *user);

int
amiga_custom_ui_bplptr_lineLimitEndTextboxKey(e9ui_context_t *ctx, SDL_Keycode key,
                                             SDL_Keymod mods, void *user);

void
amiga_custom_ui_bplptr_tickLineLimitTextboxes(amiga_custom_ui_state_t *ui);

void
amiga_custom_ui_audio_applyOption(int audioChannelIndex);

void
amiga_custom_ui_audio_syncMasterCheckbox(amiga_custom_ui_state_t *ui);

void
amiga_custom_ui_audio_masterChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user);

void
amiga_custom_ui_audio_changed(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user);

void
amiga_custom_ui_estimateFpsChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user);

void
amiga_custom_ui_blitter_syncVisDecaySeekBar(amiga_custom_ui_state_t *ui);

void
amiga_custom_ui_blitter_tickVisDecayTextbox(amiga_custom_ui_state_t *ui);

void
amiga_custom_ui_dma_updateStatsCharts(amiga_custom_ui_state_t *ui);
