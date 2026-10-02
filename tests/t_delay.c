/* t_delay.c - is dos_delay_long actually counting frames?
 *
 * Every wait in this program is spent in dos_delay_long, and every one of
 * them is documented in frames: the 1541 track wait, the job wait, the
 * settle before the directory, the spacing between job polls. If the unit
 * is wrong then every one of those numbers is wrong by the same factor, and
 * the symptom would be a wait that runs out before the drive has finished
 * and a machine that hangs talking to a deaf 1541.
 *
 * The loop watches the raster register at $D012 and counts transitions into
 * zero. $D012 holds the low eight bits of the raster line, and a frame has
 * more than 256 lines on both NTSC (263) and PAL (312), so the line counter
 * passes through a low byte of zero twice per frame: at line 0 and again at
 * line 256. If that is what is happening, the loop counts half frames.
 *
 * This measures it against the jiffy clock at $A2, which the KERNAL's own
 * interrupt advances once per frame and which is independent of anything
 * here. Ask for 600; a frame counter reads about 600 jiffies back, a half
 * frame counter about 300.
 *
 * (C) 2026 Robert Mech. Licence MIT.
 */

#include <stdio.h>
#include <conio.h>
#include "dos.h"

/* The jiffy clock is three bytes at $A0-$A2, big endian, incremented by the
 * KERNAL interrupt. Two bytes is plenty for a few hundred frames, but the
 * low byte alone would wrap, so read both.
 */
#define JIFFY_MID (*(volatile unsigned char *)0xa1)
#define JIFFY_LO  (*(volatile unsigned char *)0xa2)

static unsigned int jiffies(void)
{
    unsigned char hi, lo;

    /* Read until the pair is consistent, in case the interrupt lands in the
     * middle of it and carries into the middle byte.
     */
    do {
        hi = JIFFY_MID;
        lo = JIFFY_LO;
    } while (hi != JIFFY_MID);

    return (unsigned int)(((unsigned int)hi << 8) | lo);
}

static void measure(unsigned int asked)
{
    unsigned int t0, t1;

    t0 = jiffies();
    dos_delay_long(asked);
    t1 = jiffies();

    printf("asked %u got %u\n", asked, (unsigned int)(t1 - t0));
}

int main(void)
{
    clrscr();
    printf("dos_delay_long vs jiffy clock\n");
    printf("raster $d012 lo byte hits 0\n");
    printf("twice a frame if this is wrong\n\n");

    measure(60u);
    measure(120u);
    measure(600u);

    printf("\ndone\n");
    for (;;) { }
    return 0;
}
