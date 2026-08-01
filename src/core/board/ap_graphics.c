#include <stddef.h>

#include "board/ap_graphics.h"

void ap_graphics_init(ap_graphics_t *graphics, ap_screen_kind_t screen) {
  graphics->screen = screen;
  graphics->colour_memory = NULL;
  graphics->colour_bytes = 0u;
  graphics->mono_memory = NULL;
  graphics->mono_bytes = 0u;
}

void ap_graphics_attach_memory(ap_graphics_t *graphics, uint8_t *colour,
                               uint32_t colour_bytes, uint8_t *mono,
                               uint32_t mono_bytes) {
  graphics->colour_memory = colour;
  graphics->colour_bytes = colour_bytes;
  graphics->mono_memory = mono;
  graphics->mono_bytes = mono_bytes;
}

/* The storage behind an address, or NULL when there is none: no card of that
 * family fitted, no memory attached, or an offset past what was attached. All
 * three read `FF`, because all three are "nothing is there to answer". */
static uint8_t *memory_at(const ap_graphics_t *graphics, bool colour,
                          uint32_t offset) {
  const bool fitted = colour ? ap_graphics_is_colour(graphics->screen)
                             : ap_graphics_is_monochrome(graphics->screen);
  if (!fitted) {
    return NULL;
  }
  uint8_t *base = colour ? graphics->colour_memory : graphics->mono_memory;
  const uint32_t bytes = colour ? graphics->colour_bytes : graphics->mono_bytes;
  if (base == NULL || offset >= bytes) {
    return NULL;
  }
  return base + offset;
}

bool ap_graphics_is_colour(ap_screen_kind_t screen) {
  return screen == AP_SCREEN_COLOUR_4_PLANE ||
         screen == AP_SCREEN_COLOUR_8_PLANE;
}

bool ap_graphics_is_monochrome(ap_screen_kind_t screen) {
  return screen == AP_SCREEN_MONO_19_INCH || screen == AP_SCREEN_MONO_15_INCH;
}

bool ap_graphics_decode(uint32_t address, bool *colour, uint32_t *offset) {
  if (address >= AP_GRAPHICS_MONO_ADDR &&
      address < AP_GRAPHICS_MONO_ADDR + AP_GRAPHICS_RANGE) {
    *colour = false;
    *offset = address - AP_GRAPHICS_MONO_ADDR;
    return true;
  }
  if (address >= AP_GRAPHICS_COLOUR_ADDR &&
      address < AP_GRAPHICS_COLOUR_ADDR + AP_GRAPHICS_RANGE) {
    *colour = true;
    *offset = address - AP_GRAPHICS_COLOUR_ADDR;
    return true;
  }
  return false;
}

bool ap_graphics_decode_memory(uint32_t address, bool *colour,
                               uint32_t *offset) {
  if (address >= AP_GRAPHICS_COLOUR_MEMORY_ADDR &&
      address <= AP_GRAPHICS_COLOUR_MEMORY_END) {
    *colour = true;
    *offset = address - AP_GRAPHICS_COLOUR_MEMORY_ADDR;
    return true;
  }
  if (address >= AP_GRAPHICS_MONO_MEMORY_ADDR &&
      address <= AP_GRAPHICS_MONO_MEMORY_END) {
    *colour = false;
    *offset = address - AP_GRAPHICS_MONO_MEMORY_ADDR;
    return true;
  }
  return false;
}

uint8_t ap_graphics_read(const ap_graphics_t *graphics, uint32_t address) {
  bool colour = false;
  uint32_t offset = 0;
  if (ap_graphics_decode_memory(address, &colour, &offset)) {
    /* Storage a fitted card provides. `FF` when there is none -- no card of
     * that family, no memory attached, or past what was attached -- because all
     * three mean nothing is there to answer. */
    const uint8_t *at = memory_at(graphics, colour, offset);
    return at != NULL ? *at : 0xFFu;
  }
  if (!ap_graphics_decode(address, &colour, &offset)) {
    return 0xFFu;
  }
  if (offset != AP_GRAPHICS_DEVICE_ID) {
    /* Every other register in the block is unmodelled. `FF` rather than zero:
     * zero is a value some of these registers can legitimately hold, so a
     * driver reading it would take an unmodelled register for a real one
     * reporting a real state. `FF` is what an absent part reads, which is the
     * truthful thing for a part that is not here. */
    return 0xFFu;
  }
  /* The ID answers only for its own family. A colour screen leaves the
   * monochrome block reading `FF` and vice versa, which is exactly how the
   * firmware tells which controller is fitted: it reads both. */
  const bool matches = colour ? ap_graphics_is_colour(graphics->screen)
                              : ap_graphics_is_monochrome(graphics->screen);
  return matches ? (uint8_t)graphics->screen : 0xFFu;
}

void ap_graphics_write(ap_graphics_t *graphics, uint32_t address,
                       uint8_t value) {
  bool colour = false;
  uint32_t offset = 0;
  if (ap_graphics_decode_memory(address, &colour, &offset)) {
    uint8_t *at = memory_at(graphics, colour, offset);
    if (at != NULL) {
      *at = value;
    }
    /* A write with no memory behind it is discarded, not refused: the region
     * decodes either way, and refusing would be a bus error the board does not
     * raise. */
    return;
  }
  /* Accepted and discarded. The board reports the write as answered -- the
   * block is decoded -- and this module has no register semantics to apply.
   * Storing the value would be worse than dropping it: a later read would have
   * to decide what it meant, and there is no answer to that yet. */
  (void)value;
}

ap_graphics_cr0_mode_t ap_graphics_cr0_mode(uint8_t cr0) {
  return (ap_graphics_cr0_mode_t)(cr0 >> 5);
}

ap_graphics_cr2_access_t ap_graphics_cr2_access(uint8_t cr2) {
  return (ap_graphics_cr2_access_t)(cr2 >> 6);
}

const char *ap_graphics_cr0_mode_name(ap_graphics_cr0_mode_t m) {
  switch (m) {
  case AP_GRAPHICS_CR0_CPU_DEST_BLT: return "CPU destination BLT";
  case AP_GRAPHICS_CR0_ALTERNATING_BLT: return "alternating BLT";
  case AP_GRAPHICS_CR0_VECTOR: return "vector";
  case AP_GRAPHICS_CR0_CPU_SOURCE_BLT: return "CPU source BLT";
  case AP_GRAPHICS_CR0_DOUBLE_ACCESS_BLT: return "double access BLT";
  case AP_GRAPHICS_CR0_UNKNOWN_5: return "unknown (mode 5)";
  case AP_GRAPHICS_CR0_UNKNOWN_6: return "unknown (mode 6)";
  case AP_GRAPHICS_CR0_NORMAL: return "normal";
  }
  return "unknown";
}

const char *ap_graphics_cr2_access_name(ap_graphics_cr2_access_t a) {
  switch (a) {
  case AP_GRAPHICS_CR2_CONSTANT_ACCESS: return "constant";
  case AP_GRAPHICS_CR2_PIXEL_ACCESS: return "pixel";
  case AP_GRAPHICS_CR2_UNKNOWN_2: return "unknown (access 2)";
  case AP_GRAPHICS_CR2_PLANE_ACCESS: return "plane";
  }
  return "unknown";
}
