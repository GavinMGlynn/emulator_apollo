/* MC68030 effective address timings: `[030]` §11.6.1 and §11.6.3.
 *
 * The instruction tables in `ap_m68030_timing_table.h` publish, for many rows, a
 * *component* rather than a total: `ADD Dn,EA` is 3 clocks plus "Fetch Effective
 * Address Time", and the oracle measures the whole instruction at 7. These are
 * the other side of that composition.
 *
 * ## Fetch against calculate, and why an instruction needs one or the other
 *
 * §11.6.1 is the **fetch** effective address time (`fea`): the address is
 * computed *and the operand read*. §11.6.3 is the **calculate** time (`cea`):
 * the address is computed and nothing is read. Which an instruction needs is
 * given by its own footnote — `*` for fetch, and the calculate table for the
 * rows marked with the corresponding symbol.
 *
 * The difference is visible in the numbers: `(An)` is `3(1/0/0)` to fetch and
 * `2(0/0/0)` to calculate, the `1` being the operand read the fetch performs.
 * Using the wrong one costs or saves a memory access, which is exactly the size
 * of error `FINDINGS.md` C9 measured.
 *
 * ## Two notations the tables use that are not numbers
 *
 * The register-direct rows give head and tail as `-` rather than 0. A register
 * has no address to compute, so there is nothing to overlap *with*; that is a
 * different statement from an overlap of zero, and `head_applies` carries it.
 *
 * The calculate table writes several heads as **"2+op head"** — the head is the
 * operation's own head plus two, not a constant. That is the table expressing a
 * dependency between the two halves of Equation (11-2), and flattening it to 2
 * would silently drop whatever the operation contributes. `head_adds_operation`
 * carries it, and a caller composing the two must add the operation's head.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_EA_TIMING_H
#define APOLLO_CPU_M68030_AP_M68030_EA_TIMING_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_ea.h"
#include "cpu/m68030/ap_m68030_overlap.h"

typedef struct {
  const char *mode; /* as §11.6.1 writes it */
  ap_m68030_timing_t timing;

  /* False for the register-direct rows, whose head and tail the table gives as
   * `-`: there is no address computation to overlap with at all. */
  bool head_applies;

  /* True where the table writes "2+op head" or "4+op head": the head is the
   * *operation's* head plus the figure carried in `timing.head`. */
  bool head_adds_operation;
} ap_m68030_ea_timing_t;

/* §11.6.1, Fetch Effective Address: the address computed and the operand read.
 * `operand_size` is read only for the immediate rows, which the table splits by
 * size -- a long immediate costs twice a word one, because it is two words of
 * instruction stream rather than one.
 *
 * NULL for a mode not transcribed, which is the honest answer: the full-format
 * and memory-indirect rows are a separate pass. */
[[nodiscard]] const ap_m68030_ea_timing_t *
ap_m68030_ea_fetch_timing(ap_m68030_ea_kind_t kind, unsigned operand_size);

/* §11.6.3, Calculate Effective Address: the address computed, nothing read. */
[[nodiscard]] const ap_m68030_ea_timing_t *
ap_m68030_ea_calculate_timing(ap_m68030_ea_kind_t kind);

/* §11.6.1's FULL FORMAT EXTENSION WORD(S) rows, selected by the extension word
 * rather than by the addressing mode -- a full-format extension can express
 * sixteen distinct costs behind one mode field, from 6 clocks to 18.
 *
 * ## The reading this depends on, which is `PROVISIONAL`
 *
 * The table publishes its full-format rows in two groups, one written with
 * `d16,An` spelled out and one with `B`, defined by the table's own footnote as
 * "Base Address; 0, An, PC, Xn, An + Xn, PC + Xn. Form does not affect timing".
 * Taken literally those groups overlap and contradict: `(d16,An)` is 6 clocks
 * and `(d16,B)` is 8, and `B` may be `An`.
 *
 * What resolves it is that **every** group A row equals its group B row with
 * the base displacement dropped -- all eight, with no exception -- so the
 * reading is that a *word* base displacement is free when the base is a
 * register and costs 2 clocks when it is not. A long base displacement is never
 * free, which is why group A has no `d32` row at all, and the head column
 * agrees: 2 for the free rows against 4 for the rest.
 *
 * The `MC68020 User's Manual` §9.2.1 corroborates it from outside: it has the
 * same table with the same footnotes and **no `d16,An` group**, its `(d16,An)`
 * costing 2 more than its `(B)`. So the free word displacement is something the
 * 68030 added, which is exactly why only its table needs two groups.
 *
 * Eight rows with no counterexample, a corroborating sibling manual and an
 * agreeing head column is a documented reading rather than a guess -- but it is
 * still a reading, so it is marked `PROVISIONAL` in `PROJECT_STATUS.md` with
 * the measurement that would close it. `docs/references/M68030_TIMING.md` has
 * the full derivation.
 */
[[nodiscard]] const ap_m68030_ea_timing_t *
ap_m68030_ea_fetch_timing_full(const ap_m68030_extension_t *extension);

/* ---------------------------------------------------------------------------
 * Equation (11-2): composing an effective address with its operation.
 * ------------------------------------------------------------------------- */

/* The row's head, with the table's "2+op head" notation resolved against the
 * operation it belongs to.
 *
 * Kept separate from the composition below because it is the one place the
 * notation is interpreted, and because a caller reading a head for any other
 * purpose needs the same resolution. Flattening it to the bare figure would
 * drop whatever the operation contributes -- and for `cea (An)` against
 * `TAS Mem` that is three clocks of head, which is three clocks of overlap the
 * instruction before it can no longer be given. */
[[nodiscard]] unsigned ap_m68030_ea_timing_head(const ap_m68030_ea_timing_t *ea,
                                                unsigned operation_head);

/* Add one instruction as Equation (11-2) composes it: the effective address
 * component, then the operation component.
 *
 * `ea` may be NULL, and a register row -- whose head and tail the table writes
 * `-` rather than 0 -- contributes **no component at all**. Both mean the same
 * thing here, that there is no address computation to overlap with, and both
 * leave the operation to reach back to the previous instruction for its
 * overlap. A zero-cost component in its place would cost nothing itself and
 * still consume the previous tail, which over-counts by up to that tail. */
void ap_m68030_ea_timing_compose(ap_m68030_overlap_state_t *state,
                                 const ap_m68030_ea_timing_t *ea,
                                 const ap_m68030_timing_t *operation);

#endif /* APOLLO_CPU_M68030_AP_M68030_EA_TIMING_H */
