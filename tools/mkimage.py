#!/usr/bin/env python3
"""mkimage.py - make a disk image whose every byte is a marker value, so a
format run shows up as a diff instead of having to be believed.

usage: mkimage.py out.d64|out.d81 [fill_hex]
(C) 2026 Robert Mech. Licence GPL-3.0-or-later.
"""
import sys

SIZES = {"d64": 174848, "d81": 819200}


def main():
    path = sys.argv[1]
    fill = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x55
    ext = path.rsplit(".", 1)[-1].lower()
    open(path, "wb").write(bytes([fill]) * SIZES[ext])
    print(f"{path}: {SIZES[ext]} bytes of ${fill:02x}")


if __name__ == "__main__":
    main()
