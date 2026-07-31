/* MC68030 translation control register. See ap_m68030_tc.h for the citations
 * and for why this register's bit layout is trusted where TT's was not. */

#include "cpu/m68030/ap_m68030_tc.h"

#include <stddef.h> /* NULL */

/* The smallest page the PS field can encode is 256 bytes, so any PS below 8 is
 * one of the reserved combinations. */
#define PS_MIN_BITS 8u
#define PS_MAX_BITS 15u

static uint8_t field(uint32_t value, unsigned shift) {
  return (uint8_t)((value >> shift) & 0x0Fu);
}

ap_m68030_tc_t ap_m68030_tc_decode(uint32_t value) {
  ap_m68030_tc_t tc = {
      .enable = (value & UINT32_C(0x80000000)) != 0,        /* bit 31 */
      .supervisor_root = (value & UINT32_C(0x02000000)) != 0, /* bit 25 */
      .function_code_lookup = (value & UINT32_C(0x01000000)) != 0, /* bit 24 */
      .page_size_bits = field(value, 20),
      .initial_shift = field(value, 16),
  };
  /* TIA is the highest level table and occupies the highest field. */
  for (unsigned level = 0; level < AP_M68030_TC_LEVELS; level++) {
    tc.table_index[level] = field(value, 12u - level * 4u);
  }
  return tc;
}

uint32_t ap_m68030_tc_page_size(const ap_m68030_tc_t *tc) {
  if (tc->page_size_bits < PS_MIN_BITS || tc->page_size_bits > PS_MAX_BITS) {
    return 0; /* reserved encoding */
  }
  return UINT32_C(1) << tc->page_size_bits;
}

bool ap_m68030_tc_is_consistent(const ap_m68030_tc_t *tc, uint32_t *total) {
  uint32_t sum = 0;

  /* "The TIx fields are added together **until a zero field is reached**" --
   * so a zero terminates the sum, and any non-zero field beyond it does not
   * contribute. Summing all four unconditionally would accept configurations
   * the hardware rejects. */
  for (unsigned level = 0; level < AP_M68030_TC_LEVELS; level++) {
    if (tc->table_index[level] == 0) {
      break;
    }
    sum += tc->table_index[level];
  }

  sum += tc->page_size_bits;
  sum += tc->initial_shift;

  if (total != NULL) {
    *total = sum;
  }

  /* A reserved page size is independently a configuration exception, so it
   * fails here even in the unlikely event the arithmetic still reaches 32. */
  return sum == 32u && ap_m68030_tc_page_size(tc) != 0;
}

ap_m68030_tc_split_t ap_m68030_tc_split(const ap_m68030_tc_t *tc,
                                        uint32_t address) {
  ap_m68030_tc_split_t split = {0};

  /* Bits consumed so far, counting down from the top of the 32-bit address.
   * The initial shift comes off first: those bits "are ignored during table
   * search operations". */
  unsigned consumed = tc->initial_shift;

  for (unsigned level = 0; level < AP_M68030_TC_LEVELS; level++) {
    const unsigned width = tc->table_index[level];
    if (width == 0) {
      break; /* "the search is over" */
    }
    /* Guard against a TC that never passed the consistency check: a caller may
     * still split with one, and shifting by >= 32 is undefined behaviour. */
    if (consumed + width > 32u) {
      break;
    }
    const unsigned shift = 32u - consumed - width;
    split.index[level] = (address >> shift) & ((UINT32_C(1) << width) - 1u);
    split.levels++;
    consumed += width;
  }

  const uint32_t page_size = ap_m68030_tc_page_size(tc);
  split.page_offset = (page_size == 0) ? 0 : (address & (page_size - 1u));
  return split;
}

uint32_t ap_m68030_tc_encode(const ap_m68030_tc_t *tc) {
  uint32_t value = 0;
  if (tc->enable) {
    value |= UINT32_C(1) << 31;
  }
  if (tc->supervisor_root) {
    value |= UINT32_C(1) << 25;
  }
  if (tc->function_code_lookup) {
    value |= UINT32_C(1) << 24;
  }
  value |= ((uint32_t)tc->page_size_bits & 0xFu) << 20;
  value |= ((uint32_t)tc->initial_shift & 0xFu) << 16;
  for (unsigned level = 0; level < AP_M68030_TC_LEVELS; level++) {
    /* TIA is the *highest* nibble of the low half, TID the lowest. */
    const unsigned shift = 12u - 4u * level;
    value |= ((uint32_t)tc->table_index[level] & 0xFu) << shift;
  }
  return value;
}
