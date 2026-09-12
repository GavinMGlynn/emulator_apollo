#!/usr/bin/env python3
"""`tools/awd_read.py` against a volume this test builds.

`media/` is gitignored -- the volumes are not ours to redistribute -- so a test
that read one would pass here and fail everywhere else. What this checks is that
the reader implements the structure `docs/references/002398-03_WALK.md` records:
the block framing, the two canned label UIDs, the label fields, the VTOCX split
and the file map's four levels.
"""

import struct
import sys
import unittest

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import awd_read as A


def build(pv_block=0, lv_block=1, blocks=64):
    """A volume with both labels, one VTOC block and one two-level file."""
    img = bytearray(blocks * A.BLOCK)

    def put(off, data):
        img[off:off + len(data)] = data

    def own(n, uid_high):
        put(n * A.BLOCK, struct.pack(">II", uid_high, 0))

    def data_at(n):
        return n * A.BLOCK + A.HEADER

    own(pv_block, A.PV_LABEL_UID_HIGH)
    own(lv_block, A.LV_LABEL_UID_HIGH)

    pv = data_at(pv_block)
    put(pv + 0x00, struct.pack(">H", 1))
    put(pv + 0x02, b"APOLLO")
    put(pv + 0x08, b"TESTVOL".ljust(32, b" "))
    put(pv + 0x28, struct.pack(">II", 0xA45AA673, 0x10012345))
    put(pv + 0x30, struct.pack(">HH", 0, 0x0504))
    put(pv + 0x34, struct.pack(">I", blocks))
    put(pv + 0x38, struct.pack(">HH", 18, 15))
    put(pv + 0x3C, struct.pack(">I", lv_block))

    # The VTOC block, and the entry index 2 inside it.
    vtoc_daddr = 20
    own(vtoc_daddr, 0xA0000001)
    v = data_at(vtoc_daddr)
    put(v + 0x00, struct.pack(">I", 0))
    e = v + A.VTOCE_AT[2]
    put(e + 0x00, bytes([1, 1]))              # version, sys_type = directory
    put(e + 0x04, struct.pack(">II", 0xA4610000, 0x00012345))
    put(e + 0x1C, struct.pack(">I", 4096))    # cur_len
    put(e + 0x20, struct.pack(">I", 4))       # blocks_used
    # Two direct blocks, then a level-1 block naming two more.
    put(e + 0x40, struct.pack(">I", 31))
    put(e + 0x44, struct.pack(">I", 32))
    level1 = 40
    put(e + 0xC0, struct.pack(">I", level1))
    l1 = data_at(level1)
    put(l1 + 0, struct.pack(">I", 33))
    put(l1 + 4, struct.pack(">I", 34))

    lv = data_at(lv_block)
    put(lv + 0x00, struct.pack(">H", 1))
    put(lv + 0x04, b"TESTVOL".ljust(32, b" "))
    put(lv + 0x24, struct.pack(">II", 0xA4615F51, 0x20012345))
    put(lv + 0x2C, struct.pack(">I", blocks))          # bat .n_blk
    put(lv + 0x30, struct.pack(">I", blocks - 10))     # bat .n_free
    put(lv + 0x3C, struct.pack(">I", 0))               # bat .vol_trouble
    put(lv + 0x4C, struct.pack(">HH", 2, 3607))        # vtoc version, size
    put(lv + 0x50, struct.pack(">I", 1))               # vtoc_blocks
    put(lv + 0x58, struct.pack(">I", (vtoc_daddr << 4) | 2))   # root_x
    put(lv + 0x64, struct.pack(">H", 1) + struct.pack(">I", vtoc_daddr))
    put(lv + 0xBC, struct.pack(">I", 0xA45DF6AB))      # mounted
    put(lv + 0xC0, struct.pack(">I", 0xA45E5C0C))      # dismounted
    put(lv + 0xCE, struct.pack(">H", A.SHUT_STATE and 0))
    return A.Volume(bytes(img))


class Blocks(unittest.TestCase):
    def test_a_block_is_1056_bytes_with_a_32_byte_header(self):
        """1024 of file data behind a header, which is why the logical label
        sits at 0x440 on a volume whose labels are blocks 0 and 1."""
        self.assertEqual(1056, A.BLOCK)
        self.assertEqual(32, A.HEADER)
        self.assertEqual(1024, A.DATA)
        vol = build()
        self.assertEqual(0x440, 1 * A.BLOCK + A.HEADER)
        self.assertEqual(A.DATA, len(vol.block(0)))

    def test_the_labels_are_found_by_their_block_headers(self):
        """Not at fixed offsets: a DS5500 volume puts them in blocks 0 and 4."""
        for pv_block, lv_block in ((0, 1), (0, 4), (3, 5)):
            vol = build(pv_block, lv_block)
            self.assertEqual(pv_block, A.pv_label(vol)["block"])
            self.assertEqual(lv_block, A.lv_label(vol)["block"])

    def test_a_file_without_the_signature_is_not_a_volume(self):
        vol = build()
        img = bytearray(vol.data)
        img[A.HEADER + 0x02] = ord("Z")
        self.assertIsNone(A.pv_label(A.Volume(bytes(img))))


class Labels(unittest.TestCase):
    def test_the_physical_label_reads_back(self):
        pv = A.pv_label(build())
        self.assertEqual("TESTVOL", pv["name"])
        self.assertEqual((0xA45AA673, 0x10012345), pv["id"])
        self.assertEqual(0x0504, pv["dtype"])
        self.assertEqual(18, pv["blocks_per_track"])
        self.assertEqual(15, pv["tracks_per_cyl"])
        self.assertEqual(1, pv["lv_list"][0])

    def test_the_logical_label_carries_the_bat_and_vtoc_headers(self):
        """`002398-04` names `+2C` and `+4C` as single blocks; `002398-03`
        pp. 2-3 and 2-24 decode them."""
        lv = A.lv_label(build())
        self.assertEqual("TESTVOL", lv["name"])
        self.assertEqual(64, lv["bat"]["n_blk"])
        self.assertEqual(54, lv["bat"]["n_free"])
        self.assertEqual(2, lv["vtoc"]["version"])
        self.assertEqual(3607, lv["vtoc"]["vtoc_size"])
        self.assertEqual(1, lv["vtoc"]["vtoc_blocks"])
        self.assertEqual((1, 20), lv["vtoc"]["map"][0])
        self.assertEqual(0xA45DF6AB, lv["mounted_time"])
        self.assertEqual(0, lv["sys_shut_state"])


class Indices(unittest.TestCase):
    def test_a_vtoc_index_splits_at_bit_four(self):
        """`002398-03` p. 2-25: DADDR in bits 30-4, INDX in 3-0, bit 31 set
        for a remote object, a zero DADDR for the volume-number form."""
        self.assertEqual({"kind": "local", "daddr": 20, "indx": 2},
                         A.vtocx((20 << 4) | 2))
        self.assertEqual({"kind": "remote", "node_id": 0x12345},
                         A.vtocx(0x80012345))
        self.assertEqual({"kind": "volx", "volx": 3}, A.vtocx(3))

    def test_the_five_entries_fill_the_block_exactly(self):
        """4 + 5 x 0xCC = 1024, which is what makes the offsets checkable."""
        self.assertEqual(A.DATA, 4 + 5 * A.VTOCE_BYTES)
        self.assertEqual((0x04, 0xD0, 0x19C, 0x268, 0x334), A.VTOCE_AT)


class FileMap(unittest.TestCase):
    def test_the_entry_and_its_file_map_read_back(self):
        vol = build()
        lv = A.lv_label(vol)
        e = A.vtoc_entry(vol, lv["vtoc"]["root_x"])
        self.assertIsNotNone(e)
        self.assertEqual((0xA4610000, 0x00012345), e["uid"])
        self.assertEqual(1, e["sys_type"])
        self.assertEqual(4096, e["cur_len"])
        # Two direct blocks, then the level-1 block's two.
        self.assertEqual([31, 32, 33, 34], A.file_blocks(vol, e))

    def test_the_walk_stops_where_it_is_told(self):
        vol = build()
        e = A.vtoc_entry(vol, A.lv_label(vol)["vtoc"]["root_x"])
        self.assertEqual([31, 32], A.file_blocks(vol, e, limit=2))

    def test_the_handbooks_maximum_file_size_is_rounded(self):
        """p. 2-11 prints "Maximum file size = (32+256+256**2+256**3)*1024
        bytes = **17,247,300,000** bytes", and gives the expression first.

        The expression comes to **17,247,272,960**. The two differ by 27,040 --
        0.00016% -- so the printed total is rounded and the expression is the
        figure. Asserted because a reader who took the round number for the
        limit would size a buffer 27 KB too large, and because checking a
        document's own arithmetic is cheap."""
        blocks = 32 + 256 + 256 ** 2 + 256 ** 3
        self.assertEqual(16843040, blocks)
        self.assertEqual(17247272960, blocks * A.DATA)
        self.assertNotEqual(17247300000, blocks * A.DATA)


if __name__ == "__main__":
    unittest.main(verbosity=2)
