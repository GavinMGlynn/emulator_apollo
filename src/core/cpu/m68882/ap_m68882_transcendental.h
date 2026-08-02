/* MC68882 §4.3.2's transcendentals: the trigonometric, hyperbolic,
 * logarithmic and exponential instructions.
 *
 * ## Why these are computed rather than reported unimplemented
 *
 * §4.3.2 specifies a **bound**, not a result:
 *
 *   "In general, the worst-case accuracy of any transcendental function is one
 *   unit in the last place of double precision (which is equal to 4096 units in
 *   the last place of extended precision). The typical error bound for these
 *   instructions is approximately 64 units in the last place of extended
 *   precision."
 *
 * Motorola publishes no algorithm, in this manual or in the sibling manuals on
 * disk, so a bit-identical model is not available by any route: the recurrence
 * the part uses is simply not documented. What *is* documented is the interval
 * a conforming result must fall in, and an implementation that lands inside it
 * conforms to everything the manual actually says.
 *
 * That reframes the choice. Reporting these unimplemented is not the
 * conservative option -- it is the **larger** divergence. Real hardware given an
 * `FSIN` computes a sine; a model that raises an unimplemented-instruction
 * exception instead diverges by the whole result and kills a process that
 * should have got an answer. Being 64 units in the last place from the part is
 * a smaller error than not being a floating-point unit at all, and any program
 * that can tell the difference could equally tell the difference between two
 * conforming parts.
 *
 * So: computed, within the published bound, with the divergence stated. The
 * bounds themselves are in `ap_m68882_accuracy.h` and are the acceptance
 * criterion these functions are tested against.
 *
 * ## What this model will not reproduce
 *
 * §4.3.2 also documents where the part is *deliberately* inexact:
 *
 *   "the transcendental functions perform limited checking for special case
 *   input values ... the exponential functions check for a zero input value,
 *   but do not check for exact integer values. Thus, raising a number to an
 *   exact integer value may not produce an exact result (e.g., the instruction
 *   FTENTOX #1,FP0 does not produce an extended precision value of exactly
 *   10.0)."
 *
 * This model computes `FTENTOX #1` as exactly 10.0. That is a known, recorded
 * divergence in the direction of being *more* correct than the part, and it is
 * the one place where our conformance to the bound is visible as a difference
 * rather than hidden in the last bits. It is not closable without the
 * unpublished algorithm.
 *
 * ## Determinism, and why the host's library is not used
 *
 * Nothing here calls `libm`. The host's `sinl` differs between glibc, musl and
 * macOS, and between versions of each; a reference core whose results depend on
 * which machine built it cannot have portable goldens, and CI asserts that it
 * does. Every function below is evaluated with this core's own extended-
 * precision arithmetic -- the same `ap_m68882_add`, `_sub`, `_mul` and `_div`
 * the hardware instructions use -- so a result is a function of the input and
 * nothing else, identical on every platform and every build type.
 *
 * The cost of that choice is that each intermediate rounds to 64 bits. A Horner
 * evaluation of the degree used here accumulates well under a hundred units in
 * the last place in the worst case, which is inside §4.3.2's worst-case bound
 * with room to spare, and the argument reductions are arranged to be exact so
 * that none of the error comes from the part that would matter most.
 */

#ifndef APOLLO_CPU_M68882_AP_M68882_TRANSCENDENTAL_H
#define APOLLO_CPU_M68882_AP_M68882_TRANSCENDENTAL_H

#include "cpu/m68882/ap_m68882_arith.h"

/* The exponential family.
 *
 * All four share one kernel and differ only in the argument reduction, which is
 * where their accuracy is won or lost: `FETOX` reduces by `ln 2`, `FTWOTOX` by
 * an integer, and `FTENTOX` by `log2(10)` in two pieces because a single
 * rounded product would lose sixteen bits of the answer for a large exponent.
 *
 * `FETOXM1` is not `FETOX` minus one. For a small argument that subtraction
 * cancels away every significant bit -- `e^x - 1` for `x = 2^-40` is about
 * `2^-40`, and computing it as `(1 + 2^-40) - 1` gives a result with 24 bits
 * left. The instruction exists precisely so that case is accurate, so the
 * kernel is evaluated directly and the subtraction never happens. */
[[nodiscard]] ap_m68882_op_t ap_m68882_etox(const ap_m68882_extended_t *x,
                                            ap_m68882_rounding_t mode,
                                            ap_m68882_precision_t precision);

[[nodiscard]] ap_m68882_op_t ap_m68882_etoxm1(const ap_m68882_extended_t *x,
                                              ap_m68882_rounding_t mode,
                                              ap_m68882_precision_t precision);

[[nodiscard]] ap_m68882_op_t ap_m68882_twotox(const ap_m68882_extended_t *x,
                                              ap_m68882_rounding_t mode,
                                              ap_m68882_precision_t precision);

[[nodiscard]] ap_m68882_op_t ap_m68882_tentox(const ap_m68882_extended_t *x,
                                              ap_m68882_rounding_t mode,
                                              ap_m68882_precision_t precision);

/* The logarithms.
 *
 * All four reduce `x` to `m * 2^k` with `m` in `[1/sqrt2, sqrt2)` and evaluate
 * `ln(m)` as `2 atanh((m-1)/(m+1))`. The substitution is what makes the family
 * cheap: it maps the whole reduced range onto `|s| <= 0.1716` and leaves only
 * odd powers, so fourteen terms suffice where a series in `m - 1` would need
 * far more and would be at its worst exactly where `x` is near one.
 *
 * `FLOG2` is not `FLOGN` divided by `ln 2`. Its exponent term stays an exact
 * integer and only the significand's contribution is scaled, so `log2` of a
 * power of two is that power exactly -- which a final division would round
 * along with everything else.
 *
 * `FLOGNP1` is not `FLOGN` of `1 + x`, for the same reason `FETOXM1` is not
 * `FETOX` minus one: for a small argument `1 + x` rounds back to one and takes
 * the answer with it. It uses the identity `ln(1+x) = 2 atanh(x/(x+2))`, where
 * `x + 2` cannot cancel.
 *
 * Their singularities differ, and the manual is explicit about it. `FLOGN(0)`
 * raises `DZ` and returns a *negative infinity*; `FLOGNP1(-1)` raises `DZ` and
 * returns a *NAN*, per the note on page 4-58. Same mathematical pole, two
 * different documented results. */
[[nodiscard]] ap_m68882_op_t ap_m68882_logn(const ap_m68882_extended_t *x,
                                            ap_m68882_rounding_t mode,
                                            ap_m68882_precision_t precision);

[[nodiscard]] ap_m68882_op_t ap_m68882_lognp1(const ap_m68882_extended_t *x,
                                              ap_m68882_rounding_t mode,
                                              ap_m68882_precision_t precision);

[[nodiscard]] ap_m68882_op_t ap_m68882_log2(const ap_m68882_extended_t *x,
                                            ap_m68882_rounding_t mode,
                                            ap_m68882_precision_t precision);

[[nodiscard]] ap_m68882_op_t ap_m68882_log10(const ap_m68882_extended_t *x,
                                             ap_m68882_rounding_t mode,
                                             ap_m68882_precision_t precision);

/* The trigonometric functions.
 *
 * All four share one reduction, `x = n*(pi/2) + r` with `|r| <= pi/4`, and one
 * pair of series. The quadrant `n mod 4` then selects which of `+/-sin` and
 * `+/-cos` each answer is, which is why `FSINCOS` is one instruction: the
 * reduction is the expensive part and both results fall out of it.
 *
 * The reduction is the whole accuracy story. `pi/2` is held to about 199 bits
 * in three pieces and each `n * pi/2_i` is formed as an exact pair, because the
 * constant's truncation error is multiplied by `n` -- and `n` is as large as
 * the argument. A single 64-bit `pi/2` would leave an error near a radian for a
 * large argument, which is not an inaccurate answer but a meaningless one.
 *
 * Accuracy therefore holds while `n` fits in a 64-bit significand, to arguments
 * around `1.4e19`, and degrades beyond. That matches the part: the `FSIN` page
 * says "large arguments may lose accuracy during reduction, and very large
 * arguments (greater than approximately 10^20) lose all accuracy". The
 * degradation is modelled, not merely tolerated.
 *
 * None of the three has a divide by zero. `FTAN` at `pi/2` is a large finite
 * number, because `pi/2` is not representable and the argument is never exactly
 * at the pole. */
[[nodiscard]] ap_m68882_op_t ap_m68882_sin(const ap_m68882_extended_t *x,
                                           ap_m68882_rounding_t mode,
                                           ap_m68882_precision_t precision);

[[nodiscard]] ap_m68882_op_t ap_m68882_cos(const ap_m68882_extended_t *x,
                                           ap_m68882_rounding_t mode,
                                           ap_m68882_precision_t precision);

[[nodiscard]] ap_m68882_op_t ap_m68882_tan(const ap_m68882_extended_t *x,
                                           ap_m68882_rounding_t mode,
                                           ap_m68882_precision_t precision);

/* `FSINCOS` writes two registers, so it returns two results rather than one.
 * Its eight encodings `$30-$37` differ only in which register takes the
 * cosine. */
void ap_m68882_sincos(const ap_m68882_extended_t *x, ap_m68882_rounding_t mode,
                      ap_m68882_precision_t precision, ap_m68882_op_t *sine,
                      ap_m68882_op_t *cosine);

#endif /* APOLLO_CPU_M68882_AP_M68882_TRANSCENDENTAL_H */
