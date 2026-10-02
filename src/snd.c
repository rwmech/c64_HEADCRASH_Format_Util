/* snd.c - drive noise on the SID.
 *
 * Two voices, both noise.
 *
 * Voice 1 is the stepper: a high frequency noise burst with no attack and
 * a short decay, which is as close to a head seek tick as a SID gets. It
 * is retriggered by dropping the gate and raising it again; the decay does
 * the rest, so nothing here ever waits.
 *
 * Voice 2 is the spindle motor: the same noise generator run at a very low
 * frequency, which comes out as a rumble rather than a hiss, held at full
 * sustain for as long as the drive is working.
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
#define V2      7               /* motor */
#define VOLUME  24

#define WAVE_NOISE 0x80

static unsigned char on_flag = 1;
static unsigned char motor_on;

void snd_init(void)
{
    unsigned char i;

    for (i = 0; i < 25; ++i) {
        SID[i] = 0;
    }

    /* Stepper: no attack, a decay of about 25 ms, nothing held after. */
    SID[V1 + V_FLO] = 0x00;
    SID[V1 + V_FHI] = 0x28;
    SID[V1 + V_AD]  = 0x08;
    SID[V1 + V_SR]  = 0x00;
    SID[V1 + V_CTRL] = WAVE_NOISE;

    /* Motor: low enough that the noise generator rumbles, held flat. */
    SID[V2 + V_FLO] = 0x40;
    SID[V2 + V_FHI] = 0x01;
    SID[V2 + V_AD]  = 0x00;
    SID[V2 + V_SR]  = 0xf0;
    SID[V2 + V_CTRL] = WAVE_NOISE;

    SID[VOLUME] = 0x0f;
    motor_on = 0;
}

void snd_hush(void)
{
    SID[V1 + V_CTRL] = WAVE_NOISE;
    SID[V2 + V_CTRL] = WAVE_NOISE;
    motor_on = 0;
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

void snd_motor(unsigned char on)
{
    if (!on_flag) {
        return;
    }
    if (on) {
        if (!motor_on) {
            SID[V2 + V_CTRL] = WAVE_NOISE | 0x01;
            motor_on = 1;
        }
    } else if (motor_on) {
        SID[V2 + V_CTRL] = WAVE_NOISE;
        motor_on = 0;
    }
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
