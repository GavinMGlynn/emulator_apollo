/* MC68882 destination format conversion.
 *
 * Each test states a rule the manual gives and checks the rule rather than a
 * constant this project produced. Where a number is asserted it is one the IEEE
 * encodings fix -- 1.0 single is $3F800000 on every machine ever built -- or one
 * the manual prints.
 */

#include "cpu/m68882/ap_m68882_store.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Read back what the encoder wrote, most significant byte first. Taken by value
 * so a call can be nested directly inside an assertion. */
static uint64_t stored(ap_m68882_store_t out) {
  uint64_t value = 0;
  for (unsigned i = 0; i < out.size; i++) {
    value = (value << 8) | out.bytes[i];
  }
  return value;
}

/* Infinities and NANs, which the biased-exponent helper above cannot express:
 * their exponent field is the maximum rather than an offset from the bias. */
static ap_m68882_extended_t special(bool sign, uint64_t mantissa) {
  return (ap_m68882_extended_t){
      .sign = sign, .exponent = 0x7FFFu, .mantissa = mantissa};
}

static ap_m68882_store_t encode(ap_m68882_format_t format,
                                ap_m68882_extended_t value,
                                ap_m68882_rounding_t mode) {
  ap_m68882_store_t out = {0};
  TEST_ASSERT_TRUE(ap_m68882_store_encode(format, &value, mode, &out));
  return out;
}

static ap_m68882_extended_t from_parts(bool sign, int exponent,
                                       uint64_t mantissa) {
  return (ap_m68882_extended_t){
      .sign = sign,
      .exponent = (uint16_t)(AP_M68882_BIAS_EXTENDED + exponent),
      .mantissa = mantissa};
}

/* One point zero in each destination, which is the value that pins the bias and
 * the field positions at once. */
static void test_one_point_zero_lands_in_every_format(void) {
  const ap_m68882_extended_t one = from_parts(false, 0, UINT64_C(1) << 63);

  const ap_m68882_store_t single =
      encode(AP_M68882_FORMAT_SINGLE, one, AP_M68882_ROUND_NEAREST);
  TEST_ASSERT_EQUAL_UINT(4u, single.size);
  TEST_ASSERT_EQUAL_HEX32(0x3F800000u, (uint32_t)stored(single));

  const ap_m68882_store_t twice =
      encode(AP_M68882_FORMAT_DOUBLE, one, AP_M68882_ROUND_NEAREST);
  TEST_ASSERT_EQUAL_UINT(8u, twice.size);
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0x3FF0000000000000), stored(twice));

  const ap_m68882_store_t integer =
      encode(AP_M68882_FORMAT_LONG, one, AP_M68882_ROUND_NEAREST);
  TEST_ASSERT_EQUAL_HEX32(1u, (uint32_t)stored(integer));

  /* None of them is inexact: 1.0 is representable in all three. */
  TEST_ASSERT_EQUAL_HEX32(0u, single.exceptions);
  TEST_ASSERT_EQUAL_HEX32(0u, twice.exceptions);
  TEST_ASSERT_EQUAL_HEX32(0u, integer.exceptions);
}

/* **The store rounds to the destination format, not to the FPCR's precision.**
 * §2.2.2: "If the destination is a memory location, the PREC bits are ignored.
 * In this case, a number in the extended precision format is taken from the
 * source floating-point data register, rounded to the destination format
 * precision, and written to memory."
 *
 * Stated here as the *rounding mode still applying while the width does not*: a
 * value one bit past single's precision rounds up under RN and down under RZ,
 * and both are inexact. A model that consulted PREC could not produce the second
 * pair of answers at all. */
static void test_a_store_rounds_to_the_destination_width(void) {
  /* 1 + 2^-24: needs 25 significand bits, single holds 24. */
  const ap_m68882_extended_t value =
      from_parts(false, 0, (UINT64_C(1) << 63) | (UINT64_C(1) << 39));

  const ap_m68882_store_t nearest =
      encode(AP_M68882_FORMAT_SINGLE, value, AP_M68882_ROUND_NEAREST);
  /* An exact tie, and the last kept bit is zero, so round-half-to-even keeps
   * it: 1.0 rather than the next single up. */
  TEST_ASSERT_EQUAL_HEX32(0x3F800000u, (uint32_t)stored(nearest));
  TEST_ASSERT_EQUAL_HEX32(UINT32_C(1) << AP_M68882_EXC_INEX2,
                          nearest.exceptions);

  const ap_m68882_store_t up =
      encode(AP_M68882_FORMAT_SINGLE, value, AP_M68882_ROUND_PLUS_INFINITY);
  TEST_ASSERT_EQUAL_HEX32(0x3F800001u, (uint32_t)stored(up));

  const ap_m68882_store_t down =
      encode(AP_M68882_FORMAT_SINGLE, value, AP_M68882_ROUND_ZERO);
  TEST_ASSERT_EQUAL_HEX32(0x3F800000u, (uint32_t)stored(down));

  /* The same value into a double is exact -- 53 bits is ample -- which is what
   * makes this the destination's width and not a property of the value. */
  const ap_m68882_store_t exact =
      encode(AP_M68882_FORMAT_DOUBLE, value, AP_M68882_ROUND_NEAREST);
  TEST_ASSERT_EQUAL_HEX32(0u, exact.exceptions);
}

/* **Gradual underflow.** A value below single's smallest normal is a subnormal
 * there, not a zero, and the significand loses one bit per power of two. The
 * smallest single subnormal is 2^-149; 2^-140 keeps ten bits.
 *
 * A converter that flushed to zero here would lose the value entirely and still
 * set no exception a program could notice, since the number it stored would be
 * a perfectly ordinary zero. */
static void test_a_value_below_the_format_denormalises_rather_than_vanishing(
    void) {
  const ap_m68882_extended_t small =
      from_parts(false, -140, UINT64_C(1) << 63);
  const ap_m68882_store_t out =
      encode(AP_M68882_FORMAT_SINGLE, small, AP_M68882_ROUND_NEAREST);

  /* Exponent field zero, and the single significant bit at 2^-140, which in a
   * field scaled to 2^-149 is bit 9. */
  TEST_ASSERT_EQUAL_HEX32(UINT32_C(1) << 9, (uint32_t)stored(out));
  TEST_ASSERT_TRUE((out.exceptions & (UINT32_C(1) << AP_M68882_EXC_UNFL)) != 0u);
  /* Exact: no bit was lost, so it underflows without being inexact. */
  TEST_ASSERT_EQUAL_HEX32(0u,
                          out.exceptions & (UINT32_C(1) << AP_M68882_EXC_INEX2));

  /* The smallest subnormal single, 2^-149, still survives. */
  const ap_m68882_extended_t least = from_parts(false, -149, UINT64_C(1) << 63);
  const ap_m68882_store_t tiny =
      encode(AP_M68882_FORMAT_SINGLE, least, AP_M68882_ROUND_NEAREST);
  TEST_ASSERT_EQUAL_HEX32(1u, (uint32_t)stored(tiny));

  /* Below that there is nothing left to hold, and the *mode* decides between
   * zero and the smallest magnitude -- a mode rounding away from zero cannot
   * turn a non-zero into a zero. */
  const ap_m68882_extended_t under = from_parts(false, -160, UINT64_C(1) << 63);
  TEST_ASSERT_EQUAL_HEX32(
      0u, (uint32_t)stored(encode(AP_M68882_FORMAT_SINGLE, under,
                                   AP_M68882_ROUND_NEAREST)));
  TEST_ASSERT_EQUAL_HEX32(
      1u, (uint32_t)stored(encode(AP_M68882_FORMAT_SINGLE, under,
                                   AP_M68882_ROUND_PLUS_INFINITY)));
}

/* Overflow's trap-disabled result is **mode-dependent** and not always an
 * infinity: §6.1.4 has round-to-zero pull back to the largest finite magnitude.
 * The exception byte reads the same either way, so a model returning infinity
 * unconditionally would be wrong silently. */
static void test_overflowing_a_single_follows_the_rounding_mode(void) {
  /* 2^200, far above single's largest. */
  const ap_m68882_extended_t big = from_parts(false, 200, UINT64_C(1) << 63);

  const ap_m68882_store_t nearest =
      encode(AP_M68882_FORMAT_SINGLE, big, AP_M68882_ROUND_NEAREST);
  TEST_ASSERT_EQUAL_HEX32(0x7F800000u, (uint32_t)stored(nearest)); /* +inf */
  TEST_ASSERT_TRUE((nearest.exceptions & (UINT32_C(1) << AP_M68882_EXC_OVFL)) !=
                   0u);

  const ap_m68882_store_t zero_ward =
      encode(AP_M68882_FORMAT_SINGLE, big, AP_M68882_ROUND_ZERO);
  TEST_ASSERT_EQUAL_HEX32(0x7F7FFFFFu, (uint32_t)stored(zero_ward));

  /* And it is the *destination* that overflows, not the value: the same number
   * is an ordinary double. */
  const ap_m68882_store_t fits =
      encode(AP_M68882_FORMAT_DOUBLE, big, AP_M68882_ROUND_NEAREST);
  TEST_ASSERT_EQUAL_HEX32(0u, fits.exceptions);
}

/* §6.1.3: "An operand error is never generated when the destination is an MPU
 * data register or memory and the destination format is S, D, or X." So an
 * infinity stored to a single is simply an infinity -- the case that would look
 * most like an error if the integer rule were applied to every format. */
static void test_a_real_destination_never_raises_an_operand_error(void) {
  const ap_m68882_extended_t infinity = special(true, UINT64_C(1) << 63);
  for (unsigned f = 0; f < 3u; f++) {
    const ap_m68882_format_t format =
        (f == 0) ? AP_M68882_FORMAT_SINGLE
                 : (f == 1) ? AP_M68882_FORMAT_DOUBLE
                            : AP_M68882_FORMAT_EXTENDED;
    const ap_m68882_store_t out =
        encode(format, infinity, AP_M68882_ROUND_NEAREST);
    TEST_ASSERT_EQUAL_HEX32(
        0u, out.exceptions & (UINT32_C(1) << AP_M68882_EXC_OPERR));
  }
  TEST_ASSERT_EQUAL_HEX32(
      0xFF800000u,
      (uint32_t)stored(encode(AP_M68882_FORMAT_SINGLE, infinity,
                               AP_M68882_ROUND_NEAREST)));
}

/* Table 6-2's row for `FMOVE to B,W, or L`: "Integer Overflow/Underflow, Source
 * is Non-Signaling NAN, or Source is +/-infinity". Three conditions, and §6.1.3
 * gives infinity and overflow the same result -- "the largest positive or
 * negative integer that can fit in the specified destination format size". */
static void test_an_integer_destination_saturates_and_reports_it(void) {
  const ap_m68882_extended_t infinity = special(false, UINT64_C(1) << 63);
  const ap_m68882_store_t inf_long =
      encode(AP_M68882_FORMAT_LONG, infinity, AP_M68882_ROUND_NEAREST);
  TEST_ASSERT_EQUAL_HEX32(0x7FFFFFFFu, (uint32_t)stored(inf_long));
  TEST_ASSERT_TRUE((inf_long.exceptions &
                    (UINT32_C(1) << AP_M68882_EXC_OPERR)) != 0u);

  /* 1000 fits a word and a long word and does not fit a byte -- so the same
   * value is an operand error in one destination and exact in the others,
   * which is what makes the limit the *format's* and not the value's. */
  const ap_m68882_extended_t thousand =
      from_parts(false, 9, UINT64_C(0xFA00000000000000));
  const ap_m68882_store_t as_word =
      encode(AP_M68882_FORMAT_WORD, thousand, AP_M68882_ROUND_NEAREST);
  TEST_ASSERT_EQUAL_HEX32(1000u, (uint32_t)stored(as_word));
  TEST_ASSERT_EQUAL_HEX32(0u, as_word.exceptions);

  const ap_m68882_store_t as_byte =
      encode(AP_M68882_FORMAT_BYTE, thousand, AP_M68882_ROUND_NEAREST);
  TEST_ASSERT_EQUAL_HEX32(0x7Fu, (uint32_t)stored(as_byte));
  TEST_ASSERT_TRUE((as_byte.exceptions & (UINT32_C(1) << AP_M68882_EXC_OPERR)) !=
                   0u);

  /* Negative saturation goes to the most negative value, which has no positive
   * counterpart -- the input that a magnitude-then-negate implementation gets
   * wrong. */
  const ap_m68882_extended_t minus_thousand =
      from_parts(true, 9, UINT64_C(0xFA00000000000000));
  const ap_m68882_store_t low =
      encode(AP_M68882_FORMAT_BYTE, minus_thousand, AP_M68882_ROUND_NEAREST);
  TEST_ASSERT_EQUAL_HEX32(0x80u, (uint32_t)stored(low));
}

/* An integer store rounds by the mode rather than truncating, which is `FINT`'s
 * rule reused: "the integer part of 137.57 is 137.0 for the round-to-zero and
 * round-to-minus infinity modes, and 138.0 for the round-to-nearest and
 * round-to-plus infinity modes" -- the manual's own example, here as a store. */
static void test_an_integer_store_follows_the_rounding_mode(void) {
  /* 137.57 to the nearest extended value. */
  const ap_m68882_extended_t value =
      from_parts(false, 7, UINT64_C(0x8991EB851EB851EB));

  TEST_ASSERT_EQUAL_HEX32(
      138u, (uint32_t)stored(encode(AP_M68882_FORMAT_LONG, value,
                                     AP_M68882_ROUND_NEAREST)));
  TEST_ASSERT_EQUAL_HEX32(
      137u, (uint32_t)stored(encode(AP_M68882_FORMAT_LONG, value,
                                     AP_M68882_ROUND_ZERO)));
  TEST_ASSERT_EQUAL_HEX32(
      138u, (uint32_t)stored(encode(AP_M68882_FORMAT_LONG, value,
                                     AP_M68882_ROUND_PLUS_INFINITY)));
  TEST_ASSERT_EQUAL_HEX32(
      137u, (uint32_t)stored(encode(AP_M68882_FORMAT_LONG, value,
                                     AP_M68882_ROUND_MINUS_INFINITY)));

  /* And it is inexact, which truncation would also have to report. */
  const ap_m68882_store_t out =
      encode(AP_M68882_FORMAT_LONG, value, AP_M68882_ROUND_NEAREST);
  TEST_ASSERT_TRUE((out.exceptions & (UINT32_C(1) << AP_M68882_EXC_INEX2)) !=
                   0u);
}

/* §6.1.2's two NAN rules, which differ by destination *kind* and not by format
 * width. For S, D and X: "the SNAN bit in the NAN is set to one and the
 * resulting non-signaling NAN is transferred". For B, W and L: "the 8, 16, or 32
 * most significant bits of the SNAN significand, with the SNAN bit set". Both
 * raise SNAN; only the integer one also raises an operand error, and only when
 * the NAN was quiet to begin with. */
static void test_a_signalling_nan_is_quietened_and_reported(void) {
  /* Integer bit set, quiet bit clear: signalling, with a payload. */
  const ap_m68882_extended_t snan = special(false, UINT64_C(0x8000000012345678));

  const ap_m68882_store_t as_single =
      encode(AP_M68882_FORMAT_SINGLE, snan, AP_M68882_ROUND_NEAREST);
  TEST_ASSERT_TRUE((as_single.exceptions &
                    (UINT32_C(1) << AP_M68882_EXC_SNAN)) != 0u);
  /* Quiet on arrival: single's top fraction bit is 22. */
  TEST_ASSERT_TRUE((stored(as_single) & (UINT32_C(1) << 22)) != 0u);
  /* And an operand error is *not* raised, because the format is S. */
  TEST_ASSERT_EQUAL_HEX32(
      0u, as_single.exceptions & (UINT32_C(1) << AP_M68882_EXC_OPERR));

  /* The extended destination sets the same bit and changes nothing else. */
  const ap_m68882_store_t as_extended =
      encode(AP_M68882_FORMAT_EXTENDED, snan, AP_M68882_ROUND_NEAREST);
  TEST_ASSERT_EQUAL_UINT(12u, as_extended.size);
  TEST_ASSERT_TRUE((as_extended.exceptions &
                    (UINT32_C(1) << AP_M68882_EXC_SNAN)) != 0u);
  TEST_ASSERT_EQUAL_HEX8(0xC0u, as_extended.bytes[4]); /* quiet bit set */
  TEST_ASSERT_EQUAL_HEX8(0x12u, as_extended.bytes[8]); /* payload intact */

  /* A quiet NAN into an integer is an operand error and *not* a signalling
   * one -- the two conditions are separate rows in separate sections. */
  const ap_m68882_extended_t quiet = special(false, UINT64_C(0xC000000012345678));
  const ap_m68882_store_t as_long =
      encode(AP_M68882_FORMAT_LONG, quiet, AP_M68882_ROUND_NEAREST);
  TEST_ASSERT_TRUE((as_long.exceptions & (UINT32_C(1) << AP_M68882_EXC_OPERR)) !=
                   0u);
  TEST_ASSERT_EQUAL_HEX32(
      0u, as_long.exceptions & (UINT32_C(1) << AP_M68882_EXC_SNAN));
  /* "the 32 most significant bits of the NAN significand". */
  TEST_ASSERT_EQUAL_HEX32(0xC0000000u, (uint32_t)stored(as_long));
}

/* Extended is the one destination that cannot be inexact: it *is* the internal
 * format. Twelve bytes, with the sixteen unused bits written as zero so that two
 * stores of the same value compare equal. */
static void test_an_extended_store_is_exact_and_twelve_bytes(void) {
  const ap_m68882_extended_t value =
      from_parts(true, 5, UINT64_C(0xFEDCBA9876543210));
  const ap_m68882_store_t out =
      encode(AP_M68882_FORMAT_EXTENDED, value, AP_M68882_ROUND_ZERO);

  TEST_ASSERT_EQUAL_UINT(12u, out.size);
  TEST_ASSERT_EQUAL_HEX32(0u, out.exceptions);
  TEST_ASSERT_EQUAL_HEX8(0xC0u, out.bytes[0]); /* sign, and exponent's top */
  TEST_ASSERT_EQUAL_HEX8(0x00u, out.bytes[2]); /* the unused field */
  TEST_ASSERT_EQUAL_HEX8(0x00u, out.bytes[3]);
  TEST_ASSERT_EQUAL_HEX8(0xFEu, out.bytes[4]);
  TEST_ASSERT_EQUAL_HEX8(0x10u, out.bytes[11]);

  /* And it round-trips through the load path, which is the strongest statement
   * available without asserting a constant: the two conversions are inverse. */
  ap_m68882_extended_t back = {0};
  TEST_ASSERT_TRUE(
      ap_m68882_operand_decode(AP_M68882_FORMAT_EXTENDED, out.bytes, &back));
  TEST_ASSERT_EQUAL_HEX16(value.exponent, back.exponent);
  TEST_ASSERT_EQUAL_HEX64(value.mantissa, back.mantissa);
  TEST_ASSERT_TRUE(back.sign);
}

/* Packed decimal declines in both directions, so the gap stays visible rather
 * than becoming a plausible wrong number. */
static void test_packed_decimal_declines_on_the_way_out_too(void) {
  const ap_m68882_extended_t one = from_parts(false, 0, UINT64_C(1) << 63);
  ap_m68882_store_t out = {0};
  TEST_ASSERT_FALSE(
      ap_m68882_store_encode(AP_M68882_FORMAT_PACKED, &one,
                             AP_M68882_ROUND_NEAREST, &out));
  TEST_ASSERT_FALSE(
      ap_m68882_store_encode(AP_M68882_FORMAT_PACKED_DYNAMIC, &one,
                             AP_M68882_ROUND_NEAREST, &out));
}

/* Every single-precision value survives a round trip out and back. Structural:
 * it asks not what a particular constant should be, only that storing a value
 * the format can hold and loading it again is the identity -- which catches a
 * field one bit out of place at any exponent, including the subnormal range the
 * sweep deliberately reaches into. */
static void test_single_precision_round_trips_across_the_range(void) {
  for (uint32_t exponent = 0u; exponent < 255u; exponent += 7u) {
    for (uint32_t fraction = 0u; fraction < 0x800000u;
         fraction += 0x111111u) {
      const uint32_t bits = (exponent << 23) | fraction;
      const ap_m68882_extended_t value = ap_m68882_from_single(bits);
      const ap_m68882_store_t out =
          encode(AP_M68882_FORMAT_SINGLE, value, AP_M68882_ROUND_NEAREST);
      TEST_ASSERT_EQUAL_HEX32(bits, (uint32_t)stored(out));
    }
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_one_point_zero_lands_in_every_format);
  RUN_TEST(test_a_store_rounds_to_the_destination_width);
  RUN_TEST(test_a_value_below_the_format_denormalises_rather_than_vanishing);
  RUN_TEST(test_overflowing_a_single_follows_the_rounding_mode);
  RUN_TEST(test_a_real_destination_never_raises_an_operand_error);
  RUN_TEST(test_an_integer_destination_saturates_and_reports_it);
  RUN_TEST(test_an_integer_store_follows_the_rounding_mode);
  RUN_TEST(test_a_signalling_nan_is_quietened_and_reported);
  RUN_TEST(test_an_extended_store_is_exact_and_twelve_bytes);
  RUN_TEST(test_packed_decimal_declines_on_the_way_out_too);
  RUN_TEST(test_single_precision_round_trips_across_the_range);
  return UNITY_END();
}
