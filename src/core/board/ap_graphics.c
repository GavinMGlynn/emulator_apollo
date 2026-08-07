#include <stddef.h>

#include "board/ap_graphics.h"

void ap_graphics_init(ap_graphics_t *graphics, ap_screen_kind_t screen) {
  graphics->screen = screen;
  /* Every register zero at reset, which is not a neutral choice: `CR1`'s
   * `DISP_EN` is bit 0, so a controller that has not been programmed has its
   * display *off*. That is what the hardware does and it is why a screenshot
   * taken before the firmware programs anything reports the bit clear rather
   * than showing a picture nothing asked for. */
  graphics->reg = (ap_graphics_registers_t){0};
  ap_bt458_reset(&graphics->lut);
  /* Every chip select **deasserted**, which is all ones -- not zero. A control
   * register cleared at reset would leave the A/D selected and the first
   * data-port write would go to a converter this core does not have. */
  graphics->lut_control = 0xFFu;
  graphics->lut_data = 0u;
  graphics->lut_fifo_head = 0u;
  graphics->lut_fifo_count = 0u;
  graphics->lut_fifo_overruns = 0u;
  graphics->lut_ad_accesses = 0u;
  graphics->diag_refresh_request = 0u;
  graphics->diag_refresh_requests = 0u;
  graphics->blt_cycle = 0u;
  graphics->now = 0u;
  /* The stepped counters, which are state like any other and were left out of
   * this on the first pass -- so a caller with a stack-allocated controller got
   * a beam wound to wherever the stack happened to point. */
  graphics->h_clock = 0u;
  graphics->v_clock = 0u;
  graphics->p_clock = 0u;
  for (unsigned i = 0; i < AP_GRAPHICS_MAX_PLANES; i++) {
    graphics->guard_latch[i] = 0u;
  }
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

/* One 16-bit word of image memory, as the 68030 wrote it: high byte first.
 * Declared here because both the blitter and the scanout read through it --
 * which is the point of them sharing one buffer. */
static uint16_t image_word(const uint8_t *memory, uint32_t word_index);

/* `CR3A` and `CR3B` are not values but **bit ports**: with bit 7 clear, bits
 * 3-1 name a bit of the target register and bit 0 is the value to put there.
 * That is how a driver flips one control bit -- `DISP_EN`, say -- without a
 * read-modify-write on a register it may not be able to read.
 *
 * The bit number is `(value & 0x0F) >> 1`, so bit 0 of the port is the *data*
 * and the number is one place up. Reading the low nibble as the number instead
 * addresses the wrong bit and, worse, does so consistently -- every set lands
 * two bits away and the register still changes, so it looks like it works. */
static void apply_bit_port(uint8_t *target, uint8_t value) {
  if ((value & 0x80u) != 0u) {
    return;
  }
  const unsigned bit = (unsigned)(value & 0x0Fu) >> 1;
  const uint8_t mask = (uint8_t)(1u << bit);
  if ((value & 0x01u) != 0u) {
    *target = (uint8_t)(*target | mask);
  } else {
    *target = (uint8_t)(*target & (uint8_t)~mask);
  }
}

/* The FIFO between the data port and the part. Bytes go in while `FIFO_CS` is
 * asserted and are drained when `CPAL_CS` is *released*, so a whole palette is
 * committed at once rather than a byte at a time. */
static void lut_fifo_put(ap_graphics_t *graphics, uint8_t value) {
  if (graphics->lut_fifo_count >= AP_GRAPHICS_LUT_FIFO_BYTES) {
    /* The depth is the oracle's and no manual gives one, so the byte is dropped
     * *and counted* rather than silently discarded or allowed to wrap over data
     * the driver has not committed yet. */
    graphics->lut_fifo_overruns++;
    return;
  }
  const unsigned at =
      (graphics->lut_fifo_head + graphics->lut_fifo_count) %
      AP_GRAPHICS_LUT_FIFO_BYTES;
  graphics->lut_fifo[at] = value;
  graphics->lut_fifo_count++;
}

static uint8_t lut_fifo_get(ap_graphics_t *graphics) {
  if (graphics->lut_fifo_count == 0u) {
    return 0u;
  }
  const uint8_t value = graphics->lut_fifo[graphics->lut_fifo_head];
  graphics->lut_fifo_head =
      (graphics->lut_fifo_head + 1u) % AP_GRAPHICS_LUT_FIFO_BYTES;
  graphics->lut_fifo_count--;
  return value;
}

static void lut_control_write(ap_graphics_t *graphics, uint8_t value) {
  const uint8_t changed = (uint8_t)(graphics->lut_control ^ value);
  graphics->lut_control = value;

  /* `CPAL_CS` **released** -- the transition, not the level -- commits the
   * buffered palette. A model watching the level would drain on every write
   * that left the select high, including the ones that never asserted it. */
  if ((changed & AP_GRAPHICS_LUT_CPAL_CS) != 0u &&
      (value & AP_GRAPHICS_LUT_CPAL_CS) != 0u) {
    while (graphics->lut_fifo_count > 0u) {
      ap_bt458_write(&graphics->lut,
                     (ap_bt458_select_t)(graphics->lut_control &
                                         AP_GRAPHICS_LUT_C1_C0),
                     lut_fifo_get(graphics));
    }
  }
  /* `FIFO_RST` is active low, so the reset is the falling edge. */
  if ((changed & AP_GRAPHICS_LUT_FIFO_RST) != 0u &&
      (value & AP_GRAPHICS_LUT_FIFO_RST) == 0u) {
    graphics->lut_fifo_head = 0u;
    graphics->lut_fifo_count = 0u;
  }
}

static void lut_data_write(ap_graphics_t *graphics, uint8_t value) {
  graphics->lut_data = value;
  const uint8_t control = graphics->lut_control;

  /* Active low throughout, and tried in this order. A write does *not* try the
   * FIFO first -- see the header; the read does. */
  if ((control & AP_GRAPHICS_LUT_AD_CS) == 0u) {
    /* A write here *selects the channel* -- the byte says which gun and what
     * kind of conversion -- and the result is read back through the same port.
     * Counted as well, so a run still says how much of this the firmware did. */
    graphics->lut_ad_accesses++;
    return;
  }
  if ((control & AP_GRAPHICS_LUT_CPAL_CS) == 0u) {
    ap_bt458_write(&graphics->lut,
                   (ap_bt458_select_t)(control & AP_GRAPHICS_LUT_C1_C0), value);
    return;
  }
  if ((control & AP_GRAPHICS_LUT_FIFO_CS) == 0u) {
    lut_fifo_put(graphics, value);
    return;
  }
  /* No select asserted: the byte reaches nothing, and the data register keeps
   * it. */
}

static uint8_t lut_data_read(ap_graphics_t *graphics) {
  const uint8_t control = graphics->lut_control;
  if ((control & AP_GRAPHICS_LUT_FIFO_CS) == 0u) {
    return lut_fifo_get(graphics);
  }
  if ((control & AP_GRAPHICS_LUT_R_W) == 0u) {
    /* The direction bit says write. Reading here is a driver's mistake and the
     * part answers with the data register rather than driving the bus. */
    return graphics->lut_data;
  }
  if ((control & AP_GRAPHICS_LUT_AD_CS) == 0u) {
    /* The A/D's result. The channel is whatever was last written to the data
     * port, which is how the converter is told what to measure. */
    graphics->lut_ad_accesses++;
    uint8_t level = 0u;
    if (ap_graphics_adc(graphics, graphics->lut_data, &level)) {
      return level;
    }
    return graphics->lut_data;
  }
  if ((control & AP_GRAPHICS_LUT_CPAL_CS) == 0u) {
    return ap_bt458_read(&graphics->lut,
                         (ap_bt458_select_t)(control & AP_GRAPHICS_LUT_C1_C0));
  }
  return graphics->lut_data;
}

/* The status register, which is the raster. See the header for the bit map and
 * for why only the vertical part free-runs. */
static uint8_t graphics_status(const ap_graphics_t *graphics) {
  /* The oracle drives the blanking bits only when `CR1` has **both** `RESET`
   * and `SYNC_EN`. Until the firmware has released the reset and enabled sync
   * there is no beam to report, and answering as though there were would let a
   * driver believe a display it has not started yet. */
  const uint8_t needed = AP_GRAPHICS_CR1_RESET | AP_GRAPHICS_CR1_SYNC_EN;
  if ((graphics->reg.cr1 & needed) != needed) {
    /* **Held in reset is not silent.** The register holds a defined value with
     * several bits set, and the boot PROM depends on it: at `007026` it does a
     * bare `btst #2` -- the sync bit -- and branches to read the device ID if it
     * is set. That is a *display present* probe, made before the controller has
     * been programmed at all, so a model answering zero here reports no display
     * on a machine that has one.
     *
     * The values are the oracle's, per family, and they are not the same. A
     * 15-inch monochrome answers with two bits where the others answer with
     * four. */
    switch (graphics->screen) {
      case AP_SCREEN_MONO_15_INCH:
        return AP_GRAPHICS_SR_V_BLANK | AP_GRAPHICS_SR_V_SYNC;
      case AP_SCREEN_MONO_19_INCH:
        return AP_GRAPHICS_SR_H_CK | AP_GRAPHICS_SR_V_BLANK |
               AP_GRAPHICS_SR_H_SYNC | AP_GRAPHICS_SR_V_SYNC;
      case AP_SCREEN_COLOUR_4_PLANE:
      case AP_SCREEN_COLOUR_8_PLANE:
        /* `SR_H_SYNC`'s bit is `DONE` on a colour board, and it is set here. */
        return AP_GRAPHICS_SR_H_CK | AP_GRAPHICS_SR_V_BLANK |
               AP_GRAPHICS_SR_V_SYNC | AP_GRAPHICS_SR_H_SYNC;
      case AP_SCREEN_NONE:
      default:
        return 0x00u;
    }
  }

  unsigned line = 0u, pixel = 0u;
  if (!ap_graphics_beam(graphics, &line, &pixel)) {
    return 0x00u;
  }
  ap_graphics_geometry_t geometry;
  if (!ap_graphics_geometry(graphics->screen, &geometry)) {
    return 0x00u;
  }

  uint8_t sr = 0u;
  /* **`BLANK` and `V_BLANK` are active low**, like most of this board. They are
   * *set* while the beam is drawing and *cleared* while it is blanked, which is
   * the opposite of the reading their names invite and is what both of the
   * oracle's paths do -- `increment_v_clock` clears `V_BLANK` at the line
   * blanking begins and sets it again at line 0, and the vblank callback clears
   * both on entering the interval.
   *
   * This was implemented the obvious way round first, with a comment arguing
   * for it. Getting it backwards is not subtle in effect: a driver waiting for
   * the blanking interval to update the screen would see one permanently and
   * update whenever it liked. */
  const bool v_blank = line >= geometry.height;
  const bool h_blank = pixel >= geometry.width;
  if (!v_blank) {
    sr |= AP_GRAPHICS_SR_V_BLANK;
  }
  if (!v_blank && !h_blank) {
    sr |= AP_GRAPHICS_SR_BLANK;
  }
  /* The **vertical sync pulse**, also active low, and the bit the boot PROM
   * waits on at `007026` before it will believe there is a display -- a bounded
   * `dbra` loop that falls through to the no-display path when it times out.
   *
   * Four lines into the blanking interval and four lines long. That is the
   * oracle's structure for every family, and the two it gives outright agree:
   * an 8-plane board blanks at line 800 and syncs 804 to 808, and a 19-inch
   * blanks at 1023 and syncs 1028 to 1032 -- `height + 4` to `height + 8` in
   * both. */
  const unsigned v_sync_start = geometry.height + 4u;
  const unsigned v_sync_end = geometry.height + 8u;
  if (!(line >= v_sync_start && line < v_sync_end)) {
    sr |= AP_GRAPHICS_SR_V_SYNC;
  }
  /* `H_CK` is the horizontal clock, and it toggles once a line -- the lowest
   * bit of the line number, so a driver watching it sees a square wave at half
   * the line rate rather than a pulse it could miss between two reads. */
  if ((line & 1u) != 0u) {
    sr |= AP_GRAPHICS_SR_H_CK;
  }
  /* The sync pulse sits inside the blanking interval, after the front porch.
   * `008778-03` Table 11-8 gives the monochrome monitor's porches directly --
   * horizontal front 407 ns, sync 1.49 us -- and the colour table does not, so
   * this is the fraction of the blanking those figures describe rather than a
   * per-screen measurement. **Active low**, as the oracle has it: the bit is
   * *cleared* while the pulse is asserted. */
  const unsigned blank_pixels = geometry.h_total - geometry.width;
  const unsigned sync_start = geometry.width + blank_pixels / 8u;
  const unsigned sync_end = sync_start + blank_pixels / 2u;
  const bool h_sync = pixel >= sync_start && pixel < sync_end;
  if (!h_sync) {
    sr |= AP_GRAPHICS_SR_H_SYNC;
  }
  return sr;
}

uint8_t ap_graphics_read(ap_graphics_t *graphics, uint32_t address) {
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
  /* The ID answers only for its own family. A colour screen leaves the
   * monochrome block reading `FF` and vice versa, which is exactly how the
   * firmware tells which controller is fitted: it reads both. */
  const bool matches = colour ? ap_graphics_is_colour(graphics->screen)
                              : ap_graphics_is_monochrome(graphics->screen);
  if (offset == AP_GRAPHICS_DEVICE_ID) {
    return matches ? (uint8_t)graphics->screen : 0xFFu;
  }
  if (!matches) {
    /* The other family's block, or no screen at all. It decodes -- both blocks
     * always do -- and holds nothing. */
    return 0xFFu;
  }

  const bool eight = graphics->screen == AP_SCREEN_COLOUR_8_PLANE;
  switch (offset & AP_GRAPHICS_REGISTER_MASK) {
    case AP_GRAPHICS_REG_CR0: return graphics->reg.cr0;
    case AP_GRAPHICS_REG_CR1: return graphics->reg.cr1;
    case AP_GRAPHICS_REG_CR2: return graphics->reg.cr2;
    case AP_GRAPHICS_REG_CR3A: return graphics->reg.cr3a;
    /* The raster operation reads back only on the 8-plane board, and only its
     * high half -- the low half is write-only there and everywhere. */
    case AP_GRAPHICS_REG_ROP_31_24:
      return eight ? (uint8_t)(graphics->reg.rop >> 24) : 0xFFu;
    case AP_GRAPHICS_REG_ROP_23_16:
      return eight ? (uint8_t)(graphics->reg.rop >> 16) : 0xFFu;
    case AP_GRAPHICS_REG_CR2B: return eight ? graphics->reg.cr2b : 0xFFu;
    case AP_GRAPHICS_REG_LUT_DATA:
      return eight ? lut_data_read(graphics) : 0xFFu;
    case AP_GRAPHICS_REG_LUT_CONTROL:
      return eight ? graphics->lut_control : 0xFFu;
    case AP_GRAPHICS_REG_CR3B: return eight ? graphics->reg.cr3b : 0xFFu;
    case AP_GRAPHICS_REG_STATUS:
      return graphics_status(graphics);
    default:
      /* Every other register in the low group is write-only or unmodelled.
       * `FF` rather than zero, for the reason the header gives -- zero is a
       * state a real register can report and these cannot report anything. */
      return 0xFFu;
  }
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
  if (!ap_graphics_decode(address, &colour, &offset)) {
    return;
  }
  const bool matches = colour ? ap_graphics_is_colour(graphics->screen)
                              : ap_graphics_is_monochrome(graphics->screen);
  if (!matches) {
    /* Decoded and discarded: the block answers whether or not a card of that
     * family is behind it, and a write with nothing behind it terminates
     * normally rather than faulting. */
    return;
  }

  const bool eight = graphics->screen == AP_SCREEN_COLOUR_8_PLANE;
  const uint32_t reg = offset & AP_GRAPHICS_REGISTER_MASK;
  switch (reg) {
    /* The two scrambled multi-byte registers. Each pair is **high byte
     * first**, and the ROP's pairs run low half before high half -- see the
     * header. Assembling either in address order is the mistake, and for the
     * ROP it gives every plane its neighbour's function. */
    case AP_GRAPHICS_REG_WRITE_ENABLE_HI:
      graphics->reg.write_enable =
          (uint16_t)((graphics->reg.write_enable & 0x00FFu) |
                     (uint16_t)((uint16_t)value << 8));
      return;
    case AP_GRAPHICS_REG_WRITE_ENABLE_LO:
      graphics->reg.write_enable =
          (uint16_t)((graphics->reg.write_enable & 0xFF00u) | value);
      return;
    case AP_GRAPHICS_REG_ROP_15_8:
      graphics->reg.rop = (graphics->reg.rop & 0xFFFF00FFu) |
                          ((uint32_t)value << 8);
      return;
    case AP_GRAPHICS_REG_ROP_7_0:
      graphics->reg.rop = (graphics->reg.rop & 0xFFFFFF00u) | value;
      return;
    case AP_GRAPHICS_REG_ROP_31_24:
      /* Offsets 4 and 5 are the ROP's high half on an 8-plane board and a
       * diagnostic memory-refresh trigger on the others -- the same per-family
       * split `CR1`'s top bits have. On those boards the write must not reach
       * the ROP; it is *recorded* rather than dropped, so a driver's request is
       * something a report and a test can see. */
      if (eight) {
        graphics->reg.rop = (graphics->reg.rop & 0x00FFFFFFu) |
                            ((uint32_t)value << 24);
      } else {
        graphics->diag_refresh_request = value;
        graphics->diag_refresh_requests++;
      }
      return;
    case AP_GRAPHICS_REG_ROP_23_16:
      if (eight) {
        graphics->reg.rop = (graphics->reg.rop & 0xFF00FFFFu) |
                            ((uint32_t)value << 16);
      } else {
        graphics->diag_refresh_request = value;
        graphics->diag_refresh_requests++;
      }
      return;

    case AP_GRAPHICS_REG_CR0: graphics->reg.cr0 = value; return;
    case AP_GRAPHICS_REG_CR1: {
      /* Each clock-step bit advances its counter on the **falling edge**, not
       * on its level: the diagnostic pulses the bit and a model watching the
       * level would step once and then stop, or step for ever. And `DV_CK` does
       * not exist on a single-plane board, where the same bit is `DADDR_16` --
       * stepping there would wind the vertical counter every time a monochrome
       * driver set an address bit. */
      const uint8_t before = graphics->reg.cr1;
      const uint8_t changed = (uint8_t)(before ^ value);
      graphics->reg.cr1 = value;

      const bool fell_dp = (changed & AP_GRAPHICS_CR1_DP_CK) != 0u &&
                           (value & AP_GRAPHICS_CR1_DP_CK) == 0u;
      const bool fell_dh = (changed & AP_GRAPHICS_CR1_DH_CK) != 0u &&
                           (value & AP_GRAPHICS_CR1_DH_CK) == 0u;
      const bool fell_dv = colour &&
                           (changed & AP_GRAPHICS_CR1_COLOUR_DV_CK) != 0u &&
                           (value & AP_GRAPHICS_CR1_COLOUR_DV_CK) == 0u;

      ap_graphics_geometry_t geometry;
      const bool fitted = ap_graphics_geometry(graphics->screen, &geometry);

      /* `RESET` going low zeroes them, with the guard latch -- the controller
       * is being restarted and the beam is back at the top left. */
      if ((changed & AP_GRAPHICS_CR1_RESET) != 0u &&
          (value & AP_GRAPHICS_CR1_RESET) == 0u) {
        graphics->h_clock = 0u;
        graphics->v_clock = 0u;
        graphics->p_clock = 0u;
        for (unsigned i = 0; i < AP_GRAPHICS_MAX_PLANES; i++) {
          graphics->guard_latch[i] = 0u;
        }
        return;
      }
      if (!fitted) {
        return;
      }
      if (fell_dp) {
        graphics->p_clock++;
      }
      if (fell_dh) {
        /* The horizontal counter carries into the vertical at the line's sync
         * point, which is what makes a run of horizontal steps walk down the
         * screen rather than round one line for ever. */
        graphics->h_clock++;
        if (graphics->h_clock >= geometry.h_total / 16u) {
          graphics->h_clock = 0u;
          graphics->v_clock++;
        }
      }
      if (fell_dv) {
        graphics->v_clock++;
      }
      if (graphics->v_clock >= geometry.v_total) {
        graphics->v_clock = 0u;
      }
      return;
    }
    case AP_GRAPHICS_REG_CR2: graphics->reg.cr2 = value; return;
    case AP_GRAPHICS_REG_CR2B:
      if (eight) { graphics->reg.cr2b = value; }
      return;
    case AP_GRAPHICS_REG_LUT_DATA:
      if (eight) { lut_data_write(graphics, value); }
      return;
    case AP_GRAPHICS_REG_LUT_CONTROL:
      if (eight) { lut_control_write(graphics, value); }
      return;
    case AP_GRAPHICS_REG_CR3A:
      graphics->reg.cr3a = value;
      apply_bit_port(&graphics->reg.cr1, value);
      return;
    case AP_GRAPHICS_REG_CR3B:
      /* `CR3B` is the lookup table's control port and does the same job for it
       * that `CR3A` does for `CR1`. The LUT is not wired to this board, so the
       * register stores and the bit operation has nothing to apply to -- which
       * is recorded rather than pretended, and is why `cr3b` is storage here
       * and `cr3a` is not only storage. */
      if (eight) { graphics->reg.cr3b = value; }
      return;
    default:
      /* Offset 0 and 1 as *writes* are the write enable register above; every
       * other offset in the block is unmodelled and absorbed. */
      return;
  }
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
                                 unsigned plane, uint32_t latched) {
  switch (access) {
  case AP_GRAPHICS_CR2_CONSTANT_ACCESS:
    /* All ones, "used for vectors": a line draw wants a solid source and takes
     * its shape from the addresses it writes, not from the data. */
    return 0xFFFFu;
  case AP_GRAPHICS_CR2_PIXEL_ACCESS:
    /* One bit of the source, replicated across the word -- the bit belonging
     * to this plane. That is how a packed pixel becomes a plane's worth of
     * solid colour. */
    return (latched & (1u << (plane & 0x1Fu))) != 0u ? 0xFFFFu : 0u;
  case AP_GRAPHICS_CR2_SHIFT_ACCESS:
    /* The shifter's least significant bit, replicated. The same idea as pixel
     * access with the bit chosen by the shift rather than by the plane. */
    return (latched & 1u) != 0u ? 0xFFFFu : 0u;
  case AP_GRAPHICS_CR2_PLANE_ACCESS:
    break;
  }
  /* "Normal use": the word itself, shifted by `CR0`'s count -- across the whole
   * thirty-two bit latch, so the bits shifted in are the *previous* word's. A
   * count of 16 or more rotates the halves first, so the field reaches across
   * the pair rather than shifting everything out of it. */
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

unsigned ap_graphics_blit(const ap_graphics_blit_t *blit, uint8_t *image,
                          uint32_t bytes, uint32_t dest, uint16_t mem_mask,
                          const uint32_t *latched) {
  const uint32_t words = bytes / 2u;
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

    const uint16_t destination = image_word(image, at);
    const uint16_t combined = ap_graphics_rop_apply(
        blit->cr1, blit->rop_register, plane, source, destination);
    const uint16_t result =
        ap_graphics_combine(blit->write_enable, mem_mask, combined, destination);
    image[at * 2u] = (uint8_t)(result >> 8);
    image[at * 2u + 1u] = (uint8_t)result;
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
  uint32_t dot_clock = 0u;
  unsigned h_total = 0u, v_total = 0u;

  switch (kind) {
    case AP_SCREEN_COLOUR_4_PLANE:
      /* "512 KB of image memory arranged in four 128-KB planes" -- and 128 KB
       * is exactly 1024 x 1024 bits, so the buffer geometry is the manual's
       * capacity divided out rather than a number taken from elsewhere. */
      planes = 4u; width = 1024u; height = 800u;
      buffer_width = 1024u; buffer_height = 1024u;
      dot_clock = 68000000u; h_total = 1346u; v_total = 841u;
      break;
    case AP_SCREEN_COLOUR_8_PLANE:
      /* §1.5.3 states both geometries in one sentence: "each consists of a 1024
       * pixel by 1024 line memory, with a resolution of 1024 pixels x 800
       * lines". */
      planes = 8u; width = 1024u; height = 800u;
      buffer_width = 1024u; buffer_height = 1024u;
      dot_clock = 68000000u; h_total = 1346u; v_total = 841u;
      break;
    case AP_SCREEN_MONO_19_INCH:
      /* "256-KB image memory", one plane: 2048 x 1024 bits for a 1280 x 1024
       * screen. The buffer is 768 pixels wider than the display, which is the
       * largest gap of the four and the one a wrong stride shows soonest. */
      planes = 1u; width = 1280u; height = 1024u;
      buffer_width = 2048u; buffer_height = 1024u;
      /* The dot clock here is `PROVISIONAL` -- see the header. Table 11-8's
       * 8.47 ns pixel implies 118.06 MHz and this is the oracle's 120, taken
       * because 118.06 MHz does not divide the time base and this does. */
      dot_clock = 120000000u; h_total = 1728u; v_total = 1066u;
      break;
    case AP_SCREEN_MONO_15_INCH:
      /* The oracle's, not the manual's -- see the header. */
      planes = 1u; width = 1024u; height = 800u;
      buffer_width = 1024u; buffer_height = 1024u;
      dot_clock = 68000000u; h_total = 1346u; v_total = 841u;
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
  out->dot_clock_hz = dot_clock;
  out->h_total = h_total;
  out->v_total = v_total;
  return true;
}

bool ap_graphics_display_enabled(uint8_t cr1) {
  return (cr1 & AP_GRAPHICS_CR1_DISP_EN) != 0u;
}

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

void ap_graphics_cr2_fields(const ap_graphics_t *graphics, unsigned *s_plane,
                            unsigned *d_plane,
                            ap_graphics_cr2_access_t *access) {
  const bool eight = graphics->screen == AP_SCREEN_COLOUR_8_PLANE;
  const bool colour = ap_graphics_is_colour(graphics->screen);

  if (!colour) {
    /* One plane, so the selects are not read at all: source plane 0 and a
     * destination mask of `0E`, which -- the mask being **active low** --
     * leaves plane 0 selected and the three that do not exist masked out. */
    *s_plane = 0u;
    *d_plane = 0x0Eu;
    *access = ap_graphics_cr2_access(graphics->reg.cr2);
    return;
  }
  if (eight) {
    /* `CR2A` is the destination mask entire, and `CR2B` carries the source
     * plane *and the access mode*. Reading the access from `CR2` here would
     * pick up the top two bits of the destination mask, which is a value that
     * changes with every plane the driver selects. */
    *d_plane = graphics->reg.cr2;
    *s_plane = graphics->reg.cr2b & AP_GRAPHICS_CR2_S_PLANE_MASK_8;
    *access = ap_graphics_cr2_access(graphics->reg.cr2b);
    return;
  }
  *s_plane = ap_graphics_cr2_source_plane(graphics->reg.cr2, false);
  *d_plane = ap_graphics_cr2_dest_plane(graphics->reg.cr2, false);
  *access = ap_graphics_cr2_access(graphics->reg.cr2);
}

/* Shift one source word into a plane's guard latch. The latch keeps the
 * previous word above the new one, which is what a shifted blit reaches back
 * into -- see `ap_graphics_blit_t`. */
static void latch_source(ap_graphics_t *graphics, unsigned plane,
                         uint16_t word) {
  if (plane >= AP_GRAPHICS_MAX_PLANES) {
    return;
  }
  graphics->guard_latch[plane] =
      (graphics->guard_latch[plane] << 16) | word;
}

/* Latch from the image memory at `offset`, one word per plane -- or one word
 * broadcast, when the board has a single plane or `AD_BIT` says so. */
static void latch_from_memory(ap_graphics_t *graphics, uint32_t offset,
                              unsigned s_plane, unsigned planes,
                              uint32_t plane_words, const uint8_t *memory,
                              uint32_t words) {
  const bool broadcast =
      planes == 1u ||
      (graphics->reg.cr1 & AP_GRAPHICS_CR1_COLOUR_AD_BIT) != 0u;
  if (broadcast) {
    const uint32_t at = offset + plane_words * s_plane;
    latch_source(graphics, s_plane, at < words ? image_word(memory, at) : 0u);
    return;
  }
  uint32_t at = offset;
  for (unsigned plane = 0; plane < planes; plane++) {
    latch_source(graphics, plane, at < words ? image_word(memory, at) : 0u);
    at += plane_words;
  }
}

ap_graphics_cycle_t ap_graphics_memory_cycle(ap_graphics_t *graphics,
                                             uint32_t offset, uint16_t data,
                                             uint16_t mem_mask) {
  ap_graphics_cycle_t out = {0};

  ap_graphics_geometry_t geometry;
  if (!ap_graphics_geometry(graphics->screen, &geometry)) {
    return out;
  }
  const bool colour = ap_graphics_is_colour(graphics->screen);
  uint8_t *memory = colour ? graphics->colour_memory : graphics->mono_memory;
  const uint32_t bytes = colour ? graphics->colour_bytes : graphics->mono_bytes;
  if (memory == NULL) {
    return out;
  }
  const uint32_t words = bytes / 2u;

  unsigned s_plane = 0u, d_plane = 0u;
  ap_graphics_cr2_access_t access = AP_GRAPHICS_CR2_CONSTANT_ACCESS;
  ap_graphics_cr2_fields(graphics, &s_plane, &d_plane, &access);

  ap_graphics_blit_t blit = {
      .cr0 = graphics->reg.cr0,
      .cr1 = graphics->reg.cr1,
      .access = access,
      .rop_register = graphics->reg.rop,
      .write_enable = graphics->reg.write_enable,
      .d_plane = d_plane,
      .s_plane = s_plane,
      .planes = geometry.planes,
      .plane_stride = geometry.plane_words,
  };

  switch (ap_graphics_cr0_mode(graphics->reg.cr0)) {
    case AP_GRAPHICS_CR0_CPU_DEST_BLT:
      /* The write carries an address and nothing else: the controller latches
       * the source from memory for the CPU to read back. Nothing is drawn, and
       * a model that drew here would paint over the very word being read. */
      latch_source(graphics, s_plane,
                   (offset + geometry.plane_words * s_plane) < words
                       ? image_word(memory,
                                    offset + geometry.plane_words * s_plane)
                       : 0u);
      return out;

    case AP_GRAPHICS_CR0_ALTERNATING_BLT:
      if (graphics->blt_cycle == 0u) {
        graphics->blt_cycle = 1u;
        latch_from_memory(graphics, offset, s_plane, geometry.planes,
                          geometry.plane_words, memory, words);
        return out;
      }
      graphics->blt_cycle = 0u;
      /* The second write *is* the write enable register. It is stored, not
       * merely used: the register keeps the value afterwards. */
      graphics->reg.write_enable = data;
      blit.write_enable = data;
      out.planes_written =
          ap_graphics_blit(&blit, memory, bytes, offset, mem_mask,
                           graphics->guard_latch);
      out.blitted = true;
      return out;

    case AP_GRAPHICS_CR0_VECTOR:
      graphics->reg.write_enable = data;
      blit.write_enable = data;
      out.planes_written =
          ap_graphics_blit(&blit, memory, bytes, offset, mem_mask,
                           graphics->guard_latch);
      out.blitted = true;
      return out;

    case AP_GRAPHICS_CR0_CPU_SOURCE_BLT:
      if (graphics->blt_cycle == 0u) {
        graphics->blt_cycle = 1u;
        /* A byte access on the **upper** lane is moved down before latching.
         * The oracle carries this as an explicit fix for a Domain/OS test, and
         * it is the sort of thing no manual would state: the source is a value,
         * not a placed byte, so a driver writing the high half means the value
         * and not the position. */
        uint16_t value = data;
        uint16_t mask = mem_mask;
        if (mask == 0xFF00u) {
          value = (uint16_t)(value >> 8);
          mask = (uint16_t)(mask >> 8);
        }
        latch_source(graphics, s_plane, (uint16_t)(value & mask));
        return out;
      }
      graphics->blt_cycle = 0u;
      graphics->reg.write_enable = data;
      blit.write_enable = data;
      out.planes_written =
          ap_graphics_blit(&blit, memory, bytes, offset, mem_mask,
                           graphics->guard_latch);
      out.blitted = true;
      return out;

    case AP_GRAPHICS_CR0_DOUBLE_ACCESS_BLT: {
      /* The address lines carry the source and the *data* lines the
       * destination word offset -- one bus cycle moving a word from one place
       * in the image memory to another, which is what makes a full-screen copy
       * one access per word instead of two. */
      latch_from_memory(graphics, offset, s_plane, geometry.planes,
                        geometry.plane_words, memory, words);
      uint32_t dest = (uint32_t)(data & mem_mask);
      /* `DADDR_16` is a *monochrome* bit and only the 19-inch board's: its
       * destination needs a seventeenth address bit that the data lines cannot
       * carry. On any other card the same bit position is `DV_CK`. */
      if (graphics->screen == AP_SCREEN_MONO_19_INCH &&
          (graphics->reg.cr1 & AP_GRAPHICS_CR1_MONO_DADDR_16) != 0u) {
        dest += 0x10000u;
      }
      out.planes_written = ap_graphics_blit(&blit, memory, bytes, dest, 0xFFFFu,
                                            graphics->guard_latch);
      out.blitted = true;
      return out;
    }

    case AP_GRAPHICS_CR0_NORMAL:
      latch_source(graphics, s_plane, (uint16_t)(data & mem_mask));
      out.planes_written =
          ap_graphics_blit(&blit, memory, bytes, offset, mem_mask,
                           graphics->guard_latch);
      out.blitted = true;
      return out;

    case AP_GRAPHICS_CR0_UNKNOWN_5:
    case AP_GRAPHICS_CR0_UNKNOWN_6:
    default:
      /* Nothing names these, so nothing is done and the caller is told. A run
       * that reaches one is a run whose picture cannot be trusted, and a silent
       * store would hide that behind a plausible image. */
      out.unknown_mode = true;
      return out;
  }
}

uint16_t ap_graphics_memory_read_cycle(ap_graphics_t *graphics,
                                       uint32_t offset) {
  ap_graphics_geometry_t geometry;
  if (!ap_graphics_geometry(graphics->screen, &geometry)) {
    return 0xFFFFu;
  }
  const bool colour = ap_graphics_is_colour(graphics->screen);
  const uint8_t *memory = colour ? graphics->colour_memory
                                 : graphics->mono_memory;
  const uint32_t bytes = colour ? graphics->colour_bytes
                                : graphics->mono_bytes;
  if (memory == NULL) {
    return 0xFFFFu;
  }
  const uint32_t words = bytes / 2u;

  unsigned s_plane = 0u, d_plane = 0u;
  ap_graphics_cr2_access_t access = AP_GRAPHICS_CR2_CONSTANT_ACCESS;
  ap_graphics_cr2_fields(graphics, &s_plane, &d_plane, &access);

  switch (ap_graphics_cr0_mode(graphics->reg.cr0)) {
    case AP_GRAPHICS_CR0_VECTOR:
    case AP_GRAPHICS_CR0_CPU_SOURCE_BLT:
      /* These two drive an internal data bus rather than the memory: what comes
       * back is the guard latch, which is how a driver reads the source it has
       * been assembling instead of whatever the destination happens to hold. */
      return (uint16_t)graphics->guard_latch[s_plane < AP_GRAPHICS_MAX_PLANES
                                                 ? s_plane
                                                 : 0u];
    case AP_GRAPHICS_CR0_CPU_DEST_BLT:
    case AP_GRAPHICS_CR0_ALTERNATING_BLT:
    case AP_GRAPHICS_CR0_DOUBLE_ACCESS_BLT:
    case AP_GRAPHICS_CR0_NORMAL:
    case AP_GRAPHICS_CR0_UNKNOWN_5:
    case AP_GRAPHICS_CR0_UNKNOWN_6:
    default:
      break;
  }

  /* Every other mode **latches while reading**. A read of this device changes
   * it, which is why nothing here takes a const graphics and why an instrument
   * that watched this range would perturb what it measured. */
  latch_from_memory(graphics, offset, s_plane, geometry.planes,
                    geometry.plane_words, memory, words);

  /* And the word comes from the **source plane**, not from plane 0. The window
   * is one plane's worth of addresses; which plane is `CR2`'s answer. */
  const uint32_t at = offset + geometry.plane_words * s_plane;
  return at < words ? image_word(memory, at) : 0xFFFFu;
}

void ap_graphics_advance(ap_graphics_t *graphics, ap_time_t now) {
  /* The raster is a function of the instant, not an accumulation, so there is
   * no remainder to carry and no dependence on how often this is called. A
   * caller that skipped a thousand frames and one that ticked every pixel see
   * the same beam. */
  graphics->now = now;
}

bool ap_graphics_beam(const ap_graphics_t *graphics, unsigned *line,
                      unsigned *pixel) {
  ap_graphics_geometry_t geometry;
  if (!ap_graphics_geometry(graphics->screen, &geometry) ||
      geometry.dot_clock_hz == 0u) {
    return false;
  }
  ap_clock_t dot;
  if (!ap_clock_init(&dot, geometry.dot_clock_hz)) {
    /* Unrepresentable at this time base, which `ap_clock_init` refuses rather
     * than rounding. A screen whose clock the base does not divide has no
     * raster here, and saying so is better than a beam that drifts. */
    return false;
  }

  const uint64_t dots = graphics->now / dot.period;
  const uint64_t frame_dots = (uint64_t)geometry.h_total * geometry.v_total;
  const uint64_t into_frame = dots % frame_dots;
  if (line != NULL) { *line = (unsigned)(into_frame / geometry.h_total); }
  if (pixel != NULL) { *pixel = (unsigned)(into_frame % geometry.h_total); }
  return true;
}

bool ap_graphics_adc(const ap_graphics_t *graphics, uint8_t channel,
                     uint8_t *level) {
  if (level == NULL) {
    return false;
  }
  /* Bits 3-2 must be `01` for a video measurement. Anything else is a
   * conversion this core has nothing to say about. */
  if ((channel & 0x0Cu) != 0x04u) {
    return false;
  }
  ap_graphics_geometry_t geometry;
  if (!ap_graphics_geometry(graphics->screen, &geometry)) {
    return false;
  }
  const bool colour = ap_graphics_is_colour(graphics->screen);
  const uint8_t *memory = colour ? graphics->colour_memory
                                 : graphics->mono_memory;
  const uint32_t bytes = colour ? graphics->colour_bytes
                                : graphics->mono_bytes;
  if (memory == NULL) {
    return false;
  }

  /* The **stepped** beam, not the running one. The diagnostic winds the counters
   * to a chosen place with `DH_CK` and `DV_CK` and then asks what is there, so
   * measuring where the free-running raster happens to be would answer a
   * question about a different pixel -- which is why the reading was out of
   * range with a running raster and no counters. */
  unsigned line = 0u, pixel = 0u;
  if (!ap_graphics_stepped_beam(graphics, &line, &pixel)) {
    return false;
  }

  /* The pixel under the beam, composed from the planes exactly as the scanout
   * does, and always the **leftmost** bit of the word -- which is the oracle's
   * `get_pixel(..., 0x8000)`. The beam's position picks the word; the mask does
   * not follow it into the word. */
  const uint32_t word = (uint32_t)line * (geometry.buffer_width / 16u) +
                        pixel / 16u;
  unsigned index = 0u;
  for (unsigned p = 0; p < geometry.planes; p++) {
    const uint32_t at = p * geometry.plane_words + word;
    if (at * 2u + 1u >= bytes) {
      continue;
    }
    index |= (unsigned)((image_word(memory, at) >> 15) & 1u) << p;
  }

  uint8_t rgb[3] = {0u, 0u, 0u};
  (void)ap_bt458_palette(&graphics->lut, index, rgb);

  /* Which of the three guns. */
  const unsigned gun = channel & 0x03u;
  if (gun > 2u) {
    *level = 0u;
    return true;
  }

  /* Drawing, blanking or sync, and the three give different levels. "Drawing"
   * is `BLANK` **set**, which is this board's active-low convention. */
  const bool drawing = line < geometry.height && pixel < geometry.width;
  if (drawing) {
    static const uint8_t base[3] = {10u, 70u, 10u};
    *level = (uint8_t)(base[gun] + rgb[gun] / 2u);
    return true;
  }
  /* Inside the blanking interval the level is a floor, and green sits far above
   * the other two -- the composite sync rides on the green gun, which is why it
   * reads 60 where red and blue read 5. */
  const unsigned v_sync_start = geometry.height + 4u;
  const unsigned v_sync_end = geometry.height + 8u;
  const bool in_sync = line >= v_sync_start && line < v_sync_end;
  if (!in_sync && pixel < 20u) {
    static const uint8_t blanking[3] = {5u, 60u, 5u};
    *level = blanking[gun];
    return true;
  }
  *level = 5u;
  return true;
}

bool ap_graphics_stepped_beam(const ap_graphics_t *graphics, unsigned *line,
                              unsigned *pixel) {
  ap_graphics_geometry_t geometry;
  if (!ap_graphics_geometry(graphics->screen, &geometry)) {
    return false;
  }
  if (line != NULL) { *line = graphics->v_clock; }
  /* The horizontal counter counts *words*, not pixels -- the oracle indexes
   * `m_v_clock * buffer_width / 16 + m_h_clock`, so a step is sixteen pixels.
   * That is the granularity a diagnostic walking the screen wants, and reading
   * it as pixels would put the beam sixteen times too far left. */
  if (pixel != NULL) { *pixel = graphics->h_clock * 16u; }
  return true;
}
