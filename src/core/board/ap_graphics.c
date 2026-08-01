#include "board/ap_graphics.h"

void ap_graphics_init(ap_graphics_t *graphics, ap_screen_kind_t screen) {
  graphics->screen = screen;
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

uint8_t ap_graphics_read(const ap_graphics_t *graphics, uint32_t address) {
  bool colour = false;
  uint32_t offset = 0;
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
  /* Accepted and discarded. The board reports the write as answered -- the
   * block is decoded -- and this module has no register semantics to apply.
   * Storing the value would be worse than dropping it: a later read would have
   * to decide what it meant, and there is no answer to that yet. */
  (void)graphics;
  (void)address;
  (void)value;
}
