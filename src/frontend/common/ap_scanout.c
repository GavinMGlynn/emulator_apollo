#include "ap_scanout.h"

#include <stdlib.h>

#include "device/ap_bt458.h"

bool ap_scanout_palette(const ap_graphics_t *graphics,
                        ap_scanout_palette_t *out) {
  ap_graphics_geometry_t geometry;
  if (graphics == NULL || out == NULL ||
      !ap_graphics_geometry(graphics->screen, &geometry)) {
    return false;
  }

  out->colours = 1u << geometry.planes;
  out->real = false;

  if (geometry.planes == 1u) {
    /* Ink, not light: a set bit is black. That is the whole colour model for a
     * monochrome screen and it is exact. */
    out->rgb[0][0] = out->rgb[0][1] = out->rgb[0][2] = 0xFFu;
    out->rgb[1][0] = out->rgb[1][1] = out->rgb[1][2] = 0x00u;
    out->real = true;
    return true;
  }

  if (graphics->screen == AP_SCREEN_COLOUR_8_PLANE) {
    /* The Bt458's own, as the firmware loaded it. */
    out->real = true;
    for (unsigned i = 0; i < out->colours; i++) {
      uint8_t rgb[3] = {0u, 0u, 0u};
      (void)ap_bt458_palette(&graphics->lut, i, rgb);
      out->rgb[i][0] = rgb[0];
      out->rgb[i][1] = rgb[1];
      out->rgb[i][2] = rgb[2];
    }
    return true;
  }

  /* A 4-plane board's lookup table is sixteen entries written through three
   * registers of the controller's own, and is not modelled. An even ramp,
   * flagged as not real rather than passed off as colours. */
  for (unsigned i = 0; i < out->colours; i++) {
    const uint8_t level = (uint8_t)(i * 255u / (out->colours - 1u));
    out->rgb[i][0] = out->rgb[i][1] = out->rgb[i][2] = level;
  }
  return true;
}

bool ap_scanout_rgba(const ap_graphics_t *graphics, uint8_t cr1, uint32_t *out,
                     uint32_t capacity, uint32_t *width, uint32_t *height) {
  ap_graphics_geometry_t geometry;
  if (graphics == NULL || out == NULL ||
      !ap_graphics_geometry(graphics->screen, &geometry)) {
    return false;
  }

  const uint32_t pixels = (uint32_t)geometry.width * geometry.height;
  if (capacity < pixels) {
    return false;
  }

  ap_scanout_palette_t palette;
  if (!ap_scanout_palette(graphics, &palette)) {
    return false;
  }

  /* The indices are scanned into the *tail* of the caller's buffer and expanded
   * in place, so a window costs one allocation rather than two.
   *
   * **Ascending, and the direction is the whole correctness argument.** Index
   * `i` sits at byte `3p + i` and its colour is written to bytes `[4i, 4i+4)`.
   * Going upwards, a write can only reach a byte a later read needs when
   * `4i + 4 > 3p + i + 1`, that is `i > p - 1` -- which is past the last
   * iteration, so it never happens. Going *downwards* it does: the very first
   * step writes `[4p-4, 4p)`, which is inside the index region and destroys
   * indices `p-4` through `p-2` before they are read. */
  uint8_t *indices = (uint8_t *)out + (size_t)pixels * 3u;
  if (ap_graphics_scanout(graphics, cr1, indices, pixels) != pixels) {
    return false;
  }

  for (uint32_t i = 0; i < pixels; i++) {
    const uint8_t index = indices[i];
    /* An index with no entry behind it means the scanout and the table
     * disagree about the plane count, which `ap_png.h` treats as an error.
     * A window cannot refuse a frame usefully, so it shows it as black and
     * stays honest about the geometry rather than reading past the palette. */
    const uint8_t r = index < palette.colours ? palette.rgb[index][0] : 0u;
    const uint8_t g = index < palette.colours ? palette.rgb[index][1] : 0u;
    const uint8_t b = index < palette.colours ? palette.rgb[index][2] : 0u;
    out[i] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  }

  if (width != NULL) {
    *width = geometry.width;
  }
  if (height != NULL) {
    *height = geometry.height;
  }
  return true;
}
