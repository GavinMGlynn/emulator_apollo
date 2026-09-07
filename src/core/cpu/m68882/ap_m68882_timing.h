/* MC68882 instruction execution timing, from `[881]` §8.
 *
 * ## What this charges, and what it deliberately does not
 *
 * §8.5.2's phase diagrams split a coprocessor instruction into six phases and
 * say which side owns each:
 *
 *     START-UP | EVALUATE <ea> | TRANSFER OPERAND | CONVERT | CALCULATE | ROUND
 *     \______________ MPU-dependent _____________/ \____ FPCP only ____/
 *
 * and §8.5.2 states the consequence outright: "the timing prior to the start of
 * the conversion operation is almost entirely dependent on the execution
 * characteristics of the main processor, while the timing for the rest of the
 * instruction is dependent **solely on the FPCP**. This distinction is useful
 * when the execution timing for a main processor other than the MC68020 or
 * MC68030 is to be determined."
 *
 * This core already charges the left-hand half: the instruction fetch, the
 * effective address and the operand read are real bus cycles through
 * `ap_m68030_access`, priced by the memory system this machine actually has.
 * What it charged nothing for was the right-hand half -- the calculation -- so
 * an `FSIN` cost the same as an `FMOVE`. That is what this module supplies, and
 * why it is a *separate* number added to the bus time rather than a replacement
 * for it: adding a table that already contains the transfers would count the
 * bus twice.
 *
 * ## Why the register-to-register column, for every operand location
 *
 * Table 8-3's `FPn to FPm` column is the same instruction with **no external
 * operand**: start-up, the conversion of an already-internal operand, the
 * calculation and the round. Its memory columns differ from it only by the
 * operand transfer and the wider conversion -- for `FADD` the gap is 81 - 56 =
 * 25 clocks against Table 8-4's ten bus cycles for an extended operand, which
 * is the transfer this core is already charging as bus time.
 *
 * So the register-to-register column is the *part that is ours to add*, and it
 * carries the whole of the term that matters: `FADD` 56 against `FSIN` 394.
 *
 * ## `PROVISIONAL`, and precisely in what way
 *
 * **The calculation time is data-dependent and this charges one point value.**
 * Tables 8-14 and 8-15 give the calculation phase per operand type -- a zero
 * source to `FADD` costs 2, an infinity 6, a NAN 28 -- and Table 8-3's column
 * is the *typical* case, §8.5.1's "input operands are assumed to be normalized
 * numbers in the legal range for a given function". A program feeding zeros and
 * infinities runs faster on the real part than this model says.
 *
 * One further piece is not modelled and is named here rather than left to be
 * discovered:
 *
 * - **Rounding and exception handling.** Table 8-18 adds 6 clocks for an
 *   extended round with no exception -- which is what Table 8-3 assumes -- and
 *   up to 60 for a single-precision underflow with round overflow. Only the
 *   assumed case is charged.
 *
 * **The concurrency is modelled**, as of 2026-09-07 -- see the section below and
 * `ap_m68030_step.c`, which composes it across consecutive floating-point
 * instructions. What the manual does not specify, and this core therefore
 * chooses, is when a sequence *ends*: §8.5.1.3's tail is "the period during
 * which the MC68882 can begin another floating-point instruction", so a tail
 * that meets an instruction which is not one is discarded rather than held for
 * a later `FADD`. That charges more than the hardware would and never less.
 *
 * What is *not* a caveat here: §8.5.1's NOTE that "the timing numbers are
 * derived assuming that the main processor is an MC68020. The MC68030 has a
 * more optimized coprocessor interface ... Actual operation when using the
 * MC68030 always yields better values than the calculations derived from these
 * tables." That warning is about the MPU-dependent phases, which this module
 * does not take from the tables at all.
 */

#ifndef APOLLO_CPU_M68882_AP_M68882_TIMING_H
#define APOLLO_CPU_M68882_AP_M68882_TIMING_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68882/ap_m68882_decode.h"
#include "cpu/m68882/ap_m68882_format.h"

/* ---------------------------------------------------------------------------
 * Concurrency, §8.5.1.3 and Table 8-5
 *
 * The MC68882's conversion unit can hold one instruction while the arithmetic
 * unit runs another, so a run of floating-point instructions costs less than
 * the sum of their totals. §8.5.1.3 names the two quantities Table 8-3 carries
 * for it:
 *
 *   "H -- Head. The effective address calculation should be added to the head
 *   to obtain the true head time. (This does not apply for FMOVE to memory if
 *   a register conflict occurs.)"
 *   "T -- Tail. The period during which the MC68882 can begin another
 *   floating-point instruction."
 *   "The total execution time for a set of instructions is the sum of the
 *   overall execution times of the individual instructions in the set minus the
 *   total overlap time. This formula applies to both the MC68881 and the
 *   MC68882; **for the MC68881, the overlap between floating-point instructions
 *   is zero**."
 *
 * ## The `T = *` rule is what makes this more than the 68030's Equation (11-1)
 *
 * Table 8-3 prints `*` for the tail of every `FMOVE`, and its footnote says
 * "these instruction do not have a tail time. **The next instruction's head can
 * be added to determine the effective head time.**" §8.5.1.3 restates it: "where
 * T is shown as `T = *`, the effective head time is the sum of the FMOVE H time
 * plus the H time of the subsequent instruction."
 *
 * So a fully-concurrent `FMOVE` is *transparent*: it has no tail of its own,
 * and instead merges its head into the following instruction's, so the tail
 * before it overlaps with the pair. That needs one instruction of lookahead,
 * which is why this accumulator defers rather than settling each instruction as
 * it arrives -- and why a model built as a straight copy of
 * `ap_m68030_overlap` would drop every `FMOVE`'s contribution.
 *
 * ## The overlap itself
 *
 * §8.5.1.3, the fourth column of Table 8-5: "the actual overlap time, which is
 * the lesser of the effective tail and the effective head of the column to the
 * left." Same `min` as the 68030's, over different quantities.
 *
 * ## What the effective address adds, and the one place it does not
 *
 * Table 8-3's footnote `***`: "Add the effective address time to obtain overall
 * execution time. Add the effective address time to obtain effective head time.
 * **(This does not apply to the FMOVE to memory instruction.)**" §8.5.1.3 says
 * it again -- "for the FMOVE to memory instructions (opclass 011), the effective
 * address calculation is not added to the head time."
 *
 * **Table 8-5 contradicts both of them and it does not matter.** Its rows for
 * `FMOVE.D FP2,<ea>` and `FMOVE.D FP1,<ea>` print an adjusted head of
 * `44 + 6 = 50`, adding the very effective address time the footnote excludes;
 * and its second `FMUL`/`FMOVE` pair prints an effective tail of `59` and an
 * effective head of `51 + 42 = 93` where the identical first pair prints `58`
 * and `50 + 42 = 92`. All three are off by one or six against their own inputs.
 *
 * None of them changes an answer: the tails (58, 38) are below the heads (86 or
 * 92) in every pair, so the `min` is the tail either way, and the table's own
 * **Actual Overlap** column and its printed total of **175** are what the two
 * texts predict. `min(59, 93)` would make the total 176, which the table does
 * not print. So the intermediate cells are the error and the result is right --
 * which is why the test composes the whole example and checks 331, rather than
 * checking a cell.
 * ------------------------------------------------------------------------- */

/* Table 8-3's `H` and `T` for one operation. */
typedef struct {
  unsigned head;
  /* Meaningless when `has_tail` is false: Table 8-3 prints `*`, not a number,
   * and zero is a different claim -- a zero tail overlaps with nothing, where
   * `*` merges into the next instruction's head. */
  unsigned tail;
  bool has_tail;
} ap_m68882_concurrency_t;

/* Table 8-3's `FPn to FPm` `H` and `T`, the same column
 * `ap_m68882_operation_clocks` takes its total from and for the same reason:
 * the memory columns differ from it by the operand transfer, which this core
 * measures on the bus. `H` is 17 for every arithmetic operation there, 21 for
 * `FMOVE to FPn` and 10 for `FMOVECR`; the tails are what separate an `FABS`
 * from an `FATANH`. */
[[nodiscard]] ap_m68882_concurrency_t
ap_m68882_operation_concurrency(ap_m68882_operation_t operation);

/* Accumulates §8.5.1.3 across a sequence of floating-point instructions.
 *
 * `deferred_head` is the `T = *` lookahead: a no-tail instruction parks its
 * head here until the next instruction's head can be added to it. */
typedef struct {
  uint64_t total;
  unsigned pending_tail;
  bool has_pending_tail;
  unsigned deferred_head;
  bool has_deferred_head;
} ap_m68882_overlap_state_t;

[[nodiscard]] ap_m68882_overlap_state_t ap_m68882_overlap_begin(void);

/* Add one instruction. `total` is its overall execution time including whatever
 * the effective address cost; `head` is its **adjusted** head -- Table 8-3's `H`
 * plus that same effective address time, except for `FMOVE` to memory where the
 * footnote excludes it.
 *
 * Returns the overlap this instruction claimed, which is the quantity a running
 * machine needs: the accumulator's own total is for composing a closed sequence,
 * and a machine has none. The two are the same arithmetic seen from either end,
 * so there is one implementation and no second rule to drift. */
unsigned ap_m68882_overlap_add(ap_m68882_overlap_state_t *state, unsigned total,
                               unsigned head, unsigned tail, bool has_tail);

/* End a sequence, returning the overlap a trailing no-tail instruction claims
 * and clearing the state. Table 8-5's last row is that case: an `FMOVE` with
 * nothing after it, whose effective head is its own and whose overlap is
 * `min(38, 21) = 21`.
 *
 * A machine calls this when the run of floating-point instructions ends. What
 * *ends* it is the caller's judgement and not the manual's -- see
 * `ap_m68030_step.c`, which discards the result rather than crediting it to an
 * instruction the manual gives no head for. */
unsigned ap_m68882_overlap_flush(ap_m68882_overlap_state_t *state);

/* The sequence total. Settles a trailing no-tail instruction, whose effective
 * head is then its own head alone -- Table 8-5's last row, where the overlap is
 * `min(38, 21) = 21` and the head is the smaller of the two. That row is the
 * one that tells this rule apart from "the overlap is always the tail". */
[[nodiscard]] uint64_t
ap_m68882_overlap_total(const ap_m68882_overlap_state_t *state);

/* Table 8-3's `FPn to FPm` total for a general-type operation, in FPCP clock
 * cycles. Zero for an encoding Table 8-3 does not list. */
[[nodiscard]] unsigned
ap_m68882_operation_clocks(ap_m68882_operation_t operation);

/* Table 8-3's `FMOVE to memory` row, whose `H` is per destination format where
 * the arithmetic rows share one:
 *
 *     Integer   H 0,  T 0,  Total 110
 *     Single    H 38, T -,  Total 38
 *     Double    H 44, T -,  Total 44
 *     Extended  H 50, T -,  Total 50
 *     Packed    H 0,  T 0,  Total 2006
 *
 * where `T -` is the table's asterisk.
 *
 * The three real formats carry `T = *` -- no tail, merging forward like every
 * other `FMOVE` -- and the two that convert through a much longer path carry a
 * genuine zero. */
[[nodiscard]] ap_m68882_concurrency_t
ap_m68882_store_concurrency(ap_m68882_format_t format);

/* The output conversion an `FMOVE FPm,<ea>` performs, Table 8-16 and Table
 * 8-17, for a normalized source with no underflow, overflow or round overflow
 * -- the same assumption Table 8-3's memory row makes. */
[[nodiscard]] unsigned ap_m68882_store_clocks(ap_m68882_format_t format);

#endif /* APOLLO_CPU_M68882_AP_M68882_TIMING_H */
