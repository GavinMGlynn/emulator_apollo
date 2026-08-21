#include "board/ap_atmap.h"

#include <string.h>

/* "which yields a 26-bit physical address" -- `[ADD]` §4.2.1.4. */
#define PHYSICAL_BITS 26u
#define PHYSICAL_MASK ((UINT32_C(1) << PHYSICAL_BITS) - 1u)

void ap_atmap_init(ap_atmap_t *map) {
  ap_atmap_init_entries(map, AP_ATMAP_ENTRIES);
}

void ap_atmap_init_entries(ap_atmap_t *map, unsigned entries) {
  memset(map, 0, sizeof *map);
  map->entries =
      entries > AP_ATMAP_ENTRIES_MAX ? AP_ATMAP_ENTRIES_MAX : entries;
}

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
  /* Wrapped in the *Series 3000/4000* map's size deliberately. A DMA index is
   * §4.2.1.4's, and that section describes the map every model has; a machine
   * with a larger one has more storage, not a wider DMA index. Widening this
   * would be inferring a bus behaviour from a memory map. */
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
  /* The **Series 3000/4000** region, which is what a caller with no map in hand
   * can be told. A DS5500's is twice as wide; `ap_atmap_decodes_to_entry` takes
   * the map and answers for the machine. Kept as the narrow answer rather than
   * widened to the largest, because a question asked without a machine should
   * not silently answer for the biggest one. */
  return address >= AP_ATMAP_BASE && address <= AP_ATMAP_LIMIT;
}

bool ap_atmap_decodes_to_entry(const ap_atmap_t *map, uint32_t address) {
  if (address < AP_ATMAP_BASE) {
    return false;
  }
  /* The whole region is entries -- see the header, and the diagnostic that
   * writes every word of it and reads each one back distinct. Kept as a
   * separate question from `in_range` even though the two now agree. */
  return (address - AP_ATMAP_BASE) < (map->entries * 2u);
}

/* Entry selected by a programmed access. Byte address to entry index; see the
 * header on why this is the assumed decode rather than a transcribed one. */
static unsigned entry_of(const ap_atmap_t *map, uint32_t address) {
  /* The machine's own count, so a DS5500's upper half is storage of its own
   * rather than an alias of its lower -- which is the fault `ap_atmap.h`
   * records the loaded diagnostic catching, and the reason the wider region
   * could not simply be placed. */
  const unsigned entries = map->entries == 0u ? AP_ATMAP_ENTRIES : map->entries;
  return (unsigned)(((address - AP_ATMAP_BASE) / 2u) & (entries - 1u));
}

uint16_t ap_atmap_read(const ap_atmap_t *map, uint32_t address) {
  return map->entry[entry_of(map, address)];
}

void ap_atmap_write(ap_atmap_t *map, uint32_t address, uint16_t value) {
  map->entry[entry_of(map, address)] = value;
}
