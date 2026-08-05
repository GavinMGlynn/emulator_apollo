#include "board/ap_atmap.h"

#include <string.h>

/* "which yields a 26-bit physical address" -- `[ADD]` §4.2.1.4. */
#define PHYSICAL_BITS 26u
#define PHYSICAL_MASK ((UINT32_C(1) << PHYSICAL_BITS) - 1u)

void ap_atmap_init(ap_atmap_t *map) { memset(map, 0, sizeof *map); }

unsigned ap_atmap_reachable_entries(ap_atmap_transfer_t transfer) {
  switch (transfer) {
  case AP_ATMAP_TRANSFER_8BIT:
    /* "address bits <15:10> provide an index into the Address Translation Map;
     * they select one of the 64 entries contained within it." Six bits. */
    return 64u;
  case AP_ATMAP_TRANSFER_16BIT:
    /* "address bits <16:10> ... select one of the 128 entries". Seven bits. */
    return 128u;
  }
  return 0u;
}

unsigned ap_atmap_index(uint32_t dma_address, ap_atmap_transfer_t transfer) {
  /* Both cases start the index at bit 10 -- the page size is the same, only the
   * span differs -- so the shift is shared and the mask is not. */
  uint32_t raw = dma_address >> AP_ATMAP_PAGE_SHIFT;
  unsigned reachable = ap_atmap_reachable_entries(transfer);
  if (reachable == 0u) {
    return 0u;
  }
  return (unsigned)((AP_ATMAP_WINDOW_FIRST_ENTRY + (raw & (reachable - 1u))) &
                    (AP_ATMAP_ENTRIES - 1u));
}

uint32_t ap_atmap_offset(uint32_t dma_address, ap_atmap_transfer_t transfer) {
  switch (transfer) {
  case AP_ATMAP_TRANSFER_8BIT:
    /* "concatenated with the page offset (DMA address bits <9:0>)" */
    return dma_address & (AP_ATMAP_PAGE_SIZE - 1u);
  case AP_ATMAP_TRANSFER_16BIT:
    /* "concatenated with the page offset (DMA address bits <9:1>)".
     *
     * Nine bits, not ten, and bit 0 is not lost so much as never present: a
     * 16-bit DMA controller counts words and has no bit 0 to drive. Masking
     * <9:0> here instead would let an odd DMA address produce an odd physical
     * address, which this transfer width cannot express. */
    return dma_address & (AP_ATMAP_PAGE_SIZE - 2u);
  }
  return 0u;
}

uint32_t ap_atmap_translate(const ap_atmap_t *map, uint32_t dma_address,
                            ap_atmap_transfer_t transfer) {
  unsigned index = ap_atmap_index(dma_address, transfer);
  /* "The 16-bit Address Translation Map entry (a physical page number, bits
   * <25:10>) is concatenated with the page offset". */
  uint32_t page = (uint32_t)map->entry[index] << AP_ATMAP_PAGE_SHIFT;
  return (page | ap_atmap_offset(dma_address, transfer)) & PHYSICAL_MASK;
}

bool ap_atmap_in_range(uint32_t address) {
  return address >= AP_ATMAP_BASE && address <= AP_ATMAP_LIMIT;
}

bool ap_atmap_decodes_to_entry(uint32_t address) {
  if (!ap_atmap_in_range(address)) {
    return false;
  }
  /* The whole region is entries -- see the header, and the diagnostic that
   * writes every word of it and reads each one back distinct. Kept as a
   * separate question from `in_range` even though the two now agree. */
  return (address - AP_ATMAP_BASE) < (AP_ATMAP_ENTRIES * 2u);
}

/* Entry selected by a programmed access. Byte address to entry index; see the
 * header on why this is the assumed decode rather than a transcribed one. */
static unsigned entry_of(uint32_t address) {
  return (unsigned)(((address - AP_ATMAP_BASE) / 2u) & (AP_ATMAP_ENTRIES - 1u));
}

uint16_t ap_atmap_read(const ap_atmap_t *map, uint32_t address) {
  return map->entry[entry_of(address)];
}

void ap_atmap_write(ap_atmap_t *map, uint32_t address, uint16_t value) {
  map->entry[entry_of(address)] = value;
}
