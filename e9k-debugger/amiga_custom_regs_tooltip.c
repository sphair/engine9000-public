/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "amiga_custom_regs.h"

#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Bitfield decode text sourced from:
 * e9k-debugger/elowar/amigadev.elowar.com/read/ADCD_2.1/Hardware_Manual_guide/
 */
typedef struct amiga_custom_regs_tooltip_builder {
    char text[1024];
    size_t len;
} amiga_custom_regs_tooltip_builder_t;

static void
amiga_custom_regs_tooltip_reset(amiga_custom_regs_tooltip_builder_t *builder)
{
    if (!builder) {
        return;
    }
    builder->text[0] = '\0';
    builder->len = 0;
}

static void
amiga_custom_regs_tooltip_appendLineV(amiga_custom_regs_tooltip_builder_t *builder,
                                      const char *fmt,
                                      va_list ap)
{
    if (!builder || !fmt || builder->len >= sizeof(builder->text) - 1u) {
        return;
    }

    if (builder->len > 0u) {
        builder->text[builder->len++] = '\n';
        builder->text[builder->len] = '\0';
    }

    size_t remain = sizeof(builder->text) - builder->len;
    int written = vsnprintf(builder->text + builder->len, remain, fmt, ap);
    if (written < 0) {
        builder->text[builder->len] = '\0';
        return;
    }
    if ((size_t)written >= remain) {
        builder->len = sizeof(builder->text) - 1u;
        builder->text[builder->len] = '\0';
        return;
    }
    builder->len += (size_t)written;
}

static void
amiga_custom_regs_tooltip_appendLine(amiga_custom_regs_tooltip_builder_t *builder,
                                     const char *fmt,
                                     ...)
{
    va_list ap;
    va_start(ap, fmt);
    amiga_custom_regs_tooltip_appendLineV(builder, fmt, ap);
    va_end(ap);
}

static int
amiga_custom_regs_tooltip_isNamed(const char *name, const char *expect)
{
    if (!name || !expect) {
        return 0;
    }
    return strcmp(name, expect) == 0;
}

static int
amiga_custom_regs_tooltip_endsWith(const char *text, const char *suffix)
{
    if (!text || !suffix) {
        return 0;
    }
    size_t tLen = strlen(text);
    size_t sLen = strlen(suffix);
    if (tLen < sLen) {
        return 0;
    }
    return strcmp(text + (tLen - sLen), suffix) == 0;
}

static int
amiga_custom_regs_tooltip_startsWith(const char *text, const char *prefix)
{
    if (!text || !prefix) {
        return 0;
    }
    size_t pLen = strlen(prefix);
    return strncmp(text, prefix, pLen) == 0;
}

static int
amiga_custom_regs_tooltip_parseSingleIndex(const char *name,
                                           const char *prefix,
                                           const char *suffix,
                                           int *indexOut)
{
    if (!name || !prefix || !suffix) {
        return 0;
    }
    size_t pLen = strlen(prefix);
    size_t sLen = strlen(suffix);
    size_t nLen = strlen(name);
    if (nLen != pLen + 1u + sLen) {
        return 0;
    }
    if (strncmp(name, prefix, pLen) != 0) {
        return 0;
    }
    if (strcmp(name + pLen + 1u, suffix) != 0) {
        return 0;
    }
    if (!isdigit((unsigned char)name[pLen])) {
        return 0;
    }
    if (indexOut) {
        *indexOut = name[pLen] - '0';
    }
    return 1;
}

static int
amiga_custom_regs_tooltip_parseColorIndex(const char *name, int *indexOut)
{
    if (!name) {
        return 0;
    }
    if (strlen(name) != 7u || strncmp(name, "COLOR", 5u) != 0) {
        return 0;
    }
    if (!isdigit((unsigned char)name[5]) || !isdigit((unsigned char)name[6])) {
        return 0;
    }
    if (indexOut) {
        *indexOut = (name[5] - '0') * 10 + (name[6] - '0');
    }
    return 1;
}

static void
amiga_custom_regs_tooltip_decodePointerHigh(const char *name,
                                            uint16_t value,
                                            amiga_custom_regs_tooltip_builder_t *builder)
{
    amiga_custom_regs_tooltip_appendLine(builder, "%s high word=$%04x", name, (unsigned)value);
    amiga_custom_regs_tooltip_appendLine(builder, "High address bits for chip pointer.");
    amiga_custom_regs_tooltip_appendLine(builder, "ECS may use 5 high bits; OCS uses 3.");
}

static void
amiga_custom_regs_tooltip_decodePointerLow(const char *name,
                                           uint16_t value,
                                           amiga_custom_regs_tooltip_builder_t *builder)
{
    uint16_t masked = (uint16_t)(value & 0xfffeu);
    amiga_custom_regs_tooltip_appendLine(builder, "%s low word=$%04x", name, (unsigned)masked);
    amiga_custom_regs_tooltip_appendLine(builder, "Bit0 is forced 0 for even chip address.");
}

static void
amiga_custom_regs_tooltip_decodeSignedModulo(const char *name,
                                             uint16_t value,
                                             amiga_custom_regs_tooltip_builder_t *builder)
{
    int16_t signedValue = (int16_t)(value & 0xfffeu);
    amiga_custom_regs_tooltip_appendLine(builder, "%s modulo=%d ($%04x)",
                                         name,
                                         (int)signedValue,
                                         (unsigned)(value & 0xfffeu));
    amiga_custom_regs_tooltip_appendLine(builder, "Added at end of each fetched line.");
}

static void
amiga_custom_regs_tooltip_decodeColor(const char *name,
                                      uint16_t value,
                                      amiga_custom_regs_tooltip_builder_t *builder)
{
    unsigned r = (unsigned)((value >> 8) & 0x0fu);
    unsigned g = (unsigned)((value >> 4) & 0x0fu);
    unsigned b = (unsigned)(value & 0x0fu);
    unsigned r8 = r * 17u;
    unsigned g8 = g * 17u;
    unsigned b8 = b * 17u;
    amiga_custom_regs_tooltip_appendLine(builder, "%s RGB4: r=%u g=%u b=%u", name, r, g, b);
    amiga_custom_regs_tooltip_appendLine(builder, "Approx RGB8: #%02x%02x%02x", r8, g8, b8);
    if ((value & 0x8000u) != 0u) {
        amiga_custom_regs_tooltip_appendLine(builder, "Bit15 set: genlock/control extension.");
    }
}

static void
amiga_custom_regs_tooltip_decodeDmacon(const char *name,
                                       uint16_t value,
                                       amiga_custom_regs_tooltip_builder_t *builder)
{
    int setclr = (value >> 15) & 1;
    amiga_custom_regs_tooltip_appendLine(builder, "%s=$%04x", name, (unsigned)value);
    amiga_custom_regs_tooltip_appendLine(builder, "SETCLR=%d (%s)", setclr, setclr ? "set bits" : "clear bits");
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "DMAEN=%d BLTPRI=%d BPL=%d COP=%d BLT=%d",
                                         (value >> 9) & 1,
                                         (value >> 10) & 1,
                                         (value >> 8) & 1,
                                         (value >> 7) & 1,
                                         (value >> 6) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "SPR=%d DSK=%d AUD3..0=%d%d%d%d",
                                         (value >> 5) & 1,
                                         (value >> 4) & 1,
                                         (value >> 3) & 1,
                                         (value >> 2) & 1,
                                         (value >> 1) & 1,
                                         value & 1);
    if (amiga_custom_regs_tooltip_endsWith(name, "R")) {
        amiga_custom_regs_tooltip_appendLine(builder,
                                             "Status: BBUSY=%d BZERO=%d",
                                             (value >> 14) & 1,
                                             (value >> 13) & 1);
    }
}

static void
amiga_custom_regs_tooltip_decodeInt(const char *name,
                                    uint16_t value,
                                    amiga_custom_regs_tooltip_builder_t *builder)
{
    int setclr = (value >> 15) & 1;
    amiga_custom_regs_tooltip_appendLine(builder, "%s=$%04x", name, (unsigned)value);
    amiga_custom_regs_tooltip_appendLine(builder, "SETCLR=%d INTEN=%d", setclr, (value >> 14) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "EXTER=%d DSKSYN=%d RBF=%d",
                                         (value >> 13) & 1,
                                         (value >> 12) & 1,
                                         (value >> 11) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "AUD3..0=%d%d%d%d BLIT=%d",
                                         (value >> 10) & 1,
                                         (value >> 9) & 1,
                                         (value >> 8) & 1,
                                         (value >> 7) & 1,
                                         (value >> 6) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "VERTB=%d COPER=%d PORTS=%d",
                                         (value >> 5) & 1,
                                         (value >> 4) & 1,
                                         (value >> 3) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "SOFT=%d DSKBLK=%d TBE=%d",
                                         (value >> 2) & 1,
                                         (value >> 1) & 1,
                                         value & 1);
}

static void
amiga_custom_regs_tooltip_decodeAdkcon(const char *name,
                                       uint16_t value,
                                       amiga_custom_regs_tooltip_builder_t *builder)
{
    static const int precompNs[4] = { 0, 140, 280, 560 };
    int setclr = (value >> 15) & 1;
    int precompCode = (value >> 13) & 0x3;
    amiga_custom_regs_tooltip_appendLine(builder, "%s=$%04x", name, (unsigned)value);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "SETCLR=%d PRECOMP=%dns",
                                         setclr,
                                         precompNs[precompCode]);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "MFM=%d UARTBRK=%d WORDSYNC=%d",
                                         (value >> 12) & 1,
                                         (value >> 11) & 1,
                                         (value >> 10) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "MSBSYNC=%d FAST=%d",
                                         (value >> 9) & 1,
                                         (value >> 8) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "Period mod: %d%d%d%d",
                                         (value >> 7) & 1,
                                         (value >> 6) & 1,
                                         (value >> 5) & 1,
                                         (value >> 4) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "Volume mod: %d%d%d%d",
                                         (value >> 3) & 1,
                                         (value >> 2) & 1,
                                         (value >> 1) & 1,
                                         value & 1);
}

static void
amiga_custom_regs_tooltip_decodeDsklen(uint16_t value,
                                       amiga_custom_regs_tooltip_builder_t *builder)
{
    amiga_custom_regs_tooltip_appendLine(builder, "DSKLEN=$%04x", (unsigned)value);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "DMAEN=%d WRITE=%d LEN=%u words",
                                         (value >> 15) & 1,
                                         (value >> 14) & 1,
                                         (unsigned)(value & 0x3fffu));
}

static void
amiga_custom_regs_tooltip_decodeDskbytr(uint16_t value,
                                        amiga_custom_regs_tooltip_builder_t *builder)
{
    amiga_custom_regs_tooltip_appendLine(builder, "DSKBYTR=$%04x", (unsigned)value);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "DSKBYT=%d DMAON=%d WRITE=%d",
                                         (value >> 15) & 1,
                                         (value >> 14) & 1,
                                         (value >> 13) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "WORDEQUAL=%d DATA=$%02x",
                                         (value >> 12) & 1,
                                         (unsigned)(value & 0x00ffu));
}

static void
amiga_custom_regs_tooltip_decodeBplcon0(uint16_t value,
                                        amiga_custom_regs_tooltip_builder_t *builder)
{
    int bpu = (value >> 12) & 0x7;
    int dbpf = (value >> 10) & 1;
    int ham = (value >> 11) & 1;
    const char *res = "LORES";
    if (((value >> 6) & 1) != 0) {
        res = "SHRES";
    } else if (((value >> 15) & 1) != 0) {
        res = "HIRES";
    }
    const char *mode = "Indexed";
    if (ham) {
        mode = "HAM";
    } else if (bpu == 6 && !dbpf) {
        mode = "EHB";
    }

    amiga_custom_regs_tooltip_appendLine(builder,
                                         "BPLCON0=$%04x RES=%s BPU=%d",
                                         (unsigned)value,
                                         res,
                                         bpu);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "Mode=%s DBLPF=%d COLOR=%d GAUD=%d",
                                         mode,
                                         dbpf,
                                         (value >> 9) & 1,
                                         (value >> 8) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "LPEN=%d LACE=%d ERSY=%d",
                                         (value >> 3) & 1,
                                         (value >> 2) & 1,
                                         (value >> 1) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "ECS bits: SHRES=%d BPLHWRM=%d SPRHWRM=%d",
                                         (value >> 6) & 1,
                                         (value >> 5) & 1,
                                         (value >> 4) & 1);
}

static void
amiga_custom_regs_tooltip_decodeBplcon1(uint16_t value,
                                        amiga_custom_regs_tooltip_builder_t *builder)
{
    unsigned pf1h = (unsigned)(value & 0x0fu);
    unsigned pf2h = (unsigned)((value >> 4) & 0x0fu);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "BPLCON1=$%04x PF1H=%u PF2H=%u",
                                         (unsigned)value,
                                         pf1h,
                                         pf2h);
    if ((value & 0xff00u) != 0u) {
        amiga_custom_regs_tooltip_appendLine(builder,
                                             "Upper bits set ($%02x): chip extension.",
                                             (unsigned)((value >> 8) & 0xffu));
    }
}

static void
amiga_custom_regs_tooltip_decodeBplcon2(uint16_t value,
                                        amiga_custom_regs_tooltip_builder_t *builder)
{
    unsigned pf1p = (unsigned)(value & 0x7u);
    unsigned pf2p = (unsigned)((value >> 3) & 0x7u);
    unsigned zdbpsel = (unsigned)((value >> 12) & 0x7u);

    amiga_custom_regs_tooltip_appendLine(builder,
                                         "BPLCON2=$%04x PF1P=%u PF2P=%u",
                                         (unsigned)value,
                                         pf1p,
                                         pf2p);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "PF2PRI=%d KILLEHB=%d",
                                         (value >> 6) & 1,
                                         (value >> 9) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "Genlock: ZDBPEN=%d ZDCTEN=%d ZDBPSEL=%u",
                                         (value >> 11) & 1,
                                         (value >> 10) & 1,
                                         zdbpsel);
}

static void
amiga_custom_regs_tooltip_decodeBplcon3(uint16_t value,
                                        amiga_custom_regs_tooltip_builder_t *builder)
{
    amiga_custom_regs_tooltip_appendLine(builder, "BPLCON3=$%04x", (unsigned)value);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "ENBPLCN3=%d BRDRBLNK=%d BRDNTRAN=%d",
                                         value & 1,
                                         (value >> 5) & 1,
                                         (value >> 4) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "Raw bits for chip-specific enhancements.");
}

static void
amiga_custom_regs_tooltip_decodeBplcon4(uint16_t value,
                                        amiga_custom_regs_tooltip_builder_t *builder)
{
    amiga_custom_regs_tooltip_appendLine(builder, "BPLCON4=$%04x", (unsigned)value);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "AGA bitplane/sprite mask control.");
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "Use raw value for chip-specific decode.");
}

static void
amiga_custom_regs_tooltip_decodeClxcon(uint16_t value,
                                       amiga_custom_regs_tooltip_builder_t *builder)
{
    unsigned enBp = (unsigned)((value >> 6) & 0x3fu);
    unsigned mvBp = (unsigned)(value & 0x3fu);
    amiga_custom_regs_tooltip_appendLine(builder, "CLXCON=$%04x", (unsigned)value);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "ENSP: 1|0=%d 3|2=%d 5|4=%d 7|6=%d",
                                         (value >> 12) & 1,
                                         (value >> 13) & 1,
                                         (value >> 14) & 1,
                                         (value >> 15) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "ENBP mask=%02x MVBP mask=%02x",
                                         enBp,
                                         mvBp);
}

static void
amiga_custom_regs_tooltip_decodeClxcon2(uint16_t value,
                                        amiga_custom_regs_tooltip_builder_t *builder)
{
    amiga_custom_regs_tooltip_appendLine(builder, "CLXCON2=$%04x", (unsigned)value);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "AGA extended collision control bits.");
}

static void
amiga_custom_regs_tooltip_decodeBeamcon0(uint16_t value,
                                         amiga_custom_regs_tooltip_builder_t *builder)
{
    amiga_custom_regs_tooltip_appendLine(builder, "BEAMCON0=$%04x", (unsigned)value);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "HARDDIS=%d LPENDIS=%d VARVBEN=%d",
                                         (value >> 14) & 1,
                                         (value >> 13) & 1,
                                         (value >> 12) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "LOLDIS=%d CSCBEN=%d VARVSYEN=%d",
                                         (value >> 11) & 1,
                                         (value >> 10) & 1,
                                         (value >> 9) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "VARHSYEN=%d VARBEAMEN=%d DUAL=%d",
                                         (value >> 8) & 1,
                                         (value >> 7) & 1,
                                         (value >> 6) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "PAL=%d VARCSYEN=%d BLANKEN=%d",
                                         (value >> 5) & 1,
                                         (value >> 4) & 1,
                                         (value >> 3) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "CSYTRUE=%d VSYTRUE=%d HSYTRUE=%d",
                                         (value >> 2) & 1,
                                         (value >> 1) & 1,
                                         value & 1);
}

static void
amiga_custom_regs_tooltip_decodeBltcon0(uint16_t value,
                                        amiga_custom_regs_tooltip_builder_t *builder)
{
    unsigned ash = (unsigned)((value >> 12) & 0x0fu);
    unsigned lf = (unsigned)(value & 0xffu);

    amiga_custom_regs_tooltip_appendLine(builder,
                                         "BLTCON0=$%04x ASH=%u LF=$%02x",
                                         (unsigned)value,
                                         ash,
                                         lf);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "USEA=%d USEB=%d USEC=%d USED=%d",
                                         (value >> 11) & 1,
                                         (value >> 10) & 1,
                                         (value >> 9) & 1,
                                         (value >> 8) & 1);
}

static void
amiga_custom_regs_tooltip_decodeBltcon1(uint16_t value,
                                        amiga_custom_regs_tooltip_builder_t *builder)
{
    unsigned bsh = (unsigned)((value >> 12) & 0x0fu);
    int lineMode = value & 1;

    amiga_custom_regs_tooltip_appendLine(builder,
                                         "BLTCON1=$%04x BSH=%u LINE=%d",
                                         (unsigned)value,
                                         bsh,
                                         lineMode);
    if (lineMode) {
        amiga_custom_regs_tooltip_appendLine(builder,
                                             "DOFF=%d SIGN=%d SUD=%d SUL=%d AUL=%d SING=%d",
                                             (value >> 7) & 1,
                                             (value >> 6) & 1,
                                             (value >> 4) & 1,
                                             (value >> 3) & 1,
                                             (value >> 2) & 1,
                                             (value >> 1) & 1);
    } else {
        amiga_custom_regs_tooltip_appendLine(builder,
                                             "DOFF=%d DESC=%d EFE=%d IFE=%d FCI=%d",
                                             (value >> 7) & 1,
                                             (value >> 1) & 1,
                                             (value >> 4) & 1,
                                             (value >> 3) & 1,
                                             (value >> 2) & 1);
    }
}

static unsigned
amiga_custom_regs_tooltip_countOne16(uint16_t value)
{
    unsigned count = 0;
    for (int bit = 0; bit < 16; ++bit) {
        if (((value >> bit) & 1u) != 0u) {
            count++;
        }
    }
    return count;
}

static void
amiga_custom_regs_tooltip_decodeBltWordMask(const char *name,
                                            uint16_t value,
                                            amiga_custom_regs_tooltip_builder_t *builder)
{
    unsigned enabledBits = amiga_custom_regs_tooltip_countOne16(value);
    int highestSet = -1;
    int lowestSet = -1;

    for (int bit = 15; bit >= 0; --bit) {
        if (((value >> bit) & 1u) != 0u) {
            highestSet = bit;
            break;
        }
    }
    for (int bit = 0; bit < 16; ++bit) {
        if (((value >> bit) & 1u) != 0u) {
            lowestSet = bit;
            break;
        }
    }

    amiga_custom_regs_tooltip_appendLine(builder,
                                         "%s=$%04x",
                                         name,
                                         (unsigned)value);
    if (value == 0xffffu) {
        amiga_custom_regs_tooltip_appendLine(builder,
                                             "All bits pass source A (no mask).");
    } else if (value == 0x0000u) {
        amiga_custom_regs_tooltip_appendLine(builder,
                                             "All bits blocked from source A.");
    } else {
        amiga_custom_regs_tooltip_appendLine(builder,
                                             "Enabled bits=%u/16 (per-bit AND mask)",
                                             enabledBits);
        amiga_custom_regs_tooltip_appendLine(builder,
                                             "Highest set bit=%d Lowest set bit=%d",
                                             highestSet,
                                             lowestSet);
    }
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "Mask is ANDed with A before shifting.");
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "AFWM=first word, ALWM=last word.");
}

static void
amiga_custom_regs_tooltip_decodeBltsize(uint16_t value,
                                        amiga_custom_regs_tooltip_builder_t *builder)
{
    unsigned h = (unsigned)((value >> 6) & 0x03ffu);
    unsigned w = (unsigned)(value & 0x003fu);
    if (h == 0u) {
        h = 1024u;
    }
    if (w == 0u) {
        w = 64u;
    }
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "BLTSIZE H=%u W=%u words",
                                         h,
                                         w);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "Write starts blitter operation.");
}

static void
amiga_custom_regs_tooltip_decodeBltsizv(uint16_t value,
                                        amiga_custom_regs_tooltip_builder_t *builder)
{
    unsigned h = (unsigned)(value & 0x7fffu);
    if (h == 0u) {
        h = 32768u;
    }
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "BLTSIZV height=%u lines",
                                         h);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "Write BLTSIZV before BLTSIZH.");
}

static void
amiga_custom_regs_tooltip_decodeBltsizh(uint16_t value,
                                        amiga_custom_regs_tooltip_builder_t *builder)
{
    unsigned w = (unsigned)(value & 0x07ffu);
    if (w == 0u) {
        w = 2048u;
    }
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "BLTSIZH width=%u words",
                                         w);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "BLTSIZH write starts big blit.");
}

static void
amiga_custom_regs_tooltip_decodeSerdat(uint16_t value,
                                       amiga_custom_regs_tooltip_builder_t *builder)
{
    unsigned data = (unsigned)(value & 0x01ffu);
    unsigned upper = (unsigned)((value >> 10) & 0x003fu);
    int stop = (value >> 9) & 1;
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "SERDAT STOP=%d D8=%u D7..0=$%02x",
                                         stop,
                                         (unsigned)((value >> 8) & 0x1u),
                                         (unsigned)(value & 0x00ffu));
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "Packed data bits D8..D0=$%03x",
                                         data);
    if (upper != 0u) {
        amiga_custom_regs_tooltip_appendLine(builder,
                                             "Bits15..10 are set ($%02x), usually zero.",
                                             upper);
    }
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "Writing queues transmit buffer.");
}

static void
amiga_custom_regs_tooltip_decodeSerdatr(uint16_t value,
                                        amiga_custom_regs_tooltip_builder_t *builder)
{
    amiga_custom_regs_tooltip_appendLine(builder, "SERDATR=$%04x", (unsigned)value);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "OVRUN=%d RBF=%d TBE=%d TSRE=%d",
                                         (value >> 15) & 1,
                                         (value >> 14) & 1,
                                         (value >> 13) & 1,
                                         (value >> 12) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "RXD=%d STP=%d STP/DB8=%d DATA7..0=$%02x",
                                         (value >> 11) & 1,
                                         (value >> 9) & 1,
                                         (value >> 8) & 1,
                                         (unsigned)(value & 0x00ffu));
}

static void
amiga_custom_regs_tooltip_decodeSerper(uint16_t value,
                                       amiga_custom_regs_tooltip_builder_t *builder)
{
    unsigned rate = (unsigned)(value & 0x7fffu);
    int longWord = (value >> 15) & 1;
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "SERPER LONG=%d RATE=%u",
                                         longWord,
                                         rate);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "Baud uses 1/((N+1)*0.2794us).");
}

static void
amiga_custom_regs_tooltip_decodeDiw(const char *name,
                                    uint16_t value,
                                    amiga_custom_regs_tooltip_builder_t *builder)
{
    unsigned v = (unsigned)((value >> 8) & 0xffu);
    unsigned h = (unsigned)(value & 0xffu);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "%s V=%u H=%u",
                                         name,
                                         v,
                                         h);
}

static void
amiga_custom_regs_tooltip_decodeDiwhigh(uint16_t value,
                                        amiga_custom_regs_tooltip_builder_t *builder)
{
    unsigned hStop8 = (unsigned)((value >> 13) & 0x1u);
    unsigned vStopHi = (unsigned)((value >> 8) & 0x7u);
    unsigned hStart8 = (unsigned)((value >> 5) & 0x1u);
    unsigned vStartHi = (unsigned)(value & 0x7u);

    amiga_custom_regs_tooltip_appendLine(builder, "DIWHIGH=$%04x", (unsigned)value);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "Stop: H8=%u V10..8=%u",
                                         hStop8,
                                         vStopHi);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "Start: H8=%u V10..8=%u",
                                         hStart8,
                                         vStartHi);
}

static void
amiga_custom_regs_tooltip_decodeDdf(const char *name,
                                    uint16_t value,
                                    amiga_custom_regs_tooltip_builder_t *builder)
{
    unsigned hCode = (unsigned)((value >> 3) & 0x3fu);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "%s H8..H3=%u ($%03x)",
                                         name,
                                         hCode,
                                         (unsigned)(value & 0x01f8u));
}

static void
amiga_custom_regs_tooltip_decodeVpos(const char *name,
                                     uint16_t value,
                                     amiga_custom_regs_tooltip_builder_t *builder)
{
    unsigned lof = (unsigned)((value >> 15) & 0x1u);
    unsigned vmsb = (unsigned)(value & 0x7u);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "%s LOF=%u V10..8=%u",
                                         name,
                                         lof,
                                         vmsb);
}

static void
amiga_custom_regs_tooltip_decodeVhpos(const char *name,
                                      uint16_t value,
                                      amiga_custom_regs_tooltip_builder_t *builder)
{
    unsigned v = (unsigned)((value >> 8) & 0xffu);
    unsigned h = (unsigned)(value & 0xffu);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "%s V=%u Hcode=%u",
                                         name,
                                         v,
                                         h);
}

static void
amiga_custom_regs_tooltip_decodePot(const char *name,
                                    uint16_t value,
                                    amiga_custom_regs_tooltip_builder_t *builder)
{
    unsigned y = (unsigned)((value >> 8) & 0xffu);
    unsigned x = (unsigned)(value & 0xffu);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "%s Y=%u X=%u",
                                         name,
                                         y,
                                         x);
}

static void
amiga_custom_regs_tooltip_decodePotgo(uint16_t value,
                                      amiga_custom_regs_tooltip_builder_t *builder)
{
    amiga_custom_regs_tooltip_appendLine(builder, "POTGO/POTGOR=$%04x", (unsigned)value);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "OUTRY=%d DATRY=%d OUTRX=%d DATRX=%d",
                                         (value >> 15) & 1,
                                         (value >> 14) & 1,
                                         (value >> 13) & 1,
                                         (value >> 12) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "OUTLY=%d DATLY=%d OUTLX=%d DATLX=%d",
                                         (value >> 11) & 1,
                                         (value >> 10) & 1,
                                         (value >> 9) & 1,
                                         (value >> 8) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "START=%d",
                                         value & 1);
}

static void
amiga_custom_regs_tooltip_decodeJoy(uint16_t value,
                                    amiga_custom_regs_tooltip_builder_t *builder)
{
    unsigned y = (unsigned)((value >> 8) & 0xffu);
    unsigned x = (unsigned)(value & 0xffu);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "Mouse counters: Y=%u X=%u",
                                         y,
                                         x);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "Joy fwd=y1^y0 back=x1^x0 left=y1 right=x1.");
}

static void
amiga_custom_regs_tooltip_decodeJoytest(uint16_t value,
                                        amiga_custom_regs_tooltip_builder_t *builder)
{
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "JOYTEST=$%04x",
                                         (unsigned)value);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "Writes test values to all joy counters.");
}

static void
amiga_custom_regs_tooltip_decodeCopcon(uint16_t value,
                                       amiga_custom_regs_tooltip_builder_t *builder)
{
    amiga_custom_regs_tooltip_appendLine(builder, "COPCON CDANG=%d", (value >> 1) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "1 enables Copper dangerous register writes.");
}

static void
amiga_custom_regs_tooltip_decodeCopjmp(const char *name,
                                       amiga_custom_regs_tooltip_builder_t *builder)
{
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "%s strobe: restart Copper pointer.",
                                         name);
}

static void
amiga_custom_regs_tooltip_decodeFmode(uint16_t value,
                                      amiga_custom_regs_tooltip_builder_t *builder)
{
    unsigned bplFetch = (unsigned)(value & 0x3u);
    unsigned sprFetch = (unsigned)((value >> 2) & 0x3u);
    unsigned scan2 = (unsigned)((value >> 14) & 0x1u);

    const char *sprWidth = "16px";
    if (sprFetch == 1u || sprFetch == 2u) {
        sprWidth = "32px";
    } else if (sprFetch == 3u) {
        sprWidth = "64px";
    }

    amiga_custom_regs_tooltip_appendLine(builder,
                                         "FMODE=$%04x BPLF=%u SPRF=%u",
                                         (unsigned)value,
                                         bplFetch,
                                         sprFetch);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "Sprite fetch width=%s SSCAN2=%u",
                                         sprWidth,
                                         scan2);
}

static void
amiga_custom_regs_tooltip_decodeSprPos(uint16_t value,
                                       amiga_custom_regs_tooltip_builder_t *builder)
{
    unsigned sv = (unsigned)((value >> 8) & 0xffu);
    unsigned sh = (unsigned)(value & 0xffu);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "SPRxPOS SV7..0=%u SH8..1=%u",
                                         sv,
                                         sh);
}

static void
amiga_custom_regs_tooltip_decodeSprCtl(uint16_t value,
                                       amiga_custom_regs_tooltip_builder_t *builder)
{
    unsigned ev = (unsigned)((value >> 8) & 0xffu);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "SPRxCTL EV7..0=%u ATT=%d",
                                         ev,
                                         (value >> 7) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "SV8=%d EV8=%d SH0=%d",
                                         (value >> 2) & 1,
                                         (value >> 1) & 1,
                                         value & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "ECS SHR bits: SHSH1=%d SHSH0=%d",
                                         (value >> 4) & 1,
                                         (value >> 3) & 1);
}

static void
amiga_custom_regs_tooltip_decodeSprData(const char *name,
                                        uint16_t value,
                                        amiga_custom_regs_tooltip_builder_t *builder)
{
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "%s sprite image data=$%04x",
                                         name,
                                         (unsigned)value);
    if (amiga_custom_regs_tooltip_endsWith(name, "DATA")) {
        amiga_custom_regs_tooltip_appendLine(builder,
                                             "Writing DATA arms sprite output.");
    }
}

static void
amiga_custom_regs_tooltip_decodeClxdat(uint16_t value,
                                       amiga_custom_regs_tooltip_builder_t *builder)
{
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "CLXDAT=$%04x (read clears it)",
                                         (unsigned)value);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "PF1-PF2=%d PF1-S0/1=%d PF1-S2/3=%d PF1-S4/5=%d PF1-S6/7=%d",
                                         value & 1,
                                         (value >> 1) & 1,
                                         (value >> 2) & 1,
                                         (value >> 3) & 1,
                                         (value >> 4) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "PF2-S0/1=%d PF2-S2/3=%d PF2-S4/5=%d PF2-S6/7=%d",
                                         (value >> 5) & 1,
                                         (value >> 6) & 1,
                                         (value >> 7) & 1,
                                         (value >> 8) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "S0/1-S2/3=%d S0/1-S4/5=%d S0/1-S6/7=%d",
                                         (value >> 9) & 1,
                                         (value >> 10) & 1,
                                         (value >> 11) & 1);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "S2/3-S4/5=%d S2/3-S6/7=%d S4/5-S6/7=%d",
                                         (value >> 12) & 1,
                                         (value >> 13) & 1,
                                         (value >> 14) & 1);
}

static void
amiga_custom_regs_tooltip_decodeAudioLen(const char *name,
                                         uint16_t value,
                                         amiga_custom_regs_tooltip_builder_t *builder)
{
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "%s length=%u words",
                                         name,
                                         (unsigned)value);
}

static void
amiga_custom_regs_tooltip_decodeAudioPer(const char *name,
                                         uint16_t value,
                                         amiga_custom_regs_tooltip_builder_t *builder)
{
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "%s period=%u",
                                         name,
                                         (unsigned)value);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "Minimum practical period is 124.");
}

static void
amiga_custom_regs_tooltip_decodeAudioVol(const char *name,
                                         uint16_t value,
                                         amiga_custom_regs_tooltip_builder_t *builder)
{
    unsigned level = (unsigned)(value & 0x7fu);
    if (level > 64u) {
        level = 64u;
    }
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "%s volume=%u/64",
                                         name,
                                         level);
}

static void
amiga_custom_regs_tooltip_decodeAudioDat(const char *name,
                                         uint16_t value,
                                         amiga_custom_regs_tooltip_builder_t *builder)
{
    int8_t hi = (int8_t)(value >> 8);
    int8_t lo = (int8_t)(value & 0xff);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "%s sample hi=%d lo=%d",
                                         name,
                                         (int)hi,
                                         (int)lo);
}

static void
amiga_custom_regs_tooltip_decodeStrobe(const char *name,
                                       amiga_custom_regs_tooltip_builder_t *builder)
{
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "%s is a strobe register.",
                                         name);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "Write triggers hardware action.");
}

static void
amiga_custom_regs_tooltip_decodeGeneric(const char *name,
                                        uint16_t value,
                                        amiga_custom_regs_tooltip_builder_t *builder)
{
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "%s=$%04x (%u)",
                                         name,
                                         (unsigned)value,
                                         (unsigned)value);
    amiga_custom_regs_tooltip_appendLine(builder,
                                         "No custom bit decoder in this build.");
}

const char *
amiga_custom_regs_valueTooltipForName(const char *regName, uint16_t value)
{
    static amiga_custom_regs_tooltip_builder_t builder;
    int colorIndex = -1;

    amiga_custom_regs_tooltip_reset(&builder);

    if (!regName || !regName[0]) {
        amiga_custom_regs_tooltip_appendLine(&builder,
                                             "Unknown custom register value=$%04x",
                                             (unsigned)value);
        return builder.text;
    }

    if (amiga_custom_regs_tooltip_parseColorIndex(regName, &colorIndex)) {
        amiga_custom_regs_tooltip_decodeColor(regName, value, &builder);
        return builder.text;
    }

    if (amiga_custom_regs_tooltip_isNamed(regName, "DMACON") ||
        amiga_custom_regs_tooltip_isNamed(regName, "DMACONR")) {
        amiga_custom_regs_tooltip_decodeDmacon(regName, value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_startsWith(regName, "INTENA") ||
        amiga_custom_regs_tooltip_startsWith(regName, "INTREQ")) {
        amiga_custom_regs_tooltip_decodeInt(regName, value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_startsWith(regName, "ADKCON")) {
        amiga_custom_regs_tooltip_decodeAdkcon(regName, value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "DSKLEN")) {
        amiga_custom_regs_tooltip_decodeDsklen(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "DSKBYTR")) {
        amiga_custom_regs_tooltip_decodeDskbytr(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "SERDAT")) {
        amiga_custom_regs_tooltip_decodeSerdat(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "SERDATR")) {
        amiga_custom_regs_tooltip_decodeSerdatr(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "SERPER")) {
        amiga_custom_regs_tooltip_decodeSerper(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "BPLCON0")) {
        amiga_custom_regs_tooltip_decodeBplcon0(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "BPLCON1")) {
        amiga_custom_regs_tooltip_decodeBplcon1(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "BPLCON2")) {
        amiga_custom_regs_tooltip_decodeBplcon2(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "BPLCON3")) {
        amiga_custom_regs_tooltip_decodeBplcon3(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "BPLCON4")) {
        amiga_custom_regs_tooltip_decodeBplcon4(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "CLXCON")) {
        amiga_custom_regs_tooltip_decodeClxcon(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "CLXCON2")) {
        amiga_custom_regs_tooltip_decodeClxcon2(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "CLXDAT")) {
        amiga_custom_regs_tooltip_decodeClxdat(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "COPCON")) {
        amiga_custom_regs_tooltip_decodeCopcon(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "COPJMP1") ||
        amiga_custom_regs_tooltip_isNamed(regName, "COPJMP2")) {
        amiga_custom_regs_tooltip_decodeCopjmp(regName, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "BEAMCON0")) {
        amiga_custom_regs_tooltip_decodeBeamcon0(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "BLTCON0")) {
        amiga_custom_regs_tooltip_decodeBltcon0(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "BLTCON1")) {
        amiga_custom_regs_tooltip_decodeBltcon1(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "BLTAFWM") ||
        amiga_custom_regs_tooltip_isNamed(regName, "BLTALWM")) {
        amiga_custom_regs_tooltip_decodeBltWordMask(regName, value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "BLTSIZE")) {
        amiga_custom_regs_tooltip_decodeBltsize(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "BLTSIZV")) {
        amiga_custom_regs_tooltip_decodeBltsizv(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "BLTSIZH")) {
        amiga_custom_regs_tooltip_decodeBltsizh(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "FMODE")) {
        amiga_custom_regs_tooltip_decodeFmode(value, &builder);
        return builder.text;
    }

    if (amiga_custom_regs_tooltip_isNamed(regName, "DIWSTRT") ||
        amiga_custom_regs_tooltip_isNamed(regName, "DIWSTOP")) {
        amiga_custom_regs_tooltip_decodeDiw(regName, value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "DIWHIGH")) {
        amiga_custom_regs_tooltip_decodeDiwhigh(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "DDFSTRT") ||
        amiga_custom_regs_tooltip_isNamed(regName, "DDFSTOP")) {
        amiga_custom_regs_tooltip_decodeDdf(regName, value, &builder);
        return builder.text;
    }

    if (amiga_custom_regs_tooltip_startsWith(regName, "VPOS")) {
        amiga_custom_regs_tooltip_decodeVpos(regName, value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_startsWith(regName, "VHPOS") ||
        amiga_custom_regs_tooltip_startsWith(regName, "HHPOS")) {
        amiga_custom_regs_tooltip_decodeVhpos(regName, value, &builder);
        return builder.text;
    }

    if (amiga_custom_regs_tooltip_isNamed(regName, "JOYTEST")) {
        amiga_custom_regs_tooltip_decodeJoytest(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_startsWith(regName, "JOY") ||
        amiga_custom_regs_tooltip_startsWith(regName, "JOT")) {
        amiga_custom_regs_tooltip_decodeJoy(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_isNamed(regName, "POTGO") ||
        amiga_custom_regs_tooltip_isNamed(regName, "POTGOR")) {
        amiga_custom_regs_tooltip_decodePotgo(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_startsWith(regName, "POT") &&
        amiga_custom_regs_tooltip_endsWith(regName, "DAT")) {
        amiga_custom_regs_tooltip_decodePot(regName, value, &builder);
        return builder.text;
    }

    if (amiga_custom_regs_tooltip_startsWith(regName, "STR")) {
        amiga_custom_regs_tooltip_decodeStrobe(regName, &builder);
        return builder.text;
    }

    if (amiga_custom_regs_tooltip_startsWith(regName, "SPR") &&
        amiga_custom_regs_tooltip_endsWith(regName, "POS")) {
        amiga_custom_regs_tooltip_decodeSprPos(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_startsWith(regName, "SPR") &&
        amiga_custom_regs_tooltip_endsWith(regName, "CTL")) {
        amiga_custom_regs_tooltip_decodeSprCtl(value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_startsWith(regName, "SPR") &&
        (amiga_custom_regs_tooltip_endsWith(regName, "DATA") ||
         amiga_custom_regs_tooltip_endsWith(regName, "DATB"))) {
        amiga_custom_regs_tooltip_decodeSprData(regName, value, &builder);
        return builder.text;
    }

    if (amiga_custom_regs_tooltip_endsWith(regName, "PTH") ||
        amiga_custom_regs_tooltip_endsWith(regName, "LCH")) {
        amiga_custom_regs_tooltip_decodePointerHigh(regName, value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_endsWith(regName, "PTL") ||
        amiga_custom_regs_tooltip_endsWith(regName, "LCL")) {
        amiga_custom_regs_tooltip_decodePointerLow(regName, value, &builder);
        return builder.text;
    }

    if (amiga_custom_regs_tooltip_endsWith(regName, "MOD")) {
        amiga_custom_regs_tooltip_decodeSignedModulo(regName, value, &builder);
        return builder.text;
    }

    if (amiga_custom_regs_tooltip_parseSingleIndex(regName, "AUD", "LEN", NULL)) {
        amiga_custom_regs_tooltip_decodeAudioLen(regName, value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_parseSingleIndex(regName, "AUD", "PER", NULL)) {
        amiga_custom_regs_tooltip_decodeAudioPer(regName, value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_parseSingleIndex(regName, "AUD", "VOL", NULL)) {
        amiga_custom_regs_tooltip_decodeAudioVol(regName, value, &builder);
        return builder.text;
    }
    if (amiga_custom_regs_tooltip_parseSingleIndex(regName, "AUD", "DAT", NULL)) {
        amiga_custom_regs_tooltip_decodeAudioDat(regName, value, &builder);
        return builder.text;
    }

    if (amiga_custom_regs_tooltip_startsWith(regName, "BPL") &&
        amiga_custom_regs_tooltip_endsWith(regName, "DAT")) {
        amiga_custom_regs_tooltip_appendLine(&builder,
                                             "%s bitplane data=$%04x",
                                             regName,
                                             (unsigned)value);
        return builder.text;
    }

    if (amiga_custom_regs_tooltip_isNamed(regName, "NO-OP")) {
        amiga_custom_regs_tooltip_appendLine(&builder,
                                             "Copper NOP: value is ignored.");
        return builder.text;
    }

    amiga_custom_regs_tooltip_decodeGeneric(regName, value, &builder);
    return builder.text;
}

const char *
amiga_custom_regs_valueTooltipForOffset(uint16_t regOffset, uint16_t value)
{
    const char *name = amiga_custom_regs_nameForOffset(regOffset);
    return amiga_custom_regs_valueTooltipForName(name, value);
}
