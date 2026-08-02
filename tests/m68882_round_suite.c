/* MC68882 rounding, Figure 6-3.
 *
 * The algorithm is published as pseudocode, so these tests assert its *branches*
 * rather than a handful of results: the exact case, each of the four modes, the
 * tie that separates round-to-nearest from everything else, and the carry the
 * increment can produce.
 */

#include "cpu/m68882/ap_m68882_format.h"
#include "cpu/m68882/ap_m68882_round.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define ONE_POINT_ZERO UINT64_C(0x8000000000000000)

static ap_m68882_extended_t value(bool sign, uint64_t mantissa) {
  return (ap_m68882_extended_t){
      .sign = sign, .exponent = AP_M68882_BIAS_EXTENDED, .mantissa = mantissa};
}

/* "IF GUARD, ROUND AND STICKY = 0 THEN (RESULT IS EXACT) DON'T SET INEX2,
 * DON'T CHANGE THE INTERMEDIATE RESULT." All four modes, because an exact
 * result must be left alone by every one of them -- a mode that incremented
 * unconditionally would pass a test using only round-to-nearest. */
static void test_an_exact_result_is_untouched_in_every_mode(void) {
  const ap_m68882_rounding_t modes[] = {
      AP_M68882_ROUND_NEAREST, AP_M68882_ROUND_ZERO,
      AP_M68882_ROUND_MINUS_INFINITY, AP_M68882_ROUND_PLUS_INFINITY};

  for (unsigned i = 0; i < 4u; i++) {
    for (unsigned negative = 0; negative < 2u; negative++) {
      const ap_m68882_extended_t in = value(negative != 0u, ONE_POINT_ZERO | 1u);
      const ap_m68882_round_result_t out = ap_m68882_round(
          in, false, false, false, modes[i], AP_M68882_PRECISION_EXTENDED);
      TEST_ASSERT_FALSE(out.inexact);
      TEST_ASSERT_EQUAL_HEX64(in.mantissa, out.value.mantissa);
      TEST_ASSERT_EQUAL_HEX16(in.exponent, out.value.exponent);
    }
  }
}

/* **Round to nearest is round half to *even*.** "IF GUARD=1 AND ROUND AND
 * STICKY=0 (TIE CASE) THEN IF LSB=1 ADD 1 TO LSB". An exact tie goes up only
 * when doing so clears the last bit -- a model rounding every tie up is wrong
 * half the time on ties and biases a long summation, which is the whole reason
 * the standard specifies this case. */
static void test_a_tie_rounds_to_even(void) {
  /* LSB clear: the tie stays put. */
  const ap_m68882_round_result_t down = ap_m68882_round(
      value(false, ONE_POINT_ZERO), true, false, false,
      AP_M68882_ROUND_NEAREST, AP_M68882_PRECISION_EXTENDED);
  TEST_ASSERT_TRUE(down.inexact);
  TEST_ASSERT_EQUAL_HEX64(ONE_POINT_ZERO, down.value.mantissa);

  /* LSB set: the tie goes up, clearing it. */
  const ap_m68882_round_result_t up = ap_m68882_round(
      value(false, ONE_POINT_ZERO | 1u), true, false, false,
      AP_M68882_ROUND_NEAREST, AP_M68882_PRECISION_EXTENDED);
  TEST_ASSERT_TRUE(up.inexact);
  TEST_ASSERT_EQUAL_HEX64(ONE_POINT_ZERO + 2u, up.value.mantissa);
}

/* Above a tie -- guard set with round or sticky also set -- rounds up whatever
 * the LSB is. That is the "ELSE IF GUARD = 1" arm, and confusing it with the
 * tie arm makes every value just above halfway round to even instead of up. */
static void test_above_a_tie_rounds_up_regardless_of_the_lsb(void) {
  const ap_m68882_round_result_t out = ap_m68882_round(
      value(false, ONE_POINT_ZERO), true, false, true, AP_M68882_ROUND_NEAREST,
      AP_M68882_PRECISION_EXTENDED);
  TEST_ASSERT_TRUE(out.inexact);
  TEST_ASSERT_EQUAL_HEX64(ONE_POINT_ZERO + 1u, out.value.mantissa);
}

/* Below a tie -- guard clear -- never rounds up, however much is below it. */
static void test_below_a_tie_never_rounds_up(void) {
  const ap_m68882_round_result_t out = ap_m68882_round(
      value(false, ONE_POINT_ZERO | 1u), false, true, true,
      AP_M68882_ROUND_NEAREST, AP_M68882_PRECISION_EXTENDED);
  TEST_ASSERT_TRUE(out.inexact); /* still inexact... */
  TEST_ASSERT_EQUAL_HEX64(ONE_POINT_ZERO | 1u, out.value.mantissa); /* ...but
                                                                       unchanged */
}

/* **The two directed modes depend on the sign, not the magnitude.** "RM: IF
 * INTERMEDIATE RESULT IS NEGATIVE THEN ADD 1 TO LSB" and "RP: IF ... POSITIVE".
 * Rounding toward minus infinity makes a negative number *larger* in magnitude,
 * which reads backwards until you remember the sign is separate here. */
static void test_the_directed_modes_follow_the_sign(void) {
  /* Toward minus infinity: negative rounds away from zero, positive chops. */
  const ap_m68882_round_result_t negative_rm = ap_m68882_round(
      value(true, ONE_POINT_ZERO), true, true, true,
      AP_M68882_ROUND_MINUS_INFINITY, AP_M68882_PRECISION_EXTENDED);
  TEST_ASSERT_EQUAL_HEX64(ONE_POINT_ZERO + 1u, negative_rm.value.mantissa);

  const ap_m68882_round_result_t positive_rm = ap_m68882_round(
      value(false, ONE_POINT_ZERO), true, true, true,
      AP_M68882_ROUND_MINUS_INFINITY, AP_M68882_PRECISION_EXTENDED);
  TEST_ASSERT_EQUAL_HEX64(ONE_POINT_ZERO, positive_rm.value.mantissa);

  /* Toward plus infinity: the mirror image. */
  const ap_m68882_round_result_t positive_rp = ap_m68882_round(
      value(false, ONE_POINT_ZERO), true, true, true,
      AP_M68882_ROUND_PLUS_INFINITY, AP_M68882_PRECISION_EXTENDED);
  TEST_ASSERT_EQUAL_HEX64(ONE_POINT_ZERO + 1u, positive_rp.value.mantissa);

  const ap_m68882_round_result_t negative_rp = ap_m68882_round(
      value(true, ONE_POINT_ZERO), true, true, true,
      AP_M68882_ROUND_PLUS_INFINITY, AP_M68882_PRECISION_EXTENDED);
  TEST_ASSERT_EQUAL_HEX64(ONE_POINT_ZERO, negative_rp.value.mantissa);
}

/* Toward zero chops, and still reports inexact: "RZ: (FALL THROUGH; GUARD,
 * ROUND AND STICKY ARE CHOPPED)". The discarded bits are gone and the fact that
 * there were any is not -- INEX2 is what tells a program the answer was
 * truncated. */
static void test_toward_zero_chops_but_still_reports_inexact(void) {
  const ap_m68882_round_result_t out = ap_m68882_round(
      value(false, ONE_POINT_ZERO), true, true, true, AP_M68882_ROUND_ZERO,
      AP_M68882_PRECISION_EXTENDED);
  TEST_ASSERT_TRUE(out.inexact);
  TEST_ASSERT_EQUAL_HEX64(ONE_POINT_ZERO, out.value.mantissa);
}

/* "IF OVERFLOW = 1 THEN SHIFT MANTISSA RIGHT BY ONE BIT, ADD 1 TO THE
 * EXPONENT." Rounding an all-ones mantissa up carries out of the top, and the
 * result is 1.0 with the exponent one higher -- not zero, which is what an
 * unhandled wrap produces and which is a catastrophically different number. */
static void test_a_carry_out_of_the_mantissa_increments_the_exponent(void) {
  const ap_m68882_extended_t in = value(false, UINT64_C(0xFFFFFFFFFFFFFFFF));
  const ap_m68882_round_result_t out =
      ap_m68882_round(in, true, true, true, AP_M68882_ROUND_NEAREST,
                      AP_M68882_PRECISION_EXTENDED);
  TEST_ASSERT_TRUE(out.inexact);
  TEST_ASSERT_EQUAL_HEX64(ONE_POINT_ZERO, out.value.mantissa);
  TEST_ASSERT_EQUAL_HEX16((uint16_t)(in.exponent + 1u), out.value.exponent);
}

/* The rounding boundary moves with the precision: 24 bits for single, 53 for
 * double, 64 for extended. */
static void test_the_precision_boundaries(void) {
  TEST_ASSERT_EQUAL_UINT(
      64u, ap_m68882_precision_bits(AP_M68882_PRECISION_EXTENDED));
  TEST_ASSERT_EQUAL_UINT(24u,
                         ap_m68882_precision_bits(AP_M68882_PRECISION_SINGLE));
  TEST_ASSERT_EQUAL_UINT(53u,
                         ap_m68882_precision_bits(AP_M68882_PRECISION_DOUBLE));
}

/* At single precision the mantissa is cut at 24 bits and the discarded bits
 * **fold into** the rounding decision rather than being dropped -- so a value
 * whose extended form is exact can still be inexact at single, and the bits
 * below the boundary decide which way it goes. */
static void test_single_precision_rounds_at_its_own_boundary(void) {
  /* A one just below the 24-bit boundary: exact as extended, a tie at single. */
  const uint64_t just_below = ONE_POINT_ZERO | (UINT64_C(1) << 39);
  const ap_m68882_round_result_t out = ap_m68882_round(
      value(false, just_below), false, false, false, AP_M68882_ROUND_NEAREST,
      AP_M68882_PRECISION_SINGLE);

  TEST_ASSERT_TRUE(out.inexact);
  /* A tie with the LSB clear stays put, and everything below the boundary is
   * cleared. */
  TEST_ASSERT_EQUAL_HEX64(ONE_POINT_ZERO, out.value.mantissa);

  /* The same value at extended precision is exact and untouched, which is what
   * makes this a property of the boundary rather than of the number. */
  const ap_m68882_round_result_t extended = ap_m68882_round(
      value(false, just_below), false, false, false, AP_M68882_ROUND_NEAREST,
      AP_M68882_PRECISION_EXTENDED);
  TEST_ASSERT_FALSE(extended.inexact);
  TEST_ASSERT_EQUAL_HEX64(just_below, extended.value.mantissa);
}

/* Rounding is applied **once**, from the full intermediate. Rounding to
 * extended and then to single is a different answer for values near a tie --
 * the classic double-rounding error -- so the bits below the single boundary
 * must reach the single decision directly. */
static void test_rounding_happens_once_not_twice(void) {
  /* Just above a single-precision tie: guard set at bit 39, and something below
   * it, so single must round *up* rather than to even. */
  const uint64_t above_tie =
      ONE_POINT_ZERO | (UINT64_C(1) << 39) | (UINT64_C(1) << 20);
  const ap_m68882_round_result_t once = ap_m68882_round(
      value(false, above_tie), false, false, false, AP_M68882_ROUND_NEAREST,
      AP_M68882_PRECISION_SINGLE);
  TEST_ASSERT_EQUAL_HEX64(ONE_POINT_ZERO + (UINT64_C(1) << 40),
                          once.value.mantissa);

  /* Double-rounding would first round the low bits away at extended -- giving
   * an exact tie -- and then round that tie to even, leaving the mantissa
   * unchanged. The two answers differ, which is the whole point. */
  TEST_ASSERT_NOT_EQUAL_UINT64(ONE_POINT_ZERO, once.value.mantissa);
}

/* The caller's guard, round and sticky are not lost when the precision cuts
 * further up: they are "to the right of the round bit" and so fold into sticky.
 * Dropping them would make a value that is inexact at extended appear exact at
 * single whenever the boundary bits happen to be zero. */
static void test_the_callers_low_bits_fold_into_sticky(void) {
  const ap_m68882_round_result_t out = ap_m68882_round(
      value(false, ONE_POINT_ZERO), false, false, true,
      AP_M68882_ROUND_PLUS_INFINITY, AP_M68882_PRECISION_SINGLE);
  TEST_ASSERT_TRUE(out.inexact);
  /* Toward plus infinity with anything below: up by one single-precision unit. */
  TEST_ASSERT_EQUAL_HEX64(ONE_POINT_ZERO + (UINT64_C(1) << 40),
                          out.value.mantissa);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_an_exact_result_is_untouched_in_every_mode);
  RUN_TEST(test_a_tie_rounds_to_even);
  RUN_TEST(test_above_a_tie_rounds_up_regardless_of_the_lsb);
  RUN_TEST(test_below_a_tie_never_rounds_up);
  RUN_TEST(test_the_directed_modes_follow_the_sign);
  RUN_TEST(test_toward_zero_chops_but_still_reports_inexact);
  RUN_TEST(test_a_carry_out_of_the_mantissa_increments_the_exponent);
  RUN_TEST(test_the_precision_boundaries);
  RUN_TEST(test_single_precision_rounds_at_its_own_boundary);
  RUN_TEST(test_rounding_happens_once_not_twice);
  RUN_TEST(test_the_callers_low_bits_fold_into_sticky);
  return UNITY_END();
}
