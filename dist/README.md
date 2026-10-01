# Built binaries

Ready to run, built from the source in this repository with cc65 2.19.

| File | What it is |
|---|---|
| `headcrash.prg` | The program. `LOAD"HEADCRASH",8,1` then `RUN`. |
| `headcrash.d64` | The same program on a 1541 disk image. |
| `headcrash.d81` | The same program on a 1581 disk image. |

Rebuild them with `make` and `make dist` from the top of the repository.

Keys: F1 picks the device, F3 sets the 1541 track wait, F5 names the disk,
F7 formats. RUN/STOP stops a run in progress.

The program loads at `$0801` and runs from there, moving the screen into
VIC bank 1. It does not use the REU or any cartridge, and it talks to the
drive through the KERNAL, so JiffyDOS and the usual accelerators are fine.

(C) 2026 Robert Mech. Licence GPL-3.0-or-later.
