/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "strutil.h"

#include <string.h>

static char
strutil_preferredPathSeparator(void)
{
#ifdef _WIN32
    return '\\';
#else
    return '/';
#endif
}

static void
strutil_normalizePathSeparators(char *path)
{
#ifdef _WIN32
    char from = '/';
    char to = '\\';
#else
    char from = '\\';
    char to = '/';
#endif
    for (char *p = path; *p; p++) {
        if (*p == from) {
            *p = to;
        }
    }
}

void
strutil_strlcpy(char *dst, size_t dstCap, const char *src)
{
    if (!dst || dstCap == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t len = strlen(src);
    if (len > dstCap - 1) {
        len = dstCap - 1;
    }
    if (len > 0) {
        memcpy(dst, src, len);
    }
    dst[len] = '\0';
}

void
strutil_join2Trunc(char *out, size_t outCap, const char *a, const char *b)
{
    if (!out || outCap == 0) {
        return;
    }
    out[0] = '\0';
    if (!a) {
        a = "";
    }
    if (!b) {
        b = "";
    }
    size_t pos = 0;
    const char *parts[2] = { a, b };
    for (int i = 0; i < 2; ++i) {
        size_t len = strlen(parts[i]);
        size_t remain = (pos < outCap) ? (outCap - 1 - pos) : 0;
        if (len > remain) {
            len = remain;
        }
        if (len > 0) {
            memcpy(out + pos, parts[i], len);
            pos += len;
        }
        if (pos >= outCap - 1) {
            break;
        }
    }
    out[pos] = '\0';
}

void
strutil_join3Trunc(char *out, size_t outCap, const char *a, const char *b, const char *c)
{
    if (!out || outCap == 0) {
        return;
    }
    out[0] = '\0';
    if (!a) {
        a = "";
    }
    if (!b) {
        b = "";
    }
    if (!c) {
        c = "";
    }
    size_t pos = 0;
    const char *parts[3] = { a, b, c };
    for (int i = 0; i < 3; ++i) {
        size_t len = strlen(parts[i]);
        size_t remain = (pos < outCap) ? (outCap - 1 - pos) : 0;
        if (len > remain) {
            len = remain;
        }
        if (len > 0) {
            memcpy(out + pos, parts[i], len);
            pos += len;
        }
        if (pos >= outCap - 1) {
            break;
        }
    }
    out[pos] = '\0';
}

void
strutil_pathJoinTrunc(char *out, size_t outCap, const char *dir, const char *leaf)
{
    if (!out || outCap == 0) {
        return;
    }
    out[0] = '\0';
    if (!dir || !dir[0]) {
        strutil_strlcpy(out, outCap, leaf);
        strutil_normalizePathSeparators(out);
        return;
    }
    if (!leaf || !leaf[0]) {
        strutil_strlcpy(out, outCap, dir);
        strutil_normalizePathSeparators(out);
        return;
    }
    size_t dirLen = strlen(dir);
    int needSep = 1;
    if (dirLen > 0) {
        char c = dir[dirLen - 1];
        if (c == '/' || c == '\\') {
            needSep = 0;
        }
    }
    if (needSep) {
        char sep[2] = { strutil_preferredPathSeparator(), '\0' };
        strutil_join3Trunc(out, outCap, dir, sep, leaf);
    } else {
        strutil_join2Trunc(out, outCap, dir, leaf);
    }
    strutil_normalizePathSeparators(out);
}
