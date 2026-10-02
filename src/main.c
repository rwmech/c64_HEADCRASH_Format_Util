/* main.c - HEADCRASH Format Util.
 *
 * A formatter that works one track at a time so it can show you what it is
 * doing, retry a track that fails and tell you which ones are bad.
 *
 * (C) 2026 Robert Mech. Licence MIT.
 *
 * Required libraries: cc65 C library (cbm.h for the KERNAL entry points).
 */

#include <string.h>
#include <cbm.h>
#include "ui.h"
#include <6502.h>
#include "snd.h"
#include "dos.h"
#include "fmt.h"

#define VERSION "V1.4"

/* How many times a track is attempted before it is called bad. Each
 * attempt is a whole job, drawn as it happens.
 */
#define MAX_RETRY 5
#define MAX_BAD   16

/* Layout, in pixels. Text starts on a cell row, because a glyph is written
 * as a whole 8x8 cell; lines and curves can go anywhere. Rows are spaced
 * two cells apart so the panel reads as a list rather than a block.
 */
#define TITLE_Y    2
#define TITLE_TY   8

/* One drive panel, showing the drive that is actually there. Two bodies
 * stacked with one of them greyed out was half the panel saying nothing,
 * and the grey one was never the drive you were using. Now the panel is
 * the drive: a 1541 front, a 1581 front, or, when nothing answers, a
 * hatched plate that says so. The space the second body was taking goes
 * into making the one that is left the right shape.
 *
 * The panel's cells are its own, so the activity light is a recolour.
 */
#define DRV_X      8
#define DRV_W      144
#define DRV_Y      32
#define DRV_H      28
#define DRV_CX     1            /* in cells */
#define DRV_CW     18
#define DRV_CY     4
#define DRV_CH     4
#define LED_CX     4            /* the activity light, its own cell */
#define LED_CY     (DRV_CY + 1)

/* The device number, as a red seven segment readout, level with the top of
 * the panel and clear of both it and the disc.
 */
#define SEG_X      156
#define SEG_CX     19
#define SEG_CW     4
#define SEG_CH     3

#define DISC_CX    254
#define DISC_CY    76
#define DISC_RX    54
#define DISC_RY    45
#define DISC_RMIN  11

/* The drive went back to its old height, and what that reclaimed is a
 * column beside the disc. The DOS line and the three running figures live
 * there now, in one block with their values on a common left margin,
 * instead of being spread across the width further down.
 */
#define DOS_Y      64
#define INFO_Y     80           /* TRACK, then BAD, then RETRY */
#define INFO_STEP  8
#define FIELD_X    8
#define FIELD_Y    112
#define FIELD_STEP 16
#define VAL_X      72
#define BAR_X      8
#define BAR_Y      152
#define BAR_W      304

#define MSG_Y      168
#define KEYS_Y     184          /* two rows of buttons, four each */
#define KEYS2_Y    192
#define MSG_W      38u          /* both message rows, full width */
#define COPY_X     152          /* 20 cells right aligned on row two */

static unsigned char dev = 8;

/* Unshifted PETSCII, not C letters: cc65 maps 'W' in a C literal to the
 * shifted code $D7, which is a graphics character to the drive, and a disk
 * named with those comes back as nonsense in the directory. These are the
 * codes the DOS means by WORKDISK.
 */
static char disk_name[17] = {
    0x57, 0x4f, 0x52, 0x4b, 0x44, 0x49, 0x53, 0x4b, 0  /* WORKDISK */
};
static unsigned char id1 = '6', id2 = '4';
static unsigned char bad_list[MAX_BAD];
static unsigned char bad_count;
static unsigned char last_ring;

/* Two ways to do this. QUICK hands the whole disk to the drive, which is
 * how fast it can possibly go. SURFACE drives it a track at a time, which
 * is slower but is the only way to see a bad track coming and keep it out
 * of the BAM afterwards.
 */
#define MODE_QUICK   0
#define MODE_SURFACE 1
static unsigned char mode = MODE_QUICK;

/* ---------------------------------------------------------------------- */
/* static furniture                                                        */
/* ---------------------------------------------------------------------- */

/* The panel, drawn from what the fronts actually look like rather than
 * from a generic box. What separates the two drives is the hole in the
 * case, not the case: a 5.25 inch slot is 146 mm across a 200 mm front, so
 * it takes most of the width, with the spring loaded latch standing proud
 * in the middle of it; a 3.5 inch mechanism behind the same case leaves a
 * 90 mm slot in a recessed bezel with the eject button hard against its
 * right hand end. At 144 by 56 the case is about two and a half to one,
 * against the real 1541's two to one, which is as close as the screen
 * allows and close enough to recognise.
 *
 * Redrawn whenever the drive changes, which is the only time it changes.
 */
static void draw_case(void)
{
    unsigned int  x   = DRV_X;
    unsigned char y   = DRV_Y;
    unsigned char lip = (unsigned char)(y + DRV_H - 7);
    unsigned char bot = (unsigned char)(y + DRV_H - 1);

    ui_hline(x + 2, x + DRV_W - 3, y);
    ui_vline(x + 2, y, lip);
    ui_vline(x + DRV_W - 3, y, lip);
    ui_hline(x, x + 2, lip);
    ui_hline(x + DRV_W - 3, x + DRV_W - 1, lip);
    ui_vline(x, lip, bot);
    ui_vline(x + DRV_W - 1, lip, bot);
    ui_hline(x, x + DRV_W - 1, bot);
}

static void draw_lights(void)
{
    /* Power, green, and the activity light in a cell of its own so that
     * showing the drive working is one colour byte and no redrawing.
     */
    /* Green power on the left, red activity to the right of it, which is
     * the way round a real drive has them.
     */
    ui_ink(UI_LGREEN);
    ui_fill(DRV_X + 9, (unsigned char)(DRV_Y + 11), 5, 5);
    ui_ink(UI_RED);
    ui_fill(LED_CX * 8 + 1, (unsigned char)(DRV_Y + 11), 6, 5);
}

static void draw_drive_1541(void)
{
    unsigned int  x  = DRV_X;
    unsigned char ym = (unsigned char)(DRV_Y + 12);

    ui_ink(UI_CYAN);
    draw_case();

    /* The slot, broken either side of the latch so the latch reads as
     * standing in front of it rather than drawn over it.
     */
    ui_hline(x + 40, x + 66, (unsigned char)(ym - 6));
    ui_hline(x + 86, x + 126, (unsigned char)(ym - 6));
    ui_hline(x + 40, x + 66, (unsigned char)(ym + 6));
    ui_hline(x + 86, x + 126, (unsigned char)(ym + 6));
    ui_vline(x + 40, (unsigned char)(ym - 6), (unsigned char)(ym + 6));
    ui_vline(x + 126, (unsigned char)(ym - 6), (unsigned char)(ym + 6));

    /* The latch, proud above and below the slot, with a raised face. */
    ui_box(x + 67, (unsigned char)(ym - 10), 18, 21);
    ui_box(x + 71, (unsigned char)(ym - 6), 10, 13);

    draw_lights();
}

static void draw_drive_1581(void)
{
    unsigned int  x  = DRV_X;
    unsigned char ym = (unsigned char)(DRV_Y + 12);

    ui_ink(UI_CYAN);
    draw_case();

    /* Recessed bezel, the small slot in it, then the eject button. */
    ui_box(x + 38, (unsigned char)(ym - 9), 94, 19);
    ui_box(x + 46, (unsigned char)(ym - 4), 54, 8);
    ui_hline(x + 49, x + 97, ym);
    ui_box(x + 108, (unsigned char)(ym - 3), 11, 6);

    draw_lights();
}

/* Nothing answered, so the panel says so rather than showing a drive that
 * is not there. Hatched, because a hatched plate reads as absent at a
 * glance, with the cells behind the words cleared so the text has them to
 * itself.
 */
static void draw_drive_absent(void)
{
    unsigned int  x = DRV_X;
    unsigned char y = DRV_Y;
    unsigned int  k;
    unsigned char j;
    unsigned int  xx;

    ui_ink(UI_DGREY);
    ui_box(x, y, DRV_W, DRV_H);

    for (k = 8; k < DRV_W + DRV_H; k += 10) {
        for (j = 1; j < DRV_H - 1; ++j) {
            xx = x + k - j;
            if (xx > x && xx < x + DRV_W - 1) {
                ui_plot(xx, (unsigned char)(y + j));
            }
        }
    }

    ui_cell_blank(4, (unsigned char)(DRV_CY + 1), 12, 2);
    ui_ink(UI_LRED);
    ui_text(40, (unsigned char)(DRV_Y + 8), "DRIVE NOT");
    ui_text(40, (unsigned char)(DRV_Y + 16), "CONNECTED");
}

static void draw_drive(void)
{
    ui_cell_blank(DRV_CX, DRV_CY, DRV_CW, DRV_CH);

    if (dos_drive_type == DRV_1581) {
        draw_drive_1581();
    } else if (dos_drive_type == DRV_UNKNOWN) {
        draw_drive_absent();
    } else {
        draw_drive_1541();
    }
}

/* Seven segment digits, the way a drive number ought to be shown. Each
 * segment is a short bar two pixels thick; the table is one bit per
 * segment, a at the top going clockwise with g in the middle.
 */
/* Small enough to read as an indicator rather than a headline, and a
 * one pixel stroke so it sits with the rest of the line art.
 */
#define SEG_W 10
#define SEG_H 16

static const unsigned char seg_digit[10] = {
    0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f
};

static void draw_seg(unsigned int x, unsigned char y, unsigned char d)
{
    unsigned char m = seg_digit[d % 10];
    unsigned char i;

    for (i = 0; i < 1; ++i) {
        if (m & 0x01) {                      /* a, top */
            ui_hline(x + 3, x + SEG_W - 3, (unsigned char)(y + i));
        }
        if (m & 0x08) {                      /* d, bottom */
            ui_hline(x + 3, x + SEG_W - 3, (unsigned char)(y + SEG_H - 1 - i));
        }
        if (m & 0x40) {                      /* g, middle */
            ui_hline(x + 3, x + SEG_W - 3,
                     (unsigned char)(y + SEG_H / 2 - 1 + i));
        }
        if (m & 0x02) {                      /* b, top right */
            ui_vline(x + SEG_W - 1 - i, (unsigned char)(y + 2),
                     (unsigned char)(y + SEG_H / 2 - 2));
        }
        if (m & 0x04) {                      /* c, bottom right */
            ui_vline(x + SEG_W - 1 - i, (unsigned char)(y + SEG_H / 2 + 1),
                     (unsigned char)(y + SEG_H - 3));
        }
        if (m & 0x20) {                      /* f, top left */
            ui_vline(x + i, (unsigned char)(y + 2),
                     (unsigned char)(y + SEG_H / 2 - 2));
        }
        if (m & 0x10) {                      /* e, bottom left */
            ui_vline(x + i, (unsigned char)(y + SEG_H / 2 + 1),
                     (unsigned char)(y + SEG_H - 3));
        }
    }
}

/* The readout sits level with the top of the drive that is selected, so it
 * reads as belonging to that drive rather than floating between the two.
 * Both possible positions are cleared before it is drawn.
 */
static void show_seg_device(void)
{
    ui_cell_blank(SEG_CX, DRV_CY, SEG_CW, SEG_CH);
    ui_ink(UI_LRED);
    draw_seg(SEG_X, DRV_Y, (unsigned char)(dev / 10));
    draw_seg(SEG_X + SEG_W + 3, DRV_Y, (unsigned char)(dev % 10));
}

/* Which drive is selected, and whether its light is on. Both are colour
 * only: the art underneath never changes.
 */
/* The panel is the drive that is there, so there is nothing to select any
 * more. All that is left is the activity light, which is one cell.
 */
static void show_drive_state(unsigned char busy)
{
    if (dos_drive_type == DRV_UNKNOWN) {
        return;
    }
    ui_cell_colour(LED_CX, LED_CY, 1, 1, busy ? UI_LRED : UI_DGREY);
}

/* The activity light and the spindle motor are the same fact about the
 * drive, so they are shown and heard from one call.
 */
static void led(unsigned char on)
{
    show_drive_state(on);
}

/* The head access window. On a 5.25 inch disk this is the slot cut in the
 * jacket that the head writes through, so it runs from just clear of the
 * hub out to near the edge, with rounded ends. It is drawn over the rings
 * and put back after each one, because what it exposes is the media the
 * rings are being laid down on, not a hole in the picture.
 */
#define SLOT_X   248u
#define SLOT_W   14u
#define SLOT_Y1  ((unsigned char)(DISC_CY + 15))
#define SLOT_Y2  ((unsigned char)(DISC_CY + 42))

static void draw_slot(void)
{
    ui_ink(UI_CYAN);        /* one ink across the whole disc, see draw_disc */
    ui_vline(SLOT_X, (unsigned char)(SLOT_Y1 + 3), (unsigned char)(SLOT_Y2 - 3));
    ui_vline(SLOT_X + SLOT_W, (unsigned char)(SLOT_Y1 + 3),
             (unsigned char)(SLOT_Y2 - 3));
    ui_hline(SLOT_X + 3, SLOT_X + SLOT_W - 3, SLOT_Y1);
    ui_hline(SLOT_X + 3, SLOT_X + SLOT_W - 3, SLOT_Y2);

    /* Two pixels a corner is all the rounding a slot this size needs. */
    ui_plot(SLOT_X + 1, (unsigned char)(SLOT_Y1 + 2));
    ui_plot(SLOT_X + 2, (unsigned char)(SLOT_Y1 + 1));
    ui_plot(SLOT_X + SLOT_W - 1, (unsigned char)(SLOT_Y1 + 2));
    ui_plot(SLOT_X + SLOT_W - 2, (unsigned char)(SLOT_Y1 + 1));
    ui_plot(SLOT_X + 1, (unsigned char)(SLOT_Y2 - 2));
    ui_plot(SLOT_X + 2, (unsigned char)(SLOT_Y2 - 1));
    ui_plot(SLOT_X + SLOT_W - 1, (unsigned char)(SLOT_Y2 - 2));
    ui_plot(SLOT_X + SLOT_W - 2, (unsigned char)(SLOT_Y2 - 1));
}

/* The track window sits in sprites 1 and 2, stacked, because it is 28
 * pixels tall and a sprite is 21. Sprite column 5 is the left edge of the
 * window, so the sprites go five pixels left of it.
 */
#define WIN_SX  (SLOT_X - 5)     /* sprite column 6 lands inside the frame */
#define HEAD_SX SLOT_X           /* the pad sits centred in the window */
#define WIN_SY1 SLOT_Y1
#define WIN_SY2 ((unsigned char)(SLOT_Y1 + 21))

static void draw_disc(void)
{
    /* All of it in one ink. The hub used to be grey against cyan rings,
     * and because a cell takes whichever ink was written to it last, the
     * first ring to pass through a hub cell turned half the hub cyan and
     * left the rest grey. That blotching was the colour bug. One ink for
     * the whole disc cannot do it, and it leaves disc_colour free to take
     * the disc through yellow, green or red in one piece.
     */
    ui_ink(UI_CYAN);
    ui_ring(DISC_CX, DISC_CY, (unsigned char)(DISC_RY + 4));
    ui_ring(DISC_CX, DISC_CY, DISC_RMIN);
    ui_ring(DISC_CX, DISC_CY, (unsigned char)(DISC_RMIN - 5));

    /* The frame of the window, once. What is inside it is two black
     * sprites, which is what cuts the hole, so none of this has to be
     * drawn again when a ring lands on it.
     */
    draw_slot();
    ui_sprite(1, WIN_SX, WIN_SY1);
    ui_sprite(2, WIN_SX, WIN_SY2);
}


/* The key row, drawn as reverse video buttons across the full width.
 *
 * Eight keys in two rows of four. Each label is exactly eight characters,
 * so the buttons line up on a grid of 8, 88, 168 and 248 with two cells of
 * air between them, and every one gets its own ink. Reverse video keeps a
 * button inside its own cells, which is what makes eight colours along the
 * bottom of a one ink per cell screen possible at all.
 *
 * Two of them carry state rather than a fixed label: F2 says which kind of
 * format will run and F4 says whether the drive noise is on, so there is
 * no separate field on the screen for either.
 */
static void button(unsigned int x, unsigned char y, const char *s,
                   unsigned char colour)
{
    ui_ink(colour);
    ui_reverse(1);
    ui_text(x, y, s);
    ui_reverse(0);
}

/* F3's label when it is showing a figure: "F3 n.nS ", eight cells like
 * every other button, with the two digits written in place.
 */
static char f3[9] = "F3 0.0S ";

static void draw_keys(void)
{
    button(8,   KEYS_Y, "F1 DRIVE", UI_CYAN);
    button(88,  KEYS_Y, mode == MODE_QUICK ? "F2 QUICK" : "F2 SLOW ",
           mode == MODE_QUICK ? UI_LGREEN : UI_YELLOW);
    if (dos_drive_type == DRV_1541 && mode == MODE_SURFACE) {
        unsigned int tenths = fmt_track_wait / 6u;

        f3[3] = (char)('0' + (tenths / 10u) % 10u);
        f3[5] = (char)('0' + tenths % 10u);
        button(168, KEYS_Y, f3, UI_YELLOW);
    } else {
        button(168, KEYS_Y, "F3 WAIT ", UI_GREY);
    }
    button(248, KEYS_Y, snd_enabled() ? "F4 SOUND" : "F4 QUIET",
           snd_enabled() ? UI_LBLUE : UI_DGREY);

    button(8,   KEYS2_Y, "F5 NAME ", UI_WHITE);
    button(88,  KEYS2_Y, "F6 ID   ", UI_LRED);
    button(168, KEYS2_Y, "F7 GO   ", UI_LGREEN);
    button(248, KEYS2_Y, "F8 EXIT ", UI_DGREY);
}

/* The logo cycles its colours. It is nine cells on cell row 1, and this is
 * the one thing on the screen that moves on its own, so it runs from the
 * interrupt: nine stores into the screen matrix every fourth frame, which
 * is fifteen steps a second and costs nothing measurable. Nothing else
 * writes those cells after start up, so there is nobody to collide with.
 */
#define SCREENM  ((volatile unsigned char *)0x5c00)   /* the VIC matrix */
#define LOGO_CX  2
#define LOGO_CY  1
#define LOGO_W   9

static const unsigned char logo_pal[8] = {
    UI_WHITE, UI_CYAN, UI_LBLUE, 6u, 6u, UI_LBLUE, UI_CYAN, UI_LGREY
};

static unsigned char logo_phase;
static unsigned char logo_div;

static void logo_tick(void)
{
    unsigned char i;

    if (++logo_div < 4) {
        return;
    }
    logo_div = 0;
    ++logo_phase;

    for (i = 0; i < LOGO_W; ++i) {
        SCREENM[LOGO_CY * 40 + LOGO_CX + i] =
            (unsigned char)(logo_pal[(unsigned char)(i + logo_phase) & 7] << 4);
    }
}

/* The interrupt. The KERNAL's own handler still runs after this one, which
 * is what keeps the keyboard and the jiffy clock alive, so it reports the
 * interrupt as not handled.
 */
/* The interrupt handler's own stack, in the free RAM above the sector
 * buffer rather than in BSS. The program is within twenty bytes of its
 * ceiling; anything that does not have to be down there should not be.
 */
#define irq_stack  ((unsigned char *)0xc900)
#define IRQ_STACK_SIZE 128u

static unsigned char irq_tick(void)
{
    snd_tick();
    logo_tick();
    return IRQ_NOT_HANDLED;
}

/* The interrupt comes off while the drive is being driven.
 *
 * Serial on a C64 is bit banged by the KERNAL against the drive's own
 * timing, and it only protects the parts of that it knows about. An extra
 * handler on the vector adds latency to every interrupt in the middle of
 * it, which an emulator forgives and a real drive does not: both hardware
 * hangs in a track at a time pass happened on builds that had this
 * running, and the version before it had none and formatted through.
 *
 * Nothing is lost by taking it off. The tune is finished before the format
 * starts, by the time the format is running there is no music to advance,
 * and the logo not cycling for a minute is not a feature anybody will
 * miss.
 */
static unsigned char irq_live;

static void irq_pause(void)
{
    if (irq_live) {
        reset_irq();
        irq_live = 0;
    }
}

static void irq_resume(void)
{
    if (!irq_live) {
        set_irq(irq_tick, irq_stack, IRQ_STACK_SIZE);
        irq_live = 1;
    }
}

/* The splash: the card, laid out the way the card is.
 *
 * Not the artwork converted. A full screen picture is eight thousand bytes
 * of bitmap and a thousand of colour, and there is nowhere in a single PRG
 * to keep that. This is the same composition drawn with the primitives the
 * program already has, and it follows the card piece for piece: the blue
 * banner with the rainbow flash, the logo plate across the width, the
 * subtitle, the yellow sticker, the platter off to the right with the arm
 * coming in off the edge of the screen and the gouge torn across it, the
 * floor receding behind, and the specifications along the bottom.
 *
 * The gouge is drawn into the bitmap rather than as a sprite on purpose.
 * One ink per cell means the cells it crosses go with it, taking the rings
 * in them along, and that is exactly what a scratch through a platter
 * looks like.
 *
 * Held for about two and a half seconds, or until a key.
 */
#ifndef SPLASH_FRAMES
#define SPLASH_FRAMES 150u
#endif

static void splash(void)
{
    unsigned char i;
    unsigned char n;

    ui_ink(UI_BLACK);
    ui_clear();

    /* --- the wordmark ------------------------------------------------ */
    ui_ink(UI_WHITE);
    ui_text_big(52, 16, "HEADCRASH", 3);
    ui_ink(UI_YELLOW);
    ui_text_big(72, 48, "FORMAT UTIL", 2);

    /* --- the floor, receding to a vanishing point -------------------- */
    ui_ink(UI_GREY);
    for (i = 0; i < 9; ++i) {
        signed char   dx = (signed char)(((signed char)i - 4) * 10);
        unsigned char j;

        unsigned int px = 160;

        for (j = 0; j < 28; ++j) {
            unsigned int cx = (unsigned int)(160 + (int)dx * (int)j / 8);

            /* Joined, not plotted. A line this shallow moves several
             * pixels across for every row down at the near end, and
             * plotting the points on their own leaves it dotted.
             */
            if (cx > px) {
                ui_hline(px, cx, (unsigned char)(140 + j));
            } else {
                ui_hline(cx, px, (unsigned char)(140 + j));
            }
            px = cx;
        }
    }
    ui_hline(0, 319, 142);
    ui_hline(0, 319, 145);
    ui_hline(0, 319, 149);
    ui_hline(0, 319, 154);
    ui_hline(0, 319, 160);
    ui_hline(0, 319, 167);

    /* --- the platter, the arm, and the gouge -------------------------- */
    ui_ink(UI_CYAN);
    for (i = 0; i < 6; ++i) {
        ui_ring(236, 104, (unsigned char)(34 - i * 6));
    }

    ui_ink(UI_LGREY);
    ui_fill(288, 90, 32, 3);
    ui_fill(276, 88, 12, 7);

    /* The gouge. Two pixels deep, because one disappears against the
     * rings it is supposed to be tearing through.
     */
    ui_ink(UI_LRED);
    for (i = 0; i < 36; ++i) {
        ui_hline(274u - (unsigned int)i * 2u, 275u - (unsigned int)i * 2u,
                 (unsigned char)(94 + i / 2));
        ui_hline(274u - (unsigned int)i * 2u, 275u - (unsigned int)i * 2u,
                 (unsigned char)(95 + i / 2));
    }

    /* --- the sticker, and the specifications ------------------------- */
    ui_ink(UI_YELLOW);
    ui_reverse(1);
    ui_text(8, 96, " QUICK AND SLOW MODES ");
    ui_reverse(0);

    ui_ink(UI_WHITE);
    ui_text(8, 176, "DISK FORMATTER . 1541 AND 1581");
    ui_ink(UI_GREY);
    ui_text(8, 184, "(C) 2026 ROBERT MECH . MIT LICENCE");
    ui_ink(UI_YELLOW);
    ui_reverse(1);
    ui_text(264, 176, " ");
    ui_text(272, 176, VERSION);
    ui_reverse(0);

    /* Drain whatever is still in the keyboard buffer. Loading the program
     * leaves the RUN that started it in there, and without this the splash
     * reads that as the key that dismisses it and is gone before anyone
     * sees it.
     */
    while (cbm_k_getin() != 0) {
    }

    for (n = 0; n < SPLASH_FRAMES; ++n) {
        if (cbm_k_getin() != 0) {
            break;
        }
        dos_delay_long(1);
    }
}

static void draw_static(void)
{
    ui_ink(UI_CYAN);
    ui_clear();

    ui_ink(UI_LBLUE);
    ui_hline(4, 315, TITLE_Y);
    ui_hline(4, 315, 19);
    ui_vline(4, TITLE_Y, 19);
    ui_vline(315, TITLE_Y, 19);
    ui_text(16, TITLE_TY, "HEADCRASH");
    ui_ink(UI_CYAN);
    ui_text(104, TITLE_TY, "FORMAT UTIL");
    ui_ink(UI_DGREY);
    ui_text(280, TITLE_TY, VERSION);

    draw_drive();

    ui_ink(UI_GREY);
    ui_text(FIELD_X, (unsigned char)(FIELD_Y + 0 * FIELD_STEP), "NAME");
    ui_text(FIELD_X, (unsigned char)(FIELD_Y + 1 * FIELD_STEP), "ID");

    draw_disc();

    ui_ink(UI_CYAN);
    ui_box(BAR_X, (unsigned char)(BAR_Y - 2), BAR_W, 12);

    ui_ink(UI_GREY);
    draw_keys();
    ui_ink(UI_DGREY);
}

/* ---------------------------------------------------------------------- */
/* live fields                                                             */
/* ---------------------------------------------------------------------- */

/* The 1541 wait, in tenths of a second. See docs/DESIGN.md for why there
 * is a wait at all.
 */
/* The 1541 track wait used to be printed beside the device number, which
 * put thirteen cells of padding straight through the left of the disc and
 * wiped eight pixel rows of it every time the screen was refreshed. That
 * was the gap in the circle, and no amount of work on the ring was ever
 * going to close it. It lives on the F3 button now, where it has cells of
 * its own and nothing to collide with.
 */
static void show_wait(void)
{
    draw_keys();
}

static void show_device(void)
{
    show_seg_device();
    show_drive_state(0);
}

static void show_dos(void)
{
    ui_ink(dos_drive_type == DRV_UNKNOWN ? UI_LRED : UI_LGREY);
    ui_text_pad(8, DOS_Y, dos_type_name(), 18);
}

static void show_name(void)
{
    ui_ink(UI_WHITE);
    ui_text_pad(VAL_X, (unsigned char)(FIELD_Y + 0 * FIELD_STEP),
                disk_name, 16);
    {
        char idbuf[3];
        idbuf[0] = (char)id1;
        idbuf[1] = (char)id2;
        idbuf[2] = 0;
        ui_text(VAL_X, (unsigned char)(FIELD_Y + 1 * FIELD_STEP), idbuf);
    }
}

/* The message area is two rows of the full width, and the copyright lives
 * in it: it sits right aligned on the second row whenever a message does
 * not need that row, and a message that does need it simply writes over
 * the top. It comes back on the next message that fits on one row.
 *
 * Text goes in at 2400 baud, because that is what the screen is pretending
 * to be. 2400 baud with start and stop bits is 240 characters a second,
 * which at sixty frames a second is four characters a frame, so the budget
 * is spent four at a time and then the code waits out a frame on the jiffy
 * clock. The longest message in the program is 66 characters, so the worst
 * case is about a third of a second. The wait is bounded in case something
 * has left interrupts off and the clock is not running.
 */
#define BAUD_CHARS 4u
#define JIFFY      (*(volatile unsigned char *)0xa2)

static char mbuf[MSG_W];
static unsigned char baud_budget;

static void baud_wait(void)
{
    unsigned char t = JIFFY;
    unsigned int  guard = 0;

    while (JIFFY == t && ++guard < 20000u) {
    }
}

/* Clear the row, then lay n characters of s across it one at a time. */
static void type_row(unsigned char y, const char *s, unsigned char n)
{
    unsigned char i;

    ui_text_pad(8, y, "", MSG_W);
    for (i = 0; i < n; ++i) {
        ui_char(8u + (unsigned int)i * 8u, y, s[i]);
        if (--baud_budget == 0) {
            baud_budget = BAUD_CHARS;
            baud_wait();
        }
    }
}

static void msg(const char *s, unsigned char colour)
{
    unsigned char n = 0;
    unsigned char brk, i;

    while (s[n] != 0) {
        ++n;
    }

    ui_ink(colour);
    baud_budget = BAUD_CHARS;

    if (n <= MSG_W) {
        type_row(MSG_Y, s, n);
        ui_ink(UI_DGREY);
        ui_text_pad(8, (unsigned char)(MSG_Y + 8), "", MSG_W);
        ui_text(COPY_X, (unsigned char)(MSG_Y + 8), "(C) 2026 ROBERT MECH");
        return;
    }

    /* Break at the last space that fits on the first row. */
    brk = (unsigned char)MSG_W;
    for (i = (unsigned char)MSG_W; i > 0; --i) {
        if (s[i] == 0x20) {
            brk = i;
            break;
        }
    }
    for (i = 0; i < brk; ++i) {
        mbuf[i] = s[i];
    }
    type_row(MSG_Y, mbuf, brk);

    while (s[brk] == 0x20) {
        ++brk;
    }
    n = (unsigned char)(n - brk);
    if (n > MSG_W) {
        n = (unsigned char)MSG_W;
    }
    type_row((unsigned char)(MSG_Y + 8), s + brk, n);
}

/* The whole disc goes over to one colour, which is how the state of the
 * disk is shown once the tracks are down: yellow while it is being
 * checked, green when it came back good, red when it did not. Colour only,
 * so it costs one store a cell and redraws nothing.
 */
static void disc_colour(unsigned char colour)
{
    ui_cell_colour(23, 3, 17, 13, colour);
}

/* QUICK mode has nothing to count. The drive runs its own format as one
 * job and reports nothing until it is finished: a 1541 goes deaf for the
 * duration, and a 1581, which does answer the bus, holds its cylinder
 * counter at 79 from the moment the job starts, so there is no cylinder to
 * read. Measured, not assumed: tests/t_poll.c polls $0088 four thousand
 * times during an N: and gets one value.
 *
 * So the bar does not pretend. It sweeps to say work is happening, the
 * track counter shows dashes rather than a number nobody knows, and the
 * disc fills as the shape of the pass. Only SURFACE mode, which issues one
 * job per track, has a real figure to draw, and there it draws one.
 */
static void show_sweep(unsigned char phase)
{
    ui_cell_blank(2, BAR_Y / 8, 36, 1);
    ui_ink(UI_CYAN);
    ui_bar(16u + ((unsigned int)(phase & 31u) * 8u), BAR_Y, 32);
}

static void show_track_unknown(unsigned char total)
{
    ui_ink(UI_GREY);
    ui_text(VAL_X, INFO_Y, "--");
    ui_ink(UI_WHITE);
    ui_num(VAL_X + 24, INFO_Y, total, 2);
    ui_ink(UI_DGREY);
    ui_num(VAL_X, (unsigned char)(INFO_Y + INFO_STEP), bad_count, 2);
    ui_num(VAL_X, (unsigned char)(INFO_Y + 2 * INFO_STEP), 0, 1);
}

/* Back to an empty disc. The rings and the colour from the last run were
 * both still there when the next one started, so a second format began
 * with a full green disc and filled it again underneath. Pixels and ink
 * both have to go.
 */
static void reset_disc(void)
{
    ui_cell_blank(23, 3, 17, 13);
    disc_colour(UI_CYAN);
    draw_disc();
    last_ring = 0;
}

static void show_bar(unsigned char done, unsigned char total)
{
    unsigned int w;

    if (total == 0) {
        return;
    }
    w = ((unsigned int)(BAR_W - 8) * done) / total;
    ui_ink(UI_CYAN);
    ui_bar(BAR_X + 4, BAR_Y, w);
}

/* One ring per track, laid from the outside in, so the disc fills up as
 * the pass goes on. With eighty tracks over the radius available some
 * tracks share a ring; the counter is the exact figure, the disc is the
 * shape of it. Rings that would land on one already drawn are skipped,
 * which is most of the redraw cost gone.
 */
static unsigned char ring_radius(unsigned char track, unsigned char total)
{
    unsigned int  span = DISC_RY - DISC_RMIN - 3;
    unsigned char r    = (unsigned char)(DISC_RY - 1 -
                                         (span * (unsigned int)(track - 1))
                                         / total);

    /* Snapped to every second radius, which is what takes the moire off
     * the disc. Eighty tracks over thirty one pixels of radius puts a ring
     * on every single one, and because the horizontal radius is stretched
     * by a fifth those rings land a fifth of a pixel apart in x: they
     * overlap, then clear, then overlap, and the interference shows up as
     * banding across the whole disc, worse on a television than on an
     * emulator. Two pixels apart leaves a clear gap between every ring and
     * the pattern goes away. The disc reads as a record rather than as a
     * grey wash, and half as many rings is half the drawing.
     */
    return (unsigned char)(r & 0xfe);
}

static void show_ring(unsigned char track, unsigned char total,
                      unsigned char colour)
{
    unsigned char ry;
    unsigned int  rx;

    if (total == 0) {
        return;
    }
    ry = ring_radius(track, total);
    if (ry == last_ring && colour == UI_CYAN) {
        return;                 /* same ring as the last track */
    }
    last_ring = ry;
    rx = ((unsigned int)ry * 6u) / 5u;
    ui_ink(colour);
    ui_ring(DISC_CX, DISC_CY, ry);

    /* The head rides the window, on the ring it is writing: straight down
     * from the middle of the disc, the ring at radius ry crosses the
     * window at DISC_CY + ry, so that is simply where the carriage goes.
     * It walks up the window towards the hub as the pass goes on.
     */
    {
        unsigned char hy = (unsigned char)(DISC_CY + ry);

        if (hy > SLOT_Y2) {
            hy = SLOT_Y2;
        } else if (hy < SLOT_Y1) {
            hy = SLOT_Y1;
        }
        ui_sprite(0, HEAD_SX, (unsigned char)(hy - 10));
    }
    (void)rx;
}

static void show_track(unsigned char track, unsigned char total,
                       unsigned char retry)
{
    ui_ink(UI_WHITE);
    ui_num(VAL_X, INFO_Y, track, 2);
    ui_num(VAL_X + 24, INFO_Y, total, 2);
    ui_ink(bad_count ? UI_LRED : UI_DGREY);
    ui_num(VAL_X, (unsigned char)(INFO_Y + INFO_STEP), bad_count, 2);
    ui_ink(retry ? UI_LRED : UI_DGREY);
    ui_num(VAL_X, (unsigned char)(INFO_Y + 2 * INFO_STEP), retry, 1);
}

static void show_track_labels(void)
{
    ui_ink(UI_GREY);
    ui_text(FIELD_X, INFO_Y, "TRACK");
    ui_text(VAL_X + 16, INFO_Y, "/");
    ui_ink(UI_DGREY);
    ui_text(FIELD_X, (unsigned char)(INFO_Y + INFO_STEP), "BAD");
    ui_text(FIELD_X, (unsigned char)(INFO_Y + 2 * INFO_STEP), "RETRY");
}

/* ---------------------------------------------------------------------- */
/* actions                                                                 */
/* ---------------------------------------------------------------------- */

static void probe(void)
{
    unsigned char answered;
    unsigned char was = irq_live;

    msg("LOOKING FOR THE DRIVE", UI_GREY);
    show_seg_device();
    irq_pause();
    answered = 0;
    if (!dos_present(dev)) {
        dos_close();
        dos_drive_type = DRV_UNKNOWN;
        dos_tracks     = 0;
    } else if (!dos_open(dev)) {
        dos_drive_type = DRV_UNKNOWN;
        dos_tracks     = 0;
    } else {
        answered = 1;
        dos_identify();
    }
    if (was) {
        irq_resume();
    }
    ui_head_shape((unsigned char)(dos_drive_type != DRV_1581));
    draw_drive();
    show_device();
    show_dos();
    show_name();
    draw_keys();
    show_wait();
    if (dos_drive_type != DRV_UNKNOWN) {
        msg("READY", UI_LGREEN);
    } else if (answered) {
        msg("SOMETHING ANSWERED AT THIS DEVICE NUMBER, "
            "BUT IT IS NOT A 1541 OR A 1581", UI_LRED);
    } else {
        msg("NOTHING ANSWERED AT THIS DEVICE NUMBER. "
            "CHECK THE DRIVE IS SWITCHED ON AND CABLED", UI_LRED);
    }
}

/* Type over the disk name in place. Letters and digits only, stored as the
 * unshifted PETSCII the DOS expects.
 */
/* Two characters, which is all a disk ID is. Letters and digits only,
 * unshifted, for the same reason the disk name is: a shifted PETSCII code
 * lands on the disk as a graphics character.
 */
static void edit_id(void)
{
    unsigned char n = 0;
    unsigned char c;

    msg("TYPE TWO CHARACTERS FOR THE ID", UI_YELLOW);
    for (;;) {
        c = cbm_k_getin();
        if (c == 0) {
            continue;
        }
        if (c == 13) {
            break;
        }
        if (c == 20 && n > 0) {         /* delete */
            --n;
        } else if (n < 2 &&
                   ((c >= 0x41 && c <= 0x5a) || (c >= 0xc1 && c <= 0xda) ||
                    (c >= 0x30 && c <= 0x39))) {
            if (c >= 0xc1) {
                c = (unsigned char)(c - 0x80);
            }
            if (n == 0) {
                id1 = c;
            } else {
                id2 = c;
            }
            ++n;
        }
        show_name();
    }
    show_name();
    msg("READY", UI_LGREEN);
}

static void edit_name(void)
{
    unsigned char n = (unsigned char)strlen(disk_name);
    unsigned char c;

    msg("TYPE A NAME, RETURN WHEN DONE", UI_YELLOW);
    for (;;) {
        c = cbm_k_getin();
        if (c == 0) {
            continue;
        }
        if (c == 13) {
            break;
        }
        if (c == 20 && n > 0) { /* delete */
            disk_name[--n] = 0;
        } else if (n < 16 &&
                   ((c >= 0x41 && c <= 0x5a) || (c >= 0xc1 && c <= 0xda) ||
                    (c >= 0x30 && c <= 0x39) || c == 0x20)) {
            if (c >= 0xc1) {
                c = (unsigned char)(c - 0x80);
            }
            disk_name[n++] = (char)c;
            disk_name[n] = 0;
        }
        show_name();
    }
    if (n == 0) {
        disk_name[0] = 0x44; /* DISK */
        disk_name[1] = 0x49;
        disk_name[2] = 0x53;
        disk_name[3] = 0x4b;
        disk_name[4] = 0;
    }
    show_name();
    msg("READY", UI_LGREEN);
}

static unsigned char confirm(void)
{
    unsigned char c;

    msg("THIS ERASES THE DISK. Y TO GO ON", UI_YELLOW);
    for (;;) {
        c = cbm_k_getin();
        if (c == 0xd9 || c == 0x59) {
            return 1;
        }
        if (c != 0) {
            return 0;
        }
    }
}

/* The drive's own format: one job, as fast as the hardware goes, and
 * nothing to watch while it runs. The bar moves on a clock rather than on
 * news from the drive, and says so, because the alternative is a bar that
 * sits still for a minute and a half.
 */
/* The keys that still answer while a format is running.
 *
 * F4 is the one that matters. A noise you cannot stop once the thing has
 * started is worse than no noise at all, and neither format loop read the
 * keyboard before. Returns true for RUN/STOP, which only SURFACE mode can
 * act on: a QUICK format is one job inside the drive and stopping the C64
 * end of it would leave the disk half written with nothing to show for it.
 */
static unsigned char poll_keys(void)
{
    unsigned char c = cbm_k_getin();

    if (c == 138) {                     /* F4 */
        snd_enable((unsigned char)(snd_enabled() ? 0 : 1));
        draw_keys();
        return 0;
    }
    return (unsigned char)(c == 3);     /* RUN/STOP */
}

/* Let the opening tune finish before the drive is spoken to.
 *
 * The KERNAL turns interrupts off around every byte it puts on the serial
 * bus, and a format is nothing but serial traffic, so once it starts the
 * player hardly gets a look in and the tune comes out in pieces. There is
 * no clever fix: either the music waits for the drive or the drive waits
 * for the music, and two seconds of waiting is cheaper than a tune in
 * pieces. F4 still cuts it short.
 */
static void wait_for_tune(void)
{
    unsigned char guard;

    for (guard = 0; guard < 200 && snd_busy(); ++guard) {
        dos_delay_long(2);
        (void)poll_keys();
    }
}

static void run_quick(void)
{
    unsigned char total = dos_tracks;
    unsigned int  step  = fmt_native_frames() / total;
    unsigned char t;
    unsigned char st;

    bad_count = 0;
    reset_disc();

    snd_play(SND_START);
    msg("FORMATTING. THE DRIVE REPORTS NOTHING UNTIL DONE", UI_CYAN);
    wait_for_tune();
    irq_pause();
    fmt_native_start(disk_name, id1, id2);

    /* One step of the bar per track's worth of time. The drive is not
     * telling us anything, so this is a clock, not a report.
     */
    for (t = 1; t <= total; ++t) {
        led((unsigned char)(t & 1));    /* the drive is working throughout */
        dos_delay_long(step);
        snd_click();
        show_ring(t, total, UI_CYAN);
        show_sweep(t);
        show_track_unknown(total);
        (void)poll_keys();
    }

    show_ring(total, total, UI_CYAN);
    show_bar(total, total);
    show_track(total, total, 0);
    ui_sprite_off(0);

    /* Now ask the drive whether it finished. It answers nothing until it
     * has, so this is a wait, but it is a wait with the screen still
     * moving and the keyboard still read: the bar sweeps, the count of
     * seconds is on screen, and F4 still works. Ninety seconds is the cap,
     * after which it says so rather than sitting there.
     */
    msg("CHECKING THE DRIVE FINISHED", UI_CYAN);
    disc_colour(UI_YELLOW);
    led(1);
    for (t = 0; t < 90; ++t) {
        dos_delay_long(60);
        show_sweep(t);
        ui_ink(UI_GREY);
        ui_num(VAL_X, INFO_Y, t, 2);
        (void)poll_keys();
        st = fmt_native_poll();
        if (st != DOS_ERR_TIMEOUT) {
            break;
        }
    }
    led(0);

    if (t >= 90) {
        disc_colour(UI_LRED);
        irq_resume();
        msg("THE DRIVE NEVER ANSWERED. IT MAY STILL BE WORKING, "
            "OR THE DISK MAY BE UNREADABLE", UI_LRED);
        return;
    }

    show_bar(total, total);
    show_track(total, total, 0);
    disc_colour(st ? UI_LRED : UI_LGREEN);
    if (st != 0) {
        irq_resume();
        msg("THE DRIVE REPORTED A PROBLEM", UI_LRED);
        ui_ink(UI_LRED);
        ui_text_pad(8, (unsigned char)(MSG_Y + 8), dos_msg, 38);
        return;
    }
    irq_resume();
    snd_play(SND_DONE);
    msg("DONE", UI_LGREEN);
}

static void run_format(void)
{
    unsigned char t, st, retry;
    unsigned char total = dos_tracks;

    bad_count = 0;
    reset_disc();

    snd_play(SND_START);
    msg("PREPARING THE DRIVE", UI_CYAN);
    wait_for_tune();
    irq_pause();
    if (!fmt_begin(id1, id2)) {
        irq_resume();
        msg("THIS DRIVE CANNOT BE DRIVEN TRACK BY TRACK", UI_LRED);
        return;
    }

    msg("FORMATTING", UI_CYAN);
    for (t = 1; t <= total; ++t) {
        retry = 0;
        for (;;) {
            led(1);
            st = fmt_track(t);
            led(0);
            if (st == 1) {
                break;
            }
            if (++retry >= MAX_RETRY) {
                if (bad_count < MAX_BAD) {
                    bad_list[bad_count] = t;
                }
                ++bad_count;
                last_ring = 0;          /* let the bad ring be drawn */
                show_ring(t, total, UI_RED);
                break;
            }
            show_track(t, total, retry);
            msg("RETRYING", UI_LRED);
        }
        if (st == 1) {
            if (retry) {
                msg("FORMATTING", UI_CYAN);
            }
            show_ring(t, total, UI_CYAN);
        }
        snd_click();
        show_bar(t, total);
        show_track(t, total, retry);

        if (poll_keys()) {      /* RUN/STOP */
            ui_sprite_off(0);
            irq_resume();
            msg("STOPPED. THE DISK IS NOT USABLE", UI_LRED);
            fmt_end();
            return;
        }
    }
    fmt_end();
    ui_sprite_off(0);

    /* The directory. On a 1581 this formats the whole surface a second
     * time, which is why it gets the same treatment as the check in QUICK
     * mode: the drive is left alone for as long as it needs to be, then
     * asked once a second, with the bar sweeping and the keyboard read
     * throughout. The old version sat blind for seventy seconds and then
     * spoke to the drive whether it was ready or not.
     */
    msg("WRITING THE DIRECTORY", UI_CYAN);
    disc_colour(UI_YELLOW);
    led(1);
    fmt_fs_start(disk_name, id1, id2);

    {
        unsigned int settle = fmt_fs_settle();

        for (t = 0; settle != 0; ++t) {
            unsigned int step = (settle > 30u) ? 30u : settle;

            dos_delay_long(step);
            settle -= step;
            show_sweep(t);
            (void)poll_keys();
        }
    }

    for (t = 0; t < 120; ++t) {
        st = fmt_fs_poll();
        if (st != DOS_ERR_TIMEOUT) {
            break;
        }
        dos_delay_long(60);
        show_sweep((unsigned char)(t + 7));
        (void)poll_keys();
    }
    led(0);

    if (t >= 120) {
        disc_colour(UI_LRED);
        irq_resume();
        msg("THE DRIVE NEVER FINISHED THE DIRECTORY", UI_LRED);
        return;
    }

    show_bar(total, total);
    disc_colour(st ? UI_LRED : (bad_count ? UI_YELLOW : UI_LGREEN));
    if (st != 0) {
        irq_resume();
        msg("THE DIRECTORY DID NOT TAKE", UI_LRED);
        return;
    }

    if (bad_count) {
        fmt_lock_out(bad_list, (unsigned char)(bad_count > MAX_BAD
                                               ? MAX_BAD : bad_count));
        irq_resume();
        snd_play(SND_DONE);
        msg("DONE, WITH BAD TRACKS LOCKED OUT", UI_YELLOW);
    } else {
        irq_resume();
        snd_play(SND_DONE);
        msg("DONE", UI_LGREEN);
    }
}

/* ---------------------------------------------------------------------- */

int main(void)
{
    unsigned char c;

    ui_init();
    snd_init();
    splash();
    draw_static();
    irq_resume();
    show_track_labels();
    probe();
    show_track(0, dos_tracks, 0);

#ifdef AUTORUN
    /* The harness runs under an emulator, where a 1541 writes a track in
     * about 156 frames. The shipped wait is six seconds because a real
     * drive has a motor; holding the harness to that makes a 35 track pass
     * take longer in wall time than it is worth. Build with
     * -DAUTORUN_WAIT=n to shorten it. Nothing else reads this.
     */
#ifdef AUTORUN_WAIT
    fmt_track_wait = AUTORUN_WAIT;
#endif
#ifdef AUTORUN_TRACKS
    dos_tracks = AUTORUN_TRACKS;   /* short sweeps */
#endif
    /* Built for the test harness: format without waiting to be asked, so a
     * run can be driven from the command line. AUTORUN_SURFACE picks the
     * track at a time path instead of the drive's own.
     */
    if (dos_drive_type != DRV_UNKNOWN) {
#ifdef AUTORUN_SURFACE
        mode = MODE_SURFACE;
        draw_keys();
        run_format();
#else
        run_quick();
#endif
    }
    for (;;) { }
#endif

    for (;;) {
        c = cbm_k_getin();
        switch (c) {
            case 133: /* F1 */
                dos_close();
                dev = (unsigned char)(dev >= 11 ? 8 : dev + 1);
                probe();
                show_track(0, dos_tracks, 0);
                break;
            case 134: /* F3: how long to allow a 1541 track */
                if (dos_drive_type != DRV_1541) {
                    msg("F3 SETS HOW LONG A 1541 TRACK MAY TAKE. "
                        "THIS DRIVE ANSWERS FOR ITSELF AND DOES NOT NEED IT",
                        UI_GREY);
                    break;
                }
                if (mode != MODE_SURFACE) {
                    msg("F3 ONLY APPLIES IN SLOW MODE, WHERE TRACKS GO "
                        "ONE AT A TIME. PRESS F2 FOR SLOW FIRST", UI_YELLOW);
                    break;
                }
                fmt_track_wait += 60u;
                if (fmt_track_wait > 900u) {
                    fmt_track_wait = 60u;
                }
                show_wait();
                msg("LOWER IS QUICKER AND TOO LOW HANGS THE MACHINE. "
                    "THE FIGURE IS ON THE BUTTON", UI_YELLOW);
                break;
            case 138: /* F4: the drive noise */
                snd_enable((unsigned char)(snd_enabled() ? 0 : 1));
                draw_keys();
                msg(snd_enabled() ? "DRIVE NOISE ON" : "DRIVE NOISE OFF",
                    UI_GREY);
                break;
            case 135: /* F5 */
                edit_name();
                break;
            case 139: /* F6 */
                edit_id();
                break;
            case 137: /* F2: which kind of format */
                mode = (unsigned char)(mode == MODE_QUICK ? MODE_SURFACE
                                                          : MODE_QUICK);
                draw_keys();
                msg(mode == MODE_QUICK
                    ? "QUICK: THE DRIVE FORMATS THE WHOLE DISK ITSELF, NO BAD TRACK CHECK"
                    : "SLOW: ONE TRACK AT A TIME, FINDS BAD TRACKS AND LOCKS THEM OUT",
                    UI_YELLOW);
                break;
            case 136: /* F7 */
                if (dos_drive_type == DRV_UNKNOWN) {
                    msg("NOTHING HERE TO FORMAT", UI_LRED);
                } else if (confirm()) {
                    if (mode == MODE_QUICK) {
                        run_quick();
                    } else {
                        run_format();
                    }
                } else {
                    msg("LEFT ALONE", UI_GREY);
                }
                break;
            case 140: /* F8: back to BASIC, screen as we found it */
                reset_irq();
                snd_hush();
                snd_enable(0);
                dos_close();
                ui_done();
                return 0;
            default:
                break;
        }
    }
    return 0;
}
