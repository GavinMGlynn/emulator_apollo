#include "ap_png.h"

#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>

#ifdef APOLLO_HAVE_PNG
#include <png.h>
#endif

const char *ap_png_status_name(ap_png_status_t status) {
  switch (status) {
    case AP_PNG_OK: return "ok";
    case AP_PNG_UNSUPPORTED: return "built without libpng";
    case AP_PNG_BAD_ARGUMENT: return "bad argument";
    case AP_PNG_CANNOT_OPEN: return "cannot open";
    case AP_PNG_WRITE_FAILED: return "write failed";
    default: return "unknown";
  }
}

bool ap_png_available(void) {
#ifdef APOLLO_HAVE_PNG
  return true;
#else
  return false;
#endif
}

ap_png_status_t ap_png_write_indexed(const char *path, const uint8_t *pixels,
                                     uint32_t width, uint32_t height,
                                     const uint8_t (*palette)[3],
                                     unsigned palette_entries) {
  if (path == NULL || pixels == NULL || palette == NULL || width == 0u ||
      height == 0u || palette_entries == 0u || palette_entries > 256u) {
    return AP_PNG_BAD_ARGUMENT;
  }
  /* Every index must have a colour behind it. A missing entry means the
   * scanout and the lookup table disagree about how many colours the screen
   * has, and painting it black would hide exactly that. */
  for (uint32_t i = 0; i < width * height; i++) {
    if (pixels[i] >= palette_entries) {
      return AP_PNG_BAD_ARGUMENT;
    }
  }

#ifndef APOLLO_HAVE_PNG
  return AP_PNG_UNSUPPORTED;
#else
  FILE *file = fopen(path, "wb");
  if (file == NULL) {
    return AP_PNG_CANNOT_OPEN;
  }

  png_structp png =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  png_infop info = png == NULL ? NULL : png_create_info_struct(png);
  if (png == NULL || info == NULL) {
    png_destroy_write_struct(png == NULL ? NULL : &png, NULL);
    fclose(file);
    return AP_PNG_WRITE_FAILED;
  }
  /* libpng reports errors by longjmp, so every failure below lands here. */
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    fclose(file);
    return AP_PNG_WRITE_FAILED;
  }

  png_init_io(png, file);
  png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_PALETTE,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);

  png_color entries[256];
  for (unsigned i = 0; i < palette_entries; i++) {
    entries[i].red = palette[i][0];
    entries[i].green = palette[i][1];
    entries[i].blue = palette[i][2];
  }
  png_set_PLTE(png, info, entries, (int)palette_entries);
  png_write_info(png, info);

  for (uint32_t y = 0; y < height; y++) {
    /* The cast drops const only because libpng's row pointer is not const; the
     * row is not written to. */
    png_write_row(png, (png_bytep)(uintptr_t)(pixels + (size_t)y * width));
  }
  png_write_end(png, NULL);
  png_destroy_write_struct(&png, &info);

  if (fclose(file) != 0) {
    return AP_PNG_WRITE_FAILED;
  }
  return AP_PNG_OK;
#endif
}
