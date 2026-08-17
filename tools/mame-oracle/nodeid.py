#!/usr/bin/env python3
"""Write an Apollo node-ID ROM image for the oracle's `-node_id` slot.

**Why this exists.** A Domain volume's label records the node that initialised
it, and `node_id_from_volume` in our headless frontend makes a machine present
that ID -- because "a node that disagreed with its own disk would create objects
attributed to a machine that is not there". So two nodes on one ring need two
volumes carrying two *different* node IDs, and the plan recorded that as a media
question with no route.

There is a route, and it is in the oracle's own source rather than in any
manual. `apollo_ni` is a `device_image_interface` (`apollo.h:375`), so the node
ID is a **loadable 32-byte image** and not a compiled-in constant -- MAME's
`DEFAULT_NODE_ID` of `0x12345` is only what you get when no image is supplied.

**The format, from `apollo_ni::call_load` (`apollo_m.cpp:932`)** and not from a
guess:

    size            exactly 32 bytes, or the load is refused
    data[2]         node ID bits 23-16
    data[4]         node ID bits 15-8
    data[6]         node ID bits 7-0
    data[30]        (data[2] + data[4] + data[6]) & 0xFF, checked on load
    everything else zero

The stride of two is the hardware's, not an oddity of the loader: `apollo_ni`'s
own `read` returns each byte in the **high half** of a 16-bit word, so the ROM
is byte-wide on a word-addressed bus and every second byte is a hole. Offset 15
answers the checksum, which is why byte 30 holds it.

Refusing rather than truncating a node ID that does not fit 24 bits: an ID
silently masked would produce a machine whose disk and whose ROM disagree, which
is the exact failure this whole mechanism exists to prevent.

    tools/mame-oracle/nodeid.py 22222 out.ani
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

IMAGE_BYTES = 32
CHECKSUM_AT = 30


def build(node_id: int) -> bytes:
    if node_id < 0 or node_id > 0xFFFFFF:
        raise ValueError("a node ID is 24 bits: %#x does not fit" % node_id)
    image = bytearray(IMAGE_BYTES)
    image[2] = (node_id >> 16) & 0xFF
    image[4] = (node_id >> 8) & 0xFF
    image[6] = node_id & 0xFF
    image[CHECKSUM_AT] = (image[2] + image[4] + image[6]) & 0xFF
    return bytes(image)


def read_back(image: bytes) -> int:
    """The ID `apollo_ni::call_load` would take from this image.

    Written as the loader's own expression rather than as the inverse of
    `build`, so a mistake in one is not reproduced by the other.
    """
    if len(image) != IMAGE_BYTES:
        raise ValueError("an image is %d bytes, not %d" % (IMAGE_BYTES,
                                                          len(image)))
    checksum = (image[2] + image[4] + image[6]) & 0xFF
    if checksum != image[CHECKSUM_AT]:
        raise ValueError("checksum is %02x, image says %02x"
                         % (checksum, image[CHECKSUM_AT]))
    return ((image[2] << 8) | image[4]) << 8 | image[6]


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Write an Apollo node-ID ROM image (32 bytes).")
    parser.add_argument("node_id",
                        help="the node ID, hexadecimal by default (`12345`) "
                             "or with an explicit base (`0x12345`)")
    parser.add_argument("path", type=Path, help="the image to write")
    args = parser.parse_args(argv)

    text = args.node_id
    # Hexadecimal by default, because every node ID in this project's documents
    # and in MAME's own `DEFAULT_NODE_ID` is written that way. A bare decimal
    # reading would silently produce a different machine.
    node_id = int(text, 16) if not text.lower().startswith("0x") \
        else int(text, 16)
    try:
        image = build(node_id)
    except ValueError as error:
        sys.stderr.write("nodeid: %s\n" % error)
        return 2

    args.path.parent.mkdir(parents=True, exist_ok=True)
    args.path.write_bytes(image)
    sys.stdout.write("%s: node ID %05X, %d bytes, checksum %02X\n"
                     % (args.path, read_back(image), len(image),
                        image[CHECKSUM_AT]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
