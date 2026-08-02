/* The MC68851 as a fitted part. See the header; the side effects of `PMOVE`
 * come from Appendix A's PMOVE page and are most of what the instruction does. */

#include <string.h>

#include "cpu/m68851/ap_m68851.h"

void ap_m68851_reset(ap_m68851_t *mmu) {
  const unsigned cpid = mmu->cpid;
  memset(mmu, 0, sizeof *mmu);
  mmu->cpid = cpid;
  /* `E` clear and `ALC` zero fall out of the zeroing, which is what the manual
   * describes rather than a coincidence: a reset part translates nothing and
   * checks no access levels. */
  ap_m68851_atc_flush(&mmu->atc);
}

/* Which root pointer a function code selects, and whether it is the DRP -- the
 * search needs both, and the second only to suppress a limit check. */
static const ap_m68851_rp_t *select_root(const ap_m68851_t *mmu,
                                         unsigned function_code,
                                         bool *is_drp) {
  const ap_m68851_root_t root = ap_m68851_select_root(
      function_code, mmu->tc.supervisor_root_pointer_enable);
  *is_drp = (root == AP_M68851_ROOT_DRP);
  switch (root) {
  case AP_M68851_ROOT_SRP:
    return &mmu->srp;
  case AP_M68851_ROOT_DRP:
    return &mmu->drp;
  case AP_M68851_ROOT_CRP:
    break;
  }
  return &mmu->crp;
}

ap_m68851_translation_t ap_m68851_translate(ap_m68851_t *mmu,
                                            uint32_t logical_address,
                                            unsigned function_code,
                                            bool is_write,
                                            ap_m68851_fetch_fn fetch,
                                            void *fetch_context) {
  ap_m68851_translation_t out = {0};

  if (!mmu->tc.enable) {
    /* §6.1.3.1: "when the translation mechanism is disabled, logical addresses
     * are routed directly from the logical address bus to the physical address
     * bus". Not an identity *mapping* -- there is no mapping at all, and no ATC
     * entry is made, which is why a disabled MMU cannot be modelled as a
     * transparent one that caches. */
    out.physical_address = logical_address;
    return out;
  }

  const uint32_t page_bytes = ap_m68851_tc_page_bytes(&mmu->tc);
  const uint32_t offset_mask = page_bytes - 1u;

  const ap_m68851_atc_entry_t *hit = ap_m68851_atc_lookup(
      &mmu->atc, logical_address, function_code, page_bytes);
  if (hit != NULL) {
    out.cache_hit = true;
    out.cache_inhibit = hit->cache_inhibit;
    if (hit->bus_error) {
      /* A cached denial. The entry matched precisely so this could be found. */
      out.status = AP_M68851_TRANSLATE_BUS_ERROR;
      return out;
    }
    if (is_write && hit->write_protect) {
      out.status = AP_M68851_TRANSLATE_WRITE_PROTECTED;
      return out;
    }
    out.physical_address = (hit->physical_address & ~offset_mask) |
                           (logical_address & offset_mask);
    return out;
  }

  /* A miss: walk the tables. */
  bool root_is_drp = false;
  const ap_m68851_rp_t *root =
      select_root(mmu, function_code, &root_is_drp);
  const ap_m68851_search_config_t config = {
      .tc = &mmu->tc,
      .root = root,
      .root_is_drp = root_is_drp,
      .fetch = fetch,
      .fetch_context = fetch_context,
  };
  const ap_m68851_search_result_t found =
      ap_m68851_search(&config, logical_address, function_code);

  /* Build the entry the search earned, denial or not. */
  ap_m68851_atc_entry_t entry = {
      .logical_address = logical_address,
      .function_code = function_code,
      .task_alias = mmu->atc.task_alias,
      .shared_globally = found.shared_globally,
      .physical_address = found.physical_address,
      .write_protect = found.write_protect,
      .cache_inhibit = found.cache_inhibit,
      .modified = found.modified,
      .gate = found.gate,
      .lock = found.lock,
      .bus_error = found.type == AP_M68851_SEARCH_TYPE_INVALID,
  };
  ap_m68851_atc_fill(&mmu->atc, ap_m68851_atc_select_victim(&mmu->atc), entry);

  /* `PSR` records what the search met, for a later `PTEST` to read. */
  mmu->psr.bus_error = found.fault == AP_M68851_SEARCH_FAULT_BUS_ERROR;
  mmu->psr.limit_violation =
      found.fault == AP_M68851_SEARCH_FAULT_LIMIT_VIOLATION;
  mmu->psr.invalid = found.type == AP_M68851_SEARCH_TYPE_INVALID;
  mmu->psr.write_protected = found.write_protect;
  mmu->psr.modified = found.modified;
  mmu->psr.gate = found.gate;
  mmu->psr.globally_sharable = found.shared_globally;
  mmu->psr.levels = found.levels & 0x7u;

  out.cache_inhibit = entry.cache_inhibit;
  if (entry.bus_error) {
    out.status = AP_M68851_TRANSLATE_BUS_ERROR;
    return out;
  }
  if (is_write && entry.write_protect) {
    out.status = AP_M68851_TRANSLATE_WRITE_PROTECTED;
    return out;
  }
  out.physical_address = (found.physical_address & ~offset_mask) |
                         (logical_address & offset_mask);
  return out;
}

/* Invalidate every entry formed with a given root pointer. The manual's
 * parenthesis -- "(even globally shared)" -- is why this cannot be
 * `ap_m68851_atc_flush_task`: a `PFLUSH` spares shared entries and this does
 * not. Entries are identified by their function code, since that is what chose
 * the root pointer when they were made. */
static void flush_by_root(ap_m68851_t *mmu, ap_m68851_root_t root) {
  for (unsigned i = 0; i < AP_M68851_ATC_ENTRIES; i++) {
    ap_m68851_atc_entry_t *e = &mmu->atc.entry[i];
    if (!e->valid) {
      continue;
    }
    if (ap_m68851_select_root(e->function_code,
                              mmu->tc.supervisor_root_pointer_enable) != root) {
      continue;
    }
    e->valid = false;
    e->history = false;
    e->lock = false;
  }
}

ap_m68851_status_t ap_m68851_pmove_write(ap_m68851_t *mmu,
                                         ap_m68851_preg_t preg,
                                         uint64_t value) {
  switch (preg) {
  case AP_M68851_PREG_TC: {
    const ap_m68851_tc_t written = ap_m68851_tc_decode((uint32_t)value);
    if (!written.enable) {
      /* "Writing a value with its enable bit clear to this register cause a
       * flush of the entire ATC." */
      mmu->tc = written;
      ap_m68851_atc_flush(&mmu->atc);
      return AP_M68851_EXECUTED;
    }
    if (ap_m68851_tc_check(&written) != AP_M68851_TC_OK) {
      /* "If an exception is taken, the TC register is updated with the data
       * except that the E bit is cleared." The write happens; only the enable
       * is dropped, which is what lets software read back what it tried. */
      mmu->tc = written;
      mmu->tc.enable = false;
      return AP_M68851_CONFIGURATION_ERROR;
    }
    mmu->tc = written;
    return AP_M68851_EXECUTED;
  }

  case AP_M68851_PREG_CRP:
    /* "Causes the internal root pointer table to be searched for the new value.
     * If a matching value is not found, an entry ... is selected for
     * replacement, and all ATC entries associated with the replaced entry are
     * invalidated." The root pointer table itself is a `PROVISIONAL` gap --
     * see `PROJECT_STATUS.md` -- so this takes the conservative branch and
     * flushes the current task's entries, which is what a replacement would do
     * when the table holds one live alias. `PCSR` reports it. */
    mmu->crp = ap_m68851_rp_decode(value);
    ap_m68851_atc_flush_task(&mmu->atc, mmu->atc.task_alias);
    mmu->pcsr.flush = true;
    mmu->pcsr.task_alias = mmu->atc.task_alias;
    return AP_M68851_EXECUTED;

  case AP_M68851_PREG_SRP:
    mmu->srp = ap_m68851_rp_decode(value);
    /* "(even globally shared)". */
    flush_by_root(mmu, AP_M68851_ROOT_SRP);
    return AP_M68851_EXECUTED;

  case AP_M68851_PREG_DRP:
    mmu->drp = ap_m68851_rp_decode(value);
    flush_by_root(mmu, AP_M68851_ROOT_DRP);
    return AP_M68851_EXECUTED;

  case AP_M68851_PREG_CAL:
    mmu->cal = (uint8_t)value;
    return AP_M68851_EXECUTED;
  case AP_M68851_PREG_VAL:
    mmu->val = (uint8_t)value;
    return AP_M68851_EXECUTED;
  case AP_M68851_PREG_SCC:
    mmu->scc = (uint8_t)value;
    return AP_M68851_EXECUTED;
  case AP_M68851_PREG_AC:
    mmu->ac = ap_m68851_ac_decode((uint16_t)value);
    return AP_M68851_EXECUTED;
  case AP_M68851_PREG_PSR:
    mmu->psr = ap_m68851_psr_decode((uint16_t)value);
    return AP_M68851_EXECUTED;

  case AP_M68851_PREG_PCSR:
    /* §6.1.2: "The format of this 16-bit **read-only** register." A write is
     * not an error; there is simply nothing to write to. */
    return AP_M68851_EXECUTED;

  case AP_M68851_PREG_BAD:
  case AP_M68851_PREG_BAC:
    /* The breakpoint registers exist to answer the 68020's `BKPT` acknowledge
     * cycle. Not implemented here: the acknowledge path is the CPU's and this
     * part's halves of one mechanism, and the CPU's half landed in Phase 2. */
    return AP_M68851_UNIMPLEMENTED;

  case AP_M68851_PREG_UNDEFINED:
    return AP_M68851_TAKE_LINE_F;
  }
  return AP_M68851_TAKE_LINE_F;
}

uint64_t ap_m68851_pmove_read(const ap_m68851_t *mmu, ap_m68851_preg_t preg) {
  switch (preg) {
  case AP_M68851_PREG_TC:
    return ap_m68851_tc_encode(&mmu->tc);
  case AP_M68851_PREG_CRP:
    return ap_m68851_rp_encode(&mmu->crp);
  case AP_M68851_PREG_SRP:
    return ap_m68851_rp_encode(&mmu->srp);
  case AP_M68851_PREG_DRP:
    return ap_m68851_rp_encode(&mmu->drp);
  case AP_M68851_PREG_CAL:
    return mmu->cal;
  case AP_M68851_PREG_VAL:
    return mmu->val;
  case AP_M68851_PREG_SCC:
    return mmu->scc;
  case AP_M68851_PREG_AC:
    return ap_m68851_ac_encode(&mmu->ac);
  case AP_M68851_PREG_PSR:
    return ap_m68851_psr_encode(&mmu->psr);
  case AP_M68851_PREG_PCSR:
    return ap_m68851_pcsr_encode(&mmu->pcsr);
  case AP_M68851_PREG_BAD:
  case AP_M68851_PREG_BAC:
  case AP_M68851_PREG_UNDEFINED:
    return 0u;
  }
  return 0u;
}

ap_m68851_status_t ap_m68851_pflush(ap_m68851_t *mmu,
                                    const ap_m68851_instruction_t *instruction,
                                    unsigned function_code,
                                    uint32_t address) {
  if (instruction->opcode != AP_M68851_OP_PFLUSH ||
      !ap_m68851_instruction_is_valid(instruction)) {
    return AP_M68851_TAKE_LINE_F;
  }

  if (instruction->pflush_mode == AP_M68851_PFLUSH_ALL) {
    ap_m68851_atc_flush(&mmu->atc);
    return AP_M68851_EXECUTED;
  }

  const bool shared = ap_m68851_pflush_includes_shared(instruction->pflush_mode);
  const bool by_address =
      ap_m68851_pflush_uses_address(instruction->pflush_mode);
  const uint32_t page_mask = ~(ap_m68851_tc_page_bytes(&mmu->tc) - 1u);

  for (unsigned i = 0; i < AP_M68851_ATC_ENTRIES; i++) {
    ap_m68851_atc_entry_t *e = &mmu->atc.entry[i];
    if (!e->valid) {
      continue;
    }
    /* "for all entries whose task alias matches the task alias currently active
     * when the instruction is executed" -- so a flush is scoped to the running
     * task even before the shared question arises. */
    if (e->task_alias != mmu->atc.task_alias && !e->shared_globally) {
      continue;
    }
    /* "ATC entries whose SG bit is set will not be invalidated unless the
     * PFLUSHS is specified." */
    if (e->shared_globally && !shared) {
      continue;
    }
    if (!ap_m68851_pflush_matches_fc(instruction->mask, function_code,
                                     e->function_code)) {
      continue;
    }
    if (by_address &&
        (e->logical_address & page_mask) != (address & page_mask)) {
      continue;
    }
    e->valid = false;
    e->history = false;
    e->lock = false;
  }
  return AP_M68851_EXECUTED;
}
