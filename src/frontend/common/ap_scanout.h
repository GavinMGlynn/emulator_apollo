/* The display, as pixels a host can show.
 *
 * `ap_graphics_scanout` produces what the hardware produces: one **palette
 * index** per pixel. That is the right thing to keep in a screenshot, because
 * an index plus its lookup table can distinguish a drawing fault from a palette
 * fault, and a flattened image cannot -- `ap_png.h` says so at length and
 * writes indexed PNGs for exactly that reason.
 *
 * A window is the other case. SDL wants colours, and the conversion from index
 * to colour is the same work the screenshot path already does: the Bt458's
 * table for the 8-plane board, ink-on-paper for a 1-plane screen, and an even
 * ramp for the 4-plane board whose sixteen-entry table this core does not model.
 * Doing it twice in two frontends would let them drift, and the one that drifts
 * is the interactive one nobody diffs against a golden.
 *
 * So the index-to-colour step lives here, shared, and each frontend keeps only
 * what is genuinely its own: the file format, or the texture upload. */

#ifndef APOLLO_FRONTEND_AP_SCANOUT_H
#define APOLLO_FRONTEND_AP_SCANOUT_H

#include <stdbool.h>
#include <stdint.h>

#include "board/ap_graphics.h"

typedef struct {
  uint8_t rgb[256][3];
  unsigned colours;
  /* False when the entries are an even grey ramp standing in for a lookup
   * table this core does not model -- the 4-plane board's. A frontend that
   * shows colours to a person should say so; one writing a golden must not
   * pass the ramp off as what the monitor displayed. */
  bool real;
} ap_scanout_palette_t;

/* The palette behind the indices, by screen kind. False when no screen is
 * fitted. */
[[nodiscard]] bool ap_scanout_palette(const ap_graphics_t *graphics,
                                      ap_scanout_palette_t *out);

/* Scan out and flatten to 32-bit `0xAARRGGBB`, host byte order, one word per
 * pixel. `capacity` is in pixels. Returns false when no screen is fitted, the
 * buffer is too small, or the scanout refuses.
 *
 * The alpha byte is `0xFF` throughout: this machine has no notion of
 * transparency and a window that inherited whatever the buffer held would show
 * it. */
[[nodiscard]] bool ap_scanout_rgba(const ap_graphics_t *graphics, uint8_t cr1,
                                   uint32_t *out, uint32_t capacity,
                                   uint32_t *width, uint32_t *height);

#endif /* APOLLO_FRONTEND_AP_SCANOUT_H */
