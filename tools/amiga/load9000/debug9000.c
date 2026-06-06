/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <exec/types.h>

#include "debug9000.h"
#include "lib9000.h"

#define printf lib9000_infoPrintf
#define strcpy lib9000_strcpy

#define ENGINE_9000_DEBUG_BREAK     ((volatile ULONG*)0xFC0010)
#define ENGINE_9000_DEBUG_PUSH_BASE ((volatile ULONG*)0xFC0014)
#define ENGINE_9000_DEBUG_PUSH_TYPE ((volatile ULONG*)0xFC0018)
#define ENGINE_9000_DEBUG_PUSH_SIZE ((volatile ULONG*)0xFC001C)

#define ENGINE_9000_DEBUG_SECTION_TEXT 0UL
#define ENGINE_9000_DEBUG_SECTION_DATA 1UL
#define ENGINE_9000_DEBUG_SECTION_BSS 2UL
#define ENGINE_9000_DEBUG_INVALID_SIZE 0xFFFFFFFFUL

#ifndef BADDR
#define BADDR(bptr) ((APTR)((ULONG)(bptr) << 2))
#endif

static const char *
debug9000_sectionTypeName(ULONG type)
{
  switch (type) {
  case HUNK_CODE:
    return ".text";
  case HUNK_DATA:
    return ".data";
  case HUNK_BSS:
    return ".bss";
  default:
    break;
  }
  return NULL;
}

static BOOL
debug9000_sectionTypeToDebugType(ULONG type, ULONG *outSection)
{
  if (outSection) {
    *outSection = ENGINE_9000_DEBUG_SECTION_TEXT;
  }

  switch (type) {
  case HUNK_CODE:
    if (outSection) {
      *outSection = ENGINE_9000_DEBUG_SECTION_TEXT;
    }
    return TRUE;
  case HUNK_DATA:
    if (outSection) {
      *outSection = ENGINE_9000_DEBUG_SECTION_DATA;
    }
    return TRUE;
  case HUNK_BSS:
    if (outSection) {
      *outSection = ENGINE_9000_DEBUG_SECTION_BSS;
    }
    return TRUE;
  default:
    break;
  }

  return FALSE;
}

static void
debug9000_formatSectionLabel(ULONG type, ULONG index, char *out, ULONG outCap)
{
  const char *sectionName = debug9000_sectionTypeName(type);
  ULONG writePos = 0;

  if (!out || outCap == 0UL) {
    return;
  }
  out[0] = 0;
  if (!sectionName) {
    return;
  }

  while (sectionName[writePos] && (writePos + 1UL) < outCap) {
    out[writePos] = sectionName[writePos];
    writePos++;
  }
  out[writePos] = 0;
  (void)index;
}

static void
debug9000_pushBase(ULONG section, ULONG base, ULONG size)
{
  *ENGINE_9000_DEBUG_PUSH_BASE = base;
  *ENGINE_9000_DEBUG_PUSH_TYPE = section;
  *ENGINE_9000_DEBUG_PUSH_SIZE = size;
}

void
debug9000_printSegList(BPTR seglist,
                       const hunk_seg_type_t *types,
                       ULONG typeCount,
                       int breakEnabled)
{
  ULONG idx = 0;
  BPTR seg = seglist;
  int haveBreak = 0;

  while (seg) {
    ULONG *p = (ULONG *)BADDR(seg);
    BPTR next = (BPTR)p[0];
    APTR base = (APTR)(p + 1);

    int haveType = (types && idx < typeCount) ? 1 : 0;
    ULONG t = haveType ? types[idx].type : 0;
    ULONG sz = haveType ? types[idx].sizeBytes : ENGINE_9000_DEBUG_INVALID_SIZE;
    ULONG pushSection = 0;
    char sectionLabel[32];
    int push = 0;

    if (sz == 0UL) {
      sz = ENGINE_9000_DEBUG_INVALID_SIZE;
    }
    sectionLabel[0] = 0;

    if (haveType) {
      debug9000_formatSectionLabel(t, idx, sectionLabel, sizeof(sectionLabel));

      if (breakEnabled && !haveBreak && t == HUNK_CODE) {
        ULONG breakAddr = (ULONG)base;
        ULONG breakAddr2 = ((ULONG)base) + 2UL;

        printf("engine9000: setting entry breakpoint=%08lx from %s\n",
               breakAddr,
               sectionLabel[0] ? sectionLabel : ".text");
        *ENGINE_9000_DEBUG_BREAK = breakAddr;
        printf("engine9000: setting entry breakpoint=%08lx from %s\n",
               breakAddr2,
               sectionLabel[0] ? sectionLabel : ".text");
        *ENGINE_9000_DEBUG_BREAK = breakAddr2;
        haveBreak = 1;
      }

      if (debug9000_sectionTypeToDebugType(t, &pushSection)) {
        push = 1;
      }
    } else {
      if (idx == 0UL) {
        strcpy(sectionLabel, ".text");
        printf("engine9000: fallback push .text base=%08lx\n", (ULONG)base);
        push = 1;
        pushSection = ENGINE_9000_DEBUG_SECTION_TEXT;
      } else if (idx == 1UL) {
        strcpy(sectionLabel, ".data");
        printf("engine9000: fallback push .data base=%08lx\n", (ULONG)base);
        push = 1;
        pushSection = ENGINE_9000_DEBUG_SECTION_DATA;
      } else if (idx == 2UL) {
        strcpy(sectionLabel, ".bss");
        printf("engine9000: fallback push .bss base=%08lx\n", (ULONG)base);
        push = 1;
        pushSection = ENGINE_9000_DEBUG_SECTION_BSS;
      }
    }

    if (push) {
      printf("engine9000: push %s base=%08lx size=%08lx\n",
             sectionLabel[0] ? sectionLabel : "section",
             (ULONG)base,
             sz);
      debug9000_pushBase(pushSection, (ULONG)base, sz);
    }

    seg = next;
    idx++;
  }
}
