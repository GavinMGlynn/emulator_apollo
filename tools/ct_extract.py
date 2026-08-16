#!/usr/bin/env python3
"""Objects out of an Apollo `wbak` distribution cartridge.

Every Domain/OS file this project has wanted to read -- `ring8a.drvr` for the
ring controller's status bits, the SELF_TEST image for the configuration
checksum, the graphics and Ethernet drivers behind the same wall -- lives on a
volume or a tape, and until now the only route to one was the AEGIS filesystem
walk of `docs/references/RING.md` findings 84-85d. That walk stalled on a single
undocumented offset inside a VTOC entry. The distribution cartridges carry the
same files in a format that is **entirely self-describing**, and this reads it.

## Three layers, and only the innermost one is Apollo's

**ANSI labels.** The cartridges are ISO 1001 / ANSI X3.27 labelled tapes, not an
Apollo invention. Block 0 is `VOL1`, and the body is a sequence of tape files
each framed `HDR1` / `HDR2` / (optional `UHL1`) / data / `EOF1` / `EOF2`. Every
label is 80 bytes at the start of a 512-byte block, and `HDR1` columns 5-21 give
the file identifier -- `/base_open_sau8_d` and its siblings. The earlier reading
of these cartridges sampled `wbak` records without knowing a label structure sat
above them, which is why the offsets it recorded did not line up.

**`wbak` blocks.** Inside a tape file, every 512-byte block carries a 14-byte
header:

    +0   u32  block sequence number within the tape file, 1-based
    +4   u64  the backup set's UID, the same one `UVL1` carries
    +12  u16  bytes of this block that are used, header included

so a short block is explicit rather than padded, and the tape's leader and
trailer blocks -- filled with `deaffaed` -- are outside the scheme and skipped.

**Records.** The used part of a block after the header is a chain of records:

    +0   u16  type
    +2   u16  length of the payload that follows the 6-byte header
    +4   u16  stream: 2 for the catalogue, 1 for data

**padded to an even length**. That padding is the whole grammar: without it a
parse desynchronises on the first object whose path has an odd number of
characters, and it then reads plausible garbage rather than failing. Records
never span a block, so each block's records fill its used length exactly -- which
is the check `--verify` makes on every block of every tape file.

The types that carry an ordinary file, which are the ones decoded here:

    9  the current directory: UID and path, repeated in *every* block, which is
       what lets a restore resume from any point on the tape
    2  an object: UID and path, opening it
    0  the object's attributes, 144 bytes -- see below
    1  a chunk of the object's data
    8  end of object

Five more types occur, and are **passed over rather than decoded**, so that what
this tool does not understand is visible instead of silent. Their counts over
the five SR10.3 cartridges, against 9,426 ordinary objects:

    5  518  a symbolic link: six bytes then the target path, plainly readable
            (`sys/dm/fonts/stdf7x13`). A link has no data records
    3   69  253 bytes of UIDs and `4Cxxxxxx` timestamps, always after a type-9
    4   69  four bytes, always after a type-8
    6    9  144 bytes in the shape of a type-0, opening an object the way a
            type-2 does -- which is why there are nine more type-8 records than
            type-2 records
   10    9  52 or 84 bytes, always following a type-6

Nothing here guesses at those. They are recorded so that "9,426 objects" is
known to be *ordinary files* rather than everything on the cartridge.

## The catalogue and the data are one stream, not two

`RING.md` finding 87c recorded the opposite -- that `wbak` writes its catalogue
and its data as separate streams and the mapping between them was the last
unknown. It does not. The type-2, type-0, type-1... type-8 sequence is
contiguous, and the reason a scan for 68000 opcodes found nothing next to
`ring8a.drvr`'s catalogue record is that its **first 986 bytes are zero** and
every 512 bytes of the file is interrupted by a block header and a repeat of the
type-9 record. There was never a mapping to find.

## What is read out of the 144-byte attribute record, and what is not

    +20  u32  the object's length in bytes
    +24  u32  the object's block count on disk

Both are used and both are checked. The rest of the record -- a UID, four
`4Cxxxxxx`-era timestamps, the parent's UID, ACL fields -- is *not* decoded,
because nothing here needs it and a guessed field name outlives the guess.

Note that the block count is the count **on disk**, and for a large object it
exceeds the data by one: `dex` is 217,392 bytes, 213 blocks of data, and the
record says 214. The file map is a block too. So the invariant worth asserting
is not the block count but

    sum of the type-1 record lengths  ==  ceil(length / 1024) * 1024

which holds for every object on the cartridge tested, and pins both ends of the
extraction: the first data record and the last.

Usage:
    ct_extract.py TAPE --list [--files]
    ct_extract.py TAPE --extract PATH [-o FILE]
    ct_extract.py TAPE --extract-all DIR
    ct_extract.py TAPE --verify

`--extract` matches on the whole stored path or on its trailing component, so
`--extract ring8a.drvr` finds `install/ri.apollo.os.v.10.3/sau8/ring8a.drvr`
and refuses rather than guesses if more than one object matches.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys

BLOCK = 512
LABEL = 80
HDR_LEN = 14
REC_HDR = 6
AEGIS_BLOCK = 1024

FILLER = bytes.fromhex("deaffaed")

LABELS = (b"VOL1", b"UVL1", b"HDR1", b"HDR2", b"UHL1", b"EOF1", b"EOF2", b"UTL1", b"EOV1")

REC_DIR = 9
REC_OBJECT = 2
REC_ATTRS = 0
REC_DATA = 1
REC_END = 8


class TapeError(Exception):
    """The image is not a labelled Apollo backup cartridge, or is damaged."""


class TapeFile:
    """One ANSI-labelled file on the cartridge: its identifier and extent."""

    def __init__(self, index, identifier, start, end):
        self.index = index
        self.identifier = identifier
        self.start = start
        self.end = end

    def __repr__(self):  # pragma: no cover - diagnostics only
        return "TapeFile(%d, %r, %#x, %#x)" % (
            self.index, self.identifier, self.start, self.end)


class Obj:
    """One object restored from the catalogue: path, length, and its data."""

    def __init__(self, path, uid):
        self.path = path
        self.uid = uid
        self.length = None
        self.blocks = None
        self.data = bytearray()

    @property
    def expected_data(self):
        """Bytes of type-1 record the object must carry, from its length."""
        return -(-self.length // AEGIS_BLOCK) * AEGIS_BLOCK


def read_labels(image):
    """The tape's files, from its ANSI labels.

    A `HDR1` opens a file and the `EOF1` naming the same identifier closes it.
    The data lies between the last label block of the header group and the
    `EOF1`, which is where the optional `UHL1` user label is accounted for
    without this having to know whether one is present.
    """
    files = []
    open_hdr = None
    for off in range(0, len(image) - LABEL + 1, BLOCK):
        tag = image[off:off + 4]
        if tag not in LABELS:
            continue
        if tag == b"HDR1":
            open_hdr = (image[off + 4:off + 21].decode("ascii", "replace").strip(), off)
        elif tag == b"EOF1":
            if open_hdr is None:
                raise TapeError("EOF1 at %#x with no open HDR1" % off)
            ident, hdr_off = open_hdr
            # Data begins after the header group: HDR1, HDR2 and any user
            # labels, each in its own 512-byte block.
            start = hdr_off + BLOCK
            while image[start:start + 4] in LABELS:
                start += BLOCK
            files.append(TapeFile(len(files) + 1, ident, start, off))
            open_hdr = None
    if not files:
        raise TapeError("no HDR1/EOF1 label pairs found -- not a labelled tape")
    return files


def _records(image, tape_file, verify=False):
    """Every record of one tape file, in order, as (type, stream, payload).

    Yields nothing for the `deaffaed` leader and trailer blocks. With `verify`
    set, a block whose records do not exactly fill its used length raises rather
    than being read past.
    """
    for off in range(tape_file.start, tape_file.end, BLOCK):
        block = image[off:off + BLOCK]
        if len(block) < HDR_LEN:
            raise TapeError("short block at %#x" % off)
        if block[:4] == FILLER:
            continue
        used = struct.unpack_from(">H", block, 12)[0]
        if used < HDR_LEN or used > BLOCK:
            raise TapeError("block at %#x claims %d bytes used" % (off, used))
        p = HDR_LEN
        while p + REC_HDR <= used:
            rtype, rlen, stream = struct.unpack_from(">HHH", block, p)
            end = p + REC_HDR + rlen
            if end > used:
                raise TapeError(
                    "record at %#x overruns its block (%d > %d)" % (off + p, end, used))
            yield rtype, stream, block[p + REC_HDR:end]
            p = end + (end & 1)
        if verify and p != used:
            raise TapeError(
                "block at %#x has %d bytes after its last record" % (off, used - p))


def objects(image, tape_file, verify=False, want=None):
    """The objects catalogued in one tape file.

    `want` is a predicate on the path; when given, the data of objects it
    rejects is discarded as it is read rather than accumulated, which keeps a
    single-object extraction from holding a 1.8 MB tape file's worth of it.
    """
    cur = None
    keep = True
    for rtype, _stream, payload in _records(image, tape_file, verify):
        if rtype == REC_OBJECT:
            cur = Obj(payload[12:].decode("latin1"), payload[:8].hex().upper())
            keep = want is None or want(cur.path)
        elif cur is None:
            continue
        elif rtype == REC_ATTRS:
            # Exactly 144 on every one of the 9,426 objects across the five
            # SR10.3 cartridges. A shorter one means the parse has
            # desynchronised, and reading two longwords out of it anyway is how
            # a desync turns into a plausible length.
            if len(payload) != 144:
                raise TapeError(
                    "attribute record of %d bytes for %s, expected 144"
                    % (len(payload), cur.path))
            cur.length, cur.blocks = struct.unpack_from(">II", payload, 20)
        elif rtype == REC_DATA:
            if keep:
                cur.data += payload
        elif rtype == REC_END:
            if keep:
                if cur.length is None:
                    raise TapeError("object %s ended with no attribute record" % cur.path)
                if len(cur.data) != cur.expected_data:
                    raise TapeError(
                        "object %s: %d bytes of data records, %d expected for a "
                        "length of %d" % (cur.path, len(cur.data),
                                          cur.expected_data, cur.length))
                yield cur
            cur = None


def find(image, path, verify=False):
    """The one object whose stored path is, or ends in, `path`."""
    def want(stored):
        return stored == path or stored.endswith("/" + path)

    hits = []
    for tf in read_labels(image):
        for obj in objects(image, tf, verify=verify, want=want):
            hits.append((tf, obj))
    if not hits:
        raise TapeError("no object matching %r on this cartridge" % path)
    if len(hits) > 1:
        raise TapeError("%r is ambiguous: %s" % (
            path, ", ".join(o.path for _t, o in hits)))
    return hits[0]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("tape", help="a .ct cartridge image")
    ap.add_argument("--list", action="store_true", help="list the objects")
    ap.add_argument("--files", action="store_true", help="list the tape's ANSI files")
    ap.add_argument("--extract", metavar="PATH", help="one object, by path or basename")
    ap.add_argument("--extract-all", metavar="DIR", help="every object, under DIR")
    ap.add_argument("-o", "--out", metavar="FILE", help="where --extract writes")
    ap.add_argument("--verify", action="store_true",
                    help="check every block's records fill it exactly")
    args = ap.parse_args(argv)

    with open(args.tape, "rb") as fh:
        image = fh.read()

    try:
        files = read_labels(image)
    except TapeError as exc:
        sys.exit("%s: %s" % (args.tape, exc))

    if args.files:
        for tf in files:
            print("%2d  %-24s %#010x..%#010x  %9d bytes"
                  % (tf.index, tf.identifier, tf.start, tf.end, tf.end - tf.start))
        return 0

    try:
        if args.extract:
            tf, obj = find(image, args.extract, verify=args.verify)
            out = args.out or os.path.basename(obj.path)
            with open(out, "wb") as fh:
                fh.write(bytes(obj.data[:obj.length]))
            print("%s -> %s (%d bytes, from tape file %d %s)"
                  % (obj.path, out, obj.length, tf.index, tf.identifier))
            return 0

        if args.extract_all:
            n = 0
            for tf in files:
                for obj in objects(image, tf, verify=args.verify):
                    dest = os.path.join(args.extract_all, obj.path.lstrip("/"))
                    os.makedirs(os.path.dirname(dest), exist_ok=True)
                    with open(dest, "wb") as fh:
                        fh.write(bytes(obj.data[:obj.length]))
                    n += 1
            print("%d objects under %s" % (n, args.extract_all))
            return 0

        # --list, and the default
        total = 0
        for tf in files:
            for obj in objects(image, tf, verify=args.verify):
                print("%2d %-17s %8d %4d  %s"
                      % (tf.index, tf.identifier, obj.length, obj.blocks, obj.path))
                total += 1
        print("%d objects" % total)
    except TapeError as exc:
        sys.exit("%s: %s" % (args.tape, exc))
    return 0


if __name__ == "__main__":
    sys.exit(main())
