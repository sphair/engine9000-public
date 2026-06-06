CC ?= cc
PKG_CONFIG ?= pkg-config
W64_CC ?= x86_64-w64-mingw32-gcc

E9K_ENABLE_AMIGA ?= 1
E9K_ENABLE_NEOGEO ?= 1
E9K_ENABLE_MEGADRIVE ?= 1
E9K_ENABLE_GL_COMPOSITE ?= 1

UNAME_S := $(shell uname -s)
MINGW_HOME ?= /usr/local/mingw
W64_TRIPLE := $(shell command -v $(W64_CC) >/dev/null 2>&1 && $(W64_CC) -dumpmachine 2>/dev/null || true)
IS_MAC_TO_W64 := $(and $(filter Darwin,$(UNAME_S)),$(findstring w64-mingw32,$(W64_TRIPLE)))
PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_image SDL2_ttf libpng)
PKG_LIBS := $(shell $(PKG_CONFIG) --libs sdl2 SDL2_image SDL2_ttf libpng)

BASE_INCLUDES += -I../e9k-lib
BASE_INCLUDES += -I../e9ui
BASE_INCLUDES += -I./lib9000
BASE_INCLUDES += -I.
INCLUDES += $(BASE_INCLUDES)
INCLUDES += $(PKG_CFLAGS)

ifneq ($(filter -j%,$(MAKEFLAGS)),)
  ifneq ($(filter output-sync,$(.FEATURES)),)
    ifneq ($(findstring test,$(MAKECMDGOALS)),)
      MAKEFLAGS += --output-sync=target
    endif
  endif
endif

DEBUG_FLAGS=-g -fno-omit-frame-pointer
SANITIZER_LIBS=
LTO_FLAGS := -flto
ifeq ($(UNAME_S),Darwin)
DEBUG_FLAGS += -fsanitize=address,undefined
SANITIZER_LIBS += -fsanitize=address,undefined
CFLAGS += 
endif
CFLAGS += $(DEBUG_FLAGS) -Wall -Wextra -std=c99 -MMD -MP -Werror -pthread
CFLAGS += -O2 $(LTO_FLAGS)
ifeq ($(UNAME_S),Linux)
CFLAGS += -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
endif
CFLAGS += -DE9K_ENABLE_AMIGA=$(E9K_ENABLE_AMIGA)
CFLAGS += -DE9K_ENABLE_MEGADRIVE=$(E9K_ENABLE_MEGADRIVE)
CFLAGS += -DE9K_ENABLE_NEOGEO=$(E9K_ENABLE_NEOGEO)
CFLAGS += -DE9K_ENABLE_GL_COMPOSITE=$(E9K_ENABLE_GL_COMPOSITE)
CFLAGS += -DE9UI_ENABLE_GAMEPAD -DE9UI_ENABLE_DEBUG_FONT
CFLAGS += $(INCLUDES)

LIBS   += -ldl
LIBS   += $(SANITIZER_LIBS)
LIBS   += -lreadline
LIBS   += -pthread
LIBS   += -lm
LIBS   += -lz
LIBS   += $(PKG_LIBS)

ifeq ($(UNAME_S),Darwin)
LIBS   += -framework Cocoa
ifeq ($(E9K_ENABLE_GL_COMPOSITE),1)
LIBS   += -framework OpenGL
endif
else
ifeq ($(E9K_ENABLE_GL_COMPOSITE),1)
LIBS   += -lGL
endif
endif

TARGET = e9k-debugger
BUILD_DIR = build
W64_TARGET = e9k-debugger.exe
W64_ADDITIONAL_TARGET = dist/e9kd/e9k-debugger.exe
W64_BUILD_DIR = build-w64
RELEASE_DIR = releases
RELEASE_MINOR_FILE = assets/release_minor.txt
RELEASE_PACKAGE_NAME = e9k-debugger
RELEASE_MEGA9000_PACKAGE_NAME = mega9000
RELEASE_W64_DLLS = libfreetype-6.dll libpng16-16.dll libreadline8.dll libssp-0.dll libtermcap-0.dll libwinpthread-1.dll SDL2_image.dll SDL2_ttf.dll SDL2.dll zlib1.dll
RELEASE_SYSTEM_FILES = v-hunk-addr2line.exe v-hunk-objdump.exe load9000 load9013 z80_srcmap.exe ami9000.dll geo9000.dll
W64_CFLAGS = $(LTO_FLAGS) -O2 -Wall -Wextra -std=c99 -MMD -MP
W64_CFLAGS += $(BASE_INCLUDES)
W64_CFLAGS +=  -D_THREAD_SAFE
W64_CFLAGS += -DE9K_ENABLE_AMIGA=$(E9K_ENABLE_AMIGA)
W64_CFLAGS += -DE9K_ENABLE_MEGADRIVE=$(E9K_ENABLE_MEGADRIVE)
W64_CFLAGS += -DE9K_ENABLE_NEOGEO=$(E9K_ENABLE_NEOGEO)
W64_CFLAGS += -DE9K_ENABLE_GL_COMPOSITE=$(E9K_ENABLE_GL_COMPOSITE)
W64_CFLAGS += -DE9UI_ENABLE_GAMEPAD -DE9UI_ENABLE_DEBUG_FONT
W64_CFLAGS += $(W64_EXTRA_CFLAGS)
W64_CFLAGS += -I../ami9000/libretro-common/include/compat/zlib
W64_LIBS    = -lmingw32 -lSDL2main -lSDL2 -lSDL2_ttf -lSDL2_image -lreadline -lole32 -lcomdlg32 -lpng -lz -lopengl32
W64_LIBS   += $(W64_EXTRA_LIBS)
W64_LDFLAGS = -s $(LTO_FLAGS) -mconsole

ifneq ($(IS_MAC_TO_W64),)
W64_CFLAGS += -I$(MINGW_HOME)/include/SDL2 -I$(MINGW_HOME)/include -I$(MINGW_HOME)/include/readline
W64_LIBS += -L$(MINGW_HOME)/lib
else
W64_CFLAGS += $(PKG_CFLAGS)
W64_LIBS += $(PKG_LIBS)
endif

BUNDLE_NAME = Engine9000.app
BUNDLE_DIR = $(BUNDLE_NAME)/Contents
BUNDLE_MACOS = $(BUNDLE_DIR)/MacOS
BUNDLE_RESOURCES = $(BUNDLE_DIR)/Resources

LIB9000_SRCS=alloc.c list.c resource.c file.c strutil.c
E9UI_DIR = ../e9ui
MINIZ_SRC = deps/miniz/miniz.c
MINIZ_OBJ = $(BUILD_DIR)/deps/miniz/miniz.o
W64_MINIZ_OBJ = $(W64_BUILD_DIR)/deps/miniz/miniz.o

E9UI_SRCS=e9ui_textbox.c e9ui_data_edit.c e9ui_text_cache.c e9ui_text_select.c e9ui_text.c e9ui_scroll.c e9ui_scrollbar.c e9ui_step_buttons.c e9ui.c e9ui_split.c e9ui_split_stack.c e9ui_stack.c\
	  e9ui_image.c e9ui_box.c e9ui_hstack.c e9ui_button.c e9ui_checkbox.c e9ui_flow.c e9ui_header_flow.c e9ui_spacer.c e9ui_vspacer.c e9ui_separator.c\
          e9ui_badge.c e9ui_overlay.c e9ui_center.c e9ui_fileselect.c e9ui_labeled_textbox.c e9ui_labeled_checkbox.c e9ui_labeled_select.c e9ui_range_bar.c e9ui_seek_bar.c e9ui_slider.c e9ui_event.c \
	  e9ui_theme.c e9ui_link.c e9ui_modal.c e9ui_child.c e9ui_window.c 

PLATFORM_POSIX_SRCS = platform/posix/addr2line.c platform/posix/analyse.c platform/posix/profile_view.c platform/posix/signal.c platform/posix/debugger_platform.c
PLATFORM_LINUX_SRCS = platform/posix/clipboard.c
PLATFORM_W64_SRCS = platform/w64/addr2line.c platform/w64/analyse.c platform/w64/profile_view.c platform/w64/signal.c platform/w64/debugger_platform.c platform/w64/clipboard.c
PLATFORM_MACOS_SRCS = platform/macos/clipboard.m platform/macos/tinyfiledialogs.m

SRCS = main.c source_pane.c source_pane_symbols.c source_pane_fileline.c source_pane_source_view.c source_pane_asm_view.c source_z80.c source.c hunk_fileline_cache.c dasm.c dasm_geo.c dasm_ami.c elfutil.c symbol_text_map.c console_cmd.c console_cmd_base.c console_cmd_break.c console_cmd_cls.c console_cmd_continue.c console_cmd_diff.c console_cmd_finish.c console_cmd_loop.c console_cmd_next.c console_cmd_print.c console_cmd_protect.c console_cmd_step.c console_cmd_stepi.c console_cmd_symbols.c console_cmd_train.c console_cmd_transition.c console_cmd_watch.c console_cmd_write.c print_eval.c print_debuginfo_readelf.c print_debuginfo_readelf_frames.c print_debuginfo_objdump_stabs.c prompt.c console.c registers.c memory.c stack.c breakpoints.c trainer.c linebuf.c debug_font.c debug.c machine.c base_map.c profile.c profile_list.c profile_hotspot.c profile_checkpoints.c input_record.c hotkeys.c smoke_test.c ui_test.c snapshot.c rom_config.c romset.c romset_crypto.c settings.c target.c core_options.c core_config.c debugger_input_bindings.c cli.c runtime.c config.c ui.c debugger.c help.c emu.c system_badge.c libretro_host.c libretro_host_amiga.c libretro_host_neogeo.c libretro_host_megadrive.c clipboard.c state_wrap.c state_buffer.c tinyfiledialogs.c transition.c transition_doom.c transition_slide.c transition_explode.c transition_flip.c transition_rbar.c crt.c shader_ui.c memory_track_ui.c aux_window.c shader_base.c shader_advanced.c shader_bloom.c train.c protect.c status_bar.c hex_convert.c hex_byte_color.c inline_edit_pause.c
ifeq ($(E9K_ENABLE_AMIGA),1)
SRCS += source_cpr.c settings_amiga.c amiga_uae_options.c amiga_custom_regs.c amiga_custom_regs_tooltip.c emu_ami.c amiga_blittervis.c amiga_custom_ui.c amiga_custom_ui_common.c amiga_custom_ui_blitter.c amiga_custom_ui_copper.c amiga_custom_ui_dma.c amiga_custom_ui_sprite.c amiga_custom_ui_bitplane.c amiga_custom_ui_bplptr.c amiga_custom_ui_audio.c amiga_custom_log.c amiga_custom.c amiga_memview.c amiga_memsave.c amiga_blit_info.c target_amiga.c
else
SRCS += target_amiga_stub.c
endif
ifeq ($(E9K_ENABLE_NEOGEO),1)
SRCS += neogeo_core_options.c target_neogeo.c emu_geo.c neogeo_sprite_debug.c neogeo_palette_debug.c neogeo_register_log.c neogeo_register_log_regs.c neogeo_memview.c neogeo_memview_overview.c neogeo_audio_vis.c
else
SRCS += target_neogeo_stub.c
endif
ifeq ($(E9K_ENABLE_GL_COMPOSITE),1)
SRCS += gl_composite.c
else
SRCS += gl_composite_stub.c
endif
ifeq ($(E9K_ENABLE_MEGADRIVE),1)
SRCS += megadrive_core_options.c emu_mega.c mega_sprite_debug.c mega_palette_debug.c mega_memview.c mega_memview_overview.c mega_audio_vis.c mega_vdp.c target_megadrive.c
else
SRCS += target_megadrive_stub.c
endif
ifeq ($(UNAME_S),Darwin)
SRCS += $(PLATFORM_POSIX_SRCS) $(PLATFORM_MACOS_SRCS)
else
SRCS += $(PLATFORM_POSIX_SRCS) $(PLATFORM_LINUX_SRCS)
endif
SRCS += syntax_highlight.c syntax_highlight_asm.c

TREE_SITTER_CORE_DIR = deps/tree-sitter/lib
TREE_SITTER_C_DIR = deps/tree-sitter-c

ifneq ($(wildcard $(TREE_SITTER_CORE_DIR)/src/lib.c),)
ifneq ($(wildcard $(TREE_SITTER_C_DIR)/src/parser.c),)
CFLAGS := -I$(TREE_SITTER_CORE_DIR)/include $(CFLAGS)
W64_CFLAGS := -I$(TREE_SITTER_CORE_DIR)/include $(W64_CFLAGS)
CFLAGS += -I$(TREE_SITTER_CORE_DIR)/src
W64_CFLAGS += -I$(TREE_SITTER_CORE_DIR)/src
CFLAGS += -DE9K_USE_TREE_SITTER
W64_CFLAGS += -DE9K_USE_TREE_SITTER
SRCS += $(TREE_SITTER_CORE_DIR)/src/lib.c
SRCS += $(TREE_SITTER_C_DIR)/src/parser.c
endif
endif

W64_SRCS = $(filter-out $(PLATFORM_POSIX_SRCS) $(PLATFORM_LINUX_SRCS) $(PLATFORM_MACOS_SRCS),$(SRCS)) $(PLATFORM_W64_SRCS) $(addprefix lib9000/, $(LIB9000_SRCS))
SRCS_C = $(filter %.c,$(SRCS)) $(addprefix lib9000/, $(LIB9000_SRCS))
SRCS_M = $(filter %.m,$(SRCS))
E9UI_OBJS = $(addprefix $(BUILD_DIR)/e9ui/,$(E9UI_SRCS:%.c=%.o))
OBJS = $(SRCS_C:%.c=$(BUILD_DIR)/%.o) $(SRCS_M:%.m=$(BUILD_DIR)/%.o) $(E9UI_OBJS) $(MINIZ_OBJ)
DEPS = $(OBJS:.o=.d)
W64_E9UI_OBJS = $(addprefix $(W64_BUILD_DIR)/e9ui/,$(E9UI_SRCS:%.c=%.o))
W64_OBJS = $(W64_SRCS:%.c=$(W64_BUILD_DIR)/%.o) $(W64_E9UI_OBJS) $(W64_MINIZ_OBJ)
W64_DEPS = $(W64_OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

$(W64_TARGET): $(W64_OBJS)
	@mkdir -p $(dir $@)
	$(W64_CC) $(W64_CFLAGS) $(W64_LDFLAGS) -o $@ $(W64_OBJS) $(W64_LIBS)
	@cp $(W64_TARGET) $(W64_ADDITIONAL_TARGET)

w64: $(W64_TARGET)

release: release-w64

release-w64: w64
	@$(MAKE) release-w64-package

release-w64-package:
	@mkdir -p $(RELEASE_DIR)
	@temp_dir=`mktemp -d "$(RELEASE_DIR)/tmp.release.XXXXXX"`; \
	if [ ! -d "$$temp_dir" ]; then \
		echo "Failed to create temporary release directory"; \
		exit 1; \
	fi; \
	trap 'rm -rf "$$temp_dir"' EXIT INT TERM; \
	package_dir="$$temp_dir/$(RELEASE_PACKAGE_NAME)"; \
	mkdir -p "$$package_dir/system"; \
	mkdir -p "$$package_dir/saves"; \
	: > "$$package_dir/saves/.saves"; \
	for file in e9k-debugger.exe $(RELEASE_W64_DLLS); do \
		if [ ! -f "dist/e9kd/$$file" ]; then \
			echo "Missing file: dist/e9kd/$$file"; \
			exit 1; \
		fi; \
		cp "dist/e9kd/$$file" "$$package_dir/$$file"; \
	done; \
	if [ ! -d assets ]; then \
		echo "Missing directory: e9k-debugger/assets"; \
		exit 1; \
	fi; \
	if [ ! -f "$(RELEASE_MINOR_FILE)" ]; then \
		echo "0" > "$(RELEASE_MINOR_FILE)"; \
	fi; \
	cp -R assets "$$package_dir/assets"; \
	find "$$package_dir/assets" -type f ! \( -iname '*.png' -o -iname '*.ttf' -o -iname '*.ico' -o -name 'release_minor.txt' \) -delete; \
	for file in $(RELEASE_SYSTEM_FILES); do \
		if [ ! -f "system/$$file" ]; then \
			echo "Missing file: e9k-debugger/system/$$file"; \
			exit 1; \
		fi; \
		cp "system/$$file" "$$package_dir/system/$$file"; \
	done; \
	if [ ! -f "../README.md" ]; then \
		echo "Missing file: ../README.md"; \
		exit 1; \
	fi; \
	cp ../README.md "$$package_dir/README.md"; \
	current_minor=0; \
	if [ -f "$(RELEASE_MINOR_FILE)" ]; then \
		current_minor=`cat "$(RELEASE_MINOR_FILE)"`; \
	fi; \
	next_minor=`expr "$$current_minor" + 1`; \
	zip_name="e9k-debugger-v0.$$next_minor.zip"; \
	mega9000_zip_name="mega9000-v0.$$next_minor.zip"; \
	mega9000_package_dir="$$temp_dir/$(RELEASE_MEGA9000_PACKAGE_NAME)"; \
	mkdir -p "$$mega9000_package_dir"; \
	if [ ! -f "system/mega9000.dll" ]; then \
		echo "Missing file: e9k-debugger/system/mega9000.dll"; \
		exit 1; \
	fi; \
	if [ ! -f "../mega9000/COPYING" ]; then \
		echo "Missing file: ../mega9000/COPYING"; \
		exit 1; \
	fi; \
	cp "system/mega9000.dll" "$$mega9000_package_dir/mega9000.dll"; \
	cp "../mega9000/COPYING" "$$mega9000_package_dir/COPYING"; \
	( cd "$$temp_dir" && zip -r "../$$zip_name" "$(RELEASE_PACKAGE_NAME)" ); \
	( cd "$$temp_dir" && zip -r "../$$mega9000_zip_name" "$(RELEASE_MEGA9000_PACKAGE_NAME)" ); \
	echo "$$next_minor" > "$(RELEASE_MINOR_FILE)"; \
	echo "Created $(RELEASE_DIR)/$$zip_name"; \
	echo "Created $(RELEASE_DIR)/$$mega9000_zip_name"

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/e9ui/%.o: $(E9UI_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: lib9000/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(MINIZ_OBJ): $(MINIZ_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES -c -o $@ $<

$(BUILD_DIR)/%.o: %.m
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(W64_BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(W64_CC) $(W64_CFLAGS) -c -o $@ $<

$(W64_BUILD_DIR)/e9ui/%.o: $(E9UI_DIR)/%.c
	@mkdir -p $(dir $@)
	$(W64_CC) $(W64_CFLAGS) -c -o $@ $<

$(W64_MINIZ_OBJ): $(MINIZ_SRC)
	@mkdir -p $(dir $@)
	$(W64_CC) $(W64_CFLAGS) -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES -c -o $@ $<

clean:
	rm -f $(TARGET) $(W64_TARGET) $(OBJS) $(DEPS) $(W64_OBJS) $(W64_DEPS)
	rm -rf $(BUILD_DIR) $(W64_BUILD_DIR)

bundle: $(TARGET)
	@mkdir -p $(BUNDLE_MACOS) $(BUNDLE_RESOURCES)
	@cp $(TARGET) $(BUNDLE_MACOS)/e9k-debugger
	@rm -rf $(BUNDLE_MACOS)/assets $(BUNDLE_MACOS)/system
	@cp -R assets $(BUNDLE_MACOS)/assets
	@cp -R dist/e9kd/system $(BUNDLE_MACOS)/system
	@cp assets/icons/osx/Engine9000.icns $(BUNDLE_RESOURCES)/Engine9000.icns
	@printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' \
		'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
		'<plist version="1.0">' \
		'<dict>' \
		'  <key>CFBundleName</key>' \
		'  <string>Engine9000</string>' \
		'  <key>CFBundleDisplayName</key>' \
		'  <string>Engine9000</string>' \
		'  <key>CFBundleIdentifier</key>' \
		'  <string>com.engine9000.debugger</string>' \
		'  <key>CFBundleExecutable</key>' \
		'  <string>e9k-debugger</string>' \
		'  <key>CFBundleIconFile</key>' \
		'  <string>Engine9000</string>' \
		'  <key>CFBundlePackageType</key>' \
		'  <string>APPL</string>' \
		'  <key>CFBundleVersion</key>' \
		'  <string>1.0</string>' \
		'  <key>CFBundleShortVersionString</key>' \
		'  <string>1.0</string>' \
		'</dict>' \
		'</plist>' > $(BUNDLE_DIR)/Info.plist

.PHONY: all w64 release release-w64 release-w64-package clean bundle macos-app

-include $(DEPS)
-include $(W64_DEPS)


HEADLESS ?= --headless

include tests/neogeo.mk
include tests/amiga.mk
include tests/mega.mk
include tests/ui.mk

test:  $(AMIGA_TESTS) $(NEOGEO_TESTS) $(MEGA_TESTS) $(UI_TESTS)
	@echo
	@echo "✅✅✅ TESTS PASSED ✅✅✅"

test-window: test-neogeosprite test-neogeotracker test-uishader test-amigacustom test-amigalog 
	@echo
	@echo "✅✅✅ WINDOW TESTS PASSED ✅✅✅"

test-ui: $(UI_TESTS)
	@echo
	@echo "✅✅✅ UI TESTS PASSED ✅✅✅"

test-amiga: $(AMIGA_TESTS)
	@echo
	@echo "✅✅✅ AMIGA TESTS PASSED ✅✅✅"

test-neogeo: $(NEOGEO_TESTS)
	@echo
	@echo "✅✅✅ NEO GEO TESTS PASSED ✅✅✅"

test-mega: $(MEGA_TESTS)
	@echo
	@echo "✅✅✅ MEGA TESTS PASSED ✅✅✅"

test-remake: $(AMIGA_REMAKE) $(UI_REMAKE) $(NEOGEO_REMAKE) $(MEGA_REMAKE)
	@echo
	@echo "✅✅✅ REMAKE COMPLETE ✅✅✅"

test-remake-amiga: $(AMIGA_REMAKE)
	@echo
	@echo "✅✅✅ REMAKE AMIGA COMPLETE ✅✅✅"

test-remake-ui: $(UI_REMAKE)
	@echo
	@echo "✅✅✅ REMAKE UI COMPLETE ✅✅✅"

test-remake-neogeo: $(NEOGEO_REMAKE)
	@echo
	@echo "✅✅✅ REMAKE NEOGEO COMPLETE ✅✅✅"

test-remake-mega: $(MEGA_REMAKE)
	@echo
	@echo "✅✅✅ REMAKE MEGA COMPLETE ✅✅✅"

test-show: HEADLESS :=
test-show: test
