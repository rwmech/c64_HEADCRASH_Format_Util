# Design notes

Why the awkward parts are the way they are. Written down because every one
of them cost a debugging round, and because the reasoning is not obvious
from the code.

(C) 2026 Robert Mech. Licence GPL-3.0-or-later.

## The drive goes deaf while it writes a track

A 1541 writes a whole track inside one pass of its controller interrupt. For
the duration, a second or more, the drive's main loop does not run and
nothing on the serial bus is serviced.

That on its own would be survivable. What makes it dangerous is that the
1541 acknowledges ATN in hardware, so from the C64 the drive still looks
present and willing. The KERNAL's byte sender checks for that acknowledgement
once, at `$ED44`, and then waits at `$ED5A` for the listener to release
DATA, with no timeout at all. Sending anything to a drive that is part way
into a format hangs the machine outright, and no amount of care on the C64
side changes that.

Three approaches were tried before the current one.

**Polling the job slot anyway.** Works for the first track and hangs on the
second, as soon as a poll overlaps the moment the job starts.

**Probing the bus for signs of life.** A bounded ATN probe, written by hand
so it could not block, to ask whether the drive was answering before saying
anything to it. It cannot work, for the reason above: the ATN acknowledge is
hardware, so a drive with a wedged CPU answers the probe just the same.

**Having the drivecode raise a busy flag on the serial CLK line**, which the
C64 can read straight off CIA 2 with no handshake. This very nearly worked,
and it is the approach to revisit if the timing constant ever becomes a
problem. It was abandoned because the serial port shares VIA 1 port B with
the drive's own bus code, and a read-modify-write from inside the controller
interrupt lands on top of whatever that code was in the middle of. The drive
wedges, usually one job later.

What is left is to wait out a measured worst case and only then ask. See
`A41_TRACK_WAIT` in `src/fmt.c`, and the caveat in the README about where
that number came from.

The 1581 does not have the problem. It answers the bus throughout a format
job, so its jobs are simply polled.

## Ending a format job early corrupts the drive

The 1541 gate ends a job by jumping to the ROM's own job exit at `$F969`
with a status in A. That exit does this:

```
F96E  LDA $50
F970  BEQ $F975
F972  JSR $F5F2
```

`$F5F2` is a GCR decode routine. It walks pointers through page 1 and the
drive's buffers, so running it after a format rather than after a read
writes over the drive's own stack. The drive survives long enough to answer
one or two more commands and then stops dead.

The ROM's own format exit at `$FD96` clears `$50` immediately before
jumping to `$F969`, for exactly this reason. The gate does the same, and
also restores `FTNUM` to `$FF` so the formatter's state is left as the ROM
would have left it.

## The DOS will not build a directory on a disk we formatted

A freshly laid down track is filled with `$01` on a 1541 and `$E5` on a
1581. The directory header sector therefore holds no DOS version byte, and
the DOS refuses the disk with error 73. That applies to `N:` without an ID,
which is the command that would otherwise rebuild just the BAM and
directory, and it applies to the block commands as well, which is what makes
it a deadlock: the one byte that would fix it cannot be written through the
DOS, because the DOS has already decided the disk is unusable.

**1541**: the job queue goes round the outside. Its buffers are plain 256
byte sectors, so the header sector is staged into buffer 3 at `$0600`
sixteen bytes at a time and written with job `$90`. Then `I0` makes the
drive look at the disk again, and `N0:name` builds a correct BAM and
directory on top. The whole step takes a few seconds.

Note the sixteen bytes: the drive's command buffer is 41 bytes, and an M-W
carrying 32 bytes of payload does not survive the trip intact. This is
silent, and it looks exactly like a write that went to the wrong address.

**1581**: no way in. Block commands are refused for the same reason, and its
job queue stages writes through a 512 byte physical sector which is re-read
from the disk immediately before the write, so anything staged there is
thrown away. Verified by patching the buffer, issuing the write, and reading
the buffer back to find the disk's own content in it again. Job codes `$A4`
and `$A6` do not re-read but fail before touching the disk.

So the 1581 hands the finished disk to its own `N0:name,id`, which formats
the surface a second time in order to write a filesystem it trusts. That
second pass is the price of the first one, and it is the main thing worth
revisiting.

## Why cc65 string literals cannot be sent to a drive

cc65 translates C string literals to PETSCII for the c64 target, so an
ASCII `N` arrives at the drive as `$CE` rather than `$4E` and every command
comes back as error 31. Commands are either built from numeric constants or
folded back down in `dos_cmd()`. Binary payloads never go through that path.
