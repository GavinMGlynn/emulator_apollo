#include "board/ap_tape.h"

void ap_tape_reset(ap_tape_t *tape) { ap_sc499_reset(&tape->controller); }

bool ap_tape_decode(uint32_t address, unsigned *reg) {
  if ((address & ~(AP_TAPE_RANGE - 1u)) != AP_TAPE_ADDR) {
    return false;
  }
  uint32_t offset = (address - AP_TAPE_ADDR) & 7u;
  /* Four registers in each eight-byte block; the upper four read `FF` in the
   * measured dump and are not the part. */
  if (offset >= AP_SC499_REGISTERS) {
    return false;
  }
  *reg = offset;
  return true;
}

uint8_t ap_tape_read(ap_tape_t *tape, uint32_t address) {
  unsigned reg;
  if (!ap_tape_decode(address, &reg) || !ap_sc499_readable(reg)) {
    /* Nothing drives the bus: either the address is undecoded, or it is one of
     * the two write-only DMA commands. The measured dump reads `FF` for both,
     * so the board supplies the floating value -- the part cannot, because the
     * part is precisely what is not answering. */
    return 0xFFu;
  }
  return ap_sc499_read(&tape->controller, reg);
}

void ap_tape_write(ap_tape_t *tape, uint32_t address, uint8_t value) {
  unsigned reg;
  if (!ap_tape_decode(address, &reg)) {
    return;
  }
  ap_sc499_write(&tape->controller, reg, value);
}

bool ap_tape_irq(const ap_tape_t *tape) { return ap_sc499_irq(&tape->controller); }
