# CLAUDE.md

Working directives, goals and hard-won knowledge for HEADCRASH Format Util.
Read this first. It exists so that none of it has to be rediscovered.

(C) 2026 Robert Mech. Licence MIT.

## What this is

A disk formatter for the Commodore 64 that formats a disk and shows what it
is doing while it does it: progress per track, retries in view, bad tracks
found and kept out of the BAM afterwards. Supports the 1541 (35 tracks) and
the 1581 (80 tracks), with the 1581 as the default.

Rob owns the project and the copyright. He sets direction; propose and
build.

## Working rules

- Every copyright, licence or author line names **Robert Mech** only,
  licence **MIT**. Never name Anthropic or Claude as an author
  or copyright holder, in code, commits or documentation.
- Commits carry no Co-Authored-By or "Generated with" line. Commit by file
  name, never `git commit -a`. Do not tag or cut releases without Rob's go.
- **Committing is not shipping.** Work that is only in the local repository
  is invisible: fifteen commits of fixes and documentation once sat here
  while Rob was reading a GitHub page still showing v1.1 and being told
  each round that the docs were updated. Push when the work is done, and
  say plainly whether it went out.
- **The version goes up every time the repository is pushed.** `VERSION` in
  `src/main.c` is what is on the screen and in the card artwork, and
  CHANGELOG.md gets the matching section. A build Rob is looking at and a
  build being described have to be the same build.
- C first with cc65, ca65 only where the generated code is the problem.
- No code in chat unless asked for; the repository is the deliverable.
- Code listings, when asked for, carry the filename, a description, whether
  they are complete or partial, inline comments and the libraries needed.
- Metric first. No em dashes in anything written.
- Verify before asserting. Every claim about ROM behaviour in this project
  came from disassembling the ROM; every claim about what works came from
  running it. Say plainly what was checked and what was not.

## Goals, in the order they matter

1. **Correct disks.** A disk this program formats is a disk the DOS and
   every other program will accept. Verified by reading the image back.
2. **Honest progress.** What the bar shows is what is happening. Where it
   is an estimate, it says so on screen.
3. **Speed.** Close to what the drive itself can do. A format that takes
   three times as long as the drive's own is a failure of this program, not
   a feature of the approach.
4. **Bad media handled.** Retries in view, bad tracks reported and locked
   out of the BAM so the disk stays usable.
5. **Looks like the rest of the project.** Hi-res line art, cyan on black,
   one ink per cell. See the unleashed house style.

## The two modes, and why both exist

- **QUICK**: hands the whole disk to the drive with a plain `N:`. As fast
  as the hardware goes. The drive reports nothing at all while it runs, so
  the bar sweeps and the track counter shows dashes rather than inventing
  a figure. No bad track detection.
- **SLOW** (SURFACE in the code): drives the format one track per job.
  Slower, because every track costs serial round trips on top of the
  drive's own time, and it is the only way to see a bad track coming.

QUICK is the default. SLOW is for a disk that is suspect. The two are
called QUICK and SLOW everywhere the user sees them.

## Hardware facts that cost real time to learn

Each of these is written up at length in `docs/DESIGN.md`. The short form:

- **A 1541 goes deaf while it writes a track.** The whole track is written
  inside one pass of its controller interrupt. Its ATN acknowledge is done
  in hardware, so from the C64 it still looks present, and the KERNAL's
  byte sender waits at `$ED5A` for a listener that is not listening, with
  no timeout. Anything sent to it then hangs the machine. There is no safe
  signal to poll. Three approaches were tried and failed: polling anyway,
  a bounded ATN probe (defeated by the hardware acknowledge), and raising
  a busy flag on CLK from inside the drive (wedges the drive, because the
  serial port shares VIA 1 port B with the drive's own bus code). What is
  left is to wait out a measured worst case.
- **A busy 1541 cannot be told from an idle one, either.** The addressing
  sequence that tells an empty device number from a real one looked like
  the way out: a drive whose processor is writing a track should not be
  able to give the per device answer that comes after ATN is released.
  It gives it anyway. `tests/t_busy.c` starts a track and asks 3000 times
  across the write: five say busy. That is approach four, and it fails like
  the other three. The clock stays.
- **A watchdog NMI can escape a hung KERNAL call**, and `src/watchdog.s`
  does work in isolation, but unwinding out of a cc65 call tree mid KERNAL
  leaves state that still breaks. Kept in the tree, not in the build. If
  it is ever finished it removes the wait entirely.
- **A 1581 answers the bus while it formats**, so its jobs can be polled.
  Its cylinder counter at `$0088` is live during a track-at-a-time pass and
  dead during its own `N:`. Measured, not assumed: `tests/t_poll.c` reads
  it four thousand times across a full format and gets one value, 79, from
  the first read to the last. There is no progress to read in QUICK mode on
  either drive, which is why the bar there sweeps instead of counting.
- **Probing a device takes the whole addressing sequence.** Measured with
  `tests/t_dev.c` against a 1581 on 8 and nothing on 9 or 10:
  - `cbm_k_open` returns 0 for present and absent alike. Useless, and this
    is why the first version never noticed an empty device number and went
    on to send an M-R into the dark, which left the bus half addressed and
    stuck the real drive until it was power cycled.
  - LISTEN followed by a read of ST is also 0 for all of them. Every device
    on the bus pulls DATA low to acknowledge ATN, not just the one being
    addressed, so this answers "someone is there", never "that one is".
  - LISTEN, then `cbm_k_second($6F)`, then UNLISTEN, then read ST: 0 for 8,
    128 for 9 and 10. The per device answer only exists once ATN has been
    released, because that is when the devices that were not addressed let
    DATA go. This is the one that works.
- **ST is sticky.** The KERNAL ORs bit 7 into `$90` and never clears it, so
  without clearing it by hand every device after the first empty one reads
  as empty too: cycle past an empty number and the drive that is really
  there is lost until the machine is reset. There is no KERNAL call for it.
  Write `$90` directly, before and after.
- **A job start and a job status read are not the same breath.** `dos_job`
  wrote the job code and then read the slot back immediately, because the
  busy signal it used to wait on was taken out when that turned out to
  wedge the drive, and nothing replaced it. An emulated drive answers
  anyway; a real 1541 is deaf for the whole job and hangs the machine. It
  showed up at the directory stage, where the first thing after the last
  track is a sector write through the job queue. There is a measured wait
  in there now, `DOS_JOB_WAIT`, and the same goes for `I0`, which seeks and
  reads the BAM before it will talk again.
- **Nothing extra goes on the interrupt vector while the drive is being
  driven.** Serial on a C64 is bit banged by the KERNAL against the
  drive's own timing and it only protects the parts of that it knows
  about. A handler chained onto `$0314` adds latency to every interrupt in
  the middle of a transfer, which an emulator forgives and a real drive
  does not. Both hardware hangs in a track at a time pass, at track 18 and
  at track 25, were on builds carrying the music and logo interrupt; v1.1,
  which had none, formatted straight through. `irq_pause` takes the vector
  off before any drive work and `irq_resume` puts it back, and nothing is
  lost by it: the tune is finished before the format starts and the logo
  not cycling for a minute is not a feature.
- **Chasing the track wait was the wrong tree, twice.** Raising it from
  four seconds to six moved the hardware hang earlier rather than later,
  which is not how a timeout behaves, and that should have been the clue.
  A speed zone theory was built on top of it and shipped: giving the first
  track of each zone two and a half times the wait produced ten bad tracks
  in VICE where a flat wait produced none. Measure before theorising, and
  when a fix makes the symptom move the wrong way, the theory is wrong.
- **Ending a format job early corrupts the drive** unless `$50` is cleared
  first: the ROM's job exit runs a GCR decode across the drive's own stack.
  The ROM's own format exit clears it at `$FD96` for that reason.
- **Neither DOS will build a directory on a disk we formatted.** The header
  sector holds no DOS version byte, so `N:` without an ID answers 73, and
  so do the block commands that would write that byte. The 1541 is seeded
  through the job queue (its buffers are plain 256 byte sectors). The 1581
  cannot be: its job queue re-reads the 512 byte physical sector before
  every write, so anything staged there is thrown away. Laying its
  directory track down with `D` as the filler gets `I0` to accept the disk
  but not the DOS to build on it.
- **M-W carries 16 bytes safely, not 32.** The drive's command buffer is 41
  bytes and a longer payload does not survive. This fails silently and
  looks exactly like a write to the wrong address.
- **cc65 translates C string literals to PETSCII**, so `'N'` arrives at the
  drive as `$CE` and every command comes back as error 31. Commands are
  built from numeric constants. The same bites disk names: a name written
  as C letters lands on the disk as graphics characters, so `disk_name`
  holds unshifted PETSCII codes.

## Screen

VIC bank 1, with everything the VIC reads packed against the top of the
bank: sprites at `$5B00`, screen matrix at `$5C00`, bitmap at `$6000`, and the
character set copy outside the bank at `$C000`. All plain RAM with the
ROMs mapped in, so drawing never banks anything out, and the program gets
everything below `$5B00` (`-Wl -D__HIMEM__=0x5b00`). Set that wrong and the C stack lands on
the screen data and the machine resets, which is what the first version
did.

One ink per 8x8 cell. The layout keeps colours in their own cells, which is
why the panels, the disc and the bar do not touch. Anything that needs a
colour over the top of something else is a sprite.

That rule applies inside a shape as well as between them. The disc is one
ink from edge to hub, because the hub used to be grey against cyan rings
and the first ring through a hub cell turned half the hub cyan. Two inks in
one shape is the same mistake as two inks in one cell, just harder to see
coming.

**The sprite pointers live in the screen matrix.** They are the last eight
bytes of the kilobyte at `$5C00`, so anything that floods the matrix a page
at a time writes over them. `gfx_clear` did exactly that, which pointed
every sprite at whatever block the ink byte spelled, somewhere inside the
program's own string table. The disc had shapes on it that looked like
letters because they were letters. Clear a thousand bytes, not four pages,
and keep the pointers in a table so they can be put back.

What the VIC has to read out of bank 1 is the bitmap, the matrix and the
sprites, and nothing else. The character set copy is not one of them: the
VIC is in bitmap mode and never looks at it, it is only the source this
program blits glyphs from. It sits at `$C000`, outside the bank, and the
sprites sit at `$5B00` directly under the matrix, so the program gets
everything below `$5B00`.

A C64 pixel is about five sixths as wide as it is tall, so the disc is
drawn as an ellipse with the horizontal radius a fifth larger than the
vertical. Equal radii look squashed on a television.

Text is drawn as whole cells, so it starts on a cell row. x is 16 bit
everywhere: 320 does not fit in a byte, and the first version wrapped the
right of the disc onto the left of the screen.

The two message rows are the full width and the copyright lives in them,
right aligned on the second row when nothing needs it and written over when
something does. Text is laid in at 2400 baud: 240 characters a second with
start and stop bits, four a frame at sixty frames a second, paced on the
jiffy clock at `$A2` with a bounded spin in case interrupts are off.

**Draw once, recolour after.** The drive bodies, the disc outline and the
panel furniture are drawn at start up and never redrawn. Selection and
activity are shown by writing colour bytes into the screen matrix, which is
one store per cell.

## Memory, and where the spare is

The program runs from `$0801` to `$5B00`, and that ceiling is fixed by the
sprites sitting directly under the screen matrix. It is nearly full.

What is left, and worth using before squeezing anything:

- `$C000`-`$C7FF` holds the copy of the character set. Not VIC visible from
  bank 1, which is the whole point of it being there.
- `$C800`-`$C8FF` holds the sector staging buffer in `fmt.c`.
- `$C900`-`$C97F` holds the interrupt handler's stack.
- `$C980`-`$C9D0` holds the horizontal stretch table.
- `$C9D1`-`$CFFF` is about 1580 bytes, free, and the first place to put
  anything that does not have to be inside the program's own ceiling.

There are about 200 bytes left below the stack. Anything bigger than that
goes to `$C9D1` or does not get added.

**Do not pay for space out of the C stack.** `__STACKSIZE__` was cut to
`0x300` to make a build fit and the 1581 track at a time pass started
failing in a different place on every run: once at track 37, once at track
1, both times leaving a disk whose BAM read back as 6715 blocks free. A
stack running into BSS does not crash, it corrupts, and it corrupts
differently every time. Move a buffer to `$C900` instead; the stack stays
at `0x400`.

## Build and verify

```
make            build/headcrash.prg
make tests      the harnesses in tests/, each a standalone .prg
make dist       prg plus d64 and d81 in dist/
```

Needs cc65 2.19 and Python 3. Verification needs VICE 3.10 built with the
headless UI (`--enable-headlessui`) and original drive ROMs.

**And then it goes on the hardware.** Rob runs every round on a real C64
with a real 1541 and a real 1581. The emulator proves the disks are
correct, because an image can be read back byte by byte; the hardware is
the only thing that settles speed, how the screen reads on a television,
and whether it is pleasant to use. Most of the bugs that mattered came
from there and from nowhere else: the slow redraw, the slow format, the
empty device number that took a drive power cycle to clear, the spindle
drone, the disc reading as broken on a CRT. Do not call something done on
the strength of a VICE screenshot.

```
x64sc -default -warp -ntsc -autostartprgmode 1 \
      -drive8type 1581 -drive8truedrive -8 image.d81 \
      -limitcycles 400000000 -exitscreenshot shot.png \
      -autostart build/headcrash.prg
```

`tools/mkimage.py` makes a disk image filled with a marker byte and
`tools/diffimage.py` reports which tracks differ from it, which is how
single track formatting was proved rather than assumed. Read the result
back with `c1541 -attach image -dir`: a good format reads as an empty disk
with the right name, the right ID and 664 blocks free on a 1541 or 3160 on
a 1581.

VICE is cycle accurate, which makes it good evidence about ROM behaviour
and poor evidence about how long a real drive takes, because there is no
motor and no disk in it. Its 1581 is noticeably slower than the real
thing. Timing constants come from hardware, not from here.

## Roadmap

Done:

- Track at a time formatting on both drives, proved by image diff
- Full passes on both drives producing valid, readable disks
- Retries in view, bad tracks collected, BAM lockout
- Hi-res interface, assembly primitives, sprite head marker
- QUICK and SURFACE modes, QUICK's bar indeterminate because there is
  nothing to count
- Drive bodies drawn from the real front panels, drawn once and recoloured,
  activity light, red seven segment device readout cycled with F1
- Head carriage sprite riding the media window, sized to the medium
- Drive noise on the SID, motor and stepper, F4 to silence it
- Eight keys as reverse video buttons in two rows, F2 mode, F6 ID, F8 exit
- Device probe by LISTEN, so an empty device number reports instead of
  wedging the bus

Next:

- Finish the watchdog so the 1541 can be polled and the wait disappears.
  This is the one that matters: it would make SURFACE mode run at the
  drive's own speed instead of a worst case per track.
- A way into the 1581 that avoids its second pass over the surface in
  SURFACE mode
- A verify pass that reads a formatted disk back and locks out what fails
- Sound, perhaps: the drive makes enough noise already

## Things not to try again

- Polling a 1541 during a format without a working escape. It hangs the
  machine, every time, however the poll is dressed up.
- Raising a busy flag on a serial line from inside drive code. It wedges
  the drive; the serial port is not ours to touch from the interrupt.
- Seeding a 1581 through its job queue. The write re-reads the physical
  sector first and throws the staged bytes away. Job codes $A4 and $A6 do
  not re-read but fail before touching the disk.
- 32 bit arithmetic anywhere that runs per pixel. The ellipse did and the
  redraw was visibly slow; the ring is integer with a scale table now.
- Looking for a drawing bug in the drawing code before checking what else
  writes to those cells. The gap in the disc was never the ring: the 1541
  wait readout cleared thirteen cells at x=208 on cell row 13, which runs
  straight through the left of the disc and wiped eight pixel rows of it.
  Two rounds of work went into the ring before anyone counted the lit
  pixels per row and found six rows blank from edge to edge. Count first.
- Putting anything in VIC bank 1 that the VIC does not read. The character
  set copy sat in the middle of it for no reason and cost the program two
  kilobytes, which is what made it run into its own stack.
- Starting a drive job and reading its status in the same breath. The
  emulator answers, the hardware hangs.
- A held noise voice for the spindle motor. It is a drone, not a drive. The
  stepper tick on its own is the sound; everything else is noise.
- Trusting $0088 on a 1581 as a progress counter during its own N:. It is
  live during a track at a time pass and not during the drive's own
  format.
