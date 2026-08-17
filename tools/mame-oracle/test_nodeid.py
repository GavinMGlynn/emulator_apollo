#!/usr/bin/env python3
"""The node-ID ROM image, checked against the loader it has to satisfy.

Every assertion here is a restatement of `apollo_ni::call_load`
(`ext/mame/src/mame/apollo/apollo_m.cpp:932`) or of `apollo_ni::read`, which is
the point: the file is only useful if MAME accepts it, and MAME's acceptance
rule is three lines of C++ that this suite copies out rather than paraphrases.

Run standalone; wired into CTest as `oracle_nodeid`.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import nodeid  # noqa: E402

failures = 0


def check(name: str, actual, expected) -> None:
    global failures
    if actual == expected:
        sys.stdout.write("ok   %s\n" % name)
    else:
        failures += 1
        sys.stdout.write("FAIL %s\n       expected %r\n       actual   %r\n"
                         % (name, expected, actual))


def main() -> int:
    # The size is checked by the loader before anything else, and a wrong size
    # is refused outright rather than padded.
    image = nodeid.build(0x12345)
    check("an image is exactly 32 bytes", len(image), 32)

    # `m_node_id = (((data[2] << 8) | data[4]) << 8) | (data[6])`, verbatim.
    # The stride of two is the hardware's: `apollo_ni::read` returns each byte
    # in the high half of a 16-bit word, so the ROM is byte-wide on a
    # word-addressed bus.
    check("the three ID bytes are at 2, 4 and 6",
          (image[2], image[4], image[6]), (0x01, 0x23, 0x45))
    check("and the bytes between them are holes",
          (image[3], image[5]), (0, 0))

    # `uint8_t checksum = data[2] + data[4] + data[6]; if (checksum !=
    # data[30])` -- so byte 30 and a **byte-wide** sum, which matters: a wider
    # accumulator agrees for every small ID and disagrees exactly when the sum
    # carries.
    check("byte 30 is the checksum", image[30], (0x01 + 0x23 + 0x45) & 0xFF)
    carrying = nodeid.build(0xF0F0F0)
    check("and it is a byte-wide sum, so it wraps",
          carrying[30], (0xF0 + 0xF0 + 0xF0) & 0xFF)

    # MAME's own default, which is what a run without an image gets. Worth
    # pinning: MINST's default Authorized Area is `//node_12345`, so this
    # constant is visible in every install transcript and a change to it would
    # silently change those paths.
    check("MAME's DEFAULT_NODE_ID round-trips",
          nodeid.read_back(nodeid.build(0x12345)), 0x12345)

    for value in (0x000000, 0x000001, 0x22222, 0xFFFFFF):
        check("node ID %06X round-trips" % value,
              nodeid.read_back(nodeid.build(value)), value)

    # Refused, not masked. An ID silently truncated would configure a machine
    # whose ROM and whose disk disagree, which is the failure the whole
    # mechanism exists to prevent.
    try:
        nodeid.build(0x1000000)
        check("a 25-bit node ID is refused", "accepted", "refused")
    except ValueError:
        check("a 25-bit node ID is refused", "refused", "refused")

    # A corrupt checksum is reported rather than believed, because that is what
    # the loader does with it.
    broken = bytearray(nodeid.build(0x12345))
    broken[30] ^= 0xFF
    try:
        nodeid.read_back(bytes(broken))
        check("a bad checksum is rejected", "accepted", "rejected")
    except ValueError:
        check("a bad checksum is rejected", "rejected", "rejected")

    # And the command line writes the file, reading the ID as hexadecimal --
    # which every node ID in this project's documents and in MAME's own
    # constant is written as.
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "n.ani"
        proc = subprocess.run(
            [sys.executable, str(HERE / "nodeid.py"), "22222", str(path)],
            capture_output=True, text=True, timeout=30)
        check("the tool writes an image", proc.returncode, 0)
        check("of the right size", path.stat().st_size, 32)
        check("carrying the ID it was given, read as hexadecimal",
              nodeid.read_back(path.read_bytes()), 0x22222)

    if failures:
        sys.stderr.write("\n%d check(s) failed\n" % failures)
        return 1
    sys.stdout.write("\nall checks passed\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
