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

#include "debug9000.h"
#include "hunk.h"
#include "lib9000.h"

#define printf lib9000_printf
#define strlen lib9000_strlen
#define strcmp lib9000_strcmp
#define strtoul lib9000_strtoul
#define memset lib9000_memset

#define DEFAULT_STACK_SIZE 16384UL

#ifndef BADDR
#define BADDR(bptr) ((APTR)((ULONG)(bptr) << 2))
#endif

typedef struct main_args {
  STRPTR exePath;
  STRPTR childArgs;
  ULONG childArgsSize;
  STRPTR commandLine;
  ULONG commandLineSize;
  STRPTR *argv;
  ULONG argvSize;
  int argc;
  ULONG stackSize;
  int breakEnabled;
} main_args_t;

extern LONG
main_callEntry(APTR entry, STRPTR argstr, LONG argLen, APTR stackTop);

/* Call a LoadSeg entry point with DOS CLI D0/A0 args on a temporary stack. */
__asm__(
    ".text\n"
    ".globl _main_callEntry\n"
    "_main_callEntry:\n"
    "  movem.l d2-d7/a2-a6,-(sp)\n"
    "  move.l 48(sp),a1\n"
    "  move.l 52(sp),a0\n"
    "  move.l 56(sp),d0\n"
    "  move.l 60(sp),a2\n"
    "  move.l sp,a3\n"
    "  move.l a2,sp\n"
    "  move.l a3,-(sp)\n"
    "  jsr (a1)\n"
    "  move.l (sp),a3\n"
    "  move.l a3,sp\n"
    "  movem.l (sp)+,d2-d7/a2-a6\n"
    "  rts\n");

static int
main_isArgSpace(char c)
{
  return ((UBYTE)c) <= ' ';
}

static void
main_freeArgs(main_args_t *args)
{
  if (args->childArgs) {
    lib9000_freeMemory(args->childArgs, args->childArgsSize);
  }
  if (args->argv) {
    lib9000_freeMemory(args->argv, args->argvSize);
  }
  if (args->commandLine) {
    lib9000_freeMemory(args->commandLine, args->commandLineSize);
  }
}

static BOOL
main_normalizeCommandLine(main_args_t *args)
{
  ULONG len;
  ULONG src;
  ULONG dst = 0;

  if (!lib9000_commandLine || lib9000_commandLineLen <= 0) {
    return FALSE;
  }

  len = (ULONG)lib9000_commandLineLen;
  args->commandLineSize = len + 1UL;
  args->commandLine =
      (STRPTR)lib9000_allocMemory(args->commandLineSize, MEMF_PUBLIC | MEMF_CLEAR);
  args->argvSize = sizeof(STRPTR) * (len + 1UL);
  args->argv = (STRPTR *)lib9000_allocMemory(args->argvSize, MEMF_PUBLIC | MEMF_CLEAR);
  if (!args->commandLine || !args->argv) {
    return FALSE;
  }

  CopyMem(lib9000_commandLine, args->commandLine, len);
  args->commandLine[len] = 0;

  src = 0;
  while (src < len) {
    int quoted = 0;

    while (src < len && main_isArgSpace(args->commandLine[src])) {
      src++;
    }
    if (src >= len) {
      break;
    }

    args->argv[args->argc++] = args->commandLine + dst;
    while (src < len) {
      char c = args->commandLine[src++];

      if (c == '"') {
        quoted = !quoted;
        continue;
      }
      if (!quoted && main_isArgSpace(c)) {
        break;
      }
      args->commandLine[dst++] = c;
    }
    args->commandLine[dst++] = 0;
  }

  return (args->argc > 0);
}

static BOOL
main_parseArgs(main_args_t *args)
{
  int exeIndex = 0;

  memset(args, 0, sizeof(*args));
  args->stackSize = DEFAULT_STACK_SIZE;

  if (!main_normalizeCommandLine(args)) {
    main_freeArgs(args);
    return FALSE;
  }

  while (exeIndex < args->argc) {
    if (strcmp((const char *)args->argv[exeIndex], "--quite") == 0 ||
        strcmp((const char *)args->argv[exeIndex], "--quiet") == 0) {
      lib9000_quiet = 1;
      exeIndex++;
      continue;
    }
    if (strcmp((const char *)args->argv[exeIndex], "--break") == 0) {
      args->breakEnabled = 1;
      exeIndex++;
      continue;
    }
    if (strcmp((const char *)args->argv[exeIndex], "--stack") == 0) {
      char *end;
      unsigned long parsedStackSize;

      if ((exeIndex + 1) >= args->argc || !args->argv[exeIndex + 1][0]) {
        main_freeArgs(args);
        return FALSE;
      }
      parsedStackSize = strtoul((const char *)args->argv[exeIndex + 1], &end, 10);
      if (*end != 0 || parsedStackSize == 0UL || parsedStackSize > 0x7FFFFFFFUL) {
        main_freeArgs(args);
        return FALSE;
      }
      args->stackSize = (ULONG)parsedStackSize;
      exeIndex += 2;
      continue;
    }
    break;
  }

  if (exeIndex >= args->argc) {
    main_freeArgs(args);
    return FALSE;
  }

  args->exePath = args->argv[exeIndex];
  args->childArgs = lib9000_buildArgString(args->argc,
                                           args->argv,
                                           exeIndex + 1,
                                           &args->childArgsSize);
  if (!args->childArgs) {
    main_freeArgs(args);
    return FALSE;
  }

  return TRUE;
}

static LONG
main_runLoadedSeg(BPTR seglist, STRPTR argstr, LONG argLen, ULONG stackSize)
{
  UBYTE *stack;
  APTR stackTop;
  APTR entry;
  struct Process *process;
  struct CommandLineInterface *cli = NULL;
  BPTR oldModule = 0;
  LONG rc;

  stack = (UBYTE *)lib9000_allocMemory(stackSize, MEMF_PUBLIC);
  if (!stack) {
    printf("AllocMem failed stackSize=%ld\n", stackSize);
    return 20;
  }

  stackTop = (APTR)((ULONG)(stack + stackSize) & ~3UL);
  entry = (APTR)((ULONG)BADDR(seglist) + 4UL);
  process = (struct Process *)FindTask(NULL);

  if (process && process->pr_CLI) {
    cli = (struct CommandLineInterface *)BADDR(process->pr_CLI);
    oldModule = cli->cli_Module;
    cli->cli_Module = seglist;
  }

  rc = main_callEntry(entry, argstr, argLen, stackTop);

  if (cli) {
    cli->cli_Module = oldModule;
  }

  lib9000_freeMemory(stack, stackSize);
  return rc;
}

static int
main_run(int argc, char **argv)
{
  BPTR seglist;
  hunk_seg_type_t *types = NULL;
  ULONG typeCount = 0;
  main_args_t args;
  LONG rc;

  (void)argc;
  (void)argv;

  if (!main_parseArgs(&args)) {
    printf("usage: load9013 [--quite] [--break] [--stack <bytes>] <exe> [args]\n");
    return 20;
  }

  if (!hunk_parseTypes(args.exePath, &types, &typeCount)) {
    lib9000_infoPrintf("warning: failed to parse hunk types, continuing\n");
    types = NULL;
    typeCount = 0;
  }

  seglist = lib9000_loadSegQuiet(args.exePath);

  if (!seglist) {
    printf("LoadSeg failed IoErr=%ld\n", IoErr());
    hunk_freeTypes(types, typeCount);
    main_freeArgs(&args);
    return 20;
  }

  debug9000_printSegList(seglist, types, typeCount, args.breakEnabled);

  rc = main_runLoadedSeg(seglist,
                         args.childArgs,
                         (LONG)(args.childArgsSize - 1UL),
                         args.stackSize);

  UnLoadSeg(seglist);
  hunk_freeTypes(types, typeCount);
  main_freeArgs(&args);

  return rc;
}

__attribute__((externally_visible)) int
load9000_main(STRPTR commandLine, LONG commandLineLen)
{
  lib9000_commandLine = commandLine;
  lib9000_commandLineLen = commandLineLen;

  return main_run(0, NULL);
}
