#include "board/ap_boardreg.h"

#include <string.h>

/* Measured power-on values; see the header on what "measured" claims here. */
#define CPU_STATUS_RESET 0x8100u
#define CPU_CONTROL_RESET 0xF700u
#define CACHE_CONTROL_RESET 0xEFu
#define LATCH_PAGE_RESET 0x0000u

void ap_boardreg_init(ap_boardreg_t *regs) {
  memset(regs, 0, sizeof *regs);
  regs->cpu_status = CPU_STATUS_RESET;
  regs->cpu_control = CPU_CONTROL_RESET;
  regs->cache_control = CACHE_CONTROL_RESET;
  regs->latch_page_on_parity = LATCH_PAGE_RESET;
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
  return false;
}

bool ap_boardreg_is_declined(uint32_t address) {
  return in_range(address, AP_BOARDREG_TASK_ALIAS_ADDR) ||
         in_range(address, AP_BOARDREG_MASTER_REQUEST_ADDR);
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
    /* Eight bits. Only bit 7 is storage; the rest read a fixed pattern. */
    return (uint16_t)((regs->cache_control & AP_BOARDREG_CACHE_WRITABLE) |
                      AP_BOARDREG_CACHE_FIXED);
  case AP_BOARDREG_LATCH_PAGE_ON_PARITY:
    return regs->latch_page_on_parity;
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
  return (uint8_t)value_of(regs, id);
}

static void store(ap_boardreg_t *regs, ap_boardreg_id_t id, uint16_t value) {
  switch (id) {
  case AP_BOARDREG_CPU_STATUS:
    /* Measured: after a write of *any* value the register reads `8000`, and
     * the bit that was set at power-on could not be put back. So a write
     * retains nothing and clears what was latched.
     *
     * Which is a stronger statement than "writes are ignored", and the probe
     * separated the two: an ignored write would have left the initial `8100`
     * intact. */
    regs->cpu_status = AP_BOARDREG_STATUS_ALWAYS_SET;
    break;
  case AP_BOARDREG_CPU_CONTROL:
    regs->cpu_control = value;
    break;
  case AP_BOARDREG_CACHE_CONTROL:
    regs->cache_control = (uint8_t)(value & AP_BOARDREG_CACHE_WRITABLE);
    break;
  case AP_BOARDREG_LATCH_PAGE_ON_PARITY:
    regs->latch_page_on_parity = value;
    break;
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
  store(regs, id, value);
}

void ap_boardreg_write8(ap_boardreg_t *regs, uint32_t address, uint8_t value) {
  ap_boardreg_id_t id;
  if (!ap_boardreg_decode(address, &id)) {
    return;
  }
  store(regs, id, value);
}

void ap_boardreg_latch_status(ap_boardreg_t *regs, uint16_t mask) {
  regs->cpu_status |= mask;
}
