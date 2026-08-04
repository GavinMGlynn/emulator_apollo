/* An Apollo floppy image: `.afd`, a raw run of sectors.
 *
 * ## The geometry, which the format does carry implicitly
 *
 * `ext/mame/src/lib/formats/apollo_dsk.cpp` gives one format and only one:
 *
 *     FF_525, DSHD, MFM, 1200 (1 us, 360 rpm)
 *     8 sectors, 77 cylinders, 2 heads, 1024 bytes a sector
 *
 * So a `.afd` is 1,261,568 bytes, and unlike the Winchester's `.awd` there is
 * nothing to choose: the extension names the shape. A file of another size is
 * refused rather than reinterpreted, because the only thing a wrong geometry
 * does is address the wrong sector and return plausible data.
 *
 * A 1024-byte sector here against the Winchester's 1056 is not an inconsistency:
 * `[OMTI]` §5.4.14's table lists 256, 512, 1024 and 1056 as the sector sizes
 * the controller supports, and the two media use different ones.
 *
 * ## Sector numbering starts at one
 *
 * `[OMTI]` §6.2 calls the field `R`, "the sector number", and the floppy
 * convention it inherits numbers sectors from 1 while cylinders and heads count
 * from 0. A reader treating `R` as a zero-based index is off by one sector on
 * every access -- which reads as data, not as an error, and is the reason this
 * is stated here rather than left to the caller.
 */

#ifndef APOLLO_IMAGE_AP_AFD_H
#define APOLLO_IMAGE_AP_AFD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* `apollo_dsk.cpp`'s single format. */
#define AP_AFD_CYLINDERS 77u
#define AP_AFD_HEADS 2u
#define AP_AFD_SECTORS 8u
#define AP_AFD_SECTOR_BYTES 1024u
#define AP_AFD_BYTES \
  (AP_AFD_CYLINDERS * AP_AFD_HEADS * AP_AFD_SECTORS * AP_AFD_SECTOR_BYTES)

typedef struct {
  /* Caller-owned, as every image in this core is. */
  uint8_t *data;
  size_t bytes;
  bool writable;
} ap_afd_t;

/* Attach an image. Refuses anything that is not exactly one Apollo floppy:
 * there is one format, so a different size is a different thing. */
[[nodiscard]] bool ap_afd_open(ap_afd_t *image, uint8_t *data, size_t bytes,
                               bool writable);

/* Cylinder, head and **one-based** sector to a linear sector number. */
[[nodiscard]] bool ap_afd_lba(uint16_t cylinder, uint8_t head, uint8_t sector,
                              uint32_t *lba);

[[nodiscard]] bool ap_afd_read(const ap_afd_t *image, uint32_t lba,
                               uint8_t *out);
[[nodiscard]] bool ap_afd_write(ap_afd_t *image, uint32_t lba,
                                const uint8_t *in);

#endif /* APOLLO_IMAGE_AP_AFD_H */
