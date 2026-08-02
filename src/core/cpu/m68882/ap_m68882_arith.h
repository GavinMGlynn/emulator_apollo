/* MC68882 arithmetic: add, subtract, multiply, divide and compare.
 *
 * `MC68881/MC68882 User's Manual` §4 for the operations, §6.1 for the
 * exceptions and Table 6-2 for the operand errors.
 *
 * ## The special cases are the specification
 *
 * The ordinary path -- align, operate, normalise, round -- is the easy half.
 * What makes a floating-point unit right or wrong is what it does with NANs,
 * infinities and zeros, and the manual enumerates those rather than leaving
 * them to the arithmetic. Table 6-2 lists the combinations that are *errors*:
 *
 *     FADD   (+infinity) + (-infinity), or the reverse
 *     FSUB   source and FPn both +infinity, or both -infinity
 *     FMUL   one operand is 0 and the other is +/-infinity
 *     FDIV   0/0 or infinity/infinity
 *
 * Every other infinity or zero combination has a defined *value* rather than an
 * error -- infinity plus one is infinity, one divided by zero is infinity with
 * `DZ` raised -- so a model that treated all of them as errors would trap where
 * the hardware computes, and a model that treated none of them as errors would
 * compute where the hardware traps.
 *
 * ## An operand error yields a non-signalling NAN
 *
 * Not zero and not an infinity: §6.1.3's trap-disabled result for a
 * floating-point destination is a NAN, which is what propagates the error
 * through a calculation instead of quietly participating in it.
 */

#ifndef APOLLO_CPU_M68882_AP_M68882_ARITH_H
#define APOLLO_CPU_M68882_AP_M68882_ARITH_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68882/ap_m68882_format.h"
#include "cpu/m68882/ap_m68882_round.h"

typedef struct {
  ap_m68882_extended_t value;
  /* A mask of `AP_M68882_EXC_*` bit positions. Returned rather than written
   * into an FPSR, so one operation's exceptions can be folded in once by the
   * caller -- which is what `ap_m68882_accrue` needs and what keeps this module
   * free of the register file. */
  uint32_t exceptions;
} ap_m68882_op_t;

/* §6.1.4's trap-disabled overflow result, which is **not** always an infinity.
 *
 *     RN   Infinity, with the sign of the intermediate result
 *     RZ   Largest magnitude number, with the sign of the intermediate result
 *     RM   For positive overflow, largest positive number
 *          For negative overflow, -infinity
 *     RP   For positive overflow, +infinity
 *          For negative overflow, largest negative number
 *
 * The pattern is one rule: the result is an infinity when the rounding mode
 * pushes *away* from zero in that direction, and the largest finite number when
 * it pulls back. Returning an infinity unconditionally -- the obvious reading of
 * "overflow" -- makes round-to-zero produce a value the part never produces,
 * and silently, because the exception byte is identical either way.
 *
 * "Largest magnitude number" is the largest in the **rounding precision**, not
 * in extended: §6.1.4 detects overflow against "the maximum exponent value of
 * the selected rounding precision", and its NOTE spells out that a result small
 * enough for extended can still overflow a single-precision destination. */
[[nodiscard]] ap_m68882_extended_t
ap_m68882_overflow_result(bool sign, ap_m68882_rounding_t mode,
                          ap_m68882_precision_t precision);

/* The largest biased exponent the rounding precision can hold. A result at or
 * above this overflows that precision even when extended could represent it. */
[[nodiscard]] uint16_t
ap_m68882_overflow_exponent(ap_m68882_precision_t precision);

/* The smallest biased exponent at which the rounding precision can hold a
 * *normalized* number. Below it a result is denormalised into that precision's
 * subnormal range and `UNFL` is reported -- §6.1.5's NOTE: "an underflow can
 * occur when the destination is a floating-point data register and the selected
 * rounding precision is single or double **even if the intermediate result is
 * large enough to be represented as an extended precision number**."
 *
 * Extended's is zero, and that is the asymmetry the format's explicit integer
 * bit creates: single and double reach their minimum exponent and then trade
 * significand bits for range, while extended can hold a normalized number at
 * its own minimum. §3.6 gives the normalized ranges -- `0 < e < 2047` biased by
 * 1023 for double, so -1022; -126 for single by the same construction. */
[[nodiscard]] uint16_t
ap_m68882_underflow_exponent(ap_m68882_precision_t precision);

[[nodiscard]] ap_m68882_op_t ap_m68882_add(const ap_m68882_extended_t *a,
                                           const ap_m68882_extended_t *b,
                                           ap_m68882_rounding_t mode,
                                           ap_m68882_precision_t precision);

[[nodiscard]] ap_m68882_op_t ap_m68882_sub(const ap_m68882_extended_t *a,
                                           const ap_m68882_extended_t *b,
                                           ap_m68882_rounding_t mode,
                                           ap_m68882_precision_t precision);

[[nodiscard]] ap_m68882_op_t ap_m68882_mul(const ap_m68882_extended_t *a,
                                           const ap_m68882_extended_t *b,
                                           ap_m68882_rounding_t mode,
                                           ap_m68882_precision_t precision);

[[nodiscard]] ap_m68882_op_t ap_m68882_div(const ap_m68882_extended_t *a,
                                           const ap_m68882_extended_t *b,
                                           ap_m68882_rounding_t mode,
                                           ap_m68882_precision_t precision);

/* ---------------------------------------------------------------------------
 * The single-precision pair.
 *
 * `FSGLMUL` and `FSGLDIV` are the ordinary multiply and divide with two
 * differences, and both come from §6.1.4's paragraph on them: "the rounding
 * precision programmed in the mode control byte is **ignored** (although the
 * selected rounding mode is used). The input operands ... are assumed to be
 * single precision values, but no checking is performed to verify the inputs
 * (each mantissa is truncated to 23 bits, and the exponent is accepted as an
 * extended precision value)."
 *
 * So the significands are cut down on the way in and the answer is rounded to
 * single on the way out, whatever the FPCR says. But the *range* stays
 * extended: "the mantissa of the intermediate result is rounded to single
 * precision, [yet] the exponent remains an extended format exponent. Therefore,
 * those instructions can never report an overflow as long as the intermediate
 * result is small enough to be represented in extended precision format."
 * §6.1.4 puts it as "the final result generated has the range of an extended
 * precision number with a mantissa accurate to only 23 bits".
 *
 * ## How many bits the truncation keeps is a reading
 *
 * §6.1.4 says "truncated to 23 bits". The `FSGLMUL` page says the operands are
 * "assumed to be representable in the single precision format" and that "if
 * either operand requires more than **24** bits of mantissa to be accurately
 * represented, the accuracy of the result is not guaranteed".
 *
 * Twenty-four is single precision's significand -- one integer bit and 23 of
 * fraction -- so the two statements reconcile if §6.1.4 is counting the
 * *fraction* field, which is what a single-precision number stores and what an
 * extended significand carries below its explicit integer bit. Read literally
 * as 23 significand bits instead, the truncation would discard a bit the
 * instruction page says is representable, and the two pages would contradict
 * each other.
 *
 * So 24 significand bits are kept, and this is recorded as a reading rather
 * than a quotation: no page states the count in both vocabularies at once. It
 * affects only operands the manual already says are outside the instruction's
 * contract, where "the accuracy of the result is not guaranteed".
 * ------------------------------------------------------------------------- */

[[nodiscard]] ap_m68882_op_t ap_m68882_single_mul(
    const ap_m68882_extended_t *a, const ap_m68882_extended_t *b,
    ap_m68882_rounding_t mode);

[[nodiscard]] ap_m68882_op_t ap_m68882_single_div(
    const ap_m68882_extended_t *a, const ap_m68882_extended_t *b,
    ap_m68882_rounding_t mode);

/* ---------------------------------------------------------------------------
 * The remainder pair.
 *
 * `FREM` is the IEEE remainder and `FMOD` the modulo, and the *only* difference
 * between them is how the implied quotient is rounded:
 *
 *     FPn - (Source x N),  where N = INT(FPn / Source)
 *
 * round-to-nearest for `FREM` and round-to-zero for `FMOD`. The manual is
 * explicit that this is not a detail -- `FMOD` "uses the round-to-zero mode and
 * thus returns a remainder that is different from the remainder required by the
 * IEEE Specification for Binary Floating-Point Arithmetic".
 *
 * Both are **exact**. A remainder is always representable, whatever the
 * operands, because it is smaller than the divisor and shares its exponent
 * range -- so this cannot round, cannot overflow, and never raises `INEX2`.
 * That is why it is computed by long division on the significands rather than
 * as `a - b * round(a / b)`: the quotient can be astronomically large and the
 * product would round away the very bits the remainder is made of.
 * ------------------------------------------------------------------------- */

typedef struct {
  ap_m68882_extended_t value;
  uint32_t exceptions;
  /* §2.3.2's quotient byte: "the seven least-significant bits of the quotient
   * (unsigned) and the sign of the entire quotient", where the sign "is the
   * exclusive OR of the sign bits of the source and destination operands" --
   * so it is the sign the quotient *would* have, not the sign of the remainder.
   *
   * Seven bits are kept because they are what the byte holds, and the manual
   * says why they are enough: "the quotient bits can be used in argument
   * reduction for transcendentals ... seven bits are more than enough to
   * determine the quadrant of a circle in which an operand resides." */
  bool quotient_sign;
  unsigned quotient;
} ap_m68882_remainder_t;

/* `round_to_nearest` selects `FREM`; clear it for `FMOD`. */
[[nodiscard]] ap_m68882_remainder_t
ap_m68882_remainder(const ap_m68882_extended_t *destination,
                    const ap_m68882_extended_t *source, bool round_to_nearest);

/* Compare, which sets condition codes rather than producing a value.
 * `unordered` is the NAN case -- "an unordered condition occurs when one or
 * both of the operands in a floating-point compare operation is a NAN" -- and
 * it is not the same as "neither less nor greater nor equal", which is what a
 * three-way comparison would report. */
typedef struct {
  bool less;
  bool equal;
  bool unordered;
  uint32_t exceptions;
} ap_m68882_compare_t;

[[nodiscard]] ap_m68882_compare_t
ap_m68882_compare(const ap_m68882_extended_t *a,
                  const ap_m68882_extended_t *b);

/* ---------------------------------------------------------------------------
 * The exactly-specified monadic operations.
 *
 * These are the ones §4.3.1 puts under the IEEE error bound rather than
 * §4.3.2's transcendentals: "the IEEE specification does not define the error
 * bound to which transcendental (**except square root**) functions are to be
 * performed". So a square root has one right answer to within half a unit in
 * the last place, and so do the exponent and mantissa extractions, the integer
 * parts and the scale -- none of which is even approximate.
 * ------------------------------------------------------------------------- */

/* "Calculates the square root of that value". IEEE-specified, so correctly
 * rounded rather than approximated -- and `OPERR` for a negative source, which
 * Table 6-2 lists as "Source <0, Source = -infinity". */
[[nodiscard]] ap_m68882_op_t ap_m68882_sqrt(const ap_m68882_extended_t *a,
                                            ap_m68882_rounding_t mode,
                                            ap_m68882_precision_t precision);

/* "Extracts the binary exponent. Removes the exponent bias, converts the
 * exponent to an extended precision floating-point number." So the *result* is
 * a float holding an integer, not an integer -- and `OPERR` for an infinity,
 * which has no meaningful exponent. */
[[nodiscard]] ap_m68882_op_t ap_m68882_getexp(const ap_m68882_extended_t *a);

/* "Extracts the mantissa ... The result is in the range [1.0 ... 2.0) with the
 * sign of the source mantissa, zero, or is a NAN." The sign is *kept*, which is
 * what makes this the mantissa rather than its magnitude. */
[[nodiscard]] ap_m68882_op_t ap_m68882_getman(const ap_m68882_extended_t *a);

/* "Extracts the integer part ... by rounding the extended precision number to
 * an integer using the current rounding mode". So `FINT` follows the mode --
 * "the integer part of 137.57 is 137.0 for the round-to-zero and round-to-minus
 * infinity modes, and 138.0 for the round-to-nearest and round-to-plus infinity
 * modes" -- while `FINTRZ` always truncates whatever the mode says. Two
 * instructions because the mode-following one is not always what a program
 * wants. */
[[nodiscard]] ap_m68882_op_t ap_m68882_int(const ap_m68882_extended_t *a,
                                           ap_m68882_rounding_t mode);
[[nodiscard]] ap_m68882_op_t ap_m68882_intrz(const ap_m68882_extended_t *a);

/* "FPn x INT(2^Source) -> FPn": the source is converted to an integer and added
 * to the destination's exponent. A power of two by exponent arithmetic, so it
 * is exact and cannot round -- which is the point of having it at all rather
 * than multiplying. */
[[nodiscard]] ap_m68882_op_t ap_m68882_scale(const ap_m68882_extended_t *a,
                                             const ap_m68882_extended_t *b);

#endif /* APOLLO_CPU_M68882_AP_M68882_ARITH_H */
