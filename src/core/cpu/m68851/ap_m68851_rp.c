/* MC68851 root pointer registers. See ap_m68851_rp.h; Figure 6-1's bit
 * boundaries read from the page image. */

#include "cpu/m68851/ap_m68851_rp.h"

ap_m68851_rp_t ap_m68851_rp_decode(uint64_t value) {
  return (ap_m68851_rp_t){
      .lower_limit = (value & (UINT64_C(1) << 63)) != 0u,
      .limit = (unsigned)((value >> 48) & 0x7FFFu),
      .shared_globally = (value & (UINT64_C(1) << 41)) != 0u,
      .descriptor_type =
          (ap_m68851_descriptor_type_t)((value >> 32) & 0x3u),
      .table_address = (uint32_t)(value & 0xFFFFFFF0u),
      .software_bits = (unsigned)(value & 0xFu),
  };
}

uint64_t ap_m68851_rp_encode(const ap_m68851_rp_t *rp) {
  uint64_t value = 0;
  if (rp->lower_limit) {
    value |= UINT64_C(1) << 63;
  }
  value |= ((uint64_t)(rp->limit & 0x7FFFu)) << 48;
  if (rp->shared_globally) {
    value |= UINT64_C(1) << 41;
  }
  value |= ((uint64_t)rp->descriptor_type & 0x3u) << 32;
  value |= (uint64_t)(rp->table_address & 0xFFFFFFF0u);
  value |= (uint64_t)(rp->software_bits & 0xFu);
  /* Bits 47-42 and 40-34 stay clear: "all other unused bits of the root pointer
   * registers must be zero." */
  return value;
}

unsigned ap_m68851_rp_descriptor_bytes(const ap_m68851_rp_t *rp) {
  switch (rp->descriptor_type) {
  case AP_M68851_DT_VALID_4_BYTE:
    return 4u;
  case AP_M68851_DT_VALID_8_BYTE:
    return 8u;
  case AP_M68851_DT_INVALID:
  case AP_M68851_DT_PAGE_DESCRIPTOR:
    /* Neither names a table, so there is no index to scale. */
    return 0u;
  }
  return 0u;
}

bool ap_m68851_rp_index_within_limit(const ap_m68851_rp_t *rp,
                                     unsigned index) {
  /* Both comparisons are inclusive: "less than or equal to" and "greater than
   * or equal to". An exclusive bound would make $7FFF fail to suppress the
   * upper check, which the manual says it does. */
  return rp->lower_limit ? (index >= rp->limit) : (index <= rp->limit);
}

bool ap_m68851_rp_limit_suppressed(const ap_m68851_rp_t *rp) {
  /* "Either setting L/U to zero and setting the limit field to all ones ($7FFF)
   * or by setting L/U to one and clearing the limit field ($8000)." A table
   * index is at most fifteen bits, so an upper bound of $7FFF admits every
   * index and a lower bound of zero rejects none. */
  return rp->lower_limit ? (rp->limit == 0u) : (rp->limit == 0x7FFFu);
}

bool ap_m68851_rp_loadable_by_pmove(const ap_m68851_rp_t *rp) {
  return rp->descriptor_type != AP_M68851_DT_INVALID;
}

bool ap_m68851_rp_limit_applies(const ap_m68851_rp_t *rp,
                                bool function_code_lookup) {
  /* The exception first: "if the DT field of a root pointer is set to $1, the
   * MC68851 performs a limit check regardless of the state of the FCL bit."
   * Which is the case that most needs it -- a page descriptor walks no table,
   * so the limit is the only bound on the mapping it creates. */
  if (rp->descriptor_type == AP_M68851_DT_PAGE_DESCRIPTOR) {
    return true;
  }
  return !function_code_lookup;
}
