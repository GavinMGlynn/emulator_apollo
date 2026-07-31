#include "board/ap_sio.h"

void ap_sio_reset(ap_sio_t *sio) {
  ap_mc68681_reset(&sio->port[0]);
  ap_mc68681_reset(&sio->port[1]);
}

bool ap_sio_decode(uint32_t address, unsigned *unit, unsigned *reg) {
  uint32_t base = address & ~(AP_SIO_RANGE - 1u);
  if (base != AP_SIO1_ADDR && base != AP_SIO2_ADDR) {
    return false;
  }
  *unit = base == AP_SIO2_ADDR ? 1u : 0u;
  /* Stride 2: both bytes of a word select the same register, which is why the
   * measured dump reads every value twice. */
  *reg = ((address - base) >> 1) & (AP_MC68681_REGISTERS - 1u);
  return true;
}

uint8_t ap_sio_read(ap_sio_t *sio, uint32_t address) {
  unsigned unit;
  unsigned reg;
  if (!ap_sio_decode(address, &unit, &reg)) {
    return 0u;
  }
  return ap_mc68681_read(&sio->port[unit], reg);
}

void ap_sio_write(ap_sio_t *sio, uint32_t address, uint8_t value) {
  unsigned unit;
  unsigned reg;
  if (!ap_sio_decode(address, &unit, &reg)) {
    return;
  }
  ap_mc68681_write(&sio->port[unit], reg, value);
}

bool ap_sio_irq(const ap_sio_t *sio) {
  return ap_mc68681_irq(&sio->port[0]) || ap_mc68681_irq(&sio->port[1]);
}
