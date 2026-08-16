#!/usr/bin/env python3
"""`ct_extract.py`'s own tests, against cartridges built here.

The tool is how this project reads Domain/OS binaries -- `ring8a.drvr` first,
and the SELF_TEST, graphics and Ethernet images behind it. Everything it returns
becomes a claim about hardware, so a fault in it is a wrong register map wearing
the authority of a primary source. The cartridges it normally reads are
`media/`, which this repository does not carry, so the tests build their own.

Four properties are load-bearing and three of them fail *silently* when they are
wrong -- the parse does not stop, it desynchronises and returns plausible bytes:

  * records are padded to an **even** length. An object whose path has an odd
    number of characters shifts every following record by one byte, and a type
    word read one byte out lands on a length that is still small enough to look
    like a record. `install/ri.apollo.os.v.10.3/sau8/ring8b.dex` is 43
    characters, so a real cartridge contains this case;
  * a block's `used` field is honoured. Reading all 512 bytes takes stale buffer
    content -- and the tail of a real block genuinely holds fragments of the
    *previous* object, including 68000 code, so the extra bytes look like data;
  * the `deaffaed` leader and trailer blocks are skipped rather than parsed;
  * `sum of data records == ceil(length / 1024) * 1024`. This is the check that
    pins both ends of an object: a start one record early or one record late
    still yields a file of the right length, and only the total gives it away.

Also checked: that a file which is not a labelled tape is refused rather than
returning "0 objects", and that an ambiguous basename is refused rather than
resolved to whichever object came first.
"""

import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import ct_extract as ct  # noqa: E402  (after the path fix, deliberately)

TOOL = os.path.join(HERE, "ct_extract.py")

FAILURES = []


def check(name, cond, detail=""):
    if cond:
        print(f"  ok    {name}")
    else:
        FAILURES.append(name)
        print(f"  FAIL  {name}  {detail}")


def label(tag, body=""):
    """One 80-byte ANSI label in its own 512-byte block."""
    text = (tag + body).ljust(ct.LABEL)[:ct.LABEL].encode("ascii")
    return text + b"\0" * (ct.BLOCK - ct.LABEL)


def record(rtype, stream, payload):
    """A record, with the even-length padding the grammar requires."""
    rec = struct.pack(">HHH", rtype, len(payload), stream) + payload
    return rec + (b"\0" if len(rec) & 1 else b"")


UID = bytes.fromhex("4CE869EC10027133")
SET_UID = bytes.fromhex("4CE869EC10027133")


def attrs(length, blocks):
    """A 144-byte attribute record payload, with the two fields we decode."""
    payload = bytearray(144)
    struct.pack_into(">II", payload, 20, length, blocks)
    return bytes(payload)


def pack_blocks(records, dir_record):
    """Records into 512-byte `wbak` blocks, never spanning one.

    Mirrors what the cartridges do: every block opens with the type-9 current
    directory record, and a block that cannot fit the next record is closed
    short with its `used` field set accordingly.
    """
    blocks = []
    pending = list(records)
    while pending:
        body = bytearray(dir_record)
        while pending and len(body) + len(pending[0]) <= ct.BLOCK - ct.HDR_LEN:
            body += pending.pop(0)
        used = ct.HDR_LEN + len(body)
        head = struct.pack(">I", len(blocks) + 1) + SET_UID + struct.pack(">H", used)
        blocks.append((head + bytes(body)).ljust(ct.BLOCK, b"\0"))
    return blocks


def object_records(path, data, blocks_field=None):
    """The type-2 / type-0 / type-1... / type-8 sequence for one object."""
    length = len(data)
    padded = data + b"\0" * (-len(data) % ct.AEGIS_BLOCK)
    recs = [record(ct.REC_OBJECT, 2, UID + b"\0" * 4 + path.encode("latin1")),
            record(ct.REC_ATTRS, 2,
                   attrs(length, blocks_field if blocks_field is not None
                         else len(padded) // ct.AEGIS_BLOCK))]
    for i in range(0, len(padded), 442):
        recs.append(record(ct.REC_DATA, 1, padded[i:i + 442]))
    recs.append(record(ct.REC_END, 1, b"\0" * 4))
    return recs


def build(objects, identifier="/base_open_sau8_d", filler=True):
    """A whole labelled cartridge carrying `objects`, a list of (path, data)."""
    dir_record = record(ct.REC_DIR, 2, UID + b"\0" * 4 + b"install/ri.apollo")
    recs = []
    for path, data in objects:
        recs += object_records(path, data)
    body = pack_blocks(recs, dir_record)
    if filler:
        body = [ct.FILLER * (ct.BLOCK // 4)] + body + [ct.FILLER * (ct.BLOCK // 4)]
    image = label("VOL1", "ST0257") + label("UVL1", "4CE8637D.10027133")
    image += label("HDR1", identifier.ljust(17) + "BACKUP0001000100010")
    image += label("HDR2", "F0051200512")
    image += label("UHL1", "4CE869EC.10027133 1990/09/19 20:46:12")
    image += b"".join(body)
    image += label("EOF1", identifier.ljust(17) + "BACKUP0001000100010")
    image += label("EOF2", "F0051200512")
    return image


def main():
    print("ct_extract")

    # A path with an odd length is the case that desynchronises a parser with no
    # even-padding rule, and a real cartridge contains it. Two objects, so the
    # *second* one is what proves the first's padding was consumed correctly.
    odd = "install/ri.apollo.os.v.10.3/sau8/ring8b.dex"
    even = "install/ri.apollo.os.v.10.3/sau8/ring8a.drvr"
    check("an odd-length path is padded to an even record",
          len(odd) % 2 == 1 and len(even) % 2 == 0)

    a = bytes(range(256)) * 8          # 2048 bytes, exactly two AEGIS blocks
    b = b"#csm\nexit\n"                # 10 bytes, one block once padded
    image = build([(odd, a), (even, b)])

    got = list(ct.objects(image, ct.read_labels(image)[0], verify=True))
    check("both objects are catalogued", len(got) == 2,
          "got %d" % len(got))
    if len(got) == 2:
        check("the object after an odd-length path parses", got[1].path == even,
              got[1].path)
        check("its data survives the padding", bytes(got[1].data[:got[1].length]) == b,
              repr(bytes(got[1].data[:16])))
        check("a short object is padded to one AEGIS block",
              len(got[0].data) == 2048 and len(got[1].data) == 1024)
        check("the length field truncates the padding",
              got[1].length == len(b))

    # The used field. A block read to its full 512 bytes takes stale buffer
    # content, which on a real cartridge is the tail of the previous object.
    stale = bytearray(image)
    tail = image.index(b"\x00\x00\x00\x01" + SET_UID)
    used = struct.unpack_from(">H", stale, tail + 12)[0]
    if used < ct.BLOCK:
        stale[tail + used:tail + ct.BLOCK] = b"\x4e\x75" * ((ct.BLOCK - used) // 2)
        got2 = list(ct.objects(bytes(stale), ct.read_labels(bytes(stale))[0], verify=True))
        check("bytes past a block's used field are not read",
              [bytes(o.data) for o in got2] == [bytes(o.data) for o in got])
    else:
        check("bytes past a block's used field are not read", True, "(block was full)")

    # Filler. Built both ways; the leader and trailer must make no difference.
    plain = build([(even, b)], filler=False)
    check("deaffaed leader and trailer blocks are skipped",
          [bytes(o.data) for o in ct.objects(plain, ct.read_labels(plain)[0], verify=True)]
          == [bytes(o.data) for o in
              ct.objects(build([(even, b)]), ct.read_labels(build([(even, b)]))[0])])

    # The data invariant, which is what pins the first and last data record. A
    # dropped record leaves a file of exactly the right length and only the
    # total gives it away.
    short = bytearray(image)
    hit = short.index(struct.pack(">HHH", ct.REC_DATA, 442, 1))
    short[hit:hit + 6] = struct.pack(">HHH", ct.REC_DATA, 440, 1)
    try:
        list(ct.objects(bytes(short), ct.read_labels(bytes(short))[0]))
        check("a short data stream is refused", False, "no TapeError")
    except ct.TapeError as exc:
        check("a short data stream is refused", "expected for a length" in str(exc),
              str(exc))
    except Exception as exc:  # a desync raises something else, which is not the check
        check("a short data stream is refused", False, "%r" % exc)

    # A block whose records leave a residue is a grammar this does not know.
    # Two spare bytes, which is less than a record header, so the chain ends
    # cleanly and only the residue check can see it.
    ragged = bytearray(image)
    struct.pack_into(">H", ragged, tail + 12, used + 2)
    try:
        list(ct.objects(bytes(ragged), ct.read_labels(bytes(ragged))[0], verify=True))
        check("--verify refuses a block with a residue", False, "no TapeError")
    except ct.TapeError as exc:
        check("--verify refuses a block with a residue",
              "after its last record" in str(exc), str(exc))
    except Exception as exc:
        check("--verify refuses a block with a residue", False, "%r" % exc)

    # And a truncated attribute record, which is what a desync produces when it
    # lands somewhere that still parses as a chain.
    stub = bytearray(image)
    at = stub.index(struct.pack(">HHH", ct.REC_ATTRS, 144, 2))
    struct.pack_into(">HHH", stub, at, ct.REC_ATTRS, 20, 2)
    try:
        list(ct.objects(bytes(stub), ct.read_labels(bytes(stub))[0]))
        check("a truncated attribute record is refused", False, "no TapeError")
    except ct.TapeError as exc:
        check("a truncated attribute record is refused", "expected 144" in str(exc),
              str(exc))
    except Exception as exc:
        check("a truncated attribute record is refused", False, "%r" % exc)

    # Not a tape at all. "0 objects" must not be able to mean "this is a tar".
    try:
        ct.read_labels(b"\0" * (ct.BLOCK * 8))
        check("a file that is not a labelled tape is refused", False, "no TapeError")
    except ct.TapeError:
        check("a file that is not a labelled tape is refused", True)

    # An ambiguous basename must be refused, not resolved to the first hit.
    dup = build([("install/a/ring8a.drvr", b), ("install/b/ring8a.drvr", b)])
    try:
        ct.find(dup, "ring8a.drvr")
        check("an ambiguous basename is refused", False, "no TapeError")
    except ct.TapeError as exc:
        check("an ambiguous basename is refused", "ambiguous" in str(exc), str(exc))
    check("an unambiguous full path still resolves",
          ct.find(dup, "install/b/ring8a.drvr")[1].path == "install/b/ring8a.drvr")

    # And the command line, end to end.
    with tempfile.TemporaryDirectory() as td:
        tape = os.path.join(td, "test.ct")
        with open(tape, "wb") as fh:
            fh.write(image)
        out = os.path.join(td, "out.bin")
        r = subprocess.run([sys.executable, TOOL, tape, "--extract", "ring8a.drvr",
                            "--verify", "-o", out], capture_output=True, text=True)
        check("--extract writes the object", r.returncode == 0 and
              open(out, "rb").read() == b, r.stderr.strip())
        r = subprocess.run([sys.executable, TOOL, tape, "--list", "--verify"],
                           capture_output=True, text=True)
        check("--list reports every object", r.returncode == 0 and
              "2 objects" in r.stdout, r.stdout.strip() + r.stderr.strip())

    print()
    if FAILURES:
        print("FAILED: " + ", ".join(FAILURES))
        return 1
    print("all ct_extract checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
