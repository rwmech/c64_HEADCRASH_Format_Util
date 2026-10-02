# Changelog

(C) 2026 Robert Mech. Licence MIT.

## v1.7

### Measured

- **Every wait in the program is half what it says it is.** `dos_delay_long`
  watches the raster register at `$D012` and counts transitions into zero,
  but `$D012` holds only the low eight bits of the raster line and a frame
  has more than 256 lines on both NTSC and PAL, so the count passes through
  zero twice a frame: once at line 0 and again at line 256. The loop has
  been counting half frames since it was written.

  `tests/t_delay.c` times it against the jiffy clock, which the KERNAL
  advances once a frame and which knows nothing about any of this. Asked
  for 60 it waits 30, asked for 600 it waits 304.

  So the 1541 track wait reads 6.0S on the F3 button and is three seconds.
  A real 1541 track is about two and a bit, and longer at a zone edge where
  the formatter's gap convergence loop runs extra revolutions. When a track
  overruns the wait, the very next thing out of the program is an M-R to a
  drive that is still writing, and that is the hang. The reported hangs at
  tracks 17, 25 and 30 all sit where a track runs long.

  The same arithmetic caps `wait_for_tune` at 200 frames rather than 400,
  and the 1812 runs 242, so one start tune in three is cut off part way
  with the player left holding a chord and `tune` still set, which also
  silences the stepper for the rest of the run. That is why a format with
  the music on looked different from one with it off.

- Memory is not the problem, which was the other candidate. BSS ends at
  `$5693` and the C stack starts at `$5700`, so there is no collision, and
  the interrupt handler's own stack at `$C900` is nowhere near full.

## v1.5

### Fixed

- **The 1541 hang in a track at a time pass.** `dos_job_status` read the
  job slot four hundred times with no delay between attempts, and that
  loop only goes round when the job has not finished, which on a 1541
  means the drive is deaf. The one state in which nothing may be sent was
  the only state in which it sent four hundred commands back to back. An
  emulated drive always finishes inside the wait, so the loop never went
  round once and none of this appeared in VICE. Polls are spaced by half a
  second and capped at forty.
- **The zone edges.** The formatter converges a revolution byte count at
  `$0621/$0622` against the real spindle, with no iteration limit and the
  drive deaf throughout, and the ROM seeds it once at `$FAE8` and never
  again. Every track inherited the previous one's value, which is wrong by
  a whole density step at tracks 18, 25 and 31. The ROM's own figure is
  poked back before every track.
- **A build that silently overran its own ceiling.** The stock linker
  configuration sizes BSS by subtraction, so outgrowing the space wrapped
  the size rather than failing: BSS landed past the sprites with the C
  stack inside it, linked without a warning, and drew garbage.
  `cfg/headcrash.cfg` gives the area a checkable size.

### Removed

- The splash screen. It cost about a kilobyte and the program ends a
  hundred or so bytes below a ceiling fixed at `$5B00` by the sprites, so
  it was never going to fit alongside the rest. Putting it in a second
  code region running from the free RAM at `$C9D1` does not help either:
  the segment still has to be stored in the file below the ceiling.

### Wrong turns, recorded

- v1.4 blamed the music interrupt. Disproved by disassembling the KERNAL:
  `ISOUR` and `ACPTR` both mask interrupts for the whole of every
  transfer, so nothing on the IRQ vector can fire inside one. The change
  is kept because there is no reason to have a handler running during
  drive work, but it was not the fix.
- Before that, two rounds of blaming the track wait, including a speed
  zone wait that produced ten bad tracks in VICE where a flat wait
  produced none. That one is backed out.

## v1.4

### Fixed

- **A 1541 hanging part way through a slow format.** Not the track wait,
  which is what the previous two attempts assumed. Serial on a C64 is bit
  banged by the KERNAL against the drive's own timing, and the music and
  logo interrupt added latency to every interrupt in the middle of a
  transfer. An emulator forgives that; a real drive does not. Both hangs,
  at track 18 and at track 25, were on builds carrying that interrupt, and
  v1.1, which had none, formatted straight through. The vector comes off
  before any drive work and goes back afterwards. Nothing is lost: the
  tune finishes before the format starts, and the logo not cycling for a
  minute is not a feature.
- The speed zone wait added while chasing the wrong cause is backed out.
  It produced ten bad tracks in VICE where a flat wait produced none.

## v1.3

Tested on a real Commodore 64 with a real 1541 and a real 1581, and in
VICE 3.10 with the original drive ROMs.

### Fixed

- **The finish tune never played after a QUICK format.** Only the slow
  path started it. Both paths play it now.
- **The verify looked like a hang.** It was two hundred attempts a second
  apart inside the format engine, up to three minutes during which nothing
  was drawn and no key was read. The waiting moved out to the caller: the
  bar sweeps, the seconds are counted on screen, F4 still works, and after
  ninety seconds it says the drive never answered rather than sitting
  there.
- **The colour bug, the garbage on the disc, and the track window never
  appearing were all one fault.** `gfx_clear` flooded four whole pages of
  the screen matrix, and the last eight bytes of the fourth page are the
  sprite pointers, so every clear repointed all eight sprites at whatever
  data block the ink byte happened to spell. That block lands inside the
  program's own string table, which is why the disc had shapes on it that
  looked like letters: they were letters. The clear writes a thousand
  bytes now, and the pointers are held in a table and written again every
  time a sprite is placed, so nothing can leave a sprite showing somebody
  else's bytes.
- **The head sprite** is a slider pad on a tapered arm now, in two sizes,
  rather than the block nobody had actually seen yet.
- **Moire across the disc.** Eighty tracks over thirty one pixels of
  radius put a ring on every one, and with the horizontal radius stretched
  by a fifth they landed a fifth of a pixel apart and interfered. Rings
  snap to every second radius, which leaves a clear gap between each one.
- **Both slow passes locked up near the end.** The directory stage issued
  its command and then sat blind: seventy seconds on a 1581, where the
  drive formats the whole surface a second time, with the screen frozen
  and no key read, and then spoke to the drive whether it was ready or
  not. It is split now, the same way the check in QUICK mode is: the drive
  is left alone for as long as it needs, then asked once a second, with
  the bar sweeping and the keyboard live throughout.
- **The 1581 slow pass failed differently on every run** once the C stack
  had been cut to make a build fit: once at track 37, once at track 1,
  both leaving a disk whose BAM read back as 6715 blocks free rather than
  3160. A stack running into BSS corrupts rather than crashes. The sector
  staging buffer moved to free RAM at $C800 and the stack went back to
  where it was.
- **The opening tune came out in pieces.** The KERNAL turns interrupts off
  around every byte it puts on the serial bus and a format is nothing but
  serial traffic, so the player hardly got a look in. The tune is allowed
  to finish before the drive is spoken to.
- **The disc kept its colour and its rings between runs**, so a second
  format started on a full green disc. Both the pixels and the ink are
  cleared at the start of every run.
- **The 1541 locked up writing the directory.** `dos_job` started a job
  and read the slot back immediately: the busy signal it once waited on
  had been removed when that turned out to wedge the drive, and nothing
  replaced it. An emulated drive answers anyway, a real one is deaf for
  the whole job. There is a measured wait there now, and after `I0`, which
  seeks and reads the BAM before it will talk again.
- **The drive lights were the wrong way round.** Green power on the left,
  red activity to the right of it, which is how a real drive has them.

### Added

- **A splash screen**, drawn rather than stored. A full screen picture is
  eight thousand bytes of bitmap and a thousand of colour and there is
  nowhere in a single PRG to keep that, so this is the card's composition
  drawn with the primitives the program already has: the banner and the
  rainbow flash, the logo plate, the platter and the floor it sits on. It
  costs a few hundred bytes of code and no image data, and it looks like
  the rest of the screen rather than like a photograph of something else.
  Held for two and a half seconds or until a key.
- **Card artwork** for a TeensyROM NFC card, in `art/`: CR80 badge size as
  vector, with print renders in both orientations and the area outside the
  card transparent.

### Changed

- The splash's title is drawn at three times size by `ui_text_big` rather
  than set in 8 by 8 text, which is a caption and not a logo. It carries no
  platform banner: the card says COMMODORE 64 because a card has to say
  which machine it is for, and the machine's own screen does not.

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

- **Sound.** The stepper tick, a 24 ms noise burst per track, and six
  tunes on three voices, picked at random from two pools:

  - starting: Beethoven's Fifth, Eine kleine Nachtmusik, the 1812
  - finishing: the Ode to Joy, the Rondo alla Turca, the Can Can, the 1812

  Every finishing tune ends on a rising figure into a held tonic chord, so
  it sounds finished rather than stopped. Driven a row at a time from the
  interrupt, so nothing waits on a note. The note table is accurate to
  within 0.7 cents across five octaves. Master volume is half; this is a
  utility, not a game. F4 silences it, during a format as well as before
  one.
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
