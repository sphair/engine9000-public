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

#include "e9ui_scrollbar.h"
#include "e9ui_step_buttons.h"
#include "machine.h"
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

typedef struct source_pane_symbols_cache {
    char **sourceFiles;
    char **sourceLabels;
    e9ui_textbox_option_t *sourceOptions;
    int sourceFileCount;
    int sourceFileCap;
    int sourceFilesLoaded;
    char sourceFilesElf[PATH_MAX];
    char sourceFilesToolchain[PATH_MAX];
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
    uint64_t asmSymbolsTextMapRevision;
    char asmSymbolsElf[PATH_MAX];
    char asmSymbolsToolchain[PATH_MAX];
} source_pane_symbols_cache_t;

typedef struct source_pane_state {
    source_pane_mode_t viewMode;
    int scrollLine;
    int scrollIndex;
    int scrollLocked;
    uint64_t scrollAnchorAddr;
    int scrollAnchorValid;
    uint64_t lastPcAddr;
    uint64_t lastResolvedPc;
    source_pane_mode_t lastResolvedMode;
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
    int modeOptionsKind;
    int gutterPending;
    int gutterLine;
    uint32_t gutterAddr;
    int gutterDownX;
    int gutterDownY;
    source_pane_mode_t gutterMode;
    int inlineEditPending;
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
    char *functionSelectMeta;
    source_pane_symbols_cache_t primarySymbols;
    source_pane_symbols_cache_t z80Symbols;
    source_pane_symbols_cache_t *activeSymbols;
    int functionScrollLock;
    int searchActive;
    int searchMatchValid;
    int searchMatchLine;
    int searchMatchColumn;
    int searchMatchLength;
    e9ui_component_t *ownerPane;
    char *inlineEditMeta;
    char *inlineDataEditMeta;
    int inlineEditActive;
    int inlineEditAutoResume;
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

#define sourceFiles activeSymbols->sourceFiles
#define sourceLabels activeSymbols->sourceLabels
#define sourceOptions activeSymbols->sourceOptions
#define sourceFileCount activeSymbols->sourceFileCount
#define sourceFileCap activeSymbols->sourceFileCap
#define sourceFilesLoaded activeSymbols->sourceFilesLoaded
#define sourceFilesElf activeSymbols->sourceFilesElf
#define sourceFilesToolchain activeSymbols->sourceFilesToolchain
#define sourceFunctionNames activeSymbols->sourceFunctionNames
#define sourceFunctionFiles activeSymbols->sourceFunctionFiles
#define sourceFunctionLabels activeSymbols->sourceFunctionLabels
#define sourceFunctionValues activeSymbols->sourceFunctionValues
#define sourceFunctionLines activeSymbols->sourceFunctionLines
#define sourceFunctionOptions activeSymbols->sourceFunctionOptions
#define sourceFunctionCount activeSymbols->sourceFunctionCount
#define sourceFunctionCap activeSymbols->sourceFunctionCap
#define sourceFunctionsLoaded activeSymbols->sourceFunctionsLoaded
#define sourceFunctionsElf activeSymbols->sourceFunctionsElf
#define sourceFunctionsToolchain activeSymbols->sourceFunctionsToolchain
#define sourceFunctionsFile activeSymbols->sourceFunctionsFile
#define asmSymbolNames activeSymbols->asmSymbolNames
#define asmSymbolLabels activeSymbols->asmSymbolLabels
#define asmSymbolValues activeSymbols->asmSymbolValues
#define asmSymbolAddrs activeSymbols->asmSymbolAddrs
#define asmSymbolOptions activeSymbols->asmSymbolOptions
#define asmSymbolCount activeSymbols->asmSymbolCount
#define asmSymbolCap activeSymbols->asmSymbolCap
#define asmSymbolsLoaded activeSymbols->asmSymbolsLoaded
#define asmSymbolsTextMapRevision activeSymbols->asmSymbolsTextMapRevision
#define asmSymbolsElf activeSymbols->asmSymbolsElf
#define asmSymbolsToolchain activeSymbols->asmSymbolsToolchain

void
source_pane_freeFrozenAsm(source_pane_state_t *st);

source_pane_line_metrics_t
source_pane_computeLineMetrics(e9ui_component_t *self, e9ui_context_t *ctx, TTF_Font *font, int padPx);

TTF_Font *
source_pane_resolveFont(const e9ui_context_t *ctx);

SDL_Rect
source_pane_getContentArea(e9ui_component_t *self, e9ui_context_t *ctx, int padPx);

void
source_pane_trackPosition(source_pane_state_t *st);

void
source_pane_source_view_updateSourceLocation(source_pane_state_t *st, int allowWhileRunning);

void
source_pane_syncLockButtonVisual(source_pane_state_t *st);

int
source_pane_shouldFreezeAsmWhileRunning(const source_pane_state_t *st);

int
source_pane_asm_view_isAsmLikeMode(source_pane_mode_t mode);

int
source_pane_asm_view_isCpuAsmLikeMode(source_pane_mode_t mode);

int
source_pane_asm_view_shouldFreezeWhileRunning(const source_pane_state_t *st);

int
source_pane_asm_view_areStepButtonsEnabled(const source_pane_state_t *st);

int
source_pane_asm_view_beginGutterPress(e9ui_component_t *self, e9ui_context_t *ctx,
                                      source_pane_state_t *st, source_pane_mode_t mode,
                                      int mx, int my);

void
source_pane_asm_view_renderAsm(e9ui_component_t *self, e9ui_context_t *ctx);

void
source_pane_asm_view_renderHex(e9ui_component_t *self, e9ui_context_t *ctx);

int
source_pane_asm_view_beginInlineHexEditAtPoint(e9ui_component_t *self, e9ui_context_t *ctx,
                                               source_pane_state_t *st, int mx, int my);

void
source_pane_asm_view_symbolSelectChanged(e9ui_context_t *ctx, e9ui_component_t *comp,
                                         const char *value, void *user);

void
source_pane_asm_view_addressSubmitted(e9ui_context_t *ctx, void *user);

int
source_pane_parseHex(const char *s, uint32_t *out);

int
source_pane_parseHex64(const char *s, uint64_t *out);

int
source_pane_fileMatches(const char *a, const char *b);

void
source_pane_resolveSourcePath(const char *path, char *out, size_t out_cap);

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
                            uint16_t word1, uint16_t word2, const char *text, SDL_Rect rect,
                            int initialCursor);

int
source_pane_dataEditCursorForPoint(TTF_Font *font, const char *text, e9ui_data_edit_mode_t mode,
                                   int textX, int mx);

int
source_pane_source_view_beginSourceGutterPress(e9ui_component_t *self, e9ui_context_t *ctx,
                                               source_pane_state_t *st, int mx, int my);

int
source_pane_source_view_handleCScrollEvent(e9ui_component_t *self, e9ui_context_t *ctx,
                                           source_pane_state_t *st, const e9ui_event_t *ev);

void
source_pane_source_view_clearHover(e9ui_component_t *self, source_pane_state_t *st);

void
source_pane_source_view_updateHoverTooltip(e9ui_component_t *self, e9ui_context_t *ctx,
                                           source_pane_state_t *st, const e9ui_event_t *ev);

void
source_pane_source_view_render(e9ui_component_t *self, e9ui_context_t *ctx);

void
source_pane_symbols_clearSourceFiles(source_pane_state_t *st);

void
source_pane_symbols_clearSourceFunctions(source_pane_state_t *st);

void
source_pane_symbols_clearAsmSymbols(source_pane_state_t *st);

void
source_pane_symbols_clearAllCaches(source_pane_state_t *st);

void
source_pane_symbols_syncFileSelect(e9ui_component_t *comp, source_pane_state_t *st);

void
source_pane_symbols_syncFunctionSelect(e9ui_component_t *comp, source_pane_state_t *st);

void
source_pane_symbols_trackCurrentFunction(e9ui_component_t *comp, source_pane_state_t *st,
                                         const char *path, int line);

void
source_pane_symbols_refreshAsmSymbols(e9ui_component_t *comp, source_pane_state_t *st);

void
source_pane_symbols_refreshSourceFunctions(e9ui_component_t *comp, source_pane_state_t *st,
                                           const char *source_file);

void
source_pane_symbols_refreshSourceFiles(e9ui_component_t *comp, source_pane_state_t *st);

int
source_pane_fileline_resolveFileLine(const char *elf, const char *file, int line_no, uint32_t *out_addr);

machine_breakpoint_t *
source_pane_fileline_findBreakpointForLine(const char *path, int line,
                                           const machine_breakpoint_t *bps, int count);

int
source_pane_fileline_removeBreakpointsForLine(const char *path, int line,
                                              const machine_breakpoint_t *bps, int count);

int
source_pane_fileline_addBreakpointsForLine(const char *path, int lineNo);
