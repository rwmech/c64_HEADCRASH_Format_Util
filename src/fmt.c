/* fmt.c - low level, track at a time formatting engine.
 *
 * Both drives can already format; neither will tell you how far it has got,
 * because the DOS sits in a wait loop for the whole job. So we take the
 * drive's own formatter and run it one track per job, which puts the loop
 * on this side of the cable where it can be drawn and retried.
 *
 *   1541  no per track format job exists, so a small gate is uploaded in
 *         front of the ROM formatter's re-entry point. See
 *         src/drivecode_1541.s.
 *   1581  FORMATDK (job $F0) already walks cylinders and stops when the
 *         current cylinder reaches the end marker at $8F, so pointing start
 *         and end at the same cylinder is all it takes.
 *
 * The two drives also differ in how a finished job is noticed, and the
 * difference is forced by the hardware rather than chosen:
 *
 *   1581  stays responsive while it formats, so the job slot is simply
 *         polled until the controller clears the busy bit.
 *   1541  writes a whole track inside one pass of its controller
 *         interrupt and answers nothing on the serial bus for the duration.
 *         Its ATN acknowledge is done in hardware, so from the C64 it still
 *         looks present and willing, and anything sent to it runs into the
 *         KERNAL's wait at $ED5A, which has no timeout: the machine hangs
 *         outright. There is no signal to wait for, so the host waits out a
 *         measured worst case instead and only then asks. See docs/DESIGN.md.
 *
 * (C) 2026 Robert Mech. Licence MIT.
 *
 * Required libraries: cc65 C library.
 */

#include <string.h>
#include "dos.h"
#include "fmt.h"
#include "drivecode_1541.h"

/* --- 1541 ROM and zero page addresses --------------------------------- */
#define A41_FTNUM   0x0051u /* track the ROM formatter is working on */
#define A41_MASTID  0x0012u /* master disk ID, drive 0 (ID1, ID2)    */
#define A41_RETRY   0x0620u /* formatter's own retry counter         */
#define A41_SLOT    3u      /* buffer 3, as the ROM's own N: uses    */

/* How long one 1541 track can take, in video frames of about 17 ms.
 * Measured under VICE with a 1541 formatting back to back tracks: about
 * 190 frames per track with the motor already running, and about 210 more
 * if it has to spin up first, and a real 1541 formats a whole disk in
 * about eighty seconds, which is a shade over two seconds a track. The
 * default allows four, because overrunning it means talking to a deaf
 * drive and that hangs the machine rather than failing. Adjustable from
 * the interface, because the drive it runs on is the only authority.
 */
/* How long a 1541 track is allowed, in frames.
 *
 * There is still nothing to poll. tests/t_busy.c starts a track and then
 * asks the drive 3000 times, across the whole write, whether it is there:
 * it said no five times. The addressing sequence that tells an empty
 * device number from a real one cannot tell a busy drive from an idle one,
 * so that avenue is closed along with the other three.
 *
 * Which leaves the clock, and the clock has to be generous, because too
 * high only costs time and too low hangs the machine with no way back.
 * VICE writes a track in about 156 frames; real hardware has exceeded four
 * seconds around track 25, where the drive changes speed zone. Six is the
 * default now, and F3 takes it from one second to fifteen.
 */
#define A41_TRACK_WAIT 360u

unsigned int fmt_track_wait = A41_TRACK_WAIT;


/* --- 1581 zero page and work area ------------------------------------- */
#define A81_ENDCYL  0x008fu /* FORMATDK stops when current == this   */
#define A81_CURTRK  0x01bcu /* controller's current cylinder, buf 0  */
#define A81_SLOT    0u      /* buffer 0, as the ROM's own N: uses    */

/* Geometry the 1581 formatter expects, mirroring what the ROM's N: command
 * sets up at $BD7C before it issues FORMATDK. Values read out of
 * dos1581-318045-02.bin, not from memory.
 */
#define A81_NSECT   0x0075u /* 40 logical sectors per track          */
#define A81_DENS    0x0091u /* density index 2                       */
#define A81_SEC0    0x0092u /* first physical sector    (10)         */
#define A81_SECN    0x0093u /* last physical sector     (10)         */
#define A81_SIDES   0x0094u /* sides per cylinder       (1 -> 2)     */
#define A81_FILL    0x009bu /* format filler byte       ($e5)        */
#define A81_GAP     0x009au /* format gap length        ($26)        */
#define A81_SECNV   0x01f0u /* shadow of $93                         */
#define A81_SIDESV  0x01efu /* shadow of $94                         */

#define JOB_RECAL   0xc0u   /* 1581: recalibrate, issued before $f0  */

/* How long the 1581's own N: takes, in frames. A whole disk, both sides,
 * measured at about 45 s under VICE; this is comfortably past it.
 */

/* One sector's worth of scratch, static because cc65 keeps locals on a
 * software stack that this would not fit on.
 */
/* The staging buffer for a sector, parked in the free RAM above the copy
 * of the character set at $C000 rather than in BSS. Nothing else wants
 * $C800, the VIC cannot see it from bank 1, and a quarter of a kilobyte
 * back is a quarter of a kilobyte the C stack does not have to fight for.
 * The program had squeezed the stack down to make room and the 1581 pass
 * started failing in a different place on every run, which is what a
 * stack running into BSS looks like.
 */
#define sec ((unsigned char *)0xc800)

unsigned char fmt_seed_status;
unsigned char fmt_init_status;

/* ---------------------------------------------------------------------- */

unsigned char fmt_begin(unsigned char id1, unsigned char id2)
{
    unsigned char ids[2];

    if (dos_drive_type == DRV_1541) {
        /* Every 1541 sector header carries the disk ID, and the formatter
         * takes it from the master ID at $12/$13 (ROM $FC53).
         */
        ids[0] = id1;
        ids[1] = id2;
        dos_mw(A41_MASTID, ids, 2);

        /* Up to 32 bytes per M-W, so the gate goes up in two pieces. */
        dos_mw(DC1541_ADDR, dc1541, 16);
        dos_mw(DC1541_ADDR + 16, dc1541 + 16, DC1541_SIZE - 16);

        /* Run the gate once with FTNUM = $ff and the end track at 1. The
         * ROM's init pass runs (head to track 1, timings, bit rate) and
         * then the gate ends the job before any track is written, so from
         * here on every track takes the same path, and the motor is
         * already up to speed when the first one starts.
         */
        dos_poke(A41_FTNUM, 0xff);
        dos_poke(DC1541_ENDTRK, 1);
        dos_job_start(A41_SLOT, JOB_EXEC, 1, 0);
        dos_delay_long(fmt_track_wait);
        dos_job_status(A41_SLOT);
        return 1;
    }

    if (dos_drive_type == DRV_1581) {
        /* The 1581 writes MFM headers that hold no disk ID, so id1/id2 only
         * matter later, in the directory header sector.
         */
        dos_poke(A81_NSECT,  0x28);
        dos_poke(A81_DENS,   0x02);
        dos_poke(A81_SEC0,   0x0a);
        dos_poke(A81_SECN,   0x0a);
        dos_poke(A81_SECNV,  0x0a);
        dos_poke(A81_SIDES,  0x01);
        dos_poke(A81_SIDESV, 0x01);
        dos_poke(A81_FILL,   0xe5);
        dos_poke(A81_GAP,    0x26);

        /* Recalibrate so the controller's idea of where the head is agrees
         * with ours before we start pinning cylinders.
         */
        dos_job(A81_SLOT, JOB_RECAL, 1, 0);
        return 1;
    }

    return 0; /* nothing we know how to drive at this level */
}

/* The first half of fmt_track for a 1541: set the gate up and start the
 * job, without waiting for it. Diagnostics use this to ask what the drive
 * looks like from the bus while it is actually writing a track.
 */
void fmt_track_start(unsigned char track)
{
    if (dos_drive_type != DRV_1541) {
        return;
    }
    dos_poke(DC1541_ENDTRK, (unsigned char)(track + 1));
    dos_poke(A41_RETRY, 1);
    dos_poke(A41_FTNUM, track);
    dos_job_start(A41_SLOT, JOB_EXEC, track, 0);
}

unsigned char fmt_track(unsigned char track)
{
    if (dos_drive_type == DRV_1541) {
        dos_poke(DC1541_ENDTRK, (unsigned char)(track + 1));
        /* One attempt per job: the UI owns the retry loop, so the ROM's
         * own retry counter is pinned at 1 and errors come straight back.
         */
        dos_poke(A41_RETRY, 1);
        dos_poke(A41_FTNUM, track);
        dos_job_start(A41_SLOT, JOB_EXEC, track, 0);

        dos_delay_long(fmt_track_wait);
        return dos_job_status(A41_SLOT);
    }

    if (dos_drive_type == DRV_1581) {
        unsigned char cyl = (unsigned char)(track - 1);

        /* Logical track 1..80 is physical cylinder 0..79, both sides. The
         * controller does that conversion itself when it takes the track
         * out of the job header, but FORMATDK's end marker at $8f is
         * compared against the cylinder, so the two are set in different
         * units on purpose. Checked on a $55 filled image: header 5 with
         * $8f = 4 rewrites logical track 5 and nothing else.
         */
        dos_poke(A81_CURTRK, cyl);
        dos_poke(A81_ENDCYL, cyl);
        return dos_job(A81_SLOT, JOB_FORMAT, track, 0);
    }

    return DOS_ERR_TIMEOUT;
}

void fmt_end(void)
{
    if (dos_drive_type == DRV_1541) {
        /* Leave FTNUM the way the ROM leaves it after a normal format. */
        dos_poke(A41_FTNUM, 0xff);
    }
}

/* ---------------------------------------------------------------------- */
/* the 1581's own format                                                   */
/* ---------------------------------------------------------------------- */

unsigned char fmt_native_start(const char *name,
                               unsigned char id1, unsigned char id2)
{
    unsigned char cmd[24];
    unsigned char i = 0;
    unsigned char c;

    cmd[i++] = 0x4e; /* 'N' */
    cmd[i++] = 0x30; /* '0' */
    cmd[i++] = 0x3a; /* ':' */
    while ((c = (unsigned char)*name++) != 0 && i < 17) {
        cmd[i++] = c;
    }
    cmd[i++] = 0x2c; /* ',' */
    cmd[i++] = id1;
    cmd[i++] = id2;
    dos_cmd_raw(cmd, i);
    return 1;
}

unsigned int fmt_native_frames(void)
{
    /* A 1541 takes about eighty seconds over 35 tracks, a 1581 about
     * forty over 80. Both at sixty frames a second.
     */
    if (dos_drive_type == DRV_1581) {
        return 2700u;
    }
    return 5100u;
}

/* One attempt at the drive's error channel, with no waiting of its own.
 *
 * This used to be a loop of two hundred attempts a second apart inside
 * fmt.c, which is up to three minutes during which the program drew
 * nothing, read no keys and looked for all the world like it had hung.
 * The waiting belongs to the caller, which has a screen to keep moving and
 * a keyboard to read.
 */
unsigned char fmt_native_poll(void)
{
    return dos_status();
}

/* ---------------------------------------------------------------------- */
/* filesystem                                                              */
/* ---------------------------------------------------------------------- */

/* N: with an ID formats the disk; N: without one only rebuilds the BAM and
 * directory on a disk that is already formatted, which is exactly the half
 * we still need once the track pass has finished. It is also the half worth
 * leaving to the DOS, which knows its own BAM layout.
 */
/* How long to leave the drive strictly alone after the directory command
 * before anything is sent to it. The 1581 formats the whole surface over
 * again here, so it is deaf for a while in the middle of that; the 1541
 * only writes a BAM and a directory, which is a handful of sectors.
 */
unsigned int fmt_fs_settle(void)
{
    return (unsigned int)(dos_drive_type == DRV_1581 ? 600u : 240u);
}

/* Ask once whether the directory is written. Safe on a 1581, which answers
 * the bus throughout; on a 1541 it must not be called until the settle
 * above has run out, because a 1541 writing a sector is deaf and the
 * KERNAL has no timeout for it.
 */
unsigned char fmt_fs_poll(void)
{
    return dos_status();
}

unsigned char fmt_fs_start(const char *name,
                           unsigned char id1, unsigned char id2)
{
    unsigned char cmd[24];
    unsigned char i = 0;
    unsigned char c;

    if (dos_drive_type == DRV_1541) {
        /* A freshly laid down track has $01 in every byte, so the
         * directory header sector carries no DOS version marker and the
         * DOS refuses the disk: N: without an ID answers 73, and so does
         * every block command, which is what would otherwise write the
         * byte. The job queue goes round the outside of all that, and once
         * the header sector reads as a 1541 disk again the DOS is happy to
         * build the real BAM and directory on top of it.
         */
        unsigned int t;

        for (t = 0; t < 256u; ++t) {
            sec[t] = 0;
        }
        sec[0] = 18;   /* directory starts at 18/1 */
        sec[1] = 1;
        sec[2] = 0x41; /* 'A', CBM DOS V2.6 */
        fmt_seed_status = dos_write_sector_job(dos_dir_track, 0, sec);

        /* Make the drive look at the disk it is actually holding. I0 makes
         * it seek and read the BAM, so it is deaf for a moment afterwards
         * and the status channel cannot be read until it is done.
         */
        dos_cmd("I0");
        dos_delay_long(120);
        fmt_init_status = dos_status();

        cmd[i++] = 0x4e; /* 'N' */
        cmd[i++] = 0x30; /* '0' */
        cmd[i++] = 0x3a; /* ':' */
        while ((c = (unsigned char)*name++) != 0 && i < 19) {
            cmd[i++] = c;
        }
        dos_cmd_raw(cmd, i);
        return 1;
    }

    /* The 1581's seed was written by the format itself: the directory
     * track was laid down with 'D' as its filler, so the DOS already sees
     * a disk it recognises and will build the BAM and directory on it
     * without formatting the surface all over again.
     */
    /* The 1581's own N: with an ID. Its block commands stay refused on a
     * disk we laid down ourselves, and its job queue stages writes through
     * a 512 byte physical sector that is re-read before every write, so
     * there is no way to plant a seed the way the 1541 gets one. Laying
     * the directory track down with 'D' as its filler gets I0 to accept
     * the disk but not the DOS to build on it. So the drive formats the
     * surface a second time in order to write a filesystem it trusts.
     */
    cmd[i++] = 0x4e; /* 'N' */
    cmd[i++] = 0x30; /* '0' */
    cmd[i++] = 0x3a; /* ':' */
    while ((c = (unsigned char)*name++) != 0 && i < 17) {
        cmd[i++] = c;
    }
    cmd[i++] = 0x2c; /* ',' */
    cmd[i++] = id1;
    cmd[i++] = id2;
    dos_cmd_raw(cmd, i);
    return 1;
}

/* Mark every sector of a bad track as allocated, so the disk is still
 * usable and nothing is ever written where the surface failed. Done after
 * the directory exists, when the block commands work again.
 *
 * 1541: one BAM at 18/0, four bytes per track from offset 4.
 * 1581: two BAMs, 40/1 for tracks 1 to 40 and 40/2 for 41 to 80, six bytes
 *       per track from offset 16.
 */
unsigned char fmt_lock_out(const unsigned char *tracks, unsigned char count)
{
    unsigned char i, t, st;
    unsigned char bam_sector;
    unsigned char last = 0;
    unsigned char dirty = 0;

    if (count == 0) {
        return 0;
    }

    if (dos_drive_type == DRV_1541) {
        st = dos_read_sector(dos_dir_track, 0, sec);
        if (st != 0) {
            return st;
        }
        for (i = 0; i < count; ++i) {
            t = tracks[i];
            if (t >= 1 && t <= 35) {
                unsigned char o = (unsigned char)(4 + (t - 1) * 4);
                sec[o]     = 0; /* no blocks free on this track */
                sec[o + 1] = 0;
                sec[o + 2] = 0;
                sec[o + 3] = 0;
            }
        }
        return dos_write_sector(dos_dir_track, 0, sec);
    }

    if (dos_drive_type == DRV_1581) {
        /* Walk the two BAM sectors in turn rather than re-reading one per
         * bad track. The list arrives in track order.
         */
        for (bam_sector = 1; bam_sector <= 2; ++bam_sector) {
            unsigned char lo = (unsigned char)(bam_sector == 1 ? 1 : 41);
            unsigned char hi = (unsigned char)(bam_sector == 1 ? 40 : 80);

            dirty = 0;
            for (i = 0; i < count; ++i) {
                if (tracks[i] >= lo && tracks[i] <= hi) {
                    dirty = 1;
                    break;
                }
            }
            if (!dirty) {
                continue;
            }

            st = dos_read_sector(dos_dir_track, bam_sector, sec);
            if (st != 0) {
                return st;
            }
            for (i = 0; i < count; ++i) {
                t = tracks[i];
                if (t >= lo && t <= hi) {
                    unsigned char o =
                        (unsigned char)(16 + (t - lo) * 6);
                    unsigned char k;
                    for (k = 0; k < 6; ++k) {
                        sec[o + k] = 0;
                    }
                }
            }
            st = dos_write_sector(dos_dir_track, bam_sector, sec);
            if (st != 0) {
                return st;
            }
        }
        (void)last;
        return 0;
    }

    return DOS_ERR_TIMEOUT;
}
