/* MC68030 translation control register.
 *
 * Cited to MC68030 User's Manual 3ed §9.7.2 pp. 9-54 ff. throughout.
 *
 * The consistency rule is the centrepiece: the TIx fields summed until a zero
 * is reached, plus PS and IS, must total exactly 32. That is the hardware
 * stating that a logical address is fully accounted for -- every bit either
 * ignored, used as a table index, or part of the page offset. A configuration
 * that does not add up is rejected by the part, so it is rejected here.
 */

#include "cpu/m68030/ap_m68030_tc.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Build a TC value from fields, using the layout ap_m68030_tc.h justifies. */
static uint32_t tc_value(bool enable, bool sre, bool fcl, uint8_t ps, uint8_t is,
                         uint8_t tia, uint8_t tib, uint8_t tic, uint8_t tid) {
  return (enable ? UINT32_C(0x80000000) : 0) | (sre ? UINT32_C(0x02000000) : 0) |
         (fcl ? UINT32_C(0x01000000) : 0) | ((uint32_t)(ps & 0x0Fu) << 20) |
         ((uint32_t)(is & 0x0Fu) << 16) | ((uint32_t)(tia & 0x0Fu) << 12) |
         ((uint32_t)(tib & 0x0Fu) << 8) | ((uint32_t)(tic & 0x0Fu) << 4) |
         (uint32_t)(tid & 0x0Fu);
}

/* A consistent three-level configuration with 4K pages:
 * IS 0 + TIA 7 + TIB 7 + TIC 6 + TID 0 + PS 12 = 32. */
#define TC_THREE_LEVEL_4K tc_value(true, false, false, 12, 0, 7, 7, 6, 0)

static void test_each_field_decodes_from_its_own_bits(void) {
  ap_m68030_tc_t tc = ap_m68030_tc_decode(TC_THREE_LEVEL_4K);
  TEST_ASSERT_TRUE(tc.enable);
  TEST_ASSERT_FALSE(tc.supervisor_root);
  TEST_ASSERT_FALSE(tc.function_code_lookup);
  TEST_ASSERT_EQUAL_UINT8(12, tc.page_size_bits);
  TEST_ASSERT_EQUAL_UINT8(0, tc.initial_shift);
  TEST_ASSERT_EQUAL_UINT8(7, tc.table_index[0]); /* TIA */
  TEST_ASSERT_EQUAL_UINT8(7, tc.table_index[1]); /* TIB */
  TEST_ASSERT_EQUAL_UINT8(6, tc.table_index[2]); /* TIC */
  TEST_ASSERT_EQUAL_UINT8(0, tc.table_index[3]); /* TID */
}

/* [030] 9.7.2: "the E bit (bit 31)" is stated in prose, not just the figure. */
static void test_enable_is_bit_31(void) {
  TEST_ASSERT_TRUE(ap_m68030_tc_decode(UINT32_C(0x80000000)).enable);
  TEST_ASSERT_FALSE(ap_m68030_tc_decode(UINT32_C(0x7FFFFFFF)).enable);
}

static void test_supervisor_root_and_function_code_lookup_are_distinct_bits(void) {
  ap_m68030_tc_t sre = ap_m68030_tc_decode(tc_value(false, true, false, 12, 0, 7, 7, 6, 0));
  ap_m68030_tc_t fcl = ap_m68030_tc_decode(tc_value(false, false, true, 12, 0, 7, 7, 6, 0));
  TEST_ASSERT_TRUE(sre.supervisor_root);
  TEST_ASSERT_FALSE(sre.function_code_lookup);
  TEST_ASSERT_FALSE(fcl.supervisor_root);
  TEST_ASSERT_TRUE(fcl.function_code_lookup);
}

/* [030] 9.7.2: "1000 - 256 bytes ... 1111 - 32K bytes", i.e. the field is the
 * page offset width. All eight documented encodings, from the manual's table. */
static void test_every_documented_page_size_decodes(void) {
  static const struct { uint8_t ps; uint32_t bytes; } cases[] = {
      {0x8, 256},  {0x9, 512},   {0xA, 1024},  {0xB, 2048},
      {0xC, 4096}, {0xD, 8192},  {0xE, 16384}, {0xF, 32768},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    ap_m68030_tc_t tc = ap_m68030_tc_decode(tc_value(true, false, false,
                                                     cases[i].ps, 0, 7, 7, 6, 0));
    TEST_ASSERT_EQUAL_UINT32(cases[i].bytes, ap_m68030_tc_page_size(&tc));
  }
}

/* "All other bit combinations are reserved ... an attempt to load other values
 * into this field of the TC register causes an MMU configuration exception." */
static void test_a_reserved_page_size_is_rejected(void) {
  for (uint8_t ps = 0; ps < 8; ps++) {
    ap_m68030_tc_t tc = ap_m68030_tc_decode(tc_value(true, false, false, ps, 0, 7, 7, 6, 0));
    TEST_ASSERT_EQUAL_UINT32(0, ap_m68030_tc_page_size(&tc));
    TEST_ASSERT_FALSE(ap_m68030_tc_is_consistent(&tc, NULL));
  }
}

/* [030] 9.7.2: "The TIx fields are added together until a zero field is
 * reached, and this sum is added to PS and IS. The total must be 32." */
static void test_a_configuration_summing_to_32_is_consistent(void) {
  ap_m68030_tc_t tc = ap_m68030_tc_decode(TC_THREE_LEVEL_4K);
  uint32_t total = 0;
  TEST_ASSERT_TRUE(ap_m68030_tc_is_consistent(&tc, &total));
  TEST_ASSERT_EQUAL_UINT32(32, total);
}

static void test_a_configuration_not_summing_to_32_is_rejected(void) {
  /* 0 + 7 + 7 + 6 + 12 = 32, so bumping TIC by one must break it. */
  ap_m68030_tc_t tc = ap_m68030_tc_decode(tc_value(true, false, false, 12, 0, 7, 7, 7, 0));
  uint32_t total = 0;
  TEST_ASSERT_FALSE(ap_m68030_tc_is_consistent(&tc, &total));
  TEST_ASSERT_EQUAL_UINT32(33, total);
}

/* The subtle half of the rule: the sum stops at the first zero field. A
 * non-zero TIx *after* a zero one must not contribute, or configurations the
 * hardware rejects would be accepted here. */
static void test_table_indices_after_a_zero_field_do_not_count(void) {
  /* IS 0 + TIA 10 + TIB 10 + PS 12 = 32 with TIC zero terminating the sum.
   * TID is deliberately non-zero and must be ignored. */
  ap_m68030_tc_t tc = ap_m68030_tc_decode(tc_value(true, false, false, 12, 0, 10, 10, 0, 9));
  uint32_t total = 0;
  TEST_ASSERT_TRUE(ap_m68030_tc_is_consistent(&tc, &total));
  TEST_ASSERT_EQUAL_UINT32(32, total);
}

/* [030] 9.7.2: IS "contains an integer, 0-15, which sets the effective size of
 * the logical address to 32-17 bits". Ignored bits still have to be accounted
 * for in the sum. */
static void test_the_initial_shift_counts_toward_the_total(void) {
  /* IS 4 + TIA 7 + TIB 6 + TIC 3 + PS 12 = 32. */
  ap_m68030_tc_t tc = ap_m68030_tc_decode(tc_value(true, false, false, 12, 4, 7, 6, 3, 0));
  uint32_t total = 0;
  TEST_ASSERT_TRUE(ap_m68030_tc_is_consistent(&tc, &total));
  TEST_ASSERT_EQUAL_UINT32(32, total);
}

/* A logical address splits into one index per level plus a page offset, taken
 * from the top down after the initial shift. */
static void test_an_address_splits_into_indices_and_a_page_offset(void) {
  ap_m68030_tc_t tc = ap_m68030_tc_decode(TC_THREE_LEVEL_4K);

  /* TIA = bits 31-25 (7), TIB = bits 24-18 (7), TIC = bits 17-12 (6),
   * page offset = bits 11-0 (12). Choose an address with a distinct value in
   * each so a mis-shift cannot coincidentally pass. */
  const uint32_t address =
      (UINT32_C(0x05) << 25) | (UINT32_C(0x13) << 18) | (UINT32_C(0x2A) << 12) |
      UINT32_C(0x123);

  ap_m68030_tc_split_t s = ap_m68030_tc_split(&tc, address);
  TEST_ASSERT_EQUAL_UINT8(3, s.levels);
  TEST_ASSERT_EQUAL_HEX32(0x05, s.index[0]);
  TEST_ASSERT_EQUAL_HEX32(0x13, s.index[1]);
  TEST_ASSERT_EQUAL_HEX32(0x2A, s.index[2]);
  TEST_ASSERT_EQUAL_HEX32(0x123, s.page_offset);
}

/* "When a zero value in a TIx field is encountered during a table search
 * operation, the search is over" -- so the number of levels follows the table
 * indices, not the size of the array. */
static void test_the_search_stops_at_the_first_zero_table_index(void) {
  ap_m68030_tc_t tc = ap_m68030_tc_decode(tc_value(true, false, false, 12, 0, 10, 10, 0, 9));
  ap_m68030_tc_split_t s = ap_m68030_tc_split(&tc, 0xFFFFFFFF);
  TEST_ASSERT_EQUAL_UINT8(2, s.levels);
}

/* The initial shift drops high-order bits before any index is taken, so
 * changing only those bits must not change the split. */
static void test_the_initial_shift_bits_do_not_reach_any_index(void) {
  ap_m68030_tc_t tc = ap_m68030_tc_decode(tc_value(true, false, false, 12, 4, 7, 6, 3, 0));

  const uint32_t base = 0x0ABCDEF0;
  ap_m68030_tc_split_t a = ap_m68030_tc_split(&tc, base);
  ap_m68030_tc_split_t b = ap_m68030_tc_split(&tc, base | UINT32_C(0xF0000000));

  TEST_ASSERT_EQUAL_UINT8(a.levels, b.levels);
  for (unsigned i = 0; i < a.levels; i++) {
    TEST_ASSERT_EQUAL_HEX32(a.index[i], b.index[i]);
  }
  TEST_ASSERT_EQUAL_HEX32(a.page_offset, b.page_offset);
}

/* The page offset width follows PS, so a larger page takes more low bits. */
static void test_a_larger_page_takes_more_of_the_address_as_offset(void) {
  /* IS 0 + TIA 7 + TIB 7 + TIC 3 + PS 15 (32K) = 32. */
  ap_m68030_tc_t tc = ap_m68030_tc_decode(tc_value(true, false, false, 15, 0, 7, 7, 3, 0));
  TEST_ASSERT_TRUE(ap_m68030_tc_is_consistent(&tc, NULL));
  ap_m68030_tc_split_t s = ap_m68030_tc_split(&tc, 0xFFFFFFFF);
  TEST_ASSERT_EQUAL_HEX32(0x7FFF, s.page_offset);
}

/* Splitting with an inconsistent TC must not shift by 32 or more, which would
 * be undefined behaviour -- a caller can hold a TC that never passed the check,
 * because the part itself stores the value and merely clears E. */
static void test_splitting_an_inconsistent_configuration_is_safe(void) {
  ap_m68030_tc_t tc = ap_m68030_tc_decode(tc_value(true, false, false, 12, 0, 15, 15, 15, 15));
  TEST_ASSERT_FALSE(ap_m68030_tc_is_consistent(&tc, NULL));
  ap_m68030_tc_split_t s = ap_m68030_tc_split(&tc, 0xFFFFFFFF);
  TEST_ASSERT_LESS_OR_EQUAL_UINT8(AP_M68030_TC_LEVELS, s.levels);
}

/* [030] 9.7.2: "A reset operation clears this bit" (E), and "When translation
 * is disabled, logical addresses are used as physical addresses." A zeroed TC
 * must therefore read as disabled rather than as some accidental configuration.
 */
static void test_a_zeroed_register_is_disabled(void) {
  ap_m68030_tc_t tc = ap_m68030_tc_decode(0);
  TEST_ASSERT_FALSE(tc.enable);
  TEST_ASSERT_FALSE(ap_m68030_tc_is_consistent(&tc, NULL));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_each_field_decodes_from_its_own_bits);
  RUN_TEST(test_enable_is_bit_31);
  RUN_TEST(test_supervisor_root_and_function_code_lookup_are_distinct_bits);
  RUN_TEST(test_every_documented_page_size_decodes);
  RUN_TEST(test_a_reserved_page_size_is_rejected);
  RUN_TEST(test_a_configuration_summing_to_32_is_consistent);
  RUN_TEST(test_a_configuration_not_summing_to_32_is_rejected);
  RUN_TEST(test_table_indices_after_a_zero_field_do_not_count);
  RUN_TEST(test_the_initial_shift_counts_toward_the_total);
  RUN_TEST(test_an_address_splits_into_indices_and_a_page_offset);
  RUN_TEST(test_the_search_stops_at_the_first_zero_table_index);
  RUN_TEST(test_the_initial_shift_bits_do_not_reach_any_index);
  RUN_TEST(test_a_larger_page_takes_more_of_the_address_as_offset);
  RUN_TEST(test_splitting_an_inconsistent_configuration_is_safe);
  RUN_TEST(test_a_zeroed_register_is_disabled);
  return UNITY_END();
}
