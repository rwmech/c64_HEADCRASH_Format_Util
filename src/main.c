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
#include "snd.h"
#include "dos.h"
#include "fmt.h"

#define VERSION "V1.1"

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

/* Two drive bodies, stacked, drawn once. Their cells are theirs alone so
 * that selection and the activity light are a matter of recolouring.
 */
#define DRV_X      8
#define DRV_W      144
#define DRV_H      28
#define DRV1_Y     24           /* 1581, cell rows 3..6  */
#define DRV2_Y     64           /* 1541, cell rows 8..11 */
#define DRV_CX     1            /* in cells */
#define DRV_CW     18
#define DRV1_CY    3
#define DRV2_CY    8
#define DRV_CH     4
#define LED_CX     3            /* the activity light, its own cell */
#define LED_OFFY   8

/* The device number, as a red seven segment readout. */
#define SEG_X      164
#define SEG_Y      32
#define SEG_CX     20
#define SEG_CY     4
#define SEG_CW     5
#define SEG_CH     3

#define DISC_CX    254
#define DISC_CY    76
#define DISC_RX    54
#define DISC_RY    45
#define DISC_RMIN  11

#define DOS_Y      104
#define FIELD_X    8
#define FIELD_Y    120
#define FIELD_STEP 16
#define VAL_X      72
#define BAR_X      8
#define BAR_Y      152
#define BAR_W      304
#define STAT_Y     136          /* shares the ID row: 00/80 is the total */
#define MSG_Y      176
#define KEYS_Y     184          /* two rows of buttons, four each */
#define KEYS2_Y    192
#define FOOT_Y     96           /* the clear band under the drives */

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

/* The two drives, drawn from what their front panels actually look like
 * rather than from a generic box. What separates them at this size is not
 * the case, which is a wide low box on both, but the hole in it:
 *
 * 1541: a 5.25 inch slot is 146 mm across a 200 mm front, so it takes up
 * most of the width and is tall enough to see. The spring loaded latch
 * stands proud of it in the middle, which is the feature nobody mistakes
 * for anything else. Two lights at the left, green for power and red for
 * activity; the 1541-II has them the other way round.
 *
 * 1581: a 3.5 inch mechanism behind the same width of case, so the slot is
 * 90 mm in a 200 mm front and sits in a recessed bezel with the eject
 * button hard against its right hand end. Small hole, lots of case.
 *
 * Both are drawn once at start up. Selection and activity are colour only.
 */
static void draw_drive_body(unsigned char y, unsigned char kind)
{
    unsigned int x = DRV_X;
    unsigned char ym = (unsigned char)(y + DRV_H / 2);
    unsigned char lip = (unsigned char)(y + DRV_H - 7);
    unsigned char bot = (unsigned char)(y + DRV_H - 1);

    ui_ink(UI_CYAN);

    /* Case. The lower part of both steps out a little, and that lip is
     * most of what gives either drive its shape head on.
     */
    ui_hline(x + 2, x + DRV_W - 3, y);
    ui_vline(x + 2, y, lip);
    ui_vline(x + DRV_W - 3, y, lip);
    ui_hline(x, x + 2, lip);
    ui_hline(x + DRV_W - 3, x + DRV_W - 1, lip);
    ui_vline(x, lip, bot);
    ui_vline(x + DRV_W - 1, lip, bot);
    ui_hline(x, x + DRV_W - 1, bot);

    if (kind == DRV_1541) {
        /* The slot: wide, and deep enough that a disk would go in it. Its
         * top and bottom edges stop either side of the latch, so the latch
         * reads as standing in front of the slot rather than over it.
         */
        ui_hline(x + 34, x + 70, (unsigned char)(ym - 6));
        ui_hline(x + 90, x + 126, (unsigned char)(ym - 6));
        ui_hline(x + 34, x + 70, (unsigned char)(ym + 6));
        ui_hline(x + 90, x + 126, (unsigned char)(ym + 6));
        ui_vline(x + 34, (unsigned char)(ym - 6), (unsigned char)(ym + 6));
        ui_vline(x + 126, (unsigned char)(ym - 6), (unsigned char)(ym + 6));

        /* The latch, proud above and below the slot, with a raised face. */
        ui_box(x + 71, (unsigned char)(ym - 10), 18, 21);
        ui_box(x + 75, (unsigned char)(ym - 6), 10, 13);
    } else {
        /* Recessed bezel, the small slot in it, then the eject button. */
        ui_box(x + 34, (unsigned char)(ym - 9), 96, 19);
        ui_box(x + 44, (unsigned char)(ym - 4), 54, 8);
        ui_hline(x + 47, x + 95, ym);
        ui_box(x + 106, (unsigned char)(ym - 3), 11, 6);
    }

    /* Power light, in the clear strip at the left of the case, with the
     * activity light beside it.
     */
    ui_ink(UI_LGREEN);
    ui_fill(x + 8, (unsigned char)(ym - 2), 5, 5);

    ui_ink(UI_RED);
    ui_fill(DRV_X + (LED_CX - DRV_CX) * 8 + 1, (unsigned char)(ym - 2), 6, 5);
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

static void show_seg_device(void)
{
    ui_cell_blank(SEG_CX, SEG_CY, SEG_CW, SEG_CH);
    ui_ink(UI_LRED);
    draw_seg(SEG_X, SEG_Y, (unsigned char)(dev / 10));
    draw_seg(SEG_X + SEG_W + 3, SEG_Y, (unsigned char)(dev % 10));
}

/* Which drive is selected, and whether its light is on. Both are colour
 * only: the art underneath never changes.
 */
static void show_drive_state(unsigned char busy)
{
    unsigned char sel81 = (unsigned char)(dos_drive_type == DRV_1581);
    unsigned char sel41 = (unsigned char)(dos_drive_type == DRV_1541);

    ui_cell_colour(DRV_CX, DRV1_CY, DRV_CW, DRV_CH,
                   sel81 ? UI_CYAN : UI_DGREY);
    ui_cell_colour(DRV_CX, DRV2_CY, DRV_CW, DRV_CH,
                   sel41 ? UI_CYAN : UI_DGREY);

    ui_cell_colour(LED_CX, (unsigned char)(DRV1_CY + 1), 1, 1,
                   (sel81 && busy) ? UI_LRED : UI_DGREY);
    ui_cell_colour(LED_CX, (unsigned char)(DRV2_CY + 1), 1, 1,
                   (sel41 && busy) ? UI_LRED : UI_DGREY);
}

/* The activity light and the spindle motor are the same fact about the
 * drive, so they are shown and heard from one call.
 */
static void led(unsigned char on)
{
    show_drive_state(on);
    snd_motor(on);
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
    ui_ink(UI_LGREY);
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

static void draw_disc(void)
{
    ui_ink(UI_GREY);
    ui_ring(DISC_CX, DISC_CY, (unsigned char)(DISC_RY + 4));
    ui_ring(DISC_CX, DISC_CY, DISC_RMIN);
    ui_ink(UI_DGREY);
    ui_ring(DISC_CX, DISC_CY, (unsigned char)(DISC_RMIN - 5));
    draw_slot();
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

static void draw_keys(void)
{
    button(8,   KEYS_Y, "F1 DRIVE", UI_CYAN);
    button(88,  KEYS_Y, mode == MODE_QUICK ? "F2 QUICK" : "F2 SLOW ",
           mode == MODE_QUICK ? UI_LGREEN : UI_YELLOW);
    button(168, KEYS_Y, "F3 WAIT ", UI_GREY);
    button(248, KEYS_Y, snd_enabled() ? "F4 SOUND" : "F4 QUIET",
           snd_enabled() ? UI_LBLUE : UI_DGREY);

    button(8,   KEYS2_Y, "F5 NAME ", UI_WHITE);
    button(88,  KEYS2_Y, "F6 ID   ", UI_LRED);
    button(168, KEYS2_Y, "F7 GO   ", UI_LGREEN);
    button(248, KEYS2_Y, "F8 EXIT ", UI_DGREY);
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

    draw_drive_body(DRV1_Y, DRV_1581);
    draw_drive_body(DRV2_Y, DRV_1541);

    ui_ink(UI_GREY);
    ui_text(FIELD_X, (unsigned char)(FIELD_Y + 0 * FIELD_STEP), "NAME");
    ui_text(FIELD_X, (unsigned char)(FIELD_Y + 1 * FIELD_STEP), "ID");

    draw_disc();

    ui_ink(UI_CYAN);
    ui_box(BAR_X, (unsigned char)(BAR_Y - 2), BAR_W, 12);

    ui_ink(UI_GREY);
    draw_keys();
    ui_ink(UI_DGREY);
    ui_text(8, FOOT_Y, "(C) 2026 ROBERT MECH");
}

/* ---------------------------------------------------------------------- */
/* live fields                                                             */
/* ---------------------------------------------------------------------- */

/* The 1541 wait, in tenths of a second. See docs/DESIGN.md for why there
 * is a wait at all.
 */
static void show_wait(void)
{
    ui_ink(UI_DGREY);
    if (dos_drive_type != DRV_1541 || mode != MODE_SURFACE) {
        ui_text_pad(208, DOS_Y, "", 13);
        return;
    }
    ui_text(208, DOS_Y, "WAIT");
    ui_num(248, DOS_Y, (unsigned int)(fmt_track_wait / 6u), 3);
    ui_text(272, DOS_Y, "/10S");
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

static void msg(const char *s, unsigned char colour)
{
    ui_ink(colour);
    ui_text_pad(8, MSG_Y, s, 38);
}

/* The whole disc goes over to one colour, which is how the state of the
 * disk is shown once the tracks are down: yellow while it is being
 * checked, green when it came back good, red when it did not. Colour only,
 * so it costs one store a cell and redraws nothing.
 */
static void disc_colour(unsigned char colour)
{
    ui_cell_colour(24, 3, 16, 13, colour);
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
    ui_text(152, STAT_Y, "--");
    ui_ink(UI_WHITE);
    ui_num(176, STAT_Y, total, 2);
    ui_ink(UI_DGREY);
    ui_num(232, STAT_Y, bad_count, 2);
    ui_num(304, STAT_Y, 0, 1);
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
    unsigned int span = DISC_RY - DISC_RMIN - 3;

    return (unsigned char)(DISC_RY - 1 -
                           (span * (unsigned int)(track - 1)) / total);
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
    draw_slot();

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
        ui_arrow(SLOT_X + SLOT_W / 2 - 12, (unsigned char)(hy - 10));
    }
    (void)rx;
}

static void show_track(unsigned char track, unsigned char total,
                       unsigned char retry)
{
    ui_ink(UI_WHITE);
    ui_num(152, STAT_Y, track, 2);
    ui_num(176, STAT_Y, total, 2);
    ui_ink(bad_count ? UI_LRED : UI_DGREY);
    ui_num(232, STAT_Y, bad_count, 2);
    ui_ink(retry ? UI_LRED : UI_DGREY);
    ui_num(304, STAT_Y, retry, 1);
}

static void show_track_labels(void)
{
    ui_ink(UI_GREY);
    ui_text(104, STAT_Y, "TRACK");
    ui_text(168, STAT_Y, "/");
    ui_ink(UI_DGREY);
    ui_text(200, STAT_Y, "BAD");
    ui_text(256, STAT_Y, "RETRY");
}

/* ---------------------------------------------------------------------- */
/* actions                                                                 */
/* ---------------------------------------------------------------------- */

static void probe(void)
{
    msg("LOOKING FOR THE DRIVE", UI_GREY);
    show_seg_device();
    if (!dos_present(dev)) {
        dos_close();
        dos_drive_type = DRV_UNKNOWN;
        dos_tracks     = 0;
    } else if (!dos_open(dev)) {
        dos_drive_type = DRV_UNKNOWN;
        dos_tracks     = 0;
    } else {
        dos_identify();
    }
    ui_head_shape((unsigned char)(dos_drive_type != DRV_1581));
    show_device();
    show_dos();
    show_name();
    draw_keys();
    show_wait();
    if (dos_drive_type == DRV_UNKNOWN) {
        msg("NO DRIVE THERE, OR ONE I CANNOT DRIVE", UI_LRED);
    } else {
        msg("READY", UI_LGREEN);
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
static void run_quick(void)
{
    unsigned char total = dos_tracks;
    unsigned int  step  = fmt_native_frames() / total;
    unsigned char t;
    unsigned char st;

    bad_count = 0;
    last_ring = 0;

    msg("FORMATTING. THE DRIVE REPORTS NOTHING UNTIL DONE", UI_CYAN);
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
    }

    show_ring(total, total, UI_CYAN);
    show_bar(total, total);
    show_track(total, total, 0);
    ui_arrow_off();

    msg("CHECKING", UI_CYAN);
    disc_colour(UI_YELLOW);
    led(1);
    st = fmt_native_end();
    led(0);
    disc_colour(st ? UI_LRED : UI_LGREEN);
    if (st != 0) {
        msg("THE DRIVE REPORTED A PROBLEM", UI_LRED);
        ui_ink(UI_LRED);
        ui_text_pad(8, (unsigned char)(MSG_Y + 8), dos_msg, 38);
        return;
    }
    msg("DONE", UI_LGREEN);
}

static void run_format(void)
{
    unsigned char t, st, retry;
    unsigned char total = dos_tracks;

    bad_count = 0;
    last_ring = 0;

    if (!fmt_begin(id1, id2)) {
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

        if (cbm_k_getin() == 3) { /* RUN/STOP */
            ui_arrow_off();
            msg("STOPPED. THE DISK IS NOT USABLE", UI_LRED);
            fmt_end();
            return;
        }
    }
    fmt_end();
    ui_arrow_off();

    msg("WRITING THE DIRECTORY", UI_CYAN);
    disc_colour(UI_YELLOW);
    led(1);
    st = fmt_filesystem(disk_name, id1, id2);
    led(0);
    disc_colour(st ? UI_LRED : (bad_count ? UI_YELLOW : UI_LGREEN));
    if (st != 0) {
        msg("THE DIRECTORY DID NOT TAKE", UI_LRED);
        return;
    }

    if (bad_count) {
        fmt_lock_out(bad_list, (unsigned char)(bad_count > MAX_BAD
                                               ? MAX_BAD : bad_count));
        msg("DONE, WITH BAD TRACKS LOCKED OUT", UI_YELLOW);
    } else {
        msg("DONE", UI_LGREEN);
    }
}

/* ---------------------------------------------------------------------- */

int main(void)
{
    unsigned char c;

    ui_init();
    snd_init();
    draw_static();
    show_track_labels();
    probe();
    show_track(0, dos_tracks, 0);

#ifdef AUTORUN
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
                    msg("THE WAIT ONLY APPLIES TO A 1541", UI_GREY);
                    break;
                }
                fmt_track_wait += 30u;
                if (fmt_track_wait > 540u) {
                    fmt_track_wait = 90u;
                }
                show_wait();
                msg("LOWER IS QUICKER, TOO LOW HANGS THE MACHINE",
                    UI_YELLOW);
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
                    ? "QUICK: THE DRIVE DOES IT, NO BAD TRACK CHECK"
                    : "SURFACE: TRACK BY TRACK, SLOW, FINDS BAD ONES",
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
