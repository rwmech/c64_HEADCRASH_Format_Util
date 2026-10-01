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

#define VERSION "V1.0"

/* How many times a track is attempted before it is called bad. Each
 * attempt is a whole job, drawn as it happens.
 */
#define MAX_RETRY 5

#define MAX_BAD   16

/* Layout, in pixels. Text has to start on a cell row, because a glyph is
 * written as the whole 8x8 cell; lines and circles can go anywhere.
 */
#define TITLE_Y    2
#define TITLE_TY   8
#define DRV_Y      26
#define DRV_W      72
#define DRV_H      26
#define DRV1_X     8
#define DRV2_X     88
#define DRV_LY     56
#define FIELD_X    8
#define FIELD_Y    72
#define FIELD_STEP 8
#define VAL_X      72
#define DISC_CX    252
#define DISC_CY    112
#define DISC_R     36
#define DISC_RMIN  10
#define BAR_X      8
#define BAR_Y      150
#define BAR_W      304
#define BAR_H      10
#define STAT_Y     168
#define MSG_Y      176
#define KEYS_Y     184
#define FOOT_Y     192

static unsigned char dev = 8;
static char          disk_name[17] = { 'W', 'O', 'R', 'K', 'D', 'I', 'S',
                                       'K', 0 };
static unsigned char id1 = '6', id2 = '4';
static unsigned char bad_list[MAX_BAD];
static unsigned char bad_count;

/* ---------------------------------------------------------------------- */
/* static furniture                                                        */
/* ---------------------------------------------------------------------- */

static void draw_drive_icon(unsigned int x, unsigned char sel,
                            unsigned char kind)
{
    ui_ink(sel ? UI_CYAN : UI_DGREY);
    ui_box(x, DRV_Y, DRV_W, DRV_H);
    ui_hline(x + 3, x + DRV_W - 4, (unsigned char)(DRV_Y + DRV_H - 7));
    if (kind == DRV_1581) {
        /* A 3.5 inch disk: shutter across the top, write tab at the side. */
        ui_box(x + 14, (unsigned char)(DRV_Y + 7), 34, 5);
        ui_box(x + 52, (unsigned char)(DRV_Y + 7), 8, 5);
    } else {
        /* A 5.25 inch slot. */
        ui_hline(x + 10, x + 60, (unsigned char)(DRV_Y + 9));
        ui_hline(x + 10, x + 60, (unsigned char)(DRV_Y + 13));
        ui_vline(x + 10, (unsigned char)(DRV_Y + 9),
                 (unsigned char)(DRV_Y + 13));
        ui_vline(x + 60, (unsigned char)(DRV_Y + 9),
                 (unsigned char)(DRV_Y + 13));
    }
}

static void draw_disc(void)
{
    ui_ink(UI_GREY);
    ui_circle(DISC_CX, DISC_CY, (unsigned char)(DISC_R + 4));
    ui_circle(DISC_CX, DISC_CY, DISC_RMIN);
    ui_ink(UI_DGREY);
    ui_circle(DISC_CX, DISC_CY, (unsigned char)(DISC_RMIN - 5));
}

static void draw_static(void)
{
    ui_clear();

    ui_ink(UI_LBLUE);
    ui_hline(4, 315, TITLE_Y);
    ui_hline(4, 315, 19);
    ui_vline(4, TITLE_Y, 19);
    ui_vline(315, TITLE_Y, 19);
    ui_text(16, TITLE_TY, "HEADCRASH");
    ui_ink(UI_CYAN);
    ui_text(96, TITLE_TY, "FORMAT UTIL");
    ui_ink(UI_DGREY);
    ui_text(280, TITLE_TY, VERSION);

    ui_ink(UI_GREY);
    ui_text(FIELD_X, (unsigned char)(FIELD_Y + 0 * FIELD_STEP), "DEVICE");
    ui_text(FIELD_X, (unsigned char)(FIELD_Y + 1 * FIELD_STEP), "NAME");
    ui_text(FIELD_X, (unsigned char)(FIELD_Y + 2 * FIELD_STEP), "ID");
    ui_text(FIELD_X, (unsigned char)(FIELD_Y + 3 * FIELD_STEP), "DOS");

    draw_disc();

    ui_ink(UI_CYAN);
    ui_box(BAR_X, BAR_Y, BAR_W, BAR_H);

    ui_ink(UI_GREY);
    ui_text(8, KEYS_Y, "F1 DEV F3 WAIT F5 NAME F7 FORMAT");
    ui_ink(UI_DGREY);
    ui_text(8, FOOT_Y, "(C) 2026 ROBERT MECH  GPL-3.0+");
}

/* ---------------------------------------------------------------------- */
/* live fields                                                             */
/* ---------------------------------------------------------------------- */

/* The 1541 wait, shown in tenths of a second so it means something at a
 * glance. See docs/DESIGN.md for why there is a wait at all.
 */
static void show_wait(void)
{
    ui_ink(UI_DGREY);
    if (dos_drive_type != DRV_1541) {
        ui_text_pad(168, (unsigned char)(FIELD_Y + 0 * FIELD_STEP), "", 14);
        return;
    }
    ui_text(168, (unsigned char)(FIELD_Y + 0 * FIELD_STEP), "WAIT");
    ui_num(208, (unsigned char)(FIELD_Y + 0 * FIELD_STEP),
           (unsigned int)(fmt_track_wait / 6u), 3);
    ui_text(232, (unsigned char)(FIELD_Y + 0 * FIELD_STEP), "/10S");
}

static void show_device(void)
{
    ui_ink(UI_WHITE);
    ui_num(VAL_X, (unsigned char)(FIELD_Y + 0 * FIELD_STEP), dev, 2);

    draw_drive_icon(DRV1_X, (unsigned char)(dos_drive_type == DRV_1581),
                    DRV_1581);
    draw_drive_icon(DRV2_X, (unsigned char)(dos_drive_type == DRV_1541),
                    DRV_1541);
    ui_ink(dos_drive_type == DRV_1581 ? UI_CYAN : UI_DGREY);
    ui_text(DRV1_X, DRV_LY, "1581 3.5");
    ui_ink(dos_drive_type == DRV_1541 ? UI_CYAN : UI_DGREY);
    ui_text(DRV2_X, DRV_LY, "1541 5.25");
}

static void show_dos(void)
{
    ui_ink(dos_drive_type == DRV_UNKNOWN ? UI_LRED : UI_LGREY);
    ui_text_pad(VAL_X, (unsigned char)(FIELD_Y + 3 * FIELD_STEP),
                dos_type_name(), 17);
}

static void show_name(void)
{
    ui_ink(UI_WHITE);
    ui_text_pad(VAL_X, (unsigned char)(FIELD_Y + 1 * FIELD_STEP),
                disk_name, 16);
    {
        char idbuf[3];
        idbuf[0] = (char)id1;
        idbuf[1] = (char)id2;
        idbuf[2] = 0;
        ui_text(VAL_X, (unsigned char)(FIELD_Y + 2 * FIELD_STEP), idbuf);
    }
    ui_ink(UI_DGREY);
    if (dos_drive_type != DRV_UNKNOWN) {
        ui_num(VAL_X + 40, (unsigned char)(FIELD_Y + 2 * FIELD_STEP),
               dos_tracks, 2);
        ui_text(VAL_X + 64, (unsigned char)(FIELD_Y + 2 * FIELD_STEP),
                "TRACKS");
    }
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
    w = ((unsigned int)(BAR_W - 4) * done) / total;
    ui_ink(UI_CYAN);
    if (w > 0) {
        ui_fill(BAR_X + 2, (unsigned char)(BAR_Y + 2), w,
                (unsigned char)(BAR_H - 4));
    }
}

/* One ring per track, laid from the outside in, so the disc fills up as
 * the pass goes on. With 80 tracks over 38 pixels of radius some tracks
 * share a ring; that is the display's resolution, not a lie about which
 * track is being done, which the counter shows exactly.
 */
static void show_ring(unsigned char track, unsigned char total,
                      unsigned char colour)
{
    unsigned int span = DISC_R - DISC_RMIN - 2;
    unsigned char r;

    if (total == 0) {
        return;
    }
    r = (unsigned char)(DISC_R - 1 -
                        (span * (unsigned int)(track - 1)) / total);
    ui_ink(colour);
    ui_circle(DISC_CX, DISC_CY, r);
}

static void show_track(unsigned char track, unsigned char total,
                       unsigned char retry)
{
    ui_ink(UI_GREY);
    ui_text(8, STAT_Y, "TRACK");
    ui_ink(UI_WHITE);
    ui_num(56, STAT_Y, track, 2);
    ui_ink(UI_GREY);
    ui_text(72, STAT_Y, "/");
    ui_ink(UI_WHITE);
    ui_num(80, STAT_Y, total, 2);

    ui_ink(UI_DGREY);
    ui_text(112, STAT_Y, "BAD");
    ui_ink(bad_count ? UI_LRED : UI_DGREY);
    ui_num(144, STAT_Y, bad_count, 2);

    ui_ink(UI_DGREY);
    ui_text(176, STAT_Y, "RETRY");
    ui_ink(retry ? UI_LRED : UI_DGREY);
    ui_num(224, STAT_Y, retry, 1);
}

/* ---------------------------------------------------------------------- */
/* actions                                                                 */
/* ---------------------------------------------------------------------- */

static void probe(void)
{
    msg("LOOKING FOR THE DRIVE", UI_GREY);
    if (!dos_open(dev)) {
        dos_drive_type = DRV_UNKNOWN;
    } else {
        dos_identify();
    }
    show_device();
    show_dos();
    show_name();
    show_wait();
    if (dos_drive_type == DRV_UNKNOWN) {
        msg("NO DRIVE THERE, OR ONE I CANNOT DRIVE", UI_LRED);
    } else {
        msg("READY", UI_LGREEN);
    }
}

/* Type over the disk name in place. Letters and digits only, which is what
 * a CBM disk name can hold without quoting games.
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
        disk_name[0] = 'D';
        disk_name[1] = 'I';
        disk_name[2] = 'S';
        disk_name[3] = 'K';
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
        if (c == 'Y' || c == 0xd9 || c == 0x59) {
            return 1;
        }
        if (c != 0) {
            return 0;
        }
    }
}

static void run_format(void)
{
    unsigned char t, st, retry;
    unsigned char total = dos_tracks;

    bad_count = 0;

    if (!fmt_begin(id1, id2)) {
        msg("THIS DRIVE CANNOT BE DRIVEN TRACK BY TRACK", UI_LRED);
        return;
    }

    for (t = 1; t <= total; ++t) {
        retry = 0;
        for (;;) {
            show_track(t, total, retry);
            msg(retry ? "RETRYING" : "FORMATTING", retry ? UI_LRED : UI_CYAN);

            st = fmt_track(t);
            if (st == 1) {
                break;
            }
            if (++retry >= MAX_RETRY) {
                if (bad_count < MAX_BAD) {
                    bad_list[bad_count] = t;
                }
                ++bad_count;
                show_ring(t, total, UI_DGREY);
                break;
            }
        }
        if (st == 1) {
            show_ring(t, total, UI_CYAN);
        }
        show_bar(t, total);
        show_track(t, total, retry);

        if (cbm_k_getin() == 3) { /* RUN/STOP */
            msg("STOPPED. THE DISK IS NOT USABLE", UI_LRED);
            fmt_end();
            return;
        }
    }
    fmt_end();

    msg("WRITING THE DIRECTORY", UI_CYAN);
    st = fmt_filesystem(disk_name, id1, id2);
    if (st != 0) {
        msg("THE DIRECTORY DID NOT TAKE", UI_LRED);
        ui_ink(UI_LRED);
        ui_text_pad(8, (unsigned char)(MSG_Y + 10), dos_msg, 38);
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
    probe();
    show_track(0, dos_tracks, 0);

#ifdef AUTORUN
    /* Built for the test harness: format without waiting to be asked, so a
     * run can be driven from the command line.
     */
    if (dos_drive_type != DRV_UNKNOWN) {
        run_format();
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
                fmt_track_wait += 60u;
                if (fmt_track_wait > 900u) {
                    fmt_track_wait = 120u;
                }
                show_wait();
                msg("LOWER IS QUICKER, TOO LOW HANGS THE MACHINE",
                    UI_YELLOW);
                break;
            case 135: /* F5 */
                edit_name();
                break;
            case 136: /* F7 */
                if (dos_drive_type == DRV_UNKNOWN) {
                    msg("NOTHING HERE TO FORMAT", UI_LRED);
                } else if (confirm()) {
                    run_format();
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
