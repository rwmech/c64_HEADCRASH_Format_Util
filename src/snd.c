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
#define E2  5
#define F2  6
#define G2  8
#define A2  10

#define C3  13
#define D3  15
#define EB3 16
#define E3  17
#define F3  18
#define FS3 19
#define G3  20
#define A3  22
#define B3  24

#define C4  25
#define D4  27
#define EB4 28
#define E4  29
#define F4  30
#define FS4 31
#define G4  32
#define GS4 33
#define A4  34
#define B4  36

#define C5  37
#define CS5 38
#define D5  39
#define DS5 40
#define E5  41
#define F5  42
#define G5  44
#define A5  46
#define B5  48

#define C6  49
#define E6  53

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

/* One row is a chord and how many frames to hold it. A voice set to 0 is
 * silent for that row, which is how the fast runs keep the harmony and the
 * bass out of the way without a note per semiquaver.
 *
 * All six are out of copyright by a century or more. Every finishing tune
 * ends on a rising figure into a held tonic chord, so it sounds finished
 * rather than stopped.
 */
struct row {
    unsigned char v1, v2, v3, len;
};

/* Beethoven, Symphony No. 5, the opening. Three short and one long,
 * twice, in octaves. About two seconds.
 */
static const struct row tune_5th[] = {
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

/* Mozart, Eine kleine Nachtmusik, the opening two bars: the rising arpeggio
 * on G, then the same shape on D.
 */
static const struct row tune_nacht[] = {
    { G4,  G3,  G2,  16 },
    { D4,  D3,  G2,  16 },
    { G4,  G3,  G2,   8 },
    { D4,  D3,  G2,   8 },
    { G4,  G3,  G2,   8 },
    { B4,  B3,  G2,   8 },
    { D5,  D4,  G2,  20 },
    { 0,   0,   0,    6 },
    { D5,  D4,  D2,  16 },
    { A4,  A3,  D2,  16 },
    { D4,  D3,  D2,   8 },
    { A4,  A3,  D2,   8 },
    { D4,  D3,  D2,   8 },
    { FS4, FS3, D2,   8 },
    { A4,  A3,  D2,  22 },
    { 0,   0,   0,    6 }
};

/* Tchaikovsky, 1812 Overture, the triumphal theme from the close. Used at
 * both ends, which is why it carries its own flourish.
 */
static const struct row tune_1812[] = {
    { G4,  D4,  G2,  12 },
    { C5,  E4,  C3,  20 },
    { C5,  E4,  C3,   8 },
    { B4,  D4,  G2,   8 },
    { C5,  E4,  C3,  16 },
    { D5,  F4,  G2,  16 },
    { E5,  G4,  C3,  24 },
    { D5,  F4,  G2,   8 },
    { C5,  E4,  C3,  16 },
    { B4,  D4,  G2,  16 },
    { C5,  E4,  C3,  28 },
    { 0,   0,   0,    6 },
    { E5,  C4,  C3,   8 },
    { G5,  E4,  C3,   8 },
    { C6,  G4,  C3,  40 },
    { 0,   0,   0,    8 }
};

/* Beethoven, Symphony No. 9, the Ode to Joy, first phrase, with a third
 * below it and the root underneath, resolved onto the tonic.
 */
static const struct row tune_joy[] = {
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
    { C5, E4, C3,  8 },
    { E5, G4, C3,  8 },
    { G5, C5, C3, 40 },
    { 0,  0,  0,   8 }
};

/* Mozart, Rondo alla Turca, the opening. Semiquaver turns on to each
 * accented note, which is the whole character of it.
 */
static const struct row tune_turk[] = {
    { B4,  0,   0,   5 },
    { A4,  0,   0,   5 },
    { GS4, 0,   0,   5 },
    { A4,  0,   0,   5 },
    { C5,  A3,  A2, 12 },
    { D5,  0,   0,   5 },
    { C5,  0,   0,   5 },
    { B4,  0,   0,   5 },
    { C5,  0,   0,   5 },
    { E5,  C4,  A2, 12 },
    { F5,  0,   0,   5 },
    { E5,  0,   0,   5 },
    { DS5, 0,   0,   5 },
    { E5,  0,   0,   5 },
    { B5,  GS4, E2, 10 },
    { A5,  0,   0,   5 },
    { G5,  0,   0,   5 },
    { A5,  0,   0,   5 },
    { B5,  0,   0,   5 },
    { A5,  0,   0,   5 },
    { G5,  0,   0,   5 },
    { A5,  0,   0,   5 },
    { C6,  A4,  A2, 20 },
    { 0,   0,   0,   6 },
    { A5,  E4,  A2,  8 },
    { C6,  E4,  A2,  8 },
    { E6,  A4,  A2,  8 },
    { A5,  CS5, A2, 40 },
    { 0,   0,   0,   8 }
};

/* Offenbach, Galop Infernal, which nobody calls that. Straight quavers,
 * which is why it runs.
 */
static const struct row tune_can[] = {
    { G4, 0,  0,   7 },
    { C5, E4, C3,  7 },
    { C5, E4, C3,  7 },
    { C5, E4, C3,  7 },
    { C5, E4, C3,  7 },
    { C5, E4, C3,  7 },
    { D5, F4, C3,  7 },
    { E5, G4, C3,  7 },
    { C5, E4, C3,  7 },
    { D5, F4, G2,  7 },
    { D5, F4, G2,  7 },
    { D5, F4, G2,  7 },
    { D5, F4, G2,  7 },
    { D5, F4, G2,  7 },
    { E5, G4, G2,  7 },
    { F5, A4, G2,  7 },
    { D5, F4, G2,  7 },
    { E5, C4, C3,  7 },
    { E5, C4, C3,  7 },
    { E5, C4, C3,  7 },
    { E5, C4, C3,  7 },
    { F5, A4, F3,  7 },
    { E5, G4, C3,  7 },
    { D5, F4, G2,  7 },
    { C5, E4, C3, 20 },
    { 0,  0,  0,   6 },
    { E5, C4, C3,  8 },
    { G5, E4, C3,  8 },
    { C6, G4, C3, 40 },
    { 0,  0,  0,   8 }
};

/* Which tune goes where. The 1812 is in both pools because it works at
 * either end.
 */
struct tune {
    const struct row *rows;
    unsigned char     n;
};

#define ROWS(t) { t, (unsigned char)(sizeof(t) / sizeof(t[0])) }

static const struct tune pool_start[3] = {
    ROWS(tune_5th), ROWS(tune_nacht), ROWS(tune_1812)
};

static const struct tune pool_done[4] = {
    ROWS(tune_joy), ROWS(tune_turk), ROWS(tune_can), ROWS(tune_1812)
};

/* Enough randomness to not play the same one twice in a row. The raster
 * register is wherever the beam happens to be when the key was pressed,
 * which for this purpose is as good as a dice.
 */
static unsigned char seed;

static unsigned char pick(unsigned char n)
{
    seed = (unsigned char)(seed * 5u
                           + *(volatile unsigned char *)0xd012
                           + *(volatile unsigned char *)0x00a2
                           + 17u);
    return (unsigned char)(seed % n);
}

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
        const struct tune *t = &pool_start[pick(3)];
        tune      = t->rows;
        tune_rows = t->n;
    } else if (which == SND_DONE) {
        const struct tune *t = &pool_done[pick(4)];
        tune      = t->rows;
        tune_rows = t->n;
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
