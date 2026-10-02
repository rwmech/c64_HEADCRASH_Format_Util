/* dos.c - IEC / CBM DOS layer for HEADCRASH Format Util.
 *
 * Everything here goes through the KERNAL serial routines, so it works with
 * stock DOS, JiffyDOS, and (for the plain commands) IEC peripherals.
 *
 * (C) 2026 Robert Mech. Licence MIT.
 *
 * Required libraries: cc65 C library (cbm.h for the KERNAL entry points).
 */

#include <string.h>
#include <cbm.h>
#include "dos.h"

#define CMD_LFN 15u /* logical file for the command channel */
#define BUF_LFN 2u  /* logical file for the buffer channel ("#") */

/* Build with -DDOS_TRACE to leave a breadcrumb in the bottom right corner of
 * the screen at each serial step, so a run that wedges inside a KERNAL call
 * can be read off a screenshot instead of guessed at.
 */
#ifdef DOS_TRACE
#define TRACE(n) (*(unsigned char *)0x07e7 = (n))
unsigned char dos_tag;
#define TAG() (*(unsigned char *)0x07e6 = dos_tag)
#else
#define TRACE(n)
#define TAG()
#endif

unsigned char dos_drive_type = DRV_UNKNOWN;
unsigned char dos_tracks     = 35;
unsigned char dos_dir_track  = 18;

char          dos_msg[40];
unsigned char dos_err_track;
unsigned char dos_err_sector;

static unsigned char dos_dev      = 8;
static unsigned char cmd_is_open  = 0;
static unsigned char buf_is_open  = 0;


/* ---------------------------------------------------------------------- */
/* timing                                                                  */
/* ---------------------------------------------------------------------- */

/* Wait n video frames by watching the raster counter, which keeps working
 * with interrupts off and needs no CIA state of its own. Roughly 17 ms per
 * frame on NTSC, 20 ms on PAL; nothing here needs better than that.
 */
void dos_delay(unsigned char frames)
{
    while (frames--) {
        while (*(volatile unsigned char *)0xd012 != 0) {
        }
        while (*(volatile unsigned char *)0xd012 == 0) {
        }
    }
}

void dos_delay_long(unsigned int frames)
{
    while (frames--) {
        while (*(volatile unsigned char *)0xd012 != 0) {
        }
        while (*(volatile unsigned char *)0xd012 == 0) {
        }
    }
}

/* ---------------------------------------------------------------------- */
/* channel handling                                                        */
/* ---------------------------------------------------------------------- */

/* Is there anything at this device number at all.
 *
 * Two things make this harder than it looks, and both were measured with
 * tests/t_dev.c against a 1581 on 8 and nothing on 9 or 10.
 *
 * The first is that LISTEN alone tells you nothing. Every device on the
 * bus pulls DATA low to acknowledge ATN, not just the one being addressed,
 * so a bare LISTEN followed by a read of ST answers "someone is there" for
 * every device number while any drive is connected. Measured: 0 for 8, 9
 * and 10 alike. The per device answer only appears once ATN is released,
 * because that is when the devices that were not addressed let DATA go and
 * the one that was keeps holding it. So the sequence has to run all the
 * way through a secondary address and an UNLISTEN before ST means
 * anything. Measured that way: 0 for 8, 128 for 9 and 10.
 *
 * The KERNAL's own OPEN is no help either; it returned 0 for all three.
 * That is why the first version of this program never noticed an empty
 * device number and went on to send an M-R into the dark, which left the
 * bus half addressed and stuck the real drive until it was power cycled.
 *
 * The second is that ST is sticky. The KERNAL ORs bit 7 into $90 and never
 * clears it, so without clearing it by hand every device after the first
 * empty one reads as empty too, and cycling past an empty number loses the
 * drive that is really there until the machine is reset. There is no
 * KERNAL call for it, so this writes $90 directly, before and after.
 *
 * Secondary $6F selects channel 15 and nothing more. It opens no file and
 * leaves nothing behind once UNLISTEN has gone out.
 */
unsigned char dos_present(unsigned char dev)
{
    unsigned char st;

    *(volatile unsigned char *)0x90 = 0;

    cbm_k_listen(dev);
    cbm_k_second(0x6f);
    cbm_k_unlsn();
    st = *(volatile unsigned char *)0x90;

    *(volatile unsigned char *)0x90 = 0;

    return (unsigned char)((st & 0x80) ? 0 : 1);
}

unsigned char dos_open(unsigned char dev)
{
    dos_close();
    dos_dev = dev;

    /* Nothing is sent to a device that has not answered its own number. */
    if (!dos_present(dev)) {
        return 0;
    }

    cbm_k_setlfs(CMD_LFN, dev, 15);
    cbm_k_setnam("");
    if (cbm_k_open() != 0) {
        return 0;
    }
    cmd_is_open = 1;

    return 1;
}

void dos_close(void)
{
    cbm_k_clrch();
    if (buf_is_open) {
        cbm_k_close(BUF_LFN);
        buf_is_open = 0;
    }
    if (cmd_is_open) {
        cbm_k_close(CMD_LFN);
        cmd_is_open = 0;
    }
}

void dos_cmd_raw(const unsigned char *buf, unsigned char len)
{
    TAG();
    TRACE(1);
    if (cbm_k_ckout(CMD_LFN) != 0) {
        TRACE(2);
        cbm_k_clrch();
        return;
    }
    TRACE(3);
    while (len--) {
        cbm_k_bsout(*buf++);
    }
    TRACE(4);
    /* CLRCHN sends UNLISTEN, which is what makes the drive act on the
     * command. No CR is needed and none is sent, so M-W data stays binary
     * clean.
     */
    cbm_k_clrch();
}

/* cc65 translates C string literals to PETSCII for the c64 target, so an
 * ASCII 'N' arrives here as $CE (shifted). The DOS command parser wants the
 * unshifted $4E, so fold $C1..$DA back down. Binary payloads never go through
 * this path - they use dos_cmd_raw() directly.
 */
void dos_cmd(const char *cmd)
{
    unsigned char packet[40];
    unsigned char i = 0;
    unsigned char c;

    while ((c = (unsigned char)*cmd++) != 0 && i < sizeof(packet)) {
        if (c >= 0xc1 && c <= 0xda) {
            c = (unsigned char)(c - 0x80);
        }
        packet[i++] = c;
    }
    dos_cmd_raw(packet, i);
}

unsigned char dos_status(void)
{
    unsigned char i = 0;
    unsigned char c;
    unsigned char code;
    unsigned char tries;

    dos_msg[0]     = '\0';
    dos_err_track  = 0;
    dos_err_sector = 0;

    if (cbm_k_chkin(CMD_LFN) != 0) {
        cbm_k_clrch();
        return DOS_ERR_TIMEOUT;
    }
    /* A preceding M-R can leave its trailing CR in the channel, which would
     * read back as an empty line. Take the next line in that case.
     */
    for (tries = 0; tries < 2; ++tries) {
        i = 0;
        for (;;) {
            c = cbm_k_basin();
            if (cbm_k_readst() != 0 || c == 13) {
                break;
            }
            if (i < sizeof(dos_msg) - 1) {
                dos_msg[i++] = (char)c;
            }
        }
        if (i >= 2 || cbm_k_readst() != 0) {
            break;
        }
    }
    dos_msg[i] = '\0';
    cbm_k_clrch();

    if (i < 2) {
        return DOS_ERR_TIMEOUT;
    }

    /* "nn,message,tt,ss" */
    code = (unsigned char)((dos_msg[0] - '0') * 10 + (dos_msg[1] - '0'));

    /* Pull the two trailing numbers out, if they are there. */
    {
        unsigned char commas = 0;
        unsigned char p;
        for (p = 0; p < i; ++p) {
            if (dos_msg[p] == ',') {
                ++commas;
                if (commas == 2 && p + 2 < i) {
                    dos_err_track = (unsigned char)
                        ((dos_msg[p + 1] - '0') * 10 + (dos_msg[p + 2] - '0'));
                }
                if (commas == 3 && p + 2 < i) {
                    dos_err_sector = (unsigned char)
                        ((dos_msg[p + 1] - '0') * 10 + (dos_msg[p + 2] - '0'));
                }
            }
        }
    }
    return code;
}

/* ---------------------------------------------------------------------- */
/* drive memory                                                            */
/* ---------------------------------------------------------------------- */

void dos_mw(unsigned int addr, const unsigned char *buf, unsigned char len)
{
    unsigned char packet[4 + 32];
    unsigned char i;

    if (len > 32) {
        len = 32;
    }
    packet[0] = 0x4d; /* 'M' in PETSCII, written numerically because cc65 */
    packet[1] = 0x2d; /* '-'  maps C character constants to shifted PETSCII */
    packet[2] = 0x57; /* 'W' */
    packet[3] = (unsigned char)(addr & 0xff);
    packet[4] = (unsigned char)(addr >> 8);
    packet[5] = len;
    for (i = 0; i < len; ++i) {
        packet[6 + i] = buf[i];
    }
    dos_cmd_raw(packet, (unsigned char)(6 + len));
}

void dos_poke(unsigned int addr, unsigned char val)
{
    dos_mw(addr, &val, 1);
}

unsigned char dos_mr(unsigned int addr, unsigned char *buf, unsigned char len)
{
    unsigned char packet[6];
    unsigned char i;

    packet[0] = 0x4d; /* 'M' */
    packet[1] = 0x2d; /* '-' */
    packet[2] = 0x52; /* 'R' */
    packet[3] = (unsigned char)(addr & 0xff);
    packet[4] = (unsigned char)(addr >> 8);
    packet[5] = len;
    dos_cmd_raw(packet, 6);

    TRACE(5);
    if (cbm_k_chkin(CMD_LFN) != 0) {
        TRACE(6);
        cbm_k_clrch();
        return 0;
    }
    TRACE(7);
    for (i = 0; i < len; ++i) {
        buf[i] = cbm_k_basin();
        if (cbm_k_readst() != 0) {
            TRACE(8);
            cbm_k_clrch();
            return 0;
        }
    }
    TRACE(9);
    /* Nothing is read past the payload here. The drive can go deaf the
     * instant a job starts, and the KERNAL's byte-in has no timeout once a
     * talker has been addressed, so a speculative read of the trailing CR
     * is a hang waiting to happen. dos_status() skips the stray byte
     * instead.
     */
    cbm_k_clrch();
    return 1;
}

/* ---------------------------------------------------------------------- */
/* sectors                                                                 */
/* ---------------------------------------------------------------------- */

/* Append an unsigned decimal number to a command being built. */
static unsigned char put_num(unsigned char *p, unsigned char n)
{
    unsigned char i = 0;

    if (n >= 100) {
        p[i++] = (unsigned char)(0x30 + n / 100);
        n = (unsigned char)(n % 100);
        p[i++] = (unsigned char)(0x30 + n / 10);
    } else if (n >= 10) {
        p[i++] = (unsigned char)(0x30 + n / 10);
    }
    p[i++] = (unsigned char)(0x30 + n % 10);
    return i;
}

/* Write one 256 byte sector through the buffer channel: fill the drive's
 * buffer, then U2 it out to the disk. Block commands rather than the job
 * queue, so this needs to know nothing about either drive's RAM.
 */
unsigned char dos_write_sector(unsigned char track, unsigned char sector,
                               const unsigned char *buf)
{
    unsigned char cmd[20];
    unsigned char i;
    unsigned int  n;

    /* The buffer channel is opened per write rather than held open, so the
     * DOS never hands out the same drive buffer that the 1541 drivecode
     * lives in while the format pass is running.
     */
    cbm_k_setlfs(BUF_LFN, dos_dev, BUF_LFN);
    cbm_k_setnam("#");
    if (cbm_k_open() != 0) {
        return DOS_ERR_TIMEOUT;
    }
    buf_is_open = 1;

    /* B-P: rewind the buffer so the 256 bytes land at offset 0. */
    i = 0;
    cmd[i++] = 0x42; /* 'B' */
    cmd[i++] = 0x2d; /* '-' */
    cmd[i++] = 0x50; /* 'P' */
    cmd[i++] = 0x3a; /* ':' */
    i += put_num(cmd + i, BUF_LFN);
    cmd[i++] = 0x20; /* ' ' */
    i += put_num(cmd + i, 0);
    dos_cmd_raw(cmd, i);

    if (cbm_k_ckout(BUF_LFN) != 0) {
        cbm_k_clrch();
        cbm_k_close(BUF_LFN);
        buf_is_open = 0;
        return DOS_ERR_TIMEOUT;
    }
    for (n = 0; n < 256u; ++n) {
        cbm_k_bsout(buf[n]);
    }
    cbm_k_clrch();

    /* U2: buffer out to track/sector. */
    i = 0;
    cmd[i++] = 0x55; /* 'U' */
    cmd[i++] = 0x32; /* '2' */
    cmd[i++] = 0x3a; /* ':' */
    i += put_num(cmd + i, BUF_LFN);
    cmd[i++] = 0x20;
    i += put_num(cmd + i, 0);
    cmd[i++] = 0x20;
    i += put_num(cmd + i, track);
    cmd[i++] = 0x20;
    i += put_num(cmd + i, sector);
    dos_cmd_raw(cmd, i);

    i = dos_status();
    cbm_k_close(BUF_LFN);
    buf_is_open = 0;
    return i;
}

/* Read one 256 byte sector through the buffer channel. */
unsigned char dos_read_sector(unsigned char track, unsigned char sector,
                              unsigned char *buf)
{
    unsigned char cmd[20];
    unsigned char i;
    unsigned int  n;

    cbm_k_setlfs(BUF_LFN, dos_dev, BUF_LFN);
    cbm_k_setnam("#");
    if (cbm_k_open() != 0) {
        return DOS_ERR_TIMEOUT;
    }
    buf_is_open = 1;

    /* U1: disk to buffer. */
    i = 0;
    cmd[i++] = 0x55; /* 'U' */
    cmd[i++] = 0x31; /* '1' */
    cmd[i++] = 0x3a; /* ':' */
    i += put_num(cmd + i, BUF_LFN);
    cmd[i++] = 0x20;
    i += put_num(cmd + i, 0);
    cmd[i++] = 0x20;
    i += put_num(cmd + i, track);
    cmd[i++] = 0x20;
    i += put_num(cmd + i, sector);
    dos_cmd_raw(cmd, i);

    i = dos_status();
    if (i != 0) {
        cbm_k_close(BUF_LFN);
        buf_is_open = 0;
        return i;
    }

    if (cbm_k_chkin(BUF_LFN) != 0) {
        cbm_k_clrch();
        cbm_k_close(BUF_LFN);
        buf_is_open = 0;
        return DOS_ERR_TIMEOUT;
    }
    for (n = 0; n < 256u; ++n) {
        buf[n] = cbm_k_basin();
    }
    cbm_k_clrch();
    cbm_k_close(BUF_LFN);
    buf_is_open = 0;
    return 0;
}

/* Write one sector using the job queue instead of the block commands.
 *
 * The DOS refuses block commands on a disk whose directory header sector
 * holds no DOS version byte, which is exactly the state a freshly laid
 * down track is in, so the seed sector has to go round the outside. Only
 * the 1541 is handled: its job buffers are plain 256 byte sectors, while
 * the 1581 caches a 512 byte physical sector and re-reads it before every
 * write, which would throw the staged data away.
 */
unsigned char dos_write_sector_job(unsigned char track, unsigned char sector,
                                   const unsigned char *buf)
{
    unsigned char i;

    if (dos_drive_type != DRV_1541) {
        return DOS_ERR_TIMEOUT;
    }
    /* 16 bytes per M-W: the drive's command buffer is 41 bytes and a
     * longer payload does not survive the trip.
     */
    for (i = 0; i < 16; ++i) {
        dos_mw((unsigned int)(0x0600u + (unsigned int)i * 16u),
               buf + (unsigned int)i * 16u, 16);
    }
    return dos_job(3, JOB_WRITE, track, sector);
}

/* ---------------------------------------------------------------------- */
/* identification                                                          */
/* ---------------------------------------------------------------------- */

unsigned char dos_identify(void)
{
    unsigned char v[2];

    dos_drive_type = DRV_UNKNOWN;
    dos_tracks     = 35;
    dos_dir_track  = 18;

    /* The 6502 reset vector in drive ROM is the cheapest reliable
     * fingerprint: 1541/1541-II/1571 hold $EAA0, the 1581 holds $AF24.
     * Verified against the ROM images shipped with VICE 3.10:
     *   dos1541-325302-01+901229-05.bin  FFFC/FFFD = A0 EA
     *   dos1581-318045-02.bin            FFFC/FFFD = 24 AF
     */
    if (!dos_mr(0xfffc, v, 2)) {
        return DRV_UNKNOWN;
    }

    if (v[0] == 0x24 && v[1] == 0xaf) {
        dos_drive_type = DRV_1581;
        dos_tracks     = 80;
        dos_dir_track  = 40;
    } else if (v[0] == 0xa0 && v[1] == 0xea) {
        /* 1541 and 1571 share the vector; $FFE0 tells them apart
         * ($AA on 1541/1541-II, $FF on 1571).
         */
        if (!dos_mr(0xffe0, v, 1)) {
            return DRV_UNKNOWN;
        }
        if (v[0] == 0xaa) {
            dos_drive_type = DRV_1541;
            dos_tracks     = 35;
            dos_dir_track  = 18;
        } else {
            dos_drive_type = DRV_1571;
            dos_tracks     = 35;
            dos_dir_track  = 18;
        }
    }
    return dos_drive_type;
}

const char *dos_type_name(void)
{
    switch (dos_drive_type) {
        case DRV_1541: return "CBM DOS V2.6 1541";
        case DRV_1571: return "CBM DOS V3.0 1571";
        case DRV_1581: return "CBM DOS V10 1581";
        default:       return "UNKNOWN DOS";
    }
}

/* ---------------------------------------------------------------------- */
/* job queue                                                               */
/* ---------------------------------------------------------------------- */

/* Job queue layout differs between the two families:
 *
 *   1541: job code for buffer n at $0000+n, header track/sector at
 *         $0006+2n / $0007+2n. (As used by the ROM's own N: command,
 *         which runs the formatter as job $E0 in buffer 3 with the
 *         header at $000C/$000D.)
 *   1581: job code for buffer n at $0002+n, header track/sector at
 *         $000B+2n / $000C+2n (ROM $9588 writes $4D/$4E there).
 */
static unsigned int job_code_addr(unsigned char slot)
{
    if (dos_drive_type == DRV_1581) {
        return 0x0002u + slot;
    }
    return 0x0000u + slot;
}

static unsigned int job_hdr_addr(unsigned char slot)
{
    if (dos_drive_type == DRV_1581) {
        return 0x000bu + (unsigned int)slot * 2u;
    }
    return 0x0006u + (unsigned int)slot * 2u;
}

/* Put a job in the queue and return without waiting for it. */
void dos_job_start(unsigned char slot, unsigned char code,
                   unsigned char track, unsigned char sector)
{
    unsigned char hdr[2];

    hdr[0] = track;
    hdr[1] = sector;
    dos_mw(job_hdr_addr(slot), hdr, 2);
    dos_poke(job_code_addr(slot), code);
}

/* How long a 1541 is left alone between starting a job and reading the
 * slot back, in frames. Measured against a sector write plus a seek,
 * with room on top; too high only costs time, too low hangs.
 */
#define DOS_JOB_WAIT 90u

unsigned char dos_job(unsigned char slot, unsigned char code,
                      unsigned char track, unsigned char sector)
{
    unsigned char hdr[2];
    unsigned char st;
    unsigned int  guard;

    hdr[0] = track;
    hdr[1] = sector;
#ifdef DOS_TRACE
    dos_tag = 4;
#endif
    dos_mw(job_hdr_addr(slot), hdr, 2);
#ifdef DOS_TRACE
    dos_tag = 5;
#endif
    dos_poke(job_code_addr(slot), code);
#ifdef DOS_TRACE
    dos_tag = 6;
#endif

    /* And now wait, because the next thing this does is read the job slot
     * back and a 1541 in the middle of a job is deaf. The busy signal this
     * used to watch for was taken out when it turned out to wedge the
     * drive, and nothing was put in its place: the job was started and the
     * slot read in the same breath. An emulated drive answers anyway. A
     * real one hangs the machine, which is what happened at the directory
     * stage, where the very first thing after the last track is a sector
     * write through the job queue.
     *
     * A seek and one sector is nothing beside a track format, so this is a
     * second and a half rather than the track wait. A 1581 answers its bus
     * throughout and only needs long enough for the controller to pick the
     * job up.
     */
    dos_delay_long(dos_drive_type == DRV_1541 ? DOS_JOB_WAIT : 8u);

    (void)st;
    (void)guard;
    return dos_job_status(slot);
}

/* Read a job slot until the controller has cleared the busy bit and left a
 * status code behind. Only safe once the drive is known to be answering.
 */
unsigned char dos_job_status(unsigned char slot)
{
    unsigned char st;
    unsigned int  guard;

    for (guard = 0; guard < DOS_JOB_POLLS; ++guard) {
        if (dos_mr(job_code_addr(slot), &st, 1)) {
            if ((st & 0x80) == 0) {
                return st;
            }
        }
    }
    return DOS_ERR_TIMEOUT;
}
