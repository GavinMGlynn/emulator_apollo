/* MC68040 address translation caches, `[68040]` §3.3 and Figures 3-20 and 3-21.
 *
 * One test pins a width the manual states two ways in one sentence; the
 * derivation is in the header and the test checks it holds.
 */

#include "cpu/m68040/ap_m68040_atc.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define SUPERVISOR true
#define USER false

static ap_m68040_atc_entry_t mapping(uint32_t physical) {
  return (ap_m68040_atc_entry_t){.physical_address = physical,
                                 .resident = true};
}

/* ---------------------------------------------------------------------------
 * Geometry.
 * ------------------------------------------------------------------------- */

static void test_sixteen_sets_of_four_ways_hold_sixty_four_entries(void) {
  /* "Four-way set-associative caches that each store 64 logical-to-physical
   * address translations", and Figure 3-20 draws SET 0 through SET 15. */
  TEST_ASSERT_EQUAL_UINT(16u, AP_M68040_ATC_SETS);
  TEST_ASSERT_EQUAL_UINT(4u, AP_M68040_ATC_WAYS);
  TEST_ASSERT_EQUAL_UINT(AP_M68040_ATC_ENTRIES,
                         AP_M68040_ATC_SETS * AP_M68040_ATC_WAYS);
}

static void test_the_tag_is_sixteen_bits_not_thirteen(void) {
  /* The manual says both in one sentence: "This 13-bit field ... All 16 bits of
   * this field are used ... when the page size is 4 Kbytes." Sixteen is right,
   * and it follows from numbers the same manual states: sixteen sets need four
   * select bits, a 4-Kbyte page number is address bits 31-12 (twenty bits), and
   * twenty less four is sixteen.
   *
   * The test states the derivation rather than the constant, so it would fail
   * if the geometry ever changed under it. */
  const unsigned page_number_bits = 32u - 12u; /* 4-Kbyte pages */
  unsigned set_select_bits = 0;
  for (unsigned sets = AP_M68040_ATC_SETS; sets > 1u; sets >>= 1) {
    set_select_bits++;
  }
  TEST_ASSERT_EQUAL_UINT(4u, set_select_bits);
  TEST_ASSERT_EQUAL_UINT(AP_M68040_ATC_TAG_BITS,
                         page_number_bits - set_select_bits);
}

static void test_the_set_is_the_low_four_bits_of_the_page_number(void) {
  /* At 4K the page number starts at bit 12, so the set select is bits 15-12. */
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68040_atc_set(0x00000000u, AP_M68040_PAGE_4K));
  TEST_ASSERT_EQUAL_UINT(1u, ap_m68040_atc_set(0x00001000u, AP_M68040_PAGE_4K));
  TEST_ASSERT_EQUAL_UINT(15u,
                         ap_m68040_atc_set(0x0000F000u, AP_M68040_PAGE_4K));
  /* And it wraps at bit 16, which is where the tag begins. */
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68040_atc_set(0x00010000u, AP_M68040_PAGE_4K));
}

static void test_the_page_size_moves_the_set_and_the_tag_together(void) {
  /* At 8K the page number starts at bit 13, so the set select is bits 16-13 and
   * the tag begins at 17. Both move, which is why neither can be a constant. */
  TEST_ASSERT_EQUAL_UINT(1u, ap_m68040_atc_set(0x00002000u, AP_M68040_PAGE_8K));
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68040_atc_set(0x00001000u, AP_M68040_PAGE_8K));
  TEST_ASSERT_NOT_EQUAL_UINT32(
      ap_m68040_atc_tag(0x00020000u, AP_M68040_PAGE_4K),
      ap_m68040_atc_tag(0x00020000u, AP_M68040_PAGE_8K));
}

static void test_addresses_within_a_page_share_a_tag_and_set(void) {
  for (uint32_t offset = 0; offset < 0x1000u; offset += 0x400u) {
    TEST_ASSERT_EQUAL_UINT32(
        ap_m68040_atc_tag(0x12345000u, AP_M68040_PAGE_4K),
        ap_m68040_atc_tag(0x12345000u + offset, AP_M68040_PAGE_4K));
    TEST_ASSERT_EQUAL_UINT(
        ap_m68040_atc_set(0x12345000u, AP_M68040_PAGE_4K),
        ap_m68040_atc_set(0x12345000u + offset, AP_M68040_PAGE_4K));
  }
}

/* ---------------------------------------------------------------------------
 * Lookup.
 * ------------------------------------------------------------------------- */

static void test_an_entry_is_found_in_any_way_of_its_set(void) {
  for (unsigned way = 0; way < AP_M68040_ATC_WAYS; way++) {
    ap_m68040_atc_t atc;
    ap_m68040_atc_init(&atc);
    ap_m68040_atc_fill(&atc, way, 0x12345000u, AP_M68040_PAGE_4K,
                       mapping(0x90000000u));
    TEST_ASSERT_EQUAL_UINT(way, ap_m68040_atc_lookup(&atc, 0x12345000u, USER,
                                                     AP_M68040_PAGE_4K));
  }
}

static void test_fc2_is_part_of_the_match(void) {
  /* "FC2 is set for supervisor mode accesses and cleared for user mode
   * accesses", and it is the only function code bit the tag carries -- the
   * separate instruction and data ATCs make the others unnecessary. */
  ap_m68040_atc_t atc;
  ap_m68040_atc_init(&atc);
  ap_m68040_atc_entry_t e = mapping(0x90000000u);
  e.supervisor_space = SUPERVISOR;
  ap_m68040_atc_fill(&atc, 0u, 0x12345000u, AP_M68040_PAGE_4K, e);

  TEST_ASSERT_EQUAL_UINT(0u, ap_m68040_atc_lookup(&atc, 0x12345000u,
                                                  SUPERVISOR,
                                                  AP_M68040_PAGE_4K));
  TEST_ASSERT_EQUAL_UINT(AP_M68040_ATC_WAYS,
                         ap_m68040_atc_lookup(&atc, 0x12345000u, USER,
                                              AP_M68040_PAGE_4K));
}

static void test_an_invalid_entry_never_matches(void) {
  ap_m68040_atc_t atc;
  ap_m68040_atc_init(&atc);
  ap_m68040_atc_fill(&atc, 0u, 0x12345000u, AP_M68040_PAGE_4K,
                     mapping(0x90000000u));
  ap_m68040_atc_flush_all(&atc);
  TEST_ASSERT_EQUAL_UINT(AP_M68040_ATC_WAYS,
                         ap_m68040_atc_lookup(&atc, 0x12345000u, USER,
                                              AP_M68040_PAGE_4K));
}

static void test_four_pages_sharing_a_set_all_fit(void) {
  /* Pages 0x10000 apart share a set and differ in tag, so the associativity is
   * what keeps the fourth from evicting the first. */
  ap_m68040_atc_t atc;
  ap_m68040_atc_init(&atc);
  for (unsigned i = 0; i < AP_M68040_ATC_WAYS; i++) {
    const uint32_t address = 0x1000u + i * 0x10000u;
    TEST_ASSERT_EQUAL_UINT(ap_m68040_atc_set(0x1000u, AP_M68040_PAGE_4K),
                           ap_m68040_atc_set(address, AP_M68040_PAGE_4K));
    ap_m68040_atc_fill(
        &atc, ap_m68040_atc_select_way(&atc, address, AP_M68040_PAGE_4K),
        address, AP_M68040_PAGE_4K, mapping(0x90000000u + i));
  }
  for (unsigned i = 0; i < AP_M68040_ATC_WAYS; i++) {
    TEST_ASSERT_NOT_EQUAL_UINT(
        AP_M68040_ATC_WAYS,
        ap_m68040_atc_lookup(&atc, 0x1000u + i * 0x10000u, USER,
                             AP_M68040_PAGE_4K));
  }
}

/* ---------------------------------------------------------------------------
 * Replacement.
 * ------------------------------------------------------------------------- */

static void test_an_invalid_way_is_preferred_then_the_counter(void) {
  /* "A 2-bit counter, which is incremented for each ATC access, points to the
   * entry to replace when an access misses" -- but only once the set is full. */
  ap_m68040_atc_t atc;
  ap_m68040_atc_init(&atc);
  atc.counter = 3u;
  TEST_ASSERT_EQUAL_UINT(
      0u, ap_m68040_atc_select_way(&atc, 0x1000u, AP_M68040_PAGE_4K));

  for (unsigned i = 0; i < AP_M68040_ATC_WAYS; i++) {
    ap_m68040_atc_fill(&atc, i, 0x1000u + i * 0x10000u, AP_M68040_PAGE_4K,
                       mapping(0u));
  }
  for (unsigned n = 0; n < AP_M68040_ATC_WAYS; n++) {
    atc.counter = n;
    TEST_ASSERT_EQUAL_UINT(
        n, ap_m68040_atc_select_way(&atc, 0x1000u, AP_M68040_PAGE_4K));
  }
}

static void test_the_counter_wraps_at_four(void) {
  ap_m68040_atc_t atc;
  ap_m68040_atc_init(&atc);
  for (unsigned i = 0; i < 4u; i++) {
    TEST_ASSERT_EQUAL_UINT(i, atc.counter);
    ap_m68040_atc_tick(&atc);
  }
  TEST_ASSERT_EQUAL_UINT(0u, atc.counter);
}

/* ---------------------------------------------------------------------------
 * Flushing, and what `G` protects.
 * ------------------------------------------------------------------------- */

static void test_a_global_entry_survives_a_nonglobal_flush(void) {
  /* "Global entries are not invalidated by the PFLUSH instruction variants that
   * specify nonglobal entries, even when all other selection criteria are
   * satisfied." `G` overrides the match rather than being one more criterion --
   * which is the 68040's substitute for the 68851's task alias. */
  ap_m68040_atc_t atc;
  ap_m68040_atc_init(&atc);

  ap_m68040_atc_entry_t plain = mapping(0x90000000u);
  ap_m68040_atc_entry_t global = mapping(0xA0000000u);
  global.global = true;
  ap_m68040_atc_fill(&atc, 0u, 0x1000u, AP_M68040_PAGE_4K, plain);
  ap_m68040_atc_fill(&atc, 1u, 0x11000u, AP_M68040_PAGE_4K, global);

  ap_m68040_atc_flush_nonglobal(&atc, USER);
  TEST_ASSERT_EQUAL_UINT(AP_M68040_ATC_WAYS,
                         ap_m68040_atc_lookup(&atc, 0x1000u, USER,
                                              AP_M68040_PAGE_4K));
  TEST_ASSERT_NOT_EQUAL_UINT(AP_M68040_ATC_WAYS,
                             ap_m68040_atc_lookup(&atc, 0x11000u, USER,
                                                  AP_M68040_PAGE_4K));
}

static void test_a_flush_all_takes_global_entries_too(void) {
  ap_m68040_atc_t atc;
  ap_m68040_atc_init(&atc);
  ap_m68040_atc_entry_t global = mapping(0xA0000000u);
  global.global = true;
  ap_m68040_atc_fill(&atc, 0u, 0x1000u, AP_M68040_PAGE_4K, global);

  ap_m68040_atc_flush_all(&atc);
  TEST_ASSERT_EQUAL_UINT(AP_M68040_ATC_WAYS,
                         ap_m68040_atc_lookup(&atc, 0x1000u, USER,
                                              AP_M68040_PAGE_4K));
}

static void test_a_nonglobal_flush_spares_the_other_privilege_mode(void) {
  ap_m68040_atc_t atc;
  ap_m68040_atc_init(&atc);
  ap_m68040_atc_entry_t user = mapping(0x90000000u);
  ap_m68040_atc_entry_t super = mapping(0xA0000000u);
  super.supervisor_space = SUPERVISOR;
  ap_m68040_atc_fill(&atc, 0u, 0x1000u, AP_M68040_PAGE_4K, user);
  ap_m68040_atc_fill(&atc, 1u, 0x1000u, AP_M68040_PAGE_4K, super);

  ap_m68040_atc_flush_nonglobal(&atc, USER);
  TEST_ASSERT_EQUAL_UINT(AP_M68040_ATC_WAYS,
                         ap_m68040_atc_lookup(&atc, 0x1000u, USER,
                                              AP_M68040_PAGE_4K));
  TEST_ASSERT_NOT_EQUAL_UINT(AP_M68040_ATC_WAYS,
                             ap_m68040_atc_lookup(&atc, 0x1000u, SUPERVISOR,
                                                  AP_M68040_PAGE_4K));
}

static void test_a_page_flush_takes_only_that_page(void) {
  ap_m68040_atc_t atc;
  ap_m68040_atc_init(&atc);
  ap_m68040_atc_fill(&atc, 0u, 0x1000u, AP_M68040_PAGE_4K,
                     mapping(0x90000000u));
  ap_m68040_atc_fill(&atc, 1u, 0x11000u, AP_M68040_PAGE_4K,
                     mapping(0xA0000000u));

  ap_m68040_atc_flush_page(&atc, 0x1800u, USER, AP_M68040_PAGE_4K);
  /* Any address within the page names the page. */
  TEST_ASSERT_EQUAL_UINT(AP_M68040_ATC_WAYS,
                         ap_m68040_atc_lookup(&atc, 0x1000u, USER,
                                              AP_M68040_PAGE_4K));
  TEST_ASSERT_NOT_EQUAL_UINT(AP_M68040_ATC_WAYS,
                             ap_m68040_atc_lookup(&atc, 0x11000u, USER,
                                                  AP_M68040_PAGE_4K));
}

static void test_the_resident_bit_is_the_opposite_polarity_to_the_68851s(void) {
  /* "R ... set if the table search successfully completes without encountering
   * either a nonresident page or a transfer error acknowledge." The 68851's
   * equivalent is `B`, set when the search *failed*. An entry copied across
   * without inverting would turn every good translation into a bus error, so
   * the default-constructed entry is deliberately *not* resident. */
  const ap_m68040_atc_entry_t blank = {0};
  TEST_ASSERT_FALSE(blank.resident);
  TEST_ASSERT_TRUE(mapping(0x90000000u).resident);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_sixteen_sets_of_four_ways_hold_sixty_four_entries);
  RUN_TEST(test_the_tag_is_sixteen_bits_not_thirteen);
  RUN_TEST(test_the_set_is_the_low_four_bits_of_the_page_number);
  RUN_TEST(test_the_page_size_moves_the_set_and_the_tag_together);
  RUN_TEST(test_addresses_within_a_page_share_a_tag_and_set);
  RUN_TEST(test_an_entry_is_found_in_any_way_of_its_set);
  RUN_TEST(test_fc2_is_part_of_the_match);
  RUN_TEST(test_an_invalid_entry_never_matches);
  RUN_TEST(test_four_pages_sharing_a_set_all_fit);
  RUN_TEST(test_an_invalid_way_is_preferred_then_the_counter);
  RUN_TEST(test_the_counter_wraps_at_four);
  RUN_TEST(test_a_global_entry_survives_a_nonglobal_flush);
  RUN_TEST(test_a_flush_all_takes_global_entries_too);
  RUN_TEST(test_a_nonglobal_flush_spares_the_other_privilege_mode);
  RUN_TEST(test_a_page_flush_takes_only_that_page);
  RUN_TEST(test_the_resident_bit_is_the_opposite_polarity_to_the_68851s);
  return UNITY_END();
}
