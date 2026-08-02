/* MC68020 on-chip instruction cache.
 *
 * `MC68020 32-Bit Microprocessor User's Manual` §7.1.1 and Figure 7-1.
 *
 * Every test names a hardware fact that distinguishes this cache from the
 * 68030's, because the whole risk in this module is modelling one as the other.
 */

#include <string.h>

#include "cpu/m68020/ap_m68020_cache.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* FC2 set is supervisor space, clear is user. */
#define SUPERVISOR_PROGRAM 6u
#define USER_PROGRAM 2u

static void test_a_cleared_cache_misses_everywhere(void) {
  ap_m68020_cache_t cache;
  memset(&cache, 0xFF, sizeof cache);
  ap_m68020_cache_clear(&cache);

  for (unsigned i = 0; i < AP_M68020_CACHE_ENTRIES; i++) {
    uint16_t word = 0;
    TEST_ASSERT(!ap_m68020_cache_lookup(&cache, i * 4u, SUPERVISOR_PROGRAM, &word));
  }
}

static void test_the_index_is_address_bits_2_through_7(void) {
  /* "The index field (A2-A7) of the access address". Six bits above the long
   * word: consecutive long words take consecutive entries, and A0-A1 select
   * within the entry rather than choosing it. */
  TEST_ASSERT_EQUAL_UINT32(ap_m68020_cache_index(0x00000000u), 0u);
  TEST_ASSERT_EQUAL_UINT32(ap_m68020_cache_index(0x00000003u), 0u);
  TEST_ASSERT_EQUAL_UINT32(ap_m68020_cache_index(0x00000004u), 1u);
  TEST_ASSERT_EQUAL_UINT32(ap_m68020_cache_index(0x000000FCu), 63u);
  /* A8 is the first bit above the index, so it wraps: this is what makes the
   * address space "partitioned into blocks, each 256 bytes in size". */
  TEST_ASSERT_EQUAL_UINT32(ap_m68020_cache_index(0x00000100u), 0u);
}

static void test_the_cache_covers_256_bytes_in_64_long_word_entries(void) {
  /* The 68030 also holds 256 bytes, but as 16 lines of four long words. The
   * difference shows in the index width: six bits here, four there. */
  TEST_ASSERT_EQUAL_UINT32(AP_M68020_CACHE_ENTRIES, 64u);
  TEST_ASSERT_EQUAL_UINT32(AP_M68020_CACHE_ENTRIES * 4u, 256u);
}

static void test_the_tag_holds_the_upper_24_address_bits(void) {
  /* "A tag field made up of the upper 24 address bits and the FC2 value." */
  TEST_ASSERT_EQUAL_UINT32(ap_m68020_cache_tag(0x12345678u, USER_PROGRAM), 0x00123456u);
  /* Everything below A8 is index and word select, so it cannot reach the tag. */
  TEST_ASSERT_EQUAL_UINT32(ap_m68020_cache_tag(0x123456FFu, USER_PROGRAM),
                 ap_m68020_cache_tag(0x12345600u, USER_PROGRAM));
}

static void test_the_tag_holds_fc2_so_supervisor_and_user_are_distinct(void) {
  /* One address, two entries -- which is what stops a user-mode fetch from
   * hitting on an instruction the supervisor cached at the same address. */
  TEST_ASSERT(ap_m68020_cache_tag(0x1000u, SUPERVISOR_PROGRAM) !=
            ap_m68020_cache_tag(0x1000u, USER_PROGRAM));
}

static void test_the_tag_ignores_fc0_and_fc1(void) {
  /* Only FC2 is in the tag. FC1 distinguishes program from data space and FC0
   * is part of the space encoding; neither participates, which is right for a
   * cache every access to which is a program fetch. */
  const uint8_t supervisor_data = 5u; /* FC2 set, FC1 clear */
  TEST_ASSERT_EQUAL_UINT32(ap_m68020_cache_tag(0x1000u, SUPERVISOR_PROGRAM),
                 ap_m68020_cache_tag(0x1000u, supervisor_data));
}

static void test_a_filled_entry_hits_on_the_same_address(void) {
  ap_m68020_cache_t cache;
  ap_m68020_cache_clear(&cache);
  ap_m68020_cache_fill(&cache, 0x2000u, SUPERVISOR_PROGRAM, 0x4E714E75u, false);

  uint16_t word = 0;
  TEST_ASSERT(ap_m68020_cache_lookup(&cache, 0x2000u, SUPERVISOR_PROGRAM, &word));
  TEST_ASSERT_EQUAL_UINT32(word, 0x4E71u);
}

static void test_address_bit_1_selects_the_word_within_the_entry(void) {
  /* "Address bit A1 is used to select the proper word from the cache entry."
   * One fill answers two fetches, which is the whole reason the entry is a long
   * word and not a word. */
  ap_m68020_cache_t cache;
  ap_m68020_cache_clear(&cache);
  ap_m68020_cache_fill(&cache, 0x2000u, SUPERVISOR_PROGRAM, 0x4E714E75u, false);

  uint16_t high = 0, low = 0;
  TEST_ASSERT(ap_m68020_cache_lookup(&cache, 0x2000u, SUPERVISOR_PROGRAM, &high));
  TEST_ASSERT(ap_m68020_cache_lookup(&cache, 0x2002u, SUPERVISOR_PROGRAM, &low));
  /* Big endian: A1 clear is the upper half. */
  TEST_ASSERT_EQUAL_UINT32(high, 0x4E71u);
  TEST_ASSERT_EQUAL_UINT32(low, 0x4E75u);
}

static void test_a_fetch_with_a_different_tag_misses_on_a_valid_entry(void) {
  /* The index collides, the tag does not: a miss, not a hit on the wrong data.
   * 0x2000 and 0x2100 differ only in A8, the first bit above the index. */
  ap_m68020_cache_t cache;
  ap_m68020_cache_clear(&cache);
  ap_m68020_cache_fill(&cache, 0x2000u, SUPERVISOR_PROGRAM, 0x4E714E75u, false);

  uint16_t word = 0;
  TEST_ASSERT_EQUAL_UINT32(ap_m68020_cache_index(0x2100u),
                 ap_m68020_cache_index(0x2000u));
  TEST_ASSERT(!ap_m68020_cache_lookup(&cache, 0x2100u, SUPERVISOR_PROGRAM, &word));
}

static void test_a_supervisor_fill_does_not_answer_a_user_fetch(void) {
  ap_m68020_cache_t cache;
  ap_m68020_cache_clear(&cache);
  ap_m68020_cache_fill(&cache, 0x2000u, SUPERVISOR_PROGRAM, 0x4E714E75u, false);

  uint16_t word = 0;
  TEST_ASSERT(!ap_m68020_cache_lookup(&cache, 0x2000u, USER_PROGRAM, &word));
}

static void test_a_fill_replaces_the_entry_at_a_colliding_index(void) {
  /* Direct mapped with one entry per index, so there is no associativity to
   * fall back on: the second fill evicts the first. The 68030 would have kept
   * both if they fell in different entries of one line. */
  ap_m68020_cache_t cache;
  ap_m68020_cache_clear(&cache);
  ap_m68020_cache_fill(&cache, 0x2000u, SUPERVISOR_PROGRAM, 0x11112222u, false);
  ap_m68020_cache_fill(&cache, 0x2100u, SUPERVISOR_PROGRAM, 0x33334444u, false);

  uint16_t word = 0;
  TEST_ASSERT(!ap_m68020_cache_lookup(&cache, 0x2000u, SUPERVISOR_PROGRAM, &word));
  TEST_ASSERT(ap_m68020_cache_lookup(&cache, 0x2100u, SUPERVISOR_PROGRAM, &word));
  TEST_ASSERT_EQUAL_UINT32(word, 0x3333u);
}

static void test_a_frozen_cache_refuses_the_fill(void) {
  /* "This new instruction is automatically written into the cache entry, and
   * the valid bit is set, unless the freeze cache bit has been set." */
  ap_m68020_cache_t cache;
  ap_m68020_cache_clear(&cache);
  ap_m68020_cache_fill(&cache, 0x2000u, SUPERVISOR_PROGRAM, 0x4E714E75u, true);

  uint16_t word = 0;
  TEST_ASSERT(!ap_m68020_cache_lookup(&cache, 0x2000u, SUPERVISOR_PROGRAM, &word));
}

static void test_a_frozen_cache_still_hits_on_what_it_already_holds(void) {
  /* Freeze stops replacement, not lookup -- which is what makes freezing useful
   * for holding a loop resident rather than merely a way to disable the cache. */
  ap_m68020_cache_t cache;
  ap_m68020_cache_clear(&cache);
  ap_m68020_cache_fill(&cache, 0x2000u, SUPERVISOR_PROGRAM, 0x4E714E75u, false);
  ap_m68020_cache_fill(&cache, 0x2100u, SUPERVISOR_PROGRAM, 0x33334444u, true);

  uint16_t word = 0;
  TEST_ASSERT(ap_m68020_cache_lookup(&cache, 0x2000u, SUPERVISOR_PROGRAM, &word));
  TEST_ASSERT_EQUAL_UINT32(word, 0x4E71u);
}

static void test_clearing_one_entry_leaves_the_others(void) {
  ap_m68020_cache_t cache;
  ap_m68020_cache_clear(&cache);
  ap_m68020_cache_fill(&cache, 0x2000u, SUPERVISOR_PROGRAM, 0x11112222u, false);
  ap_m68020_cache_fill(&cache, 0x2004u, SUPERVISOR_PROGRAM, 0x33334444u, false);
  ap_m68020_cache_clear_entry(&cache, 0x2000u);

  uint16_t word = 0;
  TEST_ASSERT(!ap_m68020_cache_lookup(&cache, 0x2000u, SUPERVISOR_PROGRAM, &word));
  TEST_ASSERT(ap_m68020_cache_lookup(&cache, 0x2004u, SUPERVISOR_PROGRAM, &word));
}

static void test_clearing_an_entry_clears_it_for_every_function_code(void) {
  /* The clear-entry operation is indexed by address alone -- there is one entry
   * at an index whatever tag it holds, so it cannot clear selectively by FC2. */
  ap_m68020_cache_t cache;
  ap_m68020_cache_clear(&cache);
  ap_m68020_cache_fill(&cache, 0x2000u, USER_PROGRAM, 0x11112222u, false);
  ap_m68020_cache_clear_entry(&cache, 0x2000u);

  uint16_t word = 0;
  TEST_ASSERT(!ap_m68020_cache_lookup(&cache, 0x2000u, USER_PROGRAM, &word));
}

static void test_all_64_entries_are_independently_addressable(void) {
  /* Fill the whole cache from one 256-byte block and read every word back: no
   * index aliases another, and 64 distinct long words all survive together. */
  ap_m68020_cache_t cache;
  ap_m68020_cache_clear(&cache);
  for (unsigned i = 0; i < AP_M68020_CACHE_ENTRIES; i++) {
    ap_m68020_cache_fill(&cache, 0x8000u + i * 4u, SUPERVISOR_PROGRAM,
                         0x10000000u + i, false);
  }
  for (unsigned i = 0; i < AP_M68020_CACHE_ENTRIES; i++) {
    uint16_t word = 0;
    TEST_ASSERT(ap_m68020_cache_lookup(&cache, 0x8002u + i * 4u,
                                     SUPERVISOR_PROGRAM, &word));
    TEST_ASSERT_EQUAL_UINT32(word, (uint16_t)i);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_cleared_cache_misses_everywhere);
  RUN_TEST(test_the_index_is_address_bits_2_through_7);
  RUN_TEST(test_the_cache_covers_256_bytes_in_64_long_word_entries);
  RUN_TEST(test_the_tag_holds_the_upper_24_address_bits);
  RUN_TEST(test_the_tag_holds_fc2_so_supervisor_and_user_are_distinct);
  RUN_TEST(test_the_tag_ignores_fc0_and_fc1);
  RUN_TEST(test_a_filled_entry_hits_on_the_same_address);
  RUN_TEST(test_address_bit_1_selects_the_word_within_the_entry);
  RUN_TEST(test_a_fetch_with_a_different_tag_misses_on_a_valid_entry);
  RUN_TEST(test_a_supervisor_fill_does_not_answer_a_user_fetch);
  RUN_TEST(test_a_fill_replaces_the_entry_at_a_colliding_index);
  RUN_TEST(test_a_frozen_cache_refuses_the_fill);
  RUN_TEST(test_a_frozen_cache_still_hits_on_what_it_already_holds);
  RUN_TEST(test_clearing_one_entry_leaves_the_others);
  RUN_TEST(test_clearing_an_entry_clears_it_for_every_function_code);
  RUN_TEST(test_all_64_entries_are_independently_addressable);
  return UNITY_END();
}
