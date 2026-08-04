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

typedef struct {
  /* Caller-owned. */
  uint8_t *data;
  size_t bytes;
  ap_awd_geometry_t geometry;
  bool writable;
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
 * `get_disk_address`. Out of range answers false rather than wrapping: a
 * head beyond the drive's is a driver's mistake and returning some other
 * track's data would hide it. */
[[nodiscard]] bool ap_awd_lba(ap_awd_geometry_t geometry, uint16_t cylinder,
                              uint8_t head, uint8_t sector, uint32_t *lba);

/* One sector in or out. `AP_AWD_SECTOR_BYTES` each way. */
[[nodiscard]] bool ap_awd_read(const ap_awd_t *image, uint32_t lba,
                               uint8_t *out);
[[nodiscard]] bool ap_awd_write(ap_awd_t *image, uint32_t lba,
                                const uint8_t *in);

#endif /* APOLLO_IMAGE_AP_AWD_H */
