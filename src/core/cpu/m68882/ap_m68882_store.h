/* MC68882: converting a floating-point value into a destination data format.
 *
 * The other half of `ap_m68882_operand_decode`, and *not* its mirror image.
 * Loading is exact -- every source format fits in extended precision with room
 * to spare -- while storing narrows, and narrowing is where the rounding mode,
 * the exception byte and three separate special-case tables come in.
 *
 * ## The rounding precision does not apply here
 *
 * §2.2.2, and it is the rule most easily got wrong because the FPCR is right
 * there: "If the destination is a memory location, the PREC bits are ignored.
 * In this case, a number in the extended precision format is taken from the
 * source floating-point data register, rounded to the **destination format**
 * precision, and written to memory."
 *
 * So `FMOVE.S FP0,(A0)` rounds to single whatever the FPCR says, and a model
 * that consulted PREC would store a double-rounded value whenever the program
 * had set the mode byte -- correct for most programs, wrong for the ones that
 * use it, and invisible either way.
 *
 * The rounding *mode* still applies. It is the precision alone that the
 * destination overrides.
 *
 * ## Three tables, not one
 *
 * The special cases differ by destination *kind*, and each is quoted where it
 * is used:
 *
 * - `S`, `D` and `X` never raise an operand error. §6.1.3's trap-disabled
 *   results are explicit: "An operand error is never generated when the
 *   destination is an MPU data register or memory and the destination format is
 *   S, D, or X."
 * - `B`, `W` and `L` raise one for three separate reasons -- Table 6-2's row
 *   for `FMOVE to B,W, or L` is "Integer Overflow/Underflow, Source is
 *   Non-Signaling NAN, or Source is +/-infinity" -- and each has its own
 *   result.
 * - A signalling NAN is §6.1.2's rule and cuts across both: it raises `SNAN`
 *   whatever the destination, and what gets written differs.
 *
 * ## Packed decimal
 *
 * Handled by `ap_m68882_packed_encode`, which is where its own operand error
 * conditions live -- "Result Exponent > 999 (Decimal) or k-Factor > +17" -- and
 * which needs the k-factor this interface therefore has to carry.
 */

#ifndef APOLLO_CPU_M68882_AP_M68882_STORE_H
#define APOLLO_CPU_M68882_AP_M68882_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68882/ap_m68882_format.h"
#include "cpu/m68882/ap_m68882_regs.h"

typedef struct {
  /* The operand in memory order, most significant byte first -- the order §3
   * states for every format, and the same one `ap_m68882_operand_decode` reads.
   * Only the leading `size` bytes are written. */
  uint8_t bytes[12];
  unsigned size;
  /* A mask of `AP_M68882_EXC_*` bit positions, for the caller to accrue. */
  uint32_t exceptions;
} ap_m68882_store_t;

/* Convert `value` into `format`, rounding by `mode`.
 *
 * `k_factor` is meaningful only for the two packed decimal formats, where it
 * decides the string's shape; every binary format ignores it. */
[[nodiscard]] bool ap_m68882_store_encode(ap_m68882_format_t format,
                                          const ap_m68882_extended_t *value,
                                          ap_m68882_rounding_t mode,
                                          int k_factor,
                                          ap_m68882_store_t *out);

#endif /* APOLLO_CPU_M68882_AP_M68882_STORE_H */
