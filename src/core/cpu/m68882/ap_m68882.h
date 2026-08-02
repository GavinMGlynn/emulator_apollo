/* The MC68882 as a fitted part: its state, and executing an instruction.
 *
 * ## Fitted or not is a machine property, not a compile-time one
 *
 * A DN3500 has a 68882 and a DN3000 does not, and the difference is visible to
 * software in exactly one way: with no coprocessor fitted, an F-line word takes
 * the line 1111 emulator exception. `[030]` §8.1 and this core's existing
 * behaviour. So the part is attached to a CPU rather than compiled into it, and
 * a machine without one keeps the trap it had.
 *
 * That also keeps the distinction the step already draws: a trap because the
 * hardware has no coprocessor is the machine doing what it does, and a trap
 * because this model has not implemented something is not. They must not report
 * the same thing.
 */

#ifndef APOLLO_CPU_M68882_AP_M68882_H
#define APOLLO_CPU_M68882_AP_M68882_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68882/ap_m68882_arith.h"
#include "cpu/m68882/ap_m68882_cir.h"
#include "cpu/m68882/ap_m68882_decode.h"
#include "cpu/m68882/ap_m68882_regs.h"

typedef struct {
  ap_m68882_regs_t regs;
  /* Which coprocessor ID this part answers. "Motorola assemblers default to
   * ID = 1 for the FPCP", and a system may hold several coprocessors, so this
   * is configured rather than assumed. */
  unsigned cpid;
} ap_m68882_t;

void ap_m68882_reset(ap_m68882_t *fpu);

/* What executing an instruction produced. */
typedef enum {
  AP_M68882_EXECUTED,
  /* The encoding is not this coprocessor's, or is one Table 4-13 leaves
   * undefined: the F-line emulator trap, which is hardware behaviour. */
  AP_M68882_TAKE_LINE_F,
  /* A form this model has not implemented. Distinct from the trap above
   * because one is the machine and the other is us -- the same distinction the
   * 68030's step draws for its own gaps. */
  AP_M68882_UNIMPLEMENTED,
} ap_m68882_status_t;

/* Execute a general-type instruction from its operation and command words.
 *
 * Register-to-register only so far, which is opclass `000`: the forms whose
 * operands are both already in the part. Anything needing an external operand
 * reports unimplemented rather than guessing at an address, because fetching it
 * is the *main processor's* job -- "the MPU evaluates the source/destination
 * effective address ... and transfers operand(s) to/from the FPCP" -- and that
 * is a dialog through the coprocessor interface rather than something the FPU
 * can do alone. */
[[nodiscard]] ap_m68882_status_t ap_m68882_execute(ap_m68882_t *fpu,
                                                   uint16_t operation_word,
                                                   uint16_t command_word);

#endif /* APOLLO_CPU_M68882_AP_M68882_H */
