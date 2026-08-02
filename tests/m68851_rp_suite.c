/* MC68851 root pointer registers, `[68851]` §6.1.1 and Figure 6-1.
 *
 * Bit boundaries from the page image. The register is 64 bits with two small
 * fields buried in a sea of mandatory zeros, which is exactly the shape that
 * hides an off-by-one -- so every field is placed by its own test.
 */

#include "cpu/m68851/ap_m68851_rp.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_fields_sit_where_figure_6_1_draws_them(void) {
  /* L/U@63, LIMIT@62-48, SG@41, DT@33-32, table address@31-4, software@3-0. */
  const ap_m68851_rp_t rp =
      ap_m68851_rp_decode(UINT64_C(0xABCD020312345678));
  TEST_ASSERT_TRUE(rp.lower_limit);              /* 0xA has bit 63 set */
  TEST_ASSERT_EQUAL_UINT(0x2BCDu, rp.limit);     /* 15 bits below it */
  TEST_ASSERT_TRUE(rp.shared_globally);          /* 0x0203: bit 41 */
  TEST_ASSERT_EQUAL_INT(AP_M68851_DT_VALID_8_BYTE, rp.descriptor_type);
  TEST_ASSERT_EQUAL_HEX32(0x12345670u, rp.table_address);
  TEST_ASSERT_EQUAL_UINT(8u, rp.software_bits);
}

static void test_the_limit_and_its_direction_bit_share_the_top_word(void) {
  /* The manual quotes both switch-off values as whole 16-bit patterns -- $7FFF
   * and $8000 -- which only reads correctly if L/U is the top bit of the same
   * word as the limit. */
  const ap_m68851_rp_t upper =
      ap_m68851_rp_decode(UINT64_C(0x7FFF000000000000));
  TEST_ASSERT_FALSE(upper.lower_limit);
  TEST_ASSERT_EQUAL_UINT(0x7FFFu, upper.limit);

  const ap_m68851_rp_t lower =
      ap_m68851_rp_decode(UINT64_C(0x8000000000000000));
  TEST_ASSERT_TRUE(lower.lower_limit);
  TEST_ASSERT_EQUAL_UINT(0u, lower.limit);
}

static void test_the_mandatory_zero_bits_reach_no_field(void) {
  /* "All other unused bits of the root pointer registers must be zero": bits
   * 47-42 and 40-34 sit either side of SG and must not leak into it or DT. */
  const ap_m68851_rp_t rp =
      ap_m68851_rp_decode(UINT64_C(0x0000FDFC00000000));
  TEST_ASSERT_FALSE(rp.shared_globally);
  TEST_ASSERT_EQUAL_INT(AP_M68851_DT_INVALID, rp.descriptor_type);
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0), ap_m68851_rp_encode(&rp));
}

static void test_encoding_round_trips_every_implemented_bit(void) {
  /* Each implemented bit alone, back to the same position. The unused bits are
   * skipped rather than asserted zero here -- that is the test above. */
  const uint64_t implemented =
      (UINT64_C(0x7FFF) << 48) | (UINT64_C(1) << 63) | (UINT64_C(1) << 41) |
      (UINT64_C(3) << 32) | UINT64_C(0xFFFFFFFF);
  for (unsigned bit = 0; bit < 64u; bit++) {
    const uint64_t value = UINT64_C(1) << bit;
    if ((value & implemented) == 0u) {
      continue;
    }
    const ap_m68851_rp_t rp = ap_m68851_rp_decode(value);
    TEST_ASSERT_EQUAL_HEX64(value, ap_m68851_rp_encode(&rp));
  }
}

static void test_the_table_address_is_16_byte_aligned(void) {
  /* The field is bits 31-4, so the low four bits are not part of it -- every
   * translation table starts on a 16-byte boundary and the bits below belong
   * to software. */
  const ap_m68851_rp_t rp = ap_m68851_rp_decode(UINT64_C(0x00000000FFFFFFFF));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFF0u, rp.table_address);
  TEST_ASSERT_EQUAL_UINT(0xFu, rp.software_bits);
}

static void test_the_software_bits_survive_a_round_trip(void) {
  /* "Bits [3-0] of the root pointer are not used by the MC68851 and may be used
   * by the operating system for other purposes." So they are storage, and a
   * model that masked them off would lose data the OS expects to read back. */
  for (unsigned bits = 0; bits < 16u; bits++) {
    const uint64_t value = UINT64_C(0x0000000200001000) | bits;
    const ap_m68851_rp_t rp = ap_m68851_rp_decode(value);
    TEST_ASSERT_EQUAL_UINT(bits, rp.software_bits);
    TEST_ASSERT_EQUAL_HEX64(value, ap_m68851_rp_encode(&rp));
  }
}

static void test_the_descriptor_type_scales_the_table_index(void) {
  /* "$2 VALID 4-BYTE ... must scale the table index for this level of the table
   * search by four bytes"; "$3 VALID 8-BYTE ... by eight bytes." The other two
   * name no table at all. */
  ap_m68851_rp_t rp = {0};
  rp.descriptor_type = AP_M68851_DT_VALID_4_BYTE;
  TEST_ASSERT_EQUAL_UINT(4u, ap_m68851_rp_descriptor_bytes(&rp));
  rp.descriptor_type = AP_M68851_DT_VALID_8_BYTE;
  TEST_ASSERT_EQUAL_UINT(8u, ap_m68851_rp_descriptor_bytes(&rp));
  rp.descriptor_type = AP_M68851_DT_INVALID;
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68851_rp_descriptor_bytes(&rp));
  rp.descriptor_type = AP_M68851_DT_PAGE_DESCRIPTOR;
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68851_rp_descriptor_bytes(&rp));
}

static void test_an_upper_limit_bounds_indices_from_above_inclusively(void) {
  /* "All table indices must be less than or equal to the value contained in the
   * limit field." Inclusive, so the limit itself passes. */
  const ap_m68851_rp_t rp = {.lower_limit = false, .limit = 100u};
  TEST_ASSERT_TRUE(ap_m68851_rp_index_within_limit(&rp, 0u));
  TEST_ASSERT_TRUE(ap_m68851_rp_index_within_limit(&rp, 100u));
  TEST_ASSERT_FALSE(ap_m68851_rp_index_within_limit(&rp, 101u));
}

static void test_a_lower_limit_bounds_indices_from_below_inclusively(void) {
  /* "All table indices must be greater than or equal to the value contained in
   * the limit field. Otherwise, a limit violation will occur." One bit reverses
   * the whole comparison. */
  const ap_m68851_rp_t rp = {.lower_limit = true, .limit = 100u};
  TEST_ASSERT_FALSE(ap_m68851_rp_index_within_limit(&rp, 99u));
  TEST_ASSERT_TRUE(ap_m68851_rp_index_within_limit(&rp, 100u));
  TEST_ASSERT_TRUE(ap_m68851_rp_index_within_limit(&rp, 0x7FFFu));
}

static void test_both_documented_ways_to_suppress_the_limit(void) {
  /* "The limit function can be effectively suppressed by either setting L/U to
   * zero and setting the limit field to all ones ($7FFF) or by setting L/U to
   * one and clearing the limit field ($8000)." Two encodings, and each really
   * does admit every index a fifteen-bit field can hold. */
  const ap_m68851_rp_t upper = {.lower_limit = false, .limit = 0x7FFFu};
  const ap_m68851_rp_t lower = {.lower_limit = true, .limit = 0u};
  TEST_ASSERT_TRUE(ap_m68851_rp_limit_suppressed(&upper));
  TEST_ASSERT_TRUE(ap_m68851_rp_limit_suppressed(&lower));

  for (unsigned index = 0; index <= 0x7FFFu; index += 0x111u) {
    TEST_ASSERT_TRUE(ap_m68851_rp_index_within_limit(&upper, index));
    TEST_ASSERT_TRUE(ap_m68851_rp_index_within_limit(&lower, index));
  }

  /* And the near misses are not suppressed: the two values are not
   * interchangeable between the directions. */
  const ap_m68851_rp_t upper_zero = {.lower_limit = false, .limit = 0u};
  const ap_m68851_rp_t lower_max = {.lower_limit = true, .limit = 0x7FFFu};
  TEST_ASSERT_FALSE(ap_m68851_rp_limit_suppressed(&upper_zero));
  TEST_ASSERT_FALSE(ap_m68851_rp_limit_suppressed(&lower_max));
}

static void test_pmove_refuses_an_invalid_descriptor_type(void) {
  /* "The MC68851 does not allow the operating system to load a root pointer
   * with an 'invalid' descriptor type with the PMOVE instruction." */
  ap_m68851_rp_t rp = {0};
  rp.descriptor_type = AP_M68851_DT_INVALID;
  TEST_ASSERT_FALSE(ap_m68851_rp_loadable_by_pmove(&rp));
  for (unsigned dt = 1; dt < 4u; dt++) {
    rp.descriptor_type = (ap_m68851_descriptor_type_t)dt;
    TEST_ASSERT_TRUE(ap_m68851_rp_loadable_by_pmove(&rp));
  }
}

static void test_function_code_lookup_suppresses_the_limit_check(void) {
  /* "If function code lookup is enabled, the limit field and the L/U bit of a
   * root pointer are ignored." */
  ap_m68851_rp_t rp = {.lower_limit = false, .limit = 4u,
                       .descriptor_type = AP_M68851_DT_VALID_4_BYTE};
  TEST_ASSERT_TRUE(ap_m68851_rp_limit_applies(&rp, false));
  TEST_ASSERT_FALSE(ap_m68851_rp_limit_applies(&rp, true));
}

static void test_a_page_descriptor_is_limit_checked_whatever_fcl_says(void) {
  /* "If the DT field of a root pointer is set to $1, the MC68851 performs a
   * limit check regardless of the state of the FCL bit." The exception matters:
   * a page descriptor walks no table, so the limit is the only thing bounding
   * the direct mapping it creates. */
  ap_m68851_rp_t rp = {.lower_limit = false, .limit = 4u,
                       .descriptor_type = AP_M68851_DT_PAGE_DESCRIPTOR};
  TEST_ASSERT_TRUE(ap_m68851_rp_limit_applies(&rp, false));
  TEST_ASSERT_TRUE(ap_m68851_rp_limit_applies(&rp, true));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_fields_sit_where_figure_6_1_draws_them);
  RUN_TEST(test_the_limit_and_its_direction_bit_share_the_top_word);
  RUN_TEST(test_the_mandatory_zero_bits_reach_no_field);
  RUN_TEST(test_encoding_round_trips_every_implemented_bit);
  RUN_TEST(test_the_table_address_is_16_byte_aligned);
  RUN_TEST(test_the_software_bits_survive_a_round_trip);
  RUN_TEST(test_the_descriptor_type_scales_the_table_index);
  RUN_TEST(test_an_upper_limit_bounds_indices_from_above_inclusively);
  RUN_TEST(test_a_lower_limit_bounds_indices_from_below_inclusively);
  RUN_TEST(test_both_documented_ways_to_suppress_the_limit);
  RUN_TEST(test_pmove_refuses_an_invalid_descriptor_type);
  RUN_TEST(test_function_code_lookup_suppresses_the_limit_check);
  RUN_TEST(test_a_page_descriptor_is_limit_checked_whatever_fcl_says);
  return UNITY_END();
}
