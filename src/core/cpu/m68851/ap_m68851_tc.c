/* MC68851 translation control register. See ap_m68851_tc.h; Figure 6-3's bit
 * boundaries read from the page image. */

#include "cpu/m68851/ap_m68851_tc.h"

ap_m68851_tc_t ap_m68851_tc_decode(uint32_t value) {
  ap_m68851_tc_t tc = {
      .enable = (value & 0x80000000u) != 0u,
      .supervisor_root_pointer_enable = (value & 0x02000000u) != 0u,
      .function_code_lookup = (value & 0x01000000u) != 0u,
      .page_size = (unsigned)((value >> 20) & 0xFu),
      .initial_shift = (unsigned)((value >> 16) & 0xFu),
  };
  for (unsigned i = 0; i < 4u; i++) {
    /* TIA at 15-12 down to TID at 3-0: the first level searched is the most
     * significant nibble, which matches the order the address is consumed. */
    tc.table_index[i] = (unsigned)((value >> (12u - 4u * i)) & 0xFu);
  }
  return tc;
}

uint32_t ap_m68851_tc_encode(const ap_m68851_tc_t *tc) {
  uint32_t value = 0;
  if (tc->enable) {
    value |= 0x80000000u;
  }
  if (tc->supervisor_root_pointer_enable) {
    value |= 0x02000000u;
  }
  if (tc->function_code_lookup) {
    value |= 0x01000000u;
  }
  value |= (tc->page_size & 0xFu) << 20;
  value |= (tc->initial_shift & 0xFu) << 16;
  for (unsigned i = 0; i < 4u; i++) {
    value |= (tc->table_index[i] & 0xFu) << (12u - 4u * i);
  }
  /* Bits 30-26 stay clear: unimplemented, "read as zeros". */
  return value & AP_M68851_TC_IMPLEMENTED_MASK;
}

unsigned ap_m68851_tc_bit_total(const ap_m68851_tc_t *tc) {
  unsigned total = tc->initial_shift + tc->page_size;
  for (unsigned i = 0; i < 4u; i++) {
    total += tc->table_index[i];
  }
  return total;
}

ap_m68851_tc_status_t ap_m68851_tc_check(const ap_m68851_tc_t *tc) {
  /* Checked first: a page size below 256 bytes is refused whatever the sum
   * comes to, and reporting the sum for it would name the wrong fault. */
  if ((tc->page_size & 0x8u) == 0u) {
    return AP_M68851_TC_PAGE_SIZE_TOO_SMALL;
  }
  if (ap_m68851_tc_bit_total(tc) != 32u) {
    return AP_M68851_TC_INCONSISTENT;
  }
  return AP_M68851_TC_OK;
}

uint32_t ap_m68851_tc_page_bytes(const ap_m68851_tc_t *tc) {
  return (uint32_t)1u << tc->page_size;
}

unsigned ap_m68851_tc_levels(const ap_m68851_tc_t *tc) {
  for (unsigned i = 0; i < 4u; i++) {
    if (tc->table_index[i] == 0u) {
      return i;
    }
  }
  return 4u;
}
