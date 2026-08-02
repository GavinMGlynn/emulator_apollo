/* MC68882 rounding, Figure 6-3 transcribed. See ap_m68882_round.h for why the
 * three extra bits are all needed and why precision is a parameter. */

#include "cpu/m68882/ap_m68882_round.h"

unsigned ap_m68882_precision_bits(ap_m68882_precision_t precision) {
  switch (precision) {
  case AP_M68882_PRECISION_SINGLE:
    return 24u; /* "a single precision result is rounded to a 24-bit boundary" */
  case AP_M68882_PRECISION_DOUBLE:
    return 53u; /* "a double precision result ... to a 53-bit boundary" */
  case AP_M68882_PRECISION_EXTENDED:
  case AP_M68882_PRECISION_RESERVED:
    break;
  }
  /* "For extended precision, the result is rounded to a 64-bit boundary." The
   * reserved encoding lands here deliberately: it has to do something
   * deterministic, and keeping every bit is the choice that cannot silently
   * discard one. */
  return 64u;
}

ap_m68882_round_result_t ap_m68882_round(ap_m68882_extended_t value, bool guard,
                                         bool round_bit, bool sticky,
                                         ap_m68882_rounding_t mode,
                                         ap_m68882_precision_t precision) {
  ap_m68882_round_result_t out = {.value = value, .inexact = false};

  const unsigned keep = ap_m68882_precision_bits(precision);
  if (keep < 64u) {
    /* Range control: the boundary moves up the mantissa, and the bits below it
     * **fold into** guard, round and sticky rather than being discarded. That
     * is what makes a single-precision result rounded *once* from the full
     * intermediate instead of twice -- rounding to extended and then to single
     * is a different answer for values near a tie. */
    const unsigned drop = 64u - keep;
    const uint64_t below = value.mantissa & ((UINT64_C(1) << drop) - 1u);

    const bool new_guard = (below & (UINT64_C(1) << (drop - 1u))) != 0u;
    const bool new_round =
        drop >= 2u && (below & (UINT64_C(1) << (drop - 2u))) != 0u;
    /* Everything below the new round bit, plus whatever the caller already had
     * below the mantissa -- the sticky bit is "the logical OR of all the bits
     * in the infinitely precise result to the right of the round bit", and the
     * caller's three are all to the right of this one. */
    const uint64_t rest_mask =
        drop >= 2u ? ((UINT64_C(1) << (drop - 2u)) - 1u) : 0u;
    const bool new_sticky =
        (below & rest_mask) != 0u || guard || round_bit || sticky;

    value.mantissa &= ~((UINT64_C(1) << drop) - 1u);
    out.value = value;
    guard = new_guard;
    round_bit = new_round;
    sticky = new_sticky;
  }

  /* "IF GUARD, ROUND AND STICKY = 0 THEN (RESULT IS EXACT) DON'T SET INEX2,
   * DON'T CHANGE THE INTERMEDIATE RESULT". */
  if (!guard && !round_bit && !sticky) {
    return out;
  }
  out.inexact = true;

  const uint64_t lsb = UINT64_C(1) << (64u - keep);
  bool increment = false;

  switch (mode) {
  case AP_M68882_ROUND_MINUS_INFINITY:
    /* "RM: IF INTERMEDIATE RESULT IS NEGATIVE THEN ADD 1 TO LSB". The sign, not
     * the magnitude: rounding toward minus infinity makes a negative number
     * larger in magnitude and a positive one smaller. */
    increment = value.sign;
    break;

  case AP_M68882_ROUND_NEAREST:
    /* "RN: IF GUARD=1 AND ROUND AND STICKY=0 (TIE CASE) THEN IF LSB=1 ADD 1 TO
     * LSB; ELSE IF GUARD=1 ADD 1 TO LSB".
     *
     * Round half to **even**: an exact tie goes up only when doing so clears
     * the last bit. A model rounding every tie up is wrong half the time on
     * ties and biases a long summation, which is the whole reason the IEEE
     * standard specifies this case. */
    if (guard && !round_bit && !sticky) {
      increment = (out.value.mantissa & lsb) != 0u;
    } else {
      increment = guard;
    }
    break;

  case AP_M68882_ROUND_PLUS_INFINITY:
    /* "RP: IF INTERMEDIATE RESULT IS POSITIVE THEN ADD 1 TO LSB". */
    increment = !value.sign;
    break;

  case AP_M68882_ROUND_ZERO:
    /* "RZ: (FALL THROUGH; GUARD, ROUND AND STICKY ARE CHOPPED)". */
    break;
  }

  if (!increment) {
    return out;
  }

  const uint64_t before = out.value.mantissa;
  out.value.mantissa += lsb;

  /* "IF OVERFLOW = 1 THEN SHIFT MANTISSA RIGHT BY ONE BIT, ADD 1 TO THE
   * EXPONENT." The carry out of a 64-bit mantissa wraps to zero, so the
   * overflow is detected by the result being *smaller* -- and the integer bit
   * has to be put back, since the shift moves a one out of the top that the
   * carry conceptually put there. */
  if (out.value.mantissa < before) {
    out.value.mantissa = (out.value.mantissa >> 1) | (UINT64_C(1) << 63);
    out.value.exponent = (uint16_t)(out.value.exponent + 1u);
  }
  return out;
}
