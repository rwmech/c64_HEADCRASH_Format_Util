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
 * (C) 2026 Robert Mech. Licence MIT.
 *
 * Required libraries: cc65 C library.
 */

#include <string.h>
#include "ui.h"

#define SCREEN  ((unsigned char *)0x5c00)
#define CHARSET ((unsigned char *)0xc000)
/* What the VIC actually has to read out of bank 1 is the bitmap, the
 * screen matrix and the sprites, and nothing else. The copy of the
 * character set is not one of them: the VIC is in bitmap mode and never
 * looks at it, it is only the source table this program blits glyphs from,
 * so it has no business taking two kilobytes out of the bank. It lives at
 * $C000 now, which is RAM the VIC cannot see from here and nothing else
 * wants.
 *
 * That leaves the sprites as the lowest thing the VIC reads, and they are
 * 256 bytes, so they go directly under the matrix at $5B00. The program
 * gets everything below that: nearly eleven kilobytes more than it had
 * when the character set was sitting in the middle of the bank.
 */
#define SPRITES ((unsigned char *)0x5b00)

#define VIC_CR1   (*(volatile unsigned char *)0xd011)
#define VIC_CR2   (*(volatile unsigned char *)0xd016)
#define VIC_MEM   (*(volatile unsigned char *)0xd018)
#define VIC_BORD  (*(volatile unsigned char *)0xd020)
#define VIC_BACK  (*(volatile unsigned char *)0xd021)
#define VIC_SPREN (*(volatile unsigned char *)0xd015)
#define VIC_SPCOL ((volatile unsigned char *)0xd027)
#define VIC_SPMC  (*(volatile unsigned char *)0xd01c)
#define VIC_SPXE  (*(volatile unsigned char *)0xd01d)
#define VIC_SPYE  (*(volatile unsigned char *)0xd017)
#define VIC_SPX   ((volatile unsigned char *)0xd000)
#define VIC_SPMSB (*(volatile unsigned char *)0xd010)
#define CIA2_PRA  (*(volatile unsigned char *)0xdd00)
#define CIA2_DDR  (*(volatile unsigned char *)0xdd02)
#define CPU_PORT  (*(unsigned char *)0x0001)

/* Parameters for the assembly primitives. */
extern unsigned int  gfx_x, gfx_x2;
extern unsigned char gfx_y, gfx_col, gfx_ch, gfx_inv;

/* What each sprite's data block is, so it can be put back. */
static unsigned char sprite_ptr[3];
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
/* The horizontal stretch table, parked in free RAM with the other buffers
 * that do not need to be inside the program's own ceiling.
 */
#define xscale ((unsigned char *)0xc980)

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
/* Four sprite shapes.
 *
 * The track window is two of them, stacked, because the window is 28
 * pixels tall and a sprite is 21. They are solid and black, so what they
 * do on top of the disc is cut a hole in it: the rings stop at the edge of
 * the window exactly as a jacket cuts a floppy. Drawing it this way means
 * the window costs nothing per track, where the bitmap version had to be
 * redrawn over every ring.
 *
 * The head is a slider on the end of an arm, which is what the thing on a
 * hard drive looks like, and it passes over the window. Two sizes: the
 * 5.25 inch one is visibly the bigger.
 */
static const unsigned char head_525[63] = {
    /* A slider pad on the end of a tapered arm, which is the shape of the
     * thing that actually flies over a platter. The pad is an outline with
     * the read element solid in the middle of it, so it reads as a head
     * rather than as a block.
     */
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x7f,
    0x00, 0x03, 0xff,
    0x3f, 0xff, 0xff,
    0x20, 0x1f, 0xff,
    0x27, 0x9f, 0xff,
    0x20, 0x1f, 0xff,
    0x3f, 0xff, 0xff,
    0x00, 0x03, 0xff,
    0x00, 0x00, 0x7f,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00
};

static const unsigned char head_35[63] = {
    /* The same idea at 3.5 inch scale: a smaller pad, a thinner arm. */
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x1f,
    0x0f, 0xe0, 0xff,
    0x0f, 0xff, 0xff,
    0x0f, 0xff, 0xff,
    0x0f, 0xe0, 0xff,
    0x00, 0x00, 0x1f,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00
};

static const unsigned char win_top[63] = {
    /* Twelve pixels wide, which is two narrower than the frame drawn in
     * the bitmap, so the black fill sits inside the window instead of
     * swallowing its edges.
     */
    0x00, 0xff, 0x00,
    0x01, 0xff, 0x80,
    0x03, 0xff, 0xc0,
    0x03, 0xff, 0xc0,
    0x03, 0xff, 0xc0,
    0x03, 0xff, 0xc0,
    0x03, 0xff, 0xc0,
    0x03, 0xff, 0xc0,
    0x03, 0xff, 0xc0,
    0x03, 0xff, 0xc0,
    0x03, 0xff, 0xc0,
    0x03, 0xff, 0xc0,
    0x03, 0xff, 0xc0,
    0x03, 0xff, 0xc0,
    0x03, 0xff, 0xc0,
    0x03, 0xff, 0xc0,
    0x03, 0xff, 0xc0,
    0x03, 0xff, 0xc0,
    0x03, 0xff, 0xc0,
    0x03, 0xff, 0xc0,
    0x03, 0xff, 0xc0
};

static const unsigned char win_bot[63] = {
    0x03, 0xff, 0xc0,
    0x03, 0xff, 0xc0,
    0x03, 0xff, 0xc0,
    0x03, 0xff, 0xc0,
    0x03, 0xff, 0xc0,
    0x01, 0xff, 0x80,
    0x00, 0xff, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
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
        SPRITES[n]       = head_525[n];
        SPRITES[64 + n]  = head_35[n];
        SPRITES[128 + n] = win_top[n];
        SPRITES[192 + n] = win_bot[n];
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

    /* Sprite 0 is the head, 1 and 2 are the two halves of the window.
     * They are kept in a table and written again every time a sprite is
     * placed, so a stray write over the pointers cannot leave a sprite
     * showing somebody else's bytes.
     */
    sprite_ptr[0] = 0x6c;
    sprite_ptr[1] = 0x6e;
    sprite_ptr[2] = 0x6f;
    SCREEN[0x3f8] = sprite_ptr[0];
    SCREEN[0x3f9] = sprite_ptr[1];
    SCREEN[0x3fa] = sprite_ptr[2];
    VIC_SPCOL[0] = UI_LGREY;
    VIC_SPCOL[1] = UI_BLACK;
    VIC_SPCOL[2] = UI_BLACK;
    /* Single width, single height, one colour. Never assume a reset
     * left these clear; a doubled sprite is a blob.
     */
    VIC_SPMC = 0x00;
    VIC_SPXE = 0x00;
    VIC_SPYE = 0x00;
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
    unsigned char sx, sy, psx, psy;

    if (r > 80) {
        return;
    }

    /* Midpoint circle, with every x taken through the stretch table so the
     * ring looks round on a television rather than squashed.
     *
     * The stretch is what makes this more than a plot per octant. One step
     * in y moves the stretched x by about one and a fifth pixels, so
     * plotting points alone leaves the ring combed, worst at the forty
     * five degree corners where both coordinates are moving. Each point is
     * therefore joined to the one before it with a short horizontal run,
     * which closes the gaps in all eight octants and costs nothing: a run
     * of one pixel is a plot.
     */
    psx = xscale[0];
    psy = xscale[y];

    while (x <= y) {
        sx = xscale[x];
        sy = xscale[y];

        /* The steep octants, joined along the row they share. */
        ui_hline(cx + sy, cx + psy, (unsigned char)(cy + x));
        ui_hline(cx - psy, cx - sy, (unsigned char)(cy + x));
        ui_hline(cx + sy, cx + psy, (unsigned char)(cy - x));
        ui_hline(cx - psy, cx - sy, (unsigned char)(cy - x));

        /* The shallow octants, where the run is the whole of the step. */
        ui_hline(cx + psx, cx + sx, (unsigned char)(cy + y));
        ui_hline(cx - sx, cx - psx, (unsigned char)(cy + y));
        ui_hline(cx + psx, cx + sx, (unsigned char)(cy - y));
        ui_hline(cx - sx, cx - psx, (unsigned char)(cy - y));

        psx = sx;
        psy = sy;

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

/* Which carriage to use. The two blocks sit back to back at the bottom of
 * the VIC bank, so this is one store into the sprite pointer.
 */
void ui_head_shape(unsigned char big)
{
    sprite_ptr[0] = (unsigned char)(big ? 0x6c : 0x6d);
    SCREEN[0x3f8] = sprite_ptr[0];
}

/* Position a sprite and switch it on. x and y are the top left of the 24
 * by 21 shape in screen coordinates; the VIC's own offsets are added here.
 */
void ui_sprite(unsigned char n, unsigned int x, unsigned char y)
{
    unsigned int  sx  = x + 24;
    unsigned char bit = (unsigned char)(1u << n);

    SCREEN[0x3f8 + n] = sprite_ptr[n];

    VIC_SPX[n * 2]     = (unsigned char)(sx & 0xff);
    VIC_SPX[n * 2 + 1] = (unsigned char)(y + 50);
    if (sx > 255) {
        VIC_SPMSB |= bit;
    } else {
        VIC_SPMSB = (unsigned char)(VIC_SPMSB & ~bit);
    }
    VIC_SPREN |= bit;
}

void ui_sprite_off(unsigned char n)
{
    VIC_SPREN = (unsigned char)(VIC_SPREN & ~(1u << n));
}

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

/* Reverse video: the ink fills the cell and the letter is knocked out of
 * it in black. One EOR in the glyph blitter, so it costs nothing.
 */
void ui_reverse(unsigned char on)
{
    gfx_inv = on ? 0xffu : 0x00u;
}

/* One character at a pixel position, so a caller can pace a line out at a
 * character at a time instead of drawing the whole string at once.
 */
void ui_char(unsigned int x, unsigned char y, char c)
{
    draw_glyph(x, y, glyph_of((unsigned char)c));
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
