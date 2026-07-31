/* MC68030 MMU status register. See ap_m68030_mmusr.h for the citations, for the
 * two-source argument behind the bit layout, and for why "undefined" is zero. */

#include "cpu/m68030/ap_m68030_mmusr.h"

static uint16_t bit_at(bool set, unsigned position) {
  return set ? (uint16_t)(1u << position) : (uint16_t)0u;
}

uint16_t ap_m68030_mmusr_pack(const ap_m68030_mmusr_t *mmusr) {
  return (uint16_t)(bit_at(mmusr->bus_error, AP_M68030_MMUSR_B_BIT) |
                    bit_at(mmusr->limit_violation, AP_M68030_MMUSR_L_BIT) |
                    bit_at(mmusr->supervisor_violation, AP_M68030_MMUSR_S_BIT) |
                    bit_at(mmusr->write_protected, AP_M68030_MMUSR_W_BIT) |
                    bit_at(mmusr->invalid, AP_M68030_MMUSR_I_BIT) |
                    bit_at(mmusr->modified, AP_M68030_MMUSR_M_BIT) |
                    bit_at(mmusr->transparent, AP_M68030_MMUSR_T_BIT) |
                    (uint16_t)(mmusr->levels & AP_M68030_MMUSR_N_MASK));
}

ap_m68030_mmusr_t ap_m68030_mmusr_unpack(uint16_t word) {
  return (ap_m68030_mmusr_t){
      .bus_error = ((word >> AP_M68030_MMUSR_B_BIT) & 1u) != 0u,
      .limit_violation = ((word >> AP_M68030_MMUSR_L_BIT) & 1u) != 0u,
      .supervisor_violation = ((word >> AP_M68030_MMUSR_S_BIT) & 1u) != 0u,
      .write_protected = ((word >> AP_M68030_MMUSR_W_BIT) & 1u) != 0u,
      .invalid = ((word >> AP_M68030_MMUSR_I_BIT) & 1u) != 0u,
      .modified = ((word >> AP_M68030_MMUSR_M_BIT) & 1u) != 0u,
      .transparent = ((word >> AP_M68030_MMUSR_T_BIT) & 1u) != 0u,
      .levels = (uint8_t)(word & AP_M68030_MMUSR_N_MASK),
  };
}

ap_m68030_mmusr_t ap_m68030_mmusr_probe_atc(const ap_m68030_atc_t *atc,
                                            uint8_t function_code,
                                            uint32_t address,
                                            uint8_t page_size_bits,
                                            bool transparent_match) {
  /* L, S and N are "cleared to zero" for a level 0 probe: all three describe a
   * table search, and this form never performs one. Zero-initialising says that
   * once rather than three times. */
  ap_m68030_mmusr_t mmusr = {0};

  /* "This bit is set if a match occurred in either (or both) of the transparent
   * translation registers (TT0 or TT1). If the T bit is set, all remaining
   * MMUSR bits are undefined." Nothing else is reported, so nothing else is
   * filled in. */
  if (transparent_match) {
    mmusr.transparent = true;
    return mmusr;
  }

  const ap_m68030_atc_result_t found = ap_m68030_atc_lookup(
      atc, function_code, address, page_size_bits, false, false);

  if (found.index < 0) {
    /* "The I bit is set if the translation for the specified logical address is
     * not resident in the ATC". W and M are undefined once it is. */
    mmusr.invalid = true;
    return mmusr;
  }

  const ap_m68030_atc_entry_t *entry = &atc->entry[found.index];

  /* "This bit is set if the bus error bit is set in the ATC entry for the
   * specified logical address", and I is set "... or if the B bit of the
   * corresponding ATC entry is set". */
  mmusr.bus_error = entry->bus_error;
  mmusr.invalid = entry->bus_error;
  if (mmusr.invalid) {
    return mmusr;
  }

  mmusr.write_protected = entry->write_protect;
  mmusr.modified = entry->modified;
  return mmusr;
}

ap_m68030_mmusr_t ap_m68030_mmusr_from_search(
    const ap_m68030_walk_result_t *result, uint8_t function_code) {
  /* T "is set to zero" for a level 1-7 probe, which zero-initialising gives. */
  ap_m68030_mmusr_t mmusr = {0};

  mmusr.bus_error = result->bus_error;
  mmusr.limit_violation = result->search.limit_violation;

  /* "This 3-bit field contains the actual number of tables accessed during the
   * search." */
  mmusr.levels = (uint8_t)(result->levels_walked & AP_M68030_MMUSR_N_MASK);

  /* "The I bit is set if the DT field of a table or a page descriptor
   * encountered during the search is set to invalid or if either the B or L
   * bits of the MMUSR are set during the table search." So I is deliberately
   * broader than "found an invalid descriptor". */
  mmusr.invalid =
      result->search.invalid || mmusr.bus_error || mmusr.limit_violation;
  if (mmusr.invalid) {
    /* S, W and M are all "undefined if the I bit is set". */
    return mmusr;
  }

  /* "This bit is set if the S bit of a long format table descriptor or long
   * format page descriptor encountered during the search is set, and the FC2
   * bit of the function code specified by the PTEST instruction is not equal to
   * one." FC2 set is a supervisor function code, so probing a supervisor-only
   * tree from supervisor state is not a violation -- S reports the *access*
   * against the tree, not the tree alone. */
  const bool fc2 = (function_code & 4u) != 0u;
  mmusr.supervisor_violation = result->search.supervisor_only && !fc2;

  mmusr.write_protected = result->search.write_protected;
  mmusr.modified = result->page_modified;
  return mmusr;
}
