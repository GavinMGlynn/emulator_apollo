/* MC68040 instruction and data caches, `[68040]` §4.1 and Figure 4-2.
 *
 * The third cache organisation in this core and the first that is set
 * associative, so several tests contrast it with the two already modelled.
 */

#include <string.h>

#include "cpu/m68040/ap_m68040_cache.h"
#include "cpu/m68020/ap_m68020_cache.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static const uint32_t sample[AP_M68040_CACHE_LINE_LONGS] = {
    0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u};

/* ---------------------------------------------------------------------------
 * Organisation.
 * ------------------------------------------------------------------------- */

static void test_the_geometry_accounts_for_four_kilobytes(void) {
  /* "Both four-way set-associative caches have 64 sets of four 16-byte
   * lines." */
  TEST_ASSERT_EQUAL_UINT(64u, AP_M68040_CACHE_SETS);
  TEST_ASSERT_EQUAL_UINT(4u, AP_M68040_CACHE_WAYS);
  TEST_ASSERT_EQUAL_UINT(16u, AP_M68040_CACHE_LINE_BYTES);
  TEST_ASSERT_EQUAL_UINT(AP_M68040_CACHE_BYTES,
                         AP_M68040_CACHE_SETS * AP_M68040_CACHE_WAYS *
                             AP_M68040_CACHE_LINE_BYTES);
}

static void test_this_cache_is_sixteen_times_the_68020s(void) {
  /* 4 Kbytes against 256 bytes, and set associative against direct mapped --
   * the two facts that make this a different cache rather than a bigger one. */
  TEST_ASSERT_EQUAL_UINT(4096u, AP_M68040_CACHE_BYTES);
  TEST_ASSERT_EQUAL_UINT(256u, AP_M68020_CACHE_ENTRIES * 4u);
}

static void test_the_address_splits_into_tag_set_and_long_word(void) {
  /* Tag 31-10, set 9-4, long word 3-2, byte 1-0. The three together account
   * for the whole address, which is the check that none overlaps. */
  const uint32_t address = 0x12345678u;
  TEST_ASSERT_EQUAL_HEX32(0x48D15u, ap_m68040_cache_tag(address));
  TEST_ASSERT_EQUAL_UINT(0x27u, ap_m68040_cache_set(address));
  TEST_ASSERT_EQUAL_UINT(0x2u, ap_m68040_cache_long(address));
}

static void test_the_tag_is_the_upper_22_bits(void) {
  /* "An address tag consisting of the upper 22 bits of the physical address."
   * Everything below bit 10 is set index and offset, so it cannot reach the
   * tag -- which is what makes a whole line share one. */
  TEST_ASSERT_EQUAL_HEX32(ap_m68040_cache_tag(0x12345000u),
                          ap_m68040_cache_tag(0x123453FFu));
  TEST_ASSERT_NOT_EQUAL_UINT32(ap_m68040_cache_tag(0x12345000u),
                               ap_m68040_cache_tag(0x12345400u));
}

/* ---------------------------------------------------------------------------
 * Lookup.
 * ------------------------------------------------------------------------- */

static void test_a_line_is_found_in_any_way_of_its_set(void) {
  /* Set associative: every way of the set is compared, so a line hits wherever
   * it was placed. A direct-mapped model would only find it in one. */
  for (unsigned way = 0; way < AP_M68040_CACHE_WAYS; way++) {
    ap_m68040_cache_t cache;
    ap_m68040_cache_init(&cache, false);
    ap_m68040_cache_fill(&cache, way, 0x12345670u, sample, 0u);
    TEST_ASSERT_EQUAL_UINT(way, ap_m68040_cache_lookup(&cache, 0x12345670u));
  }
}

static void test_four_addresses_that_collide_all_fit(void) {
  /* Four tags in one set: the associativity is what stops the fourth evicting
   * the first. Addresses 0x400 apart share a set and differ in tag. */
  ap_m68040_cache_t cache;
  ap_m68040_cache_init(&cache, false);
  for (unsigned i = 0; i < AP_M68040_CACHE_WAYS; i++) {
    const uint32_t address = 0x1000u + i * 0x400u;
    TEST_ASSERT_EQUAL_UINT(ap_m68040_cache_set(0x1000u),
                           ap_m68040_cache_set(address));
    ap_m68040_cache_fill(&cache, ap_m68040_cache_select_way(&cache, address),
                         address, sample, 0u);
  }
  for (unsigned i = 0; i < AP_M68040_CACHE_WAYS; i++) {
    TEST_ASSERT_NOT_EQUAL_UINT(
        AP_M68040_CACHE_WAYS,
        ap_m68040_cache_lookup(&cache, 0x1000u + i * 0x400u));
  }
}

static void test_a_miss_is_reported_rather_than_a_wrong_line(void) {
  ap_m68040_cache_t cache;
  ap_m68040_cache_init(&cache, false);
  ap_m68040_cache_fill(&cache, 0u, 0x1000u, sample, 0u);
  TEST_ASSERT_EQUAL_UINT(AP_M68040_CACHE_WAYS,
                         ap_m68040_cache_lookup(&cache, 0x1400u));
}

static void test_an_invalid_line_never_hits(void) {
  ap_m68040_cache_t cache;
  ap_m68040_cache_init(&cache, false);
  ap_m68040_cache_fill(&cache, 0u, 0x1000u, sample, 0u);
  ap_m68040_cache_invalidate_all(&cache);
  TEST_ASSERT_EQUAL_UINT(AP_M68040_CACHE_WAYS,
                         ap_m68040_cache_lookup(&cache, 0x1000u));
}

/* ---------------------------------------------------------------------------
 * Replacement.
 * ------------------------------------------------------------------------- */

static void test_an_invalid_way_is_preferred(void) {
  /* "If all lines in the set are already valid, a pseudo-random replacement
   * algorithm is used" -- so the counter only matters once the set is full,
   * and a cold cache fills before it evicts. */
  ap_m68040_cache_t cache;
  ap_m68040_cache_init(&cache, false);
  cache.counter = 2u;
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68040_cache_select_way(&cache, 0x1000u));

  ap_m68040_cache_fill(&cache, 0u, 0x1000u, sample, 0u);
  TEST_ASSERT_EQUAL_UINT(1u, ap_m68040_cache_select_way(&cache, 0x1400u));
}

static void test_a_full_set_is_replaced_by_the_counter(void) {
  /* "The line pointed to by the current counter" -- one counter per cache, two
   * bits, and entirely deterministic despite the manual's name for it. */
  ap_m68040_cache_t cache;
  ap_m68040_cache_init(&cache, false);
  for (unsigned way = 0; way < AP_M68040_CACHE_WAYS; way++) {
    ap_m68040_cache_fill(&cache, way, 0x1000u + way * 0x400u, sample, 0u);
  }
  for (unsigned n = 0; n < AP_M68040_CACHE_WAYS; n++) {
    cache.counter = n;
    TEST_ASSERT_EQUAL_UINT(n, ap_m68040_cache_select_way(&cache, 0x1000u));
  }
}

static void test_the_counter_is_two_bits_and_wraps(void) {
  ap_m68040_cache_t cache;
  ap_m68040_cache_init(&cache, false);
  for (unsigned i = 0; i < 4u; i++) {
    TEST_ASSERT_EQUAL_UINT(i, cache.counter);
    ap_m68040_cache_tick(&cache);
  }
  TEST_ASSERT_EQUAL_UINT(0u, cache.counter);
}

static void test_the_counter_belongs_to_the_cache_not_the_set(void) {
  /* "Each cache contains a 2-bit counter, which is incremented for each access
   * to the cache." So activity in one set moves the victim chosen in another --
   * a per-set counter would be a different, and quieter, machine. */
  ap_m68040_cache_t cache;
  ap_m68040_cache_init(&cache, false);
  /* Two *different* sets: the index is bits 9-4, so the addresses must differ
   * there -- 0x1000 and 0x2000 share a set, which is the trap this comment
   * exists to record. */
  for (unsigned way = 0; way < AP_M68040_CACHE_WAYS; way++) {
    ap_m68040_cache_fill(&cache, way, 0x1000u + way * 0x400u, sample, 0u);
    ap_m68040_cache_fill(&cache, way, 0x1010u + way * 0x400u, sample, 0u);
  }
  TEST_ASSERT_NOT_EQUAL_UINT(ap_m68040_cache_set(0x1000u),
                             ap_m68040_cache_set(0x1010u));

  const unsigned before = ap_m68040_cache_select_way(&cache, 0x1000u);
  ap_m68040_cache_tick(&cache); /* an access anywhere in the cache */
  TEST_ASSERT_NOT_EQUAL_UINT(before,
                             ap_m68040_cache_select_way(&cache, 0x1000u));
}

/* ---------------------------------------------------------------------------
 * Line state and dirty long words.
 * ------------------------------------------------------------------------- */

static void test_the_three_line_states(void) {
  /* "For invalid lines, the V-bit is clear ... Valid lines have their V-bit set
   * and D-bits cleared ... Dirty cache lines have the V-bit and one or more
   * D-bits set." Dirty implies valid, so these are three states rather than
   * two independent bits. */
  ap_m68040_cache_t cache;
  ap_m68040_cache_init(&cache, true);
  TEST_ASSERT_EQUAL_INT(AP_M68040_LINE_INVALID,
                        ap_m68040_cache_line_state(&cache.line[0][0]));

  ap_m68040_cache_fill(&cache, 0u, 0x1000u, sample, 0u);
  TEST_ASSERT_EQUAL_INT(AP_M68040_LINE_VALID,
                        ap_m68040_cache_line_state(&cache.line[0][0]));

  ap_m68040_cache_mark_dirty(&cache, 0u, 0x1000u);
  TEST_ASSERT_EQUAL_INT(AP_M68040_LINE_DIRTY,
                        ap_m68040_cache_line_state(&cache.line[0][0]));
}

static void test_a_dirty_bit_belongs_to_one_long_word(void) {
  /* "Four additional bits to indicate dirty status for each long word in the
   * line." A copyback of a partly-written line writes back only what changed;
   * one dirty bit per *line* would write back clean data, which is invisible
   * in memory contents and wrong in the bus traffic a probe measures. */
  ap_m68040_cache_t cache;
  ap_m68040_cache_init(&cache, true);
  ap_m68040_cache_fill(&cache, 0u, 0x1000u, sample, 0u);

  ap_m68040_cache_mark_dirty(&cache, 0u, 0x1008u); /* long word 2 */
  TEST_ASSERT_EQUAL_UINT(
      0x4u, ap_m68040_cache_writeback_mask(&cache.line[0][0]));

  ap_m68040_cache_mark_dirty(&cache, 0u, 0x1000u); /* long word 0 */
  TEST_ASSERT_EQUAL_UINT(
      0x5u, ap_m68040_cache_writeback_mask(&cache.line[0][0]));
}

static void test_only_the_data_cache_has_dirty_state(void) {
  /* "Note that only the data cache supports dirty cache lines." An instruction
   * cache asked to record one has no bits for it, so it must not silently keep
   * the state somewhere else. */
  ap_m68040_cache_t icache;
  ap_m68040_cache_init(&icache, false);
  ap_m68040_cache_fill(&icache, 0u, 0x1000u, sample, 0xFu);
  TEST_ASSERT_EQUAL_INT(AP_M68040_LINE_VALID,
                        ap_m68040_cache_line_state(&icache.line[0][0]));
  ap_m68040_cache_mark_dirty(&icache, 0u, 0x1000u);
  TEST_ASSERT_EQUAL_INT(AP_M68040_LINE_VALID,
                        ap_m68040_cache_line_state(&icache.line[0][0]));
  TEST_ASSERT_EQUAL_UINT(
      0u, ap_m68040_cache_writeback_mask(&icache.line[0][0]));
}

static void test_an_invalid_line_writes_nothing_back(void) {
  ap_m68040_cache_t cache;
  ap_m68040_cache_init(&cache, true);
  ap_m68040_cache_fill(&cache, 0u, 0x1000u, sample, 0xFu);
  TEST_ASSERT_EQUAL_UINT(
      0xFu, ap_m68040_cache_writeback_mask(&cache.line[0][0]));
  ap_m68040_cache_invalidate_all(&cache);
  TEST_ASSERT_EQUAL_UINT(
      0u, ap_m68040_cache_writeback_mask(&cache.line[0][0]));
}

static void test_a_whole_line_is_filled_at_once(void) {
  /* "Only burst mode accesses that successfully read four long words can be
   * cached. The cache stores an entire line, providing validity on a
   * line-by-line basis." So there is no partial fill to model. */
  ap_m68040_cache_t cache;
  ap_m68040_cache_init(&cache, true);
  ap_m68040_cache_fill(&cache, 0u, 0x1000u, sample, 0u);
  for (unsigned i = 0; i < AP_M68040_CACHE_LINE_LONGS; i++) {
    TEST_ASSERT_EQUAL_HEX32(sample[i], cache.line[0][0].data[i]);
  }
}

/* ---------------------------------------------------------------------------
 * Snoop control, `[040]` Table 4-1.
 * ------------------------------------------------------------------------- */

static void test_the_snoop_control_pins_decode_to_five_requests(void) {
  /* Table 4-1, both columns. Four encodings x two directions, and the read and
   * write columns differ for 01 and 10. */
  TEST_ASSERT_EQUAL_INT(AP_M68040_SNOOP_INHIBIT,
                        ap_m68040_snoop_request(0u, false));
  TEST_ASSERT_EQUAL_INT(AP_M68040_SNOOP_INHIBIT,
                        ap_m68040_snoop_request(0u, true));
  TEST_ASSERT_EQUAL_INT(AP_M68040_SNOOP_SUPPLY_LEAVE_DIRTY,
                        ap_m68040_snoop_request(1u, false));
  TEST_ASSERT_EQUAL_INT(AP_M68040_SNOOP_SINK,
                        ap_m68040_snoop_request(1u, true));
  TEST_ASSERT_EQUAL_INT(AP_M68040_SNOOP_SUPPLY_MARK_INVALID,
                        ap_m68040_snoop_request(2u, false));
  TEST_ASSERT_EQUAL_INT(AP_M68040_SNOOP_INVALIDATE,
                        ap_m68040_snoop_request(2u, true));
}

static void test_the_reserved_snoop_encoding_inhibits_rather_than_faults(void) {
  /* "Reserved (Snoop Inhibited)" in both columns, so 11 behaves as 00. The pins
   * are driven by another master and the part has no way to complain. */
  TEST_ASSERT_EQUAL_INT(AP_M68040_SNOOP_INHIBIT,
                        ap_m68040_snoop_request(3u, false));
  TEST_ASSERT_EQUAL_INT(AP_M68040_SNOOP_INHIBIT,
                        ap_m68040_snoop_request(3u, true));
}

/* ---------------------------------------------------------------------------
 * Line state transitions, `[040]` Tables 4-3 and 4-4.
 * ------------------------------------------------------------------------- */

/* The next-state column of Table 4-4, transcribed. `X` marks the cells the
 * manual prints as "Not Possible". Rows are in the enum's order, which is the
 * table's order; columns are invalid, valid, dirty. */
#define X (-1)
static const int data_next_state[13][3] = {
    /* 1  CPU Read Miss                 */ {1, 1, 1},
    /* 2  CPU Read Hit                  */ {X, 1, 2},
    /* 3  CPU Write Miss (Copyback)     */ {2, 2, 2},
    /* 4  CPU Write Miss (Write-thru)   */ {0, 1, 2},
    /* 5  CPU Write Hit (Copyback)      */ {X, 2, 2},
    /* 6  CPU Write Hit (Write-thru)    */ {X, 1, 2},
    /* 7  Cache Invalidate              */ {0, 0, 0},
    /* 8  Cache Push                    */ {0, 0, 0},
    /* 9  Snoop Read, leave dirty       */ {X, 1, 2},
    /* 10 Snoop Read, invalidate        */ {X, 0, 0},
    /* 11 Snoop Write, invalidate       */ {X, 0, 0},
    /* 12 Snoop Write, sink, size != ln */ {X, 0, 2},
    /* 13 Snoop Write, sink, size  = ln */ {X, 0, 0},
};

/* Table 4-3, the same shape. The instruction cache has no dirty column, and
 * rows 3 and 7/8 are one row in the manual, as are 11, 12 and 13. */
static const int instruction_next_state[13][3] = {
    /* 1  CPU Read Miss           I1/V1 */ {1, 1, X},
    /* 2  CPU Read Hit            I2/V2 */ {X, 1, X},
    /* 3  no such row                   */ {X, X, X},
    /* 4  no such row                   */ {X, X, X},
    /* 5  no such row                   */ {X, X, X},
    /* 6  no such row                   */ {X, X, X},
    /* 7  CINV                    I3/V3 */ {0, 0, X},
    /* 8  CPUSH                   I3/V3 */ {0, 0, X},
    /* 9  Snoop Read leave dirty  I4/V4 */ {X, X, X},
    /* 10 Snoop Read invalidate   I5/V5 */ {X, 0, X},
    /* 11 Snoop Write invalidate  I6/V6 */ {X, 0, X},
    /* 12 Snoop Write sink != ln  I6/V6 */ {X, 0, X},
    /* 13 Snoop Write sink  = ln  I6/V6 */ {X, 0, X},
};

static void check_table(bool has_dirty_state, const int expected[13][3]) {
  for (int op = 0; op < 13; op++) {
    for (int state = 0; state < 3; state++) {
      const ap_m68040_cache_transition_t t = ap_m68040_cache_transition(
          has_dirty_state, (ap_m68040_line_state_t)state,
          (ap_m68040_cache_op_t)op);
      if (expected[op][state] == X) {
        TEST_ASSERT_FALSE(t.possible);
        /* "Not Possible" leaves the line where it was. */
        TEST_ASSERT_EQUAL_INT(state, (int)t.next);
        continue;
      }
      TEST_ASSERT_TRUE(t.possible);
      TEST_ASSERT_EQUAL_INT(expected[op][state], (int)t.next);
    }
  }
}

static void test_every_cell_of_the_data_cache_table(void) {
  /* All thirty-nine cells of Table 4-4, next state only; the actions are
   * checked one trap at a time below. */
  check_table(true, data_next_state);
}

static void test_every_cell_of_the_instruction_cache_table(void) {
  /* Table 4-3. Its absent rows -- every write, and the leave-dirty read
   * snoop -- report impossible rather than guessing a data-cache answer. */
  check_table(false, instruction_next_state);
}
#undef X

static void test_an_instruction_cache_line_is_never_dirty(void) {
  /* Table 4-3 has no dirty column, so the state itself is unreachable. */
  const ap_m68040_cache_transition_t t = ap_m68040_cache_transition(
      false, AP_M68040_LINE_DIRTY, AP_M68040_CACHE_OP_CPU_READ_HIT);
  TEST_ASSERT_FALSE(t.possible);
}

static void test_a_push_invalidates_as_well_as_writing_back(void) {
  /* D8: "write dirty data to memory; go to invalid state". The write-back is
   * the obvious half; the invalidate is the half a model forgets. */
  const ap_m68040_cache_transition_t t = ap_m68040_cache_transition(
      true, AP_M68040_LINE_DIRTY, AP_M68040_CACHE_OP_CPUSH);
  TEST_ASSERT_TRUE(t.push_dirty);
  TEST_ASSERT_EQUAL_INT(AP_M68040_LINE_INVALID, (int)t.next);
}

static void test_an_invalidate_loses_the_dirty_data(void) {
  /* D7: "no action (dirty data lost); go to invalid state". Same end state as
   * a push and a different bus history, which is the whole difference between
   * the two instructions. */
  const ap_m68040_cache_transition_t t = ap_m68040_cache_transition(
      true, AP_M68040_LINE_DIRTY, AP_M68040_CACHE_OP_CINV);
  TEST_ASSERT_TRUE(t.dirty_data_lost);
  TEST_ASSERT_FALSE(t.push_dirty);
  TEST_ASSERT_EQUAL_INT(AP_M68040_LINE_INVALID, (int)t.next);
}

static void test_only_a_dirty_line_sinks_a_snooped_write(void) {
  /* V12 is "no action; go to invalid state" and D12 sinks. A clean line is
   * discarded rather than merged, so the sink path is reachable from one state
   * out of three. */
  const ap_m68040_cache_transition_t clean = ap_m68040_cache_transition(
      true, AP_M68040_LINE_VALID, AP_M68040_CACHE_OP_SNOOP_WRITE_SINK_PARTIAL);
  TEST_ASSERT_FALSE(clean.sink_data);
  TEST_ASSERT_EQUAL_INT(AP_M68040_LINE_INVALID, (int)clean.next);

  const ap_m68040_cache_transition_t dirty = ap_m68040_cache_transition(
      true, AP_M68040_LINE_DIRTY, AP_M68040_CACHE_OP_SNOOP_WRITE_SINK_PARTIAL);
  TEST_ASSERT_TRUE(dirty.sink_data);
  TEST_ASSERT_TRUE(dirty.inhibit_memory);
  TEST_ASSERT_TRUE(dirty.set_dirty);
  TEST_ASSERT_EQUAL_INT(AP_M68040_LINE_DIRTY, (int)dirty.next);
}

static void test_a_line_sized_snooped_write_never_sinks(void) {
  /* D13, the row that resolves Table 4-1's misprinted cell: at line size there
   * is nothing to merge, so the line goes invalid whatever its state. */
  const ap_m68040_cache_transition_t t = ap_m68040_cache_transition(
      true, AP_M68040_LINE_DIRTY, AP_M68040_CACHE_OP_SNOOP_WRITE_SINK_LINE);
  TEST_ASSERT_FALSE(t.sink_data);
  TEST_ASSERT_EQUAL_INT(AP_M68040_LINE_INVALID, (int)t.next);
}

static void test_a_dirty_line_sources_data_to_the_alternate_master(void) {
  /* D9: "inhibit memory and source data; remain in current state". D10 does
   * the same and then invalidates -- the difference between the two read
   * encodings is only what happens to our copy. */
  const ap_m68040_cache_transition_t leave = ap_m68040_cache_transition(
      true, AP_M68040_LINE_DIRTY, AP_M68040_CACHE_OP_SNOOP_READ_LEAVE_DIRTY);
  TEST_ASSERT_TRUE(leave.inhibit_memory);
  TEST_ASSERT_TRUE(leave.source_data);
  TEST_ASSERT_EQUAL_INT(AP_M68040_LINE_DIRTY, (int)leave.next);

  const ap_m68040_cache_transition_t mark = ap_m68040_cache_transition(
      true, AP_M68040_LINE_DIRTY, AP_M68040_CACHE_OP_SNOOP_READ_INVALIDATE);
  TEST_ASSERT_TRUE(mark.source_data);
  TEST_ASSERT_EQUAL_INT(AP_M68040_LINE_INVALID, (int)mark.next);
}

static void test_a_clean_line_lets_memory_answer_a_read_snoop(void) {
  /* V9: "no action; remain in current state". Only dirty data is worth
   * intervening for. */
  const ap_m68040_cache_transition_t t = ap_m68040_cache_transition(
      true, AP_M68040_LINE_VALID, AP_M68040_CACHE_OP_SNOOP_READ_LEAVE_DIRTY);
  TEST_ASSERT_FALSE(t.inhibit_memory);
  TEST_ASSERT_FALSE(t.source_data);
  TEST_ASSERT_EQUAL_INT(AP_M68040_LINE_VALID, (int)t.next);
}

static void test_replacing_a_dirty_line_buffers_it(void) {
  /* D1 and D3: "buffer dirty cache line; read new line from memory; ... write
   * buffered dirty data to memory". The push buffer of §4.6.2, and the order
   * matters -- the new line is fetched first to cut the requested data's
   * latency. */
  const ap_m68040_cache_transition_t read = ap_m68040_cache_transition(
      true, AP_M68040_LINE_DIRTY, AP_M68040_CACHE_OP_CPU_READ_MISS);
  TEST_ASSERT_TRUE(read.buffer_dirty);
  TEST_ASSERT_TRUE(read.read_line);
  /* The new line is clean, so a dirty line ends valid on a read miss -- the
   * only state change a read causes. */
  TEST_ASSERT_EQUAL_INT(AP_M68040_LINE_VALID, (int)read.next);

  const ap_m68040_cache_transition_t write = ap_m68040_cache_transition(
      true, AP_M68040_LINE_DIRTY, AP_M68040_CACHE_OP_CPU_WRITE_MISS_COPYBACK);
  TEST_ASSERT_TRUE(write.buffer_dirty);
  TEST_ASSERT_EQUAL_INT(AP_M68040_LINE_DIRTY, (int)write.next);
}

static void test_a_write_through_miss_does_not_allocate(void) {
  /* I4: "write data to memory; remain in current state" -- no read, no fill.
   * A copyback write miss (I3) reads the line first, which is the difference
   * that costs a bus transfer. */
  const ap_m68040_cache_transition_t wt = ap_m68040_cache_transition(
      true, AP_M68040_LINE_INVALID,
      AP_M68040_CACHE_OP_CPU_WRITE_MISS_WRITE_THROUGH);
  TEST_ASSERT_FALSE(wt.read_line);
  TEST_ASSERT_TRUE(wt.write_to_memory);
  TEST_ASSERT_EQUAL_INT(AP_M68040_LINE_INVALID, (int)wt.next);

  const ap_m68040_cache_transition_t cb = ap_m68040_cache_transition(
      true, AP_M68040_LINE_INVALID, AP_M68040_CACHE_OP_CPU_WRITE_MISS_COPYBACK);
  TEST_ASSERT_TRUE(cb.read_line);
  TEST_ASSERT_FALSE(cb.write_to_memory);
  TEST_ASSERT_EQUAL_INT(AP_M68040_LINE_DIRTY, (int)cb.next);
}

static void test_a_write_through_hit_on_a_dirty_line_is_flagged(void) {
  /* The NOTE under Table 4-4: "dirty state transitions D4 and D6 are the result
   * of a system programming error and should be avoided even though they are
   * technically valid" -- a page whose attributes changed without a flush. They
   * are reported, not refused, and D6 leaves the D-bits alone: "write data into
   * cache (no change to Dn bits)". */
  const ap_m68040_cache_transition_t d6 = ap_m68040_cache_transition(
      true, AP_M68040_LINE_DIRTY, AP_M68040_CACHE_OP_CPU_WRITE_HIT_WRITE_THROUGH);
  TEST_ASSERT_TRUE(d6.programming_error);
  TEST_ASSERT_TRUE(d6.write_to_cache);
  TEST_ASSERT_TRUE(d6.write_to_memory);
  TEST_ASSERT_FALSE(d6.set_dirty);

  const ap_m68040_cache_transition_t d4 = ap_m68040_cache_transition(
      true, AP_M68040_LINE_DIRTY,
      AP_M68040_CACHE_OP_CPU_WRITE_MISS_WRITE_THROUGH);
  TEST_ASSERT_TRUE(d4.programming_error);

  /* V4 and V6 are the same operations on a clean line and are ordinary. */
  TEST_ASSERT_FALSE(
      ap_m68040_cache_transition(true, AP_M68040_LINE_VALID,
                                 AP_M68040_CACHE_OP_CPU_WRITE_HIT_WRITE_THROUGH)
          .programming_error);
}

static void test_the_instruction_cache_is_not_read_snooped(void) {
  /* I4/V4: "not possible; not snooped". It is invalidated by the other three
   * snoop rows, so "not snooped" applies to the leave-dirty read alone. */
  TEST_ASSERT_FALSE(
      ap_m68040_cache_transition(false, AP_M68040_LINE_VALID,
                                 AP_M68040_CACHE_OP_SNOOP_READ_LEAVE_DIRTY)
          .possible);
  TEST_ASSERT_TRUE(
      ap_m68040_cache_transition(false, AP_M68040_LINE_VALID,
                                 AP_M68040_CACHE_OP_SNOOP_READ_INVALIDATE)
          .possible);
}

static void test_a_push_to_the_instruction_cache_writes_nothing_back(void) {
  /* Table 4-3 merges `CINV` and `CPUSH` into one row because there is no dirty
   * data: both are "no action", and neither pushes. */
  const ap_m68040_cache_transition_t push = ap_m68040_cache_transition(
      false, AP_M68040_LINE_VALID, AP_M68040_CACHE_OP_CPUSH);
  TEST_ASSERT_FALSE(push.push_dirty);
  TEST_ASSERT_EQUAL_INT(AP_M68040_LINE_INVALID, (int)push.next);
}

/* ---- Line and page maintenance, `M68000PRM`'s CINV and CPUSH -------------- */

/* Fill one line at a known physical address. */
static void place(ap_m68040_cache_t *cache, uint32_t address,
                  unsigned dirty_mask) {
  const uint32_t data[AP_M68040_CACHE_LINE_LONGS] = {1u, 2u, 3u, 4u};
  const unsigned way = ap_m68040_cache_select_way(cache, address);
  ap_m68040_cache_fill(cache, way, address, data, dirty_mask);
}

/* The set index and the tag put a line's address back together, which is what
 * a page-scoped operation has to do. */
static void test_the_set_and_tag_shifts_reconstruct_an_address(void) {
  TEST_ASSERT_EQUAL_UINT(4u, AP_M68040_CACHE_SET_SHIFT);
  TEST_ASSERT_EQUAL_UINT(10u, AP_M68040_CACHE_TAG_SHIFT);
  const uint32_t address = 0x00123450u;
  const uint32_t rebuilt =
      (ap_m68040_cache_tag(address) << AP_M68040_CACHE_TAG_SHIFT) |
      ((uint32_t)ap_m68040_cache_set(address) << AP_M68040_CACHE_SET_SHIFT);
  TEST_ASSERT_EQUAL_HEX32(address & ~0xFu, rebuilt);
}

/* CINV over one line drops it "without regard to its dirty state" -- which is
 * the whole difference between CINV and CPUSH. */
static void test_cinv_of_a_line_discards_dirty_data(void) {
  ap_m68040_cache_t cache;
  ap_m68040_cache_init(&cache, true);
  place(&cache, 0x00010000u, 0x3u);
  TEST_ASSERT_TRUE(ap_m68040_cache_lookup(&cache, 0x00010000u) <
                   AP_M68040_CACHE_WAYS);

  TEST_ASSERT_TRUE(ap_m68040_cache_invalidate_line(&cache, 0x00010000u));
  TEST_ASSERT_EQUAL_UINT(AP_M68040_CACHE_WAYS,
                         ap_m68040_cache_lookup(&cache, 0x00010000u));
  /* And a line that is not there is not an error. */
  TEST_ASSERT_FALSE(ap_m68040_cache_invalidate_line(&cache, 0x00020000u));
}

/* CPUSH reports what a dirty line owes memory and then invalidates it. */
static void test_cpush_of_a_line_reports_its_writeback(void) {
  ap_m68040_cache_t cache;
  ap_m68040_cache_init(&cache, true);
  place(&cache, 0x00010000u, 0x5u); /* long words 0 and 2 dirty */

  unsigned mask = 0u;
  TEST_ASSERT_EQUAL_UINT(1u,
                         ap_m68040_cache_push_line(&cache, 0x00010000u, &mask));
  TEST_ASSERT_EQUAL_HEX8(0x5u, mask);
  TEST_ASSERT_EQUAL_UINT(AP_M68040_CACHE_WAYS,
                         ap_m68040_cache_lookup(&cache, 0x00010000u));
}

/* An instruction cache has no dirty state, so a push there finds nothing to
 * write back and degenerates to an invalidate. */
static void test_a_push_of_the_instruction_cache_writes_nothing_back(void) {
  ap_m68040_cache_t cache;
  ap_m68040_cache_init(&cache, false);
  place(&cache, 0x00010000u, 0xFu);
  unsigned mask = 0xFu;
  TEST_ASSERT_EQUAL_UINT(1u,
                         ap_m68040_cache_push_line(&cache, 0x00010000u, &mask));
  TEST_ASSERT_EQUAL_HEX8(0u, mask);
}

/* A page scope reaches every line inside the page and none outside it. A page
 * is the MMU's unit, so the size is passed in. */
static void test_a_page_scope_reaches_only_that_page(void) {
  ap_m68040_cache_t cache;
  ap_m68040_cache_init(&cache, true);
  /* Three lines inside one 4 KB page, and one in the next. */
  place(&cache, 0x00010000u, 0u);
  place(&cache, 0x00010010u, 0u);
  place(&cache, 0x00010FF0u, 0u);
  place(&cache, 0x00011000u, 0u);

  TEST_ASSERT_EQUAL_UINT(
      3u, ap_m68040_cache_invalidate_page(&cache, 0x00010800u, 4096u));
  TEST_ASSERT_EQUAL_UINT(AP_M68040_CACHE_WAYS,
                         ap_m68040_cache_lookup(&cache, 0x00010000u));
  /* The line in the next page is untouched. */
  TEST_ASSERT_TRUE(ap_m68040_cache_lookup(&cache, 0x00011000u) <
                   AP_M68040_CACHE_WAYS);

  /* And an 8 KB page takes both, `[040]` §3's `TCR` bit 14. */
  TEST_ASSERT_EQUAL_UINT(
      1u, ap_m68040_cache_invalidate_page(&cache, 0x00010000u, 8192u));
}

/* A page push counts the dirty lines it wrote back, not every line it
 * invalidated. */
static void test_a_page_push_counts_only_the_dirty_lines(void) {
  ap_m68040_cache_t cache;
  ap_m68040_cache_init(&cache, true);
  place(&cache, 0x00010000u, 0u);
  place(&cache, 0x00010010u, 0x1u);
  place(&cache, 0x00010020u, 0x8u);

  unsigned dirty = 0u;
  TEST_ASSERT_EQUAL_UINT(
      3u, ap_m68040_cache_push_page(&cache, 0x00010000u, 4096u, &dirty));
  TEST_ASSERT_EQUAL_UINT(2u, dirty);
}

/* An "all" push empties the cache and counts what it owed memory. */
static void test_an_all_push_empties_the_cache(void) {
  ap_m68040_cache_t cache;
  ap_m68040_cache_init(&cache, true);
  place(&cache, 0x00010000u, 0x1u);
  place(&cache, 0x00020000u, 0u);
  place(&cache, 0x00030000u, 0xFu);

  unsigned dirty = 0u;
  TEST_ASSERT_EQUAL_UINT(3u, ap_m68040_cache_push_all(&cache, &dirty));
  TEST_ASSERT_EQUAL_UINT(2u, dirty);
  for (unsigned set = 0; set < AP_M68040_CACHE_SETS; set++) {
    for (unsigned way = 0; way < AP_M68040_CACHE_WAYS; way++) {
      TEST_ASSERT_FALSE(cache.line[set][way].valid);
    }
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_set_and_tag_shifts_reconstruct_an_address);
  RUN_TEST(test_cinv_of_a_line_discards_dirty_data);
  RUN_TEST(test_cpush_of_a_line_reports_its_writeback);
  RUN_TEST(test_a_push_of_the_instruction_cache_writes_nothing_back);
  RUN_TEST(test_a_page_scope_reaches_only_that_page);
  RUN_TEST(test_a_page_push_counts_only_the_dirty_lines);
  RUN_TEST(test_an_all_push_empties_the_cache);
  RUN_TEST(test_the_geometry_accounts_for_four_kilobytes);
  RUN_TEST(test_this_cache_is_sixteen_times_the_68020s);
  RUN_TEST(test_the_address_splits_into_tag_set_and_long_word);
  RUN_TEST(test_the_tag_is_the_upper_22_bits);
  RUN_TEST(test_a_line_is_found_in_any_way_of_its_set);
  RUN_TEST(test_four_addresses_that_collide_all_fit);
  RUN_TEST(test_a_miss_is_reported_rather_than_a_wrong_line);
  RUN_TEST(test_an_invalid_line_never_hits);
  RUN_TEST(test_an_invalid_way_is_preferred);
  RUN_TEST(test_a_full_set_is_replaced_by_the_counter);
  RUN_TEST(test_the_counter_is_two_bits_and_wraps);
  RUN_TEST(test_the_counter_belongs_to_the_cache_not_the_set);
  RUN_TEST(test_the_three_line_states);
  RUN_TEST(test_a_dirty_bit_belongs_to_one_long_word);
  RUN_TEST(test_only_the_data_cache_has_dirty_state);
  RUN_TEST(test_an_invalid_line_writes_nothing_back);
  RUN_TEST(test_a_whole_line_is_filled_at_once);
  RUN_TEST(test_the_snoop_control_pins_decode_to_five_requests);
  RUN_TEST(test_the_reserved_snoop_encoding_inhibits_rather_than_faults);
  RUN_TEST(test_every_cell_of_the_data_cache_table);
  RUN_TEST(test_every_cell_of_the_instruction_cache_table);
  RUN_TEST(test_an_instruction_cache_line_is_never_dirty);
  RUN_TEST(test_a_push_invalidates_as_well_as_writing_back);
  RUN_TEST(test_an_invalidate_loses_the_dirty_data);
  RUN_TEST(test_only_a_dirty_line_sinks_a_snooped_write);
  RUN_TEST(test_a_line_sized_snooped_write_never_sinks);
  RUN_TEST(test_a_dirty_line_sources_data_to_the_alternate_master);
  RUN_TEST(test_a_clean_line_lets_memory_answer_a_read_snoop);
  RUN_TEST(test_replacing_a_dirty_line_buffers_it);
  RUN_TEST(test_a_write_through_miss_does_not_allocate);
  RUN_TEST(test_a_write_through_hit_on_a_dirty_line_is_flagged);
  RUN_TEST(test_the_instruction_cache_is_not_read_snooped);
  RUN_TEST(test_a_push_to_the_instruction_cache_writes_nothing_back);
  return UNITY_END();
}
