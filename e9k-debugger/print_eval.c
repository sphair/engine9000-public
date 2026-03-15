/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "print_eval.h"
#include "print_eval_internal.h"
#include "print_debuginfo_readelf.h"
#include "print_debuginfo_readelf_frames.h"
#include "print_debuginfo_objdump_stabs.h"
#include "alloc.h"
#include "debug.h"
#include "debugger.h"
#include "base_map.h"
#include "libretro.h"
#include "libretro_host.h"
#include "machine.h"
#include "strutil.h"

typedef struct print_value {
    print_type_t *type;
    uint32_t address;
    uint64_t immediate;
    int hasAddress;
    int hasImmediate;
} print_value_t;

typedef struct print_temp_type {
    print_type_t *type;
    struct print_temp_type *next;
} print_temp_type_t;

static print_value_t
print_eval_makeAddressValue(print_type_t *type, uint32_t addr);

static print_value_t
print_eval_makeImmediateValue(print_type_t *type, uint64_t immediate);

static print_type_t *
print_eval_getType(print_index_t *index, uint32_t offset);

static print_type_t *
print_eval_defaultU32(print_index_t *index);

static print_index_t print_eval_index = {0};
static char *print_eval_captureBuffer = NULL;
static size_t print_eval_captureCap = 0;
static size_t print_eval_captureLen = 0;
static int print_eval_captureEnabled = 0;

static uint64_t
print_eval_baseMapSignature(void)
{
    uint64_t h = 1469598103934665603ull;
    h ^= (uint64_t)base_map_getMode();
    h *= 1099511628211ull;
    size_t count = base_map_getStackCount();
    h ^= (uint64_t)count;
    h *= 1099511628211ull;
    for (size_t i = 0; i < count; ++i) {
        base_map_section_t section = BASE_MAP_SECTION_TEXT;
        uint32_t base = 0;
        uint32_t size = 0;
        if (!base_map_getStackEntry(i, &section, &base, &size)) {
            continue;
        }
        h ^= (uint64_t)((uint32_t)section & 0xffu);
        h *= 1099511628211ull;
        h ^= (uint64_t)(base & 0x00ffffffu);
        h *= 1099511628211ull;
        h ^= (uint64_t)size;
        h *= 1099511628211ull;
    }
    return h;
}

static int
print_eval_debugEnabled(void)
{
    static int cached = -1;
    if (cached >= 0) {
        return cached;
    }
    const char *v = getenv("E9K_PRINT_DEBUG");
    if (!v || !*v || strcmp(v, "0") == 0) {
        cached = 0;
        return cached;
    }
    cached = 1;
    return cached;
}

static int
print_eval_debugWantsSymbol(const char *name)
{
    const char *want = getenv("E9K_PRINT_DEBUG_SYM");
    if (!want || !*want || !name) {
        return 0;
    }
    return strstr(name, want) != NULL;
}

static int
print_eval_perfEnabled(void)
{
    static int cached = -1;
    if (cached >= 0) {
        return cached;
    }
    const char *v = getenv("E9K_PRINT_PERF");
    if (!v || !*v || strcmp(v, "0") == 0) {
        cached = 0;
        return cached;
    }
    cached = 1;
    return cached;
}

static uint64_t
print_eval_nowNs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void
print_eval_freeString(char *s)
{
    if (s) {
        alloc_free(s);
    }
}

static char *
print_eval_strdup(const char *s)
{
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    char *out = (char *)alloc_alloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, len + 1);
    return out;
}

static print_dwarf_node_t *
print_eval_findNode(print_index_t *index, uint32_t offset)
{
    if (!index) {
        return NULL;
    }
    for (int i = 0; i < index->nodeCount; ++i) {
        if (index->nodes[i].offset == offset) {
            return &index->nodes[i];
        }
        if (index->nodes[i].hasAltOffset && index->nodes[i].altOffset == offset) {
            return &index->nodes[i];
        }
        if (index->nodes[i].hasAltOffset2 && index->nodes[i].altOffset2 == offset) {
            return &index->nodes[i];
        }
    }
    return NULL;
}

static int
print_eval_getRegValueByDwarfReg(uint8_t dwarfReg, uint32_t *outValue)
{
    if (outValue) {
        *outValue = 0;
    }
    if (!outValue) {
        return 0;
    }
    char name[8];
    if (dwarfReg <= 7) {
        snprintf(name, sizeof(name), "D%u", (unsigned)dwarfReg);
    } else if (dwarfReg <= 15) {
        snprintf(name, sizeof(name), "A%u", (unsigned)(dwarfReg - 8u));
    } else {
        return 0;
    }
    unsigned long v = 0;
    if (!machine_findReg(&debugger.machine, name, &v)) {
        return 0;
    }
    *outValue = (uint32_t)v;
    return 1;
}

static int
print_eval_computeCfa(print_index_t *index, uint32_t pc, uint32_t *outCfa)
{
    if (outCfa) {
        *outCfa = 0;
    }
    if (!index || !outCfa) {
        return 0;
    }
    if (!index->fdes || index->fdeCount <= 0) {
        return 0;
    }
    for (int i = 0; i < index->fdeCount; ++i) {
        print_cfi_fde_t *fde = &index->fdes[i];
        if (pc < fde->pcStart || pc >= fde->pcEnd) {
            continue;
        }
        uint8_t reg = fde->defaultCfaReg;
        int32_t off = fde->defaultCfaOffset;
        for (int r = 0; r < fde->rowCount; ++r) {
            if (pc >= fde->rows[r].loc) {
                reg = fde->rows[r].cfaReg;
                off = fde->rows[r].cfaOffset;
            } else {
                break;
            }
        }
        uint32_t regVal = 0;
        if (!print_eval_getRegValueByDwarfReg(reg, &regVal)) {
            return 0;
        }
        int64_t cfa64 = (int64_t)(uint64_t)regVal + (int64_t)off;
        *outCfa = (uint32_t)cfa64;
        return 1;
    }
    return 0;
}

static int
print_eval_nodeRangeContainsPc(const print_dwarf_node_t *node, uint32_t pc)
{
    if (!node || !node->hasLowPc || !node->hasHighPc) {
        return 0;
    }
    uint64_t begin = node->lowPc;
    uint64_t end = node->highPcIsOffset ? (node->lowPc + node->highPc) : node->highPc;
    return (uint64_t)pc >= begin && (uint64_t)pc < end;
}

static print_dwarf_node_t *
print_eval_findScopeForPc(print_index_t *index, uint32_t pc)
{
    if (!index) {
        return NULL;
    }
    print_dwarf_node_t *best = NULL;
    int bestDepth = -1;
    uint64_t bestSize = UINT64_MAX;
    for (int i = 0; i < index->nodeCount; ++i) {
        print_dwarf_node_t *node = &index->nodes[i];
        if (node->tag != print_dwarf_tag_subprogram &&
            node->tag != print_dwarf_tag_lexical_block &&
            node->tag != print_dwarf_tag_inlined_subroutine) {
            continue;
        }
        if (!print_eval_nodeRangeContainsPc(node, pc)) {
            continue;
        }
        uint64_t begin = node->lowPc;
        uint64_t end = node->highPcIsOffset ? (node->lowPc + node->highPc) : node->highPc;
        uint64_t size = (end > begin) ? (end - begin) : 0;
        int depth = (int)node->depth;
        if (depth > bestDepth || (depth == bestDepth && size < bestSize)) {
            best = node;
            bestDepth = depth;
            bestSize = size;
        }
    }
    return best;
}

static int
print_eval_resolveAbstractOrigin(print_index_t *index, const print_dwarf_node_t *node, const char **outName, uint32_t *outTypeRef)
{
    if (outName) {
        *outName = NULL;
    }
    if (outTypeRef) {
        *outTypeRef = 0;
    }
    if (!index || !node || !node->hasAbstractOrigin) {
        return 0;
    }
    print_dwarf_node_t *origin = print_eval_findNode(index, node->abstractOrigin);
    if (!origin) {
        return 0;
    }
    if (outName) {
        *outName = origin->name;
    }
    if (outTypeRef && origin->hasTypeRef) {
        *outTypeRef = origin->typeRef;
    }
    return 1;
}

static int
print_eval_resolveLocalDwarf(const char *name, print_index_t *index, print_value_t *out, int typeOnly)
{
    if (!name || !*name || !index || !out) {
        return 0;
    }
    if (machine_getRunning(debugger.machine)) {
        return 0;
    }
    unsigned long pcRaw = 0;
    if (!machine_findReg(&debugger.machine, "PC", &pcRaw)) {
        return 0;
    }
    uint32_t pc = (uint32_t)pcRaw & 0x00ffffffu;

    uint32_t cfa = 0;
    if (!print_eval_computeCfa(index, pc, &cfa)) {
        return 0;
    }

    print_dwarf_node_t *scope = print_eval_findScopeForPc(index, pc);
    if (!scope) {
        return 0;
    }

    uint32_t chain[64];
    int chainCount = 0;
    for (const print_dwarf_node_t *cur = scope; cur && chainCount < (int)(sizeof(chain) / sizeof(chain[0]));) {
        chain[chainCount++] = cur->offset;
        if (cur->parentOffset == 0) {
            break;
        }
        cur = print_eval_findNode(index, cur->parentOffset);
    }

    // Nearest subprogram in the chain provides frame base.
    print_dwarf_node_t *subp = NULL;
    for (int i = 0; i < chainCount; ++i) {
        print_dwarf_node_t *n = print_eval_findNode(index, chain[i]);
        if (n && n->tag == print_dwarf_tag_subprogram) {
            subp = n;
            break;
        }
    }
    uint32_t frameBase = cfa;
    if (subp && subp->hasFrameBase && subp->frameBaseKind == print_dwarf_location_cfa) {
        frameBase = cfa;
    }

    for (int c = 0; c < chainCount; ++c) {
        uint32_t scopeOff = chain[c];
        for (int i = 0; i < index->nodeCount; ++i) {
            print_dwarf_node_t *n = &index->nodes[i];
            if (n->parentOffset != scopeOff) {
                continue;
            }
            if (n->tag != print_dwarf_tag_variable && n->tag != print_dwarf_tag_formal_parameter) {
                continue;
            }
            const char *nName = n->name;
            uint32_t typeRef = n->hasTypeRef ? n->typeRef : 0;
            if ((!nName || !*nName) && n->hasAbstractOrigin) {
                const char *originName = NULL;
                uint32_t originType = 0;
                if (print_eval_resolveAbstractOrigin(index, n, &originName, &originType)) {
                    if (originName && *originName) {
                        nName = originName;
                    }
                    if (typeRef == 0 && originType != 0) {
                        typeRef = originType;
                    }
                }
            }
            if (!nName || strcmp(nName, name) != 0) {
                continue;
            }

            print_type_t *type = NULL;
            if (typeRef != 0) {
                type = print_eval_getType(index, typeRef);
            }
            if (!type) {
                type = print_eval_defaultU32(index);
            }

            if (n->locationKind == print_dwarf_location_fbreg) {
                int64_t addr64 = (int64_t)(uint64_t)frameBase + (int64_t)n->locationOffset;
                uint32_t addr = (uint32_t)addr64 & 0x00ffffffu;
                if (typeOnly) {
                    *out = print_eval_makeAddressValue(type, 0);
                    out->hasAddress = 0;
                } else {
                    *out = print_eval_makeAddressValue(type, addr);
                }
                return 1;
            }
            if (n->locationKind == print_dwarf_location_breg) {
                uint32_t regVal = 0;
                if (!print_eval_getRegValueByDwarfReg(n->locationReg, &regVal)) {
                    return 0;
                }
                int64_t addr64 = (int64_t)(uint64_t)regVal + (int64_t)n->locationOffset;
                uint32_t addr = (uint32_t)addr64 & 0x00ffffffu;
                if (typeOnly) {
                    *out = print_eval_makeAddressValue(type, 0);
                    out->hasAddress = 0;
                } else {
                    *out = print_eval_makeAddressValue(type, addr);
                }
                return 1;
            }
            if (n->locationKind == print_dwarf_location_addr && n->hasAddr) {
                uint32_t addr = (uint32_t)n->addr & 0x00ffffffu;
                if (typeOnly) {
                    *out = print_eval_makeAddressValue(type, 0);
                    out->hasAddress = 0;
                } else {
                    *out = print_eval_makeAddressValue(type, addr);
                }
                return 1;
            }
            if (n->locationKind == print_dwarf_location_const && n->hasConstValue) {
                if (typeOnly) {
                    *out = print_eval_makeImmediateValue(type, 0);
                    out->hasImmediate = 0;
                } else {
                    *out = print_eval_makeImmediateValue(type, n->constValue);
                }
                return 1;
            }
            if (n->locationKind == print_dwarf_location_reg) {
                uint32_t regVal = 0;
                if (!print_eval_getRegValueByDwarfReg(n->locationReg, &regVal)) {
                    return 0;
                }
                if (typeOnly) {
                    *out = print_eval_makeImmediateValue(type, 0);
                    out->hasImmediate = 0;
                } else {
                    *out = print_eval_makeImmediateValue(type, (uint64_t)regVal);
                }
                return 1;
            }
            if (n->locationKind == print_dwarf_location_cfa) {
                if (typeOnly) {
                    *out = print_eval_makeAddressValue(type, 0);
                    out->hasAddress = 0;
                } else {
                    *out = print_eval_makeAddressValue(type, cfa & 0x00ffffffu);
                }
                return 1;
            }
            return 0;
        }
    }
    return 0;
}

static int
print_eval_stabsScopeContainsPc(const print_stabs_scope_t *scope, uint32_t pcRel)
{
    if (!scope) {
        return 0;
    }
    if (pcRel < scope->startPc) {
        return 0;
    }
    if (scope->hasEnd && pcRel >= scope->endPc) {
        return 0;
    }
    return 1;
}

static int
print_eval_stabsPushSizeFromMnemonic(const char *line)
{
    if (!line || !*line) {
        return 0;
    }
    if (strstr(line, "pea ") != NULL) {
        return 4;
    }
    if (strstr(line, ".b") != NULL) {
        return 1;
    }
    if (strstr(line, ".w") != NULL) {
        return 2;
    }
    if (strstr(line, ".l") != NULL) {
        return 4;
    }
    return 4;
}

static int
print_eval_stabsParseImmAfterHash(const char *line, int32_t *outValue)
{
    if (outValue) {
        *outValue = 0;
    }
    if (!line || !outValue) {
        return 0;
    }
    const char *hash = strchr(line, '#');
    if (!hash || !hash[1]) {
        return 0;
    }
    const char *num = hash + 1;
    while (*num && isspace((unsigned char)*num)) {
        ++num;
    }
    int sign = 1;
    if (*num == '+') {
        ++num;
    } else if (*num == '-') {
        sign = -1;
        ++num;
    }
    if (!*num) {
        return 0;
    }
    if (*num == '$') {
        ++num;
        if (!isxdigit((unsigned char)*num)) {
            return 0;
        }
        int64_t value = 0;
        while (isxdigit((unsigned char)*num)) {
            char c = *num++;
            int digit = 0;
            if (c >= '0' && c <= '9') {
                digit = c - '0';
            } else {
                c = (char)tolower((unsigned char)c);
                digit = 10 + (c - 'a');
            }
            value = (value << 4) + digit;
        }
        *outValue = (int32_t)(sign * value);
        return 1;
    }
    errno = 0;
    char *end = NULL;
    long v = strtol(num, &end, 0);
    if (errno != 0 || !end || end == num) {
        return 0;
    }
    *outValue = (int32_t)(sign * v);
    return 1;
}

static int
print_eval_stabsParseLeaSpAdjust(const char *line, int32_t *outValue)
{
    if (outValue) {
        *outValue = 0;
    }
    if (!line || !outValue) {
        return 0;
    }
    const char *tail = strstr(line, "(sp),sp");
    if (!tail) {
        tail = strstr(line, "(a7),a7");
    }
    if (!tail) {
        return 0;
    }
    const char *end = tail;
    while (end > line && isspace((unsigned char)end[-1])) {
        --end;
    }
    const char *start = end;
    while (start > line) {
        char c = start[-1];
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == 'x' || c == 'X' ||
            c == '$' || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
            --start;
            continue;
        }
        break;
    }
    if (start >= end) {
        return 0;
    }
    char buf[32];
    size_t n = (size_t)(end - start);
    if (n >= sizeof(buf)) {
        n = sizeof(buf) - 1;
    }
    memcpy(buf, start, n);
    buf[n] = '\0';
    int sign = 1;
    const char *num = buf;
    if (*num == '+') {
        ++num;
    } else if (*num == '-') {
        sign = -1;
        ++num;
    }
    if (!*num) {
        return 0;
    }
    if (*num == '$') {
        ++num;
        if (!isxdigit((unsigned char)*num)) {
            return 0;
        }
        int64_t value = 0;
        while (isxdigit((unsigned char)*num)) {
            char c = *num++;
            int digit = 0;
            if (c >= '0' && c <= '9') {
                digit = c - '0';
            } else {
                c = (char)tolower((unsigned char)c);
                digit = 10 + (c - 'a');
            }
            value = (value << 4) + digit;
        }
        if (*num != '\0') {
            return 0;
        }
        *outValue = (int32_t)(sign * value);
        return 1;
    }
    errno = 0;
    char *numEnd = NULL;
    long v = strtol(num, &numEnd, 0);
    if (errno != 0 || !numEnd || numEnd == num || *numEnd != '\0') {
        return 0;
    }
    *outValue = (int32_t)(sign * v);
    return 1;
}

static int
print_eval_estimateStabsStackBase(const print_stabs_func_t *func,
                                  const char *debugName,
                                  uint32_t pcAbs,
                                  uint32_t spNow,
                                  uint32_t *outBase)
{
    if (outBase) {
        *outBase = spNow;
    }
    if (!func || !outBase) {
        return 0;
    }
    uint32_t startAbs = func->startPc & 0x00ffffffu;
    (void)base_map_debugToRuntime(BASE_MAP_SECTION_TEXT, startAbs, &startAbs);
    if (pcAbs <= startAbs) {
        *outBase = spNow;
        return 1;
    }

    uint32_t scanStart = startAbs;
    if (pcAbs > startAbs) {
        uint32_t delta = pcAbs - startAbs;
        const uint32_t maxScanBytes = 128u;
        if (delta > maxScanBytes) {
            scanStart = pcAbs - maxScanBytes;
        }
    }
    scanStart &= ~1u;

    uint32_t cur = scanStart;
    int32_t pendingCallArgs = 0;
    int seenNonPrologue = (scanStart != startAbs);
    int seenFrameAccess = (scanStart != startAbs);
    int debugOn = print_eval_debugEnabled() &&
                  print_eval_debugWantsSymbol(debugName ? debugName : (func->name ? func->name : ""));
    if (debugOn) {
        debug_printf("print: stabs est name='%s' func='%s' start=0x%06X scanStart=0x%06X pc=0x%06X sp=0x%06X\n",
                     debugName ? debugName : "?",
                     func->name ? func->name : "?",
                     (unsigned)startAbs,
                     (unsigned)scanStart,
                     (unsigned)pcAbs,
                     (unsigned)spNow);
    }
    for (int steps = 0; cur < pcAbs && steps < 512; ++steps) {
        char disBuf[160];
        char lowerBuf[160];
        lowerBuf[0] = '\0';
        size_t insnLen = 0;
        if (!libretro_host_debugDisassembleQuick(cur, disBuf, sizeof(disBuf), &insnLen) || insnLen == 0) {
            insnLen = 2;
        }
        if (insnLen > 64) {
            insnLen = 2;
        }
        for (size_t i = 0; i < sizeof(lowerBuf) - 1 && disBuf[i]; ++i) {
            lowerBuf[i] = (char)tolower((unsigned char)disBuf[i]);
            lowerBuf[i + 1] = '\0';
        }

        int isPea = strstr(lowerBuf, "pea") != NULL;
        int isPush = isPea || strstr(lowerBuf, "-(sp)") != NULL || strstr(lowerBuf, "-(a7)") != NULL;
        int isCall = strstr(lowerBuf, "jsr") != NULL || strstr(lowerBuf, "bsr") != NULL;
        int isRegSavePush = 0;
        if (isPush) {
            if (strstr(lowerBuf, "movem") != NULL) {
                isRegSavePush = 1;
            } else if ((strstr(lowerBuf, "move.l a") != NULL || strstr(lowerBuf, "move.w a") != NULL ||
                        strstr(lowerBuf, "move.b a") != NULL || strstr(lowerBuf, "move.l d") != NULL ||
                        strstr(lowerBuf, "move.w d") != NULL || strstr(lowerBuf, "move.b d") != NULL) &&
                       (strstr(lowerBuf, ",-(sp)") != NULL || strstr(lowerBuf, ",-(a7)") != NULL)) {
                isRegSavePush = 1;
            }
        }
        int hasFrameAccess = 0;
        if (strstr(lowerBuf, "(sp,$") != NULL || strstr(lowerBuf, "(a7,$") != NULL ||
            strstr(lowerBuf, "(sp)") != NULL || strstr(lowerBuf, "(a7)") != NULL) {
            hasFrameAccess = 1;
        }
        int32_t cleanup = 0;
        int hasCleanup = 0;

        int32_t imm = 0;
        if ((strstr(lowerBuf, "addq") != NULL || strstr(lowerBuf, "adda") != NULL || strstr(lowerBuf, "add.") != NULL) &&
            (strstr(lowerBuf, ",sp") != NULL || strstr(lowerBuf, ",a7") != NULL) &&
            print_eval_stabsParseImmAfterHash(lowerBuf, &imm) &&
            imm > 0) {
            cleanup = imm;
            hasCleanup = 1;
        } else if (strstr(lowerBuf, "lea") != NULL &&
                   (strstr(lowerBuf, "(sp),sp") != NULL || strstr(lowerBuf, "(a7),a7") != NULL) &&
                   print_eval_stabsParseLeaSpAdjust(lowerBuf, &imm) &&
                   imm > 0) {
            cleanup = imm;
            hasCleanup = 1;
        } else if (strstr(lowerBuf, "(sp)+") != NULL || strstr(lowerBuf, "(a7)+") != NULL) {
            cleanup = print_eval_stabsPushSizeFromMnemonic(lowerBuf);
            hasCleanup = cleanup > 0;
        }

        if (isPush) {
            if (seenNonPrologue) {
                if (!(isRegSavePush && !seenFrameAccess)) {
                    int pushSize = print_eval_stabsPushSizeFromMnemonic(disBuf);
                    if (pushSize > 0) {
                        pendingCallArgs -= pushSize;
                        if (debugOn) {
                            debug_printf("print: stabs est push pc=0x%06X sz=%d pending=%d ins='%s'\n",
                                         (unsigned)cur, pushSize, (int)pendingCallArgs, disBuf);
                        }
                    }
                }
            }
        } else if (isCall) {
            // Keep pending pushes as call-argument candidates across jsr/bsr.
            if (debugOn) {
                debug_printf("print: stabs est call pc=0x%06X pending=%d ins='%s'\n",
                             (unsigned)cur, (int)pendingCallArgs, disBuf);
            }
        } else if (hasCleanup) {
            if (cleanup > 0 && pendingCallArgs < 0) {
                int32_t use = cleanup;
                int32_t need = -pendingCallArgs;
                if (use > need) {
                    use = need;
                }
                pendingCallArgs += use;
                if (debugOn) {
                    debug_printf("print: stabs est cleanup pc=0x%06X clean=%d used=%d pending=%d ins='%s'\n",
                                 (unsigned)cur, (int)cleanup, (int)use, (int)pendingCallArgs, disBuf);
                }
            } else {
                pendingCallArgs = 0;
                if (debugOn) {
                    debug_printf("print: stabs est reset pc=0x%06X clean=%d pending=%d ins='%s'\n",
                                 (unsigned)cur, (int)cleanup, (int)pendingCallArgs, disBuf);
                }
            }
        }
        if (!seenNonPrologue && !isPush && !isCall) {
            seenNonPrologue = 1;
        }
        if (hasFrameAccess && !isPush && !isCall) {
            seenFrameAccess = 1;
        }

        uint32_t next = (cur + (uint32_t)insnLen) & 0x00ffffffu;
        if (next == cur) {
            next = (cur + 2u) & 0x00ffffffu;
        }
        cur = next;
    }

    int64_t base64 = (int64_t)(uint64_t)spNow - (int64_t)pendingCallArgs;
    *outBase = (uint32_t)base64;
    if (debugOn) {
        debug_printf("print: stabs est end pending=%d stackBase=0x%06X\n",
                     (int)pendingCallArgs, (unsigned)*outBase);
    }
    return 1;
}

static int
print_eval_resolveLocalStabs(const char *name, print_index_t *index, print_value_t *out, int typeOnly)
{
    if (!name || !*name || !index || !out) {
        return 0;
    }
    if (machine_getRunning(debugger.machine)) {
        return 0;
    }
    if (index->stabsFuncCount <= 0 || index->stabsScopeCount <= 0 || index->stabsVarCount <= 0) {
        return 0;
    }
    unsigned long pcRaw = 0;
    if (!machine_findReg(&debugger.machine, "PC", &pcRaw)) {
        return 0;
    }
    uint32_t pc = (uint32_t)pcRaw & 0x00ffffffu;
    uint32_t pcRel = pc;
    (void)base_map_runtimeToDebug(BASE_MAP_SECTION_TEXT, pc, &pcRel);

    int funcIndex = -1;
    uint32_t bestStart = 0;
    for (int i = 0; i < index->stabsFuncCount; ++i) {
        print_stabs_func_t *f = &index->stabsFuncs[i];
        if (!f->name || !*f->name) {
            continue;
        }
        if (pcRel < f->startPc) {
            continue;
        }
        if (f->hasEnd && pcRel >= f->endPc) {
            continue;
        }
        if (funcIndex < 0 || f->startPc >= bestStart) {
            funcIndex = i;
            bestStart = f->startPc;
        }
    }
    if (funcIndex < 0) {
        return 0;
    }

    print_stabs_func_t *func = &index->stabsFuncs[funcIndex];
    int bestScopeIndex = func->rootScopeIndex;
    int bestDepth = -1;
    uint64_t bestSize = UINT64_MAX;
    int scopeBegin = func->scopeStart;
    int scopeEnd = func->scopeStart + func->scopeCount;
    if (scopeBegin < 0) {
        scopeBegin = 0;
    }
    if (scopeEnd > index->stabsScopeCount) {
        scopeEnd = index->stabsScopeCount;
    }
    for (int i = scopeBegin; i < scopeEnd; ++i) {
        print_stabs_scope_t *s = &index->stabsScopes[i];
        if (!print_eval_stabsScopeContainsPc(s, pcRel)) {
            continue;
        }
        uint64_t size = UINT64_MAX;
        if (s->hasEnd && s->endPc > s->startPc) {
            size = (uint64_t)(s->endPc - s->startPc);
        }
        int depth = (int)s->depth;
        if (depth > bestDepth || (depth == bestDepth && size < bestSize)) {
            bestScopeIndex = i;
            bestDepth = depth;
            bestSize = size;
        }
    }
    if (bestScopeIndex < 0 || bestScopeIndex >= index->stabsScopeCount) {
        return 0;
    }

    uint32_t stackBase = 0;
    int hasStackBase = 0;
    if (func->hasParamBase) {
        uint32_t cfa = 0;
        if (print_eval_computeCfa(index, pc, &cfa)) {
            int64_t base64 = (int64_t)(uint64_t)cfa - (int64_t)func->paramBaseOffset;
            stackBase = (uint32_t)base64;
            hasStackBase = 1;
        }
    }
    if (!hasStackBase) {
        unsigned long spRaw = 0;
        if (!machine_findReg(&debugger.machine, "A7", &spRaw)) {
            return 0;
        }
        uint32_t sp = (uint32_t)spRaw;
        stackBase = sp;
        (void)print_eval_estimateStabsStackBase(func, name, pc, sp, &stackBase);
    }

    int scopeIndex = bestScopeIndex;
    while (scopeIndex >= 0 && scopeIndex < index->stabsScopeCount) {
        int varBegin = func->varStart;
        int varEnd = func->varStart + func->varCount;
        if (varBegin < 0) {
            varBegin = 0;
        }
        if (varEnd > index->stabsVarCount) {
            varEnd = index->stabsVarCount;
        }
        for (int v = varBegin; v < varEnd; ++v) {
            print_stabs_var_t *var = &index->stabsVars[v];
            if (var->scopeIndex != scopeIndex) {
                continue;
            }
            if (!var->name || strcmp(var->name, name) != 0) {
                continue;
            }

            print_type_t *type = NULL;
            if (var->typeRef != 0) {
                type = print_eval_getType(index, var->typeRef);
            }
            if (!type) {
                type = print_eval_defaultU32(index);
            }

            if (var->kind == print_stabs_var_stack) {
                int64_t addr64 = (int64_t)(uint64_t)stackBase + (int64_t)var->stackOffset;
                uint32_t addr = (uint32_t)addr64 & 0x00ffffffu;
                if (typeOnly) {
                    *out = print_eval_makeAddressValue(type, 0);
                    out->hasAddress = 0;
                } else {
                    *out = print_eval_makeAddressValue(type, addr);
                }
                return 1;
            }
            if (var->kind == print_stabs_var_reg) {
                uint32_t regVal = 0;
                if (!print_eval_getRegValueByDwarfReg(var->reg, &regVal)) {
                    return 0;
                }
                if (typeOnly) {
                    *out = print_eval_makeImmediateValue(type, 0);
                    out->hasImmediate = 0;
                } else {
                    *out = print_eval_makeImmediateValue(type, (uint64_t)regVal);
                }
                return 1;
            }
            if (var->kind == print_stabs_var_const && var->hasConstValue) {
                if (typeOnly) {
                    *out = print_eval_makeImmediateValue(type, 0);
                    out->hasImmediate = 0;
                } else {
                    *out = print_eval_makeImmediateValue(type, var->constValue);
                }
                return 1;
            }
            return 0;
        }
        scopeIndex = index->stabsScopes[scopeIndex].parentIndex;
    }
    return 0;
}

static int
print_eval_resolveLocal(const char *name, print_index_t *index, print_value_t *out, int typeOnly)
{
    if (print_eval_resolveLocalDwarf(name, index, out, typeOnly)) {
        return 1;
    }
    return print_eval_resolveLocalStabs(name, index, out, typeOnly);
}

static int
print_eval_addVariable(print_index_t *index, const char *name, uint32_t addr, uint32_t typeRef, size_t byteSize, int hasByteSize)
{
    if (!index || !name || !*name) {
        return 0;
    }
    if (index->varCount >= index->varCap) {
        int next = index->varCap ? index->varCap * 2 : 128;
        print_variable_t *nextVars = (print_variable_t *)alloc_realloc(index->vars, sizeof(*nextVars) * (size_t)next);
        if (!nextVars) {
            return 0;
        }
        index->vars = nextVars;
        index->varCap = next;
    }
    print_variable_t *var = &index->vars[index->varCount++];
    memset(var, 0, sizeof(*var));
    var->name = print_eval_strdup(name);
    var->addr = addr;
    var->typeRef = typeRef;
    var->byteSize = byteSize;
    var->hasByteSize = hasByteSize ? 1 : 0;
    return var->name != NULL;
}

static void
print_eval_clearIndex(print_index_t *index)
{
    if (!index) {
        return;
    }
    for (int i = 0; i < index->nodeCount; ++i) {
        print_eval_freeString(index->nodes[i].name);
    }
    alloc_free(index->nodes);
    index->nodes = NULL;
    index->nodeCount = 0;
    index->nodeCap = 0;
    if (index->fdes) {
        for (int i = 0; i < index->fdeCount; ++i) {
            alloc_free(index->fdes[i].rows);
            index->fdes[i].rows = NULL;
            index->fdes[i].rowCount = 0;
            index->fdes[i].rowCap = 0;
        }
    }
    alloc_free(index->fdes);
    index->fdes = NULL;
    index->fdeCount = 0;
    index->fdeCap = 0;
    for (int i = 0; i < index->symbolCount; ++i) {
        print_eval_freeString(index->symbols[i].name);
    }
    alloc_free(index->symbols);
    index->symbols = NULL;
    index->symbolCount = 0;
    index->symbolCap = 0;
    alloc_free(index->symbolLookup);
    index->symbolLookup = NULL;
    index->symbolLookupMask = 0;
    for (int i = 0; i < index->varCount; ++i) {
        print_eval_freeString(index->vars[i].name);
    }
    alloc_free(index->vars);
    index->vars = NULL;
    index->varCount = 0;
    index->varCap = 0;
    for (int i = 0; i < index->stabsFuncCount; ++i) {
        print_eval_freeString(index->stabsFuncs[i].name);
    }
    alloc_free(index->stabsFuncs);
    index->stabsFuncs = NULL;
    index->stabsFuncCount = 0;
    index->stabsFuncCap = 0;
    alloc_free(index->stabsScopes);
    index->stabsScopes = NULL;
    index->stabsScopeCount = 0;
    index->stabsScopeCap = 0;
    for (int i = 0; i < index->stabsVarCount; ++i) {
        print_eval_freeString(index->stabsVars[i].name);
    }
    alloc_free(index->stabsVars);
    index->stabsVars = NULL;
    index->stabsVarCount = 0;
    index->stabsVarCap = 0;
    for (int i = 0; i < index->typeCount; ++i) {
        print_type_t *type = index->types[i];
        if (type) {
            print_eval_freeString(type->name);
            for (int m = 0; m < type->memberCount; ++m) {
                print_eval_freeString(type->members[m].name);
            }
            alloc_free(type->members);
            alloc_free(type);
        }
    }
    alloc_free(index->types);
    index->types = NULL;
    index->typeCount = 0;
    index->typeCap = 0;
    index->defaultU8 = NULL;
    index->defaultU16 = NULL;
    index->defaultU32 = NULL;
    index->defaultU64 = NULL;
    index->cacheTextBaseAddr = 0;
    index->cacheDataBaseAddr = 0;
    index->cacheBssBaseAddr = 0;
    index->cacheBaseMapSignature = 0;
    index->elfPath[0] = '\0';
}

static uint32_t
print_eval_hashString(const char *s)
{
    uint32_t h = 2166136261u;
    if (!s) {
        return h;
    }
    while (*s) {
        h ^= (uint8_t)(*s++);
        h *= 16777619u;
    }
    return h;
}

static void
print_eval_buildSymbolLookup(print_index_t *index)
{
    if (!index) {
        return;
    }
    alloc_free(index->symbolLookup);
    index->symbolLookup = NULL;
    index->symbolLookupMask = 0;

    if (index->symbolCount <= 0) {
        return;
    }
    uint32_t need = (uint32_t)index->symbolCount * 2u;
    if (need < 16u) {
        need = 16u;
    }
    uint32_t cap = 1u;
    while (cap < need && cap < (1u << 30)) {
        cap <<= 1u;
    }
    uint32_t *table = (uint32_t *)alloc_calloc((size_t)cap, sizeof(*table));
    if (!table) {
        return;
    }
    uint32_t mask = cap - 1u;

    for (int i = 0; i < index->symbolCount; ++i) {
        const char *name = index->symbols[i].name;
        if (!name || !*name) {
            continue;
        }
        uint32_t h = print_eval_hashString(name);
        uint32_t pos = h & mask;
        for (uint32_t step = 0; step < cap; ++step) {
            uint32_t slot = table[pos];
            if (slot == 0) {
                table[pos] = (uint32_t)(i + 1);
                break;
            }
            uint32_t existingIndex = slot - 1u;
            if (existingIndex < (uint32_t)index->symbolCount &&
                index->symbols[existingIndex].name &&
                strcmp(index->symbols[existingIndex].name, name) == 0) {
                break;
            }
            pos = (pos + 1u) & mask;
        }
    }

    index->symbolLookup = table;
    index->symbolLookupMask = mask;
}

static uint32_t
print_eval_lookupSymbolAddr(print_index_t *index, const char *name, int *found)
{
    if (found) {
        *found = 0;
    }
    if (!index || !name) {
        return 0;
    }
    if (index->symbolLookup && index->symbolLookupMask != 0) {
        uint32_t h = print_eval_hashString(name);
        uint32_t pos = h & index->symbolLookupMask;
        uint32_t cap = index->symbolLookupMask + 1u;
        for (uint32_t step = 0; step < cap; ++step) {
            uint32_t slot = index->symbolLookup[pos];
            if (slot == 0) {
                return 0;
            }
            uint32_t idx = slot - 1u;
            if (idx < (uint32_t)index->symbolCount &&
                index->symbols[idx].name &&
                strcmp(index->symbols[idx].name, name) == 0) {
                if (found) {
                    *found = 1;
                }
                return index->symbols[idx].addr;
            }
            pos = (pos + 1u) & index->symbolLookupMask;
        }
        return 0;
    }
    for (int i = 0; i < index->symbolCount; ++i) {
        if (strcmp(index->symbols[i].name, name) == 0) {
            if (found) {
                *found = 1;
            }
            return index->symbols[i].addr;
        }
    }
    return 0;
}

static void
print_eval_buildVariables(print_index_t *index)
{
    if (!index) {
        return;
    }
    for (int i = 0; i < index->nodeCount; ++i) {
        print_dwarf_node_t *node = &index->nodes[i];
        if (node->tag != print_dwarf_tag_variable) {
            continue;
        }
        if (!node->name || !node->hasTypeRef) {
            continue;
        }
        uint32_t addr = 0;
        int hasAddr = 0;
        if (node->hasAddr) {
            addr = (uint32_t)(node->addr & 0x00ffffffu);
            hasAddr = 1;
        }
        if (!hasAddr) {
            addr = print_eval_lookupSymbolAddr(index, node->name, &hasAddr);
        }
        if (!hasAddr) {
            continue;
        }
        print_eval_addVariable(index, node->name, addr, node->typeRef, 0, 0);
    }
}

static print_type_t *
print_eval_findType(print_index_t *index, uint32_t offset)
{
    if (!index) {
        return NULL;
    }
    for (int i = 0; i < index->typeCount; ++i) {
        if (index->types[i] && index->types[i]->dieOffset == offset) {
            return index->types[i];
        }
    }
    return NULL;
}

static print_type_t *
print_eval_addType(print_index_t *index, uint32_t offset)
{
    if (!index) {
        return NULL;
    }
    if (index->typeCount >= index->typeCap) {
        int next = index->typeCap ? index->typeCap * 2 : 128;
        print_type_t **nextTypes = (print_type_t **)alloc_realloc(index->types, sizeof(*nextTypes) * (size_t)next);
        if (!nextTypes) {
            return NULL;
        }
        index->types = nextTypes;
        index->typeCap = next;
    }
    print_type_t *type = (print_type_t *)alloc_calloc(1, sizeof(*type));
    if (!type) {
        return NULL;
    }
    type->dieOffset = offset;
    index->types[index->typeCount++] = type;
    return type;
}

static print_type_t *
print_eval_getType(print_index_t *index, uint32_t offset);

static size_t
print_eval_arrayCountFromNode(print_index_t *index, print_dwarf_node_t *node)
{
    if (!index || !node) {
        return 0;
    }
    for (int i = 0; i < index->nodeCount; ++i) {
        print_dwarf_node_t *child = &index->nodes[i];
        if (child->parentOffset != node->offset) {
            continue;
        }
        if (child->tag != print_dwarf_tag_subrange_type) {
            continue;
        }
        if (child->hasCount) {
            return (size_t)child->count;
        }
        if (child->hasUpperBound) {
            return (size_t)(child->upperBound + 1);
        }
    }
    return 0;
}

static int
print_eval_collectMembers(print_index_t *index, print_dwarf_node_t *node, print_type_t *type)
{
    if (!index || !node || !type) {
        return 0;
    }
    int memberCount = 0;
    for (int i = 0; i < index->nodeCount; ++i) {
        if (index->nodes[i].parentOffset == node->offset && index->nodes[i].tag == print_dwarf_tag_member) {
            ++memberCount;
        }
    }
    if (memberCount <= 0) {
        return 1;
    }
    type->members = (print_member_t *)alloc_calloc((size_t)memberCount, sizeof(*type->members));
    if (!type->members) {
        return 0;
    }
    type->memberCount = 0;
    for (int i = 0; i < index->nodeCount; ++i) {
        print_dwarf_node_t *child = &index->nodes[i];
        if (child->parentOffset != node->offset || child->tag != print_dwarf_tag_member) {
            continue;
        }
        print_member_t *member = &type->members[type->memberCount++];
        member->name = print_eval_strdup(child->name ? child->name : "<anon>");
        member->offset = child->hasMemberOffset ? (uint32_t)child->memberOffset : 0;
        member->type = child->hasTypeRef ? print_eval_getType(index, child->typeRef) : NULL;
    }
    return 1;
}

static print_type_t *
print_eval_getType(print_index_t *index, uint32_t offset)
{
    if (!index || offset == 0) {
        return NULL;
    }
    print_type_t *existing = print_eval_findType(index, offset);
    if (existing) {
        return existing;
    }
    print_dwarf_node_t *node = print_eval_findNode(index, offset);
    if (!node) {
        return NULL;
    }
    print_type_t *type = print_eval_addType(index, offset);
    if (!type) {
        return NULL;
    }
    type->name = print_eval_strdup(node->name ? node->name : "");
    switch (node->tag) {
        case print_dwarf_tag_base_type:
            type->kind = print_type_base;
            type->byteSize = node->hasByteSize ? (size_t)node->byteSize : 0;
            type->encoding = node->encoding;
            break;
        case print_dwarf_tag_pointer_type:
            type->kind = print_type_pointer;
            type->byteSize = node->hasByteSize ? (size_t)node->byteSize : 4;
            if (node->hasTypeRef) {
                type->targetType = print_eval_getType(index, node->typeRef);
            }
            break;
        case print_dwarf_tag_structure_type:
            type->kind = print_type_struct;
            type->byteSize = node->hasByteSize ? (size_t)node->byteSize : 0;
            print_eval_collectMembers(index, node, type);
            break;
        case print_dwarf_tag_array_type:
            type->kind = print_type_array;
            type->arrayCount = print_eval_arrayCountFromNode(index, node);
            if (node->hasTypeRef) {
                type->targetType = print_eval_getType(index, node->typeRef);
            }
            break;
        case print_dwarf_tag_typedef:
            type->kind = print_type_typedef;
            if (node->hasTypeRef) {
                type->targetType = print_eval_getType(index, node->typeRef);
            }
            break;
        case print_dwarf_tag_const_type:
            type->kind = print_type_const;
            if (node->hasTypeRef) {
                type->targetType = print_eval_getType(index, node->typeRef);
            }
            break;
        case print_dwarf_tag_volatile_type:
            type->kind = print_type_volatile;
            if (node->hasTypeRef) {
                type->targetType = print_eval_getType(index, node->typeRef);
            }
            break;
        case print_dwarf_tag_enumeration_type:
            type->kind = print_type_enum;
            type->byteSize = node->hasByteSize ? (size_t)node->byteSize : 4;
            type->encoding = print_base_encoding_signed;
            break;
        default:
            type->kind = print_type_invalid;
            break;
    }
    return type;
}

static print_type_t *
print_eval_resolveType(print_type_t *type)
{
    print_type_t *cur = type;
    while (cur) {
        if (cur->kind == print_type_typedef || cur->kind == print_type_const || cur->kind == print_type_volatile) {
            cur = cur->targetType;
            continue;
        }
        break;
    }
    return cur;
}

static print_type_t *
print_eval_defaultU8(print_index_t *index)
{
    if (!index) {
        return NULL;
    }
    if (index->defaultU8) {
        return index->defaultU8;
    }
    print_type_t *type = (print_type_t *)alloc_calloc(1, sizeof(*type));
    if (!type) {
        return NULL;
    }
    type->kind = print_type_base;
    type->byteSize = 1;
    type->encoding = print_base_encoding_unsigned;
    type->name = print_eval_strdup("uint8_t");
    index->defaultU8 = type;
    if (index->typeCount >= index->typeCap) {
        int next = index->typeCap ? index->typeCap * 2 : 128;
        print_type_t **nextTypes = (print_type_t **)alloc_realloc(index->types, sizeof(*nextTypes) * (size_t)next);
        if (!nextTypes) {
            return type;
        }
        index->types = nextTypes;
        index->typeCap = next;
    }
    index->types[index->typeCount++] = type;
    return type;
}

static print_type_t *
print_eval_defaultU16(print_index_t *index)
{
    if (!index) {
        return NULL;
    }
    if (index->defaultU16) {
        return index->defaultU16;
    }
    print_type_t *type = (print_type_t *)alloc_calloc(1, sizeof(*type));
    if (!type) {
        return NULL;
    }
    type->kind = print_type_base;
    type->byteSize = 2;
    type->encoding = print_base_encoding_unsigned;
    type->name = print_eval_strdup("uint16_t");
    index->defaultU16 = type;
    if (index->typeCount >= index->typeCap) {
        int next = index->typeCap ? index->typeCap * 2 : 128;
        print_type_t **nextTypes = (print_type_t **)alloc_realloc(index->types, sizeof(*nextTypes) * (size_t)next);
        if (!nextTypes) {
            return type;
        }
        index->types = nextTypes;
        index->typeCap = next;
    }
    index->types[index->typeCount++] = type;
    return type;
}

static print_type_t *
print_eval_defaultU32(print_index_t *index)
{
    if (!index) {
        return NULL;
    }
    if (index->defaultU32) {
        return index->defaultU32;
    }
    print_type_t *type = (print_type_t *)alloc_calloc(1, sizeof(*type));
    if (!type) {
        return NULL;
    }
    type->kind = print_type_base;
    type->byteSize = 4;
    type->encoding = print_base_encoding_unsigned;
    type->name = print_eval_strdup("uint32_t");
    index->defaultU32 = type;
    if (index->typeCount >= index->typeCap) {
        int next = index->typeCap ? index->typeCap * 2 : 128;
        print_type_t **nextTypes = (print_type_t **)alloc_realloc(index->types, sizeof(*nextTypes) * (size_t)next);
        if (!nextTypes) {
            return type;
        }
        index->types = nextTypes;
        index->typeCap = next;
    }
    index->types[index->typeCount++] = type;
    return type;
}

static print_type_t *
print_eval_defaultU64(print_index_t *index)
{
    if (!index) {
        return NULL;
    }
    if (index->defaultU64) {
        return index->defaultU64;
    }
    print_type_t *type = (print_type_t *)alloc_calloc(1, sizeof(*type));
    if (!type) {
        return NULL;
    }
    type->kind = print_type_base;
    type->byteSize = 8;
    type->encoding = print_base_encoding_unsigned;
    type->name = print_eval_strdup("uint64_t");
    index->defaultU64 = type;
    if (index->typeCount >= index->typeCap) {
        int next = index->typeCap ? index->typeCap * 2 : 128;
        print_type_t **nextTypes = (print_type_t **)alloc_realloc(index->types, sizeof(*nextTypes) * (size_t)next);
        if (!nextTypes) {
            return type;
        }
        index->types = nextTypes;
        index->typeCap = next;
    }
    index->types[index->typeCount++] = type;
    return type;
}

static int
print_eval_loadIndex(print_index_t *index)
{
    const char *elfPath = debugger.libretro.exePath;
    if (!elfPath || !*elfPath || !index) {
        return 0;
    }
    uint32_t curText = debugger.machine.textBaseAddr;
    uint32_t curData = debugger.machine.dataBaseAddr;
    uint32_t curBss = debugger.machine.bssBaseAddr;
    uint64_t curBaseMapSignature = print_eval_baseMapSignature();
    if (index->elfPath[0] != '\0' && strcmp(index->elfPath, elfPath) == 0 &&
        index->cacheTextBaseAddr == curText &&
        index->cacheDataBaseAddr == curData &&
        index->cacheBssBaseAddr == curBss &&
        index->cacheBaseMapSignature == curBaseMapSignature) {
        return 1;
    }
    if (print_eval_debugEnabled()) {
        debug_printf("print: load debuginfo elf='%s' bases text=0x%08X data=0x%08X bss=0x%08X mode=%u stack=%zu sig=0x%llX\n",
                     elfPath,
                     (unsigned)curText,
                     (unsigned)curData,
                     (unsigned)curBss,
                     (unsigned)base_map_getMode(),
                     base_map_getStackCount(),
                     (unsigned long long)curBaseMapSignature);
    }
    uint64_t t0 = 0;
    if (print_eval_perfEnabled()) {
        t0 = print_eval_nowNs();
    }
    print_eval_clearIndex(index);
    strncpy(index->elfPath, elfPath, sizeof(index->elfPath) - 1);
    index->elfPath[sizeof(index->elfPath) - 1] = '\0';
    index->cacheTextBaseAddr = curText;
    index->cacheDataBaseAddr = curData;
    index->cacheBssBaseAddr = curBss;
    index->cacheBaseMapSignature = curBaseMapSignature;
    print_debuginfo_readelf_loadSymbols(elfPath, index);
    uint64_t tSymbols = 0;
    if (print_eval_perfEnabled()) {
        tSymbols = print_eval_nowNs();
    }
    print_debuginfo_readelf_loadDwarfInfo(elfPath, index);
    uint64_t tDwarf = 0;
    if (print_eval_perfEnabled()) {
        tDwarf = print_eval_nowNs();
    }
    (void)print_debuginfo_readelf_loadFrames(elfPath, index);
    uint64_t tFrames = 0;
    if (print_eval_perfEnabled()) {
        tFrames = print_eval_nowNs();
    }
    if (print_eval_debugEnabled()) {
        debug_printf("print: readelf pass nodes=%d symbols=%d\n", index->nodeCount, index->symbolCount);
    }
    if (index->nodeCount == 0) {
        if (print_eval_debugEnabled()) {
            debug_printf("print: falling back to objdump -G (STABS)\n");
        }
        (void)print_debuginfo_objdump_stabs_loadSymbols(elfPath, index);
        (void)print_debuginfo_objdump_stabs_loadLocals(elfPath, index);
        if (print_eval_debugEnabled()) {
            debug_printf("print: stabs pass symbols=%d vars=%d stabsFuncs=%d stabsScopes=%d stabsVars=%d\n",
                         index->symbolCount, index->varCount,
                         index->stabsFuncCount, index->stabsScopeCount, index->stabsVarCount);
        }
    }
    print_eval_buildSymbolLookup(index);
    uint64_t tLookup = 0;
    if (print_eval_perfEnabled()) {
        tLookup = print_eval_nowNs();
    }
    print_eval_buildVariables(index);
    if (print_eval_perfEnabled()) {
        uint64_t tVars = print_eval_nowNs();
        debug_printf("print: perf loadSymbols=%llums loadDwarf=%llums loadFrames=%llums buildLookup=%llums buildVars=%llums total=%llums nodes=%d syms=%d vars=%d fdes=%d\n",
                     (unsigned long long)((tSymbols - t0) / 1000000ull),
                     (unsigned long long)((tDwarf - tSymbols) / 1000000ull),
                     (unsigned long long)((tFrames - tDwarf) / 1000000ull),
                     (unsigned long long)((tLookup - tFrames) / 1000000ull),
                     (unsigned long long)((tVars - tLookup) / 1000000ull),
                     (unsigned long long)((tVars - t0) / 1000000ull),
                     index->nodeCount, index->symbolCount, index->varCount, index->fdeCount);
    }
    print_eval_defaultU32(index);
    return 1;
}

static print_variable_t *
print_eval_findVariable(print_index_t *index, const char *name)
{
    if (!index || !name) {
        return NULL;
    }
    for (int i = 0; i < index->varCount; ++i) {
        if (strcmp(index->vars[i].name, name) == 0) {
            return &index->vars[i];
        }
    }
    return NULL;
}

static print_symbol_t *
print_eval_findSymbol(print_index_t *index, const char *name)
{
    if (!index || !name) {
        return NULL;
    }
    for (int i = 0; i < index->symbolCount; ++i) {
        if (strcmp(index->symbols[i].name, name) == 0) {
            return &index->symbols[i];
        }
    }
    return NULL;
}

static int
print_eval_readMemory(uint32_t addr, void *out, size_t size)
{
    if (!out || size == 0) {
        return 0;
    }
    if (libretro_host_debugReadMemory(addr, out, size)) {
        return 1;
    }
    size_t ramSize = 0;
    const uint8_t *ram = (const uint8_t *)libretro_host_getMemory(RETRO_MEMORY_SYSTEM_RAM, &ramSize);
    if (!ram || ramSize == 0) {
        return 0;
    }
    const uint32_t ramBase = 0x00100000u;
    const uint32_t ramEnd = 0x001fffffu;
    for (size_t i = 0; i < size; ++i) {
        uint32_t cur = addr + (uint32_t)i;
        if (cur < ramBase || cur > ramEnd) {
            return 0;
        }
        uint32_t offset = cur & 0xffffu;
        if (offset >= ramSize) {
            return 0;
        }
        ((uint8_t *)out)[i] = ram[offset];
    }
    return 1;
}

static uint64_t
print_eval_readUnsigned(uint32_t addr, size_t size, int *ok)
{
    uint8_t buf[16];
    if (ok) {
        *ok = 0;
    }
    if (size == 0 || size > sizeof(buf)) {
        return 0;
    }
    if (!print_eval_readMemory(addr, buf, size)) {
        return 0;
    }
    uint64_t val = 0;
    for (size_t i = 0; i < size; ++i) {
        val = (val << 8) | (uint64_t)buf[i];
    }
    if (ok) {
        *ok = 1;
    }
    return val;
}

static int64_t
print_eval_signExtend(uint64_t value, size_t size)
{
    if (size == 0 || size >= 8) {
        return (int64_t)value;
    }
    size_t bits = size * 8;
    uint64_t signBit = 1ull << (bits - 1);
    if (value & signBit) {
        uint64_t mask = (~0ull) << bits;
        return (int64_t)(value | mask);
    }
    return (int64_t)value;
}

static void
print_eval_printLine(int indent, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char msg[1024];
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    int pad = indent > 0 ? indent : 0;
    if (pad > 120) {
        pad = 120;
    }
    char line[1152];
    int offset = 0;
    if (pad > 0) {
        memset(line, ' ', (size_t)pad);
        offset = pad;
    }
    size_t remaining = sizeof(line) - (size_t)offset - 1;
    strncpy(line + offset, msg, remaining);
    line[offset + remaining] = '\0';
    if (print_eval_captureEnabled && print_eval_captureBuffer && print_eval_captureCap > 0) {
        if (print_eval_captureLen > 0 && print_eval_captureLen + 1 < print_eval_captureCap) {
            print_eval_captureBuffer[print_eval_captureLen++] = '\n';
            print_eval_captureBuffer[print_eval_captureLen] = '\0';
        }
        size_t left = (print_eval_captureCap - 1) - print_eval_captureLen;
        if (left > 0) {
            strncpy(print_eval_captureBuffer + print_eval_captureLen, line, left);
            print_eval_captureBuffer[print_eval_captureLen + left] = '\0';
            print_eval_captureLen += strlen(print_eval_captureBuffer + print_eval_captureLen);
        }
        return;
    }
    debug_printf("%s", line);
}

static void
print_eval_dumpValueAt(print_type_t *type, uint32_t addr, int indent, const char *label)
{
    print_type_t *resolved = print_eval_resolveType(type);
    if (!resolved) {
        print_eval_printLine(indent, "%s: 0x%06X", label ? label : "<value>", addr);
        return;
    }
    switch (resolved->kind) {
        case print_type_base: {
            int ok = 0;
            size_t size = resolved->byteSize > 0 ? resolved->byteSize : 4;
            uint64_t val = print_eval_readUnsigned(addr, size, &ok);
            if (!ok) {
                if (print_eval_debugEnabled() && label && print_eval_debugWantsSymbol(label)) {
                    debug_printf("print: unreadable addr=0x%08X size=%u kind=base\n", (unsigned)addr, (unsigned)size);
                }
                print_eval_printLine(indent, "%s: <unreadable>", label ? label : "<value>");
                return;
            }
            if (resolved->encoding == print_base_encoding_float) {
                if (size == 4) {
                    uint32_t tmp = (uint32_t)val;
                    float f = 0.0f;
                    memcpy(&f, &tmp, sizeof(f));
                    print_eval_printLine(indent, "%s: %f", label ? label : "<value>", (double)f);
                } else if (size == 8) {
                    uint64_t tmp = val;
                    double d = 0.0;
                    memcpy(&d, &tmp, sizeof(d));
                    print_eval_printLine(indent, "%s: %f", label ? label : "<value>", d);
                } else {
                    print_eval_printLine(indent, "%s: 0x%llX", label ? label : "<value>", (unsigned long long)val);
                }
                return;
            }
            if (resolved->encoding == print_base_encoding_signed) {
                int64_t s = print_eval_signExtend(val, size);
                print_eval_printLine(indent, "%s: %lld (0x%llX)", label ? label : "<value>",
                                    (long long)s, (unsigned long long)val);
                return;
            }
            if (resolved->encoding == print_base_encoding_boolean) {
                print_eval_printLine(indent, "%s: %s", label ? label : "<value>", val ? "true" : "false");
                return;
            }
            print_eval_printLine(indent, "%s: %llu (0x%llX)", label ? label : "<value>",
                                (unsigned long long)val, (unsigned long long)val);
            return;
        }
        case print_type_pointer: {
            int ok = 0;
            size_t size = resolved->byteSize > 0 ? resolved->byteSize : 4;
            uint64_t ptrVal = print_eval_readUnsigned(addr, size, &ok);
            if (!ok) {
                if (print_eval_debugEnabled() && label && print_eval_debugWantsSymbol(label)) {
                    debug_printf("print: unreadable addr=0x%08X size=%u kind=ptr\n", (unsigned)addr, (unsigned)size);
                }
                print_eval_printLine(indent, "%s: <unreadable>", label ? label : "<value>");
                return;
            }
            print_eval_printLine(indent, "%s: 0x%08llX", label ? label : "<value>", (unsigned long long)ptrVal);
            return;
        }
        case print_type_struct: {
            print_eval_printLine(indent, "%s:", label ? label : (resolved->name && *resolved->name ? resolved->name : "<struct>"));
            for (int i = 0; i < resolved->memberCount; ++i) {
                print_member_t *member = &resolved->members[i];
                uint32_t memberAddr = addr + member->offset;
                const char *memberName = member->name ? member->name : "<member>";
                print_eval_dumpValueAt(member->type, memberAddr, indent + 2, memberName);
            }
            return;
        }
        case print_type_array: {
            size_t count = resolved->arrayCount;
            print_eval_printLine(indent, "%s:", label ? label : "<array>");
            if (!resolved->targetType || count == 0) {
                return;
            }
            size_t elemSize = resolved->targetType->byteSize > 0 ? resolved->targetType->byteSize : 1;
            for (size_t i = 0; i < count; ++i) {
                uint32_t elemAddr = addr + (uint32_t)(i * elemSize);
                char nameBuf[64];
                snprintf(nameBuf, sizeof(nameBuf), "[%zu]", i);
                print_eval_dumpValueAt(resolved->targetType, elemAddr, indent + 2, nameBuf);
            }
            return;
        }
        case print_type_enum: {
            int ok = 0;
            size_t size = resolved->byteSize > 0 ? resolved->byteSize : 4;
            uint64_t val = print_eval_readUnsigned(addr, size, &ok);
            if (!ok) {
                if (print_eval_debugEnabled() && label && print_eval_debugWantsSymbol(label)) {
                    debug_printf("print: unreadable addr=0x%08X size=%u kind=enum\n", (unsigned)addr, (unsigned)size);
                }
                print_eval_printLine(indent, "%s: <unreadable>", label ? label : "<value>");
                return;
            }
            int64_t s = print_eval_signExtend(val, size);
            print_eval_printLine(indent, "%s: %lld (0x%llX)", label ? label : "<value>",
                                (long long)s, (unsigned long long)val);
            return;
        }
        default:
            print_eval_printLine(indent, "%s: <unsupported>", label ? label : "<value>");
            return;
    }
}

static int
print_eval_readPointerValue(const print_value_t *value, uint32_t *outAddr)
{
    if (!value || !outAddr) {
        return 0;
    }
    if (value->hasImmediate) {
        *outAddr = (uint32_t)value->immediate;
        return 1;
    }
    if (!value->hasAddress) {
        return 0;
    }
    size_t size = 4;
    if (value->type && value->type->byteSize > 0) {
        size = value->type->byteSize;
    }
    int ok = 0;
    uint64_t val = print_eval_readUnsigned(value->address, size, &ok);
    if (!ok) {
        return 0;
    }
    *outAddr = (uint32_t)val;
    return 1;
}

static void
print_eval_skipSpace(const char **cursor)
{
    if (!cursor || !*cursor) {
        return;
    }
    const char *p = *cursor;
    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }
    *cursor = p;
}

static int
print_eval_parseIdentifier(const char **cursor, char *out, size_t cap)
{
    if (!cursor || !*cursor || !out || cap == 0) {
        return 0;
    }
    const char *p = *cursor;
    if (!isalpha((unsigned char)*p) && *p != '_') {
        return 0;
    }
    size_t len = 0;
    while ((isalnum((unsigned char)*p) || *p == '_') && len + 1 < cap) {
        out[len++] = *p++;
    }
    out[len] = '\0';
    *cursor = p;
    return (int)len;
}

static int
print_eval_parseNumber(const char **cursor, uint64_t *out)
{
    if (!cursor || !*cursor || !out) {
        return 0;
    }
    const char *p = *cursor;
    if (*p == '$') {
        ++p;
        if (!isxdigit((unsigned char)*p)) {
            return 0;
        }
        errno = 0;
        char *end = NULL;
        uint64_t val = strtoull(p, &end, 16);
        if (errno != 0 || !end || end == p) {
            return 0;
        }
        *out = val;
        *cursor = end;
        return 1;
    }
    if (!isdigit((unsigned char)*p)) {
        return 0;
    }
    errno = 0;
    char *end = NULL;
    uint64_t val = strtoull(p, &end, 0);
    if (errno != 0 || !end || end == p) {
        return 0;
    }
    *out = val;
    *cursor = end;
    return 1;
}

static print_value_t
print_eval_makeAddressValue(print_type_t *type, uint32_t addr)
{
    print_value_t val;
    memset(&val, 0, sizeof(val));
    val.type = type;
    val.address = addr;
    val.hasAddress = 1;
    return val;
}

static print_value_t
print_eval_makeImmediateValue(print_type_t *type, uint64_t immediate)
{
    print_value_t val;
    memset(&val, 0, sizeof(val));
    val.type = type;
    val.immediate = immediate;
    val.hasImmediate = 1;
    return val;
}

static print_type_t *
print_eval_makeTempPointerType(print_temp_type_t **list, print_type_t *target)
{
    if (!list) {
        return NULL;
    }
    print_type_t *type = (print_type_t *)alloc_calloc(1, sizeof(*type));
    if (!type) {
        return NULL;
    }
    type->kind = print_type_pointer;
    type->byteSize = 4;
    type->targetType = target;
    type->name = print_eval_strdup("");
    print_temp_type_t *node = (print_temp_type_t *)alloc_calloc(1, sizeof(*node));
    if (!node) {
        alloc_free(type);
        return NULL;
    }
    node->type = type;
    node->next = *list;
    *list = node;
    return type;
}

static void
print_eval_freeTempTypes(print_temp_type_t *list)
{
    while (list) {
        print_temp_type_t *next = list->next;
        if (list->type) {
            print_eval_freeString(list->type->name);
            alloc_free(list->type);
        }
        alloc_free(list);
        list = next;
    }
}

static int
print_eval_parseExpression(const char **cursor, print_index_t *index, print_value_t *out, print_temp_type_t **tempTypes, int typeOnly);

static int
print_eval_valueToImmediate(const print_value_t *value, int typeOnly, uint64_t *outImm)
{
    if (!value || !outImm) {
        return 0;
    }
    if (typeOnly) {
        *outImm = 0;
        return 1;
    }
    if (value->hasImmediate) {
        *outImm = value->immediate;
        return 1;
    }
    if (!value->hasAddress) {
        return 0;
    }
    print_type_t *resolved = print_eval_resolveType(value->type);
    if (resolved && resolved->kind == print_type_pointer) {
        uint32_t ptrAddr = 0;
        print_value_t ptrValue = *value;
        ptrValue.type = resolved;
        if (!print_eval_readPointerValue(&ptrValue, &ptrAddr)) {
            return 0;
        }
        *outImm = (uint64_t)ptrAddr;
        return 1;
    }
    size_t size = 4;
    if (resolved && resolved->byteSize > 0) {
        size = resolved->byteSize;
    }
    int ok = 0;
    uint64_t imm = print_eval_readUnsigned(value->address, size, &ok);
    if (!ok) {
        return 0;
    }
    *outImm = imm;
    return 1;
}

static int
print_eval_parsePrimary(const char **cursor, print_index_t *index, print_value_t *out, int typeOnly)
{
    print_eval_skipSpace(cursor);
    if (!cursor || !*cursor || !out || !index) {
        return 0;
    }
    if (**cursor == '(') {
        ++(*cursor);
        if (!print_eval_parseExpression(cursor, index, out, NULL, typeOnly)) {
            return 0;
        }
        print_eval_skipSpace(cursor);
        if (**cursor == ')') {
            ++(*cursor);
        }
        return 1;
    }
    char ident[256];
    if (print_eval_parseIdentifier(cursor, ident, sizeof(ident))) {
        print_variable_t *var = print_eval_findVariable(index, ident);
        if (var) {
            print_type_t *type = NULL;
            if (var->typeRef != 0) {
                type = print_eval_getType(index, var->typeRef);
            }
            if (!type && print_eval_debugEnabled() && print_eval_debugWantsSymbol(ident) && var->typeRef != 0) {
                debug_printf("print: var '%s' missing type die=0x%X\n", ident, (unsigned)var->typeRef);
            }
            if (!type && var->hasByteSize) {
                if (var->byteSize == 1) {
                    type = print_eval_defaultU8(index);
                } else if (var->byteSize == 2) {
                    type = print_eval_defaultU16(index);
                } else if (var->byteSize == 8) {
                    type = print_eval_defaultU64(index);
                } else {
                    type = print_eval_defaultU32(index);
                }
            }
            if (!type) {
                type = print_eval_defaultU32(index);
            }
            *out = print_eval_makeAddressValue(type, var->addr);
            if (print_eval_debugEnabled()) {
                size_t size = 0;
                print_type_t *resolved = print_eval_resolveType(type);
                if (resolved && resolved->byteSize > 0) {
                    size = resolved->byteSize;
                }
                if (size == 0) {
                    size = 4;
                }
                debug_printf("print: resolved var '%s' -> addr=0x%08X size=%u typeRef=0x%X\n",
                             ident, (unsigned)var->addr, (unsigned)size, (unsigned)var->typeRef);
            }
            return 1;
        }
        print_symbol_t *sym = print_eval_findSymbol(index, ident);
        if (sym) {
            print_type_t *type = print_eval_defaultU32(index);
            if (debugger_toolchainUsesHunkAddr2line()) {
                *out = print_eval_makeImmediateValue(type, (uint64_t)sym->addr);
                if (print_eval_debugEnabled()) {
                    debug_printf("print: resolved hunk sym '%s' -> ptr=0x%08X (immediate)\n",
                                 ident, (unsigned)sym->addr);
                }
            } else {
                *out = print_eval_makeAddressValue(type, sym->addr);
                if (print_eval_debugEnabled()) {
                    debug_printf("print: resolved sym '%s' -> addr=0x%08X (default u32)\n", ident, (unsigned)sym->addr);
                }
            }
            return 1;
        }
        unsigned long regValue = 0;
        if (machine_findReg(&debugger.machine, ident, &regValue)) {
            print_type_t *type = print_eval_defaultU32(index);
            *out = print_eval_makeImmediateValue(type, (uint64_t)regValue);
            if (print_eval_debugEnabled()) {
                debug_printf("print: resolved reg '%s' -> 0x%08lX\n", ident, regValue);
            }
            return 1;
        }
        // Local resolution can be expensive (requires scope + CFI lookup), so only
        // attempt it after globals/symbols/regs have failed.
        if (print_eval_resolveLocal(ident, index, out, typeOnly)) {
            if (print_eval_debugEnabled()) {
                if (out->hasAddress) {
                    debug_printf("print: resolved local '%s' -> addr=0x%08X\n", ident, (unsigned)out->address);
                } else if (out->hasImmediate) {
                    debug_printf("print: resolved local '%s' -> imm=0x%llX\n", ident, (unsigned long long)out->immediate);
                } else {
                    debug_printf("print: resolved local '%s'\n", ident);
                }
            }
            return 1;
        }
        return 0;
    }
    uint64_t number = 0;
    if (print_eval_parseNumber(cursor, &number)) {
        print_type_t *type = print_eval_defaultU32(index);
        *out = print_eval_makeImmediateValue(type, number);
        return 1;
    }
    return 0;
}

static int
print_eval_parseUnary(const char **cursor, print_index_t *index, print_value_t *out, print_temp_type_t **tempTypes, int typeOnly)
{
    print_eval_skipSpace(cursor);
    if (**cursor == '~') {
        ++(*cursor);
        print_value_t inner;
        if (!print_eval_parseUnary(cursor, index, &inner, tempTypes, typeOnly)) {
            return 0;
        }
        uint64_t innerImm = 0;
        if (!print_eval_valueToImmediate(&inner, typeOnly, &innerImm)) {
            return 0;
        }
        uint32_t v = (uint32_t)innerImm;
        *out = print_eval_makeImmediateValue(print_eval_defaultU32(index), (uint64_t)(~v));
        return 1;
    }
    if (**cursor == '&') {
        ++(*cursor);
        print_value_t inner;
        if (!print_eval_parseUnary(cursor, index, &inner, tempTypes, typeOnly)) {
            return 0;
        }
        if (!inner.hasAddress && !typeOnly) {
            return 0;
        }
        print_type_t *ptrType = print_eval_makeTempPointerType(tempTypes, inner.type);
        if (typeOnly) {
            *out = print_eval_makeImmediateValue(ptrType, 0);
            out->hasImmediate = 0;
        } else {
            *out = print_eval_makeImmediateValue(ptrType, inner.address);
        }
        return 1;
    }
    if (**cursor == '*') {
        ++(*cursor);
        print_value_t inner;
        if (!print_eval_parseUnary(cursor, index, &inner, tempTypes, typeOnly)) {
            return 0;
        }
        print_type_t *resolved = print_eval_resolveType(inner.type);
        if (resolved && resolved->kind == print_type_pointer) {
            if (typeOnly) {
                *out = print_eval_makeAddressValue(resolved->targetType, 0);
                out->hasAddress = 0;
            } else {
                uint32_t addr = 0;
                if (!print_eval_readPointerValue(&inner, &addr)) {
                    return 0;
                }
                *out = print_eval_makeAddressValue(resolved->targetType, addr);
            }
            return 1;
        }
        if (typeOnly) {
            *out = print_eval_makeAddressValue(print_eval_defaultU32(index), 0);
            out->hasAddress = 0;
            return 1;
        }
        uint32_t addr = 0;
        if (inner.hasImmediate) {
            addr = (uint32_t)inner.immediate;
        } else if (inner.hasAddress) {
            addr = inner.address;
        } else {
            return 0;
        }
        *out = print_eval_makeAddressValue(print_eval_defaultU32(index), addr);
        return 1;
    }
    return print_eval_parsePrimary(cursor, index, out, typeOnly);
}

static int
print_eval_parsePostfix(const char **cursor, print_index_t *index, print_value_t *out, print_temp_type_t **tempTypes, int typeOnly)
{
    if (!print_eval_parseUnary(cursor, index, out, tempTypes, typeOnly)) {
        return 0;
    }
    for (;;) {
        print_eval_skipSpace(cursor);
        if (**cursor == '.' || (**cursor == '-' && (*cursor)[1] == '>')) {
            int isArrow = 0;
            if (**cursor == '-') {
                isArrow = 1;
                *cursor += 2;
            } else {
                ++(*cursor);
            }
            print_eval_skipSpace(cursor);
            char memberName[256];
            if (!print_eval_parseIdentifier(cursor, memberName, sizeof(memberName))) {
                return 0;
            }
            print_type_t *resolved = print_eval_resolveType(out->type);
            uint32_t baseAddr = 0;
            if (isArrow) {
                if (!resolved || resolved->kind != print_type_pointer) {
                    return 0;
                }
                if (!typeOnly) {
                    print_value_t ptrVal = *out;
                    ptrVal.type = resolved;
                    if (!print_eval_readPointerValue(&ptrVal, &baseAddr)) {
                        return 0;
                    }
                }
                resolved = print_eval_resolveType(resolved->targetType);
            } else {
                if (!out->hasAddress && !typeOnly) {
                    return 0;
                }
                baseAddr = out->address;
            }
            if (!resolved || resolved->kind != print_type_struct) {
                return 0;
            }
            print_member_t *member = NULL;
            for (int i = 0; i < resolved->memberCount; ++i) {
                if (resolved->members[i].name && strcmp(resolved->members[i].name, memberName) == 0) {
                    member = &resolved->members[i];
                    break;
                }
            }
            if (!member) {
                return 0;
            }
            if (typeOnly) {
                *out = print_eval_makeAddressValue(member->type, 0);
                out->hasAddress = 0;
            } else {
                uint32_t memberAddr = baseAddr + member->offset;
                *out = print_eval_makeAddressValue(member->type, memberAddr);
            }
            continue;
        }
        if (**cursor == '[') {
            ++(*cursor);
            print_eval_skipSpace(cursor);
            uint64_t indexVal = 0;
            if (!print_eval_parseNumber(cursor, &indexVal)) {
                return 0;
            }
            print_eval_skipSpace(cursor);
            if (**cursor == ']') {
                ++(*cursor);
            }
            print_type_t *resolved = print_eval_resolveType(out->type);
            if (!resolved) {
                return 0;
            }
            print_type_t *elemType = NULL;
            uint32_t baseAddr = 0;
            if (resolved->kind == print_type_array) {
                elemType = resolved->targetType;
                if (!out->hasAddress && !typeOnly) {
                    return 0;
                }
                baseAddr = out->address;
            } else if (resolved->kind == print_type_pointer) {
                elemType = resolved->targetType;
                if (!typeOnly && !print_eval_readPointerValue(out, &baseAddr)) {
                    return 0;
                }
            } else {
                return 0;
            }
            size_t elemSize = elemType && elemType->byteSize > 0 ? elemType->byteSize : 1;
            if (typeOnly) {
                *out = print_eval_makeAddressValue(elemType, 0);
                out->hasAddress = 0;
            } else {
                uint32_t elemAddr = baseAddr + (uint32_t)(indexVal * elemSize);
                *out = print_eval_makeAddressValue(elemType, elemAddr);
            }
            continue;
        }
        break;
    }
    return 1;
}

static int
print_eval_parseMultiplicative(const char **cursor, print_index_t *index, print_value_t *out, print_temp_type_t **tempTypes, int typeOnly)
{
    if (!print_eval_parsePostfix(cursor, index, out, tempTypes, typeOnly)) {
        return 0;
    }
    for (;;) {
        print_eval_skipSpace(cursor);
        char op = **cursor;
        if (op != '*' && op != '/' && op != '%') {
            break;
        }
        ++(*cursor);
        print_value_t rhs;
        if (!print_eval_parsePostfix(cursor, index, &rhs, tempTypes, typeOnly)) {
            return 0;
        }
        uint64_t lhsImm = 0;
        uint64_t rhsImm = 0;
        if (!print_eval_valueToImmediate(out, typeOnly, &lhsImm) ||
            !print_eval_valueToImmediate(&rhs, typeOnly, &rhsImm)) {
            return 0;
        }
        uint64_t result = 0;
        if (op == '*') {
            result = lhsImm * rhsImm;
        } else if (op == '/') {
            if (rhsImm == 0) {
                return 0;
            }
            result = lhsImm / rhsImm;
        } else {
            if (rhsImm == 0) {
                return 0;
            }
            result = lhsImm % rhsImm;
        }
        *out = print_eval_makeImmediateValue(print_eval_defaultU32(index), result);
    }
    return 1;
}

static int
print_eval_parseAdditive(const char **cursor, print_index_t *index, print_value_t *out, print_temp_type_t **tempTypes, int typeOnly)
{
    if (!print_eval_parseMultiplicative(cursor, index, out, tempTypes, typeOnly)) {
        return 0;
    }
    for (;;) {
        print_eval_skipSpace(cursor);
        char op = **cursor;
        if (op != '+' && op != '-') {
            break;
        }
        ++(*cursor);
        print_value_t rhs;
        if (!print_eval_parseMultiplicative(cursor, index, &rhs, tempTypes, typeOnly)) {
            return 0;
        }
        uint64_t lhsImm = 0;
        uint64_t rhsImm = 0;
        if (!print_eval_valueToImmediate(out, typeOnly, &lhsImm) ||
            !print_eval_valueToImmediate(&rhs, typeOnly, &rhsImm)) {
            return 0;
        }
        uint64_t result = (op == '+') ? (lhsImm + rhsImm) : (lhsImm - rhsImm);
        *out = print_eval_makeImmediateValue(print_eval_defaultU32(index), result);
    }
    return 1;
}

static int
print_eval_parseBitwiseAnd(const char **cursor, print_index_t *index, print_value_t *out, print_temp_type_t **tempTypes, int typeOnly)
{
    if (!print_eval_parseAdditive(cursor, index, out, tempTypes, typeOnly)) {
        return 0;
    }
    for (;;) {
        print_eval_skipSpace(cursor);
        if (**cursor != '&') {
            break;
        }
        if ((*cursor)[1] == '&') {
            break;
        }
        ++(*cursor);
        print_value_t rhs;
        if (!print_eval_parseAdditive(cursor, index, &rhs, tempTypes, typeOnly)) {
            return 0;
        }
        uint64_t lhsImm = 0;
        uint64_t rhsImm = 0;
        if (!print_eval_valueToImmediate(out, typeOnly, &lhsImm) ||
            !print_eval_valueToImmediate(&rhs, typeOnly, &rhsImm)) {
            return 0;
        }
        uint32_t lhs32 = (uint32_t)lhsImm;
        uint32_t rhs32 = (uint32_t)rhsImm;
        *out = print_eval_makeImmediateValue(print_eval_defaultU32(index), (uint64_t)(lhs32 & rhs32));
    }
    return 1;
}

static int
print_eval_parseBitwiseXor(const char **cursor, print_index_t *index, print_value_t *out, print_temp_type_t **tempTypes, int typeOnly)
{
    if (!print_eval_parseBitwiseAnd(cursor, index, out, tempTypes, typeOnly)) {
        return 0;
    }
    for (;;) {
        print_eval_skipSpace(cursor);
        if (**cursor != '^') {
            break;
        }
        ++(*cursor);
        print_value_t rhs;
        if (!print_eval_parseBitwiseAnd(cursor, index, &rhs, tempTypes, typeOnly)) {
            return 0;
        }
        uint64_t lhsImm = 0;
        uint64_t rhsImm = 0;
        if (!print_eval_valueToImmediate(out, typeOnly, &lhsImm) ||
            !print_eval_valueToImmediate(&rhs, typeOnly, &rhsImm)) {
            return 0;
        }
        uint32_t lhs32 = (uint32_t)lhsImm;
        uint32_t rhs32 = (uint32_t)rhsImm;
        *out = print_eval_makeImmediateValue(print_eval_defaultU32(index), (uint64_t)(lhs32 ^ rhs32));
    }
    return 1;
}

static int
print_eval_parseBitwiseOr(const char **cursor, print_index_t *index, print_value_t *out, print_temp_type_t **tempTypes, int typeOnly)
{
    if (!print_eval_parseBitwiseXor(cursor, index, out, tempTypes, typeOnly)) {
        return 0;
    }
    for (;;) {
        print_eval_skipSpace(cursor);
        if (**cursor != '|') {
            break;
        }
        if ((*cursor)[1] == '|') {
            break;
        }
        ++(*cursor);
        print_value_t rhs;
        if (!print_eval_parseBitwiseXor(cursor, index, &rhs, tempTypes, typeOnly)) {
            return 0;
        }
        uint64_t lhsImm = 0;
        uint64_t rhsImm = 0;
        if (!print_eval_valueToImmediate(out, typeOnly, &lhsImm) ||
            !print_eval_valueToImmediate(&rhs, typeOnly, &rhsImm)) {
            return 0;
        }
        uint32_t lhs32 = (uint32_t)lhsImm;
        uint32_t rhs32 = (uint32_t)rhsImm;
        *out = print_eval_makeImmediateValue(print_eval_defaultU32(index), (uint64_t)(lhs32 | rhs32));
    }
    return 1;
}

static int
print_eval_parseExpression(const char **cursor, print_index_t *index, print_value_t *out, print_temp_type_t **tempTypes, int typeOnly)
{
    return print_eval_parseBitwiseOr(cursor, index, out, tempTypes, typeOnly);
}

static int
print_eval_resolveTypeFromExpression(const char *expr, print_index_t *index, print_type_t **outType)
{
    if (!expr || !outType) {
        return 0;
    }
    const char *cursor = expr;
    print_temp_type_t *tempTypes = NULL;
    print_value_t value;
    int ok = print_eval_parseExpression(&cursor, index, &value, &tempTypes, 1);
    if (ok && outType) {
        *outType = print_eval_resolveType(value.type);
    }
    print_eval_freeTempTypes(tempTypes);
    return ok;
}

static int
print_eval_completeMembers(print_index_t *index, const char *baseExpr, const char *prefix, const char *sep, char ***outList, int *outCount)
{
    if (!index || !baseExpr || !sep || !outList || !outCount) {
        return 0;
    }
    print_type_t *baseType = NULL;
    if (!print_eval_resolveTypeFromExpression(baseExpr, index, &baseType)) {
        return 0;
    }
    print_type_t *resolved = baseType;
    if (resolved && resolved->kind == print_type_pointer) {
        resolved = print_eval_resolveType(resolved->targetType);
    }
    if (!resolved || resolved->kind != print_type_struct) {
        return 0;
    }
    int cap = 0;
    char **items = NULL;
    int count = 0;
    for (int i = 0; i < resolved->memberCount; ++i) {
        const char *name = resolved->members[i].name;
        if (!name) {
            continue;
        }
        if (prefix && *prefix && strncmp(name, prefix, strlen(prefix)) != 0) {
            continue;
        }
        char fullName[512];
        strutil_join3Trunc(fullName, sizeof(fullName), baseExpr, sep, name);
        if (count >= cap) {
            int next = cap ? cap * 2 : 32;
            char **nextItems = (char **)alloc_realloc(items, sizeof(*nextItems) * (size_t)next);
            if (!nextItems) {
                break;
            }
            items = nextItems;
            cap = next;
        }
        items[count++] = print_eval_strdup(fullName);
    }
    if (count == 0) {
        alloc_free(items);
        return 0;
    }
    *outList = items;
    *outCount = count;
    return 1;
}

static int
print_eval_completeGlobals(print_index_t *index, const char *prefix, char ***outList, int *outCount)
{
    if (!index || !outList || !outCount) {
        return 0;
    }
    int cap = 0;
    char **items = NULL;
    int count = 0;
    for (int i = 0; i < index->varCount; ++i) {
        const char *name = index->vars[i].name;
        if (!name) {
            continue;
        }
        if (prefix && *prefix && strncmp(name, prefix, strlen(prefix)) != 0) {
            continue;
        }
        if (count >= cap) {
            int next = cap ? cap * 2 : 64;
            char **nextItems = (char **)alloc_realloc(items, sizeof(*nextItems) * (size_t)next);
            if (!nextItems) {
                break;
            }
            items = nextItems;
            cap = next;
        }
        items[count++] = print_eval_strdup(name);
    }
    for (int i = 0; i < index->symbolCount; ++i) {
        const char *name = index->symbols[i].name;
        if (!name) {
            continue;
        }
        if (prefix && *prefix && strncmp(name, prefix, strlen(prefix)) != 0) {
            continue;
        }
        int duplicate = 0;
        for (int j = 0; j < count; ++j) {
            if (items[j] && strcmp(items[j], name) == 0) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        if (count >= cap) {
            int next = cap ? cap * 2 : 64;
            char **nextItems = (char **)alloc_realloc(items, sizeof(*nextItems) * (size_t)next);
            if (!nextItems) {
                break;
            }
            items = nextItems;
            cap = next;
        }
        items[count++] = print_eval_strdup(name);
    }
    if (count == 0) {
        alloc_free(items);
        return 0;
    }
    *outList = items;
    *outCount = count;
    return 1;
}

int
print_eval_complete(const char *prefix, char ***outList, int *outCount)
{
    if (outList) {
        *outList = NULL;
    }
    if (outCount) {
        *outCount = 0;
    }
    if (!outList || !outCount) {
        return 0;
    }
    if (!print_eval_loadIndex(&print_eval_index)) {
        return 0;
    }
    const char *dot = prefix ? strrchr(prefix, '.') : NULL;
    const char *arrow = NULL;
    if (prefix) {
        for (const char *p = prefix; p[0] && p[1]; ++p) {
            if (p[0] == '-' && p[1] == '>') {
                arrow = p;
            }
        }
    }
    const char *sep = NULL;
    int sepLen = 0;
    if (arrow && (!dot || arrow > dot)) {
        sep = arrow;
        sepLen = 2;
    } else if (dot) {
        sep = dot;
        sepLen = 1;
    }
    if (sep) {
        char baseExpr[512];
        size_t baseLen = (size_t)(sep - prefix);
        if (baseLen >= sizeof(baseExpr)) {
            baseLen = sizeof(baseExpr) - 1;
        }
        memcpy(baseExpr, prefix, baseLen);
        baseExpr[baseLen] = '\0';
        const char *memberPrefix = sep + sepLen;
        char sepBuf[3];
        if (sepLen >= (int)sizeof(sepBuf)) {
            sepLen = (int)sizeof(sepBuf) - 1;
        }
        memcpy(sepBuf, sep, (size_t)sepLen);
        sepBuf[sepLen] = '\0';
        return print_eval_completeMembers(&print_eval_index, baseExpr, memberPrefix, sepBuf, outList, outCount);
    }
    return print_eval_completeGlobals(&print_eval_index, prefix, outList, outCount);
}

void
print_eval_freeCompletions(char **list, int count)
{
    if (!list || count <= 0) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        alloc_free(list[i]);
    }
    alloc_free(list);
}

int
print_eval_resolveSymbol(const char *name, uint32_t *outAddr, size_t *outSize)
{
    if (!name || !*name || !outAddr || !outSize) {
        return 0;
    }
    if (!print_eval_loadIndex(&print_eval_index)) {
        return 0;
    }
    print_variable_t *var = print_eval_findVariable(&print_eval_index, name);
    if (!var) {
        return 0;
    }
    print_type_t *type = NULL;
    if (var->typeRef != 0) {
        type = print_eval_getType(&print_eval_index, var->typeRef);
    }
    print_type_t *resolved = print_eval_resolveType(type);
    size_t size = 0;
    if (resolved && resolved->byteSize > 0) {
        size = resolved->byteSize;
    }
    if (size == 0 && var->hasByteSize && var->byteSize > 0) {
        size = var->byteSize;
    }
    if (size == 0) {
        size = 4;
    }
    *outAddr = var->addr;
    *outSize = size;
    return 1;
}

int
print_eval_resolveAddress(const char *expr, uint32_t *outAddr, size_t *outSize)
{
    if (!expr || !*expr || !outAddr || !outSize) {
        return 0;
    }
    if (!print_eval_loadIndex(&print_eval_index)) {
        return 0;
    }
    print_temp_type_t *tempTypes = NULL;
    print_value_t value;
    const char *cursor = expr;
    if (!print_eval_parseExpression(&cursor, &print_eval_index, &value, &tempTypes, 0)) {
        print_eval_freeTempTypes(tempTypes);
        return 0;
    }
    print_eval_skipSpace(&cursor);
    if (*cursor != '\0') {
        print_eval_freeTempTypes(tempTypes);
        return 0;
    }
    if (!value.hasAddress) {
        if (debugger_toolchainUsesHunkAddr2line() && value.hasImmediate) {
            print_type_t *resolvedImm = print_eval_resolveType(value.type);
            size_t sizeImm = 0;
            if (resolvedImm && resolvedImm->byteSize > 0) {
                sizeImm = resolvedImm->byteSize;
            }
            if (sizeImm == 0) {
                sizeImm = 4;
            }
            *outAddr = (uint32_t)(value.immediate & 0x00ffffffu);
            *outSize = sizeImm;
            print_eval_freeTempTypes(tempTypes);
            return 1;
        }
        print_eval_freeTempTypes(tempTypes);
        return 0;
    }
    print_type_t *resolved = print_eval_resolveType(value.type);
    size_t size = 0;
    if (resolved && resolved->byteSize > 0) {
        size = resolved->byteSize;
    }
    if (size == 0) {
        size = 4;
    }
    *outAddr = value.address;
    *outSize = size;
    print_eval_freeTempTypes(tempTypes);
    return 1;
}

int
print_eval_print(const char *expr)
{
    if (!expr || !*expr) {
        debug_error("print: missing expression");
        return 0;
    }
    if (!print_eval_loadIndex(&print_eval_index)) {
        debug_error("print: failed to load symbols (check --elf)");
        return 0;
    }
    print_temp_type_t *tempTypes = NULL;
    print_value_t value;
    const char *cursor = expr;
    if (!print_eval_parseExpression(&cursor, &print_eval_index, &value, &tempTypes, 0)) {
        print_eval_freeTempTypes(tempTypes);
        debug_error("print: failed to parse '%s'", expr);
        return 0;
    }
    print_eval_skipSpace(&cursor);
    if (*cursor != '\0') {
        print_eval_freeTempTypes(tempTypes);
        debug_error("print: failed to parse '%s'", expr);
        return 0;
    }
    if (value.hasAddress) {
        print_eval_dumpValueAt(value.type, value.address, 0, expr);
    } else if (value.hasImmediate) {
        print_type_t *resolved = print_eval_resolveType(value.type);
        if (!resolved) {
            print_eval_printLine(0, "%s: %llu (0x%llX)", expr,
                                (unsigned long long)value.immediate,
                                (unsigned long long)value.immediate);
        } else if (resolved->kind == print_type_pointer) {
            print_eval_printLine(0, "%s: 0x%08llX", expr, (unsigned long long)value.immediate);
        } else if (resolved->kind == print_type_base || resolved->kind == print_type_enum) {
            print_eval_printLine(0, "%s: %llu (0x%llX)", expr,
                                (unsigned long long)value.immediate,
                                (unsigned long long)value.immediate);
        } else {
            print_eval_printLine(0, "%s: 0x%llX", expr, (unsigned long long)value.immediate);
        }
    } else {
        debug_error("print: no value");
    }
    print_eval_freeTempTypes(tempTypes);
    return 1;
}

int
print_eval_eval(const char *expr, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!expr || !*expr) {
        return 0;
    }
    if (!print_eval_loadIndex(&print_eval_index)) {
        return 0;
    }
    print_temp_type_t *tempTypes = NULL;
    print_value_t value;
    const char *cursor = expr;
    if (!print_eval_parseExpression(&cursor, &print_eval_index, &value, &tempTypes, 0)) {
        print_eval_freeTempTypes(tempTypes);
        return 0;
    }
    print_eval_skipSpace(&cursor);
    if (*cursor != '\0') {
        print_eval_freeTempTypes(tempTypes);
        return 0;
    }
    print_eval_captureBuffer = out;
    print_eval_captureCap = cap;
    print_eval_captureLen = 0;
    print_eval_captureEnabled = 1;
    if (value.hasAddress) {
        print_eval_dumpValueAt(value.type, value.address, 0, expr);
    } else if (value.hasImmediate) {
        print_type_t *resolved = print_eval_resolveType(value.type);
        if (!resolved) {
            print_eval_printLine(0, "%s: %llu (0x%llX)", expr,
                                 (unsigned long long)value.immediate,
                                 (unsigned long long)value.immediate);
        } else if (resolved->kind == print_type_pointer) {
            print_eval_printLine(0, "%s: 0x%08llX", expr, (unsigned long long)value.immediate);
        } else if (resolved->kind == print_type_base || resolved->kind == print_type_enum) {
            print_eval_printLine(0, "%s: %llu (0x%llX)", expr,
                                 (unsigned long long)value.immediate,
                                 (unsigned long long)value.immediate);
        } else {
            print_eval_printLine(0, "%s: 0x%llX", expr, (unsigned long long)value.immediate);
        }
    } else {
        print_eval_captureEnabled = 0;
        print_eval_captureBuffer = NULL;
        print_eval_captureCap = 0;
        print_eval_captureLen = 0;
        print_eval_freeTempTypes(tempTypes);
        return 0;
    }
    print_eval_captureEnabled = 0;
    print_eval_captureBuffer = NULL;
    print_eval_captureCap = 0;
    print_eval_captureLen = 0;
    print_eval_freeTempTypes(tempTypes);
    return out[0] != '\0';
}

void
print_eval_invalidateCache(void)
{
    print_eval_clearIndex(&print_eval_index);
}
