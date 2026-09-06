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
 * Two further pieces are not modelled and are named here rather than left to be
 * discovered:
 *
 * - **The MC68882's concurrency.** §5.1 and Table 8-3's `H`/`T` columns are the
 *   overlap: the conversion unit can hold one instruction while the arithmetic
 *   unit runs another, so a run of floating-point instructions costs less than
 *   the sum of their totals -- Table 8-5's worked example gets 331 clocks for a
 *   sequence whose totals add to 470. This core's part completes each
 *   instruction inside the step that issues it, so it charges the sum. Every
 *   floating-point sequence is therefore an **over**-estimate, in the one
 *   direction the bus time is an under-estimate.
 * - **Rounding and exception handling.** Table 8-18 adds 6 clocks for an
 *   extended round with no exception -- which is what Table 8-3 assumes -- and
 *   up to 60 for a single-precision underflow with round overflow. Only the
 *   assumed case is charged.
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

#include "cpu/m68882/ap_m68882_decode.h"
#include "cpu/m68882/ap_m68882_format.h"

/* Table 8-3's `FPn to FPm` total for a general-type operation, in FPCP clock
 * cycles. Zero for an encoding Table 8-3 does not list. */
[[nodiscard]] unsigned
ap_m68882_operation_clocks(ap_m68882_operation_t operation);

/* The output conversion an `FMOVE FPm,<ea>` performs, Table 8-16 and Table
 * 8-17, for a normalized source with no underflow, overflow or round overflow
 * -- the same assumption Table 8-3's memory row makes. */
[[nodiscard]] unsigned ap_m68882_store_clocks(ap_m68882_format_t format);

#endif /* APOLLO_CPU_M68882_AP_M68882_TIMING_H */
