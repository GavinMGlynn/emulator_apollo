/* MC68882 arithmetic. See ap_m68882_arith.h for why the special cases are the
 * specification and what Table 6-2 makes an error rather than a value. */

#include "cpu/m68882/ap_m68882_arith.h"

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

  const ap_m68882_round_result_t rounded =
      ap_m68882_round(value, guard, round_bit, sticky, mode, precision);
  out.value = rounded.value;
  if (rounded.inexact) {
    out.exceptions |= UINT32_C(1) << AP_M68882_EXC_INEX2;
  }

  if (out.value.exponent >= MAX_EXPONENT) {
    /* "Overflow is detected ... when the result exponent is greater than the
     * maximum". The result is an infinity, and `INEX2` comes with it since an
     * overflowed result is not the exact one -- which is also why
     * `AEXC(INEX)` is set by `OVFL`. */
    out.value = make_infinity(out.value.sign);
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

  if (exponent <= 0) {
    /* Underflow past the representable range: shift the mantissa down to the
     * minimum exponent, which is gradual underflow rather than flush to zero. */
    shift_right_sticky(&high, (unsigned)(1 - exponent), &guard, &round_bit,
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
   * The dividend is normalised against the divisor **first**, so the quotient's
   * leading one always lands at bit 63. Both mantissas are in [1,2), so their
   * quotient is in [0.5,2) -- and without this the [0.5,1) half would produce a
   * quotient one bit short and a value half what it should be. */
  uint64_t remainder = a->mantissa;
  uint64_t quotient = 0;
  int exponent = (int)a->exponent - (int)b->exponent + AP_M68882_BIAS_EXTENDED;
  if (remainder < b->mantissa) {
    exponent -= 1;
  } else {
    /* Halve the dividend rather than doubling it, since doubling would leave
     * the 64-bit register. The bit that falls off is always zero here: the
     * mantissa's low bit only matters below the quotient's last, where sticky
     * already accounts for it. */
    remainder -= b->mantissa;
    quotient = 1u;
  }

  /* 63 more bits complete the 64-bit mantissa, its leading one already at the
   * top by construction. A left shift of the remainder can lose its own top
   * bit, so that bit is carried explicitly -- losing it silently makes the
   * comparison wrong for exactly the operands whose remainder has grown large,
   * which is most of them. */
  for (unsigned i = 0; i < 63u; i++) {
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
  if (exponent <= 0) {
    bool g = guard;
    bool r = round_bit;
    bool s = sticky;
    shift_right_sticky(&quotient, (unsigned)(1 - exponent), &g, &r, &s);
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
