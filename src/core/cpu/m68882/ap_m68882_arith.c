/* MC68882 arithmetic. See ap_m68882_arith.h for why the special cases are the
 * specification and what Table 6-2 makes an error rather than a value. */

#include "cpu/m68882/ap_m68882_arith.h"

#include "cpu/m68882/ap_m68882_format.h"

#define INTEGER_BIT (UINT64_C(1) << 63)
#define QUIET_BIT (UINT64_C(1) << 62)
#define MAX_EXPONENT 0x7FFFu

static ap_m68882_extended_t make_nan(void) {
  return (ap_m68882_extended_t){
      .sign = false, .exponent = MAX_EXPONENT, .mantissa = INTEGER_BIT | QUIET_BIT};
}

static ap_m68882_extended_t make_infinity(bool sign) {
  return (ap_m68882_extended_t){
      .sign = sign, .exponent = MAX_EXPONENT, .mantissa = INTEGER_BIT};
}

static ap_m68882_extended_t make_zero(bool sign) {
  return (ap_m68882_extended_t){.sign = sign, .exponent = 0u, .mantissa = 0u};
}

/* An operand error: `OPERR` raised and a non-signalling NAN produced. §6.1.3's
 * trap-disabled result for a floating-point destination is a NAN rather than a
 * zero or an infinity, which is what propagates the error through a
 * calculation instead of quietly taking part in it. */
static ap_m68882_op_t operand_error(void) {
  return (ap_m68882_op_t){.value = make_nan(),
                          .exceptions = UINT32_C(1) << AP_M68882_EXC_OPERR};
}

/* A NAN operand propagates, and a *signalling* one raises SNAN on the way.
 * Returns true when the result is decided by this rule alone -- which it is
 * whenever either operand is a NAN, before any of Table 6-2's combinations are
 * considered: a NAN beats an infinity. */
static bool propagate_nan(const ap_m68882_extended_t *a,
                          const ap_m68882_extended_t *b, ap_m68882_op_t *out) {
  const bool a_nan = ap_m68882_classify(a) == AP_M68882_TYPE_NAN;
  const bool b_nan = ap_m68882_classify(b) == AP_M68882_TYPE_NAN;
  if (!a_nan && !b_nan) {
    return false;
  }

  uint32_t exceptions = 0u;
  if ((a_nan && ap_m68882_is_signalling_nan(a)) ||
      (b_nan && ap_m68882_is_signalling_nan(b))) {
    exceptions |= UINT32_C(1) << AP_M68882_EXC_SNAN;
  }

  /* The source operand's NAN is the one that propagates when both are, which
   * keeps a payload flowing through a chain of operations rather than being
   * replaced at each step. */
  ap_m68882_extended_t value = a_nan ? *a : *b;
  /* A signalling NAN is quietened on the way out -- otherwise every later
   * operation on the result would raise SNAN again, turning one invalid operand
   * into an exception per instruction. */
  value.mantissa |= QUIET_BIT;
  out->value = value;
  out->exceptions = exceptions;
  return true;
}

/* Shift a mantissa right by `count`, accumulating everything shifted out into
 * the sticky bit. This is what makes the intermediate behave "as if to produce
 * infinite precision": the bits are gone and the fact that they were non-zero
 * is not. */
static void shift_right_sticky(uint64_t *mantissa, unsigned count, bool *guard,
                               bool *round_bit, bool *sticky) {
  if (count == 0u) {
    return;
  }
  if (count >= 64u) {
    *sticky = *sticky || *guard || *round_bit || *mantissa != 0u;
    *guard = false;
    *round_bit = false;
    *mantissa = 0u;
    return;
  }

  /* The bits about to leave, in order: the new guard is the highest of them. */
  for (unsigned i = 0; i < count; i++) {
    *sticky = *sticky || *round_bit;
    *round_bit = *guard;
    *guard = (*mantissa & 1u) != 0u;
    *mantissa >>= 1;
  }
}

/* Normalise a non-zero mantissa so its integer bit is set, pulling guard and
 * round back in as it shifts left. An operation's raw result can be
 * denormalised by up to 63 places -- a subtraction of two close numbers is the
 * usual way -- and leaving it that way would report a normalised number as
 * denormalised and round it at the wrong boundary. */
static void normalise(ap_m68882_extended_t *value, bool *guard,
                      bool *round_bit, bool *sticky) {
  if (value->mantissa == 0u) {
    return;
  }
  while ((value->mantissa & INTEGER_BIT) == 0u && value->exponent > 0u) {
    value->mantissa = (value->mantissa << 1) | (*guard ? 1u : 0u);
    *guard = *round_bit;
    *round_bit = *sticky;
    /* Sticky is a summary and cannot be shifted back out of; once set it stays,
     * which is the approximation the bit exists to make. */
    value->exponent = (uint16_t)(value->exponent - 1u);
  }
}

uint16_t ap_m68882_overflow_exponent(ap_m68882_precision_t precision) {
  switch (precision) {
  case AP_M68882_PRECISION_SINGLE:
    /* The largest single-precision exponent is 127 unbiased. */
    return (uint16_t)(AP_M68882_BIAS_EXTENDED + 128);
  case AP_M68882_PRECISION_DOUBLE:
    return (uint16_t)(AP_M68882_BIAS_EXTENDED + 1024);
  case AP_M68882_PRECISION_EXTENDED:
  case AP_M68882_PRECISION_RESERVED:
    break;
  }
  return MAX_EXPONENT;
}

uint16_t ap_m68882_underflow_exponent(ap_m68882_precision_t precision) {
  switch (precision) {
  case AP_M68882_PRECISION_SINGLE:
    return (uint16_t)(AP_M68882_BIAS_EXTENDED - 126);
  case AP_M68882_PRECISION_DOUBLE:
    return (uint16_t)(AP_M68882_BIAS_EXTENDED - 1022);
  case AP_M68882_PRECISION_EXTENDED:
  case AP_M68882_PRECISION_RESERVED:
    break;
  }
  return 0u;
}

ap_m68882_extended_t ap_m68882_overflow_result(bool sign,
                                               ap_m68882_rounding_t mode,
                                               ap_m68882_precision_t precision) {
  bool infinite;
  switch (mode) {
  case AP_M68882_ROUND_NEAREST:
    infinite = true;
    break;
  case AP_M68882_ROUND_ZERO:
    infinite = false;
    break;
  case AP_M68882_ROUND_MINUS_INFINITY:
    infinite = sign; /* only a negative overflow runs away downward */
    break;
  case AP_M68882_ROUND_PLUS_INFINITY:
    infinite = !sign;
    break;
  default:
    infinite = true;
    break;
  }
  if (infinite) {
    return make_infinity(sign);
  }
  /* The largest finite number of the rounding precision: every mantissa bit the
   * precision keeps set, at one below its overflow exponent. */
  const unsigned keep = ap_m68882_precision_bits(precision);
  const uint64_t mantissa =
      keep >= 64u ? UINT64_C(0xFFFFFFFFFFFFFFFF)
                  : (UINT64_C(0xFFFFFFFFFFFFFFFF) << (64u - keep));
  return (ap_m68882_extended_t){
      .sign = sign,
      .exponent = (uint16_t)(ap_m68882_overflow_exponent(precision) - 1u),
      .mantissa = mantissa};
}

/* Finish an operation: normalise, round, and report overflow or underflow.
 * Shared by all four so the exception rules cannot drift between them. */
static ap_m68882_op_t finish(ap_m68882_extended_t value, bool guard,
                             bool round_bit, bool sticky,
                             ap_m68882_rounding_t mode,
                             ap_m68882_precision_t precision,
                             uint32_t exceptions) {
  ap_m68882_op_t out = {.exceptions = exceptions};

  if (value.mantissa == 0u && !guard && !round_bit && !sticky) {
    out.value = make_zero(value.sign);
    return out;
  }

  normalise(&value, &guard, &round_bit, &sticky);

  /* "At the end of any operation that could potentially underflow, the
   * intermediate result is checked for underflow, rounded, and checked for
   * overflow before it is stored at the destination." So the denormalisation
   * happens *here*, before the rounding -- §6.1.5 again: "denormalization is
   * accomplished by shifting the mantissa of the intermediate result to the
   * right while incrementing the exponent until it is equal to the denormalized
   * exponent value for the destination format. The denormalized intermediate
   * result is [then] rounded to the selected rounding precision."
   *
   * Doing it the other way round would round twice: once at the intermediate's
   * own position and again after shifting, which is the classic double-rounding
   * error and gives a different answer near a tie.
   *
   * The threshold is the *rounding precision's*, not extended's. A result that
   * extended holds perfectly well is subnormal at single precision, and without
   * this it kept an extended exponent no single-precision destination could
   * represent and reported nothing at all. */
  const uint16_t minimum = ap_m68882_underflow_exponent(precision);
  if (value.exponent < minimum && value.mantissa != 0u) {
    shift_right_sticky(&value.mantissa,
                       (unsigned)(minimum - value.exponent), &guard,
                       &round_bit, &sticky);
    value.exponent = minimum;
    out.exceptions |= UINT32_C(1) << AP_M68882_EXC_UNFL;
  }

  const ap_m68882_round_result_t rounded =
      ap_m68882_round(value, guard, round_bit, sticky, mode, precision);
  out.value = rounded.value;
  if (rounded.inexact) {
    out.exceptions |= UINT32_C(1) << AP_M68882_EXC_INEX2;
  }

  if (out.value.exponent >= ap_m68882_overflow_exponent(precision)) {
    /* "Overflow is detected ... when the intermediate result exponent is
     * greater than or equal to the maximum exponent value of the selected
     * rounding precision" -- so a value extended could hold still overflows a
     * single-precision destination, which is what §6.1.4's NOTE spells out.
     *
     * The *value* stored is mode-dependent and is not always an infinity; see
     * `ap_m68882_overflow_result`. `INEX2` comes with it either way, since an
     * overflowed result is not the exact one -- which is also why `AEXC(INEX)`
     * is set by `OVFL`. */
    out.value = ap_m68882_overflow_result(out.value.sign, mode, precision);
    out.exceptions |= (UINT32_C(1) << AP_M68882_EXC_OVFL) |
                      (UINT32_C(1) << AP_M68882_EXC_INEX2);
    return out;
  }

  if (out.value.exponent == 0u && (out.value.mantissa & INTEGER_BIT) == 0u &&
      out.value.mantissa != 0u) {
    /* Denormalised: the exponent reached its minimum before the mantissa
     * normalised. "Underflow is detected for a given data format and operation
     * when the result exponent is less than or equal to the minimum exponent
     * value." Reported, and the value kept -- gradual underflow rather than
     * flush to zero, which is the IEEE behaviour the manual spends a page on. */
    out.exceptions |= UINT32_C(1) << AP_M68882_EXC_UNFL;
  }
  return out;
}

/* Add two values of the *same* sign, or subtract when they differ. Both
 * directions share the alignment, so they are one function with the sign
 * decided by the caller -- writing them separately is how the two ends up with
 * the other's rounding. */
static ap_m68882_op_t add_magnitudes(ap_m68882_extended_t a,
                                     ap_m68882_extended_t b, bool subtract,
                                     ap_m68882_rounding_t mode,
                                     ap_m68882_precision_t precision) {
  /* Align on the larger exponent. */
  /* Align on the larger operand, comparing the exponent *and then* the
   * mantissa: two values with the same exponent still have an order, and a
   * subtraction that took the smaller as its base would produce a negative
   * mantissa. The result then simply carries the larger operand's sign, which
   * is why nothing is flipped here -- flipping is the mistake that makes
   * addition non-commutative for mixed signs. */
  if (a.exponent < b.exponent ||
      (a.exponent == b.exponent && a.mantissa < b.mantissa)) {
    const ap_m68882_extended_t swap = a;
    a = b;
    b = swap;
  }

  bool guard = false;
  bool round_bit = false;
  bool sticky = false;
  const unsigned shift = (unsigned)(a.exponent - b.exponent);
  shift_right_sticky(&b.mantissa, shift, &guard, &round_bit, &sticky);

  ap_m68882_extended_t result = a;
  if (!subtract) {
    const uint64_t sum = a.mantissa + b.mantissa;
    if (sum < a.mantissa) {
      /* Carry out: shift the sum right and take the exponent up, exactly as
       * rounding's own overflow arm does. */
      result.mantissa = (sum >> 1) | INTEGER_BIT;
      sticky = sticky || round_bit;
      round_bit = guard;
      guard = (sum & 1u) != 0u;
      result.exponent = (uint16_t)(result.exponent + 1u);
    } else {
      result.mantissa = sum;
    }
    return finish(result, guard, round_bit, sticky, mode, precision, 0u);
  }

  /* Subtraction. The borrow from the guard bits has to come out of the
   * mantissa, or a difference of exactly one unit in the last place comes back
   * one too large. */
  uint64_t difference = a.mantissa - b.mantissa;
  if (guard || round_bit || sticky) {
    difference -= 1u;
    /* The bits below become the complement of what was shifted out. */
    guard = !guard;
    round_bit = !round_bit;
    sticky = !sticky;
  }
  result.mantissa = difference;

  if (difference == 0u && !guard && !round_bit && !sticky) {
    /* "x - x" is zero, and the IEEE standard gives it a **positive** sign in
     * every rounding mode but one: toward minus infinity it is negative. A
     * model returning the operand's sign gets that backwards half the time. */
    result = make_zero(mode == AP_M68882_ROUND_MINUS_INFINITY);
    return (ap_m68882_op_t){.value = result, .exceptions = 0u};
  }
  return finish(result, guard, round_bit, sticky, mode, precision, 0u);
}

static ap_m68882_op_t add_or_subtract(const ap_m68882_extended_t *a,
                                      const ap_m68882_extended_t *b,
                                      bool negate_b,
                                      ap_m68882_rounding_t mode,
                                      ap_m68882_precision_t precision) {
  ap_m68882_op_t out = {0};
  ap_m68882_extended_t rhs = *b;
  if (negate_b) {
    rhs.sign = !rhs.sign;
  }

  if (propagate_nan(a, &rhs, &out)) {
    return out;
  }

  const ap_m68882_type_t a_kind = ap_m68882_classify(a);
  const ap_m68882_type_t b_kind = ap_m68882_classify(&rhs);

  if (a_kind == AP_M68882_TYPE_INFINITY || b_kind == AP_M68882_TYPE_INFINITY) {
    if (a_kind == AP_M68882_TYPE_INFINITY &&
        b_kind == AP_M68882_TYPE_INFINITY) {
      /* Table 6-2: "(+infinity) + (-infinity) or (-infinity) + (+infinity)" is
       * the operand error. Two infinities of the *same* sign are not -- they
       * are that infinity -- which is the distinction a blanket
       * infinity-plus-infinity rule loses. */
      if (a->sign != rhs.sign) {
        return operand_error();
      }
      out.value = make_infinity(a->sign);
      return out;
    }
    out.value = make_infinity(a_kind == AP_M68882_TYPE_INFINITY ? a->sign
                                                               : rhs.sign);
    return out;
  }

  if (a_kind == AP_M68882_TYPE_ZERO && b_kind == AP_M68882_TYPE_ZERO) {
    /* Two zeros: the sign is positive unless both were negative, or the mode is
     * toward minus infinity. IEEE's rule, and it is why zero carries a sign at
     * all. */
    const bool sign = (a->sign && rhs.sign) ||
                      (mode == AP_M68882_ROUND_MINUS_INFINITY &&
                       (a->sign || rhs.sign));
    out.value = make_zero(sign);
    return out;
  }
  if (a_kind == AP_M68882_TYPE_ZERO) {
    out.value = rhs;
    return out;
  }
  if (b_kind == AP_M68882_TYPE_ZERO) {
    out.value = *a;
    return out;
  }

  return add_magnitudes(*a, rhs, a->sign != rhs.sign, mode, precision);
}

ap_m68882_op_t ap_m68882_add(const ap_m68882_extended_t *a,
                             const ap_m68882_extended_t *b,
                             ap_m68882_rounding_t mode,
                             ap_m68882_precision_t precision) {
  return add_or_subtract(a, b, false, mode, precision);
}

ap_m68882_op_t ap_m68882_sub(const ap_m68882_extended_t *a,
                             const ap_m68882_extended_t *b,
                             ap_m68882_rounding_t mode,
                             ap_m68882_precision_t precision) {
  /* Subtraction is addition of the negated operand, which is what makes
   * Table 6-2's `FSUB` row -- "source and floating-point data register are
   * +infinity or source and FPn are -infinity" -- the same condition as
   * `FADD`'s once the sign is flipped. */
  return add_or_subtract(a, b, true, mode, precision);
}

/* 64x64 -> 128, in halves. Written out rather than using a 128-bit type,
 * because this core is C23 on three platforms and `unsigned __int128` is a
 * compiler extension -- and the emulated result must be identical on all of
 * them. */
static void multiply_64(uint64_t x, uint64_t y, uint64_t *high, uint64_t *low) {
  const uint64_t x_low = x & UINT64_C(0xFFFFFFFF);
  const uint64_t x_high = x >> 32;
  const uint64_t y_low = y & UINT64_C(0xFFFFFFFF);
  const uint64_t y_high = y >> 32;

  const uint64_t low_low = x_low * y_low;
  const uint64_t cross_a = x_high * y_low;
  const uint64_t cross_b = x_low * y_high;
  const uint64_t high_high = x_high * y_high;

  const uint64_t middle = (low_low >> 32) + (cross_a & UINT64_C(0xFFFFFFFF)) +
                          (cross_b & UINT64_C(0xFFFFFFFF));
  *low = (low_low & UINT64_C(0xFFFFFFFF)) | (middle << 32);
  *high = high_high + (cross_a >> 32) + (cross_b >> 32) + (middle >> 32);
}

ap_m68882_op_t ap_m68882_mul(const ap_m68882_extended_t *a,
                             const ap_m68882_extended_t *b,
                             ap_m68882_rounding_t mode,
                             ap_m68882_precision_t precision) {
  ap_m68882_op_t out = {0};
  if (propagate_nan(a, b, &out)) {
    return out;
  }

  const ap_m68882_type_t a_kind = ap_m68882_classify(a);
  const ap_m68882_type_t b_kind = ap_m68882_classify(b);
  const bool sign = a->sign != b->sign;

  const bool a_zero = a_kind == AP_M68882_TYPE_ZERO;
  const bool b_zero = b_kind == AP_M68882_TYPE_ZERO;
  const bool a_infinite = a_kind == AP_M68882_TYPE_INFINITY;
  const bool b_infinite = b_kind == AP_M68882_TYPE_INFINITY;

  if ((a_zero && b_infinite) || (a_infinite && b_zero)) {
    /* Table 6-2: "One Operand is 0, Other Operand is +/-infinity". */
    return operand_error();
  }
  if (a_infinite || b_infinite) {
    out.value = make_infinity(sign);
    return out;
  }
  if (a_zero || b_zero) {
    out.value = make_zero(sign);
    return out;
  }

  uint64_t high = 0;
  uint64_t low = 0;
  multiply_64(a->mantissa, b->mantissa, &high, &low);

  /* Two mantissas in [1,2) give a product in [1,4), so the result is either
   * already normalised in `high` or needs one shift. Exponents add, and the
   * bias is added twice by that addition so one copy comes back off. */
  ap_m68882_extended_t result = {.sign = sign};
  /* Both mantissas are in [1,2), so the product is in [1,4) and lands one bit
   * higher than a single mantissa. The `+1` is that bit: with the product's own
   * integer bit set the value is in [2,4) and the exponent is one *above* the
   * sum, and the shift below takes it back when the value is in [1,2).
   *
   * Getting this backwards puts every product out by a factor of two -- 3 * 4
   * comes to 6 -- which is the error that made this comment necessary. */
  int exponent =
      (int)a->exponent + (int)b->exponent - AP_M68882_BIAS_EXTENDED + 1;

  bool guard = (low & (UINT64_C(1) << 63)) != 0u;
  bool round_bit = (low & (UINT64_C(1) << 62)) != 0u;
  bool sticky = (low & ((UINT64_C(1) << 62) - 1u)) != 0u;

  if ((high & INTEGER_BIT) == 0u) {
    high = (high << 1) | (guard ? 1u : 0u);
    guard = round_bit;
    round_bit = sticky;
    sticky = sticky || (low & ((UINT64_C(1) << 62) - 1u)) != 0u;
    exponent -= 1;
  }

  if (exponent < 0) {
    /* Below the representable range: shift the mantissa down until the exponent
     * reaches its minimum, which is gradual underflow rather than flush to
     * zero -- "the result mantissa is shifted right (denormalized) while the
     * result exponent is incremented until the result exponent reaches the
     * minimum value".
     *
     * The test is `< 0` and the shift `-exponent`, and both halves matter.
     * Exponent zero is a *legal* extended exponent, not the first one below the
     * range: §6.1.5's own footnote says "underflow is NOT detected for
     * intermediate result exponents that are equal to the extended precision
     * minimum exponent, since the explicit integer part bit of extended
     * precision permits representation of normalized numbers with a minimum
     * exponent". Treating it as underflow shifted every such result down one
     * more place -- so a denormal multiplied by one came back halved, and the
     * smallest denormal came back as zero.
     *
     * `finish` still reports the genuine denormals: a result that lands at
     * exponent zero with its integer bit clear is denormalized and raises
     * `UNFL` there, which is where that decision belongs. */
    shift_right_sticky(&high, (unsigned)(-exponent), &guard, &round_bit,
                       &sticky);
    exponent = 0;
    out.exceptions |= UINT32_C(1) << AP_M68882_EXC_UNFL;
  }
  result.exponent = (uint16_t)(exponent > (int)MAX_EXPONENT ? (int)MAX_EXPONENT
                                                            : exponent);
  result.mantissa = high;
  return finish(result, guard, round_bit, sticky, mode, precision,
                out.exceptions);
}

ap_m68882_op_t ap_m68882_div(const ap_m68882_extended_t *a,
                             const ap_m68882_extended_t *b,
                             ap_m68882_rounding_t mode,
                             ap_m68882_precision_t precision) {
  ap_m68882_op_t out = {0};
  if (propagate_nan(a, b, &out)) {
    return out;
  }

  const ap_m68882_type_t a_kind = ap_m68882_classify(a);
  const ap_m68882_type_t b_kind = ap_m68882_classify(b);
  const bool sign = a->sign != b->sign;

  if (a_kind == AP_M68882_TYPE_INFINITY && b_kind == AP_M68882_TYPE_INFINITY) {
    return operand_error(); /* Table 6-2: "infinity/infinity" */
  }
  if (a_kind == AP_M68882_TYPE_ZERO && b_kind == AP_M68882_TYPE_ZERO) {
    return operand_error(); /* Table 6-2: "0/0" */
  }
  if (b_kind == AP_M68882_TYPE_ZERO) {
    /* Not an operand error: §6.1.4's divide by zero, whose result is an
     * infinity of the correct sign. `DZ` and `OPERR` are different exceptions
     * with different vectors, and 1/0 is the one that is *defined*. */
    out.value = make_infinity(sign);
    out.exceptions = UINT32_C(1) << AP_M68882_EXC_DZ;
    return out;
  }
  if (a_kind == AP_M68882_TYPE_INFINITY) {
    out.value = make_infinity(sign);
    return out;
  }
  if (b_kind == AP_M68882_TYPE_INFINITY || a_kind == AP_M68882_TYPE_ZERO) {
    out.value = make_zero(sign);
    return out;
  }

  /* Long division, one bit at a time, into a 64-bit quotient plus guard, round
   * and a sticky built from whether any remainder is left. Bitwise rather than
   * by a wider type for the same portability reason as the multiply.
   *
   * Both mantissas are in [1,2), so their quotient is in [0.5,2) and the
   * leading one lands in one of two places. The two cases are therefore
   * *different lengths of division*, not merely different exponents:
   *
   *   dividend >= divisor: the first quotient bit is a one. Store it, then take
   *   63 more, and the leading one is at bit 63 with the exponent unchanged.
   *
   *   dividend < divisor: the first quotient bit is a zero, which carries no
   *   information and is not stored. The exponent drops by one to account for
   *   it, and the division runs **64** times rather than 63 so that the leading
   *   one still ends at bit 63.
   *
   * Adjusting the exponent without lengthening the division is the trap: it
   * gives a quotient whose leading one sits at bit 62 *and* an exponent already
   * reduced, so the answer comes out exactly half. That is what `2/3` and
   * `0.25/2.25` did, while `x/x` and `4/2` -- where the dividend is not the
   * smaller -- stayed correct and hid it. */
  uint64_t remainder = a->mantissa;
  uint64_t quotient = 0;
  unsigned iterations = 63u;
  int exponent = (int)a->exponent - (int)b->exponent + AP_M68882_BIAS_EXTENDED;
  if (remainder < b->mantissa) {
    exponent -= 1;
    iterations = 64u;
  } else {
    /* Halve the dividend rather than doubling it, since doubling would leave
     * the 64-bit register. The bit that falls off is always zero here: the
     * mantissa's low bit only matters below the quotient's last, where sticky
     * already accounts for it. */
    remainder -= b->mantissa;
    quotient = 1u;
  }

  /* A left shift of the remainder can lose its own top bit, so that bit is
   * carried explicitly -- losing it silently makes the comparison wrong for
   * exactly the operands whose remainder has grown large, which is most of
   * them. */
  for (unsigned i = 0; i < iterations; i++) {
    const bool carry = (remainder & (UINT64_C(1) << 63)) != 0u;
    remainder <<= 1;
    quotient <<= 1;
    if (carry || remainder >= b->mantissa) {
      remainder -= b->mantissa;
      quotient |= 1u;
    }
  }

  /* Two more bits, taken *without* shifting the quotient -- there is no room
   * left in it, and these are guard and round rather than mantissa. */
  bool guard = false;
  bool round_bit = false;
  for (unsigned i = 0; i < 2u; i++) {
    const bool carry = (remainder & (UINT64_C(1) << 63)) != 0u;
    remainder <<= 1;
    bool bit = false;
    if (carry || remainder >= b->mantissa) {
      remainder -= b->mantissa;
      bit = true;
    }
    if (i == 0u) {
      guard = bit;
    } else {
      round_bit = bit;
    }
  }
  const bool sticky = remainder != 0u;

  ap_m68882_extended_t result = {.sign = sign};
  if (exponent < 0) {
    /* The same rule as the multiply, and for the same reason: exponent zero is
     * a legal extended exponent, so only a *negative* one needs denormalising
     * and the shift is `-exponent` rather than one more. See the multiply for
     * the footnote that settles it. */
    bool g = guard;
    bool r = round_bit;
    bool s = sticky;
    shift_right_sticky(&quotient, (unsigned)(-exponent), &g, &r, &s);
    result.exponent = 0u;
    result.mantissa = quotient;
    out.exceptions |= UINT32_C(1) << AP_M68882_EXC_UNFL;
    return finish(result, g, r, s, mode, precision, out.exceptions);
  }
  result.exponent = (uint16_t)(exponent > (int)MAX_EXPONENT ? (int)MAX_EXPONENT
                                                            : exponent);
  result.mantissa = quotient;
  return finish(result, guard, round_bit, sticky, mode, precision, 0u);
}

ap_m68882_compare_t ap_m68882_compare(const ap_m68882_extended_t *a,
                                      const ap_m68882_extended_t *b) {
  ap_m68882_compare_t out = {0};

  const ap_m68882_type_t a_kind = ap_m68882_classify(a);
  const ap_m68882_type_t b_kind = ap_m68882_classify(b);

  if (a_kind == AP_M68882_TYPE_NAN || b_kind == AP_M68882_TYPE_NAN) {
    /* "An unordered condition occurs when one or both of the operands in a
     * floating-point compare operation is a NAN." Unordered is its own answer
     * and not "neither less nor greater nor equal" -- a conditional branch
     * distinguishes them, which is what `BSUN` exists for. */
    out.unordered = true;
    if (ap_m68882_is_signalling_nan(a) || ap_m68882_is_signalling_nan(b)) {
      out.exceptions = UINT32_C(1) << AP_M68882_EXC_SNAN;
    }
    return out;
  }

  if (a_kind == AP_M68882_TYPE_ZERO && b_kind == AP_M68882_TYPE_ZERO) {
    /* **+0 equals -0**, which is the one place the sign of a zero does *not*
     * matter. A comparison that took the sign into account would report them
     * unequal, and every loop testing a computed zero would take the wrong
     * branch half the time. */
    out.equal = true;
    return out;
  }

  if (a->sign != b->sign) {
    out.less = a->sign;
    return out;
  }

  /* Same sign: compare magnitudes, then flip the sense if both are negative.
   * Because the exponent is biased and sits above the mantissa, one unsigned
   * comparison of the pair serves -- which is the property the manual points at
   * when it says "a program can execute an integer compare instruction (CMP) to
   * compare floating-point numbers in memory". */
  if (a->exponent != b->exponent) {
    out.less = (a->exponent < b->exponent) != a->sign;
    return out;
  }
  if (a->mantissa != b->mantissa) {
    out.less = (a->mantissa < b->mantissa) != a->sign;
    return out;
  }
  out.equal = true;
  return out;
}

/* ---------------------------------------------------------------------------
 * The exactly-specified monadic operations. See the header for why these are
 * not transcendentals.
 * ------------------------------------------------------------------------- */

/* A 128-bit value as two halves. Written out rather than using
 * `unsigned __int128`, which is a compiler extension: this core is C23 on three
 * platforms and the emulated result must be identical on all of them. */
typedef struct {
  uint64_t high;
  uint64_t low;
} ap_m68882_u128_t;

static bool u128_at_least(ap_m68882_u128_t a, ap_m68882_u128_t b) {
  return a.high != b.high ? a.high > b.high : a.low >= b.low;
}

static ap_m68882_u128_t u128_subtract(ap_m68882_u128_t a,
                                      ap_m68882_u128_t b) {
  ap_m68882_u128_t out = {.high = a.high - b.high, .low = a.low - b.low};
  if (a.low < b.low) {
    out.high -= 1u;
  }
  return out;
}

static ap_m68882_u128_t u128_shift_left(ap_m68882_u128_t a, unsigned count) {
  if (count == 0u) {
    return a;
  }
  if (count >= 64u) {
    return (ap_m68882_u128_t){.high = a.low << (count - 64u), .low = 0u};
  }
  return (ap_m68882_u128_t){
      .high = (a.high << count) | (a.low >> (64u - count)),
      .low = a.low << count};
}

static ap_m68882_u128_t u128_bit(unsigned position) {
  return position >= 64u
             ? (ap_m68882_u128_t){.high = UINT64_C(1) << (position - 64u),
                                  .low = 0u}
             : (ap_m68882_u128_t){.high = 0u, .low = UINT64_C(1) << position};
}

static ap_m68882_u128_t u128_add(ap_m68882_u128_t a, ap_m68882_u128_t b) {
  ap_m68882_u128_t out = {.high = a.high + b.high, .low = a.low + b.low};
  if (out.low < a.low) {
    out.high += 1u;
  }
  return out;
}

ap_m68882_op_t ap_m68882_sqrt(const ap_m68882_extended_t *a,
                              ap_m68882_rounding_t mode,
                              ap_m68882_precision_t precision) {
  ap_m68882_op_t out = {0};
  const ap_m68882_extended_t self = *a;
  if (propagate_nan(a, &self, &out)) {
    return out;
  }

  const ap_m68882_type_t kind = ap_m68882_classify(a);
  if (kind == AP_M68882_TYPE_ZERO) {
    /* "sqrt(-0) = -0", IEEE's rule and not an oversight: a zero's sign is
     * information and the square root preserves it. Checked *before* the sign
     * test below, or negative zero would be an operand error. */
    out.value = *a;
    return out;
  }
  if (a->sign) {
    return operand_error(); /* Table 6-2: "FSQRT: Source <0" */
  }
  if (kind == AP_M68882_TYPE_INFINITY) {
    out.value = *a;
    return out;
  }

  /* The mantissa is `M / 2^63` in [1,2) and the value is `m * 2^e`.
   *
   *   e even:  sqrt = sqrt(m)  * 2^(e/2),     sqrt(m)  in [1, 1.415)
   *   e odd:   sqrt = sqrt(2m) * 2^((e-1)/2), sqrt(2m) in [1.415, 2)
   *
   * so the root's mantissa is normalised either way, and the exponent is
   * `floor(e/2)` in both -- which an arithmetic shift gives directly. Halving
   * the mantissa instead, as this first did, leaves it in [0.5,1) and produces
   * a root a factor of two out: `sqrt(9)` came to 6. */
  const int exponent = (int)a->exponent - AP_M68882_BIAS_EXTENDED;
  const bool odd = (exponent & 1) != 0;
  ap_m68882_u128_t radicand = {.high = 0u, .low = a->mantissa};
  radicand = u128_shift_left(radicand, odd ? 64u : 63u);

  /* Restoring square root, one bit at a time from the top. At each step, test
   * whether setting bit `b` keeps the root's square within the radicand:
   * `(root + 2^b)^2 - root^2` is `2*root*2^b + 2^2b`, which is the trial
   * below. */
  uint64_t root = 0;
  ap_m68882_u128_t remainder = radicand;
  for (unsigned b = 64u; b-- > 0u;) {
    ap_m68882_u128_t trial =
        u128_shift_left((ap_m68882_u128_t){.high = 0u, .low = root}, b + 1u);
    trial = u128_add(trial, u128_bit(2u * b));
    if (u128_at_least(remainder, trial)) {
      remainder = u128_subtract(remainder, trial);
      root |= UINT64_C(1) << b;
    }
  }

  ap_m68882_extended_t result = {
      .sign = false,
      .exponent = (uint16_t)((exponent >> 1) + AP_M68882_BIAS_EXTENDED),
      .mantissa = root};
  /* Anything left over means the root was not exact. There is no guard or round
   * bit to give: the loop produced all 64 mantissa bits and stopped, so what
   * remains is entirely below them -- which is what sticky means. */
  const bool inexact = remainder.high != 0u || remainder.low != 0u;
  return finish(result, false, false, inexact, mode, precision, 0u);
}

ap_m68882_op_t ap_m68882_getexp(const ap_m68882_extended_t *a) {
  ap_m68882_op_t out = {0};
  const ap_m68882_extended_t self = *a;
  if (propagate_nan(a, &self, &out)) {
    return out;
  }

  const ap_m68882_type_t kind = ap_m68882_classify(a);
  if (kind == AP_M68882_TYPE_ZERO) {
    /* The operation table gives `+0.0` for a positive zero and `-0.0` for a
     * negative one: the sign is carried through rather than the exponent being
     * computed. */
    out.value = *a;
    return out;
  }
  if (kind == AP_M68882_TYPE_INFINITY) {
    /* Table 6-2: "FGETEXP: Source is +/- infinity". */
    return operand_error();
  }

  int32_t unbiased = (int32_t)a->exponent - AP_M68882_BIAS_EXTENDED;
  if (unbiased == 0) {
    out.value = make_zero(false);
    return out;
  }

  /* "converts the exponent to an extended precision floating-point number" --
   * so the answer is a float holding an integer, not an integer. */
  ap_m68882_extended_t result = {.sign = unbiased < 0};
  uint32_t magnitude = (uint32_t)(unbiased < 0 ? -unbiased : unbiased);
  unsigned shift = 0;
  while ((magnitude >> shift) > 1u) {
    shift++;
  }
  result.exponent = (uint16_t)(AP_M68882_BIAS_EXTENDED + shift);
  result.mantissa = (uint64_t)magnitude << (63u - shift);
  out.value = result;
  return out;
}

ap_m68882_op_t ap_m68882_getman(const ap_m68882_extended_t *a) {
  ap_m68882_op_t out = {0};
  const ap_m68882_extended_t self = *a;
  if (propagate_nan(a, &self, &out)) {
    return out;
  }

  const ap_m68882_type_t kind = ap_m68882_classify(a);
  if (kind == AP_M68882_TYPE_ZERO) {
    out.value = *a;
    return out;
  }
  if (kind == AP_M68882_TYPE_INFINITY) {
    return operand_error(); /* Table 6-2: "FGETMAN: Source is +/- infinity" */
  }

  /* "The result is in the range [1.0 ... 2.0) **with the sign of the source
   * mantissa**" -- the sign is kept, which is what makes this the mantissa
   * rather than its magnitude. */
  out.value.sign = a->sign;
  out.value.exponent = AP_M68882_BIAS_EXTENDED;
  out.value.mantissa = a->mantissa;
  return out;
}

/* Truncate a value to its integer part, reporting the bits discarded so the
 * caller can round them. Shared by FINT and FINTRZ, which differ only in
 * whether the mode is consulted. */
static ap_m68882_op_t integer_part(const ap_m68882_extended_t *a,
                                   ap_m68882_rounding_t mode, bool truncate) {
  ap_m68882_op_t out = {0};
  const ap_m68882_extended_t self = *a;
  if (propagate_nan(a, &self, &out)) {
    return out;
  }

  const ap_m68882_type_t kind = ap_m68882_classify(a);
  if (kind == AP_M68882_TYPE_ZERO || kind == AP_M68882_TYPE_INFINITY) {
    /* Both are already integers, and an infinity is *not* an operand error
     * here -- Table 6-2 does not list FINT, and the operation table gives the
     * infinity straight back. */
    out.value = *a;
    return out;
  }

  const int exponent = (int)a->exponent - AP_M68882_BIAS_EXTENDED;
  if (exponent >= 63) {
    out.value = *a; /* already an integer: no fraction bits remain */
    return out;
  }
  if (exponent < 0) {
    /* Below one in magnitude: the integer part is zero before rounding, and
     * rounding can only lift it to one. Handled through the same path so the
     * directed modes behave. */
    const uint64_t fraction = a->mantissa;
    ap_m68882_extended_t zero = make_zero(a->sign);
    if (truncate || fraction == 0u) {
      out.value = zero;
      return out;
    }
    const bool up = (mode == AP_M68882_ROUND_PLUS_INFINITY && !a->sign) ||
                    (mode == AP_M68882_ROUND_MINUS_INFINITY && a->sign) ||
                    (mode == AP_M68882_ROUND_NEAREST && exponent == -1 &&
                     (a->mantissa & ~INTEGER_BIT) != 0u);
    out.value = zero;
    if (up) {
      out.value.exponent = AP_M68882_BIAS_EXTENDED;
      out.value.mantissa = INTEGER_BIT;
    }
    return out;
  }

  const unsigned fraction_bits = (unsigned)(63 - exponent);
  const uint64_t fraction_mask = (UINT64_C(1) << fraction_bits) - 1u;
  const uint64_t fraction = a->mantissa & fraction_mask;
  if (fraction == 0u) {
    out.value = *a;
    return out;
  }

  ap_m68882_extended_t result = *a;
  result.mantissa = a->mantissa & ~fraction_mask;

  if (!truncate) {
    /* FINT rounds by the current mode, so it reuses the rounding stage rather
     * than reimplementing round-half-to-even -- which is the whole reason that
     * stage is a module. The guard is the fraction's top bit and the sticky is
     * everything below it. */
    const bool guard =
        (fraction & (UINT64_C(1) << (fraction_bits - 1u))) != 0u;
    const bool sticky =
        (fraction & ((UINT64_C(1) << (fraction_bits - 1u)) - 1u)) != 0u;
    ap_m68882_extended_t shifted = result;
    shifted.mantissa >>= fraction_bits;
    const ap_m68882_round_result_t rounded =
        ap_m68882_round(shifted, guard, false, sticky, mode,
                        AP_M68882_PRECISION_EXTENDED);
    result.mantissa = rounded.value.mantissa << fraction_bits;
    if (rounded.value.exponent != shifted.exponent) {
      /* The rounding carried, so the value grew a bit. */
      result.exponent = (uint16_t)(result.exponent + 1u);
      result.mantissa = INTEGER_BIT;
    }
  }

  out.value = result;
  return out;
}

ap_m68882_op_t ap_m68882_int(const ap_m68882_extended_t *a,
                             ap_m68882_rounding_t mode) {
  return integer_part(a, mode, false);
}

ap_m68882_op_t ap_m68882_intrz(const ap_m68882_extended_t *a) {
  /* "Integer Part (Truncated)": always toward zero, whatever the mode says.
   * The mode argument is unused deliberately -- passing the current one would
   * make this the same instruction as FINT. */
  return integer_part(a, AP_M68882_ROUND_ZERO, true);
}

ap_m68882_op_t ap_m68882_scale(const ap_m68882_extended_t *a,
                               const ap_m68882_extended_t *b) {
  ap_m68882_op_t out = {0};
  if (propagate_nan(a, b, &out)) {
    return out;
  }

  const ap_m68882_type_t a_kind = ap_m68882_classify(a);
  const ap_m68882_type_t b_kind = ap_m68882_classify(b);

  if (b_kind == AP_M68882_TYPE_INFINITY) {
    /* Table 6-2: "FSCALE: Source is +/- infinity, Other Operand is Not a
     * NAN" -- the NAN case having been taken above. */
    return operand_error();
  }
  if (a_kind == AP_M68882_TYPE_ZERO || a_kind == AP_M68882_TYPE_INFINITY) {
    out.value = *a; /* scaling either leaves it what it was */
    return out;
  }

  /* "Converts the source operand to an integer ... and adds that integer to the
   * exponent". Exponent arithmetic, so it is exact and cannot round -- which is
   * the point of the instruction rather than multiplying by a power of two. */
  const ap_m68882_op_t truncated = ap_m68882_intrz(b);
  int32_t amount = 0;
  const int b_exponent = (int)truncated.value.exponent - AP_M68882_BIAS_EXTENDED;
  if (ap_m68882_classify(&truncated.value) != AP_M68882_TYPE_ZERO &&
      b_exponent >= 0 && b_exponent < 31) {
    amount = (int32_t)(truncated.value.mantissa >> (63 - b_exponent));
    if (truncated.value.sign) {
      amount = -amount;
    }
  }

  const int scaled = (int)a->exponent + amount;
  ap_m68882_extended_t result = *a;
  if (scaled >= (int)MAX_EXPONENT) {
    out.value = make_infinity(a->sign);
    out.exceptions = (UINT32_C(1) << AP_M68882_EXC_OVFL) |
                     (UINT32_C(1) << AP_M68882_EXC_INEX2);
    return out;
  }
  if (scaled <= 0) {
    out.value = make_zero(a->sign);
    out.exceptions = UINT32_C(1) << AP_M68882_EXC_UNFL;
    return out;
  }
  result.exponent = (uint16_t)scaled;
  out.value = result;
  return out;
}

/* Split a finite non-zero value into a normalised significand and an unbiased
 * exponent, so a denormal and a normal are handled by one loop. */
static void decompose(const ap_m68882_extended_t *v, uint64_t *mantissa,
                      int *exponent) {
  uint64_t m = v->mantissa;
  int e = (int)v->exponent - AP_M68882_BIAS_EXTENDED;
  while ((m & INTEGER_BIT) == 0u) {
    m <<= 1;
    e--;
  }
  *mantissa = m;
  *exponent = e;
}

/* Build an extended value from a significand and an unbiased exponent,
 * denormalising if the exponent has fallen below the format's minimum. */
static ap_m68882_extended_t compose(bool sign, uint64_t mantissa,
                                    int exponent) {
  if (mantissa == 0u) {
    return make_zero(sign);
  }
  while ((mantissa & INTEGER_BIT) == 0u) {
    mantissa <<= 1;
    exponent--;
  }
  int biased = exponent + AP_M68882_BIAS_EXTENDED;
  if (biased < 0) {
    const unsigned shift = (unsigned)(-biased);
    mantissa = shift >= 64u ? 0u : (mantissa >> shift);
    biased = 0;
  }
  return (ap_m68882_extended_t){sign, (uint16_t)biased, mantissa};
}

ap_m68882_remainder_t
ap_m68882_remainder(const ap_m68882_extended_t *destination,
                    const ap_m68882_extended_t *source,
                    bool round_to_nearest) {
  ap_m68882_remainder_t out = {0};
  /* The quotient's sign is the operands' signs exclusive-ORed, whatever the
   * remainder turns out to be. */
  out.quotient_sign = destination->sign != source->sign;

  ap_m68882_op_t nan_out = {0};
  if (propagate_nan(destination, source, &nan_out)) {
    out.value = nan_out.value;
    out.exceptions = nan_out.exceptions;
    return out;
  }

  const ap_m68882_type_t a_kind = ap_m68882_classify(destination);
  const ap_m68882_type_t b_kind = ap_m68882_classify(source);

  /* "Set if the source is zero, or the destination is infinity" -- the two
   * cases the function is not defined for, and the operation table's whole
   * bottom row and middle column. */
  if (b_kind == AP_M68882_TYPE_ZERO || a_kind == AP_M68882_TYPE_INFINITY) {
    out.value = make_nan();
    out.exceptions = UINT32_C(1) << AP_M68882_EXC_OPERR;
    return out;
  }
  if (a_kind == AP_M68882_TYPE_ZERO) {
    /* "+0.0 / -0.0": a zero dividend keeps its sign through both a finite and
     * an infinite modulus. */
    out.value = make_zero(destination->sign);
    return out;
  }
  if (b_kind == AP_M68882_TYPE_INFINITY) {
    /* Note 2: "returns the value of FPn before the operation" -- nothing is
     * taken away, because no multiple of an infinity fits inside a finite
     * number. The quotient is zero, and so is its recorded value. */
    out.value = *destination;
    return out;
  }

  uint64_t ma, mb;
  int ea, eb;
  decompose(destination, &ma, &ea);
  decompose(source, &mb, &eb);
  const int difference = ea - eb;

  uint64_t remainder;
  int remainder_exponent;
  uint64_t quotient = 0u;

  if (difference < 0) {
    /* The divisor is the larger, so the truncated quotient is zero and the
     * remainder is the dividend untouched. */
    remainder = ma;
    remainder_exponent = ea;
  } else {
    /* Restoring long division on the significands, which is what makes this
     * exact: every step is a subtraction of the divisor from a shifted
     * remainder, and no product is ever formed. The carry is the 65th bit a
     * doubled remainder needs -- dropping it makes the comparison wrong for
     * exactly the operands whose remainder has grown large. */
    remainder = ma;
    remainder_exponent = eb;
    for (int i = 0; i <= difference; i++) {
      bool carry = false;
      if (i > 0) {
        carry = (remainder & INTEGER_BIT) != 0u;
        remainder <<= 1;
      }
      quotient <<= 1;
      if (carry || remainder >= mb) {
        remainder -= mb;
        quotient |= 1u;
      }
    }
  }

  bool sign = destination->sign;
  if (round_to_nearest) {
    /* `FREM` rounds the implied quotient to nearest, so the remainder may be
     * the *negative* one: subtract the divisor once more when the remainder is
     * more than half of it, or exactly half with an odd quotient -- the same
     * round-half-to-even the arithmetic uses, applied to N rather than to a
     * significand. */
    bool take_one_more;
    if (difference < 0) {
      /* Compare 2|a| against |b| without forming either. */
      const int doubled = ea + 1;
      take_one_more = doubled > eb || (doubled == eb && ma > mb) ||
                      (doubled == eb && ma == mb && (quotient & 1u) != 0u);
    } else {
      const uint64_t twice = remainder << 1;
      const bool overflowed = (remainder & INTEGER_BIT) != 0u;
      take_one_more = overflowed || twice > mb ||
                      (twice == mb && (quotient & 1u) != 0u);
    }
    if (take_one_more) {
      /* The remainder becomes `|a| - (N+1)|b|`, which is negative, so its
       * magnitude is `|b| - r` and its sign is the dividend's flipped. */
      if (difference < 0) {
        /* Align the dividend to the divisor's exponent to subtract. */
        const unsigned shift = (unsigned)(eb - ea);
        const uint64_t aligned = shift >= 64u ? 0u : (ma >> shift);
        remainder = mb - aligned;
        remainder_exponent = eb;
      } else {
        remainder = mb - remainder;
      }
      sign = !sign;
      quotient += 1u;
    }
  }

  out.quotient = (unsigned)(quotient & 0x7Fu);
  /* The significand sits at the divisor's exponent minus the 63 places the
   * integer bit is above the least significant one. */
  out.value = compose(sign, remainder, remainder_exponent);
  if (out.value.mantissa == 0u) {
    /* An exact division leaves a zero, and IEEE gives it the dividend's sign
     * rather than the adjusted one. */
    out.value = make_zero(destination->sign);
  }
  return out;
}
