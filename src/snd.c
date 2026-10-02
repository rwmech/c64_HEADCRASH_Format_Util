/* snd.c - drive noise on the SID.
 *
 * One voice, one sound: the stepper. A noise burst with no attack and a
 * decay of 24 ms, which is a tick and nothing more. It is retriggered by
 * dropping the gate and raising it again, and the decay does the rest, so
 * nothing here ever waits.
 *
 * There was a second voice holding a low noise rumble for the spindle. It
 * was a drone, it was the worst thing on the machine, and it is gone. A
 * drive is ticks.
 *
 * (C) 2026 Robert Mech. Licence MIT.
 */

#include "snd.h"

#define SID ((volatile unsigned char *)0xd400)

/* Offsets within a voice. */
#define V_FLO   0
#define V_FHI   1
#define V_PWLO  2
#define V_PWHI  3
#define V_CTRL  4
#define V_AD    5
#define V_SR    6

#define V1      0               /* stepper */
#define VOLUME  24

#define WAVE_NOISE 0x80

static unsigned char on_flag = 1;

void snd_init(void)
{
    unsigned char i;

    for (i = 0; i < 25; ++i) {
        SID[i] = 0;
    }

    /* Stepper: a bright tick. The noise generator is run fast, which is
     * what makes it a click rather than a rasp, and the decay is short
     * enough that it is over well before the next track.
     */
    SID[V1 + V_FLO] = 0x00;
    SID[V1 + V_FHI] = 0x48;
    SID[V1 + V_AD]  = 0x01;
    SID[V1 + V_SR]  = 0x00;
    SID[V1 + V_CTRL] = WAVE_NOISE;


    SID[VOLUME] = 0x0f;
}

void snd_hush(void)
{
    SID[V1 + V_CTRL] = WAVE_NOISE;
}

void snd_enable(unsigned char on)
{
    on_flag = on ? 1 : 0;
    if (!on_flag) {
        snd_hush();
        SID[VOLUME] = 0x00;
    } else {
        SID[VOLUME] = 0x0f;
    }
}

unsigned char snd_enabled(void)
{
    return on_flag;
}


void snd_click(void)
{
    if (!on_flag) {
        return;
    }
    /* Gate down then up is what retriggers the envelope. Two stores. */
    SID[V1 + V_CTRL] = WAVE_NOISE;
    SID[V1 + V_CTRL] = WAVE_NOISE | 0x01;
}
