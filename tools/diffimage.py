#!/usr/bin/env python3
"""diffimage.py - report which logical tracks of a disk image differ from the
marker fill, so a single track format can be checked rather than assumed.

usage: diffimage.py image.d64|image.d81 [fill_hex]
(C) 2026 Robert Mech. Licence MIT.
"""
import sys

# D64: sectors per track, tracks 1..35
D64_SPT = [21] * 17 + [19] * 7 + [18] * 6 + [17] * 5


def track_map(ext):
    """Return [(track, offset, length), ...] for the image layout."""
    out = []
    off = 0
    if ext == "d64":
        for i, n in enumerate(D64_SPT, start=1):
            out.append((i, off, n * 256))
            off += n * 256
    else:  # d81: 80 tracks of 40 sectors
        for i in range(1, 81):
            out.append((i, off, 40 * 256))
            off += 40 * 256
    return out


def main():
    path = sys.argv[1]
    fill = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x55
    ext = path.rsplit(".", 1)[-1].lower()
    data = open(path, "rb").read()

    changed = []
    for track, off, length in track_map(ext):
        blk = data[off:off + length]
        n = sum(1 for b in blk if b != fill)
        if n:
            changed.append((track, n, length, blk[:8].hex(" ")))

    if not changed:
        print("no track differs from the fill")
        return
    print(f"{len(changed)} track(s) changed:")
    for track, n, length, head in changed:
        print(f"  track {track:3d}: {n:6d}/{length} bytes, first 8: {head}")


if __name__ == "__main__":
    main()
