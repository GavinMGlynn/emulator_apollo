#include "board/ap_boardreg.h"

#include <string.h>

/* Measured power-on values; see the header on what "measured" claims here.
 *
 * `8100` is the measurement, and it was taken against the oracle in its
 * shipping configuration -- which is **service** mode. Bit 0 is the
 * Normal/Service switch and it is an input rather than a power-on level, so the
 * default here sets it: a workstation runs normal. See the header. */
/* `8000`, not the measured `8100`. Bit 8 is the CPU timeout, and the probe that
 * measured `8100` ran at 0.001, 0.5 and 2.0 emulated seconds -- all of them
 * *after* the boot PROM had begun probing for absent hardware, so the bit was a
 * latched condition rather than a power-on level. The oracle's own initialiser
 * is `BIT15 | SERVICE`, with bit 8 clear, and it is the one thing that can
 * speak to the instant before any instruction runs. Now that a declined access
 * sets the bit, a probe taken at the same moments would measure `8100` again --
 * which is what makes the two accounts agree rather than one replace the
 * other. */
#define CPU_STATUS_RESET (0x8000u | AP_BOARDREG_STATUS_NORMAL_MODE)
#define CPU_STATUS_SERVICE 0x8000u
#define CPU_CONTROL_RESET 0xF700u
#define CACHE_CONTROL_RESET 0xEFu
#define LATCH_PAGE_RESET 0x0000u

void ap_boardreg_init(ap_boardreg_t *regs) {
  memset(regs, 0, sizeof *regs);
  regs->cpu_status = CPU_STATUS_RESET;
  regs->cpu_control = CPU_CONTROL_RESET;
  regs->cache_control = CACHE_CONTROL_RESET;
  regs->latch_page_on_parity = LATCH_PAGE_RESET;
  regs->active_low_parity_lanes = true;
  /* "(No Board)  FF". A board with memory fitted calls
   * `ap_boardreg_set_memory_boards`; until it does, the register says what an
   * empty machine's would. */
  regs->memory_present = AP_BOARDREG_MEM_PRESENT_EMPTY;
}

bool ap_boardreg_memory_present_code(unsigned megabytes, unsigned *out) {
  if (out == NULL) {
    return false;
  }
  /* `019411-A00` §4.2.1.18's table, read back as a code per size. Not ordered
   * by capacity -- see the header. */
  switch (megabytes) {
  case 0u: *out = AP_BOARDREG_MEM_PRESENT_CODE_NONE; return true;
  case 4u: *out = AP_BOARDREG_MEM_PRESENT_CODE_4MB; return true;
  case 8u: *out = AP_BOARDREG_MEM_PRESENT_CODE_8MB; return true;
  case 16u: *out = AP_BOARDREG_MEM_PRESENT_CODE_16MB; return true;
  default: break;
  }
  /* Every code is taken, so an unlisted size has no honest encoding. */
  return false;
}

bool ap_boardreg_set_memory_boards(ap_boardreg_t *regs,
                                   const unsigned *slot_megabytes,
                                   unsigned slots) {
  if (regs == NULL || slot_megabytes == NULL ||
      slots != AP_BOARDREG_MEM_PRESENT_SLOTS) {
    return false;
  }
  /* Assembled whole and stored once: a slot rejected halfway through must not
   * leave the register holding a configuration no machine has. */
  uint8_t value = 0u;
  for (unsigned slot = 0; slot < slots; slot++) {
    unsigned code = 0u;
    if (!ap_boardreg_memory_present_code(slot_megabytes[slot], &code)) {
      return false;
    }
    /* "Bits 0 and 1 are slot 0, bits 2 and 3 are slot 1, bits 4 and 5 are slot
     * 2, and bits 6 and 7 are slot 3." */
    value |= (uint8_t)(code << (slot * 2u));
  }
  regs->memory_present = value;
  return true;
}

uint8_t ap_boardreg_memory_present(const ap_boardreg_t *regs) {
  return regs == NULL ? AP_BOARDREG_MEM_PRESENT_EMPTY : regs->memory_present;
}

void ap_boardreg_set_active_low_lanes(ap_boardreg_t *regs, bool active_low) {
  regs->active_low_parity_lanes = active_low;
}

bool ap_boardreg_fpu_trapped(const ap_boardreg_t *regs) {
  return (regs->cpu_control & AP_BOARDREG_CONTROL_FPU_TRAP) != 0u;
}

void ap_boardreg_set_interrupt_pending(ap_boardreg_t *regs, bool pending) {
  regs->interrupt_pending = pending;
}

/* Table 2-8's ranges are 256 bytes and the register is aliased within one, so
 * the decode drops the low byte. */
static bool in_range(uint32_t address, uint32_t base) {
  return (address & ~(AP_BOARDREG_RANGE - 1u)) == base;
}

bool ap_boardreg_decode(uint32_t address, ap_boardreg_id_t *out) {
  if (in_range(address, AP_BOARDREG_CPU_STATUS_ADDR)) {
    *out = AP_BOARDREG_CPU_STATUS;
    return true;
  }
  if (in_range(address, AP_BOARDREG_CPU_CONTROL_ADDR)) {
    *out = AP_BOARDREG_CPU_CONTROL;
    return true;
  }
  if (in_range(address, AP_BOARDREG_CACHE_CONTROL_ADDR)) {
    *out = AP_BOARDREG_CACHE_CONTROL;
    return true;
  }
  if (in_range(address, AP_BOARDREG_LATCH_PAGE_ADDR)) {
    *out = AP_BOARDREG_LATCH_PAGE_ON_PARITY;
    return true;
  }
  if (in_range(address, AP_BOARDREG_MASTER_REQUEST_ADDR)) {
    *out = AP_BOARDREG_MASTER_REQUEST;
    return true;
  }
  if (in_range(address, AP_BOARDREG_TASK_ALIAS_ADDR)) {
    *out = AP_BOARDREG_TASK_ALIAS;
    return true;
  }
  if (in_range(address, AP_BOARDREG_SELECTIVE_CLEAR_ADDR)) {
    *out = AP_BOARDREG_SELECTIVE_CLEAR;
    return true;
  }
  if (in_range(address, AP_BOARDREG_MEMORY_PRESENT_ADDR)) {
    /* Decoded here for every model, and *placed* only by the DS5500 map. The
     * board decides which addresses exist -- `011400` is outside every
     * placement in `DS4000_PLACEMENT`, so a DN3500 still bus-errors on it and
     * this arm is unreachable there. Gating it twice would put the model test
     * in the file that has no model. */
    *out = AP_BOARDREG_MEMORY_PRESENT;
    return true;
  }
  return false;
}

/* The one range where the low bits are the decode rather than an alias --
 * `019411-A00` gives each function its own address. See the header. */
static void selective_clear(ap_boardreg_t *regs, uint32_t address) {
  switch (address & (AP_BOARDREG_RANGE - 1u)) {
  case AP_BOARDREG_CLEAR_ALL_OFFSET:
    regs->cpu_status &= (uint16_t)~AP_BOARDREG_STATUS_CONDITIONS;
    break;
  case AP_BOARDREG_CLEAR_FPU_TRAP_OFFSET:
    regs->cpu_status &= (uint16_t)~AP_BOARDREG_STATUS_FP_TRAP;
    break;
  case AP_BOARDREG_CLEAR_PARITY_OFFSET:
    regs->cpu_status &= (uint16_t)~AP_BOARDREG_STATUS_PARITY_MASK;
    break;
  case AP_BOARDREG_CLEAR_BUS_ERROR_OFFSET:
    regs->cpu_status &= (uint16_t)~AP_BOARDREG_STATUS_BUS_ERROR;
    break;
  case AP_BOARDREG_CLEAR_GRAPHICS_TRAP_OFFSET:
    /* Named by the addendum, and which status bit it is has no source here.
     * Decoded so the address is not reported as unmapped, and clearing nothing
     * rather than guessing at a bit. See the header. */
    break;
  default:
    /* Inside the range and not one of the five. Nothing is named for it, so
     * nothing happens -- the same position as the gap addresses in Table 2-8. */
    break;
  }
}

bool ap_boardreg_is_declined(uint32_t address) {
  /* Nothing is declined now: both of Table 2-8's remaining registers are
   * modelled as the byte-wide storage the table says exists. Kept as a function
   * rather than deleted because a caller asking the question deserves a
   * truthful answer, and because a future register whose behaviour genuinely
   * cannot be established would use it. */
  (void)address;
  return false;
}

uint8_t ap_boardreg_master_request(const ap_boardreg_t *regs) {
  return regs == NULL ? 0u : regs->master_request;
}

/* Registers that are eight bits wide and aliased across their whole 256-byte
 * range, so that neither read nor write has a byte lane to choose.
 *
 * Measured for the cache register -- `010201` behaves identically to `010200`,
 * and a word read returns the byte twice. The memory present register is here
 * on the family's evidence rather than its own: `019411-A00` calls it "8-bit"
 * and gives it a 256-byte row in Table 2-5, exactly as for the registers whose
 * aliasing was measured, and no source on disk reports a read of `011401`. */
static bool is_byte_register(ap_boardreg_id_t id) {
  return id == AP_BOARDREG_CACHE_CONTROL || id == AP_BOARDREG_MASTER_REQUEST ||
         id == AP_BOARDREG_TASK_ALIAS || id == AP_BOARDREG_MEMORY_PRESENT;
}

/* The value a register reads as, at its own width. */
static uint16_t value_of(const ap_boardreg_t *regs, ap_boardreg_id_t id) {
  switch (id) {
  case AP_BOARDREG_CPU_STATUS:
    /* Bit 15 reads 1 whatever is written or held. */
    return regs->cpu_status | AP_BOARDREG_STATUS_ALWAYS_SET;
  case AP_BOARDREG_CPU_CONTROL:
    return regs->cpu_control;
  case AP_BOARDREG_CACHE_CONTROL:
    /* Eight bits. Only bit 7 is storage, the rest read a fixed pattern -- and
     * bit 4 is neither: it is the master controller's request line. See the
     * header on why the probe's "fixed" pattern could not contain it. */
    return (uint16_t)((regs->cache_control & AP_BOARDREG_CACHE_WRITABLE) |
                      AP_BOARDREG_CACHE_FIXED |
                      (regs->interrupt_pending
                           ? AP_BOARDREG_CACHE_INTERRUPT_PENDING
                           : 0u));
  case AP_BOARDREG_LATCH_PAGE_ON_PARITY:
    return regs->latch_page_on_parity;
  case AP_BOARDREG_MASTER_REQUEST:
    return regs->master_request;
  case AP_BOARDREG_TASK_ALIAS:
    return regs->task_alias;
  case AP_BOARDREG_MEMORY_PRESENT:
    return regs->memory_present;
  case AP_BOARDREG_SELECTIVE_CLEAR:
    /* Write-only as far as anything here can say: no firmware in hand reads
     * the range, so there is no measurement and no page. All-ones because C10
     * established that is what nothing-driving-the-bus looks like on this
     * machine, which is the closest thing to a default it has. */
    return 0xFFFFu;
  case AP_BOARDREG_COUNT:
    break;
  }
  return 0u;
}

uint16_t ap_boardreg_read16(const ap_boardreg_t *regs, uint32_t address) {
  ap_boardreg_id_t id;
  if (!ap_boardreg_decode(address, &id)) {
    return 0u;
  }
  uint16_t value = value_of(regs, id);
  if (id == AP_BOARDREG_CACHE_CONTROL) {
    /* Measured: a 16-bit read of the byte register returns `EFEF`, not `00EF`.
     * The byte appears in both halves. This is the register's own behaviour and
     * not a convenience of this model -- reading it as a plain zero-extended
     * byte would disagree with the hardware in the top eight bits of every
     * word access firmware makes to it. */
    return (uint16_t)((value << 8) | value);
  }
  return value;
}

uint8_t ap_boardreg_read8(const ap_boardreg_t *regs, uint32_t address) {
  ap_boardreg_id_t id;
  if (!ap_boardreg_decode(address, &id)) {
    return 0u;
  }
  const uint16_t value = value_of(regs, id);
  if (is_byte_register(id)) {
    /* Byte registers, aliased across their whole range: the same byte at
     * `010200` and `010201`, which is why a word read of the cache register
     * returns `EFEF`. There is no lane to choose. */
    return (uint8_t)value;
  }
  /* Sixteen-bit registers: address bit 0 is the byte lane, big-endian, so an
   * even address is the **high** half.
   *
   * This returned the low byte for both, which is invisible on a byte read of
   * a register whose interesting bits are low, and wrong on every *word* read:
   * the board serves a word as two byte reads, so the status register's `8001`
   * came back as `0101` -- low byte duplicated -- and bit 8 read set whatever
   * the register held. The loaded diagnostic's bus-error test is what found it:
   * it clears the CPU timeout at `016408` and requires bit 8 to read clear,
   * which no value of the register could satisfy.
   *
   * The write side was given this lane split earlier, when the control
   * register's LED byte turned out to be separate from its parity byte. Reads
   * were left alone, and the two halves of one register disagreed about what a
   * byte meant. */
  return (address & 1u) ? (uint8_t)(value & 0xFFu) : (uint8_t)(value >> 8);
}

static void store(ap_boardreg_t *regs, ap_boardreg_id_t id, uint16_t value) {
  switch (id) {
  case AP_BOARDREG_CPU_STATUS:
    /* A write carries nothing and *acknowledges* -- `008778-03` §3.2, "Writing
     * to the status register clears the interrupt status". Measured too: after
     * a write of any value the register read `8000`, and the bit standing at
     * power-on could not be put back, which is a stronger statement than
     * "writes are ignored".
     *
     * What the measurement could not show is the three bits that are not
     * conditions and survive -- the switch input, the FP trap, and bit 15. It
     * ran in service mode with no trap pending, where all three are already
     * what an unconditional `8000` says. See the header. */
    regs->cpu_status &= AP_BOARDREG_STATUS_WRITE_KEEPS;
    regs->cpu_status |= AP_BOARDREG_STATUS_ALWAYS_SET;
    break;
  case AP_BOARDREG_CPU_CONTROL:
    regs->cpu_control = value;
    /* Also the diagnostic LEDs, which are the register's *upper* byte: a
     * self-test failure posts its code here rather than to any console, so this
     * is the machine's own account of what went wrong. See the header. */
    ap_boardreg_post_code(regs, (uint8_t)(value >> 8));
    break;
  case AP_BOARDREG_CACHE_CONTROL:
    regs->cache_control = (uint8_t)(value & AP_BOARDREG_CACHE_WRITABLE);
    break;
  case AP_BOARDREG_LATCH_PAGE_ON_PARITY:
    regs->latch_page_on_parity = value;
    break;
  case AP_BOARDREG_MASTER_REQUEST:
    /* Byte-wide: every one of the 29 firmware sites is a `CLR.B` or a `MOVE.B`.
     * Stored and nothing more -- §2.4.7 says a bit here asserts an external
     * master's DMA request and does not say which, so acting on one would be
     * this core deciding a fact the manual withheld. */
    regs->master_request = (uint8_t)value;
    break;
  case AP_BOARDREG_TASK_ALIAS:
    /* No firmware in hand references this address at all, so its width is not
     * even established from that direction; Table 2-8's 256-byte range and the
     * board's byte lanes are all there is. Stored at a byte for the same reason
     * as its neighbour. */
    regs->task_alias = (uint8_t)value;
    break;
  case AP_BOARDREG_MEMORY_PRESENT:
    /* "This 8-bit, **read-only** register". What it holds is which slots have
     * boards in them, which no store can change -- so a write is dropped rather
     * than letting software tell the machine it has memory it does not.
     * `ap_boardreg_set_memory_boards` is the only way in, and the board calls
     * it from the same per-slot sizes the configuration table gets. */
    break;
  case AP_BOARDREG_SELECTIVE_CLEAR:
    /* Never reaches here: the write paths route this id by address before
     * calling `store`, because the value is not what it carries. */
  case AP_BOARDREG_COUNT:
    break;
  }
}

void ap_boardreg_write16(ap_boardreg_t *regs, uint32_t address,
                         uint16_t value) {
  ap_boardreg_id_t id;
  if (!ap_boardreg_decode(address, &id)) {
    return;
  }
  if (id == AP_BOARDREG_SELECTIVE_CLEAR) {
    /* The address, not the id: within this range the low bits choose the
     * function, so `store` -- which sees only the id -- cannot do it. */
    selective_clear(regs, address);
    return;
  }
  store(regs, id, value);
}

void ap_boardreg_write8(ap_boardreg_t *regs, uint32_t address, uint8_t value) {
  ap_boardreg_id_t id;
  if (!ap_boardreg_decode(address, &id)) {
    return;
  }
  if (id == AP_BOARDREG_SELECTIVE_CLEAR) {
    selective_clear(regs, address);
    return;
  }
  if (is_byte_register(id)) {
    /* Byte registers, aliased across the *whole* range -- `010201` behaves
     * identically to `010200`, measured for the cache register. So there is no
     * lane to choose.
     *
     * The master request register is byte-wide on the firmware's own evidence:
     * all 29 sites are `CLR.B` or `MOVE.B`. Task alias has no such evidence
     * either way and follows its neighbour rather than being given a width no
     * source states. */
    store(regs, id, value);
    return;
  }
  /* Sixteen-bit registers: address bit 0 is the byte lane, big-endian, so an
   * even address is the high half. Merging rather than replacing, because a
   * byte write leaves the other half alone -- which is what the firmware
   * writing LED codes at `010100` and parity control at `010101` depends on.
   *
   * The status register is the exception that proves it: a write to either half
   * acknowledges, because a write there carries no value at all. */
  const uint16_t held = value_of(regs, id);
  const uint16_t merged =
      (address & 1u) ? (uint16_t)((held & 0xFF00u) | (uint16_t)value)
                     : (uint16_t)((held & 0x00FFu) | (uint16_t)(value << 8));
  store(regs, id, merged);
}

uint16_t ap_boardreg_forced_lanes(const ap_boardreg_t *regs) {
  const uint16_t control = regs->cpu_control;
  if ((control & AP_BOARDREG_CONTROL_FORCE_BAD_PARITY) == 0u) {
    return 0u;
  }
  /* `08` from this family's PROMs means all four lanes, and `F8` from the
   * DN3000's means the same thing on a family where the bits are not inverted.
   * Two families, complementary values, one behaviour. See the header. */
  const unsigned bits = regs->active_low_parity_lanes ? ~(unsigned)control
                                                      : (unsigned)control;
  return (uint16_t)(bits & AP_BOARDREG_CONTROL_PARITY_LANE_MASK);
}

void ap_boardreg_post_code(ap_boardreg_t *regs, uint8_t written) {
  /* Exactly as written. The post routine complements what it displays and the
   * error loop's direct writes do not, so undoing it here would be right for
   * one caller and wrong for the other. See the header. */
  const uint8_t code = written;
  regs->posted_total++;
  /* Distinct in order: an error loop posts the same pair for ever, and a plain
   * ring would hold nothing but the last two. */
  if (regs->posted_count > 0u &&
      regs->posted[regs->posted_count - 1u] == code) {
    return;
  }
  if (regs->posted_count >= AP_BOARDREG_POSTED_CODES) {
    return;
  }
  regs->posted[regs->posted_count++] = code;
}

void ap_boardreg_set_normal_mode(ap_boardreg_t *regs, bool normal) {
  if (normal) {
    regs->cpu_status |= AP_BOARDREG_STATUS_NORMAL_MODE;
  } else {
    regs->cpu_status &= (uint16_t)~AP_BOARDREG_STATUS_NORMAL_MODE;
  }
}

void ap_boardreg_latch_status(ap_boardreg_t *regs, uint16_t mask) {
  regs->cpu_status |= mask;
}
