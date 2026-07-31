#include "board/ap_dma.h"

void ap_dma_reset(ap_dma_t *dma) {
  ap_i8237_reset(&dma->controller[0]);
  ap_i8237_reset(&dma->controller[1]);
}

bool ap_dma_decode(uint32_t address, unsigned *unit, unsigned *reg) {
  uint32_t base = address & ~(AP_DMA_RANGE - 1u);
  uint32_t offset = address - base;

  if (base == AP_DMA1_ADDR) {
    *unit = 0u;
    /* Stride 1, aliased every sixteen bytes -- measured: the all-mask register
     * appears at offset 15 and again at 31. */
    *reg = offset & (AP_I8237_REGISTERS - 1u);
    return true;
  }
  if (base == AP_DMA2_ADDR) {
    *unit = 1u;
    /* Stride 2: register 15 lands at offset 30, and offset 15 reads nothing,
     * which is what rules stride 1 out here rather than any convention. The
     * odd byte of each pair selects the same register, which is what the dump
     * shows -- offsets 30 and 31 both carry the mask register's value. */
    *reg = (offset >> 1) & (AP_I8237_REGISTERS - 1u);
    return true;
  }
  return false;
}

uint8_t ap_dma_read(ap_dma_t *dma, uint32_t address) {
  unsigned unit;
  unsigned reg;
  if (!ap_dma_decode(address, &unit, &reg)) {
    return 0u;
  }
  return ap_i8237_read(&dma->controller[unit], reg);
}

void ap_dma_write(ap_dma_t *dma, uint32_t address, uint8_t value) {
  unsigned unit;
  unsigned reg;
  if (!ap_dma_decode(address, &unit, &reg)) {
    return;
  }
  ap_i8237_write(&dma->controller[unit], reg, value);
}
