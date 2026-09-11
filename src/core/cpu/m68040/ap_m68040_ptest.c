#include "cpu/m68040/ap_m68040_ptest.h"

ap_m68040_ptest_result_t ap_m68040_ptest(const ap_m68040_mmu_t *mmu,
                                         uint32_t logical,
                                         unsigned function_code, bool write,
                                         ap_m68040_fetch_fn fetch,
                                         ap_m68040_update_fn update,
                                         void *context) {
  ap_m68040_ptest_result_t out = {.defined = false};

  /* "A PTEST instruction with a DFC value of 0, 3, 4, or 7 is undefined and
   * will return an unknown value in the MMUSR." The four are exactly the codes
   * an ordinary access cannot generate -- `[040]` Table 5-4 -- so this is the
   * instruction declining to name a space the MMU has no tables for. */
  if (function_code != 1u && function_code != 2u && function_code != 5u &&
      function_code != 6u) {
    return out;
  }

  /* The first of the four terminating conditions, and it is first for the same
   * reason it is first in an ordinary access: §3.1.3's transparent translation
   * registers "operate independently of the E-bit in the TCR". */
  for (unsigned i = 0; i < 2u; i++) {
    const ap_m68040_ttr_t ttr = ap_m68040_ttr_decode(mmu->ttr[i]);
    if (!ap_m68040_ttr_matches(&ttr, logical, function_code)) {
      continue;
    }
    /* "Set if the PTEST address matches an instruction or data transparent
     * translation register and the R-bit is set; all other bits are zero." A
     * write-protected TTR does not change that: `W` is described as set from
     * "the descriptors encountered during the table search", and no table was
     * searched. */
    out.mmusr = ap_m68040_mmusr_transparent();
    out.defined = true;
    return out;
  }

  const ap_m68040_tcr_t tcr =
      ap_m68040_tcr_decode((uint16_t)(*mmu->tc & 0xFFFFu));
  if (!tcr.enable) {
    /* `[040]` §3.1: "PTEST results are undefined if the MMU is disabled and no
     * table search occurs" -- and both halves of that condition now hold, the
     * TTR loop above having found nothing. */
    return out;
  }

  const bool supervisor = (function_code & 4u) != 0u;

  /* "A matching entry in the address translation cache (data or instruction)
   * specified by the function code will be flushed by PTEST." Before the
   * search, so the search cannot be short-circuited by the entry that is about
   * to stop existing. */
  /* `false`: §3.7.3's flush carries no global qualifier -- "a matching entry in
   * the address translation cache ... specified by the function code will be
   * flushed by PTEST", with no exception for `G`. */
  ap_m68040_atc_flush_page(mmu->atc, logical, supervisor, tcr.page_size, false);

  /* "The PTESTR instruction simulates a read access and sets the U-bit in each
   * descriptor during table searches; PTESTW simulates a write access and also
   * sets the M-bit in the descriptors" -- so the search is configured exactly
   * as the simulated access would be, update callback included. */
  const ap_m68040_search_config_t config = {
      .root_pointer = supervisor ? *mmu->srp : *mmu->urp,
      .page_size = tcr.page_size,
      .write = write,
      .supervisor = supervisor,
      .fetch = fetch,
      .fetch_context = context,
      .update = update,
      .update_context = context};
  const ap_m68040_search_result_t search = ap_m68040_search(&config, logical);
  out.fetches = search.fetches;
  out.defined = true;

  if (search.status == AP_M68040_SEARCH_BUS_ERROR) {
    /* "Set if a transfer error is encountered during the table search for the
     * PTEST instruction. If this bit is set, all other bits are zero." */
    out.mmusr = ap_m68040_mmusr_bus_error();
    return out;
  }

  /* "Completion of PTEST results in the creation of a new address translation
   * cache entry", and an invalid descriptor is a completion: the search's own
   * header carries the sentence, "when an invalid descriptor is encountered, an
   * ATC entry is created for the logical address with the resident bit in the
   * MMUSR clear". */
  const uint32_t offset_mask =
      tcr.page_size == AP_M68040_PAGE_8K ? 0x1FFFu : 0x0FFFu;
  const ap_m68040_atc_entry_t entry = {
      .valid = true,
      .global = search.global,
      .supervisor_space = supervisor,
      .logical_tag = ap_m68040_atc_tag(logical, tcr.page_size),
      .user_attribute_1 = search.user_attribute_1,
      .user_attribute_0 = search.user_attribute_0,
      .supervisor = search.supervisor,
      .cache_mode = search.cache_mode,
      /* "PTESTW ... also sets the M-bit in the descriptors, the address
       * translation cache entry, and the MMU status register." All three, and
       * from one place: the search has already set the descriptor's bit where
       * Table 3-1 allows it, so `search.modified` is what the entry and the
       * register both take. */
      .modified = search.modified,
      .write_protect = search.write_protect,
      .resident = search.status == AP_M68040_SEARCH_RESIDENT,
      .physical_address = search.physical_address & ~offset_mask};
  const unsigned fill =
      ap_m68040_atc_select_way(mmu->atc, logical, supervisor, tcr.page_size);
  ap_m68040_atc_fill(mmu->atc, fill, logical, tcr.page_size, entry);
  ap_m68040_atc_tick(mmu->atc);
  out.filled = true;

  out.mmusr = (ap_m68040_mmusr_t){
      /* "This 20-bit field contains the upper bits of the translated physical
       * address. Merging these bits with the lower bits of the logical address
       * forms the actual physical address."
       *
       * Masking the *whole* address rather than the frame, and the difference
       * shows only at 8-Kbyte pages: the field is bits 31-12 either way, but an
       * 8K page descriptor supplies 31-13, and §3.4 says "physical address bit
       * 12 is driven by logical address bit 12". `ap_m68040_search` has already
       * ORed the page offset in, so bit 12 is the logical one exactly where the
       * manual wants it and the descriptor's everywhere else. */
      .physical_address = search.physical_address & 0xFFFFF000u,
      .bus_error = false,
      .global = search.global,
      .user_attribute_1 = search.user_attribute_1,
      .user_attribute_0 = search.user_attribute_0,
      /* "Set if the S-bit in the page descriptor is set. This bit does not
       * indicate that a violation has occurred" -- so a supervisor-only page
       * tested with a user function code still reports `R`, and it is the
       * caller's business to notice. `PTEST` tests, it does not fault. */
      .supervisor = search.supervisor,
      .cache_mode = search.cache_mode,
      .modified = search.modified,
      /* "Set if the W-bit is set in any of the descriptors encountered during
       * the table search", which is what `ap_m68040_search` accumulates. */
      .write_protect = search.write_protect,
      .transparent = false,
      /* "Set if the PTEST address matches a transparent translation register or
       * if the table search completes by obtaining a valid page descriptor." */
      .resident = search.status == AP_M68040_SEARCH_RESIDENT};
  return out;
}
