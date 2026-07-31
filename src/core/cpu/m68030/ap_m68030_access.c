/* MC68030 logical memory access. See ap_m68030_access.h for why the cache is
 * consulted before the MMU rather than after. */

#include "cpu/m68030/ap_m68030_access.h"

#include <stddef.h>

ap_m68030_access_result_t ap_m68030_access_read(ap_m68030_access_ctx_t *access,
                                                uint32_t logical,
                                                uint8_t function_code) {
  ap_m68030_access_result_t out = {0};

  const bool cache_usable =
      ap_m68030_cache_enabled(access->cache_enabled, access->cache_disable,
                              false);

  /* Step one, and the whole point of the module: the cache answers first, from
   * the *logical* address. "the MMU is completely ignored" if it does. */
  if (cache_usable &&
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
                                     .read_modify_write = false};
  const ap_m68030_tt_result_t transparent =
      ap_m68030_tt_translate(access->tt0, access->tt1, &tt_access);

  if (transparent.transparent) {
    out.transparent = true;
    physical = transparent.physical;
    cache_inhibit = transparent.cache_inhibit;
  } else if (access->translation_enabled) {
    /* The ATC first; a miss pays for a table search. */
    const ap_m68030_atc_result_t lookup = ap_m68030_atc_lookup(
        access->atc, function_code, logical, access->tc->page_size_bits, false,
        false);

    if (lookup.status == AP_M68030_ATC_HIT) {
      physical = lookup.physical;
      cache_inhibit = lookup.cache_inhibit;
    } else if (lookup.status == AP_M68030_ATC_FAULT) {
      out.fault = true;
      return out;
    } else {
      const ap_m68030_search_access_t search_access = {
          .write = false,
          .read_modify_write = false,
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
        out.fault = true;
        return out;
      }
      physical = walk.physical;
      cache_inhibit = walk.search.cache_inhibited;
    }
  }

  out.physical = physical;

  /* CIOUT, from whichever of the two produced the translation, suppresses the
   * cache for this access -- which is why it is only consulted now. */
  const bool fillable = ap_m68030_cache_enabled(
      access->cache_enabled, access->cache_disable, cache_inhibit);

  const ap_m68030_cache_access_t fetched = ap_m68030_cache_read(
      access->cache, logical, function_code, fillable, access->burst_enabled,
      access->cache_frozen, false, access->fill, access->context);

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
                                                 bool aligned_long_word) {
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
                                        .read_modify_write = false};
  const ap_m68030_tt_result_t transparent =
      ap_m68030_tt_translate(access->tt0, access->tt1, &tt_access);

  if (transparent.transparent) {
    out.transparent = true;
    physical = transparent.physical;
    cache_inhibit = transparent.cache_inhibit;
  } else if (access->translation_enabled) {
    const ap_m68030_atc_result_t lookup = ap_m68030_atc_lookup(
        access->atc, function_code, logical, access->tc->page_size_bits, true,
        false);

    bool search = (lookup.status == AP_M68030_ATC_MISS);

    if (lookup.status == AP_M68030_ATC_FAULT) {
      /* B set, or WP set on a write: a bus error exception, taken immediately
       * and without the write reaching memory. */
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
          .read_modify_write = false,
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
  if (access->store != NULL) {
    access->store(access->context, physical, value, 4);
  }

  /* The cache's own part, which is an update rather than a fill. */
  const bool cache_usable = ap_m68030_cache_enabled(
      access->cache_enabled, access->cache_disable, cache_inhibit);
  if (cache_usable) {
    (void)ap_m68030_cache_write(access->cache, logical, function_code, value,
                                aligned_long_word,
                                access->write_allocate, access->cache_frozen);
  }

  out.value = value;
  out.ok = true;
  return out;
}
