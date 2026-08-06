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

/* `OMTI_DISK_SECTOR_SIZE`. 1024 of data and 32 beyond it; what the last
 * thirty-two carry is not established here, and no read this core performs
 * depends on it. */
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
