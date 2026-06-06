AMIGA_GCC=/opt/amiga/bin/m68k-amigaos-gcc
CFLAGS=-fomit-frame-pointer -flto -fwhole-program -Wall -Werror -Os -nostdlib
LOAD9000_COMMON=startup.c lib9000.c hunk.c debug9000.c

all: load9000 load9013
	cp load9000 ../../../e9k-debugger/system
	cp load9013 ../../../e9k-debugger/system
	mkdir -p ../../../e9k-debugger/docs/vasm-example/C
	cp load9000 ../../../e9k-debugger/docs/vasm-example/C
	cp load9013 ../../../e9k-debugger/docs/vasm-example/C
	mkdir -p ../../../e9k-debugger/docs/vasm-example/DH0/C
	cp load9000 ../../../e9k-debugger/docs/vasm-example/DH0/C
	cp load9013 ../../../e9k-debugger/docs/vasm-example/DH0/C
	mkdir -p ../../../e9k-debugger/docs/vasm-13-example/C
	cp load9000 ../../../e9k-debugger/docs/vasm-13-example/C
	cp load9013 ../../../e9k-debugger/docs/vasm-13-example/C
	mkdir -p ../../../e9k-debugger/docs/vasm-13-example/DH0/C
	cp load9000 ../../../e9k-debugger/docs/vasm-13-example/DH0/C
	cp load9013 ../../../e9k-debugger/docs/vasm-13-example/DH0/C

load9000: main.c lib9000.c lib9000.h hunk.c hunk.h debug9000.c debug9000.h startup.c Makefile
	$(AMIGA_GCC) $(CFLAGS) $(LOAD9000_COMMON) main.c -o $@ -lamiga -lstubs -s

load9013: main13.c lib9000.c lib9000.h hunk.c hunk.h debug9000.c debug9000.h startup.c Makefile
	$(AMIGA_GCC) $(CFLAGS) $(LOAD9000_COMMON) main13.c -o $@ -lamiga -lstubs -s

clean:
	rm -f load9000 load9013 ../../../e9k-debugger/system/load9000 ../../../e9k-debugger/system/load9013
	rm -f ../../../e9k-debugger/docs/vasm-example/C/load9000 ../../../e9k-debugger/docs/vasm-example/C/load9013
	rm -f ../../../e9k-debugger/docs/vasm-example/DH0/C/load9000 ../../../e9k-debugger/docs/vasm-example/DH0/C/load9013
	rm -f ../../../e9k-debugger/docs/vasm-13-example/C/load9000 ../../../e9k-debugger/docs/vasm-13-example/C/load9013
	rm -f ../../../e9k-debugger/docs/vasm-13-example/DH0/C/load9000 ../../../e9k-debugger/docs/vasm-13-example/DH0/C/load9013
