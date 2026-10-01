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

/* Start a track and return immediately, for diagnostics. */
void          fmt_track_start(unsigned char track);

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
