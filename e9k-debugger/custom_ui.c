/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <limits.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <SDL.h>

#include "config.h"
#include "alloc.h"
#include "custom_ui.h"
#include "debug.h"
#include "debugger.h"
#include "emu_ami.h"
#include "e9ui.h"
#include "e9ui_scroll.h"
#include "e9ui_seek_bar.h"
#include "e9ui_text.h"
#include "libretro_host.h"

#ifndef E9K_HACK_AMI_SPRITE_VIS
#define E9K_HACK_AMI_SPRITE_VIS 0
#endif

#define CUSTOM_UI_TITLE "ENGINE9000 DEBUGGER - CUSTOM"
#define CUSTOM_UI_AMIGA_SPRITE_COUNT 8
#define CUSTOM_UI_AMIGA_BITPLANE_COUNT 8
#define CUSTOM_UI_AMIGA_AUDIO_CHANNEL_COUNT 4
#define CUSTOM_UI_AMIGA_BPLPTR_COUNT 6
#define CUSTOM_UI_BLITTER_VIS_MODE_SOLID 0x1
#define CUSTOM_UI_BLITTER_VIS_MODE_COLLECT 0x2
#define CUSTOM_UI_BLITTER_VIS_MODE_PATTERN 0x4
#define CUSTOM_UI_BLITTER_VIS_MODE_STYLE_MASK (CUSTOM_UI_BLITTER_VIS_MODE_SOLID | CUSTOM_UI_BLITTER_VIS_MODE_PATTERN)
#define CUSTOM_UI_BLITTER_VIS_DECAY_MAX 64
#define CUSTOM_UI_COPPER_LINE_MAX 2047
#define CUSTOM_UI_DMA_RECORD_CPU 2u
#define CUSTOM_UI_DMA_RECORD_COPPER 3u
#define CUSTOM_UI_DMA_RECORD_AUDIO 4u
#define CUSTOM_UI_DMA_RECORD_BLITTER 5u
#define CUSTOM_UI_DMA_RECORD_BITPLANE 6u
#define CUSTOM_UI_DMA_RECORD_SPRITE 7u
#define CUSTOM_UI_DMA_RECORD_DISK 8u
#define CUSTOM_UI_DMA_RECORD_CONFLICT 9u
#define CUSTOM_UI_DMA_RECORD_REFRESH 1u

typedef struct custom_ui_state {
    int open;
    int blitterEnabled;
    int copperVisualiserEnabled;
    int blitterDebugEnabled;
    int suppressBlitterDebugCallbacks;
#if E9K_HACK_AMI_SPRITE_VIS
    int spriteVisEnabled;
    int suppressSpriteVisCallbacks;
#endif
    int blitterVisMode;
    int suppressBlitterVisModeCallbacks;
    int blitterVisBlink;
    int blitterVisDecay;
    int dmaStatsEnabled;
    int estimateFpsEnabled;
    int copperLimitEnabled;
    int copperLimitStart;
    int copperLimitEnd;
    int bplptrBlockAllEnabled;
    int bplptrBlockEnabled[CUSTOM_UI_AMIGA_BPLPTR_COUNT];
    int bplptrLineLimitStart;
    int bplptrLineLimitEnd;
    int suppressBplptrBlockCallbacks;
    int bplcon1DelayScrollEnabled;
    int spritesEnabled;
    int spriteEnabled[CUSTOM_UI_AMIGA_SPRITE_COUNT];
    int suppressSpriteCallbacks;
    int bitplanesEnabled;
    int bitplaneEnabled[CUSTOM_UI_AMIGA_BITPLANE_COUNT];
    int suppressBitplaneCallbacks;
    int audiosEnabled;
    int suppressAudioCallbacks;
    int audioEnabled[CUSTOM_UI_AMIGA_AUDIO_CHANNEL_COUNT];
    int warnedMissingOption;
    e9ui_window_t *windowHost;
    int winX;
    int winY;
    int winW;
    int winH;
    int winHasSaved;
    e9ui_context_t ctx;
    e9ui_component_t *root;
    e9ui_component_t *fullscreen;
    e9ui_component_t *pendingRemove;
    e9ui_component_t *spritesCheckbox;
    e9ui_component_t *spriteCheckboxes[CUSTOM_UI_AMIGA_SPRITE_COUNT];
    e9ui_component_t *bitplanesCheckbox;
    e9ui_component_t *bitplaneCheckboxes[CUSTOM_UI_AMIGA_BITPLANE_COUNT];
    e9ui_component_t *audiosCheckbox;
    e9ui_component_t *copperVisualiserCheckbox;
    e9ui_component_t *blitterDebugCheckbox;
#if E9K_HACK_AMI_SPRITE_VIS
    e9ui_component_t *spriteVisCheckbox;
#endif
    e9ui_component_t *blitterVisPatternCheckbox;
    e9ui_component_t *blitterVisModeCheckbox;
    e9ui_component_t *blitterVisCollectCheckbox;
    e9ui_component_t *blitterVisBlinkCheckbox;
    e9ui_component_t *blitterVisDecayRow;
    e9ui_component_t *blitterVisDecayTextbox;
    e9ui_component_t *blitterVisDecaySeekRow;
    e9ui_component_t *blitterVisDecaySeekBar;
    e9ui_component_t *dmaStatsCheckbox;
    e9ui_component_t *estimateFpsCheckbox;
    e9ui_component_t *estimateFpsText;
    e9ui_component_t *estimateFpsTextRow;
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
    e9ui_component_t *bplptrBlockCheckboxes[CUSTOM_UI_AMIGA_BPLPTR_COUNT];
    e9ui_component_t *audioCheckboxes[CUSTOM_UI_AMIGA_AUDIO_CHANNEL_COUNT];
    struct custom_ui_sprite_cb {
        struct custom_ui_state *ui;
        int spriteIndex;
    } spriteCb[CUSTOM_UI_AMIGA_SPRITE_COUNT];
    struct custom_ui_bitplane_cb {
        struct custom_ui_state *ui;
        int bitplaneIndex;
    } bitplaneCb[CUSTOM_UI_AMIGA_BITPLANE_COUNT];
    struct custom_ui_audio_cb {
        struct custom_ui_state *ui;
        int audioChannelIndex;
    } audioCb[CUSTOM_UI_AMIGA_AUDIO_CHANNEL_COUNT];
    struct custom_ui_bplptr_cb {
        struct custom_ui_state *ui;
        int bplptrIndex;
    } bplptrCb[CUSTOM_UI_AMIGA_BPLPTR_COUNT];
} custom_ui_state_t;

typedef struct custom_ui_seek_row_state {
    e9ui_component_t *bar;
    int leftInset;
    int rightInset;
    int barHeight;
    int rowPadding;
} custom_ui_seek_row_state_t;

typedef struct custom_ui_inset_row_state {
    e9ui_component_t *child;
    int leftInset;
    int rightInset;
} custom_ui_inset_row_state_t;

typedef struct custom_ui_dma_stats_header_row_state {
    e9ui_component_t *checkbox;
    e9ui_component_t *hintRow;
    int leftInset;
    int gap;
} custom_ui_dma_stats_header_row_state_t;

typedef struct custom_ui_overlay_body_state {
    custom_ui_state_t *ui;
} custom_ui_overlay_body_state_t;

typedef struct custom_ui_blitter_stats_chart_state {
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
} custom_ui_blitter_stats_chart_state_t;

typedef struct custom_ui_copper_stats_chart_state {
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
} custom_ui_copper_stats_chart_state_t;

typedef struct custom_ui_blitter_dma_stats_chart_state {
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
} custom_ui_blitter_dma_stats_chart_state_t;

typedef struct custom_ui_cpu_dma_stats_chart_state {
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
} custom_ui_cpu_dma_stats_chart_state_t;

typedef struct custom_ui_bitplane_dma_stats_chart_state {
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
} custom_ui_bitplane_dma_stats_chart_state_t;

typedef struct custom_ui_sprite_dma_stats_chart_state {
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
} custom_ui_sprite_dma_stats_chart_state_t;

typedef struct custom_ui_disk_dma_stats_chart_state {
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
} custom_ui_disk_dma_stats_chart_state_t;

typedef struct custom_ui_audio_dma_stats_chart_state {
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
} custom_ui_audio_dma_stats_chart_state_t;

typedef struct custom_ui_other_dma_stats_chart_state {
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
} custom_ui_other_dma_stats_chart_state_t;

typedef struct custom_ui_idle_dma_stats_chart_state {
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
} custom_ui_idle_dma_stats_chart_state_t;

typedef struct custom_ui_dma_total_mix_chart_state {
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
} custom_ui_dma_total_mix_chart_state_t;

static const SDL_Color custom_ui_blitterStatsChartLabelColor = { 220, 220, 220, 255 };
static const SDL_Color custom_ui_blitterStatsChartTextColor = { 232, 236, 240, 255 };
static const SDL_Color custom_ui_blitterStatsChartTextShadowColor = { 12, 14, 18, 220 };
static const SDL_Color custom_ui_dmaColorCpu = { 0xa2, 0x53, 0x42, 255 };
static const SDL_Color custom_ui_dmaColorCopper = { 0xee, 0xee, 0x00, 255 };
static const SDL_Color custom_ui_dmaColorAudio = { 0xff, 0x00, 0x00, 255 };
static const SDL_Color custom_ui_dmaColorBlitter = { 0x00, 0x88, 0x88, 255 };
static const SDL_Color custom_ui_dmaColorBitplane = { 0x00, 0x00, 0xff, 255 };
static const SDL_Color custom_ui_dmaColorSprite = { 0xff, 0x00, 0xff, 255 };
static const SDL_Color custom_ui_dmaColorDisk = { 0xff, 0xff, 0xff, 255 };
static const SDL_Color custom_ui_dmaColorOther = { 0xff, 0xb8, 0x40, 255 };
static const SDL_Color custom_ui_dmaColorIdle = { 0x5a, 0x5a, 0x5a, 255 };

static custom_ui_state_t custom_ui_state = {
    .blitterEnabled = 1,
    .copperVisualiserEnabled = 0,
    .blitterDebugEnabled = 0,
#if E9K_HACK_AMI_SPRITE_VIS
    .spriteVisEnabled = 0,
#endif
    .blitterVisMode = CUSTOM_UI_BLITTER_VIS_MODE_COLLECT,
    .blitterVisBlink = 1,
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

static int
custom_ui_parseInt(const char *value, int *out)
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

static int
custom_ui_clampCopperLine(int line);

static void
custom_ui_enableDmaDebugForCopperStats(custom_ui_state_t *ui);

static void
custom_ui_restoreDmaDebugForCopperStats(custom_ui_state_t *ui);

static void
custom_ui_syncDmaStatsSuboptions(custom_ui_state_t *ui);

static void
custom_ui_syncDmaStatsCycleExactHint(custom_ui_state_t *ui);

static void
custom_ui_syncEstimateFpsDisplay(custom_ui_state_t *ui);

static void
custom_ui_syncCopperVisualiserCheckbox(custom_ui_state_t *ui);

#if E9K_HACK_AMI_SPRITE_VIS
static void
custom_ui_applySpriteVisOption(void);

static void
custom_ui_syncSpriteVisCheckbox(custom_ui_state_t *ui);
#endif

static e9ui_window_backend_t
custom_ui_windowBackend(void)
{
    return e9ui_window_backend_overlay;
}

static e9ui_rect_t
custom_ui_windowDefaultRect(const e9ui_context_t *ctx)
{
    e9ui_rect_t rect = {
        e9ui_scale_px(ctx, 64),
        e9ui_scale_px(ctx, 64),
        e9ui_scale_px(ctx, 560),
        e9ui_scale_px(ctx, 560)
    };
    return rect;
}

static float
custom_ui_blitterVisDecayToPercent(int decay)
{
    int clamped = decay;
    if (clamped < 1) {
        clamped = 1;
    }
    if (clamped > CUSTOM_UI_BLITTER_VIS_DECAY_MAX) {
        clamped = CUSTOM_UI_BLITTER_VIS_DECAY_MAX;
    }
    if (CUSTOM_UI_BLITTER_VIS_DECAY_MAX <= 1) {
        return 1.0f;
    }
    return (float)(clamped - 1) / (float)(CUSTOM_UI_BLITTER_VIS_DECAY_MAX - 1);
}

static int
custom_ui_blitterVisDecayFromPercent(float percent)
{
    float clamped = percent;
    if (clamped < 0.0f) {
        clamped = 0.0f;
    }
    if (clamped > 1.0f) {
        clamped = 1.0f;
    }
    int decay = 1 + (int)(clamped * (float)(CUSTOM_UI_BLITTER_VIS_DECAY_MAX - 1) + 0.5f);
    if (decay < 1) {
        decay = 1;
    }
    if (decay > CUSTOM_UI_BLITTER_VIS_DECAY_MAX) {
        decay = CUSTOM_UI_BLITTER_VIS_DECAY_MAX;
    }
    return decay;
}

static void
custom_ui_syncBlitterVisDecaySeekBar(custom_ui_state_t *ui)
{
    if (!ui || !ui->blitterVisDecaySeekBar) {
        return;
    }
    e9ui_seek_bar_setPercent(ui->blitterVisDecaySeekBar, custom_ui_blitterVisDecayToPercent(ui->blitterVisDecay));
}

static void
custom_ui_blitterStatsChartSetValues(e9ui_component_t *comp,
                                     int hasStats,
                                     uint32_t wordsFrame,
                                     uint32_t maxWordsEstimateFrame,
                                     uint32_t blitsFrame)
{
    if (!comp || !comp->state) {
        return;
    }
    custom_ui_blitter_stats_chart_state_t *st = (custom_ui_blitter_stats_chart_state_t *)comp->state;
    st->hasStats = hasStats ? 1 : 0;
    st->wordsFrame = wordsFrame;
    st->maxWordsEstimateFrame = maxWordsEstimateFrame;
    st->blitsFrame = blitsFrame;
}

static void
custom_ui_copperStatsChartSetValues(e9ui_component_t *comp,
                                 int hasCopperStats,
                                 uint32_t copperSlotsFrame,
                                 uint32_t copperSlotsMaxFrame)
{
    if (!comp || !comp->state) {
        return;
    }
    custom_ui_copper_stats_chart_state_t *st = (custom_ui_copper_stats_chart_state_t *)comp->state;
    st->hasCopperStats = hasCopperStats ? 1 : 0;
    st->copperSlotsFrame = copperSlotsFrame;
    st->copperSlotsMaxFrame = copperSlotsMaxFrame;
}

static void
custom_ui_blitterDmaStatsChartSetValues(e9ui_component_t *comp,
                                        int hasBlitterDmaStats,
                                        uint32_t blitterSlotsFrame,
                                        uint32_t blitterSlotsMaxFrame)
{
    if (!comp || !comp->state) {
        return;
    }
    custom_ui_blitter_dma_stats_chart_state_t *st = (custom_ui_blitter_dma_stats_chart_state_t *)comp->state;
    st->hasBlitterDmaStats = hasBlitterDmaStats ? 1 : 0;
    st->blitterSlotsFrame = blitterSlotsFrame;
    st->blitterSlotsMaxFrame = blitterSlotsMaxFrame;
}

static void
custom_ui_cpuDmaStatsChartSetValues(e9ui_component_t *comp,
                                    int hasCpuDmaStats,
                                    uint32_t cpuSlotsFrame,
                                    uint32_t cpuSlotsMaxFrame)
{
    if (!comp || !comp->state) {
        return;
    }
    custom_ui_cpu_dma_stats_chart_state_t *st = (custom_ui_cpu_dma_stats_chart_state_t *)comp->state;
    st->hasCpuDmaStats = hasCpuDmaStats ? 1 : 0;
    st->cpuSlotsFrame = cpuSlotsFrame;
    st->cpuSlotsMaxFrame = cpuSlotsMaxFrame;
}

static void
custom_ui_bitplaneDmaStatsChartSetValues(e9ui_component_t *comp,
                                         int hasBitplaneDmaStats,
                                         uint32_t bitplaneSlotsFrame,
                                         uint32_t bitplaneSlotsMaxFrame)
{
    if (!comp || !comp->state) {
        return;
    }
    custom_ui_bitplane_dma_stats_chart_state_t *st = (custom_ui_bitplane_dma_stats_chart_state_t *)comp->state;
    st->hasBitplaneDmaStats = hasBitplaneDmaStats ? 1 : 0;
    st->bitplaneSlotsFrame = bitplaneSlotsFrame;
    st->bitplaneSlotsMaxFrame = bitplaneSlotsMaxFrame;
}

static void
custom_ui_spriteDmaStatsChartSetValues(e9ui_component_t *comp,
                                       int hasSpriteDmaStats,
                                       uint32_t spriteSlotsFrame,
                                       uint32_t spriteSlotsMaxFrame)
{
    if (!comp || !comp->state) {
        return;
    }
    custom_ui_sprite_dma_stats_chart_state_t *st = (custom_ui_sprite_dma_stats_chart_state_t *)comp->state;
    st->hasSpriteDmaStats = hasSpriteDmaStats ? 1 : 0;
    st->spriteSlotsFrame = spriteSlotsFrame;
    st->spriteSlotsMaxFrame = spriteSlotsMaxFrame;
}

static void
custom_ui_diskDmaStatsChartSetValues(e9ui_component_t *comp,
                                     int hasDiskDmaStats,
                                     uint32_t diskSlotsFrame,
                                     uint32_t diskSlotsMaxFrame)
{
    if (!comp || !comp->state) {
        return;
    }
    custom_ui_disk_dma_stats_chart_state_t *st = (custom_ui_disk_dma_stats_chart_state_t *)comp->state;
    st->hasDiskDmaStats = hasDiskDmaStats ? 1 : 0;
    st->diskSlotsFrame = diskSlotsFrame;
    st->diskSlotsMaxFrame = diskSlotsMaxFrame;
}

static void
custom_ui_audioDmaStatsChartSetValues(e9ui_component_t *comp,
                                      int hasAudioDmaStats,
                                      uint32_t audioSlotsFrame,
                                      uint32_t audioSlotsMaxFrame)
{
    if (!comp || !comp->state) {
        return;
    }
    custom_ui_audio_dma_stats_chart_state_t *st = (custom_ui_audio_dma_stats_chart_state_t *)comp->state;
    st->hasAudioDmaStats = hasAudioDmaStats ? 1 : 0;
    st->audioSlotsFrame = audioSlotsFrame;
    st->audioSlotsMaxFrame = audioSlotsMaxFrame;
}

static void
custom_ui_otherDmaStatsChartSetValues(e9ui_component_t *comp,
                                      int hasOtherDmaStats,
                                      uint32_t otherSlotsFrame,
                                      uint32_t otherSlotsMaxFrame)
{
    if (!comp || !comp->state) {
        return;
    }
    custom_ui_other_dma_stats_chart_state_t *st = (custom_ui_other_dma_stats_chart_state_t *)comp->state;
    st->hasOtherDmaStats = hasOtherDmaStats ? 1 : 0;
    st->otherSlotsFrame = otherSlotsFrame;
    st->otherSlotsMaxFrame = otherSlotsMaxFrame;
}

static void
custom_ui_idleDmaStatsChartSetValues(e9ui_component_t *comp,
                                     int hasIdleDmaStats,
                                     uint32_t idleSlotsFrame,
                                     uint32_t idleSlotsMaxFrame)
{
    if (!comp || !comp->state) {
        return;
    }
    custom_ui_idle_dma_stats_chart_state_t *st = (custom_ui_idle_dma_stats_chart_state_t *)comp->state;
    st->hasIdleDmaStats = hasIdleDmaStats ? 1 : 0;
    st->idleSlotsFrame = idleSlotsFrame;
    st->idleSlotsMaxFrame = idleSlotsMaxFrame;
}

static void
custom_ui_dmaTotalMixChartSetValues(e9ui_component_t *comp,
                                    int hasStats,
                                    uint32_t totalSlotsFrame,
                                    uint32_t totalSlotsMaxFrame,
                                    uint32_t cpuSlotsFrame,
                                    uint32_t copperSlotsFrame,
                                    uint32_t audioSlotsFrame,
                                    uint32_t blitterSlotsFrame,
                                    uint32_t bitplaneSlotsFrame,
                                    uint32_t spriteSlotsFrame,
                                    uint32_t diskSlotsFrame,
                                    uint32_t otherSlotsFrame,
                                    uint32_t idleSlotsFrame)
{
    if (!comp || !comp->state) {
        return;
    }
    custom_ui_dma_total_mix_chart_state_t *st = (custom_ui_dma_total_mix_chart_state_t *)comp->state;
    st->hasStats = hasStats ? 1 : 0;
    st->totalSlotsFrame = totalSlotsFrame;
    st->totalSlotsMaxFrame = totalSlotsMaxFrame;
    st->cpuSlotsFrame = cpuSlotsFrame;
    st->copperSlotsFrame = copperSlotsFrame;
    st->audioSlotsFrame = audioSlotsFrame;
    st->blitterSlotsFrame = blitterSlotsFrame;
    st->bitplaneSlotsFrame = bitplaneSlotsFrame;
    st->spriteSlotsFrame = spriteSlotsFrame;
    st->diskSlotsFrame = diskSlotsFrame;
    st->otherSlotsFrame = otherSlotsFrame;
    st->idleSlotsFrame = idleSlotsFrame;
}

static void
custom_ui_blitterStatsChartDrawText(e9ui_context_t *ctx,
                                    TTF_Font *font,
                                    const char *text,
                                    SDL_Color color,
                                    int x,
                                    int y)
{
    TTF_Font *useFont = font ? font : (ctx ? ctx->font : NULL);
    if (!ctx || !ctx->renderer || !useFont || !text || !*text) {
        return;
    }
    int tw = 0;
    int th = 0;
    SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, useFont, text, color, &tw, &th);
    if (!tex) {
        return;
    }
    SDL_Rect dst = { x, y, tw, th };
    SDL_RenderCopy(ctx->renderer, tex, NULL, &dst);
}

static int
custom_ui_statsChartU32ToText(uint32_t value, char *buf, int cap)
{
    if (!buf || cap <= 1) {
        return 0;
    }
    char rev[16];
    int n = 0;
    uint32_t v = value;
    do {
        rev[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v > 0u && n < (int)sizeof(rev));
    if (n + 1 > cap) {
        n = cap - 1;
    }
    for (int i = 0; i < n; ++i) {
        buf[i] = rev[n - 1 - i];
    }
    buf[n] = '\0';
    return n;
}

static void
custom_ui_statsChartMeasureUint(e9ui_context_t *ctx, TTF_Font *font, uint32_t value, SDL_Color color, int *outW, int *outH)
{
    if (outW) {
        *outW = 0;
    }
    if (outH) {
        *outH = 0;
    }
    if (!ctx || !ctx->renderer || !font) {
        return;
    }
    char digits[16];
    int n = custom_ui_statsChartU32ToText(value, digits, (int)sizeof(digits));
    int w = 0;
    int h = 0;
    for (int i = 0; i < n; ++i) {
        char ch[2] = { digits[i], '\0' };
        int cw = 0;
        int chh = 0;
        SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, ch, color, &cw, &chh);
        if (!tex) {
            continue;
        }
        w += cw;
        if (chh > h) {
            h = chh;
        }
    }
    if (outW) {
        *outW = w;
    }
    if (outH) {
        *outH = h;
    }
}

static void
custom_ui_statsChartDrawUint(e9ui_context_t *ctx, TTF_Font *font, uint32_t value, SDL_Color color, int x, int y)
{
    if (!ctx || !ctx->renderer || !font) {
        return;
    }
    char digits[16];
    int n = custom_ui_statsChartU32ToText(value, digits, (int)sizeof(digits));
    int penX = x;
    for (int i = 0; i < n; ++i) {
        char ch[2] = { digits[i], '\0' };
        int cw = 0;
        int chh = 0;
        SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, ch, color, &cw, &chh);
        if (!tex) {
            continue;
        }
        SDL_Rect dst = { penX, y, cw, chh };
        SDL_RenderCopy(ctx->renderer, tex, NULL, &dst);
        penX += cw;
    }
}

static void
custom_ui_statsChartMeasureText(e9ui_context_t *ctx, TTF_Font *font, const char *text, SDL_Color color, int *outW, int *outH)
{
    if (outW) {
        *outW = 0;
    }
    if (outH) {
        *outH = 0;
    }
    if (!ctx || !ctx->renderer || !font || !text || !*text) {
        return;
    }
    int tw = 0;
    int th = 0;
    SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, text, color, &tw, &th);
    if (!tex) {
        return;
    }
    if (outW) {
        *outW = tw;
    }
    if (outH) {
        *outH = th;
    }
}

static void
custom_ui_statsChartDrawValueUsedSuffix(e9ui_context_t *ctx,
                                        TTF_Font *font,
                                        uint32_t valueUsed,
                                        const char *valueSuffix,
                                        SDL_Color color,
                                        int x,
                                        int y)
{
    int usedW = 0;
    int usedH = 0;
    int spaceW = 0;
    int spaceH = 0;
    int suffixW = 0;
    int suffixH = 0;
    custom_ui_statsChartMeasureUint(ctx, font, valueUsed, color, &usedW, &usedH);
    custom_ui_statsChartMeasureText(ctx, font, " ", color, &spaceW, &spaceH);
    custom_ui_statsChartMeasureText(ctx, font, valueSuffix, color, &suffixW, &suffixH);
    int lineH = usedH;
    if (spaceH > lineH) {
        lineH = spaceH;
    }
    if (suffixH > lineH) {
        lineH = suffixH;
    }
    int penX = x;
    int usedY = y + (lineH - usedH) / 2;
    int spaceY = y + (lineH - spaceH) / 2;
    int suffixY = y + (lineH - suffixH) / 2;
    custom_ui_statsChartDrawUint(ctx, font, valueUsed, color, penX, usedY);
    penX += usedW;
    custom_ui_blitterStatsChartDrawText(ctx, font, " ", color, penX, spaceY);
    penX += spaceW;
    custom_ui_blitterStatsChartDrawText(ctx, font, valueSuffix, color, penX, suffixY);
}

static void
custom_ui_blitterStatsChartHueToRgb(float h, Uint8 *r, Uint8 *g, Uint8 *b)
{
    if (h < 0.0f) {
        h -= (int)h;
    }
    if (h >= 1.0f) {
        h -= (int)h;
    }
    float i = floorf(h * 6.0f);
    float f = h * 6.0f - i;
    float q = 1.0f - f;
    int ii = ((int)i) % 6;
    float rr = 0.0f;
    float gg = 0.0f;
    float bb = 0.0f;
    switch (ii) {
    case 0: rr = 1.0f; gg = f; bb = 0.0f; break;
    case 1: rr = q; gg = 1.0f; bb = 0.0f; break;
    case 2: rr = 0.0f; gg = 1.0f; bb = f; break;
    case 3: rr = 0.0f; gg = q; bb = 1.0f; break;
    case 4: rr = f; gg = 0.0f; bb = 1.0f; break;
    case 5: rr = 1.0f; gg = 0.0f; bb = q; break;
    }
    *r = (Uint8)(rr * 255.0f);
    *g = (Uint8)(gg * 255.0f);
    *b = (Uint8)(bb * 255.0f);
}

static void
custom_ui_blitterStatsChartFillGradient(e9ui_context_t *ctx,
                                        const SDL_Rect *rect,
                                        int gradientX,
                                        int gradientW)
{
    if (!ctx || !ctx->renderer || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    if (gradientW <= 0) {
        gradientW = rect->w;
    }
    int denom = gradientW > 1 ? (gradientW - 1) : 1;
    for (int dx = 0; dx < rect->w; ++dx) {
        int gx = (rect->x + dx) - gradientX;
        if (gx < 0) {
            gx = 0;
        }
        if (gx >= gradientW) {
            gx = gradientW - 1;
        }
        float t = (float)gx / (float)denom;
        float h = (1.0f / 3.0f) * (1.0f - t);
        Uint8 rr = 0;
        Uint8 gg = 0;
        Uint8 bb = 0;
        custom_ui_blitterStatsChartHueToRgb(h, &rr, &gg, &bb);
        SDL_SetRenderDrawColor(ctx->renderer, rr, gg, bb, 255);
        SDL_RenderDrawLine(ctx->renderer,
                           rect->x + dx,
                           rect->y,
                           rect->x + dx,
                           rect->y + rect->h - 1);
    }
}

static int
custom_ui_textboxLikeHeight(e9ui_context_t *ctx)
{
    TTF_Font *font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : (ctx ? ctx->font : NULL);
    int lineHeight = font ? TTF_FontHeight(font) : 16;
    if (lineHeight <= 0) {
        lineHeight = 16;
    }
    return lineHeight + 12;
}

static int
custom_ui_blitterStatsChartPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    if (!self || !self->state || !ctx) {
        return 0;
    }
    custom_ui_blitter_stats_chart_state_t *st = (custom_ui_blitter_stats_chart_state_t *)self->state;
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);
    int barH = custom_ui_textboxLikeHeight(ctx);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    int rowGap = e9ui_scale_px(ctx, st->rowGap);
    if (fontH <= 0) {
        fontH = 12;
    }
    int rowH = fontH > barH ? fontH : barH;
    return topPad + rowH + rowGap + rowH + bottomPad;
}

static void
custom_ui_blitterStatsChartLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static void
custom_ui_statsChartRenderBarRow(e9ui_context_t *ctx,
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
                                 int useTextClip)
{
    SDL_Color labelColor = custom_ui_blitterStatsChartLabelColor;
    SDL_Color textColor = custom_ui_blitterStatsChartTextColor;
    SDL_Color textShadow = custom_ui_blitterStatsChartTextShadowColor;
    int rowLabelY = rowY + (rowH - fontH) / 2;
    int rowBarY = rowY + (rowH - barHeight) / 2;

    {
        TTF_Font *labelFont = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
        int labelTextW = 0;
        if (labelFont) {
            TTF_SizeText(labelFont, labelText, &labelTextW, NULL);
        }
        int labelX = barX - labelGap - labelTextW;
        if (labelX < contentX) {
            labelX = contentX;
        }
        custom_ui_blitterStatsChartDrawText(ctx, labelFont, labelText, labelColor, labelX, rowLabelY);
    }

    SDL_Rect trackRect = { barX, rowBarY, barW, barHeight };
    SDL_Rect innerRect = trackRect;
    if (innerRect.w > 2) {
        innerRect.x += 1;
        innerRect.w -= 2;
    }
    if (innerRect.h > 2) {
        innerRect.y += 1;
        innerRect.h -= 2;
    }

    SDL_SetRenderDrawColor(ctx->renderer, 34, 40, 46, 255);
    SDL_RenderFillRect(ctx->renderer, &trackRect);
    if (innerRect.w > 0 && innerRect.h > 0) {
        SDL_SetRenderDrawColor(ctx->renderer, 22, 26, 31, 255);
        SDL_RenderFillRect(ctx->renderer, &innerRect);
    }

    int fillW = 0;
    if (hasData && valueMax > 0u && innerRect.w > 0) {
        uint64_t scaled = (uint64_t)valueUsed * (uint64_t)(uint32_t)innerRect.w;
        fillW = (int)((scaled + (uint64_t)(valueMax / 2u)) / (uint64_t)valueMax);
        if (fillW < 0) {
            fillW = 0;
        }
        if (fillW > innerRect.w) {
            fillW = innerRect.w;
        }
    }
    if (fillW > 0 && innerRect.h > 0) {
        SDL_Rect fillRect = { innerRect.x, innerRect.y, fillW, innerRect.h };
        if (useGradientFill) {
            custom_ui_blitterStatsChartFillGradient(ctx, &fillRect, innerRect.x, innerRect.w);
        } else {
            SDL_SetRenderDrawColor(ctx->renderer, solidFillColor.r, solidFillColor.g, solidFillColor.b, 255);
            SDL_RenderFillRect(ctx->renderer, &fillRect);
        }
    }

    SDL_SetRenderDrawColor(ctx->renderer, 64, 72, 82, 255);
    SDL_RenderDrawRect(ctx->renderer, &trackRect);

    if (ctx->font) {
        int tw = 0;
        int th = 0;
        if (!hasData) {
            custom_ui_statsChartMeasureText(ctx, ctx->font, "n/a", textColor, &tw, &th);
        } else {
            int usedW = 0;
            int usedH = 0;
            int spaceW = 0;
            int spaceH = 0;
            int suffixW = 0;
            int suffixH = 0;
            custom_ui_statsChartMeasureUint(ctx, ctx->font, valueUsed, textColor, &usedW, &usedH);
            custom_ui_statsChartMeasureText(ctx, ctx->font, " ", textColor, &spaceW, &spaceH);
            custom_ui_statsChartMeasureText(ctx, ctx->font, valueSuffix, textColor, &suffixW, &suffixH);
            tw = usedW + spaceW + suffixW;
            th = usedH;
            if (spaceH > th) {
                th = spaceH;
            }
            if (suffixH > th) {
                th = suffixH;
            }
        }
        if (tw > 0 && th > 0) {
            int tx = barX + (barW - tw) / 2;
            int ty = rowBarY + (barHeight - th) / 2;
            if (tx < barX + 2) {
                tx = barX + 2;
            }
            if (ty < rowBarY) {
                ty = rowBarY;
            }
            SDL_bool hadPrevClip = SDL_FALSE;
            SDL_Rect prevClip;
            if (useTextClip) {
                hadPrevClip = SDL_RenderIsClipEnabled(ctx->renderer);
                if (hadPrevClip) {
                    SDL_RenderGetClipRect(ctx->renderer, &prevClip);
                }
                int clipPad = e9ui_scale_px(ctx, 4);
                if (clipPad < 1) {
                    clipPad = 1;
                }
                SDL_Rect textClip = { barX + clipPad, rowBarY, barW - clipPad * 2, barHeight };
                if (textClip.w < 1) {
                    textClip.x = barX;
                    textClip.w = barW;
                }
                if (textClip.h < 1) {
                    textClip.y = rowBarY;
                    textClip.h = barHeight;
                }
                SDL_RenderSetClipRect(ctx->renderer, &textClip);
            }
            if (!hasData) {
                if (useTextShadow) {
                    custom_ui_blitterStatsChartDrawText(ctx, NULL, "n/a", textShadow, tx + 1, ty + 1);
                }
                custom_ui_blitterStatsChartDrawText(ctx, NULL, "n/a", textColor, tx, ty);
            } else {
                if (useTextShadow) {
                    custom_ui_statsChartDrawValueUsedSuffix(ctx,
                                                            ctx->font,
                                                            valueUsed,
                                                            valueSuffix,
                                                            textShadow,
                                                            tx + 1,
                                                            ty + 1);
                }
                custom_ui_statsChartDrawValueUsedSuffix(ctx,
                                                        ctx->font,
                                                        valueUsed,
                                                        valueSuffix,
                                                        textColor,
                                                        tx,
                                                        ty);
            }
            if (useTextClip) {
                if (hadPrevClip) {
                    SDL_RenderSetClipRect(ctx->renderer, &prevClip);
                } else {
                    SDL_RenderSetClipRect(ctx->renderer, NULL);
                }
            }
        }
    }
}

static void
custom_ui_blitterStatsChartRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx || !ctx->renderer) {
        return;
    }

    custom_ui_blitter_stats_chart_state_t *st = (custom_ui_blitter_stats_chart_state_t *)self->state;
    int leftInset = e9ui_scale_px(ctx, st->leftInset);
    int rightInset = e9ui_scale_px(ctx, st->rightInset);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    int rowGap = e9ui_scale_px(ctx, st->rowGap);
    int labelWidth = e9ui_scale_px(ctx, st->labelWidth);
    int labelGap = e9ui_scale_px(ctx, st->labelGap);
    int barHeight = custom_ui_textboxLikeHeight(ctx);
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);

    if (fontH <= 0) {
        fontH = 12;
    }
    int contentX = self->bounds.x + leftInset;
    int contentY = self->bounds.y + topPad;
    int contentW = self->bounds.w - leftInset - rightInset;
    if (contentW < 1) {
        contentW = 1;
    }

    int rowH = fontH > barHeight ? fontH : barHeight;

    int barX = contentX + labelWidth + labelGap;
    int barW = contentW - labelWidth - labelGap;
    if (barW < 1) {
        barW = 1;
    }
    int rowY = contentY;
    uint32_t blitsMax = 300u;
    custom_ui_statsChartRenderBarRow(ctx,
                                     contentX,
                                     barX,
                                     barW,
                                     barHeight,
                                     labelGap,
                                     rowY,
                                     rowH,
                                     fontH,
                                     "B/W",
                                     st->hasStats,
                                     st->wordsFrame,
                                     st->maxWordsEstimateFrame,
                                     "words/frame",
                                     1,
                                     custom_ui_dmaColorBlitter,
                                     1,
                                     1);
    rowY += rowH + rowGap;
    custom_ui_statsChartRenderBarRow(ctx,
                                     contentX,
                                     barX,
                                     barW,
                                     barHeight,
                                     labelGap,
                                     rowY,
                                     rowH,
                                     fontH,
                                     "Blits",
                                     st->hasStats,
                                     st->blitsFrame,
                                     blitsMax,
                                     "blits/frame",
                                     1,
                                     custom_ui_dmaColorBlitter,
                                     1,
                                     1);
    (void)bottomPad;
}

static void
custom_ui_blitterStatsChartDtor(e9ui_component_t *self, e9ui_context_t *ctx)
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

static e9ui_component_t *
custom_ui_blitterStatsChartMake(void)
{
    e9ui_component_t *comp = (e9ui_component_t *)alloc_calloc(1, sizeof(*comp));
    custom_ui_blitter_stats_chart_state_t *st =
        (custom_ui_blitter_stats_chart_state_t *)alloc_calloc(1, sizeof(*st));
    if (!comp || !st) {
        if (comp) {
            alloc_free(comp);
        }
        if (st) {
            alloc_free(st);
        }
        return NULL;
    }

    st->leftInset = 0;
    st->rightInset = 14;
    st->topPadding = 2;
    st->bottomPadding = 2;
    st->rowGap = 4;
    st->labelWidth = 78;
    st->labelGap = 8;
    st->barHeight = 0;

    comp->name = "custom_ui_blitter_stats_chart";
    comp->state = st;
    comp->preferredHeight = custom_ui_blitterStatsChartPreferredHeight;
    comp->layout = custom_ui_blitterStatsChartLayout;
    comp->render = custom_ui_blitterStatsChartRender;
    comp->dtor = custom_ui_blitterStatsChartDtor;
    return comp;
}

static int
custom_ui_copperStatsChartPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    if (!self || !self->state || !ctx) {
        return 0;
    }
    custom_ui_copper_stats_chart_state_t *st = (custom_ui_copper_stats_chart_state_t *)self->state;
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);
    int barH = custom_ui_textboxLikeHeight(ctx);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    if (fontH <= 0) {
        fontH = 12;
    }
    int rowH = fontH > barH ? fontH : barH;
    return topPad + rowH + bottomPad;
}

static void
custom_ui_copperStatsChartLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static void
custom_ui_copperStatsChartRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx || !ctx->renderer) {
        return;
    }

    custom_ui_copper_stats_chart_state_t *st = (custom_ui_copper_stats_chart_state_t *)self->state;
    int leftInset = e9ui_scale_px(ctx, st->leftInset);
    int rightInset = e9ui_scale_px(ctx, st->rightInset);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    int labelWidth = e9ui_scale_px(ctx, st->labelWidth);
    int labelGap = e9ui_scale_px(ctx, st->labelGap);
    int barHeight = custom_ui_textboxLikeHeight(ctx);
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);

    if (fontH <= 0) {
        fontH = 12;
    }
    int contentX = self->bounds.x + leftInset;
    int contentY = self->bounds.y + topPad;
    int contentW = self->bounds.w - leftInset - rightInset;
    if (contentW < 1) {
        contentW = 1;
    }

    int rowH = fontH > barHeight ? fontH : barHeight;
    int barX = contentX + labelWidth + labelGap;
    int barW = contentW - labelWidth - labelGap;
    if (barW < 1) {
        barW = 1;
    }
    custom_ui_statsChartRenderBarRow(ctx,
                                     contentX,
                                     barX,
                                     barW,
                                     barHeight,
                                     labelGap,
                                     contentY,
                                     rowH,
                                     fontH,
                                     "Copper",
                                     st->hasCopperStats,
                                     st->copperSlotsFrame,
                                     st->copperSlotsMaxFrame,
                                     "slots/frame",
                                     0,
                                     custom_ui_dmaColorCopper,
                                     0,
                                     0);
    (void)bottomPad;
}

static void
custom_ui_copperStatsChartDtor(e9ui_component_t *self, e9ui_context_t *ctx)
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

static e9ui_component_t *
custom_ui_copperStatsChartMake(void)
{
    e9ui_component_t *comp = (e9ui_component_t *)alloc_calloc(1, sizeof(*comp));
    custom_ui_copper_stats_chart_state_t *st =
        (custom_ui_copper_stats_chart_state_t *)alloc_calloc(1, sizeof(*st));
    if (!comp || !st) {
        if (comp) {
            alloc_free(comp);
        }
        if (st) {
            alloc_free(st);
        }
        return NULL;
    }

    st->leftInset = 0;
    st->rightInset = 14;
    st->topPadding = 2;
    st->bottomPadding = 2;
    st->rowGap = 4;
    st->labelWidth = 78;
    st->labelGap = 8;
    st->barHeight = 0;

    comp->name = "custom_ui_copper_stats_chart";
    comp->state = st;
    comp->preferredHeight = custom_ui_copperStatsChartPreferredHeight;
    comp->layout = custom_ui_copperStatsChartLayout;
    comp->render = custom_ui_copperStatsChartRender;
    comp->dtor = custom_ui_copperStatsChartDtor;
    return comp;
}

static int
custom_ui_blitterDmaStatsChartPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    if (!self || !self->state || !ctx) {
        return 0;
    }
    custom_ui_blitter_dma_stats_chart_state_t *st = (custom_ui_blitter_dma_stats_chart_state_t *)self->state;
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);
    int barH = custom_ui_textboxLikeHeight(ctx);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    if (fontH <= 0) {
        fontH = 12;
    }
    int rowH = fontH > barH ? fontH : barH;
    return topPad + rowH + bottomPad;
}

static void
custom_ui_blitterDmaStatsChartLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static void
custom_ui_blitterDmaStatsChartRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx || !ctx->renderer) {
        return;
    }

    custom_ui_blitter_dma_stats_chart_state_t *st = (custom_ui_blitter_dma_stats_chart_state_t *)self->state;
    int leftInset = e9ui_scale_px(ctx, st->leftInset);
    int rightInset = e9ui_scale_px(ctx, st->rightInset);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    int labelWidth = e9ui_scale_px(ctx, st->labelWidth);
    int labelGap = e9ui_scale_px(ctx, st->labelGap);
    int barHeight = custom_ui_textboxLikeHeight(ctx);
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);

    if (fontH <= 0) {
        fontH = 12;
    }
    int contentX = self->bounds.x + leftInset;
    int contentY = self->bounds.y + topPad;
    int contentW = self->bounds.w - leftInset - rightInset;
    if (contentW < 1) {
        contentW = 1;
    }

    int rowH = fontH > barHeight ? fontH : barHeight;
    int barX = contentX + labelWidth + labelGap;
    int barW = contentW - labelWidth - labelGap;
    if (barW < 1) {
        barW = 1;
    }
    custom_ui_statsChartRenderBarRow(ctx,
                                     contentX,
                                     barX,
                                     barW,
                                     barHeight,
                                     labelGap,
                                     contentY,
                                     rowH,
                                     fontH,
                                     "Blitter",
                                     st->hasBlitterDmaStats,
                                     st->blitterSlotsFrame,
                                     st->blitterSlotsMaxFrame,
                                     "slots/frame",
                                     0,
                                     custom_ui_dmaColorBlitter,
                                     0,
                                     0);
    (void)bottomPad;
}

static void
custom_ui_blitterDmaStatsChartDtor(e9ui_component_t *self, e9ui_context_t *ctx)
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

static e9ui_component_t *
custom_ui_blitterDmaStatsChartMake(void)
{
    e9ui_component_t *comp = (e9ui_component_t *)alloc_calloc(1, sizeof(*comp));
    custom_ui_blitter_dma_stats_chart_state_t *st =
        (custom_ui_blitter_dma_stats_chart_state_t *)alloc_calloc(1, sizeof(*st));
    if (!comp || !st) {
        if (comp) {
            alloc_free(comp);
        }
        if (st) {
            alloc_free(st);
        }
        return NULL;
    }

    st->leftInset = 0;
    st->rightInset = 14;
    st->topPadding = 2;
    st->bottomPadding = 2;
    st->rowGap = 4;
    st->labelWidth = 78;
    st->labelGap = 8;
    st->barHeight = 0;

    comp->name = "custom_ui_blitter_dma_stats_chart";
    comp->state = st;
    comp->preferredHeight = custom_ui_blitterDmaStatsChartPreferredHeight;
    comp->layout = custom_ui_blitterDmaStatsChartLayout;
    comp->render = custom_ui_blitterDmaStatsChartRender;
    comp->dtor = custom_ui_blitterDmaStatsChartDtor;
    return comp;
}

static int
custom_ui_cpuDmaStatsChartPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    if (!self || !self->state || !ctx) {
        return 0;
    }
    custom_ui_cpu_dma_stats_chart_state_t *st = (custom_ui_cpu_dma_stats_chart_state_t *)self->state;
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);
    int barH = custom_ui_textboxLikeHeight(ctx);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    if (fontH <= 0) {
        fontH = 12;
    }
    int rowH = fontH > barH ? fontH : barH;
    return topPad + rowH + bottomPad;
}

static void
custom_ui_cpuDmaStatsChartLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static void
custom_ui_cpuDmaStatsChartRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx || !ctx->renderer) {
        return;
    }

    custom_ui_cpu_dma_stats_chart_state_t *st = (custom_ui_cpu_dma_stats_chart_state_t *)self->state;
    int leftInset = e9ui_scale_px(ctx, st->leftInset);
    int rightInset = e9ui_scale_px(ctx, st->rightInset);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    int labelWidth = e9ui_scale_px(ctx, st->labelWidth);
    int labelGap = e9ui_scale_px(ctx, st->labelGap);
    int barHeight = custom_ui_textboxLikeHeight(ctx);
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);

    if (fontH <= 0) {
        fontH = 12;
    }
    int contentX = self->bounds.x + leftInset;
    int contentY = self->bounds.y + topPad;
    int contentW = self->bounds.w - leftInset - rightInset;
    if (contentW < 1) {
        contentW = 1;
    }

    int rowH = fontH > barHeight ? fontH : barHeight;
    int barX = contentX + labelWidth + labelGap;
    int barW = contentW - labelWidth - labelGap;
    if (barW < 1) {
        barW = 1;
    }
    custom_ui_statsChartRenderBarRow(ctx,
                                     contentX,
                                     barX,
                                     barW,
                                     barHeight,
                                     labelGap,
                                     contentY,
                                     rowH,
                                     fontH,
                                     "CPU",
                                     st->hasCpuDmaStats,
                                     st->cpuSlotsFrame,
                                     st->cpuSlotsMaxFrame,
                                     "slots/frame",
                                     0,
                                     custom_ui_dmaColorCpu,
                                     0,
                                     0);
    (void)bottomPad;
}

static void
custom_ui_cpuDmaStatsChartDtor(e9ui_component_t *self, e9ui_context_t *ctx)
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

static e9ui_component_t *
custom_ui_cpuDmaStatsChartMake(void)
{
    e9ui_component_t *comp = (e9ui_component_t *)alloc_calloc(1, sizeof(*comp));
    custom_ui_cpu_dma_stats_chart_state_t *st =
        (custom_ui_cpu_dma_stats_chart_state_t *)alloc_calloc(1, sizeof(*st));
    if (!comp || !st) {
        if (comp) {
            alloc_free(comp);
        }
        if (st) {
            alloc_free(st);
        }
        return NULL;
    }

    st->leftInset = 0;
    st->rightInset = 14;
    st->topPadding = 2;
    st->bottomPadding = 2;
    st->rowGap = 4;
    st->labelWidth = 78;
    st->labelGap = 8;
    st->barHeight = 0;

    comp->name = "custom_ui_cpu_dma_stats_chart";
    comp->state = st;
    comp->preferredHeight = custom_ui_cpuDmaStatsChartPreferredHeight;
    comp->layout = custom_ui_cpuDmaStatsChartLayout;
    comp->render = custom_ui_cpuDmaStatsChartRender;
    comp->dtor = custom_ui_cpuDmaStatsChartDtor;
    return comp;
}

static int
custom_ui_bitplaneDmaStatsChartPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    if (!self || !self->state || !ctx) {
        return 0;
    }
    custom_ui_bitplane_dma_stats_chart_state_t *st = (custom_ui_bitplane_dma_stats_chart_state_t *)self->state;
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);
    int barH = custom_ui_textboxLikeHeight(ctx);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    if (fontH <= 0) {
        fontH = 12;
    }
    int rowH = fontH > barH ? fontH : barH;
    return topPad + rowH + bottomPad;
}

static void
custom_ui_bitplaneDmaStatsChartLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static void
custom_ui_bitplaneDmaStatsChartRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx || !ctx->renderer) {
        return;
    }

    custom_ui_bitplane_dma_stats_chart_state_t *st = (custom_ui_bitplane_dma_stats_chart_state_t *)self->state;
    int leftInset = e9ui_scale_px(ctx, st->leftInset);
    int rightInset = e9ui_scale_px(ctx, st->rightInset);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    int labelWidth = e9ui_scale_px(ctx, st->labelWidth);
    int labelGap = e9ui_scale_px(ctx, st->labelGap);
    int barHeight = custom_ui_textboxLikeHeight(ctx);
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);

    if (fontH <= 0) {
        fontH = 12;
    }
    int contentX = self->bounds.x + leftInset;
    int contentY = self->bounds.y + topPad;
    int contentW = self->bounds.w - leftInset - rightInset;
    if (contentW < 1) {
        contentW = 1;
    }

    int rowH = fontH > barHeight ? fontH : barHeight;
    int barX = contentX + labelWidth + labelGap;
    int barW = contentW - labelWidth - labelGap;
    if (barW < 1) {
        barW = 1;
    }
    custom_ui_statsChartRenderBarRow(ctx,
                                     contentX,
                                     barX,
                                     barW,
                                     barHeight,
                                     labelGap,
                                     contentY,
                                     rowH,
                                     fontH,
                                     "Bitplane",
                                     st->hasBitplaneDmaStats,
                                     st->bitplaneSlotsFrame,
                                     st->bitplaneSlotsMaxFrame,
                                     "slots/frame",
                                     0,
                                     custom_ui_dmaColorBitplane,
                                     0,
                                     0);
    (void)bottomPad;
}

static void
custom_ui_bitplaneDmaStatsChartDtor(e9ui_component_t *self, e9ui_context_t *ctx)
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

static e9ui_component_t *
custom_ui_bitplaneDmaStatsChartMake(void)
{
    e9ui_component_t *comp = (e9ui_component_t *)alloc_calloc(1, sizeof(*comp));
    custom_ui_bitplane_dma_stats_chart_state_t *st =
        (custom_ui_bitplane_dma_stats_chart_state_t *)alloc_calloc(1, sizeof(*st));
    if (!comp || !st) {
        if (comp) {
            alloc_free(comp);
        }
        if (st) {
            alloc_free(st);
        }
        return NULL;
    }

    st->leftInset = 0;
    st->rightInset = 14;
    st->topPadding = 2;
    st->bottomPadding = 2;
    st->rowGap = 4;
    st->labelWidth = 78;
    st->labelGap = 8;
    st->barHeight = 0;

    comp->name = "custom_ui_bitplane_dma_stats_chart";
    comp->state = st;
    comp->preferredHeight = custom_ui_bitplaneDmaStatsChartPreferredHeight;
    comp->layout = custom_ui_bitplaneDmaStatsChartLayout;
    comp->render = custom_ui_bitplaneDmaStatsChartRender;
    comp->dtor = custom_ui_bitplaneDmaStatsChartDtor;
    return comp;
}

static int
custom_ui_spriteDmaStatsChartPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    if (!self || !self->state || !ctx) {
        return 0;
    }
    custom_ui_sprite_dma_stats_chart_state_t *st = (custom_ui_sprite_dma_stats_chart_state_t *)self->state;
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);
    int barH = custom_ui_textboxLikeHeight(ctx);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    if (fontH <= 0) {
        fontH = 12;
    }
    int rowH = fontH > barH ? fontH : barH;
    return topPad + rowH + bottomPad;
}

static void
custom_ui_spriteDmaStatsChartLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static void
custom_ui_spriteDmaStatsChartRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx || !ctx->renderer) {
        return;
    }

    custom_ui_sprite_dma_stats_chart_state_t *st = (custom_ui_sprite_dma_stats_chart_state_t *)self->state;
    int leftInset = e9ui_scale_px(ctx, st->leftInset);
    int rightInset = e9ui_scale_px(ctx, st->rightInset);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    int labelWidth = e9ui_scale_px(ctx, st->labelWidth);
    int labelGap = e9ui_scale_px(ctx, st->labelGap);
    int barHeight = custom_ui_textboxLikeHeight(ctx);
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);

    if (fontH <= 0) {
        fontH = 12;
    }
    int contentX = self->bounds.x + leftInset;
    int contentY = self->bounds.y + topPad;
    int contentW = self->bounds.w - leftInset - rightInset;
    if (contentW < 1) {
        contentW = 1;
    }

    int rowH = fontH > barHeight ? fontH : barHeight;
    int barX = contentX + labelWidth + labelGap;
    int barW = contentW - labelWidth - labelGap;
    if (barW < 1) {
        barW = 1;
    }
    custom_ui_statsChartRenderBarRow(ctx,
                                     contentX,
                                     barX,
                                     barW,
                                     barHeight,
                                     labelGap,
                                     contentY,
                                     rowH,
                                     fontH,
                                     "Sprite",
                                     st->hasSpriteDmaStats,
                                     st->spriteSlotsFrame,
                                     st->spriteSlotsMaxFrame,
                                     "slots/frame",
                                     0,
                                     custom_ui_dmaColorSprite,
                                     0,
                                     0);
    (void)bottomPad;
}

static void
custom_ui_spriteDmaStatsChartDtor(e9ui_component_t *self, e9ui_context_t *ctx)
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

static e9ui_component_t *
custom_ui_spriteDmaStatsChartMake(void)
{
    e9ui_component_t *comp = (e9ui_component_t *)alloc_calloc(1, sizeof(*comp));
    custom_ui_sprite_dma_stats_chart_state_t *st =
        (custom_ui_sprite_dma_stats_chart_state_t *)alloc_calloc(1, sizeof(*st));
    if (!comp || !st) {
        if (comp) {
            alloc_free(comp);
        }
        if (st) {
            alloc_free(st);
        }
        return NULL;
    }

    st->leftInset = 0;
    st->rightInset = 14;
    st->topPadding = 2;
    st->bottomPadding = 2;
    st->rowGap = 4;
    st->labelWidth = 78;
    st->labelGap = 8;
    st->barHeight = 0;

    comp->name = "custom_ui_sprite_dma_stats_chart";
    comp->state = st;
    comp->preferredHeight = custom_ui_spriteDmaStatsChartPreferredHeight;
    comp->layout = custom_ui_spriteDmaStatsChartLayout;
    comp->render = custom_ui_spriteDmaStatsChartRender;
    comp->dtor = custom_ui_spriteDmaStatsChartDtor;
    return comp;
}

static int
custom_ui_diskDmaStatsChartPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    if (!self || !self->state || !ctx) {
        return 0;
    }
    custom_ui_disk_dma_stats_chart_state_t *st = (custom_ui_disk_dma_stats_chart_state_t *)self->state;
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);
    int barH = custom_ui_textboxLikeHeight(ctx);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    if (fontH <= 0) {
        fontH = 12;
    }
    int rowH = fontH > barH ? fontH : barH;
    return topPad + rowH + bottomPad;
}

static void
custom_ui_diskDmaStatsChartLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static void
custom_ui_diskDmaStatsChartRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx || !ctx->renderer) {
        return;
    }

    custom_ui_disk_dma_stats_chart_state_t *st = (custom_ui_disk_dma_stats_chart_state_t *)self->state;
    int leftInset = e9ui_scale_px(ctx, st->leftInset);
    int rightInset = e9ui_scale_px(ctx, st->rightInset);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    int labelWidth = e9ui_scale_px(ctx, st->labelWidth);
    int labelGap = e9ui_scale_px(ctx, st->labelGap);
    int barHeight = custom_ui_textboxLikeHeight(ctx);
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);

    if (fontH <= 0) {
        fontH = 12;
    }
    int contentX = self->bounds.x + leftInset;
    int contentY = self->bounds.y + topPad;
    int contentW = self->bounds.w - leftInset - rightInset;
    if (contentW < 1) {
        contentW = 1;
    }

    int rowH = fontH > barHeight ? fontH : barHeight;
    int barX = contentX + labelWidth + labelGap;
    int barW = contentW - labelWidth - labelGap;
    if (barW < 1) {
        barW = 1;
    }
    custom_ui_statsChartRenderBarRow(ctx,
                                     contentX,
                                     barX,
                                     barW,
                                     barHeight,
                                     labelGap,
                                     contentY,
                                     rowH,
                                     fontH,
                                     "Disk",
                                     st->hasDiskDmaStats,
                                     st->diskSlotsFrame,
                                     st->diskSlotsMaxFrame,
                                     "slots/frame",
                                     0,
                                     custom_ui_dmaColorDisk,
                                     0,
                                     0);
    (void)bottomPad;
}

static void
custom_ui_diskDmaStatsChartDtor(e9ui_component_t *self, e9ui_context_t *ctx)
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

static e9ui_component_t *
custom_ui_diskDmaStatsChartMake(void)
{
    e9ui_component_t *comp = (e9ui_component_t *)alloc_calloc(1, sizeof(*comp));
    custom_ui_disk_dma_stats_chart_state_t *st =
        (custom_ui_disk_dma_stats_chart_state_t *)alloc_calloc(1, sizeof(*st));
    if (!comp || !st) {
        if (comp) {
            alloc_free(comp);
        }
        if (st) {
            alloc_free(st);
        }
        return NULL;
    }

    st->leftInset = 0;
    st->rightInset = 14;
    st->topPadding = 2;
    st->bottomPadding = 2;
    st->rowGap = 4;
    st->labelWidth = 78;
    st->labelGap = 8;
    st->barHeight = 0;

    comp->name = "custom_ui_disk_dma_stats_chart";
    comp->state = st;
    comp->preferredHeight = custom_ui_diskDmaStatsChartPreferredHeight;
    comp->layout = custom_ui_diskDmaStatsChartLayout;
    comp->render = custom_ui_diskDmaStatsChartRender;
    comp->dtor = custom_ui_diskDmaStatsChartDtor;
    return comp;
}

static int
custom_ui_audioDmaStatsChartPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    if (!self || !self->state || !ctx) {
        return 0;
    }
    custom_ui_audio_dma_stats_chart_state_t *st = (custom_ui_audio_dma_stats_chart_state_t *)self->state;
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);
    int barH = custom_ui_textboxLikeHeight(ctx);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    if (fontH <= 0) {
        fontH = 12;
    }
    int rowH = fontH > barH ? fontH : barH;
    return topPad + rowH + bottomPad;
}

static void
custom_ui_audioDmaStatsChartLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static void
custom_ui_audioDmaStatsChartRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx || !ctx->renderer) {
        return;
    }

    custom_ui_audio_dma_stats_chart_state_t *st = (custom_ui_audio_dma_stats_chart_state_t *)self->state;
    int leftInset = e9ui_scale_px(ctx, st->leftInset);
    int rightInset = e9ui_scale_px(ctx, st->rightInset);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    int labelWidth = e9ui_scale_px(ctx, st->labelWidth);
    int labelGap = e9ui_scale_px(ctx, st->labelGap);
    int barHeight = custom_ui_textboxLikeHeight(ctx);
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);

    if (fontH <= 0) {
        fontH = 12;
    }
    int contentX = self->bounds.x + leftInset;
    int contentY = self->bounds.y + topPad;
    int contentW = self->bounds.w - leftInset - rightInset;
    if (contentW < 1) {
        contentW = 1;
    }

    int rowH = fontH > barHeight ? fontH : barHeight;
    int barX = contentX + labelWidth + labelGap;
    int barW = contentW - labelWidth - labelGap;
    if (barW < 1) {
        barW = 1;
    }
    custom_ui_statsChartRenderBarRow(ctx,
                                     contentX,
                                     barX,
                                     barW,
                                     barHeight,
                                     labelGap,
                                     contentY,
                                     rowH,
                                     fontH,
                                     "Audio",
                                     st->hasAudioDmaStats,
                                     st->audioSlotsFrame,
                                     st->audioSlotsMaxFrame,
                                     "slots/frame",
                                     0,
                                     custom_ui_dmaColorAudio,
                                     0,
                                     0);
    (void)bottomPad;
}

static void
custom_ui_audioDmaStatsChartDtor(e9ui_component_t *self, e9ui_context_t *ctx)
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

static e9ui_component_t *
custom_ui_audioDmaStatsChartMake(void)
{
    e9ui_component_t *comp = (e9ui_component_t *)alloc_calloc(1, sizeof(*comp));
    custom_ui_audio_dma_stats_chart_state_t *st =
        (custom_ui_audio_dma_stats_chart_state_t *)alloc_calloc(1, sizeof(*st));
    if (!comp || !st) {
        if (comp) {
            alloc_free(comp);
        }
        if (st) {
            alloc_free(st);
        }
        return NULL;
    }

    st->leftInset = 0;
    st->rightInset = 14;
    st->topPadding = 2;
    st->bottomPadding = 2;
    st->rowGap = 4;
    st->labelWidth = 78;
    st->labelGap = 8;
    st->barHeight = 0;

    comp->name = "custom_ui_audio_dma_stats_chart";
    comp->state = st;
    comp->preferredHeight = custom_ui_audioDmaStatsChartPreferredHeight;
    comp->layout = custom_ui_audioDmaStatsChartLayout;
    comp->render = custom_ui_audioDmaStatsChartRender;
    comp->dtor = custom_ui_audioDmaStatsChartDtor;
    return comp;
}

static int
custom_ui_otherDmaStatsChartPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    if (!self || !self->state || !ctx) {
        return 0;
    }
    custom_ui_other_dma_stats_chart_state_t *st = (custom_ui_other_dma_stats_chart_state_t *)self->state;
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);
    int barH = custom_ui_textboxLikeHeight(ctx);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    if (fontH <= 0) {
        fontH = 12;
    }
    int rowH = fontH > barH ? fontH : barH;
    return topPad + rowH + bottomPad;
}

static void
custom_ui_otherDmaStatsChartLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static void
custom_ui_otherDmaStatsChartRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx || !ctx->renderer) {
        return;
    }

    custom_ui_other_dma_stats_chart_state_t *st = (custom_ui_other_dma_stats_chart_state_t *)self->state;
    int leftInset = e9ui_scale_px(ctx, st->leftInset);
    int rightInset = e9ui_scale_px(ctx, st->rightInset);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    int labelWidth = e9ui_scale_px(ctx, st->labelWidth);
    int labelGap = e9ui_scale_px(ctx, st->labelGap);
    int barHeight = custom_ui_textboxLikeHeight(ctx);
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);

    if (fontH <= 0) {
        fontH = 12;
    }
    int contentX = self->bounds.x + leftInset;
    int contentY = self->bounds.y + topPad;
    int contentW = self->bounds.w - leftInset - rightInset;
    if (contentW < 1) {
        contentW = 1;
    }

    int rowH = fontH > barHeight ? fontH : barHeight;
    int barX = contentX + labelWidth + labelGap;
    int barW = contentW - labelWidth - labelGap;
    if (barW < 1) {
        barW = 1;
    }
    custom_ui_statsChartRenderBarRow(ctx,
                                     contentX,
                                     barX,
                                     barW,
                                     barHeight,
                                     labelGap,
                                     contentY,
                                     rowH,
                                     fontH,
                                     "Other",
                                     st->hasOtherDmaStats,
                                     st->otherSlotsFrame,
                                     st->otherSlotsMaxFrame,
                                     "slots/frame",
                                     0,
                                     custom_ui_dmaColorOther,
                                     0,
                                     0);
    (void)bottomPad;
}

static void
custom_ui_otherDmaStatsChartDtor(e9ui_component_t *self, e9ui_context_t *ctx)
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

static e9ui_component_t *
custom_ui_otherDmaStatsChartMake(void)
{
    e9ui_component_t *comp = (e9ui_component_t *)alloc_calloc(1, sizeof(*comp));
    custom_ui_other_dma_stats_chart_state_t *st =
        (custom_ui_other_dma_stats_chart_state_t *)alloc_calloc(1, sizeof(*st));
    if (!comp || !st) {
        if (comp) {
            alloc_free(comp);
        }
        if (st) {
            alloc_free(st);
        }
        return NULL;
    }

    st->leftInset = 0;
    st->rightInset = 14;
    st->topPadding = 2;
    st->bottomPadding = 2;
    st->rowGap = 4;
    st->labelWidth = 78;
    st->labelGap = 8;
    st->barHeight = 0;

    comp->name = "custom_ui_other_dma_stats_chart";
    comp->state = st;
    comp->preferredHeight = custom_ui_otherDmaStatsChartPreferredHeight;
    comp->layout = custom_ui_otherDmaStatsChartLayout;
    comp->render = custom_ui_otherDmaStatsChartRender;
    comp->dtor = custom_ui_otherDmaStatsChartDtor;
    return comp;
}

static int
custom_ui_idleDmaStatsChartPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    if (!self || !self->state || !ctx) {
        return 0;
    }
    custom_ui_idle_dma_stats_chart_state_t *st = (custom_ui_idle_dma_stats_chart_state_t *)self->state;
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);
    int barH = custom_ui_textboxLikeHeight(ctx);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    if (fontH <= 0) {
        fontH = 12;
    }
    int rowH = fontH > barH ? fontH : barH;
    return topPad + rowH + bottomPad;
}

static void
custom_ui_idleDmaStatsChartLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static void
custom_ui_idleDmaStatsChartRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx || !ctx->renderer) {
        return;
    }

    custom_ui_idle_dma_stats_chart_state_t *st = (custom_ui_idle_dma_stats_chart_state_t *)self->state;
    int leftInset = e9ui_scale_px(ctx, st->leftInset);
    int rightInset = e9ui_scale_px(ctx, st->rightInset);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    int labelWidth = e9ui_scale_px(ctx, st->labelWidth);
    int labelGap = e9ui_scale_px(ctx, st->labelGap);
    int barHeight = custom_ui_textboxLikeHeight(ctx);
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);

    if (fontH <= 0) {
        fontH = 12;
    }
    int contentX = self->bounds.x + leftInset;
    int contentY = self->bounds.y + topPad;
    int contentW = self->bounds.w - leftInset - rightInset;
    if (contentW < 1) {
        contentW = 1;
    }

    int rowH = fontH > barHeight ? fontH : barHeight;
    int barX = contentX + labelWidth + labelGap;
    int barW = contentW - labelWidth - labelGap;
    if (barW < 1) {
        barW = 1;
    }
    custom_ui_statsChartRenderBarRow(ctx,
                                     contentX,
                                     barX,
                                     barW,
                                     barHeight,
                                     labelGap,
                                     contentY,
                                     rowH,
                                     fontH,
                                     "Idle",
                                     st->hasIdleDmaStats,
                                     st->idleSlotsFrame,
                                     st->idleSlotsMaxFrame,
                                     "slots/frame",
                                     0,
                                     custom_ui_dmaColorIdle,
                                     0,
                                     0);
    (void)bottomPad;
}

static void
custom_ui_idleDmaStatsChartDtor(e9ui_component_t *self, e9ui_context_t *ctx)
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

static e9ui_component_t *
custom_ui_idleDmaStatsChartMake(void)
{
    e9ui_component_t *comp = (e9ui_component_t *)alloc_calloc(1, sizeof(*comp));
    custom_ui_idle_dma_stats_chart_state_t *st =
        (custom_ui_idle_dma_stats_chart_state_t *)alloc_calloc(1, sizeof(*st));
    if (!comp || !st) {
        if (comp) {
            alloc_free(comp);
        }
        if (st) {
            alloc_free(st);
        }
        return NULL;
    }

    st->leftInset = 0;
    st->rightInset = 14;
    st->topPadding = 2;
    st->bottomPadding = 2;
    st->rowGap = 4;
    st->labelWidth = 78;
    st->labelGap = 8;
    st->barHeight = 0;

    comp->name = "custom_ui_idle_dma_stats_chart";
    comp->state = st;
    comp->preferredHeight = custom_ui_idleDmaStatsChartPreferredHeight;
    comp->layout = custom_ui_idleDmaStatsChartLayout;
    comp->render = custom_ui_idleDmaStatsChartRender;
    comp->dtor = custom_ui_idleDmaStatsChartDtor;
    return comp;
}

static int
custom_ui_dmaTotalMixChartPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    if (!self || !self->state || !ctx) {
        return 0;
    }
    custom_ui_dma_total_mix_chart_state_t *st = (custom_ui_dma_total_mix_chart_state_t *)self->state;
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);
    int barH = custom_ui_textboxLikeHeight(ctx);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    if (fontH <= 0) {
        fontH = 12;
    }
    int rowH = fontH > barH ? fontH : barH;
    return topPad + rowH + bottomPad;
}

static void
custom_ui_dmaTotalMixChartLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static void
custom_ui_dmaTotalMixChartRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx || !ctx->renderer) {
        return;
    }

    custom_ui_dma_total_mix_chart_state_t *st = (custom_ui_dma_total_mix_chart_state_t *)self->state;
    int leftInset = e9ui_scale_px(ctx, st->leftInset);
    int rightInset = e9ui_scale_px(ctx, st->rightInset);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    int labelWidth = e9ui_scale_px(ctx, st->labelWidth);
    int labelGap = e9ui_scale_px(ctx, st->labelGap);
    int barHeight = custom_ui_textboxLikeHeight(ctx);
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);
    if (fontH <= 0) {
        fontH = 12;
    }

    int contentX = self->bounds.x + leftInset;
    int contentY = self->bounds.y + topPad;
    int contentW = self->bounds.w - leftInset - rightInset;
    if (contentW < 1) {
        contentW = 1;
    }

    int rowH = fontH > barHeight ? fontH : barHeight;
    int barX = contentX + labelWidth + labelGap;
    int barW = contentW - labelWidth - labelGap;
    if (barW < 1) {
        barW = 1;
    }
    {
        TTF_Font *labelFont = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
        int labelTextW = 0;
        if (labelFont) {
            TTF_SizeText(labelFont, "Total", &labelTextW, NULL);
        }
        int labelX = barX - labelGap - labelTextW;
        if (labelX < contentX) {
            labelX = contentX;
        }
        custom_ui_blitterStatsChartDrawText(ctx,
                                            labelFont,
                                            "Total",
                                            custom_ui_blitterStatsChartLabelColor,
                                            labelX,
                                            contentY + (rowH - fontH) / 2);
    }

    SDL_Rect trackRect = { barX, contentY + (rowH - barHeight) / 2, barW, barHeight };
    SDL_Rect innerRect = trackRect;
    if (innerRect.w > 2) {
        innerRect.x += 1;
        innerRect.w -= 2;
    }
    if (innerRect.h > 2) {
        innerRect.y += 1;
        innerRect.h -= 2;
    }
    SDL_SetRenderDrawColor(ctx->renderer, 34, 40, 46, 255);
    SDL_RenderFillRect(ctx->renderer, &trackRect);
    if (innerRect.w > 0 && innerRect.h > 0) {
        SDL_SetRenderDrawColor(ctx->renderer, 22, 26, 31, 255);
        SDL_RenderFillRect(ctx->renderer, &innerRect);
    }

    if (st->hasStats && st->totalSlotsMaxFrame > 0u && innerRect.w > 0 && innerRect.h > 0) {
        const uint32_t values[] = {
            st->cpuSlotsFrame,
            st->copperSlotsFrame,
            st->audioSlotsFrame,
            st->blitterSlotsFrame,
            st->bitplaneSlotsFrame,
            st->spriteSlotsFrame,
            st->diskSlotsFrame,
            st->otherSlotsFrame,
            st->idleSlotsFrame
        };
        const SDL_Color colors[] = {
            custom_ui_dmaColorCpu,
            custom_ui_dmaColorCopper,
            custom_ui_dmaColorAudio,
            custom_ui_dmaColorBlitter,
            custom_ui_dmaColorBitplane,
            custom_ui_dmaColorSprite,
            custom_ui_dmaColorDisk,
            custom_ui_dmaColorOther,
            custom_ui_dmaColorIdle
        };
        uint64_t accum = 0u;
        int drawX = innerRect.x;
        for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
            uint64_t prev = accum;
            accum += (uint64_t)values[i];
            if (accum > (uint64_t)st->totalSlotsMaxFrame) {
                accum = (uint64_t)st->totalSlotsMaxFrame;
            }
            int x0 = innerRect.x + (int)((prev * (uint64_t)(uint32_t)innerRect.w) / (uint64_t)st->totalSlotsMaxFrame);
            int x1 = innerRect.x + (int)((accum * (uint64_t)(uint32_t)innerRect.w) / (uint64_t)st->totalSlotsMaxFrame);
            if (x1 <= x0) {
                continue;
            }
            SDL_Rect seg = { x0, innerRect.y, x1 - x0, innerRect.h };
            SDL_SetRenderDrawColor(ctx->renderer, colors[i].r, colors[i].g, colors[i].b, 255);
            SDL_RenderFillRect(ctx->renderer, &seg);
            drawX = x1;
        }
        if (drawX < innerRect.x + innerRect.w) {
            SDL_Rect seg = { drawX, innerRect.y, innerRect.x + innerRect.w - drawX, innerRect.h };
            SDL_SetRenderDrawColor(ctx->renderer, custom_ui_dmaColorIdle.r, custom_ui_dmaColorIdle.g, custom_ui_dmaColorIdle.b, 255);
            SDL_RenderFillRect(ctx->renderer, &seg);
        }
    }

    SDL_SetRenderDrawColor(ctx->renderer, 64, 72, 82, 255);
    SDL_RenderDrawRect(ctx->renderer, &trackRect);
    (void)bottomPad;
}

static void
custom_ui_dmaTotalMixChartDtor(e9ui_component_t *self, e9ui_context_t *ctx)
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

static e9ui_component_t *
custom_ui_dmaTotalMixChartMake(void)
{
    e9ui_component_t *comp = (e9ui_component_t *)alloc_calloc(1, sizeof(*comp));
    custom_ui_dma_total_mix_chart_state_t *st =
        (custom_ui_dma_total_mix_chart_state_t *)alloc_calloc(1, sizeof(*st));
    if (!comp || !st) {
        if (comp) {
            alloc_free(comp);
        }
        if (st) {
            alloc_free(st);
        }
        return NULL;
    }

    st->leftInset = 0;
    st->rightInset = 14;
    st->topPadding = 2;
    st->bottomPadding = 2;
    st->rowGap = 4;
    st->labelWidth = 78;
    st->labelGap = 8;
    st->barHeight = 0;

    comp->name = "custom_ui_dma_total_mix_chart";
    comp->state = st;
    comp->preferredHeight = custom_ui_dmaTotalMixChartPreferredHeight;
    comp->layout = custom_ui_dmaTotalMixChartLayout;
    comp->render = custom_ui_dmaTotalMixChartRender;
    comp->dtor = custom_ui_dmaTotalMixChartDtor;
    return comp;
}

static void
custom_ui_enableDmaDebugForCopperStats(custom_ui_state_t *ui)
{
    if (!ui || ui->dmaDebugAutoEnabled) {
        return;
    }
    int *debugDma = NULL;
    if (!libretro_host_debugGetAmigaDebugDmaAddr(&debugDma) || !debugDma) {
        return;
    }
    if (*debugDma == 0) {
        ui->dmaDebugAutoPrevValue = 0;
        *debugDma = (int)E9K_DEBUG_AMI_DMA_DEBUG_MODE_COLLECT_ONLY;
        ui->dmaDebugAutoEnabled = 1;
    }
}

static void
custom_ui_restoreDmaDebugForCopperStats(custom_ui_state_t *ui)
{
    if (!ui || !ui->dmaDebugAutoEnabled) {
        return;
    }
    int *debugDma = NULL;
    if (libretro_host_debugGetAmigaDebugDmaAddr(&debugDma) && debugDma) {
        if (*debugDma == (int)E9K_DEBUG_AMI_DMA_DEBUG_MODE_COLLECT_ONLY) {
            *debugDma = ui->dmaDebugAutoPrevValue;
        }
    }
    ui->dmaDebugAutoEnabled = 0;
}

static void
custom_ui_updateBlitterStatsChart(custom_ui_state_t *ui)
{
    if (!ui) {
        return;
    }

    e9k_debug_ami_blitter_vis_stats_t stats;
    int hasStats = 0;
    if (emu_ami_getBlitterVisLatestStats(&stats) ||
        libretro_host_debugAmiReadBlitterVisStats(&stats)) {
        hasStats = 1;
    }

    if (ui->blitterVisStatsChart) {
        custom_ui_blitterStatsChartSetValues(ui->blitterVisStatsChart,
                                             hasStats,
                                             hasStats ? stats.writesThisFrame : 0u,
                                             hasStats ? (stats.writeBytesMaxEstimateFrame / 2u) : 0u,
                                             hasStats ? stats.blitsThisFrame : 0u);
    }
}

static void
custom_ui_updateCopperStatsChart(custom_ui_state_t *ui)
{
    if (!ui) {
        return;
    }

    const e9k_debug_ami_dma_debug_frame_view_t *probedFrame = NULL;
    int haveProbe = 0;
    if (ui->dmaStatsEnabled) {
        probedFrame = libretro_host_debugAmiGetDmaDebugFrameView(
            E9K_DEBUG_AMI_DMA_DEBUG_FRAME_LATEST_COMPLETE);
        if (probedFrame &&
            probedFrame->records &&
            probedFrame->info.recordCount > 0u &&
            probedFrame->info.frameNumber >= 0) {
            haveProbe = 1;
            if (ui->dmaStatsCacheValid &&
                ui->dmaStatsCacheFrameSelect == (int)E9K_DEBUG_AMI_DMA_DEBUG_FRAME_LATEST_COMPLETE &&
                ui->dmaStatsCacheFrameNumber == probedFrame->info.frameNumber) {
                return;
            }
        }
    }

    int hasCopperStats = 0;
    uint32_t copperSlotsFrame = 0u;
    uint32_t copperSlotsMaxFrame = 0u;
    int hasBlitterDmaStats = 0;
    uint32_t blitterSlotsFrame = 0u;
    uint32_t blitterSlotsMaxFrame = 0u;
    int hasCpuDmaStats = 0;
    uint32_t cpuSlotsFrame = 0u;
    uint32_t cpuSlotsMaxFrame = 0u;
    int hasBitplaneDmaStats = 0;
    uint32_t bitplaneSlotsFrame = 0u;
    uint32_t bitplaneSlotsMaxFrame = 0u;
    int hasSpriteDmaStats = 0;
    uint32_t spriteSlotsFrame = 0u;
    uint32_t spriteSlotsMaxFrame = 0u;
    int hasDiskDmaStats = 0;
    uint32_t diskSlotsFrame = 0u;
    uint32_t diskSlotsMaxFrame = 0u;
    int hasAudioDmaStats = 0;
    uint32_t audioSlotsFrame = 0u;
    uint32_t audioSlotsMaxFrame = 0u;
    int hasOtherDmaStats = 0;
    uint32_t otherSlotsFrame = 0u;
    uint32_t otherSlotsMaxFrame = 0u;
    int hasIdleDmaStats = 0;
    uint32_t idleSlotsFrame = 0u;
    uint32_t idleSlotsMaxFrame = 0u;
    int hasDmaTotalStats = 0;
    uint32_t dmaTotalSlotsFrame = 0u;
    uint32_t dmaTotalSlotsMaxFrame = 0u;
    uint32_t dmaSlotsAvailableFrame = 0u;
    if (ui->dmaStatsEnabled) {
        int *debugDma = NULL;
        libretro_host_debugGetAmigaDebugDmaAddr(&debugDma);
        uint32_t dmaFrameSelect = E9K_DEBUG_AMI_DMA_DEBUG_FRAME_LATEST_COMPLETE;
        const e9k_debug_ami_dma_debug_frame_view_t *dmaFrame = probedFrame;
        if (!haveProbe) {
            dmaFrame = libretro_host_debugAmiGetDmaDebugFrameView(dmaFrameSelect);
        }
        if (dmaFrame && dmaFrame->records && dmaFrame->info.recordCount > 0u) {
            size_t readTotal = dmaFrame->info.recordCount;
            e9k_debug_ami_dma_debug_frame_info_t readInfo = dmaFrame->info;
            const e9k_debug_ami_dma_debug_raw_record_t *records = dmaFrame->records;
            int stride = readInfo.hposCount;
            if (stride <= 0) {
                stride = 288;
            }
            size_t rowCount = readTotal / (size_t)stride;
            int xLimit = stride;
            int yLimit = (int)rowCount - 1;
            int videoLineCount = 0;
            if (libretro_host_debugAmiGetVideoLineCount(&videoLineCount) && videoLineCount > 0) {
                int debugDmaMode = (debugDma && *debugDma > 0) ? *debugDma : 0;
                int visibleMax = videoLineCount - 1;
                if (debugDmaMode == (int)E9K_DEBUG_AMI_DMA_DEBUG_MODE_COLLECT_ONLY ||
                    debugDmaMode == (int)E9K_DEBUG_AMI_DMA_DEBUG_MODE_VIDEO_SYNC) {
                    // These modes do not use the legacy core-side overlay scaling.
                } else if (debugDmaMode >= 4) {
                    visibleMax /= 2;
                } else {
                    visibleMax -= 8;
                }
                if (visibleMax < 0) {
                    visibleMax = 0;
                }
                if (yLimit > visibleMax) {
                    yLimit = visibleMax;
                }
            }
            if (yLimit >= (int)rowCount) {
                yLimit = (int)rowCount - 1;
            }
            if (yLimit < -1) {
                yLimit = -1;
            }
            for (int row = 0; row <= yLimit; ++row) {
                size_t rowBase = (size_t)row * (size_t)stride;
                for (int x = 0; x < xLimit; ++x) {
                    const e9k_debug_ami_dma_debug_raw_record_t *rec = &records[rowBase + (size_t)x];
                    if (rec->end) {
                        break;
                    }
                    if (rec->reg != 0xffffu) {
                        switch (rec->type) {
                        case CUSTOM_UI_DMA_RECORD_COPPER:
                            if (copperSlotsFrame < UINT32_MAX) {
                                copperSlotsFrame++;
                            }
                            continue;
                        case CUSTOM_UI_DMA_RECORD_BLITTER:
                            if (blitterSlotsFrame < UINT32_MAX) {
                                blitterSlotsFrame++;
                            }
                            continue;
                        case CUSTOM_UI_DMA_RECORD_CPU:
                            if (cpuSlotsFrame < UINT32_MAX) {
                                cpuSlotsFrame++;
                            }
                            continue;
                        case CUSTOM_UI_DMA_RECORD_BITPLANE:
                            if (bitplaneSlotsFrame < UINT32_MAX) {
                                bitplaneSlotsFrame++;
                            }
                            continue;
                        case CUSTOM_UI_DMA_RECORD_SPRITE:
                            if (spriteSlotsFrame < UINT32_MAX) {
                                spriteSlotsFrame++;
                            }
                            continue;
                        case CUSTOM_UI_DMA_RECORD_DISK:
                            if (diskSlotsFrame < UINT32_MAX) {
                                diskSlotsFrame++;
                            }
                            continue;
                        case CUSTOM_UI_DMA_RECORD_AUDIO:
                            if (audioSlotsFrame < UINT32_MAX) {
                                audioSlotsFrame++;
                            }
                            continue;
                        default:
                            break;
                        }
                    }
                    if (rec->type == (int16_t)CUSTOM_UI_DMA_RECORD_REFRESH ||
                        rec->type == (int16_t)CUSTOM_UI_DMA_RECORD_CONFLICT ||
                        rec->type != 0) {
                        if (otherSlotsFrame < UINT32_MAX) {
                            otherSlotsFrame++;
                        }
                    } else {
                        if (idleSlotsFrame < UINT32_MAX) {
                            idleSlotsFrame++;
                        }
                    }
                }
            }
            if (xLimit > 0 && yLimit >= 0) {
                        uint64_t max64 = (uint64_t)(uint32_t)(yLimit + 1) *
                                         (uint64_t)((uint32_t)xLimit / 2u);
                        uint64_t totalAvailable64 = (uint64_t)(uint32_t)(yLimit + 1) *
                                                    (uint64_t)(uint32_t)xLimit;
                        if (max64 > 0xffffffffu) {
                            max64 = 0xffffffffu;
                        }
                        if (totalAvailable64 > 0xffffffffu) {
                            totalAvailable64 = 0xffffffffu;
                        }
                        copperSlotsMaxFrame = (uint32_t)max64;
                        hasCopperStats = 1;
                        blitterSlotsMaxFrame = (uint32_t)max64;
                        hasBlitterDmaStats = 1;
                        cpuSlotsMaxFrame = (uint32_t)max64;
                        hasCpuDmaStats = 1;
                        bitplaneSlotsMaxFrame = (uint32_t)max64;
                        hasBitplaneDmaStats = 1;
                        spriteSlotsMaxFrame = (uint32_t)max64;
                        hasSpriteDmaStats = 1;
                        diskSlotsMaxFrame = (uint32_t)max64;
                        hasDiskDmaStats = 1;
                        audioSlotsMaxFrame = (uint32_t)max64;
                        hasAudioDmaStats = 1;
                        otherSlotsMaxFrame = (uint32_t)totalAvailable64;
                        hasOtherDmaStats = 1;
                        idleSlotsMaxFrame = (uint32_t)totalAvailable64;
                        hasIdleDmaStats = 1;
                        dmaSlotsAvailableFrame = (uint32_t)totalAvailable64;
            }
            if (!hasCopperStats &&
                readInfo.hposCount > 0 &&
                libretro_host_debugAmiGetVideoLineCount(&videoLineCount) &&
                videoLineCount > 0) {
                        uint64_t max64 = (uint64_t)(uint32_t)videoLineCount *
                                         (uint64_t)((uint32_t)readInfo.hposCount / 2u);
                        uint64_t totalAvailable64 = (uint64_t)(uint32_t)videoLineCount *
                                                    (uint64_t)(uint32_t)readInfo.hposCount;
                        if (max64 > 0xffffffffu) {
                            max64 = 0xffffffffu;
                        }
                        if (totalAvailable64 > 0xffffffffu) {
                            totalAvailable64 = 0xffffffffu;
                        }
                        copperSlotsMaxFrame = (uint32_t)max64;
                        hasCopperStats = 1;
                        blitterSlotsMaxFrame = (uint32_t)max64;
                        hasBlitterDmaStats = 1;
                        cpuSlotsMaxFrame = (uint32_t)max64;
                        hasCpuDmaStats = 1;
                        bitplaneSlotsMaxFrame = (uint32_t)max64;
                        hasBitplaneDmaStats = 1;
                        spriteSlotsMaxFrame = (uint32_t)max64;
                        hasSpriteDmaStats = 1;
                        diskSlotsMaxFrame = (uint32_t)max64;
                        hasDiskDmaStats = 1;
                        audioSlotsMaxFrame = (uint32_t)max64;
                        hasAudioDmaStats = 1;
                        otherSlotsMaxFrame = (uint32_t)totalAvailable64;
                        hasOtherDmaStats = 1;
                        idleSlotsMaxFrame = (uint32_t)totalAvailable64;
                        hasIdleDmaStats = 1;
                        dmaSlotsAvailableFrame = (uint32_t)totalAvailable64;
            }
        }
        if (dmaFrame &&
            dmaFrameSelect == E9K_DEBUG_AMI_DMA_DEBUG_FRAME_LATEST_COMPLETE &&
            dmaFrame->info.frameNumber >= 0) {
            ui->dmaStatsCacheValid = 1;
            ui->dmaStatsCacheFrameSelect = (int)dmaFrameSelect;
            ui->dmaStatsCacheFrameNumber = dmaFrame->info.frameNumber;
        } else {
            ui->dmaStatsCacheValid = 0;
            ui->dmaStatsCacheFrameSelect = 0;
            ui->dmaStatsCacheFrameNumber = -1;
        }
    } else {
        ui->dmaStatsCacheValid = 0;
        ui->dmaStatsCacheFrameSelect = 0;
        ui->dmaStatsCacheFrameNumber = -1;
    }
    if (hasCopperStats && copperSlotsFrame > copperSlotsMaxFrame) {
        copperSlotsMaxFrame = copperSlotsFrame;
    }
    if (hasBlitterDmaStats && blitterSlotsFrame > blitterSlotsMaxFrame) {
        blitterSlotsMaxFrame = blitterSlotsFrame;
    }
    if (hasCpuDmaStats && cpuSlotsFrame > cpuSlotsMaxFrame) {
        cpuSlotsMaxFrame = cpuSlotsFrame;
    }
    if (hasBitplaneDmaStats && bitplaneSlotsFrame > bitplaneSlotsMaxFrame) {
        bitplaneSlotsMaxFrame = bitplaneSlotsFrame;
    }
    if (hasSpriteDmaStats && spriteSlotsFrame > spriteSlotsMaxFrame) {
        spriteSlotsMaxFrame = spriteSlotsFrame;
    }
    if (hasDiskDmaStats && diskSlotsFrame > diskSlotsMaxFrame) {
        diskSlotsMaxFrame = diskSlotsFrame;
    }
    if (hasAudioDmaStats && audioSlotsFrame > audioSlotsMaxFrame) {
        audioSlotsMaxFrame = audioSlotsFrame;
    }
    if (hasOtherDmaStats && otherSlotsFrame > otherSlotsMaxFrame) {
        otherSlotsMaxFrame = otherSlotsFrame;
    }
    if (hasIdleDmaStats && idleSlotsFrame > idleSlotsMaxFrame) {
        idleSlotsMaxFrame = idleSlotsFrame;
    }
    if (hasCopperStats) {
        hasDmaTotalStats = 1;
        dmaTotalSlotsFrame = copperSlotsFrame +
                             blitterSlotsFrame +
                             cpuSlotsFrame +
                             bitplaneSlotsFrame +
                             spriteSlotsFrame +
                             diskSlotsFrame +
                             audioSlotsFrame +
                             otherSlotsFrame +
                             idleSlotsFrame;
        dmaTotalSlotsMaxFrame = dmaSlotsAvailableFrame;
        if (dmaTotalSlotsFrame > dmaTotalSlotsMaxFrame) {
            dmaTotalSlotsMaxFrame = dmaTotalSlotsFrame;
        }
    }

    if (ui->copperStatsChart) {
        custom_ui_copperStatsChartSetValues(ui->copperStatsChart,
                                            hasCopperStats,
                                            copperSlotsFrame,
                                            copperSlotsMaxFrame);
    }
    if (ui->blitterDmaStatsChart) {
        custom_ui_blitterDmaStatsChartSetValues(ui->blitterDmaStatsChart,
                                                hasBlitterDmaStats,
                                                blitterSlotsFrame,
                                                blitterSlotsMaxFrame);
    }
    if (ui->cpuDmaStatsChart) {
        custom_ui_cpuDmaStatsChartSetValues(ui->cpuDmaStatsChart,
                                            hasCpuDmaStats,
                                            cpuSlotsFrame,
                                            cpuSlotsMaxFrame);
    }
    if (ui->bitplaneDmaStatsChart) {
        custom_ui_bitplaneDmaStatsChartSetValues(ui->bitplaneDmaStatsChart,
                                                 hasBitplaneDmaStats,
                                                 bitplaneSlotsFrame,
                                                 bitplaneSlotsMaxFrame);
    }
    if (ui->spriteDmaStatsChart) {
        custom_ui_spriteDmaStatsChartSetValues(ui->spriteDmaStatsChart,
                                               hasSpriteDmaStats,
                                               spriteSlotsFrame,
                                               spriteSlotsMaxFrame);
    }
    if (ui->diskDmaStatsChart) {
        custom_ui_diskDmaStatsChartSetValues(ui->diskDmaStatsChart,
                                             hasDiskDmaStats,
                                             diskSlotsFrame,
                                             diskSlotsMaxFrame);
    }
    if (ui->audioDmaStatsChart) {
        custom_ui_audioDmaStatsChartSetValues(ui->audioDmaStatsChart,
                                              hasAudioDmaStats,
                                              audioSlotsFrame,
                                              audioSlotsMaxFrame);
    }
    if (ui->otherDmaStatsChart) {
        custom_ui_otherDmaStatsChartSetValues(ui->otherDmaStatsChart,
                                              hasOtherDmaStats,
                                              otherSlotsFrame,
                                              otherSlotsMaxFrame);
    }
    if (ui->idleDmaStatsChart) {
        custom_ui_idleDmaStatsChartSetValues(ui->idleDmaStatsChart,
                                             hasIdleDmaStats,
                                             idleSlotsFrame,
                                             idleSlotsMaxFrame);
    }
    if (ui->dmaTotalMixChart) {
        custom_ui_dmaTotalMixChartSetValues(ui->dmaTotalMixChart,
                                            hasDmaTotalStats,
                                            dmaTotalSlotsFrame,
                                            dmaTotalSlotsMaxFrame,
                                            cpuSlotsFrame,
                                            copperSlotsFrame,
                                            audioSlotsFrame,
                                            blitterSlotsFrame,
                                            bitplaneSlotsFrame,
                                            spriteSlotsFrame,
                                            diskSlotsFrame,
                                            otherSlotsFrame,
                                            idleSlotsFrame);
    }
}

static void
custom_ui_updateStatsCharts(custom_ui_state_t *ui)
{
    custom_ui_updateBlitterStatsChart(ui);
    custom_ui_updateCopperStatsChart(ui);
}

static int
custom_ui_seekRowPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    if (!self || !self->state || !ctx) {
        return 0;
    }
    custom_ui_seek_row_state_t *st = (custom_ui_seek_row_state_t*)self->state;
    int barH = e9ui_scale_px(ctx, st->barHeight);
    int pad = e9ui_scale_px(ctx, st->rowPadding);
    if (barH <= 0) {
        barH = 10;
    }
    return barH + pad * 2;
}

static void
custom_ui_seekRowLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self || !self->state || !ctx) {
        return;
    }
    custom_ui_seek_row_state_t *st = (custom_ui_seek_row_state_t*)self->state;
    self->bounds = bounds;
    if (!st->bar) {
        return;
    }
    int leftInset = e9ui_scale_px(ctx, st->leftInset);
    int rightInset = e9ui_scale_px(ctx, st->rightInset);
    int barH = e9ui_scale_px(ctx, st->barHeight);
    if (barH <= 0) {
        barH = 10;
    }
    int barW = bounds.w - leftInset - rightInset;
    if (barW < 1) {
        barW = 1;
    }
    st->bar->bounds.x = bounds.x + leftInset;
    st->bar->bounds.w = barW;
    st->bar->bounds.h = barH;
    st->bar->bounds.y = bounds.y + (bounds.h - barH) / 2;
}

static void
custom_ui_seekRowRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx) {
        return;
    }
    custom_ui_seek_row_state_t *st = (custom_ui_seek_row_state_t*)self->state;
    if (st->bar && st->bar->render) {
        st->bar->render(st->bar, ctx);
    }
}

static void
custom_ui_seekRowDtor(e9ui_component_t *self, e9ui_context_t *ctx)
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

static e9ui_component_t *
custom_ui_blitterVisDecaySeekRowMake(e9ui_component_t **outBar)
{
    e9ui_component_t *row = (e9ui_component_t*)alloc_calloc(1, sizeof(*row));
    custom_ui_seek_row_state_t *st = (custom_ui_seek_row_state_t*)alloc_calloc(1, sizeof(*st));
    if (!row || !st) {
        if (row) {
            alloc_free(row);
        }
        if (st) {
            alloc_free(st);
        }
        return NULL;
    }
    st->leftInset = 92;
    st->rightInset = 14;
    st->barHeight = 10;
    st->rowPadding = 3;
    st->bar = e9ui_seek_bar_make();
    if (st->bar) {
        e9ui_seek_bar_setMargins(st->bar, 0, 0, 0);
        e9ui_seek_bar_setHeight(st->bar, 10);
        e9ui_seek_bar_setHoverMargin(st->bar, 6);
    }
    row->name = "custom_ui_seek_row";
    row->state = st;
    row->preferredHeight = custom_ui_seekRowPreferredHeight;
    row->layout = custom_ui_seekRowLayout;
    row->render = custom_ui_seekRowRender;
    row->dtor = custom_ui_seekRowDtor;
    if (st->bar) {
        e9ui_child_add(row, st->bar, NULL);
    }
    if (outBar) {
        *outBar = st->bar;
    }
    return row;
}

static int
custom_ui_insetRowPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    if (!self || !self->state || !ctx) {
        return 0;
    }
    custom_ui_inset_row_state_t *st = (custom_ui_inset_row_state_t *)self->state;
    if (!st->child || !st->child->preferredHeight) {
        return 0;
    }
    int leftInset = e9ui_scale_px(ctx, st->leftInset);
    int rightInset = e9ui_scale_px(ctx, st->rightInset);
    int childAvailW = availW - leftInset - rightInset;
    if (childAvailW < 0) {
        childAvailW = 0;
    }
    return st->child->preferredHeight(st->child, ctx, childAvailW);
}

static void
custom_ui_insetRowLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self || !self->state || !ctx) {
        return;
    }
    custom_ui_inset_row_state_t *st = (custom_ui_inset_row_state_t *)self->state;
    self->bounds = bounds;
    if (!st->child || !st->child->layout) {
        return;
    }
    int leftInset = e9ui_scale_px(ctx, st->leftInset);
    int rightInset = e9ui_scale_px(ctx, st->rightInset);
    e9ui_rect_t childBounds = bounds;
    childBounds.x += leftInset;
    childBounds.w -= leftInset + rightInset;
    if (childBounds.w < 1) {
        childBounds.w = 1;
    }
    st->child->layout(st->child, ctx, childBounds);
}

static void
custom_ui_insetRowRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx) {
        return;
    }
    custom_ui_inset_row_state_t *st = (custom_ui_inset_row_state_t *)self->state;
    if (st->child && st->child->render) {
        st->child->render(st->child, ctx);
    }
}

static void
custom_ui_insetRowDtor(e9ui_component_t *self, e9ui_context_t *ctx)
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

static e9ui_component_t *
custom_ui_insetRowMake(e9ui_component_t *child, int leftInset, int rightInset)
{
    if (!child) {
        return NULL;
    }
    e9ui_component_t *row = (e9ui_component_t *)alloc_calloc(1, sizeof(*row));
    custom_ui_inset_row_state_t *st = (custom_ui_inset_row_state_t *)alloc_calloc(1, sizeof(*st));
    if (!row || !st) {
        if (row) {
            alloc_free(row);
        }
        if (st) {
            alloc_free(st);
        }
        return NULL;
    }
    st->child = child;
    st->leftInset = leftInset;
    st->rightInset = rightInset;
    row->name = "custom_ui_inset_row";
    row->state = st;
    row->preferredHeight = custom_ui_insetRowPreferredHeight;
    row->layout = custom_ui_insetRowLayout;
    row->render = custom_ui_insetRowRender;
    row->dtor = custom_ui_insetRowDtor;
    e9ui_child_add(row, child, NULL);
    return row;
}

static int
custom_ui_dmaStatsHeaderRowPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    if (!self || !self->state || !ctx) {
        return 0;
    }
    custom_ui_dma_stats_header_row_state_t *st = (custom_ui_dma_stats_header_row_state_t *)self->state;
    int maxHeight = 0;
    int innerWidth = availW - st->leftInset;
    if (innerWidth < 0) {
        innerWidth = 0;
    }
    if (st->checkbox &&
        !e9ui_getHidden(st->checkbox) &&
        st->checkbox->preferredHeight) {
        int h = st->checkbox->preferredHeight(st->checkbox, ctx, innerWidth);
        if (h > maxHeight) {
            maxHeight = h;
        }
    }
    if (st->hintRow &&
        !e9ui_getHidden(st->hintRow) &&
        st->hintRow->preferredHeight) {
        int h = st->hintRow->preferredHeight(st->hintRow, ctx, innerWidth);
        if (h > maxHeight) {
            maxHeight = h;
        }
    }
    return maxHeight;
}

static void
custom_ui_dmaStatsHeaderRowLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self || !self->state || !ctx) {
        return;
    }
    self->bounds = bounds;
    custom_ui_dma_stats_header_row_state_t *st = (custom_ui_dma_stats_header_row_state_t *)self->state;
    int checkboxWidth = 0;
    int checkboxHeight = 0;
    if (st->checkbox) {
        e9ui_checkbox_measure(st->checkbox, ctx, &checkboxWidth, &checkboxHeight);
    }
    if (checkboxWidth < 0) {
        checkboxWidth = 0;
    }
    int startX = bounds.x + st->leftInset;
    if (startX > bounds.x + bounds.w) {
        startX = bounds.x + bounds.w;
    }
    int availableWidth = bounds.w - (startX - bounds.x);
    if (availableWidth < 0) {
        availableWidth = 0;
    }
    if (checkboxWidth > availableWidth) {
        checkboxWidth = availableWidth;
    }

    if (st->checkbox &&
        !e9ui_getHidden(st->checkbox) &&
        st->checkbox->layout) {
        e9ui_rect_t checkboxBounds = {
            startX,
            bounds.y,
            checkboxWidth,
            bounds.h
        };
        st->checkbox->layout(st->checkbox, ctx, checkboxBounds);
    }

    int hintX = startX + checkboxWidth + st->gap;
    if (hintX > bounds.x + bounds.w) {
        hintX = bounds.x + bounds.w;
    }
    int hintWidth = bounds.w - (hintX - bounds.x);
    if (hintWidth < 0) {
        hintWidth = 0;
    }
    if (st->hintRow &&
        !e9ui_getHidden(st->hintRow) &&
        st->hintRow->layout) {
        e9ui_rect_t hintBounds = {
            hintX,
            bounds.y,
            hintWidth,
            bounds.h
        };
        st->hintRow->layout(st->hintRow, ctx, hintBounds);
    }
}

static void
custom_ui_dmaStatsHeaderRowRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx) {
        return;
    }
    custom_ui_dma_stats_header_row_state_t *st = (custom_ui_dma_stats_header_row_state_t *)self->state;
    if (st->checkbox &&
        !e9ui_getHidden(st->checkbox) &&
        st->checkbox->render) {
        st->checkbox->render(st->checkbox, ctx);
    }
    if (st->hintRow &&
        !e9ui_getHidden(st->hintRow) &&
        st->hintRow->render) {
        st->hintRow->render(st->hintRow, ctx);
    }
}

static void
custom_ui_dmaStatsHeaderRowDtor(e9ui_component_t *self, e9ui_context_t *ctx)
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

static e9ui_component_t *
custom_ui_dmaStatsHeaderRowMake(e9ui_component_t *checkbox, e9ui_component_t *hintRow, int leftInset, int gap)
{
    if (!checkbox || !hintRow) {
        return NULL;
    }
    e9ui_component_t *row = (e9ui_component_t *)alloc_calloc(1, sizeof(*row));
    custom_ui_dma_stats_header_row_state_t *st = (custom_ui_dma_stats_header_row_state_t *)alloc_calloc(1, sizeof(*st));
    if (!row || !st) {
        if (row) {
            alloc_free(row);
        }
        if (st) {
            alloc_free(st);
        }
        return NULL;
    }
    st->checkbox = checkbox;
    st->hintRow = hintRow;
    st->leftInset = leftInset;
    st->gap = gap;
    row->name = "custom_ui_dma_stats_header_row";
    row->state = st;
    row->preferredHeight = custom_ui_dmaStatsHeaderRowPreferredHeight;
    row->layout = custom_ui_dmaStatsHeaderRowLayout;
    row->render = custom_ui_dmaStatsHeaderRowRender;
    row->dtor = custom_ui_dmaStatsHeaderRowDtor;
    e9ui_child_add(row, checkbox, NULL);
    e9ui_child_add(row, hintRow, NULL);
    return row;
}

static void
custom_ui_applyOption(e9k_debug_option_t option, uint32_t argument)
{
    custom_ui_state_t *ui = &custom_ui_state;
    if (libretro_host_debugSetDebugOption(option, argument, NULL)) {
        ui->warnedMissingOption = 0;
        return;
    }
    if (!ui->warnedMissingOption) {
        debug_error("custom ui: core does not expose debug option API");
        ui->warnedMissingOption = 1;
    }
}

static e9k_debug_option_t
custom_ui_spriteOptionForIndex(int spriteIndex)
{
    switch (spriteIndex) {
        case 0: return E9K_DEBUG_OPTION_AMIGA_SPRITE0;
        case 1: return E9K_DEBUG_OPTION_AMIGA_SPRITE1;
        case 2: return E9K_DEBUG_OPTION_AMIGA_SPRITE2;
        case 3: return E9K_DEBUG_OPTION_AMIGA_SPRITE3;
        case 4: return E9K_DEBUG_OPTION_AMIGA_SPRITE4;
        case 5: return E9K_DEBUG_OPTION_AMIGA_SPRITE5;
        case 6: return E9K_DEBUG_OPTION_AMIGA_SPRITE6;
        case 7: return E9K_DEBUG_OPTION_AMIGA_SPRITE7;
        default: break;
    }
    return e9k_debug_option_none;
}

static e9k_debug_option_t
custom_ui_bitplaneOptionForIndex(int bitplaneIndex)
{
    switch (bitplaneIndex) {
        case 0: return E9K_DEBUG_OPTION_AMIGA_BITPLANE0;
        case 1: return E9K_DEBUG_OPTION_AMIGA_BITPLANE1;
        case 2: return E9K_DEBUG_OPTION_AMIGA_BITPLANE2;
        case 3: return E9K_DEBUG_OPTION_AMIGA_BITPLANE3;
        case 4: return E9K_DEBUG_OPTION_AMIGA_BITPLANE4;
        case 5: return E9K_DEBUG_OPTION_AMIGA_BITPLANE5;
        case 6: return E9K_DEBUG_OPTION_AMIGA_BITPLANE6;
        case 7: return E9K_DEBUG_OPTION_AMIGA_BITPLANE7;
        default: break;
    }
    return e9k_debug_option_none;
}

static e9k_debug_option_t
custom_ui_audioOptionForIndex(int audioChannelIndex)
{
    switch (audioChannelIndex) {
        case 0: return E9K_DEBUG_OPTION_AMIGA_AUDIO0;
        case 1: return E9K_DEBUG_OPTION_AMIGA_AUDIO1;
        case 2: return E9K_DEBUG_OPTION_AMIGA_AUDIO2;
        case 3: return E9K_DEBUG_OPTION_AMIGA_AUDIO3;
        default: break;
    }
    return e9k_debug_option_none;
}

static e9k_debug_option_t
custom_ui_bplptrOptionForIndex(int bplptrIndex)
{
    switch (bplptrIndex) {
        case 0: return E9K_DEBUG_OPTION_AMIGA_BPLPTR1_BLOCK;
        case 1: return E9K_DEBUG_OPTION_AMIGA_BPLPTR2_BLOCK;
        case 2: return E9K_DEBUG_OPTION_AMIGA_BPLPTR3_BLOCK;
        case 3: return E9K_DEBUG_OPTION_AMIGA_BPLPTR4_BLOCK;
        case 4: return E9K_DEBUG_OPTION_AMIGA_BPLPTR5_BLOCK;
        case 5: return E9K_DEBUG_OPTION_AMIGA_BPLPTR6_BLOCK;
        default: break;
    }
    return e9k_debug_option_none;
}

static void
custom_ui_applyBlitterOption(void)
{
    custom_ui_state_t *ui = &custom_ui_state;
    custom_ui_applyOption(E9K_DEBUG_OPTION_AMIGA_BLITTER, ui->blitterEnabled ? 1u : 0u);
}

static void
custom_ui_applyBlitterVisDecayOption(void)
{
    custom_ui_state_t *ui = &custom_ui_state;
    custom_ui_applyOption(E9K_DEBUG_OPTION_AMIGA_BLITTER_VIS_DECAY, (uint32_t)ui->blitterVisDecay);
}

static void
custom_ui_applyBlitterVisModeOption(void)
{
    custom_ui_state_t *ui = &custom_ui_state;
    custom_ui_applyOption(E9K_DEBUG_OPTION_AMIGA_BLITTER_VIS_MODE, (uint32_t)ui->blitterVisMode);
}

static void
custom_ui_applyBlitterVisBlinkOption(void)
{
    custom_ui_state_t *ui = &custom_ui_state;
    custom_ui_applyOption(E9K_DEBUG_OPTION_AMIGA_BLITTER_VIS_BLINK, ui->blitterVisBlink ? 1u : 0u);
}

static void
custom_ui_applyBlitterDebugOption(void)
{
    custom_ui_state_t *ui = &custom_ui_state;
    (void)libretro_host_debugAmiSetBlitterDebug(ui->blitterDebugEnabled ? 1 : 0);
}

#if E9K_HACK_AMI_SPRITE_VIS
static void
custom_ui_applySpriteVisOption(void)
{
    custom_ui_state_t *ui = &custom_ui_state;
    (void)libretro_host_debugAmiSetSpriteVis(ui->spriteVisEnabled ? 1 : 0);
}
#endif

static void
custom_ui_applyBplcon1DelayScrollOption(void)
{
    custom_ui_state_t *ui = &custom_ui_state;
    custom_ui_applyOption(E9K_DEBUG_OPTION_AMIGA_BPLCON1_DELAY_SCROLL, ui->bplcon1DelayScrollEnabled ? 1u : 0u);
}

static void
custom_ui_applyCopperLimitEnabledOption(void)
{
    custom_ui_state_t *ui = &custom_ui_state;
    custom_ui_applyOption(E9K_DEBUG_OPTION_AMIGA_COPPER_LINE_LIMIT_ENABLED, ui->copperLimitEnabled ? 1u : 0u);
}

static void
custom_ui_applyCopperLimitStartOption(void)
{
    custom_ui_state_t *ui = &custom_ui_state;
    ui->copperLimitStart = custom_ui_clampCopperLine(ui->copperLimitStart);
    custom_ui_applyOption(E9K_DEBUG_OPTION_AMIGA_COPPER_LINE_LIMIT_START, (uint32_t)ui->copperLimitStart);
}

static void
custom_ui_applyCopperLimitEndOption(void)
{
    custom_ui_state_t *ui = &custom_ui_state;
    ui->copperLimitEnd = custom_ui_clampCopperLine(ui->copperLimitEnd);
    custom_ui_applyOption(E9K_DEBUG_OPTION_AMIGA_COPPER_LINE_LIMIT_END, (uint32_t)ui->copperLimitEnd);
}

static void
custom_ui_applyBplptrBlockAllOption(void)
{
    custom_ui_state_t *ui = &custom_ui_state;
    custom_ui_applyOption(E9K_DEBUG_OPTION_AMIGA_BPLPTR_BLOCK_ALL, ui->bplptrBlockAllEnabled ? 1u : 0u);
}

static void
custom_ui_applyBplptrLineLimitStartOption(void)
{
    custom_ui_state_t *ui = &custom_ui_state;
    ui->bplptrLineLimitStart = custom_ui_clampCopperLine(ui->bplptrLineLimitStart);
    custom_ui_applyOption(E9K_DEBUG_OPTION_AMIGA_BPLPTR_LINE_LIMIT_START, (uint32_t)ui->bplptrLineLimitStart);
}

static void
custom_ui_applyBplptrLineLimitEndOption(void)
{
    custom_ui_state_t *ui = &custom_ui_state;
    ui->bplptrLineLimitEnd = custom_ui_clampCopperLine(ui->bplptrLineLimitEnd);
    custom_ui_applyOption(E9K_DEBUG_OPTION_AMIGA_BPLPTR_LINE_LIMIT_END, (uint32_t)ui->bplptrLineLimitEnd);
}

static void
custom_ui_applyBplptrBlockOption(int bplptrIndex)
{
    custom_ui_state_t *ui = &custom_ui_state;
    if (bplptrIndex < 0 || bplptrIndex >= CUSTOM_UI_AMIGA_BPLPTR_COUNT) {
        return;
    }
    e9k_debug_option_t option = custom_ui_bplptrOptionForIndex(bplptrIndex);
    if (option == e9k_debug_option_none) {
        return;
    }
    custom_ui_applyOption(option, ui->bplptrBlockEnabled[bplptrIndex] ? 1u : 0u);
}

static void
custom_ui_applySpriteOption(int spriteIndex)
{
    custom_ui_state_t *ui = &custom_ui_state;
    if (spriteIndex < 0 || spriteIndex >= CUSTOM_UI_AMIGA_SPRITE_COUNT) {
        return;
    }
    e9k_debug_option_t option = custom_ui_spriteOptionForIndex(spriteIndex);
    if (option == e9k_debug_option_none) {
        return;
    }
    custom_ui_applyOption(option, ui->spriteEnabled[spriteIndex] ? 1u : 0u);
}

static void
custom_ui_applyBitplaneOption(int bitplaneIndex)
{
    custom_ui_state_t *ui = &custom_ui_state;
    if (bitplaneIndex < 0 || bitplaneIndex >= CUSTOM_UI_AMIGA_BITPLANE_COUNT) {
        return;
    }
    e9k_debug_option_t option = custom_ui_bitplaneOptionForIndex(bitplaneIndex);
    if (option == e9k_debug_option_none) {
        return;
    }
    custom_ui_applyOption(option, ui->bitplaneEnabled[bitplaneIndex] ? 1u : 0u);
}

static void
custom_ui_applyAudioOption(int audioChannelIndex)
{
    custom_ui_state_t *ui = &custom_ui_state;
    if (audioChannelIndex < 0 || audioChannelIndex >= CUSTOM_UI_AMIGA_AUDIO_CHANNEL_COUNT) {
        return;
    }
    e9k_debug_option_t option = custom_ui_audioOptionForIndex(audioChannelIndex);
    if (option == e9k_debug_option_none) {
        return;
    }
    custom_ui_applyOption(option, ui->audioEnabled[audioChannelIndex] ? 1u : 0u);
}

static void
custom_ui_applyAllOptions(void)
{
    custom_ui_applyBlitterOption();
    custom_ui_applyBlitterVisDecayOption();
    custom_ui_applyBlitterVisModeOption();
    custom_ui_applyBlitterVisBlinkOption();
#if E9K_HACK_AMI_SPRITE_VIS
    custom_ui_applySpriteVisOption();
#endif
    custom_ui_applyBplcon1DelayScrollOption();
    custom_ui_applyCopperLimitEnabledOption();
    custom_ui_applyCopperLimitStartOption();
    custom_ui_applyCopperLimitEndOption();
    custom_ui_applyBplptrBlockAllOption();
    custom_ui_applyBplptrLineLimitStartOption();
    custom_ui_applyBplptrLineLimitEndOption();
    for (int bplptrIndex = 0; bplptrIndex < CUSTOM_UI_AMIGA_BPLPTR_COUNT; ++bplptrIndex) {
        custom_ui_applyBplptrBlockOption(bplptrIndex);
    }
    for (int spriteIndex = 0; spriteIndex < CUSTOM_UI_AMIGA_SPRITE_COUNT; ++spriteIndex) {
        custom_ui_applySpriteOption(spriteIndex);
    }
    for (int bitplaneIndex = 0; bitplaneIndex < CUSTOM_UI_AMIGA_BITPLANE_COUNT; ++bitplaneIndex) {
        custom_ui_applyBitplaneOption(bitplaneIndex);
    }
    for (int audioChannelIndex = 0; audioChannelIndex < CUSTOM_UI_AMIGA_AUDIO_CHANNEL_COUNT; ++audioChannelIndex) {
        custom_ui_applyAudioOption(audioChannelIndex);
    }
}

static void
custom_ui_setComponentDisabled(e9ui_component_t *comp, int disabled)
{
    if (!comp) {
        return;
    }
    comp->disabled = disabled ? 1 : 0;
}

static void
custom_ui_syncBlitterDebugSuboptions(custom_ui_state_t *ui)
{
    if (!ui) {
        return;
    }
    int disabled = ui->blitterDebugEnabled ? 0 : 1;
    custom_ui_setComponentDisabled(ui->blitterVisCollectCheckbox, disabled);
    custom_ui_setComponentDisabled(ui->blitterVisDecayRow, disabled);
    custom_ui_setComponentDisabled(ui->blitterVisDecayTextbox, disabled);
    custom_ui_setComponentDisabled(ui->blitterVisDecaySeekRow, disabled);
    custom_ui_setComponentDisabled(ui->blitterVisDecaySeekBar, disabled);
    custom_ui_setComponentDisabled(ui->blitterVisBlinkCheckbox, disabled);
    custom_ui_setComponentDisabled(ui->blitterVisPatternCheckbox, disabled);
    custom_ui_setComponentDisabled(ui->blitterVisModeCheckbox, disabled);
}

static void
custom_ui_syncCopperLimitSuboptions(custom_ui_state_t *ui)
{
    if (!ui) {
        return;
    }
    int disabled = ui->copperLimitEnabled ? 0 : 1;
    custom_ui_setComponentDisabled(ui->copperLimitStartRow, disabled);
    custom_ui_setComponentDisabled(ui->copperLimitStartTextbox, disabled);
    custom_ui_setComponentDisabled(ui->copperLimitEndRow, disabled);
    custom_ui_setComponentDisabled(ui->copperLimitEndTextbox, disabled);
}

static void
custom_ui_syncDmaStatsSuboptions(custom_ui_state_t *ui)
{
    if (!ui) {
        return;
    }
    int disabled = ui->dmaStatsEnabled ? 0 : 1;
    custom_ui_setComponentDisabled(ui->copperStatsChartRow, disabled);
    custom_ui_setComponentDisabled(ui->copperStatsChart, disabled);
    custom_ui_setComponentDisabled(ui->blitterDmaStatsChartRow, disabled);
    custom_ui_setComponentDisabled(ui->blitterDmaStatsChart, disabled);
    custom_ui_setComponentDisabled(ui->cpuDmaStatsChartRow, disabled);
    custom_ui_setComponentDisabled(ui->cpuDmaStatsChart, disabled);
    custom_ui_setComponentDisabled(ui->bitplaneDmaStatsChartRow, disabled);
    custom_ui_setComponentDisabled(ui->bitplaneDmaStatsChart, disabled);
    custom_ui_setComponentDisabled(ui->spriteDmaStatsChartRow, disabled);
    custom_ui_setComponentDisabled(ui->spriteDmaStatsChart, disabled);
    custom_ui_setComponentDisabled(ui->diskDmaStatsChartRow, disabled);
    custom_ui_setComponentDisabled(ui->diskDmaStatsChart, disabled);
    custom_ui_setComponentDisabled(ui->audioDmaStatsChartRow, disabled);
    custom_ui_setComponentDisabled(ui->audioDmaStatsChart, disabled);
    custom_ui_setComponentDisabled(ui->otherDmaStatsChartRow, disabled);
    custom_ui_setComponentDisabled(ui->otherDmaStatsChart, disabled);
    custom_ui_setComponentDisabled(ui->idleDmaStatsChartRow, disabled);
    custom_ui_setComponentDisabled(ui->idleDmaStatsChart, disabled);
    custom_ui_setComponentDisabled(ui->dmaTotalMixChartRow, disabled);
    custom_ui_setComponentDisabled(ui->dmaTotalMixChart, disabled);
    custom_ui_setComponentDisabled(ui->dmaStatsHintTextRow, disabled);
    custom_ui_setComponentDisabled(ui->dmaStatsHintText, disabled);
    custom_ui_syncDmaStatsCycleExactHint(ui);
}

static void
custom_ui_syncDmaStatsCycleExactHint(custom_ui_state_t *ui)
{
    if (!ui || !ui->dmaStatsHintText || !ui->dmaStatsHintTextRow) {
        return;
    }
    int showHint = 0;
    const char *hostCompat = libretro_host_getCoreOptionValue("puae_cpu_compatibility");
    int cycleExactConfigured = 0;
    if (hostCompat &&
        (strcmp(hostCompat, "memory") == 0 || strcmp(hostCompat, "exact") == 0)) {
        cycleExactConfigured = 1;
    }
    if (ui->dmaStatsEnabled && !cycleExactConfigured) {
        showHint = 1;
    }
    e9ui_setHidden(ui->dmaStatsHintText, showHint ? 0 : 1);
    e9ui_setHidden(ui->dmaStatsHintTextRow, showHint ? 0 : 1);
}

static void
custom_ui_syncBlitterDebugCheckbox(custom_ui_state_t *ui)
{
    if (!ui) {
        return;
    }
    int enabled = 0;
    if (!libretro_host_debugAmiGetBlitterDebug(&enabled)) {
        return;
    }
    ui->blitterDebugEnabled = enabled ? 1 : 0;
    if (ui->blitterDebugCheckbox) {
        ui->suppressBlitterDebugCallbacks = 1;
        e9ui_checkbox_setSelected(ui->blitterDebugCheckbox, ui->blitterDebugEnabled, &ui->ctx);
        ui->suppressBlitterDebugCallbacks = 0;
    }
    custom_ui_syncBlitterDebugSuboptions(ui);
}

static void
custom_ui_syncCopperVisualiserCheckbox(custom_ui_state_t *ui)
{
    if (!ui) {
        return;
    }
    ui->copperVisualiserEnabled = emu_ami_getCopperDebugEnabled() ? 1 : 0;
    if (ui->copperVisualiserCheckbox) {
        e9ui_checkbox_setSelected(ui->copperVisualiserCheckbox, ui->copperVisualiserEnabled, &ui->ctx);
    }
}

#if E9K_HACK_AMI_SPRITE_VIS
static void
custom_ui_syncSpriteVisCheckbox(custom_ui_state_t *ui)
{
    if (!ui) {
        return;
    }
    int enabled = 0;
    if (!libretro_host_debugAmiGetSpriteVis(&enabled)) {
        return;
    }
    ui->spriteVisEnabled = enabled ? 1 : 0;
    if (ui->spriteVisCheckbox) {
        ui->suppressSpriteVisCallbacks = 1;
        e9ui_checkbox_setSelected(ui->spriteVisCheckbox, ui->spriteVisEnabled, &ui->ctx);
        ui->suppressSpriteVisCallbacks = 0;
    }
}
#endif

static int
custom_ui_areAllSpritesEnabled(const custom_ui_state_t *ui)
{
    if (!ui) {
        return 0;
    }
    for (int spriteIndex = 0; spriteIndex < CUSTOM_UI_AMIGA_SPRITE_COUNT; ++spriteIndex) {
        if (!ui->spriteEnabled[spriteIndex]) {
            return 0;
        }
    }
    return 1;
}

static int
custom_ui_areAllBitplanesEnabled(const custom_ui_state_t *ui)
{
    if (!ui) {
        return 0;
    }
    for (int bitplaneIndex = 0; bitplaneIndex < CUSTOM_UI_AMIGA_BITPLANE_COUNT; ++bitplaneIndex) {
        if (!ui->bitplaneEnabled[bitplaneIndex]) {
            return 0;
        }
    }
    return 1;
}

static int
custom_ui_areAllAudiosEnabled(const custom_ui_state_t *ui)
{
    if (!ui) {
        return 0;
    }
    for (int audioChannelIndex = 0; audioChannelIndex < CUSTOM_UI_AMIGA_AUDIO_CHANNEL_COUNT; ++audioChannelIndex) {
        if (!ui->audioEnabled[audioChannelIndex]) {
            return 0;
        }
    }
    return 1;
}

static int
custom_ui_areAllBplptrBlocked(const custom_ui_state_t *ui)
{
    if (!ui) {
        return 0;
    }
    for (int bplptrIndex = 0; bplptrIndex < CUSTOM_UI_AMIGA_BPLPTR_COUNT; ++bplptrIndex) {
        if (!ui->bplptrBlockEnabled[bplptrIndex]) {
            return 0;
        }
    }
    return 1;
}

static void
custom_ui_syncSpritesMasterCheckbox(custom_ui_state_t *ui)
{
    if (!ui) {
        return;
    }
    ui->spritesEnabled = custom_ui_areAllSpritesEnabled(ui) ? 1 : 0;
    if (!ui->spritesCheckbox) {
        return;
    }
    ui->suppressSpriteCallbacks = 1;
    e9ui_checkbox_setSelected(ui->spritesCheckbox, ui->spritesEnabled, &ui->ctx);
    ui->suppressSpriteCallbacks = 0;
}

static void
custom_ui_syncBitplanesMasterCheckbox(custom_ui_state_t *ui)
{
    if (!ui) {
        return;
    }
    ui->bitplanesEnabled = custom_ui_areAllBitplanesEnabled(ui) ? 1 : 0;
    if (!ui->bitplanesCheckbox) {
        return;
    }
    ui->suppressBitplaneCallbacks = 1;
    e9ui_checkbox_setSelected(ui->bitplanesCheckbox, ui->bitplanesEnabled, &ui->ctx);
    ui->suppressBitplaneCallbacks = 0;
}

static void
custom_ui_syncAudiosMasterCheckbox(custom_ui_state_t *ui)
{
    if (!ui) {
        return;
    }
    ui->audiosEnabled = custom_ui_areAllAudiosEnabled(ui) ? 1 : 0;
    if (!ui->audiosCheckbox) {
        return;
    }
    ui->suppressAudioCallbacks = 1;
    e9ui_checkbox_setSelected(ui->audiosCheckbox, ui->audiosEnabled, &ui->ctx);
    ui->suppressAudioCallbacks = 0;
}

static void
custom_ui_syncBplptrBlockMasterCheckbox(custom_ui_state_t *ui)
{
    if (!ui) {
        return;
    }
    ui->bplptrBlockAllEnabled = custom_ui_areAllBplptrBlocked(ui) ? 1 : 0;
    if (!ui->bplptrBlockAllCheckbox) {
        return;
    }
    ui->suppressBplptrBlockCallbacks = 1;
    e9ui_checkbox_setSelected(ui->bplptrBlockAllCheckbox, ui->bplptrBlockAllEnabled, &ui->ctx);
    ui->suppressBplptrBlockCallbacks = 0;
}

static void
custom_ui_blitterChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    custom_ui_state_t *ui = (custom_ui_state_t*)user;
    if (!ui) {
        return;
    }
    ui->blitterEnabled = selected ? 1 : 0;
    custom_ui_applyBlitterOption();
}

static void
custom_ui_copperLimitChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    custom_ui_state_t *ui = (custom_ui_state_t*)user;
    if (!ui) {
        return;
    }
    ui->copperLimitEnabled = selected ? 1 : 0;
    custom_ui_applyCopperLimitEnabledOption();
    custom_ui_syncCopperLimitSuboptions(ui);
}

static void
custom_ui_dmaStatsChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    custom_ui_state_t *ui = (custom_ui_state_t*)user;
    if (!ui) {
        return;
    }
    ui->dmaStatsEnabled = selected ? 1 : 0;
    if (ui->dmaStatsEnabled) {
        custom_ui_enableDmaDebugForCopperStats(ui);
    } else {
        custom_ui_restoreDmaDebugForCopperStats(ui);
    }
    custom_ui_syncDmaStatsSuboptions(ui);
    custom_ui_updateStatsCharts(ui);
}

static void
custom_ui_estimateFpsChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    custom_ui_state_t *ui = (custom_ui_state_t *)user;
    if (!ui) {
        return;
    }
    ui->estimateFpsEnabled = selected ? 1 : 0;
    libretro_host_setEstimateFpsEnabled(ui->estimateFpsEnabled);
    custom_ui_syncEstimateFpsDisplay(ui);
}

static void
custom_ui_bplptrBlockAllChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    custom_ui_state_t *ui = (custom_ui_state_t*)user;
    if (!ui) {
        return;
    }
    if (ui->suppressBplptrBlockCallbacks) {
        return;
    }
    int nextValue = selected ? 1 : 0;
    ui->bplptrBlockAllEnabled = nextValue;
    custom_ui_applyBplptrBlockAllOption();
    for (int bplptrIndex = 0; bplptrIndex < CUSTOM_UI_AMIGA_BPLPTR_COUNT; ++bplptrIndex) {
        ui->bplptrBlockEnabled[bplptrIndex] = nextValue;
        custom_ui_applyBplptrBlockOption(bplptrIndex);
        if (ui->bplptrBlockCheckboxes[bplptrIndex]) {
            ui->suppressBplptrBlockCallbacks = 1;
            e9ui_checkbox_setSelected(ui->bplptrBlockCheckboxes[bplptrIndex], nextValue, &ui->ctx);
            ui->suppressBplptrBlockCallbacks = 0;
        }
    }
}

static void
custom_ui_bplptrBlockChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    if (!user) {
        return;
    }
    struct custom_ui_bplptr_cb *cb = (struct custom_ui_bplptr_cb *)user;
    if (!cb->ui) {
        return;
    }
    if (cb->bplptrIndex < 0 || cb->bplptrIndex >= CUSTOM_UI_AMIGA_BPLPTR_COUNT) {
        return;
    }
    if (cb->ui->suppressBplptrBlockCallbacks) {
        return;
    }
    cb->ui->bplptrBlockEnabled[cb->bplptrIndex] = selected ? 1 : 0;
    custom_ui_applyBplptrBlockOption(cb->bplptrIndex);
    custom_ui_syncBplptrBlockMasterCheckbox(cb->ui);
    custom_ui_applyBplptrBlockAllOption();
}

static void
custom_ui_blitterDebugChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    custom_ui_state_t *ui = (custom_ui_state_t*)user;
    if (!ui) {
        return;
    }
    if (ui->suppressBlitterDebugCallbacks) {
        return;
    }
    ui->blitterDebugEnabled = selected ? 1 : 0;
    custom_ui_applyBlitterDebugOption();
    custom_ui_syncBlitterDebugSuboptions(ui);
}

#if E9K_HACK_AMI_SPRITE_VIS
static void
custom_ui_spriteVisChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    custom_ui_state_t *ui = (custom_ui_state_t *)user;
    if (!ui) {
        return;
    }
    if (ui->suppressSpriteVisCallbacks) {
        return;
    }
    ui->spriteVisEnabled = selected ? 1 : 0;
    custom_ui_applySpriteVisOption();
}
#endif

static void
custom_ui_copperVisualiserChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    custom_ui_state_t *ui = (custom_ui_state_t*)user;
    if (!ui) {
        return;
    }
    ui->copperVisualiserEnabled = selected ? 1 : 0;
    emu_ami_setCopperDebugEnabled(ui->copperVisualiserEnabled);
}

static void
custom_ui_blitterVisPatternChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    custom_ui_state_t *ui = (custom_ui_state_t*)user;
    if (!ui) {
        return;
    }
    if (!ui->blitterDebugEnabled) {
        return;
    }
    if (ui->suppressBlitterVisModeCallbacks) {
        return;
    }
    if (selected) {
        ui->blitterVisMode |= CUSTOM_UI_BLITTER_VIS_MODE_PATTERN;
        ui->blitterVisMode &= ~CUSTOM_UI_BLITTER_VIS_MODE_SOLID;
        if (ui->blitterVisModeCheckbox) {
            ui->suppressBlitterVisModeCallbacks = 1;
            e9ui_checkbox_setSelected(ui->blitterVisModeCheckbox, 0, &ui->ctx);
            ui->suppressBlitterVisModeCallbacks = 0;
        }
    } else {
        ui->blitterVisMode &= ~CUSTOM_UI_BLITTER_VIS_MODE_PATTERN;
    }
    custom_ui_applyBlitterVisModeOption();
}

static void
custom_ui_blitterVisModeChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    custom_ui_state_t *ui = (custom_ui_state_t*)user;
    if (!ui) {
        return;
    }
    if (!ui->blitterDebugEnabled) {
        return;
    }
    if (ui->suppressBlitterVisModeCallbacks) {
        return;
    }
    if (selected) {
        ui->blitterVisMode |= CUSTOM_UI_BLITTER_VIS_MODE_SOLID;
        ui->blitterVisMode &= ~CUSTOM_UI_BLITTER_VIS_MODE_PATTERN;
        if (ui->blitterVisPatternCheckbox) {
            ui->suppressBlitterVisModeCallbacks = 1;
            e9ui_checkbox_setSelected(ui->blitterVisPatternCheckbox, 0, &ui->ctx);
            ui->suppressBlitterVisModeCallbacks = 0;
        }
    } else {
        ui->blitterVisMode &= ~CUSTOM_UI_BLITTER_VIS_MODE_SOLID;
    }
    custom_ui_applyBlitterVisModeOption();
}

static void
custom_ui_blitterVisCollectChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    custom_ui_state_t *ui = (custom_ui_state_t*)user;
    if (!ui) {
        return;
    }
    if (!ui->blitterDebugEnabled) {
        return;
    }
    if (ui->suppressBlitterVisModeCallbacks) {
        return;
    }
    if (selected) {
        ui->blitterVisMode |= CUSTOM_UI_BLITTER_VIS_MODE_COLLECT;
    } else {
        ui->blitterVisMode &= ~CUSTOM_UI_BLITTER_VIS_MODE_COLLECT;
    }
    custom_ui_applyBlitterVisModeOption();
}

static void
custom_ui_blitterVisBlinkChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    custom_ui_state_t *ui = (custom_ui_state_t*)user;
    if (!ui) {
        return;
    }
    if (!ui->blitterDebugEnabled) {
        return;
    }
    ui->blitterVisBlink = selected ? 1 : 0;
    custom_ui_applyBlitterVisBlinkOption();
}

static void
custom_ui_blitterVisDecayChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *value, void *user)
{
    (void)ctx;
    (void)comp;
    (void)value;
    (void)user;
}

static void
custom_ui_blitterVisDecaySeekChanged(float percent, void *user)
{
    custom_ui_state_t *ui = (custom_ui_state_t*)user;
    if (!ui || !ui->blitterDebugEnabled) {
        return;
    }
    int nextDecay = custom_ui_blitterVisDecayFromPercent(percent);
    if (nextDecay == ui->blitterVisDecay) {
        return;
    }
    ui->blitterVisDecay = nextDecay;
    custom_ui_applyBlitterVisDecayOption();
    if (ui->blitterVisDecayRow) {
        char decayText[16];
        snprintf(decayText, sizeof(decayText), "%d", ui->blitterVisDecay);
        e9ui_labeled_textbox_setText(ui->blitterVisDecayRow, decayText);
    }
    custom_ui_syncBlitterVisDecaySeekBar(ui);
}

static void
custom_ui_blitterVisDecaySeekTooltip(float percent, char *out, size_t cap, void *user)
{
    (void)user;
    if (!out || cap == 0) {
        return;
    }
    int decay = custom_ui_blitterVisDecayFromPercent(percent);
    snprintf(out, cap, "Decay %d", decay);
}

static void
custom_ui_copperLimitStartChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *value, void *user)
{
    (void)ctx;
    (void)comp;
    (void)value;
    (void)user;
}

static void
custom_ui_copperLimitEndChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *value, void *user)
{
    (void)ctx;
    (void)comp;
    (void)value;
    (void)user;
}

static void
custom_ui_bplptrLineLimitStartChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *value, void *user)
{
    (void)ctx;
    (void)comp;
    (void)value;
    (void)user;
}

static void
custom_ui_bplptrLineLimitEndChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *value, void *user)
{
    (void)ctx;
    (void)comp;
    (void)value;
    (void)user;
}

static int
custom_ui_clampCopperLine(int line)
{
    if (line < 0) {
        return 0;
    }
    if (line > CUSTOM_UI_COPPER_LINE_MAX) {
        return CUSTOM_UI_COPPER_LINE_MAX;
    }
    return line;
}

static void
custom_ui_commitBlitterVisDecayTextbox(custom_ui_state_t *ui)
{
    if (!ui || !ui->blitterVisDecayTextbox) {
        return;
    }
    if (!ui->blitterDebugEnabled) {
        return;
    }

    const char *value = e9ui_textbox_getText(ui->blitterVisDecayTextbox);
    int nextDecay = 0;
    if (!value || sscanf(value, "%d", &nextDecay) != 1) {
        return;
    }
    if (nextDecay <= 0 || nextDecay > CUSTOM_UI_BLITTER_VIS_DECAY_MAX) {
        return;
    }
    if (nextDecay == ui->blitterVisDecay) {
        return;
    }
    ui->blitterVisDecay = nextDecay;
    custom_ui_applyBlitterVisDecayOption();
    custom_ui_syncBlitterVisDecaySeekBar(ui);
}

static int
custom_ui_blitterVisDecayTextboxKey(e9ui_context_t *ctx, SDL_Keycode key, SDL_Keymod mods, void *user)
{
    (void)ctx;
    (void)mods;
    custom_ui_state_t *ui = (custom_ui_state_t*)user;
    if (!ui || !ui->blitterVisDecayTextbox) {
        return 0;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        custom_ui_commitBlitterVisDecayTextbox(ui);
    }
    return 0;
}

static void
custom_ui_tickBlitterVisDecayTextbox(custom_ui_state_t *ui)
{
    if (!ui || !ui->blitterVisDecayTextbox) {
        return;
    }
    int hasFocus = e9ui_getFocus(&ui->ctx) == ui->blitterVisDecayTextbox ? 1 : 0;
    if (ui->blitterVisDecayTextboxHadFocus && !hasFocus) {
        custom_ui_commitBlitterVisDecayTextbox(ui);
    }
    ui->blitterVisDecayTextboxHadFocus = hasFocus;
}

static void
custom_ui_commitCopperLimitStartTextbox(custom_ui_state_t *ui)
{
    if (!ui || !ui->copperLimitStartTextbox) {
        return;
    }
    if (!ui->copperLimitEnabled) {
        return;
    }

    const char *value = e9ui_textbox_getText(ui->copperLimitStartTextbox);
    int nextStart = 0;
    if (!value || sscanf(value, "%d", &nextStart) != 1) {
        return;
    }
    nextStart = custom_ui_clampCopperLine(nextStart);
    if (nextStart == ui->copperLimitStart) {
        return;
    }
    ui->copperLimitStart = nextStart;
    custom_ui_applyCopperLimitStartOption();
}

static void
custom_ui_commitCopperLimitEndTextbox(custom_ui_state_t *ui)
{
    if (!ui || !ui->copperLimitEndTextbox) {
        return;
    }
    if (!ui->copperLimitEnabled) {
        return;
    }

    const char *value = e9ui_textbox_getText(ui->copperLimitEndTextbox);
    int nextEnd = 0;
    if (!value || sscanf(value, "%d", &nextEnd) != 1) {
        return;
    }
    nextEnd = custom_ui_clampCopperLine(nextEnd);
    if (nextEnd == ui->copperLimitEnd) {
        return;
    }
    ui->copperLimitEnd = nextEnd;
    custom_ui_applyCopperLimitEndOption();
}

static int
custom_ui_copperLimitStartTextboxKey(e9ui_context_t *ctx, SDL_Keycode key, SDL_Keymod mods, void *user)
{
    (void)ctx;
    (void)mods;
    custom_ui_state_t *ui = (custom_ui_state_t*)user;
    if (!ui || !ui->copperLimitStartTextbox) {
        return 0;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        custom_ui_commitCopperLimitStartTextbox(ui);
    }
    return 0;
}

static int
custom_ui_copperLimitEndTextboxKey(e9ui_context_t *ctx, SDL_Keycode key, SDL_Keymod mods, void *user)
{
    (void)ctx;
    (void)mods;
    custom_ui_state_t *ui = (custom_ui_state_t*)user;
    if (!ui || !ui->copperLimitEndTextbox) {
        return 0;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        custom_ui_commitCopperLimitEndTextbox(ui);
    }
    return 0;
}

static void
custom_ui_tickCopperLimitTextboxes(custom_ui_state_t *ui)
{
    if (!ui) {
        return;
    }
    int startHasFocus = e9ui_getFocus(&ui->ctx) == ui->copperLimitStartTextbox ? 1 : 0;
    if (ui->copperLimitStartTextboxHadFocus && !startHasFocus) {
        custom_ui_commitCopperLimitStartTextbox(ui);
    }
    ui->copperLimitStartTextboxHadFocus = startHasFocus;

    int endHasFocus = e9ui_getFocus(&ui->ctx) == ui->copperLimitEndTextbox ? 1 : 0;
    if (ui->copperLimitEndTextboxHadFocus && !endHasFocus) {
        custom_ui_commitCopperLimitEndTextbox(ui);
    }
    ui->copperLimitEndTextboxHadFocus = endHasFocus;
}

static void
custom_ui_commitBplptrLineLimitStartTextbox(custom_ui_state_t *ui)
{
    if (!ui || !ui->bplptrLineLimitStartTextbox) {
        return;
    }

    const char *value = e9ui_textbox_getText(ui->bplptrLineLimitStartTextbox);
    int nextStart = 0;
    if (!value || sscanf(value, "%d", &nextStart) != 1) {
        return;
    }
    nextStart = custom_ui_clampCopperLine(nextStart);
    if (nextStart == ui->bplptrLineLimitStart) {
        return;
    }
    ui->bplptrLineLimitStart = nextStart;
    custom_ui_applyBplptrLineLimitStartOption();
}

static void
custom_ui_commitBplptrLineLimitEndTextbox(custom_ui_state_t *ui)
{
    if (!ui || !ui->bplptrLineLimitEndTextbox) {
        return;
    }

    const char *value = e9ui_textbox_getText(ui->bplptrLineLimitEndTextbox);
    int nextEnd = 0;
    if (!value || sscanf(value, "%d", &nextEnd) != 1) {
        return;
    }
    nextEnd = custom_ui_clampCopperLine(nextEnd);
    if (nextEnd == ui->bplptrLineLimitEnd) {
        return;
    }
    ui->bplptrLineLimitEnd = nextEnd;
    custom_ui_applyBplptrLineLimitEndOption();
}

static int
custom_ui_bplptrLineLimitStartTextboxKey(e9ui_context_t *ctx, SDL_Keycode key, SDL_Keymod mods, void *user)
{
    (void)ctx;
    (void)mods;
    custom_ui_state_t *ui = (custom_ui_state_t*)user;
    if (!ui || !ui->bplptrLineLimitStartTextbox) {
        return 0;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        custom_ui_commitBplptrLineLimitStartTextbox(ui);
    }
    return 0;
}

static int
custom_ui_bplptrLineLimitEndTextboxKey(e9ui_context_t *ctx, SDL_Keycode key, SDL_Keymod mods, void *user)
{
    (void)ctx;
    (void)mods;
    custom_ui_state_t *ui = (custom_ui_state_t*)user;
    if (!ui || !ui->bplptrLineLimitEndTextbox) {
        return 0;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        custom_ui_commitBplptrLineLimitEndTextbox(ui);
    }
    return 0;
}

static void
custom_ui_tickBplptrLineLimitTextboxes(custom_ui_state_t *ui)
{
    if (!ui) {
        return;
    }
    int startHasFocus = e9ui_getFocus(&ui->ctx) == ui->bplptrLineLimitStartTextbox ? 1 : 0;
    if (ui->bplptrLineLimitStartTextboxHadFocus && !startHasFocus) {
        custom_ui_commitBplptrLineLimitStartTextbox(ui);
    }
    ui->bplptrLineLimitStartTextboxHadFocus = startHasFocus;

    int endHasFocus = e9ui_getFocus(&ui->ctx) == ui->bplptrLineLimitEndTextbox ? 1 : 0;
    if (ui->bplptrLineLimitEndTextboxHadFocus && !endHasFocus) {
        custom_ui_commitBplptrLineLimitEndTextbox(ui);
    }
    ui->bplptrLineLimitEndTextboxHadFocus = endHasFocus;
}

static void
custom_ui_bplcon1DelayScrollChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    custom_ui_state_t *ui = (custom_ui_state_t*)user;
    if (!ui) {
        return;
    }
    ui->bplcon1DelayScrollEnabled = selected ? 1 : 0;
    custom_ui_applyBplcon1DelayScrollOption();
}

static void
custom_ui_spritesChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    custom_ui_state_t *ui = (custom_ui_state_t*)user;
    if (!ui) {
        return;
    }
    if (ui->suppressSpriteCallbacks) {
        return;
    }
    int nextValue = selected ? 1 : 0;
    ui->spritesEnabled = nextValue;
    for (int spriteIndex = 0; spriteIndex < CUSTOM_UI_AMIGA_SPRITE_COUNT; ++spriteIndex) {
        ui->spriteEnabled[spriteIndex] = nextValue;
        custom_ui_applySpriteOption(spriteIndex);
        if (ui->spriteCheckboxes[spriteIndex]) {
            ui->suppressSpriteCallbacks = 1;
            e9ui_checkbox_setSelected(ui->spriteCheckboxes[spriteIndex], nextValue, &ui->ctx);
            ui->suppressSpriteCallbacks = 0;
        }
    }
}

static void
custom_ui_spriteChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    if (!user) {
        return;
    }
    struct custom_ui_sprite_cb *cb = (struct custom_ui_sprite_cb *)user;
    if (!cb->ui) {
        return;
    }
    if (cb->spriteIndex < 0 || cb->spriteIndex >= CUSTOM_UI_AMIGA_SPRITE_COUNT) {
        return;
    }
    if (cb->ui->suppressSpriteCallbacks) {
        return;
    }
    cb->ui->spriteEnabled[cb->spriteIndex] = selected ? 1 : 0;
    custom_ui_applySpriteOption(cb->spriteIndex);
    custom_ui_syncSpritesMasterCheckbox(cb->ui);
}

static void
custom_ui_bitplanesChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    custom_ui_state_t *ui = (custom_ui_state_t*)user;
    if (!ui) {
        return;
    }
    if (ui->suppressBitplaneCallbacks) {
        return;
    }
    int nextValue = selected ? 1 : 0;
    ui->bitplanesEnabled = nextValue;
    for (int bitplaneIndex = 0; bitplaneIndex < CUSTOM_UI_AMIGA_BITPLANE_COUNT; ++bitplaneIndex) {
        ui->bitplaneEnabled[bitplaneIndex] = nextValue;
        custom_ui_applyBitplaneOption(bitplaneIndex);
        if (ui->bitplaneCheckboxes[bitplaneIndex]) {
            ui->suppressBitplaneCallbacks = 1;
            e9ui_checkbox_setSelected(ui->bitplaneCheckboxes[bitplaneIndex], nextValue, &ui->ctx);
            ui->suppressBitplaneCallbacks = 0;
        }
    }
}

static void
custom_ui_bitplaneChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    if (!user) {
        return;
    }
    struct custom_ui_bitplane_cb *cb = (struct custom_ui_bitplane_cb *)user;
    if (!cb->ui) {
        return;
    }
    if (cb->bitplaneIndex < 0 || cb->bitplaneIndex >= CUSTOM_UI_AMIGA_BITPLANE_COUNT) {
        return;
    }
    if (cb->ui->suppressBitplaneCallbacks) {
        return;
    }
    cb->ui->bitplaneEnabled[cb->bitplaneIndex] = selected ? 1 : 0;
    custom_ui_applyBitplaneOption(cb->bitplaneIndex);
    custom_ui_syncBitplanesMasterCheckbox(cb->ui);
}

static void
custom_ui_audiosChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    custom_ui_state_t *ui = (custom_ui_state_t*)user;
    if (!ui) {
        return;
    }
    if (ui->suppressAudioCallbacks) {
        return;
    }
    int nextValue = selected ? 1 : 0;
    ui->audiosEnabled = nextValue;
    for (int audioChannelIndex = 0; audioChannelIndex < CUSTOM_UI_AMIGA_AUDIO_CHANNEL_COUNT; ++audioChannelIndex) {
        ui->audioEnabled[audioChannelIndex] = nextValue;
        custom_ui_applyAudioOption(audioChannelIndex);
        if (ui->audioCheckboxes[audioChannelIndex]) {
            ui->suppressAudioCallbacks = 1;
            e9ui_checkbox_setSelected(ui->audioCheckboxes[audioChannelIndex], nextValue, &ui->ctx);
            ui->suppressAudioCallbacks = 0;
        }
    }
}

static void
custom_ui_audioChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    if (!user) {
        return;
    }
    struct custom_ui_audio_cb *cb = (struct custom_ui_audio_cb *)user;
    if (!cb->ui) {
        return;
    }
    if (cb->audioChannelIndex < 0 || cb->audioChannelIndex >= CUSTOM_UI_AMIGA_AUDIO_CHANNEL_COUNT) {
        return;
    }
    if (cb->ui->suppressAudioCallbacks) {
        return;
    }
    cb->ui->audioEnabled[cb->audioChannelIndex] = selected ? 1 : 0;
    custom_ui_applyAudioOption(cb->audioChannelIndex);
    custom_ui_syncAudiosMasterCheckbox(cb->ui);
}

static e9ui_component_t *
custom_ui_buildRoot(custom_ui_state_t *ui)
{
    e9ui_component_t *rootStack = e9ui_stack_makeVertical();
    if (!rootStack) {
        return NULL;
    }
    e9ui_stack_addFixed(rootStack, e9ui_vspacer_make(12));

    e9ui_component_t *columns = e9ui_hstack_make();
    if (!columns) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }

    e9ui_component_t *leftColumn = e9ui_stack_makeVertical();
    if (!leftColumn) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }

    e9ui_component_t *cbBplcon1DelayScroll = e9ui_checkbox_make("BPLCON1 Scroll",
                                                                 ui->bplcon1DelayScrollEnabled,
                                                                 custom_ui_bplcon1DelayScrollChanged,
                                                                 ui);
    if (!cbBplcon1DelayScroll) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    e9ui_checkbox_setLeftMargin(cbBplcon1DelayScroll, 12);
    e9ui_stack_addFixed(leftColumn, cbBplcon1DelayScroll);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(8));

    e9ui_component_t *cbSprites = e9ui_checkbox_make("Sprites",
                                                     ui->spritesEnabled,
                                                     custom_ui_spritesChanged,
                                                     ui);
    if (!cbSprites) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->spritesCheckbox = cbSprites;
    e9ui_checkbox_setLeftMargin(cbSprites, 12);
    e9ui_stack_addFixed(leftColumn, cbSprites);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(6));

    e9ui_component_t *spriteColumns = e9ui_hstack_make();
    if (!spriteColumns) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    e9ui_component_t *spriteColumnLeft = e9ui_stack_makeVertical();
    e9ui_component_t *spriteColumnRight = e9ui_stack_makeVertical();
    if (!spriteColumnLeft || !spriteColumnRight) {
        if (spriteColumnLeft) {
            e9ui_childDestroy(spriteColumnLeft, &ui->ctx);
        }
        if (spriteColumnRight) {
            e9ui_childDestroy(spriteColumnRight, &ui->ctx);
        }
        e9ui_childDestroy(spriteColumns, &ui->ctx);
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    e9ui_hstack_addFlex(spriteColumns, spriteColumnLeft);
    e9ui_hstack_addFixed(spriteColumns, e9ui_spacer_make(6), 6);
    e9ui_hstack_addFlex(spriteColumns, spriteColumnRight);

    for (int spriteIndex = 0; spriteIndex < CUSTOM_UI_AMIGA_SPRITE_COUNT; ++spriteIndex) {
        char label[32];
        snprintf(label, sizeof(label), "Spr %d", spriteIndex);
        ui->spriteCb[spriteIndex].ui = ui;
        ui->spriteCb[spriteIndex].spriteIndex = spriteIndex;
        e9ui_component_t *cbSprite = e9ui_checkbox_make(label,
                                                        ui->spriteEnabled[spriteIndex],
                                                        custom_ui_spriteChanged,
                                                        &ui->spriteCb[spriteIndex]);
        if (!cbSprite) {
            e9ui_childDestroy(rootStack, &ui->ctx);
            return NULL;
        }
        ui->spriteCheckboxes[spriteIndex] = cbSprite;
        if ((spriteIndex & 1) == 0) {
            e9ui_stack_addFixed(spriteColumnLeft, cbSprite);
        } else {
            e9ui_stack_addFixed(spriteColumnRight, cbSprite);
        }
    }
    e9ui_component_t *spriteColumnsInsetRow = custom_ui_insetRowMake(spriteColumns, 24, 0);
    if (!spriteColumnsInsetRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    e9ui_stack_addFixed(leftColumn, spriteColumnsInsetRow);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(8));

    e9ui_component_t *cbBitplanes = e9ui_checkbox_make("Bitplanes",
                                                       ui->bitplanesEnabled,
                                                       custom_ui_bitplanesChanged,
                                                       ui);
    if (!cbBitplanes) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->bitplanesCheckbox = cbBitplanes;
    e9ui_checkbox_setLeftMargin(cbBitplanes, 12);
    e9ui_stack_addFixed(leftColumn, cbBitplanes);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(6));

    e9ui_component_t *bitplaneColumns = e9ui_hstack_make();
    if (!bitplaneColumns) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    e9ui_component_t *bitplaneColumnLeft = e9ui_stack_makeVertical();
    e9ui_component_t *bitplaneColumnRight = e9ui_stack_makeVertical();
    if (!bitplaneColumnLeft || !bitplaneColumnRight) {
        if (bitplaneColumnLeft) {
            e9ui_childDestroy(bitplaneColumnLeft, &ui->ctx);
        }
        if (bitplaneColumnRight) {
            e9ui_childDestroy(bitplaneColumnRight, &ui->ctx);
        }
        e9ui_childDestroy(bitplaneColumns, &ui->ctx);
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    e9ui_hstack_addFlex(bitplaneColumns, bitplaneColumnLeft);
    e9ui_hstack_addFixed(bitplaneColumns, e9ui_spacer_make(6), 6);
    e9ui_hstack_addFlex(bitplaneColumns, bitplaneColumnRight);

    for (int bitplaneIndex = 0; bitplaneIndex < CUSTOM_UI_AMIGA_BITPLANE_COUNT; ++bitplaneIndex) {
        char label[32];
        snprintf(label, sizeof(label), "Bpl %d", bitplaneIndex);
        ui->bitplaneCb[bitplaneIndex].ui = ui;
        ui->bitplaneCb[bitplaneIndex].bitplaneIndex = bitplaneIndex;
        e9ui_component_t *cbBitplane = e9ui_checkbox_make(label,
                                                          ui->bitplaneEnabled[bitplaneIndex],
                                                          custom_ui_bitplaneChanged,
                                                          &ui->bitplaneCb[bitplaneIndex]);
        if (!cbBitplane) {
            e9ui_childDestroy(rootStack, &ui->ctx);
            return NULL;
        }
        ui->bitplaneCheckboxes[bitplaneIndex] = cbBitplane;
        if ((bitplaneIndex & 1) == 0) {
            e9ui_stack_addFixed(bitplaneColumnLeft, cbBitplane);
        } else {
            e9ui_stack_addFixed(bitplaneColumnRight, cbBitplane);
        }
    }
    e9ui_component_t *bitplaneColumnsInsetRow = custom_ui_insetRowMake(bitplaneColumns, 24, 0);
    if (!bitplaneColumnsInsetRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    e9ui_stack_addFixed(leftColumn, bitplaneColumnsInsetRow);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(8));

    e9ui_component_t *cbBplptrBlockAll = e9ui_checkbox_make("Bitplane Ptr Block",
                                                            ui->bplptrBlockAllEnabled,
                                                            custom_ui_bplptrBlockAllChanged,
                                                            ui);
    if (!cbBplptrBlockAll) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->bplptrBlockAllCheckbox = cbBplptrBlockAll;
    e9ui_checkbox_setLeftMargin(cbBplptrBlockAll, 12);
    e9ui_stack_addFixed(leftColumn, cbBplptrBlockAll);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(6));

    char bplptrLineLimitStartText[16];
    snprintf(bplptrLineLimitStartText, sizeof(bplptrLineLimitStartText), "%d", custom_ui_clampCopperLine(ui->bplptrLineLimitStart));
    e9ui_component_t *bplptrLineLimitStartRow = e9ui_labeled_textbox_make("Start",
                                                                           78,
                                                                           0,
                                                                           custom_ui_bplptrLineLimitStartChanged,
                                                                           ui);
    if (!bplptrLineLimitStartRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    e9ui_labeled_textbox_setText(bplptrLineLimitStartRow, bplptrLineLimitStartText);
    e9ui_component_t *bplptrLineLimitStartTextbox = e9ui_labeled_textbox_getTextbox(bplptrLineLimitStartRow);
    if (bplptrLineLimitStartTextbox) {
        e9ui_textbox_setNumericOnly(bplptrLineLimitStartTextbox, 1);
        e9ui_textbox_setKeyHandler(bplptrLineLimitStartTextbox, custom_ui_bplptrLineLimitStartTextboxKey, ui);
    }
    ui->bplptrLineLimitStartRow = bplptrLineLimitStartRow;
    ui->bplptrLineLimitStartTextbox = bplptrLineLimitStartTextbox;
    e9ui_component_t *bplptrLineLimitStartInsetRow = custom_ui_insetRowMake(bplptrLineLimitStartRow, 0, 14);
    if (!bplptrLineLimitStartInsetRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    e9ui_stack_addFixed(leftColumn, bplptrLineLimitStartInsetRow);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(6));

    char bplptrLineLimitEndText[16];
    snprintf(bplptrLineLimitEndText, sizeof(bplptrLineLimitEndText), "%d", custom_ui_clampCopperLine(ui->bplptrLineLimitEnd));
    e9ui_component_t *bplptrLineLimitEndRow = e9ui_labeled_textbox_make("End",
                                                                         78,
                                                                         0,
                                                                         custom_ui_bplptrLineLimitEndChanged,
                                                                         ui);
    if (!bplptrLineLimitEndRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    e9ui_labeled_textbox_setText(bplptrLineLimitEndRow, bplptrLineLimitEndText);
    e9ui_component_t *bplptrLineLimitEndTextbox = e9ui_labeled_textbox_getTextbox(bplptrLineLimitEndRow);
    if (bplptrLineLimitEndTextbox) {
        e9ui_textbox_setNumericOnly(bplptrLineLimitEndTextbox, 1);
        e9ui_textbox_setKeyHandler(bplptrLineLimitEndTextbox, custom_ui_bplptrLineLimitEndTextboxKey, ui);
    }
    ui->bplptrLineLimitEndRow = bplptrLineLimitEndRow;
    ui->bplptrLineLimitEndTextbox = bplptrLineLimitEndTextbox;
    e9ui_component_t *bplptrLineLimitEndInsetRow = custom_ui_insetRowMake(bplptrLineLimitEndRow, 0, 14);
    if (!bplptrLineLimitEndInsetRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    e9ui_stack_addFixed(leftColumn, bplptrLineLimitEndInsetRow);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(6));

    for (int bplptrIndex = 0; bplptrIndex < CUSTOM_UI_AMIGA_BPLPTR_COUNT; ++bplptrIndex) {
        char label[32];
        snprintf(label, sizeof(label), "BPL%dPT", bplptrIndex + 1);
        ui->bplptrCb[bplptrIndex].ui = ui;
        ui->bplptrCb[bplptrIndex].bplptrIndex = bplptrIndex;
        e9ui_component_t *cbBplptrBlock = e9ui_checkbox_make(label,
                                                             ui->bplptrBlockEnabled[bplptrIndex],
                                                             custom_ui_bplptrBlockChanged,
                                                             &ui->bplptrCb[bplptrIndex]);
        if (!cbBplptrBlock) {
            e9ui_childDestroy(rootStack, &ui->ctx);
            return NULL;
        }
        ui->bplptrBlockCheckboxes[bplptrIndex] = cbBplptrBlock;
        e9ui_checkbox_setLeftMargin(cbBplptrBlock, 28);
        e9ui_stack_addFixed(leftColumn, cbBplptrBlock);
    }
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(8));

    e9ui_component_t *cbCopperLimit = e9ui_checkbox_make("Copper Block",
                                                         ui->copperLimitEnabled,
                                                         custom_ui_copperLimitChanged,
                                                         ui);
    if (!cbCopperLimit) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->copperLimitCheckbox = cbCopperLimit;
    e9ui_checkbox_setLeftMargin(cbCopperLimit, 12);
    e9ui_stack_addFixed(leftColumn, cbCopperLimit);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(6));

    char copperLimitStartText[16];
    snprintf(copperLimitStartText, sizeof(copperLimitStartText), "%d", custom_ui_clampCopperLine(ui->copperLimitStart));
    e9ui_component_t *copperLimitStartRow = e9ui_labeled_textbox_make("Start",
                                                                       78,
                                                                       0,
                                                                       custom_ui_copperLimitStartChanged,
                                                                       ui);
    if (!copperLimitStartRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    e9ui_labeled_textbox_setText(copperLimitStartRow, copperLimitStartText);
    e9ui_component_t *copperLimitStartTextbox = e9ui_labeled_textbox_getTextbox(copperLimitStartRow);
    if (copperLimitStartTextbox) {
        e9ui_textbox_setNumericOnly(copperLimitStartTextbox, 1);
        e9ui_textbox_setKeyHandler(copperLimitStartTextbox, custom_ui_copperLimitStartTextboxKey, ui);
    }
    ui->copperLimitStartRow = copperLimitStartRow;
    ui->copperLimitStartTextbox = copperLimitStartTextbox;
    e9ui_component_t *copperLimitStartInsetRow = custom_ui_insetRowMake(copperLimitStartRow, 0, 14);
    if (!copperLimitStartInsetRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    e9ui_stack_addFixed(leftColumn, copperLimitStartInsetRow);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(6));

    char copperLimitEndText[16];
    snprintf(copperLimitEndText, sizeof(copperLimitEndText), "%d", custom_ui_clampCopperLine(ui->copperLimitEnd));
    e9ui_component_t *copperLimitEndRow = e9ui_labeled_textbox_make("End",
                                                                     78,
                                                                     0,
                                                                     custom_ui_copperLimitEndChanged,
                                                                     ui);
    if (!copperLimitEndRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    e9ui_labeled_textbox_setText(copperLimitEndRow, copperLimitEndText);
    e9ui_component_t *copperLimitEndTextbox = e9ui_labeled_textbox_getTextbox(copperLimitEndRow);
    if (copperLimitEndTextbox) {
        e9ui_textbox_setNumericOnly(copperLimitEndTextbox, 1);
        e9ui_textbox_setKeyHandler(copperLimitEndTextbox, custom_ui_copperLimitEndTextboxKey, ui);
    }
    ui->copperLimitEndRow = copperLimitEndRow;
    ui->copperLimitEndTextbox = copperLimitEndTextbox;
    e9ui_component_t *copperLimitEndInsetRow = custom_ui_insetRowMake(copperLimitEndRow, 0, 14);
    if (!copperLimitEndInsetRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    e9ui_stack_addFixed(leftColumn, copperLimitEndInsetRow);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(8));

    custom_ui_syncCopperLimitSuboptions(ui);

    e9ui_component_t *cbBlitter = e9ui_checkbox_make("Blitter",
                                                     ui->blitterEnabled,
                                                     custom_ui_blitterChanged,
                                                     ui);
    if (!cbBlitter) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    e9ui_checkbox_setLeftMargin(cbBlitter, 12);
    e9ui_stack_addFixed(leftColumn, cbBlitter);
    e9ui_stack_addFixed(leftColumn, e9ui_vspacer_make(8));

    e9ui_component_t *cbEstimateFps = e9ui_checkbox_make("Estimate FPS",
                                                         ui->estimateFpsEnabled,
                                                         custom_ui_estimateFpsChanged,
                                                         ui);
    if (!cbEstimateFps) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->estimateFpsCheckbox = cbEstimateFps;
    e9ui_checkbox_setLeftMargin(cbEstimateFps, 12);

    e9ui_component_t *estimateFpsText = e9ui_text_make("--");
    if (!estimateFpsText) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    e9ui_text_setColor(estimateFpsText, (SDL_Color){ 196, 214, 232, 255 });
    ui->estimateFpsText = estimateFpsText;
    e9ui_component_t *estimateFpsTextRow = custom_ui_insetRowMake(estimateFpsText, 0, 0);
    if (!estimateFpsTextRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->estimateFpsTextRow = estimateFpsTextRow;
    e9ui_component_t *estimateFpsRow = custom_ui_dmaStatsHeaderRowMake(cbEstimateFps, estimateFpsTextRow, 0, 16);
    if (!estimateFpsRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    e9ui_stack_addFixed(leftColumn, estimateFpsRow);
    custom_ui_syncEstimateFpsDisplay(ui);

    e9ui_component_t *rightColumn = e9ui_stack_makeVertical();
    if (!rightColumn) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }

    e9ui_component_t *cbCopperVisualiser = e9ui_checkbox_make("Copper Visualiser",
                                                              ui->copperVisualiserEnabled,
                                                              custom_ui_copperVisualiserChanged,
                                                              ui);
    if (!cbCopperVisualiser) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->copperVisualiserCheckbox = cbCopperVisualiser;
    e9ui_checkbox_setLeftMargin(cbCopperVisualiser, 12);
    e9ui_stack_addFixed(rightColumn, cbCopperVisualiser);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(8));

#if E9K_HACK_AMI_SPRITE_VIS
    e9ui_component_t *cbSpriteVis = e9ui_checkbox_make("Sprite Visualiser",
                                                       ui->spriteVisEnabled,
                                                       custom_ui_spriteVisChanged,
                                                       ui);
    if (!cbSpriteVis) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->spriteVisCheckbox = cbSpriteVis;
    e9ui_checkbox_setLeftMargin(cbSpriteVis, 12);
    e9ui_stack_addFixed(rightColumn, cbSpriteVis);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(8));
#endif

    e9ui_component_t *cbBlitterDebug = e9ui_checkbox_make("Blitter Visualiser",
                                                           ui->blitterDebugEnabled,
                                                           custom_ui_blitterDebugChanged,
                                                           ui);
    if (!cbBlitterDebug) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->blitterDebugCheckbox = cbBlitterDebug;
    e9ui_checkbox_setLeftMargin(cbBlitterDebug, 12);
    e9ui_stack_addFixed(rightColumn, cbBlitterDebug);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(8));

    e9ui_component_t *cbBlitterVisCollect = e9ui_checkbox_make("Overlay",
                                                               (ui->blitterVisMode & CUSTOM_UI_BLITTER_VIS_MODE_COLLECT) != 0,
                                                               custom_ui_blitterVisCollectChanged,
                                                               ui);
    if (!cbBlitterVisCollect) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->blitterVisCollectCheckbox = cbBlitterVisCollect;
    e9ui_checkbox_setLeftMargin(cbBlitterVisCollect, 28);
    /* Hidden (kept for state/default compatibility):
     * e9ui_stack_addFixed(rightColumn, cbBlitterVisCollect);
     * e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(8));
     */

    char decayText[16];
    snprintf(decayText, sizeof(decayText), "%d", ui->blitterVisDecay);
    e9ui_component_t *blitterVisDecayTextboxRow = e9ui_labeled_textbox_make("Decay",
                                                                             78,
                                                                             0,
                                                                             custom_ui_blitterVisDecayChanged,
                                                                             ui);
    if (!blitterVisDecayTextboxRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    e9ui_labeled_textbox_setText(blitterVisDecayTextboxRow, decayText);
    e9ui_component_t *blitterVisDecayTextbox = e9ui_labeled_textbox_getTextbox(blitterVisDecayTextboxRow);
    if (blitterVisDecayTextbox) {
        e9ui_textbox_setKeyHandler(blitterVisDecayTextbox, custom_ui_blitterVisDecayTextboxKey, ui);
    }
    ui->blitterVisDecayRow = blitterVisDecayTextboxRow;
    ui->blitterVisDecayTextbox = blitterVisDecayTextbox;
    e9ui_component_t *blitterVisDecayInsetRow = custom_ui_insetRowMake(blitterVisDecayTextboxRow, 0, 14);
    if (!blitterVisDecayInsetRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    e9ui_stack_addFixed(rightColumn, blitterVisDecayInsetRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(4));

    e9ui_component_t *blitterVisDecaySeekBar = NULL;
    e9ui_component_t *blitterVisDecaySeekRow = custom_ui_blitterVisDecaySeekRowMake(&blitterVisDecaySeekBar);
    if (!blitterVisDecaySeekRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    if (blitterVisDecaySeekBar) {
        e9ui_seek_bar_setCallback(blitterVisDecaySeekBar, custom_ui_blitterVisDecaySeekChanged, ui);
        e9ui_seek_bar_setTooltipCallback(blitterVisDecaySeekBar, custom_ui_blitterVisDecaySeekTooltip, ui);
    }
    ui->blitterVisDecaySeekRow = blitterVisDecaySeekRow;
    ui->blitterVisDecaySeekBar = blitterVisDecaySeekBar;
    custom_ui_syncBlitterVisDecaySeekBar(ui);
    e9ui_stack_addFixed(rightColumn, blitterVisDecaySeekRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(8));

    e9ui_component_t *cbBlitterVisBlink = e9ui_checkbox_make("Core Blink",
                                                             ui->blitterVisBlink,
                                                             custom_ui_blitterVisBlinkChanged,
                                                             ui);
    if (!cbBlitterVisBlink) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->blitterVisBlinkCheckbox = cbBlitterVisBlink;
    e9ui_checkbox_setLeftMargin(cbBlitterVisBlink, 28);
    /* Hidden (kept for state/default compatibility):
     * e9ui_stack_addFixed(rightColumn, cbBlitterVisBlink);
     * e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(8));
     */

    e9ui_component_t *cbBlitterVisPattern = e9ui_checkbox_make("Core Pattern",
                                                               (ui->blitterVisMode & CUSTOM_UI_BLITTER_VIS_MODE_PATTERN) != 0,
                                                               custom_ui_blitterVisPatternChanged,
                                                               ui);
    if (!cbBlitterVisPattern) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->blitterVisPatternCheckbox = cbBlitterVisPattern;
    e9ui_checkbox_setLeftMargin(cbBlitterVisPattern, 28);
    /* Hidden (kept for state/default compatibility):
     * e9ui_stack_addFixed(rightColumn, cbBlitterVisPattern);
     * e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(8));
     */

    e9ui_component_t *cbBlitterVisMode = e9ui_checkbox_make("Core Solid",
                                                            (ui->blitterVisMode & CUSTOM_UI_BLITTER_VIS_MODE_SOLID) != 0,
                                                            custom_ui_blitterVisModeChanged,
                                                            ui);
    if (!cbBlitterVisMode) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->blitterVisModeCheckbox = cbBlitterVisMode;
    e9ui_checkbox_setLeftMargin(cbBlitterVisMode, 28);
    /* Hidden (kept for state/default compatibility):
     * e9ui_stack_addFixed(rightColumn, cbBlitterVisMode);
     * e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(8));
     */

    custom_ui_syncBlitterDebugSuboptions(ui);

    e9ui_component_t *blitterVisStatsChart = custom_ui_blitterStatsChartMake();
    if (!blitterVisStatsChart) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->blitterVisStatsChart = blitterVisStatsChart;
    e9ui_component_t *blitterVisStatsChartInsetRow = custom_ui_insetRowMake(blitterVisStatsChart, 16, 0);
    if (!blitterVisStatsChartInsetRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    e9ui_stack_addFixed(rightColumn, blitterVisStatsChartInsetRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(8));

    e9ui_component_t *cbAudios = e9ui_checkbox_make("Audio",
                                                    ui->audiosEnabled,
                                                    custom_ui_audiosChanged,
                                                    ui);
    if (!cbAudios) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->audiosCheckbox = cbAudios;
    e9ui_checkbox_setLeftMargin(cbAudios, 12);
    e9ui_stack_addFixed(rightColumn, cbAudios);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(6));

    for (int audioChannelIndex = 0; audioChannelIndex < CUSTOM_UI_AMIGA_AUDIO_CHANNEL_COUNT; ++audioChannelIndex) {
        char label[32];
        snprintf(label, sizeof(label), "Audio %d", audioChannelIndex);
        ui->audioCb[audioChannelIndex].ui = ui;
        ui->audioCb[audioChannelIndex].audioChannelIndex = audioChannelIndex;
        e9ui_component_t *cbAudio = e9ui_checkbox_make(label,
                                                       ui->audioEnabled[audioChannelIndex],
                                                       custom_ui_audioChanged,
                                                       &ui->audioCb[audioChannelIndex]);
        if (!cbAudio) {
            e9ui_childDestroy(rootStack, &ui->ctx);
            return NULL;
        }
        ui->audioCheckboxes[audioChannelIndex] = cbAudio;
        e9ui_checkbox_setLeftMargin(cbAudio, 28);
        e9ui_stack_addFixed(rightColumn, cbAudio);
    }

    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(8));

    e9ui_component_t *cbDmaStats = e9ui_checkbox_make("DMA Stats",
                                                      ui->dmaStatsEnabled,
                                                      custom_ui_dmaStatsChanged,
                                                      ui);
    if (!cbDmaStats) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->dmaStatsCheckbox = cbDmaStats;

    e9ui_component_t *dmaStatsHintText = e9ui_text_make("NEEDS CYCLE EXACT!");
    if (!dmaStatsHintText) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    e9ui_text_setColor(dmaStatsHintText, (SDL_Color){ 196, 164, 92, 255 });
    ui->dmaStatsHintText = dmaStatsHintText;
    e9ui_component_t *dmaStatsHintTextInsetRow = custom_ui_insetRowMake(dmaStatsHintText, 0, 0);
    if (!dmaStatsHintTextInsetRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->dmaStatsHintTextRow = dmaStatsHintTextInsetRow;

    e9ui_component_t *dmaStatsRow = custom_ui_dmaStatsHeaderRowMake(cbDmaStats, dmaStatsHintTextInsetRow, 12, 8);
    if (!dmaStatsRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    e9ui_stack_addFixed(rightColumn, dmaStatsRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(6));

    e9ui_component_t *copperStatsChart = custom_ui_copperStatsChartMake();
    if (!copperStatsChart) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->copperStatsChart = copperStatsChart;
    e9ui_component_t *copperStatsChartInsetRow = custom_ui_insetRowMake(copperStatsChart, 16, 0);
    if (!copperStatsChartInsetRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->copperStatsChartRow = copperStatsChartInsetRow;
    e9ui_stack_addFixed(rightColumn, copperStatsChartInsetRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(4));

    e9ui_component_t *blitterDmaStatsChart = custom_ui_blitterDmaStatsChartMake();
    if (!blitterDmaStatsChart) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->blitterDmaStatsChart = blitterDmaStatsChart;
    e9ui_component_t *blitterDmaStatsChartInsetRow = custom_ui_insetRowMake(blitterDmaStatsChart, 16, 0);
    if (!blitterDmaStatsChartInsetRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->blitterDmaStatsChartRow = blitterDmaStatsChartInsetRow;
    e9ui_stack_addFixed(rightColumn, blitterDmaStatsChartInsetRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(4));

    e9ui_component_t *cpuDmaStatsChart = custom_ui_cpuDmaStatsChartMake();
    if (!cpuDmaStatsChart) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->cpuDmaStatsChart = cpuDmaStatsChart;
    e9ui_component_t *cpuDmaStatsChartInsetRow = custom_ui_insetRowMake(cpuDmaStatsChart, 16, 0);
    if (!cpuDmaStatsChartInsetRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->cpuDmaStatsChartRow = cpuDmaStatsChartInsetRow;
    e9ui_stack_addFixed(rightColumn, cpuDmaStatsChartInsetRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(4));

    e9ui_component_t *bitplaneDmaStatsChart = custom_ui_bitplaneDmaStatsChartMake();
    if (!bitplaneDmaStatsChart) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->bitplaneDmaStatsChart = bitplaneDmaStatsChart;
    e9ui_component_t *bitplaneDmaStatsChartInsetRow = custom_ui_insetRowMake(bitplaneDmaStatsChart, 16, 0);
    if (!bitplaneDmaStatsChartInsetRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->bitplaneDmaStatsChartRow = bitplaneDmaStatsChartInsetRow;
    e9ui_stack_addFixed(rightColumn, bitplaneDmaStatsChartInsetRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(4));

    e9ui_component_t *spriteDmaStatsChart = custom_ui_spriteDmaStatsChartMake();
    if (!spriteDmaStatsChart) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->spriteDmaStatsChart = spriteDmaStatsChart;
    e9ui_component_t *spriteDmaStatsChartInsetRow = custom_ui_insetRowMake(spriteDmaStatsChart, 16, 0);
    if (!spriteDmaStatsChartInsetRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->spriteDmaStatsChartRow = spriteDmaStatsChartInsetRow;
    e9ui_stack_addFixed(rightColumn, spriteDmaStatsChartInsetRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(4));

    e9ui_component_t *diskDmaStatsChart = custom_ui_diskDmaStatsChartMake();
    if (!diskDmaStatsChart) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->diskDmaStatsChart = diskDmaStatsChart;
    e9ui_component_t *diskDmaStatsChartInsetRow = custom_ui_insetRowMake(diskDmaStatsChart, 16, 0);
    if (!diskDmaStatsChartInsetRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->diskDmaStatsChartRow = diskDmaStatsChartInsetRow;
    e9ui_stack_addFixed(rightColumn, diskDmaStatsChartInsetRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(4));

    e9ui_component_t *audioDmaStatsChart = custom_ui_audioDmaStatsChartMake();
    if (!audioDmaStatsChart) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->audioDmaStatsChart = audioDmaStatsChart;
    e9ui_component_t *audioDmaStatsChartInsetRow = custom_ui_insetRowMake(audioDmaStatsChart, 16, 0);
    if (!audioDmaStatsChartInsetRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->audioDmaStatsChartRow = audioDmaStatsChartInsetRow;
    e9ui_stack_addFixed(rightColumn, audioDmaStatsChartInsetRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(4));

    e9ui_component_t *otherDmaStatsChart = custom_ui_otherDmaStatsChartMake();
    if (!otherDmaStatsChart) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->otherDmaStatsChart = otherDmaStatsChart;
    e9ui_component_t *otherDmaStatsChartInsetRow = custom_ui_insetRowMake(otherDmaStatsChart, 16, 0);
    if (!otherDmaStatsChartInsetRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->otherDmaStatsChartRow = otherDmaStatsChartInsetRow;
    e9ui_stack_addFixed(rightColumn, otherDmaStatsChartInsetRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(4));

    e9ui_component_t *idleDmaStatsChart = custom_ui_idleDmaStatsChartMake();
    if (!idleDmaStatsChart) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->idleDmaStatsChart = idleDmaStatsChart;
    e9ui_component_t *idleDmaStatsChartInsetRow = custom_ui_insetRowMake(idleDmaStatsChart, 16, 0);
    if (!idleDmaStatsChartInsetRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->idleDmaStatsChartRow = idleDmaStatsChartInsetRow;
    e9ui_stack_addFixed(rightColumn, idleDmaStatsChartInsetRow);
    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(4));

    e9ui_component_t *dmaTotalMixChart = custom_ui_dmaTotalMixChartMake();
    if (!dmaTotalMixChart) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->dmaTotalMixChart = dmaTotalMixChart;
    e9ui_component_t *dmaTotalMixChartInsetRow = custom_ui_insetRowMake(dmaTotalMixChart, 16, 0);
    if (!dmaTotalMixChartInsetRow) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    ui->dmaTotalMixChartRow = dmaTotalMixChartInsetRow;
    e9ui_stack_addFixed(rightColumn, dmaTotalMixChartInsetRow);

    e9ui_stack_addFixed(rightColumn, e9ui_vspacer_make(8));
    custom_ui_syncDmaStatsSuboptions(ui);

    e9ui_hstack_addFlex(columns, leftColumn);
    e9ui_hstack_addFixed(columns, e9ui_spacer_make(16), 16);
    e9ui_hstack_addFlex(columns, rightColumn);
    e9ui_stack_addFlex(rootStack, columns);

    e9ui_component_t *scrollRoot = e9ui_scroll_make(rootStack);
    if (!scrollRoot) {
        e9ui_childDestroy(rootStack, &ui->ctx);
        return NULL;
    }
    return scrollRoot;
}

static void
custom_ui_syncEstimateFpsDisplay(custom_ui_state_t *ui)
{
    if (!ui) {
        return;
    }
    if (ui->estimateFpsTextRow) {
        e9ui_setHidden(ui->estimateFpsTextRow, ui->estimateFpsEnabled ? 0 : 1);
    }
    if (!ui->estimateFpsText) {
        return;
    }
    if (!ui->estimateFpsEnabled) {
        e9ui_text_setText(ui->estimateFpsText, "--");
        return;
    }
    double estimatedFps = libretro_host_getEstimatedVideoFps();
    if (estimatedFps > 0.0) {
        char text[32];
        snprintf(text, sizeof(text), "%.1f", estimatedFps);
        e9ui_text_setText(ui->estimateFpsText, text);
        return;
    }
    e9ui_text_setText(ui->estimateFpsText, "...");
}

static void
custom_ui_prepareFrame(custom_ui_state_t *ui, const e9ui_context_t *frameCtx)
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
    custom_ui_syncBlitterDebugCheckbox(ui);
#if E9K_HACK_AMI_SPRITE_VIS
    custom_ui_syncSpriteVisCheckbox(ui);
#endif
    custom_ui_syncCopperVisualiserCheckbox(ui);
    custom_ui_tickBlitterVisDecayTextbox(ui);
    custom_ui_tickCopperLimitTextboxes(ui);
    custom_ui_tickBplptrLineLimitTextboxes(ui);
    custom_ui_updateStatsCharts(ui);
    custom_ui_syncDmaStatsCycleExactHint(ui);
    custom_ui_syncEstimateFpsDisplay(ui);
}

static int
custom_ui_overlayBodyPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)ctx;
    (void)availW;
    return 0;
}

static void
custom_ui_overlayBodyLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self) {
        return;
    }
    self->bounds = bounds;
    custom_ui_overlay_body_state_t *st = (custom_ui_overlay_body_state_t *)self->state;
    if (!st || !st->ui || !st->ui->root || !st->ui->root->layout) {
        return;
    }
    st->ui->root->layout(st->ui->root, ctx, bounds);
}

static void
custom_ui_overlayBodyRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx) {
        return;
    }
    custom_ui_overlay_body_state_t *st = (custom_ui_overlay_body_state_t *)self->state;
    if (!st || !st->ui || !st->ui->root) {
        return;
    }
    custom_ui_prepareFrame(st->ui, ctx);
    if (st->ui->root->render) {
        st->ui->root->render(st->ui->root, ctx);
    }
}

static e9ui_component_t *
custom_ui_makeOverlayBodyHost(custom_ui_state_t *ui)
{
    if (!ui || !ui->root) {
        return NULL;
    }
    e9ui_component_t *host = (e9ui_component_t *)alloc_calloc(1, sizeof(*host));
    if (!host) {
        return NULL;
    }
    custom_ui_overlay_body_state_t *st = (custom_ui_overlay_body_state_t *)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(host);
        return NULL;
    }
    st->ui = ui;
    host->name = "custom_ui_overlay_body";
    host->state = st;
    host->preferredHeight = custom_ui_overlayBodyPreferredHeight;
    host->layout = custom_ui_overlayBodyLayout;
    host->render = custom_ui_overlayBodyRender;
    e9ui_child_add(host, ui->root, alloc_strdup("custom_ui_root"));
    return host;
}

static void
custom_ui_overlayWindowCloseRequested(e9ui_window_t *window, void *user)
{
    (void)window;
    custom_ui_state_t *ui = (custom_ui_state_t *)user;
    if (!ui) {
        return;
    }
    custom_ui_shutdown();
}

int
custom_ui_init(void)
{
    custom_ui_state_t *ui = &custom_ui_state;
    if (ui->open) {
        return 1;
    }

    ui->windowHost = e9ui_windowCreate(custom_ui_windowBackend());
    if (!ui->windowHost) {
        return 0;
    }
    ui->warnedMissingOption = 0;
    ui->suppressBlitterDebugCallbacks = 0;
#if E9K_HACK_AMI_SPRITE_VIS
    ui->suppressSpriteVisCallbacks = 0;
#endif
    ui->suppressBlitterVisModeCallbacks = 0;
    ui->suppressSpriteCallbacks = 0;
    ui->suppressBitplaneCallbacks = 0;
    ui->suppressAudioCallbacks = 0;
    ui->suppressBplptrBlockCallbacks = 0;
    ui->spritesCheckbox = NULL;
    ui->bitplanesCheckbox = NULL;
    ui->bplptrBlockAllCheckbox = NULL;
    ui->audiosCheckbox = NULL;
    ui->copperVisualiserCheckbox = NULL;
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
#if E9K_HACK_AMI_SPRITE_VIS
    ui->spriteVisCheckbox = NULL;
#endif
    ui->blitterVisPatternCheckbox = NULL;
    ui->blitterVisModeCheckbox = NULL;
    ui->blitterVisCollectCheckbox = NULL;
    ui->blitterVisBlinkCheckbox = NULL;
    ui->blitterVisDecayRow = NULL;
    ui->blitterVisDecayTextbox = NULL;
    ui->blitterVisDecaySeekRow = NULL;
    ui->blitterVisDecaySeekBar = NULL;
    ui->dmaStatsCheckbox = NULL;
    ui->estimateFpsCheckbox = NULL;
    ui->estimateFpsText = NULL;
    ui->estimateFpsTextRow = NULL;
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
    for (int spriteIndex = 0; spriteIndex < CUSTOM_UI_AMIGA_SPRITE_COUNT; ++spriteIndex) {
        ui->spriteCheckboxes[spriteIndex] = NULL;
    }
    for (int bitplaneIndex = 0; bitplaneIndex < CUSTOM_UI_AMIGA_BITPLANE_COUNT; ++bitplaneIndex) {
        ui->bitplaneCheckboxes[bitplaneIndex] = NULL;
    }
    for (int bplptrIndex = 0; bplptrIndex < CUSTOM_UI_AMIGA_BPLPTR_COUNT; ++bplptrIndex) {
        ui->bplptrBlockCheckboxes[bplptrIndex] = NULL;
    }
    for (int audioChannelIndex = 0; audioChannelIndex < CUSTOM_UI_AMIGA_AUDIO_CHANNEL_COUNT; ++audioChannelIndex) {
        ui->audioCheckboxes[audioChannelIndex] = NULL;
    }
    custom_ui_syncSpritesMasterCheckbox(ui);
    custom_ui_syncBitplanesMasterCheckbox(ui);
    custom_ui_syncBplptrBlockMasterCheckbox(ui);
    custom_ui_syncAudiosMasterCheckbox(ui);
    custom_ui_syncBlitterDebugCheckbox(ui);
#if E9K_HACK_AMI_SPRITE_VIS
    custom_ui_syncSpriteVisCheckbox(ui);
#endif
    libretro_host_setEstimateFpsEnabled(ui->estimateFpsEnabled);

    ui->root = custom_ui_buildRoot(ui);
    if (!ui->root) {
        custom_ui_shutdown();
        return 0;
    }
    {
        e9ui_rect_t rect = e9ui_windowResolveOpenRect(&e9ui->ctx,
                                                               custom_ui_windowDefaultRect(&e9ui->ctx),
                                                               420,
                                                               420,
                                                               1,
                                                               ui->winHasSaved ? 1 : 0,
                                                               (ui->winHasSaved && ui->winW > 0 && ui->winH > 0) ? 1 : 0,
                                                               ui->winX,
                                                               ui->winY,
                                                               ui->winW,
                                                               ui->winH);
        e9ui_component_t *overlayBodyHost = custom_ui_makeOverlayBodyHost(ui);
        if (!overlayBodyHost) {
            custom_ui_shutdown();
            return 0;
        }
        if (!e9ui_windowOpen(ui->windowHost,
                                     CUSTOM_UI_TITLE,
                                     rect,
                                     overlayBodyHost,
                                     custom_ui_overlayWindowCloseRequested,
                                     ui,
                                     &e9ui->ctx)) {
            e9ui_childDestroy(overlayBodyHost, &e9ui->ctx);
            custom_ui_shutdown();
            return 0;
        }
        ui->ctx = e9ui->ctx;
    }

    custom_ui_applyAllOptions();
    if (ui->dmaStatsEnabled) {
        custom_ui_enableDmaDebugForCopperStats(ui);
    }
    ui->open = 1;
    return 1;
}

void
custom_ui_shutdown(void)
{
    custom_ui_state_t *ui = &custom_ui_state;
    custom_ui_restoreDmaDebugForCopperStats(ui);
    if (!ui->open) {
        return;
    }
    if (ui->windowHost) {
        e9ui_windowDestroy(ui->windowHost);
        ui->windowHost = NULL;
    }
    ui->root = NULL;
    ui->fullscreen = NULL;
    ui->open = 0;
    ui->warnedMissingOption = 0;
    ui->pendingRemove = NULL;
    memset(&ui->ctx, 0, sizeof(ui->ctx));
}

void
custom_ui_toggle(void)
{
    if (custom_ui_isOpen()) {
        custom_ui_shutdown();
        return;
    }
    (void)custom_ui_init();
}

int
custom_ui_isOpen(void)
{
    return custom_ui_state.open ? 1 : 0;
}

uint32_t
custom_ui_getWindowId(void)
{
    return 0;
}

void
custom_ui_setMainWindowFocused(int focused)
{
    (void)focused;
}

int
custom_ui_getBlitterVisDecay(void)
{
    return custom_ui_state.blitterVisDecay;
}

int
custom_ui_getEstimateFpsEnabled(void)
{
    return custom_ui_state.estimateFpsEnabled ? 1 : 0;
}

int
custom_ui_getCopperLimitEnabled(void)
{
    return custom_ui_state.copperLimitEnabled ? 1 : 0;
}

int
custom_ui_getCopperLimitRange(int *outStart, int *outEnd)
{
    custom_ui_state_t *ui = &custom_ui_state;
    if (outStart) {
        *outStart = ui->copperLimitStart;
    }
    if (outEnd) {
        *outEnd = ui->copperLimitEnd;
    }
    return 1;
}

void
custom_ui_setCopperLimitRange(int start, int end)
{
    custom_ui_state_t *ui = &custom_ui_state;
    int nextStart = custom_ui_clampCopperLine(start);
    int nextEnd = custom_ui_clampCopperLine(end);
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
    custom_ui_applyCopperLimitStartOption();
    custom_ui_applyCopperLimitEndOption();
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
custom_ui_getBplptrBlockEnabled(void)
{
    custom_ui_state_t *ui = &custom_ui_state;
    for (int bplptrIndex = 0; bplptrIndex < CUSTOM_UI_AMIGA_BPLPTR_COUNT; ++bplptrIndex) {
        if (ui->bplptrBlockEnabled[bplptrIndex]) {
            return 1;
        }
    }
    return ui->bplptrBlockAllEnabled ? 1 : 0;
}

int
custom_ui_getBplptrLineLimitRange(int *outStart, int *outEnd)
{
    custom_ui_state_t *ui = &custom_ui_state;
    if (outStart) {
        *outStart = ui->bplptrLineLimitStart;
    }
    if (outEnd) {
        *outEnd = ui->bplptrLineLimitEnd;
    }
    return 1;
}

void
custom_ui_setBplptrLineLimitRange(int start, int end)
{
    custom_ui_state_t *ui = &custom_ui_state;
    int nextStart = custom_ui_clampCopperLine(start);
    int nextEnd = custom_ui_clampCopperLine(end);
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
    custom_ui_applyBplptrLineLimitStartOption();
    custom_ui_applyBplptrLineLimitEndOption();
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
custom_ui_handleEvent(SDL_Event *ev)
{
    (void)ev;
}

void
custom_ui_render(void)
{
    custom_ui_state_t *ui = &custom_ui_state;
    if (!ui->open || !ui->root) {
        return;
    }
    if (e9ui_windowCaptureRectChanged(ui->windowHost,
                                      (e9ui ? &e9ui->ctx : &ui->ctx),
                                      &ui->winHasSaved,
                                      &ui->winX,
                                      &ui->winY,
                                      &ui->winW,
                                      &ui->winH)) {
        config_saveConfig();
    }
}

void
custom_ui_persistConfig(FILE *file)
{
    custom_ui_state_t *ui = &custom_ui_state;
    if (!file) {
        return;
    }
    if (ui->open) {
        (void)e9ui_windowCaptureRectSnapshot(ui->windowHost,
                                                (e9ui ? &e9ui->ctx : &ui->ctx),
                                                &ui->winHasSaved,
                                                &ui->winX,
                                                &ui->winY,
                                                &ui->winW,
                                                &ui->winH);
    }
    if (ui->winHasSaved) {
        fprintf(file, "comp.custom_ui.win_x=%d\n", ui->winX);
        fprintf(file, "comp.custom_ui.win_y=%d\n", ui->winY);
        fprintf(file, "comp.custom_ui.win_w=%d\n", ui->winW);
        fprintf(file, "comp.custom_ui.win_h=%d\n", ui->winH);
    }
    fprintf(file, "comp.custom_ui.estimate_fps=%d\n", ui->estimateFpsEnabled ? 1 : 0);
}

int
custom_ui_loadConfigProperty(const char *prop, const char *value)
{
    custom_ui_state_t *ui = &custom_ui_state;
    if (!prop || !value) {
        return 0;
    }
    int intValue = 0;
    if (strcmp(prop, "win_x") == 0) {
        if (!custom_ui_parseInt(value, &intValue)) {
            return 0;
        }
        ui->winX = intValue;
    } else if (strcmp(prop, "win_y") == 0) {
        if (!custom_ui_parseInt(value, &intValue)) {
            return 0;
        }
        ui->winY = intValue;
    } else if (strcmp(prop, "win_w") == 0) {
        if (!custom_ui_parseInt(value, &intValue)) {
            return 0;
        }
        ui->winW = intValue;
    } else if (strcmp(prop, "win_h") == 0) {
        if (!custom_ui_parseInt(value, &intValue)) {
            return 0;
        }
        ui->winH = intValue;
    } else if (strcmp(prop, "estimate_fps") == 0) {
        if (!custom_ui_parseInt(value, &intValue)) {
            return 0;
        }
        ui->estimateFpsEnabled = intValue ? 1 : 0;
        return 1;
    } else {
        return 0;
    }
    ui->winHasSaved = 1;
    return 1;
}
