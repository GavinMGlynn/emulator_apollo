/* MC68030 logical memory access. See ap_m68030_access.h for why the cache is
 * consulted before the MMU rather than after. */

#include "cpu/m68030/ap_m68030_access.h"

#include <stddef.h>

/* Report a refused translation, if anyone is listening. Kept as one function
 * because there are four refusal points -- an ATC entry that already carries B,
 * and a completed search, on each of the read and write paths -- and a shape
 * maintained by hand at four sites is one where a site gets missed. */
static void report_mmu_fault(const ap_m68030_access_ctx_t *access,
                             uint32_t logical, uint8_t function_code,
                             bool write, ap_m68030_mmu_fault_t reason) {
  if (access->mmu_faulted != NULL) {
    access->mmu_faulted(access->context, logical, function_code, write, reason);
  }
}

/* Which of §9.4's causes a completed search hit. Order matters: a search that
 * bus errored also reports invalid, and the bus error is the more specific
 * answer -- the tree could not be read, rather than the tree saying no. */
static ap_m68030_mmu_fault_t search_fault_reason(
    const ap_m68030_walk_result_t *walk) {
  if (walk->bus_error) {
    return AP_M68030_MMU_FAULT_SEARCH_BUS;
  }
  if (walk->search.limit_violation) {
    return AP_M68030_MMU_FAULT_LIMIT;
  }
  if (walk->search.invalid) {
    return AP_M68030_MMU_FAULT_INVALID;
  }
  return AP_M68030_MMU_FAULT_PROTECTION;
}

/* The 68040's MMU, when the part has one, in front of the 68030's whole
 * translation path. Returns false when this part has no 68040 MMU, which is
 * every model but the DS5500 -- and that is what keeps the 68030's translation,
 * and its state hash, untouched by this file's existence.
 *
 * `[040]` §3.1.3 is why it runs even with paged translation disabled: the
 * transparent translation registers "operate independently of the E-bit in the
 * TCR", so a 68040 always has something to ask. */
static bool translate_040(const ap_m68030_access_ctx_t *access,
                          uint32_t logical, unsigned function_code, bool write,
                          uint32_t *physical, bool *cache_inhibit,
                          unsigned *fetches, bool *fault,
                          ap_m68030_mmu_fault_t *reason) {
  if (access->mmu_040 == NULL) {
    return false;
  }
  const ap_m68040_mmu_result_t r = ap_m68040_mmu_translate(
      access->mmu_040, logical, function_code, write,
      access->table_fetch_040, access->context);
  *fetches = r.fetches;
  if (r.status == AP_M68040_MMU_FAULT) {
    *fault = true;
    /* The reason, mapped onto the report's existing vocabulary rather than
     * reported as "cached" for everything -- which is what this did, and it
     * made every 68040 fault read as an ATC entry that was already known bad
     * even when the tables had just been walked. A diagnosis is only worth
     * having if it can be wrong. */
    switch (r.reason) {
    case AP_M68040_MMU_FAULT_NOT_RESIDENT:
      *reason = AP_M68030_MMU_FAULT_INVALID;
      break;
    case AP_M68040_MMU_FAULT_PROTECTION:
      *reason = AP_M68030_MMU_FAULT_PROTECTION;
      break;
    case AP_M68040_MMU_FAULT_SEARCH_BUS:
      *reason = AP_M68030_MMU_FAULT_SEARCH_BUS;
      break;
    case AP_M68040_MMU_FAULT_CACHED:
    case AP_M68040_MMU_FAULT_NONE:
      *reason = AP_M68030_MMU_FAULT_CACHED;
      break;
    }
    return true;
  }
  *physical = r.physical;
  /* §3.2.2.3's four modes; this core's caches take one bit rather than the
   * mode, so the two noncachable ones inhibit and the two cachable ones do not.
   * Write-through against copyback is not distinguished because the 68040's
   * caches are attached to no CPU -- the distinction has nowhere to land. */
  *cache_inhibit = r.cache_mode == AP_M68040_CM_NONCACHABLE_SERIALIZED ||
                   r.cache_mode == AP_M68040_CM_NONCACHABLE;
  return true;
}

ap_m68030_access_result_t ap_m68030_access_read(ap_m68030_access_ctx_t *access,
                                                uint32_t logical,
                                                uint8_t function_code) {
  return ap_m68030_access_read_sized(access, logical, function_code, 4u);
}

ap_m68030_access_result_t
ap_m68030_access_read_sized(ap_m68030_access_ctx_t *access, uint32_t logical,
                            uint8_t function_code, unsigned size) {
  ap_m68030_access_result_t out = {0};

  /* Whether the cache may *answer*. `CIIN` is a bus signal and so belongs to
   * the physical address, which is not known yet -- but it does not have to be
   * for this question: a device address is never *in* the cache, because the
   * fill below is gated by the same predicate against the address the bus
   * actually carried. This is the belt to that braces, and the logical address
   * is all it has to work with. */
  const bool cache_usable = ap_m68030_cache_enabled(
      access->cache_enabled, access->cache_disable,
      access->inhibits_cache != NULL &&
          access->inhibits_cache(access->context, logical));

  /* Step one, and the whole point of the module: the cache answers first, from
   * the *logical* address. "the MMU is completely ignored" if it does.
   *
   * **Except under RMC**, which is the read half of an indivisible operation
   * and must go to memory. `[030]` §6.1.2.2: "The read portion of a
   * read-modify-write cycle is **always forced to miss in the data cache**",
   * and §11.4's note says the same from the timing end -- "RMC cycles (e.g.,
   * TAS and CAS) are forced to miss on data cache reads. Therefore, a data
   * cache hit has no effect on these instructions."
   *
   * It is not only a timing rule. A `TAS` that answered from this cache would
   * read whatever the line held, and an alternate bus master can have written
   * that location since -- the line is not invalidated by anyone else's write.
   * Forcing the miss is what makes the semaphore read see memory, which is the
   * whole reason the operation is indivisible.
   *
   * The fill still happens: §6.1.2.2 continues that the processor "either uses
   * the data read from memory to update a matching entry in the data cache or
   * creates a new entry with the read data", which is what the miss path below
   * does anyway. Only the *hit* is suppressed. */
  if (cache_usable && !access->rmc &&
      ap_m68030_cache_lookup(access->cache, logical, function_code,
                             &out.value)) {
    out.ok = true;
    out.cache_hit = true;
    out.mmu_consulted = false;
    out.clocks = 0;
    return out;
  }

  /* An external cycle is needed, so now the MMU is asked. "The MMU is used to
   * validate all accesses that require external bus cycles." */
  out.mmu_consulted = true;

  uint32_t physical = logical;
  bool cache_inhibit = false;

  /* Transparent translation is checked before the tables: a matching TTx
   * register translates without them and without protection checking. */
  const ap_m68030_access_t tt_access = {.address = logical,
                                     .function_code = function_code,
                                     .read = true,
                                     .read_modify_write = access->rmc};
  const ap_m68030_tt_result_t transparent =
      ap_m68030_tt_translate(access->tt0, access->tt1, &tt_access);

  bool fault_040 = false;
  unsigned fetches_040 = 0u;
  ap_m68030_mmu_fault_t reason_040 = AP_M68030_MMU_FAULT_CACHED;
  if (translate_040(access, logical, function_code, false, &physical,
                    &cache_inhibit, &fetches_040, &fault_040, &reason_040)) {
    out.descriptor_fetches = fetches_040;
    if (fault_040) {
      report_mmu_fault(access, logical, function_code, false, reason_040);
      out.translation_fault = true;
      out.fault = true;
      return out;
    }
  } else if (transparent.transparent) {
    out.transparent = true;
    physical = transparent.physical;
    cache_inhibit = transparent.cache_inhibit;
  } else if (ap_m68030_translating(access)) {
    /* The ATC first; a miss pays for a table search. */
    const ap_m68030_atc_result_t lookup = ap_m68030_atc_lookup(
        access->atc, function_code, logical, access->tc->page_size_bits, false,
        false);
    /* A translation *used* the entry, so the replacement algorithm's history
     * bit is set -- unlike a PTEST probe, which must not perturb it. */
    ap_m68030_atc_mark_used(access->atc, lookup.index);

    if (lookup.status == AP_M68030_ATC_HIT) {
      physical = lookup.physical;
      cache_inhibit = lookup.cache_inhibit;
    } else if (lookup.status == AP_M68030_ATC_FAULT) {
      report_mmu_fault(access, logical, function_code, false,
                       AP_M68030_MMU_FAULT_CACHED);
      out.translation_fault = true;
      out.fault = true;
      return out;
    } else {
      const ap_m68030_search_access_t search_access = {
          .write = false,
          .read_modify_write = access->rmc,
          .supervisor = (function_code & 4u) != 0u};
      const ap_m68030_walk_result_t walk =
          ap_m68030_walk(access->tc, access->root, logical, &search_access,
                         access->table_fetch, access->table_update,
                         access->context);
      out.descriptor_fetches = walk.descriptor_fetches;
      (void)ap_m68030_walk_fill_atc(access->atc, &walk, &search_access,
                                    function_code, logical,
                                    access->tc->page_size_bits);
      if (!walk.ok ||
          !ap_m68030_search_permits_access(&walk.search,
                                           search_access.supervisor)) {
        report_mmu_fault(access, logical, function_code, false,
                         search_fault_reason(&walk));
        out.translation_fault = true;
        out.fault = true;
        return out;
      }
      physical = walk.physical;
      cache_inhibit = walk.search.cache_inhibited;
    }
  }

  out.physical = physical;

  /* `CIIN` proper, now that there is an address to assert it against. The
   * board is a map of physical space, so asking it about a logical address is
   * asking the wrong question -- and while translation is off the two are the
   * same number, which is why it went unnoticed until an operating system
   * turned the MMU on. */
  const bool board_inhibits =
      access->inhibits_cache != NULL &&
      access->inhibits_cache(access->context, physical);

  /* A device, and a caller that said how much of it it wanted. Run exactly that
   * cycle: a wider one would touch registers the program never addressed, and
   * on a part with a FIFO or a read-to-clear status that is not a wasted read
   * but a changed machine.
   *
   * This sat *above* the MMU, and so ran the cycle at the logical address.
   * Harmless until the MMU is on and then not: Domain/OS puts its vector table
   * at logical `3C400800`, and the PROM service that reads a byte of it --
   * `movec vbr,a0; btst #7,(a0)` -- took this path and addressed a physical
   * `3C400800` that no memory answers. The long-word fetch of the vector next
   * to it went the wide way, translated, and worked, so the machine faulted on
   * a byte of a page it had just successfully read. */
  if (board_inhibits && access->read_sized != NULL && size < 4u) {
    uint32_t narrow = 0;
    if (!access->read_sized(access->context, physical, function_code, size,
                            &narrow)) {
      out.fault = true;
      return out;
    }
    /* Positioned within the long word where the wide path would have put it,
     * so a caller extracting with a shift needs to know none of this. */
    const unsigned offset = physical & 3u;
    const unsigned shift = (4u - offset - size) * 8u;
    out.value = narrow << shift;
    out.ok = true;
    out.clocks = AP_M68030_MIN_BUS_CLOCKS +
                 (access->wait_states != NULL
                      ? access->wait_states(access->context, physical, true)
                      : 0u);
    return out;
  }

  /* CIOUT, from whichever of the two produced the translation, suppresses the
   * cache for this access -- which is why it is only consulted now. */
  const bool fillable = ap_m68030_cache_enabled(
      access->cache_enabled, access->cache_disable,
      cache_inhibit || board_inhibits);

  /* `access->rmc` reaches the cache here, where a literal `false` used to sit.
   * Three things followed from that constant, all of them wrong under an RMC:
   * the second hit test inside `ap_m68030_cache_read` could still answer from
   * the cache; `ap_m68030_cache_burst_request` never saw the RMC that §7.3.6
   * says suppresses `CBREQ`; and `ap_m68030_cache_read`'s own
   * `bus->rmc = read_modify_write` *cleared* the signal on the read cycle of an
   * indivisible operation, which is exactly the cycle it must be asserted on. */
  const ap_m68030_cache_request_t request = {
      .address = logical,
      .physical = physical,
      .function_code = function_code,
      .cache_enabled = fillable,
      .burst_enable = access->burst_enabled,
      .frozen = access->cache_frozen,
      .read_modify_write = access->rmc,
      .fill = access->fill,
      .wait_states = access->wait_states,
      .context = access->context,
  };
  const ap_m68030_cache_access_t fetched =
      ap_m68030_cache_read(access->cache, &access->bus, &request);

  out.value = fetched.value;
  out.clocks = fetched.clocks;
  out.ok = !fetched.bus_error;
  out.fault = fetched.bus_error;
  return out;
}

ap_m68030_access_result_t ap_m68030_access_write(ap_m68030_access_ctx_t *access,
                                                 uint32_t logical,
                                                 uint8_t function_code,
                                                 uint32_t value,
                                                 unsigned size) {
  /* "a misaligned data write or a write of data that is not long word" does not
   * validate an allocated cache entry, so the rule is the size *and* the
   * alignment together, not either alone. */
  const bool aligned_long_word = (size == 4u) && ((logical & 3u) == 0u);
  ap_m68030_access_result_t out = {0};

  /* No cache-first shortcut here. The data cache is writethrough, so an
   * external cycle always happens, and every access needing one is validated by
   * the MMU. A write can never be answered from the cache alone -- which is
   * also what makes write protection work on a resident page. */
  out.mmu_consulted = true;

  uint32_t physical = logical;
  bool cache_inhibit = false;

  const ap_m68030_access_t tt_access = {.address = logical,
                                        .function_code = function_code,
                                        .read = false,
                                        .read_modify_write = access->rmc};
  const ap_m68030_tt_result_t transparent =
      ap_m68030_tt_translate(access->tt0, access->tt1, &tt_access);

  bool fault_040 = false;
  unsigned fetches_040 = 0u;
  ap_m68030_mmu_fault_t reason_040 = AP_M68030_MMU_FAULT_CACHED;
  if (translate_040(access, logical, function_code, true, &physical,
                    &cache_inhibit, &fetches_040, &fault_040, &reason_040)) {
    out.descriptor_fetches = fetches_040;
    if (fault_040) {
      report_mmu_fault(access, logical, function_code, true, reason_040);
      out.translation_fault = true;
      out.fault = true;
      return out;
    }
  } else if (transparent.transparent) {
    out.transparent = true;
    physical = transparent.physical;
    cache_inhibit = transparent.cache_inhibit;
  } else if (ap_m68030_translating(access)) {
    const ap_m68030_atc_result_t lookup = ap_m68030_atc_lookup(
        access->atc, function_code, logical, access->tc->page_size_bits, true,
        false);
    /* A translation *used* the entry, so the replacement algorithm's history
     * bit is set -- unlike a PTEST probe, which must not perturb it. */
    ap_m68030_atc_mark_used(access->atc, lookup.index);

    bool search = (lookup.status == AP_M68030_ATC_MISS);

    if (lookup.status == AP_M68030_ATC_FAULT) {
      /* B set, or WP set on a write: a bus error exception, taken immediately
       * and without the write reaching memory. */
      report_mmu_fault(access, logical, function_code, true,
                       AP_M68030_MMU_FAULT_CACHED);
      out.translation_fault = true;
      out.fault = true;
      return out;
    }

    if (lookup.status == AP_M68030_ATC_MODIFY) {
      /* §9.4: the entry is a hit, but its M bit is clear, so the processor
       * "aborts the access and initiates a table search". The first write to a
       * page that has only been read therefore costs a full search. */
      search = true;
    }

    if (search) {
      const ap_m68030_search_access_t search_access = {
          .write = true,
          .read_modify_write = access->rmc,
          .supervisor = (function_code & 4u) != 0u};
      const ap_m68030_walk_result_t walk =
          ap_m68030_walk(access->tc, access->root, logical, &search_access,
                         access->table_fetch, access->table_update,
                         access->context);
      out.descriptor_fetches = walk.descriptor_fetches;
      (void)ap_m68030_walk_fill_atc(access->atc, &walk, &search_access,
                                    function_code, logical,
                                    access->tc->page_size_bits);
      if (!walk.ok ||
          !ap_m68030_search_permits_write(&walk.search) ||
          !ap_m68030_search_permits_access(&walk.search,
                                           search_access.supervisor)) {
        report_mmu_fault(access, logical, function_code, true,
                         search_fault_reason(&walk));
        out.translation_fault = true;
        out.fault = true;
        return out;
      }
      physical = walk.physical;
      cache_inhibit = walk.search.cache_inhibited;
    } else {
      physical = lookup.physical;
      cache_inhibit = lookup.cache_inhibit;
    }
  }

  out.physical = physical;

  /* The external write happens on every write, which is what "writethrough"
   * means: "the data is written both to the cache and to external memory". The
   * cache update below is in addition to it, never instead of it. */
  if (access->store != NULL &&
      !access->store(access->context, physical, value, size)) {
    /* Nothing answered. This is a bus error exactly as a read of the same
     * address would be -- the direction does not change whether a device is
     * there -- and it must be reported before the cache is updated below. A
     * cache holding a value external memory refused is a cache that will hand
     * that value back on a later read, which is how a silently dropped write
     * becomes a wrong *read*. */
    out.fault = true;
    return out;
  }

  /* And it costs what the bus charges for it, counted by running the cycle --
   * the same way a read miss is priced, rather than by a constant. A write that
   * cost nothing is what this was until the published `NCC` column caught it:
   * `ADD Dn,EA` is `CC 3(0/0/1)` against `NCC 4(0/1/1)`, and the core produced
   * 3 for both because the write contributed no time.
   *
   * The termination is STERM: this is the synchronous case, which is what the
   * timing tables assume ("All memory accesses occur with two-clock bus cycles
   * and no wait states"). A memory system that inserts wait states will make
   * this longer by itself, which is the point of counting ticks rather than
   * asserting a number. */
  ap_m68030_bus_t *const write_bus = &access->bus;
  /* The context's RMC, so a cycle inside an indivisible operation carries the
   * signal the operation asserted. */
  write_bus->rmc = access->rmc;
  ap_m68030_bus_begin(write_bus, physical, function_code,
                      size == 4u ? AP_M68030_SIZE_LONG
                                 : (size == 2u ? AP_M68030_SIZE_WORD
                                               : AP_M68030_SIZE_BYTE),
                      false, true);
  /* The device's answer arrives when the device says it does. Withholding
   * termination is exactly what §7.3.1 describes — the processor "continues to
   * sample the DSACKx signals on the falling edges of the clock until one is
   * recognized" — so the wait states are *counted by the bus* rather than added
   * to a total afterwards, and a cycle lengthened this way lengthens everything
   * built on it without any of those layers knowing. */
  const unsigned write_waits =
      access->wait_states != NULL
          ? access->wait_states(access->context, physical, false)
          : 0u;
  while (ap_m68030_bus_active(write_bus)) {
    ap_m68030_bus_terminate(write_bus,
                            write_bus->wait_states >= write_waits
                                ? AP_M68030_TERM_STERM
                                : AP_M68030_TERM_NONE);
    (void)ap_m68030_bus_tick(write_bus);
    out.clocks++;
    if (out.clocks > 64u) {
      break; /* as the read path does: a device that never answers is a bug */
    }
  }

  /* The cache's own part, which is an update rather than a fill. `CIIN` counts
   * here too: a write-allocating cache would otherwise create the very entry a
   * read must never find. */
  const bool cache_usable = ap_m68030_cache_enabled(
      access->cache_enabled, access->cache_disable,
      cache_inhibit || (access->inhibits_cache != NULL &&
                        access->inhibits_cache(access->context, physical)));
  if (cache_usable) {
    (void)ap_m68030_cache_write(access->cache, logical, function_code, value,
                                aligned_long_word, access->write_allocate,
                                access->cache_frozen, size);
  }

  out.value = value;
  out.ok = true;
  return out;
}
