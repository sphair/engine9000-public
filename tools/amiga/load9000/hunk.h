#pragma once

#include <dos/dos.h>

#define HUNK_CODE 0x000003E9UL
#define HUNK_DATA 0x000003EAUL
#define HUNK_BSS 0x000003EBUL

typedef struct hunk_seg_type {
  ULONG type;
  ULONG sizeBytes;
} hunk_seg_type_t;

BOOL
hunk_parseTypes(const STRPTR path, hunk_seg_type_t **out, ULONG *outCount);

void
hunk_freeTypes(hunk_seg_type_t *types, ULONG typeCount);
