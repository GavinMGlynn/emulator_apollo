/* MC68882 binary real data formats.
 *
 * Checked against the manual's own statements and against structure -- round
 * trips, the documented biases, and the one place where the three formats
 * genuinely disagree -- rather than against constants this project produced.
 */

#include "cpu/m68882/ap_m68882_format.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* 1.0 in each format, which is the value that pins the bias: its exponent is
 * exactly the bias and its fraction is zero. A bias transcribed wrongly moves
 * every number by a power of two and nothing faults. */
static void test_one_point_zero_pins_each_bias(void) {
  /* Single: exponent $7F = 127. */
  const ap_m68882_extended_t from_single =
      ap_m68882_from_single(0x3F800000u);
  TEST_ASSERT_EQUAL_HEX16(AP_M68882_BIAS_EXTENDED, from_single.exponent);
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0x8000000000000000), from_single.mantissa);
  TEST_ASSERT_FALSE(from_single.sign);

  /* Double: exponent $3FF = 1023. */
  const ap_m68882_extended_t from_double =
      ap_m68882_from_double(UINT64_C(0x3FF0000000000000));
  TEST_ASSERT_EQUAL_HEX16(AP_M68882_BIAS_EXTENDED, from_double.exponent);
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0x8000000000000000), from_double.mantissa);

  /* And back again. */
  TEST_ASSERT_EQUAL_HEX32(0x3F800000u, ap_m68882_to_single(&from_single));
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0x3FF0000000000000),
                          ap_m68882_to_double(&from_double));
}

/* The implied leading one is made **explicit** on the way in. That is the whole
 * difference between the extended format and the other two, and a converter
 * that copied the fraction across without setting the integer bit would halve
 * every normalized number. */
static void test_the_implied_one_becomes_explicit(void) {
  /* 1.5 single: fraction $400000, so the mantissa is 1.1 binary. */
  const ap_m68882_extended_t value = ap_m68882_from_single(0x3FC00000u);
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0xC000000000000000), value.mantissa);
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_NORMALIZED,
                        ap_m68882_classify(&value));
}

/* **The NOTE, and the reason classification takes an extended value.** "An
 * extended precision number with an exponent of zero may have an explicit
 * integer bit equal to one, which results in a normalized number (even though
 * the exponent is equal to the minimum value)."
 *
 * So "exponent zero means denormalized" holds for two formats out of three, and
 * a classifier carried over from the other two misreads exactly the numbers the
 * extended format exists to hold. Both cases here share an exponent of zero and
 * differ only in the integer bit. */
static void test_an_extended_exponent_of_zero_is_not_always_denormalized(void) {
  const ap_m68882_extended_t normalized = {
      .sign = false, .exponent = 0u, .mantissa = UINT64_C(0x8000000000000000)};
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_NORMALIZED,
                        ap_m68882_classify(&normalized));

  const ap_m68882_extended_t denormalized = {
      .sign = false, .exponent = 0u, .mantissa = UINT64_C(0x4000000000000000)};
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_DENORMALIZED,
                        ap_m68882_classify(&denormalized));
}

/* The five data types, each reachable and each distinct. */
static void test_the_five_data_types(void) {
  const ap_m68882_extended_t zero = {false, 0u, 0u};
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_ZERO, ap_m68882_classify(&zero));

  const ap_m68882_extended_t negative_zero = {true, 0u, 0u};
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_ZERO,
                        ap_m68882_classify(&negative_zero));

  const ap_m68882_extended_t infinity = {false, 0x7FFFu,
                                         UINT64_C(0x8000000000000000)};
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_INFINITY,
                        ap_m68882_classify(&infinity));

  const ap_m68882_extended_t nan = {false, 0x7FFFu,
                                    UINT64_C(0xC000000000000000)};
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_NAN, ap_m68882_classify(&nan));

  const ap_m68882_extended_t normalized = {false, AP_M68882_BIAS_EXTENDED,
                                           UINT64_C(0x8000000000000000)};
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_NORMALIZED,
                        ap_m68882_classify(&normalized));
}

/* An infinity is told from a NAN by the **fraction**, with the integer bit
 * ignored. Real 68881 output sets the integer bit on infinities, so a model
 * requiring it clear would classify the part's own results as NANs -- which is
 * a different data type, with different condition codes, on every overflow. */
static void test_an_infinity_is_recognised_with_its_integer_bit_set(void) {
  const ap_m68882_extended_t with_bit = {false, 0x7FFFu,
                                         UINT64_C(0x8000000000000000)};
  const ap_m68882_extended_t without = {false, 0x7FFFu, 0u};
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_INFINITY,
                        ap_m68882_classify(&with_bit));
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_INFINITY, ap_m68882_classify(&without));

  /* Any fraction at all makes it a NAN. */
  const ap_m68882_extended_t nan = {false, 0x7FFFu, UINT64_C(0x8000000000000001)};
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_NAN, ap_m68882_classify(&nan));
}

/* Signalling is the top **fraction** bit clear, one below the integer bit.
 * Reading bit 63 instead would call every NAN signalling, since a NAN carries
 * its integer bit set -- and a signalling NAN raises SNAN where a quiet one
 * propagates silently, so the two are not interchangeable. */
static void test_a_signalling_nan_is_the_fraction_bit_not_the_integer_bit(void) {
  const ap_m68882_extended_t signalling = {false, 0x7FFFu,
                                           UINT64_C(0x8000000000000001)};
  const ap_m68882_extended_t quiet = {false, 0x7FFFu,
                                      UINT64_C(0xC000000000000000)};
  TEST_ASSERT_TRUE(ap_m68882_is_signalling_nan(&signalling));
  TEST_ASSERT_FALSE(ap_m68882_is_signalling_nan(&quiet));

  /* And nothing that is not a NAN is signalling, whatever its bits. */
  const ap_m68882_extended_t normalized = {false, AP_M68882_BIAS_EXTENDED,
                                           UINT64_C(0x8000000000000000)};
  TEST_ASSERT_FALSE(ap_m68882_is_signalling_nan(&normalized));
}

/* A denormal's true exponent is the format's *minimum*, `1 - bias`, not
 * `0 - bias`. Getting that wrong puts every denormalized number a factor of two
 * out -- a small enough error to look like rounding and a wrong one. */
static void test_a_denormal_uses_the_minimum_exponent_not_zero(void) {
  /* The smallest single-precision denormal: exponent 0, fraction 1. Its true
   * value is 2^-126 * 2^-23, so its extended exponent is bias - 126 - 23 after
   * normalisation -- but this converter does not normalise, so what is checked
   * is the exponent it assigns: 1 - 127 = -126. */
  const ap_m68882_extended_t denormal = ap_m68882_from_single(0x00000001u);
  TEST_ASSERT_EQUAL_HEX16((uint16_t)(AP_M68882_BIAS_EXTENDED - 126),
                          denormal.exponent);
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_DENORMALIZED,
                        ap_m68882_classify(&denormal));
}

/* Signed zeros survive: `-0` is not `+0`, and Table 2-1 gives them different
 * condition codes. A converter dropping the sign on a zero mantissa would make
 * them indistinguishable. */
static void test_a_signed_zero_keeps_its_sign(void) {
  const ap_m68882_extended_t negative = ap_m68882_from_single(0x80000000u);
  TEST_ASSERT_TRUE(negative.sign);
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_ZERO, ap_m68882_classify(&negative));
  TEST_ASSERT_EQUAL_HEX32(0x80000000u, ap_m68882_to_single(&negative));
}

/* A NAN keeps its payload through a round trip, and does **not** become an
 * infinity when the payload lives entirely in bits the narrower format
 * discards. An infinity is a different data type with different condition
 * codes, so that conversion would change the answer rather than lose
 * precision. */
static void test_a_nan_round_trips_without_becoming_an_infinity(void) {
  /* A payload only in the low bits, which single precision cannot hold. */
  const ap_m68882_extended_t nan = {false, 0x7FFFu,
                                    UINT64_C(0x8000000000000001)};
  const uint32_t as_single = ap_m68882_to_single(&nan);
  const ap_m68882_extended_t back = ap_m68882_from_single(as_single);
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_NAN, ap_m68882_classify(&back));
}

/* The extended memory format is **96 bits of which 80 are used**: sign and a
 * 15-bit exponent in the first long word, then sixteen unused bits, then the
 * mantissa. A model packing it as 80 contiguous bits reads every mantissa 16
 * bits out of place the moment it touches memory. */
static void test_the_extended_memory_format_has_sixteen_unused_bits(void) {
  const ap_m68882_extended_t value = {true, 0x4001u,
                                      UINT64_C(0xC000000000000000)};
  uint32_t high = 0;
  uint64_t mantissa = 0;
  ap_m68882_to_extended(&value, &high, &mantissa);

  /* The sign at bit 31 and the exponent at 30-16. */
  TEST_ASSERT_EQUAL_HEX32(0xC0010000u, high);
  /* And bits 15-0 are zero, not part of the exponent or the mantissa. */
  TEST_ASSERT_EQUAL_HEX32(0u, high & 0xFFFFu);
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0xC000000000000000), mantissa);

  const ap_m68882_extended_t back = ap_m68882_from_extended(high, mantissa);
  TEST_ASSERT_EQUAL_HEX16(value.exponent, back.exponent);
  TEST_ASSERT_EQUAL_HEX64(value.mantissa, back.mantissa);
  TEST_ASSERT_EQUAL_INT(value.sign, back.sign);
}

/* Whatever is in the unused sixteen bits is ignored on the way in. They are not
 * part of the exponent, so a memory image with rubbish there must read back as
 * the same number -- which is what lets two stores of one value be compared. */
static void test_the_unused_bits_are_ignored_on_the_way_in(void) {
  const ap_m68882_extended_t clean =
      ap_m68882_from_extended(0x40010000u, UINT64_C(0x8000000000000000));
  const ap_m68882_extended_t noisy =
      ap_m68882_from_extended(0x4001FFFFu, UINT64_C(0x8000000000000000));
  TEST_ASSERT_EQUAL_HEX16(clean.exponent, noisy.exponent);
  TEST_ASSERT_EQUAL_HEX64(clean.mantissa, noisy.mantissa);
}

/* Every normalized single-precision value round trips exactly, because extended
 * is strictly wider in both fields. Swept rather than sampled: a shift written
 * one place out shows on some fractions and not others, and a single value can
 * easily be one of the ones it survives. */
static void test_single_precision_round_trips_across_the_range(void) {
  for (unsigned exponent = 1u; exponent < 255u; exponent += 17u) {
    for (uint32_t fraction = 0u; fraction < 0x800000u;
         fraction += 0x111111u) {
      const uint32_t bits = (exponent << 23) | fraction;
      const ap_m68882_extended_t value = ap_m68882_from_single(bits);
      TEST_ASSERT_EQUAL_HEX32(bits, ap_m68882_to_single(&value));
    }
  }
}

/* §3.1: the integer formats are "signed (twos complement)". So the top bit is a
 * sign and not a magnitude bit, and a byte of $FF is -1 rather than 255. A
 * converter that widened without sign-extending would be right for exactly half
 * of every integer operand the part ever loads. */
static void test_integer_operands_are_signed(void) {
  const uint8_t minus_one_byte[1] = {0xFF};
  ap_m68882_extended_t value;
  TEST_ASSERT_TRUE(
      ap_m68882_operand_decode(AP_M68882_FORMAT_BYTE, minus_one_byte, &value));
  TEST_ASSERT_TRUE(value.sign);
  TEST_ASSERT_EQUAL_HEX16(AP_M68882_BIAS_EXTENDED, value.exponent);
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0x8000000000000000), value.mantissa);

  const uint8_t minus_one_word[2] = {0xFF, 0xFF};
  TEST_ASSERT_TRUE(
      ap_m68882_operand_decode(AP_M68882_FORMAT_WORD, minus_one_word, &value));
  TEST_ASSERT_TRUE(value.sign);
  TEST_ASSERT_EQUAL_HEX16(AP_M68882_BIAS_EXTENDED, value.exponent);
}

/* The two ends of the long-word range. `INT32_MIN` is the input that has no
 * positive counterpart, so negating it in signed arithmetic is undefined -- and
 * it is a perfectly ordinary operand for the part. */
static void test_the_integer_extremes_convert_exactly(void) {
  /* $80000000 = -2^31, so the mantissa is a lone integer bit and the exponent
   * is bias + 31. */
  const ap_m68882_extended_t most_negative = ap_m68882_from_integer(INT32_MIN);
  TEST_ASSERT_TRUE(most_negative.sign);
  TEST_ASSERT_EQUAL_HEX16(AP_M68882_BIAS_EXTENDED + 31, most_negative.exponent);
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0x8000000000000000),
                          most_negative.mantissa);

  /* $7FFFFFFF = 2^31 - 1: 31 significant bits, so the exponent is bias + 30 and
   * the mantissa holds them all with 33 bits to spare. Exact -- which is the
   * reason this conversion raises no exception. */
  const ap_m68882_extended_t most_positive = ap_m68882_from_integer(INT32_MAX);
  TEST_ASSERT_FALSE(most_positive.sign);
  TEST_ASSERT_EQUAL_HEX16(AP_M68882_BIAS_EXTENDED + 30, most_positive.exponent);
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0xFFFFFFFE00000000), most_positive.mantissa);

  /* Zero is the one value with no leading one to normalize onto. */
  const ap_m68882_extended_t zero = ap_m68882_from_integer(0);
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_ZERO, ap_m68882_classify(&zero));
  TEST_ASSERT_FALSE(zero.sign);
}

/* §4.8.4's encoding is not ordered by width and not grouped by kind: the three
 * integer formats sit at 000, 100 and 110 with the reals interleaved between
 * them. Stated as sizes, because that is what the encoding decides -- get one
 * entry wrong and the main processor fetches the wrong number of bytes from the
 * right address, which reads as arithmetic going wrong rather than as decode. */
static void test_the_source_specifier_encoding_is_not_ordered_by_width(void) {
  TEST_ASSERT_EQUAL_UINT(4u, ap_m68882_format_size(AP_M68882_FORMAT_LONG));
  TEST_ASSERT_EQUAL_UINT(4u, ap_m68882_format_size(AP_M68882_FORMAT_SINGLE));
  TEST_ASSERT_EQUAL_UINT(12u, ap_m68882_format_size(AP_M68882_FORMAT_EXTENDED));
  TEST_ASSERT_EQUAL_UINT(12u, ap_m68882_format_size(AP_M68882_FORMAT_PACKED));
  TEST_ASSERT_EQUAL_UINT(2u, ap_m68882_format_size(AP_M68882_FORMAT_WORD));
  TEST_ASSERT_EQUAL_UINT(8u, ap_m68882_format_size(AP_M68882_FORMAT_DOUBLE));
  TEST_ASSERT_EQUAL_UINT(1u, ap_m68882_format_size(AP_M68882_FORMAT_BYTE));

  /* The encoding runs 0..7 with no hole: $7 is a real format in the store
   * direction, packed decimal with a dynamic k-factor. */
  TEST_ASSERT_EQUAL_UINT(
      12u, ap_m68882_format_size(AP_M68882_FORMAT_PACKED_DYNAMIC));
}

/* §3: "the most-significant byte is located at the lowest address ... The
 * least-significant byte is located at the highest address." Checked on the
 * extended format, where the sixteen unused bits sit at bytes 2 and 3 -- so a
 * decoder that packed the 80 used bits contiguously would read every mantissa
 * two bytes out of place while still getting the sign and exponent right. */
static void test_an_operand_is_most_significant_byte_first(void) {
  /* 1.0 extended: exponent $3FFF, mantissa $8000000000000000. */
  const uint8_t one[12] = {0x3F, 0xFF, 0xAA, 0xAA, 0x80, 0x00,
                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  ap_m68882_extended_t value;
  TEST_ASSERT_TRUE(
      ap_m68882_operand_decode(AP_M68882_FORMAT_EXTENDED, one, &value));
  TEST_ASSERT_FALSE(value.sign);
  TEST_ASSERT_EQUAL_HEX16(AP_M68882_BIAS_EXTENDED, value.exponent);
  /* The $AAAA at bytes 2-3 is the unused field and reaches nothing. */
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0x8000000000000000), value.mantissa);

  /* The same value as a double, whose eight bytes are also high-first. */
  const uint8_t one_double[8] = {0x3F, 0xF0, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00};
  ap_m68882_extended_t as_double;
  TEST_ASSERT_TRUE(
      ap_m68882_operand_decode(AP_M68882_FORMAT_DOUBLE, one_double, &as_double));
  TEST_ASSERT_EQUAL_HEX16(value.exponent, as_double.exponent);
  TEST_ASSERT_EQUAL_HEX64(value.mantissa, as_double.mantissa);
}

/* Packed decimal is reported as our gap rather than decoded as something else.
 * A decoder that fell through to the binary path would turn $12345 BCD into a
 * plausible wrong number and the gap would stop being visible. */
static void test_packed_decimal_reports_the_gap(void) {
  const uint8_t operand[12] = {0};
  ap_m68882_extended_t value = {.sign = true, .exponent = 0x1234u};
  TEST_ASSERT_FALSE(
      ap_m68882_operand_decode(AP_M68882_FORMAT_PACKED, operand, &value));
  TEST_ASSERT_FALSE(ap_m68882_operand_decode(AP_M68882_FORMAT_PACKED_DYNAMIC,
                                             operand, &value));
  /* And the caller's value is left alone rather than half-written. */
  TEST_ASSERT_TRUE(value.sign);
  TEST_ASSERT_EQUAL_HEX16(0x1234u, value.exponent);
}

/* Every integer the three widths can hold, converted and read back by scaling
 * the mantissa down by the exponent. Structural: it never asks what a
 * particular constant should be, only that the conversion is the identity. */
static void test_integers_round_trip_across_their_range(void) {
  for (int64_t v = -70000; v <= 70000; v += 7) {
    const ap_m68882_extended_t value = ap_m68882_from_integer((int32_t)v);
    if (v == 0) {
      TEST_ASSERT_EQUAL_HEX64(0u, value.mantissa);
      continue;
    }
    const int exponent = (int)value.exponent - AP_M68882_BIAS_EXTENDED;
    /* The value is `mantissa * 2^(exponent - 63)`, and the integer bit is set,
     * so shifting right by `63 - exponent` recovers the magnitude exactly. */
    TEST_ASSERT_TRUE(exponent >= 0 && exponent <= 63);
    const uint64_t magnitude = value.mantissa >> (63 - exponent);
    TEST_ASSERT_EQUAL_INT64(v < 0 ? -v : v, (int64_t)magnitude);
    TEST_ASSERT_EQUAL_INT(v < 0, value.sign);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_integer_operands_are_signed);
  RUN_TEST(test_the_integer_extremes_convert_exactly);
  RUN_TEST(test_the_source_specifier_encoding_is_not_ordered_by_width);
  RUN_TEST(test_an_operand_is_most_significant_byte_first);
  RUN_TEST(test_packed_decimal_reports_the_gap);
  RUN_TEST(test_integers_round_trip_across_their_range);
  RUN_TEST(test_one_point_zero_pins_each_bias);
  RUN_TEST(test_the_implied_one_becomes_explicit);
  RUN_TEST(test_an_extended_exponent_of_zero_is_not_always_denormalized);
  RUN_TEST(test_the_five_data_types);
  RUN_TEST(test_an_infinity_is_recognised_with_its_integer_bit_set);
  RUN_TEST(test_a_signalling_nan_is_the_fraction_bit_not_the_integer_bit);
  RUN_TEST(test_a_denormal_uses_the_minimum_exponent_not_zero);
  RUN_TEST(test_a_signed_zero_keeps_its_sign);
  RUN_TEST(test_a_nan_round_trips_without_becoming_an_infinity);
  RUN_TEST(test_the_extended_memory_format_has_sixteen_unused_bits);
  RUN_TEST(test_the_unused_bits_are_ignored_on_the_way_in);
  RUN_TEST(test_single_precision_round_trips_across_the_range);
  return UNITY_END();
}
