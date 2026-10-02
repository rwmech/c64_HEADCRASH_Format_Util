/* snd.h - drive noise and music on the SID.
 *
 * The stepper tick, a 24 ms noise burst every time the head moves a track,
 * and two tunes: the opening of Beethoven's Fifth when a format starts and
 * the first phrase of the Ode to Joy when it finishes.
 *
 * Nothing here blocks. A click is gated and left to its own decay; a tune
 * is advanced a row at a time by snd_tick() from the interrupt, so the
 * format loop never waits on a note.
 *
 * The master volume is half, on purpose. This is a utility, not a game.
 *
 * (C) 2026 Robert Mech. Licence MIT.
 */

#ifndef SND_H
#define SND_H

#define SND_NONE  0
#define SND_START 1
#define SND_DONE  2

/* Set the SID up and leave it silent. */
void snd_init(void);

/* Sound on or off as a whole. Off silences what is playing and keeps
 * anything else from starting until it is turned back on.
 */
void snd_enable(unsigned char on);
unsigned char snd_enabled(void);

/* One stepper tick. Fire and forget, and ignored while a tune has the
 * voice.
 */
void snd_click(void);

/* Start one of the tunes. Returns immediately; it plays from the tick. */
void snd_play(unsigned char which);

/* Whether a tune is still playing. */
unsigned char snd_busy(void);

/* Advance the player one frame. Call this from the interrupt and from
 * nowhere else.
 */
void snd_tick(void);

/* Everything quiet, without changing whether sound is enabled. */
void snd_hush(void);

#endif /* SND_H */
