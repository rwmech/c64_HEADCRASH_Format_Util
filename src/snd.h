/* snd.h - drive noise on the SID.
 *
 * One sound: the stepper tick, a 24 ms noise burst every time the head
 * moves a track. A held rumble for the spindle was tried and was simply a
 * drone, so there is not one.
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


/* One stepper tick. Fire and forget. */
void snd_click(void);

/* Everything quiet, without changing whether sound is enabled. */
void snd_hush(void);

#endif /* SND_H */
