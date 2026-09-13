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

/* One line, wherever in its set it lives. */
static void drop(ap_m68040_cache_line_t *line) {
  line->valid = false;
  for (unsigned i = 0; i < AP_M68040_CACHE_LINE_LONGS; i++) {
    line->dirty[i] = false;
  }
}

bool ap_m68040_cache_invalidate_line(ap_m68040_cache_t *cache,
                                     uint32_t address) {
  const unsigned way = ap_m68040_cache_lookup(cache, address);
  if (way >= AP_M68040_CACHE_WAYS) {
    return false;
  }
  /* "Without regard to its dirty state": a dirty line's data is lost, which is
   * the whole difference between CINV and CPUSH. */
  drop(&cache->line[ap_m68040_cache_set(address)][way]);
  return true;
}

/* Walk every line the page covers. A page is 4 KB or 8 KB and a line is 16
 * bytes, so this is 256 or 512 lines -- but only the sets the page reaches,
 * because a 4 KB page spans every set exactly four times and an 8 KB page
 * eight. Walking the whole cache and testing each tag is both simpler and
 * exact, and the cache is 256 lines. */
static unsigned over_page(ap_m68040_cache_t *cache, uint32_t address,
                          uint32_t page_bytes, unsigned *dirty_lines,
                          bool push) {
  if (page_bytes == 0u) {
    return 0u;
  }
  const uint32_t base = address & ~(page_bytes - 1u);
  unsigned touched = 0u;
  for (unsigned set = 0; set < AP_M68040_CACHE_SETS; set++) {
    for (unsigned way = 0; way < AP_M68040_CACHE_WAYS; way++) {
      ap_m68040_cache_line_t *line = &cache->line[set][way];
      if (!line->valid) {
        continue;
      }
      /* The tag is the upper 22 bits and the set index supplies bits 9-4, so
       * the line's physical address is the two put back together. */
      const uint32_t line_address =
          (line->tag << AP_M68040_CACHE_TAG_SHIFT) |
          (uint32_t)(set << AP_M68040_CACHE_SET_SHIFT);
      if ((line_address & ~(page_bytes - 1u)) != base) {
        continue;
      }
      if (push && dirty_lines != nullptr &&
          ap_m68040_cache_writeback_mask(line) != 0u) {
        (*dirty_lines)++;
      }
      drop(line);
      touched++;
    }
  }
  return touched;
}

unsigned ap_m68040_cache_invalidate_page(ap_m68040_cache_t *cache,
                                         uint32_t address,
                                         uint32_t page_bytes) {
  return over_page(cache, address, page_bytes, nullptr, false);
}

unsigned ap_m68040_cache_push_line(ap_m68040_cache_t *cache, uint32_t address,
                                   unsigned *writeback) {
  const unsigned way = ap_m68040_cache_lookup(cache, address);
  if (writeback != nullptr) {
    *writeback = 0u;
  }
  if (way >= AP_M68040_CACHE_WAYS) {
    return 0u;
  }
  ap_m68040_cache_line_t *line =
      &cache->line[ap_m68040_cache_set(address)][way];
  const unsigned mask = ap_m68040_cache_writeback_mask(line);
  if (writeback != nullptr) {
    *writeback = mask;
  }
  drop(line);
  return 1u;
}

unsigned ap_m68040_cache_push_page(ap_m68040_cache_t *cache, uint32_t address,
                                   uint32_t page_bytes,
                                   unsigned *dirty_lines) {
  if (dirty_lines != nullptr) {
    *dirty_lines = 0u;
  }
  return over_page(cache, address, page_bytes, dirty_lines, true);
}

unsigned ap_m68040_cache_push_all(ap_m68040_cache_t *cache,
                                  unsigned *dirty_lines) {
  unsigned touched = 0u;
  if (dirty_lines != nullptr) {
    *dirty_lines = 0u;
  }
  for (unsigned set = 0; set < AP_M68040_CACHE_SETS; set++) {
    for (unsigned way = 0; way < AP_M68040_CACHE_WAYS; way++) {
      ap_m68040_cache_line_t *line = &cache->line[set][way];
      if (!line->valid) {
        continue;
      }
      if (dirty_lines != nullptr &&
          ap_m68040_cache_writeback_mask(line) != 0u) {
        (*dirty_lines)++;
      }
      drop(line);
      touched++;
    }
  }
  return touched;
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

/* ---------------------------------------------------------------------------
 * Snoop control, `[040]` Table 4-1.
 * ------------------------------------------------------------------------- */

ap_m68040_snoop_request_t ap_m68040_snoop_request(unsigned sc, bool write) {
  switch (sc & 0x3u) {
  case 0x1u:
    /* Read: "supply dirty data and leave dirty data". Write: the misprinted
     * cell -- see the header -- resolved as a sinking write. */
    return write ? AP_M68040_SNOOP_SINK : AP_M68040_SNOOP_SUPPLY_LEAVE_DIRTY;
  case 0x2u:
    /* "Supply dirty data and mark line invalid" / "invalidate line". */
    return write ? AP_M68040_SNOOP_INVALIDATE
                 : AP_M68040_SNOOP_SUPPLY_MARK_INVALID;
  case 0x3u:
    /* "Reserved (Snoop Inhibited)", both columns. */
    return AP_M68040_SNOOP_INHIBIT;
  default:
    /* 00, "inhibit snooping", both columns. */
    return AP_M68040_SNOOP_INHIBIT;
  }
}

/* ---------------------------------------------------------------------------
 * Line state transitions, `[040]` Tables 4-3 and 4-4.
 * ------------------------------------------------------------------------- */

static ap_m68040_cache_transition_t impossible(ap_m68040_line_state_t state) {
  /* "Not Possible", and for the instruction cache's read-snoop rows "not
   * possible; not snooped". The line does not move. */
  return (ap_m68040_cache_transition_t){.possible = false, .next = state};
}

/* Table 4-3: the instruction cache. Two states, and only six of the thirteen
 * operations reach it -- it holds no dirty data, so it never sources, sinks or
 * pushes, and every snoop hit that is snooped at all simply invalidates. */
static ap_m68040_cache_transition_t
instruction_transition(ap_m68040_line_state_t state, ap_m68040_cache_op_t op) {
  const bool valid = state == AP_M68040_LINE_VALID;
  switch (op) {
  case AP_M68040_CACHE_OP_CPU_READ_MISS:
    /* I1 and V1: "read line from memory; supply data to CPU and update cache".
     * V1 adds "(replacing old line)" and stays valid; I1 goes valid. */
    return (ap_m68040_cache_transition_t){.possible = true,
                                          .next = AP_M68040_LINE_VALID,
                                          .read_line = true,
                                          .supply_to_cpu = true};
  case AP_M68040_CACHE_OP_CPU_READ_HIT:
    /* I2 "Not Possible" -- an invalid line cannot be hit. V2 supplies. */
    if (!valid) {
      return impossible(state);
    }
    return (ap_m68040_cache_transition_t){.possible = true,
                                          .next = AP_M68040_LINE_VALID,
                                          .supply_to_cpu = true};
  case AP_M68040_CACHE_OP_CINV:
  case AP_M68040_CACHE_OP_CPUSH:
    /* I3/V3, one row for both instructions: "no action". There is no dirty
     * data here for `CPUSH` to write back, which is why the table merges them
     * and Table 4-4 does not. */
    return (ap_m68040_cache_transition_t){.possible = true,
                                          .next = AP_M68040_LINE_INVALID};
  case AP_M68040_CACHE_OP_SNOOP_READ_LEAVE_DIRTY:
    /* I4/V4: "not possible; not snooped." A leave-dirty read snoop asks for
     * dirty data, and this cache has none to be asked for. */
    return impossible(state);
  case AP_M68040_CACHE_OP_SNOOP_READ_INVALIDATE:
    /* I5 "Not Possible", V5 "no action; go to invalid state". */
    if (!valid) {
      return impossible(state);
    }
    return (ap_m68040_cache_transition_t){.possible = true,
                                          .next = AP_M68040_LINE_INVALID};
  case AP_M68040_CACHE_OP_SNOOP_WRITE_INVALIDATE:
  case AP_M68040_CACHE_OP_SNOOP_WRITE_SINK_PARTIAL:
  case AP_M68040_CACHE_OP_SNOOP_WRITE_SINK_LINE:
    /* I6/V6 is one row for "Snoop Control = 01 - leave Dirty or Snoop Control =
     * 10 - Invalidate", so both encodings and both sizes land here: "no action;
     * go to invalid state". */
    if (!valid) {
      return impossible(state);
    }
    return (ap_m68040_cache_transition_t){.possible = true,
                                          .next = AP_M68040_LINE_INVALID};
  case AP_M68040_CACHE_OP_CPU_WRITE_MISS_COPYBACK:
  case AP_M68040_CACHE_OP_CPU_WRITE_MISS_WRITE_THROUGH:
  case AP_M68040_CACHE_OP_CPU_WRITE_HIT_COPYBACK:
  case AP_M68040_CACHE_OP_CPU_WRITE_HIT_WRITE_THROUGH:
    /* Table 4-3 has no write rows at all: the IU never writes to the
     * instruction cache, and §4.5 says coherency with the data cache "must be
     * maintained in software since the instruction cache does not monitor data
     * accesses". Listed rather than defaulted so a row added to Table 4-4
     * fails the build here instead of silently reporting impossible. */
    return impossible(state);
  }
  return impossible(state);
}

/* Table 4-4: the data cache. Three states, thirteen operations. */
static ap_m68040_cache_transition_t
data_transition(ap_m68040_line_state_t state, ap_m68040_cache_op_t op) {
  const bool invalid = state == AP_M68040_LINE_INVALID;
  const bool dirty = state == AP_M68040_LINE_DIRTY;
  switch (op) {
  case AP_M68040_CACHE_OP_CPU_READ_MISS:
    /* I1/V1 read and fill. D1 additionally "buffer dirty cache line ... write
     * buffered dirty data to memory", and ends *valid* -- the new line is
     * clean, so a dirty line is the only case that changes state on a read. */
    return (ap_m68040_cache_transition_t){.possible = true,
                                          .next = AP_M68040_LINE_VALID,
                                          .read_line = true,
                                          .supply_to_cpu = true,
                                          .buffer_dirty = dirty};
  case AP_M68040_CACHE_OP_CPU_READ_HIT:
    /* I2 "Not Possible". V2/D2 "supply data to CPU; remain in current
     * state" -- a read hit never cleans a dirty line. */
    if (invalid) {
      return impossible(state);
    }
    return (ap_m68040_cache_transition_t){
        .possible = true, .next = state, .supply_to_cpu = true};
  case AP_M68040_CACHE_OP_CPU_WRITE_MISS_COPYBACK:
    /* I3/V3/D3: allocate by reading the line, then write into it and set the
     * D-bits. Every case ends dirty. D3 buffers the line it replaced. */
    return (ap_m68040_cache_transition_t){.possible = true,
                                          .next = AP_M68040_LINE_DIRTY,
                                          .read_line = true,
                                          .write_to_cache = true,
                                          .set_dirty = true,
                                          .buffer_dirty = dirty};
  case AP_M68040_CACHE_OP_CPU_WRITE_MISS_WRITE_THROUGH:
    /* I4/V4/D4: "write data to memory; remain in current state". No line is
     * allocated -- a write-through miss does not read. D4 carries the NOTE. */
    return (ap_m68040_cache_transition_t){.possible = true,
                                          .next = state,
                                          .write_to_memory = true,
                                          .programming_error = dirty};
  case AP_M68040_CACHE_OP_CPU_WRITE_HIT_COPYBACK:
    /* I5 "Not Possible". V5 goes dirty, D5 stays dirty; neither writes to
     * memory, which is what copyback means. */
    if (invalid) {
      return impossible(state);
    }
    return (ap_m68040_cache_transition_t){.possible = true,
                                          .next = AP_M68040_LINE_DIRTY,
                                          .write_to_cache = true,
                                          .set_dirty = true};
  case AP_M68040_CACHE_OP_CPU_WRITE_HIT_WRITE_THROUGH:
    /* I6 "Not Possible". V6 writes both cache and memory and stays valid. D6 is
     * the same "(no change to Dn bits)" and stays dirty, and carries the NOTE:
     * the line was made dirty under copyback and the page attribute changed
     * without a flush. Explicitly *not* setting `set_dirty` is the whole
     * content of that parenthesis. */
    if (invalid) {
      return impossible(state);
    }
    return (ap_m68040_cache_transition_t){.possible = true,
                                          .next = state,
                                          .write_to_cache = true,
                                          .write_to_memory = true,
                                          .programming_error = dirty};
  case AP_M68040_CACHE_OP_CINV:
    /* I7 "no action; remain in current state" -- already invalid. V7 goes
     * invalid. D7 "no action (dirty data lost); go to invalid state". */
    return (ap_m68040_cache_transition_t){.possible = true,
                                          .next = AP_M68040_LINE_INVALID,
                                          .dirty_data_lost = dirty};
  case AP_M68040_CACHE_OP_CPUSH:
    /* I8/V8 "no action". D8 "write dirty data to memory". All three end
     * invalid: on this part a push invalidates as well as writing back. */
    return (ap_m68040_cache_transition_t){.possible = true,
                                          .next = AP_M68040_LINE_INVALID,
                                          .push_dirty = dirty};
  case AP_M68040_CACHE_OP_SNOOP_READ_LEAVE_DIRTY:
    /* I9 "Not Possible". V9 "no action; remain in current state" -- a clean
     * line lets memory answer. D9 "inhibit memory and source data; remain in
     * current state", the intervention, and it stays dirty. */
    if (invalid) {
      return impossible(state);
    }
    return (ap_m68040_cache_transition_t){.possible = true,
                                          .next = state,
                                          .inhibit_memory = dirty,
                                          .source_data = dirty};
  case AP_M68040_CACHE_OP_SNOOP_READ_INVALIDATE:
    /* I10 "Not Possible". V10 goes invalid with no action. D10 sources the
     * data first, then goes invalid. */
    if (invalid) {
      return impossible(state);
    }
    return (ap_m68040_cache_transition_t){.possible = true,
                                          .next = AP_M68040_LINE_INVALID,
                                          .inhibit_memory = dirty,
                                          .source_data = dirty};
  case AP_M68040_CACHE_OP_SNOOP_WRITE_INVALIDATE:
    /* I11 "Not Possible". V11 and D11 both "no action; go to invalid state" --
     * a dirty line is discarded, not pushed, because the alternate master is
     * writing that memory anyway. */
    if (invalid) {
      return impossible(state);
    }
    return (ap_m68040_cache_transition_t){.possible = true,
                                          .next = AP_M68040_LINE_INVALID};
  case AP_M68040_CACHE_OP_SNOOP_WRITE_SINK_PARTIAL:
    /* I12 "Not Possible". V12 "no action; go to invalid state" -- a *clean*
     * line does not sink. D12 "inhibit memory and sink data; set Dn bits of
     * modified long words; remain in current state". One state out of three
     * takes the data. */
    if (invalid) {
      return impossible(state);
    }
    if (!dirty) {
      return (ap_m68040_cache_transition_t){.possible = true,
                                            .next = AP_M68040_LINE_INVALID};
    }
    return (ap_m68040_cache_transition_t){.possible = true,
                                          .next = AP_M68040_LINE_DIRTY,
                                          .inhibit_memory = true,
                                          .sink_data = true,
                                          .set_dirty = true};
  case AP_M68040_CACHE_OP_SNOOP_WRITE_SINK_LINE:
    /* I13 "Not Possible". V13 and D13 both "no action; go to invalid state":
     * the alternate master is replacing the whole line, so there is nothing to
     * merge into and nothing worth keeping. */
    if (invalid) {
      return impossible(state);
    }
    return (ap_m68040_cache_transition_t){.possible = true,
                                          .next = AP_M68040_LINE_INVALID};
  default:
    return impossible(state);
  }
}

ap_m68040_cache_transition_t
ap_m68040_cache_transition(bool has_dirty_state, ap_m68040_line_state_t state,
                           ap_m68040_cache_op_t op) {
  if (!has_dirty_state) {
    /* Table 4-3 has no dirty column at all, so a caller that reaches one has a
     * bug rather than a transition. */
    if (state == AP_M68040_LINE_DIRTY) {
      return impossible(state);
    }
    return instruction_transition(state, op);
  }
  return data_transition(state, op);
}
