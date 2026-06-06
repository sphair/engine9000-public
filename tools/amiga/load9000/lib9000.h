#pragma once

#include <dos/dos.h>

typedef struct lib9000_arg_list {
  STRPTR commandLine;
  ULONG commandLineSize;
  STRPTR *argv;
  ULONG argvSize;
  int argc;
} lib9000_arg_list_t;

extern LONG lib9000_commandLineLen;
extern STRPTR lib9000_commandLine;
extern int lib9000_quiet;

ULONG
lib9000_strlen(const char *text);

int
lib9000_strcmp(const char *left, const char *right);

char *
lib9000_strcpy(char *out, const char *text);

void *
lib9000_memset(void *out, int value, ULONG byteCount);

unsigned long
lib9000_strtoul(const char *text, char **end, int base);

int
lib9000_printf(const char *format, ...);

int
lib9000_infoPrintf(const char *format, ...);

APTR
lib9000_allocMemory(ULONG byteSize, ULONG requirements);

void
lib9000_freeMemory(APTR memoryBlock, ULONG byteSize);

STRPTR
lib9000_buildArgString(int argc, STRPTR *argv, int firstArgIndex, ULONG *outSize);

BPTR
lib9000_loadSegQuiet(const STRPTR path);

BOOL
lib9000_parseCommandLine(const char *programName,
                          STRPTR commandLine,
                          LONG commandLineLen,
                          lib9000_arg_list_t *out);

void
lib9000_freeArgList(lib9000_arg_list_t *args);
