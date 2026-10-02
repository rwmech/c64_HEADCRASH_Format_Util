# Changelog

(C) 2026 Robert Mech. Licence MIT.

## v1.2

Tested on a real Commodore 64 with a real 1541 and a real 1581, and in
VICE 3.10 with the original drive ROMs.

### Fixed

- **Cycling the device number lost the drive.** ST is sticky: the KERNAL
  ORs bit 7 into `$90` on a timeout and never clears it, so every device
  after the first empty one read as empty too and the real drive was gone
  until the machine was reset. Cleared by hand now, before and after.
- **An empty device number stuck the bus.** The old probe opened a channel
  and sent an M-R, because the KERNAL's own OPEN reports present for an
  absent device. With nobody listening that left the bus half addressed and
  a real drive further along stuck until it was power cycled. Measured
  three probes with `tests/t_dev.c`: OPEN says present for everything, a
  bare LISTEN says present for everything, and LISTEN plus a secondary
  address plus UNLISTEN is the one that answers per device.
- **The gap in the disc.** It was never the ring. The 1541 wait readout
  cleared thirteen cells at x=208 on cell row 13, which runs through the
  left of the disc, and wiped eight pixel rows of it on every refresh.
  Found by counting lit pixels per row rather than by looking at the
  drawing code again.
- **The blotchy disc.** The hub was drawn in grey and the rings in cyan,
  and since a cell takes whichever ink was written last, the first ring
  through a hub cell turned half the hub cyan. The whole disc is one ink
  now.
- **A 1541 hanging part way through a slow format.** The track wait was too
  short for real hardware, which exceeded four seconds around track 25
  where the drive changes speed zone. The default is six seconds and F3
  covers one to fifteen. There is still nothing to poll:
  `tests/t_busy.c` asks a formatting drive 3000 times across one track
  whether it is there and is told no five times.
- Message text was cut at 23 characters. It wraps at a space across both
  rows now, at the full width.

### Added

- **Sound.** The stepper tick, a 24 ms noise burst per track. Two tunes on
  three voices: the opening of Beethoven's Fifth when a format starts and
  the first phrase of the Ode to Joy when it finishes. Driven a row at a
  time from the interrupt, so nothing waits on a note. Master volume is
  half; this is a utility, not a game. F4 silences it, during a format as
  well as before one.
- **Eight keys as reverse video buttons**, two rows across the full width,
  each with its own ink. F2 carries the mode, F3 the 1541 wait and F4 the
  sound state, so none of them needs a field on the screen.
- F6 sets the disk ID. F8 exits to BASIC.
- The logo cycles its colours, nine cells from the interrupt.
- Text arrives at 2400 baud, four characters a frame.
- The copyright lives in the message area: right aligned on the second row
  when nothing needs the row, written over when something does.

### Changed

- **One drive panel instead of two.** Two bodies with one greyed out was
  half the panel saying nothing, and the grey one was never the drive being
  used. The panel is the drive now: a 1541 front, a 1581 front, or a
  hatched plate reading DRIVE NOT CONNECTED. The DOS line and the running
  figures moved into the column that freed up.
- **The drive fronts are drawn from the real ones.** What separates them is
  the hole in the case: a 5.25 inch slot takes most of the width with the
  latch standing proud in the middle, a 3.5 inch one is small in a recessed
  bezel with the eject button beside it.
- **The track window and the head are sprites.** The window is two black
  sprites stacked, which cuts a hole in the disc and costs nothing per
  track; the head is a slider on an arm that rides it, sized to the medium.
- The disc recolours as a whole: yellow while the disk is checked, green if
  it came back clean, red if it did not.
- **QUICK mode stopped pretending.** The drive reports nothing during its
  own `N:`, so the bar sweeps and the track counter shows dashes.
  `tests/t_poll.c` reads a 1581's cylinder counter 4000 times during a
  format and gets one value. Only SLOW mode has a real figure, and there it
  draws one.
- The two modes are called QUICK and SLOW everywhere they are shown.
- The seven segment device readout is smaller and sits level with the top
  of the drive.
- **The licence is MIT**, changed from GPL-3.0-or-later.
- The character set copy left the VIC bank. The VIC is in bitmap mode and
  never reads it; it is only the glyph source this program blits from, so
  it had no business taking two kilobytes out of the bank. It sits at
  `$C000` and the sprites moved under the matrix, which gave the program
  nearly eleven kilobytes more room.

## v1.1

- Track at a time formatting on both drives, proved by image diff.
- Full passes on both drives producing valid, readable disks: 664 blocks
  free on a 1541, 3160 on a 1581.
- Retries in view, bad tracks collected and locked out of the BAM.
- QUICK and SLOW modes.
- Hi-res interface: assembly drawing primitives, the disc filling a ring
  per track, drive bodies drawn once and recoloured, activity light, red
  seven segment device readout.
