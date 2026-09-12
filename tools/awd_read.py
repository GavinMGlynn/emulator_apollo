#!/usr/bin/env python3
"""Read an Apollo `.awd` disk image down to a named file's bytes.

Everything down to the VTOC header is on paper. The chain is the one
`002398-03` chapter 2 draws and `002398-04` and `019411-A00` corroborate, and it
was read at 600 dpi in September 2026 -- see `docs/references/002398-03_WALK.md`.
The last two links -- the SR10 VTOC entry and the SR10 directory entry -- are
**measured**, because the documents that print them do not exist on this shelf;
what was searched, and how each field was proven, is under *The two structures
no document on this shelf prints* below.

## Why it exists

The project had been reading volumes by **grepping the raw image for names**.
That is how a wrong root-directory block number got into a living document: the
byte offsets were right and the block numbers derived from them assumed a
1024-byte block. An Apollo block is **1056** -- 1024 of file data behind a
32-byte header -- and a reader that follows the structure is right by
construction.

## The chain

    block header (blk_hdr_t, 32 bytes)   says which object and which page a
                                         block is; the two canned label UIDs
                                         are what locates the labels
    physical volume label (pv_label_t)   `APOLLO` at +02, the volume name, and
                                         .lv_list[1..10], a DADDR per logical
                                         volume
    logical volume label (lv_label_t)    its +2C BAT header and +4C VTOC header
    VTOC header (vtoc_hdr_t)             .root_x, .os_x, .boot_x -- VTOC indices
                                         for the root directory, the paging file
                                         and the boot file -- and an 8-entry map
                                         of the VTOC's own extents
    VTOC index (vtocx_t)                 a VTOC page in bits 31-4 and an entry
                                         index in bits 3-0
    VTOC block                           entries from +008, then an 8-byte
                                         trailer: `FEDCA984` and the block's
                                         own VTOC page, which is the self-check
    VTOC entry (vtoce)                   the object's UID, its system type, the
                                         UID of the directory it is in, and its
                                         file map
    file map                             32 direct block numbers on a volume
                                         whose block is 1 KB, 64 where it is
                                         4 KB
    directory block                      a header, a sorted index of entry
                                         offsets, and a heap of variable-length
                                         entries growing down from the block's
                                         end

## The canned UIDs, which is how any of this is found

`00000200,0` is the physical volume label, `00000201,0` the logical volume
label, `00000202,0` the VTOC, `00000203,0` the one this reader does not name,
and `00000204,0` the VTOC's index. The last three are **volume-wide objects laid
out so that page P is at DADDR P+1**, which is what lets a VTOC index name a
block by page and be resolved in one step with no indirection at all.

## A stored block address is relative to the logical volume, and `002398-03` says so

A block number stored in a VTOC entry's file map, in a `vtocx`, or in the VTOC
header's extent map resolves at **`lv_base + stored`**, where `lv_base` is the
logical volume's own first physical block -- `.lv_list[0]` in the physical
volume label, and 1 on both volumes this project holds.

**Measured first, then explained.** Over the two volumes, **76,576** file-map
pointers were checked against the object UID *and* the page number in the target
block's header: 76,576 resolve at `stored + 1` and **not one resolves at
`stored`**. `.lv_list` was the control -- it stores a plain *physical* DADDR,
and the logical volume label is at physical block 1 on both.

**`002398-03` p. 2-10 states the rule outright**, under DISK/VOLUME FORMAT:
"**all disk addresses (DADDRs) in a logical volume are relative to the start of
a logical volume**". So the constant is not one; it is `lv_base`, which `dvte_t`
at `+16` calls `.lv_base` and which this reader now takes from the physical
label instead of assuming. On a volume whose logical volume began anywhere else
the assumed 1 would have been silently wrong, and the same page warns that it
can: "there may be dead space between logical volumes".

Zero therefore still means *no block*: block 0 of a logical volume is its own
label, which can never be a file's page.


## The two structures no document on this shelf prints

**Searched, not merely unfound.** `[EH1]` Apr 83 p. 5-22, `[EH3]` Feb 85
p. 2-23/2-24, `[AEGIS]` Fig 8-7 Jan 86 and `002398-04` Feb 87 all print the same
SR9 pair: a 204-byte VTOC entry and a directory entry with a 32-byte fixed name.
An SR10.4 volume uses neither. So both were derived from the volumes, and every
field below is either proven by a cross-check named here or left unnamed.

**The VTOC entry.** Entries start at **+008** of the VTOC block and are
**0x150 bytes** where the volume's block is 1 KB and **0x1D0** where it is 4 KB;
the block ends with `FEDCA984` and its own VTOC page number. Proven four ways:
the stride is exact and constant over 4,024 VTOC blocks; three entries fill a
1 KB block to the byte (`0x008 + 3*0x150 = 0x3F8`, where the trailer starts);
the trailer's page equals `root_x >> 4` on both volumes; and `root_x`'s entry is
the object whose UID the root directory's own data block carries in its header.
Named fields are `.version` (+00), `.sys_type` (+01), `.uid` (+04),
`.dir_uid` (+3C) and the file map (+D0) -- the last three each confirmed by a
consistency check this reader still makes. The bytes between +0C and +CF are
**not named**: they hold timestamps and rights masks whose meanings no document
here gives, and inventing names for them would be the opposite of what this
tool is for.

**The directory entry.** Entries occupy the block from the offset at header
**+10** to the block's end, walked linearly, each

    +00 u8   flags: 0x02 an entry, 0x04 a link, bit 7 set means deleted
    +01 u8   length of the name
    +02 u16  length of the link text, 0 when this is not a link
    +04 uid  the object's UID, `FFFF0000,0` on a link
    +0C u32  the object's `vtocx` -- **absent on a link**
    +10      the name, then the link text, padded to a longword
             (a link's text starts at +0C, since it has no `vtocx`)

Proven by reading both volumes' root directories whole: every entry's length
lands exactly on the next entry's header, the last lands exactly on the block's
end, `sysboot`'s `vtocx` on the DN3500 volume **is** that volume's `.boot_x` to
the bit, and the entries the sorted index at +80 omits are exactly those with
bit 7 set -- `sau7`, `sau8`, `sau9`, `sau11` and `sau12` on a volume whose SAU 14
was installed over them.

## A DS5500 block is four sectors, and the layout is per cylinder

Four consecutive 1056-byte sectors carry one logical block, all four with the
same header -- UID, page and DADDR. That is the **4 KB block** `[RN104]` names
for SAU 11, 12 and 14 (SS4.10.3 "4-KB disk block size", SS5.5 "4K-page
machines"). On a DN3500 volume every block's `daddr` equals its sector index; on
a DS5500 volume almost none does, which is the check `tools/kernel_symbols.py`
has always made and which that volume has always failed -- correctly.

**The mapping is not affine, it is per cylinder, and the label supplies every
term.** The physical label gives 18 blocks per track and 15 tracks per cylinder,
so a cylinder is **270 sectors**; 270 does not divide by 4, so a cylinder holds
**67 blocks and two sectors go spare**:

    sector = (daddr // 67) * 270 + (daddr % 67) * 4

Verified on 541 sampled blocks of the restored DS5500 volume -- every one whose
header is a real header agrees -- and on the two landmarks the volume names
itself: the VTOC's first extent, `daddr 40901`, lands on sector 164824 whose own
header reads `daddr 40901`; and the root directory's page 0, `daddr 41131`,
lands on sector 165750, which is where a search for its UID had already found
it.

`sectors_per_block` is derived rather than assumed -- 1 where a label block's
`daddr` equals its sector index, 4 where it does not -- so the same code reads
both families.

**The VTOC header is not what changed** -- `[EH1]` Apr 83 p. 5-22 and `[EH3]`
Feb 85 p. 2-24 print it identically, and this reads it coherently off a 1992
volume. The VTOC *entry* and the directory entry are.
"""

import argparse
import struct
import sys

BLOCK = 1056
HEADER = 32
DATA = 1024

PV_LABEL_UID_HIGH = 0x00000200
LV_LABEL_UID_HIGH = 0x00000201
VTOC_UID_HIGH = 0x00000202
VTOC_INDEX_UID_HIGH = 0x00000204

# The VTOC block's trailer, and the entries in front of it.
VTOC_MAGIC = 0xFEDCA984
VTOC_TRAILER_BYTES = 8
VTOCE_FIRST = 0x008
VTOCE_FM = 0x0D0
# Bit 31 of a file-map pointer is a flag and the address is the low 31 bits.
# Measured on the DN3500 volume: 160 entries carry it, on the object's last
# pages in 144 of them and elsewhere in 16, so it is not "the final page" and
# is not named here. Masking it takes the pointers that resolve against their
# own block headers from 74,642 to 74,869 of 74,920.
VTOCE_FM_FLAG = 0x80000000
# One per block size; both measured, and each fills its block exactly.
VTOCE_BYTES = {1024: 0x150, 4096: 0x1D0}

# Directory block: the header word holding the lowest entry offset, and the
# entry's own fields.
DIR_HEAP_OFFSET = 0x10
DIR_ENTRY_DELETED = 0x80
DIR_ENTRY_LINK = 0x04
DIR_ENTRY_UID = 0x04
DIR_ENTRY_VTOCX = 0x0C
DIR_TEXT_AT = {False: 0x10, True: 0x0C}

SHUT_STATE = {0: "dismounted", 1: "mounted", 2: "salvaged"}
# `002398-03` p. 2-8 names the first three: "SYSTYP: 0 - File, 1 - Directory,
# 2 - System directory", the same field as the block header's. The measurement
# agrees and says which objects are which: over 11,825 entries on the DN3500
# volume exactly two are type 2 -- the ones `.root_x` and `.net_x` name -- and
# every entry a directory block lists as a subdirectory is type 1. 3, 4 and 5
# occur on real volumes and that page does not name them.
SYS_TYPE = {0: "file", 1: "directory", 2: "system directory",
            3: "?3", 4: "?4", 5: "?5"}
DIRECTORY_TYPES = (1, 2)


def u16(b, off):
    return struct.unpack_from(">H", b, off)[0]


def u32(b, off):
    return struct.unpack_from(">I", b, off)[0]


def uid(b, off):
    return (u32(b, off), u32(b, off + 4))


class Volume:
    def __init__(self, data, sectors_per_block=None, sectors_per_cylinder=None):
        self.data = data
        # Both are derived from the image itself below, once the physical label
        # has been read; the arguments exist so a test can state them.
        self.sectors_per_block = sectors_per_block or 1
        self.sectors_per_cylinder = sectors_per_cylinder or 0
        # The logical volume's first physical block, from `.lv_list[0]`.
        # Defaulted to 1 so a `Volume` built by hand behaves as every volume
        # this project holds does; `derive_geometry` sets it from the label.
        self.lv_base = 1

    def block_bytes(self):
        """The volume's logical block: 1024 on a DN3500, 4096 on a DS5500."""
        return self.sectors_per_block * DATA

    def blocks_per_cylinder(self):
        """Whole blocks in a cylinder; the remainder of the division is spare."""
        if self.sectors_per_cylinder == 0 or self.sectors_per_block == 0:
            return 0
        return self.sectors_per_cylinder // self.sectors_per_block

    def sector_of(self, daddr):
        if self.sectors_per_block == 1:
            return daddr
        per = self.blocks_per_cylinder()
        if per == 0:
            return daddr
        return ((daddr // per) * self.sectors_per_cylinder +
                (daddr % per) * self.sectors_per_block)

    def blocks(self):
        return len(self.data) // BLOCK

    def block(self, n):
        """The 1024 bytes of file data in sector `n`, past its header."""
        at = n * BLOCK + HEADER
        if at + DATA > len(self.data):
            raise IndexError(f"block {n} is past the end of the image")
        return self.data[at:at + DATA]

    def block_header(self, n):
        at = n * BLOCK
        h = self.data[at:at + HEADER]
        if len(h) < HEADER:
            raise IndexError(f"block {n} is past the end of the image")
        return {
            "uid": uid(h, 0x00),
            "page": u32(h, 0x08),
            "dtm": u32(h, 0x0C),
            "blk_type": h[0x10],
            "sys_type": h[0x11],
            "chksum": u16(h, 0x1A),
            "daddr": u32(h, 0x1C),
        }

    def logical(self, daddr):
        """One whole logical block: every sector's data, concatenated."""
        first = self.sector_of(daddr)
        return b"".join(self.block(first + i)
                        for i in range(self.sectors_per_block))

    def header_of_daddr(self, daddr):
        return self.block_header(self.sector_of(daddr))

    def daddr_of(self, stored):
        """The physical DADDR a logical-volume-relative address names.

        `002398-03` p. 2-10: "all disk addresses (DADDRs) in a logical volume
        are relative to the start of a logical volume". `lv_base` is that
        start, read off the physical label rather than assumed -- it is 1 on
        both volumes this project holds, which is why an assumed 1 was right
        and would not have stayed right.
        """
        return self.lv_base + stored

    def pointed_at(self, stored):
        """The logical block a stored address names; see the module docstring.

        Every block address inside a VTOC entry, a `vtocx` or the VTOC header's
        extent map is one less than the target's own `daddr`.
        """
        return self.logical(self.daddr_of(stored))

    def derive_geometry(self, pv):
        """Fill `sectors_per_block` and `sectors_per_cylinder` from the volume.

        The physical label gives the cylinder; the block's size in sectors is
        read off the labels themselves -- a block that is one sector has a
        header whose `daddr` is its own index, and one that is four does not.
        """
        self.sectors_per_cylinder = (pv["blocks_per_track"] *
                                     pv["tracks_per_cyl"])
        if pv["lv_list"][0] != 0:
            self.lv_base = pv["lv_list"][0]
        self.sectors_per_block = 1
        lv = self.find_label(LV_LABEL_UID_HIGH)
        if lv is None:
            return
        head = self.block_header(lv)
        if head["daddr"] != lv and head["daddr"] != 0:
            # Four is the only other size this shelf documents: `[RN104]`'s
            # 4-KB block against a 1024-byte one. Checked rather than assumed.
            for candidate in (4, 2, 8):
                self.sectors_per_block = candidate
                if self.sector_of(head["daddr"]) <= lv < (
                        self.sector_of(head["daddr"]) + candidate):
                    return
            self.sectors_per_block = 1

    def find_label(self, uid_high, search=8):
        """The block index of the first block claimed by a canned label UID."""
        for n in range(min(search, self.blocks())):
            h = self.block_header(n)
            if h["uid"] == (uid_high, 0):
                return n
        return None


def pv_label(vol):
    n = vol.find_label(PV_LABEL_UID_HIGH)
    if n is None:
        return None
    b = vol.block(n)
    if b[0x02:0x08] != b"APOLLO":
        return None
    return {
        "block": n,
        "version": u16(b, 0x00),
        "name": b[0x08:0x28].rstrip(b" \0").decode("latin-1"),
        "id": uid(b, 0x28),
        "dtype": u16(b, 0x32),
        "blocks_per_pvol": u32(b, 0x34),
        "blocks_per_track": u16(b, 0x38),
        "tracks_per_cyl": u16(b, 0x3A),
        "lv_list": [u32(b, 0x3C + 4 * i) for i in range(10)],
        "alt_lv_list": [u32(b, 0x68 + 4 * i) for i in range(10)],
        "phys_badspot_daddr": u32(b, 0x98),
        "phys_diag_daddr": u32(b, 0x9C),
        "phys_sector_start": u16(b, 0xA0),
        "phys_sector_size": u16(b, 0xA2),
        "pre_comp": u16(b, 0xA4),
    }


def lv_label(vol):
    n = vol.find_label(LV_LABEL_UID_HIGH)
    if n is None:
        return None
    b = vol.block(n)
    return {
        "block": n,
        "version": u16(b, 0x00),
        "name": b[0x04:0x24].rstrip(b" \0").decode("latin-1"),
        "id": uid(b, 0x24),
        # BAT header, `002398-03` p. 2-3, label-relative
        "bat": {
            "n_blk": u32(b, 0x2C),
            "n_free": u32(b, 0x30),
            "daddr": u32(b, 0x34),
            "base_add": u32(b, 0x38),
            "vol_trouble": u32(b, 0x3C),
            "bat_step": u32(b, 0x40),
        },
        # VTOC header, `002398-03` p. 2-24, label-relative
        "vtoc": {
            "version": u16(b, 0x4C),
            "vtoc_size": u16(b, 0x4E),
            "vtoc_blocks": u32(b, 0x50),
            "net_x": u32(b, 0x54),
            "root_x": u32(b, 0x58),
            "os_x": u32(b, 0x5C),
            "boot_x": u32(b, 0x60),
            "map": [(u16(b, 0x64 + 6 * i), u32(b, 0x66 + 6 * i))
                    for i in range(8)],
        },
        "label_write_time": u32(b, 0xB0),
        "last_mounted_node": u32(b, 0xB4),
        "node_boot_time": u32(b, 0xB8),
        "mounted_time": u32(b, 0xBC),
        "dismounted_time": u32(b, 0xC0),
        "salvage_node": u32(b, 0xC4),
        "salvage_time": u32(b, 0xC8),
        "salvage_mode": u16(b, 0xCC),
        "sys_shut_state": u16(b, 0xCE),
        "utc_delta": struct.unpack_from(">h", b, 0xE0)[0],
        "bad_spot_barrier": u32(b, 0xEC),
    }


def vtocx(value):
    """`002398-03` p. 2-25's three forms, with the local one measured.

    The document says "DADDR of a VTOC block in bits 30-4". The field is a VTOC
    *page*, and the block is the one whose header `daddr` is that page plus one
    -- proven by the block's own trailer, which repeats the page.
    """
    if value & 0x80000000:
        return {"kind": "remote", "node_id": value & 0x000FFFFF}
    page = (value >> 4) & 0x0FFFFFFF
    if page == 0:
        return {"kind": "volx", "volx": value & 0xF}
    return {"kind": "local", "page": page, "indx": value & 0xF}


def vtoce_bytes(vol):
    """The VTOC entry's size on this volume, and how many fit in a block."""
    size = VTOCE_BYTES.get(vol.block_bytes())
    if size is None:
        return None, 0
    room = vol.block_bytes() - VTOCE_FIRST - VTOC_TRAILER_BYTES
    return size, room // size


def vtoc_block(vol, page):
    """A VTOC block, checked against the page it says it is.

    The last eight bytes are `FEDCA984` and the block's own VTOC page. A block
    that does not say so is not the one the index meant, and this returns None
    rather than decoding whatever is there.
    """
    try:
        b = vol.pointed_at(page)
    except IndexError:
        # An index can name a page off the end of this image -- an object on
        # another volume of the same logical volume. Not this reader's to
        # resolve, and not an error either.
        return None
    at = len(b) - VTOC_TRAILER_BYTES
    if u32(b, at) != VTOC_MAGIC or u32(b, at + 4) != page:
        return None
    return b


def vtoc_entry(vol, x):
    """The VTOC entry a VTOC index names."""
    d = vtocx(x)
    if d["kind"] != "local":
        return None
    size, per_block = vtoce_bytes(vol)
    if size is None or d["indx"] >= per_block:
        return None
    b = vtoc_block(vol, d["page"])
    if b is None:
        return None
    at = VTOCE_FIRST + d["indx"] * size
    e = b[at:at + size]
    if e[0x00] != 1:
        return None
    return {
        "vtocx": d,
        "version": e[0x00],
        "sys_type": e[0x01],
        "uid": uid(e, 0x04),
        "dir_uid": uid(e, 0x3C),
        "fm": [u32(e, VTOCE_FM + 4 * i)
               for i in range((size - VTOCE_FM) // 4)],
    }


def file_blocks(vol, entry, limit=None):
    """The object's data block DADDRs, in order, following the file map.

    Direct pointers only: the file map fills the entry to its last byte -- 32
    pointers where the block is 1 KB, 64 where it is 4 KB -- so the indirect
    levels `002398-03` p. 2-11 describes are somewhere in the entry's unnamed
    middle, and this reader does not guess which longword. A file longer than
    the direct map is reported short, and `file_is_whole` says so.
    """
    out = []
    for stored in entry["fm"]:
        if stored == 0:
            continue
        out.append(vol.daddr_of(stored & ~VTOCE_FM_FLAG))
        if limit is not None and len(out) >= limit:
            break
    return out


def file_confirmed(vol, entry, blocks):
    """How many of `blocks` their own block headers agree with.

    A block the file map names should carry the object's UID and its index in
    the map as its page. Checked rather than assumed, so a map this reader
    reads wrongly is reported as a disagreement instead of as file content.
    """
    agree = 0
    for page, daddr in enumerate(blocks):
        try:
            head = vol.header_of_daddr(daddr)
        except IndexError:
            continue
        if head["uid"] == entry["uid"] and head["page"] == page:
            agree += 1
    return agree


def file_is_whole(vol, entry, blocks):
    """Whether the direct map held the whole object.

    True when the map's last slot is empty, which is the only evidence in the
    entry that nothing spilled into an indirect level.
    """
    return entry["fm"][-1] == 0


def read_file(vol, entry):
    """An object's bytes, its pages in order."""
    return b"".join(vol.logical(d) for d in file_blocks(vol, entry))


def dir_entries(vol, block):
    """Every entry in one directory block, deleted ones included.

    Walked linearly from the heap offset the header carries at +10 to the end
    of the block; an entry's own length is what finds the next.
    """
    out = []
    at = u16(block, DIR_HEAP_OFFSET)
    while at + DIR_ENTRY_VTOCX <= len(block):
        flags = block[at]
        name_len = block[at + 1]
        link_len = u16(block, at + 2)
        link = (flags & ~DIR_ENTRY_DELETED) == DIR_ENTRY_LINK
        text_at = DIR_TEXT_AT[link]
        size = text_at + ((name_len + link_len + 3) & ~3)
        if size <= text_at and name_len == 0 and link_len == 0:
            break
        if at + size > len(block):
            break
        text = block[at + text_at:at + text_at + name_len + link_len]
        out.append({
            "offset": at,
            "flags": flags,
            "deleted": bool(flags & DIR_ENTRY_DELETED),
            "link": link,
            "name": text[:name_len].decode("latin-1"),
            "link_text": text[name_len:].decode("latin-1"),
            "uid": uid(block, at + DIR_ENTRY_UID),
            "vtocx": None if link else u32(block, at + DIR_ENTRY_VTOCX),
        })
        at += size
    return out


def directory(vol, entry):
    """Every live entry of a directory object, its blocks in order."""
    out = []
    for daddr in file_blocks(vol, entry):
        for e in dir_entries(vol, vol.logical(daddr)):
            # The heap ends with a one-byte NUL name: a stop, not an entry.
            if not e["deleted"] and e["name"] and "\0" not in e["name"]:
                out.append(e)
    return out


def resolve(vol, root_x, path):
    """Walk a `/`-separated path from the root directory.

    Returns (entry, trail) where `trail` names each component that resolved, so
    a caller can say which one failed.
    """
    entry = vtoc_entry(vol, root_x)
    trail = []
    for part in [p for p in path.split("/") if p]:
        if entry is None or entry["sys_type"] not in DIRECTORY_TYPES:
            return None, trail
        hit = next((e for e in directory(vol, entry)
                    if e["name"].lower() == part.lower()), None)
        if hit is None or hit["vtocx"] is None:
            return None, trail
        entry = vtoc_entry(vol, hit["vtocx"])
        trail.append(part)
    return entry, trail


def show(path, args):
    vol = Volume(open(path, "rb").read())
    pv = pv_label(vol)
    if pv is None:
        print(f"{path}: no Apollo physical volume label", file=sys.stderr)
        return 1
    vol.derive_geometry(pv)
    lv = lv_label(vol)
    print(f"{path}: {vol.blocks()} blocks of {BLOCK} "
          f"({HEADER} header + {DATA} data)")
    print(f"  physical label   block {pv['block']}")
    print(f"    name           {pv['name']}")
    print(f"    uid            {pv['id'][0]:08X}{pv['id'][1]:08X}")
    print(f"    dtype          {pv['dtype']:04X}")
    print(f"    blocks/volume  {pv['blocks_per_pvol']}")
    print(f"    geometry       {pv['blocks_per_track']} blocks/track, "
          f"{pv['tracks_per_cyl']} tracks/cylinder")
    print(f"    lv_list        {[hex(x) for x in pv['lv_list'] if x]}")
    spare = (vol.sectors_per_cylinder -
             vol.blocks_per_cylinder() * vol.sectors_per_block)
    print(f"    block          {vol.block_bytes()} bytes in "
          f"{vol.sectors_per_block} sector(s); {vol.blocks_per_cylinder()} per "
          f"{vol.sectors_per_cylinder}-sector cylinder, {spare} spare")
    if lv is None:
        print("  logical label    absent")
        return 0
    v = lv["vtoc"]
    print(f"  logical label    block {lv['block']}")
    print(f"    name           {lv['name']}")
    print(f"    uid            {lv['id'][0]:08X}{lv['id'][1]:08X}")
    print(f"    shut state     {lv['sys_shut_state']} "
          f"({SHUT_STATE.get(lv['sys_shut_state'], 'unknown')})")
    print(f"    mounted        {lv['mounted_time']:08X}")
    print(f"    dismounted     {lv['dismounted_time']:08X}")
    print(f"    bat            {lv['bat']['n_free']} free of "
          f"{lv['bat']['n_blk']} blocks, trouble {lv['bat']['vol_trouble']:08X}")
    size, per_block = vtoce_bytes(vol)
    print(f"    vtoc           version {v['version']}, {v['vtoc_blocks']} "
          f"blocks used, hash over {v['vtoc_size']}")
    if size:
        print(f"    vtoc entry     {size} bytes, {per_block} per block, "
              f"{(size - VTOCE_FM) // 4} direct file-map pointers")
    for count, add in v["map"]:
        if count == 0 and add == 0:
            continue
        physical = vol.daddr_of(add)
        head = vol.header_of_daddr(physical)
        agree = "agrees" if head["daddr"] == physical else f"says {head['daddr']}"
        print(f"    vtoc extent    {count} blocks at LV DADDR {add} "
              f"-> physical {physical}, sector {vol.sector_of(physical)}, "
              f"whose header {agree}")
    for name in ("net_x", "root_x", "os_x", "boot_x"):
        print(f"    {name:<14} {v[name]:08X}  {vtocx(v[name])}")
    if args.path is not None:
        return walk(vol, v["root_x"], args)
    for name, x in (("root", v["root_x"]), ("os", v["os_x"]),
                    ("boot", v["boot_x"])):
        e = vtoc_entry(vol, x)
        if e is None:
            print(f"  {name} object       does not resolve")
            continue
        blocks = file_blocks(vol, e, limit=args.max_blocks)
        print(f"  {name} object")
        print(f"    uid            {e['uid'][0]:08X}{e['uid'][1]:08X}")
        print(f"    sys_type       {e['sys_type']} "
              f"({SYS_TYPE.get(e['sys_type'], '?')})")
        print(f"    dir_uid        {e['dir_uid'][0]:08X}{e['dir_uid'][1]:08X}")
        print(f"    blocks         {blocks[:8]}"
              f"{' ...' if len(blocks) > 8 else ''}")
    return 0


def walk(vol, root_x, args):
    entry, trail = resolve(vol, root_x, args.path)
    where = "/" + "/".join(trail)
    if entry is None:
        print(f"  {args.path}: stops at {where}", file=sys.stderr)
        return 1
    blocks = file_blocks(vol, entry)
    whole = file_is_whole(vol, entry, blocks)
    print(f"  {where or '/'}")
    print(f"    uid            {entry['uid'][0]:08X}{entry['uid'][1]:08X}")
    print(f"    sys_type       {entry['sys_type']} "
          f"({SYS_TYPE.get(entry['sys_type'], '?')})")
    agree = file_confirmed(vol, entry, blocks)
    print(f"    blocks         {len(blocks)}, {agree} confirmed by their own "
          f"headers{'' if whole else '; the direct map is full -- short'}")
    if entry["sys_type"] in DIRECTORY_TYPES:
        for e in directory(vol, entry):
            if e["link"]:
                print(f"    {e['name']:<24} -> {e['link_text']}")
            else:
                print(f"    {e['name']:<24} vtocx {e['vtocx']:08X}  uid "
                      f"{e['uid'][0]:08X}{e['uid'][1]:08X}")
    elif args.extract:
        data = read_file(vol, entry)
        with open(args.extract, "wb") as f:
            f.write(data)
        print(f"    wrote          {len(data)} bytes to {args.extract}")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("image", nargs="+")
    ap.add_argument("--max-blocks", type=int, default=64,
                    help="stop a file-map walk after this many blocks")
    ap.add_argument("--path",
                    help="resolve this path from the root directory")
    ap.add_argument("--extract", metavar="FILE",
                    help="write the object --path names to FILE")
    args = ap.parse_args(argv)
    rc = 0
    for path in args.image:
        rc |= show(path, args)
    return rc


if __name__ == "__main__":
    sys.exit(main())
