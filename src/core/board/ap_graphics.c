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
  case AP_GRAPHICS_CR2_SHIFT_ACCESS: return "shift access";
  case AP_GRAPHICS_CR2_PLANE_ACCESS: return "plane";
  }
  return "unknown";
}

unsigned ap_graphics_cr0_shift(uint8_t cr0) {
  return (unsigned)(cr0 & AP_GRAPHICS_CR0_SHIFT_MASK);
}

unsigned ap_graphics_cr2_source_plane(uint8_t cr2, bool eight) {
  /* Three bits on the 8-plane board, two on the 4-plane one. */
  if (eight) {
    return cr2 & AP_GRAPHICS_CR2_S_PLANE_MASK_8;
  }
  return (unsigned)(cr2 & AP_GRAPHICS_CR2_S_PLANE_MASK) >>
         AP_GRAPHICS_CR2_S_PLANE_SHIFT;
}

unsigned ap_graphics_cr2_dest_plane(uint8_t cr2, bool eight) {
  /* A whole byte on the 8-plane board -- it selects a *set* of planes, and
   * eight of them need eight bits -- against four bits on the 4-plane one. */
  return eight ? cr2 : (unsigned)(cr2 & AP_GRAPHICS_CR2_D_PLANE_MASK);
}

ap_graphics_rop_t ap_graphics_rop_for(uint32_t rop_register, unsigned plane) {
  /* Four bits each, low plane first. A plane beyond the eight the register
   * holds selects nothing, and the mask makes that zero -- which is the
   * function that writes zeroes rather than an error the part cannot report. */
  if (plane >= AP_GRAPHICS_ROP_PLANES) {
    return AP_GRAPHICS_ROP_ZERO;
  }
  return (ap_graphics_rop_t)((rop_register >> (plane * 4u)) & 0x0Fu);
}

uint16_t ap_graphics_rop_apply(uint8_t cr1, uint32_t rop_register,
                               unsigned plane, uint16_t source,
                               uint16_t destination) {
  if ((cr1 & AP_GRAPHICS_CR1_ROP_EN) == 0u) {
    /* Not enabled: the source passes through whatever the register holds. A
     * driver that programmed an operation and forgot the enable gets a copy,
     * which is the hardware's behaviour and not a fallback chosen here. */
    return source;
  }
  const uint16_t s = source;
  const uint16_t d = destination;
  switch (ap_graphics_rop_for(rop_register, plane)) {
  case AP_GRAPHICS_ROP_ZERO: return 0u;
  case AP_GRAPHICS_ROP_SRC_AND_DST: return (uint16_t)(s & d);
  case AP_GRAPHICS_ROP_SRC_AND_NOT_DST: return (uint16_t)(s & (uint16_t)~d);
  case AP_GRAPHICS_ROP_SRC: return s;
  case AP_GRAPHICS_ROP_NOT_SRC_AND_DST: return (uint16_t)((uint16_t)~s & d);
  case AP_GRAPHICS_ROP_DST: return d;
  case AP_GRAPHICS_ROP_SRC_XOR_DST: return (uint16_t)(s ^ d);
  case AP_GRAPHICS_ROP_SRC_OR_DST: return (uint16_t)(s | d);
  case AP_GRAPHICS_ROP_SRC_NOR_DST: return (uint16_t)~(uint16_t)(s | d);
  case AP_GRAPHICS_ROP_SRC_XNOR_DST: return (uint16_t)~(uint16_t)(s ^ d);
  case AP_GRAPHICS_ROP_NOT_DST: return (uint16_t)~d;
  case AP_GRAPHICS_ROP_SRC_OR_NOT_DST: return (uint16_t)(s | (uint16_t)~d);
  case AP_GRAPHICS_ROP_NOT_SRC: return (uint16_t)~s;
  case AP_GRAPHICS_ROP_NOT_SRC_OR_DST: return (uint16_t)((uint16_t)~s | d);
  case AP_GRAPHICS_ROP_SRC_NAND_DST: return (uint16_t)~(uint16_t)(s & d);
  case AP_GRAPHICS_ROP_ONE: return 0xFFFFu;
  }
  return s;
}

uint16_t ap_graphics_source_data(uint8_t cr0, ap_graphics_cr2_access_t access,
                                 unsigned plane, uint16_t latched) {
  switch (access) {
  case AP_GRAPHICS_CR2_CONSTANT_ACCESS:
    /* All ones, "used for vectors": a line draw wants a solid source and takes
     * its shape from the addresses it writes, not from the data. */
    return 0xFFFFu;
  case AP_GRAPHICS_CR2_PIXEL_ACCESS:
    /* One bit of the source, replicated across the word -- the bit belonging
     * to this plane. That is how a packed pixel becomes a plane's worth of
     * solid colour. */
    return (latched & (uint16_t)(1u << (plane & 0x0Fu))) != 0u ? 0xFFFFu : 0u;
  case AP_GRAPHICS_CR2_SHIFT_ACCESS:
    /* The shifter's least significant bit, replicated. The same idea as pixel
     * access with the bit chosen by the shift rather than by the plane. */
    return (latched & 1u) != 0u ? 0xFFFFu : 0u;
  case AP_GRAPHICS_CR2_PLANE_ACCESS:
    break;
  }
  /* "Normal use": the word itself, shifted by `CR0`'s count. A count of 16 or
   * more rotates the halves first, so the field reaches across the word rather
   * than shifting everything out of it. */
  {
    uint32_t wide = latched;
    const unsigned shift = ap_graphics_cr0_shift(cr0);
    if (shift >= 16u) {
      wide = (wide << 16) | (wide >> 16);
    }
    return (uint16_t)(wide >> (shift & 0x0Fu));
  }
}

bool ap_graphics_plane_selected(unsigned d_plane, unsigned plane) {
  if (plane >= AP_GRAPHICS_ROP_PLANES) {
    return false;
  }
  /* **Zero selects.** A set bit masks the plane out. */
  return (d_plane & (1u << plane)) == 0u;
}

uint16_t ap_graphics_combine(uint16_t write_enable, uint16_t mem_mask,
                             uint16_t source, uint16_t destination) {
  /* A bit is written when the write enable register has it clear *and* the bus
   * cycle covers it. Both are expressed as "protect", which is why they are
   * OR'd rather than AND'd: either one protecting is enough. */
  const uint16_t protect = (uint16_t)(write_enable | (uint16_t)~mem_mask);
  return (uint16_t)((destination & protect) | (source & (uint16_t)~protect));
}

unsigned ap_graphics_blit(const ap_graphics_blit_t *blit, uint16_t *image,
                          uint32_t words, uint32_t dest, uint16_t mem_mask,
                          const uint16_t *latched) {
  if (blit == nullptr || image == nullptr || latched == nullptr) {
    return 0u;
  }
  unsigned written = 0u;
  uint32_t address = dest;

  for (unsigned plane = 0; plane < blit->planes; plane++) {
    /* The address advances for every plane, written or not: the planes are a
     * fixed layout, not a list of the ones taking part. Advancing only on a
     * write would pack the written planes together and put each after the
     * first in the wrong one. */
    const uint32_t at = address;
    address += blit->plane_stride;

    if (!ap_graphics_plane_selected(blit->d_plane, plane)) {
      continue;
    }
    if (at >= words) {
      /* Past the memory. Skipped rather than wrapped: a blit that ran off the
       * end and reappeared at the top would draw a second, wrong image
       * somewhere a caller never asked about. */
      continue;
    }

    /* One source for all planes when the board has one, or when `AD_BIT` says
     * to broadcast; otherwise each plane reads its own. */
    const bool broadcast =
        blit->planes == 1u || (blit->cr1 & AP_GRAPHICS_CR1_COLOUR_AD_BIT) != 0u;
    const unsigned from = broadcast ? blit->s_plane : plane;
    const uint16_t source = ap_graphics_source_data(
        blit->cr0, blit->access, plane,
        latched[from < blit->planes ? from : 0u]);

    const uint16_t destination = image[at];
    const uint16_t combined = ap_graphics_rop_apply(
        blit->cr1, blit->rop_register, plane, source, destination);
    image[at] =
        ap_graphics_combine(blit->write_enable, mem_mask, combined, destination);
    written++;
  }
  return written;
}

bool ap_graphics_geometry(ap_screen_kind_t kind, ap_graphics_geometry_t *out) {
  if (out == nullptr) {
    return false;
  }
  unsigned planes = 0u, width = 0u, height = 0u;
  unsigned buffer_width = 0u, buffer_height = 0u;

  switch (kind) {
    case AP_SCREEN_COLOUR_4_PLANE:
      /* "512 KB of image memory arranged in four 128-KB planes" -- and 128 KB
       * is exactly 1024 x 1024 bits, so the buffer geometry is the manual's
       * capacity divided out rather than a number taken from elsewhere. */
      planes = 4u; width = 1024u; height = 800u;
      buffer_width = 1024u; buffer_height = 1024u;
      break;
    case AP_SCREEN_COLOUR_8_PLANE:
      /* §1.5.3 states both geometries in one sentence: "each consists of a 1024
       * pixel by 1024 line memory, with a resolution of 1024 pixels x 800
       * lines". */
      planes = 8u; width = 1024u; height = 800u;
      buffer_width = 1024u; buffer_height = 1024u;
      break;
    case AP_SCREEN_MONO_19_INCH:
      /* "256-KB image memory", one plane: 2048 x 1024 bits for a 1280 x 1024
       * screen. The buffer is 768 pixels wider than the display, which is the
       * largest gap of the four and the one a wrong stride shows soonest. */
      planes = 1u; width = 1280u; height = 1024u;
      buffer_width = 2048u; buffer_height = 1024u;
      break;
    case AP_SCREEN_MONO_15_INCH:
      /* The oracle's, not the manual's -- see the header. */
      planes = 1u; width = 1024u; height = 800u;
      buffer_width = 1024u; buffer_height = 1024u;
      break;
    case AP_SCREEN_NONE:
    default:
      return false;
  }

  out->planes = planes;
  out->width = width;
  out->height = height;
  out->buffer_width = buffer_width;
  out->buffer_height = buffer_height;
  out->plane_words = (uint32_t)buffer_width * buffer_height / 16u;
  return true;
}

bool ap_graphics_display_enabled(uint8_t cr1) {
  return (cr1 & AP_GRAPHICS_CR1_DISP_EN) != 0u;
}

/* One 16-bit word of image memory, as the 68030 wrote it: high byte first. */
static uint16_t image_word(const uint8_t *memory, uint32_t word_index) {
  const uint32_t at = word_index * 2u;
  return (uint16_t)(((uint16_t)memory[at] << 8) | memory[at + 1u]);
}

uint32_t ap_graphics_scanout(const ap_graphics_t *graphics, uint8_t cr1,
                             uint8_t *pixels, uint32_t capacity) {
  if (graphics == nullptr || pixels == nullptr) {
    return 0u;
  }
  ap_graphics_geometry_t geometry;
  if (!ap_graphics_geometry(graphics->screen, &geometry)) {
    return 0u;
  }

  const bool colour = ap_graphics_is_colour(graphics->screen);
  const uint8_t *memory = colour ? graphics->colour_memory
                                 : graphics->mono_memory;
  const uint32_t bytes = colour ? graphics->colour_bytes
                                : graphics->mono_bytes;
  if (memory == nullptr) {
    return 0u;
  }

  /* The whole of every plane must be there. A card with a short memory is not
   * a card showing part of a picture -- it is a configuration this core cannot
   * scan out, and reading past the buffer to find out would be worse than
   * saying so. */
  const uint32_t needed_words = geometry.plane_words * geometry.planes;
  if (bytes / 2u < needed_words) {
    return 0u;
  }

  const uint32_t produced = (uint32_t)geometry.width * geometry.height;
  if (capacity < produced) {
    return 0u;
  }

  /* `INV` is a *monochrome* bit; on a colour controller the same position is
   * `AD_BIT` and inverting on it would blank a colour screen whenever the
   * blitter had been told to broadcast. */
  const uint16_t invert =
      (!colour && (cr1 & AP_GRAPHICS_CR1_MONO_INV) != 0u) ? 0xFFFFu : 0x0000u;

  const uint32_t line_words = geometry.buffer_width / 16u;
  const uint32_t visible_words = geometry.width / 16u;
  uint32_t out = 0u;

  for (unsigned y = 0; y < geometry.height; y++) {
    const uint32_t row = (uint32_t)y * line_words;
    for (uint32_t xw = 0; xw < visible_words; xw++) {
      uint16_t plane_data[8];
      for (unsigned p = 0; p < geometry.planes; p++) {
        plane_data[p] = (uint16_t)(
            image_word(memory, p * geometry.plane_words + row + xw) ^ invert);
      }
      /* Bit 15 first: the high bit of a word is the *leftmost* pixel. */
      for (int bit = 15; bit >= 0; bit--) {
        unsigned index = 0u;
        for (unsigned p = 0; p < geometry.planes; p++) {
          index |= (unsigned)((plane_data[p] >> (unsigned)bit) & 1u) << p;
        }
        pixels[out++] = (uint8_t)index;
      }
    }
  }
  return out;
}
