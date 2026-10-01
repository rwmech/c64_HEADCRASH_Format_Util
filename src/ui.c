/* ui.c - hi-res bitmap interface for HEADCRASH Format Util.
 *
 * The bitmap sits at $4000 and the screen matrix at $6000, which puts the
 * VIC in bank 1. Everything the VIC reads is then plain RAM with the ROMs
 * still mapped in, so drawing never has to bank anything out or turn
 * interrupts off, and a line can read a byte back to merge it like any
 * other memory. The program and its stack live below $4000.
 *
 * (C) 2026 Robert Mech. Licence GPL-3.0-or-later.
 *
 * Required libraries: cc65 C library.
 */

#include <string.h>
#include "ui.h"

#define BITMAP  ((unsigned char *)0x4000)
#define SCREEN  ((unsigned char *)0x6000)
#define CHARSET ((unsigned char *)0x6400)

#define VIC_CR1  (*(volatile unsigned char *)0xd011)
#define VIC_CR2  (*(volatile unsigned char *)0xd016)
#define VIC_MEM  (*(volatile unsigned char *)0xd018)
#define VIC_BORD (*(volatile unsigned char *)0xd020)
#define VIC_BACK (*(volatile unsigned char *)0xd021)
#define CIA2_PRA (*(volatile unsigned char *)0xdd00)
#define CIA2_DDR (*(volatile unsigned char *)0xdd02)
#define CPU_PORT (*(unsigned char *)0x0001)

static unsigned char ink = UI_CYAN;

/* Row base addresses, so the per pixel maths is an add rather than a
 * multiply. Row n of cells starts at 320 * n.
 */
static unsigned int row_base[25];

/* ---------------------------------------------------------------------- */
/* setup                                                                   */
/* ---------------------------------------------------------------------- */

void ui_init(void)
{
    unsigned char i;
    unsigned int  n;

    for (i = 0; i < 25; ++i) {
        row_base[i] = (unsigned int)i * 320u;
    }

    /* The character set is wanted as data, not as a character generator,
     * so take a copy out of ROM while it is briefly visible at $D000 and
     * keep it in RAM where reading it costs nothing.
     */
    __asm__("sei");
    CPU_PORT = 0x33;
    for (n = 0; n < 2048u; ++n) {
        CHARSET[n] = ((unsigned char *)0xd000)[n];
    }
    CPU_PORT = 0x37;
    __asm__("cli");

    /* VIC bank 1 ($4000-$7FFF). The two low bits of port A are inverted,
     * so bank 1 is written as %10.
     */
    CIA2_DDR |= 0x03;
    CIA2_PRA = (unsigned char)((CIA2_PRA & 0xfc) | 0x02);

    /* Bitmap at $4000 (offset 0), screen matrix at $6000 (offset $2000). */
    VIC_MEM  = 0x80;
    VIC_CR1  = 0x3b; /* bitmap mode, 25 rows, screen on */
    VIC_CR2  = 0x08; /* 40 columns, no multicolour */
    VIC_BORD = UI_BLACK;
    VIC_BACK = UI_BLACK;

    ui_clear();
}

void ui_done(void)
{
    VIC_CR1  = 0x1b;
    VIC_CR2  = 0xc8;
    VIC_MEM  = 0x15;
    CIA2_PRA = (unsigned char)(CIA2_PRA | 0x03);
    VIC_BORD = 14;
    VIC_BACK = 6;
}

void ui_clear(void)
{
    unsigned int n;

    for (n = 0; n < 8000u; ++n) {
        BITMAP[n] = 0;
    }

    for (n = 0; n < 1000u; ++n) {
        SCREEN[n] = (unsigned char)((ink << 4) | UI_BLACK);
    }
}

void ui_ink(unsigned char colour)
{
    ink = colour;
}

/* ---------------------------------------------------------------------- */
/* primitives                                                              */
/* ---------------------------------------------------------------------- */

/* Byte holding pixel (x, y), and the bit within it. */
static unsigned int byte_of(unsigned int x, unsigned char y)
{
    return row_base[y >> 3] + ((x >> 3) << 3) + (y & 7);
}

static void cell_ink(unsigned int x, unsigned char y)
{
    SCREEN[(unsigned int)(y >> 3) * 40u + (x >> 3)] =
        (unsigned char)((ink << 4) | UI_BLACK);
}

void ui_plot(unsigned int x, unsigned char y)
{
    if (x > 319 || y > 199) {
        return;
    }
    cell_ink(x, y);
    BITMAP[byte_of(x, y)] |= (unsigned char)(0x80 >> (x & 7));
}

void ui_hline(unsigned int x1, unsigned int x2, unsigned char y)
{
    unsigned int x;

    if (y > 199) {
        return;
    }
    if (x2 > 319) {
        x2 = 319;
    }
    for (x = x1; x <= x2; ++x) {
        cell_ink(x, y);
        BITMAP[byte_of(x, y)] |= (unsigned char)(0x80 >> (x & 7));
    }
}

void ui_vline(unsigned int x, unsigned char y1, unsigned char y2)
{
    unsigned char y;

    if (x > 319) {
        return;
    }
    if (y2 > 199) {
        y2 = 199;
    }
    for (y = y1; y <= y2; ++y) {
        cell_ink(x, y);
        BITMAP[byte_of(x, y)] |= (unsigned char)(0x80 >> (x & 7));
    }
}

void ui_box(unsigned int x, unsigned char y,
            unsigned int w, unsigned char h)
{
    ui_hline(x, x + w - 1, y);
    ui_hline(x, x + w - 1, (unsigned char)(y + h - 1));
    ui_vline(x, y, (unsigned char)(y + h - 1));
    ui_vline(x + w - 1, y, (unsigned char)(y + h - 1));
}

void ui_fill(unsigned int x, unsigned char y,
             unsigned int w, unsigned char h)
{
    unsigned char r;

    for (r = 0; r < h; ++r) {
        ui_hline(x, x + w - 1, (unsigned char)(y + r));
    }
}

/* Bresenham, drawn eight points at a time. Clipped at the screen edges
 * rather than wrapped, because a wrapped pixel lands somewhere surprising.
 */
void ui_circle(unsigned int cx, unsigned char cy, unsigned char r)
{
    int x = 0;
    int y = r;
    int d = 3 - 2 * (int)r;
    int px, py;

    while (x <= y) {
        static const signed char sx[8] = { 1, -1, 1, -1, 1, -1, 1, -1 };
        static const signed char sy[8] = { 1, 1, -1, -1, 1, 1, -1, -1 };
        unsigned char i;

        for (i = 0; i < 8; ++i) {
            if (i < 4) {
                px = (int)cx + x * sx[i];
                py = (int)cy + y * sy[i];
            } else {
                px = (int)cx + y * sx[i];
                py = (int)cy + x * sy[i];
            }
            if (px >= 0 && px < 320 && py >= 0 && py < 200) {
                ui_plot((unsigned int)px, (unsigned char)py);
            }
        }
        if (d < 0) {
            d += 4 * x + 6;
        } else {
            d += 4 * (x - y) + 10;
            --y;
        }
        ++x;
    }
}

/* ---------------------------------------------------------------------- */
/* text                                                                    */
/* ---------------------------------------------------------------------- */

/* cc65 hands over string literals already in PETSCII, which is not the
 * same thing as a position in the character set. Fold both letter ranges
 * onto the uppercase/graphics glyphs.
 */
static unsigned char glyph_of(unsigned char c)
{
    if (c >= 0xc1 && c <= 0xda) {
        return (unsigned char)(c - 0xc0);       /* shifted letters */
    }
    if (c >= 0x41 && c <= 0x5a) {
        return (unsigned char)(c - 0x40);       /* unshifted letters */
    }
    if (c >= 0x20 && c <= 0x3f) {
        return c;                                /* digits, punctuation */
    }
    return 0x20;
}

static void draw_glyph(unsigned int x, unsigned char y, unsigned char g)
{
    unsigned int src = (unsigned int)g * 8u;
    unsigned int dst = row_base[y >> 3] + ((x >> 3) << 3);
    unsigned char i;

    if (x > 312 || y > 192) {
        return;
    }
    cell_ink(x, y);
    for (i = 0; i < 8; ++i) {
        BITMAP[dst + i] = CHARSET[src + i];
    }
}

void ui_text(unsigned int x, unsigned char y, const char *s)
{
    unsigned char c;

    while ((c = (unsigned char)*s++) != 0) {
        draw_glyph(x, y, glyph_of(c));
        if (x >= 312) {
            break;
        }
        x += 8;
    }
}

void ui_text_pad(unsigned int x, unsigned char y, const char *s,
                 unsigned char width)
{
    unsigned char c;
    unsigned char n = 0;

    while (n < width) {
        c = (unsigned char)*s;
        if (c != 0) {
            ++s;
        } else {
            c = 0x20;
        }
        draw_glyph(x + (unsigned int)n * 8u, y, glyph_of(c));
        ++n;
    }
}

void ui_num(unsigned int x, unsigned char y, unsigned int n,
            unsigned char digits)
{
    unsigned char buf[6];
    unsigned char i = digits;

    while (i-- > 0) {
        buf[i] = (unsigned char)(0x30 + (n % 10u));
        n /= 10u;
    }
    for (i = 0; i < digits; ++i) {
        draw_glyph(x + (unsigned int)i * 8u, y, glyph_of(buf[i]));
    }
}
