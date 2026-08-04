/* The DS3000's DMA page registers. See ap_dmapage.h for what is and is not
 * claimed about them. */

#include "board/ap_dmapage.h"

#include <string.h>

void ap_dmapage_reset(ap_dmapage_t *pages) { memset(pages, 0, sizeof *pages); }

unsigned ap_dmapage_index(uint32_t address) {
  return (unsigned)(address & (AP_DMAPAGE_REGISTERS - 1u));
}

uint8_t ap_dmapage_read(const ap_dmapage_t *pages, uint32_t address) {
  return pages->page[ap_dmapage_index(address)];
}

void ap_dmapage_write(ap_dmapage_t *pages, uint32_t address, uint8_t value) {
  pages->page[ap_dmapage_index(address)] = value;
}
