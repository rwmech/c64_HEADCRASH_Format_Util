/* t_nprog.c - does a 1581 answer M-R while it runs its own N: command?
 * If it does, the drive's cylinder counter is a progress source and the
 * whole track by track pass is unnecessary on that drive.
 * (C) 2026 Robert Mech. Licence GPL-3.0-or-later.
 */
#include <stdio.h>
#include <conio.h>
#include "dos.h"

int main(void)
{
    unsigned char v[2];
    unsigned char i;

    clrscr();
    if (!dos_open(9)) { printf("open failed\n"); for(;;){} }
    dos_identify();
    printf("%s\n", dos_type_name());

    dos_cmd("N0:probe,px");
    printf("sent\n");
    for (i = 0; i < 40; ++i) {
        dos_delay(20);
        if (dos_mr(0x0088, v, 1)) {
            printf("%02x ", v[0]);
        } else {
            printf("-- ");
        }
    }
    printf("\nstatus %u\n", dos_status());
    for(;;){}
    return 0;
}
