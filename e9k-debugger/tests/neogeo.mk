NEOGEO_TESTS=test-neogeosound test-neogeosavestate test-neogeostepping test-neogeoprint test-neogeoz80 test-neogeosprite test-neogeotracker test-neogeolog test-neogeomemview test-neogeozip test-neogeofolder
NEOGEO_REMAKE=remake-test-neogeosound remake-test-neogeosavestate remake-test-neogeostepping remake-test-neogeoprint remake-test-neogeoz80 remake-test-neogeosprite remake-test-neogeotracker remake-test-neogeolog remake-test-neogeomemview remake-test-neogeozip remake-test-neogeofolder

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

make-test-neogeomemview: all 
	./e9k-debugger --volume=0 --neogeo --rom=./tests/neogeo/sound/st.neo --make-test tests/results/neogeo/memview

make-test-neogeostepping: tests/neogeo/stepping/build/rom.elf
	./e9k-debugger --neogeo --source-dir=./tests/neogeo/stepping --elf=./tests/neogeo/stepping/build/rom.elf --rom=./tests/neogeo/stepping/build/stepping.neo --make-test tests/results/neogeo/stepping

make-test-neogeoprint: tests/neogeo/stepping/build/rom.elf
	./e9k-debugger --neogeo --source-dir=./tests/neogeo/stepping --elf=./tests/neogeo/stepping/build/rom.elf --rom=./tests/neogeo/stepping/build/stepping.neo --make-test tests/results/neogeo/print

make-test-neogeoz80: tests/neogeo/neogeoz80/neogeoz80.neo tests/neogeo/neogeoz80/rom.elf tests/neogeo/neogeoz80/demo_driver.z80srcmap
	./e9k-debugger --neogeo --source-dir=./tests/neogeo/neogeoz80 --elf=./tests/neogeo/neogeoz80/rom.elf --rom=./tests/neogeo/neogeoz80/neogeoz80.neo --make-test tests/results/neogeo/neogeoz80

make-test-neogeozip: all
	./e9k-debugger --neogeo --rom=./tests/neogeo/zip/test.zip --make-test tests/results/neogeo/zip

make-test-neogeofolder: all
	./e9k-debugger --neogeo --rom-folder=./tests/neogeo/folder/test --make-test tests/results/neogeo/folder


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

remake-test-neogeomemview: all 
	@printf "NEO GEO MEMORY VIEW ($@) ..."
	./e9k-debugger --neogeo --volume=0 --rom=./tests/neogeo/sound/st.neo --remake-test tests/results/neogeo/memview

remake-test-neogeostepping: tests/neogeo/stepping/build/rom.elf
	@printf "NEO GEO STEPPING ($@) ..."
	./e9k-debugger --neogeo --volume=0 --source-dir=./tests/neogeo/stepping --elf=./tests/neogeo/stepping/build/rom.elf --rom=./tests/neogeo/stepping/build/stepping.neo --remake-test tests/results/neogeo/stepping

remake-test-neogeoprint: tests/neogeo/stepping/build/rom.elf
	@printf "NEO GEO PRINT ($@) ..."
	./e9k-debugger --neogeo --volume=0 --source-dir=./tests/neogeo/stepping --elf=./tests/neogeo/stepping/build/rom.elf --rom=./tests/neogeo/stepping/build/stepping.neo --remake-test tests/results/neogeo/print

remake-test-neogeoz80: tests/neogeo/neogeoz80/neogeoz80.neo tests/neogeo/neogeoz80/rom.elf tests/neogeo/neogeoz80/demo_driver.z80srcmap
	@printf "NEO GEO Z80 DEBUG ($@) ..."
	./e9k-debugger --neogeo --volume=0 --source-dir=./tests/neogeo/neogeoz80 --elf=./tests/neogeo/neogeoz80/rom.elf --rom=./tests/neogeo/neogeoz80/neogeoz80.neo --remake-test tests/results/neogeo/neogeoz80

remake-test-neogeozip: all
	@printf "NEO GEO ZIP ROM ($@) ..."
	./e9k-debugger --neogeo --volume=0 --rom=./tests/neogeo/zip/test.zip --remake-test tests/results/neogeo/zip

remake-test-neogeofolder: all
	@printf "NEO GEO FOLDER ROM ($@) ..."
	./e9k-debugger --neogeo --volume=0 --rom-folder=./tests/neogeo/folder/test --remake-test tests/results/neogeo/folder


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

test-neogeomemview: all
	@printf "NEO GEO MEMORY VIEW ($@) ..." 
	@./e9k-debugger $(HEADLESS) --volume=0 --neogeo --rom=./tests/neogeo/sound/st.neo --test tests/results/neogeo/memview >> test.log 2>&1
	@echo "PASSED ✅"

test-neogeostepping: tests/neogeo/stepping/build/rom.elf
	@printf "NEO GEO STEPPING ($@) ..." 
	@./e9k-debugger $(HEADLESS) --neogeo --source-dir=./tests/neogeo/stepping --elf=./tests/neogeo/stepping/build/rom.elf --rom=./tests/neogeo/stepping/build/stepping.neo --test tests/results/neogeo/stepping  >> test.log 2>&1
	@echo "PASSED ✅"

test-neogeoprint: tests/neogeo/stepping/build/rom.elf
	@printf "NEO GEO PRINT ($@) ..."
	@./e9k-debugger $(HEADLESS) --neogeo --source-dir=./tests/neogeo/stepping --elf=./tests/neogeo/stepping/build/rom.elf --rom=./tests/neogeo/stepping/build/stepping.neo --test tests/results/neogeo/print  >> test.log 2>&1
	@echo "PASSED ✅"

test-neogeoz80: tests/neogeo/neogeoz80/neogeoz80.neo tests/neogeo/neogeoz80/rom.elf tests/neogeo/neogeoz80/demo_driver.z80srcmap
	@printf "NEO GEO Z80 DEBUG ($@) ..."
	@./e9k-debugger $(HEADLESS) --neogeo --volume=0 --source-dir=./tests/neogeo/neogeoz80 --elf=./tests/neogeo/neogeoz80/rom.elf --rom=./tests/neogeo/neogeoz80/neogeoz80.neo --test tests/results/neogeo/neogeoz80  >> test.log 2>&1
	@echo "PASSED ✅"

test-neogeozip: all
	@printf "NEO GEO ZIP ROM ($@) ..."
	@./e9k-debugger $(HEADLESS) --neogeo --volume=0 --rom=./tests/neogeo/zip/test.zip --test tests/results/neogeo/zip >> test.log 2>&1
	@echo "PASSED ✅"

test-neogeofolder: all
	@printf "NEO GEO FOLDER ROM ($@) ..."
	@./e9k-debugger $(HEADLESS) --neogeo --volume=0 --rom-folder=./tests/neogeo/folder/test --test tests/results/neogeo/folder >> test.log 2>&1
	@echo "PASSED ✅"

# assets

tests/neogeo/stepping/build/rom.elf:
	make -C ./tests/neogeo/stepping/
