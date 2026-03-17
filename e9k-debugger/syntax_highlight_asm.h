/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stddef.h>

#include "syntax_highlight.h"

typedef int (*syntax_highlight_asm_add_span_fn)(void *user, int lineIndex, int startColumn, int length,
                                                syntax_highlight_kind_t kind);

int
syntax_highlight_asm_buildLineSpans(const char *line,
                                    int lineLength,
                                    int lineIndex,
                                    syntax_highlight_asm_add_span_fn addSpanFn,
                                    void *addSpanUser);

int
syntax_highlight_asm_buildSpans(const char *text,
                                size_t textLength,
                                const size_t *lineStarts,
                                int lineCount,
                                syntax_highlight_asm_add_span_fn addSpanFn,
                                void *addSpanUser);
