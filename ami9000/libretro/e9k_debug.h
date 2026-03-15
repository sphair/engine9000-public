#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "uae/types.h"
#include "e9k-lib.h"

#if !defined(E9K_DEBUGGER_CUSTOM_LOGGER)
#define E9K_DEBUGGER_CUSTOM_LOGGER 0
#endif

#ifndef E9K_HACK_AMI_SPRITE_VIS
#define E9K_HACK_AMI_SPRITE_VIS 0
#endif

// Debug base register sections (passed to e9k_debug_set_debug_base_callback()).
#define E9K_DEBUG_BASE_SECTION_TEXT 0u
#define E9K_DEBUG_BASE_SECTION_DATA 1u
#define E9K_DEBUG_BASE_SECTION_BSS  2u

int
e9k_debug_instructionHook(uaecptr pc, uae_u16 opcode);

void
e9k_debug_pause(void);

void
e9k_debug_resume(void);

int
e9k_debug_is_paused(void);

void
e9k_debug_step_instr(void);

void
e9k_debug_step_line(void);

void
e9k_debug_step_next(void);

void
e9k_debug_step_out(void);

size_t
e9k_debug_read_callstack(uint32_t *out, size_t cap);

size_t
e9k_debug_read_regs(uint32_t *out, size_t cap);

size_t
e9k_debug_read_memory(uint32_t addr, uint8_t *out, size_t cap);

int
e9k_debug_write_memory(uint32_t addr, uint32_t value, size_t size);

size_t
e9k_debug_disassemble_quick(uint32_t pc, char *out, size_t cap);

uint64_t
e9k_debug_read_cycle_count(void);

void
e9k_debug_add_breakpoint(uint32_t addr);

void
e9k_debug_remove_breakpoint(uint32_t addr);

void
e9k_debug_add_temp_breakpoint(uint32_t addr);

void
e9k_debug_remove_temp_breakpoint(uint32_t addr);

// Optional host callback invoked once per vblank/frame.
void
e9k_debug_set_vblank_callback(void (*cb)(void *), void *user);

#if E9K_DEBUGGER_CUSTOM_LOGGER
void
e9k_debug_set_custom_log_frame_callback(e9k_debug_ami_custom_log_frame_callback_t cb, void *user);

void
e9k_debug_ami_customLogWrite(int vpos, int hpos, uae_u32 reg, uae_u16 value, uae_u32 sourcePc);

void
e9k_debug_ami_customLogFrameCommit(void);
#endif

void
e9k_vblank_notify(void);

void
e9k_debug_ami_on_video_presented(void);

size_t
e9k_debug_ami_blitter_vis_read_points(e9k_debug_ami_blitter_vis_point_t *out, size_t cap, uint32_t *outWidth, uint32_t *outHeight);

size_t
e9k_debug_ami_blitter_vis_read_stats(e9k_debug_ami_blitter_vis_stats_t *out, size_t cap);

#if E9K_HACK_AMI_SPRITE_VIS
void
e9k_debug_ami_set_sprite_vis(int enabled);

int
e9k_debug_ami_get_sprite_vis(void);

size_t
e9k_debug_ami_sprite_vis_read_points(e9k_debug_ami_sprite_vis_point_t *out, size_t cap, uint32_t *outWidth, uint32_t *outHeight);
#endif

const e9k_debug_ami_dma_debug_frame_view_t *
e9k_debug_ami_dma_debug_get_frame_view(uint32_t frameSelect);

#if E9K_HACK_COPPER_DEBUG_EXPORT
const e9k_debug_ami_copper_debug_frame_view_t *
e9k_debug_ami_copper_debug_get_frame_view(uint32_t frameSelect);
#endif

int
e9k_debug_ami_get_video_line_count(void);

int
e9k_debug_ami_video_line_to_core_line(int videoLine);

int
e9k_debug_ami_core_line_to_video_line(int coreLine);

bool
e9k_debug_ami_set_floppy_path(int drive, const char *path);

#if E9K_HACK_COPPER_DEBUG_EXPORT
int *
e9k_debug_amiga_get_debug_copper_addr(void);
#endif

void
e9k_debug_reapply_memhooks(void);

// Optional host callback invoked when the target writes a new relocatable base.
void
e9k_debug_set_debug_base_callback(void (*cb)(uint32_t section, uint32_t base));

// Optional host callback invoked when the target pushes a relocatable base entry (section, base, size).
void
e9k_debug_set_debug_base_stack_callback(void (*cb)(uint32_t section, uint32_t base, uint32_t size));

// Optional host callback invoked when the target requests a breakpoint via a fake debug peripheral.
void
e9k_debug_set_debug_breakpoint_callback(void (*cb)(uint32_t addr));

// Optional host callback used for source location resolution in cores that support source-line stepping.
void
e9k_debug_set_source_location_resolver(int (*resolver)(uint32_t pc24, uint64_t *out_location, void *user), void *user);

void
e9k_debug_set_debug_option(e9k_debug_option_t option, uint32_t argument, void *user);

void
e9k_debug_push_debug_base(uint32_t section, uae_u32 base, uae_u32 size);
