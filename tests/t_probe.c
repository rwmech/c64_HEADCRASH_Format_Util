/* t_probe.c - harness: identify the drive and dump the bytes used to do it.
 * Build:  cl65 -t c64 -O -I src -o build/t_probe.prg tests/t_probe.c src/dos.c
 * (C) 2026 Robert Mech. Licence MIT.
 */

#include <stdio.h>
#include <conio.h>
#include "dos.h"

static void probe(unsigned char dev)
{
    unsigned char v[4];
    unsigned char t;

    printf("dev %u: ", dev);
    if (!dos_open(dev)) {
        printf("open failed\n");
        return;
    }
    t = dos_identify();
    if (dos_mr(0xfffc, v, 2)) {
        printf("fffc=%02x%02x ", v[1], v[0]);
    } else {
        printf("mr failed ");
    }
    printf("type %u %s\n", t, dos_type_name());
    printf("  trk %u dir %u st=%u %s\n",
           dos_tracks, dos_dir_track, dos_status(), dos_msg);
    dos_close();
}

int main(void)
{
    clrscr();
    printf("headcrash probe\n");
    probe(8);
    probe(9);
    printf("done\n");
    for (;;) {
    }
    return 0;
}
