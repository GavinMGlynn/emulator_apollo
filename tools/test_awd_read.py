#!/usr/bin/env python3
"""`tools/awd_read.py` against a volume this test builds.

`media/` is gitignored -- the volumes are not ours to redistribute -- so a test
that read one would pass here and fail everywhere else. What this checks is that
the reader implements the structure `docs/references/002398-03_WALK.md` records:
the block framing, the canned UIDs, the label fields, the VTOCX split, the
`stored + 1` pointer convention, the VTOC block's trailer, and the SR10
directory entry.
"""

import struct
import sys
import unittest

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import awd_read as A

VTOC_PAGE = 20
ROOT_UID = (0xA4610000, 0x40012345)
SYS_UID = (0xA4610000, 0x60012345)


def dir_entry(name, vtocx=None, link=None, deleted=False, uid=(0, 0)):
    """One SR10 directory entry, as the reader's docstring records it."""
    flags = A.DIR_ENTRY_LINK if link is not None else 0x02
    if deleted:
        flags |= A.DIR_ENTRY_DELETED
    text = name.encode() + (link.encode() if link is not None else b"")
    head = bytes([flags, len(name)])
    head += struct.pack(">H", len(link.encode()) if link is not None else 0)
    head += struct.pack(">II", *uid)
    if link is None:
        head += struct.pack(">I", vtocx)
    return head + text + b"\0" * (-len(text) % 4)


def build(pv_block=0, lv_block=1, blocks=64):
    """A volume with both labels, a VTOC block and a root directory."""
    img = bytearray(blocks * A.BLOCK)

    def put(off, data):
        img[off:off + len(data)] = data

    def own(n, uid_high, page=0, daddr=None):
        put(n * A.BLOCK, struct.pack(">II", uid_high, 0))
        put(n * A.BLOCK + 0x08, struct.pack(">I", page))
        put(n * A.BLOCK + 0x1C, struct.pack(">I", n if daddr is None else daddr))

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

    # The VTOC block. Its page is VTOC_PAGE and it therefore lives at DADDR
    # VTOC_PAGE + 1 -- the convention every stored address here follows.
    vtoc_daddr = VTOC_PAGE + 1
    own(vtoc_daddr, A.VTOC_UID_HIGH, page=VTOC_PAGE)
    v = data_at(vtoc_daddr)
    put(v + A.DATA - A.VTOC_TRAILER_BYTES,
        struct.pack(">II", A.VTOC_MAGIC, VTOC_PAGE))

    size = A.VTOCE_BYTES[A.DATA]

    def entry(indx, sys_type, uid, dir_uid, fm):
        e = v + A.VTOCE_FIRST + indx * size
        put(e + 0x00, bytes([1, sys_type]))
        put(e + 0x04, struct.pack(">II", *uid))
        put(e + 0x3C, struct.pack(">II", *dir_uid))
        for i, daddr in enumerate(fm):
            put(e + A.VTOCE_FM + 4 * i, struct.pack(">I", daddr - 1))

    # Index 2 is the root directory, one block; index 0 a two-block file.
    entry(2, 2, ROOT_UID, (0, 0), [31])
    entry(0, 0, SYS_UID, ROOT_UID, [40, 41])
    own(31, ROOT_UID[0], page=0)
    put(31 * A.BLOCK + 0x04, struct.pack(">I", ROOT_UID[1]))
    for n, text in ((40, b"first page "), (41, b"second page")):
        own(n, SYS_UID[0], page=n - 40)
        put(n * A.BLOCK + 0x04, struct.pack(">I", SYS_UID[1]))
        put(data_at(n), text)

    heap = [
        dir_entry("sys", vtocx=(VTOC_PAGE << 4) | 0, uid=SYS_UID),
        dir_entry("tmp", link="`node_data/tmp"),
        dir_entry("sau7", vtocx=(VTOC_PAGE << 4) | 1, deleted=True),
        # The heap ends with a one-byte NUL name: a stop, not an entry.
        dir_entry("\0", link=""),
    ]
    body = b"".join(heap)
    d = data_at(31)
    put(d + 0x00, struct.pack(">H", 0x2005))
    put(d + A.DIR_HEAP_OFFSET, struct.pack(">H", A.DATA - len(body)))
    put(d + A.DATA - len(body), body)

    lv = data_at(lv_block)
    put(lv + 0x00, struct.pack(">H", 1))
    put(lv + 0x04, b"TESTVOL".ljust(32, b" "))
    put(lv + 0x24, struct.pack(">II", 0xA4615F51, 0x20012345))
    put(lv + 0x2C, struct.pack(">I", blocks))          # bat .n_blk
    put(lv + 0x30, struct.pack(">I", blocks - 10))     # bat .n_free
    put(lv + 0x3C, struct.pack(">I", 0))               # bat .vol_trouble
    put(lv + 0x4C, struct.pack(">HH", 2, 3607))        # vtoc version, size
    put(lv + 0x50, struct.pack(">I", 1))               # vtoc_blocks
    put(lv + 0x58, struct.pack(">I", (VTOC_PAGE << 4) | 2))    # root_x
    put(lv + 0x64, struct.pack(">H", 1) + struct.pack(">I", VTOC_PAGE))
    put(lv + 0xBC, struct.pack(">I", 0xA45DF6AB))      # mounted
    put(lv + 0xC0, struct.pack(">I", 0xA45E5C0C))      # dismounted
    put(lv + 0xCE, struct.pack(">H", 0))
    return A.Volume(bytes(img))


def root(vol):
    return A.vtoc_entry(vol, A.lv_label(vol)["vtoc"]["root_x"])


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
        self.assertEqual((1, VTOC_PAGE), lv["vtoc"]["map"][0])
        self.assertEqual(0xA45DF6AB, lv["mounted_time"])
        self.assertEqual(0, lv["sys_shut_state"])

    def test_the_lv_list_is_a_plain_daddr_and_not_one_less(self):
        """The exception that shows the `stored + 1` rule belongs to the VTOC
        structures and not to the image: `.lv_list[0]` is 1 on both volumes
        this project holds, and the logical volume label is at DADDR 1."""
        for lv_block in (1, 4):
            vol = build(lv_block=lv_block)
            self.assertEqual(lv_block, A.pv_label(vol)["lv_list"][0])
            self.assertEqual(lv_block, A.lv_label(vol)["block"])


class Indices(unittest.TestCase):
    def test_a_vtoc_index_names_a_vtoc_page_not_a_daddr(self):
        """`002398-03` p. 2-25 calls bits 30-4 a DADDR. It is a VTOC *page*,
        and the block is at that page plus one -- which the block's own trailer
        confirms, because it repeats the page."""
        self.assertEqual({"kind": "local", "page": 20, "indx": 2},
                         A.vtocx((20 << 4) | 2))
        self.assertEqual({"kind": "remote", "node_id": 0x12345},
                         A.vtocx(0x80012345))
        self.assertEqual({"kind": "volx", "volx": 3}, A.vtocx(3))

    def test_a_stored_address_is_one_less_than_the_daddr_it_names(self):
        vol = build()
        vol.derive_geometry(A.pv_label(vol))
        self.assertEqual(vol.logical(VTOC_PAGE + 1), vol.pointed_at(VTOC_PAGE))

    def test_the_vtoc_block_is_checked_against_the_page_it_says_it_is(self):
        """The trailer is `FEDCA984` and the block's own page. A block that
        disagrees is not the one the index meant."""
        vol = build()
        vol.derive_geometry(A.pv_label(vol))
        self.assertIsNotNone(A.vtoc_block(vol, VTOC_PAGE))
        img = bytearray(vol.data)
        at = (VTOC_PAGE + 1) * A.BLOCK + A.HEADER + A.DATA - 4
        img[at:at + 4] = struct.pack(">I", VTOC_PAGE + 9)
        broken = A.Volume(bytes(img))
        broken.derive_geometry(A.pv_label(broken))
        self.assertIsNone(A.vtoc_block(broken, VTOC_PAGE))

    def test_the_entries_and_the_trailer_fill_the_block_exactly(self):
        """Three 0x150-byte entries from +008 end at 0x3F8, where the trailer
        starts; eight 0x1D0-byte ones fit a 4-KB block."""
        self.assertEqual(0x3F8, A.VTOCE_FIRST + 3 * A.VTOCE_BYTES[1024])
        self.assertEqual(A.DATA, 0x3F8 + A.VTOC_TRAILER_BYTES)
        one = A.Volume(b"", sectors_per_block=1)
        four = A.Volume(b"", sectors_per_block=4)
        self.assertEqual((0x150, 3), A.vtoce_bytes(one))
        self.assertEqual((0x1D0, 8), A.vtoce_bytes(four))

    def test_the_file_map_fills_the_entry_to_its_last_byte(self):
        """32 direct pointers on a 1-KB-block volume, 64 on a 4-KB one, which
        is why the indirect levels cannot be after the map."""
        self.assertEqual(0x150, A.VTOCE_FM + 4 * 32)
        self.assertEqual(0x1D0, A.VTOCE_FM + 4 * 64)


class Geometry(unittest.TestCase):
    """A DADDR names a block; a block need not be one sector.

    The restored DS5500 volume's own numbers: 18 blocks per track and 15 tracks
    per cylinder from the physical label, four sectors to a 4-KB block, so 67
    blocks per 270-sector cylinder and **two sectors spare** -- 270 does not
    divide by four. Both landmarks the volume names itself land exactly:

        daddr 40901 (the VTOC's first extent) -> sector 164824
        daddr 41131 (the root directory, page 0) -> sector 165750
    """

    def ds5500(self):
        return A.Volume(b"", sectors_per_block=4, sectors_per_cylinder=270)

    def test_a_cylinder_holds_whole_blocks_and_spares_the_remainder(self):
        v = self.ds5500()
        self.assertEqual(67, v.blocks_per_cylinder())
        self.assertEqual(
            2, v.sectors_per_cylinder - v.blocks_per_cylinder() * v.sectors_per_block)

    def test_the_two_landmarks_the_volume_names_itself(self):
        v = self.ds5500()
        self.assertEqual(164824, v.sector_of(40901))
        self.assertEqual(165750, v.sector_of(41131))

    def test_the_first_blocks_are_where_a_naive_reading_expects(self):
        """Which is why the mapping was invisible for so long: it agrees with
        `4 * daddr` for the whole first cylinder and diverges after it."""
        v = self.ds5500()
        for daddr in range(0, 67):
            self.assertEqual(4 * daddr, v.sector_of(daddr))
        self.assertEqual(270, v.sector_of(67))
        self.assertNotEqual(4 * 67, v.sector_of(67))

    def test_a_one_sector_block_is_the_identity(self):
        """A DN3500 volume, where `daddr == index` for every block -- which is
        the check `tools/kernel_symbols.py` makes."""
        v = A.Volume(b"", sectors_per_block=1, sectors_per_cylinder=270)
        self.assertEqual(270, v.blocks_per_cylinder())
        for daddr in (0, 1, 269, 270, 160110, 164701):
            self.assertEqual(daddr, v.sector_of(daddr))

    def test_the_block_size_is_derived_from_the_labels(self):
        """Not assumed: a label block whose header `daddr` is its own index is
        one sector, and one whose is not is four."""
        vol = build()
        pv = A.pv_label(vol)
        vol.derive_geometry(pv)
        self.assertEqual(18 * 15, vol.sectors_per_cylinder)
        self.assertEqual(1, vol.sectors_per_block)
        self.assertEqual(1024, vol.block_bytes())


class Entries(unittest.TestCase):
    def test_the_entry_and_its_file_map_read_back(self):
        vol = build()
        e = root(vol)
        self.assertIsNotNone(e)
        self.assertEqual(ROOT_UID, e["uid"])
        self.assertEqual(2, e["sys_type"])
        self.assertEqual([31], A.file_blocks(vol, e))

    def test_an_entry_names_the_directory_it_is_in(self):
        """`.dir_uid` at +3C: the root's is zero and every other object's is
        the UID of a directory that lists it."""
        vol = build()
        self.assertEqual((0, 0), root(vol)["dir_uid"])
        sysfile = A.vtoc_entry(vol, (VTOC_PAGE << 4) | 0)
        self.assertEqual(ROOT_UID, sysfile["dir_uid"])

    def test_a_file_reads_back_its_pages_in_order(self):
        vol = build()
        vol.derive_geometry(A.pv_label(vol))
        data = A.read_file(vol, A.vtoc_entry(vol, (VTOC_PAGE << 4) | 0))
        self.assertEqual(2 * A.DATA, len(data))
        self.assertTrue(data.startswith(b"first page "))
        self.assertTrue(data[A.DATA:].startswith(b"second page"))

    def test_the_walk_stops_where_it_is_told(self):
        vol = build()
        e = A.vtoc_entry(vol, (VTOC_PAGE << 4) | 0)
        self.assertEqual([40], A.file_blocks(vol, e, limit=1))

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


class Directories(unittest.TestCase):
    def test_a_directory_lists_its_entries(self):
        vol = build()
        vol.derive_geometry(A.pv_label(vol))
        names = [e["name"] for e in A.directory(vol, root(vol))]
        self.assertEqual(["sys", "tmp"], names)

    def test_a_link_carries_its_text_and_has_no_vtocx(self):
        """A link's UID slot holds `FFFF0000,0` and the four bytes an entry
        would spend on a VTOCX are the first four of its text."""
        vol = build()
        vol.derive_geometry(A.pv_label(vol))
        link = [e for e in A.directory(vol, root(vol)) if e["link"]][0]
        self.assertEqual("tmp", link["name"])
        self.assertEqual("`node_data/tmp", link["link_text"])
        self.assertIsNone(link["vtocx"])

    def test_bit_seven_of_the_flags_means_deleted(self):
        """Measured: the entries the sorted index at +80 omits are exactly the
        ones with bit 7 set -- `sau7` through `sau12` on a volume whose SAU 14
        was installed over them."""
        vol = build()
        vol.derive_geometry(A.pv_label(vol))
        block = vol.logical(31)
        every = A.dir_entries(vol, block)
        self.assertEqual(["sys", "tmp", "sau7", "\0"],
                         [e["name"] for e in every])
        self.assertEqual([False, False, True, False],
                         [e["deleted"] for e in every])
        self.assertNotIn("sau7", [e["name"] for e in A.directory(vol, root(vol))])

    def test_each_entry_length_lands_on_the_next(self):
        """The walk is linear and the entry's own length is what finds the
        next, so the last must land exactly on the block's end."""
        vol = build()
        vol.derive_geometry(A.pv_label(vol))
        block = vol.logical(31)
        every = A.dir_entries(vol, block)
        self.assertEqual(A.u16(block, A.DIR_HEAP_OFFSET), every[0]["offset"])
        for a, b in zip(every, every[1:]):
            self.assertLess(a["offset"], b["offset"])

    def test_a_path_resolves_from_the_root(self):
        vol = build()
        vol.derive_geometry(A.pv_label(vol))
        lv = A.lv_label(vol)
        e, trail = A.resolve(vol, lv["vtoc"]["root_x"], "/sys")
        self.assertEqual(["sys"], trail)
        self.assertEqual(SYS_UID, e["uid"])
        e, trail = A.resolve(vol, lv["vtoc"]["root_x"], "/nope")
        self.assertIsNone(e)
        self.assertEqual([], trail)


if __name__ == "__main__":
    unittest.main(verbosity=2)
