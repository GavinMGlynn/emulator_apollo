/* MC68851 table search: a transcription of Figure 5-23, with Figure 5-26's
 * limit check and the root pointer selection truth table. See the header for
 * why the flowchart rather than the prose is the specification. */

#include "cpu/m68851/ap_m68851_search.h"

ap_m68851_root_t ap_m68851_select_root(unsigned function_code, bool sre) {
  /* FC3 is the bus master: set for a logical bus master other than the CPU. */
  if ((function_code & 0x8u) != 0u) {
    return AP_M68851_ROOT_DRP;
  }
  /* FC2 is supervisor. The supervisor root pointer is used only when `SRE`
   * enables it; otherwise "the CPU root pointer is used for supervisor space
   * translations". */
  if ((function_code & 0x4u) != 0u && sre) {
    return AP_M68851_ROOT_SRP;
  }
  return AP_M68851_ROOT_CRP;
}

unsigned ap_m68851_search_index(const ap_m68851_tc_t *tc,
                                uint32_t logical_address, unsigned level) {
  /* The initial shift discards the top `IS` bits, then levels consume their
   * `TIx` bits from the top down. */
  unsigned consumed = tc->initial_shift;
  for (unsigned i = 0; i < level; i++) {
    consumed += tc->table_index[i];
  }
  const unsigned width = tc->table_index[level];
  if (width == 0u) {
    return 0u;
  }
  /* Bits [31 - consumed] down to [31 - consumed - width + 1]. */
  const unsigned shift = 32u - consumed - width;
  return (unsigned)((logical_address >> shift) & ((1u << width) - 1u));
}

/* Figure 5-26. `previous_is_root` is the flowchart's `y = 'RP'`. */
static bool limit_violated(const ap_m68851_search_config_t *config,
                           bool previous_is_root, unsigned last_size,
                           bool lower_limit, unsigned limit, unsigned index) {
  if (previous_is_root) {
    /* "FCL = 1 OR DRP IS RP" -- no limit check. A DMA search always performs a
     * function code lookup, so its root pointer's limit is bypassed for the
     * same reason `FCL` bypasses the others'. */
    if (config->tc->function_code_lookup || config->root_is_drp) {
      return false;
    }
  }

  /* "LAST_SIZE = 4" -- no limit check. A short-format descriptor has no limit
   * field, so whether this level is bounded was decided by the *format* of the
   * descriptor at the level above. */
  if (last_size == 4u) {
    return false;
  }

  /* "L/U = 0": violated when LPA[TIx] > LIMIT.
   * "L/U = 1": violated when LPA[TIx] < LIMIT. */
  return lower_limit ? (index < limit) : (index > limit);
}

/* Figure 5-23's terminal states, gathered so each exit sets the same fields. */
static ap_m68851_search_result_t
invalid_result(ap_m68851_search_fault_t fault, unsigned levels,
               bool write_protect) {
  return (ap_m68851_search_result_t){
      .type = AP_M68851_SEARCH_TYPE_INVALID,
      .fault = fault,
      .levels = levels,
      .write_protect = write_protect,
  };
}

/* Fill the result from a terminating page descriptor. */
static void take_page(ap_m68851_search_result_t *out,
                      const ap_m68851_descriptor_t *page, bool write_protect) {
  out->physical_address = page->address;
  out->cache_inhibit = page->cache_inhibit;
  out->modified = page->modified;
  out->gate = page->gate;
  out->lock = page->lock;
  out->shared_globally = page->shared_globally;
  /* "The effective write protection determined during the translation table
   * search": the accumulated protection from every level, not this
   * descriptor's bit alone. A write protect above cannot be undone below. */
  out->write_protect = write_protect || page->write_protect;
}

ap_m68851_search_result_t
ap_m68851_search(const ap_m68851_search_config_t *config,
                 uint32_t logical_address, unsigned function_code) {
  ap_m68851_search_result_t out = {0};
  const ap_m68851_tc_t *tc = config->tc;

  /* The flowchart's state. `x` is the level being indexed, `y` says what the
   * previous descriptor was -- the root pointer, or a table descriptor. */
  unsigned x = 0u; /* 'A' */
  bool previous_is_root = true; /* y = 'RP' */
  unsigned size = 0u;
  unsigned last_size = 8u; /* "LAST_SIZE <- 8": a root pointer is 64 bits */
  /* The limit that bounds the *next* index, carried from the descriptor above:
   * Figure 5-26 reads `LIMIT` and `L/U` from the previous descriptor, which is
   * the root pointer on the first pass. */
  bool previous_lower_limit = config->root->lower_limit;
  unsigned previous_limit = config->root->limit;
  bool write_protect = false;
  uint32_t table = config->root->table_address;
  unsigned levels = 0u;

  /* "CHECK DESCRIPTOR TYPE OF ROOT POINTER". */
  switch (config->root->descriptor_type) {
  case AP_M68851_DT_PAGE_DESCRIPTOR:
    /* "TYPE <- 'EARLY'": the root maps directly with a constant offset and no
     * table is walked at all. §6.1.1.4: "the page descriptor is formed by
     * adding (unsigned) the value in the table address field to the incoming
     * logical address." */
    out.type = AP_M68851_SEARCH_TYPE_EARLY;
    out.physical_address = config->root->table_address + logical_address;
    out.shared_globally = config->root->shared_globally;
    return out;
  case AP_M68851_DT_VALID_4_BYTE:
    size = 4u;
    break;
  case AP_M68851_DT_VALID_8_BYTE:
    size = 8u;
    break;
  case AP_M68851_DT_INVALID:
    /* Not reachable through `PMOVE`, which refuses to load one, but reachable
     * through `PRESTORE`. The manual calls the result undefined; ending the
     * search invalid is the containable reading. */
    return invalid_result(AP_M68851_SEARCH_FAULT_INVALID_DESCRIPTOR, 0u, false);
  }

  /* "PERFORM FUNCTION CODE LOOKUP IF REQUIRED": FCL = 1 OR FC3 = 1. The DMA
   * root pointer always does one -- §6.1.3.3 -- which is why `FC3` appears
   * here as well as in the root pointer selection. */
  if (tc->function_code_lookup || (function_code & 0x8u) != 0u) {
    uint64_t raw = 0;
    const uint32_t address = table + (function_code & 0xFu) * size;
    if (!config->fetch(config->fetch_context, address, size, &raw)) {
      return invalid_result(AP_M68851_SEARCH_FAULT_BUS_ERROR, levels,
                            write_protect);
    }
    levels++;

    const ap_m68851_descriptor_t d =
        (size == 4u) ? ap_m68851_short_table_descriptor((uint32_t)raw)
                     : ap_m68851_long_table_descriptor(raw);
    write_protect = write_protect || d.write_protect;

    switch (d.dt) {
    case AP_M68851_DT_PAGE_DESCRIPTOR: {
      /* "TYPE <- 'EARLY'". Re-decode at the right format: a page descriptor and
       * a table descriptor do not share a layout. */
      const ap_m68851_descriptor_t page =
          (size == 4u) ? ap_m68851_short_page_descriptor((uint32_t)raw)
                       : ap_m68851_long_page_descriptor(raw, true);
      out.type = AP_M68851_SEARCH_TYPE_EARLY;
      out.levels = levels;
      take_page(&out, &page, write_protect);
      return out;
    }
    case AP_M68851_DT_INVALID:
      return invalid_result(AP_M68851_SEARCH_FAULT_INVALID_DESCRIPTOR, levels,
                            write_protect);
    case AP_M68851_DT_VALID_4_BYTE:
      last_size = size;
      size = 4u;
      table = d.address;
      break;
    case AP_M68851_DT_VALID_8_BYTE:
      last_size = size;
      size = 8u;
      table = d.address;
      break;
    }
    previous_lower_limit = d.lower_limit;
    previous_limit = d.limit;
    /* The function code lookup consumed a level, so the next descriptor's
     * previous is no longer the root pointer. */
    previous_is_root = false;
  }

  /* "ENTERING A LEVEL TABLE SEARCH": y <- 'A'. */
  for (;;) {
    const unsigned index = ap_m68851_search_index(tc, logical_address, x);

    /* "PERFORM LIMIT CHECK". The limit bounding this index belongs to the
     * descriptor above, which is the root pointer on the first pass. */
    if (limit_violated(config, previous_is_root, last_size,
                       previous_lower_limit, previous_limit, index)) {
      return invalid_result(AP_M68851_SEARCH_FAULT_LIMIT_VIOLATION, levels,
                            write_protect);
    }

    uint64_t raw = 0;
    const uint32_t address = table + index * size;
    if (!config->fetch(config->fetch_context, address, size, &raw)) {
      return invalid_result(AP_M68851_SEARCH_FAULT_BUS_ERROR, levels,
                            write_protect);
    }
    levels++;

    const ap_m68851_descriptor_t d =
        (size == 4u) ? ap_m68851_short_table_descriptor((uint32_t)raw)
                     : ap_m68851_long_table_descriptor(raw);

    if (d.dt == AP_M68851_DT_INVALID) {
      return invalid_result(AP_M68851_SEARCH_FAULT_INVALID_DESCRIPTOR, levels,
                            write_protect);
    }

    if (d.dt == AP_M68851_DT_PAGE_DESCRIPTOR) {
      /* The search terminates here. Whether it terminated *early* depends on
       * whether any level remained: "x != 'D'" advances x and asks whether the
       * next TIx is zero. */
      bool early = false;
      if (x < 3u) {
        early = tc->table_index[x + 1u] != 0u;
      }
      const ap_m68851_descriptor_t page =
          (size == 4u) ? ap_m68851_short_page_descriptor((uint32_t)raw)
                       : ap_m68851_long_page_descriptor(raw, early);
      out.type = early ? AP_M68851_SEARCH_TYPE_EARLY
                       : AP_M68851_SEARCH_TYPE_NORMAL;
      out.levels = levels;
      take_page(&out, &page, write_protect || d.write_protect);
      return out;
    }

    /* A table or indirect descriptor. Accumulate its protection either way. */
    write_protect = write_protect || d.write_protect;
    last_size = size;
    size = (d.dt == AP_M68851_DT_VALID_4_BYTE) ? 4u : 8u;
    table = d.address;
    /* This descriptor's limit bounds the next level's index. A short-format
     * descriptor has none, and `limit_violated` refuses the check on
     * `last_size == 4` rather than on these values -- so what they hold in that
     * case never matters. */
    previous_lower_limit = d.lower_limit;
    previous_limit = d.limit;

    /* Are there more levels? "x = 'D'" or the next TIx is zero means the table
     * indices are exhausted, and a valid descriptor there is an *indirect*
     * descriptor rather than another table. */
    const bool exhausted = (x >= 3u) || (tc->table_index[x + 1u] == 0u);
    if (!exhausted) {
      x++;
      previous_is_root = false;
      continue; /* "REPEAT SEARCH" */
    }

    /* "TYPE <- 'INDIRECT'": follow it once. The indirect descriptor's address
     * is 4-byte aligned rather than 16-, so it is re-decoded at the right
     * format. */
    const ap_m68851_descriptor_t indirect =
        (last_size == 4u)
            ? ap_m68851_short_indirect_descriptor((uint32_t)raw)
            : ap_m68851_long_indirect_descriptor(raw);

    uint64_t target = 0;
    if (!config->fetch(config->fetch_context, indirect.address, size,
                       &target)) {
      return invalid_result(AP_M68851_SEARCH_FAULT_BUS_ERROR, levels,
                            write_protect);
    }
    levels++;

    const ap_m68851_descriptor_t probe =
        (size == 4u) ? ap_m68851_short_table_descriptor((uint32_t)target)
                     : ap_m68851_long_table_descriptor(target);

    /* "DT = 'PAGE DESCRIPTOR'" is accepted and "OTHERWISE" is invalid: Figure
     * 5-10's two illegal cells, which is what stops a chain of indirections. */
    if (probe.dt != AP_M68851_DT_PAGE_DESCRIPTOR) {
      return invalid_result(AP_M68851_SEARCH_FAULT_INVALID_DESCRIPTOR, levels,
                            write_protect);
    }

    const ap_m68851_descriptor_t page =
        (size == 4u) ? ap_m68851_short_page_descriptor((uint32_t)target)
                     : ap_m68851_long_page_descriptor(target, false);
    out.type = AP_M68851_SEARCH_TYPE_INDIRECT;
    out.levels = levels;
    take_page(&out, &page, write_protect);
    return out;
  }
}
