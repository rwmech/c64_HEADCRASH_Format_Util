/* t_full.c - harness: full track pass plus BAM/directory, end to end.
 * Build with -DT_DEV=8 (1541) or -DT_DEV=9 (1581).
 * (C) 2026 Robert Mech. Licence MIT.
 */

#include <stdio.h>
#include <conio.h>
#include "dos.h"
#include "fmt.h"

#ifndef T_DEV
#define T_DEV 8
#endif

int main(void)
{
    unsigned char st, t, bad = 0;

    clrscr();
    if (!dos_open(T_DEV)) {
        printf("open failed\n");
        for (;;) { }
    }
    dos_identify();
    printf("dev %u %s trk %u\n", T_DEV, dos_type_name(), dos_tracks);

    if (!fmt_begin(0x36, 0x34)) {
        printf("begin failed\n");
        for (;;) { }
    }

    for (t = 1; t <= dos_tracks; ++t) {
        st = fmt_track(t);
        if (st != 1) {
            printf("trk %u job=%u\n", t, st);
            ++bad;
        }
    }
    fmt_end();
    printf("pass done, %u bad\n", bad);

    st = fmt_filesystem("headcrash test", 0x36, 0x34);
    printf("seed=%u init=%u\nfs st=%u %s\n", fmt_seed_status, fmt_init_status, st, dos_msg);

    dos_close();
    printf("done\n");
    for (;;) { }
    return 0;
}
