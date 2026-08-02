/* MC68882 §4.3.2: which operations are transcendental, and what accuracy the
 * part actually delivers for them.
 *
 * This header exists because "the transcendentals are unimplemented" is not a
 * specification. It records what §4.3.2 states, so that the decision to leave
 * them out is checkable and so that any future implementation has an acceptance
 * criterion instead of a vague aspiration.
 *
 * ## The manual publishes bounds, and no algorithm
 *
 * §4.3.2, read from the page image of page 4-7:
 *
 *   "The IEEE specification does not define the error bound to which
 *   transcendental (except square root) functions are to be performed. In this
 *   context, the transcendental functions are all of those operations not
 *   mentioned in the previous paragraphs (i.e., the trigonometric, hyperbolic,
 *   logarithmic, and exponential instructions). Due to the highly recursive
 *   nature of the algorithms used to calculate these functions, the round-off
 *   error in the input operands to a function, combined with the limited
 *   precision of the FPCP ALU, do not allow the calculation of a result with
 *   the same error limit as the arithmetic functions. However, these operations
 *   are quite accurate given the constraint of using an ALU with a finite
 *   precision of 67 bits. In general, the worst-case accuracy of any
 *   transcendental function is one unit in the last place of double precision
 *   (which is equal to 4096 units in the last place of extended precision). The
 *   typical error bound for these instructions is approximately 64 units in the
 *   last place of extended precision."
 *
 * "The algorithms used" is as close as Motorola comes to naming one. The
 * *bounds* are published; the recurrence is not, in this manual or in the
 * M68000 Family Programmer's Reference Manual, both of which are on disk and
 * were searched.
 *
 * ## The worst case is one ULP of *double*, which is the whole story
 *
 * 4096 units of extended is the same quantity as one unit of double, and the
 * manual gives it both ways. Stating it as double precision is the useful form:
 * these instructions deliver a double-precision answer in an extended-precision
 * register, and the twelve extra bits extended precision would buy are noise.
 * A model computing correctly-rounded extended results would be *more accurate
 * than the part* and differ from it in most results.
 *
 * ## And the manual names an instance
 *
 * §4.3.2 closes with a concrete one, which is what turns the divergence from
 * an argument into a fact:
 *
 *   "the transcendental functions perform limited checking for special case
 *   input values such as boundary conditions. For example, the exponential
 *   functions check for a zero input value, but do not check for exact integer
 *   values. Thus, raising a number to an exact integer value may not produce an
 *   exact result (e.g., the instruction FTENTOX #1,FP0 does not produce an
 *   extended precision value of exactly 10.0), and the INEX2 bit in the FPSR
 *   may be set even if an exact result is produced."
 *
 * Ten to the power of one is not ten. Any implementation that computes a
 * correctly-rounded `FTENTOX` returns exactly 10.0 and is therefore wrong about
 * this part -- not by a rounding mode, but visibly, in the value a program
 * reads back. That is why the operations report unimplemented rather than being
 * approximated: reporting the gap keeps it visible, where a good-but-different
 * answer would be indistinguishable from a correct one until something
 * downstream disagreed.
 *
 * ## What would close it
 *
 * An implementation whose results match the part to within the published
 * bounds, verified against the oracle across each function's documented range,
 * *and* which reproduces the documented inexactness -- `FTENTOX #1` not being
 * 10.0, and `INEX2` set on results that happen to be exact. The bounds below
 * are that criterion in a form a test can use.
 */

#ifndef APOLLO_CPU_M68882_AP_M68882_ACCURACY_H
#define APOLLO_CPU_M68882_AP_M68882_ACCURACY_H

#include <stdbool.h>

#include "cpu/m68882/ap_m68882_decode.h"

/* "The worst-case accuracy of any transcendental function is one unit in the
 * last place of double precision (which is equal to 4096 units in the last
 * place of extended precision)." Both forms are recorded because the manual
 * gives both -- and because they do not agree.
 *
 * A double significand is 53 bits (52 stored plus the implicit one) and an
 * extended significand is 64 explicit bits, so one unit in the last place of
 * double is 2^(64-53) = **2048** units in the last place of extended. The
 * manual prints 4096. The two figures differ by a factor of two.
 *
 * One reading closes it: a bound of ±1 ULP spans a window of 2 ULP, so counting
 * the window rather than the bound gives 4096. That is a guess about intent,
 * not a source, and the parenthesis reads as an equality rather than a window.
 * Neither the M68000 Family Programmer's Reference Manual nor the 68040
 * manual's Appendix E, both on disk and both searched, restates the conversion.
 *
 * So both figures are transcribed as printed and neither is derived from the
 * other. A reader who computes the conversion will get 2048 and should not
 * conclude this file is wrong; a test asserts the discrepancy so that nobody
 * quietly "fixes" one of the two constants into agreement. */
#define AP_M68882_TRANSCENDENTAL_WORST_CASE_ULP_DOUBLE 1u
#define AP_M68882_TRANSCENDENTAL_WORST_CASE_ULP_EXTENDED 4096u

/* "The typical error bound for these instructions is approximately 64 units in
 * the last place of extended precision." A *typical* figure, not a guarantee;
 * the manual's worked example makes it 2^6 times the least-significant bit,
 * which is how the 64 is arrived at. The exponent is worth stating: the text
 * extraction and even the page image at ordinary resolution flatten "2^6" to
 * "26", and only the arithmetic disambiguates it. */
#define AP_M68882_TRANSCENDENTAL_TYPICAL_ULP_EXTENDED 64u

/* "an ALU with a finite precision of 67 bits" -- three guard bits over the
 * 64-bit extended mantissa, which is the hardware reason the recursion cannot
 * do better. */
#define AP_M68882_ALU_PRECISION_BITS 67u

/* Whether `operation` is one of §4.3.2's transcendentals: "the trigonometric,
 * hyperbolic, logarithmic, and exponential instructions", which is defined by
 * exclusion -- everything not covered by the arithmetic and IEEE-specified
 * paragraphs before it. `FSQRT` is explicitly *not* one, and neither are the
 * exponent and mantissa extractions, the integer parts or the scale: those have
 * one right answer and are implemented. */
[[nodiscard]] bool ap_m68882_is_transcendental(ap_m68882_operation_t operation);

/* How many §4.3.2 names. Pinned so that adding an operation to the enum without
 * classifying it fails a test rather than silently defaulting. */
[[nodiscard]] unsigned ap_m68882_transcendental_count(void);

#endif /* APOLLO_CPU_M68882_AP_M68882_ACCURACY_H */
