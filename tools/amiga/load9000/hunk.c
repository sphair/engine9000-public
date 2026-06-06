/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <dos/dos.h>
#include <exec/memory.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "hunk.h"
#include "lib9000.h"

#define printf lib9000_infoPrintf

#define HUNK_HEADER 0x000003F3UL
#define HUNK_NAME 0x000003E8UL
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

#define HUNKF_ADVISORY (1UL << 29)
#define HUNK_TYPE_MASK 0x0000FFFFUL

static BOOL
hunk_readU32(BPTR fh, ULONG *out)
{
  return (Read(fh, out, 4) == 4);
}

static BOOL
hunk_readU16(BPTR fh, UWORD *out)
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
hunk_skipBytes(BPTR fh, ULONG byteCount)
{
  return (Seek(fh, (LONG)byteCount, OFFSET_CURRENT) != -1);
}

static BOOL
hunk_skipLongs(BPTR fh, ULONG longCount)
{
  return hunk_skipBytes(fh, longCount * 4UL);
}

static LONG
hunk_filePos(BPTR fh)
{
  return Seek(fh, 0, OFFSET_CURRENT);
}

static BOOL
hunk_alignLong(BPTR fh)
{
  LONG pos = hunk_filePos(fh);
  ULONG rem;

  if (pos < 0) {
    return FALSE;
  }

  rem = (ULONG)pos & 3UL;
  if (rem == 0UL) {
    return TRUE;
  }

  return hunk_skipBytes(fh, 4UL - rem);
}

static BOOL
hunk_skipReloc(BPTR fh)
{
  for (;;) {
    ULONG n;
    ULONG target;

    if (!hunk_readU32(fh, &n)) {
      return FALSE;
    }
    if (n == 0UL) {
      break;
    }
    if (!hunk_readU32(fh, &target)) {
      return FALSE;
    }
    if (!hunk_skipLongs(fh, n)) {
      return FALSE;
    }
  }

  return TRUE;
}

static BOOL
hunk_skipRelocShort(BPTR fh)
{
  for (;;) {
    UWORD n = 0;

    if (!hunk_readU16(fh, &n)) {
      return FALSE;
    }
    if (n == 0U) {
      break;
    }
    if (!hunk_skipBytes(fh, (ULONG)(n + 1U) * 2UL)) {
      return FALSE;
    }
  }

  return hunk_alignLong(fh);
}

static BOOL
hunk_skipExt(BPTR fh)
{
  for (;;) {
    ULONG extword;
    ULONG nameLongs;
    ULONG kind;

    if (!hunk_readU32(fh, &extword)) {
      return FALSE;
    }
    if (extword == 0UL) {
      break;
    }

    kind = (extword >> 24) & 0xFFUL;
    nameLongs = extword & 0x00FFFFFFUL;

    if (!hunk_skipLongs(fh, nameLongs)) {
      return FALSE;
    }

    if (kind == 1UL || kind == 2UL || kind == 3UL) {
      if (!hunk_skipLongs(fh, 1UL)) {
        return FALSE;
      }
    } else {
      ULONG nrefs;

      if (!hunk_readU32(fh, &nrefs)) {
        return FALSE;
      }
      while (nrefs--) {
        ULONG h;
        ULONG n;

        if (!hunk_readU32(fh, &h)) {
          return FALSE;
        }
        if (!hunk_readU32(fh, &n)) {
          return FALSE;
        }
        if (!hunk_skipLongs(fh, n)) {
          return FALSE;
        }
      }
    }
  }

  return TRUE;
}

static void
hunk_freeParseTypes(hunk_seg_type_t *types, ULONG typeCount, BPTR fh)
{
  hunk_freeTypes(types, typeCount);
  Close(fh);
}

BOOL
hunk_parseTypes(const STRPTR path, hunk_seg_type_t **out, ULONG *outCount)
{
  BPTR fh;
  ULONG id;
  ULONG tableSize;
  ULONG firstHunk;
  ULONG lastHunk;
  ULONG segCount;
  hunk_seg_type_t *types;

  *out = NULL;
  *outCount = 0;

  fh = Open(path, MODE_OLDFILE);
  if (!fh) {
    printf("hunk_parseTypes: Open failed for '%s'\n", path);
    return FALSE;
  }

  if (!hunk_readU32(fh, &id)) {
    printf("hunk_parseTypes: ReadU32(id) failed pos=%ld\n", hunk_filePos(fh));
    Close(fh);
    return FALSE;
  }
  if (id != HUNK_HEADER) {
    printf("hunk_parseTypes: bad header id=%08lx pos=%ld\n", id, hunk_filePos(fh));
    Close(fh);
    return FALSE;
  }

  for (;;) {
    ULONG n;

    if (!hunk_readU32(fh, &n)) {
      printf("hunk_parseTypes: ReadU32(nameLen) failed pos=%ld\n", hunk_filePos(fh));
      Close(fh);
      return FALSE;
    }
    if (n == 0UL) {
      break;
    }
    if (!hunk_skipLongs(fh, n)) {
      printf("hunk_parseTypes: SkipLongs(name) failed n=%ld pos=%ld\n",
             n,
             hunk_filePos(fh));
      Close(fh);
      return FALSE;
    }
  }

  if (!hunk_readU32(fh, &tableSize)) {
    printf("hunk_parseTypes: ReadU32(tableSize) failed pos=%ld\n", hunk_filePos(fh));
    Close(fh);
    return FALSE;
  }
  if (!hunk_readU32(fh, &firstHunk)) {
    printf("hunk_parseTypes: ReadU32(firstHunk) failed pos=%ld\n", hunk_filePos(fh));
    Close(fh);
    return FALSE;
  }
  if (!hunk_readU32(fh, &lastHunk)) {
    printf("hunk_parseTypes: ReadU32(lastHunk) failed pos=%ld\n", hunk_filePos(fh));
    Close(fh);
    return FALSE;
  }

  if (lastHunk < firstHunk) {
    printf("hunk_parseTypes: invalid range first=%ld last=%ld pos=%ld\n",
           firstHunk,
           lastHunk,
           hunk_filePos(fh));
    Close(fh);
    return FALSE;
  }

  segCount = (lastHunk - firstHunk) + 1UL;

  if (tableSize < segCount) {
    printf("hunk_parseTypes: tableSize too small tableSize=%ld segCount=%ld first=%ld last=%ld pos=%ld\n",
           tableSize,
           segCount,
           firstHunk,
           lastHunk,
           hunk_filePos(fh));
    Close(fh);
    return FALSE;
  }

  types = (hunk_seg_type_t *)AllocMem(sizeof(hunk_seg_type_t) * segCount,
                                      MEMF_PUBLIC | MEMF_CLEAR);
  if (!types) {
    printf("hunk_parseTypes: AllocMem failed segCount=%ld bytes=%ld\n",
           segCount,
           (sizeof(hunk_seg_type_t) * segCount));
    Close(fh);
    return FALSE;
  }

  if (!hunk_skipLongs(fh, tableSize)) {
    printf("hunk_parseTypes: SkipLongs(sizeTable) failed tableSize=%ld pos=%ld\n",
           tableSize,
           hunk_filePos(fh));
    hunk_freeParseTypes(types, segCount, fh);
    return FALSE;
  }

  {
    ULONG i = 0;

    while (i < segCount) {
      ULONG h;
      ULONG hid;

      if (!hunk_readU32(fh, &h)) {
        printf("hunk_parseTypes: ReadU32(hunkId) failed seg=%ld pos=%ld\n",
               i,
               hunk_filePos(fh));
        hunk_freeParseTypes(types, segCount, fh);
        return FALSE;
      }

      hid = h & HUNK_TYPE_MASK;

      switch (hid) {
      case HUNK_NAME: {
        ULONG n;

        if (!hunk_readU32(fh, &n)) {
          printf("hunk_parseTypes: ReadU32(HUNK_NAME len) failed seg=%ld pos=%ld\n",
                 i,
                 hunk_filePos(fh));
          hunk_freeParseTypes(types, segCount, fh);
          return FALSE;
        }
        if (!hunk_skipLongs(fh, n)) {
          printf("hunk_parseTypes: SkipLongs(HUNK_NAME) failed seg=%ld n=%ld pos=%ld\n",
                 i,
                 n,
                 hunk_filePos(fh));
          hunk_freeParseTypes(types, segCount, fh);
          return FALSE;
        }
      } break;

      case HUNK_CODE:
      case HUNK_PPC_CODE:
      case HUNK_DATA:
      case HUNK_BSS: {
        ULONG sz;

        if (!hunk_readU32(fh, &sz)) {
          printf("hunk_parseTypes: ReadU32(size) failed hunk=%08lx seg=%ld pos=%ld\n",
                 hid,
                 i,
                 hunk_filePos(fh));
          hunk_freeParseTypes(types, segCount, fh);
          return FALSE;
        }

        types[i].type = (hid == HUNK_PPC_CODE) ? HUNK_CODE : hid;
        types[i].sizeBytes = (sz & HUNK_SIZE_MASK) * 4UL;

        if (hid != HUNK_BSS) {
          ULONG bytes = (sz & HUNK_SIZE_MASK) * 4UL;

          if (!hunk_skipBytes(fh, bytes)) {
            printf("hunk_parseTypes: SkipBytes(payload) failed hunk=%08lx seg=%ld bytes=%ld pos=%ld\n",
                   hid,
                   i,
                   bytes,
                   hunk_filePos(fh));
            hunk_freeParseTypes(types, segCount, fh);
            return FALSE;
          }
        }
      } break;

      case HUNK_RELOC32:
      case HUNK_RELOC16:
      case HUNK_RELOC8:
      case HUNK_DREL16:
      case HUNK_DREL8:
      case HUNK_ABSRELOC16:
        if (!hunk_skipReloc(fh)) {
          printf("hunk_parseTypes: SkipReloc failed hunk=%08lx seg=%ld pos=%ld\n",
                 h,
                 i,
                 hunk_filePos(fh));
          hunk_freeParseTypes(types, segCount, fh);
          return FALSE;
        }
        break;

      case HUNK_RELOC32SHORT:
      case HUNK_DREL32:
      case HUNK_RELRELOC32:
        if (!hunk_skipRelocShort(fh)) {
          printf("hunk_parseTypes: SkipReloc(short) failed seg=%ld pos=%ld\n",
                 i,
                 hunk_filePos(fh));
          hunk_freeParseTypes(types, segCount, fh);
          return FALSE;
        }
        break;

      case HUNK_EXT:
        if (!hunk_skipExt(fh)) {
          printf("hunk_parseTypes: SkipExt failed seg=%ld pos=%ld\n", i, hunk_filePos(fh));
          hunk_freeParseTypes(types, segCount, fh);
          return FALSE;
        }
        break;

      case HUNK_SYMBOL:
        for (;;) {
          ULONG n;

          if (!hunk_readU32(fh, &n)) {
            printf("hunk_parseTypes: ReadU32(HUNK_SYMBOL n) failed seg=%ld pos=%ld\n",
                   i,
                   hunk_filePos(fh));
            hunk_freeParseTypes(types, segCount, fh);
            return FALSE;
          }
          if (n == 0UL) {
            break;
          }
          if (!hunk_skipLongs(fh, n)) {
            printf("hunk_parseTypes: SkipLongs(HUNK_SYMBOL name) failed seg=%ld n=%ld pos=%ld\n",
                   i,
                   n,
                   hunk_filePos(fh));
            hunk_freeParseTypes(types, segCount, fh);
            return FALSE;
          }
          if (!hunk_skipLongs(fh, 1UL)) {
            printf("hunk_parseTypes: SkipLongs(HUNK_SYMBOL value) failed seg=%ld pos=%ld\n",
                   i,
                   hunk_filePos(fh));
            hunk_freeParseTypes(types, segCount, fh);
            return FALSE;
          }
        }
        break;

      case HUNK_DEBUG: {
        ULONG n;

        if (!hunk_readU32(fh, &n)) {
          printf("hunk_parseTypes: ReadU32(HUNK_DEBUG n) failed seg=%ld pos=%ld\n",
                 i,
                 hunk_filePos(fh));
          hunk_freeParseTypes(types, segCount, fh);
          return FALSE;
        }
        if (!hunk_skipLongs(fh, n)) {
          printf("hunk_parseTypes: SkipLongs(HUNK_DEBUG) failed seg=%ld n=%ld pos=%ld\n",
                 i,
                 n,
                 hunk_filePos(fh));
          hunk_freeParseTypes(types, segCount, fh);
          return FALSE;
        }
      } break;

      case HUNK_END:
        i++;
        break;

      default:
        if (h & HUNKF_ADVISORY) {
          ULONG n;

          if (!hunk_readU32(fh, &n)) {
            printf("hunk_parseTypes: ReadU32(advisory n) failed seg=%ld pos=%ld\n",
                   i,
                   hunk_filePos(fh));
            hunk_freeParseTypes(types, segCount, fh);
            return FALSE;
          }
          if (!hunk_skipLongs(fh, n)) {
            printf("hunk_parseTypes: SkipLongs(advisory) failed seg=%ld n=%ld pos=%ld\n",
                   i,
                   n,
                   hunk_filePos(fh));
            hunk_freeParseTypes(types, segCount, fh);
            return FALSE;
          }
        } else {
          printf("hunk_parseTypes: unknown hunk=%08lx seg=%ld pos=%ld\n",
                 h,
                 i,
                 hunk_filePos(fh));
          hunk_freeParseTypes(types, segCount, fh);
          return FALSE;
        }
        break;
      }
    }
  }

  Close(fh);
  *out = types;
  *outCount = segCount;
  return TRUE;
}

void
hunk_freeTypes(hunk_seg_type_t *types, ULONG typeCount)
{
  if (types) {
    FreeMem(types, sizeof(hunk_seg_type_t) * typeCount);
  }
}
