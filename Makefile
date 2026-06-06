MINGW_CC=x86_64-w64-mingw32-gcc 
JOBS= -j12
MEGA9000_DIR=mega9000
MEGA9000_MAKEFILE=$(MEGA9000_DIR)/Makefile.libretro
PUBLIC_DIR=../engine9000-public
PUBLIC_ROOT=Makefile README.md
PUBLIC_MODULES=e9k-debugger geo9000 ami9000 mega9000
PUBLIC_SHARED=e9k-lib tools e9ui
HOST_OS := $(shell uname -s)
LIBRETRO_PLATFORM := unix
ifeq ($(HOST_OS),Darwin)
LIBRETRO_PLATFORM := osx
endif

.PHONY: all w64 release-w64 clean test mega9000 mega9000-w64 mega9000-clean mega9000-support update-public

all:
	$(MAKE) $(JOBS) -C ami9000 platform=$(LIBRETRO_PLATFORM)
	$(MAKE) $(JOBS) -C geo9000/libretro platform=$(LIBRETRO_PLATFORM)
	$(MAKE) mega9000
	$(MAKE) $(JOBS) -C e9k-debugger
	$(MAKE) $(JOBS) -C tools/amiga/adf9000
	$(MAKE) $(JOBS) -C tools/amiga/hdf9000
	$(MAKE) $(JOBS) -C tools/amiga/v-hunk
	$(MAKE) $(JOBS) -C tools/z80_srcmap

w64:
	$(MAKE) $(JOBS) -C ami9000 platform=win CC=$(MINGW_CC)
	$(MAKE) $(JOBS) -C geo9000/libretro platform=win64 CC=$(MINGW_CC)
	$(MAKE) mega9000-w64
	$(MAKE) $(JOBS) -C e9k-debugger w64
	$(MAKE) $(JOBS) -C tools/amiga/adf9000 w64 CC=$(MINGW_CC)
	$(MAKE) $(JOBS) -C tools/amiga/hdf9000 w64 CC=$(MINGW_CC)
	$(MAKE) $(JOBS) -C tools/amiga/v-hunk w64 CC=$(MINGW_CC)
	$(MAKE) $(JOBS) -C tools/z80_srcmap w64 W64_CC=$(MINGW_CC)

release-w64: w64
	$(MAKE) $(JOBS) -C tools/amiga/load9000
	$(MAKE) -C e9k-debugger release-w64-package

clean:
	$(MAKE) $(JOBS) -C ami9000 platform=win CC=$(MINGW_CC) clean
	$(MAKE) $(JOBS) -C ami9000 clean
	$(MAKE) $(JOBS) -C geo9000/libretro platform=win64 CC=$(MINGW_CC) clean
	$(MAKE) $(JOBS) -C geo9000/libretro clean
	$(MAKE) mega9000-clean
	$(MAKE) $(JOBS) -C e9k-debugger clean
	$(MAKE) $(JOBS) -C tools/amiga/adf9000 clean
	$(MAKE) $(JOBS) -C tools/amiga/hdf9000 clean
	$(MAKE) $(JOBS) -C tools/amiga/load9000 clean
	$(MAKE) $(JOBS) -C tools/amiga/v-hunk clean
	$(MAKE) $(JOBS) -C tools/z80_srcmap clean

test:
	$(MAKE) -C e9k-debugger test
	$(MAKE) -C tools/amiga/adf9000 test

mega9000-support:
	@if [ -f .gitmodules ] && git config -f .gitmodules --get submodule.mega9000.path >/dev/null 2>&1; then \
		echo "Updating mega9000 submodule..."; \
		if git submodule update --init --recursive $(MEGA9000_DIR); then \
			echo "mega9000 support is ready"; \
		else \
			echo "mega9000 is skipped (submodule update failed)"; \
		fi; \
	else \
		echo "mega9000 is skipped (no mega9000 submodule configured). Run 'git submodule add <url> $(MEGA9000_DIR)' to enable support."; \
	fi

mega9000:
	@if [ -f $(MEGA9000_MAKEFILE) ]; then \
		$(MAKE) $(JOBS) -C $(MEGA9000_DIR) -f Makefile.libretro platform=$(LIBRETRO_PLATFORM); \
	else \
		echo "mega9000 is skipped (repo not present). Run 'make mega9000-support' to pull submodule support."; \
	fi

mega9000-w64:
	@if [ -f $(MEGA9000_MAKEFILE) ]; then \
		$(MAKE) $(JOBS) -C $(MEGA9000_DIR) -f Makefile.libretro platform=win64 CC=$(MINGW_CC); \
	else \
		echo "mega9000 is skipped (repo not present). Run 'make mega9000-support' to pull submodule support."; \
	fi

mega9000-clean:
	@if [ -f $(MEGA9000_MAKEFILE) ]; then \
		$(MAKE) $(JOBS) -C $(MEGA9000_DIR) -f Makefile.libretro clean; \
	else \
		echo "mega9000 is skipped (repo not present)."; \
	fi

update-public:
	@mkdir -p "$(PUBLIC_DIR)"
	@rsync -a --delete --exclude='.git/' --exclude='.git' $(PUBLIC_ROOT) "$(PUBLIC_DIR)/"
	@for d in $(PUBLIC_MODULES) $(PUBLIC_SHARED); do \
		if [ -d "$$d" ]; then \
			if [ "$$d" = "$(MEGA9000_DIR)" ]; then \
				rsync -a --delete \
					--exclude='build/' \
					"$$d/" "$(PUBLIC_DIR)/$$d/"; \
			else \
				rsync -a --delete \
					--exclude='.git/' \
					--exclude='.git' \
					--exclude='build/' \
					"$$d/" "$(PUBLIC_DIR)/$$d/"; \
			fi; \
		else \
			echo "$$d is skipped (repo not present)."; \
		fi; \
	done
