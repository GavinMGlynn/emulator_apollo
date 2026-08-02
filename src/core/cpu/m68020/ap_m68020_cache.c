/* MC68020 on-chip instruction cache. See ap_m68020_cache.h for why this is not
 * the 68030's cache with a flag changed. */

#include "cpu/m68020/ap_m68020_cache.h"

void ap_m68020_cache_clear(ap_m68020_cache_t *cache) {
  for (unsigned i = 0; i < AP_M68020_CACHE_ENTRIES; i++) {
    cache->entry[i].valid = false;
  }
}

unsigned ap_m68020_cache_index(uint32_t address) {
  /* "The index field (A2-A7)": six bits, which is exactly the 64 entries. */
  return (unsigned)((address >> 2) & 0x3Fu);
}

uint32_t ap_m68020_cache_tag(uint32_t address, uint8_t function_code) {
  /* "The access address bits A8-A31, and FC2 are compared to the tag."
   *
   * FC2 is the supervisor/user bit; FC0 and FC1 distinguish program from data
   * space and are **not** in the tag. That is right for an instruction cache --
   * every access to it is program space -- and including them would be
   * harmless until a `MOVES` read the same address as data. */
  const uint32_t fc2 = (function_code >> 2) & 1u;
  return (address >> 8) | (fc2 << 24);
}

bool ap_m68020_cache_lookup(const ap_m68020_cache_t *cache, uint32_t address,
                            uint8_t function_code, uint16_t *word) {
  const ap_m68020_cache_entry_t *entry =
      &cache->entry[ap_m68020_cache_index(address)];
  if (!entry->valid || entry->tag != ap_m68020_cache_tag(address, function_code)) {
    return false;
  }

  /* "Address bit A1 is used to select the proper word from the cache entry."
   * Big endian, so A1 clear takes the *upper* half. */
  *word = ((address & 2u) == 0u) ? (uint16_t)(entry->data >> 16)
                                 : (uint16_t)entry->data;
  return true;
}

void ap_m68020_cache_fill(ap_m68020_cache_t *cache, uint32_t address,
                          uint8_t function_code, uint32_t data, bool frozen) {
  if (frozen) {
    /* "unless the freeze cache bit has been set". Freeze stops *replacement*,
     * not lookup -- a frozen cache still answers what it already holds, which
     * is what makes freezing useful for keeping a loop resident. */
    return;
  }
  ap_m68020_cache_entry_t *entry =
      &cache->entry[ap_m68020_cache_index(address)];
  entry->tag = ap_m68020_cache_tag(address, function_code);
  entry->data = data;
  entry->valid = true;
}

void ap_m68020_cache_clear_entry(ap_m68020_cache_t *cache, uint32_t address) {
  cache->entry[ap_m68020_cache_index(address)].valid = false;
}
