#include "e9k_debug.h"
#include "e9k_checkpoint.h"
#include "e9k_debug_rom.h"
#include "e9k_debug_sprite.h"
#include "e9k_protect.h"
#include "e9k_watchpoint.h"

#include "libretro.h"
#include "libretro-core.h"

#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "events.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "blitter.h"
#include "drawing.h"
#include "debug.h"

#include <stdlib.h>

#define E9K_DEBUG_CALLSTACK_MAX 256
#define E9K_DEBUG_BREAKPOINT_MAX 4096

extern bool libretro_frame_end;

#define E9K_DEBUG_EXPORT RETRO_API

#if !defined(E9K_DEBUGGER_CUSTOM_LOGGER)
#define E9K_DEBUGGER_CUSTOM_LOGGER 0
#endif

#ifndef E9K_HACK_DMA_DEBUG_EXPORT
#define E9K_HACK_DMA_DEBUG_EXPORT 0
#endif

#ifndef E9K_HACK_COPPER_DEBUG_EXPORT
#define E9K_HACK_COPPER_DEBUG_EXPORT 0
#endif

#ifndef E9K_HACK_AMI_SPRITE_VIS
#define E9K_HACK_AMI_SPRITE_VIS 0
#endif

#if E9K_DEBUGGER_CUSTOM_LOGGER
#define E9K_DEBUG_AMI_CUSTOM_LOG_ENTRY_CAP 262144u
#endif

// Fake debug output register support (written by target code, consumed by e9k-debugger)
#define E9K_DEBUG_TEXT_CAP 8192
#if E9K_HACK_BLITTER_VIS
#define E9K_DEBUG_BLITTER_VIS_MODE_COLLECT 0x2
#define E9K_DEBUG_BLITTER_VIS_LINE_TABLE_CAP 16384u
#define E9K_DEBUG_BLITTER_VIS_X_DELTA_THRESHOLD 0
#define E9K_DEBUG_BLITTER_VIS_Y_GAP_MAX 1024
#define E9K_DEBUG_BLITTER_VIS_SAME_BLIT_WORD_SHIFT_PIXELS 16
#define E9K_DEBUG_BLITTER_VIS_WORD_SHIFT_PIXELS 16
#define E9K_DEBUG_BLITTER_VIS_WORD_SHIFT_WIDTH_DELTA_MAX 2
#define E9K_DEBUG_BLITTER_VIS_MAX_ANOMALY_LOGS 24u

typedef struct e9k_debug_blitter_vis_line_stat_s
{
	uae_u32 blitId;
	uint32_t y;
	uint32_t minX;
	uint32_t maxX;
	uint32_t count;
	int used;
} e9k_debug_blitter_vis_line_stat_t;
#endif

static int e9k_debug_paused = 0;
static uint32_t e9k_debug_callstack[E9K_DEBUG_CALLSTACK_MAX];
static size_t e9k_debug_callstackDepth = 0;

static int e9k_debug_stepInstr = 0;
static int e9k_debug_stepInstrAfter = 0;

static int e9k_debug_stepLine = 0;
static int e9k_debug_stepLineHasStart = 0;
static uint64_t e9k_debug_stepLineStart = 0;
static int e9k_debug_stepNext = 0;
static size_t e9k_debug_stepNextDepth = 0;
static int e9k_debug_stepNextSkipOnce = 0;
static int e9k_debug_stepNextReturnPcValid = 0;
static uint32_t e9k_debug_stepNextReturnPc = 0;
static int e9k_debug_stepOut = 0;
static size_t e9k_debug_stepOutDepth = 0;
static int e9k_debug_stepOutSkipOnce = 0;
static int e9k_debug_stepIntoPending = 0;

static int e9k_debug_skipBreakpointOnce = 0;
static uint32_t e9k_debug_skipBreakpointPc = 0;

static uint32_t e9k_debug_breakpoints[E9K_DEBUG_BREAKPOINT_MAX];
static size_t e9k_debug_breakpointCount = 0;
static uint32_t e9k_debug_tempBreakpoints[E9K_DEBUG_BREAKPOINT_MAX];
static size_t e9k_debug_tempBreakpointCount = 0;

static void (*e9k_debug_vblankCb)(void *) = NULL;
static void *e9k_debug_vblankUser = NULL;

int debug_enableE9kHooks(void);

static int e9k_debug_memhooksEnabled = 0;

static e9k_debug_watchpoint_t e9k_debug_watchpoints[E9K_WATCHPOINT_COUNT];
static uint64_t e9k_debug_watchpointEnabledMask = 0;
static e9k_debug_watchbreak_t e9k_debug_watchbreak = {0};
static int e9k_debug_watchbreakPending = 0;
static int e9k_debug_watchpointSuspend = 0;

static e9k_debug_protect_t e9k_debug_protects[E9K_PROTECT_COUNT];
static uint64_t e9k_debug_protectEnabledMask = 0;

static int e9k_debug_checkpointEnabled = 0;
static e9k_debug_checkpoint_t e9k_debug_checkpoints[E9K_CHECKPOINT_COUNT];

static int e9k_debug_profilerEnabled = 0;

// Minimal PC-sampling profiler used by e9k-debugger. The debugger resolves PCs to symbols/lines.
// We stream aggregated PC hits as JSON in e9k_debug_profiler_stream_next(), matching geo9000.
#define E9K_DEBUG_PROF_EMPTY_PC 0xffffffffu
#define E9K_DEBUG_PROF_TABLE_CAP 4096u
#define E9K_DEBUG_PROF_SAMPLE_DIV 64u
static uint32_t e9k_debug_prof_pcs[E9K_DEBUG_PROF_TABLE_CAP];
static uint64_t e9k_debug_prof_samples[E9K_DEBUG_PROF_TABLE_CAP];
static uint64_t e9k_debug_prof_cycles[E9K_DEBUG_PROF_TABLE_CAP];
static uint32_t e9k_debug_prof_entryEpoch[E9K_DEBUG_PROF_TABLE_CAP];
static uint32_t e9k_debug_prof_dirtyIdx[E9K_DEBUG_PROF_TABLE_CAP];
static uint32_t e9k_debug_prof_dirtyCount = 0;
static uint32_t e9k_debug_prof_epoch = 1;
static uint32_t e9k_debug_prof_tick = 0;
static uint32_t e9k_debug_prof_lastTickAtFrame = 0;
static int e9k_debug_prof_streamEnabled = 0;
static int e9k_debug_prof_lastValid = 0;
static uint32_t e9k_debug_prof_lastPc = 0;
static evt_t e9k_debug_prof_lastCycle = 0;
#ifdef JIT
static int e9k_debug_prof_savedCachesize = -1;
#endif

static void (*e9k_debug_setDebugBaseCb)(uint32_t section, uint32_t base) = NULL;
static void (*e9k_debug_pushDebugBaseCb)(uint32_t section, uint32_t base, uint32_t size) = NULL;
static void (*e9k_debug_setDebugBreakpointCb)(uint32_t addr) = NULL;
static int (*e9k_debug_sourceLocationResolver)(uint32_t pc24, uint64_t *out_location, void *user) = NULL;
static void *e9k_debug_sourceLocationResolverUser = NULL;
static uint32_t e9k_debug_bitplaneMask = 0xffu;
extern int audio_channel_mask;

static char e9k_debug_textBuf[E9K_DEBUG_TEXT_CAP];
static size_t e9k_debug_textHead = 0;
static size_t e9k_debug_textTail = 0;
static size_t e9k_debug_textCount = 0;
#if E9K_DEBUGGER_CUSTOM_LOGGER
static e9k_debug_ami_custom_log_entry_t e9k_debug_amiCustomLogEntries[E9K_DEBUG_AMI_CUSTOM_LOG_ENTRY_CAP];
static size_t e9k_debug_amiCustomLogEntryCount = 0;
static uint32_t e9k_debug_amiCustomLogDropped = 0;
static uint64_t e9k_debug_amiCustomLogFrameNo = 0;
static e9k_debug_ami_custom_log_frame_callback_t e9k_debug_amiCustomLogFrameCb = NULL;
static void *e9k_debug_amiCustomLogFrameCbUser = NULL;
#endif
#if E9K_HACK_BLITTER_VIS
static e9k_debug_blitter_vis_line_stat_t e9k_debug_blitterVisLineTable[E9K_DEBUG_BLITTER_VIS_LINE_TABLE_CAP];
static e9k_debug_blitter_vis_line_stat_t e9k_debug_blitterVisLineList[E9K_DEBUG_BLITTER_VIS_LINE_TABLE_CAP];
static uint32_t e9k_debug_blitterVisDroppedLineEntries = 0;
#endif

static void e9k_debug_requestBreak(void);

#if E9K_DEBUGGER_CUSTOM_LOGGER
static void
e9k_debug_ami_customLogFlushFrame(void)
{
	if (e9k_debug_amiCustomLogFrameCb) {
		e9k_debug_amiCustomLogFrameCb(e9k_debug_amiCustomLogEntries,
			e9k_debug_amiCustomLogEntryCount,
			e9k_debug_amiCustomLogDropped,
			e9k_debug_amiCustomLogFrameNo,
			e9k_debug_amiCustomLogFrameCbUser);
	}
	e9k_debug_amiCustomLogEntryCount = 0;
	e9k_debug_amiCustomLogDropped = 0;
	e9k_debug_amiCustomLogFrameNo++;
}
#endif

static void
e9k_debug_profiler_reset(void)
{
	memset(e9k_debug_prof_pcs, 0xff, sizeof(e9k_debug_prof_pcs));
	memset(e9k_debug_prof_samples, 0, sizeof(e9k_debug_prof_samples));
	memset(e9k_debug_prof_cycles, 0, sizeof(e9k_debug_prof_cycles));
	memset(e9k_debug_prof_entryEpoch, 0, sizeof(e9k_debug_prof_entryEpoch));
	e9k_debug_prof_dirtyCount = 0;
	e9k_debug_prof_epoch = 1;
	e9k_debug_prof_tick = 0;
	e9k_debug_prof_lastTickAtFrame = 0;
	e9k_debug_prof_lastValid = 0;
	e9k_debug_prof_lastPc = 0;
	e9k_debug_prof_lastCycle = 0;
}

static void
e9k_debug_setBitplaneEnabled(int bitplaneIndex, int enabled)
{
	if (bitplaneIndex < 0 || bitplaneIndex > 7) {
		return;
	}
	uint32_t bit = (1u << (uint32_t)bitplaneIndex);
	if (enabled) {
		e9k_debug_bitplaneMask |= bit;
	} else {
		e9k_debug_bitplaneMask &= ~bit;
	}
	debug_bpl_mask = (int)(e9k_debug_bitplaneMask & 0xffu);
}

static void
e9k_debug_setAudioChannelEnabled(int audioChannelIndex, int enabled)
{
	if (audioChannelIndex < 0 || audioChannelIndex > 3) {
		return;
	}
	uint32_t bit = (1u << (uint32_t)audioChannelIndex);
	if (enabled) {
		audio_channel_mask |= (int)bit;
	} else {
		audio_channel_mask &= (int)~bit;
	}
}

static void
e9k_debug_profiler_markDirtySlot(uint32_t slot)
{
	if (slot >= E9K_DEBUG_PROF_TABLE_CAP) {
		return;
	}
	if (e9k_debug_prof_entryEpoch[slot] == e9k_debug_prof_epoch) {
		return;
	}
	e9k_debug_prof_entryEpoch[slot] = e9k_debug_prof_epoch;
	if (e9k_debug_prof_dirtyCount < E9K_DEBUG_PROF_TABLE_CAP) {
		e9k_debug_prof_dirtyIdx[e9k_debug_prof_dirtyCount++] = slot;
	}
}

static int
e9k_debug_profiler_findSlot(uint32_t pc24, int create, uint32_t *out_slot)
{
	if (out_slot) {
		*out_slot = 0;
	}
	pc24 &= 0x00ffffffu;
	uint32_t mask = E9K_DEBUG_PROF_TABLE_CAP - 1u;
	uint32_t idx = (pc24 * 2654435761u) & mask;
	for (uint32_t probe = 0; probe < E9K_DEBUG_PROF_TABLE_CAP; ++probe) {
		uint32_t slot = (idx + probe) & mask;
		uint32_t cur = e9k_debug_prof_pcs[slot];
		if (cur == pc24) {
			if (out_slot) {
				*out_slot = slot;
			}
			return 1;
		}
		if (cur == E9K_DEBUG_PROF_EMPTY_PC) {
			if (!create) {
				return 0;
			}
			e9k_debug_prof_pcs[slot] = pc24 & 0x00ffffffu;
			e9k_debug_prof_samples[slot] = 0;
			e9k_debug_prof_cycles[slot] = 0;
			if (out_slot) {
				*out_slot = slot;
			}
			return 1;
		}
	}
	return 0;
}

static void
e9k_debug_profiler_accountCycles(uint32_t pc24, uint64_t cycles)
{
	if (cycles == 0) {
		return;
	}
	uint32_t slot = 0;
	if (!e9k_debug_profiler_findSlot(pc24, 1, &slot)) {
		return;
	}
	e9k_debug_prof_cycles[slot] += cycles;
	e9k_debug_profiler_markDirtySlot(slot);
}

static void
e9k_debug_profiler_samplePc(uint32_t pc24)
{
	uint32_t slot = 0;
	if (!e9k_debug_profiler_findSlot(pc24, 1, &slot)) {
		return;
	}
	e9k_debug_prof_samples[slot] += 1;
	e9k_debug_profiler_markDirtySlot(slot);
}

static void
e9k_debug_profiler_instrHook(uint32_t pc24)
{
	if (!e9k_debug_profilerEnabled) {
		return;
	}
	if (e9k_debug_paused) {
		return;
	}

	evt_t now = get_cycles();
	if (e9k_debug_prof_lastValid) {
		evt_t deltaUnits = now - e9k_debug_prof_lastCycle;
		if (deltaUnits > 0) {
			uint64_t deltaCycles = 0;
			if (CYCLE_UNIT > 0) {
				deltaCycles = (uint64_t)(deltaUnits / (evt_t)CYCLE_UNIT);
			} else {
				deltaCycles = (uint64_t)deltaUnits;
			}
			if (deltaCycles) {
				e9k_debug_profiler_accountCycles(e9k_debug_prof_lastPc, deltaCycles);
			}
		}
	}
	e9k_debug_prof_lastCycle = now;
	e9k_debug_prof_lastPc = pc24 & 0x00ffffffu;
	e9k_debug_prof_lastValid = 1;

	e9k_debug_prof_tick++;
	if ((e9k_debug_prof_tick % E9K_DEBUG_PROF_SAMPLE_DIV) == 0u) {
		e9k_debug_profiler_samplePc(pc24);
	}
}

void
e9k_debug_text_write(uae_u8 byte)
{
		if (e9k_debug_textCount == E9K_DEBUG_TEXT_CAP) {
			e9k_debug_textTail = (e9k_debug_textTail + 1) % E9K_DEBUG_TEXT_CAP;
		e9k_debug_textCount--;
	}
	e9k_debug_textBuf[e9k_debug_textHead] = (char)byte;
	e9k_debug_textHead = (e9k_debug_textHead + 1) % E9K_DEBUG_TEXT_CAP;
	e9k_debug_textCount++;
}

void
e9k_debug_set_debug_base(uint32_t section, uae_u32 base)
{
	if (e9k_debug_setDebugBaseCb) {
		e9k_debug_setDebugBaseCb(section, (uint32_t)base);
	}
}

void
e9k_debug_push_debug_base(uint32_t section, uae_u32 base, uae_u32 size)
{
	if (e9k_debug_pushDebugBaseCb) {
		e9k_debug_pushDebugBaseCb(section, (uint32_t)base, (uint32_t)size);
	}
}

static uint32_t
e9k_debug_maskAddr(uaecptr addr)
{
	return (uint32_t)addr & 0x00ffffffu;
}

static uint32_t
e9k_debug_maskValue(uint32_t v, uint32_t sizeBits)
{
	if (sizeBits == 8u) {
		return v & 0xffu;
	}
	if (sizeBits == 16u) {
		return v & 0xffffu;
	}
	return v;
}

static uint32_t
e9k_debug_sizeBytes(uint32_t sizeBits)
{
	if (sizeBits == 8u) {
		return 1u;
	}
	if (sizeBits == 16u) {
		return 2u;
	}
	if (sizeBits == 32u) {
		return 4u;
	}
	return 0u;
}

#if E9K_HACK_BLITTER_VIS
static int
e9k_debug_blitterVisAbs(int value)
{
	if (value < 0) {
		return -value;
	}
	return value;
}

static uint32_t
e9k_debug_blitterVisHash(uae_u32 blitId, uint32_t y)
{
	uint32_t mixed = (blitId * 2654435761u) ^ (y * 2246822519u);
	return mixed & (E9K_DEBUG_BLITTER_VIS_LINE_TABLE_CAP - 1u);
}

static void
e9k_debug_blitterVisResetLineTable(void)
{
	memset(e9k_debug_blitterVisLineTable, 0, sizeof(e9k_debug_blitterVisLineTable));
	e9k_debug_blitterVisDroppedLineEntries = 0;
}

static e9k_debug_blitter_vis_line_stat_t *
e9k_debug_blitterVisFindLine(uae_u32 blitId, uint32_t y, int create)
{
	uint32_t index = e9k_debug_blitterVisHash(blitId, y);
	for (uint32_t probe = 0; probe < E9K_DEBUG_BLITTER_VIS_LINE_TABLE_CAP; ++probe) {
		e9k_debug_blitter_vis_line_stat_t *entry = &e9k_debug_blitterVisLineTable[index];
		if (!entry->used) {
			if (!create) {
				return NULL;
			}
			entry->used = 1;
			entry->blitId = blitId;
			entry->y = y;
			entry->minX = 0;
			entry->maxX = 0;
			entry->count = 0;
			return entry;
		}
		if (entry->blitId == blitId && entry->y == y) {
			return entry;
		}
		index = (index + 1u) & (E9K_DEBUG_BLITTER_VIS_LINE_TABLE_CAP - 1u);
	}
	if (create) {
		e9k_debug_blitterVisDroppedLineEntries++;
	}
	return NULL;
}

static void
e9k_debug_blitterVisRecordPoint(uae_u32 blitId, uint32_t y, uint32_t x)
{
	e9k_debug_blitter_vis_line_stat_t *entry = e9k_debug_blitterVisFindLine(blitId, y, 1);
	if (!entry) {
		return;
	}
	if (entry->count == 0) {
		entry->minX = x;
		entry->maxX = x;
	} else {
		if (x < entry->minX) {
			entry->minX = x;
		}
		if (x > entry->maxX) {
			entry->maxX = x;
		}
	}
	entry->count++;
}

static int
e9k_debug_blitterVisLineCompare(const void *lhs, const void *rhs)
{
	const e9k_debug_blitter_vis_line_stat_t *left = (const e9k_debug_blitter_vis_line_stat_t *)lhs;
	const e9k_debug_blitter_vis_line_stat_t *right = (const e9k_debug_blitter_vis_line_stat_t *)rhs;
	if (left->blitId < right->blitId) {
		return -1;
	}
	if (left->blitId > right->blitId) {
		return 1;
	}
	if (left->y < right->y) {
		return -1;
	}
	if (left->y > right->y) {
		return 1;
	}
	return 0;
}

static int
e9k_debug_blitterVisLineCompareByY(const void *lhs, const void *rhs)
{
	const e9k_debug_blitter_vis_line_stat_t *left = (const e9k_debug_blitter_vis_line_stat_t *)lhs;
	const e9k_debug_blitter_vis_line_stat_t *right = (const e9k_debug_blitter_vis_line_stat_t *)rhs;
	if (left->y < right->y) {
		return -1;
	}
	if (left->y > right->y) {
		return 1;
	}
	if (left->minX < right->minX) {
		return -1;
	}
	if (left->minX > right->minX) {
		return 1;
	}
	if (left->maxX < right->maxX) {
		return -1;
	}
	if (left->maxX > right->maxX) {
		return 1;
	}
	if (left->blitId < right->blitId) {
		return -1;
	}
	if (left->blitId > right->blitId) {
		return 1;
	}
	return 0;
}

static void
e9k_debug_blitterVisAnalyzeLineTable(uint32_t frameId, uint32_t width, uint32_t height, int collectMode)
{
	(void)frameId;
	(void)width;
	(void)height;
	(void)collectMode;
}
#endif

static int
e9k_debug_resolveSourceLocation(uint32_t pc24, uint64_t *outLocation)
{
	if (!outLocation) {
		return 0;
	}
	*outLocation = 0;
	if (!e9k_debug_sourceLocationResolver) {
		return 0;
	}
	return e9k_debug_sourceLocationResolver(pc24 & 0x00ffffffu, outLocation, e9k_debug_sourceLocationResolverUser) ? 1 : 0;
}

static int
e9k_debug_tryGetCallReturnPc(uint32_t pc24, uae_u16 opcode, uint32_t *outReturnPc)
{
	if (!outReturnPc) {
		return 0;
	}

	if ((opcode & 0xFFC0u) == 0x4E80u) {
		int mode = (opcode >> 3) & 7;
		int reg = opcode & 7;
		uint32_t ext = 0;
		if (mode == 5 || mode == 6) {
			ext = 2;
		} else if (mode == 7) {
			if (reg == 0 || reg == 2 || reg == 3) {
				ext = 2;
			} else if (reg == 1) {
				ext = 4;
			} else {
				return 0;
			}
		} else if (mode < 2) {
			return 0;
		}
		*outReturnPc = (pc24 + 2u + ext) & 0x00ffffffu;
		return 1;
	}

	if ((opcode & 0xFF00u) == 0x6100u) {
		uint32_t disp8 = opcode & 0x00ffu;
		uint32_t len = 2u;
		if (disp8 == 0u) {
			len = 4u;
		} else if (disp8 == 0xffu) {
			len = 6u;
		}
		*outReturnPc = (pc24 + len) & 0x00ffffffu;
		return 1;
	}

	return 0;
}

static void
e9k_debug_ensureMemhooks(void)
{
	if (debug_enableE9kHooks()) {
		e9k_debug_memhooksEnabled = 1;
	} else {
		e9k_debug_memhooksEnabled = 0;
	}
}

static int
e9k_debug_watchpointMatch(const e9k_debug_watchpoint_t *wp, uint32_t accessAddr, uint32_t accessKind,
                          uint32_t accessSizeBits, uint32_t value, uint32_t oldValue, int oldValueValid)
{
	if (!wp) {
		return 0;
	}
	uint32_t op = wp->op_mask;

	if (accessKind == E9K_WATCH_ACCESS_READ) {
		if ((op & E9K_WATCH_OP_READ) == 0u) {
			return 0;
		}
	} else if (accessKind == E9K_WATCH_ACCESS_WRITE) {
		if ((op & E9K_WATCH_OP_WRITE) == 0u) {
			return 0;
		}
	} else {
		return 0;
	}

	if (op & E9K_WATCH_OP_ADDR_COMPARE_MASK) {
		uint32_t mask = wp->addr_mask_operand;
		if (mask != 0u) {
			if ((accessAddr & mask) != (wp->addr & mask)) {
				return 0;
			}
		}
	} else {
		if (accessAddr != wp->addr) {
			return 0;
		}
	}

	if (op & E9K_WATCH_OP_ACCESS_SIZE) {
		if (wp->size_operand != 8u && wp->size_operand != 16u && wp->size_operand != 32u) {
			return 0;
		}
		if (accessSizeBits != wp->size_operand) {
			return 0;
		}
	}

	uint32_t v = e9k_debug_maskValue(value, accessSizeBits);
	uint32_t ov = e9k_debug_maskValue(oldValue, accessSizeBits);

	if (op & E9K_WATCH_OP_VALUE_EQ) {
		if (v != e9k_debug_maskValue(wp->value_operand, accessSizeBits)) {
			return 0;
		}
	}
	if (op & E9K_WATCH_OP_OLD_VALUE_EQ) {
		if (!oldValueValid) {
			return 0;
		}
		if (ov != e9k_debug_maskValue(wp->old_value_operand, accessSizeBits)) {
			return 0;
		}
	}
	if (op & E9K_WATCH_OP_VALUE_NEQ_OLD) {
		if (!oldValueValid) {
			return 0;
		}
		if (ov == e9k_debug_maskValue(wp->diff_operand, accessSizeBits)) {
			return 0;
		}
	}

	return 1;
}

static void
e9k_debug_watchbreakRequest(uint32_t index, uint32_t accessAddr, uint32_t accessKind, uint32_t accessSizeBits,
                            uint32_t value, uint32_t oldValue, int oldValueValid)
{
	if (e9k_debug_watchbreakPending) {
		return;
	}
	if (index >= E9K_WATCHPOINT_COUNT) {
		return;
	}

	e9k_debug_watchpoint_t *wp = &e9k_debug_watchpoints[index];

	memset(&e9k_debug_watchbreak, 0, sizeof(e9k_debug_watchbreak));
	e9k_debug_watchbreak.index = index;
	e9k_debug_watchbreak.watch_addr = wp->addr;
	e9k_debug_watchbreak.op_mask = wp->op_mask;
	e9k_debug_watchbreak.diff_operand = wp->diff_operand;
	e9k_debug_watchbreak.value_operand = wp->value_operand;
	e9k_debug_watchbreak.old_value_operand = wp->old_value_operand;
	e9k_debug_watchbreak.size_operand = wp->size_operand;
	e9k_debug_watchbreak.addr_mask_operand = wp->addr_mask_operand;

	e9k_debug_watchbreak.access_addr = accessAddr;
	e9k_debug_watchbreak.access_kind = accessKind;
	e9k_debug_watchbreak.access_size = accessSizeBits;
	e9k_debug_watchbreak.value = e9k_debug_maskValue(value, accessSizeBits);
	e9k_debug_watchbreak.old_value = e9k_debug_maskValue(oldValue, accessSizeBits);
	e9k_debug_watchbreak.old_value_valid = oldValueValid ? 1u : 0u;

	e9k_debug_watchbreakPending = 1;
	e9k_debug_requestBreak();
}

static int
e9k_debug_hasBreakpoint(uint32_t addr)
{
	for (size_t i = 0; i < e9k_debug_breakpointCount; ++i) {
		if (e9k_debug_breakpoints[i] == addr) {
			return 1;
		}
	}
	return 0;
}

static int
e9k_debug_consumeTempBreakpoint(uint32_t addr)
{
	for (size_t i = 0; i < e9k_debug_tempBreakpointCount; ++i) {
		if (e9k_debug_tempBreakpoints[i] == addr) {
			size_t remain = e9k_debug_tempBreakpointCount - (i + 1u);
			if (remain) {
				memmove(&e9k_debug_tempBreakpoints[i], &e9k_debug_tempBreakpoints[i + 1u], remain * sizeof(e9k_debug_tempBreakpoints[0]));
			}
			e9k_debug_tempBreakpointCount--;
			return 1;
		}
	}
	return 0;
}

E9K_DEBUG_EXPORT void
e9k_debug_pause(void)
{
	// Use the same break mechanism as instruction/watch breaks so execution halts immediately
	// (important when running with threaded CPU/event loops).
	e9k_debug_requestBreak();
}

E9K_DEBUG_EXPORT void
e9k_debug_resume(void)
{
	e9k_debug_paused = 0;
	e9k_debug_stepInstr = 0;
	e9k_debug_stepInstrAfter = 0;
	e9k_debug_stepLine = 0;
	e9k_debug_stepNext = 0;
	e9k_debug_stepNextSkipOnce = 0;
	e9k_debug_stepNextReturnPcValid = 0;
	e9k_debug_stepOut = 0;
	e9k_debug_stepOutDepth = 0;
	e9k_debug_stepOutSkipOnce = 0;
	e9k_debug_stepIntoPending = 0;

	uint32_t pc24 = e9k_debug_maskAddr(m68k_getpc());
	if (e9k_debug_hasBreakpoint(pc24)) {
		e9k_debug_skipBreakpointOnce = 1;
		e9k_debug_skipBreakpointPc = pc24;
	}
}

E9K_DEBUG_EXPORT int
e9k_debug_is_paused(void)
{
	return e9k_debug_paused;
}

E9K_DEBUG_EXPORT void
e9k_debug_step_instr(void)
{
	e9k_debug_paused = 0;
	e9k_debug_stepLine = 0;
	e9k_debug_stepNext = 0;
	e9k_debug_stepNextSkipOnce = 0;
	e9k_debug_stepNextReturnPcValid = 0;
	e9k_debug_stepOut = 0;
	e9k_debug_stepOutDepth = 0;
	e9k_debug_stepOutSkipOnce = 0;
	e9k_debug_stepIntoPending = 0;
	e9k_debug_stepInstr = 1;
	e9k_debug_stepInstrAfter = 0;
}

E9K_DEBUG_EXPORT void
e9k_debug_step_line(void)
{
	e9k_debug_paused = 0;
	e9k_debug_stepInstr = 0;
	e9k_debug_stepInstrAfter = 0;
	e9k_debug_stepLine = 1;
	e9k_debug_stepNext = 0;
	e9k_debug_stepNextSkipOnce = 0;
	e9k_debug_stepNextReturnPcValid = 0;
	e9k_debug_stepOut = 0;
	e9k_debug_stepOutDepth = 0;
	e9k_debug_stepOutSkipOnce = 0;
	e9k_debug_stepIntoPending = 0;

	uint32_t pc24 = e9k_debug_maskAddr(m68k_getpc());
	if (e9k_debug_resolveSourceLocation(pc24, &e9k_debug_stepLineStart)) {
		e9k_debug_stepLineHasStart = 1;
	} else {
		e9k_debug_stepLineHasStart = 0;
		e9k_debug_stepLineStart = 0;
	}
}

E9K_DEBUG_EXPORT void
e9k_debug_step_next(void)
{
	e9k_debug_paused = 0;
	e9k_debug_stepInstr = 0;
	e9k_debug_stepInstrAfter = 0;
	e9k_debug_stepLine = 1;
	e9k_debug_stepNext = 1;
	e9k_debug_stepNextDepth = e9k_debug_callstackDepth;
	e9k_debug_stepNextSkipOnce = 0;
	e9k_debug_stepNextReturnPcValid = 0;
	e9k_debug_stepNextReturnPc = 0;
	e9k_debug_stepOut = 0;
	e9k_debug_stepOutDepth = 0;
	e9k_debug_stepOutSkipOnce = 0;
	e9k_debug_stepIntoPending = 0;
	uint32_t pc24 = e9k_debug_maskAddr(m68k_getpc());
	uae_u16 opcode = get_word(munge24((uaecptr)pc24));
	if (e9k_debug_resolveSourceLocation(pc24, &e9k_debug_stepLineStart)) {
		e9k_debug_stepLineHasStart = 1;
	} else {
		e9k_debug_stepLineHasStart = 0;
		e9k_debug_stepLineStart = 0;
	}
	if (e9k_debug_tryGetCallReturnPc(pc24, opcode, &e9k_debug_stepNextReturnPc)) {
		e9k_debug_stepNextReturnPcValid = 1;
	}
}

E9K_DEBUG_EXPORT void
e9k_debug_step_out(void)
{
	e9k_debug_paused = 0;
	e9k_debug_stepInstr = 0;
	e9k_debug_stepInstrAfter = 0;
	e9k_debug_stepLine = 1;
	e9k_debug_stepNext = 0;
	e9k_debug_stepNextSkipOnce = 0;
	e9k_debug_stepNextReturnPcValid = 0;
	e9k_debug_stepOut = 1;
	e9k_debug_stepOutDepth = (e9k_debug_callstackDepth > 0) ? (e9k_debug_callstackDepth - 1u) : 0u;
	e9k_debug_stepOutSkipOnce = 0;
	e9k_debug_stepIntoPending = 0;

	uint32_t pc24 = e9k_debug_maskAddr(m68k_getpc());
	if (e9k_debug_resolveSourceLocation(pc24, &e9k_debug_stepLineStart)) {
		e9k_debug_stepLineHasStart = 1;
	} else {
		e9k_debug_stepLineHasStart = 0;
		e9k_debug_stepLineStart = 0;
	}
}

E9K_DEBUG_EXPORT size_t
e9k_debug_read_callstack(uint32_t *out, size_t cap)
{
	if (!out || cap == 0) {
		return 0;
	}
	size_t count = e9k_debug_callstackDepth;
	if (count > cap) {
		count = cap;
	}
	for (size_t i = 0; i < count; ++i) {
		out[i] = e9k_debug_callstack[i];
	}
	return count;
}

E9K_DEBUG_EXPORT size_t
e9k_debug_read_regs(uint32_t *out, size_t cap)
{
	if (!out || cap == 0) {
		return 0;
	}
	MakeSR();
	size_t count = 0;
	for (int i = 0; i < 16 && count < cap; ++i) {
		out[count++] = regs.regs[i];
	}
	if (count < cap) {
		out[count++] = regs.sr;
	}
	if (count < cap) {
		out[count++] = e9k_debug_maskAddr(m68k_getpc());
	}
	return count;
}

E9K_DEBUG_EXPORT size_t
e9k_debug_read_memory(uint32_t addr, uint8_t *out, size_t cap)
{
	if (!out || cap == 0) {
		return 0;
	}
	e9k_debug_watchpointSuspend++;
	uaecptr base = (uaecptr)addr;
	for (size_t i = 0; i < cap; ++i) {
		out[i] = (uint8_t)get_byte_debug(munge24(base + (uaecptr)i));
	}
	e9k_debug_watchpointSuspend--;
	return cap;
}

E9K_DEBUG_EXPORT int
e9k_debug_write_memory(uint32_t addr, uint32_t value, size_t size)
{
	e9k_debug_watchpointSuspend++;
	uaecptr a = munge24((uaecptr)addr);
	if (size == 1) {
		put_byte(a, value & 0xffu);
		e9k_debug_watchpointSuspend--;
		return 1;
	}
	if (size == 2) {
		put_word(a, value & 0xffffu);
		e9k_debug_watchpointSuspend--;
		return 1;
	}
	if (size == 4) {
		put_long(a, value);
		e9k_debug_watchpointSuspend--;
		return 1;
	}
	e9k_debug_watchpointSuspend--;
	return 0;
}

E9K_DEBUG_EXPORT size_t
e9k_debug_disassemble_quick(uint32_t pc, char *out, size_t cap)
{
	if (!out || cap == 0) {
		return 0;
	}
	e9k_debug_watchpointSuspend++;
	uaecptr nextpc = 0xffffffffu;
	int bufsize = (cap > 0x7fffffffU) ? 0x7fffffff : (int)cap;
	uaecptr addr = munge24((uaecptr)pc);
	m68k_disasm_2(out, bufsize, addr, NULL, 0, &nextpc, 1, NULL, NULL, 0xffffffffu, 0);
	out[bufsize - 1] = '\0';

	if (nextpc != 0xffffffffu && nextpc > addr) {
		e9k_debug_watchpointSuspend--;
		return (size_t)(nextpc - addr);
	}
	e9k_debug_watchpointSuspend--;
	return 2;
}

E9K_DEBUG_EXPORT uint64_t
e9k_debug_read_cycle_count(void)
{
	// get_cycles() returns UAE internal "cycle units" (CYCLE_UNIT = 512), not raw CPU cycles.
	// Convert to a more intuitive cycle count for the debugger UI.
	evt_t c = get_cycles();
	if (CYCLE_UNIT > 0) {
		return (uint64_t)(c / (evt_t)CYCLE_UNIT);
	}
	return (uint64_t)c;
}

E9K_DEBUG_EXPORT void
e9k_debug_add_breakpoint(uint32_t addr)
{
	uint32_t addr24 = e9k_debug_maskAddr((uaecptr)addr);
	if (e9k_debug_hasBreakpoint(addr24)) {
		return;
	}
	if (e9k_debug_breakpointCount >= E9K_DEBUG_BREAKPOINT_MAX) {
		return;
	}
	e9k_debug_breakpoints[e9k_debug_breakpointCount++] = addr24;
}

E9K_DEBUG_EXPORT void
e9k_debug_remove_breakpoint(uint32_t addr)
{
	uint32_t addr24 = e9k_debug_maskAddr((uaecptr)addr);
	for (size_t i = 0; i < e9k_debug_breakpointCount; ++i) {
		if (e9k_debug_breakpoints[i] == addr24) {
			size_t remain = e9k_debug_breakpointCount - (i + 1u);
			if (remain) {
				memmove(&e9k_debug_breakpoints[i], &e9k_debug_breakpoints[i + 1u], remain * sizeof(e9k_debug_breakpoints[0]));
			}
			e9k_debug_breakpointCount--;
			return;
		}
	}
}

E9K_DEBUG_EXPORT void
e9k_debug_add_temp_breakpoint(uint32_t addr)
{
	uint32_t addr24 = e9k_debug_maskAddr((uaecptr)addr);
	for (size_t i = 0; i < e9k_debug_tempBreakpointCount; ++i) {
		if (e9k_debug_tempBreakpoints[i] == addr24) {
			return;
		}
	}
	if (e9k_debug_tempBreakpointCount >= E9K_DEBUG_BREAKPOINT_MAX) {
		return;
	}
	e9k_debug_tempBreakpoints[e9k_debug_tempBreakpointCount++] = addr24;
}

E9K_DEBUG_EXPORT void
e9k_debug_remove_temp_breakpoint(uint32_t addr)
{
	uint32_t addr24 = e9k_debug_maskAddr((uaecptr)addr);
	(void)e9k_debug_consumeTempBreakpoint(addr24);
}

E9K_DEBUG_EXPORT void
e9k_debug_set_vblank_callback(void (*cb)(void *), void *user)
{
	e9k_debug_vblankCb = cb;
	e9k_debug_vblankUser = user;
}

#if E9K_DEBUGGER_CUSTOM_LOGGER
E9K_DEBUG_EXPORT void
e9k_debug_set_custom_log_frame_callback(e9k_debug_ami_custom_log_frame_callback_t cb, void *user)
{
	e9k_debug_amiCustomLogFrameCb = cb;
	e9k_debug_amiCustomLogFrameCbUser = user;
	e9k_debug_amiCustomLogEntryCount = 0;
	e9k_debug_amiCustomLogDropped = 0;
	e9k_debug_amiCustomLogFrameNo = 0;
}

void
e9k_debug_ami_customLogWrite(int vpos, int hpos, uae_u32 reg, uae_u16 value, uae_u32 sourcePc)
{
	if (e9k_debug_amiCustomLogEntryCount >= E9K_DEBUG_AMI_CUSTOM_LOG_ENTRY_CAP) {
		if (e9k_debug_amiCustomLogDropped != UINT32_MAX) {
			e9k_debug_amiCustomLogDropped++;
		}
		return;
	}

	e9k_debug_ami_custom_log_entry_t *entry = &e9k_debug_amiCustomLogEntries[e9k_debug_amiCustomLogEntryCount++];
	entry->vpos = (uint16_t)(vpos & 0xffff);
	entry->hpos = (uint16_t)(hpos & 0xffff);
	entry->reg = (uint16_t)(reg & 0x1feu);
	entry->value = value;
	if (sourcePc & 1u) {
		entry->sourceIsCopper = 1u;
		entry->sourceAddr = sourcePc & ~1u;
	} else {
		entry->sourceIsCopper = 0u;
		entry->sourceAddr = sourcePc;
	}
	entry->reserved[0] = 0;
	entry->reserved[1] = 0;
	entry->reserved[2] = 0;
}

void
e9k_debug_ami_customLogFrameCommit(void)
{
	e9k_debug_ami_customLogFlushFrame();
}
#else
void
e9k_debug_ami_customLogWrite(int vpos, int hpos, uae_u32 reg, uae_u16 value, uae_u32 sourcePc)
{
	(void)vpos;
	(void)hpos;
	(void)reg;
	(void)value;
	(void)sourcePc;
}

void
e9k_debug_ami_customLogFrameCommit(void)
{
}
#endif

E9K_DEBUG_EXPORT void
e9k_debug_set_debug_base_callback(void (*cb)(uint32_t section, uint32_t base))
{
	e9k_debug_setDebugBaseCb = cb;
}

E9K_DEBUG_EXPORT void
e9k_debug_set_debug_base_stack_callback(void (*cb)(uint32_t section, uint32_t base, uint32_t size))
{
	e9k_debug_pushDebugBaseCb = cb;
}

E9K_DEBUG_EXPORT void
e9k_debug_set_debug_breakpoint_callback(void (*cb)(uint32_t addr))
{
	e9k_debug_setDebugBreakpointCb = cb;
}

E9K_DEBUG_EXPORT void
e9k_debug_set_source_location_resolver(int (*resolver)(uint32_t pc24, uint64_t *out_location, void *user), void *user)
{
	e9k_debug_sourceLocationResolver = resolver;
	e9k_debug_sourceLocationResolverUser = user;
}

E9K_DEBUG_EXPORT void
e9k_debug_set_debug_option(e9k_debug_option_t option, uint32_t argument, void *user)
{
	(void)user;
	switch (option) {
		case E9K_DEBUG_OPTION_AMIGA_BLITTER:
			blitter_setDestinationWriteEnabled(argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_SPRITE0:
			drawing_setSpriteEnabled(0, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_SPRITE1:
			drawing_setSpriteEnabled(1, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_SPRITE2:
			drawing_setSpriteEnabled(2, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_SPRITE3:
			drawing_setSpriteEnabled(3, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_SPRITE4:
			drawing_setSpriteEnabled(4, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_SPRITE5:
			drawing_setSpriteEnabled(5, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_SPRITE6:
			drawing_setSpriteEnabled(6, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_SPRITE7:
			drawing_setSpriteEnabled(7, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_BITPLANE0:
			e9k_debug_setBitplaneEnabled(0, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_BITPLANE1:
			e9k_debug_setBitplaneEnabled(1, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_BITPLANE2:
			e9k_debug_setBitplaneEnabled(2, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_BITPLANE3:
			e9k_debug_setBitplaneEnabled(3, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_BITPLANE4:
			e9k_debug_setBitplaneEnabled(4, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_BITPLANE5:
			e9k_debug_setBitplaneEnabled(5, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_BITPLANE6:
			e9k_debug_setBitplaneEnabled(6, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_BITPLANE7:
			e9k_debug_setBitplaneEnabled(7, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_AUDIO0:
			e9k_debug_setAudioChannelEnabled(0, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_AUDIO1:
			e9k_debug_setAudioChannelEnabled(1, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_AUDIO2:
			e9k_debug_setAudioChannelEnabled(2, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_AUDIO3:
			e9k_debug_setAudioChannelEnabled(3, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_BPLCON1_DELAY_SCROLL:
			custom_setBplcon1DelayScrollEnabled(argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_BLITTER_VIS_DECAY:
#if E9K_HACK_BLITTER_VIS
			blitter_setDebugVisDecayFrames(argument);
#else
			(void)argument;
#endif
			break;
		case E9K_DEBUG_OPTION_AMIGA_BLITTER_VIS_MODE:
#if E9K_HACK_BLITTER_VIS
			blitter_setDebugVisMode((int)argument);
#else
			(void)argument;
#endif
			break;
		case E9K_DEBUG_OPTION_AMIGA_BLITTER_VIS_BLINK:
#if E9K_HACK_BLITTER_VIS
			blitter_setDebugVisBlink(argument != 0u);
#else
			(void)argument;
#endif
			break;
		case E9K_DEBUG_OPTION_AMIGA_COPPER_LINE_LIMIT_ENABLED:
			custom_setCopperLineLimitEnabled(argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_COPPER_LINE_LIMIT_START:
			custom_setCopperLineLimitStart((int)argument);
			break;
		case E9K_DEBUG_OPTION_AMIGA_COPPER_LINE_LIMIT_END:
			custom_setCopperLineLimitEnd((int)argument);
			break;
		case E9K_DEBUG_OPTION_AMIGA_BPLPTR_BLOCK_ALL:
			custom_setBitplanePointerWriteBlockAllEnabled(argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_BPLPTR1_BLOCK:
			custom_setBitplanePointerWriteBlockEnabled(0, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_BPLPTR2_BLOCK:
			custom_setBitplanePointerWriteBlockEnabled(1, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_BPLPTR3_BLOCK:
			custom_setBitplanePointerWriteBlockEnabled(2, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_BPLPTR4_BLOCK:
			custom_setBitplanePointerWriteBlockEnabled(3, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_BPLPTR5_BLOCK:
			custom_setBitplanePointerWriteBlockEnabled(4, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_BPLPTR6_BLOCK:
			custom_setBitplanePointerWriteBlockEnabled(5, argument != 0u);
			break;
		case E9K_DEBUG_OPTION_AMIGA_BPLPTR_LINE_LIMIT_START:
			custom_setBitplanePointerWriteBlockLineLimitStart((int)argument);
			break;
		case E9K_DEBUG_OPTION_AMIGA_BPLPTR_LINE_LIMIT_END:
			custom_setBitplanePointerWriteBlockLineLimitEnd((int)argument);
			break;
#if E9K_DEBUGGER_CUSTOM_LOGGER
		case E9K_DEBUG_OPTION_AMIGA_CUSTOM_LOGGER:
			custom_setCustomLoggerEnabled(argument != 0u);
			break;
#endif
		default:
			break;
	}
}

void
e9k_debug_add_breakpoint_fromPeripheral(uint32_t addr)
{
	uint32_t addr24 = e9k_debug_maskAddr((uaecptr)addr);
	int had = e9k_debug_hasBreakpoint(addr24);
	e9k_debug_add_breakpoint(addr24);
	if (!had && e9k_debug_setDebugBreakpointCb) {
		e9k_debug_setDebugBreakpointCb(addr24);
	}
}

E9K_DEBUG_EXPORT void
e9k_vblank_notify(void)
{
#if E9K_HACK_BLITTER_VIS
	custom_blitterVisFrameTick();
	blitter_debugFrameTick();
#endif
	if (e9k_debug_protectEnabledMask || e9k_debug_watchpointEnabledMask) {
		e9k_debug_ensureMemhooks();
	}
	if (e9k_debug_profilerEnabled && !e9k_debug_paused) {
		if (e9k_debug_prof_tick == e9k_debug_prof_lastTickAtFrame) {
			uaecptr pc = m68k_getpc();
			e9k_debug_profiler_samplePc(e9k_debug_maskAddr(pc));
		}
		e9k_debug_prof_lastTickAtFrame = e9k_debug_prof_tick;
	}
	if (e9k_debug_vblankCb) {
		e9k_debug_vblankCb(e9k_debug_vblankUser);
	}
}

E9K_DEBUG_EXPORT void
e9k_debug_ami_set_blitter_debug(int enabled)
{
#if E9K_HACK_BLITTER_VIS
	blitter_setDebugWriteEnabled(enabled ? 1 : 0);
#else
	(void)enabled;
#endif
}

E9K_DEBUG_EXPORT int
e9k_debug_ami_get_blitter_debug(void)
{
#if E9K_HACK_BLITTER_VIS
	return blitter_getDebugWriteEnabled();
#else
	return 0;
#endif
}

#if E9K_HACK_AMI_SPRITE_VIS
E9K_DEBUG_EXPORT void
e9k_debug_ami_set_sprite_vis(int enabled)
{
	drawing_setSpriteVisEnabled(enabled ? 1 : 0);
}

E9K_DEBUG_EXPORT int
e9k_debug_ami_get_sprite_vis(void)
{
	return drawing_getSpriteVisEnabled();
}
#endif

E9K_DEBUG_EXPORT size_t
e9k_debug_ami_blitter_vis_read_points(e9k_debug_ami_blitter_vis_point_t *out, size_t cap, uint32_t *outWidth, uint32_t *outHeight)
{
#if E9K_HACK_BLITTER_VIS
	static uint32_t readLogCounter = 0;
	int collectMode = ((blitter_getDebugVisMode() & E9K_DEBUG_BLITTER_VIS_MODE_COLLECT) != 0) ? 1 : 0;
	uint32_t width = (uint32_t)retrow_crop;
	uint32_t height = (uint32_t)retroh_crop;
	if (outWidth) {
		*outWidth = width;
	}
	if (outHeight) {
		*outHeight = height;
	}

	size_t written = 0;
	size_t drained = 0;
	uint32_t sampleX = 0;
	uint32_t sampleY = 0;
	uint32_t sampleBlitId = 0;
	int sampleValid = 0;
	uint32_t minX = 0;
	uint32_t minY = 0;
	uint32_t maxX = 0;
	uint32_t maxY = 0;
	int boundsValid = 0;

	if (width == 0 || height == 0) {
		return 0;
	}
	if (collectMode) {
		for (uint32_t y = 0; y < height; y++) {
			for (uint32_t x = 0; x < width; x++) {
				uae_u32 blitId = 0;
				if (!drawing_blitterVisGetNativePixelBlitId((int)y, (int)x, &blitId)) {
					continue;
				}
				if (!sampleValid) {
					sampleValid = 1;
					sampleX = x;
					sampleY = y;
					sampleBlitId = blitId;
				}
				if (!boundsValid) {
					boundsValid = 1;
					minX = maxX = x;
					minY = maxY = y;
				} else {
					if (x < minX) {
						minX = x;
					}
					if (x > maxX) {
						maxX = x;
					}
					if (y < minY) {
						minY = y;
					}
					if (y > maxY) {
						maxY = y;
					}
				}
				drained++;
				if (out && written < cap) {
					out[written].x = (uint16_t)x;
					out[written].y = (uint16_t)y;
					out[written].blitId = blitId;
					written++;
				}
			}
		}
	} else {
		for (uint32_t y = 0; y < height; y++) {
			for (uint32_t x = 0; x < width; x++) {
				uae_u32 blitId = 0;
				if (!drawing_blitterVisGetNativePixelBlitId((int)y, (int)x, &blitId)) {
					continue;
				}
				if (!sampleValid) {
					sampleValid = 1;
					sampleX = x;
					sampleY = y;
					sampleBlitId = blitId;
				}
				if (!boundsValid) {
					boundsValid = 1;
					minX = maxX = x;
					minY = maxY = y;
				} else {
					if (x < minX) {
						minX = x;
					}
					if (x > maxX) {
						maxX = x;
					}
					if (y < minY) {
						minY = y;
					}
					if (y > maxY) {
						maxY = y;
					}
				}
				if (out && written < cap) {
					out[written].x = (uint16_t)x;
					out[written].y = (uint16_t)y;
					out[written].blitId = blitId;
				}
				written++;
			}
		}
	}
	readLogCounter++;
	return written;
#else
	(void)out;
	(void)cap;
	if (outWidth) {
		*outWidth = 0;
	}
	if (outHeight) {
		*outHeight = 0;
	}
	return 0;
#endif
}

#if E9K_HACK_AMI_SPRITE_VIS
E9K_DEBUG_EXPORT size_t
e9k_debug_ami_sprite_vis_read_points(e9k_debug_ami_sprite_vis_point_t *out, size_t cap, uint32_t *outWidth, uint32_t *outHeight)
{
	uint32_t width = (uint32_t)retrow_crop;
	uint32_t height = (uint32_t)retroh_crop;
	size_t written = 0;

	if (outWidth) {
		*outWidth = width;
	}
	if (outHeight) {
		*outHeight = height;
	}
	if (width == 0 || height == 0) {
		return 0;
	}
	for (uint32_t y = 0; y < height; ++y) {
		for (uint32_t x = 0; x < width; ++x) {
			uae_u32 spriteIndex = 0;
			if (!drawing_spriteVisGetNativePixelSpriteId((int)y, (int)x, &spriteIndex)) {
				continue;
			}
			if (out && written < cap) {
				out[written].x = (uint16_t)x;
				out[written].y = (uint16_t)y;
				out[written].spriteIndex = spriteIndex;
			}
			written++;
		}
	}
	return written;
}
#endif

E9K_DEBUG_EXPORT size_t
e9k_debug_ami_blitter_vis_read_stats(e9k_debug_ami_blitter_vis_stats_t *out, size_t cap)
{
#if E9K_HACK_BLITTER_VIS
	uint32_t maxWriteBytesEstimateFrame = 0u;
	if (!out || cap < sizeof(*out)) {
		return 0;
	}
	maxWriteBytesEstimateFrame = custom_blitterVisGetMaxWriteBytesLastFrame();
	out->enabled = blitter_getDebugWriteEnabled() ? 1u : 0u;
	out->mode = (uint32_t)blitter_getDebugVisMode();
	out->blink = blitter_getDebugVisBlink() ? 1u : 0u;
	out->livePhase = blitter_getDebugShowLivePhase() ? 1u : 0u;
	out->activeCount = blitter_getDebugActiveCount();
	out->blitsThisFrame = blitter_getDebugBlitsLastFrame();
	out->writesThisFrame = blitter_getDebugWritesLastFrame();
	out->writeBytesThisFrame = blitter_getDebugWriteBytesLastFrame();
	out->writeBytesMaxEstimateFrame = maxWriteBytesEstimateFrame;
	out->frameCounter = blitter_getDebugFrameCounter();
	out->fetchQueriesThisFrame = blitter_getDebugFetchQueriesThisFrame();
	out->fetchHitsThisFrame = blitter_getDebugFetchHitsThisFrame();
	out->drawMarkCallsFrame = drawing_blitterVisGetNativeMarkCallsFrame();
	out->drawMarkCallsSnapshot = drawing_blitterVisGetNativeMarkCallsSnapshot();
	return sizeof(*out);
#else
	(void)out;
	(void)cap;
	return 0;
#endif
}

static e9k_debug_ami_dma_debug_frame_view_t e9k_debug_dmaDebugFrameViews[2];
static e9k_debug_ami_copper_debug_frame_view_t e9k_debug_copperDebugFrameViews[2];

E9K_DEBUG_EXPORT const e9k_debug_ami_dma_debug_frame_view_t *
e9k_debug_ami_dma_debug_get_frame_view(uint32_t frameSelect)
{
#if E9K_HACK_DMA_DEBUG_EXPORT
	const struct dma_rec *records = NULL;
	int recordCount = 0;
	int hposCount = 0;
	int vposCount = 0;
	int frameNumber = -1;
	int recordToggle = -1;
	int dmaHoffset = 0;
	int debugDmaEnabled = 0;
	int coreFrameSelect = DEBUG_DMA_EXPORT_FRAME_LATEST_COMPLETE;
	int topEdgeX = 0;
	int topEdgeY = 0;
	int renderWidth = 0;
	int renderHeight = 0;
	int visibleOffsetX = 0;
	int visibleOffsetY = 0;
	e9k_debug_ami_dma_debug_frame_view_t *view = NULL;

	if (frameSelect == E9K_DEBUG_AMI_DMA_DEBUG_FRAME_ACTIVE) {
		coreFrameSelect = DEBUG_DMA_EXPORT_FRAME_ACTIVE;
	}
	if (!debug_dmaExportGetFrame(coreFrameSelect,
				&records,
				&recordCount,
				&hposCount,
				&vposCount,
				&frameNumber,
				&recordToggle,
				&dmaHoffset,
				&debugDmaEnabled)) {
		return NULL;
	}
	if (recordCount < 0) {
		recordCount = 0;
	}
	if (recordToggle < 0 || recordToggle > 1) {
		recordToggle = 0;
	}
	view = &e9k_debug_dmaDebugFrameViews[recordToggle];
	memset(view, 0, sizeof(*view));
	view->records = (const e9k_debug_ami_dma_debug_raw_record_t *)records;
	view->info.version = E9K_DEBUG_AMI_DMA_DEBUG_FRAME_INFO_VERSION;
	view->info.frameSelect = frameSelect;
	view->info.frameNumber = frameNumber;
	view->info.recordToggle = recordToggle;
	view->info.hposCount = hposCount;
	view->info.vposCount = vposCount;
	view->info.dmaHoffset = dmaHoffset;
	view->info.recordCount = (uint32_t)recordCount;
	view->info.debugDmaEnabled = debugDmaEnabled > 0 ? 1u : 0u;
	view->visibleWidth = retrow_crop;
	view->visibleHeight = retroh_crop;
	get_custom_topedge(&topEdgeX, &topEdgeY, false);
	(void)topEdgeX;
	visibleOffsetX = coord_diw_shres_to_window_x(denisehtotal) - retrow_crop;
	if (visibleOffsetX < 0) {
		visibleOffsetX = 0;
	}
	visibleOffsetY = topEdgeY;
	if (visibleOffsetY < 0) {
		visibleOffsetY = 0;
	}
	renderWidth = visibleOffsetX + retrow_crop;
	renderHeight = visibleOffsetY + retroh_crop;
	view->renderWidth = renderWidth;
	view->renderHeight = renderHeight;
	view->visibleOffsetX = visibleOffsetX;
	view->visibleOffsetY = visibleOffsetY;
	view->dhposWrap = denisehtotal >> 2;
	view->dhposScale = 1 << (2 - shres_shift);
	return view;
#else
	(void)frameSelect;
	return NULL;
#endif
}

#if E9K_HACK_COPPER_DEBUG_EXPORT
E9K_DEBUG_EXPORT const e9k_debug_ami_copper_debug_frame_view_t *
e9k_debug_ami_copper_debug_get_frame_view(uint32_t frameSelect)
{
	const struct cop_rec *records = NULL;
	int recordCount = 0;
	int frameNumber = -1;
	int recordToggle = -1;
	int debugCopperEnabled = 0;
	int coreFrameSelect = DEBUG_DMA_EXPORT_FRAME_LATEST_COMPLETE;
	e9k_debug_ami_copper_debug_frame_view_t *view = NULL;

	if (frameSelect == E9K_DEBUG_AMI_COPPER_DEBUG_FRAME_ACTIVE) {
		coreFrameSelect = DEBUG_DMA_EXPORT_FRAME_ACTIVE;
	}
	if (!debug_copperExportGetFrame(coreFrameSelect,
				&records,
				&recordCount,
				&frameNumber,
				&recordToggle,
				&debugCopperEnabled)) {
		return NULL;
	}
	if (recordCount < 0) {
		recordCount = 0;
	}
	if (recordToggle < 0 || recordToggle > 1) {
		recordToggle = 0;
	}
	view = &e9k_debug_copperDebugFrameViews[recordToggle];
	memset(view, 0, sizeof(*view));
	view->records = (const e9k_debug_ami_copper_debug_raw_record_t *)records;
	view->info.version = E9K_DEBUG_AMI_COPPER_DEBUG_FRAME_INFO_VERSION;
	view->info.frameSelect = frameSelect;
	view->info.frameNumber = frameNumber;
	view->info.recordToggle = recordToggle;
	view->info.recordCount = (uint32_t)recordCount;
	view->info.debugCopperEnabled = debugCopperEnabled > 0 ? 1u : 0u;
	return view;
}
#endif

E9K_DEBUG_EXPORT int
e9k_debug_ami_get_video_line_count(void)
{
	if (retroh_crop <= 0) {
		return 0;
	}
	return (int)retroh_crop;
}

E9K_DEBUG_EXPORT int
e9k_debug_ami_video_line_to_core_line(int videoLine)
{
	int lineCount = e9k_debug_ami_get_video_line_count();
	if (videoLine < 0 || videoLine >= lineCount) {
		return -1;
	}
	int nativeLine = videoLine + (int)retroy_crop;
	return coord_native_to_amiga_y(nativeLine);
}

static int
e9k_debug_absInt(int value)
{
	if (value < 0) {
		return -value;
	}
	return value;
}

E9K_DEBUG_EXPORT int
e9k_debug_ami_core_line_to_video_line(int coreLine)
{
	if (coreLine < 0) {
		return -1;
	}
	int lineCount = e9k_debug_ami_get_video_line_count();
	if (lineCount <= 0) {
		return -1;
	}
	int bestVideoLine = -1;
	int bestDelta = 0x7fffffff;
	for (int videoLine = 0; videoLine < lineCount; ++videoLine) {
		int mappedCoreLine = e9k_debug_ami_video_line_to_core_line(videoLine);
		if (mappedCoreLine < 0) {
			continue;
		}
		int delta = e9k_debug_absInt(mappedCoreLine - coreLine);
		if (delta < bestDelta) {
			bestDelta = delta;
			bestVideoLine = videoLine;
			if (delta == 0) {
				break;
			}
		}
	}
	return bestVideoLine;
}

void
e9k_debug_ami_on_video_presented(void)
{
#if E9K_HACK_BLITTER_VIS
	uint32_t decayFrames = blitter_getDebugVisDecayFrames();
	int collectMode = ((blitter_getDebugVisMode() & E9K_DEBUG_BLITTER_VIS_MODE_COLLECT) != 0) ? 1 : 0;
	if (blitter_getDebugWriteEnabled()) {
		drawing_blitterVisSnapshotFrame();
		if (collectMode) {
			drawing_blitterVisClearFrame();
		}
	}
	if (!collectMode) {
		blitter_debugRestoreWritesOlderThan(decayFrames);
	}
#endif
#if E9K_HACK_AMI_SPRITE_VIS
	if (drawing_getSpriteVisEnabled()) {
		drawing_spriteVisSnapshotFrame();
		drawing_spriteVisClearFrame();
	}
#endif
}

E9K_DEBUG_EXPORT void
e9k_debug_reapply_memhooks(void)
{
	if (e9k_debug_protectEnabledMask || e9k_debug_watchpointEnabledMask) {
		e9k_debug_ensureMemhooks();
	}
}

static void
e9k_debug_requestBreak(void)
{
	e9k_debug_paused = 1;
	e9k_debug_stepInstr = 0;
	e9k_debug_stepInstrAfter = 0;
	e9k_debug_stepLine = 0;
	e9k_debug_stepNext = 0;
	e9k_debug_stepNextSkipOnce = 0;
	e9k_debug_stepNextReturnPcValid = 0;
	e9k_debug_stepOut = 0;
	e9k_debug_stepOutDepth = 0;
	e9k_debug_stepOutSkipOnce = 0;
	e9k_debug_stepIntoPending = 0;
	libretro_frame_end = true;
	set_special(SPCFLAG_BRK);
}

static void
e9k_debug_watchpointRead(uint32_t addr24, uint32_t value, uint32_t sizeBits)
{
	if (e9k_debug_watchpointSuspend > 0) {
		return;
	}
	if (e9k_debug_paused) {
		return;
	}
	if (e9k_debug_watchpointEnabledMask == 0) {
		return;
	}

	for (uint32_t index = 0; index < E9K_WATCHPOINT_COUNT; ++index) {
		if ((e9k_debug_watchpointEnabledMask & (1ull << index)) == 0ull) {
			continue;
		}
		if (e9k_debug_watchpointMatch(&e9k_debug_watchpoints[index], addr24, E9K_WATCH_ACCESS_READ, sizeBits, value, value, 1)) {
			e9k_debug_watchbreakRequest(index, addr24, E9K_WATCH_ACCESS_READ, sizeBits, value, value, 1);
			return;
		}
	}
}

static void
e9k_debug_watchpointWrite(uint32_t addr24, uint32_t value, uint32_t oldValue, uint32_t sizeBits, int oldValueValid)
{
	if (e9k_debug_watchpointSuspend > 0) {
		return;
	}
	if (e9k_debug_paused) {
		return;
	}
	if (e9k_debug_watchpointEnabledMask == 0) {
		return;
	}

	for (uint32_t index = 0; index < E9K_WATCHPOINT_COUNT; ++index) {
		if ((e9k_debug_watchpointEnabledMask & (1ull << index)) == 0ull) {
			continue;
		}
		if (e9k_debug_watchpointMatch(&e9k_debug_watchpoints[index], addr24, E9K_WATCH_ACCESS_WRITE, sizeBits, value, oldValue, oldValueValid)) {
			e9k_debug_watchbreakRequest(index, addr24, E9K_WATCH_ACCESS_WRITE, sizeBits, value, oldValue, oldValueValid);
			return;
		}
	}
}

static int
e9k_debug_protectFilterWrite(uint32_t addr24, uint32_t sizeBits, uint32_t oldValue, int oldValueValid, uint32_t *inoutValue)
{
	if (!inoutValue) {
		return 1;
	}
	if (e9k_debug_watchpointSuspend > 0) {
		return 1;
	}
	if (e9k_debug_protectEnabledMask == 0) {
		return 1;
	}

	uint32_t sizeBytes = e9k_debug_sizeBytes(sizeBits);
	if (sizeBytes == 0u) {
		return 1;
	}

	uint8_t bytes[4] = {0};
	uint8_t oldBytes[4] = {0};
	uint32_t v = e9k_debug_maskValue(*inoutValue, sizeBits);
	uint32_t ov = e9k_debug_maskValue(oldValue, sizeBits);

	for (uint32_t i = 0; i < sizeBytes; ++i) {
		uint32_t shift = (sizeBytes - 1u - i) * 8u;
		bytes[i] = (uint8_t)((v >> shift) & 0xffu);
		if (oldValueValid) {
			oldBytes[i] = (uint8_t)((ov >> shift) & 0xffu);
		}
	}

	for (uint32_t entryIndex = 0; entryIndex < E9K_PROTECT_COUNT; ++entryIndex) {
		if ((e9k_debug_protectEnabledMask & (1ull << entryIndex)) == 0ull) {
			continue;
		}
		const e9k_debug_protect_t *p = &e9k_debug_protects[entryIndex];
		if (p->mode != E9K_PROTECT_MODE_BLOCK) {
			continue;
		}
		uint32_t pSizeBytes = e9k_debug_sizeBytes(p->sizeBits);
		if (pSizeBytes == 0u) {
			continue;
		}
		uint32_t mask = p->addrMask ? p->addrMask : 0x00ffffffu;
		for (uint32_t writeIndex = 0; writeIndex < sizeBytes; ++writeIndex) {
			uint32_t writeAddr = (addr24 + writeIndex) & 0x00ffffffu;
			for (uint32_t byteIndex = 0; byteIndex < pSizeBytes; ++byteIndex) {
				uint32_t pa = (p->addr + byteIndex) & 0x00ffffffu;
				if ((writeAddr & mask) == (pa & mask)) {
					if (oldValueValid) {
						*inoutValue = ov;
						return 1;
					}
					return 0;
				}
			}
		}
	}

	for (uint32_t writeIndex = 0; writeIndex < sizeBytes; ++writeIndex) {
		uint32_t writeAddr = (addr24 + writeIndex) & 0x00ffffffu;
		for (uint32_t entryIndex = 0; entryIndex < E9K_PROTECT_COUNT; ++entryIndex) {
			if ((e9k_debug_protectEnabledMask & (1ull << entryIndex)) == 0ull) {
				continue;
			}
			const e9k_debug_protect_t *p = &e9k_debug_protects[entryIndex];
			if (p->mode != E9K_PROTECT_MODE_SET) {
				continue;
			}
			uint32_t pSizeBytes = e9k_debug_sizeBytes(p->sizeBits);
			if (pSizeBytes == 0u) {
				continue;
			}

			uint32_t mask = p->addrMask ? p->addrMask : 0x00ffffffu;
			for (uint32_t byteIndex = 0; byteIndex < pSizeBytes; ++byteIndex) {
				uint32_t pa = (p->addr + byteIndex) & 0x00ffffffu;
				if ((writeAddr & mask) != (pa & mask)) {
					continue;
				}

				if (p->mode == E9K_PROTECT_MODE_SET) {
					uint32_t pshift = (pSizeBytes - 1u - byteIndex) * 8u;
					bytes[writeIndex] = (uint8_t)((p->value >> pshift) & 0xffu);
				}
				goto next_write_byte;
			}
		}
next_write_byte:
		;
	}

	uint32_t outValue = 0;
	for (uint32_t i = 0; i < sizeBytes; ++i) {
		outValue = (outValue << 8) | (uint32_t)bytes[i];
	}
	*inoutValue = outValue;
	return 1;
}

E9K_DEBUG_EXPORT void
e9k_debug_memhook_afterRead(uint32_t addr24, uint32_t value, uint32_t sizeBits)
{
	addr24 &= 0x00ffffffu;
	e9k_debug_watchpointRead(addr24, value, sizeBits);
}

E9K_DEBUG_EXPORT int
e9k_debug_memhook_filterWrite(uint32_t addr24, uint32_t sizeBits, uint32_t oldValue, int oldValueValid, uint32_t *inoutValue)
{
	addr24 &= 0x00ffffffu;
	return e9k_debug_protectFilterWrite(addr24, sizeBits, oldValue, oldValueValid, inoutValue);
}

E9K_DEBUG_EXPORT void
e9k_debug_memhook_afterWrite(uint32_t addr24, uint32_t value, uint32_t oldValue, uint32_t sizeBits, int oldValueValid)
{
	addr24 &= 0x00ffffffu;
	e9k_debug_watchpointWrite(addr24, value, oldValue, sizeBits, oldValueValid);
}

E9K_DEBUG_EXPORT int
e9k_debug_instructionHook(uaecptr pc, uae_u16 opcode)
{
	uint32_t pc24 = e9k_debug_maskAddr(pc);

	e9k_debug_profiler_instrHook(pc24);

	if (e9k_debug_stepInstrAfter) {
		e9k_debug_requestBreak();
		return 1;
	}

	if ((opcode & 0xFFC0u) == 0x4E80u) {
		int mode = (opcode >> 3) & 7;
		int reg = opcode & 7;
		int ext = 0;
		if (mode == 5 || mode == 6) {
			ext = 2;
		} else if (mode == 7) {
			if (reg == 0 || reg == 2 || reg == 3) {
				ext = 2;
			} else if (reg == 1) {
				ext = 4;
			} else {
				ext = -1;
			}
		} else if (mode < 2) {
			ext = -1;
		}
		if (ext >= 0) {
			if (e9k_debug_callstackDepth < E9K_DEBUG_CALLSTACK_MAX) {
				e9k_debug_callstack[e9k_debug_callstackDepth++] = pc24;
			}
		}
	} else if ((opcode & 0xFF00u) == 0x6100u) {
		if (e9k_debug_callstackDepth < E9K_DEBUG_CALLSTACK_MAX) {
			e9k_debug_callstack[e9k_debug_callstackDepth++] = pc24;
		}
	} else if (opcode == 0x4E75u || opcode == 0x4E74u || opcode == 0x4E73u || opcode == 0x4E77u) {
		if (e9k_debug_callstackDepth > 0) {
			e9k_debug_callstackDepth--;
		}
		if (e9k_debug_stepNext) {
			e9k_debug_stepNextSkipOnce = 1;
		}
		if (e9k_debug_stepOut) {
			e9k_debug_stepOutSkipOnce = 1;
		}
	}

	if (e9k_debug_stepInstr) {
		e9k_debug_stepInstr = 0;
		e9k_debug_stepInstrAfter = 1;
		return 0;
	}
	if (e9k_debug_stepLine && !e9k_debug_stepNext && !e9k_debug_stepOut) {
		if (e9k_debug_stepIntoPending) {
			e9k_debug_stepIntoPending = 0;
			e9k_debug_requestBreak();
			return 1;
		}
		uint32_t returnPc = 0;
		if (e9k_debug_tryGetCallReturnPc(pc24, opcode, &returnPc)) {
			e9k_debug_stepIntoPending = 1;
			return 0;
		}
	}

	if (e9k_debug_stepNext && !e9k_debug_stepNextReturnPcValid) {
		uint64_t current = 0;
		if (e9k_debug_resolveSourceLocation(pc24, &current) && e9k_debug_stepLineHasStart && current == e9k_debug_stepLineStart) {
			if (e9k_debug_tryGetCallReturnPc(pc24, opcode, &e9k_debug_stepNextReturnPc)) {
				e9k_debug_stepNextReturnPcValid = 1;
			}
		}
	}

	if (e9k_debug_stepNext && e9k_debug_stepNextSkipOnce) {
		e9k_debug_stepNextSkipOnce = 0;
		return 0;
	}
	if (e9k_debug_stepOut && e9k_debug_stepOutSkipOnce) {
		e9k_debug_stepOutSkipOnce = 0;
		return 0;
	}

	if (e9k_debug_stepLine) {
		if (e9k_debug_stepNext && e9k_debug_stepNextReturnPcValid) {
			if (pc24 != e9k_debug_stepNextReturnPc) {
				goto skip_step_line_break;
			}
			e9k_debug_stepNextReturnPcValid = 0;
		}
		uint64_t current = 0;
		int hasCurrent = e9k_debug_resolveSourceLocation(pc24, &current);
		int shouldBreak = 0;
		if (hasCurrent) {
			if (!e9k_debug_stepLineHasStart) {
				shouldBreak = 1;
			} else if (current != e9k_debug_stepLineStart) {
				shouldBreak = 1;
			}
		}
		int stepNextReady = (!e9k_debug_stepNext || e9k_debug_callstackDepth <= e9k_debug_stepNextDepth);
		int stepOutReady = (!e9k_debug_stepOut || e9k_debug_callstackDepth <= e9k_debug_stepOutDepth);
		if (shouldBreak && stepNextReady && stepOutReady) {
			e9k_debug_requestBreak();
			return 1;
		}
	}
skip_step_line_break:

	if (e9k_debug_skipBreakpointOnce) {
		e9k_debug_skipBreakpointOnce = 0;
		if (pc24 == e9k_debug_skipBreakpointPc) {
			return 0;
		}
	}

	if (e9k_debug_consumeTempBreakpoint(pc24) || e9k_debug_hasBreakpoint(pc24)) {
		e9k_debug_requestBreak();
		return 1;
	}

	return 0;
}

E9K_DEBUG_EXPORT void
e9k_debug_reset_watchpoints(void)
{
	memset(e9k_debug_watchpoints, 0, sizeof(e9k_debug_watchpoints));
	e9k_debug_watchpointEnabledMask = 0;
	memset(&e9k_debug_watchbreak, 0, sizeof(e9k_debug_watchbreak));
	e9k_debug_watchbreakPending = 0;
	e9k_debug_watchpointSuspend = 0;
}

E9K_DEBUG_EXPORT int
e9k_debug_add_watchpoint(uint32_t addr, uint32_t op_mask, uint32_t diff_operand, uint32_t value_operand,
                         uint32_t old_value_operand, uint32_t size_operand, uint32_t addr_mask_operand)
{
	e9k_debug_ensureMemhooks();
	if (!e9k_debug_memhooksEnabled) {
		return -1;
	}
	for (uint32_t i = 0; i < E9K_WATCHPOINT_COUNT; ++i) {
		uint64_t bit = 1ull << i;
		if ((e9k_debug_watchpointEnabledMask & bit) != 0ull) {
			continue;
		}
		if (e9k_debug_watchpoints[i].op_mask != 0u) {
			continue;
		}
		e9k_debug_watchpoints[i].addr = addr & 0x00ffffffu;
		e9k_debug_watchpoints[i].op_mask = op_mask;
		e9k_debug_watchpoints[i].diff_operand = diff_operand;
		e9k_debug_watchpoints[i].value_operand = value_operand;
		e9k_debug_watchpoints[i].old_value_operand = old_value_operand;
		e9k_debug_watchpoints[i].size_operand = size_operand;
		e9k_debug_watchpoints[i].addr_mask_operand = addr_mask_operand;
		e9k_debug_watchpointEnabledMask |= bit;
		return (int)i;
	}
	return -1;
}

E9K_DEBUG_EXPORT void
e9k_debug_remove_watchpoint(uint32_t index)
{
	if (index >= E9K_WATCHPOINT_COUNT) {
		return;
	}
	e9k_debug_watchpointEnabledMask &= ~(1ull << index);
	memset(&e9k_debug_watchpoints[index], 0, sizeof(e9k_debug_watchpoints[index]));
}

E9K_DEBUG_EXPORT size_t
e9k_debug_read_watchpoints(e9k_debug_watchpoint_t *out, size_t cap)
{
	if (!out || cap == 0) {
		return 0;
	}
	size_t count = E9K_WATCHPOINT_COUNT;
	if (count > cap) {
		count = cap;
	}
	memcpy(out, e9k_debug_watchpoints, count * sizeof(out[0]));
	return count;
}

E9K_DEBUG_EXPORT uint64_t
e9k_debug_get_watchpoint_enabled_mask(void)
{
	return e9k_debug_watchpointEnabledMask;
}

E9K_DEBUG_EXPORT void
e9k_debug_set_watchpoint_enabled_mask(uint64_t mask)
{
	if (mask) {
		e9k_debug_ensureMemhooks();
		if (!e9k_debug_memhooksEnabled) {
			return;
		}
	}
	e9k_debug_watchpointEnabledMask = mask;
}

E9K_DEBUG_EXPORT int
e9k_debug_consume_watchbreak(e9k_debug_watchbreak_t *out)
{
	if (!out) {
		return 0;
	}
	if (!e9k_debug_watchbreakPending) {
		return 0;
	}
	*out = e9k_debug_watchbreak;
	e9k_debug_watchbreakPending = 0;
	return 1;
}

E9K_DEBUG_EXPORT void
e9k_debug_reset_protects(void)
{
	memset(e9k_debug_protects, 0, sizeof(e9k_debug_protects));
	e9k_debug_protectEnabledMask = 0;
}

E9K_DEBUG_EXPORT int
e9k_debug_add_protect(uint32_t addr, uint32_t size_bits, uint32_t mode, uint32_t value)
{
	e9k_debug_ensureMemhooks();
	if (!e9k_debug_memhooksEnabled) {
		return -1;
	}
	if (size_bits != 8u && size_bits != 16u && size_bits != 32u) {
		return -1;
	}
	if (mode != E9K_PROTECT_MODE_BLOCK && mode != E9K_PROTECT_MODE_SET) {
		return -1;
	}

	uint32_t addr24 = addr & 0x00ffffffu;
	uint32_t addrMask = 0x00ffffffu;
	uint32_t maskedValue = e9k_debug_maskValue(value, size_bits);

	for (uint32_t i = 0; i < E9K_PROTECT_COUNT; ++i) {
		if ((e9k_debug_protectEnabledMask & (1ull << i)) == 0ull) {
			continue;
		}
		const e9k_debug_protect_t *p = &e9k_debug_protects[i];
		if (p->addr == addr24 &&
		    p->addrMask == addrMask &&
		    p->sizeBits == size_bits &&
		    p->mode == mode &&
		    p->value == maskedValue) {
			return (int)i;
		}
	}

	for (uint32_t i = 0; i < E9K_PROTECT_COUNT; ++i) {
		if (e9k_debug_protects[i].sizeBits != 0u) {
			continue;
		}
		e9k_debug_protects[i].addr = addr24;
		e9k_debug_protects[i].addrMask = addrMask;
		e9k_debug_protects[i].sizeBits = size_bits;
		e9k_debug_protects[i].mode = mode;
		e9k_debug_protects[i].value = maskedValue;
		e9k_debug_protectEnabledMask |= (1ull << i);
		return (int)i;
	}

	return -1;
}

E9K_DEBUG_EXPORT void
e9k_debug_remove_protect(uint32_t index)
{
	if (index >= E9K_PROTECT_COUNT) {
		return;
	}
	memset(&e9k_debug_protects[index], 0, sizeof(e9k_debug_protects[index]));
	e9k_debug_protectEnabledMask &= ~(1ull << index);
}

E9K_DEBUG_EXPORT size_t
e9k_debug_read_protects(e9k_debug_protect_t *out, size_t cap)
{
	if (!out || cap == 0) {
		return 0;
	}
	size_t count = E9K_PROTECT_COUNT;
	if (count > cap) {
		count = cap;
	}
	memcpy(out, e9k_debug_protects, count * sizeof(out[0]));
	return count;
}

E9K_DEBUG_EXPORT uint64_t
e9k_debug_get_protect_enabled_mask(void)
{
	return e9k_debug_protectEnabledMask;
}

E9K_DEBUG_EXPORT void
e9k_debug_set_protect_enabled_mask(uint64_t mask)
{
	if (mask) {
		e9k_debug_ensureMemhooks();
		if (!e9k_debug_memhooksEnabled) {
			return;
		}
	}
	e9k_debug_protectEnabledMask = mask;
}

E9K_DEBUG_EXPORT void
e9k_debug_profiler_start(int stream)
{
	e9k_debug_profiler_reset();
	e9k_debug_prof_streamEnabled = stream ? 1 : 0;
	e9k_debug_profilerEnabled = 1;
#ifdef JIT
	if (e9k_debug_prof_savedCachesize < 0) {
		e9k_debug_prof_savedCachesize = currprefs.cachesize;
	}
	if (currprefs.cachesize) {
		currprefs.cachesize = 0;
		flush_icache(3);
		set_special(SPCFLAG_END_COMPILE);
	}
#endif
}

E9K_DEBUG_EXPORT void
e9k_debug_profiler_stop(void)
{
	e9k_debug_profilerEnabled = 0;
	e9k_debug_prof_streamEnabled = 0;
#ifdef JIT
	if (e9k_debug_prof_savedCachesize >= 0) {
		if (currprefs.cachesize != e9k_debug_prof_savedCachesize) {
			currprefs.cachesize = e9k_debug_prof_savedCachesize;
			flush_icache(3);
			set_special(SPCFLAG_END_COMPILE);
		}
		e9k_debug_prof_savedCachesize = -1;
	}
#endif
}

E9K_DEBUG_EXPORT int
e9k_debug_profiler_is_enabled(void)
{
	return e9k_debug_profilerEnabled;
}

E9K_DEBUG_EXPORT size_t
e9k_debug_profiler_stream_next(char *out, size_t cap)
{
	if (!out || cap == 0) {
		return 0;
	}

	if (!e9k_debug_prof_streamEnabled) {
		return 0;
	}
	if (e9k_debug_prof_dirtyCount == 0) {
		return 0;
	}

	const char *enabled = e9k_debug_profilerEnabled ? "enabled" : "disabled";
	size_t pos = 0;
	int written = snprintf(out, cap, "{\"stream\":\"profiler\",\"enabled\":\"%s\",\"hits\":[", enabled);
	if (written <= 0 || (size_t)written >= cap) {
		return 0;
	}
	pos = (size_t)written;

	int first = 1;
	uint32_t newDirtyCount = 0;
	for (uint32_t i = 0; i < e9k_debug_prof_dirtyCount; ++i) {
		uint32_t slot = e9k_debug_prof_dirtyIdx[i];
		if (slot >= E9K_DEBUG_PROF_TABLE_CAP) {
			continue;
		}
		uint32_t pc24 = e9k_debug_prof_pcs[slot];
		if (pc24 == E9K_DEBUG_PROF_EMPTY_PC) {
			e9k_debug_prof_entryEpoch[slot] = 0;
			continue;
		}
		unsigned long long samples = (unsigned long long)e9k_debug_prof_samples[slot];
		unsigned long long cycles = (unsigned long long)e9k_debug_prof_cycles[slot];
		if (samples == 0 && cycles == 0) {
			e9k_debug_prof_entryEpoch[slot] = 0;
			continue;
		}

		char entry[96];
		if (first) {
			written = snprintf(entry, sizeof(entry), "{\"pc\":\"0x%06X\",\"samples\":%llu,\"cycles\":%llu}",
			                   (unsigned)(pc24 & 0x00ffffffu), samples, cycles);
			first = 0;
		} else {
			written = snprintf(entry, sizeof(entry), ",{\"pc\":\"0x%06X\",\"samples\":%llu,\"cycles\":%llu}",
			                   (unsigned)(pc24 & 0x00ffffffu), samples, cycles);
		}
		if (written <= 0) {
			e9k_debug_prof_entryEpoch[slot] = 0;
			continue;
		}
		size_t need = (size_t)written;
		if (pos + need + 2 >= cap) {
			e9k_debug_prof_dirtyIdx[newDirtyCount++] = slot;
			continue;
		}
		memcpy(out + pos, entry, need);
		pos += need;
		e9k_debug_prof_entryEpoch[slot] = 0;
	}
	e9k_debug_prof_dirtyCount = newDirtyCount;

	if (pos + 2 >= cap) {
		return 0;
	}
	out[pos++] = ']';
	out[pos++] = '}';
	out[pos] = '\0';

	if (e9k_debug_prof_dirtyCount == 0) {
		e9k_debug_prof_epoch++;
		if (e9k_debug_prof_epoch == 0) {
			memset(e9k_debug_prof_entryEpoch, 0, sizeof(e9k_debug_prof_entryEpoch));
			e9k_debug_prof_epoch = 1;
		}
	}
	return pos;
}

E9K_DEBUG_EXPORT size_t
e9k_debug_text_read(char *out, size_t cap)
{
	if (!out || cap == 0 || e9k_debug_textCount == 0) {
		return 0;
	}
	size_t n = e9k_debug_textCount < cap ? e9k_debug_textCount : cap;
	for (size_t i = 0; i < n; ++i) {
		out[i] = e9k_debug_textBuf[e9k_debug_textTail];
		e9k_debug_textTail = (e9k_debug_textTail + 1) % E9K_DEBUG_TEXT_CAP;
	}
	e9k_debug_textCount -= n;
	return n;
}

E9K_DEBUG_EXPORT size_t
e9k_debug_neogeo_get_sprite_state(e9k_debug_sprite_state_t *out, size_t cap)
{
	(void)out;
	(void)cap;
	return 0;
}

E9K_DEBUG_EXPORT size_t
e9k_debug_get_sprite_state(e9k_debug_sprite_state_t *out, size_t cap)
{
	return e9k_debug_neogeo_get_sprite_state(out, cap);
}

E9K_DEBUG_EXPORT size_t
e9k_debug_neogeo_get_p1_rom(e9k_debug_rom_region_t *out, size_t cap)
{
	(void)out;
	(void)cap;
	return 0;
}

E9K_DEBUG_EXPORT size_t
e9k_debug_get_p1_rom(e9k_debug_rom_region_t *out, size_t cap)
{
	return e9k_debug_neogeo_get_p1_rom(out, cap);
}

E9K_DEBUG_EXPORT size_t
e9k_debug_read_checkpoints(e9k_debug_checkpoint_t *out, size_t cap)
{
	if (!out || cap == 0) {
		return 0;
	}
	size_t count = E9K_CHECKPOINT_COUNT;
	if (count > cap) {
		count = cap;
	}
	memcpy(out, e9k_debug_checkpoints, count * sizeof(out[0]));
	return count;
}

E9K_DEBUG_EXPORT void
e9k_debug_reset_checkpoints(void)
{
	memset(e9k_debug_checkpoints, 0, sizeof(e9k_debug_checkpoints));
}

E9K_DEBUG_EXPORT void
e9k_debug_set_checkpoint_enabled(int enabled)
{
	e9k_debug_checkpointEnabled = enabled ? 1 : 0;
}

E9K_DEBUG_EXPORT int
e9k_debug_get_checkpoint_enabled(void)
{
	return e9k_debug_checkpointEnabled;
}

E9K_DEBUG_EXPORT int *
e9k_debug_amiga_get_debug_dma_addr(void)
{
	return &debug_dma;
}

#if E9K_HACK_COPPER_DEBUG_EXPORT
E9K_DEBUG_EXPORT int *
e9k_debug_amiga_get_debug_copper_addr(void)
{
	return &debug_copper;
}
#endif
