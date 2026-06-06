/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <dos/dosextens.h>
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <proto/exec.h>

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;

__asm__(
    ".text\n"
    ".globl _start\n"
    "_start:\n"
    "  movem.l d0/a0,-(sp)\n"
    "  move.l 4.w,_SysBase\n"
    "  movem.l (sp)+,d0/a0\n"
    "  move.l a0,-(sp)\n"
    "  move.l d0,-(sp)\n"
    "  jsr _startup_entry\n"
    "  addq.l #8,sp\n"
    "  rts\n");

extern int
load9000_main(STRPTR commandLine, LONG commandLineLen);

static char startup_dosName[] = "dos.library";

__attribute__((externally_visible)) int
startup_entry(LONG commandLineLen, STRPTR commandLine)
{
  int rc;

  DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)startup_dosName, 0);
  if (!DOSBase) {
    return 20;
  }

  rc = load9000_main(commandLine, commandLineLen);

  CloseLibrary((struct Library *)DOSBase);
  DOSBase = NULL;

  return rc;
}
