/* MC68882 rounding: Figure 6-3's algorithm, and the precision control around it.
 *
 * `MC68881/MC68882 User's Manual` §6.1.7 and Figure 6-2.
 *
 * ## Why this is its own module
 *
 * Every arithmetic operation ends here. The add, the multiply, the divide and
 * the transcendentals all produce an intermediate result "as if to produce
 * infinite precision" and then round it once -- so the rounding is the single
 * place `INEX2` is raised and the single place the four modes are interpreted.
 * Written into each operation instead, it would be four chances to get
 * round-half-to-even wrong.
 *
 * ## The intermediate result carries three extra bits
 *
 * Figure 6-2's 67-bit intermediate: the 64-bit mantissa plus **guard**, **round**
 * and **sticky**. "The guard and round bits are always calculated exactly. The
 * sticky bit is used to create the illusion of an infinitely wide intermediate
 * result mantissa ... the logical OR of all the bits in the infinitely precise
 * result to the right of the round bit."
 *
 * So an operation must hand all three over rather than a single "was it exact"
 * flag: round-to-nearest needs to tell a tie (`guard` set, `round` and `sticky`
 * clear) from a value just above one, and those two round in opposite
 * directions half the time.
 *
 * ## Precision moves the rounding boundary, and the destination decides which
 *
 * "If the destination is a floating-point register, the rounding boundary is
 * determined by the selected rounding precision in the FPSR. If the destination
 * is external memory or an MPU data register, the rounding boundary is
 * determined by the destination data format." Two different sources for the
 * same decision, which is why the precision is a parameter here rather than
 * being read from the control register: the caller knows which case it is in
 * and this module cannot.
 *
 * At single or double precision the mantissa bits below the boundary are not
 * discarded -- they fold into guard, round and sticky, so a result rounded to
 * single is rounded once from the full intermediate rather than twice.
 */

#ifndef APOLLO_CPU_M68882_AP_M68882_ROUND_H
#define APOLLO_CPU_M68882_AP_M68882_ROUND_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68882/ap_m68882_regs.h"

typedef struct {
  ap_m68882_extended_t value;
  /* "If the rounded result of an operation is not exact, then the INEX2 bit is
   * set in the FPSR exception status byte." Reported rather than raised here,
   * because the FPSR belongs to the caller and one operation may have other
   * exceptions to raise alongside it. */
  bool inexact;
} ap_m68882_round_result_t;

/* Round an intermediate result. `guard`, `round` and `sticky` are Figure 6-2's
 * three extra bits, below the mantissa's least significant bit.
 *
 * The mantissa is taken with its integer bit at 63, as everywhere else in this
 * core, and is assumed already normalised by the caller -- rounding does not
 * normalise, it only rounds and then handles the one carry that rounding itself
 * can produce: "IF OVERFLOW = 1 THEN SHIFT MANTISSA RIGHT BY ONE BIT, ADD 1 TO
 * THE EXPONENT". */
[[nodiscard]] ap_m68882_round_result_t
ap_m68882_round(ap_m68882_extended_t value, bool guard, bool round_bit,
                bool sticky, ap_m68882_rounding_t mode,
                ap_m68882_precision_t precision);

/* How many mantissa bits the precision keeps: 64, 24 or 53. The reserved
 * encoding keeps 64, which is the extended case -- it has to do *something*
 * deterministic, and widening is the choice that cannot silently lose bits. */
[[nodiscard]] unsigned ap_m68882_precision_bits(ap_m68882_precision_t precision);

#endif /* APOLLO_CPU_M68882_AP_M68882_ROUND_H */
