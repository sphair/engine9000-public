/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef E9K_HACK_AMI_SPRITE_VIS
#define E9K_HACK_AMI_SPRITE_VIS 0
#endif

typedef enum e9k_debug_option
{
    e9k_debug_option_none = 0,
    E9K_DEBUG_OPTION_AMIGA_BLITTER = 1,
    E9K_DEBUG_OPTION_AMIGA_SPRITE0 = 2,
    E9K_DEBUG_OPTION_AMIGA_SPRITE1 = 3,
    E9K_DEBUG_OPTION_AMIGA_SPRITE2 = 4,
    E9K_DEBUG_OPTION_AMIGA_SPRITE3 = 5,
    E9K_DEBUG_OPTION_AMIGA_SPRITE4 = 6,
    E9K_DEBUG_OPTION_AMIGA_SPRITE5 = 7,
    E9K_DEBUG_OPTION_AMIGA_SPRITE6 = 8,
    E9K_DEBUG_OPTION_AMIGA_SPRITE7 = 9,
    E9K_DEBUG_OPTION_AMIGA_BITPLANE0 = 10,
    E9K_DEBUG_OPTION_AMIGA_BITPLANE1 = 11,
    E9K_DEBUG_OPTION_AMIGA_BITPLANE2 = 12,
    E9K_DEBUG_OPTION_AMIGA_BITPLANE3 = 13,
    E9K_DEBUG_OPTION_AMIGA_BITPLANE4 = 14,
    E9K_DEBUG_OPTION_AMIGA_BITPLANE5 = 15,
    E9K_DEBUG_OPTION_AMIGA_BITPLANE6 = 16,
    E9K_DEBUG_OPTION_AMIGA_BITPLANE7 = 17,
    E9K_DEBUG_OPTION_AMIGA_AUDIO0 = 18,
    E9K_DEBUG_OPTION_AMIGA_AUDIO1 = 19,
    E9K_DEBUG_OPTION_AMIGA_AUDIO2 = 20,
    E9K_DEBUG_OPTION_AMIGA_AUDIO3 = 21,
    E9K_DEBUG_OPTION_AMIGA_BPLCON1_DELAY_SCROLL = 22,
    E9K_DEBUG_OPTION_AMIGA_BLITTER_VIS_DECAY = 23,
    E9K_DEBUG_OPTION_AMIGA_BLITTER_VIS_MODE = 24,
    E9K_DEBUG_OPTION_AMIGA_BLITTER_VIS_BLINK = 25,
    E9K_DEBUG_OPTION_AMIGA_COPPER_LINE_LIMIT_ENABLED = 26,
    E9K_DEBUG_OPTION_AMIGA_COPPER_LINE_LIMIT_START = 27,
    E9K_DEBUG_OPTION_AMIGA_COPPER_LINE_LIMIT_END = 28,
    E9K_DEBUG_OPTION_AMIGA_BPLPTR_BLOCK_ALL = 29,
    E9K_DEBUG_OPTION_AMIGA_BPLPTR1_BLOCK = 30,
    E9K_DEBUG_OPTION_AMIGA_BPLPTR2_BLOCK = 31,
    E9K_DEBUG_OPTION_AMIGA_BPLPTR3_BLOCK = 32,
    E9K_DEBUG_OPTION_AMIGA_BPLPTR4_BLOCK = 33,
    E9K_DEBUG_OPTION_AMIGA_BPLPTR5_BLOCK = 34,
    E9K_DEBUG_OPTION_AMIGA_BPLPTR6_BLOCK = 35,
    E9K_DEBUG_OPTION_AMIGA_BPLPTR_LINE_LIMIT_START = 36,
    E9K_DEBUG_OPTION_AMIGA_BPLPTR_LINE_LIMIT_END = 37,
    E9K_DEBUG_OPTION_AMIGA_CUSTOM_LOGGER = 38
} e9k_debug_option_t;

#define E9K_CHECKPOINT_COUNT 64

typedef struct e9k_debug_checkpoint {
    uint64_t current;
    uint64_t accumulator;
    uint64_t count;
    uint64_t average;
    uint64_t minimum;
    uint64_t maximum;
} e9k_debug_checkpoint_t;

typedef struct e9k_debug_ami_blitter_vis_point {
    uint16_t x;
    uint16_t y;
    uint32_t blitId;
} e9k_debug_ami_blitter_vis_point_t;

typedef struct e9k_debug_ami_blitter_vis_stats {
    uint32_t enabled;
    uint32_t mode;
    uint32_t blink;
    uint32_t livePhase;
    uint32_t activeCount;
    uint32_t blitsThisFrame;
    uint32_t writesThisFrame;
    uint32_t writeBytesThisFrame;
    uint32_t writeBytesMaxEstimateFrame;
    uint32_t frameCounter;
    uint32_t fetchQueriesThisFrame;
    uint32_t fetchHitsThisFrame;
    uint32_t drawMarkCallsFrame;
    uint32_t drawMarkCallsSnapshot;
} e9k_debug_ami_blitter_vis_stats_t;

#if E9K_HACK_AMI_SPRITE_VIS
#define E9K_DEBUG_AMI_SPRITE_VIS_FLAG_ATTACHED 0x00000001u

typedef struct e9k_debug_ami_sprite_vis_point {
    uint16_t x;
    uint16_t y;
    uint32_t spriteIndex;
    uint32_t flags;
} e9k_debug_ami_sprite_vis_point_t;
#endif

#define E9K_DEBUG_AMI_DMA_DEBUG_FRAME_LATEST_COMPLETE 0u
#define E9K_DEBUG_AMI_DMA_DEBUG_FRAME_ACTIVE 1u
#define E9K_DEBUG_AMI_DMA_DEBUG_FRAME_INFO_VERSION 1u
#define E9K_DEBUG_AMI_COPPER_DEBUG_FRAME_LATEST_COMPLETE 0u
#define E9K_DEBUG_AMI_COPPER_DEBUG_FRAME_ACTIVE 1u
#define E9K_DEBUG_AMI_COPPER_DEBUG_FRAME_INFO_VERSION 1u
#define E9K_DEBUG_AMI_DMA_EVENT2_COPPERUSE 0x00000004u
#define E9K_DEBUG_AMI_DMA_DEBUG_MODE_VIDEO_SYNC 7u
#define E9K_DEBUG_AMI_DMA_DEBUG_MODE_COLLECT_ONLY 6u

typedef struct e9k_debug_ami_dma_debug_frame_info {
    uint32_t version;
    uint32_t frameSelect;
    int32_t frameNumber;
    int32_t recordToggle;
    int32_t hposCount;
    int32_t vposCount;
    int32_t dmaHoffset;
    uint32_t recordCount;
    uint32_t debugDmaEnabled;
} e9k_debug_ami_dma_debug_frame_info_t;

typedef struct e9k_debug_ami_dma_debug_raw_record {
    int hpos;
    int vpos;
    int dhpos;
    int dhpos_abs;
    uint16_t reg;
    uint64_t dat;
    uint16_t size;
    uint32_t addr;
    uint32_t evt;
    uint32_t evt2;
    uint32_t evtdata;
    bool evtdataset;
    int16_t type;
    uint16_t extra;
    int8_t intlev;
    int8_t ipl;
    int8_t ipl2;
    uint16_t cf_reg;
    uint16_t cf_dat;
    uint16_t cf_addr;
    int ciareg;
    int ciamask;
    bool ciarw;
    int ciaphase;
    uint16_t ciavalue;
    bool end;
} e9k_debug_ami_dma_debug_raw_record_t;

typedef struct e9k_debug_ami_dma_debug_frame_view {
    const e9k_debug_ami_dma_debug_raw_record_t *records;
    e9k_debug_ami_dma_debug_frame_info_t info;
    int32_t visibleWidth;
    int32_t visibleHeight;
    int32_t renderWidth;
    int32_t renderHeight;
    int32_t visibleOffsetX;
    int32_t visibleOffsetY;
    int32_t dhposWrap;
    int32_t dhposScale;
} e9k_debug_ami_dma_debug_frame_view_t;

typedef struct e9k_debug_ami_copper_debug_frame_info {
    uint32_t version;
    uint32_t frameSelect;
    int32_t frameNumber;
    int32_t recordToggle;
    uint32_t recordCount;
    uint32_t debugCopperEnabled;
} e9k_debug_ami_copper_debug_frame_info_t;

typedef struct e9k_debug_ami_copper_debug_raw_record {
    uint16_t w1;
    uint16_t w2;
    int32_t hpos;
    int32_t vpos;
    int32_t bhpos;
    int32_t bvpos;
    uint32_t addr;
    uint32_t nextaddr;
} e9k_debug_ami_copper_debug_raw_record_t;

typedef struct e9k_debug_ami_copper_debug_frame_view {
    const e9k_debug_ami_copper_debug_raw_record_t *records;
    e9k_debug_ami_copper_debug_frame_info_t info;
} e9k_debug_ami_copper_debug_frame_view_t;

typedef struct e9k_debug_ami_custom_log_entry {
    uint16_t vpos;
    uint16_t hpos;
    uint16_t reg;
    uint16_t value;
    uint32_t sourceAddr;
    uint8_t sourceIsCopper;
    uint8_t reserved[3];
} e9k_debug_ami_custom_log_entry_t;

typedef void (*e9k_debug_ami_custom_log_frame_callback_t)(const e9k_debug_ami_custom_log_entry_t *entries,
                                                          size_t count,
                                                          uint32_t dropped,
                                                          uint64_t frameNo,
                                                          void *user);

typedef struct e9k_debug_ami_custom_reg_state {
    uint16_t value;
    uint32_t pc;
} e9k_debug_ami_custom_reg_state_t;


#define E9K_WATCHPOINT_COUNT 64

// Watchpoint operation bits.
// These can be combined; operands are stored separately per watchpoint.
#define E9K_WATCH_OP_READ                 (1u << 0) // (1) Read
#define E9K_WATCH_OP_WRITE                (1u << 1) // (2) Write
#define E9K_WATCH_OP_VALUE_NEQ_OLD        (1u << 2) // (3) Value != existing value (write-only)
#define E9K_WATCH_OP_VALUE_EQ             (1u << 3) // (4) Value == operand
#define E9K_WATCH_OP_OLD_VALUE_EQ         (1u << 4) // (5) Existing value == operand
#define E9K_WATCH_OP_ACCESS_SIZE          (1u << 5) // (6) Access size (operand: 8/16/32 bits)
#define E9K_WATCH_OP_ADDR_COMPARE_MASK    (1u << 6) // (7) Address compare mask (operand: mask)

// Access kind for watchbreak reporting.
#define E9K_WATCH_ACCESS_READ             1u
#define E9K_WATCH_ACCESS_WRITE            2u

typedef struct e9k_debug_watchpoint
{
    uint32_t addr;
    uint32_t op_mask;
    uint32_t diff_operand;      // (3) operand value
    uint32_t value_operand;     // (4) operand value
    uint32_t old_value_operand; // (5) operand value
    uint32_t size_operand;      // (6) operand size, 8/16/32 (bits)
    uint32_t addr_mask_operand; // (7) operand mask, 0 => always match
} e9k_debug_watchpoint_t;

typedef struct e9k_debug_watchbreak
{
    uint32_t index;             // 0..E9K_WATCHPOINT_COUNT-1

    // Snapshot of the triggering watchpoint.
    uint32_t watch_addr;
    uint32_t op_mask;
    uint32_t diff_operand;
    uint32_t value_operand;
    uint32_t old_value_operand;
    uint32_t size_operand;      // 8/16/32 (bits)
    uint32_t addr_mask_operand;

    // Access details.
    uint32_t access_addr;       // address used for the access (base)
    uint32_t access_kind;       // E9K_WATCH_ACCESS_*
    uint32_t access_size;       // 8/16/32 (bits)
    uint32_t value;             // value read/written (size-truncated)
    uint32_t old_value;         // existing value (if known; for reads, equals value)
    uint32_t old_value_valid;   // 1 if old_value is valid
} e9k_debug_watchbreak_t;


#define E9K_PROTECT_COUNT 64
#define E9K_PROTECT_MODE_BLOCK 0u
#define E9K_PROTECT_MODE_SET   1u

typedef struct e9k_debug_protect
{
    uint32_t addr;
    uint32_t addrMask;
    uint32_t sizeBits; // protected region size: 8/16/32 (bits)
    uint32_t mode;     // E9K_PROTECT_MODE_*
    uint32_t value;    // set value (masked to sizeBits), ignored for BLOCK
} e9k_debug_protect_t;
