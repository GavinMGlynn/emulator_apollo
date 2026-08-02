/* MC68040 instruction and data caches. See the header for how this differs
 * from the two earlier organisations. */

#include <string.h>

#include "cpu/m68040/ap_m68040_cache.h"

ap_m68040_line_state_t
ap_m68040_cache_line_state(const ap_m68040_cache_line_t *line) {
  if (!line->valid) {
    return AP_M68040_LINE_INVALID;
  }
  for (unsigned i = 0; i < AP_M68040_CACHE_LINE_LONGS; i++) {
    if (line->dirty[i]) {
      /* "Dirty cache lines have the V-bit and one or more D-bits set." */
      return AP_M68040_LINE_DIRTY;
    }
  }
  return AP_M68040_LINE_VALID;
}

void ap_m68040_cache_init(ap_m68040_cache_t *cache, bool has_dirty_state) {
  memset(cache, 0, sizeof *cache);
  cache->has_dirty_state = has_dirty_state;
}

void ap_m68040_cache_invalidate_all(ap_m68040_cache_t *cache) {
  for (unsigned set = 0; set < AP_M68040_CACHE_SETS; set++) {
    for (unsigned way = 0; way < AP_M68040_CACHE_WAYS; way++) {
      cache->line[set][way].valid = false;
      for (unsigned i = 0; i < AP_M68040_CACHE_LINE_LONGS; i++) {
        cache->line[set][way].dirty[i] = false;
      }
    }
  }
}

unsigned ap_m68040_cache_set(uint32_t address) {
  /* 64 sets of 16-byte lines: bits 9-4. */
  return (unsigned)((address >> 4) & 0x3Fu);
}

unsigned ap_m68040_cache_long(uint32_t address) {
  return (unsigned)((address >> 2) & 0x3u);
}

uint32_t ap_m68040_cache_tag(uint32_t address) {
  /* "The upper 22 bits of the physical address": bits 31-10. */
  return address >> 10;
}

unsigned ap_m68040_cache_lookup(const ap_m68040_cache_t *cache,
                                uint32_t address) {
  const unsigned set = ap_m68040_cache_set(address);
  const uint32_t tag = ap_m68040_cache_tag(address);
  for (unsigned way = 0; way < AP_M68040_CACHE_WAYS; way++) {
    const ap_m68040_cache_line_t *line = &cache->line[set][way];
    if (line->valid && line->tag == tag) {
      return way;
    }
  }
  return AP_M68040_CACHE_WAYS;
}

unsigned ap_m68040_cache_select_way(const ap_m68040_cache_t *cache,
                                    uint32_t address) {
  const unsigned set = ap_m68040_cache_set(address);
  for (unsigned way = 0; way < AP_M68040_CACHE_WAYS; way++) {
    if (!cache->line[set][way].valid) {
      return way;
    }
  }
  /* "If all lines in the set are already valid, a pseudo-random replacement
   * algorithm is used to select one of the four cache lines" -- the counter,
   * which is per cache rather than per set, so activity anywhere moves it. */
  return cache->counter & 0x3u;
}

void ap_m68040_cache_tick(ap_m68040_cache_t *cache) {
  cache->counter = (cache->counter + 1u) & 0x3u;
}

void ap_m68040_cache_fill(ap_m68040_cache_t *cache, unsigned way,
                          uint32_t address,
                          const uint32_t data[AP_M68040_CACHE_LINE_LONGS],
                          unsigned dirty_mask) {
  ap_m68040_cache_line_t *line =
      &cache->line[ap_m68040_cache_set(address)][way & 0x3u];
  line->tag = ap_m68040_cache_tag(address);
  line->valid = true;
  for (unsigned i = 0; i < AP_M68040_CACHE_LINE_LONGS; i++) {
    line->data[i] = data[i];
    /* "Only the data cache supports dirty cache lines", so an instruction
     * cache silently drops whatever it was handed rather than storing state it
     * has no bits for. */
    line->dirty[i] =
        cache->has_dirty_state && ((dirty_mask >> i) & 1u) != 0u;
  }
}

void ap_m68040_cache_mark_dirty(ap_m68040_cache_t *cache, unsigned way,
                                uint32_t address) {
  if (!cache->has_dirty_state) {
    return;
  }
  ap_m68040_cache_line_t *line =
      &cache->line[ap_m68040_cache_set(address)][way & 0x3u];
  line->dirty[ap_m68040_cache_long(address)] = true;
}

unsigned ap_m68040_cache_writeback_mask(const ap_m68040_cache_line_t *line) {
  if (!line->valid) {
    return 0u;
  }
  unsigned mask = 0;
  for (unsigned i = 0; i < AP_M68040_CACHE_LINE_LONGS; i++) {
    if (line->dirty[i]) {
      mask |= 1u << i;
    }
  }
  return mask;
}
