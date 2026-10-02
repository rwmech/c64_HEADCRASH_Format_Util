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
bank: sprites at `$5000`, character set at `$5400`, screen matrix at
`$5C00`, bitmap at `$6000`. All plain RAM with the ROMs mapped in, so
drawing never banks anything out, and the program gets everything below
`$5000` (`-Wl -D__HIMEM__=0x5000`). Set that wrong and the C stack lands on
the screen data and the machine resets, which is what the first version
did.

One ink per 8x8 cell. The layout keeps colours in their own cells, which is
why the panels, the disc and the bar do not touch. Anything that needs a
colour over the top of something else is a sprite.

A C64 pixel is about five sixths as wide as it is tall, so the disc is
drawn as an ellipse with the horizontal radius a fifth larger than the
vertical. Equal radii look squashed on a television.

Text is drawn as whole cells, so it starts on a cell row. x is 16 bit
everywhere: 320 does not fit in a byte, and the first version wrapped the
right of the disc onto the left of the screen.

**Draw once, recolour after.** The drive bodies, the disc outline and the
panel furniture are drawn at start up and never redrawn. Selection and
activity are shown by writing colour bytes into the screen matrix, which is
one store per cell.

## Build and verify

```
make            build/headcrash.prg
make tests      the harnesses in tests/, each a standalone .prg
make dist       prg plus d64 and d81 in dist/
```

Needs cc65 2.19 and Python 3. Verification needs VICE 3.10 built with the
headless UI (`--enable-headlessui`) and original drive ROMs.

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
and poor evidence about how long a real drive takes. Its 1581 is
noticeably slower than the real thing. Timing constants come from hardware,
not from here.

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
- A held noise voice for the spindle motor. It is a drone, not a drive. The
  stepper tick on its own is the sound; everything else is noise.
- Trusting $0088 on a 1581 as a progress counter during its own N:. It is
  live during a track at a time pass and not during the drive's own
  format.
