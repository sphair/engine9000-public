/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <SDL.h>
#include <SDL_ttf.h>

#include "e9ui.h"
#include "e9ui_scrollbar.h"
#include "e9ui_step_buttons.h"
#include "source_pane.h"

typedef struct source_pane_line_metrics {
    int maxLines;
    int lineHeight;
    int innerHeight;
} source_pane_line_metrics_t;

typedef enum source_pane_inline_edit_kind {
    source_pane_inline_edit_none = 0,
    source_pane_inline_edit_hex_bytes,
    source_pane_inline_edit_cpr_words,
    source_pane_inline_edit_cpr_reg,
    source_pane_inline_edit_cpr_value,
} source_pane_inline_edit_kind_t;

typedef struct source_pane_state {
    source_pane_mode_t viewMode;
    int scrollLine;
    int scrollIndex;
    int scrollLocked;
    uint64_t scrollAnchorAddr;
    int scrollAnchorValid;
    uint64_t lastPcAddr;
    uint64_t lastResolvedPc;
    uint64_t overrideAddr;
    int overrideActive;
    int frozenActive;
    uint64_t frozenPcAddr;
    uint64_t frozenAsmAnchorAddr;
    int frozenAsmStartIndex;
    int frozenAsmMaxLines;
    char **frozenAsmLines;
    uint64_t *frozenAsmAddrs;
    int frozenAsmCount;
    char curSrcPath[PATH_MAX];
    int curSrcLine;
    char *toggleBtnMeta;
    char *lockBtnMeta;
    int gutterPending;
    int gutterLine;
    uint32_t gutterAddr;
    int gutterDownX;
    int gutterDownY;
    source_pane_mode_t gutterMode;
    e9ui_scrollbar_state_t cScrollbar;
    int cScrollX;
    int cContentPixelWidth;
    int bucketSource;
    int bucketAddr;
    char *fileSelectMeta;
    char *searchBoxMeta;
    char *asmSymbolSelectMeta;
    char *asmAddressMeta;
    char *manualSrcPath;
    int manualSrcActive;
    char **sourceFiles;
    char **sourceLabels;
    e9ui_textbox_option_t *sourceOptions;
    int sourceFileCount;
    int sourceFileCap;
    int sourceFilesLoaded;
    char sourceFilesElf[PATH_MAX];
    char sourceFilesToolchain[PATH_MAX];
    char *functionSelectMeta;
    char **sourceFunctionNames;
    char **sourceFunctionFiles;
    char **sourceFunctionLabels;
    char **sourceFunctionValues;
    int *sourceFunctionLines;
    e9ui_textbox_option_t *sourceFunctionOptions;
    int sourceFunctionCount;
    int sourceFunctionCap;
    int sourceFunctionsLoaded;
    char sourceFunctionsElf[PATH_MAX];
    char sourceFunctionsToolchain[PATH_MAX];
    char sourceFunctionsFile[PATH_MAX];
    char **asmSymbolNames;
    char **asmSymbolLabels;
    char **asmSymbolValues;
    uint64_t *asmSymbolAddrs;
    e9ui_textbox_option_t *asmSymbolOptions;
    int asmSymbolCount;
    int asmSymbolCap;
    int asmSymbolsLoaded;
    char asmSymbolsElf[PATH_MAX];
    char asmSymbolsToolchain[PATH_MAX];
    int functionScrollLock;
    int searchActive;
    int searchMatchValid;
    int searchMatchLine;
    int searchMatchColumn;
    int searchMatchLength;
    e9ui_component_t *ownerPane;
    char *inlineEditMeta;
    int inlineEditActive;
    source_pane_inline_edit_kind_t inlineEditKind;
    source_pane_mode_t inlineEditMode;
    uint32_t inlineEditAddr;
    int inlineEditByteCount;
    uint16_t inlineEditWord1;
    uint16_t inlineEditWord2;
    SDL_Rect inlineEditRect;
    e9ui_textbox_option_t *cprRegisterOptions;
    int cprRegisterOptionCount;
    char hoverExpr[256];
    char hoverTip[1024];
    int hoverLine;
    int hoverCol;
    uint32_t hoverPc;
    uint32_t hoverTick;
    int hoverActive;
    e9ui_step_buttons_state_t asmStepButtons;
} source_pane_state_t;

void
source_pane_freeFrozenAsm(source_pane_state_t *st);

source_pane_line_metrics_t
source_pane_computeLineMetrics(e9ui_component_t *self, e9ui_context_t *ctx, TTF_Font *font, int padPx);

TTF_Font *
source_pane_resolveFont(const e9ui_context_t *ctx);

SDL_Rect
source_pane_getContentArea(e9ui_component_t *self, e9ui_context_t *ctx, int padPx);

int
source_pane_shouldFreezeAsmWhileRunning(const source_pane_state_t *st);

void
source_pane_renderStatusMessage(e9ui_context_t *ctx, TTF_Font *font, SDL_Rect area, int padPx,
                                SDL_Color color, const char *msg);

int
source_pane_measureSegment(TTF_Font *font, const char *line, int start, int len);

void
source_pane_renderAsmLineHighlighted(e9ui_context_t *ctx, e9ui_component_t *owner, TTF_Font *font,
                                     const char *line, int highlightOffset, SDL_Color baseColor, int textX, int y,
                                     int lineHeight, int hitW, void *sourceBucket);

int
source_pane_findCurrentAddrRow(const uint64_t *addrs, int count, uint64_t curAddr);

int
source_pane_parseInlineHexWord(const char *text, uint16_t *outWord);

void
source_pane_inlineEditCancel(source_pane_state_t *st, e9ui_context_t *ctx);

void
source_pane_inlineEditRefreshAfterWrite(source_pane_state_t *st);

int
source_pane_beginInlineEdit(source_pane_state_t *st, e9ui_context_t *ctx, source_pane_mode_t mode,
                            source_pane_inline_edit_kind_t kind, uint32_t addr, int byteCount,
                            uint16_t word1, uint16_t word2, const char *text, SDL_Rect rect);
