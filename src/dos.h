/* dos.h - IEC / CBM DOS layer for HEADCRASH Format Util.
 *
 * Talks to a drive on the serial bus through the KERNAL, using the command
 * channel (secondary address 15) for M-R, M-W, M-E and ordinary DOS commands,
 * and the drive's own job queue for per-track work.
 *
 * (C) 2026 Robert Mech. Licence MIT.
 */

#ifndef DOS_H
#define DOS_H

/* Drive families we can tell apart from the ROM reset vector. */
#define DRV_UNKNOWN 0
#define DRV_1541    1
#define DRV_1571    2
#define DRV_1581    3

/* dos_status() / dos_job() return values are raw DOS error codes.
 * These are the ones the UI cares about by name.
 */
#define DOS_OK            0
#define DOS_ERR_TIMEOUT 255 /* no drive answered at all */

/* How many times dos_job() reads the job slot before giving up. Each poll
 * is a short M-R round trip, so this is a few seconds: long enough for the
 * slowest track, short enough that a wedged drive does not hang the UI.
 */
#define DOS_JOB_POLLS   400u

/* Read a job slot until the controller has finished with it. Only call
 * this when the drive is known to be answering the bus.
 */
unsigned char dos_job_status(unsigned char slot);


/* Geometry of the selected drive, filled in by dos_identify(). */
extern unsigned char dos_drive_type;
extern unsigned char dos_tracks;   /* 35 or 80 */
extern unsigned char dos_dir_track;/* 18 or 40 */

/* Wait n video frames (about 17 ms each on NTSC). */
void dos_delay(unsigned char frames);
void dos_delay_long(unsigned int frames);

/* --- channel handling ------------------------------------------------- */

/* Open the command channel on device dev (8..11). Returns 1 on success. */
/* Whether anything answers at this device number. Safe to call on an
 * empty bus: it addresses nobody and leaves nothing open.
 */
unsigned char dos_present(unsigned char dev);

unsigned char dos_open(unsigned char dev);
void          dos_close(void);

/* Send a NUL-terminated DOS command, e.g. "I0" or "N0:WORKDISK". */
void          dos_cmd(const char *cmd);

/* Send len raw bytes as one command (binary safe, used for M-R/M-W/M-E). */
void          dos_cmd_raw(const unsigned char *buf, unsigned char len);

/* Read the error channel. Returns the numeric DOS code; the full message is
 * left in dos_msg for display. dos_err_track / dos_err_sector hold the two
 * trailing numbers.
 */
extern char          dos_msg[40];
extern unsigned char dos_err_track;
extern unsigned char dos_err_sector;
unsigned char dos_status(void);

/* --- drive memory ----------------------------------------------------- */

/* Read len bytes (1..32) of drive memory into buf. Returns 1 on success. */
unsigned char dos_mr(unsigned int addr, unsigned char *buf, unsigned char len);


/* Write len bytes (1..32) of drive memory from buf. */
void          dos_mw(unsigned int addr, const unsigned char *buf,
                     unsigned char len);
void          dos_poke(unsigned int addr, unsigned char val);

/* --- sectors ---------------------------------------------------------- */

/* Read one 256 byte sector using the block commands. Returns the DOS
 * error code, 0 == OK.
 */
unsigned char dos_read_sector(unsigned char track, unsigned char sector,
                              unsigned char *buf);

/* Write one 256 byte sector using the block commands. Returns the DOS
 * error code, 0 == OK.
 */
unsigned char dos_write_sector(unsigned char track, unsigned char sector,
                               const unsigned char *buf);

/* Write one sector through the job queue, bypassing the DOS's own idea of
 * whether the disk is usable. 1541 only. Returns the job status, 1 == OK.
 */
unsigned char dos_write_sector_job(unsigned char track, unsigned char sector,
                                   const unsigned char *buf);

/* --- identification --------------------------------------------------- */

/* Probe the drive: sets dos_drive_type, dos_tracks, dos_dir_track.
 * Returns the drive type. Safe to call on a drive with no disk in it.
 */
unsigned char dos_identify(void);

/* Human readable DOS name for the panel, e.g. "CBM DOS V2.6 1541". */
const char   *dos_type_name(void);

/* --- job queue -------------------------------------------------------- */

/* Run one job in the drive's job queue and wait for it to finish.
 * slot   - job queue slot (buffer number)
 * code   - job code, e.g. 0x80 read, 0x90 write, 0xE0 execute, 0xF0 format
 * track  - track for the job header
 * sector - sector for the job header
 * Returns the DOS error code the controller left in the slot (1 == OK).
 */
void dos_job_start(unsigned char slot, unsigned char code,
                   unsigned char track, unsigned char sector);

unsigned char dos_job(unsigned char slot, unsigned char code,
                      unsigned char track, unsigned char sector);

/* Job codes we use. */
#define JOB_READ    0x80
#define JOB_WRITE   0x90
#define JOB_SEEK    0xB0
#define JOB_BUMP    0xC0
#define JOB_EXEC    0xE0
#define JOB_FORMAT  0xF0 /* 1581 FORMATDK only */

#endif /* DOS_H */
