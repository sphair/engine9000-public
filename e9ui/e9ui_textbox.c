/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "platform.h"

typedef struct textbox_state {
    char               *text;
    int                len;
    int                cursor;
    int                sel_start;
    int                sel_end;
    int                selecting;
    int                markActive;
    int                markPos;
    Uint32             last_click_ms;
    int                click_streak;
    int                last_click_x;
    list_t            *undo;
    list_t            *redo;
    int                maxLen;
    int                scrollX;
    int                editable;
    int                numeric_only;
    char               *placeholder;
    char               *scratch;
    e9ui_textbox_submit_cb_t submit;
    e9ui_textbox_change_cb_t change;
    e9ui_textbox_key_cb_t key_cb;
    void               *key_user;
    void               *user;
    int                frame_visible;
    int                focus_border_visible;
    e9ui_textbox_option_t *select_options;
    int                select_optionCount;
    int                select_optionCap;
    int                select_selectedIndex;
    e9ui_textbox_option_change_cb_t select_change;
    void               *select_changeUser;
    int                textColorOverride;
    SDL_Color          textColor;
    e9ui_textbox_completion_mode_t completionMode;
    char               **completionList;
    int                completionCount;
    int                completionCap;
    int                completionSel;
    int                completionPrefixLen;
    char               *completionPrefix;
    char               *completionRest;
    int                convertValid;
    SDL_Keycode        convertKey;
    char               *convertBefore;
    char               *convertAfter;
    int                convertApplying;
    int                suppressNextTextInput;
    int                suppressSelectToggleClick;
    int                enterMovesToNextTextbox;
} textbox_state_t;

typedef struct textbox_snapshot {
    char *text;
    int len;
    int cursor;
    int sel_start;
    int sel_end;
} textbox_snapshot_t;

static e9ui_textbox_shortcut_match_cb_t textbox_shortcutMatchCb = NULL;
static void *textbox_shortcutMatchUser = NULL;

static void
textbox_recordUndo(textbox_state_t *st);

static void
textbox_clearSelection(textbox_state_t *st);

static void
textbox_completionClear(textbox_state_t *st);

static void
textbox_history_clear(list_t **list);

static void
textbox_selectOverlay_toggle(e9ui_context_t *ctx, e9ui_component_t *owner);

static void
textbox_selectOverlay_openForTyping(e9ui_context_t *ctx, e9ui_component_t *owner);

static const char *
textbox_select_displayLabel(const e9ui_textbox_option_t *opt)
{
    if (!opt) {
        return "";
    }
    if (opt->label && *opt->label) {
        return opt->label;
    }
    return opt->value ? opt->value : "";
}

static int
textbox_select_containsInsensitive(const char *haystack, const char *needle)
{
    if (!needle || !needle[0]) {
        return 1;
    }
    if (!haystack || !haystack[0]) {
        return 0;
    }
    for (const char *h = haystack; *h; ++h) {
        const char *p = h;
        const char *n = needle;
        while (*p && *n && tolower((unsigned char)*p) == tolower((unsigned char)*n)) {
            ++p;
            ++n;
        }
        if (!*n) {
            return 1;
        }
    }
    return 0;
}

static int
textbox_select_optionMatchesFilter(const e9ui_textbox_option_t *opt, const char *filter)
{
    if (!opt) {
        return 0;
    }
    if (!filter || !filter[0]) {
        return 1;
    }
    const char *label = textbox_select_displayLabel(opt);
    if (textbox_select_containsInsensitive(label, filter)) {
        return 1;
    }
    if (opt->value && textbox_select_containsInsensitive(opt->value, filter)) {
        return 1;
    }
    return 0;
}

static int
textbox_select_filteredCount(const textbox_state_t *st, const char *filter)
{
    if (!st || !st->select_options || st->select_optionCount <= 0) {
        return 0;
    }
    int count = 0;
    for (int i = 0; i < st->select_optionCount; ++i) {
        if (textbox_select_optionMatchesFilter(&st->select_options[i], filter)) {
            count++;
        }
    }
    return count;
}

static int
textbox_select_filteredNthToOptionIndex(const textbox_state_t *st, const char *filter, int nth)
{
    if (!st || !st->select_options || st->select_optionCount <= 0 || nth < 0) {
        return -1;
    }
    int seen = 0;
    for (int i = 0; i < st->select_optionCount; ++i) {
        if (!textbox_select_optionMatchesFilter(&st->select_options[i], filter)) {
            continue;
        }
        if (seen == nth) {
            return i;
        }
        seen++;
    }
    return -1;
}

static int
textbox_select_filteredOptionToNthIndex(const textbox_state_t *st, const char *filter, int optionIndex)
{
    if (!st || !st->select_options || st->select_optionCount <= 0) {
        return -1;
    }
    if (optionIndex < 0 || optionIndex >= st->select_optionCount) {
        return -1;
    }
    int seen = 0;
    for (int i = 0; i < st->select_optionCount; ++i) {
        if (!textbox_select_optionMatchesFilter(&st->select_options[i], filter)) {
            continue;
        }
        if (i == optionIndex) {
            return seen;
        }
        seen++;
    }
    return -1;
}

static int
textbox_select_findIndex(const textbox_state_t *st, const char *value)
{
    if (!st || !st->select_options || st->select_optionCount <= 0 || !value) {
        return -1;
    }
    for (int i = 0; i < st->select_optionCount; ++i) {
        const char *v = st->select_options[i].value;
        if (v && strcmp(v, value) == 0) {
            return i;
        }
    }
    return -1;
}

static const char *
textbox_select_selectedValue(const textbox_state_t *st)
{
    if (!st || !st->select_options || st->select_selectedIndex < 0 ||
        st->select_selectedIndex >= st->select_optionCount) {
        return NULL;
    }
    return st->select_options[st->select_selectedIndex].value;
}

static void
textbox_select_notify(textbox_state_t *st, e9ui_context_t *ctx, e9ui_component_t *self)
{
    if (!st || !st->select_change || !self) {
        return;
    }
    const char *value = textbox_select_selectedValue(st);
    st->select_change(ctx, self, value ? value : "", st->select_changeUser);
}

static void
textbox_select_applyDisplay(textbox_state_t *st, const char *display)
{
    if (!st) {
        return;
    }
    if (!display) {
        display = "";
    }
    int len = (int)strlen(display);
    if (len > st->maxLen) {
        len = st->maxLen;
    }
    memcpy(st->text, display, (size_t)len);
    st->text[len] = '\0';
    st->len = len;
    st->cursor = len;
    textbox_clearSelection(st);
    st->scrollX = 0;
    textbox_completionClear(st);
    textbox_history_clear(&st->undo);
    textbox_history_clear(&st->redo);
    if (st->convertBefore) {
        alloc_free(st->convertBefore);
        st->convertBefore = NULL;
    }
    if (st->convertAfter) {
        alloc_free(st->convertAfter);
        st->convertAfter = NULL;
    }
    st->convertValid = 0;
}

static void
textbox_fillScratch(textbox_state_t *st, int count)
{
    if (!st || !st->scratch) {
        return;
    }
    if (count < 0) {
        count = 0;
    }
    if (count > st->len) {
        count = st->len;
    }
    if (count > 0) {
        memcpy(st->scratch, st->text, (size_t)count);
    }
    st->scratch[count] = '\0';
}

static void
textbox_updateScroll(textbox_state_t *st, TTF_Font *font, int viewW)
{
    if (!st || !font || viewW <= 0) {
        return;
    }
    int cursorX = 0;
    if (st->cursor > 0) {
        textbox_fillScratch(st, st->cursor);
        TTF_SizeText(font, st->scratch, &cursorX, NULL);
    }
    int totalW = 0;
    textbox_fillScratch(st, st->len);
    TTF_SizeText(font, st->scratch, &totalW, NULL);
    if (totalW < viewW) {
        st->scrollX = 0;
        return;
    }
    int maxOffset = totalW - viewW;
    int desired = cursorX;
    if (desired < st->scrollX) {
        st->scrollX = desired;
    } else if (desired > st->scrollX + viewW) {
        st->scrollX = desired - viewW;
    }
    if (st->scrollX < 0) {
        st->scrollX = 0;
    }
    if (st->scrollX > maxOffset) {
        st->scrollX = maxOffset;
    }
}

static void
textbox_notifyChange(textbox_state_t *st, e9ui_context_t *ctx)
{
    if (st && !st->convertApplying) {
        if (st->convertBefore) {
            alloc_free(st->convertBefore);
            st->convertBefore = NULL;
        }
        if (st->convertAfter) {
            alloc_free(st->convertAfter);
            st->convertAfter = NULL;
        }
        st->convertValid = 0;
    }
    if (!st || !st->change) {
        return;
    }
    st->change(ctx, st->user);
}

static void
textbox_notifySubmit(textbox_state_t *st, e9ui_context_t *ctx)
{
    if (!st) {
        return;
    }
    if (st->submit) {
        st->submit(ctx, st->user);
    } else {
        textbox_notifyChange(st, ctx);
    }
}

static e9ui_component_t *
textbox_findNextTextbox(e9ui_context_t *ctx, e9ui_component_t *self)
{
    if (!self) {
        return NULL;
    }
    e9ui_component_t *root = e9ui_focusTraversalRoot(ctx, self);
    if (!root) {
        return NULL;
    }
    e9ui_component_t *cursor = self;
    for (int i = 0; i < 2048; i++) {
        e9ui_component_t *next = e9ui_focusFindNext(root, cursor, 0);
        if (!next || next == self) {
            return NULL;
        }
        if (next->name && strcmp(next->name, "e9ui_textbox") == 0) {
            return next;
        }
        cursor = next;
    }
    return NULL;
}

static int
textbox_hasSelection(const textbox_state_t *st)
{
    if (!st) {
        return 0;
    }
    return st->sel_start != st->sel_end;
}

static void
textbox_markClear(textbox_state_t *st)
{
    if (!st) {
        return;
    }
    st->markActive = 0;
}

static void
textbox_markSet(textbox_state_t *st)
{
    if (!st) {
        return;
    }
    st->markActive = 1;
    st->markPos = st->cursor;
    st->sel_start = st->cursor;
    st->sel_end = st->cursor;
}

static void
textbox_postMoveSelection(textbox_state_t *st)
{
    if (!st) {
        return;
    }
    if (st->markActive) {
        st->sel_start = st->markPos;
        st->sel_end = st->cursor;
    } else {
        textbox_clearSelection(st);
    }
}

static void
textbox_clearSelection(textbox_state_t *st)
{
    if (!st) {
        return;
    }
    st->sel_start = st->cursor;
    st->sel_end = st->cursor;
    st->selecting = 0;
}

static void
textbox_normalizeSelection(const textbox_state_t *st, int *out_a, int *out_b)
{
    int a = st ? st->sel_start : 0;
    int b = st ? st->sel_end : 0;
    if (a > b) {
        int tmp = a;
        a = b;
        b = tmp;
    }
    if (out_a) {
        *out_a = a;
    }
    if (out_b) {
        *out_b = b;
    }
}

static int
textbox_deleteSelection(textbox_state_t *st)
{
    if (!st || !textbox_hasSelection(st)) {
        return 0;
    }
    int a = 0;
    int b = 0;
    textbox_normalizeSelection(st, &a, &b);
    if (a < 0) {
        a = 0;
    }
    if (b > st->len) {
        b = st->len;
    }
    if (b <= a) {
        textbox_clearSelection(st);
        return 0;
    }
    memmove(&st->text[a], &st->text[b], (size_t)(st->len - b + 1));
    st->len -= (b - a);
    st->cursor = a;
    textbox_clearSelection(st);
    return 1;
}

static int
textbox_parseUnsignedFromSelection(const char *text, uint64_t *outValue)
{
    if (!text || !outValue) {
        return 0;
    }
    const char *p = text;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (!*p) {
        return 0;
    }
    int base = 0;
    if (*p == '$') {
        base = 16;
        p++;
    } else if (strchr(p, 'x') || strchr(p, 'X')) {
        base = 0;
    } else {
        int allDigits = 1;
        int allHexDigits = 1;
        int hasHexAlpha = 0;
        for (const char *q = p; *q && !isspace((unsigned char)*q); ++q) {
            unsigned char ch = (unsigned char)*q;
            if (!isdigit(ch)) {
                allDigits = 0;
            }
            if (!isxdigit(ch)) {
                allHexDigits = 0;
            }
            if ((ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) {
                hasHexAlpha = 1;
            }
        }
        if (allHexDigits && hasHexAlpha && !allDigits) {
            base = 16;
        } else {
            base = 10;
        }
    }
    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(p, &end, base);
    if (errno != 0 || end == p) {
        return 0;
    }
    while (*end && isspace((unsigned char)*end)) {
        end++;
    }
    if (*end != '\0') {
        return 0;
    }
    *outValue = (uint64_t)value;
    return 1;
}

static int
textbox_selectionLooksHex(const char *text)
{
    if (!text) {
        return 0;
    }
    const char *p = text;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (!*p) {
        return 0;
    }
    if (*p == '$') {
        return 1;
    }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        return 1;
    }
    for (const char *q = p; *q && !isspace((unsigned char)*q); ++q) {
        unsigned char ch = (unsigned char)*q;
        if ((ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) {
            return 1;
        }
    }
    return 0;
}

static int
textbox_replaceSelection(textbox_state_t *st,
                         e9ui_context_t *ctx,
                         e9ui_component_t *self,
                         TTF_Font *font,
                         int viewW,
                         const char *replacement,
                         int keepSelected)
{
    if (!st || !replacement || !textbox_hasSelection(st)) {
        return 0;
    }
    int a = 0;
    int b = 0;
    textbox_normalizeSelection(st, &a, &b);
    if (a < 0) {
        a = 0;
    }
    if (b > st->len) {
        b = st->len;
    }
    if (b <= a) {
        return 0;
    }
    int oldLen = b - a;
    int repLen = (int)strlen(replacement);
    int maxRepLen = st->maxLen - (st->len - oldLen);
    if (maxRepLen < 0) {
        maxRepLen = 0;
    }
    if (repLen > maxRepLen) {
        repLen = maxRepLen;
    }
    textbox_recordUndo(st);
    memmove(&st->text[a + repLen], &st->text[b], (size_t)(st->len - b + 1));
    if (repLen > 0) {
        memcpy(&st->text[a], replacement, (size_t)repLen);
    }
    st->len = st->len - oldLen + repLen;
    st->cursor = a + repLen;
    if (keepSelected) {
        st->sel_start = a;
        st->sel_end = a + repLen;
        st->selecting = 0;
    } else {
        textbox_clearSelection(st);
    }
    st->convertApplying = 1;
    textbox_notifyChange(st, ctx);
    st->convertApplying = 0;
    textbox_selectOverlay_openForTyping(ctx, self);
    textbox_updateScroll(st, font, viewW);
    return 1;
}

static int
textbox_convertSelectionBase(textbox_state_t *st,
                             e9ui_context_t *ctx,
                             e9ui_component_t *self,
                             TTF_Font *font,
                             int viewW,
                             SDL_Keycode triggerKey)
{
    if (!st || !textbox_hasSelection(st)) {
        return 0;
    }
    int a = 0;
    int b = 0;
    textbox_normalizeSelection(st, &a, &b);
    if (a < 0) {
        a = 0;
    }
    if (b > st->len) {
        b = st->len;
    }
    if (b <= a) {
        return 0;
    }
    int selLen = b - a;
    char *selection = (char*)alloc_calloc((size_t)selLen + 1, 1);
    if (!selection) {
        return 0;
    }
    memcpy(selection, &st->text[a], (size_t)selLen);
    if (st->convertValid && st->convertKey == triggerKey &&
        st->convertAfter && st->convertBefore &&
        strcmp(selection, st->convertAfter) == 0) {
        int replaced = textbox_replaceSelection(st, ctx, self, font, viewW,
                                                st->convertBefore, 1);
        if (replaced) {
            char *tmp = st->convertBefore;
            st->convertBefore = st->convertAfter;
            st->convertAfter = tmp;
        }
        alloc_free(selection);
        return replaced;
    }
    uint64_t value = 0;
    int parsed = textbox_parseUnsignedFromSelection(selection, &value);
    if (!parsed) {
        alloc_free(selection);
        return 0;
    }
    int toHex = textbox_selectionLooksHex(selection) ? 0 : 1;
    char converted[64];
    if (toHex) {
        snprintf(converted, sizeof(converted), "0x%" PRIX64, value);
    } else {
        snprintf(converted, sizeof(converted), "%" PRIu64, value);
    }
    int replaced = textbox_replaceSelection(st, ctx, self, font, viewW, converted, 1);
    if (replaced) {
        if (st->convertBefore) {
            alloc_free(st->convertBefore);
            st->convertBefore = NULL;
        }
        if (st->convertAfter) {
            alloc_free(st->convertAfter);
            st->convertAfter = NULL;
        }
        st->convertBefore = alloc_strdup(selection);
        st->convertAfter = alloc_strdup(converted);
        st->convertKey = triggerKey;
        st->convertValid = (st->convertBefore && st->convertAfter) ? 1 : 0;
    }
    alloc_free(selection);
    return replaced;
}

static void
textbox_trimSpan(const char **start, const char **end)
{
    if (!start || !end || !*start || !*end) {
        return;
    }
    while (*start < *end && isspace((unsigned char)**start)) {
        (*start)++;
    }
    while (*end > *start && isspace((unsigned char)*((*end) - 1))) {
        (*end)--;
    }
}

static int
textbox_exprPreferHexOutput(const char *expr)
{
    if (!expr || !*expr) {
        return 1;
    }

    int hasHexLiteral = 0;
    int hasDecimalLiteral = 0;
    int hasIdentifier = 0;

    const char *p = expr;
    while (*p) {
        if (isspace((unsigned char)*p)) {
            p++;
            continue;
        }

        if ((*p == '0' && (p[1] == 'x' || p[1] == 'X')) || *p == '$') {
            hasHexLiteral = 1;
            if (*p == '$') {
                p++;
            } else {
                p += 2;
            }
            while (isxdigit((unsigned char)*p)) {
                p++;
            }
            continue;
        }

        if (isdigit((unsigned char)*p)) {
            hasDecimalLiteral = 1;
            while (isdigit((unsigned char)*p)) {
                p++;
            }
            continue;
        }

        if (isalpha((unsigned char)*p) || *p == '_') {
            hasIdentifier = 1;
            while (isalnum((unsigned char)*p) || *p == '_') {
                p++;
            }
            continue;
        }

        p++;
    }

    if (hasHexLiteral) {
        return 1;
    }
    if (hasDecimalLiteral && !hasIdentifier) {
        return 0;
    }
    if (hasIdentifier && !hasDecimalLiteral && !hasHexLiteral) {
        return 1;
    }
    return 1;
}

static int
textbox_evalExtractReplacement(const char *expr, const char *evalOut, char *replacement, size_t cap)
{
    if (!expr || !evalOut || !replacement || cap == 0) {
        return 0;
    }
    replacement[0] = '\0';
    int preferHex = textbox_exprPreferHexOutput(expr);

    const char *lineEnd = strchr(evalOut, '\n');
    if (!lineEnd) {
        lineEnd = evalOut + strlen(evalOut);
    }
    const char *colon = strchr(evalOut, ':');
    if (!colon || colon >= lineEnd) {
        return 0;
    }
    const char *valueStart = colon + 1;
    const char *valueEnd = lineEnd;
    textbox_trimSpan(&valueStart, &valueEnd);
    if (valueEnd <= valueStart) {
        return 0;
    }

    const char *openParen = NULL;
    const char *closeParen = NULL;
    for (const char *p = valueStart; p < valueEnd; ++p) {
        if (*p == '(') {
            openParen = p;
            break;
        }
    }
    if (openParen) {
        for (const char *p = openParen + 1; p < valueEnd; ++p) {
            if (*p == ')') {
                closeParen = p;
                break;
            }
        }
    }
    if (preferHex && openParen && closeParen && closeParen > openParen + 1) {
        const char *hexStart = openParen + 1;
        const char *hexEnd = closeParen;
        textbox_trimSpan(&hexStart, &hexEnd);
        if (hexEnd > hexStart + 2 &&
            hexStart[0] == '0' &&
            (hexStart[1] == 'x' || hexStart[1] == 'X')) {
            size_t outLen = (size_t)(hexEnd - hexStart);
            if (outLen >= cap) {
                outLen = cap - 1;
            }
            memcpy(replacement, hexStart, outLen);
            replacement[outLen] = '\0';
            return replacement[0] != '\0';
        }
    }

    if (openParen) {
        const char *trimEnd = openParen;
        textbox_trimSpan(&valueStart, &trimEnd);
        if (trimEnd > valueStart) {
            valueEnd = trimEnd;
        }
    }
    if (valueEnd <= valueStart) {
        return 0;
    }

    size_t outLen = (size_t)(valueEnd - valueStart);
    if (outLen >= cap) {
        outLen = cap - 1;
    }
    memcpy(replacement, valueStart, outLen);
    replacement[outLen] = '\0';
    return replacement[0] != '\0';
}

static int
textbox_convertSelectionExpression(textbox_state_t *st,
                                   e9ui_context_t *ctx,
                                   e9ui_component_t *self,
                                   TTF_Font *font,
                                   int viewW,
                                   SDL_Keycode triggerKey)
{
    if (!st || !textbox_hasSelection(st)) {
        return 0;
    }

    int a = 0;
    int b = 0;
    textbox_normalizeSelection(st, &a, &b);
    if (a < 0) {
        a = 0;
    }
    if (b > st->len) {
        b = st->len;
    }
    if (b <= a) {
        return 0;
    }

    int selLen = b - a;
    char *selection = (char*)alloc_calloc((size_t)selLen + 1, 1);
    if (!selection) {
        return 0;
    }
    memcpy(selection, &st->text[a], (size_t)selLen);

    if (st->convertValid && st->convertKey == triggerKey &&
        st->convertAfter && st->convertBefore &&
        strcmp(selection, st->convertAfter) == 0) {
        int replaced = textbox_replaceSelection(st, ctx, self, font, viewW,
                                                st->convertBefore, 1);
        if (replaced) {
            char *tmp = st->convertBefore;
            st->convertBefore = st->convertAfter;
            st->convertAfter = tmp;
        }
        alloc_free(selection);
        return replaced;
    }

    char evalOut[1024];
    char replacement[256];
    evalOut[0] = '\0';
    replacement[0] = '\0';
    int ok = e9ui_transformTextboxSelection(ctx,
                                            "eval",
                                            selection,
                                            evalOut,
                                            sizeof(evalOut));
    if (!ok || !textbox_evalExtractReplacement(selection, evalOut, replacement, sizeof(replacement))) {
        alloc_free(selection);
        return 0;
    }

    int replaced = textbox_replaceSelection(st, ctx, self, font, viewW, replacement, 1);
    if (replaced) {
        if (st->convertBefore) {
            alloc_free(st->convertBefore);
            st->convertBefore = NULL;
        }
        if (st->convertAfter) {
            alloc_free(st->convertAfter);
            st->convertAfter = NULL;
        }
        st->convertBefore = alloc_strdup(selection);
        st->convertAfter = alloc_strdup(replacement);
        st->convertKey = triggerKey;
        st->convertValid = (st->convertBefore && st->convertAfter) ? 1 : 0;
    }

    alloc_free(selection);
    return replaced;
}

static void
textbox_snapshot_free(textbox_snapshot_t *snap)
{
    if (!snap) {
        return;
    }
    if (snap->text) {
        alloc_free(snap->text);
    }
    alloc_free(snap);
}

static void
textbox_history_clear(list_t **list)
{
    if (!list) {
        return;
    }
    list_t *ptr = *list;
    while (ptr) {
        list_t *next = ptr->next;
        textbox_snapshot_free((textbox_snapshot_t*)ptr->data);
        alloc_free(ptr);
        ptr = next;
    }
    *list = NULL;
}

static textbox_snapshot_t *
textbox_history_pop(list_t **list)
{
    if (!list || !*list) {
        return NULL;
    }
    list_t *last = list_last(*list);
    if (!last) {
        return NULL;
    }
    textbox_snapshot_t *snap = (textbox_snapshot_t*)last->data;
    list_remove(list, snap, 0);
    return snap;
}

static void
textbox_history_push(list_t **list, textbox_snapshot_t *snap)
{
    if (!list || !snap) {
        return;
    }
    list_append(list, snap);
}

static textbox_snapshot_t *
textbox_snapshot_create(const textbox_state_t *st)
{
    if (!st) {
        return NULL;
    }
    textbox_snapshot_t *snap = (textbox_snapshot_t*)alloc_calloc(1, sizeof(*snap));
    if (!snap) {
        return NULL;
    }
    snap->len = st->len;
    snap->cursor = st->cursor;
    snap->sel_start = st->sel_start;
    snap->sel_end = st->sel_end;
    snap->text = (char*)alloc_calloc((size_t)st->len + 1, 1);
    if (!snap->text) {
        alloc_free(snap);
        return NULL;
    }
    memcpy(snap->text, st->text, (size_t)st->len);
    snap->text[st->len] = '\0';
    return snap;
}

static void
textbox_snapshot_apply(textbox_state_t *st, const textbox_snapshot_t *snap)
{
    if (!st || !snap) {
        return;
    }
    int len = snap->len;
    if (len > st->maxLen) {
        len = st->maxLen;
    }
    memcpy(st->text, snap->text, (size_t)len);
    st->text[len] = '\0';
    st->len = len;
    st->cursor = snap->cursor;
    if (st->cursor < 0) st->cursor = 0;
    if (st->cursor > st->len) st->cursor = st->len;
    st->sel_start = snap->sel_start;
    st->sel_end = snap->sel_end;
    if (st->sel_start < 0) st->sel_start = 0;
    if (st->sel_end < 0) st->sel_end = 0;
    if (st->sel_start > st->len) st->sel_start = st->len;
    if (st->sel_end > st->len) st->sel_end = st->len;
}

static void
textbox_completionClearList(textbox_state_t *st)
{
    if (!st) {
        return;
    }
    if (st->completionList) {
        for (int i = 0; i < st->completionCount; ++i) {
            alloc_free(st->completionList[i]);
        }
        alloc_free(st->completionList);
    }
    st->completionList = NULL;
    st->completionCount = 0;
    st->completionCap = 0;
    st->completionSel = -1;
}

static void
textbox_completionClear(textbox_state_t *st)
{
    if (!st) {
        return;
    }
    textbox_completionClearList(st);
    st->completionPrefixLen = 0;
    if (st->completionPrefix) {
        st->completionPrefix[0] = '\0';
    }
    if (st->completionRest) {
        st->completionRest[0] = '\0';
    }
}

static int
textbox_pathJoin(char *out, size_t cap, const char *dir, const char *name)
{
    if (!out || cap == 0 || !dir || !*dir || !name || !*name) {
        return 0;
    }
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    int needSep = (dlen > 0 && dir[dlen - 1] != '/' && dir[dlen - 1] != '\\');
    size_t total = dlen + (needSep ? 1 : 0) + nlen;
    if (total + 1 > cap) {
        return 0;
    }
    memcpy(out, dir, dlen);
    size_t pos = dlen;
    if (needSep) {
        out[pos++] = platform_preferredPathSeparator();
    }
    memcpy(out + pos, name, nlen);
    out[pos + nlen] = '\0';
    return 1;
}

static int
textbox_expandTilde(const char *in, char *out, size_t cap)
{
    if (!in || !out || cap == 0) {
        return 0;
    }
    if (in[0] != '~' || (in[1] != '\0' && in[1] != '/' && in[1] != '\\')) {
        size_t len = strlen(in);
        if (len >= cap) {
            len = cap - 1;
        }
        memcpy(out, in, len);
        out[len] = '\0';
        return 1;
    }
    char home[PATH_MAX];
    if (!platform_getHomeDir(home, sizeof(home))) {
        size_t len = strlen(in);
        if (len >= cap) {
            len = cap - 1;
        }
        memcpy(out, in, len);
        out[len] = '\0';
        return 1;
    }
    size_t hlen = strlen(home);
    const char *rest = in + 1;
    if (*rest == '/' || *rest == '\\') {
        rest++;
    }
    size_t rlen = strlen(rest);
    size_t needSep = (hlen > 0 && home[hlen - 1] != '/' && home[hlen - 1] != '\\') ? 1 : 0;
    if (hlen + needSep + rlen + 1 > cap) {
        return 0;
    }
    memcpy(out, home, hlen);
    size_t pos = hlen;
    if (needSep) {
        out[pos++] = platform_preferredPathSeparator();
    }
    memcpy(out + pos, rest, rlen);
    out[pos + rlen] = '\0';
    return 1;
}

static int
textbox_startsWith(const char *s, const char *prefix, int caseInsensitive)
{
    if (!s || !prefix) {
        return 0;
    }
    size_t plen = strlen(prefix);
    if (plen == 0) {
        return 1;
    }
    if (strlen(s) < plen) {
        return 0;
    }
    if (!caseInsensitive) {
        return strncmp(s, prefix, plen) == 0;
    }
    for (size_t i = 0; i < plen; ++i) {
        unsigned char a = (unsigned char)s[i];
        unsigned char b = (unsigned char)prefix[i];
        if (tolower(a) != tolower(b)) {
            return 0;
        }
    }
    return 1;
}

static int
textbox_isDirPath(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    struct stat sb;
    if (stat(path, &sb) != 0) {
        return 0;
    }
    return S_ISDIR(sb.st_mode) ? 1 : 0;
}

static size_t
textbox_commonPrefixLen(const char * const *cands, int count, int caseInsensitive)
{
    if (!cands || count <= 0 || !cands[0]) {
        return 0;
    }
    size_t commonLen = strlen(cands[0]);
    for (int i = 1; i < count; ++i) {
        const char *cand = cands[i] ? cands[i] : "";
        size_t j = 0;
        size_t limit = strlen(cand);
        if (limit < commonLen) {
            commonLen = limit;
        }
        while (j < commonLen) {
            unsigned char a = (unsigned char)cands[0][j];
            unsigned char b = (unsigned char)cand[j];
            if (caseInsensitive) {
                a = (unsigned char)tolower(a);
                b = (unsigned char)tolower(b);
            }
            if (a != b) {
                break;
            }
            ++j;
        }
        commonLen = j;
        if (commonLen == 0) {
            break;
        }
    }
    return commonLen;
}

static int
textbox_completionCompare(const void *a, const void *b)
{
    const char *sa = *(const char * const *)a;
    const char *sb = *(const char * const *)b;
    if (!sa) {
        sa = "";
    }
    if (!sb) {
        sb = "";
    }
    if (platform_caseInsensitivePaths()) {
    while (*sa && *sb) {
        unsigned char ca = (unsigned char)tolower((unsigned char)*sa);
        unsigned char cb = (unsigned char)tolower((unsigned char)*sb);
        if (ca != cb) {
            return (ca < cb) ? -1 : 1;
        }
        ++sa;
        ++sb;
    }
    if (*sa == *sb) {
        return 0;
    }
    return *sa ? 1 : -1;
    } else {
    return strcmp(sa, sb);
    }
}

typedef struct textbox_completion_scan_ctx {
    textbox_state_t *state;
    const char *fragment;
    int foldersOnly;
    int caseInsensitive;
} textbox_completion_scan_ctx_t;

static const char *
textbox_basenameFromPath(const char *path)
{
    if (!path || !*path) {
        return "";
    }
    const char *slash = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    const char *base = slash > back ? slash : back;
    return base ? base + 1 : path;
}

static int
textbox_completionAppend(textbox_state_t *st, const char *text)
{
    if (!st || !text) {
        return 0;
    }
    if (st->completionCount >= st->completionCap) {
        int next = st->completionCap ? st->completionCap * 2 : 64;
        char **tmp = (char**)alloc_realloc(st->completionList, (size_t)next * sizeof(char*));
        if (!tmp) {
            return 0;
        }
        st->completionList = tmp;
        st->completionCap = next;
    }
    st->completionList[st->completionCount++] = alloc_strdup(text);
    return 1;
}

static int
textbox_completionScanEntry(const char *path, void *user)
{
    textbox_completion_scan_ctx_t *ctx = (textbox_completion_scan_ctx_t *)user;
    if (!ctx || !ctx->state || !path || !*path) {
        return 1;
    }
    const char *name = textbox_basenameFromPath(path);
    if (!name || !*name) {
        return 1;
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 1;
    }
    int isDir = textbox_isDirPath(path);
    if (ctx->foldersOnly && !isDir) {
        return 1;
    }
    if (ctx->fragment && *ctx->fragment &&
        !textbox_startsWith(name, ctx->fragment, ctx->caseInsensitive)) {
        return 1;
    }
    if (isDir) {
        char entry[PATH_MAX];
        int written = snprintf(entry, sizeof(entry), "%s%c", name, platform_preferredPathSeparator());
        if (written > 0 && (size_t)written < sizeof(entry)) {
            textbox_completionAppend(ctx->state, entry);
        }
    } else {
        textbox_completionAppend(ctx->state, name);
    }
    return 1;
}

static int
textbox_buildFilenameCompletions(textbox_state_t *st, const char *dirPath, const char *fragment, int foldersOnly)
{
    if (!st || !dirPath) {
        return 0;
    }
    textbox_completionClearList(st);

    const char *dir = dirPath;
    if (!*dir) {
        dir = ".";
    }
    textbox_completion_scan_ctx_t scanCtx;
    scanCtx.state = st;
    scanCtx.fragment = fragment;
    scanCtx.foldersOnly = foldersOnly ? 1 : 0;
    scanCtx.caseInsensitive = platform_caseInsensitivePaths();
    if (!platform_scanFolder(dir, textbox_completionScanEntry, &scanCtx)) {
        return 0;
    }

    if (st->completionCount <= 0) {
        textbox_completionClear(st);
        return 0;
    }
    qsort(st->completionList, (size_t)st->completionCount, sizeof(char*), textbox_completionCompare);
    return 1;
}

static int
textbox_applyFilenameCompletionChoice(textbox_state_t *st, e9ui_context_t *ctx, TTF_Font *font, int viewW, const char *choiceText)
{
    if (!st || !choiceText || !st->completionPrefix || !st->completionRest) {
        return 0;
    }
    int prelen = st->completionPrefixLen;
    if (prelen < 0) {
        prelen = 0;
    }
    int maxLen = st->maxLen;
    if (maxLen <= 0) {
        return 0;
    }
    st->scratch[0] = '\0';
    int nl = 0;

    size_t prefixLen = strlen(st->completionPrefix);
    if ((int)prefixLen > maxLen) {
        prefixLen = (size_t)maxLen;
    }
    memcpy(st->scratch, st->completionPrefix, prefixLen);
    nl = (int)prefixLen;
    st->scratch[nl] = '\0';

    size_t clen = strlen(choiceText);
    if (nl + (int)clen > maxLen) {
        clen = (size_t)(maxLen - nl);
    }
    memcpy(st->scratch + nl, choiceText, clen);
    nl += (int)clen;
    st->scratch[nl] = '\0';

    int addSep = 0;
    if (nl < maxLen) {
        char full[PATH_MAX];
        const char *dirForCheck = (st->completionPrefix[0] != '\0') ? st->completionPrefix : ".";
        if (textbox_pathJoin(full, sizeof(full), dirForCheck, choiceText) && textbox_isDirPath(full)) {
            char last = (nl > 0) ? st->scratch[nl - 1] : '\0';
            if (last != '/' && last != '\\') {
                st->scratch[nl++] = platform_preferredPathSeparator();
                st->scratch[nl] = '\0';
                addSep = 1;
            }
        }
    }

    size_t rl = strlen(st->completionRest);
    if (nl + (int)rl > maxLen) {
        rl = (size_t)(maxLen - nl);
    }
    memcpy(st->scratch + nl, st->completionRest, rl);
    nl += (int)rl;
    st->scratch[nl] = '\0';

    textbox_recordUndo(st);
    memcpy(st->text, st->scratch, (size_t)nl + 1);
    st->len = nl;
    st->cursor = prelen + (int)strlen(choiceText) + addSep;
    if (st->cursor > st->len) {
        st->cursor = st->len;
    }
    textbox_clearSelection(st);
    textbox_notifyChange(st, ctx);
    textbox_updateScroll(st, font, viewW);
    return 1;
}

static int
textbox_filenameCompletion(textbox_state_t *st, e9ui_context_t *ctx, TTF_Font *font, int viewW, int reverse)
{
    if (!st || !ctx || !font) {
        return 0;
    }
    if (st->completionMode == e9ui_textbox_completion_none) {
        return 0;
    }

    if (st->completionList && st->completionCount > 0) {
        int total = st->completionCount;
        if (st->completionSel < 0) {
            st->completionSel = reverse ? total - 1 : 0;
        } else {
            int next = st->completionSel + (reverse ? -1 : 1);
            if (next < 0) {
                next = total - 1;
            }
            if (next >= total) {
                next = 0;
            }
            st->completionSel = next;
        }
        const char *cand = st->completionList[st->completionSel] ? st->completionList[st->completionSel] : "";
        return textbox_applyFilenameCompletionChoice(st, ctx, font, viewW, cand);
    }

    const char *text = st->text ? st->text : "";
    int cursor = st->cursor;
    if (cursor < 0) {
        cursor = 0;
    }
    if (cursor > st->len) {
        cursor = st->len;
    }

    int tokenStart = cursor;
    while (tokenStart > 0) {
        char ch = text[tokenStart - 1];
        if (ch == '/' || ch == '\\') {
            break;
        }
        tokenStart--;
    }
    int fragmentLen = cursor - tokenStart;
    if (fragmentLen < 0) {
        fragmentLen = 0;
    }
    if (tokenStart < 0) {
        tokenStart = 0;
    }
    if (tokenStart > st->len) {
        tokenStart = st->len;
    }

    if (!st->completionPrefix || !st->completionRest) {
        return 1;
    }

    char prefixRaw[PATH_MAX];
    size_t pl = (size_t)tokenStart;
    if (pl >= sizeof(prefixRaw)) {
        pl = sizeof(prefixRaw) - 1;
    }
    memcpy(prefixRaw, text, pl);
    prefixRaw[pl] = '\0';

    strncpy(st->completionPrefix, prefixRaw, (size_t)st->maxLen);
    st->completionPrefix[st->maxLen] = '\0';
    st->completionPrefixLen = (int)strlen(st->completionPrefix);

    const char *rest = &text[cursor];
    strncpy(st->completionRest, rest, (size_t)st->maxLen);
    st->completionRest[st->maxLen] = '\0';

    char fragment[PATH_MAX];
    size_t fl = (size_t)fragmentLen;
    if (fl >= sizeof(fragment)) {
        fl = sizeof(fragment) - 1;
    }
    memcpy(fragment, &text[tokenStart], fl);
    fragment[fl] = '\0';

    char dirExpanded[PATH_MAX];
    if (!textbox_expandTilde(prefixRaw, dirExpanded, sizeof(dirExpanded))) {
        strncpy(dirExpanded, prefixRaw, sizeof(dirExpanded) - 1);
        dirExpanded[sizeof(dirExpanded) - 1] = '\0';
    }
    const char *dirToOpen = dirExpanded;
    if (!dirToOpen || !*dirToOpen) {
        dirToOpen = ".";
    }
    int foldersOnly = (st->completionMode == e9ui_textbox_completion_folder) ? 1 : 0;
    if (!textbox_buildFilenameCompletions(st, dirToOpen, fragment, foldersOnly)) {
        return 1;
    }

    int count = st->completionCount;
    int caseInsensitive = platform_caseInsensitivePaths();
    if (count == 1) {
        const char *cand = st->completionList[0] ? st->completionList[0] : "";
        textbox_applyFilenameCompletionChoice(st, ctx, font, viewW, cand);
        textbox_completionClear(st);
        return 1;
    }
    size_t commonLen = textbox_commonPrefixLen((const char * const *)st->completionList, count, caseInsensitive);
    if ((int)commonLen > fragmentLen) {
        char common[PATH_MAX];
        size_t clen = commonLen;
        if (clen >= sizeof(common)) {
            clen = sizeof(common) - 1;
        }
        memcpy(common, st->completionList[0], clen);
        common[clen] = '\0';
        textbox_applyFilenameCompletionChoice(st, ctx, font, viewW, common);
        textbox_completionClear(st);
        return 1;
    }

    st->completionSel = -1;
    return 1;
}

static void
textbox_recordUndo(textbox_state_t *st)
{
    if (!st) {
        return;
    }
    textbox_snapshot_t *snap = textbox_snapshot_create(st);
    if (!snap) {
        return;
    }
    textbox_history_push(&st->undo, snap);
    textbox_history_clear(&st->redo);
}

static void
textbox_doUndo(textbox_state_t *st, e9ui_context_t *ctx, TTF_Font *font, int viewW)
{
    if (!st) {
        return;
    }
    textbox_snapshot_t *snap = textbox_history_pop(&st->undo);
    if (!snap) {
        return;
    }
    textbox_snapshot_t *cur = textbox_snapshot_create(st);
    if (cur) {
        textbox_history_push(&st->redo, cur);
    }
    textbox_snapshot_apply(st, snap);
    textbox_snapshot_free(snap);
    textbox_notifyChange(st, ctx);
    textbox_updateScroll(st, font, viewW);
}

static void
textbox_doRedo(textbox_state_t *st, e9ui_context_t *ctx, TTF_Font *font, int viewW)
{
    if (!st) {
        return;
    }
    textbox_snapshot_t *snap = textbox_history_pop(&st->redo);
    if (!snap) {
        return;
    }
    textbox_snapshot_t *cur = textbox_snapshot_create(st);
    if (cur) {
        textbox_history_push(&st->undo, cur);
    }
    textbox_snapshot_apply(st, snap);
    textbox_snapshot_free(snap);
    textbox_notifyChange(st, ctx);
    textbox_updateScroll(st, font, viewW);
}

static void
textbox_insertText(textbox_state_t *st, const char *text, int len)
{
    if (!st || !text || len <= 0) {
        return;
    }
    const char *src = text;
    if (st->numeric_only) {
        if (!st->scratch) {
            return;
        }
        int out = 0;
        for (int i = 0; i < len; ++i) {
            char c = text[i];
            if (c >= '0' && c <= '9') {
                st->scratch[out++] = c;
            }
        }
        if (out <= 0) {
            return;
        }
        st->scratch[out] = '\0';
        src = st->scratch;
        len = out;
    }
    int space = st->maxLen - st->len;
    if (space <= 0) {
        return;
    }
    if (len > space) {
        len = space;
    }
    memmove(&st->text[st->cursor + len], &st->text[st->cursor], (size_t)(st->len - st->cursor + 1));
    memcpy(&st->text[st->cursor], src, (size_t)len);
    st->len += len;
    st->cursor += len;
    textbox_clearSelection(st);
}
static int
textbox_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)availW;
    TTF_Font *font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : (ctx ? ctx->font : NULL);
    int lh = font ? TTF_FontHeight(font) : 16;
    if (lh <= 0) {
        lh = 16;
    }
    return lh + 12;
}

static void
textbox_layoutComp(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static void
textbox_renderComp(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)self->state;
    if (!st) {
        return;
    }
    SDL_Rect area = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    if (st->frame_visible) {
        SDL_SetRenderDrawColor(ctx->renderer, 30, 30, 34, 255);
        SDL_RenderFillRect(ctx->renderer, &area);
        SDL_Color borderCol = (SDL_Color){80,80,90,255};
        SDL_SetRenderDrawColor(ctx->renderer, borderCol.r, borderCol.g, borderCol.b, borderCol.a);
        SDL_RenderDrawRect(ctx->renderer, &area);
        if (st->focus_border_visible && !self->disabled && e9ui_getFocus(ctx) == self) {
            e9ui_drawFocusRingRect(ctx, area, 1);
        }
    }
    TTF_Font *font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
    if (!font) {
        return;
    }
    const int padPx = 8;
    const int viewW = area.w - padPx * 2;
    if (viewW <= 0) {
        return;
    }
    const char *display = (st->len > 0) ? st->text : (st->placeholder ? st->placeholder : "");
    SDL_Color textCol = st->len > 0 ? (SDL_Color){230,230,230,255} : (SDL_Color){150,150,170,255};
    if (self->disabled) {
        textCol = (SDL_Color){110,110,130,255};
    }
    if (st->len > 0 && st->textColorOverride && !self->disabled) {
        textCol = st->textColor;
    }
    if (st->len > 0) {
        if (st->select_optionCount > 0 && !st->editable) {
            st->scrollX = 0;
        } else {
            textbox_updateScroll(st, font, viewW);
        }
        if (e9ui_getFocus(ctx) == self && textbox_hasSelection(st)) {
            int a = 0;
            int b = 0;
            textbox_normalizeSelection(st, &a, &b);
            if (a < 0) a = 0;
            if (b > st->len) b = st->len;
            if (b > a) {
                textbox_fillScratch(st, a);
                int startPx = 0;
                TTF_SizeText(font, st->scratch, &startPx, NULL);
                textbox_fillScratch(st, b);
                int endPx = 0;
                TTF_SizeText(font, st->scratch, &endPx, NULL);
                int selX1 = area.x + padPx + startPx - st->scrollX;
                int selX2 = area.x + padPx + endPx - st->scrollX;
                if (selX2 < selX1) {
                    int tmp = selX1;
                    selX1 = selX2;
                    selX2 = tmp;
                }
                int clipL = area.x + padPx;
                int clipR = area.x + padPx + viewW;
                if (selX1 < clipL) selX1 = clipL;
                if (selX2 > clipR) selX2 = clipR;
                if (selX2 > selX1) {
                    int lh = TTF_FontHeight(font);
                    if (lh <= 0) lh = 16;
                    int selY = area.y + (area.h - lh) / 2;
                    SDL_Rect sel = { selX1, selY, selX2 - selX1, lh };
                    SDL_SetRenderDrawColor(ctx->renderer, 70, 120, 180, 255);
                    SDL_RenderFillRect(ctx->renderer, &sel);
                }
            }
        }
        textbox_fillScratch(st, st->len);
        int tw = 0;
        int th = 0;
        SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, st->scratch, textCol, &tw, &th);
        if (tex) {
            SDL_Rect src = { st->scrollX, 0, viewW, th };
            if (src.w > tw - src.x) {
                src.w = tw - src.x;
            }
            if (src.w < 0) {
                src.w = 0;
            }
            SDL_Rect dst = { area.x + padPx, area.y + (area.h - th) / 2, src.w, th };
            if (src.w > 0) {
                SDL_RenderCopy(ctx->renderer, tex, &src, &dst);
            }
        }
    } else if (display && *display) {
        int tw = 0;
        int th = 0;
        SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, display, textCol, &tw, &th);
        if (tex) {
            SDL_Rect src = { 0, 0, viewW, th };
            if (src.w > tw) {
                src.w = tw;
            }
            SDL_Rect dst = { area.x + padPx, area.y + (area.h - th) / 2, src.w, th };
            if (src.w > 0) {
                SDL_RenderCopy(ctx->renderer, tex, &src, &dst);
            }
        }
    }
    if (e9ui_getFocus(ctx) == self && st->editable) {
        textbox_fillScratch(st, st->cursor);
        int caretPx = 0;
        TTF_SizeText(font, st->scratch, &caretPx, NULL);
        int caretX = area.x + padPx + caretPx - st->scrollX;
        if (caretX < area.x + padPx) {
            caretX = area.x + padPx;
        }
        if (caretX > area.x + area.w - padPx) {
            caretX = area.x + area.w - padPx;
        }
        int lh = TTF_FontHeight(font);
        if (lh <= 0) {
            lh = 16;
        }
        SDL_SetRenderDrawColor(ctx->renderer, 230, 230, 230, 255);
        SDL_RenderDrawLine(ctx->renderer, caretX, area.y + (area.h - lh) / 2,
                           caretX, area.y + (area.h + lh) / 2);
    }
}

static void
textbox_repositionCursor(textbox_state_t *st, e9ui_component_t *self, TTF_Font *font, int mouseX)
{
    if (!st || !self || !font) {
        return;
    }
    const int padPx = 8;
    int target = mouseX - (self->bounds.x + padPx) + st->scrollX;
    if (target < 0) {
        target = 0;
    }
    int best = 0;
    for (int i = 0; i <= st->len; ++i) {
        textbox_fillScratch(st, i);
        int width = 0;
        TTF_SizeText(font, st->scratch, &width, NULL);
        if (width >= target) {
            st->cursor = i;
            best = 1;
            break;
        }
    }
    if (!best) {
        st->cursor = st->len;
    }
    int viewW = self->bounds.w - padPx * 2;
    textbox_updateScroll(st, font, viewW);
}

static int
textbox_charClass(char ch)
{
    unsigned char uch = (unsigned char)ch;
    if (isalnum(uch) || ch == '_') {
        return 2;
    }
    if (isspace(uch)) {
        return 1;
    }
    return 0;
}

static int
textbox_isWordChar(char ch)
{
    return textbox_charClass(ch) == 2;
}

static void
textbox_moveForwardWord(textbox_state_t *st)
{
    if (!st) {
        return;
    }

    int i = st->cursor;
    if (i < 0) {
        i = 0;
    }

    while (i < st->len && !textbox_isWordChar(st->text[i])) {
        i++;
    }
    while (i < st->len && textbox_isWordChar(st->text[i])) {
        i++;
    }
    st->cursor = i;
}

static void
textbox_moveBackwardWord(textbox_state_t *st)
{
    if (!st) {
        return;
    }

    int i = st->cursor;
    if (i > st->len) {
        i = st->len;
    }

    while (i > 0 && !textbox_isWordChar(st->text[i - 1])) {
        i--;
    }
    while (i > 0 && textbox_isWordChar(st->text[i - 1])) {
        i--;
    }
    st->cursor = i;
}

static void
textbox_selectWordAtCursor(textbox_state_t *st)
{
    if (!st || st->len <= 0) {
        return;
    }

    int idx = st->cursor;
    if (idx >= st->len) {
        idx = st->len - 1;
    }
    if (idx < 0) {
        idx = 0;
    }

    int cls = textbox_charClass(st->text[idx]);
    int start = idx;
    int end = idx + 1;

    while (start > 0 && textbox_charClass(st->text[start - 1]) == cls) {
        start--;
    }
    while (end < st->len && textbox_charClass(st->text[end]) == cls) {
        end++;
    }

    st->sel_start = start;
    st->sel_end = end;
    st->cursor = end;
    st->selecting = 0;
}

static void
textbox_selectLine(textbox_state_t *st)
{
    if (!st) {
        return;
    }
    st->sel_start = 0;
    st->sel_end = st->len;
    st->cursor = st->len;
    st->selecting = 0;
}

static void
textbox_onMouseDown(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *ev)
{
    if (!self || !ctx || !ev) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)self->state;
    if (!st) {
        return;
    }
    if (!st->editable) {
        return;
    }
    if (ev->button != E9UI_MOUSE_BUTTON_LEFT) {
        return;
    }
    textbox_markClear(st);
    TTF_Font *font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
    Uint32 now = e9ui_getTicks(ctx);
    if ((now - st->last_click_ms) <= 350 && abs(ev->x - st->last_click_x) <= 4) {
        st->click_streak++;
    } else {
        st->click_streak = 1;
    }
    st->last_click_ms = now;
    st->last_click_x = ev->x;

    textbox_repositionCursor(st, self, font, ev->x);
    if (st->click_streak == 2) {
        textbox_selectWordAtCursor(st);
        textbox_updateScroll(st, font, self->bounds.w - 8 * 2);
        return;
    }
    if (st->click_streak >= 3) {
        textbox_selectLine(st);
        st->click_streak = 0;
        textbox_updateScroll(st, font, self->bounds.w - 8 * 2);
        return;
    }
    st->sel_start = st->cursor;
    st->sel_end = st->cursor;
    st->selecting = 1;
}

static void
textbox_onMouseMove(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *ev)
{
    if (!self || !ctx || !ev) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)self->state;
    if (!st || !st->editable || !st->selecting) {
        return;
    }
    TTF_Font *font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
    textbox_repositionCursor(st, self, font, ev->x);
    st->sel_end = st->cursor;
}

static void
textbox_onMouseUp(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *ev)
{
    (void)ctx;
    (void)ev;
    if (!self) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)self->state;
    if (!st) {
        return;
    }
    st->selecting = 0;
}

static void
textbox_onClick(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *ev)
{
    if (!self || !ctx || !ev) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)self->state;
    if (!st) {
        return;
    }
    if (ev->button != E9UI_MOUSE_BUTTON_LEFT) {
        return;
    }
    if (st->select_optionCount <= 0) {
        return;
    }
    if (st->suppressSelectToggleClick) {
        st->suppressSelectToggleClick = 0;
        return;
    }
    if (st->editable && st->click_streak > 1) {
        return;
    }
    e9ui_setFocus(ctx, self);
    textbox_selectOverlay_toggle(ctx, self);
}

static int
textbox_handleEventComp(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!self || !ev) {
        return 0;
    }
    textbox_state_t *st = (textbox_state_t*)self->state;
    if (!st) {
        return 0;
    }
    if (!ctx || e9ui_getFocus(ctx) != self) {
        return 0;
    }
    if (ev->type == SDL_KEYDOWN) {
        SDL_Keycode kc = ev->key.keysym.sym;
        SDL_Keymod mods = ev->key.keysym.mod;
        int accel = (mods & KMOD_GUI) || (mods & KMOD_CTRL);
        int shift = (mods & KMOD_SHIFT);
        if (!accel && kc == SDLK_TAB && st->key_cb && st->key_cb(ctx, kc, mods, st->key_user)) {
            return 1;
        }
        if (!accel && kc == SDLK_TAB) {
            if (st->editable) {
                textbox_notifySubmit(st, ctx);
            }
            e9ui_focusAdvance(ctx, self, shift ? 1 : 0);
            return 1;
        }
        if (st->select_optionCount > 0 &&
            (kc == SDLK_RETURN || kc == SDLK_KP_ENTER || kc == SDLK_SPACE)) {
            textbox_selectOverlay_toggle(ctx, self);
            return 1;
        }
        if (st->key_cb && st->key_cb(ctx, kc, mods, st->key_user)) {
            return 1;
        }
    }
    if (!st->editable) {
        return 0;
    }
    TTF_Font *font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
    int viewW = self->bounds.w - 8 * 2;
    if (ev->type == SDL_TEXTINPUT) {
        if (st->suppressNextTextInput) {
            st->suppressNextTextInput = 0;
            return 1;
        }
        textbox_markClear(st);
        textbox_completionClear(st);
        if (!font) {
            return 1;
        }
        const char *text = ev->text.text;
        int len = (int)strlen(text);
        if (len <= 0) {
            return 1;
        }
        int hadSelection = textbox_hasSelection(st);
        int space = st->maxLen - st->len;
        if (!hadSelection && space <= 0) {
            return 1;
        }
        textbox_recordUndo(st);
        if (hadSelection) {
            textbox_deleteSelection(st);
        }
        space = st->maxLen - st->len;
        if (space <= 0) {
            textbox_updateScroll(st, font, viewW);
            return 1;
        }
        if (len > space) {
            len = space;
        }
        memmove(&st->text[st->cursor + len], &st->text[st->cursor], (size_t)(st->len - st->cursor + 1));
        memcpy(&st->text[st->cursor], text, (size_t)len);
        st->len += len;
        st->cursor += len;
        textbox_clearSelection(st);
        textbox_notifyChange(st, ctx);
        textbox_selectOverlay_openForTyping(ctx, self);
        textbox_updateScroll(st, font, viewW);
        return 1;
    }
    if (ev->type != SDL_KEYDOWN) {
        return 0;
    }
    // If a previous shortcut armed suppression but no TEXTINPUT arrived for it,
    // clear stale suppression so we don't swallow the next real typed character.
    st->suppressNextTextInput = 0;
    SDL_Keycode kc = ev->key.keysym.sym;
    SDL_Keymod mods = ev->key.keysym.mod;
    if (kc != SDLK_TAB && kc != SDLK_RIGHT) {
        textbox_completionClear(st);
    }
    int accel = (mods & KMOD_GUI) || (mods & KMOD_CTRL);
    int alt = (mods & KMOD_ALT) ? 1 : 0;
    int shift = (mods & KMOD_SHIFT);
    if (accel && kc == SDLK_z) {
        if (shift) {
            textbox_doRedo(st, ctx, font, viewW);
        } else {
            textbox_doUndo(st, ctx, font, viewW);
        }
        return 1;
    }
    if (accel && kc == SDLK_a) {
        st->cursor = 0;
        textbox_postMoveSelection(st);
        textbox_updateScroll(st, font, viewW);
        return 1;
    }
    if (alt && kc == SDLK_a) {
        st->suppressNextTextInput = 1;
        st->cursor = 0;
        textbox_markClear(st);
        textbox_clearSelection(st);
        st->sel_start = 0;
        st->sel_end = st->len;
        st->cursor = st->len;
        textbox_updateScroll(st, font, viewW);
        return 1;
    }
    int inlineHexShortcut = 0;
    if (textbox_shortcutMatchCb) {
        inlineHexShortcut = textbox_shortcutMatchCb(&ev->key, "hex_convert_inline", textbox_shortcutMatchUser);
    } else if (accel && kc == SDLK_h) {
        inlineHexShortcut = 1;
    }
    if (inlineHexShortcut) {
        if (textbox_convertSelectionBase(st, ctx, self, font, viewW, SDLK_h)) {
            textbox_markClear(st);
            return 1;
        }
    }
    if (accel && kc == SDLK_p) {
        if (textbox_convertSelectionExpression(st, ctx, self, font, viewW, SDLK_p)) {
            textbox_markClear(st);
            return 1;
        }
    }
    if (accel && kc == SDLK_g) {
        textbox_markClear(st);
        textbox_clearSelection(st);
        st->selecting = 0;
        textbox_updateScroll(st, font, viewW);
        return 1;
    }
    if (accel && kc == SDLK_e) {
        st->cursor = st->len;
        textbox_postMoveSelection(st);
        textbox_updateScroll(st, font, viewW);
        return 1;
    }
    if (accel && kc == SDLK_SPACE) {
        st->suppressNextTextInput = 1;
        textbox_markSet(st);
        textbox_updateScroll(st, font, viewW);
        return 1;
    }
    if (alt && kc == SDLK_b) {
        st->suppressNextTextInput = 1;
        textbox_moveBackwardWord(st);
        textbox_postMoveSelection(st);
        textbox_updateScroll(st, font, viewW);
        return 1;
    }
    if (alt && kc == SDLK_f) {
        st->suppressNextTextInput = 1;
        textbox_moveForwardWord(st);
        textbox_postMoveSelection(st);
        textbox_updateScroll(st, font, viewW);
        return 1;
    }
    if (accel && kc == SDLK_b) {
        if (st->cursor > 0) {
            st->cursor--;
        }
        textbox_postMoveSelection(st);
        textbox_updateScroll(st, font, viewW);
        return 1;
    }
    if (accel && kc == SDLK_f) {
        if (st->cursor < st->len) {
            st->cursor++;
        }
        textbox_postMoveSelection(st);
        textbox_updateScroll(st, font, viewW);
        return 1;
    }
    if (accel && kc == SDLK_d) {
        if (textbox_hasSelection(st)) {
            textbox_recordUndo(st);
            textbox_deleteSelection(st);
            textbox_notifyChange(st, ctx);
            textbox_selectOverlay_openForTyping(ctx, self);
            textbox_updateScroll(st, font, viewW);
            return 1;
        }
        if (st->cursor < st->len) {
            textbox_recordUndo(st);
            memmove(&st->text[st->cursor], &st->text[st->cursor + 1], (size_t)(st->len - st->cursor));
            st->len--;
            textbox_notifyChange(st, ctx);
            textbox_selectOverlay_openForTyping(ctx, self);
            textbox_updateScroll(st, font, viewW);
        }
        return 1;
    }
    if (accel && kc == SDLK_k) {
        textbox_markClear(st);
        if (st->cursor < st->len) {
            size_t rem = (size_t)(st->len - st->cursor);
            char *buf = (char*)alloc_calloc(rem + 1, 1);
            if (buf) {
                memcpy(buf, &st->text[st->cursor], rem);
                SDL_SetClipboardText(buf);
                alloc_free(buf);
            }
            textbox_recordUndo(st);
            st->text[st->cursor] = '\0';
            st->len = st->cursor;
            textbox_clearSelection(st);
            textbox_notifyChange(st, ctx);
            textbox_selectOverlay_openForTyping(ctx, self);
            textbox_updateScroll(st, font, viewW);
        }
        return 1;
    }
    if (accel && kc == SDLK_y) {
        textbox_markClear(st);
        if (SDL_HasClipboardText()) {
            char *clip = SDL_GetClipboardText();
            if (clip && *clip) {
                textbox_recordUndo(st);
                if (textbox_hasSelection(st)) {
                    textbox_deleteSelection(st);
                }
                textbox_insertText(st, clip, (int)strlen(clip));
                textbox_notifyChange(st, ctx);
                textbox_selectOverlay_openForTyping(ctx, self);
                textbox_updateScroll(st, font, viewW);
            }
            if (clip) {
                SDL_free(clip);
            }
        }
        return 1;
    }
    if (accel && kc == SDLK_c) {
        if (textbox_hasSelection(st)) {
            int a = 0;
            int b = 0;
            textbox_normalizeSelection(st, &a, &b);
            if (b > a) {
                char *buf = (char*)alloc_calloc((size_t)(b - a + 1), 1);
                if (buf) {
                    memcpy(buf, &st->text[a], (size_t)(b - a));
                    SDL_SetClipboardText(buf);
                    alloc_free(buf);
                }
            }
        }
        return 1;
    }
    if (accel && kc == SDLK_x) {
        textbox_markClear(st);
        if (textbox_hasSelection(st)) {
            textbox_recordUndo(st);
            int a = 0;
            int b = 0;
            textbox_normalizeSelection(st, &a, &b);
            if (b > a) {
                char *buf = (char*)alloc_calloc((size_t)(b - a + 1), 1);
                if (buf) {
                    memcpy(buf, &st->text[a], (size_t)(b - a));
                    SDL_SetClipboardText(buf);
                    alloc_free(buf);
                }
                if (textbox_deleteSelection(st)) {
                    textbox_notifyChange(st, ctx);
                    textbox_selectOverlay_openForTyping(ctx, self);
                    textbox_updateScroll(st, font, viewW);
                }
            }
        }
        return 1;
    }
    if (accel && kc == SDLK_v) {
        textbox_markClear(st);
        if (SDL_HasClipboardText()) {
            char *clip = SDL_GetClipboardText();
            if (clip && *clip) {
                textbox_recordUndo(st);
                if (textbox_deleteSelection(st)) {
                    textbox_notifyChange(st, ctx);
                }
                int len = (int)strlen(clip);
                textbox_insertText(st, clip, len);
                textbox_notifyChange(st, ctx);
                textbox_selectOverlay_openForTyping(ctx, self);
                textbox_updateScroll(st, font, viewW);
            }
            if (clip) {
                SDL_free(clip);
            }
        }
        return 1;
    }
    switch (kc) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (st->submit) {
            st->submit(ctx, st->user);
            return 1;
        }
        textbox_notifySubmit(st, ctx);
        if (st->enterMovesToNextTextbox) {
            e9ui_component_t *nextTextbox = textbox_findNextTextbox(ctx, self);
            if (nextTextbox) {
                e9ui_setFocus(ctx, nextTextbox);
            }
        }
        return 1;
    case SDLK_LEFT:
        if (!st->markActive && textbox_hasSelection(st)) {
            int a = 0;
            int b = 0;
            textbox_normalizeSelection(st, &a, &b);
            st->cursor = a;
            textbox_clearSelection(st);
            textbox_updateScroll(st, font, viewW);
            return 1;
        }
        if (st->cursor > 0) {
            st->cursor--;
        }
        textbox_postMoveSelection(st);
        textbox_updateScroll(st, font, viewW);
        return 1;
    case SDLK_RIGHT:
        if (!accel && st->completionMode != e9ui_textbox_completion_none && st->cursor >= st->len) {
            return textbox_filenameCompletion(st, ctx, font, viewW, 0);
        }
        if (!st->markActive && textbox_hasSelection(st)) {
            int a = 0;
            int b = 0;
            textbox_normalizeSelection(st, &a, &b);
            st->cursor = b;
            textbox_clearSelection(st);
            textbox_updateScroll(st, font, viewW);
            return 1;
        }
        if (st->cursor < st->len) {
            st->cursor++;
        }
        textbox_postMoveSelection(st);
        textbox_updateScroll(st, font, viewW);
        return 1;
    case SDLK_HOME:
        st->cursor = 0;
        textbox_postMoveSelection(st);
        textbox_updateScroll(st, font, viewW);
        return 1;
    case SDLK_END:
        st->cursor = st->len;
        textbox_postMoveSelection(st);
        textbox_updateScroll(st, font, viewW);
        return 1;
    case SDLK_BACKSPACE:
        textbox_markClear(st);
        if (textbox_hasSelection(st)) {
            textbox_recordUndo(st);
            textbox_deleteSelection(st);
            textbox_notifyChange(st, ctx);
            textbox_selectOverlay_openForTyping(ctx, self);
            textbox_updateScroll(st, font, viewW);
            return 1;
        }
        if (st->cursor > 0) {
            textbox_recordUndo(st);
            memmove(&st->text[st->cursor - 1], &st->text[st->cursor], (size_t)(st->len - st->cursor + 1));
            st->cursor--;
            st->len--;
            textbox_notifyChange(st, ctx);
            textbox_selectOverlay_openForTyping(ctx, self);
            textbox_updateScroll(st, font, viewW);
        }
        return 1;
    case SDLK_DELETE:
        textbox_markClear(st);
        if (textbox_hasSelection(st)) {
            textbox_recordUndo(st);
            textbox_deleteSelection(st);
            textbox_notifyChange(st, ctx);
            textbox_updateScroll(st, font, viewW);
            return 1;
        }
        if (st->cursor < st->len) {
            textbox_recordUndo(st);
            memmove(&st->text[st->cursor], &st->text[st->cursor + 1], (size_t)(st->len - st->cursor));
            st->len--;
            textbox_notifyChange(st, ctx);
            textbox_updateScroll(st, font, viewW);
        }
        return 1;
    default:
        break;
    }
    return 0;
}

static void
textbox_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
  (void)ctx;
  if (!self) {
    return;
  }
  textbox_state_t *st = (textbox_state_t*)self->state;
  if (st) {
    e9ui_textbox_selectOverlayCloseForOwner(self);
    textbox_completionClear(st);
    textbox_history_clear(&st->undo);
    textbox_history_clear(&st->redo);
    alloc_free(st->text);
    alloc_free(st->placeholder);
    alloc_free(st->scratch);
    alloc_free(st->select_options);
    alloc_free(st->completionPrefix);
    alloc_free(st->completionRest);
    alloc_free(st->convertBefore);
    alloc_free(st->convertAfter);
  }
}


e9ui_component_t *
e9ui_textbox_make(int maxLen, e9ui_textbox_submit_cb_t onSubmit, e9ui_textbox_change_cb_t onChange, void *user)
{
    if (maxLen <= 0) {
        return NULL;
    }
    e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    if (!comp) {
        return NULL;
    }
    textbox_state_t *st = (textbox_state_t*)alloc_calloc(1, sizeof(textbox_state_t));
    if (!st) {
        alloc_free(comp);
        return NULL;
    }
    st->maxLen = maxLen;
    st->text = (char*)alloc_calloc((size_t)maxLen + 1, 1);
    st->scratch = (char*)alloc_calloc((size_t)maxLen + 1, 1);
    st->completionPrefix = (char*)alloc_calloc((size_t)maxLen + 1, 1);
    st->completionRest = (char*)alloc_calloc((size_t)maxLen + 1, 1);
    st->editable = 1;
    st->sel_start = 0;
    st->sel_end = 0;
    st->selecting = 0;
    st->last_click_ms = 0;
    st->click_streak = 0;
    st->last_click_x = 0;
    st->submit = onSubmit;
    st->change = onChange;
    st->user = user;
    st->frame_visible = 1;
    st->focus_border_visible = 1;
    st->select_options = NULL;
    st->select_optionCount = 0;
    st->select_optionCap = 0;
    st->select_selectedIndex = -1;
    st->select_change = NULL;
    st->select_changeUser = NULL;
    st->textColorOverride = 0;
    st->textColor = (SDL_Color){0,0,0,0};
    st->completionMode = e9ui_textbox_completion_none;
    st->completionList = NULL;
    st->completionCount = 0;
    st->completionCap = 0;
    st->completionSel = -1;
    st->completionPrefixLen = 0;
    if (!st->text || !st->scratch || !st->completionPrefix || !st->completionRest) {
        alloc_free(st->text);
        alloc_free(st->scratch);
        alloc_free(st->completionPrefix);
        alloc_free(st->completionRest);
        alloc_free(st);
        alloc_free(comp);
        return NULL;
    }
    comp->name = "e9ui_textbox";
    comp->state = st;
    comp->focusable = 1;
    comp->preferredHeight = textbox_preferredHeight;
    comp->layout = textbox_layoutComp;
    comp->render = textbox_renderComp;
    comp->handleEvent = textbox_handleEventComp;
    comp->dtor = textbox_dtor;
    comp->onClick = textbox_onClick;
    comp->onMouseDown = textbox_onMouseDown;
    comp->onMouseMove = textbox_onMouseMove;
    comp->onMouseUp = textbox_onMouseUp;
    return comp;
}

void
e9ui_textbox_setText(e9ui_component_t *comp, const char *text)
{
    if (!comp || !comp->state || !text) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    int len = 0;
    if (st->numeric_only) {
        for (const char *p = text; *p && len < st->maxLen; ++p) {
            if (*p >= '0' && *p <= '9') {
                st->text[len++] = *p;
            }
        }
    } else {
        len = (int)strlen(text);
        if (len > st->maxLen) {
            len = st->maxLen;
        }
        memcpy(st->text, text, (size_t)len);
    }
    st->text[len] = '\0';
    st->len = len;
    st->cursor = len;
    textbox_clearSelection(st);
    st->scrollX = 0;
    textbox_completionClear(st);
    textbox_history_clear(&st->undo);
    textbox_history_clear(&st->redo);
}

const char *
e9ui_textbox_getText(const e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return NULL;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    return st->text;
}

int
e9ui_textbox_getCursor(const e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return 0;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    return st->cursor;
}

void
e9ui_textbox_setCursor(e9ui_component_t *comp, int cursor)
{
    if (!comp || !comp->state) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    if (cursor < 0) {
        cursor = 0;
    }
    if (cursor > st->len) {
        cursor = st->len;
    }
    st->cursor = cursor;
    textbox_clearSelection(st);
}

void
e9ui_textbox_setKeyHandler(e9ui_component_t *comp, e9ui_textbox_key_cb_t cb, void *user)
{
    if (!comp || !comp->state) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    st->key_cb = cb;
    st->key_user = user;
}

void
e9ui_textbox_setShortcutMatcher(e9ui_textbox_shortcut_match_cb_t cb, void *user)
{
    textbox_shortcutMatchCb = cb;
    textbox_shortcutMatchUser = user;
}

void *
e9ui_textbox_getUser(const e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return NULL;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    return st->user;
}

void
e9ui_textbox_setPlaceholder(e9ui_component_t *comp, const char *placeholder)
{
    if (!comp || !comp->state) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    alloc_free(st->placeholder);
    if (placeholder && *placeholder) {
        st->placeholder = alloc_strdup(placeholder);
    } else {
        st->placeholder = NULL;
    }
}

void
e9ui_textbox_setFrameVisible(e9ui_component_t *comp, int visible)
{
    if (!comp || !comp->state) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    st->frame_visible = visible ? 1 : 0;
}

void
e9ui_textbox_setFocusBorderVisible(e9ui_component_t *comp, int visible)
{
    if (!comp || !comp->state) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    st->focus_border_visible = visible ? 1 : 0;
}

void
e9ui_textbox_setEditable(e9ui_component_t *comp, int editable)
{
    if (!comp || !comp->state) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    st->editable = editable ? 1 : 0;
}

int
e9ui_textbox_isEditable(const e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return 0;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    return st->editable;
}

void
e9ui_textbox_setNumericOnly(e9ui_component_t *comp, int numeric_only)
{
    if (!comp || !comp->state) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    st->numeric_only = numeric_only ? 1 : 0;
    if (st->numeric_only && st->text) {
        int len = 0;
        for (int i = 0; i < st->len; ++i) {
            char c = st->text[i];
            if (c >= '0' && c <= '9') {
                st->text[len++] = c;
            }
        }
        st->text[len] = '\0';
        st->len = len;
        if (st->cursor > st->len) {
            st->cursor = st->len;
        }
        textbox_clearSelection(st);
    }
}

void
e9ui_textbox_setCompletionMode(e9ui_component_t *comp, e9ui_textbox_completion_mode_t mode)
{
    if (!comp || !comp->state) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    st->completionMode = mode;
    textbox_completionClear(st);
}

e9ui_textbox_completion_mode_t
e9ui_textbox_getCompletionMode(const e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return e9ui_textbox_completion_none;
    }
    const textbox_state_t *st = (const textbox_state_t*)comp->state;
    return st->completionMode;
}

void
e9ui_textbox_setEnterMovesToNextTextbox(e9ui_component_t *comp, int enabled)
{
    if (!comp || !comp->state) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    st->enterMovesToNextTextbox = enabled ? 1 : 0;
}

void
e9ui_textbox_setReadOnly(e9ui_component_t *comp, int readonly)
{
    e9ui_textbox_setEditable(comp, readonly ? 0 : 1);
}

int
e9ui_textbox_isReadOnly(const e9ui_component_t *comp)
{
    return e9ui_textbox_isEditable(comp) ? 0 : 1;
}

void
e9ui_textbox_setOptions(e9ui_component_t *comp, const e9ui_textbox_option_t *options, int optionCount)
{
    if (!comp || !comp->state) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;

    const char *prevValue = textbox_select_selectedValue(st);
    alloc_free(st->select_options);
    st->select_options = NULL;
    st->select_optionCount = 0;
    st->select_optionCap = 0;
    st->select_selectedIndex = -1;

    if (!options || optionCount <= 0) {
        e9ui_textbox_selectOverlayCloseForOwner(comp);
        return;
    }

    st->select_options = (e9ui_textbox_option_t*)alloc_calloc((size_t)optionCount, sizeof(*st->select_options));
    if (!st->select_options) {
        return;
    }
    memcpy(st->select_options, options, (size_t)optionCount * sizeof(*st->select_options));
    st->select_optionCount = optionCount;
    st->select_optionCap = optionCount;

    if (prevValue && *prevValue) {
        int idx = textbox_select_findIndex(st, prevValue);
        if (idx >= 0) {
            st->select_selectedIndex = idx;
            textbox_select_applyDisplay(st, textbox_select_displayLabel(&st->select_options[idx]));
        }
    }
}

void
e9ui_textbox_setSelectedValue(e9ui_component_t *comp, const char *value)
{
    if (!comp || !comp->state || !value) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    int idx = textbox_select_findIndex(st, value);
    if (idx < 0) {
        return;
    }
    st->select_selectedIndex = idx;
    textbox_select_applyDisplay(st, textbox_select_displayLabel(&st->select_options[idx]));
}

const char *
e9ui_textbox_getSelectedValue(const e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return NULL;
    }
    const textbox_state_t *st = (const textbox_state_t*)comp->state;
    const char *value = textbox_select_selectedValue(st);
    return value ? value : (st->text ? st->text : NULL);
}

void
e9ui_textbox_setOnOptionSelected(e9ui_component_t *comp, e9ui_textbox_option_change_cb_t cb, void *user)
{
    if (!comp || !comp->state) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    st->select_change = cb;
    st->select_changeUser = user;
}

void
e9ui_textbox_setTextColor(e9ui_component_t *comp, int enabled, SDL_Color color)
{
    if (!comp || !comp->state) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    st->textColorOverride = enabled ? 1 : 0;
    st->textColor = color;
}

void
e9ui_textbox_clearSelectionExternal(e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    textbox_clearSelection(st);
}

int
e9ui_textbox_getSelectedText(const e9ui_component_t *comp, char *dst, int dstLen)
{
    if (dst && dstLen > 0) {
        dst[0] = '\0';
    }
    if (!comp || !comp->state) {
        return 0;
    }
    const textbox_state_t *st = (const textbox_state_t*)comp->state;
    if (!st->text || !textbox_hasSelection(st)) {
        return 0;
    }
    int a = 0;
    int b = 0;
    textbox_normalizeSelection(st, &a, &b);
    if (a < 0) {
        a = 0;
    }
    if (b > st->len) {
        b = st->len;
    }
    if (b <= a) {
        return 0;
    }
    int len = b - a;
    if (dst && dstLen > 0) {
        int copyLen = len;
        if (copyLen >= dstLen) {
            copyLen = dstLen - 1;
        }
        if (copyLen > 0) {
            memcpy(dst, &st->text[a], (size_t)copyLen);
        }
        dst[copyLen] = '\0';
        return copyLen;
    }
    return len;
}

void
e9ui_textbox_selectAllExternal(e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    st->sel_start = 0;
    st->sel_end = st->len;
    st->cursor = st->len;
    st->selecting = 0;
}

typedef struct textbox_select_overlay_state {
    int open;
    e9ui_component_t *owner;
    int hoverOptionIndex;
    int scrollIndex;
    int pressActive;
    int pressOptionIndex;
    int justOpened;
    int useFilter;
} textbox_select_overlay_state_t;

static textbox_select_overlay_state_t textbox_select_overlay = {0};

static const char *
textbox_selectOverlay_filterText(const textbox_state_t *st)
{
    if (!st || !st->editable || !st->text || !textbox_select_overlay.useFilter) {
        return "";
    }
    return st->text;
}

static int
textbox_selectOverlay_pointInRect(const SDL_Rect *r, int x, int y)
{
    if (!r) {
        return 0;
    }
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

static int
textbox_selectOverlay_computeLayout(e9ui_context_t *ctx, SDL_Rect *outRect, int *outItemH, int *outVisibleCount)
{
    if (!ctx || !outRect || !outItemH || !outVisibleCount) {
        return 0;
    }
    if (!textbox_select_overlay.open || !textbox_select_overlay.owner) {
        return 0;
    }
    e9ui_component_t *owner = textbox_select_overlay.owner;
    textbox_state_t *st = (textbox_state_t*)owner->state;
    if (!st || !st->select_options || st->select_optionCount <= 0) {
        return 0;
    }
    const char *filter = textbox_selectOverlay_filterText(st);
    int filteredCount = textbox_select_filteredCount(st, filter);
    if (filteredCount <= 0) {
        return 0;
    }

    TTF_Font *font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
    int lh = font ? TTF_FontHeight(font) : 16;
    if (lh <= 0) {
        lh = 16;
    }
    int itemH = lh + 12;
    if (itemH < 1) {
        itemH = 1;
    }
    int outerPad = e9ui_scale_px(ctx, 4);
    if (outerPad < 0) {
        outerPad = 0;
    }

    int below = ctx->winH - (owner->bounds.y + owner->bounds.h);
    int above = owner->bounds.y;
    int useBelow = 1;
    int desiredH = filteredCount * itemH + outerPad * 2;
    if (below < desiredH && above > below) {
        useBelow = 0;
    }

    int avail = useBelow ? below : above;
    int visible = (avail - outerPad * 2) / itemH;
    if (visible < 1) {
        visible = 1;
    }
    if (visible > filteredCount) {
        visible = filteredCount;
    }

    int menuH = visible * itemH + outerPad * 2;
    SDL_Rect r = { owner->bounds.x, 0, owner->bounds.w, menuH };
    if (useBelow) {
        r.y = owner->bounds.y + owner->bounds.h;
        if (r.y + r.h > ctx->winH) {
            r.h = ctx->winH - r.y;
            visible = (r.h - outerPad * 2) / itemH;
            if (visible < 1) {
                visible = 1;
            }
            r.h = visible * itemH + outerPad * 2;
        }
    } else {
        r.y = owner->bounds.y - menuH;
        if (r.y < 0) {
            r.y = 0;
            visible = (owner->bounds.y - outerPad * 2) / itemH;
            if (visible < 1) {
                visible = 1;
            }
            if (visible > filteredCount) {
                visible = filteredCount;
            }
            r.h = visible * itemH + outerPad * 2;
            r.y = owner->bounds.y - r.h;
            if (r.y < 0) {
                r.y = 0;
            }
        }
    }

    *outRect = r;
    *outItemH = itemH;
    *outVisibleCount = visible;
    return 1;
}

static int
textbox_selectOverlay_pointInOwner(const e9ui_component_t *owner, int x, int y)
{
    if (!owner) {
        return 0;
    }
    SDL_Rect r = { owner->bounds.x, owner->bounds.y, owner->bounds.w, owner->bounds.h };
    return textbox_selectOverlay_pointInRect(&r, x, y);
}

static void
textbox_selectOverlay_close(void)
{
    textbox_select_overlay.open = 0;
    textbox_select_overlay.owner = NULL;
    textbox_select_overlay.hoverOptionIndex = -1;
    textbox_select_overlay.scrollIndex = 0;
    textbox_select_overlay.pressActive = 0;
    textbox_select_overlay.pressOptionIndex = -1;
    textbox_select_overlay.justOpened = 0;
    textbox_select_overlay.useFilter = 0;
}

void
e9ui_textbox_selectOverlayCloseForOwner(const e9ui_component_t *owner)
{
    if (!textbox_select_overlay.open) {
        return;
    }
    if (!owner || textbox_select_overlay.owner == owner) {
        textbox_selectOverlay_close();
    }
}

static void
textbox_selectOverlay_toggle(e9ui_context_t *ctx, e9ui_component_t *owner)
{
    (void)ctx;
    if (!owner) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)owner->state;
    if (!st || !st->select_options || st->select_optionCount <= 0) {
        return;
    }

    if (textbox_select_overlay.open && textbox_select_overlay.owner == owner) {
        textbox_selectOverlay_close();
        return;
    }

    const char *filter = "";
    int filteredCount = textbox_select_filteredCount(st, filter);
    if (filteredCount <= 0) {
        return;
    }

    textbox_select_overlay.open = 1;
    textbox_select_overlay.owner = owner;
    textbox_select_overlay.hoverOptionIndex = st->select_selectedIndex;
    if (textbox_select_overlay.hoverOptionIndex < 0 ||
        textbox_select_filteredOptionToNthIndex(st, filter, textbox_select_overlay.hoverOptionIndex) < 0) {
        textbox_select_overlay.hoverOptionIndex = textbox_select_filteredNthToOptionIndex(st, filter, 0);
    }
    textbox_select_overlay.scrollIndex = 0;
    textbox_select_overlay.pressActive = 0;
    textbox_select_overlay.pressOptionIndex = -1;
    textbox_select_overlay.justOpened = 1;
    textbox_select_overlay.useFilter = 0;
}

static void
textbox_selectOverlay_openForTyping(e9ui_context_t *ctx, e9ui_component_t *owner)
{
    if (!ctx || !owner || !owner->state) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)owner->state;
    if (!st->editable || st->select_optionCount <= 0) {
        return;
    }
    if (!textbox_select_overlay.open || textbox_select_overlay.owner != owner) {
        if (textbox_select_overlay.open && textbox_select_overlay.owner != owner) {
            textbox_selectOverlay_close();
        }
        textbox_selectOverlay_toggle(ctx, owner);
    }
    if (textbox_select_overlay.open && textbox_select_overlay.owner == owner) {
        textbox_select_overlay.useFilter = 1;
    }
}

static void
textbox_selectOverlay_clampScroll(const textbox_state_t *st, int visibleCount, const char *filter)
{
    if (!st) {
        textbox_select_overlay.scrollIndex = 0;
        return;
    }
    int filteredCount = textbox_select_filteredCount(st, filter);
    int maxScroll = filteredCount - visibleCount;
    if (maxScroll < 0) {
        maxScroll = 0;
    }
    if (textbox_select_overlay.scrollIndex < 0) {
        textbox_select_overlay.scrollIndex = 0;
    }
    if (textbox_select_overlay.scrollIndex > maxScroll) {
        textbox_select_overlay.scrollIndex = maxScroll;
    }
}

static void
textbox_selectOverlay_ensureIndexVisible(const textbox_state_t *st, int visibleCount, const char *filter, int optionIndex)
{
    if (!st || visibleCount <= 0) {
        return;
    }
    int filteredIndex = textbox_select_filteredOptionToNthIndex(st, filter, optionIndex);
    if (filteredIndex < 0) {
        return;
    }
    if (filteredIndex < textbox_select_overlay.scrollIndex) {
        textbox_select_overlay.scrollIndex = filteredIndex;
    } else if (filteredIndex >= textbox_select_overlay.scrollIndex + visibleCount) {
        textbox_select_overlay.scrollIndex = filteredIndex - visibleCount + 1;
    }
    textbox_selectOverlay_clampScroll(st, visibleCount, filter);
}

static void
textbox_selectOverlay_selectIndex(e9ui_context_t *ctx, e9ui_component_t *owner, int index)
{
    if (!ctx || !owner) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)owner->state;
    if (!st || !st->select_options || st->select_optionCount <= 0) {
        return;
    }
    if (index < 0 || index >= st->select_optionCount) {
        return;
    }
    if (st->select_selectedIndex == index) {
        textbox_select_applyDisplay(st, textbox_select_displayLabel(&st->select_options[index]));
        return;
    }
    st->select_selectedIndex = index;
    textbox_select_applyDisplay(st, textbox_select_displayLabel(&st->select_options[index]));
    textbox_notifyChange(st, ctx);
    textbox_select_notify(st, ctx, owner);
}

int
e9ui_textbox_selectOverlayHandleEvent(e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!ctx || !ev) {
        return 0;
    }
    if (!textbox_select_overlay.open || !textbox_select_overlay.owner) {
        return 0;
    }
    e9ui_component_t *owner = textbox_select_overlay.owner;
    textbox_state_t *st = (textbox_state_t*)owner->state;
    if (!st || !st->select_options || st->select_optionCount <= 0) {
        textbox_selectOverlay_close();
        return 0;
    }
    const char *filter = textbox_selectOverlay_filterText(st);
    int filteredCount = textbox_select_filteredCount(st, filter);
    if (filteredCount <= 0) {
        textbox_selectOverlay_close();
        return 0;
    }

    SDL_Rect rect = {0};
    int itemH = 0;
    int visibleCount = 0;
    if (!textbox_selectOverlay_computeLayout(ctx, &rect, &itemH, &visibleCount)) {
        textbox_selectOverlay_close();
        return 0;
    }
    int outerPad = e9ui_scale_px(ctx, 4);
    if (outerPad < 0) {
        outerPad = 0;
    }
    textbox_selectOverlay_clampScroll(st, visibleCount, filter);
    if (textbox_select_overlay.hoverOptionIndex < 0 ||
        textbox_select_filteredOptionToNthIndex(st, filter, textbox_select_overlay.hoverOptionIndex) < 0) {
        textbox_select_overlay.hoverOptionIndex = textbox_select_filteredNthToOptionIndex(st, filter, 0);
    }
    textbox_selectOverlay_ensureIndexVisible(st, visibleCount, filter, textbox_select_overlay.hoverOptionIndex);

    if (ev->type == SDL_MOUSEMOTION) {
        int inside = textbox_selectOverlay_pointInRect(&rect, ev->motion.x, ev->motion.y);
        if (inside) {
            int relY = ev->motion.y - (rect.y + outerPad);
            if (relY < 0 || relY >= visibleCount * itemH) {
                return 1;
            }
            int row = (itemH > 0) ? (relY / itemH) : 0;
            int filteredIdx = textbox_select_overlay.scrollIndex + row;
            int optionIdx = textbox_select_filteredNthToOptionIndex(st, filter, filteredIdx);
            if (optionIdx >= 0) {
                textbox_select_overlay.hoverOptionIndex = optionIdx;
            }
        }
        return 1;
    }

    if (ev->type == SDL_MOUSEWHEEL) {
        if (filteredCount > visibleCount) {
            int hoverFilteredIdx = textbox_select_filteredOptionToNthIndex(
                st, filter, textbox_select_overlay.hoverOptionIndex);
            if (hoverFilteredIdx < 0) {
                hoverFilteredIdx = textbox_select_overlay.scrollIndex;
            }
            if (ev->wheel.y > 0) {
                textbox_select_overlay.scrollIndex += 1;
                hoverFilteredIdx += 1;
            } else if (ev->wheel.y < 0) {
                textbox_select_overlay.scrollIndex -= 1;
                hoverFilteredIdx -= 1;
            }
            textbox_selectOverlay_clampScroll(st, visibleCount, filter);
            if (hoverFilteredIdx < textbox_select_overlay.scrollIndex) {
                hoverFilteredIdx = textbox_select_overlay.scrollIndex;
            }
            int lastVisible = textbox_select_overlay.scrollIndex + visibleCount - 1;
            if (hoverFilteredIdx > lastVisible) {
                hoverFilteredIdx = lastVisible;
            }
            int optionIdx = textbox_select_filteredNthToOptionIndex(st, filter, hoverFilteredIdx);
            if (optionIdx >= 0) {
                textbox_select_overlay.hoverOptionIndex = optionIdx;
            }
        }
        return 1;
    }

    if (ev->type == SDL_MOUSEBUTTONDOWN) {
        if (ev->button.button != SDL_BUTTON_LEFT) {
            return 1;
        }
        int insideMenu = textbox_selectOverlay_pointInRect(&rect, ev->button.x, ev->button.y);
        if (!insideMenu) {
            if (textbox_selectOverlay_pointInOwner(owner, ev->button.x, ev->button.y)) {
                st->suppressSelectToggleClick = 1;
                textbox_selectOverlay_close();
                return 0;
            }
            textbox_selectOverlay_close();
            return 1;
        }
        int relY = ev->button.y - (rect.y + outerPad);
        if (relY < 0 || relY >= visibleCount * itemH) {
            return 1;
        }
        int row = (itemH > 0) ? (relY / itemH) : 0;
        int filteredIdx = textbox_select_overlay.scrollIndex + row;
        int optionIdx = textbox_select_filteredNthToOptionIndex(st, filter, filteredIdx);
        if (optionIdx < 0) {
            return 1;
        }
        textbox_select_overlay.hoverOptionIndex = optionIdx;
        textbox_select_overlay.pressActive = 1;
        textbox_select_overlay.pressOptionIndex = optionIdx;
        return 1;
    }

    if (ev->type == SDL_MOUSEBUTTONUP) {
        if (ev->button.button != SDL_BUTTON_LEFT) {
            textbox_select_overlay.pressActive = 0;
            textbox_select_overlay.pressOptionIndex = -1;
            return 1;
        }
        if (textbox_select_overlay.justOpened && !textbox_select_overlay.pressActive) {
            textbox_select_overlay.justOpened = 0;
            return 1;
        }
        int insideMenu = textbox_selectOverlay_pointInRect(&rect, ev->button.x, ev->button.y);
        if (textbox_select_overlay.pressActive && insideMenu) {
            int relY = ev->button.y - (rect.y + outerPad);
            if (relY < 0 || relY >= visibleCount * itemH) {
                textbox_select_overlay.pressActive = 0;
                textbox_select_overlay.pressOptionIndex = -1;
                textbox_selectOverlay_close();
                return 1;
            }
            int row = (itemH > 0) ? (relY / itemH) : 0;
            int filteredIdx = textbox_select_overlay.scrollIndex + row;
            int optionIdx = textbox_select_filteredNthToOptionIndex(st, filter, filteredIdx);
            if (optionIdx >= 0 && optionIdx == textbox_select_overlay.pressOptionIndex) {
                textbox_selectOverlay_selectIndex(ctx, owner, optionIdx);
            }
        }
        textbox_select_overlay.pressActive = 0;
        textbox_select_overlay.pressOptionIndex = -1;
        textbox_selectOverlay_close();
        return 1;
    }

    if (ev->type == SDL_KEYDOWN) {
        SDL_Keycode kc = ev->key.keysym.sym;
        if (kc == SDLK_TAB) {
            textbox_selectOverlay_close();
            e9ui_setFocus(ctx, owner);
            if (owner->handleEvent) {
                (void)owner->handleEvent(owner, ctx, ev);
            }
            return 1;
        }
        if (kc == SDLK_ESCAPE) {
            textbox_selectOverlay_close();
            return 1;
        }
        if (kc == SDLK_UP) {
            int filteredIdx = textbox_select_filteredOptionToNthIndex(st, filter, textbox_select_overlay.hoverOptionIndex);
            if (filteredIdx < 0) {
                filteredIdx = 0;
            } else if (filteredIdx > 0) {
                filteredIdx -= 1;
            }
            int optionIdx = textbox_select_filteredNthToOptionIndex(st, filter, filteredIdx);
            if (optionIdx >= 0) {
                textbox_select_overlay.hoverOptionIndex = optionIdx;
            }
            textbox_selectOverlay_ensureIndexVisible(st, visibleCount, filter, textbox_select_overlay.hoverOptionIndex);
            return 1;
        }
        if (kc == SDLK_DOWN) {
            int filteredIdx = textbox_select_filteredOptionToNthIndex(st, filter, textbox_select_overlay.hoverOptionIndex);
            if (filteredIdx < 0) {
                filteredIdx = 0;
            } else if (filteredIdx + 1 < filteredCount) {
                filteredIdx += 1;
            }
            int optionIdx = textbox_select_filteredNthToOptionIndex(st, filter, filteredIdx);
            if (optionIdx >= 0) {
                textbox_select_overlay.hoverOptionIndex = optionIdx;
            }
            textbox_selectOverlay_ensureIndexVisible(st, visibleCount, filter, textbox_select_overlay.hoverOptionIndex);
            return 1;
        }
        if (kc == SDLK_RETURN || kc == SDLK_KP_ENTER) {
            if (textbox_select_overlay.hoverOptionIndex >= 0 &&
                textbox_select_overlay.hoverOptionIndex < st->select_optionCount) {
                textbox_selectOverlay_selectIndex(ctx, owner, textbox_select_overlay.hoverOptionIndex);
            }
            textbox_selectOverlay_close();
            return 1;
        }
        if (st->editable) {
            textbox_select_overlay.useFilter = 1;
            e9ui_setFocus(ctx, owner);
            if (owner->handleEvent) {
                (void)owner->handleEvent(owner, ctx, ev);
            }
            return 1;
        }
        return 1;
    }

    if (ev->type == SDL_TEXTINPUT) {
        if (st->editable) {
            textbox_select_overlay.useFilter = 1;
            e9ui_setFocus(ctx, owner);
            if (owner->handleEvent) {
                (void)owner->handleEvent(owner, ctx, ev);
            }
            return 1;
        }
        return 1;
    }

    return 0;
}

void
e9ui_textbox_selectOverlayRender(e9ui_context_t *ctx)
{
    if (!ctx || !ctx->renderer) {
        return;
    }
    if (!textbox_select_overlay.open || !textbox_select_overlay.owner) {
        return;
    }
    e9ui_component_t *owner = textbox_select_overlay.owner;
    if (e9ui_getFocus(ctx) != owner) {
        textbox_selectOverlay_close();
        return;
    }
    textbox_state_t *st = (textbox_state_t*)owner->state;
    if (!st || !st->select_options || st->select_optionCount <= 0) {
        textbox_selectOverlay_close();
        return;
    }
    const char *filter = textbox_selectOverlay_filterText(st);
    int filteredCount = textbox_select_filteredCount(st, filter);
    if (filteredCount <= 0) {
        textbox_selectOverlay_close();
        return;
    }

    SDL_Rect rect = {0};
    int itemH = 0;
    int visibleCount = 0;
    if (!textbox_selectOverlay_computeLayout(ctx, &rect, &itemH, &visibleCount)) {
        textbox_selectOverlay_close();
        return;
    }
    int outerPad = e9ui_scale_px(ctx, 4);
    if (outerPad < 0) {
        outerPad = 0;
    }
    textbox_selectOverlay_clampScroll(st, visibleCount, filter);
    if (textbox_select_overlay.hoverOptionIndex < 0 ||
        textbox_select_filteredOptionToNthIndex(st, filter, textbox_select_overlay.hoverOptionIndex) < 0) {
        if (st->select_selectedIndex >= 0 &&
            textbox_select_filteredOptionToNthIndex(st, filter, st->select_selectedIndex) >= 0) {
            textbox_select_overlay.hoverOptionIndex = st->select_selectedIndex;
        } else {
            textbox_select_overlay.hoverOptionIndex = textbox_select_filteredNthToOptionIndex(st, filter, 0);
        }
    }
    textbox_selectOverlay_ensureIndexVisible(st, visibleCount, filter, textbox_select_overlay.hoverOptionIndex);

    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ctx->renderer, 30, 30, 34, 255);
    SDL_RenderFillRect(ctx->renderer, &rect);
    SDL_SetRenderDrawColor(ctx->renderer, 80, 80, 90, 255);
    SDL_RenderDrawRect(ctx->renderer, &rect);

    TTF_Font *font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
    if (!font) {
        return;
    }

    SDL_bool hadClip = SDL_RenderIsClipEnabled(ctx->renderer);
    SDL_Rect prevClip = {0};
    if (hadClip) {
        SDL_RenderGetClipRect(ctx->renderer, &prevClip);
    }
    SDL_Rect listClip = {
        rect.x + 1,
        rect.y + outerPad,
        rect.w - 2,
        rect.h - (outerPad * 2)
    };
    if (listClip.w > 0 && listClip.h > 0) {
        SDL_RenderSetClipRect(ctx->renderer, &listClip);
    } else {
        SDL_RenderSetClipRect(ctx->renderer, NULL);
    }

    const int padPx = 8;
    for (int row = 0; row < visibleCount; ++row) {
        int filteredIdx = textbox_select_overlay.scrollIndex + row;
        int idx = textbox_select_filteredNthToOptionIndex(st, filter, filteredIdx);
        if (idx < 0 || idx >= st->select_optionCount) {
            continue;
        }
        SDL_Rect item = { rect.x, rect.y + outerPad + row * itemH, rect.w, itemH };
        int isHover = (idx == textbox_select_overlay.hoverOptionIndex) ? 1 : 0;
        int isSelected = (idx == st->select_selectedIndex) ? 1 : 0;
        if (isHover) {
            SDL_SetRenderDrawColor(ctx->renderer, 52, 52, 60, 255);
            SDL_RenderFillRect(ctx->renderer, &item);
        } else if (isSelected) {
            SDL_SetRenderDrawColor(ctx->renderer, 44, 60, 80, 255);
            SDL_RenderFillRect(ctx->renderer, &item);
        }

        const char *label = textbox_select_displayLabel(&st->select_options[idx]);
        SDL_Color textCol = (SDL_Color){230, 230, 230, 255};
        int tw = 0;
        int th = 0;
        SDL_Texture *tex = e9ui_text_cache_getUTF8(ctx->renderer, font, label ? label : "", textCol, &tw, &th);
        if (tex) {
            int tx = item.x + padPx;
            int ty = item.y + (item.h - th) / 2;
            if (ty < item.y) {
                ty = item.y;
            }
            int maxTextW = item.w - padPx * 2;
            if (maxTextW < 0) {
                maxTextW = 0;
            }
            if (st->editable && tw > maxTextW && maxTextW > 0) {
                SDL_Rect src = { tw - maxTextW, 0, maxTextW, th };
                SDL_Rect dst = { tx, ty, maxTextW, th };
                SDL_RenderCopy(ctx->renderer, tex, &src, &dst);
            } else {
                SDL_Rect dst = { tx, ty, tw, th };
                SDL_RenderCopy(ctx->renderer, tex, NULL, &dst);
            }
        }
    }

    if (hadClip) {
        SDL_RenderSetClipRect(ctx->renderer, &prevClip);
    } else {
        SDL_RenderSetClipRect(ctx->renderer, NULL);
    }
}
