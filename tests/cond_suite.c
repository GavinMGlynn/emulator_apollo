/* MC68030 conditional tests.
 *
 * Cited to the M68000 Family Programmer's Reference Manual 1992 Table 3-19.
 *
 * Table 3-19's overbars do not survive the scan -- HI reads as "C^ Z" where the
 * manual means not-C and not-Z -- so the transcription is *verified* here
 * rather than trusted. The encoding lays the conditions out in complementary
 * pairs, and a misplaced bar breaks that for some CCR value, so exhausting the
 * whole space settles it.
 */

#include "cpu/m68030/ap_m68030_cond.h"
#include "cpu/m68030/ap_m68030_regs.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define X_ (1u << AP_M68030_SR_X_BIT)
#define N_ (1u << AP_M68030_SR_N_BIT)
#define Z_ (1u << AP_M68030_SR_Z_BIT)
#define V_ (1u << AP_M68030_SR_V_BIT)
#define C_ (1u << AP_M68030_SR_C_BIT)

/* The whole CCR space is 32 states: X N Z V C. X takes no part in any
 * condition, and that it does not is itself worth asserting. */
#define CCR_STATES 32u

/* The headline check, and the one that recovers the lost overbars. Table 3-19
 * pairs every condition with its complement at the adjacent encoding -- T/F,
 * HI/LS, CC/CS, NE/EQ, VC/VS, PL/MI, GE/LT, GT/LE. If a bar were misplaced,
 * some CCR value would make a pair agree instead of disagree. */
static void test_every_condition_pair_is_complementary_over_all_ccr_states(void) {
  for (unsigned pair = 0; pair < 8; pair++) {
    for (uint16_t ccr = 0; ccr < CCR_STATES; ccr++) {
      const bool even =
          ap_m68030_condition((ap_m68030_cond_t)(pair * 2u), ccr);
      const bool odd =
          ap_m68030_condition((ap_m68030_cond_t)(pair * 2u + 1u), ccr);
      TEST_ASSERT_NOT_EQUAL_INT(even, odd);
    }
  }
}

/* X is the extend bit and no conditional test uses it, so flipping it must
 * change nothing anywhere in the table. */
static void test_the_extend_bit_affects_no_condition(void) {
  for (unsigned condition = 0; condition < 16; condition++) {
    for (uint16_t ccr = 0; ccr < CCR_STATES; ccr++) {
      TEST_ASSERT_EQUAL_INT(
          ap_m68030_condition((ap_m68030_cond_t)condition, ccr),
          ap_m68030_condition((ap_m68030_cond_t)condition,
                              (uint16_t)(ccr ^ X_)));
    }
  }
}

/* The single-flag conditions, straight from the table. */
static void test_the_single_flag_conditions_read_their_own_bit(void) {
  TEST_ASSERT_TRUE(ap_m68030_condition(AP_M68030_COND_CS, C_));
  TEST_ASSERT_FALSE(ap_m68030_condition(AP_M68030_COND_CS, 0));
  TEST_ASSERT_TRUE(ap_m68030_condition(AP_M68030_COND_EQ, Z_));
  TEST_ASSERT_FALSE(ap_m68030_condition(AP_M68030_COND_EQ, 0));
  TEST_ASSERT_TRUE(ap_m68030_condition(AP_M68030_COND_VS, V_));
  TEST_ASSERT_TRUE(ap_m68030_condition(AP_M68030_COND_MI, N_));
  TEST_ASSERT_TRUE(ap_m68030_condition(AP_M68030_COND_PL, 0));
}

/* T and F are unconditional whatever the flags say. */
static void test_true_and_false_ignore_the_flags(void) {
  for (uint16_t ccr = 0; ccr < CCR_STATES; ccr++) {
    TEST_ASSERT_TRUE(ap_m68030_condition(AP_M68030_COND_T, ccr));
    TEST_ASSERT_FALSE(ap_m68030_condition(AP_M68030_COND_F, ccr));
  }
}

/* HI is "not C and not Z" -- the entry whose bars were lost, so each of its
 * four input combinations is pinned individually. */
static void test_high_requires_both_carry_and_zero_clear(void) {
  TEST_ASSERT_TRUE(ap_m68030_condition(AP_M68030_COND_HI, 0));
  TEST_ASSERT_FALSE(ap_m68030_condition(AP_M68030_COND_HI, C_));
  TEST_ASSERT_FALSE(ap_m68030_condition(AP_M68030_COND_HI, Z_));
  TEST_ASSERT_FALSE(ap_m68030_condition(AP_M68030_COND_HI, C_ | Z_));
}

/* The signed comparisons, which are the ones a sign error hides in. GE is N
 * equal to V; LT is N differing from V. */
static void test_the_signed_comparisons_compare_negative_against_overflow(void) {
  TEST_ASSERT_TRUE(ap_m68030_condition(AP_M68030_COND_GE, 0));
  TEST_ASSERT_TRUE(ap_m68030_condition(AP_M68030_COND_GE, N_ | V_));
  TEST_ASSERT_FALSE(ap_m68030_condition(AP_M68030_COND_GE, N_));
  TEST_ASSERT_FALSE(ap_m68030_condition(AP_M68030_COND_GE, V_));

  TEST_ASSERT_TRUE(ap_m68030_condition(AP_M68030_COND_LT, N_));
  TEST_ASSERT_TRUE(ap_m68030_condition(AP_M68030_COND_LT, V_));
  TEST_ASSERT_FALSE(ap_m68030_condition(AP_M68030_COND_LT, N_ | V_));
}

/* GT is GE with Z clear as well, so equality alone defeats it -- the boundary
 * between GT and GE, and the one most easily got wrong. */
static void test_greater_than_additionally_requires_zero_clear(void) {
  TEST_ASSERT_TRUE(ap_m68030_condition(AP_M68030_COND_GT, 0));
  TEST_ASSERT_FALSE(ap_m68030_condition(AP_M68030_COND_GT, Z_));
  /* GE is still true for the same state: that is the whole difference. */
  TEST_ASSERT_TRUE(ap_m68030_condition(AP_M68030_COND_GE, Z_));

  TEST_ASSERT_TRUE(ap_m68030_condition(AP_M68030_COND_LE, Z_));
  TEST_ASSERT_TRUE(ap_m68030_condition(AP_M68030_COND_LE, N_));
}

/* LS is the complement of HI and is reached by either flag, which is what
 * distinguishes it from CS. */
static void test_low_or_same_is_reached_by_either_flag(void) {
  TEST_ASSERT_FALSE(ap_m68030_condition(AP_M68030_COND_LS, 0));
  TEST_ASSERT_TRUE(ap_m68030_condition(AP_M68030_COND_LS, C_));
  TEST_ASSERT_TRUE(ap_m68030_condition(AP_M68030_COND_LS, Z_));
}

/* "*Not available for the Bcc instruction": encodings 0 and 1 are BRA and BSR
 * there, so a Bcc decoder must not reach them as conditions. */
static void test_true_and_false_are_not_available_to_bcc(void) {
  TEST_ASSERT_FALSE(ap_m68030_condition_available_to_bcc(AP_M68030_COND_T));
  TEST_ASSERT_FALSE(ap_m68030_condition_available_to_bcc(AP_M68030_COND_F));
  for (unsigned condition = 2; condition < 16; condition++) {
    TEST_ASSERT_TRUE(
        ap_m68030_condition_available_to_bcc((ap_m68030_cond_t)condition));
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_every_condition_pair_is_complementary_over_all_ccr_states);
  RUN_TEST(test_the_extend_bit_affects_no_condition);
  RUN_TEST(test_the_single_flag_conditions_read_their_own_bit);
  RUN_TEST(test_true_and_false_ignore_the_flags);
  RUN_TEST(test_high_requires_both_carry_and_zero_clear);
  RUN_TEST(test_the_signed_comparisons_compare_negative_against_overflow);
  RUN_TEST(test_greater_than_additionally_requires_zero_clear);
  RUN_TEST(test_low_or_same_is_reached_by_either_flag);
  RUN_TEST(test_true_and_false_are_not_available_to_bcc);
  return UNITY_END();
}
