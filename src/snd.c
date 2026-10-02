/* snd.c - drive noise and music on the SID.
 *
 * Three things live here.
 *
 * The stepper tick: a noise burst with no attack and a decay of 24 ms,
 * fired every time the head moves a track. Gated and left to its own
 * decay, so it costs two stores and returns, which matters because it is
 * fired from inside the format loop. A held rumble for the spindle was
 * tried and was a drone, so there is not one.
 *
 * Two tunes, both Beethoven, both out of copyright by about a century and
 * a half, and both chosen because everyone knows them from the first bar.
 * The opening of the Fifth says a format has started. The first phrase of
 * the Ode to Joy says it has finished, which is the one that matters when
 * the machine is across the room.
 *
 * And the player that gets them out. Three voices, one row of the tune per
 * step, advanced once a frame from the interrupt, so nothing anywhere else
 * ever waits for a note. snd_tick() is the whole of it.
 *
 * The master volume sits at 7 of 15 on purpose. This is a utility, not a
 * game; it should not be the loudest thing attached to the television.
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

#define V1      0               /* melody, and the stepper between tunes */
#define V2      7               /* harmony */
#define V3      14              /* bass */
#define VOLUME  24

#define WAVE_NOISE 0x80
#define WAVE_PULSE 0x40
#define GATE       0x01

#define SND_VOLUME 0x07         /* half of full, deliberately */

static unsigned char on_flag = 1;

/* --- notes ------------------------------------------------------------ */

/* SID frequency for one octave, worked out as hz * 2^24 / 1022727, which
 * is the NTSC system clock. A PAL machine runs these about four per cent
 * flat, which nobody can hear in a two second sting.
 *
 * Notes are numbered from 1, with 1 being C2, so note - 1 gives twelve
 * semitones per octave and the octave is a shift either way from here.
 */
static const unsigned int note_base[12] = {
    4292, 4547, 4817, 5104, 5407, 5729,
    6070, 6431, 6813, 7218, 7647, 8102
};

#define C2  1
#define D2  3
#define EB2 4
#define F2  6
#define G2  8

#define C3  13
#define E3  17
#define F3  18
#define G3  20
#define A3  22
#define B3  24
#define EB3 16
#define D3  15

#define C4  25
#define D4  27
#define E4  29
#define F4  30
#define G4  32
#define EB4 28

static unsigned int note_freq(unsigned char n)
{
    unsigned int  f;
    unsigned char oct;

    if (n == 0) {
        return 0;
    }
    --n;
    oct = (unsigned char)(n / 12u);     /* 0 is octave 2 */
    f   = note_base[n % 12u];

    if (oct < 2) {
        f >>= (2 - oct);
    } else if (oct > 2) {
        f <<= (oct - 2);
    }
    return f;
}

/* --- the tunes -------------------------------------------------------- */

/* One row is a chord and how many frames to hold it. */
struct row {
    unsigned char v1, v2, v3, len;
};

/* Beethoven, Symphony No. 5, the opening. Three short and one long,
 * twice, in octaves. About two seconds.
 */
static const struct row tune_start[] = {
    { G4,  G3,  G2,   8 },
    { G4,  G3,  G2,   8 },
    { G4,  G3,  G2,   8 },
    { EB4, EB3, EB2, 28 },
    { 0,   0,   0,    6 },
    { F4,  F3,  F2,   8 },
    { F4,  F3,  F2,   8 },
    { F4,  F3,  F2,   8 },
    { D4,  D3,  D2,  30 },
    { 0,   0,   0,    4 }
};

/* Beethoven, Symphony No. 9, the Ode to Joy, first phrase, with a third
 * below it and the root underneath. Resolved onto the tonic at the end
 * rather than left hanging on the dominant, because this one is saying
 * the job is done. About five seconds.
 */
static const struct row tune_done[] = {
    { E4, C4, C3, 18 },
    { E4, C4, C3, 18 },
    { F4, A3, F3, 18 },
    { G4, B3, G3, 18 },
    { G4, B3, G3, 18 },
    { F4, A3, F3, 18 },
    { E4, C4, C3, 18 },
    { D4, B3, G3, 18 },
    { C4, E3, C3, 18 },
    { C4, E3, C3, 18 },
    { D4, B3, G3, 18 },
    { E4, C4, C3, 18 },
    { E4, C4, C3, 27 },
    { D4, B3, G3,  9 },
    { D4, B3, G3, 24 },
    { C4, E3, C3, 40 },
    { 0,  0,  0,   8 }
};

/* --- the player ------------------------------------------------------- */

static const struct row *tune;
static unsigned char tune_rows;
static unsigned char tune_pos;
static unsigned char tune_timer;

static void voice_gate_off(void)
{
    SID[V1 + V_CTRL] = WAVE_PULSE;
    SID[V2 + V_CTRL] = WAVE_PULSE;
    SID[V3 + V_CTRL] = WAVE_PULSE;
}

/* Voice 1 is shared: pulse while a tune is playing, noise the rest of the
 * time so the stepper has something to tick with.
 */
static void voice1_noise(void)
{
    SID[V1 + V_FLO]  = 0x00;
    SID[V1 + V_FHI]  = 0x48;
    SID[V1 + V_AD]   = 0x01;
    SID[V1 + V_SR]   = 0x00;
    SID[V1 + V_CTRL] = WAVE_NOISE;
}

static void voice1_pulse(void)
{
    SID[V1 + V_PWLO] = 0x00;
    SID[V1 + V_PWHI] = 0x08;
    SID[V1 + V_AD]   = 0x14;
    SID[V1 + V_SR]   = 0xa6;
    SID[V1 + V_CTRL] = WAVE_PULSE;
}

void snd_init(void)
{
    unsigned char i;

    for (i = 0; i < 25; ++i) {
        SID[i] = 0;
    }

    voice1_noise();

    /* Harmony: a narrower pulse, so it sits apart from the melody. */
    SID[V2 + V_PWLO] = 0x00;
    SID[V2 + V_PWHI] = 0x04;
    SID[V2 + V_AD]   = 0x14;
    SID[V2 + V_SR]   = 0xa6;
    SID[V2 + V_CTRL] = WAVE_PULSE;

    /* Bass: slower decay, lower sustain, so it rounds the chord off. */
    SID[V3 + V_PWLO] = 0x00;
    SID[V3 + V_PWHI] = 0x08;
    SID[V3 + V_AD]   = 0x18;
    SID[V3 + V_SR]   = 0x86;
    SID[V3 + V_CTRL] = WAVE_PULSE;

    SID[VOLUME] = SND_VOLUME;

    tune       = 0;
    tune_rows  = 0;
    tune_pos   = 0;
    tune_timer = 0;
}

void snd_hush(void)
{
    tune = 0;
    voice_gate_off();
    voice1_noise();
}

void snd_enable(unsigned char on)
{
    on_flag = on ? 1 : 0;
    if (!on_flag) {
        snd_hush();
        SID[VOLUME] = 0x00;
    } else {
        SID[VOLUME] = SND_VOLUME;
    }
}

unsigned char snd_enabled(void)
{
    return on_flag;
}

unsigned char snd_busy(void)
{
    return (unsigned char)(tune != 0);
}

void snd_click(void)
{
    if (!on_flag || tune != 0) {
        return;                 /* a tune owns the voice while it plays */
    }
    /* Gate down then up is what retriggers the envelope. Two stores. */
    SID[V1 + V_CTRL] = WAVE_NOISE;
    SID[V1 + V_CTRL] = WAVE_NOISE | GATE;
}

void snd_play(unsigned char which)
{
    if (!on_flag) {
        return;
    }
    if (which == SND_START) {
        tune      = tune_start;
        tune_rows = sizeof(tune_start) / sizeof(tune_start[0]);
    } else if (which == SND_DONE) {
        tune      = tune_done;
        tune_rows = sizeof(tune_done) / sizeof(tune_done[0]);
    } else {
        snd_hush();
        return;
    }
    voice1_pulse();
    tune_pos   = 0;
    tune_timer = 0;
}

/* Called once a frame from the interrupt. Everything it does is stores
 * into the SID, so the cost is the same every frame and small.
 */
void snd_tick(void)
{
    const struct row *r;
    unsigned int f;

    if (tune == 0) {
        return;
    }

    if (tune_timer != 0) {
        --tune_timer;
        if (tune_timer == 2) {
            voice_gate_off();   /* a short gap, so repeated notes separate */
        }
        return;
    }

    if (tune_pos >= tune_rows) {
        tune = 0;
        voice_gate_off();
        voice1_noise();         /* give the stepper its voice back */
        return;
    }

    r = &tune[tune_pos];
    ++tune_pos;
    tune_timer = r->len;

    f = note_freq(r->v1);
    SID[V1 + V_FLO] = (unsigned char)(f & 0xff);
    SID[V1 + V_FHI] = (unsigned char)(f >> 8);
    f = note_freq(r->v2);
    SID[V2 + V_FLO] = (unsigned char)(f & 0xff);
    SID[V2 + V_FHI] = (unsigned char)(f >> 8);
    f = note_freq(r->v3);
    SID[V3 + V_FLO] = (unsigned char)(f & 0xff);
    SID[V3 + V_FHI] = (unsigned char)(f >> 8);

    if (r->v1 != 0) {
        SID[V1 + V_CTRL] = WAVE_PULSE | GATE;
        SID[V2 + V_CTRL] = WAVE_PULSE | GATE;
        SID[V3 + V_CTRL] = WAVE_PULSE | GATE;
    }
}
