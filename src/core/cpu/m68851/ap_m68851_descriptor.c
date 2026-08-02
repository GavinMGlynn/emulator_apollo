/* MC68851 address translation descriptors. See ap_m68851_descriptor.h; all
 * formats read from the page images of Figures 5-12 through 5-20. */

#include "cpu/m68851/ap_m68851_descriptor.h"

ap_m68851_descriptor_kind_t
ap_m68851_descriptor_kind(ap_m68851_descriptor_type_t dt,
                          ap_m68851_search_state_t state) {
  switch (dt) {
  case AP_M68851_DT_INVALID:
    /* Invalid in all three columns. */
    return AP_M68851_DESC_INVALID;

  case AP_M68851_DT_PAGE_DESCRIPTOR:
    /* Type-2 only while index fields remain: an early termination leaves levels
     * below that a limit can still bound. Once they are exhausted -- or once an
     * indirect descriptor has been followed -- there is nothing left to bound
     * and the descriptor is type-1. */
    return (state == AP_M68851_SEARCH_TI_FIELDS_REMAIN)
               ? AP_M68851_DESC_PAGE_TYPE_2
               : AP_M68851_DESC_PAGE_TYPE_1;

  case AP_M68851_DT_VALID_4_BYTE:
  case AP_M68851_DT_VALID_8_BYTE:
    switch (state) {
    case AP_M68851_SEARCH_TI_FIELDS_REMAIN:
      return AP_M68851_DESC_TABLE;
    case AP_M68851_SEARCH_TI_FIELDS_EXHAUSTED:
      return AP_M68851_DESC_INDIRECT;
    case AP_M68851_SEARCH_INDIRECT_DESCRIPTOR_SEEN:
      /* Illegal: an indirect descriptor pointing at another. "Treated as the
       * 'invalid' type by the MC68851", which is what stops the chain rather
       * than following it forever. */
      return AP_M68851_DESC_INVALID;
    }
    return AP_M68851_DESC_INVALID;
  }
  return AP_M68851_DESC_INVALID;
}

bool ap_m68851_descriptor_is_illegal(ap_m68851_descriptor_type_t dt,
                                     ap_m68851_search_state_t state) {
  return state == AP_M68851_SEARCH_INDIRECT_DESCRIPTOR_SEEN &&
         (dt == AP_M68851_DT_VALID_4_BYTE || dt == AP_M68851_DT_VALID_8_BYTE);
}

unsigned ap_m68851_descriptor_next_width(ap_m68851_descriptor_type_t dt) {
  switch (dt) {
  case AP_M68851_DT_VALID_4_BYTE:
    return 4u;
  case AP_M68851_DT_VALID_8_BYTE:
    return 8u;
  case AP_M68851_DT_INVALID:
  case AP_M68851_DT_PAGE_DESCRIPTOR:
    return 0u;
  }
  return 0u;
}

/* The long formats share their second long word's layout below `SG`, so the
 * common part is decoded once. Bits 47-45 RAL, 44-42 WAL, 41 SG, 40 S, then the
 * per-format middle, then 35 U, 34 WP, 33-32 DT. */
static void decode_long_upper(uint64_t value, ap_m68851_descriptor_t *out) {
  out->read_access_level = (unsigned)((value >> 45) & 0x7u);
  out->write_access_level = (unsigned)((value >> 42) & 0x7u);
  out->shared_globally = (value & (UINT64_C(1) << 41)) != 0u;
  out->supervisor = (value & (UINT64_C(1) << 40)) != 0u;
  out->used = (value & (UINT64_C(1) << 35)) != 0u;
  out->write_protect = (value & (UINT64_C(1) << 34)) != 0u;
  out->dt = (ap_m68851_descriptor_type_t)((value >> 32) & 0x3u);
}

/* The limit and its direction bit, bits 63-48, on the two formats that carry
 * them. Same placement as a root pointer's, which is what lets one limit check
 * serve both. */
static void decode_limit(uint64_t value, ap_m68851_descriptor_t *out) {
  out->lower_limit = (value & (UINT64_C(1) << 63)) != 0u;
  out->limit = (unsigned)((value >> 48) & 0x7FFFu);
}

ap_m68851_descriptor_t ap_m68851_short_table_descriptor(uint32_t value) {
  /* Figure 5-12: table address PA31-PA4 at bits 31-4, then U, WP, DT. No
   * access levels and no sharing bit -- that is what "short" costs. */
  return (ap_m68851_descriptor_t){
      .address = value & 0xFFFFFFF0u,
      .used = (value & 0x8u) != 0u,
      .write_protect = (value & 0x4u) != 0u,
      .dt = (ap_m68851_descriptor_type_t)(value & 0x3u),
  };
}

ap_m68851_descriptor_t ap_m68851_long_table_descriptor(uint64_t value) {
  /* Figure 5-13. Bits 39-36 are drawn as zeros: a table descriptor has no
   * gate, cache-inhibit, lock or modified bit, because none of them describes
   * a table -- they describe a page. */
  ap_m68851_descriptor_t out = {0};
  decode_limit(value, &out);
  decode_long_upper(value, &out);
  out.address = (uint32_t)(value & 0xFFFFFFF0u);
  return out;
}

ap_m68851_descriptor_t ap_m68851_short_page_descriptor(uint32_t value) {
  /* Figure 5-14, which serves both type-1 and type-2: "the type-1 and type-2
   * short format descriptors are identical." Page address PA31-PA8 at bits
   * 31-8, so a page frame is 256-byte aligned -- the smallest page size. */
  return (ap_m68851_descriptor_t){
      .address = value & 0xFFFFFF00u,
      .gate = (value & 0x80u) != 0u,
      .cache_inhibit = (value & 0x40u) != 0u,
      .lock = (value & 0x20u) != 0u,
      .modified = (value & 0x10u) != 0u,
      .used = (value & 0x8u) != 0u,
      .write_protect = (value & 0x4u) != 0u,
      .dt = (ap_m68851_descriptor_type_t)(value & 0x3u),
  };
}

ap_m68851_descriptor_t ap_m68851_long_page_descriptor(uint64_t value,
                                                      bool type_2) {
  /* Figures 5-15 and 5-16. The middle of the second long word carries the four
   * page attributes a table descriptor has no use for: G at 39, CI at 38, L at
   * 37, M at 36. */
  ap_m68851_descriptor_t out = {0};
  if (type_2) {
    /* "The only difference in the long format of the type-1 and type-2 page
     * descriptors is the presence of the LIMIT field and L/U bit." On a type-1
     * that word is drawn UNUSED, so it is left alone rather than decoded. */
    decode_limit(value, &out);
  }
  decode_long_upper(value, &out);
  out.gate = (value & (UINT64_C(1) << 39)) != 0u;
  out.cache_inhibit = (value & (UINT64_C(1) << 38)) != 0u;
  out.lock = (value & (UINT64_C(1) << 37)) != 0u;
  out.modified = (value & (UINT64_C(1) << 36)) != 0u;
  out.address = (uint32_t)(value & 0xFFFFFF00u);
  return out;
}

ap_m68851_descriptor_t ap_m68851_short_indirect_descriptor(uint32_t value) {
  /* Figure 5-17: descriptor address PA31-PA2 at bits 31-2, DT at 1-0. Four-byte
   * aligned, because what it points at is a descriptor and not a page. */
  return (ap_m68851_descriptor_t){
      .address = value & 0xFFFFFFFCu,
      .dt = (ap_m68851_descriptor_type_t)(value & 0x3u),
  };
}

ap_m68851_descriptor_t ap_m68851_long_indirect_descriptor(uint64_t value) {
  /* Figure 5-18: everything unused but `DT` at 33-32 and the descriptor address
   * at 31-2. An indirect descriptor carries no protection of its own -- the
   * descriptor it names carries it, which is the point of the indirection. */
  return (ap_m68851_descriptor_t){
      .address = (uint32_t)(value & 0xFFFFFFFCu),
      .dt = (ap_m68851_descriptor_type_t)((value >> 32) & 0x3u),
  };
}
