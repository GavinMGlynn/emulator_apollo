/* MC68851 translation control register, `[68851]` §6.1.3 and Figure 6-3.
 *
 * The bit boundaries were read from the page image. Every test below names the
 * rule it checks, because this register is almost entirely rules: most of its
 * 2^32 values are illegal, and which ones is the specification.
 */

#include "cpu/m68851/ap_m68851_tc.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* A configuration that satisfies the consistency check, used as the base for
 * tests that perturb one field: 4K pages (PS=$C), no initial shift, and two
 * levels of ten index bits. 0 + 10 + 10 + 0 + 0 + 12 = 32. */
static ap_m68851_tc_t valid_tc(void) {
  return (ap_m68851_tc_t){
      .enable = true,
      .page_size = 0xCu,
      .initial_shift = 0,
      .table_index = {10u, 10u, 0u, 0u},
  };
}

static void test_the_register_fields_sit_where_figure_6_3_draws_them(void) {
  /* E@31, SRE@25, FCL@24, PS@23-20, IS@19-16, then the four nibbles. */
  const ap_m68851_tc_t tc = ap_m68851_tc_decode(0x83C01234u);
  TEST_ASSERT_TRUE(tc.enable);
  TEST_ASSERT_TRUE(tc.supervisor_root_pointer_enable);
  TEST_ASSERT_TRUE(tc.function_code_lookup);
  TEST_ASSERT_EQUAL_UINT(0xCu, tc.page_size);
  TEST_ASSERT_EQUAL_UINT(0x0u, tc.initial_shift);
  TEST_ASSERT_EQUAL_UINT(1u, tc.table_index[0]); /* TIA */
  TEST_ASSERT_EQUAL_UINT(2u, tc.table_index[1]); /* TIB */
  TEST_ASSERT_EQUAL_UINT(3u, tc.table_index[2]); /* TIC */
  TEST_ASSERT_EQUAL_UINT(4u, tc.table_index[3]); /* TID */
}

static void test_the_three_control_bits_are_independent(void) {
  /* E, SRE and FCL are adjacent-ish and easy to transpose. Each set alone. */
  TEST_ASSERT_TRUE(ap_m68851_tc_decode(0x80000000u).enable);
  TEST_ASSERT_FALSE(ap_m68851_tc_decode(0x80000000u).supervisor_root_pointer_enable);
  TEST_ASSERT_FALSE(ap_m68851_tc_decode(0x80000000u).function_code_lookup);

  TEST_ASSERT_FALSE(ap_m68851_tc_decode(0x02000000u).enable);
  TEST_ASSERT_TRUE(ap_m68851_tc_decode(0x02000000u).supervisor_root_pointer_enable);
  TEST_ASSERT_FALSE(ap_m68851_tc_decode(0x02000000u).function_code_lookup);

  TEST_ASSERT_FALSE(ap_m68851_tc_decode(0x01000000u).enable);
  TEST_ASSERT_FALSE(ap_m68851_tc_decode(0x01000000u).supervisor_root_pointer_enable);
  TEST_ASSERT_TRUE(ap_m68851_tc_decode(0x01000000u).function_code_lookup);
}

static void test_the_unimplemented_bits_read_as_zeros(void) {
  /* "All unimplemented fields of this register are read as zeros and must
   * always be written as zeros." Bits 30-26. */
  const ap_m68851_tc_t tc = ap_m68851_tc_decode(0xFFFFFFFFu);
  TEST_ASSERT_EQUAL_HEX32(AP_M68851_TC_IMPLEMENTED_MASK,
                          ap_m68851_tc_encode(&tc));
  TEST_ASSERT_EQUAL_HEX32(0u, ap_m68851_tc_encode(&tc) & 0x7C000000u);
}

static void test_encoding_round_trips_every_implemented_bit(void) {
  /* Sweeping the implemented mask a bit at a time: each bit that survives
   * decode must come back at the same position, which catches a field read at
   * the right width but the wrong offset. */
  for (unsigned bit = 0; bit < 32u; bit++) {
    const uint32_t value = 1u << bit;
    if ((value & AP_M68851_TC_IMPLEMENTED_MASK) == 0u) {
      continue;
    }
    const ap_m68851_tc_t tc = ap_m68851_tc_decode(value);
    TEST_ASSERT_EQUAL_HEX32(value, ap_m68851_tc_encode(&tc));
  }
}

static void test_the_bit_total_accounts_for_a_whole_logical_address(void) {
  /* "The TIx fields are added together, and this sum is added to PS and IS.
   * The total must be 32." Discarded bits, index bits and page offset bits
   * between them cover the address exactly once. */
  const ap_m68851_tc_t tc = valid_tc();
  TEST_ASSERT_EQUAL_UINT(32u, ap_m68851_tc_bit_total(&tc));
  TEST_ASSERT_EQUAL_INT(AP_M68851_TC_OK, ap_m68851_tc_check(&tc));
}

static void test_a_total_other_than_32_is_a_configuration_error(void) {
  /* Both directions: too few bits leaves part of the address unaccounted for,
   * too many uses a bit twice. Neither is merely suboptimal. */
  ap_m68851_tc_t small = valid_tc();
  small.table_index[1] = 9u;
  TEST_ASSERT_EQUAL_UINT(31u, ap_m68851_tc_bit_total(&small));
  TEST_ASSERT_EQUAL_INT(AP_M68851_TC_INCONSISTENT, ap_m68851_tc_check(&small));

  ap_m68851_tc_t large = valid_tc();
  large.table_index[1] = 11u;
  TEST_ASSERT_EQUAL_UINT(33u, ap_m68851_tc_bit_total(&large));
  TEST_ASSERT_EQUAL_INT(AP_M68851_TC_INCONSISTENT, ap_m68851_tc_check(&large));
}

static void test_the_initial_shift_counts_toward_the_total(void) {
  /* IS is "the number of bits to discard from the logical address" -- discarded
   * bits are still accounted for, which is what lets the part "adapt to systems
   * using logical addresses consisting of 17 to 32 bits". */
  ap_m68851_tc_t tc = valid_tc();
  tc.initial_shift = 8u;
  tc.table_index[0] = 2u; /* 8 + 2 + 10 + 12 = 32 */
  TEST_ASSERT_EQUAL_INT(AP_M68851_TC_OK, ap_m68851_tc_check(&tc));
}

static void test_page_size_bit_3_must_be_set(void) {
  /* "Page size bit [3] must always be one. Writing values of zero to bit [3] of
   * this field will cause an MMU configuration exception." */
  for (unsigned ps = 0; ps < 8u; ps++) {
    ap_m68851_tc_t tc = valid_tc();
    tc.page_size = ps;
    TEST_ASSERT_EQUAL_INT(AP_M68851_TC_PAGE_SIZE_TOO_SMALL,
                          ap_m68851_tc_check(&tc));
  }
}

static void test_the_eight_documented_page_sizes(void) {
  /* §6.1.3.4's table verbatim: $8 is 256 bytes through $F at 32K. PS is a
   * logarithm, which is why the check can simply add it to the other widths. */
  const struct { unsigned ps; uint32_t bytes; } sizes[] = {
      {0x8u, 256u},  {0x9u, 512u},   {0xAu, 1024u},  {0xBu, 2048u},
      {0xCu, 4096u}, {0xDu, 8192u},  {0xEu, 16384u}, {0xFu, 32768u},
  };
  for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
    ap_m68851_tc_t tc = valid_tc();
    tc.page_size = sizes[i].ps;
    TEST_ASSERT_EQUAL_UINT32(sizes[i].bytes, ap_m68851_tc_page_bytes(&tc));
  }
}

static void test_every_page_size_admits_a_consistent_configuration(void) {
  /* If some legal page size could not be made to total 32, either the check or
   * the table would be wrong.
   *
   * The index bits cannot all go in TIA: a TIx field is four bits, so one level
   * indexes at most 15 and the smallest page size leaves 24 bits to cover. That
   * is the reason there are four levels rather than one wider field -- spread
   * the remainder across as many as it takes. */
  for (unsigned ps = 0x8u; ps <= 0xFu; ps++) {
    ap_m68851_tc_t tc = {.enable = true, .page_size = ps, .initial_shift = 0};
    unsigned remaining = 32u - ps;
    for (unsigned level = 0; level < 4u && remaining > 0u; level++) {
      const unsigned take = remaining > 15u ? 15u : remaining;
      tc.table_index[level] = take;
      remaining -= take;
    }
    TEST_ASSERT_EQUAL_UINT(0u, remaining);
    TEST_ASSERT_EQUAL_INT(AP_M68851_TC_OK, ap_m68851_tc_check(&tc));
  }
}

static void test_a_zero_table_index_terminates_the_search(void) {
  /* "A zero value in a TIx field specifies that the lookup process is over when
   * that field is encountered during a table search." So a zero is a
   * terminator, not a level that happens to index nothing -- and levels beyond
   * it are unreachable however they are filled in. */
  const ap_m68851_tc_t two = valid_tc();
  TEST_ASSERT_EQUAL_UINT(2u, ap_m68851_tc_levels(&two));

  ap_m68851_tc_t buried = valid_tc();
  buried.table_index[3] = 4u; /* past the terminator: still two levels */
  TEST_ASSERT_EQUAL_UINT(2u, ap_m68851_tc_levels(&buried));

  const ap_m68851_tc_t none = {.table_index = {0u, 0u, 0u, 0u}};
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68851_tc_levels(&none));

  const ap_m68851_tc_t four = {.table_index = {4u, 4u, 4u, 4u}};
  TEST_ASSERT_EQUAL_UINT(4u, ap_m68851_tc_levels(&four));
}

static void test_a_buried_index_still_counts_toward_the_total(void) {
  /* The consistency check sums all four TIx fields, terminator or not: it says
   * "the TIx fields are added together" without excluding unreachable ones. So
   * a tree that stops at two levels but leaves rubbish in TID is refused --
   * which is why software must zero the fields it does not use. */
  ap_m68851_tc_t tc = valid_tc();
  tc.table_index[3] = 4u;
  TEST_ASSERT_EQUAL_UINT(36u, ap_m68851_tc_bit_total(&tc));
  TEST_ASSERT_EQUAL_INT(AP_M68851_TC_INCONSISTENT, ap_m68851_tc_check(&tc));
}

static void test_a_four_level_tree_is_expressible(void) {
  /* All four levels in use, which is what the four fields exist for:
   * 0 + 5 + 5 + 5 + 5 + 12 = 32. */
  const ap_m68851_tc_t tc = {.enable = true,
                             .page_size = 0xCu,
                             .initial_shift = 0,
                             .table_index = {5u, 5u, 5u, 5u}};
  TEST_ASSERT_EQUAL_UINT(4u, ap_m68851_tc_levels(&tc));
  TEST_ASSERT_EQUAL_INT(AP_M68851_TC_OK, ap_m68851_tc_check(&tc));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_register_fields_sit_where_figure_6_3_draws_them);
  RUN_TEST(test_the_three_control_bits_are_independent);
  RUN_TEST(test_the_unimplemented_bits_read_as_zeros);
  RUN_TEST(test_encoding_round_trips_every_implemented_bit);
  RUN_TEST(test_the_bit_total_accounts_for_a_whole_logical_address);
  RUN_TEST(test_a_total_other_than_32_is_a_configuration_error);
  RUN_TEST(test_the_initial_shift_counts_toward_the_total);
  RUN_TEST(test_page_size_bit_3_must_be_set);
  RUN_TEST(test_the_eight_documented_page_sizes);
  RUN_TEST(test_every_page_size_admits_a_consistent_configuration);
  RUN_TEST(test_a_zero_table_index_terminates_the_search);
  RUN_TEST(test_a_buried_index_still_counts_toward_the_total);
  RUN_TEST(test_a_four_level_tree_is_expressible);
  return UNITY_END();
}
