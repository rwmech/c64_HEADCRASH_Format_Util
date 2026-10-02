/* snd.h - drive noise on the SID.
 *
 * A 1541 makes two sounds worth having: the spindle motor, a low rumble
 * that runs the whole time the drive is working, and the stepper, a sharp
 * tick every time the head moves a track. Both are noise waveforms; the
 * difference is the frequency and the envelope.
 *
 * Nothing here blocks. A click is gated and left to its own decay, so it
 * costs two stores and returns immediately, which matters because the
 * clicks are fired from inside the format loop.
 *
 * (C) 2026 Robert Mech. Licence MIT.
 */

#ifndef SND_H
#define SND_H

/* Set the SID up and leave it silent. */
void snd_init(void);

/* Sound on or off as a whole. Off silences what is playing and keeps
 * anything else from starting until it is turned back on.
 */
void snd_enable(unsigned char on);
unsigned char snd_enabled(void);

/* The spindle motor, held for as long as the drive is working. */
void snd_motor(unsigned char on);

/* One stepper tick. Fire and forget. */
void snd_click(void);

/* Everything quiet, without changing whether sound is enabled. */
void snd_hush(void);

#endif /* SND_H */
