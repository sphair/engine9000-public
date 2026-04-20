ifeq ($(UNAME_S),Darwin)

UI_TESTS=test-uiselect test-uibasic test-uitabbing test-uishader test-uihotkeys test-uiscrollbar test-uiasmscroll test-uiconsole test-uimemory test-uihelp
UI_REMAKE=remake-test-uiselect remake-test-uibasic remake-test-uitabbing remake-test-uishader remake-test-uihotkeys remake-test-uiscrollbar remake-test-uiasmscroll remake-test-uiconsole remake-test-uimemory remake-test-uihelp

# makers

make-test-uiselect: all
	./e9k-debugger --neogeo --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --make-test tests/results/ui/select

make-test-uibasic: all
	./e9k-debugger --neogeo --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --make-test tests/results/ui/basic


make-test-uihotkeys: all
	./e9k-debugger --neogeo --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --make-test tests/results/ui/hotkeys


make-test-uishader: all
	./e9k-debugger --neogeo --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --make-test tests/results/ui/shader


make-test-uitabbing: all
	./e9k-debugger --neogeo --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/tabbing.neo --make-test tests/results/ui/tabbing

make-test-uiscrollbar: all
	./e9k-debugger --neogeo --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --make-test tests/results/ui/scrollbar

make-test-uiasmscroll: all
	./e9k-debugger --neogeo --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --make-test tests/results/ui/asmscroll

make-test-uiconsole: all
	./e9k-debugger --neogeo --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --make-test tests/results/ui/console

make-test-uimemory: all
	./e9k-debugger --neogeo --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --make-test tests/results/ui/memory

make-test-uihelp: all
	./e9k-debugger --neogeo --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --make-test tests/results/ui/help


# remakers

remake-test-uibasic: all
	@printf "UI BASIC EXAMPLE ($@) ..."
	./e9k-debugger --neogeo --volume=0 --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --remake-test tests/results/ui/basic

remake-test-uiselect: all
	@printf "UI SELECT EXAMPLE ($@) ..."
	./e9k-debugger --neogeo --volume=0 --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --remake-test tests/results/ui/select

remake-test-uihotkeys: all
	@printf "UI HOTKEYS EXAMPLE ($@) ..."
	./e9k-debugger --neogeo --volume=0 --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --remake-test tests/results/ui/hotkeys

remake-test-uishader: all
	@printf "UI SHADER EXAMPLE ($@) ..."
	./e9k-debugger --neogeo --volume=0 --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --remake-test tests/results/ui/shader

remake-test-uitabbing: all
	@printf "UI TABBING EXAMPLE ($@) ..."
	./e9k-debugger --neogeo --volume=0 --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/tabbing.neo --remake-test tests/results/ui/tabbing

remake-test-uiscrollbar: all
	@printf "UI SCROLLBAR EXAMPLE ($@) ..."
	./e9k-debugger --neogeo --volume=0 --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --remake-test tests/results/ui/scrollbar

remake-test-uiasmscroll: all
	@printf "UI ASMSCROLL EXAMPLE ($@) ..."
	./e9k-debugger --neogeo --volume=0 --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --remake-test tests/results/ui/asmscroll

remake-test-uiconsole: all
	@printf "UI CONSOLE EXAMPLE ($@) ..."
	./e9k-debugger --neogeo --volume=0 --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --remake-test tests/results/ui/console

remake-test-uimemory: all
	@printf "UI MEMORY EXAMPLE ($@) ..."
	./e9k-debugger --neogeo --volume=0 --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --remake-test tests/results/ui/memory

remake-test-uihelp: all
	@printf "UI HELP EXAMPLE ($@) ..."
	./e9k-debugger --neogeo --volume=0 --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --remake-test tests/results/ui/help

# testers

test-uibasic: all
	@printf "UI BASIC EXAMPLE ($@) ..."
	@./e9k-debugger $(HEADLESS) --neogeo --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --test tests/results/ui/basic >> test.log 2>&1
	@echo "PASSED ✅"

test-uiselect: all
	@printf "UI SELECT EXAMPLE ($@) ..."
	@./e9k-debugger $(HEADLESS) --neogeo --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --test tests/results/ui/select >> test.log 2>&1
	@echo "PASSED ✅"

test-uihotkeys: all
	@printf "UI HOTKEYS EXAMPLE ($@) ..."
	@./e9k-debugger $(HEADLESS) --neogeo --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --test tests/results/ui/hotkeys >> test.log 2>&1
	@echo "PASSED ✅"

test-uishader: all
	@printf "UI SHADER EXAMPLE ($@) ..."
	@./e9k-debugger $(HEADLESS) --neogeo --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --test tests/results/ui/shader >> test.log 2>&1
	@echo "PASSED ✅"

test-uitabbing: all
	@printf "UI TABBING EXAMPLE ($@) ..."
	@./e9k-debugger $(HEADLESS) --neogeo --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/tabbing.neo --test tests/results/ui/tabbing >> test.log 2>&1
	@echo "PASSED ✅"

test-uiscrollbar: all
	@printf "UI SCROLLBAR EXAMPLE ($@) ..."
	@./e9k-debugger $(HEADLESS) --neogeo --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --test tests/results/ui/scrollbar >> test.log 2>&1
	@echo "PASSED ✅"

test-uiasmscroll: all
	@printf "UI ASMSCROLL EXAMPLE ($@) ..."
	@./e9k-debugger $(HEADLESS) --neogeo --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --test tests/results/ui/asmscroll >> test.log 2>&1
	@echo "PASSED ✅"

test-uiconsole: all
	@printf "UI CONSOLE EXAMPLE ($@) ..."
	@./e9k-debugger $(HEADLESS) --neogeo --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --test tests/results/ui/console >> test.log 2>&1
	@echo "PASSED ✅"

test-uimemory: all
	@printf "UI MEMORY EXAMPLE ($@) ..."
	@./e9k-debugger $(HEADLESS) --neogeo --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --test tests/results/ui/memory >> test.log 2>&1
	@echo "PASSED ✅"

test-uihelp: all
	@printf "UI HELP EXAMPLE ($@) ..."
	@./e9k-debugger $(HEADLESS) --neogeo --source-dir=./tests/neogeo/basic --elf=./tests/neogeo/basic/basic.elf --rom=./tests/neogeo/basic/basic.neo --test tests/results/ui/help >> test.log 2>&1
	@echo "PASSED ✅"

endif
