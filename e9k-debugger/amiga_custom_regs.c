/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "amiga_custom_regs.h"

typedef struct amiga_custom_regs_entry {
    const char *name;
    const char *description;
} amiga_custom_regs_entry_t;

#define AMIGA_CUSTOM_REGS_PREFIX_KEY(c0, c1, c2) \
    ((((uint32_t)(uint8_t)(c0)) << 16) | (((uint32_t)(uint8_t)(c1)) << 8) | ((uint32_t)(uint8_t)(c2)))

static const amiga_custom_regs_entry_t amiga_custom_regs_entries[256] = {
    [0x000 >> 1] = { "BLTDDAT", "Blitter destination early read (unusable)" },
    [0x002 >> 1] = { "DMACONR", "DMA control (and blitter status) read" },
    [0x004 >> 1] = { "VPOSR", "Read vertical raster position bit 9 (and interlace odd/even frame)" },
    [0x006 >> 1] = { "VHPOSR", "Rest of raster XY position - High byte: vertical, low byte: horizontal" },
    [0x008 >> 1] = { "DSKDATR", "Disk data early read (unusable)" },
    [0x00A >> 1] = { "JOY0DAT", "Joystick/mouse 0 data" },
    [0x00C >> 1] = { "JOY1DAT", "Joystick/mouse 1 data" },
    [0x00E >> 1] = { "CLXDAT", "Poll (read and clear) sprite collision state" },
    [0x010 >> 1] = { "ADKCONR", "Audio, disk control register read" },
    [0x012 >> 1] = { "POT0DAT", "Pot counter pair 0 data" },
    [0x014 >> 1] = { "POT1DAT", "Pot counter pair 1 data" },
    [0x016 >> 1] = { "POTGOR", "Pot pin data read" },
    [0x018 >> 1] = { "SERDATR", "Serial port data and status read" },
    [0x01A >> 1] = { "DSKBYTR", "Disk data byte and status read" },
    [0x01C >> 1] = { "INTENAR", "Interrupt enable bits read" },
    [0x01E >> 1] = { "INTREQR", "Interrupt request bits read" },
    [0x020 >> 1] = { "DSKPTH", "Disk track buffer pointer (high 5 bits)" },
    [0x022 >> 1] = { "DSKPTL", "Disk track buffer pointer (low 15 bits)" },
    [0x024 >> 1] = { "DSKLEN", "Disk track buffer length" },
    [0x026 >> 1] = { "DSKDAT", "Disk DMA data write" },
    [0x028 >> 1] = { "REFPTR", "AGA: Refresh pointer" },
    [0x02A >> 1] = { "VPOSW", "Write vert most sig. bits (and frame flop)" },
    [0x02C >> 1] = { "VHPOSW", "Write vert and horiz pos of beam" },
    [0x02E >> 1] = { "COPCON", "Coprocessor control register (CDANG)" },
    [0x030 >> 1] = { "SERDAT", "Serial port data and stop bits write" },
    [0x032 >> 1] = { "SERPER", "Serial port period and control" },
    [0x034 >> 1] = { "POTGO", "Pot count start, pot pin drive enable data" },
    [0x036 >> 1] = { "JOYTEST", "Write to all 4 joystick/mouse counters at once" },
    [0x038 >> 1] = { "STREQU", "Strobe for horiz sync with VBLANK and EQU" },
    [0x03A >> 1] = { "STRVBL", "Strobe for horiz sync with VBLANK" },
    [0x03C >> 1] = { "STRHOR", "Strobe for horiz sync" },
    [0x03E >> 1] = { "STRLONG", "Strobe for identification of long/short horiz line" },
    [0x040 >> 1] = { "BLTCON0", "Blitter control reg 0" },
    [0x042 >> 1] = { "BLTCON1", "Blitter control reg 1" },
    [0x044 >> 1] = { "BLTAFWM", "Blitter first word mask for source A" },
    [0x046 >> 1] = { "BLTALWM", "Blitter last word mask for source A" },
    [0x048 >> 1] = { "BLTCPTH", "Blitter pointer to source C (high 5 bits)" },
    [0x04A >> 1] = { "BLTCPTL", "Blitter pointer to source C (low 15 bits)" },
    [0x04C >> 1] = { "BLTBPTH", "Blitter pointer to source B (high 5 bits)" },
    [0x04E >> 1] = { "BLTBPTL", "Blitter pointer to source B (low 15 bits)" },
    [0x050 >> 1] = { "BLTAPTH", "Blitter pointer to source A (high 5 bits)" },
    [0x052 >> 1] = { "BLTAPTL", "Blitter pointer to source A (low 15 bits)" },
    [0x054 >> 1] = { "BLTDPTH", "Blitter pointer to destination D (high 5 bits)" },
    [0x056 >> 1] = { "BLTDPTL", "Blitter pointer to destination D (low 15 bits)" },
    [0x058 >> 1] = { "BLTSIZE", "Blitter start and size (win/width, height)" },
    [0x05A >> 1] = { "BLTCON0L", "Blitter control 0 lower 8 bits (minterms)" },
    [0x05C >> 1] = { "BLTSIZV", "Blitter V size (for 15 bit vert size)" },
    [0x05E >> 1] = { "BLTSIZH", "ECS: Blitter H size & start (for 11 bit H size)" },
    [0x060 >> 1] = { "BLTCMOD", "Blitter modulo for source C" },
    [0x062 >> 1] = { "BLTBMOD", "Blitter modulo for source B" },
    [0x064 >> 1] = { "BLTAMOD", "Blitter modulo for source A" },
    [0x066 >> 1] = { "BLTDMOD", "Blitter modulo for destination D" },
    [0x068 >> 1] = { "RESERVED", "Reserved" },
    [0x06A >> 1] = { "RESERVED", "Reserved" },
    [0x06C >> 1] = { "RESERVED", "Reserved" },
    [0x06E >> 1] = { "RESERVED", "Reserved" },
    [0x070 >> 1] = { "BLTCDAT", "Blitter source C data reg" },
    [0x072 >> 1] = { "BLTBDAT", "Blitter source B data reg" },
    [0x074 >> 1] = { "BLTADAT", "Blitter source A data reg" },
    [0x076 >> 1] = { "RESERVED", "Reserved" },
    [0x078 >> 1] = { "SPRHDAT", "AGA: Ext logic UHRES sprite pointer and data identifier" },
    [0x07A >> 1] = { "BPLHDAT", "AGA: Ext logic UHRES bit plane identifier" },
    [0x07C >> 1] = { "LISAID", "AGA: Chip revision level for Denise/Lisa" },
    [0x07E >> 1] = { "DSKSYNC", "Disk sync pattern" },
    [0x080 >> 1] = { "COP1LCH", "Write Copper pointer 1 (high 5 bits)" },
    [0x082 >> 1] = { "COP1LCL", "Write Copper pointer 1 (low 15 bits)" },
    [0x084 >> 1] = { "COP2LCH", "Write Copper pointer 2 (high 5 bits)" },
    [0x086 >> 1] = { "COP2LCL", "Write Copper pointer 2 (low 15 bits)" },
    [0x088 >> 1] = { "COPJMP1", "Trigger Copper 1 (any value)" },
    [0x08A >> 1] = { "COPJMP2", "Trigger Copper 2 (any value)" },
    [0x08C >> 1] = { "COPINS", "Coprocessor inst fetch identify" },
    [0x08E >> 1] = { "DIWSTRT", "Display window start (upper left vert-hor pos)" },
    [0x090 >> 1] = { "DIWSTOP", "Display window stop (lower right vert-hor pos)" },
    [0x092 >> 1] = { "DDFSTRT", "Display bitplane data fetch start.hor pos" },
    [0x094 >> 1] = { "DDFSTOP", "Display bitplane data fetch stop.hor pos" },
    [0x096 >> 1] = { "DMACON", "DMA control write (clear or set)" },
    [0x098 >> 1] = { "CLXCON", "Write Sprite collision control bits" },
    [0x09A >> 1] = { "INTENA", "Interrupt enable bits (clear or set bits)" },
    [0x09C >> 1] = { "INTREQ", "Interrupt request bits (clear or set bits)" },
    [0x09E >> 1] = { "ADKCON", "Audio, disk and UART control" },
    [0x0A0 >> 1] = { "AUD0LCH", "Audio channel 0 pointer (high 5 bits)" },
    [0x0A2 >> 1] = { "AUD0LCL", "Audio channel 0 pointer (low 15 bits)" },
    [0x0A4 >> 1] = { "AUD0LEN", "Audio channel 0 length" },
    [0x0A6 >> 1] = { "AUD0PER", "Audio channel 0 period" },
    [0x0A8 >> 1] = { "AUD0VOL", "Audio channel 0 volume" },
    [0x0AA >> 1] = { "AUD0DAT", "Audio channel 0 data" },
    [0x0AC >> 1] = { "RESERVED", "Reserved" },
    [0x0AE >> 1] = { "RESERVED", "Reserved" },
    [0x0B0 >> 1] = { "AUD1LCH", "Audio channel 1 pointer (high 5 bits)" },
    [0x0B2 >> 1] = { "AUD1LCL", "Audio channel 1 pointer (low 15 bits)" },
    [0x0B4 >> 1] = { "AUD1LEN", "Audio channel 1 length" },
    [0x0B6 >> 1] = { "AUD1PER", "Audio channel 1 period" },
    [0x0B8 >> 1] = { "AUD1VOL", "Audio channel 1 volume" },
    [0x0BA >> 1] = { "AUD1DAT", "Audio channel 1 data" },
    [0x0BC >> 1] = { "RESERVED", "Reserved" },
    [0x0BE >> 1] = { "RESERVED", "Reserved" },
    [0x0C0 >> 1] = { "AUD2LCH", "Audio channel 2 pointer (high 5 bits)" },
    [0x0C2 >> 1] = { "AUD2LCL", "Audio channel 2 pointer (low 15 bits)" },
    [0x0C4 >> 1] = { "AUD2LEN", "Audio channel 2 length" },
    [0x0C6 >> 1] = { "AUD2PER", "Audio channel 2 period" },
    [0x0C8 >> 1] = { "AUD2VOL", "Audio channel 2 volume" },
    [0x0CA >> 1] = { "AUD2DAT", "Audio channel 2 data" },
    [0x0CC >> 1] = { "RESERVED", "Reserved" },
    [0x0CE >> 1] = { "RESERVED", "Reserved" },
    [0x0D0 >> 1] = { "AUD3LCH", "Audio channel 3 pointer (high 5 bits)" },
    [0x0D2 >> 1] = { "AUD3LCL", "Audio channel 3 pointer (low 15 bits)" },
    [0x0D4 >> 1] = { "AUD3LEN", "Audio channel 3 length" },
    [0x0D6 >> 1] = { "AUD3PER", "Audio channel 3 period" },
    [0x0D8 >> 1] = { "AUD3VOL", "Audio channel 3 volume" },
    [0x0DA >> 1] = { "AUD3DAT", "Audio channel 3 data" },
    [0x0DC >> 1] = { "RESERVED", "Reserved" },
    [0x0DE >> 1] = { "RESERVED", "Reserved" },
    [0x0E0 >> 1] = { "BPL1PTH", "Bitplane pointer 1 (high 5 bits)" },
    [0x0E2 >> 1] = { "BPL1PTL", "Bitplane pointer 1 (low 15 bits)" },
    [0x0E4 >> 1] = { "BPL2PTH", "Bitplane pointer 2 (high 5 bits)" },
    [0x0E6 >> 1] = { "BPL2PTL", "Bitplane pointer 2 (low 15 bits)" },
    [0x0E8 >> 1] = { "BPL3PTH", "Bitplane pointer 3 (high 5 bits)" },
    [0x0EA >> 1] = { "BPL3PTL", "Bitplane pointer 3 (low 15 bits)" },
    [0x0EC >> 1] = { "BPL4PTH", "Bitplane pointer 4 (high 5 bits)" },
    [0x0EE >> 1] = { "BPL4PTL", "Bitplane pointer 4 (low 15 bits)" },
    [0x0F0 >> 1] = { "BPL5PTH", "Bitplane pointer 5 (high 5 bits)" },
    [0x0F2 >> 1] = { "BPL5PTL", "Bitplane pointer 5 (low 15 bits)" },
    [0x0F4 >> 1] = { "BPL6PTH", "Bitplane pointer 6 (high 5 bits)" },
    [0x0F6 >> 1] = { "BPL6PTL", "Bitplane pointer 6 (low 15 bits)" },
    [0x0F8 >> 1] = { "BPL7PTH", "AGA: Bitplane pointer 7 (high 5 bits)" },
    [0x0FA >> 1] = { "BPL7PTL", "AGA: Bitplane pointer 7 (low 15 bits)" },
    [0x0FC >> 1] = { "BPL8PTH", "AGA: Bitplane pointer 8 (high 5 bits)" },
    [0x0FE >> 1] = { "BPL8PTL", "AGA: Bitplane pointer 8 (low 15 bits)" },
    [0x100 >> 1] = { "BPLCON0", "Bitplane depth and screen mode)" },
    [0x102 >> 1] = { "BPLCON1", "Bitplane/playfield horizontal scroll values" },
    [0x104 >> 1] = { "BPLCON2", "Sprites vs. Playfields priority" },
    [0x106 >> 1] = { "BPLCON3", "AGA: Bitplane control reg (enhanced features)" },
    [0x108 >> 1] = { "BPL1MOD", "Bitplane modulo (odd planes)" },
    [0x10A >> 1] = { "BPL2MOD", "Bitplane modulo (even planes)" },
    [0x10C >> 1] = { "BPLCON4", "AGA: Bitplane control reg (bitplane & sprite masks)" },
    [0x10E >> 1] = { "CLXCON2", "AGA: Write Extended sprite collision control bits" },
    [0x110 >> 1] = { "BPL1DAT", "Bitplane 1 data (parallel to serial convert)" },
    [0x112 >> 1] = { "BPL2DAT", "Bitplane 2 data (parallel to serial convert)" },
    [0x114 >> 1] = { "BPL3DAT", "Bitplane 3 data (parallel to serial convert)" },
    [0x116 >> 1] = { "BPL4DAT", "Bitplane 4 data (parallel to serial convert)" },
    [0x118 >> 1] = { "BPL5DAT", "Bitplane 5 data (parallel to serial convert)" },
    [0x11A >> 1] = { "BPL6DAT", "Bitplane 6 data (parallel to serial convert)" },
    [0x11C >> 1] = { "BPL7DAT", "AGA: Bitplane 7 data (parallel to serial convert)" },
    [0x11E >> 1] = { "BPL8DAT", "AGA: Bitplane 8 data (parallel to serial convert)" },
    [0x120 >> 1] = { "SPR0PTH", "Sprite 0 pointer (high 5 bits)" },
    [0x122 >> 1] = { "SPR0PTL", "Sprite 0 pointer (low 15 bits)" },
    [0x124 >> 1] = { "SPR1PTH", "Sprite 1 pointer (high 5 bits)" },
    [0x126 >> 1] = { "SPR1PTL", "Sprite 1 pointer (low 15 bits)" },
    [0x128 >> 1] = { "SPR2PTH", "Sprite 2 pointer (high 5 bits)" },
    [0x12A >> 1] = { "SPR2PTL", "Sprite 2 pointer (low 15 bits)" },
    [0x12C >> 1] = { "SPR3PTH", "Sprite 3 pointer (high 5 bits)" },
    [0x12E >> 1] = { "SPR3PTL", "Sprite 3 pointer (low 15 bits)" },
    [0x130 >> 1] = { "SPR4PTH", "Sprite 4 pointer (high 5 bits)" },
    [0x132 >> 1] = { "SPR4PTL", "Sprite 4 pointer (low 15 bits)" },
    [0x134 >> 1] = { "SPR5PTH", "Sprite 5 pointer (high 5 bits)" },
    [0x136 >> 1] = { "SPR5PTL", "Sprite 5 pointer (low 15 bits)" },
    [0x138 >> 1] = { "SPR6PTH", "Sprite 6 pointer (high 5 bits)" },
    [0x13A >> 1] = { "SPR6PTL", "Sprite 6 pointer (low 15 bits)" },
    [0x13C >> 1] = { "SPR7PTH", "Sprite 7 pointer (high 5 bits)" },
    [0x13E >> 1] = { "SPR7PTL", "Sprite 7 pointer (low 15 bits)" },
    [0x140 >> 1] = { "SPR0POS", "Sprite 0 vert-horiz start pos data" },
    [0x142 >> 1] = { "SPR0CTL", "Sprite 0 position and control data" },
    [0x144 >> 1] = { "SPR0DATA", "Sprite 0 low bitplane data" },
    [0x146 >> 1] = { "SPR0DATB", "Sprite 0 high bitplane data" },
    [0x148 >> 1] = { "SPR1POS", "Sprite 1 vert-horiz start pos data" },
    [0x14A >> 1] = { "SPR1CTL", "Sprite 1 position and control data" },
    [0x14C >> 1] = { "SPR1DATA", "Sprite 1 low bitplane data" },
    [0x14E >> 1] = { "SPR1DATB", "Sprite 1 high bitplane data" },
    [0x150 >> 1] = { "SPR2POS", "Sprite 2 vert-horiz start pos data" },
    [0x152 >> 1] = { "SPR2CTL", "Sprite 2 position and control data" },
    [0x154 >> 1] = { "SPR2DATA", "Sprite 2 low bitplane data" },
    [0x156 >> 1] = { "SPR2DATB", "Sprite 2 high bitplane data" },
    [0x158 >> 1] = { "SPR3POS", "Sprite 3 vert-horiz start pos data" },
    [0x15A >> 1] = { "SPR3CTL", "Sprite 3 position and control data" },
    [0x15C >> 1] = { "SPR3DATA", "Sprite 3 low bitplane data" },
    [0x15E >> 1] = { "SPR3DATB", "Sprite 3 high bitplane data" },
    [0x160 >> 1] = { "SPR4POS", "Sprite 4 vert-horiz start pos data" },
    [0x162 >> 1] = { "SPR4CTL", "Sprite 4 position and control data" },
    [0x164 >> 1] = { "SPR4DATA", "Sprite 4 low bitplane data" },
    [0x166 >> 1] = { "SPR4DATB", "Sprite 4 high bitplane data" },
    [0x168 >> 1] = { "SPR5POS", "Sprite 5 vert-horiz start pos data" },
    [0x16A >> 1] = { "SPR5CTL", "Sprite 5 position and control data" },
    [0x16C >> 1] = { "SPR5DATA", "Sprite 5 low bitplane data" },
    [0x16E >> 1] = { "SPR5DATB", "Sprite 5 high bitplane data" },
    [0x170 >> 1] = { "SPR6POS", "Sprite 6 vert-horiz start pos data" },
    [0x172 >> 1] = { "SPR6CTL", "Sprite 6 position and control data" },
    [0x174 >> 1] = { "SPR6DATA", "Sprite 6 low bitplane data" },
    [0x176 >> 1] = { "SPR6DATB", "Sprite 6 high bitplane data" },
    [0x178 >> 1] = { "SPR7POS", "Sprite 7 vert-horiz start pos data" },
    [0x17A >> 1] = { "SPR7CTL", "Sprite 7 position and control data" },
    [0x17C >> 1] = { "SPR7DATA", "Sprite 7 low bitplane data" },
    [0x17E >> 1] = { "SPR7DATB", "Sprite 7 high bitplane data" },
    [0x180 >> 1] = { "COLOR00", "Palette color 00" },
    [0x182 >> 1] = { "COLOR01", "Palette color 1" },
    [0x184 >> 1] = { "COLOR02", "Palette color 2" },
    [0x186 >> 1] = { "COLOR03", "Palette color 3" },
    [0x188 >> 1] = { "COLOR04", "Palette color 4" },
    [0x18A >> 1] = { "COLOR05", "Palette color 5" },
    [0x18C >> 1] = { "COLOR06", "Palette color 6" },
    [0x18E >> 1] = { "COLOR07", "Palette color 7" },
    [0x190 >> 1] = { "COLOR08", "Palette color 8" },
    [0x192 >> 1] = { "COLOR09", "Palette color 9" },
    [0x194 >> 1] = { "COLOR10", "Palette color 10" },
    [0x196 >> 1] = { "COLOR11", "Palette color 11" },
    [0x198 >> 1] = { "COLOR12", "Palette color 12" },
    [0x19A >> 1] = { "COLOR13", "Palette color 13" },
    [0x19C >> 1] = { "COLOR14", "Palette color 14" },
    [0x19E >> 1] = { "COLOR15", "Palette color 15" },
    [0x1A0 >> 1] = { "COLOR16", "Palette color 16" },
    [0x1A2 >> 1] = { "COLOR17", "Palette color 17" },
    [0x1A4 >> 1] = { "COLOR18", "Palette color 18" },
    [0x1A6 >> 1] = { "COLOR19", "Palette color 19" },
    [0x1A8 >> 1] = { "COLOR20", "Palette color 20" },
    [0x1AA >> 1] = { "COLOR21", "Palette color 21" },
    [0x1AC >> 1] = { "COLOR22", "Palette color 22" },
    [0x1AE >> 1] = { "COLOR23", "Palette color 23" },
    [0x1B0 >> 1] = { "COLOR24", "Palette color 24" },
    [0x1B2 >> 1] = { "COLOR25", "Palette color 25" },
    [0x1B4 >> 1] = { "COLOR26", "Palette color 26" },
    [0x1B6 >> 1] = { "COLOR27", "Palette color 27" },
    [0x1B8 >> 1] = { "COLOR28", "Palette color 28" },
    [0x1BA >> 1] = { "COLOR29", "Palette color 29" },
    [0x1BC >> 1] = { "COLOR30", "Palette color 30" },
    [0x1BE >> 1] = { "COLOR31", "Palette color 31" },
    [0x1C0 >> 1] = { "HTOTAL", "AGA: Highest number count in horiz line (VARBEAMEN = 1)" },
    [0x1C2 >> 1] = { "HSSTOP", "AGA: Horiz line pos for HSYNC stop" },
    [0x1C4 >> 1] = { "HBSTRT", "AGA: Horiz line pos for HBLANK start" },
    [0x1C6 >> 1] = { "HBSTOP", "AGA: Horiz line pos for HBLANK stop" },
    [0x1C8 >> 1] = { "VTOTAL", "AGA: Highest numbered vertical line (VARBEAMEN = 1)" },
    [0x1CA >> 1] = { "VSSTOP", "AGA: Vert line for Vsync stop" },
    [0x1CC >> 1] = { "VBSTRT", "AGA: Vert line for VBLANK start" },
    [0x1CE >> 1] = { "VBSTOP", "AGA: Vert line for VBLANK stop" },
    [0x1D0 >> 1] = { "SPRHSTRT", "AGA: UHRES sprite vertical start" },
    [0x1D2 >> 1] = { "SPRHSTOP", "AGA: UHRES sprite vertical stop" },
    [0x1D4 >> 1] = { "BPLHSTRT", "AGA: UHRES bit plane vertical start" },
    [0x1D6 >> 1] = { "BPLHSTOP", "AGA: UHRES bit plane vertical stop" },
    [0x1D8 >> 1] = { "HHPOSW", "AGA: DUAL mode hires H beam counter write" },
    [0x1DA >> 1] = { "HHPOSR", "AGA: DUAL mode hires H beam counter read" },
    [0x1DC >> 1] = { "BEAMCON0", "Beam counter control register" },
    [0x1DE >> 1] = { "HSSTRT", "AGA: Horizontal sync start (VARHSY)" },
    [0x1E0 >> 1] = { "VSSTRT", "AGA: Vertical sync start (VARVSY)" },
    [0x1E2 >> 1] = { "HCENTER", "AGA: Horizontal pos for vsync on interlace" },
    [0x1E4 >> 1] = { "DIWHIGH", "AGA: Display window upper bits for start/stop" },
    [0x1E6 >> 1] = { "BPLHMOD", "AGA: UHRES bit plane modulo" },
    [0x1E8 >> 1] = { "SPRHPTH", "AGA: UHRES sprite pointer (high 5 bits)" },
    [0x1EA >> 1] = { "SPRHPTL", "AGA: UHRES sprite pointer (low 15 bits)" },
    [0x1EC >> 1] = { "BPLHPTH", "AGA: VRam (UHRES) bitplane pointer (high 5 bits)" },
    [0x1EE >> 1] = { "BPLHPTL", "AGA: VRam (UHRES) bitplane pointer (low 15 bits)" },
    [0x1F0 >> 1] = { "RESERVED", "Reserved" },
    [0x1F2 >> 1] = { "RESERVED", "Reserved" },
    [0x1F4 >> 1] = { "RESERVED", "Reserved" },
    [0x1F6 >> 1] = { "RESERVED", "Reserved" },
    [0x1F8 >> 1] = { "RESERVED", "Reserved" },
    [0x1FA >> 1] = { "RESERVED", "Reserved" },
    [0x1FC >> 1] = { "FMODE", "AGA: Write Fetch mode (0=OCS compatible)" },
    [0x1FE >> 1] = { "NO-OP", "No operation/NULL (Copper NOP instruction)" },
};

static uint16_t
amiga_custom_regs_normalizeOffset(uint16_t regOffset)
{
    return (uint16_t)(regOffset & 0x01feu);
}

static uint8_t
amiga_custom_regs_upperAscii(uint8_t value)
{
    if (value >= 'a' && value <= 'z') {
        return (uint8_t)(value - ('a' - 'A'));
    }
    return value;
}

static uint32_t
amiga_custom_regs_prefixKeyForName(const char *name)
{
    if (!name || !name[0] || !name[1] || !name[2]) {
        return 0u;
    }
    uint8_t c0 = amiga_custom_regs_upperAscii((uint8_t)name[0]);
    uint8_t c1 = amiga_custom_regs_upperAscii((uint8_t)name[1]);
    uint8_t c2 = amiga_custom_regs_upperAscii((uint8_t)name[2]);
    return AMIGA_CUSTOM_REGS_PREFIX_KEY(c0, c1, c2);
}

static uint32_t
amiga_custom_regs_colorForPrefix(uint32_t prefix)
{
    switch (prefix) {
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('B', 'L', 'T'):
        return 0xE08A4Eu;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('B', 'P', 'L'):
        return 0x61C98Eu;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('S', 'P', 'R'):
        return 0x78C9D6u;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('A', 'U', 'D'):
        return 0xAC9BE7u;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('C', 'O', 'P'):
        return 0xE4CC66u;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('D', 'S', 'K'):
        return 0x7DB3E6u;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('D', 'M', 'A'):
        return 0xD08964u;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('A', 'D', 'K'):
        return 0xD08CCAu;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('I', 'N', 'T'):
        return 0xE07070u;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('C', 'L', 'X'):
        return 0xCFA37Bu;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('D', 'I', 'W'):
        return 0x82D6B4u;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('D', 'D', 'F'):
        return 0x7EC1B2u;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('V', 'P', 'O'):
        return 0xDEA06Bu;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('V', 'H', 'P'):
        return 0xD89175u;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('H', 'H', 'P'):
        return 0xC67A66u;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('H', 'T', 'O'):
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('H', 'S', 'S'):
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('H', 'B', 'S'):
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('H', 'C', 'E'):
        return 0xCC9F77u;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('V', 'T', 'O'):
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('V', 'S', 'S'):
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('V', 'B', 'S'):
        return 0xC98B8Bu;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('P', 'O', 'T'):
        return 0x72C99Cu;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('J', 'O', 'Y'):
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('J', 'O', 'T'):
        return 0x68CFAFu;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('S', 'E', 'R'):
        return 0xA57BE0u;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('S', 'T', 'R'):
        return 0xC4AA72u;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('C', 'O', 'L'):
        return 0xDB6FA4u;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('B', 'E', 'A'):
        return 0xC0A87Au;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('R', 'E', 'F'):
        return 0x71B6D0u;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('L', 'I', 'S'):
        return 0xA9B8D6u;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('F', 'M', 'O'):
        return 0x92A6E0u;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('N', 'O', '-'):
        return 0xB7B7BFu;
    case AMIGA_CUSTOM_REGS_PREFIX_KEY('R', 'E', 'S'):
        return 0x8F8F96u;
    default:
        break;
    }
    return 0xC8C8D0u;
}

const char *
amiga_custom_regs_nameForOffset(uint16_t regOffset)
{
    uint16_t normalized = amiga_custom_regs_normalizeOffset(regOffset);
    const amiga_custom_regs_entry_t *entry = &amiga_custom_regs_entries[normalized >> 1];
    if (entry->name) {
        return entry->name;
    }
    return "UNKNOWN";
}

const char *
amiga_custom_regs_descriptionForOffset(uint16_t regOffset)
{
    uint16_t normalized = amiga_custom_regs_normalizeOffset(regOffset);
    const amiga_custom_regs_entry_t *entry = &amiga_custom_regs_entries[normalized >> 1];
    if (entry->description) {
        return entry->description;
    }
    return "Unknown custom register";
}

uint32_t
amiga_custom_regs_addressFromOffset(uint16_t regOffset)
{
    uint16_t normalized = amiga_custom_regs_normalizeOffset(regOffset);
    return 0x00DFF000u + (uint32_t)normalized;
}

uint32_t
amiga_custom_regs_colorForOffset(uint16_t regOffset)
{
    uint16_t normalized = amiga_custom_regs_normalizeOffset(regOffset);
    const amiga_custom_regs_entry_t *entry = &amiga_custom_regs_entries[normalized >> 1];
    uint32_t prefix = amiga_custom_regs_prefixKeyForName(entry->name);
    return amiga_custom_regs_colorForPrefix(prefix);
}
