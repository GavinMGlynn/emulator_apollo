/* MC68882 arithmetic: add, subtract, multiply, divide and compare.
 *
 * The ordinary path is checked by construction -- values whose exact result is
 * known -- and the special cases are checked against Table 6-2, which is where
 * a floating-point unit is right or wrong.
 */

#include "cpu/m68882/ap_m68882_arith.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define RN AP_M68882_ROUND_NEAREST
#define PX AP_M68882_PRECISION_EXTENDED

/* Values built from their single-precision encodings, which keeps the test's
 * inputs readable and exercises the conversion the FPU itself uses. */
static ap_m68882_extended_t single(uint32_t bits) {
  return ap_m68882_from_single(bits);
}

static bool raised(const ap_m68882_op_t *op, unsigned bit) {
  return (op->exceptions & (UINT32_C(1) << bit)) != 0u;
}

/* 1 + 1 = 2, which is the smallest thing that can be wrong: the mantissas
 * carry, the exponent goes up by one, and nothing is inexact. */
static void test_one_plus_one_is_two(void) {
  const ap_m68882_extended_t one = single(0x3F800000u);
  const ap_m68882_op_t sum = ap_m68882_add(&one, &one, RN, PX);

  TEST_ASSERT_EQUAL_HEX32(0x40000000u, ap_m68882_to_single(&sum.value));
  TEST_ASSERT_EQUAL_UINT32(0u, sum.exceptions);
}

/* Subtraction borrows across the alignment. `1 - 2^-64` needs 65 bits, so the
 * borrow has to come out of the guard bits -- a model that dropped it would
 * return exactly 1 and call the result **exact**.
 *
 * Round-to-nearest brings the value back to 1.0, which is correct and is also
 * why the flag is what this checks rather than the value: the answer is right
 * either way and only `INEX2` says whether the borrow happened. Round-toward-
 * zero then shows it directly, keeping the result below one. */
static void test_a_subtraction_borrows_from_the_guard_bits(void) {
  const ap_m68882_extended_t one = single(0x3F800000u);
  ap_m68882_extended_t tiny = one;
  tiny.exponent = (uint16_t)(one.exponent - 64u);

  const ap_m68882_op_t nearest = ap_m68882_sub(&one, &tiny, RN, PX);
  TEST_ASSERT_TRUE(raised(&nearest, AP_M68882_EXC_INEX2));
  /* Rounded back to exactly one, which is the nearest representable value. */
  TEST_ASSERT_EQUAL_HEX16(one.exponent, nearest.value.exponent);
  TEST_ASSERT_EQUAL_HEX64(one.mantissa, nearest.value.mantissa);

  /* Toward zero the borrow stays visible: one exponent lower with an all-ones
   * mantissa, which is the largest value below one. */
  const ap_m68882_op_t chopped =
      ap_m68882_sub(&one, &tiny, AP_M68882_ROUND_ZERO, PX);
  TEST_ASSERT_TRUE(raised(&chopped, AP_M68882_EXC_INEX2));
  TEST_ASSERT_EQUAL_HEX16((uint16_t)(one.exponent - 1u),
                          chopped.value.exponent);
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0xFFFFFFFFFFFFFFFF),
                          chopped.value.mantissa);
}

/* x - x is zero, and the IEEE rule gives it a **positive** sign in every mode
 * but round-toward-minus-infinity. A model returning the operand's sign gets it
 * backwards for negative operands. */
static void test_x_minus_x_is_positive_zero(void) {
  const ap_m68882_extended_t negative = single(0xC0A00000u); /* -5 */
  const ap_m68882_op_t difference =
      ap_m68882_sub(&negative, &negative, RN, PX);

  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_ZERO,
                        ap_m68882_classify(&difference.value));
  TEST_ASSERT_FALSE(difference.value.sign);

  /* Toward minus infinity it is negative, which is the one exception. */
  const ap_m68882_op_t downward = ap_m68882_sub(
      &negative, &negative, AP_M68882_ROUND_MINUS_INFINITY, PX);
  TEST_ASSERT_TRUE(downward.value.sign);
}

/* **Table 6-2's FADD row**: "(+infinity) + (-infinity)" is an operand error,
 * and two infinities of the *same* sign are not -- they are that infinity. A
 * blanket infinity-plus-infinity rule loses the distinction and traps on a
 * calculation the hardware completes. */
static void test_opposite_infinities_are_an_operand_error(void) {
  const ap_m68882_extended_t positive = single(0x7F800000u);
  const ap_m68882_extended_t negative = single(0xFF800000u);

  const ap_m68882_op_t error = ap_m68882_add(&positive, &negative, RN, PX);
  TEST_ASSERT_TRUE(raised(&error, AP_M68882_EXC_OPERR));
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_NAN, ap_m68882_classify(&error.value));

  const ap_m68882_op_t same = ap_m68882_add(&positive, &positive, RN, PX);
  TEST_ASSERT_EQUAL_UINT32(0u, same.exceptions);
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_INFINITY,
                        ap_m68882_classify(&same.value));
}

/* Infinity plus a finite number is that infinity, with no exception. */
static void test_infinity_plus_a_finite_number_is_infinity(void) {
  const ap_m68882_extended_t infinity = single(0x7F800000u);
  const ap_m68882_extended_t one = single(0x3F800000u);

  const ap_m68882_op_t sum = ap_m68882_add(&infinity, &one, RN, PX);
  TEST_ASSERT_EQUAL_UINT32(0u, sum.exceptions);
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_INFINITY, ap_m68882_classify(&sum.value));
  TEST_ASSERT_FALSE(sum.value.sign);
}

/* 3 * 4 = 12, exactly. The multiply's exponent arithmetic subtracts one bias
 * from the sum of two, and getting that wrong moves the answer by a factor of
 * 2^16383 -- which is an infinity, so it fails loudly rather than subtly. */
static void test_a_multiply_is_exact_when_it_can_be(void) {
  const ap_m68882_extended_t three = single(0x40400000u);
  const ap_m68882_extended_t four = single(0x40800000u);
  const ap_m68882_op_t product = ap_m68882_mul(&three, &four, RN, PX);

  TEST_ASSERT_EQUAL_HEX32(0x41400000u, ap_m68882_to_single(&product.value));
  TEST_ASSERT_EQUAL_UINT32(0u, product.exceptions);
}

/* Signs multiply independently of magnitudes, including for zero. */
static void test_a_product_takes_the_exclusive_or_of_the_signs(void) {
  const ap_m68882_extended_t two = single(0x40000000u);
  const ap_m68882_extended_t minus_two = single(0xC0000000u);

  TEST_ASSERT_TRUE(ap_m68882_mul(&two, &minus_two, RN, PX).value.sign);
  TEST_ASSERT_FALSE(ap_m68882_mul(&minus_two, &minus_two, RN, PX).value.sign);

  const ap_m68882_extended_t zero = single(0x00000000u);
  TEST_ASSERT_TRUE(ap_m68882_mul(&zero, &minus_two, RN, PX).value.sign);
}

/* **Table 6-2's FMUL row**: "One Operand is 0, Other Operand is +/-infinity". */
static void test_zero_times_infinity_is_an_operand_error(void) {
  const ap_m68882_extended_t zero = single(0x00000000u);
  const ap_m68882_extended_t infinity = single(0x7F800000u);

  const ap_m68882_op_t error = ap_m68882_mul(&zero, &infinity, RN, PX);
  TEST_ASSERT_TRUE(raised(&error, AP_M68882_EXC_OPERR));
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_NAN, ap_m68882_classify(&error.value));

  /* The order does not matter. */
  const ap_m68882_op_t reversed = ap_m68882_mul(&infinity, &zero, RN, PX);
  TEST_ASSERT_TRUE(raised(&reversed, AP_M68882_EXC_OPERR));
}

/* 12 / 4 = 3, exactly, and 1 / 3 is inexact -- the two halves of the long
 * division, one terminating and one not. */
static void test_a_divide_is_exact_when_it_can_be(void) {
  const ap_m68882_extended_t twelve = single(0x41400000u);
  const ap_m68882_extended_t four = single(0x40800000u);
  const ap_m68882_op_t exact = ap_m68882_div(&twelve, &four, RN, PX);
  TEST_ASSERT_EQUAL_HEX32(0x40400000u, ap_m68882_to_single(&exact.value));
  TEST_ASSERT_EQUAL_UINT32(0u, exact.exceptions);

  const ap_m68882_extended_t one = single(0x3F800000u);
  const ap_m68882_extended_t three = single(0x40400000u);
  const ap_m68882_op_t recurring = ap_m68882_div(&one, &three, RN, PX);
  TEST_ASSERT_TRUE(raised(&recurring, AP_M68882_EXC_INEX2));
}

/* **`DZ` is not `OPERR`.** They are different exceptions with different
 * vectors: 1/0 is *defined* -- an infinity of the right sign -- and only 0/0 is
 * an operand error. A model folding the two would trap the wrong handler on
 * every division by zero. */
static void test_divide_by_zero_is_not_an_operand_error(void) {
  const ap_m68882_extended_t one = single(0x3F800000u);
  const ap_m68882_extended_t zero = single(0x00000000u);
  const ap_m68882_extended_t minus_zero = single(0x80000000u);

  const ap_m68882_op_t defined = ap_m68882_div(&one, &zero, RN, PX);
  TEST_ASSERT_TRUE(raised(&defined, AP_M68882_EXC_DZ));
  TEST_ASSERT_FALSE(raised(&defined, AP_M68882_EXC_OPERR));
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_INFINITY,
                        ap_m68882_classify(&defined.value));
  TEST_ASSERT_FALSE(defined.value.sign);

  /* The sign follows the division's, so 1/-0 is minus infinity. */
  const ap_m68882_op_t negative = ap_m68882_div(&one, &minus_zero, RN, PX);
  TEST_ASSERT_TRUE(negative.value.sign);

  /* And 0/0 *is* an operand error. */
  const ap_m68882_op_t error = ap_m68882_div(&zero, &zero, RN, PX);
  TEST_ASSERT_TRUE(raised(&error, AP_M68882_EXC_OPERR));
  TEST_ASSERT_FALSE(raised(&error, AP_M68882_EXC_DZ));
}

/* Table 6-2's FDIV row also has "infinity/infinity". */
static void test_infinity_over_infinity_is_an_operand_error(void) {
  const ap_m68882_extended_t infinity = single(0x7F800000u);
  const ap_m68882_op_t error = ap_m68882_div(&infinity, &infinity, RN, PX);
  TEST_ASSERT_TRUE(raised(&error, AP_M68882_EXC_OPERR));
}

/* A NAN operand propagates through every operation, and a **signalling** one
 * raises SNAN and comes out quiet. Leaving it signalling would raise the
 * exception again on every later operation, turning one invalid operand into an
 * exception per instruction for the rest of the calculation. */
static void test_a_signalling_nan_raises_once_and_comes_out_quiet(void) {
  ap_m68882_extended_t signalling = single(0x7F800000u);
  signalling.mantissa |= 1u; /* a payload with the quiet bit clear */
  TEST_ASSERT_TRUE(ap_m68882_is_signalling_nan(&signalling));

  const ap_m68882_extended_t one = single(0x3F800000u);
  const ap_m68882_op_t sum = ap_m68882_add(&signalling, &one, RN, PX);

  TEST_ASSERT_TRUE(raised(&sum, AP_M68882_EXC_SNAN));
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_NAN, ap_m68882_classify(&sum.value));
  TEST_ASSERT_FALSE(ap_m68882_is_signalling_nan(&sum.value));

  /* And the quiet result raises nothing when used again. */
  const ap_m68882_op_t again = ap_m68882_add(&sum.value, &one, RN, PX);
  TEST_ASSERT_EQUAL_UINT32(0u, again.exceptions);
}

/* A NAN beats an infinity: the NAN rule is applied before Table 6-2's
 * combinations, so (+inf) + (-inf) with a NAN in play is a NAN and not an
 * operand error. */
static void test_a_nan_beats_an_operand_error(void) {
  ap_m68882_extended_t nan = single(0x7F800000u);
  nan.mantissa |= UINT64_C(1) << 62; /* quiet */
  const ap_m68882_extended_t infinity = single(0x7F800000u);

  const ap_m68882_op_t out = ap_m68882_mul(&nan, &infinity, RN, PX);
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_NAN, ap_m68882_classify(&out.value));
  TEST_ASSERT_FALSE((out.exceptions & (UINT32_C(1) << AP_M68882_EXC_OPERR)) !=
                    0u);
}

/* Comparison: ordered values compare by value, and **+0 equals -0**. That is
 * the one place a zero's sign does not matter, and a comparison that consulted
 * it would send every loop testing a computed zero down the wrong branch half
 * the time. */
static void test_comparison_orders_values_and_equates_the_two_zeros(void) {
  const ap_m68882_extended_t one = single(0x3F800000u);
  const ap_m68882_extended_t two = single(0x40000000u);
  const ap_m68882_extended_t minus_one = single(0xBF800000u);

  const ap_m68882_compare_t less = ap_m68882_compare(&one, &two);
  TEST_ASSERT_TRUE(less.less);
  TEST_ASSERT_FALSE(less.equal);
  TEST_ASSERT_FALSE(less.unordered);

  const ap_m68882_compare_t greater = ap_m68882_compare(&two, &one);
  TEST_ASSERT_FALSE(greater.less);
  TEST_ASSERT_FALSE(greater.equal);

  /* Negative numbers order the other way round in magnitude. */
  TEST_ASSERT_TRUE(ap_m68882_compare(&minus_one, &one).less);
  TEST_ASSERT_FALSE(ap_m68882_compare(&one, &minus_one).less);

  const ap_m68882_extended_t zero = single(0x00000000u);
  const ap_m68882_extended_t minus_zero = single(0x80000000u);
  const ap_m68882_compare_t zeros = ap_m68882_compare(&zero, &minus_zero);
  TEST_ASSERT_TRUE(zeros.equal);
  TEST_ASSERT_FALSE(zeros.less);
}

/* Two negatives compare by magnitude *reversed*: -2 is less than -1. A model
 * comparing the raw fields without flipping for the sign gets every negative
 * pair backwards. */
static void test_two_negatives_compare_by_reversed_magnitude(void) {
  const ap_m68882_extended_t minus_one = single(0xBF800000u);
  const ap_m68882_extended_t minus_two = single(0xC0000000u);

  TEST_ASSERT_TRUE(ap_m68882_compare(&minus_two, &minus_one).less);
  TEST_ASSERT_FALSE(ap_m68882_compare(&minus_one, &minus_two).less);
}

/* **Unordered is its own answer**, not "neither less nor greater nor equal".
 * A conditional branch distinguishes them -- which is what `BSUN` exists for --
 * so a three-way comparison that collapsed the case would make every NAN
 * compare as greater. */
static void test_a_nan_compares_unordered_rather_than_unequal(void) {
  ap_m68882_extended_t nan = single(0x7F800000u);
  nan.mantissa |= UINT64_C(1) << 62;
  const ap_m68882_extended_t one = single(0x3F800000u);

  const ap_m68882_compare_t out = ap_m68882_compare(&nan, &one);
  TEST_ASSERT_TRUE(out.unordered);
  TEST_ASSERT_FALSE(out.less);
  TEST_ASSERT_FALSE(out.equal);

  /* A NAN is not even equal to itself. */
  const ap_m68882_compare_t self = ap_m68882_compare(&nan, &nan);
  TEST_ASSERT_TRUE(self.unordered);
  TEST_ASSERT_FALSE(self.equal);
}

/* Addition is commutative for every ordered pair, which is a property rather
 * than a value -- and the alignment code is where it would break, since the
 * operands are swapped there when the exponents differ. */
static void test_addition_is_commutative(void) {
  const uint32_t VALUES[] = {0x3F800000u, 0x40000000u, 0xC0A00000u,
                             0x00000000u, 0x4B000000u, 0x33000000u};
  for (unsigned i = 0; i < sizeof VALUES / sizeof VALUES[0]; i++) {
    for (unsigned k = 0; k < sizeof VALUES / sizeof VALUES[0]; k++) {
      const ap_m68882_extended_t a = single(VALUES[i]);
      const ap_m68882_extended_t b = single(VALUES[k]);
      const ap_m68882_op_t forward = ap_m68882_add(&a, &b, RN, PX);
      const ap_m68882_op_t backward = ap_m68882_add(&b, &a, RN, PX);
      TEST_ASSERT_EQUAL_HEX64(forward.value.mantissa, backward.value.mantissa);
      TEST_ASSERT_EQUAL_HEX16(forward.value.exponent, backward.value.exponent);
      TEST_ASSERT_EQUAL_INT(forward.value.sign, backward.value.sign);
    }
  }
}

/* Multiplication is commutative too, and its 64x64 product is where a
 * half-width mistake would show asymmetrically. */
static void test_multiplication_is_commutative(void) {
  const uint32_t VALUES[] = {0x3F800000u, 0x40490FDBu, 0xC0A00000u,
                             0x3E800000u, 0x4B7FFFFFu};
  for (unsigned i = 0; i < sizeof VALUES / sizeof VALUES[0]; i++) {
    for (unsigned k = 0; k < sizeof VALUES / sizeof VALUES[0]; k++) {
      const ap_m68882_extended_t a = single(VALUES[i]);
      const ap_m68882_extended_t b = single(VALUES[k]);
      const ap_m68882_op_t forward = ap_m68882_mul(&a, &b, RN, PX);
      const ap_m68882_op_t backward = ap_m68882_mul(&b, &a, RN, PX);
      TEST_ASSERT_EQUAL_HEX64(forward.value.mantissa, backward.value.mantissa);
      TEST_ASSERT_EQUAL_HEX16(forward.value.exponent, backward.value.exponent);
    }
  }
}

/* Dividing a value by itself is one, for every ordered value -- which exercises
 * the long division's normalisation on operands whose quotient is exactly at
 * the boundary. */
static void test_a_value_divided_by_itself_is_one(void) {
  const uint32_t VALUES[] = {0x3F800000u, 0x40490FDBu, 0xC0A00000u,
                             0x3E800000u, 0x4B7FFFFFu, 0x00800000u};
  const ap_m68882_extended_t one = single(0x3F800000u);
  for (unsigned i = 0; i < sizeof VALUES / sizeof VALUES[0]; i++) {
    const ap_m68882_extended_t a = single(VALUES[i]);
    const ap_m68882_op_t quotient = ap_m68882_div(&a, &a, RN, PX);
    TEST_ASSERT_EQUAL_HEX64(one.mantissa, quotient.value.mantissa);
    TEST_ASSERT_EQUAL_HEX16(one.exponent, quotient.value.exponent);
    TEST_ASSERT_FALSE(quotient.value.sign);
  }
}

/* Multiply and divide invert each other on values whose product is exact, which
 * is a check on the two together that neither can pass alone. */
static void test_multiply_and_divide_invert_each_other(void) {
  const ap_m68882_extended_t a = single(0x40490FDBu); /* ~pi */
  const ap_m68882_extended_t b = single(0x40800000u); /* 4, a power of two */

  const ap_m68882_op_t product = ap_m68882_mul(&a, &b, RN, PX);
  const ap_m68882_op_t back = ap_m68882_div(&product.value, &b, RN, PX);

  TEST_ASSERT_EQUAL_HEX64(a.mantissa, back.value.mantissa);
  TEST_ASSERT_EQUAL_HEX16(a.exponent, back.value.exponent);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_one_plus_one_is_two);
  RUN_TEST(test_a_subtraction_borrows_from_the_guard_bits);
  RUN_TEST(test_x_minus_x_is_positive_zero);
  RUN_TEST(test_opposite_infinities_are_an_operand_error);
  RUN_TEST(test_infinity_plus_a_finite_number_is_infinity);
  RUN_TEST(test_a_multiply_is_exact_when_it_can_be);
  RUN_TEST(test_a_product_takes_the_exclusive_or_of_the_signs);
  RUN_TEST(test_zero_times_infinity_is_an_operand_error);
  RUN_TEST(test_a_divide_is_exact_when_it_can_be);
  RUN_TEST(test_divide_by_zero_is_not_an_operand_error);
  RUN_TEST(test_infinity_over_infinity_is_an_operand_error);
  RUN_TEST(test_a_signalling_nan_raises_once_and_comes_out_quiet);
  RUN_TEST(test_a_nan_beats_an_operand_error);
  RUN_TEST(test_comparison_orders_values_and_equates_the_two_zeros);
  RUN_TEST(test_two_negatives_compare_by_reversed_magnitude);
  RUN_TEST(test_a_nan_compares_unordered_rather_than_unequal);
  RUN_TEST(test_addition_is_commutative);
  RUN_TEST(test_multiplication_is_commutative);
  RUN_TEST(test_a_value_divided_by_itself_is_one);
  RUN_TEST(test_multiply_and_divide_invert_each_other);
  return UNITY_END();
}
