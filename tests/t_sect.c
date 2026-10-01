/* t_sect.c - harness: write one sector through the block commands.
 * (C) 2026 Robert Mech. Licence GPL-3.0-or-later.
 */

#include <stdio.h>
#include <conio.h>
#include "dos.h"

#ifndef T_DEV
#define T_DEV 8
#endif

static unsigned char sec[256];

int main(void)
{
    unsigned int i;
    unsigned char st;

    clrscr();
    if (!dos_open(T_DEV)) {
        printf("open failed\n");
        for (;;) { }
    }
    dos_identify();
    printf("%s open st=%u %s\n", dos_type_name(), dos_status(), dos_msg);

    for (i = 0; i < 256u; ++i) {
        sec[i] = (unsigned char)i;
    }
    sec[0] = 0xde;
    sec[1] = 0xad;
    sec[2] = 0x41;

    st = dos_write_sector(dos_dir_track, 0, sec);
    printf("write st=%u %s\n", st, dos_msg);
    dos_close();
    printf("done\n");
    for (;;) { }
    return 0;
}
