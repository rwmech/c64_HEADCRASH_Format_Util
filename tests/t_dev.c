/* t_dev.c - harness: device presence across a full cycle of 8 to 11.
 *
 * The bug this guards against is a probe that works once and then reports
 * every device as missing, which is what a sticky ST does. Runs the cycle
 * twice and then asks about the real drive again.
 *
 * (C) 2026 Robert Mech. Licence MIT.
 */
#include <stdio.h>
#include <conio.h>
#include "dos.h"
int main(void)
{
    unsigned char d, pass;
    clrscr();
    for (pass = 0; pass < 3; ++pass) {
        printf("pass %u: ", pass);
        for (d = 8; d <= 11; ++d) printf("%u=%u ", d, dos_present(d));
        printf("\n");
    }
    if (dos_present(8) && dos_open(8)) {
        dos_identify();
        printf("dev 8 type %u trk %u\n", dos_drive_type, dos_tracks);
    } else {
        printf("LOST DEVICE 8\n");
    }
    for(;;){}
}
