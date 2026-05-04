NEOGEO_TESTS=test-neogeosound test-neogeosavestate test-neogeostepping test-neogeosprite test-neogeotracker test-neogeolog
NEOGEO_REMAKE=remake-test-neogeosound remake-test-neogeosavestate remake-test-neogeostepping remake-test-neogeosprite remake-test-neogeotracker remake-test-neogeolog

# makers

make-test-neogeosound: all 
	./e9k-debugger --neogeo --rom=./tests/neogeo/sound/st.neo --make-test tests/results/neogeo/sound

make-test-neogeosavestate: all 
	./e9k-debugger --neogeo --rom=./tests/neogeo/basic/basic.neo --make-test tests/results/neogeo/savestate

make-test-neogeosprite: all 
	./e9k-debugger --neogeo --rom=./tests/neogeo/basic/basic.neo --make-test tests/results/neogeo/sprite

make-test-neogeolog: all 
	./e9k-debugger --neogeo --rom=./tests/neogeo/basic/basic.neo --make-test tests/results/neogeo/log

make-test-neogeotracker: all 
	./e9k-debugger --neogeo --rom=./tests/neogeo/basic/basic.neo --make-test tests/results/neogeo/tracker

make-test-neogeostepping: tests/neogeo/stepping/build/rom.elf
	./e9k-debugger --neogeo --source-dir=./tests/neogeo/stepping --elf=./tests/neogeo/stepping/build/rom.elf --rom=./tests/neogeo/stepping/build/stepping.neo --make-test tests/results/neogeo/stepping


# remakers

remake-test-neogeosound: all 
	@printf "NEO GEO SAVE SOUND ($@) ..."
	./e9k-debugger --neogeo --volume=0 --rom=./tests/neogeo/sound/st.neo --remake-test tests/results/neogeo/sound

remake-test-neogeosavestate: all 
	@printf "NEO GEO SAVE STATE ($@) ..."
	./e9k-debugger --neogeo --volume=0 --rom=./tests/neogeo/basic/basic.neo --remake-test tests/results/neogeo/savestate

remake-test-neogeosprite: all 
	@printf "NEO GEO SPRITE DEBUG ($@) ..."
	./e9k-debugger --neogeo --volume=0 --rom=./tests/neogeo/basic/basic.neo --remake-test tests/results/neogeo/sprite

remake-test-neogeolog: all 
	@printf "NEO GEO LOG ($@) ..."
	./e9k-debugger --neogeo --volume=0 --rom=./tests/neogeo/basic/basic.neo --remake-test tests/results/neogeo/log

remake-test-neogeotracker: all 
	@printf "NEO GEO MEMORY TRACKER ($@) ..."
	./e9k-debugger --neogeo --volume=0 --rom=./tests/neogeo/basic/basic.neo --remake-test tests/results/neogeo/tracker

remake-test-neogeostepping: tests/neogeo/stepping/build/rom.elf
	@printf "NEO GEO STEPPING ($@) ..."
	./e9k-debugger --neogeo --volume=0 --source-dir=./tests/neogeo/stepping --elf=./tests/neogeo/stepping/build/rom.elf --rom=./tests/neogeo/stepping/build/stepping.neo --remake-test tests/results/neogeo/stepping


# testers


test-neogeosound: all
	@printf "NEO GEO SOUND ($@) ..." 
	@./e9k-debugger $(HEADLESS) --neogeo --rom=./tests/neogeo/sound/st.neo --test tests/results/neogeo/sound >> test.log 2>&1
	@echo "PASSED ✅"

test-neogeosavestate: all
	@printf "NEO GEO SAVE STATE ($@) ..." 
	@./e9k-debugger $(HEADLESS) --neogeo --rom=./tests/neogeo/basic/basic.neo --test tests/results/neogeo/savestate >> test.log 2>&1
	@echo "PASSED ✅"

test-neogeosprite: all
	@printf "NEO GEO SPRITE DEBUG ($@) ..." 
	@./e9k-debugger $(HEADLESS) --neogeo --rom=./tests/neogeo/basic/basic.neo --test tests/results/neogeo/sprite >> test.log 2>&1
	@echo "PASSED ✅"

test-neogeolog: all
	@printf "NEO GEO LOG ($@) ..." 
	@./e9k-debugger $(HEADLESS) --neogeo --rom=./tests/neogeo/basic/basic.neo --test tests/results/neogeo/log >> test.log 2>&1
	@echo "PASSED ✅"

test-neogeotracker: all
	@printf "NEO GEO MEMORY TRACKER ($@) ..." 
	@./e9k-debugger $(HEADLESS) --neogeo --rom=./tests/neogeo/basic/basic.neo --test tests/results/neogeo/tracker >> test.log 2>&1
	@echo "PASSED ✅"

test-neogeostepping: tests/neogeo/stepping/build/rom.elf
	@printf "NEO GEO STEPPING ($@) ..." 
	@./e9k-debugger $(HEADLESS) --neogeo --source-dir=./tests/neogeo/stepping --elf=./tests/neogeo/stepping/build/rom.elf --rom=./tests/neogeo/stepping/build/stepping.neo --test tests/results/neogeo/stepping  >> test.log 2>&1
	@echo "PASSED ✅"

# assets

tests/neogeo/stepping/build/rom.elf:
	make -C ./tests/neogeo/stepping/
