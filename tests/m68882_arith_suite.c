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


/* ---------------------------------------------------------------------------
 * The exactly-specified monadic operations.
 *
 * §4.3.2 excludes square root from the transcendentals -- "the IEEE
 * specification does not define the error bound to which transcendental
 * (except square root) functions are to be performed" -- so these have one
 * right answer and can be checked against it rather than against a bound.
 * ------------------------------------------------------------------------- */

/* Perfect squares come back exactly, which is the property a correctly rounded
 * square root has and an approximation does not. Swept over powers of two and
 * the squares between them, because an off-by-one in the exponent halving
 * survives some values and not others -- an *odd* exponent is the case that
 * needs its extra factor of two folded into the mantissa first. */
static void test_a_square_root_is_exact_for_perfect_squares(void) {
  const struct {
    uint32_t square;
    uint32_t root;
    const char *what;
  } CASES[] = {
      {0x3F800000u, 0x3F800000u, "1"},      /* sqrt(1) = 1 */
      {0x40800000u, 0x40000000u, "4"},      /* sqrt(4) = 2 */
      {0x41100000u, 0x40400000u, "9"},      /* sqrt(9) = 3 */
      {0x41800000u, 0x40800000u, "16"},     /* sqrt(16) = 4 */
      {0x42C80000u, 0x41200000u, "100"},    /* sqrt(100) = 10 */
      {0x40000000u, 0x3FB504F3u, "2"},      /* sqrt(2), inexact */
  };

  for (unsigned i = 0; i < 5u; i++) { /* the exact ones */
    const ap_m68882_extended_t in = single(CASES[i].square);
    const ap_m68882_op_t root = ap_m68882_sqrt(&in, RN, PX);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(CASES[i].root,
                                    ap_m68882_to_single(&root.value),
                                    CASES[i].what);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, root.exceptions, CASES[i].what);
  }

  /* And an irrational one is inexact rather than silently wrong. */
  const ap_m68882_extended_t two = single(CASES[5].square);
  const ap_m68882_op_t root_two = ap_m68882_sqrt(&two, RN, PX);
  TEST_ASSERT_TRUE(raised(&root_two, AP_M68882_EXC_INEX2));
  TEST_ASSERT_EQUAL_HEX32(CASES[5].root, ap_m68882_to_single(&root_two.value));
}

/* **`sqrt(-0)` is `-0`**, which is IEEE's rule and not an oversight: a zero's
 * sign is information and the square root preserves it. But any *other*
 * negative source is Table 6-2's "FSQRT: Source <0" operand error, so the two
 * cases must be told apart -- a model checking the sign alone would trap on
 * negative zero. */
static void test_a_square_root_of_negative_zero_is_negative_zero(void) {
  const ap_m68882_extended_t minus_zero = single(0x80000000u);
  const ap_m68882_op_t zero_root = ap_m68882_sqrt(&minus_zero, RN, PX);
  TEST_ASSERT_EQUAL_UINT32(0u, zero_root.exceptions);
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_ZERO,
                        ap_m68882_classify(&zero_root.value));
  TEST_ASSERT_TRUE(zero_root.value.sign);

  const ap_m68882_extended_t minus_four = single(0xC0800000u);
  const ap_m68882_op_t error = ap_m68882_sqrt(&minus_four, RN, PX);
  TEST_ASSERT_TRUE(raised(&error, AP_M68882_EXC_OPERR));
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_NAN, ap_m68882_classify(&error.value));
}

/* `FGETEXP` returns the *unbiased* exponent as a floating-point number --
 * "removes the exponent bias, converts the exponent to an extended precision
 * floating-point number". Returning the biased one, or an integer, would both
 * be plausible and both wrong. */
static void test_getexp_returns_the_unbiased_exponent_as_a_float(void) {
  const struct {
    uint32_t value;
    uint32_t exponent;
    const char *what;
  } CASES[] = {
      {0x3F800000u, 0x00000000u, "1.0 -> 0"},
      {0x40000000u, 0x3F800000u, "2.0 -> 1"},
      {0x40800000u, 0x40000000u, "4.0 -> 2"},
      {0x3F000000u, 0xBF800000u, "0.5 -> -1"},
  };
  for (unsigned i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
    const ap_m68882_extended_t in = single(CASES[i].value);
    const ap_m68882_op_t got = ap_m68882_getexp(&in);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(CASES[i].exponent,
                                    ap_m68882_to_single(&got.value),
                                    CASES[i].what);
  }

  /* An infinity has no meaningful exponent: Table 6-2's operand error. */
  const ap_m68882_extended_t infinity = single(0x7F800000u);
  const ap_m68882_op_t infinite = ap_m68882_getexp(&infinity);
  TEST_ASSERT_TRUE(raised(&infinite, AP_M68882_EXC_OPERR));
}

/* `FGETMAN` returns the mantissa in [1,2) **with the source's sign**. Dropping
 * the sign would make it the magnitude, which is a different function. */
static void test_getman_keeps_the_sign(void) {
  const ap_m68882_extended_t negative = single(0xC0A00000u); /* -5.0 */
  const ap_m68882_op_t mantissa = ap_m68882_getman(&negative);

  /* -5.0 is -1.25 x 2^2, so the mantissa is -1.25. */
  TEST_ASSERT_EQUAL_HEX32(0xBFA00000u, ap_m68882_to_single(&mantissa.value));
  TEST_ASSERT_TRUE(mantissa.value.sign);
}

/* **`FINT` follows the rounding mode and `FINTRZ` does not**, which is the only
 * difference between them and the reason there are two instructions. The
 * manual's own example: "the integer part of 137.57 is 137.0 for the
 * round-to-zero and round-to-minus infinity modes, and 138.0 for the
 * round-to-nearest and round-to-plus infinity modes". */
static void test_fint_follows_the_mode_and_fintrz_does_not(void) {
  /* 137.57 in single precision. */
  const ap_m68882_extended_t value = single(0x43099eb8u);

  const ap_m68882_op_t nearest = ap_m68882_int(&value, RN);
  TEST_ASSERT_EQUAL_HEX32(0x430A0000u, /* 138.0 */
                          ap_m68882_to_single(&nearest.value));

  const ap_m68882_op_t toward_zero =
      ap_m68882_int(&value, AP_M68882_ROUND_ZERO);
  TEST_ASSERT_EQUAL_HEX32(0x43090000u, /* 137.0 */
                          ap_m68882_to_single(&toward_zero.value));

  const ap_m68882_op_t upward =
      ap_m68882_int(&value, AP_M68882_ROUND_PLUS_INFINITY);
  TEST_ASSERT_EQUAL_HEX32(0x430A0000u, ap_m68882_to_single(&upward.value));

  /* FINTRZ truncates whatever the mode says -- which is what makes it a
   * different instruction rather than a shorthand. */
  const ap_m68882_op_t truncated = ap_m68882_intrz(&value);
  TEST_ASSERT_EQUAL_HEX32(0x43090000u, ap_m68882_to_single(&truncated.value));
}

/* A value that is already an integer comes back untouched, and one below 1 in
 * magnitude goes to zero -- or to one under a directed mode, which is the case
 * a truncating-only implementation gets wrong. */
static void test_integer_part_at_the_boundaries(void) {
  const ap_m68882_extended_t four = single(0x40800000u);
  const ap_m68882_op_t already = ap_m68882_intrz(&four);
  TEST_ASSERT_EQUAL_HEX32(0x40800000u, ap_m68882_to_single(&already.value));

  const ap_m68882_extended_t half = single(0x3F000000u); /* 0.5 */
  const ap_m68882_op_t truncated_half = ap_m68882_intrz(&half);
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_ZERO,
                        ap_m68882_classify(&truncated_half.value));

  /* Toward plus infinity, 0.5 becomes 1.0. */
  const ap_m68882_op_t up = ap_m68882_int(&half, AP_M68882_ROUND_PLUS_INFINITY);
  TEST_ASSERT_EQUAL_HEX32(0x3F800000u, ap_m68882_to_single(&up.value));
}

/* `FSCALE` adds to the exponent, so it is **exact** -- that is the point of
 * having it rather than multiplying by a power of two, which would round. */
static void test_scale_is_exact_exponent_arithmetic(void) {
  const ap_m68882_extended_t three = single(0x40400000u); /* 3.0 */
  const ap_m68882_extended_t two = single(0x40000000u);   /* 2 */
  const ap_m68882_op_t scaled = ap_m68882_scale(&three, &two);

  /* 3.0 x 2^2 = 12.0, exactly and with no exception. */
  TEST_ASSERT_EQUAL_HEX32(0x41400000u, ap_m68882_to_single(&scaled.value));
  TEST_ASSERT_EQUAL_UINT32(0u, scaled.exceptions);

  /* A negative scale divides. */
  const ap_m68882_extended_t minus_one = single(0xBF800000u);
  const ap_m68882_op_t halved = ap_m68882_scale(&three, &minus_one);
  TEST_ASSERT_EQUAL_HEX32(0x3FC00000u, /* 1.5 */
                          ap_m68882_to_single(&halved.value));

  /* Table 6-2: an infinite scale factor is an operand error. */
  const ap_m68882_extended_t infinity = single(0x7F800000u);
  const ap_m68882_op_t infinite = ap_m68882_scale(&three, &infinity);
  TEST_ASSERT_TRUE(raised(&infinite, AP_M68882_EXC_OPERR));
}

static void test_a_divide_normalises_when_the_dividend_is_the_smaller(void) {
  /* A regression, and one that had survived twenty tests in this file.
   *
   * Two significands are each in [1,2), so their quotient is in [0.5,2) and the
   * leading one lands in one of two places. When the dividend's significand is
   * the smaller, the first quotient bit is a zero, the exponent must drop by
   * one to account for it, **and the division must run one bit longer** so the
   * leading one still reaches bit 63. Dropping the exponent without lengthening
   * the division halves the answer.
   *
   * That is what `ap_m68882_div` did, for roughly half of all divides. It
   * survived because every property this suite checked happens to use operands
   * where the dividend is not the smaller: `x / x` has equal significands,
   * and the multiply-then-divide round trips were built from values that
   * divided the other way. A property test is only as good as the operands it
   * is given, and "commutative", "inverse" and "self" are all satisfiable
   * without ever entering this branch.
   *
   * The values are chosen so the quotient is not representable, which is what
   * makes the halving visible rather than hidden in an exact power of two. */
  const ap_m68882_extended_t two = {false, 0x4000, 0x8000000000000000ULL};
  const ap_m68882_extended_t three = {false, 0x4000, 0xC000000000000000ULL};
  const ap_m68882_op_t two_thirds =
      ap_m68882_div(&two, &three, AP_M68882_ROUND_NEAREST,
                    AP_M68882_PRECISION_EXTENDED);
  /* 2/3 is 1.333... x 2^-1. */
  TEST_ASSERT_EQUAL_UINT(0x3FFEu, two_thirds.value.exponent);
  TEST_ASSERT_EQUAL_UINT64(0xAAAAAAAAAAAAAAABULL, two_thirds.value.mantissa);
  TEST_ASSERT_FALSE(two_thirds.value.sign);

  /* And with different exponents as well as different significands, so the
   * exponent arithmetic and the normalisation are both exercised. */
  const ap_m68882_extended_t quarter = {false, 0x3FFD, 0x8000000000000000ULL};
  const ap_m68882_extended_t two_and_a_quarter = {false, 0x4000,
                                                  0x9000000000000000ULL};
  const ap_m68882_op_t ninth =
      ap_m68882_div(&quarter, &two_and_a_quarter, AP_M68882_ROUND_NEAREST,
                    AP_M68882_PRECISION_EXTENDED);
  /* 0.25/2.25 is 1/9, which is 1.777... x 2^-4. */
  TEST_ASSERT_EQUAL_UINT(0x3FFBu, ninth.value.exponent);
  TEST_ASSERT_EQUAL_UINT64(0xE38E38E38E38E38EULL, ninth.value.mantissa);
}

static void test_every_quotient_comes_back_normalised(void) {
  /* The general form of the fault above: whatever the operands, the result of a
   * finite non-zero divide must have its integer bit set. An unnormalised
   * significand with a normal exponent is not a representable extended value at
   * all, and it is the shape the halving bug produced. */
  const ap_m68882_extended_t values[] = {
      {false, 0x3FFF, 0x8000000000000000ULL}, /* 1 */
      {false, 0x4000, 0xC000000000000000ULL}, /* 3 */
      {false, 0x3FFD, 0x9000000000000000ULL},
      {false, 0x4005, 0xFFFFFFFFFFFFFFFFULL},
      {false, 0x3F00, 0x8000000000000001ULL},
      {false, 0x4100, 0xB504F333F9DE6484ULL},
  };
  const unsigned n = sizeof values / sizeof values[0];
  for (unsigned i = 0; i < n; i++) {
    for (unsigned j = 0; j < n; j++) {
      const ap_m68882_op_t q =
          ap_m68882_div(&values[i], &values[j], AP_M68882_ROUND_NEAREST,
                        AP_M68882_PRECISION_EXTENDED);
      if (q.value.exponent == 0u || q.value.exponent == 0x7FFFu) {
        continue; /* a denormal or an infinity is a different question */
      }
      TEST_ASSERT_TRUE_MESSAGE(
          (q.value.mantissa & (1ULL << 63)) != 0u,
          "a divide returned an unnormalised significand");
    }
  }
}

static void test_an_overflow_is_not_always_an_infinity(void) {
  /* §6.1.4's trap-disabled table, which the obvious reading of "overflow" gets
   * wrong:
   *
   *     RN   Infinity, with the sign of the intermediate result
   *     RZ   Largest magnitude number, with the sign of the intermediate result
   *     RM   For positive overflow, largest positive number
   *          For negative overflow, -infinity
   *     RP   For positive overflow, +infinity
   *          For negative overflow, largest negative number
   *
   * One rule underneath: an infinity when the mode pushes *away* from zero in
   * that direction, the largest finite number when it pulls back. Returning an
   * infinity unconditionally makes round-to-zero produce a value the part never
   * produces -- and silently, because `OVFL` is set either way and only the
   * stored number differs. */
  const ap_m68882_extended_t largest = {false, 0x7FFE,
                                        0xFFFFFFFFFFFFFFFFULL};
  const ap_m68882_extended_t two = {false, 0x4000, 0x8000000000000000ULL};
  const struct {
    ap_m68882_rounding_t mode;
    bool infinite;
  } positive[] = {{AP_M68882_ROUND_NEAREST, true},
                  {AP_M68882_ROUND_ZERO, false},
                  {AP_M68882_ROUND_MINUS_INFINITY, false},
                  {AP_M68882_ROUND_PLUS_INFINITY, true}};

  for (unsigned i = 0; i < 4u; i++) {
    const ap_m68882_op_t got = ap_m68882_mul(&largest, &two, positive[i].mode,
                                             AP_M68882_PRECISION_EXTENDED);
    TEST_ASSERT_NOT_EQUAL_UINT(0u, got.exceptions & (1u << AP_M68882_EXC_OVFL));
    TEST_ASSERT_FALSE(got.value.sign);
    if (positive[i].infinite) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(AP_M68882_TYPE_INFINITY,
                                    ap_m68882_classify(&got.value),
                                    "this mode overflows to an infinity");
    } else {
      TEST_ASSERT_EQUAL_INT_MESSAGE(AP_M68882_TYPE_NORMALIZED,
                                    ap_m68882_classify(&got.value),
                                    "this mode overflows to a finite number");
      TEST_ASSERT_EQUAL_UINT(0x7FFEu, got.value.exponent);
      TEST_ASSERT_EQUAL_UINT64(0xFFFFFFFFFFFFFFFFULL, got.value.mantissa);
    }
  }

  /* The negative direction, where the two directed modes exchange roles --
   * which is the half of the table a symmetric implementation would miss. */
  ap_m68882_extended_t minus_two = two;
  minus_two.sign = true;
  const ap_m68882_op_t runaway =
      ap_m68882_mul(&largest, &minus_two, AP_M68882_ROUND_MINUS_INFINITY,
                    AP_M68882_PRECISION_EXTENDED);
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_INFINITY,
                        ap_m68882_classify(&runaway.value));
  TEST_ASSERT_TRUE(runaway.value.sign);
  const ap_m68882_op_t held =
      ap_m68882_mul(&largest, &minus_two, AP_M68882_ROUND_PLUS_INFINITY,
                    AP_M68882_PRECISION_EXTENDED);
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_NORMALIZED,
                        ap_m68882_classify(&held.value));
  TEST_ASSERT_TRUE(held.value.sign);
}

static void test_a_value_extended_can_hold_still_overflows_a_single(void) {
  /* §6.1.4's NOTE: "An overflow can occur when the destination is a
   * floating-point data register and the selected rounding precision is single
   * or double **even if the intermediate result is small enough to be
   * represented as an extended precision number**."
   *
   * So the threshold is the rounding precision's maximum exponent, not
   * extended's. `2^200` is an ordinary extended number and an overflow at
   * single precision, where the largest exponent is 127. Checking the extended
   * limit alone would store it happily and report nothing. */
  const ap_m68882_extended_t big = {false,
                                    (uint16_t)(AP_M68882_BIAS_EXTENDED + 200),
                                    0x8000000000000000ULL};
  const ap_m68882_extended_t one = {false, AP_M68882_BIAS_EXTENDED,
                                    0x8000000000000000ULL};

  const ap_m68882_op_t wide =
      ap_m68882_mul(&big, &one, AP_M68882_ROUND_NEAREST,
                    AP_M68882_PRECISION_EXTENDED);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      0u, wide.exceptions & (1u << AP_M68882_EXC_OVFL),
      "2^200 is an ordinary extended number");
  TEST_ASSERT_EQUAL_UINT(AP_M68882_BIAS_EXTENDED + 200, wide.value.exponent);

  const ap_m68882_op_t narrow =
      ap_m68882_mul(&big, &one, AP_M68882_ROUND_NEAREST,
                    AP_M68882_PRECISION_SINGLE);
  TEST_ASSERT_NOT_EQUAL_UINT_MESSAGE(
      0u, narrow.exceptions & (1u << AP_M68882_EXC_OVFL),
      "2^200 overflows a single-precision destination");
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_INFINITY,
                        ap_m68882_classify(&narrow.value));

  /* And the largest finite number substituted under round-to-zero is the
   * largest of *that* precision, not of extended: 24 significand bits and an
   * exponent of 127. */
  const ap_m68882_op_t clamped =
      ap_m68882_mul(&big, &one, AP_M68882_ROUND_ZERO,
                    AP_M68882_PRECISION_SINGLE);
  TEST_ASSERT_EQUAL_UINT(AP_M68882_BIAS_EXTENDED + 127,
                         clamped.value.exponent);
  TEST_ASSERT_EQUAL_UINT64(0xFFFFFF0000000000ULL, clamped.value.mantissa);

  /* Double precision keeps 53 bits and an exponent of 1023. */
  const ap_m68882_op_t doubled =
      ap_m68882_mul(&big, &one, AP_M68882_ROUND_ZERO,
                    AP_M68882_PRECISION_DOUBLE);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      0u, doubled.exceptions & (1u << AP_M68882_EXC_OVFL),
      "2^200 fits a double-precision destination");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_square_root_is_exact_for_perfect_squares);
  RUN_TEST(test_a_square_root_of_negative_zero_is_negative_zero);
  RUN_TEST(test_getexp_returns_the_unbiased_exponent_as_a_float);
  RUN_TEST(test_getman_keeps_the_sign);
  RUN_TEST(test_fint_follows_the_mode_and_fintrz_does_not);
  RUN_TEST(test_integer_part_at_the_boundaries);
  RUN_TEST(test_scale_is_exact_exponent_arithmetic);
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
  RUN_TEST(test_a_divide_normalises_when_the_dividend_is_the_smaller);
  RUN_TEST(test_every_quotient_comes_back_normalised);
  RUN_TEST(test_an_overflow_is_not_always_an_infinity);
  RUN_TEST(test_a_value_extended_can_hold_still_overflows_a_single);
  return UNITY_END();
}
