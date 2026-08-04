/* Write a screenshot as a PNG.
 *
 * A frontend concern, deliberately: `src/core` knows nothing about file
 * formats, and the display controller's job ends at a pixel index. What that
 * index *looks like* is a lookup table's answer, and what it is stored as is
 * this file's.
 *
 * ## Why the item wants a PNG at all
 *
 * The plan's verification line for the drawing engine is "verify on a decoded
 * PNG rather than on register round-trips — a controller that passes register
 * tests and draws nothing is the standard way this goes wrong". Register
 * identities and word-level algebra are checkable without ever forming a
 * picture, and every one of them can pass while the image is mirrored, sheared,
 * or in the wrong colours. Only the picture catches those.
 *
 * ## libpng, and what happens without it
 *
 * This is compiled only when `find_package(PNG)` found one. CI builds on three
 * platforms and libpng is not present on all of them unprompted, so a hard
 * dependency would trade a screenshot for a red tree. Without it the entry
 * point still exists and reports that the build has no PNG support, which is a
 * different answer from "the write failed" and is worth telling apart.
 *
 * Nothing else in this project depends on the result: the goldens that must be
 * byte-identical on every platform are the probe goldens, and a screenshot is
 * an instrument rather than a fixture.
 */

#ifndef APOLLO_FRONTEND_AP_PNG_H
#define APOLLO_FRONTEND_AP_PNG_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  AP_PNG_OK = 0,
  /* Built without libpng. Not a failure of this call. */
  AP_PNG_UNSUPPORTED,
  AP_PNG_BAD_ARGUMENT,
  AP_PNG_CANNOT_OPEN,
  AP_PNG_WRITE_FAILED,
} ap_png_status_t;

[[nodiscard]] const char *ap_png_status_name(ap_png_status_t status);

/* True when this build can write one at all. */
[[nodiscard]] bool ap_png_available(void);

/* Write `width * height` bytes of 8-bit palette index as an indexed-colour
 * PNG, with `palette_entries` RGB triples behind them.
 *
 * Indexed rather than RGB on purpose. The controller produces an index and the
 * lookup table turns it into a colour, so storing the index with its palette
 * keeps the two separable in the file exactly as they are in the hardware — a
 * screenshot whose palette is wrong can be told from one whose *drawing* is
 * wrong, which an already-flattened RGB image cannot show.
 *
 * An index with no palette entry behind it is an error rather than a black
 * pixel: it means the scanout and the lookup table disagree about how many
 * colours the screen has, and that is worth a failure. */
[[nodiscard]] ap_png_status_t ap_png_write_indexed(
    const char *path, const uint8_t *pixels, uint32_t width, uint32_t height,
    const uint8_t (*palette)[3], unsigned palette_entries);

#endif /* APOLLO_FRONTEND_AP_PNG_H */
