#!/usr/bin/env python3
"""Package the HPM6364 application BIN as a bounded UF2 update image.

This tool never erases or programs a target.  The bootloader independently
checks every target address and the HPM UF2 family ID before accepting data.
"""
import argparse
import struct
from pathlib import Path

XPI0_BASE = 0x80000000
APP_OFFSET = 0x00020000
APP_SIZE = 0x00360000
APP_START = XPI0_BASE + APP_OFFSET
FAMILY_ID = 0x0A4D5048
PAYLOAD = 256

MAGIC0 = 0x0A324655
MAGIC1 = 0x9E5D5157
MAGIC_END = 0x0AB16F30
FLAG_FAMILY_ID = 0x00002000


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="application .bin linked at 0x80020000")
    parser.add_argument("output", type=Path, help="generated .uf2")
    args = parser.parse_args()
    image = args.input.read_bytes()
    if not image:
        raise SystemExit("refusing to package an empty application")
    if len(image) > APP_SIZE:
        raise SystemExit(f"application is {len(image)} bytes; app partition is {APP_SIZE} bytes")
    padded = image + b"\xff" * ((-len(image)) % PAYLOAD)
    count = len(padded) // PAYLOAD
    with args.output.open("wb") as out:
        for index in range(count):
            header = struct.pack("<8I", MAGIC0, MAGIC1, FLAG_FAMILY_ID,
                                 APP_START + index * PAYLOAD, PAYLOAD,
                                 index, count, FAMILY_ID)
            block = header + padded[index * PAYLOAD:(index + 1) * PAYLOAD]
            out.write(block + b"\x00" * (476 - PAYLOAD) + struct.pack("<I", MAGIC_END))
    print(f"wrote {args.output}: {count} UF2 blocks, app target 0x{APP_START:08x}")


if __name__ == "__main__":
    main()
