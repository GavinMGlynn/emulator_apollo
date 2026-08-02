/* The MC68851 as a fitted part. See the header; the side effects of `PMOVE`
 * come from Appendix A's PMOVE page and are most of what the instruction does. */

#include <string.h>

#include "cpu/m68851/ap_m68851.h"

void ap_m68851_reset(ap_m68851_t *mmu) {
  const unsigned cpid = mmu->cpid;
  /* §8.1: "The BPE bit is cleared at reset; the skip count field is not." So
   * the counts have to be carried across the zeroing -- a reset that cleared
   * them would silently rearm every breakpoint at its first fire. */
  unsigned counts[AP_M68851_BREAKPOINTS];
  uint16_t opcodes[AP_M68851_BREAKPOINTS];
  for (unsigned i = 0; i < AP_M68851_BREAKPOINTS; i++) {
    counts[i] = mmu->breakpoint[i].skip_count;
    opcodes[i] = mmu->breakpoint[i].replacement_opcode;
  }

  memset(mmu, 0, sizeof *mmu);
  mmu->cpid = cpid;
  for (unsigned i = 0; i < AP_M68851_BREAKPOINTS; i++) {
    mmu->breakpoint[i].skip_count = counts[i];
    mmu->breakpoint[i].replacement_opcode = opcodes[i];
  }
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
  return ap_m68851_pmove_write_numbered(mmu, preg, 0u, value);
}

ap_m68851_status_t ap_m68851_pmove_write_numbered(ap_m68851_t *mmu,
                                                  ap_m68851_preg_t preg,
                                                  unsigned number,
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
    mmu->breakpoint[number & 0x7u].replacement_opcode = (uint16_t)value;
    return AP_M68851_EXECUTED;

  case AP_M68851_PREG_BAC:
    /* "All unimplemented bits (bits [8-14]) are always read as zeros and must
     * be written as zeros." */
    mmu->breakpoint[number & 0x7u].enable = (value & 0x8000u) != 0u;
    mmu->breakpoint[number & 0x7u].skip_count = (unsigned)(value & 0xFFu);
    return AP_M68851_EXECUTED;

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

uint64_t ap_m68851_pmove_read_numbered(const ap_m68851_t *mmu,
                                       ap_m68851_preg_t preg,
                                       unsigned number) {
  switch (preg) {
  case AP_M68851_PREG_BAD:
    return mmu->breakpoint[number & 0x7u].replacement_opcode;
  case AP_M68851_PREG_BAC: {
    const ap_m68851_breakpoint_t *b = &mmu->breakpoint[number & 0x7u];
    return (b->enable ? 0x8000u : 0u) | (b->skip_count & 0xFFu);
  }
  /* Every other register is numberless; deferring keeps one description of
   * each rather than two that could drift apart. */
  case AP_M68851_PREG_TC:
  case AP_M68851_PREG_DRP:
  case AP_M68851_PREG_SRP:
  case AP_M68851_PREG_CRP:
  case AP_M68851_PREG_CAL:
  case AP_M68851_PREG_VAL:
  case AP_M68851_PREG_SCC:
  case AP_M68851_PREG_AC:
  case AP_M68851_PREG_PSR:
  case AP_M68851_PREG_PCSR:
  case AP_M68851_PREG_UNDEFINED:
    break;
  }
  return ap_m68851_pmove_read(mmu, preg);
}

ap_m68851_breakpoint_result_t
ap_m68851_breakpoint_acknowledge(ap_m68851_t *mmu, unsigned number,
                                 uint16_t *replacement_opcode) {
  ap_m68851_breakpoint_t *b = &mmu->breakpoint[number & 0x7u];

  /* §8.1: the bus error is reached "either because the corresponding enable bit
   * being clear or the skip count having been decremented to zero" -- two
   * routes to one outcome, which is why a disabled breakpoint and an exhausted
   * one are indistinguishable to the CPU. */
  if (!b->enable || b->skip_count == 0u) {
    return AP_M68851_BREAKPOINT_BUS_ERROR;
  }

  /* "If, at the beginning of a breakpoint acknowledge cycle, the breakpoint
   * skip count is non-zero, the MC68851 will return the corresponding
   * replacement opcode and assert DSACKx. During the breakpoint cycle, the skip
   * count is decremented by one." */
  *replacement_opcode = b->replacement_opcode;
  b->skip_count--;
  return AP_M68851_BREAKPOINT_REPLACED;
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

/* Shared by `PLOAD` and `PTEST`: build a search for one address. */
static ap_m68851_search_config_t search_for(const ap_m68851_t *mmu,
                                            unsigned function_code,
                                            unsigned max_levels,
                                            ap_m68851_fetch_fn fetch,
                                            void *fetch_context,
                                            bool *root_is_drp) {
  const ap_m68851_rp_t *root = select_root(mmu, function_code, root_is_drp);
  return (ap_m68851_search_config_t){
      .tc = &mmu->tc,
      .root = root,
      .root_is_drp = *root_is_drp,
      .fetch = fetch,
      .fetch_context = fetch_context,
      .max_levels = max_levels,
  };
}

ap_m68851_status_t ap_m68851_pload(ap_m68851_t *mmu,
                                   const ap_m68851_instruction_t *instruction,
                                   unsigned function_code, uint32_t address,
                                   ap_m68851_fetch_fn fetch,
                                   void *fetch_context) {
  if (instruction->opcode != AP_M68851_OP_PLOAD) {
    return AP_M68851_TAKE_LINE_F;
  }
  if (!mmu->tc.enable) {
    /* §6.1.3.1: with `E` clear the part "terminates all PTEST, PLOAD, and
     * CALLM/RTM (type $1) instructions with an exception". */
    return AP_M68851_CONFIGURATION_ERROR;
  }

  bool root_is_drp = false;
  const ap_m68851_search_config_t config = search_for(
      mmu, function_code, 0u, fetch, fetch_context, &root_is_drp);
  const ap_m68851_search_result_t found =
      ap_m68851_search(&config, address, function_code);

  /* "PLOADW causes U and M bits in the translation tables to be updated as if a
   * write access had taken place." The write-back of `U` and `M` into the
   * tables is not modelled -- see `PROJECT_STATUS.md` -- but the direction
   * still decides the entry's `M`, which is what a later write through this
   * entry consults. */
  const ap_m68851_atc_entry_t entry = {
      .logical_address = address,
      .function_code = function_code,
      .task_alias = mmu->atc.task_alias,
      .shared_globally = found.shared_globally,
      .physical_address = found.physical_address,
      .write_protect = found.write_protect,
      .cache_inhibit = found.cache_inhibit,
      .modified = found.modified || !instruction->read_from_mmu,
      .gate = found.gate,
      .lock = found.lock,
      .bus_error = found.type == AP_M68851_SEARCH_TYPE_INVALID,
  };
  ap_m68851_atc_fill(&mmu->atc, ap_m68851_atc_select_victim(&mmu->atc), entry);
  return AP_M68851_EXECUTED;
}

ap_m68851_status_t ap_m68851_ptest(ap_m68851_t *mmu,
                                   const ap_m68851_instruction_t *instruction,
                                   unsigned function_code, uint32_t address,
                                   ap_m68851_fetch_fn fetch,
                                   void *fetch_context) {
  if (instruction->opcode != AP_M68851_OP_PTEST) {
    return AP_M68851_TAKE_LINE_F;
  }
  if (!mmu->tc.enable) {
    return AP_M68851_CONFIGURATION_ERROR;
  }

  ap_m68851_psr_t psr = {0};

  if (instruction->level == 0u) {
    /* "Search ATC only." A different operation, not a shallow search: nothing
     * is walked and most bits report the entry rather than the tables. */
    const ap_m68851_atc_entry_t *hit = ap_m68851_atc_lookup(
        &mmu->atc, address, function_code, ap_m68851_tc_page_bytes(&mmu->tc));
    if (hit == NULL) {
      /* "Set if ... the PTEST instruction requested a level zero search (search
       * ATC only) and no corresponding entry was found in the ATC." A miss is
       * reported as invalid, which is the same bit a genuinely absent
       * translation sets -- the instruction cannot tell them apart and neither
       * should this. */
      psr.invalid = true;
    } else {
      psr.bus_error = hit->bus_error;
      psr.invalid = hit->bus_error;
      psr.write_protected = hit->write_protect;
      psr.modified = hit->modified;
      psr.gate = hit->gate;
      psr.globally_sharable = hit->shared_globally;
    }
    /* "For the PTEST instruction with a level specification of zero, this field
     * is always zero", and `L`, `S` and `A` "always clear". */
    psr.levels = 0u;
    mmu->psr = psr;
    return AP_M68851_EXECUTED;
  }

  bool root_is_drp = false;
  const ap_m68851_search_config_t config = search_for(
      mmu, function_code, instruction->level, fetch, fetch_context,
      &root_is_drp);
  const ap_m68851_search_result_t found =
      ap_m68851_search(&config, address, function_code);

  psr.bus_error = found.fault == AP_M68851_SEARCH_FAULT_BUS_ERROR;
  psr.limit_violation = found.fault == AP_M68851_SEARCH_FAULT_LIMIT_VIOLATION;
  /* "Set if the address has no translation in the table (i.e., an 'invalid'
   * descriptor type, bus error, or limit violation was encountered during the
   * table search)". A search stopped by the level ceiling has found no such
   * thing, so it is not invalid. */
  psr.invalid = found.type == AP_M68851_SEARCH_TYPE_INVALID;
  psr.write_protected = found.write_protect;
  psr.modified = found.modified;
  psr.gate = found.gate;
  psr.globally_sharable = found.shared_globally;
  /* "Set to the number of tables used in the translation of an address." */
  psr.levels = found.levels & 0x7u;
  mmu->psr = psr;
  return AP_M68851_EXECUTED;
}

ap_m68851_pvalid_result_t ap_m68851_pvalid(const ap_m68851_t *mmu,
                                           uint32_t operand,
                                           bool use_surrogate,
                                           uint8_t surrogate) {
  /* §6.1.7.1: "the PVALID instruction will always cause an exception when MC is
   * clear." Checked first, because with module operations disabled the access
   * levels mean nothing to compare. */
  if (!mmu->ac.module_control) {
    return AP_M68851_PVALID_ACCESS_VIOLATION;
  }

  const unsigned bits = (unsigned)mmu->ac.access_level_control;
  if (bits == 0u) {
    /* `ALC = $0` disables access level checking, so no address is more
     * privileged than another and nothing can violate. */
    return AP_M68851_PVALID_OK;
  }

  /* "The number of bits compared is defined by the ALC field of the AC
   * register", taken from the top of the logical address -- which is where the
   * access level lives, and why `CAL` and `VAL` keep theirs in their upper
   * bits. */
  const unsigned shift = 32u - bits;
  const unsigned operand_level = (unsigned)(operand >> shift);
  const unsigned against =
      ap_m68851_access_level_decode(use_surrogate ? surrogate : mmu->val) >>
      (3u - bits);

  /* "If the operand bits are arithmetically less than the VAL (or surrogate
   * VAL) bits, this instruction causes a trap." Lower is more privileged, so
   * this refuses a pointer more privileged than the caller. */
  return (operand_level < against) ? AP_M68851_PVALID_ACCESS_VIOLATION
                                   : AP_M68851_PVALID_OK;
}
