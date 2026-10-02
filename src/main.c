/* main.c - HEADCRASH Format Util.
 *
 * A formatter that works one track at a time so it can show you what it is
 * doing, retry a track that fails and tell you which ones are bad.
 *
 * (C) 2026 Robert Mech. Licence GPL-3.0-or-later.
 *
 * Required libraries: cc65 C library (cbm.h for the KERNAL entry points).
 */

#include <string.h>
#include <cbm.h>
#include "ui.h"
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
#define DRV_H      32
#define DRV1_Y     24           /* 1581, cell rows 3..6  */
#define DRV2_Y     64           /* 1541, cell rows 8..11 */
#define DRV_CX     1            /* in cells */
#define DRV_CW     18
#define DRV1_CY    3
#define DRV2_CY    8
#define DRV_CH     4
#define LED_CX     17           /* the activity light, its own cell */
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
#define STAT_Y     168
#define MSG_Y      176
#define KEYS_Y     184
#define FOOT_Y     192

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

/* A drive, drawn once: case, front bezel, slot, a badge strip and an
 * activity light. Line art rather than a photograph, but with enough of
 * the real thing's proportions to be recognisable at a glance.
 */
static void draw_drive_body(unsigned char y, unsigned char kind)
{
    unsigned int x = DRV_X;

    ui_ink(UI_CYAN);

    /* Case, with a lighter top edge to suggest the moulding. */
    ui_box(x, y, DRV_W, DRV_H);
    ui_hline(x + 3, x + DRV_W - 4, (unsigned char)(y + 3));

    /* Front bezel. */
    ui_box(x + 6, (unsigned char)(y + 7), DRV_W - 12, DRV_H - 13);

    if (kind == DRV_1581) {
        /* 3.5 inch: a short slot with the shutter lip above it and the
         * eject button to its right.
         */
        ui_box(x + 16, (unsigned char)(y + 12), 56, 7);
        ui_hline(x + 20, x + 60, (unsigned char)(y + 10));
        ui_box(x + 80, (unsigned char)(y + 13), 10, 5);
    } else {
        /* 5.25 inch: a long slot with the door seam across it and the
         * latch standing proud on the left.
         */
        ui_box(x + 14, (unsigned char)(y + 11), 84, 9);
        ui_hline(x + 16, x + 96, (unsigned char)(y + 15));
        ui_box(x + 8, (unsigned char)(y + 9), 4, 13);
    }

    /* Badge strip along the bottom of the bezel. */
    ui_ink(UI_DGREY);
    ui_hline(x + 16, x + 96, (unsigned char)(y + DRV_H - 9));

    /* The activity light, filled so that only its colour has to change. */
    ui_ink(UI_RED);
    ui_fill(DRV_X + (LED_CX - DRV_CX) * 8 + 1,
            (unsigned char)(y + LED_OFFY), 6, 5);
}

/* Seven segment digits, the way a drive number ought to be shown. Each
 * segment is a short bar two pixels thick; the table is one bit per
 * segment, a at the top going clockwise with g in the middle.
 */
#define SEG_W 14
#define SEG_H 22

static const unsigned char seg_digit[10] = {
    0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f
};

static void draw_seg(unsigned int x, unsigned char y, unsigned char d)
{
    unsigned char m = seg_digit[d % 10];
    unsigned char i;

    for (i = 0; i < 2; ++i) {
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
    draw_seg(SEG_X + SEG_W + 4, SEG_Y, (unsigned char)(dev % 10));
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

static void led(unsigned char on)
{
    show_drive_state(on);
}

static void draw_disc(void)
{
    ui_ink(UI_GREY);
    ui_ring(DISC_CX, DISC_CY, (unsigned char)(DISC_RY + 4));
    ui_ring(DISC_CX, DISC_CY, DISC_RMIN);
    ui_ink(UI_DGREY);
    ui_ring(DISC_CX, DISC_CY, (unsigned char)(DISC_RMIN - 5));
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
    ui_text(8, KEYS_Y, "F1 DRIVE F5 NAME F8 MODE F7 GO");
    ui_ink(UI_DGREY);
    ui_text(8, FOOT_Y, "(C) 2026 ROBERT MECH  GPL-3.0+");
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
    ui_ink(UI_DGREY);
    if (dos_drive_type != DRV_UNKNOWN) {
        ui_num(VAL_X + 48, (unsigned char)(FIELD_Y + 1 * FIELD_STEP),
               dos_tracks, 2);
        ui_text(VAL_X + 72, (unsigned char)(FIELD_Y + 1 * FIELD_STEP),
                "TRACKS");
    }
}

static void show_mode(void)
{
    ui_ink(mode == MODE_QUICK ? UI_LGREEN : UI_YELLOW);
    ui_text_pad(232, (unsigned char)(FIELD_Y + 1 * FIELD_STEP),
                mode == MODE_QUICK ? "QUICK" : "SURFACE", 8);
}

static void msg(const char *s, unsigned char colour)
{
    ui_ink(colour);
    ui_text_pad(8, MSG_Y, s, 38);
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

    /* The head marker sits just outside the ring being written. */
    ui_arrow(DISC_CX + rx + 6, (unsigned char)(DISC_CY - 10));
}

static void show_track(unsigned char track, unsigned char total,
                       unsigned char retry)
{
    ui_ink(UI_WHITE);
    ui_num(56, STAT_Y, track, 2);
    ui_num(80, STAT_Y, total, 2);
    ui_ink(bad_count ? UI_LRED : UI_DGREY);
    ui_num(152, STAT_Y, bad_count, 2);
    ui_ink(retry ? UI_LRED : UI_DGREY);
    ui_num(240, STAT_Y, retry, 1);
}

static void show_track_labels(void)
{
    ui_ink(UI_GREY);
    ui_text(8, STAT_Y, "TRACK");
    ui_text(72, STAT_Y, "/");
    ui_ink(UI_DGREY);
    ui_text(112, STAT_Y, "BAD");
    ui_text(184, STAT_Y, "RETRY");
}

/* ---------------------------------------------------------------------- */
/* actions                                                                 */
/* ---------------------------------------------------------------------- */

static void probe(void)
{
    msg("LOOKING FOR THE DRIVE", UI_GREY);
    show_seg_device();
    if (!dos_open(dev)) {
        dos_drive_type = DRV_UNKNOWN;
    } else {
        dos_identify();
    }
    show_device();
    show_dos();
    show_name();
    show_mode();
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

    msg("FORMATTING, THE BAR IS AN ESTIMATE", UI_CYAN);
    fmt_native_start(disk_name, id1, id2);

    /* One step of the bar per track's worth of time. The drive is not
     * telling us anything, so this is a clock, not a report.
     */
    for (t = 1; t <= total; ++t) {
        led((unsigned char)(t & 1));    /* the drive is working throughout */
        dos_delay_long(step);
        show_ring(t, total, UI_CYAN);
        show_bar(t, total);
        show_track(t, total, 0);
    }

    show_ring(total, total, UI_CYAN);
    show_bar(total, total);
    show_track(total, total, 0);
    ui_arrow_off();

    msg("CHECKING", UI_CYAN);
    led(1);
    st = fmt_native_end();
    led(0);
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
    led(1);
    st = fmt_filesystem(disk_name, id1, id2);
    led(0);
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
        show_mode();
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
            case 135: /* F5 */
                edit_name();
                break;
            case 140: /* F8: which kind of format */
                mode = (unsigned char)(mode == MODE_QUICK ? MODE_SURFACE
                                                          : MODE_QUICK);
                show_mode();
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
            default:
                break;
        }
    }
    return 0;
}
