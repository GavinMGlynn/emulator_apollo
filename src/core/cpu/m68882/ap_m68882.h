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
#include "cpu/m68882/ap_m68882_format.h"
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

/* Execute a general-type instruction whose operands are both already in the
 * part -- opclass `000`, register to register. A form needing an external
 * operand reports unimplemented here rather than guessing at an address:
 * fetching it is the *main processor's* job, and the two entry points below are
 * how it does that. */
[[nodiscard]] ap_m68882_status_t ap_m68882_execute(ap_m68882_t *fpu,
                                                   uint16_t operation_word,
                                                   uint16_t command_word);

/* ---------------------------------------------------------------------------
 * The source operand transfer
 *
 * "The MPU evaluates the source/destination effective address ... and transfers
 * operand(s) to/from the FPCP." On hardware that is §9's dialog: the FPCP
 * answers the command with an *evaluate effective address and transfer data*
 * response primitive naming a length, and the MPU obliges. The primitive
 * exchange itself is not modelled -- nothing on this machine can observe it,
 * because the CIRs live in CPU space and Domain/OS reaches the part only
 * through F-line instructions -- but the *division of labour* it describes is,
 * and it is the reason this is two calls rather than one.
 *
 * So the main processor asks what to fetch, fetches it, and hands it back:
 *
 *     switch (ap_m68882_source_transfer(fpu, op, cmd, &format)) { ... }
 *     ... evaluate the operation word's effective address, read
 *         ap_m68882_format_size(format) bytes, ap_m68882_operand_decode() ...
 *     ap_m68882_execute_source(fpu, op, cmd, &source);
 *
 * The decision has to come first because the *format decides the address*: a
 * postincrement steps by the operand's length, so a model that fetched before
 * asking would have to guess a length and would leave the address register
 * wrong for every format but the one it guessed.
 */

/* Whether this instruction needs the main processor to fetch a source operand,
 * and in what format.
 *
 * The status is the decode's: `AP_M68882_TAKE_LINE_F` for an encoding Table
 * 4-13 leaves undefined, `AP_M68882_UNIMPLEMENTED` for the opclasses this model
 * has not got to, and `AP_M68882_EXECUTED` for one it will run. `*needs_source`
 * is separate from that status and not folded into it, because "decoded fine,
 * fetch nothing" and "decoded fine, fetch a double" are both successes and a
 * caller that could not tell them apart would either fetch an operand the
 * instruction has no address for, or execute one with a source of zero.
 *
 * `*format` is written only when `*needs_source` is true. */
[[nodiscard]] ap_m68882_status_t
ap_m68882_source_transfer(const ap_m68882_t *fpu, uint16_t operation_word,
                          uint16_t command_word, bool *needs_source,
                          ap_m68882_format_t *format);

/* Execute with the source operand the main processor fetched. The command
 * word's source specifier named its *format*, so by the time it arrives here it
 * is an extended value like any other and the operation does not know where it
 * came from -- "all external operands, regardless of data format, are converted
 * to extended precision values before the specified operation is performed". */
[[nodiscard]] ap_m68882_status_t
ap_m68882_execute_source(ap_m68882_t *fpu, uint16_t operation_word,
                         uint16_t command_word,
                         const ap_m68882_extended_t *source);

/* Evaluate a conditional predicate and report whether the condition holds.
 *
 * This is the *whole* of the part's contribution to `FBcc`, `FDBcc`, `FScc` and
 * `FTRAPcc`: §9's protocol has the main processor write the predicate to the
 * condition CIR at `$0E` and read the answer back, and everything after that --
 * fetching a displacement, decrementing a register, taking a trap, writing a
 * byte of ones or zeros -- is the MPU's own work. So `ap_m68882_execute` still
 * reports those instruction *types* unimplemented, and that is not the same
 * gap: the coprocessor side is here, and what is missing is the 68030's half of
 * a dialog it does not yet hold.
 *
 * `BSUN` is raised into the FPSR and accrued when §4.4's rule demands it, which
 * is what makes this an operation rather than a query. Whether the exception
 * becomes a trap is the enable byte's business and the MPU's. */
[[nodiscard]] bool ap_m68882_condition(ap_m68882_t *fpu, unsigned predicate);

#endif /* APOLLO_CPU_M68882_AP_M68882_H */
