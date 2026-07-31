/* MC68030 on-chip caches. See ap_m68030_cache.h for the citations, for why the
 * valid bit is per entry, and for the reconstruction of the write-allocation
 * sentence the scan ran together. */

#include <stddef.h> /* NULL */

#include "cpu/m68030/ap_m68030_cache.h"

unsigned ap_m68030_cache_line_index(uint32_t address) {
  /* "The cache control circuitry selects the tag using bits A7-A4". */
  return (unsigned)((address >> 4) & (AP_M68030_CACHE_LINES - 1u));
}

unsigned ap_m68030_cache_entry_index(uint32_t address) {
  /* "Address bits A3-A2 select the valid bit for the appropriate long word". */
  return (unsigned)((address >> 2) & (AP_M68030_CACHE_ENTRIES - 1u));
}

uint32_t ap_m68030_cache_tag(uint32_t address, uint8_t function_code) {
  /* A31-A8 with the three function code bits above them. The function code is
   * part of the tag in both caches, so the same address in supervisor and user
   * space occupies different entries. */
  return ((address >> 8) & UINT32_C(0x00FFFFFF)) |
         ((uint32_t)(function_code & 0x7u) << 24);
}

void ap_m68030_cache_clear(ap_m68030_cache_t *cache) {
  /* Valid bits only: "The processor clears all valid bits". Tags and data are
   * left alone, which is observable -- a disabled cache "does not flush the
   * entries", and re-enabling it makes them usable again. */
  for (unsigned l = 0; l < AP_M68030_CACHE_LINES; l++) {
    for (unsigned e = 0; e < AP_M68030_CACHE_ENTRIES; e++) {
      cache->line[l].valid[e] = false;
    }
  }
}

void ap_m68030_cache_clear_entry(ap_m68030_cache_t *cache, uint32_t address) {
  cache->line[ap_m68030_cache_line_index(address)]
      .valid[ap_m68030_cache_entry_index(address)] = false;
}

bool ap_m68030_cache_lookup(const ap_m68030_cache_t *cache, uint32_t address,
                            uint8_t function_code, uint32_t *out) {
  const ap_m68030_cache_line_t *line =
      &cache->line[ap_m68030_cache_line_index(address)];
  const unsigned entry = ap_m68030_cache_entry_index(address);

  /* A hit needs both: the line's tag must match ("line hit") and this entry's
   * own valid bit must be set ("entry hit"). Figure 6-2 names the two
   * separately, and they are separate because validity is per entry. */
  if (line->tag != ap_m68030_cache_tag(address, function_code)) {
    return false;
  }
  if (!line->valid[entry]) {
    return false;
  }
  *out = line->entry[entry];
  return true;
}

void ap_m68030_cache_fill_entry(ap_m68030_cache_t *cache, uint32_t address,
                                uint8_t function_code, uint32_t value) {
  ap_m68030_cache_line_t *line =
      &cache->line[ap_m68030_cache_line_index(address)];
  const uint32_t tag = ap_m68030_cache_tag(address, function_code);

  if (line->tag != tag) {
    /* The line described a different address, so its other entries are stale
     * and must not survive the tag change. */
    for (unsigned e = 0; e < AP_M68030_CACHE_ENTRIES; e++) {
      line->valid[e] = false;
    }
    line->tag = tag;
  }

  const unsigned entry = ap_m68030_cache_entry_index(address);
  line->entry[entry] = value;
  line->valid[entry] = true;
}

void ap_m68030_cache_fill_line(ap_m68030_cache_t *cache, uint32_t address,
                               uint8_t function_code,
                               const uint32_t values[AP_M68030_CACHE_ENTRIES]) {
  ap_m68030_cache_line_t *line =
      &cache->line[ap_m68030_cache_line_index(address)];
  line->tag = ap_m68030_cache_tag(address, function_code);
  for (unsigned e = 0; e < AP_M68030_CACHE_ENTRIES; e++) {
    line->entry[e] = values[e];
    line->valid[e] = true;
  }
}

ap_m68030_cache_write_t
ap_m68030_cache_write(ap_m68030_cache_t *cache, uint32_t address,
                      uint8_t function_code, uint32_t value,
                      bool aligned_long_word, bool write_allocate,
                      bool frozen) {
  ap_m68030_cache_line_t *line =
      &cache->line[ap_m68030_cache_line_index(address)];
  const unsigned entry = ap_m68030_cache_entry_index(address);
  const uint32_t tag = ap_m68030_cache_tag(address, function_code);
  const bool tag_hit = (line->tag == tag);

  /* "When a hit occurs on a write cycle, the data is written both to the cache
   * and to external memory ... regardless of the operand size and even if the
   * cache is frozen." The freeze bit stops *replacement*, not updating. */
  if (tag_hit && line->valid[entry]) {
    line->entry[entry] = value;
    return AP_M68030_CACHE_WRITE_HIT;
  }

  /* "If the data cache is disabled or frozen, the WA bit is ignored", and a
   * frozen cache does not replace on a miss: "When the FD bit is set and a miss
   * occurs during a read or write of the data cache, the indexed entry is not
   * replaced." */
  if (frozen || !write_allocate) {
    /* "write cycles that miss do not alter the data cache contents." */
    return AP_M68030_CACHE_WRITE_UNTOUCHED;
  }

  if (aligned_long_word) {
    /* "the corresponding tag is replaced, and only the long word being written
     * is marked as valid. The other three entries in the cache line are
     * invalidated." */
    for (unsigned e = 0; e < AP_M68030_CACHE_ENTRIES; e++) {
      line->valid[e] = false;
    }
    line->tag = tag;
    line->entry[entry] = value;
    line->valid[entry] = true;
    return AP_M68030_CACHE_WRITE_ALLOCATED;
  }

  /* "on a misaligned long-word write or on a byte or word write, the data is
   * not written in the cache, the tag is unaltered, and the valid bit(s) are
   * cleared." So a sub-long-word write to a line that is present but whose
   * entry is invalid still clears -- it can only ever remove information. */
  line->valid[entry] = false;
  return AP_M68030_CACHE_WRITE_INVALIDATED;
}

static uint32_t bit_at(bool set, unsigned position) {
  return set ? (UINT32_C(1) << position) : UINT32_C(0);
}

uint32_t ap_m68030_cacr_pack(const ap_m68030_cacr_t *cacr) {
  /* CD, CED, CI and CEI are "always read as zero", so they are absent here
   * rather than stored and masked. */
  return bit_at(cacr->write_allocate, AP_M68030_CACR_WA_BIT) |
         bit_at(cacr->data_burst_enable, AP_M68030_CACR_DBE_BIT) |
         bit_at(cacr->freeze_data, AP_M68030_CACR_FD_BIT) |
         bit_at(cacr->enable_data, AP_M68030_CACR_ED_BIT) |
         bit_at(cacr->instruction_burst_enable, AP_M68030_CACR_IBE_BIT) |
         bit_at(cacr->freeze_instruction, AP_M68030_CACR_FI_BIT) |
         bit_at(cacr->enable_instruction, AP_M68030_CACR_EI_BIT);
}

void ap_m68030_cacr_write(ap_m68030_cacr_t *cacr, uint32_t word,
                          ap_m68030_cache_t *instruction,
                          ap_m68030_cache_t *data, uint32_t caar) {
  cacr->write_allocate = ((word >> AP_M68030_CACR_WA_BIT) & 1u) != 0u;
  cacr->data_burst_enable = ((word >> AP_M68030_CACR_DBE_BIT) & 1u) != 0u;
  cacr->freeze_data = ((word >> AP_M68030_CACR_FD_BIT) & 1u) != 0u;
  cacr->enable_data = ((word >> AP_M68030_CACR_ED_BIT) & 1u) != 0u;
  cacr->instruction_burst_enable =
      ((word >> AP_M68030_CACR_IBE_BIT) & 1u) != 0u;
  cacr->freeze_instruction = ((word >> AP_M68030_CACR_FI_BIT) & 1u) != 0u;
  cacr->enable_instruction = ((word >> AP_M68030_CACR_EI_BIT) & 1u) != 0u;

  /* The clears are performed by the write itself, and the entry clears happen
   * "regardless of the states of the [enable] and [freeze] bits". */
  if (data != NULL && ((word >> AP_M68030_CACR_CD_BIT) & 1u) != 0u) {
    ap_m68030_cache_clear(data);
  }
  if (data != NULL && ((word >> AP_M68030_CACR_CED_BIT) & 1u) != 0u) {
    ap_m68030_cache_clear_entry(data, caar);
  }
  if (instruction != NULL && ((word >> AP_M68030_CACR_CI_BIT) & 1u) != 0u) {
    ap_m68030_cache_clear(instruction);
  }
  if (instruction != NULL && ((word >> AP_M68030_CACR_CEI_BIT) & 1u) != 0u) {
    ap_m68030_cache_clear_entry(instruction, caar);
  }
}

bool ap_m68030_cache_enabled(bool enable_bit, bool cache_disable,
                             bool cache_inhibit) {
  /* CDIS wins over CACR outright; CIOUT suppresses the access's use of either
   * cache. Both are overrides rather than refinements, so they are checked
   * before the enable bit rather than combined with it. */
  if (cache_disable || cache_inhibit) {
    return false;
  }
  return enable_bit;
}

bool ap_m68030_cache_burst_request(const ap_m68030_cache_t *cache,
                                   uint32_t address, uint8_t function_code,
                                   bool burst_enable, bool cache_enabled,
                                   bool frozen, bool read_modify_write) {
  /* "If the appropriate cache is not enabled or if the cache freeze bit for the
   * cache is set, the processor does not assert CBREQ. CBREQ is not asserted
   * during the read or write cycles of any read-modify-write operation." */
  if (!burst_enable || !cache_enabled || frozen || read_modify_write) {
    return false;
  }

  const ap_m68030_cache_line_t *line =
      &cache->line[ap_m68030_cache_line_index(address)];

  /* First condition: the tag does not match. */
  if (line->tag != ap_m68030_cache_tag(address, function_code)) {
    return true;
  }

  /* Second condition, and the one that is easy to leave out: the tag *does*
   * match but "all four long words corresponding to the indexed tag ... are
   * marked invalid". Without it a cleared cache would refill an entry at a
   * time, never taking a burst. */
  for (unsigned e = 0; e < AP_M68030_CACHE_ENTRIES; e++) {
    if (line->valid[e]) {
      return false;
    }
  }
  return true;
}

ap_m68030_cache_access_t
ap_m68030_cache_read(ap_m68030_cache_t *cache, uint32_t address,
                     uint8_t function_code, bool cache_enabled,
                     bool burst_enable, bool frozen, bool read_modify_write,
                     ap_m68030_fill_fn fill, void *context) {
  ap_m68030_cache_access_t result = {0};

  /* "Whenever a read access occurs and the required instruction word or data
   * operand is resident in the appropriate on-chip cache (no external bus cycle
   * is required)" -- a hit costs nothing, which is the claim this whole module
   * exists to make measurable. */
  if (cache_enabled &&
      ap_m68030_cache_lookup(cache, address, function_code, &result.value)) {
    result.hit = true;
    return result;
  }

  const bool burst =
      cache_enabled && ap_m68030_cache_burst_request(cache, address,
                                                     function_code, burst_enable,
                                                     cache_enabled, frozen,
                                                     read_modify_write);

  /* A burst fills the whole line, so it addresses the line's base; a single
   * entry fill addresses its own long word. */
  const uint32_t line_address = address & ~UINT32_C(0xF);
  const uint32_t cycle_address = burst ? line_address : (address & ~UINT32_C(3));

  ap_m68030_fill_answer_t answer = {0};
  answer.termination = AP_M68030_TERM_STERM;
  fill(context, burst ? line_address : cycle_address, function_code, &answer);

  ap_m68030_bus_t bus;
  ap_m68030_bus_begin(&bus, cycle_address, function_code, AP_M68030_SIZE_LONG,
                      true, true);
  if (burst) {
    ap_m68030_bus_request_burst(&bus);
    ap_m68030_bus_acknowledge_burst(&bus, answer.burst_acknowledge);
  }

  while (ap_m68030_bus_active(&bus)) {
    ap_m68030_bus_terminate(&bus, answer.termination);
    (void)ap_m68030_bus_tick(&bus);
    result.clocks++;
    if (result.clocks > 64u) {
      break; /* a device that never answers is the caller's bug, not a hang */
    }
  }

  result.burst = bus.burst_beats >= AP_M68030_BURST_BEATS;
  result.long_words = result.burst ? AP_M68030_BURST_BEATS : 1u;

  if (answer.termination == AP_M68030_TERM_BERR) {
    /* Nothing is cached from a faulted access, and no value is produced. */
    result.bus_error = true;
    result.long_words = 0;
    return result;
  }

  const unsigned entry = ap_m68030_cache_entry_index(address);
  result.value = result.burst ? answer.data[entry] : answer.data[0];

  /* A frozen or disabled cache still performs the access; it just does not keep
   * the result. "When the FI bit is set and a miss occurs in the instruction
   * cache, the entry (or line) is not replaced." */
  if (!cache_enabled || frozen) {
    return result;
  }

  if (result.burst) {
    ap_m68030_cache_fill_line(cache, line_address, function_code, answer.data);
  } else {
    ap_m68030_cache_fill_entry(cache, address, function_code, answer.data[0]);
  }
  return result;
}
