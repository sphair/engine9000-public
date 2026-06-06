/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "lib9000.h"

#include <dos/dos.h>
#include <exec/memory.h>
#include <clib/compiler-specific.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stdarg.h>

LONG lib9000_commandLineLen;
STRPTR lib9000_commandLine;
int lib9000_quiet;

ULONG
lib9000_strlen(const char *text)
{
  ULONG len = 0;

  if (!text) {
    return 0;
  }

  while (text[len]) {
    len++;
  }

  return len;
}

int
lib9000_strcmp(const char *left, const char *right)
{
  while (*left && *left == *right) {
    left++;
    right++;
  }

  return (int)(UBYTE)*left - (int)(UBYTE)*right;
}

char *
lib9000_strcpy(char *out, const char *text)
{
  char *start = out;

  while ((*out++ = *text++) != 0) {
  }

  return start;
}

void *
lib9000_memset(void *out, int value, ULONG byteCount)
{
  UBYTE *dst = (UBYTE *)out;

  while (byteCount > 0UL) {
    *dst++ = (UBYTE)value;
    byteCount--;
  }

  return out;
}

unsigned long
lib9000_strtoul(const char *text, char **end, int base)
{
  unsigned long value = 0;
  const char *p = text;

  if (base != 10) {
    if (end) {
      *end = (char *)text;
    }
    return 0;
  }

  while (*p >= '0' && *p <= '9') {
    unsigned long digit = (unsigned long)(*p - '0');

    if (value > 429496729UL || (value == 429496729UL && digit > 5UL)) {
      value = 0xffffffffUL;
      p++;
      while (*p >= '0' && *p <= '9') {
        p++;
      }
      break;
    }

    value = (value * 10UL) + digit;
    p++;
  }

  if (end) {
    *end = (char *)p;
  }

  return value;
}

static void
lib9000_rawPutChar(__REG__(d0, ULONG c),
                   __REG__(a3, BPTR out))
{
  UBYTE ch = (UBYTE)c;

  if (out) {
    Write(out, &ch, 1);
  }
}

static int
lib9000_vprintf(const char *format, va_list args)
{
  BPTR out;

  out = Output();
  RawDoFmt((CONST_STRPTR)format, (APTR)args, lib9000_rawPutChar, (APTR)out);

  return 0;
}

int
lib9000_printf(const char *format, ...)
{
  va_list args;
  int rc;

  va_start(args, format);
  rc = lib9000_vprintf(format, args);
  va_end(args);

  return rc;
}

int
lib9000_infoPrintf(const char *format, ...)
{
  va_list args;
  int rc;

  if (lib9000_quiet) {
    return 0;
  }

  va_start(args, format);
  rc = lib9000_vprintf(format, args);
  va_end(args);

  return rc;
}

APTR
lib9000_allocMemory(ULONG byteSize, ULONG requirements)
{
  return AllocMem(byteSize, requirements);
}

void
lib9000_freeMemory(APTR memoryBlock, ULONG byteSize)
{
  if (memoryBlock) {
    FreeMem(memoryBlock, byteSize);
  }
}

STRPTR
lib9000_buildArgString(int argc, STRPTR *argv, int firstArgIndex, ULONG *outSize)
{
  ULONG total = 2UL;
  STRPTR out;
  int i;

  if (outSize) {
    *outSize = 0;
  }

  for (i = firstArgIndex; i < argc; i++) {
    total += lib9000_strlen((const char *)argv[i]) + 1UL;
  }

  out = (STRPTR)lib9000_allocMemory(total, MEMF_PUBLIC | MEMF_CLEAR);
  if (!out) {
    return NULL;
  }

  {
    ULONG pos = 0;

    for (i = firstArgIndex; i < argc; i++) {
      ULONG len = lib9000_strlen((const char *)argv[i]);

      CopyMem(argv[i], out + pos, len);
      pos += len;
      out[pos++] = ' ';
    }
    out[pos++] = '\n';
    out[pos] = 0;

    if (outSize) {
      *outSize = pos + 1UL;
    }
  }

  return out;
}

BPTR
lib9000_loadSegQuiet(const STRPTR path)
{
  struct Process *process = (struct Process *)FindTask(NULL);
  APTR oldWin;
  BPTR seglist;

  oldWin = process->pr_WindowPtr;
  process->pr_WindowPtr = (APTR)-1;
  seglist = LoadSeg(path);
  process->pr_WindowPtr = oldWin;

  return seglist;
}

BOOL
lib9000_parseCommandLine(const char *programName,
                          STRPTR commandLine,
                          LONG commandLineLen,
                          lib9000_arg_list_t *out)
{
  ULONG len;
  ULONG src;
  ULONG dst = 0;
  ULONG inputEnd;

  if (!out) {
    return FALSE;
  }

  out->commandLine = NULL;
  out->commandLineSize = 0;
  out->argv = NULL;
  out->argvSize = 0;
  out->argc = 0;

  if (!commandLine || commandLineLen < 0) {
    return FALSE;
  }

  len = (ULONG)commandLineLen;
  out->commandLineSize = len + lib9000_strlen(programName) + 2UL;
  out->argvSize = sizeof(STRPTR) * (len + 2UL);
  out->commandLine = (STRPTR)AllocMem(out->commandLineSize, MEMF_PUBLIC | MEMF_CLEAR);
  out->argv = (STRPTR *)AllocMem(out->argvSize, MEMF_PUBLIC | MEMF_CLEAR);
  if (!out->commandLine || !out->argv) {
    lib9000_freeArgList(out);
    return FALSE;
  }

  out->argv[out->argc++] = out->commandLine;
  while (*programName) {
    out->commandLine[dst++] = *programName++;
  }
  out->commandLine[dst++] = 0;

  CopyMem(commandLine, out->commandLine + dst, len);
  out->commandLine[dst + len] = 0;

  src = dst;
  inputEnd = dst + len;
  while (src < inputEnd) {
    int quoted = 0;

    while (src < inputEnd && ((UBYTE)out->commandLine[src]) <= ' ') {
      src++;
    }
    if (src >= inputEnd) {
      break;
    }

    out->argv[out->argc++] = out->commandLine + dst;
    while (src < inputEnd) {
      char c = out->commandLine[src++];

      if (c == '"') {
        quoted = !quoted;
        continue;
      }
      if (!quoted && ((UBYTE)c) <= ' ') {
        break;
      }
      out->commandLine[dst++] = c;
    }
    out->commandLine[dst++] = 0;
  }

  return TRUE;
}

void
lib9000_freeArgList(lib9000_arg_list_t *args)
{
  if (!args) {
    return;
  }

  if (args->argv) {
    FreeMem(args->argv, args->argvSize);
  }
  if (args->commandLine) {
    FreeMem(args->commandLine, args->commandLineSize);
  }

  args->commandLine = NULL;
  args->commandLineSize = 0;
  args->argv = NULL;
  args->argvSize = 0;
  args->argc = 0;
}
