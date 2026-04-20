// Simple 68K debugger (end-of-frame pause)
#ifndef E9K_DEBUGGER_H
#define E9K_DEBUGGER_H

#include "e9k-lib.h"
#include <stdint.h>
#include <stddef.h>

#include "e9k_watchpoint.h"

typedef int (*e9k_debugger_source_location_resolver_t)(uint32_t pc24, uint64_t *out_location, void *user);

// Initialize debugger
void e9k_debugger_init(void);

// Set optional source-location resolver used by step-line/step-next.
void e9k_debugger_set_source_location_resolver(e9k_debugger_source_location_resolver_t resolver, void *user);

// Instruction hook (called via dispatcher)
void e9k_debugger_instr_hook(unsigned pc);

// (Debugger is always enabled; window visibility controls display)

// Pause/continue state
int  e9k_debugger_is_paused(void);
void e9k_debugger_continue(void);

// Request a single-frame step (pause again after next frame)
void e9k_debugger_step_frame(void);

// Request a single-instruction step (mid-frame halt via timeslice end)
void e9k_debugger_step_instr(void);

// Query immediate break request for mid-frame halts (clears the flag)
int  e9k_debugger_break_now(void);

// Peek immediate break request (does not clear the flag)
int  e9k_debugger_should_break_now(void);

// Returns 1 if a step/break just modified the emulated frame and the
// frontend should resnapshot the base visible region; clears the flag.
int  e9k_debugger_consume_resnap_needed(void);

// Called from retro layer at end of a frame to latch breakpoint hits
void e9k_debugger_end_of_frame_update(void (*notify)(const char *msg, int frames));

// Direct control helpers (bypass UI edge detection)
void e9k_debugger_break_immediate(void);     // break now (mid-frame), enable if needed
void e9k_debugger_toggle_break(void);        // break if running, continue if paused
void e9k_debugger_step_instr_cmd(void);      // arm single-instruction step
void e9k_debugger_step_next_line_cmd(void);  // arm next-line step
void e9k_debugger_step_next_over_cmd(void);  // arm next-over (C "next")
void e9k_debugger_step_out_cmd(void);        // arm step-out (C "finish")

// Breakpoint control by address (PC is 24-bit for Neo E9k)
void e9k_debugger_add_breakpoint(uint32_t pc24);
void e9k_debugger_remove_breakpoint(uint32_t pc24);
int  e9k_debugger_has_breakpoint(uint32_t pc24);
void e9k_debugger_add_temp_breakpoint(uint32_t pc24);
void e9k_debugger_remove_temp_breakpoint(uint32_t pc24);

// Mirror call stack (return addresses, bottom to top)
size_t e9k_debugger_read_callstack(uint32_t *out, size_t cap);

size_t
e9k_debugger_readMemory(uint32_t addr, uint8_t *out, size_t cap);

int
e9k_debugger_writeMemory(uint32_t addr, uint32_t value, size_t size);

// Watchpoint table and watchbreak reporting.
void     e9k_debugger_reset_watchpoints(void);
int      e9k_debugger_add_watchpoint(uint32_t addr, uint32_t op_mask, uint32_t diff_operand, uint32_t value_operand, uint32_t old_value_operand, uint32_t size_operand, uint32_t addr_mask_operand, uint32_t access_source_operand);
void     e9k_debugger_remove_watchpoint(uint32_t index);
size_t   e9k_debugger_read_watchpoints(e9k_debug_watchpoint_t *out, size_t cap);
uint64_t e9k_debugger_get_watchpoint_enabled_mask(void);
void     e9k_debugger_set_watchpoint_enabled_mask(uint64_t mask);
int      e9k_debugger_consume_watchbreak(e9k_debug_watchbreak_t *out);

// Memory access hooks (called by the bus implementation).
void e9k_debugger_watchpoint_read(uint32_t addr, uint32_t value, uint32_t size_bits);
void e9k_debugger_watchpoint_write(uint32_t addr, uint32_t value, uint32_t old_value, uint32_t size_bits, int old_value_valid);
void e9k_debugger_watchpoint_suspend(void);
void e9k_debugger_watchpoint_resume(void);

#ifdef E9K_HACK_REGISTER_LOG
int
e9k_debugger_isRegisterLogEnabled(void);

void
e9k_debugger_setRegisterLogFrameCallback(e9k_debug_geo_register_log_frame_callback_t cb, void *user);

void
e9k_debugger_writeRegisterLog(uint16_t line, uint32_t reg, uint16_t value, uint8_t sourceKind, uint32_t sourceAddr);

void
e9k_debugger_commitRegisterLogFrame(void);
#endif

#endif // E9K_DEBUGGER_H
