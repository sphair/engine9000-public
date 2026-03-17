/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stdio.h>
#include <string.h>

#define ENGINE_9000_DEBUG_BASE_TEXT ((volatile ULONG*)0xFC0004)
#define ENGINE_9000_DEBUG_BASE_DATA ((volatile ULONG*)0xFC0008)
#define ENGINE_9000_DEBUG_BASE_BSS  ((volatile ULONG*)0xFC000C)
#define ENGINE_9000_DEBUG_BREAK     ((volatile ULONG*)0xFC0010)
#define ENGINE_9000_DEBUG_PUSH_BASE ((volatile ULONG*)0xFC0014)
#define ENGINE_9000_DEBUG_PUSH_TYPE ((volatile ULONG*)0xFC0018)
#define ENGINE_9000_DEBUG_PUSH_SIZE ((volatile ULONG*)0xFC001C)

#define ENGINE_9000_DEBUG_SECTION_TEXT 0UL
#define ENGINE_9000_DEBUG_SECTION_DATA 1UL
#define ENGINE_9000_DEBUG_SECTION_BSS 2UL
#define ENGINE_9000_DEBUG_INVALID_SIZE 0xFFFFFFFFUL

#define HUNK_HEADER 0x000003F3UL
#define HUNK_NAME 0x000003E8UL
#define HUNK_CODE 0x000003E9UL
#define HUNK_DATA 0x000003EAUL
#define HUNK_BSS 0x000003EBUL
#define HUNK_RELOC32 0x000003ECUL
#define HUNK_RELOC16 0x000003EDUL
#define HUNK_RELOC8 0x000003EEUL
#define HUNK_EXT 0x000003EFUL
#define HUNK_SYMBOL 0x000003F0UL
#define HUNK_DEBUG 0x000003F1UL
#define HUNK_END 0x000003F2UL
#define HUNK_RELOC32SHORT 0x000003FCUL
#define HUNK_DREL32 0x000003F7UL
#define HUNK_DREL16 0x000003F8UL
#define HUNK_DREL8 0x000003F9UL
#define HUNK_RELRELOC32 0x000003FDUL
#define HUNK_ABSRELOC16 0x000003FEUL
#define HUNK_PPC_CODE 0x000004E9UL

#define HUNK_SIZE_MASK 0x3FFFFFFFUL

/* Hunk type flags (see dos/doshunks.h) */
#define HUNKF_ADVISORY (1UL << 29)
#define HUNKF_CHIP     (1UL << 30)
#define HUNKF_FAST     (1UL << 31)
#define HUNK_TYPE_MASK 0x0000FFFFUL

#ifndef BADDR
#define BADDR(bptr) ((APTR)((ULONG)(bptr) << 2))
#endif

typedef struct SegType {
  ULONG type;
  ULONG sizeBytes;
} SegType;

static void
main_pushBase(ULONG section, ULONG base, ULONG size)
{
  *ENGINE_9000_DEBUG_PUSH_BASE = base;
  *ENGINE_9000_DEBUG_PUSH_TYPE = section;
  *ENGINE_9000_DEBUG_PUSH_SIZE = size;
}

static BOOL
ReadU32(BPTR fh, ULONG *out)
{
  return (Read(fh, out, 4) == 4);
}

static BOOL
ReadU16(BPTR fh, UWORD *out)
{
  UBYTE bytes[2];
  if (!out) {
    return FALSE;
  }
  if (Read(fh, bytes, 2) != 2) {
    return FALSE;
  }
  *out = (UWORD)(((UWORD)bytes[0] << 8) | (UWORD)bytes[1]);
  return TRUE;
}

static BOOL
SkipBytes(BPTR fh, ULONG n)
{
  return (Seek(fh, (LONG)n, OFFSET_CURRENT) != -1);
}

static BOOL
SkipLongs(BPTR fh, ULONG n)
{
  return SkipBytes(fh, n * 4UL);
}

static LONG
FilePos(BPTR fh)
{
  return Seek(fh, 0, OFFSET_CURRENT);
}

static BOOL
AlignLong(BPTR fh)
{
  LONG pos = FilePos(fh);
  ULONG rem;
  if (pos < 0) {
    return FALSE;
  }
  rem = (ULONG)pos & 3UL;
  if (rem == 0UL) {
    return TRUE;
  }
  return SkipBytes(fh, 4UL - rem);
}

static BOOL
SkipReloc(BPTR fh)
{
  for (;;) {
    ULONG n, target;
    if (!ReadU32(fh, &n))
      return FALSE;
    if (n == 0)
      break;
    if (!ReadU32(fh, &target))
      return FALSE;
    if (!SkipLongs(fh, n))
      return FALSE;
  }
  return TRUE;
}

static BOOL
SkipRelocShort(BPTR fh)
{
  for (;;) {
    UWORD n = 0;
    if (!ReadU16(fh, &n)) {
      return FALSE;
    }
    if (n == 0) {
      break;
    }
    if (!SkipBytes(fh, (ULONG)(n + 1U) * 2UL)) {
      return FALSE;
    }
  }
  return AlignLong(fh);
}

static BOOL
SkipExt(BPTR fh)
{
  for (;;) {
    ULONG extword, nameLongs, kind;
    if (!ReadU32(fh, &extword))
      return FALSE;
    if (extword == 0)
      break;

    kind = (extword >> 24) & 0xFF;
    nameLongs = extword & 0x00FFFFFFUL;

    if (!SkipLongs(fh, nameLongs))
      return FALSE;

    if (kind == 1 || kind == 2 || kind == 3) {
      if (!SkipLongs(fh, 1))
        return FALSE;
    } else {
      ULONG nrefs;
      if (!ReadU32(fh, &nrefs))
        return FALSE;
      while (nrefs--) {
        ULONG h, n;
        if (!ReadU32(fh, &h))
          return FALSE;
        if (!ReadU32(fh, &n))
          return FALSE;
        if (!SkipLongs(fh, n))
          return FALSE;
      }
    }
  }
  return TRUE;
}

static BOOL
ParseHunkTypes(const STRPTR path, SegType **out, ULONG *outCount)
{
  BPTR fh;
  ULONG id, tableSize, firstHunk, lastHunk;
  ULONG segCount;
  SegType *types;

  *out = NULL;
  *outCount = 0;

  fh = Open(path, MODE_OLDFILE);
  if (!fh) {
    printf("ParseHunkTypes: Open failed for '%s'\n", path);
    return FALSE;
  }

  if (!ReadU32(fh, &id)) {
    printf("ParseHunkTypes: ReadU32(id) failed pos=%d\n", FilePos(fh));
    Close(fh);
    return FALSE;
  }
  if (id != HUNK_HEADER) {
    printf("ParseHunkTypes: bad header id=%08x pos=%d\n", id, FilePos(fh));
    Close(fh);
    return FALSE;
  }

  for (;;) {
    ULONG n;
    if (!ReadU32(fh, &n)) {
      printf("ParseHunkTypes: ReadU32(nameLen) failed pos=%d\n", FilePos(fh));
      Close(fh);
      return FALSE;
    }
    if (n == 0)
      break;
    if (!SkipLongs(fh, n)) {
      printf("ParseHunkTypes: SkipLongs(name) failed n=%d pos=%d\n", n, FilePos(fh));
      Close(fh);
      return FALSE;
    }
  }

  if (!ReadU32(fh, &tableSize)) {
    printf("ParseHunkTypes: ReadU32(tableSize) failed pos=%d\n", FilePos(fh));
    Close(fh);
    return FALSE;
  }
  if (!ReadU32(fh, &firstHunk)) {
    printf("ParseHunkTypes: ReadU32(firstHunk) failed pos=%d\n", FilePos(fh));
    Close(fh);
    return FALSE;
  }
  if (!ReadU32(fh, &lastHunk)) {
    printf("ParseHunkTypes: ReadU32(lastHunk) failed pos=%d\n", FilePos(fh));
    Close(fh);
    return FALSE;
  }

  if (lastHunk < firstHunk) {
    printf("ParseHunkTypes: invalid range first=%d last=%d pos=%d\n",  firstHunk, lastHunk, FilePos(fh));
    Close(fh);
    return FALSE;
  }

  segCount = (lastHunk - firstHunk) + 1;

  if (tableSize < segCount) {
    printf("ParseHunkTypes: tableSize too small tableSize=%d segCount=%d first=%d last=%d pos=%d\n",
           tableSize, segCount, firstHunk, lastHunk, FilePos(fh));
    Close(fh);
    return FALSE;
  }

  types =
      (SegType *)AllocVec(sizeof(SegType) * segCount, MEMF_PUBLIC | MEMF_CLEAR);
  if (!types) {
    printf("ParseHunkTypes: AllocVec failed segCount=%d bytes=%d\n", segCount, (sizeof(SegType) * segCount));
    Close(fh);
    return FALSE;
  }

  if (!SkipLongs(fh, tableSize)) {
    printf(
        "ParseHunkTypes: SkipLongs(sizeTable) failed tableSize=%d pos=%d\n", tableSize, FilePos(fh));
    FreeVec(types);
    Close(fh);
    return FALSE;
  }

  {
    ULONG i = 0;

    while (i < segCount) {
      ULONG h;

      if (!ReadU32(fh, &h)) {
        printf("ParseHunkTypes: ReadU32(hunkId) failed seg=%d pos=%d\n", i, FilePos(fh));
        FreeVec(types);
        Close(fh);
        return FALSE;
      }

      {
        ULONG hid = h & HUNK_TYPE_MASK;

        switch (hid) {
      case HUNK_NAME: {
        ULONG n;
        if (!ReadU32(fh, &n)) {
          printf(
              "ParseHunkTypes: ReadU32(HUNK_NAME len) failed seg=%d pos=%d\n", i, FilePos(fh));
          FreeVec(types);
          Close(fh);
          return FALSE;
        }
        if (!SkipLongs(fh, n)) {
          printf("ParseHunkTypes: SkipLongs(HUNK_NAME) failed seg=%d n=%d pos=%d\n",  i, n, FilePos(fh));
          FreeVec(types);
          Close(fh);
          return FALSE;
        }
      } break;

      case HUNK_CODE:
      case HUNK_PPC_CODE:
      case HUNK_DATA:
      case HUNK_BSS: {
        ULONG sz;
        if (!ReadU32(fh, &sz)) {
          printf("ParseHunkTypes: ReadU32(size) failed hunk=%08x seg=%d pos=%d\n", hid, i, FilePos(fh));
          FreeVec(types);
          Close(fh);
          return FALSE;
        }
        types[i].type = (hid == HUNK_PPC_CODE) ? HUNK_CODE : hid;
        types[i].sizeBytes = (sz & HUNK_SIZE_MASK) * 4UL;
        if (hid != HUNK_BSS) {
          ULONG bytes = (sz & HUNK_SIZE_MASK) * 4UL;
          if (!SkipBytes(fh, bytes)) {
            printf("ParseHunkTypes: SkipBytes(payload) failed hunk=%08x seg=%d bytes=%d pos=%d\n",  hid, i, bytes, FilePos(fh));
            FreeVec(types);
            Close(fh);
            return FALSE;
          }
        }
      } break;

      case HUNK_RELOC32:
      case HUNK_RELOC16:
      case HUNK_RELOC8:
      case HUNK_DREL16:
      case HUNK_DREL8:
      case HUNK_ABSRELOC16: {
        if (!SkipReloc(fh)) {
          printf(
              "ParseHunkTypes: SkipReloc failed hunk=%08x seg=%d pos=%d\n",  h, i, FilePos(fh));
          FreeVec(types);
          Close(fh);
          return FALSE;
        }
      } break;

      case HUNK_RELOC32SHORT:
      case HUNK_DREL32:
      case HUNK_RELRELOC32: {
        if (!SkipRelocShort(fh)) {
          printf("ParseHunkTypes: SkipReloc(short) failed seg=%d pos=%d\n", i, FilePos(fh));
          FreeVec(types);
          Close(fh);
          return FALSE;
        }
      } break;

      case HUNK_EXT: {
        if (!SkipExt(fh)) {
          printf("ParseHunkTypes: SkipExt failed seg=%d pos=%d\n", i, FilePos(fh));
          FreeVec(types);
          Close(fh);
          return FALSE;
        }
      } break;

      case HUNK_SYMBOL: {
        for (;;) {
          ULONG n;
          if (!ReadU32(fh, &n)) {
            printf("ParseHunkTypes: ReadU32(HUNK_SYMBOL n) failed seg=%d pos=%d\n", i, FilePos(fh));
            FreeVec(types);
            Close(fh);
            return FALSE;
          }
          if (n == 0)
            break;
          if (!SkipLongs(fh, n)) {
            printf("ParseHunkTypes: SkipLongs(HUNK_SYMBOL name) failed seg=%d n=%d pos=%d\n", i, n, FilePos(fh));
            FreeVec(types);
            Close(fh);
            return FALSE;
          }
          if (!SkipLongs(fh, 1)) {
            printf("ParseHunkTypes: SkipLongs(HUNK_SYMBOL value) failed seg=%d pos=%d\n", i, FilePos(fh));
            FreeVec(types);
            Close(fh);
            return FALSE;
          }
        }
      } break;

      case HUNK_DEBUG: {
        ULONG n;
        if (!ReadU32(fh, &n)) {
          printf(
              "ParseHunkTypes: ReadU32(HUNK_DEBUG n) failed seg=%d pos=%d\n", i, FilePos(fh));
          FreeVec(types);
          Close(fh);
          return FALSE;
        }
        if (!SkipLongs(fh, n)) {
          printf("ParseHunkTypes: SkipLongs(HUNK_DEBUG) failed seg=%d n=%d pos=%d\n", i, n, FilePos(fh));
          FreeVec(types);
          Close(fh);
          return FALSE;
        }
      } break;

      case HUNK_END:
        i++;
        break;

      default:
        if (h & HUNKF_ADVISORY) {
          ULONG n;
          if (!ReadU32(fh, &n)) {
            printf("ParseHunkTypes: ReadU32(advisory n) failed seg=%d pos=%d\n", i, FilePos(fh));
            FreeVec(types);
            Close(fh);
            return FALSE;
          }
          if (!SkipLongs(fh, n)) {
            printf("ParseHunkTypes: SkipLongs(advisory) failed seg=%d n=%d pos=%d\n", i, n, FilePos(fh));
            FreeVec(types);
            Close(fh);
            return FALSE;
          }
        } else {
          printf("ParseHunkTypes: unknown hunk=%08x seg=%d pos=%d\n", h, i, FilePos(fh));
          FreeVec(types);
          Close(fh);
          return FALSE;
        }
      }
      }
    }
  }

  Close(fh);
  *out = types;
  *outCount = segCount;
  return TRUE;
}

static void
PrintSegList(BPTR seglist, const SegType *types, ULONG typeCount, int breakEnabled)
{
  ULONG idx = 0;
  BPTR seg = seglist;
  int haveText = 0;
  int haveData = 0;
  int haveBss = 0;
  int haveBreak = 0;

  while (seg) {
    ULONG *p = (ULONG *)BADDR(seg);
    BPTR next = (BPTR)p[0];
    APTR base = (APTR)(p + 1);

    int haveType = (types && idx < typeCount) ? 1 : 0;
    ULONG t = haveType ? types[idx].type : 0;
    ULONG sz = haveType ? types[idx].sizeBytes : ENGINE_9000_DEBUG_INVALID_SIZE;
    ULONG pushSection = 0;
    int push = 0;

    if (sz == 0) {
      sz = ENGINE_9000_DEBUG_INVALID_SIZE;
    }

    {
      int isEntrySeg = (t == HUNK_CODE || (idx == 0 && t == 0));
      if (!haveText && isEntrySeg) {
        printf("engine9000: setting .text base=%08x\n", (ULONG)base);
        *ENGINE_9000_DEBUG_BASE_TEXT = (ULONG)base;
        haveText = 1;
      }
      if (breakEnabled && !haveBreak && isEntrySeg) {
        ULONG breakAddr = (ULONG)base;
        ULONG breakAddr2 = ((ULONG)base) + 2;
        printf("engine9000: setting entry breakpoint=%08x\n", (ULONG)breakAddr);
        *ENGINE_9000_DEBUG_BREAK = breakAddr;
        printf("engine9000: setting entry breakpoint=%08x\n", (ULONG)breakAddr2);
        *ENGINE_9000_DEBUG_BREAK = breakAddr2;
        haveBreak = 1;
      }
      if (isEntrySeg) {
        push = 1;
        pushSection = ENGINE_9000_DEBUG_SECTION_TEXT;
      }
    }

    if (haveType) {
      switch (t) {
      case HUNK_DATA:
        if (!haveData) {
          printf("engine9000: setting .data base=%08x\n", (ULONG)base);      
          *ENGINE_9000_DEBUG_BASE_DATA = (ULONG)base;
          haveData = 1;
        }
        push = 1;
        pushSection = ENGINE_9000_DEBUG_SECTION_DATA;
        break;
      case HUNK_BSS:
        if (!haveBss) {
          printf("engine9000: setting .bss base=%08x\n", (ULONG)base);
          *ENGINE_9000_DEBUG_BASE_BSS = (ULONG)base;
          haveBss = 1;
        }
        push = 1;
        pushSection = ENGINE_9000_DEBUG_SECTION_BSS;
        break;
      case HUNK_CODE:
        push = 1;
        pushSection = ENGINE_9000_DEBUG_SECTION_TEXT;
        break;
      }
    } else {
      if (idx == 0 && !haveText) {
        printf("engine9000: fallback .text base=%08x\n", (ULONG)base);
        *ENGINE_9000_DEBUG_BASE_TEXT = (ULONG)base;
        haveText = 1;
        push = 1;
        pushSection = ENGINE_9000_DEBUG_SECTION_TEXT;
      } else if (idx == 1 && !haveData) {
        printf("engine9000: fallback .data base=%08x\n", (ULONG)base);
        *ENGINE_9000_DEBUG_BASE_DATA = (ULONG)base;
        haveData = 1;
        push = 1;
        pushSection = ENGINE_9000_DEBUG_SECTION_DATA;
      } else if (idx == 2 && !haveBss) {
        printf("engine9000: fallback .bss base=%08x\n", (ULONG)base);
        *ENGINE_9000_DEBUG_BASE_BSS = (ULONG)base;
        haveBss = 1;
        push = 1;
        pushSection = ENGINE_9000_DEBUG_SECTION_BSS;
      }
    }      

    if (push) {
      printf("engine9000: push section=%u base=%08x size=%08x\n", (unsigned)pushSection, (ULONG)base, sz);
      main_pushBase(pushSection, (ULONG)base, sz);
    }

    seg = next;
    idx++;
  }
}

static STRPTR
BuildArgString(int argc, char **argv, int firstArgIndex)
{
  ULONG total = 2;
  STRPTR s;
  int i;

  for (i = firstArgIndex; i < argc; i++)
    total += (ULONG)strlen(argv[i]) + 1;

  s = (STRPTR)AllocVec(total, MEMF_PUBLIC | MEMF_CLEAR);
  if (!s)
    return NULL;

  {
    ULONG pos = 0;
    for (i = firstArgIndex; i < argc; i++) {
      ULONG len = (ULONG)strlen(argv[i]);
      memcpy(s + pos, argv[i], len);
      pos += len;
      s[pos++] = ' ';
    }
    s[pos++] = '\n';
    s[pos] = 0;
  }

  return s;
}

int
main(int argc, char **argv)
{
  BPTR seglist;
  SegType *types = NULL;
  ULONG typeCount = 0;
  STRPTR argstr;
  LONG rc;
  int exeIndex = 1;
  int breakEnabled = 0;
  
  while (exeIndex < argc) {
    if (strcmp(argv[exeIndex], "--break") == 0) {
      breakEnabled = 1;
      exeIndex++;
      continue;
    }
    break;
  }

  if (argc <= exeIndex) {
    printf("usage: %s [--break] <exe> [args]\n", argv[0]);
    return 20;
  }

  if (!ParseHunkTypes((const STRPTR)argv[exeIndex], &types, &typeCount)) {
    printf("warning: failed to parse hunk types, continuing\n");
    types = NULL;
    typeCount = 0;
  }

  {
    struct Process *p = (struct Process *)FindTask(NULL);
    APTR oldWin = p->pr_WindowPtr;
    p->pr_WindowPtr = (APTR)-1;
    seglist = LoadSeg((const STRPTR)argv[exeIndex]);
    p->pr_WindowPtr = oldWin;
  }

  if (!seglist) {
    printf("LoadSeg failed IoErr=%d\n", IoErr());
    if (types)
      FreeVec(types);
    return 20;
  }

  PrintSegList(seglist, types, typeCount, breakEnabled);

  argstr = BuildArgString(argc, argv, exeIndex + 1);

  rc = RunCommand(seglist, 16384, argstr ? argstr : (STRPTR) "\n",
                  argstr ? (LONG)strlen((const char*)argstr) : 1);

  if (argstr)
    FreeVec(argstr);

  UnLoadSeg(seglist);
  if (types)
    FreeVec(types);

  return rc;
}
