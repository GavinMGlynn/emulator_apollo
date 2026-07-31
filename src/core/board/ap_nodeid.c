#include "board/ap_nodeid.h"

void ap_nodeid_init(ap_nodeid_t *prom, uint32_t id) {
  prom->id = id & 0x00FFFFFFu;
}

bool ap_nodeid_decode(uint32_t address, unsigned *reg) {
  if ((address & ~(AP_NODEID_RANGE - 1u)) != AP_NODEID_ADDR) {
    return false;
  }
  uint32_t offset = address - AP_NODEID_ADDR;
  /* Even bytes only. This is *not* the serial ports' arrangement, although both
   * are stride 2: there the dump reads every value twice, because both bytes of
   * a word reach the register, while here it reads `00 00 01 00 23 00 45 00` --
   * data on the even byte and zero on the odd one.
   *
   * Same stride, different odd-byte behaviour, on the same board. Reusing the
   * serial ports' decode here made offset 3 answer `01` where the hardware
   * answers `00`, and only comparing against the recorded dump caught it. */
  if ((offset & 1u) != 0u) {
    return false;
  }
  /* Aliased: the measured dump repeats after thirty-two bytes. */
  *reg = (offset >> 1) & (AP_NODEID_REGISTERS - 1u);
  return true;
}

uint8_t ap_nodeid_checksum(const ap_nodeid_t *prom) {
  unsigned sum = ((prom->id >> 16) & 0xFFu) + ((prom->id >> 8) & 0xFFu) +
                 (prom->id & 0xFFu);
  return (uint8_t)(sum & 0xFFu);
}

uint8_t ap_nodeid_read(const ap_nodeid_t *prom, uint32_t address) {
  unsigned reg;
  if (!ap_nodeid_decode(address, &reg)) {
    return 0u;
  }
  switch (reg) {
  case 0u:
    /* The dump's leading `00`: a 32-bit field holding a 24-bit identifier. */
    return (uint8_t)((prom->id >> 24) & 0xFFu);
  case 1u:
    return (uint8_t)((prom->id >> 16) & 0xFFu);
  case 2u:
    return (uint8_t)((prom->id >> 8) & 0xFFu);
  case 3u:
    return (uint8_t)(prom->id & 0xFFu);
  case AP_NODEID_CHECKSUM_REGISTER:
    return ap_nodeid_checksum(prom);
  default:
    /* Every other position read zero in the oracle. A PROM is read-only and
     * this core presents nothing it has not seen. */
    return 0u;
  }
}
