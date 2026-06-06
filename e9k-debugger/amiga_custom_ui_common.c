/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <math.h>
#include <string.h>

#include "alloc.h"
#include "debug.h"
#include "e9ui_seek_bar.h"
#include "e9ui_text.h"
#include "amiga_custom_ui_internal.h"
#include "libretro_host.h"

const SDL_Color amiga_custom_ui_common_blitterStatsChartLabelColor = { 220, 220, 220, 255 };
const SDL_Color amiga_custom_ui_common_blitterStatsChartTextColor = { 232, 236, 240, 255 };
const SDL_Color amiga_custom_ui_common_blitterStatsChartTextShadowColor = { 12, 14, 18, 220 };
const SDL_Color amiga_custom_ui_common_dmaColorCpu = { 0xa2, 0x53, 0x42, 255 };
const SDL_Color amiga_custom_ui_common_dmaColorCopper = { 0xee, 0xee, 0x00, 255 };
const SDL_Color amiga_custom_ui_common_dmaColorAudio = { 0xff, 0x00, 0x00, 255 };
const SDL_Color amiga_custom_ui_common_dmaColorBlitter = { 0x00, 0x88, 0x88, 255 };
const SDL_Color amiga_custom_ui_common_dmaColorBitplane = { 0x00, 0x00, 0xff, 255 };
const SDL_Color amiga_custom_ui_common_dmaColorSprite = { 0xff, 0x00, 0xff, 255 };
const SDL_Color amiga_custom_ui_common_dmaColorDisk = { 0xff, 0xff, 0xff, 255 };
const SDL_Color amiga_custom_ui_common_dmaColorOther = { 0xff, 0xb8, 0x40, 255 };
const SDL_Color amiga_custom_ui_common_dmaColorIdle = { 0x5a, 0x5a, 0x5a, 255 };

void
amiga_custom_ui_common_blitterStatsChartDrawText(e9ui_context_t *ctx,
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
amiga_custom_ui_common_statsChartU32ToText(uint32_t value, char *buf, int cap)
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
amiga_custom_ui_common_statsChartAppendChar(char *buf, int cap, int *pos, char ch)
{
    if (!buf || !pos || cap <= 0 || *pos < 0 || *pos >= cap - 1) {
        return;
    }
    buf[*pos] = ch;
    *pos = *pos + 1;
    buf[*pos] = '\0';
}


static void
amiga_custom_ui_common_statsChartAppendText(char *buf, int cap, int *pos, const char *text)
{
    if (!buf || !pos || !text || cap <= 0 || *pos < 0) {
        return;
    }
    while (*text && *pos < cap - 1) {
        buf[*pos] = *text;
        *pos = *pos + 1;
        text++;
    }
    if (*pos >= cap) {
        *pos = cap - 1;
    }
    buf[*pos] = '\0';
}


static void
amiga_custom_ui_common_statsChartValueText(uint32_t value,
                                           const char *suffix,
                                           char *buf,
                                           int cap)
{
    if (!buf || cap <= 0) {
        return;
    }
    buf[0] = '\0';
    int pos = amiga_custom_ui_common_statsChartU32ToText(value, buf, cap);
    amiga_custom_ui_common_statsChartAppendChar(buf, cap, &pos, ' ');
    amiga_custom_ui_common_statsChartAppendText(buf, cap, &pos, suffix);
}


void
amiga_custom_ui_common_statsChartMeasureUint(e9ui_context_t *ctx, TTF_Font *font, uint32_t value, SDL_Color color, int *outW, int *outH)
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
    int n = amiga_custom_ui_common_statsChartU32ToText(value, digits, (int)sizeof(digits));
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


void
amiga_custom_ui_common_statsChartMeasureText(e9ui_context_t *ctx, TTF_Font *font, const char *text, SDL_Color color, int *outW, int *outH)
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


void
amiga_custom_ui_common_statsChartMeasureValueText(e9ui_context_t *ctx,
                                                  TTF_Font *font,
                                                  uint32_t value,
                                                  const char *suffix,
                                                  SDL_Color color,
                                                  int *outW,
                                                  int *outH)
{
    char text[64];
    amiga_custom_ui_common_statsChartValueText(value,
                                               suffix,
                                               text,
                                               (int)sizeof(text));
    amiga_custom_ui_common_statsChartMeasureText(ctx, font, text, color, outW, outH);
}


void
amiga_custom_ui_common_statsChartDrawValueText(e9ui_context_t *ctx,
                                               TTF_Font *font,
                                               uint32_t value,
                                               const char *suffix,
                                               SDL_Color color,
                                               int x,
                                               int y)
{
    char text[64];
    amiga_custom_ui_common_statsChartValueText(value,
                                               suffix,
                                               text,
                                               (int)sizeof(text));
    amiga_custom_ui_common_blitterStatsChartDrawText(ctx, font, text, color, x, y);
}


static void
amiga_custom_ui_common_blitterStatsChartHueToRgb(float h, Uint8 *r, Uint8 *g, Uint8 *b)
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


void
amiga_custom_ui_common_blitterStatsChartFillGradient(e9ui_context_t *ctx,
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
        amiga_custom_ui_common_blitterStatsChartHueToRgb(h, &rr, &gg, &bb);
        SDL_SetRenderDrawColor(ctx->renderer, rr, gg, bb, 255);
        SDL_RenderDrawLine(ctx->renderer,
                           rect->x + dx,
                           rect->y,
                           rect->x + dx,
                           rect->y + rect->h - 1);
    }
}


int
amiga_custom_ui_common_textboxLikeHeight(e9ui_context_t *ctx)
{
    TTF_Font *font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : (ctx ? ctx->font : NULL);
    int lineHeight = font ? TTF_FontHeight(font) : 16;
    if (lineHeight <= 0) {
        lineHeight = 16;
    }
    return lineHeight + 12;
}


static int
amiga_custom_ui_common_seekRowPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    if (!self || !self->state || !ctx) {
        return 0;
    }
    amiga_custom_ui_seek_row_state_t *st = (amiga_custom_ui_seek_row_state_t*)self->state;
    int barH = e9ui_scale_px(ctx, st->barHeight);
    int pad = e9ui_scale_px(ctx, st->rowPadding);
    if (barH <= 0) {
        barH = 10;
    }
    return barH + pad * 2;
}


static void
amiga_custom_ui_common_seekRowLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self || !self->state || !ctx) {
        return;
    }
    amiga_custom_ui_seek_row_state_t *st = (amiga_custom_ui_seek_row_state_t*)self->state;
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
amiga_custom_ui_common_seekRowRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx) {
        return;
    }
    amiga_custom_ui_seek_row_state_t *st = (amiga_custom_ui_seek_row_state_t*)self->state;
    if (st->bar && st->bar->render) {
        st->bar->render(st->bar, ctx);
    }
}


static void
amiga_custom_ui_common_seekRowDtor(e9ui_component_t *self, e9ui_context_t *ctx)
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


e9ui_component_t *
amiga_custom_ui_common_blitterVisDecaySeekRowMake(e9ui_component_t **outBar)
{
    e9ui_component_t *row = (e9ui_component_t*)alloc_calloc(1, sizeof(*row));
    amiga_custom_ui_seek_row_state_t *st = (amiga_custom_ui_seek_row_state_t*)alloc_calloc(1, sizeof(*st));
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
    row->name = "amiga_custom_ui_seek_row";
    row->state = st;
    row->preferredHeight = amiga_custom_ui_common_seekRowPreferredHeight;
    row->layout = amiga_custom_ui_common_seekRowLayout;
    row->render = amiga_custom_ui_common_seekRowRender;
    row->dtor = amiga_custom_ui_common_seekRowDtor;
    if (st->bar) {
        e9ui_child_add(row, st->bar, NULL);
    }
    if (outBar) {
        *outBar = st->bar;
    }
    return row;
}


static int
amiga_custom_ui_common_insetRowPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    if (!self || !self->state || !ctx) {
        return 0;
    }
    amiga_custom_ui_inset_row_state_t *st = (amiga_custom_ui_inset_row_state_t *)self->state;
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
amiga_custom_ui_common_insetRowLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self || !self->state || !ctx) {
        return;
    }
    amiga_custom_ui_inset_row_state_t *st = (amiga_custom_ui_inset_row_state_t *)self->state;
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
amiga_custom_ui_common_insetRowRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx) {
        return;
    }
    amiga_custom_ui_inset_row_state_t *st = (amiga_custom_ui_inset_row_state_t *)self->state;
    if (st->child && st->child->render) {
        st->child->render(st->child, ctx);
    }
}


static void
amiga_custom_ui_common_insetRowDtor(e9ui_component_t *self, e9ui_context_t *ctx)
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


e9ui_component_t *
amiga_custom_ui_common_insetRowMake(e9ui_component_t *child, int leftInset, int rightInset)
{
    if (!child) {
        return NULL;
    }
    e9ui_component_t *row = (e9ui_component_t *)alloc_calloc(1, sizeof(*row));
    amiga_custom_ui_inset_row_state_t *st = (amiga_custom_ui_inset_row_state_t *)alloc_calloc(1, sizeof(*st));
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
    row->name = "amiga_custom_ui_inset_row";
    row->state = st;
    row->preferredHeight = amiga_custom_ui_common_insetRowPreferredHeight;
    row->layout = amiga_custom_ui_common_insetRowLayout;
    row->render = amiga_custom_ui_common_insetRowRender;
    row->dtor = amiga_custom_ui_common_insetRowDtor;
    e9ui_child_add(row, child, NULL);
    return row;
}


static int
amiga_custom_ui_common_dmaStatsHeaderRowPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    if (!self || !self->state || !ctx) {
        return 0;
    }
    amiga_custom_ui_dma_stats_header_row_state_t *st = (amiga_custom_ui_dma_stats_header_row_state_t *)self->state;
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
amiga_custom_ui_common_dmaStatsHeaderRowLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self || !self->state || !ctx) {
        return;
    }
    self->bounds = bounds;
    amiga_custom_ui_dma_stats_header_row_state_t *st = (amiga_custom_ui_dma_stats_header_row_state_t *)self->state;
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
amiga_custom_ui_common_dmaStatsHeaderRowRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx) {
        return;
    }
    amiga_custom_ui_dma_stats_header_row_state_t *st = (amiga_custom_ui_dma_stats_header_row_state_t *)self->state;
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
amiga_custom_ui_common_dmaStatsHeaderRowDtor(e9ui_component_t *self, e9ui_context_t *ctx)
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


e9ui_component_t *
amiga_custom_ui_common_dmaStatsHeaderRowMake(e9ui_component_t *checkbox, e9ui_component_t *hintRow, int leftInset, int gap)
{
    if (!checkbox || !hintRow) {
        return NULL;
    }
    e9ui_component_t *row = (e9ui_component_t *)alloc_calloc(1, sizeof(*row));
    amiga_custom_ui_dma_stats_header_row_state_t *st = (amiga_custom_ui_dma_stats_header_row_state_t *)alloc_calloc(1, sizeof(*st));
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
    row->name = "amiga_custom_ui_dma_stats_header_row";
    row->state = st;
    row->preferredHeight = amiga_custom_ui_common_dmaStatsHeaderRowPreferredHeight;
    row->layout = amiga_custom_ui_common_dmaStatsHeaderRowLayout;
    row->render = amiga_custom_ui_common_dmaStatsHeaderRowRender;
    row->dtor = amiga_custom_ui_common_dmaStatsHeaderRowDtor;
    e9ui_child_add(row, checkbox, NULL);
    e9ui_child_add(row, hintRow, NULL);
    return row;
}


void
amiga_custom_ui_common_applyOption(e9k_debug_option_t option, uint32_t argument)
{
    amiga_custom_ui_state_t *ui = &amiga_custom_ui_state;
    if (libretro_host_debugSetDebugOption(option, argument, NULL)) {
        ui->warnedMissingOption = 0;
        return;
    }
    if (!ui->warnedMissingOption) {
        debug_error("custom ui: core does not expose debug option API");
        ui->warnedMissingOption = 1;
    }
}


void
amiga_custom_ui_common_setComponentDisabled(e9ui_component_t *comp, int disabled)
{
    if (!comp) {
        return;
    }
    comp->disabled = disabled ? 1 : 0;
}


void
amiga_custom_ui_common_syncDmaStatsCycleExactHint(amiga_custom_ui_state_t *ui)
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


int
amiga_custom_ui_common_clampCopperLine(int line)
{
    if (line < 0) {
        return 0;
    }
    if (line > AMIGA_CUSTOM_UI_COPPER_LINE_MAX) {
        return AMIGA_CUSTOM_UI_COPPER_LINE_MAX;
    }
    return line;
}
