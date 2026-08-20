/* An Apollo Winchester image: `.awd`, a raw run of physical sectors.
 *
 * ## The format, which is not a format
 *
 * There is no container, no header and no directory -- sector `n` is at file
 * offset `n * sector_bytes` and nothing else is in the file. The oracle settles
 * both halves of that: `omti8621.cpp` reads with
 * `fseek(diskaddr * OMTI_DISK_SECTOR_SIZE)` and `OMTI_DISK_SECTOR_SIZE` is
 * **1056**, so the sector carries thirty-two bytes beyond the 1024 the file
 * system uses.
 *
 * The eleven images in `media/` are 364,904,448 bytes, which is 348 MiB exactly
 * and *not* a whole number of 1056-byte sectors -- `mdsession.py` creates the
 * file at a size the install procedure names and the drive uses only the part it
 * needs. So a short final sector is a property of these images rather than a
 * corruption, and this module bounds by sector count rather than by file size.
 *
 * ## The geometry is the drive's, not the image's
 *
 * Nothing in the file says how it is shaped. The controller is told, and
 * `omti8621.cpp`'s disk types give the two Apollo shipped:
 *
 *     348 MB  Maxtor EXT-4380-E   1223 cylinders, 15 heads, 18 sectors
 *     155 MB  Micropolis 1355     1023 cylinders,  8 heads, 18 sectors
 *
 * **Apollo's own manual names both drives**, which was not known when the two
 * were taken from the oracle: `008778-03` Table 6-4 is headed "Micropolis 1355"
 * and Table 6-5 "Maxtor EXT-4380". Table 6-6 then publishes the geometry, and
 * it confirms the 155 Mbyte row exactly -- 1023 cylinders, 8 heads, 18 sectors
 * per track, 20,832 bytes per track, 170 MB unformatted.
 *
 * **Its 348 Mbyte column is an erratum, and the table's own arithmetic proves
 * it.** That column prints "Capacity 380 MB" and "Capacity per track 20,808
 * bytes" -- and then "Number of cylinders 1023, Number of heads 8", copied
 * across from the 155 Mbyte column beside it. 20,808 x 8 x 1023 is 170 MB, not
 * the 380 MB printed three rows above in the same column, so the column
 * contradicts itself. 20,808 x **15** x **1223** is 381.7 MB, which is the
 * stated capacity. Table 6-5 carries the same slip one table earlier: its
 * "Formatted sectors 147,312" is 1023 x 8 x 18, the 155 Mbyte drive's count,
 * where 1223 x 15 x 18 is 330,210.
 *
 * So Apollo's published *capacities* confirm the oracle's 1223 x 15 geometry
 * that this module already used, and Apollo's published *cylinder and head
 * counts* for that one drive are wrong. This is the first time in the
 * `008778-03` walk that the oracle has out-accurated the manual; twice before
 * it went the other way. Recorded in `docs/references/008778-03_WALK.md`.
 *
 * ## A third source, disagreeing by exactly one cylinder each
 *
 * `002398-04` p. 6-2's DISK PARAMETERS table gives both drives again, under
 * *Winchester Dtype Class 600 -- 5 1/4" ESDI Interface*:
 *
 *     604  Maxtor 380     1224  15  18  330480 (50AF0)
 *     607  Microp. 170    1024   8  18  147456 (24000)
 *
 * **One more cylinder than this module uses, in both rows** -- and the next page
 * shows the two documents are counting different things rather than
 * disagreeing. p. 6-4 tabulates each drive's reserved cylinders:
 *
 *     604  Max 380   BAD-SPOT 508D4/1222   DIAGNOSTIC 506B8/1220   CYLS USED 1220
 *     607  Mic 170   BAD-SPOT 23EE0/1022   DIAGNOSTIC 23DC0/1020   CYLS USED 1020
 *
 * with the note: "**For the 6XX dtypes four cylinders are reserved** - the last
 * for manufacturer encoded badspots, two diagnostic cylinders, and Apollo's
 * badspot cylinder."
 *
 * 1220 used plus 4 reserved is **1224**, and 1020 plus 4 is **1024**. So the
 * handbook's figure is the cylinder *count* and the oracle's is the highest
 * cylinder *number* -- 0 through 1223 is 1224 cylinders -- and both describe the
 * same drive. The badspot cylinder at 1222 and the diagnostic pair at 1220-1221
 * only exist if 1223 does.
 *
 * `008778-03` Table 6-5's "Formatted sectors 147,312" is 1023 x 8 x 18, the
 * same convention as the oracle's; p. 6-2's 147,456 is 1024 x 8 x 18. Neither is
 * wrong.
 *
 * **This module keeps `cylinders` as a count and so should carry 1224 and 1024
 * to be exact**; it carries 1223 and 1023, which makes the *last* cylinder
 * unreachable and nothing else. The cost is bounded and known: the LBA mapping
 * below is `(cylinder * heads + head) * sectors + sector`, which does not use
 * the cylinder count at all -- only `heads` and `sectors` -- so an off-by-one
 * count can never misplace a block, and the cylinder it hides is inside the four
 * Apollo reserves. Domain/OS uses 1220 of 1224. Left as it is rather than
 * changed, because the value is the oracle's and a differential against the
 * oracle is worth more here than a cylinder no filesystem addresses.
 *
 * **The Class 500 block on the same page is an erratum and is not used.** Its
 * three rows print `73458` in the TOTAL BLOCKS column for three different
 * geometries -- 1224x7x18, 1224x15x18 and 1024x8x18, which are 154,224, 330,480
 * and 147,456 -- and Class 600 prints each of those three correctly for the same
 * three shapes one block down. A column repeated down three rows is what a
 * copy-paste looks like.
 *
 * A caller names the type; there is nothing to detect. An image whose size does
 * not reach the geometry's last sector is accepted and short -- a read past the
 * end fails rather than returning whatever follows in memory, which is the same
 * rule `machine/ap_machine.h` keeps for RAM.
 *
 * ## The core allocates nothing
 *
 * The caller owns the bytes, as it owns main memory and the boot PROM. A 348 MB
 * image is mapped or read by a frontend; a test builds a small one on the stack
 * with a geometry to suit, which is only possible because the geometry is a
 * parameter rather than a constant.
 */

#ifndef APOLLO_IMAGE_AP_AWD_H
#define APOLLO_IMAGE_AP_AWD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* `OMTI_DISK_SECTOR_SIZE`. 1024 of data and 32 beyond it.
 *
 * **What the last thirty-two carry is established**: `002398-04` p. 2-8's
 * DISK BLOCK HEADER, `blk_hdr_t` in `base.ins.pas`, which is exactly 32 bytes --
 *
 *     +00  UID of the object the block belongs to   (64 bits)
 *     +08  page number in file                      .page
 *     +0C  time written, `clock.high32`             .dtm
 *     +10  BLKTYP | SYSTYP                          .blk_type, .sys_type
 *     +14  unused
 *     +18  checksum                                 .chksum
 *     +1C  disk address                             .daddr
 *
 * with `BLKTYP` 0 for a data block and 1, 2 or 3 for a level-1, -2 or -3 index
 * block in the file map, and `SYSTYP` 0 file, 1 directory, 2 system directory.
 * So the 32 bytes are the file system's per-block header: every block on an
 * Apollo volume says which object and which page of it it is, when it was
 * written, and what it is for.
 *
 * That closes a note this file carried since it was written, and it explains
 * the sector size rather than merely recording it -- 1056 is 1024 of file data
 * plus a 32-byte header, not a drive's formatting overhead.
 *
 * **Nothing here changes.** No read this core performs depends on the header:
 * `.awd` images are whole sectors and the emulator moves them verbatim, header
 * and all, exactly as the controller does. What the transcription buys is a
 * reader of a raw image knowing what the first 32 bytes of every sector are --
 * and, for the volume-comparison work, that `.uid` and `.daddr` are there to be
 * checked against the address a block was read from.
 *
 * **Confirmed from Apollo's own requirement**, which was previously only the
 * oracle's constant: `008778-03` §6.2, "The 155-MB and 348-MB drives must be
 * formattable for 18 sectors per track (**1056 total bytes in each sector**)".
 * Table 6-6's "Format (reference only)" row prints 1117 bytes/sector for the
 * same two drives -- 61 bytes more, and 18 x 1117 = 20,106 still fits inside
 * that table's 20,808-byte track, so the larger figure is the byte cell
 * including its gap and the smaller is the sector. The 72 Mbyte ST412 drive is
 * formatted differently again: 9 sectors of 1114 bytes, which both places
 * agree on and which this core does not model. */
#define AP_AWD_SECTOR_BYTES 1056u

/* The two drives `omti8621.cpp` configures, by its own type codes. */
typedef enum {
  AP_AWD_DRIVE_348MB, /* Maxtor EXT-4380-E, type 0x604 */
  AP_AWD_DRIVE_155MB, /* Micropolis 1355, type 0x607 */
} ap_awd_drive_t;

typedef struct {
  uint16_t cylinders;
  uint16_t heads;
  uint16_t sectors;
} ap_awd_geometry_t;

/* ## The sidecar: what a raw sector image cannot carry
 *
 * An `.awd` is sector data and nothing else. A real ESDI surface carries two
 * more things per sector, and `[OMTI]` §5 has commands for both -- the **ID
 * field**, whose flags §5.4.7 sets for a bad track and §5.4.16 for an
 * alternate, and the **ECC field**, six bytes that §5.4.27 READ LONG returns
 * and §5.4.28 WRITE LONG is given.
 *
 * They live beside the image rather than in it, because
 * `docs/references/DOMAINOS_IMAGE.md` pins the image's SHA-256 as the identity
 * of an artefact that cannot be rebuilt bit-identically. Appending to the file
 * would invalidate that pin. `docs/references/AWD_META.md` has the layout and
 * the reasoning; this is the part the core needs.
 *
 * **Optional.** With no sidecar attached the drive is a defect-free surface
 * with no recorded ECC -- which is what a raw image *is*, so it is a
 * description rather than a fallback, and every existing image keeps working. */
#define AP_AWD_META_MAGIC "AWDMETA1"
#define AP_AWD_META_MAGIC_BYTES 8u
#define AP_AWD_META_HEADER_BYTES 16u
#define AP_AWD_META_RECORD_BYTES 7u
#define AP_AWD_ECC_BYTES 6u

/* ID field flags, one byte per sector. */
#define AP_AWD_FLAG_BAD_TRACK 0x01u  /* §5.4.7 */
#define AP_AWD_FLAG_ALTERNATE_ASSIGNED 0x02u /* §5.4.16 */
#define AP_AWD_FLAG_IS_ALTERNATE 0x04u       /* §5.4.16 */

typedef struct {
  /* Caller-owned. */
  uint8_t *data;
  size_t bytes;
  ap_awd_geometry_t geometry;
  bool writable;

  /* The sidecar, caller-owned and optional. `meta` is the whole file including
   * its header; `meta_records` is how many per-sector records it holds, which
   * may be fewer than the geometry has -- the same rule a short image follows.
   * `meta_record_bytes` comes from the header, so a file written by a later
   * version is read for the fields this one knows. */
  uint8_t *meta;
  size_t meta_bytes;
  uint64_t meta_records;
  unsigned meta_record_bytes;
} ap_awd_t;

[[nodiscard]] ap_awd_geometry_t ap_awd_geometry_for(ap_awd_drive_t drive);

/* How many sectors the geometry describes, which is not how many the image
 * holds. */
[[nodiscard]] uint32_t ap_awd_sector_count(ap_awd_geometry_t geometry);

/* Attach an image. `data` may be shorter than the geometry needs; reads beyond
 * what it holds fail. */
[[nodiscard]] bool ap_awd_open(ap_awd_t *image, uint8_t *data, size_t bytes,
                               ap_awd_geometry_t geometry, bool writable);

/* Cylinder, head and sector to a linear sector number.
 *
 * `(cylinder * heads + head) * sectors + sector`, which is the ordinary CHS
 * mapping and what the oracle computes in `get_disk_track` and
 * `get_disk_address`.
 *
 * ## The sector number is *not* bounded by the track
 *
 * Cylinder and head are: a head beyond the drive's is a driver's mistake and
 * returning some other track's data would hide it. The **sector** is different,
 * and the boot PROM is what says so -- its drive test reads cylinder 0, head 0,
 * sectors `0` through `24` in sequence, on a drive with **eighteen** sectors to
 * the track. A controller that refused sector 18 could not run this machine's
 * firmware, and this one did: it failed seven of those twenty-five reads with
 * `21 ILLEGAL DISK ADDRESS` and left the PROM polling a phase that never came.
 *
 * `[OMTI]` §5.1.1 gives the address as a *format* -- six bits of sector number
 * in byte 2, eleven of cylinder across bytes 1-3 -- and says nothing about
 * validity, so the arithmetic is what defines the address and a sector number
 * past the track simply carries into the next one. The oracle agrees and checks
 * exactly this pair of fields, cylinder and head, and never the sector against
 * the track.
 *
 * What still bounds it is the drive: an address past the last sector the
 * geometry has is refused, which is the check the arithmetic can actually
 * support. */
[[nodiscard]] bool ap_awd_lba(ap_awd_geometry_t geometry, uint16_t cylinder,
                              uint8_t head, uint8_t sector, uint32_t *lba);

/* One sector in or out. `AP_AWD_SECTOR_BYTES` each way. */
[[nodiscard]] bool ap_awd_read(const ap_awd_t *image, uint32_t lba,
                               uint8_t *out);
[[nodiscard]] bool ap_awd_write(ap_awd_t *image, uint32_t lba,
                                const uint8_t *in);

/* Attach a sidecar. Fails on a bad magic or a header this core cannot read;
 * a *short* file is not a failure, it simply describes fewer sectors.
 * `AWD_META.md` has the layout. */
[[nodiscard]] bool ap_awd_attach_meta(ap_awd_t *image, uint8_t *meta,
                                      size_t bytes);

/* The ID field's flags for a sector. Zero with no sidecar, or past what one
 * describes: no flags is a defect-free sector, which is the honest default. */
[[nodiscard]] uint8_t ap_awd_flags(const ap_awd_t *image, uint32_t lba);

/* Set them. False without a writable sidecar covering that sector -- a format
 * that cannot record its flags must say so rather than appear to. */
[[nodiscard]] bool ap_awd_set_flags(ap_awd_t *image, uint32_t lba,
                                    uint8_t flags);

/* The six ECC bytes as recorded. Zero-filled with no sidecar, which is "none
 * recorded" and not a computed value -- `[OMTI]` publishes no polynomial. */
void ap_awd_ecc(const ap_awd_t *image, uint32_t lba, uint8_t *out);

/* Record six ECC bytes. False without a writable sidecar covering that
 * sector. */
[[nodiscard]] bool ap_awd_set_ecc(ap_awd_t *image, uint32_t lba,
                                  const uint8_t *ecc);

#endif /* APOLLO_IMAGE_AP_AWD_H */
