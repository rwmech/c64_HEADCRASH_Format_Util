# HEADCRASH Format Util

A disk formatter for the Commodore 64 that formats one track at a time, so
it can show you how far it has got, retry a track that fails, and tell you
which tracks are bad. Supports the 1541 (5.25 inch, 35 tracks) and the 1581
(3.5 inch, 80 tracks), with the 1581 as the default.

(C) 2026 Robert Mech. Licence GPL-3.0-or-later.

## Why this is not just an N: command

`N0:name,id` formats the whole disk in one go and tells you nothing until it
is finished. It cannot do otherwise: the drive runs the format as a single
job and its DOS sits in a wait loop for the duration, so there is nothing on
the other end of the cable to ask.

This util takes the drive's own ROM formatter and drives it one track per
job, which moves the loop to the C64 side where it can be drawn and retried.
Nothing here reimplements GCR or MFM encoding; the drive still does the
writing, exactly as it always did.

The two drives need different handling:

- **1581**: `FORMATDK` (job `$F0`) already walks cylinders and stops when
  the current cylinder reaches the end marker at `$8F`, so pointing the
  start and the end at the same cylinder formats exactly one track.
- **1541**: has no per track format job, so a 26 byte gate is uploaded to
  `$0600` in drive RAM, in front of the point where the controller re-enters
  the ROM formatter. See `src/drivecode_1541.s`, which explains itself.

## Build

Needs cc65 (tested with 2.19) and Python 3.

```
make
```

That assembles the drive gate, regenerates `src/drivecode_1541.h` from it,
and links `build/headcrash.prg`.

To run the test harnesses against emulated drives you also need VICE 3.10
built with the headless UI, plus drive ROMs. `make test` expects `x64sc` and
`c1541` on the path.

## What has been verified, and where

Everything below was checked by running the code in VICE 3.10 with true
drive emulation and the original Commodore drive ROMs, then inspecting the
resulting disk images byte by byte. Claims about ROM internals come from
disassembling the ROM images themselves.

- Drive identification from the ROM reset vector at `$FFFC`: `$EAA0` on
  1541/1541-II/1571, `$AF24` on 1581, with `$FFE0` separating 1541 from
  1571.
- Per track formatting on both drives, proven by filling a disk image with
  `$55`, formatting one track, and confirming that exactly that track's
  bytes changed.
- A full pass: 35 of 35 tracks on a 1541, 80 of 80 on a 1581, no errors.
- A formatted 1541 disk that `c1541` reads back as a valid empty disk with
  the right name, ID and 664 blocks free, and the same for a 1581 with 3160
  blocks free.
- The whole thing driven from its own interface, start to finish, on both
  drives.

## The screen

The VIC sits in bank 1, with the bitmap at `$4000` and the screen matrix at
`$6000`, so everything it reads is plain RAM with the ROMs still mapped in
and drawing never has to bank anything out. The C stack comes down to
`$3f00` to stay clear of it.

One ink per 8x8 cell is the rule the layout is built around: the panels, the
disc and the bar keep to their own cells, which is why there are no two
coloured things touching anywhere on the screen.

The disc fills in from the outside as tracks are laid down, one ring per
track where the radius allows and shared rings where it does not. The track
counter is the exact figure; the disc is the shape of the progress.

## What an emulator cannot tell you

VICE is cycle accurate, which makes it good evidence for anything about ROM
behaviour and the job queue, and poor evidence for how long a real drive
with a real motor and a real disk in it actually takes.

One constant matters: `A41_TRACK_WAIT` in `src/fmt.c`, how long the C64
waits for a 1541 track before it speaks to the drive again. It is set from a
VICE measurement of about 190 frames per track, doubled. If a real drive
ever exceeds it, the C64 does not recover gracefully: it hangs, because the
KERNAL's serial send has no timeout once a device has acknowledged, and a
1541 writing a track acknowledges in hardware while its CPU is busy
elsewhere. There is no signal to wait for and nothing safe to poll.

There is no safe way to measure it automatically, because every way of
asking the drive whether it has finished is the thing that hangs. So the
wait is adjustable from the interface instead, with F3, and shown in tenths
of a second next to the device number. Lower is quicker; if a format ever
freezes the machine part way through, that number was too low for your
drive. The 1581 does not use it and does not show it.

## Layout

```
src/main.c             screen layout, keys, the format run
src/ui.c               hi-res bitmap interface
src/dos.c              IEC and CBM DOS: commands, M-R/M-W, job queue, sectors
src/fmt.c              the track at a time format engine
src/drivecode_1541.s   the 26 byte gate that runs inside the drive
tools/                 drivecode assembly, image helpers used by the tests
tests/                 harnesses, each one a standalone .prg
docs/DESIGN.md         why the awkward parts are the way they are
```
