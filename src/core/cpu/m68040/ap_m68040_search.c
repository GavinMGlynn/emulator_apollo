/* MC68040 table search: Figures 3-8 and 3-9. See the header for why the
 * concatenation widths are worth stating as identities. */

#include "cpu/m68040/ap_m68040_search.h"

#include <stddef.h>

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

/* One history-bit update, Table 3-1's write cycle.
 *
 * "Locked RMW Access to Set U" stands against every row that sets `U` and not
 * `M`; "Write to Set U and M" and "Write to Set M" are plain writes. So the
 * lock follows from what is being set, and needs no extra table.
 *
 * Returns false only for a transfer error on the write, which the caller turns
 * into `AP_M68040_SEARCH_BUS_ERROR` -- the same outcome a failed fetch has,
 * because §3.2.5 makes the update part of the search rather than something
 * after it: "the U-bit and M-bit are updated before the M68040 allows a page to
 * be accessed or written". */
static bool apply_history(const ap_m68040_search_config_t *config,
                          ap_m68040_search_result_t *out, uint32_t address,
                          bool set_used, bool set_modified) {
  if (config->update == NULL || (!set_used && !set_modified)) {
    return true;
  }
  const bool locked = set_used && !set_modified;
  out->updates++;
  if (locked) {
    out->locked_updates++;
  }
  return config->update(config->update_context, address, set_used, set_modified,
                        locked);
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
    /* No history to write: an invalid descriptor's other thirty bits are the
     * operating system's, so bit 3 is not a `U` bit. */
    out.status = AP_M68040_SEARCH_INVALID;
    return out;
  }
  /* "For a table descriptor, a write cycle that sets the U-bit occurs only if
   * the U-bit was clear." Before descending, because §3.2.5 puts the update
   * before the access it authorises -- and because a descriptor encountered on
   * the way to a page that turns out to be absent still had its `U` set. */
  if (!apply_history(config, &out, address, !root.used, false)) {
    out.status = AP_M68040_SEARCH_BUS_ERROR;
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
  if (!apply_history(config, &out, address, !pointer.used, false)) {
    out.status = AP_M68040_SEARCH_BUS_ERROR;
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
  /* Which descriptor the history bits belong to. An indirect descriptor has
   * none of its own -- Figure 3-12 gives it as `DESCRIPTOR ADDRESS` in bits
   * 31-2 and `PDT` in 1-0, so bit 3 is part of the pointer -- and the
   * descriptor it names is the one that carries `U` and `M`. */
  uint32_t page_descriptor_address = address;

  if (page.type == AP_M68040_PDT_INDIRECT) {
    /* "Bits 31-2 contain the physical address of the page descriptor." One
     * indirection only: "this encoding is invalid for a page descriptor pointed
     * to by an indirect descriptor", so a chain terminates rather than
     * looping -- the same rule the 68851 states from the other side. */
    page_descriptor_address = page.address;
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

  /* "Setting the W-bit in a table descriptor write protects all pages accessed
   * with that descriptor", so the leaf's bit is folded into what the path
   * already carried rather than replacing it. Table 3-1's note -- "WP indicates
   * the **accumulated** write-protect status" -- is why this is computed before
   * the update rather than after it. */
  const bool write_protect = out.write_protect || page.write_protect;
  /* §3.2.5's other suppressor. `S` "identifies a pointer table **or a page** as
   * a supervisor-only table or page", and on this part only the page carries
   * one: Figure 3-11's table descriptors have no `S` field. */
  const bool supervisor_violation =
      (out.supervisor || page.supervisor) && !config->supervisor;

  /* Table 3-1, read as a page image. `U` is set on every row it is clear on,
   * write-protected or not; `M` is set only for a write access that meets
   * neither suppressor. Every row that ends with `M` set also ends with `U`
   * set, which is the invariant `ap_m68040_page_descriptor_is_incoherent`
   * names from the other side. */
  const bool updating = config->update != NULL;
  const bool set_used = updating && !page.used;
  const bool set_modified = updating && config->write && !page.modified &&
                            !write_protect && !supervisor_violation;
  if (!apply_history(config, &out, page_descriptor_address, set_used,
                     set_modified)) {
    out.status = AP_M68040_SEARCH_BUS_ERROR;
    return out;
  }

  out.status = AP_M68040_SEARCH_RESIDENT;
  out.physical_address =
      page.address | ap_m68040_page_offset(logical_address, config->page_size);
  out.write_protect = write_protect;
  /* The result keeps the accumulating shape even though only the page carries
   * `S` on this part, because the manual's wording is about the tree and a
   * future part may place it higher. */
  out.supervisor = out.supervisor || page.supervisor;
  out.cache_mode = page.cache_mode;
  /* What the descriptor holds now, which is what an ATC entry caches -- and
   * with a NULL `update` that is the descriptor's own bit, unchanged. An
   * observer that reported the bit the access *would* have set would be
   * reporting a machine state that never existed; the 68030's walk says the
   * same thing in the same place. */
  out.modified = page.modified || set_modified;
  out.global = page.global;
  out.user_attribute_0 = page.user_attribute_0;
  out.user_attribute_1 = page.user_attribute_1;
  return out;
}
