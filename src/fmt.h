/* fmt.h - low level, track at a time formatting engine.
 *
 * The drive's own ROM does the GCR work; this drives it one track at a
 * time so the host can show progress and retry a track that fails.
 *
 * (C) 2026 Robert Mech. Licence GPL-3.0-or-later.
 */

#ifndef FMT_H
#define FMT_H

/* Prepare the drive for a track by track format run.
 * id1/id2 are the two disk ID characters (PETSCII, unshifted).
 * Returns 1 if the drive is one we can drive this way.
 */
unsigned char fmt_begin(unsigned char id1, unsigned char id2);

/* Format one logical track (1..dos_tracks). Returns the DOS error code,
 * 1 == OK. No retries happen in here; the caller owns the retry policy so
 * it can draw each attempt.
 */
unsigned char fmt_track(unsigned char track);

/* How long the host waits for a 1541 track before it speaks to the drive
 * again, in video frames. There is nothing to poll while a 1541 formats
 * (see docs/DESIGN.md), so this is a measured worst case rather than a
 * signal, and it is the one number in the program that an emulator cannot
 * settle. Too high only wastes time; too low hangs the machine.
 */
extern unsigned int fmt_track_wait;

/* Start a track and return immediately, for diagnostics. */
void          fmt_track_start(unsigned char track);

/* --- the drive's own format ------------------------------------------ */
/*
 * Straight N:, the way any other program would do it. The drive formats
 * the whole disk in one job, which is as fast as the hardware goes and
 * gives nothing back while it runs: a 1541 answers nothing at all, and a
 * 1581 answers but has no counter worth reading. So this is for when the
 * disk matters more than the view of it, and the track at a time path is
 * for when a bad disk needs finding.
 */
unsigned char fmt_native_start(const char *name,
                               unsigned char id1, unsigned char id2);

/* Roughly how long that takes on this drive, in video frames. Measured,
 * not guessed, but it is an estimate and the interface says so.
 */
unsigned int fmt_native_frames(void);

/* Final DOS status, once the time is up. */
unsigned char fmt_native_end(void);

/* Release anything fmt_begin() set up in the drive. */
void          fmt_end(void);

/* Write the BAM and directory for an already low level formatted disk by
 * asking the DOS to do it (N: with no ID is a directory-only format).
 * name must already be unshifted PETSCII, up to 16 characters.
 * Returns the DOS error code, 0 == OK.
 */
unsigned char fmt_filesystem(const char *name,
                             unsigned char id1, unsigned char id2);

/* DOS status left by the directory-header seed written before the BAM is
 * rebuilt; for diagnostics.
 */
extern unsigned char fmt_seed_status;
extern unsigned char fmt_init_status;

/* Mark every sector of the listed tracks as allocated in the BAM, so a
 * disk with unusable tracks is still safe to write files to.
 * Returns the DOS error code, 0 == OK.
 */
unsigned char fmt_lock_out(const unsigned char *tracks, unsigned char count);

#endif /* FMT_H */
