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

int main(void) {
  UNITY_BEGIN();
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
  return UNITY_END();
}
