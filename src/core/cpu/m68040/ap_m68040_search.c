/* MC68040 table search: Figures 3-8 and 3-9. See the header for why the
 * concatenation widths are worth stating as identities. */

#include "cpu/m68040/ap_m68040_search.h"

unsigned ap_m68040_root_index(uint32_t logical_address) {
  return (unsigned)((logical_address >> 25) & 0x7Fu); /* bits 31-25 */
}

unsigned ap_m68040_pointer_index(uint32_t logical_address) {
  return (unsigned)((logical_address >> 18) & 0x7Fu); /* bits 24-18 */
}

unsigned ap_m68040_page_index(uint32_t logical_address,
                              ap_m68040_page_size_t page_size) {
  /* Six bits at 4K (bits 17-12) and five at 8K (bits 17-13): a larger page
   * needs fewer descriptors to cover the same block. */
  return (page_size == AP_M68040_PAGE_8K)
             ? (unsigned)((logical_address >> 13) & 0x1Fu)
             : (unsigned)((logical_address >> 12) & 0x3Fu);
}

uint32_t ap_m68040_page_offset(uint32_t logical_address,
                               ap_m68040_page_size_t page_size) {
  return logical_address & (ap_m68040_page_bytes(page_size) - 1u);
}

ap_m68040_search_result_t
ap_m68040_search(const ap_m68040_search_config_t *config,
                 uint32_t logical_address) {
  ap_m68040_search_result_t out = {0};

  /* Level one. The root pointer's low nine bits "must be zero", and indexing
   * scales by four because a descriptor is a long word. */
  uint32_t address = (config->root_pointer & AP_M68040_ROOT_TABLE_MASK) |
                     (ap_m68040_root_index(logical_address) * 4u);
  uint32_t raw = 0;
  if (!config->fetch(config->fetch_context, address, &raw)) {
    out.status = AP_M68040_SEARCH_BUS_ERROR;
    return out;
  }
  out.fetches++;

  const ap_m68040_table_descriptor_t root = ap_m68040_root_descriptor(raw);
  if (root.type == AP_M68040_UDT_INVALID) {
    out.status = AP_M68040_SEARCH_INVALID;
    return out;
  }
  out.write_protect = out.write_protect || root.write_protect;

  /* Level two. "The seven bits of a logical address PI field are multiplied by
   * 4 ... and concatenated with the fetched root-level descriptor's upper 23
   * bits." */
  address = root.table_address | (ap_m68040_pointer_index(logical_address) * 4u);
  if (!config->fetch(config->fetch_context, address, &raw)) {
    out.status = AP_M68040_SEARCH_BUS_ERROR;
    return out;
  }
  out.fetches++;

  const ap_m68040_table_descriptor_t pointer =
      ap_m68040_pointer_descriptor(raw, config->page_size);
  if (pointer.type == AP_M68040_UDT_INVALID) {
    out.status = AP_M68040_SEARCH_INVALID;
    return out;
  }
  out.write_protect = out.write_protect || pointer.write_protect;

  /* Level three. */
  address = pointer.table_address |
            (ap_m68040_page_index(logical_address, config->page_size) * 4u);
  if (!config->fetch(config->fetch_context, address, &raw)) {
    out.status = AP_M68040_SEARCH_BUS_ERROR;
    return out;
  }
  out.fetches++;

  ap_m68040_page_descriptor_t page =
      ap_m68040_page_descriptor(raw, config->page_size);

  if (page.type == AP_M68040_PDT_INDIRECT) {
    /* "Bits 31-2 contain the physical address of the page descriptor." One
     * indirection only: "this encoding is invalid for a page descriptor pointed
     * to by an indirect descriptor", so a chain terminates rather than
     * looping -- the same rule the 68851 states from the other side. */
    if (!config->fetch(config->fetch_context, page.address, &raw)) {
      out.status = AP_M68040_SEARCH_BUS_ERROR;
      return out;
    }
    out.fetches++;
    out.indirect = true;
    page = ap_m68040_page_descriptor(raw, config->page_size);
    if (page.type != AP_M68040_PDT_RESIDENT) {
      out.status = AP_M68040_SEARCH_INVALID;
      return out;
    }
  } else if (page.type != AP_M68040_PDT_RESIDENT) {
    out.status = AP_M68040_SEARCH_INVALID;
    return out;
  }

  out.status = AP_M68040_SEARCH_RESIDENT;
  out.physical_address =
      page.address | ap_m68040_page_offset(logical_address, config->page_size);
  /* "Setting the W-bit in a table descriptor write protects all pages accessed
   * with that descriptor", so the leaf's bit is folded into what the path
   * already carried rather than replacing it. */
  out.write_protect = out.write_protect || page.write_protect;
  /* `S` "identifies a pointer table **or a page** as a supervisor-only table or
   * page", so it accumulates the same way. The table descriptors have no `S`
   * field of their own in Figure 3-11, so on this part only the page carries
   * it -- but the result keeps the accumulating shape, because the manual's
   * wording is about the tree and a future part may place it higher. */
  out.supervisor = out.supervisor || page.supervisor;
  out.cache_mode = page.cache_mode;
  out.modified = page.modified;
  out.global = page.global;
  out.user_attribute_0 = page.user_attribute_0;
  out.user_attribute_1 = page.user_attribute_1;
  return out;
}
