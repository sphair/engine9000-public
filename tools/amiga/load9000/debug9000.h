#pragma once

#include <dos/dos.h>

#include "hunk.h"

void
debug9000_printSegList(BPTR seglist,
                       const hunk_seg_type_t *types,
                       ULONG typeCount,
                       int breakEnabled);
