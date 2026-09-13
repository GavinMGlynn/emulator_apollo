/* The SIMH magnetic-tape container, for the EXB-8200's medium.
 *
 * ## Why this format and not one of ours
 *
 * An 8mm tape is a sequence of **variable-length records and filemarks**, so it
 * cannot be a raw block image the way `image/ap_ct.h`'s `.ct` cartridge is --
 * there is no block size to divide by. Something has to carry the record
 * boundaries.
 *
 * Rather than invent a container, this uses the one every other emulator of
 * variable-block tape already reads and writes: the **SIMH magtape format**,
 * documented in `simh/doc/simh_magtape.txt` and used by SIMH, E11 and MAME. A
 * tape written here can be read by any of them and a tape written by any of
 * them can be read here, which is worth more than a format shaped exactly to
 * this drive.
 *
 * ## The format
 *
 * A record is a **32-bit little-endian length**, then that many data bytes
 * padded to an even count, then **the same length again**. The trailing copy is
 * what makes the format readable backwards, which is why a tape drive's reverse
 * SPACE can be implemented at all.
 *
 *     00000000              tape mark (a filemark)
 *     FFFFFFFF              end of medium
 *     FFFFFFFE              erase gap
 *     0xxxxxxx              a good record of that many bytes
 *     8xxxxxxx              a record the writer marked bad
 *
 * ## What it does not carry, named rather than hidden
 *
 * **The EXB-8200 has two filemark kinds** -- `[EXB]` ch. 22's short and long,
 * 480 KB and 2,160 KB -- and the SIMH format has one. A tape mark read from a
 * `.tap` file therefore becomes a **long** filemark, which is the right default
 * for two reasons: it is what a clear byte 05 bit 7 writes, and it is the only
 * one a later write may append into (§22's erase gap). The distinction survives
 * a round trip *within* a run and is lost across a save, and a short filemark
 * is written out as an ordinary tape mark rather than refused.
 *
 * **Bad records and erase gaps are read as data and as nothing** respectively.
 * This drive has no way to report "the writer marked this bad" that is not a
 * medium error it did not have, so the flag is dropped rather than turned into
 * one.
 */

#ifndef APOLLO_IMAGE_AP_TAP_H
#define APOLLO_IMAGE_AP_TAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "device/ap_exb8200.h"

/* The four reserved lengths. */
#define AP_TAP_MARK 0x00000000u
#define AP_TAP_EOM 0xFFFFFFFFu
#define AP_TAP_ERASE_GAP 0xFFFFFFFEu
#define AP_TAP_BAD 0x80000000u
#define AP_TAP_LENGTH_MASK 0x7FFFFFFFu

/* Parse `image` into the caller's record table and data area.
 *
 * `media` must arrive with `record`, `capacity`, `data` and `data_bytes` set;
 * this fills `records`, `data_used` and nothing else, so a caller keeps control
 * of `writable`, `medium_type` and `blocks_to_leot`.
 *
 * Fails on a length that does not fit the remaining image, on a trailing length
 * that does not match its leader -- the one structural check the format admits
 * -- and on a tape with more records or more data than the caller allowed. A
 * short read is never silently truncated: a container that does not parse is
 * not a tape. */
[[nodiscard]] bool ap_tap_load(ap_exb8200_media_t *media, const uint8_t *image,
                               size_t size);

/* How many bytes `ap_tap_save` would write for this medium, so a caller can
 * size a buffer without guessing. */
[[nodiscard]] size_t ap_tap_size(const ap_exb8200_media_t *media);

/* Write the medium out. Returns the number of bytes written, or 0 when `size`
 * is too small -- `ap_tap_size` is the exact figure. An end-of-medium marker
 * closes the image, which is what makes a short file and a finished tape
 * distinguishable. */
size_t ap_tap_save(const ap_exb8200_media_t *media, uint8_t *image,
                   size_t size);

/* A digest of an image, by the hash the machine state uses -- the same service
 * `ap_ct_digest_of` provides, and for the same reason: two tapes of equal size
 * must not hash alike until one of them is read. */
[[nodiscard]] uint64_t ap_tap_digest_of(const uint8_t *image, size_t size);

#endif /* APOLLO_IMAGE_AP_TAP_H */
