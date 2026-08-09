/* MC68030 translation table search. See ap_m68030_walk.h for the citations. */

#include <stddef.h>

#include "cpu/m68030/ap_m68030_walk.h"

/* Bits of the logical address consumed before a given level's index, counting
 * down from the top: the initial shift, then each earlier level's index. */
static unsigned bits_consumed_before(const ap_m68030_tc_t *tc, unsigned level) {
  unsigned consumed = tc->initial_shift;
  for (unsigned i = 0; i < level; i++) {
    consumed += tc->table_index[i];
  }
  return consumed;
}

/* An early termination page descriptor stops the search above the page table,
 * so "the low-order bits of the address are supplied by the logical address"
 * covers every bit below the level reached -- the indices never consumed as
 * well as the page offset proper. */
static uint32_t remaining_offset(const ap_m68030_tc_t *tc, unsigned levels_used,
                                 uint32_t address) {
  const unsigned consumed = bits_consumed_before(tc, levels_used);
  if (consumed >= 32u) {
    return 0;
  }
  const unsigned width = 32u - consumed;
  return address & ((width >= 32u) ? UINT32_C(0xFFFFFFFF)
                                   : ((UINT32_C(1) << width) - 1u));
}

/* Update the U and M history bits of one descriptor.
 *
 * "During a table search, the U bit in each descriptor that is encountered is
 * checked and set if it is not already set. Similarly, when the table search is
 * for a write access and the M bit of the page descriptor is clear, the
 * processor sets the bit if the table search does not encounter a set WP bit or
 * a supervisor violation."
 *
 * Both bits live in the same descriptor, so however many of them change it is
 * one read-modify-write: `[030]` §11 p. 11-56 counts "an RMC cycle to set the U
 * bit ... as one read and one write", and the read is the fetch already
 * counted. Hence one `history_writes` per descriptor, not one per bit.
 *
 * Returns false for a bus error on the write half. */
static bool update_history(ap_m68030_walk_result_t *result,
                           const ap_m68030_search_access_t *access,
                           ap_m68030_update_fn update, void *context,
                           uint32_t descriptor_address,
                           const ap_m68030_descriptor_t *descriptor,
                           bool is_page) {
  /* "This bit is automatically set by the processor when a descriptor is
   * accessed in which the U bit is clear except after a supervisor violation is
   * detected."
   *
   * The violation is evaluated with this descriptor's own S bit already folded
   * into the search, so a descriptor that itself denies the access does not get
   * its U set, while one denied further down the tree already did -- which is
   * what "a pointer may be fetched, and its U bit set, for an address to which
   * access is denied at another level of the tree" describes. The ordering is
   * also what the hardware can do: the write half of the RMC follows the read,
   * so the S bit is known in time to suppress it. */
  const bool violated =
      !ap_m68030_search_permits_access(&result->search, access->supervisor);
  const bool set_used = !descriptor->used && !violated;

  /* M is the page descriptor's bit alone. `ap_m68030_desc` already holds the
   * full rule, including that the read half of a read-modify-write counts as a
   * write and that an already-modified page is not written again. */
  const bool set_modified =
      is_page && ap_m68030_search_should_set_modified(
                     &result->search, access->write, access->read_modify_write,
                     access->supervisor, descriptor->modified);

  /* A NULL update is a search that must not disturb the tree, which is what
   * PTEST performs -- so it decides nothing and writes nothing, but the M state
   * it *would* have cached is still the descriptor's own. */
  if (update == NULL) {
    if (is_page) {
      result->page_modified = descriptor->modified;
    }
    return true;
  }

  if (is_page) {
    /* What the ATC entry caches: M as it stands once this search is done. */
    result->page_modified = descriptor->modified || set_modified;
  }

  if (!set_used && !set_modified) {
    return true;
  }

  result->history_writes++;
  if (!update(context, descriptor_address, set_used, set_modified)) {
    result->bus_error = true;
    return false;
  }
  return true;
}

int ap_m68030_walk_fill_atc(ap_m68030_atc_t *atc,
                            const ap_m68030_walk_result_t *result,
                            const ap_m68030_search_access_t *access,
                            uint8_t function_code, uint32_t address,
                            uint8_t page_size_bits) {
  /* "The bus error bit ... is set when a bus error ... is encountered during
   * the table search", and §9.4 lists the other three conditions that set it:
   * an invalid descriptor, a supervisor violation, and a limit violation. A
   * supervisor violation is a property of the access, not of the tree, which is
   * why the access is needed here and not only the search state. */
  const bool bus_error =
      result->search.invalid || result->search.limit_violation ||
      !ap_m68030_search_permits_access(&result->search, access->supervisor);

  /* An entry whose B is set has no meaningful translation -- the search never
   * produced one -- so the physical field is stored as zero rather than as
   * whatever a partial walk left behind. */
  const uint32_t physical_page =
      bus_error ? 0u : (result->physical >> AP_M68030_ATC_ADDRESS_SHIFT);

  return ap_m68030_atc_insert(atc, function_code, address, page_size_bits,
                              physical_page, result->search.write_protected,
                              result->search.cache_inhibited,
                              result->page_modified, bus_error);
}

ap_m68030_walk_result_t ap_m68030_walk(const ap_m68030_tc_t *tc,
                                       const ap_m68030_root_t *root,
                                       uint32_t address,
                                       const ap_m68030_search_access_t *access,
                                       ap_m68030_fetch_fn fetch,
                                       ap_m68030_update_fn update,
                                       void *context) {
  return ap_m68030_walk_to_level(tc, root, address, access, fetch, update,
                                 context, 0u);
}

ap_m68030_walk_result_t ap_m68030_walk_to_level(
    const ap_m68030_tc_t *tc, const ap_m68030_root_t *root, uint32_t address,
    const ap_m68030_search_access_t *access, ap_m68030_fetch_fn fetch,
    ap_m68030_update_fn update, void *context, unsigned max_levels) {
  ap_m68030_walk_result_t result = {0};
  ap_m68030_search_reset(&result.search);

  const ap_m68030_tc_split_t split = ap_m68030_tc_split(tc, address);

  uint32_t table_address = root->table_address;
  bool long_format = root->long_format;
  bool have_limit = root->has_limit;
  uint16_t limit = root->limit;
  bool lower_limit = root->lower_limit;

  for (unsigned level = 0; level < split.levels; level++) {
    /* `PTEST`'s ceiling, checked before the fetch that would exceed it -- "the
     * search ends at the specified level", so a level of 1 accesses one table
     * and stops with `N` reporting 1. Not a failure: `truncated` is reported
     * apart from `search.invalid` so a probe that was *asked* to stop cannot be
     * read as a probe that found a broken tree. */
    if (max_levels != 0u && level >= max_levels) {
      result.truncated = true;
      return result;
    }

    const uint32_t index = split.index[level];

    /* "This 15-bit field contains a limit to which the index portion of an
     * address is compared ... The limit check applies to the index into the
     * table at the next lower level of the translation tree." So the limit
     * carried from the *previous* descriptor governs *this* index. */
    if (have_limit &&
        !ap_m68030_desc_index_within_limit(limit, lower_limit, index)) {
      ap_m68030_search_fail_limit(&result.search);
      return result;
    }

    const uint32_t stride = long_format ? 8u : 4u;
    const uint32_t descriptor_address = table_address + index * stride;

    ap_m68030_descriptor_t descriptor = {0};
    result.descriptor_fetches++;
    if (!fetch(context, descriptor_address, long_format, &descriptor)) {
      /* A bus error during the search sets the ATC's B, exactly as an invalid
       * descriptor or a protection violation does -- but `MMUSR` reports the
       * two separately, so the cause is recorded as well as the effect. */
      result.bus_error = true;
      ap_m68030_search_fail_invalid(&result.search);
      return result;
    }
    /* Recorded only now: "the physical address of the last descriptor
     * *successfully* fetched loads into the address register", so the failed
     * fetch above leaves the previous descriptor's address standing. */
    result.last_descriptor_address = descriptor_address;
    result.levels_walked = level + 1;

    /* Protection accumulates whether or not the search continues. */
    ap_m68030_search_accumulate(&result.search, descriptor.write_protect,
                                descriptor.supervisor, descriptor.cache_inhibit);

    const bool in_page_table = (level + 1 == split.levels);
    const ap_m68030_desc_role_t role =
        ap_m68030_desc_role(descriptor.dt, in_page_table);

    switch (role) {
    case AP_M68030_ROLE_INVALID:
      /* No history update. An invalid descriptor has no U bit to set: beyond
       * DT, "all long-format descriptors and short-format invalid descriptors
       * include one or two unused fields. The operating system can use these
       * fields for its own purposes" -- so writing a U bit here would land in
       * whatever the OS stored, such as the external device address of a
       * non-resident page. */
      ap_m68030_search_fail_invalid(&result.search);
      return result;

    case AP_M68030_ROLE_INDIRECT: {
      /* "this code identifies an indirect descriptor that points to a
       * short-format [or long-format] page descriptor." One more fetch, and
       * what it yields is the page descriptor itself. */
      if (!update_history(&result, access, update, context, descriptor_address,
                          &descriptor, false)) {
        ap_m68030_search_fail_invalid(&result.search);
        return result;
      }

      ap_m68030_descriptor_t pointed = {0};
      const bool pointed_long = (descriptor.dt == AP_M68030_DT_VALID_8BYTE);
      result.descriptor_fetches++;
      result.used_indirect = true;
      if (!fetch(context, descriptor.address_field, pointed_long, &pointed)) {
        result.bus_error = true;
        ap_m68030_search_fail_invalid(&result.search);
        return result;
      }
      /* Successfully fetched, so this one replaces the indirect descriptor's
       * own address as the last. */
      result.last_descriptor_address = descriptor.address_field;
      ap_m68030_search_accumulate(&result.search, pointed.write_protect,
                                  pointed.supervisor, pointed.cache_inhibit);
      if (pointed.dt != AP_M68030_DT_PAGE) {
        /* An indirect descriptor must point at a page descriptor. Anything
         * else is a malformed tree, not something to follow further. */
        ap_m68030_search_fail_invalid(&result.search);
        return result;
      }
      /* The page descriptor the indirection reached is where M belongs. */
      if (!update_history(&result, access, update, context,
                          descriptor.address_field, &pointed, true)) {
        ap_m68030_search_fail_invalid(&result.search);
        return result;
      }
      result.ok = true;
      result.physical =
          ap_m68030_desc_page_address(pointed.address_field, tc->page_size_bits) |
          (address & (ap_m68030_tc_page_size(tc) - 1u));
      return result;
    }

    case AP_M68030_ROLE_PAGE:
      if (!update_history(&result, access, update, context, descriptor_address,
                          &descriptor, true)) {
        ap_m68030_search_fail_invalid(&result.search);
        return result;
      }
      result.ok = true;
      result.physical =
          ap_m68030_desc_page_address(descriptor.address_field,
                                      tc->page_size_bits) |
          (address & (ap_m68030_tc_page_size(tc) - 1u));
      return result;

    case AP_M68030_ROLE_EARLY_PAGE:
      /* Stopped above the page table: every logical bit below this level is
       * offset, not just the page offset. The page address field is used
       * unmasked here because the block it names is larger than a page. */
      if (!update_history(&result, access, update, context, descriptor_address,
                          &descriptor, true)) {
        ap_m68030_search_fail_invalid(&result.search);
        return result;
      }
      result.ok = true;
      result.early_termination = true;
      result.physical =
          ((descriptor.address_field & UINT32_C(0x00FFFFFF)) << 8) |
          remaining_offset(tc, level + 1, address);
      return result;

    case AP_M68030_ROLE_TABLE:
      if (!update_history(&result, access, update, context, descriptor_address,
                          &descriptor, false)) {
        ap_m68030_search_fail_invalid(&result.search);
        return result;
      }
      table_address = descriptor.address_field;
      long_format = (descriptor.dt == AP_M68030_DT_VALID_8BYTE);
      have_limit = descriptor.has_limit;
      limit = descriptor.limit;
      lower_limit = descriptor.lower_limit;
      break;
    }
  }

  /* Ran out of levels without reaching a page descriptor: the tree does not
   * describe this address. */
  ap_m68030_search_fail_invalid(&result.search);
  return result;
}

/* ---------------------------------------------------------------------------
 * Decoding descriptors from memory. See the header for the five-source
 * derivation of these positions and for why it is a derivation.
 * ------------------------------------------------------------------------- */

static bool bit_set(uint32_t word, unsigned position) {
  return ((word >> position) & 1u) != 0u;
}

ap_m68030_descriptor_t ap_m68030_descriptor_unpack_short(uint32_t word,
                                                         bool in_page_table) {
  ap_m68030_descriptor_t descriptor = {0};
  descriptor.dt = (ap_m68030_dt_t)(word & AP_M68030_DESC_DT_MASK);

  /* Short format carries no LIMIT and no S: the 68851 places both in long
   * format only, and `[030]` Table 9-3 says the same of S independently. */
  switch (ap_m68030_desc_role(descriptor.dt, in_page_table)) {
  case AP_M68030_ROLE_INVALID:
    return descriptor;

  case AP_M68030_ROLE_INDIRECT:
    /* "Indirect Address. This field (bits [31-2] of all indirect descriptors)".
     * That leaves only DT, so an indirect descriptor has no status bits at all
     * -- no U to set, and therefore no history write to cost. `used` is
     * reported set so nothing tries to update a bit that does not exist. */
    descriptor.address_field = word & UINT32_C(0xFFFFFFFC);
    descriptor.used = true;
    return descriptor;

  case AP_M68030_ROLE_PAGE:
  case AP_M68030_ROLE_EARLY_PAGE:
    /* "Page Address. This field (bits [31-8] of all page descriptors)". Held as
     * the 24-bit field itself, which is what ap_m68030_desc_page_address takes. */
    descriptor.address_field = (word >> 8) & UINT32_C(0x00FFFFFF);
    descriptor.cache_inhibit = bit_set(word, AP_M68030_DESC_SHORT_CI_BIT);
    descriptor.modified = bit_set(word, AP_M68030_DESC_SHORT_M_BIT);
    descriptor.used = bit_set(word, AP_M68030_DESC_SHORT_U_BIT);
    descriptor.write_protect = bit_set(word, AP_M68030_DESC_SHORT_WP_BIT);
    return descriptor;

  case AP_M68030_ROLE_TABLE:
    /* "Table Address. This field (bits [31-4] of all table descriptors)", so
     * the status is the four bits below it -- U, WP and DT, and nothing else.
     * CI and M are page-descriptor fields. */
    descriptor.address_field = word & UINT32_C(0xFFFFFFF0);
    descriptor.used = bit_set(word, AP_M68030_DESC_SHORT_U_BIT);
    descriptor.write_protect = bit_set(word, AP_M68030_DESC_SHORT_WP_BIT);
    return descriptor;
  }
  return descriptor;
}

ap_m68030_descriptor_t ap_m68030_descriptor_unpack_long(uint32_t upper,
                                                        uint32_t lower,
                                                        bool in_page_table) {
  ap_m68030_descriptor_t descriptor = {0};
  descriptor.dt = (ap_m68030_dt_t)(upper & AP_M68030_DESC_DT_MASK);

  const ap_m68030_desc_role_t role =
      ap_m68030_desc_role(descriptor.dt, in_page_table);
  if (role == AP_M68030_ROLE_INVALID) {
    return descriptor;
  }

  /* "The limit field (bits [62-48] of a long format table or type-2 page
   * descriptor)" -- fifteen bits, with L/U above it. */
  descriptor.has_limit = true;
  descriptor.limit = (uint16_t)((upper >> AP_M68030_DESC_LONG_LIMIT_SHIFT) &
                                0x7FFFu);
  descriptor.lower_limit = bit_set(upper, AP_M68030_DESC_LONG_LU_BIT);

  descriptor.supervisor = bit_set(upper, AP_M68030_DESC_LONG_S_BIT);
  descriptor.used = bit_set(upper, AP_M68030_DESC_LONG_U_BIT);
  descriptor.write_protect = bit_set(upper, AP_M68030_DESC_LONG_WP_BIT);

  switch (role) {
  case AP_M68030_ROLE_INDIRECT:
    descriptor.address_field = lower & UINT32_C(0xFFFFFFFC);
    descriptor.has_limit = false;
    return descriptor;

  case AP_M68030_ROLE_PAGE:
  case AP_M68030_ROLE_EARLY_PAGE:
    descriptor.address_field = (lower >> 8) & UINT32_C(0x00FFFFFF);
    /* CI and M are page-descriptor fields on both parts. */
    descriptor.cache_inhibit = bit_set(upper, AP_M68030_DESC_LONG_CI_BIT);
    descriptor.modified = bit_set(upper, AP_M68030_DESC_LONG_M_BIT);
    return descriptor;

  case AP_M68030_ROLE_TABLE:
    descriptor.address_field = lower & UINT32_C(0xFFFFFFF0);
    return descriptor;

  case AP_M68030_ROLE_INVALID:
    return descriptor;
  }
  return descriptor;
}

uint32_t ap_m68030_root_pack_upper(const ap_m68030_root_t *root) {
  uint32_t upper = 0;

  /* "A descriptor-type code of $00 (invalid) is not allowed" in a root pointer,
   * so a root that reached here is one of the two valid table types, and which
   * one is exactly what `long_format` records. */
  upper |= root->long_format ? (uint32_t)AP_M68030_DT_VALID_8BYTE
                             : (uint32_t)AP_M68030_DT_VALID_4BYTE;

  if (root->has_limit) {
    upper |= ((uint32_t)root->limit & 0x7FFFu)
             << AP_M68030_DESC_LONG_LIMIT_SHIFT;
    if (root->lower_limit) {
      upper |= UINT32_C(1) << AP_M68030_DESC_LONG_LU_BIT;
    }
  }
  return upper;
}
