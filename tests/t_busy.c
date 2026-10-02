/* t_busy.c - can a 1541 be asked whether it is still formatting?
 *
 * This is the question the whole of SLOW mode hangs on. A 1541 goes deaf
 * while it writes a track, and the KERNAL's data send has no timeout once
 * a device has acknowledged, so anything sent to it then hangs the
 * machine. The program has been waiting out a measured worst case instead,
 * and when a real drive takes longer than that worst case the machine
 * hangs anyway, which is exactly what happens on hardware around track 25.
 *
 * But the addressing sequence is not the data sequence. tests/t_dev.c
 * showed that LISTEN, a secondary address and UNLISTEN come back with ST
 * set rather than hanging when nothing answers, because the per device
 * answer is given after ATN is released and the KERNAL gives up on it.
 * A drive whose processor is busy writing a track cannot give that answer
 * either, so the same sequence should report it as absent, and report it
 * as present again the moment it is finished.
 *
 * If that holds, the wait disappears and SLOW mode runs at the drive's own
 * speed. This harness starts one track and then asks, over and over,
 * printing how many times it was told no and how many frames that took.
 *
 * (C) 2026 Robert Mech. Licence MIT.
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

#define JIFFY (*(volatile unsigned char *)0xa2)

int main(void)
{
    unsigned int  polls, zeros;
    unsigned char t0, frames;
    unsigned char present;

    clrscr();
    printf("busy poll dev %u track %u\n", T_DEV, T_TRACK);

    if (!dos_present(T_DEV) || !dos_open(T_DEV)) {
        printf("no drive\n");
        for (;;) { }
    }
    dos_identify();
    printf("type %u tracks %u\n", dos_drive_type, dos_tracks);

    if (!fmt_begin('Z', 'Z')) {
        printf("fmt_begin failed\n");
        for (;;) { }
    }

    /* Before the job: the drive should say it is there. */
    printf("idle present=%u\n", dos_present(T_DEV));

    t0     = JIFFY;
    polls  = 0;
    zeros  = 0;
    fmt_track_start(T_TRACK);

    /* Sample right across the track rather than stopping at the first
     * answer, so the question is not "is it busy now" but "is it ever
     * reported busy at all".
     */
    while (polls < 3000u) {
        present = dos_present(T_DEV);
        ++polls;
        if (!present) {
            ++zeros;
        }
    }
    frames = (unsigned char)(JIFFY - t0);

    printf("polls=%u said-busy=%u frames=%u\n", polls, zeros, frames);
    printf("after present=%u\n", dos_present(T_DEV));
    printf("status %u\n", dos_status());

    fmt_end();
    printf("done\n");

    for (;;) { }
    return 0;
}
