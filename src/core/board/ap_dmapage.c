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

/* `002398-04` p. 12-25, in the order the handbook prints it. Channel 4 is the
 * cascade and the handbook lists no register for it, which is why this is a
 * table with a hole rather than eight entries. */
unsigned ap_dmapage_index_for_channel(unsigned channel) {
  static const unsigned by_channel[AP_DMAPAGE_CHANNELS] = {
      [0] = 0x7u, [1] = 0x3u, [2] = 0x1u, [3] = 0x2u,
      [AP_DMAPAGE_CASCADE_CHANNEL] = AP_DMAPAGE_REGISTERS,
      [5] = 0xBu, [6] = 0x9u, [7] = 0xAu,
  };
  if (channel >= AP_DMAPAGE_CHANNELS) {
    return AP_DMAPAGE_REGISTERS;
  }
  return by_channel[channel];
}

uint8_t ap_dmapage_channel_page(const ap_dmapage_t *pages, unsigned channel) {
  const unsigned index = ap_dmapage_index_for_channel(channel);
  if (index >= AP_DMAPAGE_REGISTERS) {
    return 0u;
  }
  return pages->page[index];
}

uint32_t ap_dmapage_physical(const ap_dmapage_t *pages, unsigned channel,
                             uint16_t offset) {
  /* "Each byte is loaded with the high 8 physical address bits", so the page
   * sits above the controller's sixteen and the result is the twenty-four the
   * handbook says the system has. */
  return ((uint32_t)ap_dmapage_channel_page(pages, channel) << 16) | offset;
}
