/* ui.c - hi-res bitmap interface for HEADCRASH Format Util.
 *
 * The bitmap sits at $6000 and the screen matrix at $5c00, which puts the
 * VIC in bank 1. Everything it reads is then plain RAM with the ROMs still
 * mapped in, so drawing never has to bank anything out or turn interrupts
 * off. It is all packed against the top of the bank, leaving the program
 * everything below $5000.
 *
 * The primitives that get called in bulk are in src/gfx.s; what is left
 * here is the arithmetic that decides where things go.
 *
 * (C) 2026 Robert Mech. Licence GPL-3.0-or-later.
 *
 * Required libraries: cc65 C library.
 */

#include <string.h>
#include "ui.h"

#define SCREEN  ((unsigned char *)0x5c00)
#define CHARSET ((unsigned char *)0x5400)
#define SPRITES ((unsigned char *)0x5000)

#define VIC_CR1   (*(volatile unsigned char *)0xd011)
#define VIC_CR2   (*(volatile unsigned char *)0xd016)
#define VIC_MEM   (*(volatile unsigned char *)0xd018)
#define VIC_BORD  (*(volatile unsigned char *)0xd020)
#define VIC_BACK  (*(volatile unsigned char *)0xd021)
#define VIC_SPREN (*(volatile unsigned char *)0xd015)
#define VIC_SPCOL (*(volatile unsigned char *)0xd027)
#define VIC_SPX   ((volatile unsigned char *)0xd000)
#define VIC_SPMSB (*(volatile unsigned char *)0xd010)
#define CIA2_PRA  (*(volatile unsigned char *)0xdd00)
#define CIA2_DDR  (*(volatile unsigned char *)0xdd02)
#define CPU_PORT  (*(unsigned char *)0x0001)

/* Parameters for the assembly primitives. */
extern unsigned int  gfx_x, gfx_x2;
extern unsigned char gfx_y, gfx_col, gfx_ch;
extern void gfx_clear(void);
extern void gfx_plot(void);
extern void gfx_hline(void);
extern void gfx_glyph(void);
extern void gfx_barfill(void);

/* Table of x radii: index by the vertical radius, get the horizontal one.
 * A C64 pixel is about five sixths as wide as it is tall, so a shape with
 * equal radii looks squashed; stretching x by six fifths puts that right.
 * Built once rather than divided per pixel.
 */
static unsigned char xscale[81];

static void build_xscale(void)
{
    unsigned char i;

    for (i = 0; i < 81; ++i) {
        xscale[i] = (unsigned char)(((unsigned int)i * 6u) / 5u);
    }
}

static unsigned char ink = UI_CYAN;

/* ---------------------------------------------------------------------- */
/* setup                                                                   */
/* ---------------------------------------------------------------------- */

/* A left pointing arrow, 24 wide by 21 high, for the head marker. Drawn
 * as a sprite rather than into the bitmap because the disc's cells are
 * already spoken for by the disc's own ink, and a sprite carries its own
 * colour over the top of them.
 */
static const unsigned char arrow[63] = {
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x01, 0x00, 0x00,
    0x03, 0x00, 0x00,
    0x07, 0x00, 0x00,
    0x0f, 0xff, 0xf0,
    0x1f, 0xff, 0xf0,
    0x3f, 0xff, 0xf0,
    0x7f, 0xff, 0xf0,
    0x3f, 0xff, 0xf0,
    0x1f, 0xff, 0xf0,
    0x0f, 0xff, 0xf0,
    0x07, 0x00, 0x00,
    0x03, 0x00, 0x00,
    0x01, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00
};

void ui_init(void)
{
    unsigned int n;

    /* Take a copy of the character set while it is briefly visible at
     * $D000, and keep it in RAM where reading it costs nothing.
     */
    __asm__("sei");
    CPU_PORT = 0x33;
    for (n = 0; n < 2048u; ++n) {
        CHARSET[n] = ((unsigned char *)0xd000)[n];
    }
    CPU_PORT = 0x37;
    __asm__("cli");

    build_xscale();

    for (n = 0; n < 63u; ++n) {
        SPRITES[n] = arrow[n];
    }

    /* VIC bank 1 ($4000-$7FFF). The two low bits of port A are inverted,
     * so bank 1 is written as %10.
     */
    CIA2_DDR |= 0x03;
    CIA2_PRA = (unsigned char)((CIA2_PRA & 0xfc) | 0x02);

    /* Screen matrix at $5c00 (offset $1c00 -> %0111) and bitmap at $6000
     * (offset $2000 -> CB13 set).
     */
    VIC_MEM  = 0x78;
    VIC_CR1  = 0x3b; /* bitmap mode, 25 rows, screen on */
    VIC_CR2  = 0x08; /* 40 columns, no multicolour */
    VIC_BORD = UI_BLACK;
    VIC_BACK = UI_BLACK;

    SCREEN[0x3f8] = (unsigned char)((0x5000 - 0x4000) / 64); /* sprite 0 */
    VIC_SPCOL = UI_WHITE;
    VIC_SPREN = 0x00;

    ui_clear();
}

void ui_done(void)
{
    VIC_SPREN = 0x00;
    VIC_CR1  = 0x1b;
    VIC_CR2  = 0xc8;
    VIC_MEM  = 0x15;
    CIA2_PRA = (unsigned char)(CIA2_PRA | 0x03);
    VIC_BORD = 14;
    VIC_BACK = 6;
}

void ui_clear(void)
{
    gfx_col = (unsigned char)((ink << 4) | UI_BLACK);
    gfx_clear();
}

void ui_ink(unsigned char colour)
{
    ink = colour;
    gfx_col = (unsigned char)((colour << 4) | UI_BLACK);
}

/* ---------------------------------------------------------------------- */
/* primitives                                                              */
/* ---------------------------------------------------------------------- */

void ui_plot(unsigned int x, unsigned char y)
{
    if (x > 319 || y > 199) {
        return;
    }
    gfx_x = x;
    gfx_y = y;
    gfx_plot();
}

void ui_hline(unsigned int x1, unsigned int x2, unsigned char y)
{
    if (y > 199 || x1 > 319) {
        return;
    }
    if (x2 > 319) {
        x2 = 319;
    }
    if (x2 < x1) {
        return;
    }
    gfx_x  = x1;
    gfx_x2 = x2;
    gfx_y  = y;
    gfx_hline();
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
    gfx_x = x;
    for (y = y1; y <= y2; ++y) {
        gfx_y = y;
        gfx_plot();
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

/* Recolour a block of cells without touching a pixel. This is how the
 * drives show which one is selected and whether their light is on: the
 * art is drawn once at start up and only the colour bytes move after.
 */
void ui_cell_colour(unsigned char cx, unsigned char cy,
                    unsigned char w, unsigned char h, unsigned char colour)
{
    unsigned char r, c;
    unsigned char v = (unsigned char)((colour << 4) | UI_BLACK);
    unsigned int  o;

    for (r = 0; r < h; ++r) {
        o = (unsigned int)(cy + r) * 40u + cx;
        for (c = 0; c < w; ++c) {
            SCREEN[o + c] = v;
        }
    }
}

/* Blank a block of cells: pixels off, colour set to the current ink. Used
 * where something is redrawn in place, like the device readout.
 */
void ui_cell_blank(unsigned char cx, unsigned char cy,
                   unsigned char w, unsigned char h)
{
    unsigned char r, c, i;
    unsigned char *p;

    for (r = 0; r < h; ++r) {
        for (c = 0; c < w; ++c) {
            p = (unsigned char *)0x6000 + (unsigned int)(cy + r) * 320u
                + (unsigned int)(cx + c) * 8u;
            for (i = 0; i < 8; ++i) {
                p[i] = 0;
            }
        }
    }
}

void ui_bar(unsigned int x, unsigned char y, unsigned int w)
{
    if (w == 0) {
        return;
    }
    if (x + w > 320) {
        w = 320 - x;
    }
    gfx_x  = x;
    gfx_x2 = x + w - 1;
    gfx_y  = y;
    gfx_barfill();
}

/* An ellipse rather than a circle, because a C64 pixel is not square: 320
 * by 200 on a four by three screen makes each one about five sixths as
 * wide as it is tall, so a shape drawn with equal radii comes out visibly
 * squashed. The horizontal radius is scaled up by six fifths to put that
 * right, which is why the disc is round on a television and oval in the
 * pixel grid.
 *
 * Midpoint ellipse, eight points per step, no multiplication in the loop.
 */
/* A ring, drawn as a circle in integer midpoint steps with every x taken
 * through the table above. All 8 and 16 bit: an earlier version did this
 * with 32 bit ellipse arithmetic and the redraw was visible from across
 * the room.
 */
void ui_ring(unsigned int cx, unsigned char cy, unsigned char r)
{
    int x = 0;
    int y = r;
    int d = 3 - 2 * (int)r;
    unsigned char sx, sy;

    if (r > 80) {
        return;
    }
    while (x <= y) {
        sx = xscale[x];
        sy = xscale[y];

        ui_plot(cx + sy, (unsigned char)(cy + x));
        ui_plot(cx - sy, (unsigned char)(cy + x));
        ui_plot(cx + sy, (unsigned char)(cy - x));
        ui_plot(cx - sy, (unsigned char)(cy - x));
        /* Across the top and bottom of the ring the x step is wider than
         * one pixel once it has been stretched, so these go down as short
         * runs rather than points; otherwise the ring comes out combed.
         */
        ui_hline(cx + sx, cx + sx + 1, (unsigned char)(cy + y));
        ui_hline(cx - sx - 1, cx - sx, (unsigned char)(cy + y));
        ui_hline(cx + sx, cx + sx + 1, (unsigned char)(cy - y));
        ui_hline(cx - sx - 1, cx - sx, (unsigned char)(cy - y));

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
/* the head marker                                                         */
/* ---------------------------------------------------------------------- */

void ui_arrow(unsigned int x, unsigned char y)
{
    unsigned int sx = x + 24;   /* sprite x is offset by the border */

    VIC_SPX[0] = (unsigned char)(sx & 0xff);
    if (sx > 255) {
        VIC_SPMSB |= 0x01;
    } else {
        VIC_SPMSB = (unsigned char)(VIC_SPMSB & 0xfe);
    }
    VIC_SPX[1] = (unsigned char)(y + 50);
    VIC_SPREN |= 0x01;
}

void ui_arrow_off(void)
{
    VIC_SPREN = (unsigned char)(VIC_SPREN & 0xfe);
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
    if (x > 312 || y > 192) {
        return;
    }
    gfx_x  = x;
    gfx_y  = y;
    gfx_ch = g;
    gfx_glyph();
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
