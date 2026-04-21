 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Blitter emulation
  *
  * (c) 1995 Bernd Schmidt
  */

#ifndef UAE_BLITTER_H
#define UAE_BLITTER_H

#include <stddef.h>
#include <stdint.h>
#include "uae/types.h"

#ifndef E9K_HACK_MEMVIS
#define E9K_HACK_MEMVIS 0
#endif

struct bltinfo {
    int blitzero;
    uae_u16 bltadat, bltbdat, bltcdat, bltddat;
    uae_u16 bltaold, bltahold, bltbold, bltbhold, bltafwm, bltalwm;
    int vblitsize, hblitsize;
    int bltamod, bltbmod, bltcmod, bltdmod;
    int got_cycle;
    int nasty_cnt, wait_nasty;
    int blitter_nasty, blit_interrupt;
    // blitter is active and D may write to visible bitplane addresses
    int blitter_dangerous_bpl;
    int blit_main, blit_finald, blit_pending;
    int blit_queued;
    int finishhpos;
};

extern struct bltinfo blt_info;

extern void check_is_blit_dangerous(uaecptr *bplpt, int planes, int words);

extern uae_u16 bltsize;
extern uae_u16 bltcon0, bltcon1;
extern uae_u32 bltapt, bltbpt, bltcpt, bltdpt;
extern uae_u32 bltptx;
extern int bltptxpos, bltptxc;

extern void maybe_blit(int, int);
extern void reset_blit(int);
extern int blitnasty(void);
extern void blitter_handler(uae_u32);
extern void build_blitfilltable(void);
extern void do_blitter(int, int, uaecptr);
extern void decide_blitter(int hpos);
extern bool decide_blitter_maybe_write(int hpos, uaecptr addr, uae_u32 v);
extern void blitter_done_notify(int);
extern void blitter_slowdown(int, int, int, int);
extern void blitter_check_start(void);
extern void blitter_reset(void);
extern void blitter_setDestinationWriteEnabled(int enabled);
#if E9K_HACK_BLITTER_VIS
extern void blitter_setDebugWriteEnabled(int enabled);
extern int blitter_getDebugWriteEnabled(void);
extern void blitter_debugFrameTick(void);
extern void blitter_debugRetireCollectedWrites(void);
extern void blitter_debugRestoreWritesOlderThan(uint32_t frameAge);
extern void blitter_setDebugVisDecayFrames(uint32_t frameCount);
extern uint32_t blitter_getDebugVisDecayFrames(void);
extern void blitter_setDebugVisMode(int mode);
extern int blitter_getDebugVisMode(void);
extern uint32_t blitter_getDebugActiveCount(void);
extern uint32_t blitter_getDebugBlitsThisFrame(void);
extern uint32_t blitter_getDebugBlitsLastFrame(void);
extern uint32_t blitter_getDebugWritesThisFrame(void);
extern uint32_t blitter_getDebugWritesLastFrame(void);
extern uint32_t blitter_getDebugWriteBytesThisFrame(void);
extern uint32_t blitter_getDebugWriteBytesLastFrame(void);
extern uint32_t blitter_getDebugFrameCounter(void);
extern int blitter_getDebugBlitInfo(uint32_t blitId,
	uaecptr *sourceAddr,
	int *sourceIsCopper,
	uaecptr *sourceDataAddr,
	uaecptr *channelAAddr,
	uaecptr *channelBAddr,
	uaecptr *channelCAddr,
	uaecptr *channelDAddr,
	int16_t *channelAModulo,
	int16_t *channelBModulo,
	int16_t *channelCModulo,
	int16_t *channelDModulo,
	uint16_t *widthWords,
	uint16_t *heightLines,
	uint16_t *sourceRowBytes,
	int16_t *sourceModulo,
	uint8_t *sourceChannelsMask,
	uint8_t *minterm,
	int *sourceDescending,
	int *lineMode);
#if E9K_HACK_MEMVIS
extern void blitter_debugSnapshotFrame(void);
extern size_t blitter_debugReadSnapshotBlitIds(uaecptr addr, uint32_t *out, size_t wordCount);
#endif
extern int blitter_getDebugVideoOverrideValue(uaecptr addr, uae_u16 *value);
extern int blitter_getDebugVideoFetchInfo(uaecptr addr, uae_u16 *value, uint32_t *blitId, int *useOverride);
#endif
extern void blitter_chipmem_wput_indirect(uaecptr addr, uae_u32 w);
extern uae_u16 blitter_chipmem_wget_indirect(uaecptr addr);
extern void blitter_debugdump(void);
extern void restore_blitter_start(void);
extern void set_blitter_last(int);

typedef void blitter_func(uaecptr, uaecptr, uaecptr, uaecptr, struct bltinfo *);

#define BLITTER_MAX_WORDS 2048

extern blitter_func *const blitfunc_dofast[256];
extern blitter_func *const blitfunc_dofast_desc[256];
extern uae_u32 blit_masktable[BLITTER_MAX_WORDS];

#endif /* UAE_BLITTER_H */
