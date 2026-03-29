AMIGA_TESTS=test-amigacustom \
	    test-amigalog \
	    test-amigavasm \
	    test-amigacoreoptions \
            test-amigasavestate \
            test-amigaconfig \
            test-amigalocals \
	    test-amigasmoke 

AMIGA_REMAKE=remake-test-amigacustom \
	     remake-test-amigalog \
	     remake-test-amigavasm \
	     remake-test-amigacoreoptions \
             remake-test-amigasavestate \
             remake-test-amigaconfig \
	     remake-test-amigalocals 

# makers

make-test-amigamemview: all tests/amiga/smoke/smoke.adf
	./e9k-debugger --amiga --uae=./tests/amiga/smoke/smoke.uae --make-test tests/results/amiga/memview

make-test-amigalocals: all tests/amiga/locals/locals.adf
	./e9k-debugger --amiga --source-dir=./tests/amiga/locals/ --uae=./tests/amiga/locals/locals.uae --hunk=./tests/amiga/locals/locals --make-test tests/results/amiga/locals

make-test-amigavasm: all tests/amiga/vasm/vasm.adf
	./e9k-debugger --amiga --source-dir=./tests/amiga/vasm/ --uae=./tests/amiga/vasm/vasm.uae --hunk=./tests/amiga/vasm/vasm --make-test tests/results/amiga/vasm

make-test-amigacustom: all tests/amiga/custom/custom.adf
	./e9k-debugger --amiga --source-dir=./tests/amiga/custom/ --uae=./tests/amiga/custom/custom.uae --make-test tests/results/amiga/custom

make-test-amigalog: all tests/amiga/custom/custom.adf
	./e9k-debugger --amiga --source-dir=./tests/amiga/custom/ --uae=./tests/amiga/custom/custom.uae --make-test tests/results/amiga/log

make-test-amigaconfig: all
	@-rm -f blank.uae
	./e9k-debugger --amiga --make-test tests/results/amiga/config
	@-rm -f blank.uae

make-test-amigacoreoptions: all tests/amiga/example/example.adf
	./e9k-debugger --amiga --source-dir=./tests/amiga/example/ --uae=./tests/amiga/example/example.uae --hunk=./tests/amiga/example/example --make-test tests/results/amiga/amigacoreoptions

make-test-amigasmoke: all tests/amiga/smoke/smoke.adf
	./e9k-debugger --amiga --uae=./tests/amiga/smoke/smoke.uae --make-smoke tests/results/amiga/smoke

make-test-amigasavestate: all tests/amiga/smoke/smoke.adf
	./e9k-debugger --amiga --uae=./tests/amiga/smoke/smoke.uae --make-test tests/results/amiga/savestate


# remakers

remake-test-amigamemview: all tests/amiga/smoke/smoke.adf
	@printf "AMIGA MEMVIEW ($@) ..."
	./e9k-debugger --amiga --volume=0 --uae=./tests/amiga/smoke/smoke.uae --remake-test tests/results/amiga/memview

remake-test-amigalocals: all tests/amiga/locals/locals.adf
	@printf "AMIGA LOCALS ($@) ..."
	./e9k-debugger --amiga --volume=0 --source-dir=./tests/amiga/locals/ --uae=./tests/amiga/locals/locals.uae --hunk=./tests/amiga/locals/locals --remake-test tests/results/amiga/locals

remake-test-amigavasm: all tests/amiga/vasm/vasm.adf
	@printf "AMIGA VASM ($@) ..."
	./e9k-debugger --amiga --volume=0 --source-dir=./tests/amiga/vasm/ --uae=./tests/amiga/vasm/vasm.uae --hunk=./tests/amiga/vasm/vasm --remake-test tests/results/amiga/vasm

remake-test-amigacustom: all tests/amiga/custom/custom.adf
	@printf "AMIGA CUSTOM ($@) ..."
	./e9k-debugger --amiga --volume=0 --source-dir=./tests/amiga/custom/ --uae=./tests/amiga/custom/custom.uae --remake-test tests/results/amiga/custom

remake-test-amigalog: all tests/amiga/custom/custom.adf
	@printf "AMIGA LOG ($@) ..."
	./e9k-debugger --amiga --volume=0 --source-dir=./tests/amiga/custom/ --uae=./tests/amiga/custom/custom.uae --remake-test tests/results/amiga/log

remake-test-amigaconfig: all 
	@printf "AMIGA CONFIG ($@) ..."
	@-rm -f blank.uae
	./e9k-debugger --amiga --volume=0 --remake-test tests/results/amiga/config
	@-rm -f blank.uae

remake-test-amigacoreoptions: all tests/amiga/example/example.adf
	@printf "AMIGA CORE OPTIONS ($@) ..."
	./e9k-debugger --amiga --source-dir=./tests/amiga/example/ --uae=./tests/amiga/example/example.uae --hunk=./tests/amiga/example/example --remake-test tests/results/amiga/amigacoreoptions

remake-test-amigasavestate: all tests/amiga/smoke/smoke.adf
	@printf "AMIGA SAVESTATE ($@) ..."
	./e9k-debugger --amiga --volume=0 --uae=./tests/amiga/smoke/smoke.uae --remake-test tests/results/amiga/savestate


# testers

test-amigaconfig: all
	@printf "AMIGA CONFIG ($@) ..."
	@-rm -f blank.uae
	@./e9k-debugger $(HEADLESS) --amiga --test tests/results/amiga/config >> test.log 2>&1
	@-rm -f blank.uae
	@echo " PASSED ✅"


test-amigalocals: all tests/amiga/locals/locals.adf
	@printf "AMIGA LOCALS ($@) ..."
	@./e9k-debugger $(HEADLESS) --volume=0 --amiga --source-dir=./tests/amiga/locals/ --uae=./tests/amiga/locals/locals.uae --hunk=./tests/amiga/locals/locals --test tests/results/amiga/locals >> test.log 2>&1
	@echo " PASSED ✅"

test-amigamemview: all tests/amiga/smoke/smoke.adf
	@printf "AMIGA MEMVIEW ($@) ..."
	@./e9k-debugger $(HEADLESS) --volume=0 --amiga --uae=./tests/amiga/smoke/smoke.uae --test tests/results/amiga/memview >> test.log 2>&1
	@echo " PASSED ✅"

test-amigavasm: all tests/amiga/vasm/vasm.adf
	@printf "AMIGA VASM ($@) ..."
	@./e9k-debugger $(HEADLESS) --volume=0 --amiga --source-dir=./tests/amiga/vasm/ --uae=./tests/amiga/vasm/vasm.uae --hunk=./tests/amiga/vasm/vasm --test tests/results/amiga/vasm >> test.log 2>&1
	@echo " PASSED ✅"

test-amigacustom: all tests/amiga/custom/custom.adf
	@printf "AMIGA CUSTOM ($@) ..."
	@./e9k-debugger $(HEADLESS) --volume=0 --amiga --source-dir=./tests/amiga/custom/ --uae=./tests/amiga/custom/custom.uae --test tests/results/amiga/custom >> test.log 2>&1
	@echo " PASSED ✅"

test-amigalog: all tests/amiga/custom/custom.adf
	@printf "AMIGA LOG ($@) ..."
	@./e9k-debugger $(HEADLESS) --volume=0 --amiga --source-dir=./tests/amiga/custom/ --uae=./tests/amiga/custom/custom.uae --test tests/results/amiga/log >> test.log 2>&1
	@echo " PASSED ✅"

test-amigacoreoptions: all tests/amiga/example/example.adf
	@printf "AMIGA CORE OPTIONS ($@) ..."
	@./e9k-debugger $(HEADLESS) --amiga --source-dir=./tests/amiga/example/ --uae=./tests/amiga/example/example.uae --hunk=./tests/amiga/example/example --test tests/results/amiga/amigacoreoptions  >> test.log 2>&1
	@echo "PASSED ✅"

test-amigasmoke: all tests/amiga/smoke/smoke.adf
	@printf "AMIGA SMOKE TEST ($@) ..."
	@./e9k-debugger $(HEADLESS) --volume=0 --amiga --uae=./tests/amiga/smoke/smoke.uae --smoke-test tests/results/amiga/smoke>> test.log 2>&1
	@echo "PASSED ✅"

test-amigasavestate: all tests/amiga/smoke/smoke.adf
	@printf "AMIGA SAVESTATE ($@) ..."
	@./e9k-debugger $(HEADLESS) --volume=0 --amiga --uae=./tests/amiga/smoke/smoke.uae --test tests/results/amiga/savestate  >> test.log 2>&1
	@echo "PASSED ✅"

# assets

tests/amiga/example/example.adf:
	make -C tests/amiga/example
