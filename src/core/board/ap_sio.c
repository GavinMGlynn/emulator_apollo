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
  sio->register_reads[unit][reg]++;
  return ap_mc68681_read(&sio->port[unit], reg);
}

void ap_sio_write(ap_sio_t *sio, uint32_t address, uint8_t value) {
  unsigned unit;
  unsigned reg;
  if (!ap_sio_decode(address, &unit, &reg)) {
    return;
  }
  sio->register_writes[unit][reg]++;
  ap_mc68681_write(&sio->port[unit], reg, value);
}

bool ap_sio_irq(const ap_sio_t *sio) {
  return ap_mc68681_irq(&sio->port[0]) || ap_mc68681_irq(&sio->port[1]);
}

void ap_sio_receive(ap_sio_t *sio, unsigned unit, unsigned channel,
                    uint8_t byte) {
  if (unit >= 2u) {
    return;
  }
  ap_mc68681_receive(&sio->port[unit], channel, byte);
}

bool ap_sio_receiver_ready(ap_sio_t *sio, unsigned unit,
                           unsigned channel) {
  if (unit >= 2u) {
    return false;
  }
  /* Read the status register the same way the program does, rather than
   * reaching into the receive FIFO: the bit the firmware polls is the bit that
   * decides, and a helper that consulted different state could disagree with
   * the machine about whether a byte is waiting. */
  const unsigned status = (channel == 0u) ? AP_MC68681_SR_CSR_A
                                          : AP_MC68681_SR_CSR_B;
  return (ap_mc68681_read(&sio->port[unit], status) & AP_MC68681_SR_RXRDY) !=
         0u;
}

bool ap_sio_transmit(ap_sio_t *sio, unsigned unit, unsigned channel,
                     uint8_t *byte) {
  if (unit >= 2u) {
    return false;
  }
  return ap_mc68681_transmit(&sio->port[unit], channel, byte);
}

void ap_sio_receive_at(ap_sio_t *sio, unsigned unit, unsigned channel,
                       uint8_t byte, uint8_t sender_csr) {
  if (unit >= 2u) {
    return;
  }
  ap_mc68681_receive_at(&sio->port[unit], channel, byte, sender_csr);
}

void ap_sio_receive_framed(ap_sio_t *sio, unsigned unit, unsigned channel,
                           uint8_t byte, uint8_t sender_csr,
                           uint8_t sender_mr1) {
  if (unit >= 2u) {
    return;
  }
  ap_mc68681_receive_framed(&sio->port[unit], channel, byte, sender_csr,
                            sender_mr1);
}

uint8_t ap_sio_clock_select(ap_sio_t *sio, unsigned unit, unsigned channel) {
  if (unit >= 2u) {
    return 0u;
  }
  const unsigned reg =
      (channel == 0u) ? AP_MC68681_SR_CSR_A : AP_MC68681_SR_CSR_B;
  /* The register reads as *status*; the clock select is write-only, so the
   * channel's stored value is the only source. */
  (void)reg;
  return sio->port[unit].channel[channel].csr;
}
