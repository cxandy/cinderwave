#!/usr/bin/env python3
"""Convert a raw RP2040 flash image (.bin) into a drag-and-drop .uf2 file.

The Pico SDK normally uses `picotool` for this step, but picotool is a Rust
binary that has to be built or downloaded separately. This script does the same
job with nothing but the standard library, so a plain `arm-none-eabi-objcopy`
image can be turned into a flashable .uf2 on any machine.

Usage:
    python bin2uf2.py firmware.bin firmware.uf2 [--base 0x10000000]

The RP2040 family ID is 0xe48bff56 and the XIP flash window starts at
0x10000000, which is where `objcopy -O binary` output is meant to be written.
"""

import argparse
import struct
import sys

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30

FLAG_FAMILY_ID = 0x00002000

RP2040_FAMILY_ID = 0xE48BFF56

PAYLOAD_SIZE = 256
BLOCK_SIZE = 512


def build_uf2(image: bytes, base: int, family_id: int) -> bytes:
    num_blocks = (len(image) + PAYLOAD_SIZE - 1) // PAYLOAD_SIZE
    out = bytearray()

    for block_no in range(num_blocks):
        chunk = image[block_no * PAYLOAD_SIZE:(block_no + 1) * PAYLOAD_SIZE]
        chunk = chunk.ljust(PAYLOAD_SIZE, b"\xFF")

        header = struct.pack(
            "<IIIIIIII",
            UF2_MAGIC_START0,
            UF2_MAGIC_START1,
            FLAG_FAMILY_ID,
            base + block_no * PAYLOAD_SIZE,
            PAYLOAD_SIZE,
            block_no,
            num_blocks,
            family_id,
        )
        block = header + chunk.ljust(476, b"\x00") + struct.pack("<I", UF2_MAGIC_END)
        assert len(block) == BLOCK_SIZE
        out += block

    return bytes(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", help="input .bin produced by objcopy")
    parser.add_argument("output", help="output .uf2 path")
    parser.add_argument("--base", default="0x10000000",
                        help="flash base address (default 0x10000000)")
    parser.add_argument("--family-id", default="0x%08x" % RP2040_FAMILY_ID,
                        help="UF2 family ID (default 0xe48bff56 for RP2040)")
    args = parser.parse_args()

    with open(args.binary, "rb") as handle:
        image = handle.read()

    if not image:
        print("error: %s is empty" % args.binary, file=sys.stderr)
        return 1

    uf2 = build_uf2(image, int(args.base, 0), int(args.family_id, 0))

    with open(args.output, "wb") as handle:
        handle.write(uf2)

    print("%s -> %s (%d bytes image, %d blocks)"
          % (args.binary, args.output, len(image), len(uf2) // BLOCK_SIZE))
    return 0


if __name__ == "__main__":
    sys.exit(main())
