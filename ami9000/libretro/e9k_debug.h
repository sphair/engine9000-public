#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "uae/types.h"
#include "e9k-lib.h"

#if !defined(E9K_DEBUGGER_CUSTOM_LOGGER)
#define E9K_DEBUGGER_CUSTOM_LOGGER 0
#endif

#ifndef E9K_HACK_DEBUGGER_RUNTIME
#define E9K_HACK_DEBUGGER_RUNTIME 0
#endif

#ifndef E9K_HACK_DEBUGGER_HOST
#define E9K_HACK_DEBUGGER_HOST 0
#endif

#ifndef E9K_HACK_MEMVIS
#define E9K_HACK_MEMVIS 0
#endif

#ifndef E9K_HACK_AMI_SPRITE_VIS
#define E9K_HACK_AMI_SPRITE_VIS 0
#endif

#ifndef E9K_HACK_DET_RTC
#define E9K_HACK_DET_RTC 0
#endif

#ifndef E9K_HACK_CHECKPOINTS
#define E9K_HACK_CHECKPOINTS 0
#endif

#define E9K_DEBUG_EXCEPTION_WATCH_PREWRITE 0x7e91

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
e9k_debug_read_processors(e9k_debug_processor_info_t *out, size_t cap);

size_t
e9k_debug_read_processor_regs(uint32_t processorId, e9k_debug_processor_reg_t *out, size_t cap);

size_t
e9k_debug_read_memory(uint32_t addr, uint8_t *out, size_t cap);

size_t
e9k_debug_read_processor_memory(uint32_t processorId, uint32_t addr, uint8_t *out, size_t cap);

int
e9k_debug_write_memory(uint32_t addr, uint32_t value, size_t size);

int
e9k_debug_write_processor_memory(uint32_t processorId, uint32_t addr, uint32_t value, size_t size);

size_t
e9k_debug_disassemble_quick(uint32_t pc, char *out, size_t cap);

size_t
e9k_debug_disassemble_processor_quick(uint32_t processorId, uint32_t pc, char *out, size_t cap);

int
e9k_debug_suppress_processor_breakpoint_at_pc(uint32_t processorId);

int
e9k_debug_step_processor_instr(uint32_t processorId);

uint64_t
e9k_debug_read_cycle_count(void);

void
e9k_debug_add_breakpoint(uint32_t addr);

void
e9k_debug_remove_breakpoint(uint32_t addr);

void
e9k_debug_add_processor_breakpoint(uint32_t processorId, uint32_t addr);

void
e9k_debug_remove_processor_breakpoint(uint32_t processorId, uint32_t addr);

void
e9k_debug_add_temp_breakpoint(uint32_t addr);

void
e9k_debug_remove_temp_breakpoint(uint32_t addr);

// Optional host callback invoked once per vblank/frame.
void
e9k_debug_set_vblank_callback(void (*cb)(void *), void *user);

#if E9K_HACK_DET_RTC
void
e9k_debug_setDeterministic(int enabled);

int
e9k_debug_getDeterministic(void);
#endif

#if E9K_DEBUGGER_CUSTOM_LOGGER
void
e9k_debug_set_amiga_custom_log_frame_callback(e9k_debug_ami_custom_log_frame_callback_t cb, void *user);

void
e9k_debug_amiga_customLogWrite(int vpos, int hpos, uae_u32 reg, uae_u16 value, uae_u32 sourcePc);

void
e9k_debug_amiga_customLogFrameCommit(void);
#endif

void
e9k_vblank_notify(void);

void
e9k_debug_amiga_on_video_presented(void);

size_t
e9k_debug_amiga_blitter_vis_read_spans(e9k_debug_ami_blitter_vis_span_t *out, size_t cap, uint32_t *outWidth, uint32_t *outHeight);

size_t
e9k_debug_amiga_blitter_vis_read_points(e9k_debug_ami_blitter_vis_point_t *out, size_t cap, uint32_t *outWidth, uint32_t *outHeight);

size_t
e9k_debug_amiga_blitter_vis_read_stats(e9k_debug_ami_blitter_vis_stats_t *out, size_t cap);

#if E9K_HACK_AMI_SPRITE_VIS
void
e9k_debug_amiga_set_sprite_vis(int enabled);

int
e9k_debug_amiga_get_sprite_vis(void);

size_t
e9k_debug_amiga_sprite_vis_read_points(e9k_debug_ami_sprite_vis_point_t *out, size_t cap, uint32_t *outWidth, uint32_t *outHeight);
#endif

const e9k_debug_ami_dma_debug_frame_view_t *
e9k_debug_amiga_dma_debug_get_frame_view(uint32_t frameSelect);

#if E9K_HACK_COPPER_DEBUG_EXPORT
const e9k_debug_ami_copper_debug_frame_view_t *
e9k_debug_amiga_copper_debug_get_frame_view(uint32_t frameSelect);
#endif

int
e9k_debug_amiga_get_video_line_count(void);

int
e9k_debug_amiga_video_line_to_core_line(int videoLine);

int
e9k_debug_amiga_core_line_to_video_line(int coreLine);

const e9k_debug_ami_video_line_state_t *
e9k_debug_amiga_get_video_line_states(void);

bool
e9k_debug_amiga_set_floppy_path(int drive, const char *path);

int *
e9k_debug_amiga_get_dma_addr(void);

#if E9K_HACK_COPPER_DEBUG_EXPORT
int *
e9k_debug_amiga_get_copper_addr(void);
#endif

const e9k_debug_ami_custom_reg_state_t *
e9k_debug_amiga_get_custom_regs(void);

void
e9k_debug_reapply_memhooks(void);

int
e9k_debug_memhook_filterWrite(uint32_t addr24, uint32_t sizeBits, uint32_t oldValue, int oldValueValid, uint32_t *inoutValue);

void
e9k_debug_memhook_afterRead(uint32_t addr24, uint32_t value, uint32_t sizeBits);

void
e9k_debug_memhook_afterReadWithSource(uint32_t addr24, uint32_t value, uint32_t sizeBits, uint32_t accessSource);

int
e9k_debug_memhook_beforeWrite(uint32_t addr24, uint32_t value, uint32_t oldValue, uint32_t sizeBits, int oldValueValid);

int
e9k_debug_memhook_beforeWriteWithSource(uint32_t addr24, uint32_t value, uint32_t oldValue, uint32_t sizeBits, int oldValueValid, uint32_t accessSource);

void
e9k_debug_memhook_afterWrite(uint32_t addr24, uint32_t value, uint32_t oldValue, uint32_t sizeBits, int oldValueValid);

void
e9k_debug_memhook_afterWriteWithSource(uint32_t addr24, uint32_t value, uint32_t oldValue, uint32_t sizeBits, int oldValueValid, uint32_t accessSource);

// Optional host callback invoked when the target writes a new relocatable base.
void
e9k_debug_set_debug_base_callback(void (*cb)(uint32_t section, uint32_t base));

// Optional host callback invoked when the target pushes a relocatable base entry (section, base, size).
void
e9k_debug_set_debug_base_stack_callback(void (*cb)(uint32_t section, uint32_t base, uint32_t size));

// Optional host callback invoked when the target requests a breakpoint via a fake debug peripheral.
void
e9k_debug_set_debug_breakpoint_callback(void (*cb)(uint32_t addr));

// Optional host callback invoked when the target requests debugger exit via a fake debug peripheral.
void
e9k_debug_set_debug_exit_callback(void (*cb)(void));

// Optional host callback invoked when the target requests smoke-test start via a fake debug peripheral.
void
e9k_debug_set_debug_smoke_start_callback(void (*cb)(void));

// Optional host callback invoked when the target requests profile start via a fake debug peripheral.
void
e9k_debug_set_debug_profile_start_callback(void (*cb)(void));

// Debug arguments exposed to the target via fake read-only debug peripherals.
void
e9k_debug_set_debug_args(const uint32_t *args, size_t count);

// Optional host callback used for source location resolution in cores that support source-line stepping.
void
e9k_debug_set_source_location_resolver(int (*resolver)(uint32_t pc24, uint64_t *out_location, void *user), void *user);

void
e9k_debug_set_debug_option(e9k_debug_option_t option, uint32_t argument, void *user);

void
e9k_debug_push_debug_base(uint32_t section, uae_u32 base, uae_u32 size);

#if E9K_HACK_CHECKPOINTS
void
e9k_debug_checkpoint_write(uint8_t index);
void
e9k_debug_checkpoint_set_name_from_pointer(uint8_t index, uint32_t ptrValue);
#endif
