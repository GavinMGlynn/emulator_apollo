/* MC68030 translation table descriptors.
 * See ap_m68030_desc.h for the citations. */

#include "cpu/m68030/ap_m68030_desc.h"

/* The PAGE ADDRESS field is 24 bits and describes a 256-byte page at minimum,
 * so it is a base address shifted right by eight. */
#define PAGE_ADDRESS_SHIFT 8u

ap_m68030_desc_role_t ap_m68030_desc_role(ap_m68030_dt_t dt,
                                          bool in_page_table) {
  switch (dt) {
  case AP_M68030_DT_INVALID:
    return AP_M68030_ROLE_INVALID;

  case AP_M68030_DT_PAGE:
    /* "The page descriptor is a normal page descriptor when it resides in a
     * page table ... A page descriptor at a higher level is an early
     * termination page descriptor." Both end the search; they differ in how
     * much of the logical address becomes the offset, which is the walk's
     * business rather than this function's. */
    return in_page_table ? AP_M68030_ROLE_PAGE : AP_M68030_ROLE_EARLY_PAGE;

  case AP_M68030_DT_VALID_4BYTE:
  case AP_M68030_DT_VALID_8BYTE:
    /* The context-dependent case. In a pointer table these name the format of
     * the next table; in a page table the very same encoding is an indirect
     * descriptor pointing at a page descriptor. */
    return in_page_table ? AP_M68030_ROLE_INDIRECT : AP_M68030_ROLE_TABLE;
  }
  return AP_M68030_ROLE_INVALID;
}

uint32_t ap_m68030_desc_next_table_stride(ap_m68030_dt_t dt) {
  switch (dt) {
  case AP_M68030_DT_VALID_4BYTE:
    return 4; /* "must be long-word aligned" */
  case AP_M68030_DT_VALID_8BYTE:
    return 8; /* "must be quad-word aligned" */
  case AP_M68030_DT_INVALID:
  case AP_M68030_DT_PAGE:
    return 0;
  }
  return 0;
}

bool ap_m68030_desc_terminates(ap_m68030_desc_role_t role) {
  switch (role) {
  case AP_M68030_ROLE_INVALID:
  case AP_M68030_ROLE_PAGE:
  case AP_M68030_ROLE_EARLY_PAGE:
    return true;
  case AP_M68030_ROLE_TABLE:
  case AP_M68030_ROLE_INDIRECT:
    /* An indirect descriptor does not end the search: it redirects it to the
     * page descriptor it points at. */
    return false;
  }
  return true;
}

bool ap_m68030_desc_index_within_limit(uint16_t limit, bool lower_limit,
                                       uint32_t index) {
  /* LIMIT is a 15-bit field; anything above that is not part of it. */
  const uint32_t bound = limit & 0x7FFFu;
  return lower_limit ? (index >= bound) : (index <= bound);
}

uint32_t ap_m68030_desc_page_address(uint32_t page_address_field,
                                     uint8_t page_size_bits) {
  /* The field is 24 bits. */
  const uint32_t base = (page_address_field & UINT32_C(0x00FFFFFF))
                        << PAGE_ADDRESS_SHIFT;

  /* "The number of unused bits is equal to the PS field value in the TC
   * register minus eight." Those low bits of the field are not used, so the
   * base address is aligned to the page size rather than to 256 bytes. A page
   * size at or below the 256-byte minimum leaves the field untouched. */
  if (page_size_bits <= PAGE_ADDRESS_SHIFT) {
    return base;
  }
  const uint32_t alignment = UINT32_C(1) << page_size_bits;
  return base & ~(alignment - 1u);
}

void ap_m68030_search_reset(ap_m68030_search_t *search) {
  *search = (ap_m68030_search_t){0};
}

void ap_m68030_search_accumulate(ap_m68030_search_t *search, bool write_protect,
                                 bool supervisor, bool cache_inhibit) {
  search->write_protected |= write_protect;
  search->supervisor_only |= supervisor;
  search->cache_inhibited |= cache_inhibit;
}

void ap_m68030_search_fail_limit(ap_m68030_search_t *search) {
  search->limit_violation = true;
}

void ap_m68030_search_fail_invalid(ap_m68030_search_t *search) {
  search->invalid = true;
}

bool ap_m68030_search_permits_write(const ap_m68030_search_t *s) {
  /* "When WP is set, the MC68030 does not allow the logical address space
   * mapped by that descriptor to be written by any program (i.e., this
   * protection is absolute)." Absolute means supervisor does not override it. */
  return !s->write_protected && !s->invalid && !s->limit_violation;
}

bool ap_m68030_search_permits_access(const ap_m68030_search_t *s,
                                     bool supervisor_access) {
  if (s->invalid || s->limit_violation) {
    return false;
  }
  return supervisor_access || !s->supervisor_only;
}

bool ap_m68030_search_should_set_modified(const ap_m68030_search_t *s,
                                          bool write, bool read_modify_write,
                                          bool supervisor_access,
                                          bool modified_already) {
  /* "An access is considered to be a write for updating purposes if either the
   * R/W or RMC signal is low" -- so a read-modify-write counts as a write for
   * M-bit purposes even on its read half. */
  if (!write && !read_modify_write) {
    return false;
  }
  if (modified_already) {
    return false; /* "for which the M bit is zero" */
  }
  /* "except after a descriptor with the WP bit set is encountered, or after a
   * supervisor violation is encountered". */
  if (s->write_protected) {
    return false;
  }
  if (s->supervisor_only && !supervisor_access) {
    return false;
  }
  return !s->invalid && !s->limit_violation;
}
