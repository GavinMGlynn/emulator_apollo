/* MC68030 transparent translation. See ap_m68030_tt.h for the citations. */

#include "cpu/m68030/ap_m68030_tt.h"

#include <stddef.h> /* NULL: either register may be absent */

/* "When a bit in a mask field is set, the corresponding bit of the base
 * function code or logical base address is ignored in the function code and
 * address comparison." */
static bool masked_equal(uint8_t a, uint8_t b, uint8_t mask) {
  return ((a ^ b) & (uint8_t)~mask) == 0;
}

bool ap_m68030_tt_matches(const ap_m68030_tt_t *tt,
                          const ap_m68030_access_t *access) {
  if (tt == NULL || !tt->enabled) {
    return false; /* "A disabled TTx register is completely ignored." */
  }

  /* "The read/write mask bit (RWM) must be set for transparent translation of
   * addresses used by instructions that execute read-modify-write operations.
   * Otherwise, neither the read nor write portions of read-modify-write
   * operations are mapped transparently with the TTx registers, **regardless of
   * the function code and address bits** for the individual cycles within a
   * read-modify-write operation."
   *
   * Checked before the address comparison because the manual makes it override
   * that comparison rather than refine it. An RMW cycle through a register with
   * RWM clear does not match even when every other field would. */
  if (access->read_modify_write && !tt->ignore_read_write) {
    return false;
  }

  /* "Each TTx register can specify read accesses or write accesses as
   * transparent. In that case, the internal read/write signal must match the
   * R/W bit in the TTx register for the match to occur." */
  if (!tt->ignore_read_write && access->read != tt->read_transparent) {
    return false;
  }

  /* "the function code and the eight high-order bits of the address are
   * compared to the block of addresses defined by TT0 and TT1." */
  const uint8_t address_high = (uint8_t)(access->address >> 24);
  return masked_equal(address_high, tt->logical_base, tt->logical_mask) &&
         masked_equal((uint8_t)(access->function_code & 0x07u),
                      (uint8_t)(tt->fc_base & 0x07u),
                      (uint8_t)(tt->fc_mask & 0x07u));
}

ap_m68030_tt_result_t ap_m68030_tt_translate(const ap_m68030_tt_t *tt0,
                                             const ap_m68030_tt_t *tt1,
                                             const ap_m68030_access_t *access) {
  const bool hit0 = ap_m68030_tt_matches(tt0, access);
  const bool hit1 = ap_m68030_tt_matches(tt1, access);

  ap_m68030_tt_result_t result = {0};
  if (!hit0 && !hit1) {
    return result; /* falls through to the translation tables */
  }

  result.transparent = true;
  /* "Logical addresses in a transparently translated block are used as physical
   * addresses, without modification and without protection checking." */
  result.physical = access->address;
  /* "If both registers match, the CI bits are ORed together to generate the
   * CIOUT signal." */
  result.cache_inhibit = (hit0 && tt0->cache_inhibit) ||
                         (hit1 && tt1->cache_inhibit);
  return result;
}

/* The register image PMOVE moves, from the PRM's Figure 1-9. See the header for
 * why this is a transcription rather than a reconstruction, and for the sense of
 * the two R/W bits. */
uint32_t ap_m68030_tt_pack(const ap_m68030_tt_t *tt) {
  uint32_t word = 0;
  word |= (uint32_t)tt->logical_base << AP_M68030_TT_ADDRESS_BASE_SHIFT;
  word |= (uint32_t)tt->logical_mask << AP_M68030_TT_ADDRESS_MASK_SHIFT;
  if (tt->enabled) {
    word |= UINT32_C(1) << AP_M68030_TT_E_BIT;
  }
  if (tt->cache_inhibit) {
    word |= UINT32_C(1) << AP_M68030_TT_CI_BIT;
  }
  /* "1 = Only read accesses permitted." */
  if (tt->read_transparent) {
    word |= UINT32_C(1) << AP_M68030_TT_RW_BIT;
  }
  /* "1 = R/W field ignored." */
  if (tt->ignore_read_write) {
    word |= UINT32_C(1) << AP_M68030_TT_RWM_BIT;
  }
  word |= (uint32_t)(tt->fc_base & AP_M68030_TT_FC_FIELD_MASK)
          << AP_M68030_TT_FC_BASE_SHIFT;
  word |= (uint32_t)(tt->fc_mask & AP_M68030_TT_FC_FIELD_MASK)
          << AP_M68030_TT_FC_MASK_SHIFT;
  return word;
}

ap_m68030_tt_t ap_m68030_tt_unpack(uint32_t word) {
  return (ap_m68030_tt_t){
      .logical_base =
          (uint8_t)((word >> AP_M68030_TT_ADDRESS_BASE_SHIFT) & 0xFFu),
      .logical_mask =
          (uint8_t)((word >> AP_M68030_TT_ADDRESS_MASK_SHIFT) & 0xFFu),
      .fc_base = (uint8_t)((word >> AP_M68030_TT_FC_BASE_SHIFT) &
                           AP_M68030_TT_FC_FIELD_MASK),
      .fc_mask = (uint8_t)((word >> AP_M68030_TT_FC_MASK_SHIFT) &
                           AP_M68030_TT_FC_FIELD_MASK),
      .enabled = ((word >> AP_M68030_TT_E_BIT) & 1u) != 0u,
      .cache_inhibit = ((word >> AP_M68030_TT_CI_BIT) & 1u) != 0u,
      .read_transparent = ((word >> AP_M68030_TT_RW_BIT) & 1u) != 0u,
      .ignore_read_write = ((word >> AP_M68030_TT_RWM_BIT) & 1u) != 0u,
  };
}
