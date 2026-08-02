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

#endif /* APOLLO_CPU_M68882_AP_M68882_ARITH_H */
