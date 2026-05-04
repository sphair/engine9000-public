/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <strings.h>
#include <stdint.h>

#include "registers.h"
#include "debugger.h"
#include "machine.h" 
#include "breakpoints.h"
#include "libretro_host.h"
#include "target.h"

typedef struct registers_entry {
    SDL_Rect rect;
    uint32_t addr;
    int bucketId;
} registers_entry_t;

typedef struct registers_state {
    registers_entry_t *entries;
    int entryCount;
    int entryCap;
    int pendingToggle;
    uint32_t pendingAddr;
    int pendingX;
    int pendingY;
    uint32_t lastPc;
    int hasLastPc;
    uint32_t lastValues[19];
    int lastValid[19];
    uint32_t prevValues[19];
    int prevValid[19];
    e9k_debug_processor_reg_t extraRegs[32];
    size_t extraRegCount;
    int extraRegsValid;
    uint64_t prevExtraValues[32];
    int prevExtraValid[32];
} registers_state_t;

static void
registers_refreshExtraRegs(registers_state_t *st);

static int
registers_findAny(const char **cands, int ncand, unsigned long *out)
{
    for (int i=0;i<ncand;i++) {
        if (machine_findReg(&debugger.machine, cands[i], out)) return 1;
    }
    return 0;
}

static int
registers_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self; (void)availW;
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    int lh = font ? TTF_FontHeight(font) : 16; if (lh <= 0) lh = 16;
    int lines = 4;
    registers_state_t *st = self ? (registers_state_t*)self->state : NULL;
    if (st && !st->extraRegsValid) {
        registers_refreshExtraRegs(st);
    }
    if (st && st->extraRegsValid && st->extraRegCount > 0) {
        lines += 2 + (int)((st->extraRegCount + 7) / 8);
    }
    return lh * lines + 8;
}

static void
registers_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static void
registers_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    registers_state_t *st = (registers_state_t*)self->state;
    if (!st) {
        return;
    }
    if (st->entries) {
        alloc_free(st->entries);
    }
    alloc_free(st);
    self->state = NULL;
}

static registers_entry_t *
registers_allocEntry(registers_state_t *st)
{
    if (!st) {
        return NULL;
    }
    if (st->entryCount >= st->entryCap) {
        int nextCap = st->entryCap > 0 ? st->entryCap * 2 : 32;
        registers_entry_t *next = (registers_entry_t*)alloc_realloc(
            st->entries, (size_t)nextCap * sizeof(*next));
        if (!next) {
            return NULL;
        }
        st->entries = next;
        st->entryCap = nextCap;
    }
    registers_entry_t *entry = &st->entries[st->entryCount++];
    memset(entry, 0, sizeof(*entry));
    return entry;
}

static int
registers_findEntryAt(const registers_state_t *st, int x, int y, int *outIndex)
{
    if (!st) {
        return 0;
    }
    for (int i = 0; i < st->entryCount; ++i) {
        const registers_entry_t *entry = &st->entries[i];
        if (x >= entry->rect.x && x < entry->rect.x + entry->rect.w &&
            y >= entry->rect.y && y < entry->rect.y + entry->rect.h) {
            if (outIndex) {
                *outIndex = i;
            }
            return 1;
        }
    }
    return 0;
}

static int
registers_hasBreakpoint(uint32_t addr)
{
    machine_breakpoint_t *bp = machine_findBreakpointByAddr(&debugger.machine, addr);
    if (!bp) {
        return 0;
    }
    return bp->enabled ? 1 : 0;
}

static void
registers_toggleBreakpoint(uint32_t addr)
{
    machine_breakpoint_t *existing = machine_findBreakpointByAddr(&debugger.machine, addr);
    if (existing) {
        if (machine_removeBreakpointByAddr(&debugger.machine, addr)) {
            libretro_host_debugRemoveBreakpoint(addr);
            breakpoints_markDirty();
        }
        return;
    }
    machine_breakpoint_t *bp = machine_addBreakpoint(&debugger.machine, addr, 1);
    if (bp) {
        breakpoints_resolveLocation(bp);
        libretro_host_debugAddBreakpoint(addr);
        breakpoints_markDirty();
    }
}

static void
registers_formatProcessorValue(const e9k_debug_processor_reg_t *reg, char *out, size_t cap)
{
    unsigned hexWidth = (unsigned)((reg->bits + 3u) / 4u);

    if (!out || cap == 0) {
        return;
    }
    if (hexWidth == 0) {
        hexWidth = 1;
    }
    if (hexWidth > 16) {
        hexWidth = 16;
    }
    snprintf(out, cap, "%0*llX", (int)hexWidth, (unsigned long long)reg->value);
}

static void
registers_refreshExtraRegs(registers_state_t *st)
{
    const char *title = NULL;
    size_t count = 0;
    e9k_debug_processor_reg_t regs[32];
    int changed = 0;

    if (!st || !target || !target->registersReadExtra) {
        return;
    }
    if (!target->registersReadExtra(&title, regs, countof(regs), &count)) {
        return;
    }
    (void)title;
    if (count > countof(regs)) {
        count = countof(regs);
    }
    if (st->extraRegsValid) {
        if (count != st->extraRegCount) {
            changed = 1;
        } else {
            for (size_t i = 0; i < count; ++i) {
                if (strncmp(st->extraRegs[i].name, regs[i].name, sizeof(st->extraRegs[i].name)) != 0 ||
                    st->extraRegs[i].value != regs[i].value) {
                    changed = 1;
                    break;
                }
            }
        }
    }
    if (changed) {
        memset(st->prevExtraValid, 0, sizeof(st->prevExtraValid));
        for (size_t i = 0; i < st->extraRegCount && i < countof(st->prevExtraValues); ++i) {
            st->prevExtraValues[i] = st->extraRegs[i].value;
            st->prevExtraValid[i] = 1;
        }
    }
    memcpy(st->extraRegs, regs, count * sizeof(regs[0]));
    st->extraRegCount = count;
    st->extraRegsValid = count > 0 ? 1 : 0;
}

void
registers_refreshExtraRegsNow(e9ui_component_t *component)
{
    if (!component) {
        return;
    }
    registers_state_t *st = (registers_state_t*)component->state;
    registers_refreshExtraRegs(st);
}

static void
registers_renderExtraBlock(e9ui_component_t *self,
                           e9ui_context_t *ctx,
                           TTF_Font *font,
                           SDL_Rect r,
                           int *curX,
                           int *curY,
                           int lh,
                           int padX,
                           int padY,
                           int spaceW,
                           SDL_Color txt,
                           SDL_Color changedLabelCol)
{
    registers_state_t *st = (registers_state_t*)self->state;

    if (!st || !st->extraRegsValid || st->extraRegCount == 0) {
        return;
    }
    if (!machine_getRunning(debugger.machine)) {
        registers_refreshExtraRegs(st);
    }
    if (!st->extraRegsValid || st->extraRegCount == 0) {
        return;
    }

    *curX = r.x + padX;
    *curY += lh + padY;
    if (*curY + lh > r.y + r.h - padY) {
        return;
    }

    *curX = r.x + padX;
    *curY += lh + padY;
    if (*curY + lh > r.y + r.h - padY) {
        return;
    }

    for (size_t i = 0; i < st->extraRegCount; ++i) {
        char label[32];
        char value[32];
        int labelW = 0;
        int labelH = 0;
        int valueW = 0;
        int valueH = 0;
        int totalW = 0;
        int labelChanged = 0;

        snprintf(label, sizeof(label), "%s:", st->extraRegs[i].name);
        registers_formatProcessorValue(&st->extraRegs[i], value, sizeof(value));
        if (font) {
            TTF_SizeText(font, label, &labelW, &labelH);
            TTF_SizeText(font, value, &valueW, &valueH);
        }
        totalW = labelW + spaceW + valueW;
        if (*curX + totalW > r.x + r.w - padX) {
            *curX = r.x + padX;
            *curY += lh + padY;
            if (*curY + lh > r.y + r.h - padY) {
                break;
            }
        }
        if (i < countof(st->prevExtraValues) &&
            st->prevExtraValid[i] &&
            st->prevExtraValues[i] != st->extraRegs[i].value) {
            labelChanged = 1;
        }
        if (font) {
            SDL_Color labelColor = labelChanged ? changedLabelCol : txt;
            SDL_Texture *labelTexture = e9ui_text_cache_getText(ctx->renderer, font, label, labelColor, NULL, NULL);
            SDL_Texture *valueTexture = e9ui_text_cache_getText(ctx->renderer, font, value, txt, NULL, NULL);
            if (labelTexture) {
                SDL_Rect labelRect = { *curX, *curY, labelW, labelH };
                SDL_RenderCopy(ctx->renderer, labelTexture, NULL, &labelRect);
            }
            if (valueTexture) {
                SDL_Rect valueRect = { *curX + labelW + spaceW, *curY, valueW, valueH };
                SDL_RenderCopy(ctx->renderer, valueTexture, NULL, &valueRect);
            }
        }
        *curX += totalW + padX;
    }
}

static void
registers_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    registers_state_t *st = (registers_state_t*)self->state;
    if (st) {
        st->entryCount = 0;
        if (!st->extraRegsValid) {
            registers_refreshExtraRegs(st);
        }
    }
    SDL_Rect r = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_SetRenderDrawColor(ctx->renderer, 22, 22, 22, 255);
    SDL_RenderFillRect(ctx->renderer, &r);
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    int lh = font ? TTF_FontHeight(font) : 16; if (lh <= 0) lh = 16;
    
    int padX = 12;
    int padY = 4;
    int curX = r.x + padX;
    int curY = r.y + padY;
    SDL_Color txt = (SDL_Color){220,220,220,255};
    SDL_Color changedLabelCol = (SDL_Color){220,80,80,255};
    SDL_Color bpCol = (SDL_Color){120,200,120,255};
    int spaceW = 4;
    int forceBreaks = 0;
    if (font) {
        TTF_SizeText(font, " ", &spaceW, NULL);
        const char *dlabels[] = {
            "D0: FFFFFFFF", "D1: FFFFFFFF", "D2: FFFFFFFF", "D3: FFFFFFFF",
            "D4: FFFFFFFF", "D5: FFFFFFFF", "D6: FFFFFFFF", "D7: FFFFFFFF"
        };
        int total = padX;
        for (size_t i = 0; i < sizeof(dlabels)/sizeof(dlabels[0]); ++i) {
            int tw = 0;
            if (TTF_SizeText(font, dlabels[i], &tw, NULL) == 0) {
                total += tw + padX;
            }
        }
        if (total <= r.w) {
            forceBreaks = 1;
        }
    }
    const char *order[] = {
        "D0","D1","D2","D3","D4","D5","D6","D7",
        "A0","A1","A2","A3","A4","A5","A6","A7",
        "SP","PC","SR"
    };
    const size_t registerCount = sizeof(order)/sizeof(order[0]);
    uint32_t currentValues[19];
    int currentValid[19];
    memset(currentValues, 0, sizeof(currentValues));
    memset(currentValid, 0, sizeof(currentValid));
    unsigned long pcNow = 0;
    int hasPcNow = 0;
    {
        const char *alts[] = { "PC", "pc" };
        hasPcNow = registers_findAny(alts, (int)(sizeof(alts)/sizeof(alts[0])), &pcNow);
    }
    if (st && st->hasLastPc && hasPcNow && ((uint32_t)pcNow != st->lastPc)) {
        memcpy(st->prevValues, st->lastValues, sizeof(st->prevValues));
        memcpy(st->prevValid, st->lastValid, sizeof(st->prevValid));
    }
    for (size_t i = 0; i < registerCount; ++i) {
        unsigned long v = 0;
        int found = 0;
        if (strcmp(order[i], "A6") == 0) {
            const char *alts[] = { "A6", "FP" , "fp" };
            found = registers_findAny(alts, (int)(sizeof(alts)/sizeof(alts[0])), &v);
        } else if (strcmp(order[i], "SP") == 0) {
            const char *alts[] = { "SP", "sp", "A7", "a7" };
            found = registers_findAny(alts, (int)(sizeof(alts)/sizeof(alts[0])), &v);
        } else if (strcmp(order[i], "PC") == 0) {
            const char *alts[] = { "PC", "pc" };
            found = registers_findAny(alts, (int)(sizeof(alts)/sizeof(alts[0])), &v);
        } else {
            found = machine_findReg(&debugger.machine, order[i], &v);
        }
        if (!found) {
            v = 0;
        }
        currentValues[i] = (uint32_t)v;
        currentValid[i] = found ? 1 : 0;
        char label[32];
        char value[32];
        snprintf(label, sizeof(label), "%s:", order[i]);
        snprintf(value, sizeof(value), "%08X", (unsigned int)v);
        int labelW = 0, labelH = 0;
        int valueW = 0, valueH = 0;
        if (font) {
            TTF_SizeText(font, label, &labelW, &labelH);
            TTF_SizeText(font, value, &valueW, &valueH);
        }
        int totalW = labelW + spaceW + valueW;
        if (curX + totalW > r.x + r.w - padX) {
            curX = r.x + padX;
            curY += lh + padY;
            if (curY + lh > r.y + r.h - padY) { break; }
        }
        int labelChanged = 0;
        if (st && i < (sizeof(st->prevValues)/sizeof(st->prevValues[0])) &&
            st->prevValid[i] && found && st->prevValues[i] != (uint32_t)v) {
            labelChanged = 1;
        }
        if (font) {
            SDL_Color labelColor = labelChanged ? changedLabelCol : txt;
            SDL_Texture *t = e9ui_text_cache_getText(ctx->renderer, font, label, labelColor, NULL, NULL);
            if (t) {
                SDL_Rect tr = { curX, curY, labelW, labelH };
                SDL_RenderCopy(ctx->renderer, t, NULL, &tr);
            }
        }
        int valueX = curX + labelW + spaceW;
        int valueY = curY;
        uint32_t addr = (uint32_t)(v & 0x00ffffffu);
        SDL_Color useCol = registers_hasBreakpoint(addr) ? bpCol : txt;
        registers_entry_t *entry = registers_allocEntry(st);
        void *bucket = entry ? (void*)&entry->bucketId : (void*)self;
        e9ui_drawSelectableText(ctx, self, font, value, useCol, valueX, valueY,
                                lh, valueW, bucket, 0, 1);
        if (entry && valueW > 0) {
            entry->rect = (SDL_Rect){ valueX, valueY, valueW, lh };
            entry->addr = addr;
        }
        curX += totalW + padX;
        if (forceBreaks && (strcmp(order[i], "D7") == 0 || strcmp(order[i], "A7") == 0)) {
            curX = r.x + padX;
            curY += lh + padY;
            if (curY + lh > r.y + r.h - padY) {
                break;
            }
        }
    }
    registers_renderExtraBlock(self, ctx, font, r, &curX, &curY, lh, padX, padY, spaceW, txt, changedLabelCol);
    if (st) {
        memcpy(st->lastValues, currentValues, sizeof(st->lastValues));
        memcpy(st->lastValid, currentValid, sizeof(st->lastValid));
        if (hasPcNow) {
            st->lastPc = (uint32_t)pcNow;
            st->hasLastPc = 1;
        }
    }
}

static int
registers_handleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!self || !ctx || !ev) {
        return 0;
    }
    registers_state_t *st = (registers_state_t*)self->state;
    if (!st) {
        return 0;
    }
    if (ev->type == SDL_MOUSEMOTION) {
        if (!st->pendingToggle) {
            return 0;
        }
        int slop = e9ui_scale_px(ctx, 4);
        int dx = ev->motion.x - st->pendingX;
        int dy = ev->motion.y - st->pendingY;
        if (dx * dx + dy * dy >= slop * slop) {
            st->pendingToggle = 0;
        }
        return 0;
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_LEFT) {
        int mx = ev->button.x;
        int my = ev->button.y;
        int hitIndex = -1;
        if (registers_findEntryAt(st, mx, my, &hitIndex)) {
            st->pendingToggle = 1;
            st->pendingAddr = st->entries[hitIndex].addr;
            st->pendingX = mx;
            st->pendingY = my;
            return 1;
        }
        return 0;
    }
    if (ev->type == SDL_MOUSEBUTTONUP && ev->button.button == SDL_BUTTON_LEFT) {
        if (!st->pendingToggle) {
            return 0;
        }
        st->pendingToggle = 0;
        int slop = e9ui_scale_px(ctx, 4);
        int dx = ev->button.x - st->pendingX;
        int dy = ev->button.y - st->pendingY;
        if (dx * dx + dy * dy >= slop * slop) {
            return 0;
        }
        registers_toggleBreakpoint(st->pendingAddr);
        return 1;
    }
    return 0;
}

e9ui_component_t *
registers_makeComponent(void)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    c->name = "e9ui_registers";
    registers_state_t *st = (registers_state_t*)alloc_calloc(1, sizeof(*st));
    c->state = st;
    c->preferredHeight = registers_preferredHeight;
    c->layout = registers_layout;
    c->render = registers_render;
    c->handleEvent = registers_handleEvent;
    c->dtor = registers_dtor;
    return c;
}

 
