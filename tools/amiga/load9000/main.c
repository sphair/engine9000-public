/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <dos/dos.h>
#include <proto/dos.h>

#include "debug9000.h"
#include "hunk.h"
#include "lib9000.h"

#define printf lib9000_printf
#define strlen lib9000_strlen
#define strcmp lib9000_strcmp
#define strtoul lib9000_strtoul

#define DEFAULT_STACK_SIZE 16384UL

static int
main_run(int argc, char **argv)
{
  BPTR seglist;
  hunk_seg_type_t *types = NULL;
  ULONG typeCount = 0;
  STRPTR argstr;
  ULONG argstrSize = 0;
  LONG rc;
  int exeIndex = 1;
  int breakEnabled = 0;
  ULONG stackSize = DEFAULT_STACK_SIZE;
  
  while (exeIndex < argc) {
    if (strcmp(argv[exeIndex], "--quite") == 0 || strcmp(argv[exeIndex], "--quiet") == 0) {
      lib9000_quiet = 1;
      exeIndex++;
      continue;
    }
    if (strcmp(argv[exeIndex], "--break") == 0) {
      breakEnabled = 1;
      exeIndex++;
      continue;
    }
    if (strcmp(argv[exeIndex], "--stack") == 0) {
      char *end;
      unsigned long parsedStackSize;

      if ((exeIndex + 1) >= argc || !argv[exeIndex + 1][0]) {
        printf("usage: %s [--quite] [--break] [--stack <bytes>] <exe> [args]\n", argv[0]);
        return 20;
      }
      parsedStackSize = strtoul(argv[exeIndex + 1], &end, 10);
      if (*end != 0 || parsedStackSize == 0UL || parsedStackSize > 0x7FFFFFFFUL) {
        printf("usage: %s [--quite] [--break] [--stack <bytes>] <exe> [args]\n", argv[0]);
        return 20;
      }
      stackSize = (ULONG)parsedStackSize;
      exeIndex += 2;
      continue;
    }
    break;
  }

  if (argc <= exeIndex) {
    printf("usage: %s [--quite] [--break] [--stack <bytes>] <exe> [args]\n", argv[0]);
    return 20;
  }

  if (!hunk_parseTypes((const STRPTR)argv[exeIndex], &types, &typeCount)) {
    lib9000_infoPrintf("warning: failed to parse hunk types, continuing\n");
    types = NULL;
    typeCount = 0;
  }

  seglist = lib9000_loadSegQuiet((const STRPTR)argv[exeIndex]);

  if (!seglist) {
    printf("LoadSeg failed IoErr=%ld\n", IoErr());
    hunk_freeTypes(types, typeCount);
    return 20;
  }

  debug9000_printSegList(seglist, types, typeCount, breakEnabled);

  argstr = lib9000_buildArgString(argc, (STRPTR *)argv, exeIndex + 1, &argstrSize);

  rc = RunCommand(seglist, (LONG)stackSize, argstr ? argstr : (STRPTR) "\n",
                  argstr ? (LONG)strlen((const char*)argstr) : 1);

  lib9000_freeMemory(argstr, argstrSize);

  UnLoadSeg(seglist);
  hunk_freeTypes(types, typeCount);

  return rc;
}

__attribute__((externally_visible)) int
load9000_main(STRPTR commandLine, LONG commandLineLen)
{
  lib9000_arg_list_t args;
  int rc;

  if (!lib9000_parseCommandLine("load9000", commandLine, commandLineLen, &args)) {
    printf("usage: load9000 [--quite] [--break] [--stack <bytes>] <exe> [args]\n");
    return 20;
  }

  rc = main_run(args.argc, (char **)args.argv);

  lib9000_freeArgList(&args);
  return rc;
}
