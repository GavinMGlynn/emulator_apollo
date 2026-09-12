#!/usr/bin/env python3
"""Read an Apollo `.awd` disk image down to a file's blocks.

Everything this walks is on paper. The chain is the one `002398-03` chapter 2
draws and `002398-04` and `019411-A00` corroborate, and it was read at 600 dpi
in September 2026 -- see `docs/references/002398-03_WALK.md`. Nothing here is
inferred from an image.

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
    VTOC index (vtocx_t)                 DADDR of a VTOC block in bits 30-4 and
                                         an entry index in bits 3-0
    VTOC block (vtoc_blk_t)              a next-in-hash-bucket pointer and five
                                         204-byte entries; or 256 file-map
                                         entries when a VTOCE's .fm2 points here
    VTOC entry (vtoce)                   the object's UID, type, ACL, length and
                                         dates, then its file map
    file map                             32 direct block numbers, then three
                                         indirect levels of 256

## What does not resolve on a real image, stated rather than fudged

On the DS5500 volume this project builds, the labels read cleanly -- name,
UIDs, mount and dismount times, shut state, BAT free count -- and the VTOC
header is **internally consistent**: `.vtoc_blocks` is 226 and `.map[0]` is
"226 blocks at DADDR 40901", the same number from two fields.

But `.root_x` decodes to DADDR **39730**, which is *below* that extent, and the
block there is zeros. So the VTOCX-to-block step does not land, and this tool
prints the decode and the empty result rather than inventing a base to make it
fit.

**A DS5500 block is four sectors, and the layout is per cylinder.**

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
volume. **The VTOC *entry* is.** Searching every entry slot in a DS5500 image
for the root directory's object UID finds one, and the bytes around it do not
fit p. 2-23: where the figure puts `.version | .sys_type | flags` there is the
tail of a UID, and where it puts `.cur_len` and `.blocks_used` there are three
*consecutive small numbers* -- the shape of a file map. So SR10 changed the VTOC
entry as well as the directory entry, and the layouts it changed to are
documented nowhere on this shelf. Detail in
`docs/references/002398-03_WALK.md`.

## Where it stops, and why that is not a gap in the reading

At the **root directory's contents**. Every document on this shelf -- `[EH1]`
Apr 83, `[EH3]` Feb 85, `[AEGIS]` Fig 8-7 Jan 86, `002398-04` Feb 87 -- prints
the same directory entry: a 32-byte fixed name first. An SR10.4 volume does not
use it; its entries are header-first and variable length, measured and recorded
in the walk record at exactly that strength. The format is a **documentary
absence**, searched for and not found, so this tool reports the root directory's
VTOC entry and its blocks and does not pretend to parse them.
"""

import argparse
import struct
import sys

BLOCK = 1056
HEADER = 32
DATA = 1024

PV_LABEL_UID_HIGH = 0x00000200
LV_LABEL_UID_HIGH = 0x00000201

SHUT_STATE = {0: "dismounted", 1: "mounted", 2: "salvaged"}


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

    def blocks_per_cylinder(self):
        """Whole blocks in a cylinder; the remainder of the division is spare."""
        if self.sectors_per_cylinder == 0 or self.sectors_per_block == 0:
            return 0
        return self.sectors_per_cylinder // self.sectors_per_block

    def sector_of(self, daddr):
        """The first sector of the block a DADDR names.

        Identity where a block is one sector. Otherwise the cylinder walk
        above, because `sectors_per_cylinder` need not divide by the block's
        size and the leftover sectors are skipped rather than packed.
        """
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
        """The 1024 bytes of file data in block `n`, past its header."""
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

    def block_of_daddr(self, daddr):
        """The 1024 bytes of data of the block a DADDR names."""
        return self.block(self.sector_of(daddr))

    def header_of_daddr(self, daddr):
        return self.block_header(self.sector_of(daddr))

    def derive_geometry(self, pv):
        """Fill `sectors_per_block` and `sectors_per_cylinder` from the volume.

        The physical label gives the cylinder; the block's size in sectors is
        read off the labels themselves -- a block that is one sector has a
        header whose `daddr` is its own index, and one that is four does not.
        """
        self.sectors_per_cylinder = (pv["blocks_per_track"] *
                                     pv["tracks_per_cyl"])
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
    """`002398-03` p. 2-25's three forms."""
    if value & 0x80000000:
        return {"kind": "remote", "node_id": value & 0x000FFFFF}
    daddr = (value >> 4) & 0x0FFFFFFF
    if daddr == 0:
        return {"kind": "volx", "volx": value & 0xF}
    return {"kind": "local", "daddr": daddr, "indx": value & 0xF}


VTOCE_BYTES = 0xCC
VTOCE_AT = (0x04, 0xD0, 0x19C, 0x268, 0x334)


def vtoc_entry(vol, x):
    """The VTOC entry a VTOC index names."""
    d = vtocx(x)
    if d["kind"] != "local":
        return None
    b = vol.block(d["daddr"])
    if d["indx"] >= len(VTOCE_AT):
        return None
    e = b[VTOCE_AT[d["indx"]]:VTOCE_AT[d["indx"]] + VTOCE_BYTES]
    return {
        "vtocx": d,
        "next_add": u32(b, 0x00),
        "version": e[0x00],
        "sys_type": e[0x01],
        "flags": u16(e, 0x02),
        "uid": uid(e, 0x04),
        "type_uid": uid(e, 0x0C),
        "acl_uid": uid(e, 0x14),
        "cur_len": u32(e, 0x1C),
        "blocks_used": u32(e, 0x20),
        "dtu": u32(e, 0x24),
        "dtm": u32(e, 0x28),
        "dir_uid": uid(e, 0x2C),
        "fm": [u32(e, 0x40 + 4 * i) for i in range(32)],
        "fm2": [u32(e, 0xC0 + 4 * i) for i in range(3)],
    }


def file_blocks(vol, entry, limit=None):
    """The object's data block numbers, in order, following the file map.

    `002398-03` p. 2-11: 32 direct, then a level-1 block of 256, then level 2
    and level 3, each 256 pointers to the level below. The maximum is
    (32 + 256 + 256**2 + 256**3) blocks; `limit` stops a walk early.
    """
    out = []

    def take(daddr):
        if daddr == 0:
            return False
        out.append(daddr)
        return limit is None or len(out) < limit

    def level(daddr, depth):
        if daddr == 0:
            return True
        b = vol.block(daddr)
        for i in range(256):
            p = u32(b, 4 * i)
            if p == 0:
                continue
            ok = level(p, depth - 1) if depth > 1 else take(p)
            if not ok:
                return False
        return True

    for d in entry["fm"]:
        if d and not take(d):
            return out
    for depth, d in enumerate(entry["fm2"], start=1):
        if d and not level(d, depth):
            return out
    return out


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
    print(f"    block          {vol.sectors_per_block} sector(s); "
          f"{vol.blocks_per_cylinder()} per {vol.sectors_per_cylinder}-sector "
          f"cylinder, {vol.sectors_per_cylinder - vol.blocks_per_cylinder() * vol.sectors_per_block} spare")
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
    print(f"    vtoc           version {v['version']}, {v['vtoc_blocks']} "
          f"blocks used, hash over {v['vtoc_size']}")
    for i, (count, add) in enumerate(v["map"]):
        if count == 0 and add == 0:
            continue
        sector = vol.sector_of(add)
        head = vol.block_header(sector)
        agree = "agrees" if head["daddr"] == add else f"says {head['daddr']}"
        print(f"    vtoc extent    {count} blocks at DADDR {add} "
              f"-> sector {sector}, whose header {agree}")
    for name in ("net_x", "root_x", "os_x", "boot_x"):
        d = vtocx(v[name])
        print(f"    {name:<14} {v[name]:08X}  {d}")
    for name, x in (("root", v["root_x"]), ("os", v["os_x"]),
                    ("boot", v["boot_x"])):
        e = vtoc_entry(vol, x)
        if e is None:
            continue
        blocks = file_blocks(vol, e, limit=args.max_blocks)
        print(f"  {name} object")
        print(f"    uid            {e['uid'][0]:08X}{e['uid'][1]:08X}")
        print(f"    sys_type       {e['sys_type']} "
              f"({'file directory system-directory'.split()[e['sys_type']] if e['sys_type'] < 3 else '?'})")
        print(f"    length         {e['cur_len']} bytes in "
              f"{e['blocks_used']} blocks")
        print(f"    first blocks   {blocks[:8]}"
              f"{' ...' if len(blocks) > 8 else ''}")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("image", nargs="+")
    ap.add_argument("--max-blocks", type=int, default=64,
                    help="stop a file-map walk after this many blocks")
    args = ap.parse_args(argv)
    rc = 0
    for path in args.image:
        rc |= show(path, args)
    return rc


if __name__ == "__main__":
    sys.exit(main())
