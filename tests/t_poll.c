/* t_poll.c - harness: does a 1581 report its cylinder during its own N:?
 *
 * The question this answers is whether QUICK mode can show real progress
 * or has to run the bar on a clock. The 1581 answers the serial bus while
 * it formats, so an M-R is safe; what is not established is whether the
 * cylinder counter at $0088 tracks the drive's own FORMATDK pass the way
 * it tracks a track at a time one.
 *
 * Sends N0: and then reads $0088 as fast as the bus allows, printing every
 * value that differs from the one before it. A counter that tracks will
 * print a rising run; one that does not will print noise or nothing.
 *
 * (C) 2026 Robert Mech. Licence MIT.
 */

#include <stdio.h>
#include <conio.h>
#include "dos.h"

#ifndef T_DEV
#define T_DEV 8
#endif

int main(void)
{
    unsigned char v, last = 0xff, n = 0;
    unsigned int  i;

    clrscr();
    printf("poll $0088 during n: dev %u\n", T_DEV);

    if (!dos_present(T_DEV)) {
        printf("nothing at %u\n", T_DEV);
        for (;;) { }
    }
    if (!dos_open(T_DEV)) {
        printf("open failed\n");
        for (;;) { }
    }
    dos_identify();
    printf("type %u tracks %u\n", dos_drive_type, dos_tracks);

    /* N0:POLLTEST,ZZ built from numeric codes, because a C literal would
     * reach the drive as shifted PETSCII and come back as error 31.
     */
    {
        unsigned char cmd[14];
        cmd[0]  = 0x4e; cmd[1]  = 0x30; cmd[2]  = 0x3a;   /* N 0 : */
        cmd[3]  = 0x50; cmd[4]  = 0x4f; cmd[5]  = 0x4c;   /* P O L */
        cmd[6]  = 0x4c; cmd[7]  = 0x54; cmd[8]  = 0x53;   /* L T S */
        cmd[9]  = 0x54; cmd[10] = 0x2c;                   /* T ,   */
        cmd[11] = 0x5a; cmd[12] = 0x5a;                   /* Z Z   */
        cmd[13] = 0;
        dos_cmd_raw(cmd, 13);
    }

    for (i = 0; i < 4000u; ++i) {
        if (!dos_mr(0x0088, &v, 1)) {
            printf("[mr fail at %u]\n", i);
            break;
        }
        if (v != last) {
            printf("%u ", v);
            last = v;
            if (++n > 120) {
                break;
            }
        }
    }
    printf("\ndone, %u changes\n", n);

    for (;;) { }
    return 0;
}
