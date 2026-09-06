/* MC68851 table search: a transcription of Figure 5-23, with Figure 5-26's
 * limit check and the root pointer selection truth table. See the header for
 * why the flowchart rather than the prose is the specification. */

#include "cpu/m68851/ap_m68851_search.h"

#include <stddef.h>

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

/* `ACC_STATUS`, Figures 5-24 and 5-27. Carried through the search as one value
 * so that every exit reports the same accrued protection -- including the
 * invalid exits, because a limit violation and an invalid descriptor still
 * leave a `PSR` for `PTEST` to read. */
typedef struct {
  unsigned ral;
  unsigned wal;
  bool wp;
  bool sg;
  bool s;
} acc_status_t;

/* Figure 5-24, *Table Search Initialization Detail*, in full:
 *
 *     ACC_STATUS[RAL] <- $7
 *     ACC_STATUS[WAL] <- $7
 *     ACC_STATUS[WP]  <- 0
 *     ACC_STATUS[SG]  <- 0
 *     ACC_STATUS[S]   <- 0
 *
 * `SG` alone departs from the figure, and §5.1.6 is why: "if there are no long
 * format descriptors in the path ... **the shared attribute is as indicated in
 * the root pointer used**". The root pointer is the only descriptor that can
 * supply it in that case -- it carries an `SG` and no other protection field --
 * so seeding from it is what makes both statements true at once. */
static acc_status_t acc_initial(const ap_m68851_rp_t *root) {
  return (acc_status_t){.ral = 7u,
                        .wal = 7u,
                        .wp = false,
                        .sg = root->shared_globally,
                        .s = false};
}

/* Figure 5-27's per-descriptor accumulation. `size` is *this* descriptor's own
 * width: the figure's `SIZE = 4` arm contributes only `WP`, which is why a path
 * of short-format descriptors leaves `RAL` and `WAL` at `$7`.
 *
 *     SIZE = 8:  IF RAL < ACC_STATUS[RAL] THEN ACC_STATUS[RAL] <- RAL
 *                IF WAL < ACC_STATUS[WAL] THEN ACC_STATUS[WAL] <- WAL
 *                ACC_STATUS[SG] <- ACC_STATUS[SG] V SG
 *                ACC_STATUS[S]  <- ACC_STATUS[S]  V S
 *                ACC_STATUS[WP] <- ACC_STATUS[WP] V WP
 *     SIZE = 4:  ACC_STATUS[WP] <- ACC_STATUS[WP] V WP
 *
 * A minimum for the two levels and an OR for the three attributes, which is
 * §5.1.6's "most strict of those indicated at any level" in both directions:
 * a smaller level number is more privileged, so the minimum is the strictest. */
static void accumulate(acc_status_t *acc, const ap_m68851_descriptor_t *d,
                       unsigned size) {
  acc->wp = acc->wp || d->write_protect;
  if (size != 8u) {
    return;
  }
  if (d->read_access_level < acc->ral) {
    acc->ral = d->read_access_level;
  }
  if (d->write_access_level < acc->wal) {
    acc->wal = d->write_access_level;
  }
  acc->sg = acc->sg || d->shared_globally;
  acc->s = acc->s || d->supervisor;
}

/* Copy the accrued protection into a result, whatever kind of exit this is. */
static void take_acc(ap_m68851_search_result_t *out, const acc_status_t *acc) {
  out->write_protect = acc->wp;
  out->read_access_level = acc->ral;
  out->write_access_level = acc->wal;
  out->supervisor_only = acc->s;
  out->shared_globally = acc->sg;
}

/* Figure 5-23's terminal states, gathered so each exit sets the same fields. */
static ap_m68851_search_result_t
invalid_result(ap_m68851_search_fault_t fault, unsigned levels,
               const acc_status_t *acc, const ap_m68851_visited_t *path,
               unsigned path_length) {
  ap_m68851_search_result_t out = {
      .type = AP_M68851_SEARCH_TYPE_INVALID,
      .fault = fault,
      .levels = levels,
  };
  /* The accrued protection survives the fault with the path: `PTEST` reports
   * `W` and `A` from what the search met, and a search that ended invalid still
   * met it. */
  take_acc(&out, acc);
  /* The path survives the fault. "A pointer may be fetched, and its U bit set,
   * for an address to which access is denied at another level of the tree" --
   * so the descriptors already read are still used, and dropping them here
   * would leave the tables claiming they never were. */
  for (unsigned i = 0; i < path_length && i < AP_M68851_SEARCH_MAX_PATH; i++) {
    out.path[i] = path[i];
  }
  out.path_length = path_length;
  return out;
}

static void copy_path(ap_m68851_search_result_t *out,
                      const ap_m68851_visited_t *path, unsigned path_length) {
  for (unsigned i = 0; i < path_length && i < AP_M68851_SEARCH_MAX_PATH; i++) {
    out->path[i] = path[i];
  }
  out->path_length = path_length;
}

/* Record a descriptor the search has just read. The status byte is the fourth
 * of the descriptor in both formats -- see `ap_m68851_visited_t`. */
static void visit(ap_m68851_visited_t *path, unsigned *path_length,
                  uint32_t address, unsigned size, uint64_t raw, bool is_page) {
  if (*path_length >= AP_M68851_SEARCH_MAX_PATH) {
    return;
  }
  path[*path_length] = (ap_m68851_visited_t){
      .address = address,
      .status = (uint8_t)((size == 4u ? raw : (raw >> 32)) & 0xFFu),
      .is_page = is_page,
  };
  (*path_length)++;
}

/* Fill the result from a terminating page descriptor. `acc` has already had
 * this descriptor accumulated into it by the caller, which is why the four
 * fields below are the only ones taken from the descriptor directly: Figure
 * 5-27 assigns `G`, `CI` and `L` where it accumulates the rest, and `M` belongs
 * to the page alone. */
static void take_page(ap_m68851_search_result_t *out,
                      const ap_m68851_descriptor_t *page,
                      const acc_status_t *acc) {
  out->physical_address = page->address;
  out->modified = page->modified;
  /* `ACC_STATUS[G] <- G`, `[CI] <- CI`, `[L] <- L` -- assignment on both arms
   * of Figure 5-27's page branch, including the short one, so a short page
   * descriptor supplies them just as a long one does. */
  out->cache_inhibit = page->cache_inhibit;
  out->gate = page->gate;
  out->lock = page->lock;
  take_acc(out, acc);
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
  acc_status_t acc = acc_initial(config->root);
  ap_m68851_visited_t path[AP_M68851_SEARCH_MAX_PATH];
  unsigned path_length = 0;
  uint32_t table = config->root->table_address;
  unsigned levels = 0u;

  /* "CHECK DESCRIPTOR TYPE OF ROOT POINTER". */
  switch (config->root->descriptor_type) {
  case AP_M68851_DT_PAGE_DESCRIPTOR:
    /* "TYPE <- 'EARLY'": the root maps directly with a constant offset and no
     * table is walked at all. §6.1.1.4: "the page descriptor is formed by
     * adding (unsigned) the value in the table address field to the incoming
     * logical address."
     *
     * ## No limit check here, and §6.1.1.4 says there is one
     *
     * That paragraph ends: "If the DT field of a root pointer is set to `$1`,
     * the MC68851 performs a limit check **regardless of the state of the FCL
     * bit**." Taken at face value it makes this arm a defect.
     *
     * It is not. **Figure 5-23, the detailed table search flowchart, draws this
     * path with no limit check on it**: `CHECK DESCRIPTOR TYPE OF ROOT POINTER`
     * -> `DT = 'PAGE DESCRIPTOR'` -> `TYPE <- 'EARLY'` -> `CREATE ATC ENTRY`,
     * and `PERFORM LIMIT CHECK` appears only after `ENTERING A LEVEL TABLE
     * SEARCH`. And §6.3.1.2 defines the violation as "a table index extracted
     * from a logical address exceed[ing] the limit field of a corresponding
     * long format descriptor" -- and this path extracts no table index at all.
     *
     * So it is the flowchart and the error definition against one sentence of
     * register-field prose, which is the same shape as `[881]`'s transposed
     * `FATANH` signs: the algorithm is right and the paragraph is not.
     * Investigated 2026-09-07 and left as it is, recorded because §6.1.1.4
     * reads like a defect report against this arm. */
    out.type = AP_M68851_SEARCH_TYPE_EARLY;
    out.physical_address = config->root->table_address + logical_address;
    /* No descriptor was fetched, so `ACC_STATUS` is Figure 5-24's initial value
     * -- which is §5.1.6's "no long format descriptors in the path" case
     * exactly: the root pointer's `SG`, not supervisor-only, `RAL` and `WAL`
     * both `$7`. */
    take_acc(&out, &acc);
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
    return invalid_result(AP_M68851_SEARCH_FAULT_INVALID_DESCRIPTOR, 0u,
                           &acc, path, path_length);
  }

  /* "PERFORM FUNCTION CODE LOOKUP IF REQUIRED": FCL = 1 OR FC3 = 1. The DMA
   * root pointer always does one -- §6.1.3.3 -- which is why `FC3` appears
   * here as well as in the root pointer selection. */
  if (tc->function_code_lookup || (function_code & 0x8u) != 0u) {
    uint64_t raw = 0;
    const uint32_t address = table + (function_code & 0xFu) * size;
    if (!config->fetch(config->fetch_context, address, size, &raw)) {
      return invalid_result(AP_M68851_SEARCH_FAULT_BUS_ERROR, levels,
                           &acc, path, path_length);
    }
    levels++;

    const ap_m68851_descriptor_t d =
        (size == 4u) ? ap_m68851_short_table_descriptor((uint32_t)raw)
                     : ap_m68851_long_table_descriptor(raw);
    /* A function code lookup can only produce a table or a page descriptor --
     * the indices have not started, so it can never be the indirect case --
     * and either way Figure 5-27 accumulates from it. The page arm below
     * re-decodes at page format and accumulates that instead, because only the
     * page format has `G`, `CI` and `L`. */
    if (d.dt != AP_M68851_DT_PAGE_DESCRIPTOR) {
      accumulate(&acc, &d, size);
    }
    /* Recorded only now, because until the type field is decoded there is no
     * telling whether this descriptor carries an `M` bit -- and an invalid one
     * is not recorded at all: nothing was translated through it, so it is not
     * what §5.1.5.3.11 calls used. The pointers *above* it still are, which is
     * the case the manual singles out. */
    if (d.dt != AP_M68851_DT_INVALID) {
      visit(path, &path_length, address, size, raw,
            d.dt == AP_M68851_DT_PAGE_DESCRIPTOR);
    }

    switch (d.dt) {
    case AP_M68851_DT_PAGE_DESCRIPTOR: {
      /* "TYPE <- 'EARLY'". Re-decode at the right format: a page descriptor and
       * a table descriptor do not share a layout. */
      const ap_m68851_descriptor_t page =
          (size == 4u) ? ap_m68851_short_page_descriptor((uint32_t)raw)
                       : ap_m68851_long_page_descriptor(raw, true);
      out.type = AP_M68851_SEARCH_TYPE_EARLY;
      out.levels = levels;
      copy_path(&out, path, path_length);
      accumulate(&acc, &page, size);
      take_page(&out, &page, &acc);
      return out;
    }
    case AP_M68851_DT_INVALID:
      return invalid_result(AP_M68851_SEARCH_FAULT_INVALID_DESCRIPTOR, levels,
                           &acc, path, path_length);
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
    /* `PTEST`'s ceiling, checked before the fetch that would exceed it. */
    if (config->max_levels != 0u && levels >= config->max_levels) {
      out.type = AP_M68851_SEARCH_TYPE_TRUNCATED;
      out.levels = levels;
      copy_path(&out, path, path_length);
      take_acc(&out, &acc);
      return out;
    }

    const unsigned index = ap_m68851_search_index(tc, logical_address, x);

    /* "PERFORM LIMIT CHECK". The limit bounding this index belongs to the
     * descriptor above, which is the root pointer on the first pass. */
    if (limit_violated(config, previous_is_root, last_size,
                       previous_lower_limit, previous_limit, index)) {
      return invalid_result(AP_M68851_SEARCH_FAULT_LIMIT_VIOLATION, levels,
                           &acc, path, path_length);
    }

    uint64_t raw = 0;
    const uint32_t address = table + index * size;
    if (!config->fetch(config->fetch_context, address, size, &raw)) {
      return invalid_result(AP_M68851_SEARCH_FAULT_BUS_ERROR, levels,
                           &acc, path, path_length);
    }
    levels++;

    const ap_m68851_descriptor_t d =
        (size == 4u) ? ap_m68851_short_table_descriptor((uint32_t)raw)
                     : ap_m68851_long_table_descriptor(raw);
    if (d.dt != AP_M68851_DT_INVALID) {
      visit(path, &path_length, address, size, raw,
            d.dt == AP_M68851_DT_PAGE_DESCRIPTOR);
    }

    if (d.dt == AP_M68851_DT_INVALID) {
      return invalid_result(AP_M68851_SEARCH_FAULT_INVALID_DESCRIPTOR, levels,
                           &acc, path, path_length);
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
      copy_path(&out, path, path_length);
      accumulate(&acc, &page, size);
      take_page(&out, &page, &acc);
      return out;
    }

    /* Are there more levels? "x = 'D'" or the next TIx is zero means the table
     * indices are exhausted, and a valid descriptor there is an *indirect*
     * descriptor rather than another table. Asked before the accumulation
     * below, because the answer decides whether this descriptor *has* any
     * protection to accumulate. */
    const bool exhausted = (x >= 3u) || (tc->table_index[x + 1u] == 0u);

    /* **An indirect descriptor contributes nothing.**
     *
     * Figure 5-27 draws one accumulation path for every `DT = '4 BYTE' OR
     * '8 BYTE'` descriptor and does not single the indirect case out, which
     * read literally would accumulate from bits Figures 5-17 and 5-18 say are
     * not protection at all: a **short** indirect descriptor's Figure 5-17 puts
     * the descriptor address at bits 31-2, so what the table format reads as
     * `WP` at bit 2 is the address's own least significant bit; a **long**
     * one's Figure 5-18 leaves everything above `DT` unused, so its `RAL` would
     * read as `$0` -- the *most* privileged level -- and lock every task out of
     * a page the tables map.
     *
     * `ap_m68851_descriptor.c` already says which way this goes: "an indirect
     * descriptor carries no protection of its own -- the descriptor it names
     * carries it, which is the point of the indirection." So the two figures
     * win over the flowchart's undifferentiated branch, and the alternative is
     * not a subtle difference but a mapping no access level can reach.
     *
     * Until this was written the `WP` half happened anyway, with a comment
     * saying "accumulate its protection either way" -- so roughly half of all
     * short indirect targets came back write-protected by an address bit. */
    if (!exhausted) {
      accumulate(&acc, &d, size);
    }

    last_size = size;
    size = (d.dt == AP_M68851_DT_VALID_4_BYTE) ? 4u : 8u;
    table = d.address;
    /* This descriptor's limit bounds the next level's index. A short-format
     * descriptor has none, and `limit_violated` refuses the check on
     * `last_size == 4` rather than on these values -- so what they hold in that
     * case never matters. */
    previous_lower_limit = d.lower_limit;
    previous_limit = d.limit;

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
                           &acc, path, path_length);
    }
    levels++;
    visit(path, &path_length, indirect.address, size, raw, true);

    const ap_m68851_descriptor_t probe =
        (size == 4u) ? ap_m68851_short_table_descriptor((uint32_t)target)
                     : ap_m68851_long_table_descriptor(target);

    /* "DT = 'PAGE DESCRIPTOR'" is accepted and "OTHERWISE" is invalid: Figure
     * 5-10's two illegal cells, which is what stops a chain of indirections. */
    if (probe.dt != AP_M68851_DT_PAGE_DESCRIPTOR) {
      return invalid_result(AP_M68851_SEARCH_FAULT_INVALID_DESCRIPTOR, levels,
                           &acc, path, path_length);
    }

    const ap_m68851_descriptor_t page =
        (size == 4u) ? ap_m68851_short_page_descriptor((uint32_t)target)
                     : ap_m68851_long_page_descriptor(target, false);
    out.type = AP_M68851_SEARCH_TYPE_INDIRECT;
    out.levels = levels;
    copy_path(&out, path, path_length);
    /* The page the indirection named is where the protection is. */
    accumulate(&acc, &page, size);
    take_page(&out, &page, &acc);
    return out;
  }
}

ap_m68851_protection_t
ap_m68851_search_protection(const ap_m68851_search_result_t *result,
                            const ap_m68851_access_t *access) {
  if (result == NULL || access == NULL) {
    return AP_M68851_PROTECTION_OK;
  }

  /* §6.3.1.3, and it is not gated on access levels: the supervisor-only
   * attribute is a function code test, and `FC[2]` is presented on every bus
   * cycle whether or not `ALC` enables anything.
   *
   * "If bit FC[2] of a logical address is zero and a set S bit is encountered
   * during the table search in a long format descriptor for that address, an
   * ATC entry will be made with its internal bus error (B) bit set." */
  if (result->supervisor_only && (access->function_code & 0x4u) == 0u) {
    return AP_M68851_PROTECTION_SUPERVISOR_ONLY;
  }

  /* Everything below is "if access levels are enabled". `ALC = $0` is the
   * reset state and the only one a machine without the MC68020's module calls
   * ever leaves it in, so this is the ordinary exit. */
  if (access->access_levels_enabled == 0u) {
    return AP_M68851_PROTECTION_OK;
  }

  /* §6.3.1.4's first paragraph, and §7.2.2 states the same rule from the task's
   * side: "the access level encoded in the highest-order logical address bits
   * must be greater than (less privileged) or equal to the value in CAL;
   * otherwise, the MC68851 aborts the access."
   *
   * Numerically less is *more* privileged, so an address claiming a level below
   * `CAL` is claiming privilege the task does not hold -- and this one is
   * checked against a register rather than against the tables, which is why it
   * caches nothing and why `PTEST` cannot see it. */
  if (access->access_level < access->current_access_level) {
    return AP_M68851_PROTECTION_ABOVE_CAL;
  }

  /* §6.3.1.4's second paragraph, `RAL` half only -- the `WAL` half is
   * §6.3.1.5's write protection and lives in `ap_m68851_search_not_writeable`.
   * The header above says why the split follows the ATC's own bits.
   *
   * §7.2.3.1's third worked example is what pins `RAL` covering writes too:
   * "consider a page with a RAL encoding of five and a WAL encoding of six; a
   * task must use an access level of five or lower to read from **or write to**
   * this page. An attempt to write to this page using an access level of six
   * would be aborted by the MC68851 **since it is less privileged than the read
   * access level of the page**." A `WAL` looser than the `RAL` buys nothing,
   * because "denying a task read access to an area implies that the task also
   * does not have sufficient privilege to write to that area ... regardless of
   * the write access level associated with that area." */
  if (access->access_level > result->read_access_level) {
    return AP_M68851_PROTECTION_ACCESS_LEVEL;
  }

  return AP_M68851_PROTECTION_OK;
}

bool ap_m68851_search_not_writeable(const ap_m68851_search_result_t *result,
                                    const ap_m68851_access_t *access) {
  if (result == NULL || access == NULL) {
    return false;
  }
  /* "If any descriptor encountered in the search contained a set WP bit" --
   * §5.1.6's "if a WP bit is set for the page at any level, the page will not
   * be writable **for any access level**", so this wins outright. */
  if (result->write_protect) {
    return true;
  }
  /* "Or if the address tested exceeded the WAL field of any long descriptor
   * encountered." `ACC_STATUS[WAL]` is already that minimum. */
  return access->access_levels_enabled != 0u &&
         access->access_level > result->write_access_level;
}

bool ap_m68851_rmc_denied(bool resident, bool modified, bool write_protect) {
  /* §4.2.3.3's condition (6), all three disjuncts. */
  return !resident || !modified || write_protect;
}

unsigned ap_m68851_status_writes(const ap_m68851_search_result_t *result,
                                 bool write_access,
                                 ap_m68851_status_write_t *out,
                                 unsigned capacity) {
  unsigned count = 0;
  if (result == NULL || out == NULL) {
    return 0u;
  }
  for (unsigned i = 0; i < result->path_length && count < capacity; i++) {
    const ap_m68851_visited_t *visited = &result->path[i];
    const bool used = (visited->status & AP_M68851_STATUS_USED) != 0u;
    const bool modified = (visited->status & AP_M68851_STATUS_MODIFIED) != 0u;

    /* `M` belongs to page descriptors only, and only a write sets it. */
    const bool needs_used = !used;
    const bool needs_modified = visited->is_page && write_access && !modified;
    if (!needs_used && !needs_modified) {
      /* "Only performing write cycles to modify these bits are required" --
       * a descriptor already carrying the right bits costs no bus cycle, which
       * is why this loop produces fewer writes than there are levels. */
      continue;
    }

    uint8_t value = visited->status | AP_M68851_STATUS_USED;
    if (visited->is_page && write_access) {
      value = (uint8_t)(value | AP_M68851_STATUS_MODIFIED);
    }

    /* When the same cycle sets `M` from clear there is nothing to preserve, so
     * a plain write serves; when `U` must be set without disturbing an `M` the
     * cycle is not itself setting, the byte has to be read back and merged.
     * That is the whole reason the part has a read-modify-write at all here:
     * two MMUs sharing a translation tree must not lose each other's `M`. */
    const bool setting_modified_from_clear =
        visited->is_page && write_access && !modified;
    const bool read_modify_write =
        visited->is_page && needs_used && !setting_modified_from_clear;

    out[count++] = (ap_m68851_status_write_t){
        /* The status byte, not the descriptor: `address + 3` in both formats. */
        .address = visited->address + 3u,
        .value = value,
        .read_modify_write = read_modify_write,
    };
  }
  return count;
}
