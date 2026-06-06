all: example_syms.exe DH0/example_run.exe

# example_syms.exe is the hunk with debug symbols - this does not need to be the program we actually run
#             the debugger will read debug symbols from this file
example_syms.exe: example.s Makefile
	vasmm68k_mot -Fhunkexe -linedebug -kick1hunks example.s -o example_syms.exe

# example_run.exe is the hunk load9013 will execute in the Amiga emulator - this does not need debug symbols
DH0/example_run.exe: example.s Makefile
	vasmm68k_mot -Fhunkexe -kick1hunks example.s -o DH0/example_run.exe

clean:
	rm -f DH0/example_run.exe
	rm -f example_syms.exe
