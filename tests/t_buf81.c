/* t_buf81.c - harness: find where a 1581 job job leaves its sector data.
 *
 * Reads a known sector with job $80 and dumps the candidate buffer
 * addresses, so the write path can address the right one.
 *
 * (C) 2026 Robert Mech. Licence GPL-3.0-or-later.
 */

#include <stdio.h>
#include <conio.h>
#include "dos.h"

static void dump(unsigned int addr)
{
    unsigned char v[8];
    unsigned char i;

    printf("%04x: ", addr);
    if (!dos_mr(addr, v, 8)) {
        printf("unread\n");
        return;
    }
    for (i = 0; i < 8; ++i) {
        printf("%02x", v[i]);
    }
    printf("\n");
}

int main(void)
{
    unsigned char st;

    clrscr();
    if (!dos_open(9)) {
        printf("open failed\n");
        for (;;) { }
    }
    dos_identify();
    printf("%s\n", dos_type_name());

    st = dos_job(0, JOB_READ, 40, 0);
    printf("read 40/0 job=%u\n", st);
    dump(0x0c00);
    dump(0x0d00);

    {   /* which job code writes without re-reading the physical sector? */
        unsigned char v[3];
        unsigned char codes[3];
        unsigned char k;

        codes[0] = 0x90; codes[1] = 0xa4; codes[2] = 0xa6;
        for (k = 0; k < 3; ++k) {
            dos_job(0, JOB_READ, 40, 0);
            v[0] = 0xd0 + k; v[1] = 0xad; v[2] = 0x44;
            dos_mw(0x0c00, v, 3);
            st = dos_job(0, codes[k], 40, 0);
            printf("job %02x -> %u ", codes[k], st);
            dump(0x0c00);
        }
    }

    dos_close();
    printf("done\n");
    for (;;) { }
    return 0;
}
