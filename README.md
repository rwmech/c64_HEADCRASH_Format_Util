# HEADCRASH Format Util

A disk formatter for the Commodore 64 that formats one track at a time, so
it can show you how far it has got, retry a track that fails, and tell you
which tracks are bad. Supports the 1541 (5.25 inch, 35 tracks) and the 1581
(3.5 inch, 80 tracks), with the 1581 as the default.

(C) 2026 Robert Mech. Licence MIT.

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

## Two ways to format

**QUICK** hands the whole disk to the drive with a plain `N:`, which is as
fast as the hardware goes, and the drive says nothing at all until it is
finished. The bar does not pretend otherwise: it sweeps to show work is
happening and the track counter shows dashes, because there is no number to
show. This is the default.

**SLOW** drives the format one track per job. Slower, because every track
costs serial round trips on top of the drive's own time, and it is the only
way to see a bad track coming and keep it out of the BAM afterwards. There
the bar and the track counter are real figures. F2 switches between them.

## Just run it

`dist/` holds a built `headcrash.prg` and the same program on a `.d64` and
a `.d81`, so nothing has to be compiled to try it.

```
LOAD"HEADCRASH",8,1
RUN
```

The keys are along the bottom of the screen, two rows of four:

```
F1 DRIVE   cycle the device number, 8 to 11
F2 QUICK   switch between QUICK and SLOW formatting, and says which
F3 WAIT    how long a 1541 track is allowed, SLOW mode only, and
           shows the figure on the button itself
F4 SOUND   the stepper tick on or off, during a format as well as
           before one
F5 NAME    name the disk
F6 ID      set the two character disk ID
F7 GO      format
F8 EXIT    back to BASIC
```

RUN/STOP stops a SURFACE run in progress.

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

Two places, and they answer different questions.

### On real hardware

A real Commodore 64 with a real 1541 and a real 1581. Every round of
changes goes back onto it, and the hardware has the last word on anything
an emulator cannot settle: how long a drive actually takes, what the screen
actually looks like on a television, whether the thing is pleasant to use.

It is also where most of the bugs worth fixing have come from. None of
these showed up under emulation:

- an interface that redrew too slowly to watch
- a format several times slower than the drive's own
- an empty device number leaving the bus in a state that took a drive power
  cycle to clear
- a held noise voice for the spindle that was a drone, not a drive
- the disc reading as broken on a CRT

The one timing constant in the program, `A41_TRACK_WAIT`, comes from
hardware and not from here, for the reasons in the section below.

### In VICE 3.10

The emulator is where correctness is proved, because a disk image can be
read back byte by byte and a real floppy cannot. All of this was checked
with true drive emulation and the original Commodore drive ROMs; claims
about ROM internals come from disassembling those ROM images.

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
  drives and in both modes: four runs, four valid disks.

## The screen

The VIC sits in bank 1 with everything it reads packed against the top of
the bank: sprites at `$5000`, the character set at `$5400`, the screen
matrix at `$5c00` and the bitmap at `$6000`. All of it is plain RAM with the
ROMs still mapped in, so drawing never has to bank anything out, and the
program gets everything below `$5000`.

One ink per 8x8 cell is the rule the layout is built around: the panels, the
disc and the bar keep to their own cells, which is why there are no two
coloured things touching anywhere on the screen.

The disc fills in from the outside as tracks are laid down, one ring per
track where the radius allows and shared rings where it does not. The head
carriage is a sprite riding the window where the media is exposed, on the
ring being written, and it is drawn at the size of the medium: the 5.25 inch
one is visibly bigger than the 3.5 inch one. When the disk has been checked
the whole disc goes green, or red if it did not come back clean.

The drive noise is one SID voice: a 24 ms noise burst on every track step
and nothing else. A held rumble for the spindle was tried and was simply a
drone. F4 silences it, during a format as well as before one.

The bottom two rows are the keys, as reverse video buttons. F2 and F3 and
F4 carry their own state, so the mode, the 1541 wait and the sound have no
fields of their own. Messages take the two rows above, wrapped at a space
rather than cut, with the copyright sharing the right of the first.

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
src/snd.c              drive noise on the SID
src/drivecode_1541.s   the 26 byte gate that runs inside the drive
tools/                 drivecode assembly, image helpers used by the tests
tests/                 harnesses, each one a standalone .prg
docs/DESIGN.md         why the awkward parts are the way they are
```
