/* t_fmt.c - harness: format a single track and report the job status.
 *
 * Build with -DT_DEV=8 -DT_TRACK=5 and diff the disk image afterwards to
 * see which region of the disk actually changed.
 *
 * (C) 2026 Robert Mech. Licence GPL-3.0-or-later.
 */

#include <stdio.h>
#include <conio.h>
#include "dos.h"
#include "fmt.h"

#ifndef T_DEV
#define T_DEV 8
#endif
#ifndef T_TRACK
#define T_TRACK 5
#endif
#ifndef T_COUNT
#define T_COUNT 1
#endif

int main(void)
{
    unsigned char st, t, i;

    clrscr();
    printf("fmt dev %u trk %u n %u\n", T_DEV, T_TRACK, T_COUNT);

    if (!dos_open(T_DEV)) {
        printf("open failed\n");
        for (;;) { }
    }
    dos_identify();
    printf("type %u %s\n", dos_drive_type, dos_type_name());

    if (!fmt_begin(0x36, 0x34)) { /* ID "64" */
        printf("begin failed\n");
        for (;;) { }
    }
    printf("begin ok st=%u\n", dos_status());

    t = T_TRACK;
    for (i = 0; i < T_COUNT; ++i) {
        st = fmt_track(t);
        printf("trk %u job=%u ", t, st);
        {   /* drive side state, so a stall can be read instead of guessed */
            unsigned char z[4];
            dos_mr(0x0020, z, 3);          /* $20 status, $21, $22 track */
            printf("20=%02x 22=%02x ", z[0], z[2]);
            dos_mr(0x0051, z, 1);
            printf("51=%02x ", z[0]);
            dos_mr(0x0620, z, 1);
            printf("620=%02x\n", z[0]);
        }
        ++t;
    }
    fmt_end();
    printf("dos st=%u %s\n", dos_status(), dos_msg);
    dos_close();
    printf("done\n");
    for (;;) { }
    return 0;
}
