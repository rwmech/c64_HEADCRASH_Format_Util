/* t_nscan.c - look for a drive variable that tracks progress during the
 * 1581's own N: command. Dumps the same block twice, well apart.
 * (C) 2026 Robert Mech. Licence MIT.
 */
#include <stdio.h>
#include <conio.h>
#include "dos.h"

static unsigned char a[16], b[16];

static void snap(unsigned int base, unsigned char *out)
{
    unsigned char i;
    for (i = 0; i < 16; ++i) {
        if (!dos_mr(base + i, out + i, 1)) {
            out[i] = 0xee;
        }
    }
}

int main(void)
{
    unsigned char i;
    unsigned int base = T_BASE;

    clrscr();
    if (!dos_open(9)) { printf("open failed\n"); for(;;){} }
    dos_identify();
    printf("scan %04x\n", base);

    dos_cmd("N0:scan,sx");
    dos_delay(180);
    snap(base, a);
    dos_delay(600);
    snap(base, b);

    for (i = 0; i < 16; ++i) {
        if (a[i] != b[i]) {
            printf("%04x %02x>%02x  ", base + i, a[i], b[i]);
        }
    }
    printf("\n");
    for(;;){}
    return 0;
}
