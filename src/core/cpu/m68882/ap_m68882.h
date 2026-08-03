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
#include "cpu/m68882/ap_m68882_store.h"

typedef struct {
  ap_m68882_regs_t regs;
  /* Which coprocessor ID this part answers. "Motorola assemblers default to
   * ID = 1 for the FPCP", and a system may hold several coprocessors, so this
   * is configured rather than assumed. */
  unsigned cpid;
  /* The microcode version this part reports in a state frame's format word.
   * **`PROVISIONAL`**: "The version number is an 8-bit value that identifies the
   * microcode version of the FPCP, and the format of this number is defined
   * internally by the FPCP" -- so the manual publishes no value, and there is
   * nothing to transcribe. Held as state rather than a constant because that is
   * what it is on the part, and because the only behaviour a program can
   * observe is *self-consistency*: what `FSAVE` writes, `FRESTORE` must accept.
   * See `PROJECT_STATUS.md` for the closing measurement. */
  unsigned version;
  /* Whether any instruction has run since the last reset or null restore, which
   * is exactly what decides a null save from an idle one: "A save of the null
   * state results when no FPCP instructions have been executed since the last
   * null state restore or hardware reset." */
  bool executed;
} ap_m68882_t;

/* ---------------------------------------------------------------------------
 * `EXC PEND` and when a floating-point trap is actually taken.
 *
 * §6.4.2, p. 6-33: "If EXC PEND is true when an attempt is made to initiate an
 * FPCP instruction (other than an FMOVEM, FMOVE control register, FSAVE, or
 * FRESTORE), the response CIR is encoded to the take pre-instruction exception
 * primitive ... otherwise, the dialog for the instruction is started."
 *
 * So the trap is **not** taken by the instruction that caused it. It waits, and
 * arrives when the next non-exempt floating-point instruction is attempted --
 * the FPCP runs concurrently with the MPU, and this is what that concurrency
 * costs. The four exempt forms are exactly the ones a handler needs, which is
 * why §6.1.9 tells handlers to move data with `FMOVEM`: it "cannot generate
 * further exceptions or change the condition codes", and it cannot trip the
 * trap it is running inside either.
 *
 * **EXC PEND is derived here rather than latched**, as `EXC & ENABLE` through
 * `ap_m68882_trap_exception`. The manual's own account of clearing it is what
 * makes that the truer model: on this part it is not cleared by the exception
 * acknowledge at all -- "the MC68881 detects the exception acknowledge, [and]
 * clears EXC PEND. However, the MC68882 does not clear the EXC PEND bit. It is
 * the responsibility of the exception handler to clear EXC PEND" -- and what a
 * handler clears it *with* is a write to the FPSR. Deriving makes that write do
 * the job by construction, where a separate latch would need clearing in step
 * with a register it duplicates.
 *
 * The cost of deriving is one stated difference: enabling a trap in the FPCR
 * *after* an exception has already been recorded in the FPSR would arm it here,
 * where a latch set at the moment of occurrence would not. §6.4.2 leans this
 * way -- "a programmer can make exceptions pending in the FPCP under software
 * control. Or, conversely, a pending exception type may be changed or cleared
 * if necessary" -- but it is a reading, and it is recorded as one.
 *
 * The consequence a program sees: a handler that returns without clearing the
 * FPSR traps again on its next arithmetic instruction. That is the 68882's
 * documented behaviour, not a loop to defend against.
 * ------------------------------------------------------------------------- */

/* §6.4.2's state frames, as far as this part produces them.
 *
 * A **busy** frame is deliberately absent, and that is a modelling statement
 * rather than a gap: it exists so an instruction suspended *part way* can be
 * resumed, and this core's 68882 completes every instruction inside the step
 * that issues it. Nothing can interrupt it half-done, so nothing can generate
 * one -- the same reasoning the 68030's stack frame `$9` once carried, and here
 * it holds, because no main processor rule reaches around it. */
enum {
  /* Figure 6-5: the null frame is one long word and the idle frame is `$3C`
   * bytes, "60 ($3C) bytes long in the MC68882". The size *byte* in the format
   * word counts what follows it: `$38` is 56, and 56 + 4 is 60. */
  AP_M68882_FRAME_NULL_BYTES = 4,
  AP_M68882_FRAME_IDLE_BYTES = 0x3C,
  AP_M68882_FRAME_IDLE_SIZE_BYTE = 0x38,
};

/* Write the state frame this part would save. Returns its length in bytes, so
 * the caller knows how far a predecrement steps. */
[[nodiscard]] unsigned ap_m68882_save(const ap_m68882_t *fpu, uint8_t *bytes);

/* How long a frame the format word describes, or zero if it is not one this
 * part will accept -- "the FPCP checks the version number and frame size values
 * for validity and signals a format exception if they are not valid for this
 * particular device". */
[[nodiscard]] unsigned ap_m68882_frame_length(const ap_m68882_t *fpu,
                                              uint16_t format_word);

/* Load a state frame. `bytes` holds `ap_m68882_frame_length` bytes. */
void ap_m68882_restore(ap_m68882_t *fpu, const uint8_t *bytes);

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
                         const ap_m68882_extended_t *source,
                         uint32_t conversion_exceptions);

/* ---------------------------------------------------------------------------
 * The destination operand transfer
 *
 * The same division of labour in the other direction, opclass `011`: the part
 * converts, the main processor writes. Register-to-memory is only ever `FMOVE`,
 * so there is no operation to dispatch -- "Rounds the source operand to the
 * size of the specified destination format and stores it at the destination
 * effective address."
 *
 * Asking first matters here for the same reason it does on the way in: the
 * destination format's length is what a predecrement steps by.
 */

/* Whether this instruction needs the main processor to store a result, and in
 * what format. Same contract as `ap_m68882_source_transfer`: the status is the
 * decode's, and `*needs_store` is separate from it. */
[[nodiscard]] ap_m68882_status_t
ap_m68882_destination_transfer(const ap_m68882_t *fpu, uint16_t operation_word,
                               uint16_t command_word, bool *needs_store,
                               ap_m68882_format_t *format);

/* Convert the named register into the destination format and report the bytes
 * for the main processor to write.
 *
 * The exceptions are raised into the FPSR here, because they belong to the
 * *conversion* and that is the part's work -- but **the condition codes are
 * not touched**, which is the trap: the FMOVE page's Status Register section
 * says "Condition Codes: Not affected" and "Quotient Byte: Not affected", where
 * every arithmetic operation sets them. A store that went through the common
 * result path would quietly rewrite the condition codes of whatever ran
 * before it. */
/* `dynamic_k_factor` is used only when the destination format is `$7`, packed
 * decimal with a dynamic k-factor, whose command field holds `rrr0000` -- a main
 * processor data register number rather than a value, so the caller reads the
 * register and passes what it found. "If a data register contains the k-factor,
 * only the least significant 7 bits are used." */
[[nodiscard]] ap_m68882_status_t
ap_m68882_execute_store(ap_m68882_t *fpu, uint16_t operation_word,
                        uint16_t command_word, int dynamic_k_factor,
                        ap_m68882_store_t *out);

/* ---------------------------------------------------------------------------
 * FMOVEM: a list of transfers rather than one
 *
 * "Moves one or more extended precision numbers to or from a list of
 * floating-point data registers. **No conversion or rounding is performed**
 * during this operation, and the FPSR is **not affected** by the instruction.
 * This instruction does not cause pending exceptions (other than protocol
 * violations) to be reported to the main processor."
 *
 * Every one of those negatives is load-bearing, and together they are why this
 * does not reuse the store and load paths above. Routing `FMOVEM` through
 * `ap_m68882_store_encode` would quieten a signalling NAN and raise `SNAN` --
 * and the manual's note is that this instruction is the *only* way to move a
 * value without that happening: "the FMOVEM instruction provides the only
 * mechanism for moving a floating-point data item between the FPCP and memory
 * without performing any data conversions or affecting the condition code and
 * exception status bits". §6.1.2 says the same from the other end: FMOVEM and
 * FSAVE "cannot generate exceptions. Therefore, these instructions are useful
 * for manipulating SNANs."
 *
 * So the format is always extended, the transfer is a copy, and the register
 * file is the only state touched.
 */

/* Whether this instruction is an `FMOVEM` of the data registers, and how.
 * Same contract as the transfer queries above: the status is the decode's and
 * `*is_movem` is separate from it. */
[[nodiscard]] ap_m68882_status_t
ap_m68882_movem_transfer(const ap_m68882_t *fpu, uint16_t operation_word,
                         uint16_t command_word, bool *is_movem,
                         ap_m68882_movem_t *movem);

/* One register's twelve bytes, in memory order. Copies: no rounding, no
 * exception, no condition code, and a signalling NAN stays signalling. */
void ap_m68882_movem_read(const ap_m68882_t *fpu, unsigned reg,
                          uint8_t *bytes);
void ap_m68882_movem_write(ap_m68882_t *fpu, unsigned reg,
                           const uint8_t *bytes);

/* ---------------------------------------------------------------------------
 * The system control registers, opclasses `100` and `101`
 *
 * `FMOVE` of one and `FMOVEM` of several are **the same encoding**, which the
 * manual says outright: "if a single register is selected, the opcode generated
 * is the same as for the FMOVE single system control register instruction". So
 * they are one path here, and the count is what changes.
 *
 * `10 dr | REGISTER SELECT (3) | 0 ...`, and the select bits are their own
 * numbering: **12 is FPCR, 11 is FPSR, 10 is FPIAR**, transferred in that
 * order "regardless of the addressing mode used" -- so unlike the data
 * registers there is no reversal, and unlike them the order does not depend on
 * the mode at all.
 *
 * Always long: "A 32-bit transfer is always performed, even though the system
 * control register may not have 32 implemented bits."
 */
enum {
  AP_M68882_CONTROL_FPCR = 12,
  AP_M68882_CONTROL_FPSR = 11,
  AP_M68882_CONTROL_FPIAR = 10,
};

/* Which bits exist. "Unimplemented bits of a control register are read as zeros
 * and are ignored during writes (but must be zero for compatability with future
 * devices)" -- so a program that writes all ones and reads back gets these, and
 * a model without the masks would hand back bits the part does not have.
 *
 * FPCR is two bytes: the enable byte at 15-8, and Figure 2-3's mode control
 * byte, which is PREC at 7-6, RND at 5-4 and **zero at 3-0**. FPSR is four,
 * with Figure 2-4's condition code byte using only 27-24 -- "31 30 29 28" are
 * printed as a single `0` field -- and the accrued exception byte only 7-3.
 * FPIAR is an address and has all thirty-two. */
#define AP_M68882_FPCR_IMPLEMENTED UINT32_C(0x0000FFF0)
#define AP_M68882_FPSR_IMPLEMENTED UINT32_C(0x0FFFFFF8)

typedef struct {
  bool to_memory;  /* the `dr` field, as for FMOVEM */
  unsigned select; /* bits 12-10, still in place */
} ap_m68882_control_t;

/* Whether this instruction moves system control registers, and which way. */
[[nodiscard]] ap_m68882_status_t
ap_m68882_control_transfer(const ap_m68882_t *fpu, uint16_t operation_word,
                           uint16_t command_word, bool *is_control,
                           ap_m68882_control_t *control);

/* How many registers the select names, which is what a predecrement steps by
 * and what an immediate operand's length is. */
[[nodiscard]] unsigned ap_m68882_control_count(unsigned select);

/* One control register by its select *bit* -- 12, 11 or 10 -- so that a caller
 * walking the select field never needs a second numbering.
 *
 * Neither raises an exception nor sets a condition code: "This instruction does
 * not cause pending exceptions (other than protocol violations) to be reported
 * to the main processor. Furthermore, a write to the FPCR exception enable byte
 * or the FPSR exception status byte **cannot generate a new exception**,
 * regardless of the value written." The one thing a write does do is stated
 * separately and is not an exception at all: writing the FPSR replaces the
 * condition codes wholesale, because they are part of the register. */
[[nodiscard]] uint32_t ap_m68882_control_read(const ap_m68882_t *fpu,
                                              unsigned bit);
void ap_m68882_control_write(ap_m68882_t *fpu, unsigned bit, uint32_t value);

/* One entry of the on-chip constant ROM, `FMOVECR`'s source.
 *
 * The offsets are published and **the values are not** -- neither the part's own
 * manual nor the `M68000 Family Programmer's Reference Manual` prints a bit
 * pattern, only a name: `$00` is "pi", `$30` is "1n(2)". So these are computed
 * independently to 200 decimal digits and correctly rounded, the same route the
 * transcendentals took, and bit-exact agreement with a particular mask set is
 * not something the documents can settle. See `PROJECT_STATUS.md`.
 *
 * `defined` is false for the offsets the manual leaves to Motorola: "The values
 * contained at offsets other than those defined above are reserved for the use
 * of Motorola, and may be different on various mask sets of the FPCP." That is
 * a documented absence of a right answer, not a gap in this model -- there is no
 * value to be correct about. The PRM names the one convention that exists:
 * "These undefined values yield the value 0.0 in the M68040FPSP", and that is
 * what `*out` gets, so a program reading one sees a stated value rather than
 * whatever was in the register. */
[[nodiscard]] bool ap_m68882_constant(unsigned offset,
                                      ap_m68882_extended_t *out);

/* Record an instruction's address in the FPIAR, if this is one that records it.
 *
 * §2.4, and both of its conditions are easy to miss. "For the subset of the
 * FPCP instructions that generate floating-point exception traps, the 32-bit
 * floating-point instruction address (FPIAR) register is loaded with the
 * logical address of an instruction **before** the instruction is executed
 * (**unless all arithmetic exceptions are disabled**)."
 *
 * The first condition excludes the transfers: "Since the FPCP FMOVE to/from the
 * FPCR, FPSR, or FPIAR and FMOVEM instructions cannot generate floating-point
 * exceptions, these instructions do not modify the FPIAR. These instructions
 * can be used to read the FPIAR in the trap handler without changing the
 * previous value" -- which is what the register is *for*, so a model that
 * updated it on every instruction would destroy the value on the way to reading
 * it.
 *
 * The second is a live condition on the enable byte, not a one-off: with every
 * arithmetic trap disabled the register simply does not track, because nothing
 * could ask it where the fault was. `BSUN` is deliberately not counted among
 * them -- the manual says *arithmetic* exceptions, and BSUN is the
 * branch-on-unordered one, raised by a conditional test rather than by an
 * operation. Reading it the other way would make the register track slightly
 * more often and is the alternative if this turns out wrong.
 *
 * Called before executing, which is what the "before the instruction is
 * executed" clause means for a handler that has to find it. */
void ap_m68882_note_instruction(ap_m68882_t *fpu, uint16_t operation_word,
                                uint16_t command_word, uint32_t address);

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
