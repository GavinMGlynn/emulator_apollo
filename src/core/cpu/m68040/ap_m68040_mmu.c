#include "cpu/m68040/ap_m68040_mmu.h"

ap_m68040_mmu_result_t ap_m68040_mmu_translate(const ap_m68040_mmu_t *mmu,
                                               uint32_t logical,
                                               unsigned function_code,
                                               bool write,
                                               ap_m68040_fetch_fn fetch,
                                               void *fetch_context) {
  ap_m68040_mmu_result_t out = {.status = AP_M68040_MMU_UNTRANSLATED,
                                .physical = logical,
                                .cache_mode = AP_M68040_CM_CACHABLE_WRITE_THROUGH};

  /* §3.1.3, and it is checked before the enable for that reason: the TTRs
   * "operate independently of the E-bit in the TCR". */
  for (unsigned i = 0; i < 2u; i++) {
    const ap_m68040_ttr_t ttr = ap_m68040_ttr_decode(mmu->ttr[i]);
    if (!ap_m68040_ttr_matches(&ttr, logical, function_code)) {
      continue;
    }
    /* "Write protection is the only attribute a transparent block carries that
     * can fault", and it faults the same way a page's does. */
    if (write && ttr.write_protect) {
      out.status = AP_M68040_MMU_FAULT;
      return out;
    }
    out.status = AP_M68040_MMU_TRANSPARENT;
    out.cache_mode = ttr.cache_mode;
    return out;
  }

  const ap_m68040_tcr_t tcr =
      ap_m68040_tcr_decode((uint16_t)(*mmu->tc & 0xFFFFu));
  if (!tcr.enable) {
    /* §3.5: with translation disabled and no TTR match the access uses the
     * logical address as physical, with the default attributes §3.1.3 gives --
     * "the caching mode is cachable/write-through, write protection is
     * disabled". Already the initialised value. */
    return out;
  }

  const bool supervisor = (function_code & 4u) != 0u;

  const unsigned way =
      ap_m68040_atc_lookup(mmu->atc, logical, supervisor, tcr.page_size);
  if (way < AP_M68040_ATC_WAYS) {
    const unsigned set = ap_m68040_atc_set(logical, tcr.page_size);
    const ap_m68040_atc_entry_t *entry = &mmu->atc->entry[set][way];
    /* "R ... set if the table search successfully completes without
     * encountering either a nonresident page or a transfer error" -- so a
     * cached *failure* faults here without a second search, which is the whole
     * point of caching it. */
    if (!entry->resident || (entry->supervisor && !supervisor) ||
        (write && entry->write_protect)) {
      out.status = AP_M68040_MMU_FAULT;
      return out;
    }
    out.status = AP_M68040_MMU_TRANSLATED;
    out.physical = entry->physical_address |
                   ap_m68040_page_offset(logical, tcr.page_size);
    out.cache_mode = entry->cache_mode;
    return out;
  }

  /* A miss pays for a table search, from the root the access's privilege
   * selects. §3.2: "the supervisor root pointer ... is used for supervisor
   * accesses and the user root pointer for user accesses". */
  const ap_m68040_search_config_t config = {
      .root_pointer = supervisor ? *mmu->srp : *mmu->urp,
      .page_size = tcr.page_size,
      .fetch = fetch,
      .fetch_context = fetch_context};
  const ap_m68040_search_result_t search = ap_m68040_search(&config, logical);
  out.fetches = search.fetches;

  /* **`ap_m68040_search` returns the *whole* physical address**, frame and page
   * offset both -- `m68040_search_suite` pins it, `0xFFF` translating to
   * `0x50FFF`. An ATC entry holds the **frame**, because Figure 3-21 caches a
   * page and a later access to a different word in it supplies its own offset.
   *
   * Masking rather than trusting the OR: storing the full address here and
   * ORing the offset on a hit is wrong in a way that hides, because the access
   * that filled the entry ORs *its own* offset back on and gets the right
   * answer. It shows up only on the second access to the same page, at a
   * different offset -- which is what caught it. */
  const uint32_t offset_mask =
      tcr.page_size == AP_M68040_PAGE_8K ? 0x1FFFu : 0x0FFFu;
  const uint32_t frame = search.physical_address & ~offset_mask;

  /* "When an invalid descriptor is encountered, an ATC entry is created for the
   * logical address with the resident bit in the MMUSR clear" -- so every
   * outcome but a bus error is cached, failures included. */
  if (search.status != AP_M68040_SEARCH_BUS_ERROR) {
    const ap_m68040_atc_entry_t entry = {
        .valid = true,
        .global = search.global,
        .supervisor_space = supervisor,
        .logical_tag = ap_m68040_atc_tag(logical, tcr.page_size),
        .user_attribute_1 = search.user_attribute_1,
        .user_attribute_0 = search.user_attribute_0,
        .supervisor = search.supervisor,
        .cache_mode = search.cache_mode,
        /* Set on a write so a second write to the page does not search again.
         * The *table* is not updated -- see this module's header. */
        .modified = search.modified || write,
        .write_protect = search.write_protect,
        .resident = search.status == AP_M68040_SEARCH_RESIDENT,
        .physical_address = frame};
    const unsigned fill =
        ap_m68040_atc_select_way(mmu->atc, logical, tcr.page_size);
    ap_m68040_atc_fill(mmu->atc, fill, logical, tcr.page_size, entry);
    ap_m68040_atc_tick(mmu->atc);
    out.filled = true;
  }

  if (search.status != AP_M68040_SEARCH_RESIDENT ||
      (search.supervisor && !supervisor) ||
      (write && search.write_protect)) {
    out.status = AP_M68040_MMU_FAULT;
    return out;
  }

  out.status = AP_M68040_MMU_TRANSLATED;
  /* Already whole: see the note above the mask. */
  out.physical = search.physical_address;
  out.cache_mode = search.cache_mode;
  return out;
}
