/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <strings.h>

#include "breakpoints.h"
#include "e9ui.h"
#include "machine.h"
#include "libretro_host.h"
#include "debugger.h"
#include "addr2line.h"
#include "e9ui_scroll.h"
#include "hotkeys.h"
#include "symbol_text_map.h"
#include "strutil.h"

typedef struct breakpoints_record {
    machine_breakpoint_t data;
    int                 present;
} breakpoints_record_t;

typedef struct breakpoints_entry_state {
    char               primary[768];
    char               prefix[64];
    char               filePath[PATH_MAX];
    char               suffix[512];
    char               overflowTooltip[PATH_MAX];
    int                hasFilePath;
    char               condition[768];
    int                hasCondition;
    e9ui_component_t  *checkbox;   // child component (owned via e9ui_addChild)
    breakpoints_record_t *record;  // non-owning pointer into list state's array
    struct breakpoints_list_state *list; // non-owning pointer (owner is list component)
} breakpoints_entry_state_t;


struct breakpoints_list_state {
    e9ui_component_t *entries;              // child stack (owned via e9ui_addChild)
    breakpoints_record_t *records;          // owned by list state
    int                  record_count;
    int                  record_cap;
    const machine_breakpoint_t *last_bps;   // non-owning snapshot pointer
    int                        last_count;
};

static e9ui_component_t *breakpoints_btnAddCurrent = NULL;
static char breakpoints_tipAddCurrent[96];

static void breakpoints_listMarkDirty(breakpoints_list_state_t *st);
static void breakpoints_listRefreshAndMarkDirty(breakpoints_list_state_t *st);
static void breakpoints_componentDtor(e9ui_component_t *self, e9ui_context_t *ctx);

static int
breakpoints_readActiveProcessorPc(uint32_t processorId, uint32_t *outAddr)
{
    if (outAddr) {
        *outAddr = 0;
    }
    if (!outAddr) {
        return 0;
    }
    if (processorId == MACHINE_PROCESSOR_PRIMARY) {
        unsigned long pc = 0;
        if (!machine_findReg(&debugger.machine, "PC", &pc)) {
            return 0;
        }
        *outAddr = (uint32_t)pc & 0x00ffffffu;
        return 1;
    }

    e9k_debug_processor_reg_t regs[32];
    size_t count = 0;
    if (!libretro_host_debugReadProcessorRegs(processorId, regs, countof(regs), &count)) {
        return 0;
    }
    if (count > countof(regs)) {
        count = countof(regs);
    }
    for (size_t i = 0; i < count; ++i) {
        if (strcasecmp(regs[i].name, "PC") == 0) {
            uint64_t mask = regs[i].bits >= 32 ? 0xffffffffull : ((1ull << regs[i].bits) - 1ull);
            *outAddr = (uint32_t)(regs[i].value & mask);
            return 1;
        }
    }
    return 0;
}

static void
breakpoints_formatProcessorLabel(uint32_t processorId, char *out, size_t cap)
{
    e9k_debug_processor_info_t processors[8];
    size_t count = 0;

    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (libretro_host_debugReadProcessors(processors,
                                          sizeof(processors) / sizeof(processors[0]),
                                          &count)) {
        for (size_t i = 0; i < count && i < sizeof(processors) / sizeof(processors[0]); ++i) {
            if (processors[i].id == processorId) {
                if (strcmp(processors[i].name, "M68000") == 0) {
                    snprintf(out, cap, "68K");
                } else if (processors[i].name[0]) {
                    snprintf(out, cap, "%s", processors[i].name);
                } else {
                    snprintf(out, cap, "CPU %u", (unsigned)processorId);
                }
                out[cap - 1] = '\0';
                return;
            }
        }
    }
    snprintf(out, cap, "CPU %u", (unsigned)processorId);
    out[cap - 1] = '\0';
}

static int
breakpoints_resolveTextMapFunction(uint32_t runtimeAddr, char *outFunction, size_t functionCap)
{
    if (!outFunction || functionCap == 0) {
        return 0;
    }
    outFunction[0] = '\0';
    if (!debugger.symbolValid || debugger.symbolFileKind != DEBUGGER_SYMBOL_FILE_KIND_TEXT_MAP) {
        return 0;
    }

    const symbol_text_map_entry_t *entry = NULL;
    if (!symbol_text_map_findNearest(debugger.libretro.exePath,
                                     runtimeAddr & 0x00ffffffu,
                                     SYMBOL_TEXT_MAP_SYMBOL_MASK_FUNCTION |
                                     SYMBOL_TEXT_MAP_SYMBOL_MASK_UNKNOWN,
                                     &entry)) {
        (void)symbol_text_map_findNearest(debugger.libretro.exePath,
                                          runtimeAddr & 0x00ffffffu,
                                          SYMBOL_TEXT_MAP_SYMBOL_MASK_ALL,
                                          &entry);
    }
    if (!entry || !entry->name || !entry->name[0]) {
        return 0;
    }

    strncpy(outFunction, entry->name, functionCap - 1);
    outFunction[functionCap - 1] = '\0';
    return 1;
}

void
breakpoints_refreshHotkeyTooltips(void)
{
    if (!breakpoints_btnAddCurrent) {
        return;
    }
    char binding[96];
    binding[0] = '\0';
    if (hotkeys_formatActionBindingDisplay("breakpoint_add_current", binding, sizeof(binding)) && binding[0]) {
        snprintf(breakpoints_tipAddCurrent, sizeof(breakpoints_tipAddCurrent), "Add Current - %s", binding);
    } else {
        snprintf(breakpoints_tipAddCurrent, sizeof(breakpoints_tipAddCurrent), "Add Current");
    }
    breakpoints_tipAddCurrent[sizeof(breakpoints_tipAddCurrent) - 1] = '\0';
    e9ui_setTooltip(breakpoints_btnAddCurrent, breakpoints_tipAddCurrent);
}

static void
breakpoints_setTooltipIfChanged(e9ui_component_t *comp, const char *tooltip)
{
    if (!comp) {
        return;
    }
    if (comp->tooltip == tooltip) {
        return;
    }
    e9ui_setTooltip(comp, tooltip);
}

static int
breakpoints_measureTextWidth(TTF_Font *font, const char *text)
{
    if (!font || !text || !text[0]) {
        return 0;
    }
    int textW = 0;
    if (TTF_SizeText(font, text, &textW, NULL) != 0) {
        return 0;
    }
    return textW;
}

static void
breakpoints_leftEllipsizeText(TTF_Font *font,
                              const char *text,
                              int maxWidth,
                              char *out,
                              size_t outCap,
                              int *outTruncated)
{
    if (outTruncated) {
        *outTruncated = 0;
    }
    if (!out || outCap == 0) {
        return;
    }
    out[0] = '\0';
    if (!text || !text[0]) {
        return;
    }
    if (!font || maxWidth <= 0) {
        return;
    }

    int fullWidth = breakpoints_measureTextWidth(font, text);
    if (fullWidth <= maxWidth) {
        snprintf(out, outCap, "%s", text);
        return;
    }

    const char *ellipsis = "...";
    int ellipsisWidth = breakpoints_measureTextWidth(font, ellipsis);
    if (ellipsisWidth >= maxWidth) {
        return;
    }

    size_t textLen = strlen(text);
    size_t lo = 0;
    size_t hi = textLen;
    size_t bestStart = textLen;
    while (lo <= hi) {
        size_t mid = lo + (hi - lo) / 2;
        int written = snprintf(out, outCap, "%s%s", ellipsis, text + mid);
        if (written < 0 || (size_t)written >= outCap) {
            lo = mid + 1;
            continue;
        }
        int width = breakpoints_measureTextWidth(font, out);
        if (width <= maxWidth) {
            bestStart = mid;
            if (mid == 0) {
                break;
            }
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }

    if (bestStart >= textLen) {
        out[0] = '\0';
        return;
    }

    snprintf(out, outCap, "%s%s", ellipsis, text + bestStart);
    if (outTruncated) {
        *outTruncated = 1;
    }
}

static void
breakpoints_formatPrimaryText(const breakpoints_entry_state_t *st,
                              TTF_Font *font,
                              int maxWidth,
                              char *out,
                              size_t outCap,
                              int *outTruncated)
{
    if (outTruncated) {
        *outTruncated = 0;
    }
    if (!out || outCap == 0 || !st) {
        return;
    }
    out[0] = '\0';

    if (!st->hasFilePath) {
        snprintf(out, outCap, "%s", st->primary);
        return;
    }

    int prefixWidth = breakpoints_measureTextWidth(font, st->prefix);
    int suffixWidth = breakpoints_measureTextWidth(font, st->suffix);
    int fileWidthBudget = maxWidth - prefixWidth - suffixWidth;
    if (fileWidthBudget < 0) {
        fileWidthBudget = 0;
    }

    char fileDisplay[PATH_MAX];
    int wasTruncated = 0;
    breakpoints_leftEllipsizeText(font,
                                  st->filePath,
                                  fileWidthBudget,
                                  fileDisplay,
                                  sizeof(fileDisplay),
                                  &wasTruncated);

    if (!fileDisplay[0]) {
        snprintf(out, outCap, "%s%s", st->prefix, st->suffix);
    } else {
        snprintf(out, outCap, "%s%s%s", st->prefix, fileDisplay, st->suffix);
    }

    if (outTruncated) {
        *outTruncated = wasTruncated;
    }
}

static breakpoints_list_state_t *breakpoints_listState = NULL;


void
breakpoints_resolveLocation(machine_breakpoint_t *bp)
{
    if (bp && bp->processorId != MACHINE_PROCESSOR_PRIMARY) {
        return;
    }
    if (!bp || (bp->file[0] && bp->line > 0 && bp->func[0])) {
        return;
    }
    const char *elf = debugger.libretro.exePath;
    if (!elf || !*elf || !debugger.symbolValid) {
        return;
    }
    if (debugger.symbolFileKind == DEBUGGER_SYMBOL_FILE_KIND_TEXT_MAP) {
        char functionName[512];
        if (breakpoints_resolveTextMapFunction((uint32_t)(bp->addr & 0x00ffffffu),
                                               functionName,
                                               sizeof(functionName))) {
            strncpy(bp->func, functionName, sizeof(bp->func) - 1);
            bp->func[sizeof(bp->func) - 1] = '\0';
        }
        return;
    }
    if (!addr2line_start(elf)) {
        return;
    }
    char path[PATH_MAX];
    int line = 0;
    char functionName[512];
    functionName[0] = '\0';
    if (addr2line_resolveDetailed((uint64_t)bp->addr,
                                  path,
                                  sizeof(path),
                                  &line,
                                  functionName,
                                  sizeof(functionName))) {
        if (path[0] && line > 0) {
            strncpy(bp->file, path, sizeof(bp->file) - 1);
            bp->file[sizeof(bp->file) - 1] = '\0';
            bp->line = line;
        }
        if (functionName[0] && strcmp(functionName, "??") != 0) {
            strncpy(bp->func, functionName, sizeof(bp->func) - 1);
            bp->func[sizeof(bp->func) - 1] = '\0';
        }
    }
}

static const char *
breakpoints_stripCliSrcPrefix(const char *path)
{
    if (!path || !path[0]) return path;

    const char *prefix = debugger.libretro.sourceDir;
    if (!prefix || !prefix[0]) return path;

    const size_t prefix_len = strlen(prefix);
    if (!prefix_len) return path;

    if (strncmp(path, prefix, prefix_len) != 0) return path;

    const char *stripped = path + prefix_len;
    if (*stripped == '/' || *stripped == '\\') ++stripped;
    return *stripped ? stripped : path;
}

static void
breakpoints_formatAddrAnnotation(const machine_breakpoint_t *bp, char *dst, size_t cap)
{
    if (!dst || cap == 0) {
        return;
    }
    dst[0] = '\0';
    if (!bp) {
        return;
    }
    const char *symbol = NULL;
    if (bp->func[0] && strcmp(bp->func, "??") != 0) {
        symbol = bp->func;
    }
    const char *addrText = bp->addr_text[0] ? bp->addr_text : NULL;
    if (symbol && addrText) {
        snprintf(dst, cap, " (%s, %s)", symbol, addrText);
        return;
    }
    if (symbol && bp->addr != 0) {
        snprintf(dst, cap, " (%s, 0x%llX)", symbol, (unsigned long long)bp->addr);
        return;
    }
    if (symbol) {
        snprintf(dst, cap, " (%s)", symbol);
        return;
    }
    if (addrText) {
        snprintf(dst, cap, " (%s)", addrText);
        return;
    }
    if (bp->addr != 0) {
        snprintf(dst, cap, " (0x%llX)", (unsigned long long)bp->addr);
        return;
    }
}

static void
breakpoints_formatLocation(const machine_breakpoint_t *bp, char *dst, size_t cap)
{
    if (!dst || cap == 0) return;
    dst[0] = '\0';

    if (!bp) {
        snprintf(dst, cap, "<unknown>");
        return;
    }
    if (bp->processorId != MACHINE_PROCESSOR_PRIMARY) {
        char processorLabel[32];
        const char *addrText = bp->addr_text[0] ? bp->addr_text : NULL;
        breakpoints_formatProcessorLabel(bp->processorId, processorLabel, sizeof(processorLabel));
        if (addrText) {
            snprintf(dst, cap, "%s %s", processorLabel, addrText);
        } else {
            snprintf(dst, cap, "%s 0x%04X", processorLabel, (unsigned)(bp->addr & 0xffffu));
        }
        return;
    }

    const char *file = breakpoints_stripCliSrcPrefix(bp->file);
    if (file && file[0] && bp->line > 0) {
        char addrAnnotation[192];
        breakpoints_formatAddrAnnotation(bp, addrAnnotation, sizeof(addrAnnotation));
        snprintf(dst, cap, "%s:%d%s", file, bp->line, addrAnnotation);
        return;
    }
    if (bp->func[0] && strcmp(bp->func, "??") != 0) {
        char addrAnnotation[192];
        breakpoints_formatAddrAnnotation(bp, addrAnnotation, sizeof(addrAnnotation));
        snprintf(dst, cap, "%s%s", bp->func, addrAnnotation);
        return;
    }
    if (bp->addr_text[0]) {
        snprintf(dst, cap, "%s", bp->addr_text);
        return;
    }
    if (bp->addr != 0) {
        snprintf(dst, cap, "0x%llX", (unsigned long long)bp->addr);
        return;
    }
    snprintf(dst, cap, "<unknown>");
}

static void
breakpoints_formatState(const machine_breakpoint_t *bp, char *dst, size_t cap)
{
    if (!dst || cap == 0) return;
    dst[0] = '\0';
    if (!bp) return;

    size_t pos = 0;
    if (!bp->enabled) {
        int written = snprintf(dst + pos, cap - pos, "disabled");
        if (written < 0) {
            return;
        }
        pos += (size_t)written;
    }

    if (bp->disp[0] && strcmp(bp->disp, "keep") != 0 && pos + 2 < cap) {
        int written = snprintf(dst + pos, cap - pos, "%s%s", pos ? ", " : "", bp->disp);
        if (written > 0) pos += (size_t)written;
    }
    if (bp->type[0] && strcmp(bp->type, "breakpoint") != 0 && pos + 2 < cap) {
        int written = snprintf(dst + pos, cap - pos, "%s%s", pos ? ", " : "", bp->type);
        if (written > 0) pos += (size_t)written;
    }
}


void
breakpoints_registerListState(breakpoints_list_state_t *state)
{
    breakpoints_listState = state;
}

void
breakpoints_unregisterListState(breakpoints_list_state_t *state)
{
    if (breakpoints_listState == state) {
        breakpoints_listState = NULL;
    }
}

void
breakpoints_markDirty(void)
{
    if (breakpoints_listState) {
        breakpoints_listRefreshAndMarkDirty(breakpoints_listState);
    }
}

static void
breakpoints_entryCheckboxCB(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;

    breakpoints_entry_state_t *st = (breakpoints_entry_state_t*)user;
    if (!st || !st->record) return;

    uint32_t processorId = st->record->data.processorId;
    uint32_t addr = processorId == MACHINE_PROCESSOR_PRIMARY
        ? (uint32_t)(st->record->data.addr & 0x00ffffffu)
        : (uint32_t)(st->record->data.addr & 0x0000ffffu);
    if (!machine_setBreakpointEnabled(&debugger.machine, st->record->data.number, selected ? 1 : 0, NULL)) {
        return;
    }
    if (selected) {
        libretro_host_debugAddProcessorBreakpoint(processorId, addr);
    } else {
        libretro_host_debugRemoveProcessorBreakpoint(processorId, addr);
    }
    if (st->list) {
        breakpoints_listRefreshAndMarkDirty(st->list);
    }
}

static int
breakpoints_entryPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;

    breakpoints_entry_state_t *st = (breakpoints_entry_state_t*)self->state;

    TTF_Font *font = e9ui->theme.text.source;
    if (!font && ctx) font = ctx->font;

    int lh = font ? TTF_FontHeight(font) : 16;
    if (lh <= 0) lh = 16;

    int lines = 1 + (st && st->hasCondition ? 1 : 0);

    int padY = e9ui_checkbox_getMargin(ctx);
    if (padY <= 0) padY = E9UI_THEME_CHECKBOX_MARGIN;

    return padY + lines * lh + padY;
}

static void
breakpoints_entryLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    self->bounds = bounds;

    breakpoints_entry_state_t *st = (breakpoints_entry_state_t*)self->state;
    if (!st || !st->checkbox) return;

    int padX = e9ui_checkbox_getMargin(ctx);
    int padY = e9ui_checkbox_getMargin(ctx);
    if (padY <= 0) padY = E9UI_THEME_CHECKBOX_MARGIN;

    const int cbSize = 18;
    int cbHeight = bounds.h - padY * 2;
    if (cbHeight < cbSize) cbHeight = cbSize;

    int cbGap = e9ui_checkbox_getTextGap(ctx);
    if (cbGap <= 0) cbGap = E9UI_THEME_CHECKBOX_TEXT_GAP;
    int cbLeft = e9ui_checkbox_getLeftMargin(st->checkbox, ctx);

    e9ui_rect_t cb_bounds = {
        bounds.x + padX,
        bounds.y + (bounds.h - cbHeight) / 2,
        cbSize + cbGap + cbLeft,
        cbHeight
    };

    if (st->checkbox->layout) {
        st->checkbox->layout(st->checkbox, ctx, cb_bounds);
    }
}

static void
breakpoints_entryRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx || !ctx->renderer) return;

    breakpoints_entry_state_t *st = (breakpoints_entry_state_t*)self->state;

    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    if (!font) return;

    if (st->checkbox && st->checkbox->render) {
        st->checkbox->render(st->checkbox, ctx);
    }

    const SDL_Color primary =
        e9ui_checkbox_isSelected(st->checkbox) ?
            (SDL_Color){200, 255, 200, 255} :
            (SDL_Color){220, 220, 220, 255};

    const SDL_Color meta = {180, 180, 210, 255};

    int padX = e9ui_checkbox_getMargin(ctx);
    if (padX <= 0) padX = 8;
    int cbLeft = e9ui_checkbox_getLeftMargin(st->checkbox, ctx);

    int padY = e9ui_checkbox_getMargin(ctx);
    if (padY <= 0) padY = E9UI_THEME_CHECKBOX_MARGIN;

    const int cbSize = 18;

    int cbGap = e9ui_checkbox_getTextGap(ctx);
    if (cbGap <= 0) cbGap = E9UI_THEME_CHECKBOX_TEXT_GAP;

    int lh = TTF_FontHeight(font);
    if (lh <= 0) lh = 16;

    int curY = self->bounds.y + padY;
    int textX = self->bounds.x + padX + cbLeft + cbSize + cbGap;
    int textRight = self->bounds.x + self->bounds.w - padX;
    int maxPrimaryWidth = textRight - textX;
    if (maxPrimaryWidth < 0) {
        maxPrimaryWidth = 0;
    }

    char primaryLine[768];
    int wasTruncated = 0;
    breakpoints_formatPrimaryText(st,
                                  font,
                                  maxPrimaryWidth,
                                  primaryLine,
                                  sizeof(primaryLine),
                                  &wasTruncated);
    int mouseOverCheckbox = 0;
    if (st->checkbox) {
        const e9ui_rect_t cb = st->checkbox->bounds;
        if (ctx->mouseX >= cb.x &&
            ctx->mouseX < (cb.x + cb.w) &&
            ctx->mouseY >= cb.y &&
            ctx->mouseY < (cb.y + cb.h)) {
            mouseOverCheckbox = 1;
        }
    }
    breakpoints_setTooltipIfChanged(self,
                                    (wasTruncated && !mouseOverCheckbox) ? st->overflowTooltip : NULL);

    int tw = 0;
    int th = 0;
    SDL_Texture *t = e9ui_text_cache_getText(ctx->renderer, font, primaryLine, primary, &tw, &th);
    if (t) {
        SDL_Rect tr = { textX, curY, tw, th };
        SDL_RenderCopy(ctx->renderer, t, NULL, &tr);
    }

    curY += lh;

    if (st->hasCondition) {
        int cw = 0, ch = 0;
        SDL_Texture *t = e9ui_text_cache_getText(ctx->renderer, font, st->condition, meta, &cw, &ch);
        if (t) {
            SDL_Rect tr = { textX + 12, curY, cw, ch };
            SDL_RenderCopy(ctx->renderer, t, NULL, &tr);
        }
    }
}


static e9ui_component_t *
breakpoints_entryMake(breakpoints_record_t *rec, breakpoints_list_state_t *list)
{
    if (!rec) return NULL;

    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    breakpoints_entry_state_t *st = (breakpoints_entry_state_t*)alloc_calloc(1, sizeof(*st));
    if (!c || !st) {
        alloc_free(c);
        alloc_free(st);
        return NULL;
    }

    c->name = "breakpoints_entry";
    c->state = st;

    const machine_breakpoint_t *bp = &rec->data;
    const char *file = breakpoints_stripCliSrcPrefix(bp->file);

    st->hasCondition = bp->cond[0] != '\0';
    st->record = rec;
    st->list = list;

    int checkbox_selected = bp->enabled ? 1 : 0;
    st->checkbox = e9ui_checkbox_make(NULL, checkbox_selected, breakpoints_entryCheckboxCB, st);
    e9ui_checkbox_setLeftMargin(st->checkbox, 8);
    //    e9ui_setDisableVariable(st->checkbox, machine_getRunningState(debugger.machine), 1);    
    if (!st->checkbox) {
        alloc_free(st);
        alloc_free(c);
        return NULL;
    }

    // ownership: entry owns checkbox as child
    e9ui_child_add(c, st->checkbox, 0);

    char location[512];
    char state[128];
    breakpoints_formatLocation(bp, location, sizeof(location));
    breakpoints_formatState(bp, state, sizeof(state));

    st->hasFilePath = 0;
    st->prefix[0] = '\0';
    st->filePath[0] = '\0';
    st->suffix[0] = '\0';
    st->overflowTooltip[0] = '\0';

    if (file && file[0] && bp->line > 0) {
        st->hasFilePath = 1;
        snprintf(st->prefix, sizeof(st->prefix), "#%d ", bp->number);
        snprintf(st->filePath, sizeof(st->filePath), "%s", file);
        snprintf(st->overflowTooltip, sizeof(st->overflowTooltip), "%s", file);

        char addrPart[192];
        breakpoints_formatAddrAnnotation(bp, addrPart, sizeof(addrPart));
        if (state[0]) {
            snprintf(st->suffix, sizeof(st->suffix), ":%d%s (%s)", bp->line, addrPart, state);
        } else {
            snprintf(st->suffix, sizeof(st->suffix), ":%d%s", bp->line, addrPart);
        }
    }

    if (st->hasFilePath) {
        strutil_join3Trunc(st->primary, sizeof(st->primary), st->prefix, st->filePath, st->suffix);
    } else if (state[0]) {
        snprintf(st->primary, sizeof(st->primary), "#%d %s (%s)", bp->number, location, state);
    } else {
        snprintf(st->primary, sizeof(st->primary), "#%d %s", bp->number, location);
    }
    if (st->hasCondition) {
        snprintf(st->condition, sizeof(st->condition), "  if %s", bp->cond);
    }

    c->preferredHeight = breakpoints_entryPreferredHeight;
    c->layout = breakpoints_entryLayout;
    c->render = breakpoints_entryRender;

    return c;
}

static int
breakpoints_emptyPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self; (void)availW;

    TTF_Font *font = e9ui->theme.text.source;
    if (!font && ctx) font = ctx->font;

    int lh = font ? TTF_FontHeight(font) : 16;
    if (lh <= 0) lh = 16;

    const int padY = 4;
    return padY + lh + padY;
}

static void
breakpoints_emptyLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static void
breakpoints_emptyRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) return;

    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    if (!font) return;

    const SDL_Color meta = {180, 180, 210, 255};
    const int padX = 8;
    const int padY = 4;

    int tw = 0, th = 0;
    SDL_Texture *t = e9ui_text_cache_getText(ctx->renderer, font, "No breakpoints", meta, &tw, &th);
    if (t) {
        SDL_Rect tr = { self->bounds.x + padX, self->bounds.y + padY, tw, th };
        SDL_RenderCopy(ctx->renderer, t, NULL, &tr);
    }
}

static e9ui_component_t *
breakpoints_emptyMake(void)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    if (!c) return NULL;

    c->name = "breakpoints_empty";
    c->preferredHeight = breakpoints_emptyPreferredHeight;
    c->layout = breakpoints_emptyLayout;
    c->render = breakpoints_emptyRender;
    return c;
}

static void
breakpoints_listMarkDirty(breakpoints_list_state_t *st)
{
    if (!st) return;
    st->last_bps = NULL;
    st->last_count = -1;
}

static void
breakpoints_listRefreshAndMarkDirty(breakpoints_list_state_t *st)
{
    machine_refresh();
    if (st) breakpoints_listMarkDirty(st);
}

static int
breakpoints_recordEnsureCapacity(breakpoints_list_state_t *st, int minCap)
{
    if (!st) return 0;
    if (st->record_cap >= minCap) return 1;

    int newCap = st->record_cap ? st->record_cap * 2 : 8;
    while (newCap < minCap) newCap *= 2;

    breakpoints_record_t *newArr =
        (breakpoints_record_t*)alloc_realloc(st->records, (size_t)newCap * sizeof(breakpoints_record_t));
    if (!newArr) return 0;

    st->records = newArr;
    st->record_cap = newCap;
    return 1;
}

static breakpoints_record_t *
breakpoints_recordFind(breakpoints_list_state_t *st, int number)
{
    if (!st || st->record_count <= 0) return NULL;

    for (int i = 0; i < st->record_count; ++i) {
        if (st->records[i].data.number == number) {
            return &st->records[i];
        }
    }
    return NULL;
}

static breakpoints_record_t *
breakpoints_recordAdd(breakpoints_list_state_t *st, const machine_breakpoint_t *bp)
{
    if (!st || !bp) return NULL;

    if (st->record_count == st->record_cap) {
        if (!breakpoints_recordEnsureCapacity(st, st->record_count + 1)) {
            return NULL;
        }
    }

    breakpoints_record_t *rec = &st->records[st->record_count++];
    memcpy(&rec->data, bp, sizeof(rec->data));
    rec->present = 0;
    return rec;
}

static int
breakpoints_updateRecords(breakpoints_list_state_t *st, const machine_breakpoint_t *bps, int count)
{
    if (!st) return 0;

    int changed = 0;
    int hadActive = 0;

    for (int i = 0; i < st->record_count; ++i) {
        if (st->records[i].present) hadActive = 1;
        st->records[i].present = 0;
    }

    if (!bps || count <= 0) {
        if (hadActive) changed = 1;
        return changed;
    }

    for (int i = 0; i < count; ++i) {
        machine_breakpoint_t *bp = (machine_breakpoint_t*)&bps[i];
        if (!bp->func[0] || (!bp->file[0] && bp->line <= 0)) {
            breakpoints_resolveLocation(bp);
        }
        breakpoints_record_t *rec = breakpoints_recordFind(st, bp->number);

        if (!rec) {
            rec = breakpoints_recordAdd(st, bp);
            if (!rec) continue;
            changed = 1;
        } else {
            if (memcmp(&rec->data, bp, sizeof(rec->data)) != 0) {
                memcpy(&rec->data, bp, sizeof(rec->data));
                changed = 1;
            }
        }
        rec->present = 1;
    }

    return changed;
}

static void
breakpoints_rebuildEntries(breakpoints_list_state_t *st, e9ui_context_t *ctx)
{
    if (!st || !st->entries) return;

    e9ui_stack_removeAll(st->entries, ctx);

    int presentCount = 0;
    for (int i = 0; i < st->record_count; ++i) {
        if (st->records[i].present) presentCount++;
    }

    if (presentCount == 0) {
        e9ui_component_t *empty = breakpoints_emptyMake();
        if (empty) {
            e9ui_stack_addFlex(st->entries, empty);
        }
        return;
    }

    for (int i = 0; i < st->record_count; ++i) {
        breakpoints_record_t *rec = &st->records[i];
        if (!rec->present) continue;

        e9ui_component_t *entry = breakpoints_entryMake(rec, st);
        if (entry) {
            e9ui_stack_addFixed(st->entries, entry);
        }
    }
}

static void
breakpoints_listRebuild(breakpoints_list_state_t *st, e9ui_context_t *ctx)
{
    if (!st || !st->entries) return;

    const machine_breakpoint_t *bps = NULL;
    int count = 0;
    machine_getBreakpoints(&debugger.machine, &bps, &count);

    int changed = 0;
    if (bps != st->last_bps || count != st->last_count) {
        st->last_bps = bps;
        st->last_count = count;
        changed = 1;
    }
    if (breakpoints_updateRecords(st, bps, count)) {
        changed = 1;
    }
    if (!changed) return;

    breakpoints_rebuildEntries(st, ctx);
}

static int
breakpoints_listPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    breakpoints_list_state_t *st = self ? (breakpoints_list_state_t*)self->state : NULL;
    if (!st || !st->entries) {
        return 0;
    }

    breakpoints_listRebuild(st, ctx);

    if (!st->entries->preferredHeight) {
        return 0;
    }
    return st->entries->preferredHeight(st->entries, ctx, availW);
}

static void
breakpoints_listLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    self->bounds = bounds;

    breakpoints_list_state_t *st = (breakpoints_list_state_t*)self->state;
    if (!st || !st->entries) return;

    breakpoints_listRebuild(st, ctx);

    if (st->entries->layout) {
        st->entries->layout(st->entries, ctx, bounds);
    }
}

static void
breakpoints_listRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!ctx || !ctx->renderer) return;

    SDL_Rect r = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_SetRenderDrawColor(ctx->renderer, 16, 16, 20, 255);
    SDL_RenderFillRect(ctx->renderer, &r);

    breakpoints_list_state_t *st = (breakpoints_list_state_t*)self->state;
    if (st && st->entries && st->entries->render) {
        st->entries->render(st->entries, ctx);
    }
}

static void
breakpoints_listDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;

    breakpoints_list_state_t *st = (breakpoints_list_state_t*)self->state;
    if (!st) return;

    breakpoints_unregisterListState(st);

    if (st->records) {
        alloc_free(st->records);
        st->records = NULL;
    }
    st->record_count = 0;
    st->record_cap = 0;
}

static void
breakpoints_addCurrentCB(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    uint32_t processorId = debugger.activeDebugProcessorId;
    uint32_t addr = 0;
    if (!breakpoints_readActiveProcessorPc(processorId, &addr)) {
        return;
    }
    machine_breakpoint_t *bp = machine_findProcessorBreakpointByAddr(&debugger.machine,
                                                                      processorId,
                                                                      addr);
    if (bp) {
        if (!bp->enabled) {
            bp->enabled = 1;
            libretro_host_debugAddProcessorBreakpoint(processorId, addr);
        }
        breakpoints_resolveLocation(bp);
    } else {
        bp = machine_addProcessorBreakpoint(&debugger.machine, processorId, addr, 1);
        if (bp) {
            libretro_host_debugAddProcessorBreakpoint(processorId, addr);
            breakpoints_resolveLocation(bp);
        }
    }
    breakpoints_markDirty();
}

static void
breakpoints_clearAllCB(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    const machine_breakpoint_t *bps = NULL;
    int count = 0;
    machine_getBreakpoints(&debugger.machine, &bps, &count);
    if (bps && count > 0) {
        for (int i = 0; i < count; ++i) {
            uint32_t addr = bps[i].processorId == MACHINE_PROCESSOR_PRIMARY
                ? (uint32_t)(bps[i].addr & 0x00ffffffu)
                : (uint32_t)(bps[i].addr & 0x0000ffffu);
            libretro_host_debugRemoveProcessorBreakpoint(bps[i].processorId, addr);
        }
    }
    machine_clearBreakpoints(&debugger.machine);
    breakpoints_markDirty();
}

static void
breakpoints_toggleAllCB(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    const machine_breakpoint_t *bps = NULL;
    int count = 0;
    machine_getBreakpoints(&debugger.machine, &bps, &count);
    if (!bps || count <= 0) {
        return;
    }
    int anyEnabled = 0;
    for (int i = 0; i < count; ++i) {
        if (bps[i].enabled) {
            anyEnabled = 1;
            break;
        }
    }
    int targetEnabled = anyEnabled ? 0 : 1;
    for (int i = 0; i < count; ++i) {
        uint32_t addr = bps[i].processorId == MACHINE_PROCESSOR_PRIMARY
            ? (uint32_t)(bps[i].addr & 0x00ffffffu)
            : (uint32_t)(bps[i].addr & 0x0000ffffu);
        machine_setBreakpointEnabled(&debugger.machine, bps[i].number, targetEnabled, NULL);
        if (targetEnabled) {
            libretro_host_debugAddProcessorBreakpoint(bps[i].processorId, addr);
        } else {
            libretro_host_debugRemoveProcessorBreakpoint(bps[i].processorId, addr);
        }
    }
    breakpoints_markDirty();
}

static e9ui_component_t *
breakpoints_makeList(void)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    if (!c) return NULL;

    breakpoints_list_state_t *st = (breakpoints_list_state_t*)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(c);
        return NULL;
    }

    c->name = "breakpoints_list";
    c->state = st;

    st->entries = e9ui_stack_makeVertical();
    if (st->entries) {
        e9ui_child_add(c, st->entries, 0);
    }

    breakpoints_registerListState(st);

    c->preferredHeight = breakpoints_listPreferredHeight;
    c->layout = breakpoints_listLayout;
    c->render = breakpoints_listRender;
    c->dtor = breakpoints_listDtor;

    return c;
}

static void
breakpoints_componentDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)self;
    (void)ctx;
    breakpoints_btnAddCurrent = NULL;
}


e9ui_component_t *
breakpoints_makeComponent(void)
{
    e9ui_component_t *list = breakpoints_makeList();
    e9ui_component_t *listScroll = list ? e9ui_scroll_make(list) : NULL;

    e9ui_component_t *toolbar = e9ui_flow_make();
    e9ui_flow_setPadding(toolbar, 0);
    e9ui_flow_setSpacing(toolbar, 6);
    e9ui_flow_setWrap(toolbar, 1);

    breakpoints_list_state_t *list_state = list ? (breakpoints_list_state_t*)list->state : NULL;

    e9ui_component_t *btn_add = e9ui_button_make("Add Current", breakpoints_addCurrentCB, list_state);
    e9ui_setDisableVariable(btn_add, machine_getRunningState(debugger.machine), 1);        
    e9ui_button_setMini(btn_add, 1);
    e9ui_button_setIconAsset(btn_add, "assets/icons/break.png");
    hotkeys_registerButtonActionHotkey(btn_add, &e9ui->ctx, "breakpoint_add_current");
    breakpoints_btnAddCurrent = btn_add;
    breakpoints_refreshHotkeyTooltips();

    e9ui_flow_add(toolbar, btn_add);

    e9ui_component_t *btn_toggle = e9ui_button_make("Toggle", breakpoints_toggleAllCB, list_state);
    //e9ui_setDisableVariable(btn_toggle, machine_getRunningState(debugger.machine), 1);
    e9ui_button_setMini(btn_toggle, 1);
    e9ui_flow_add(toolbar, btn_toggle);

    e9ui_component_t *btn_clear = e9ui_button_make("Clear", breakpoints_clearAllCB, list_state);
    //    e9ui_setDisableVariable(btn_clear, machine_getRunningState(debugger.machine), 1);
    e9ui_button_setMini(btn_clear, 1);
    e9ui_button_setIconAsset(btn_clear, "assets/icons/trash.png");
    e9ui_flow_add(toolbar, btn_clear);

    e9ui_component_t *toolbar_box = e9ui_box_make(toolbar);
    e9ui_box_setPadding(toolbar_box, 6);
    e9ui_box_setBorder(toolbar_box, E9UI_BORDER_BOTTOM, (SDL_Color){70, 70, 70, 255}, 1);

    e9ui_component_t *stack = e9ui_stack_makeVertical();
    e9ui_stack_addFixed(stack, toolbar_box);
    if (listScroll) {
        e9ui_stack_addFlex(stack, listScroll);
    } else if (list) {
        e9ui_stack_addFlex(stack, list);
    }
    stack->dtor = breakpoints_componentDtor;

    return stack;
}
