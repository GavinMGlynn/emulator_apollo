/* MC68040 integer unit instruction timings.
 *
 * `MC68040 User's Manual (1993)` §10.6, transcribed from the page images.
 *
 * ## One table shape, many instructions
 *
 * Every instruction in §10.6 is priced over the same seventeen addressing
 * modes, and the manual groups instructions that share a column: `ADD`, `AND`,
 * `EOR`, `OR`, `SUB` and `TST` are one group because their timings are
 * identical, not because they are related. So the natural unit here is the
 * *group*, and an instruction is looked up by finding the group that names it.
 *
 * ## A dash is not a zero
 *
 * The table prints `—` where an addressing mode is invalid for the group --
 * `ADDI` has no `An` form, `TST` in this grouping has no `#<xxx>` form. Those
 * cells carry no timing because the instruction does not exist, which is a
 * different fact from an instruction that happens to cost nothing. Reporting
 * them as zero would let a decoder price an encoding it should have rejected.
 *
 * ## The mode list is §10.6's, not §10.4's
 *
 * This section prices `(BR,Xn)` and `(bd,BR,Xn)` as separate rows where §10.4's
 * `MOVE` table lists only the latter, so the two sections do not share an
 * addressing-mode enumeration. They are kept apart deliberately: merging them
 * would mean inventing a `(BR,Xn)` column for `MOVE` that the manual does not
 * print.
 */

#ifndef APOLLO_CPU_M68040_AP_M68040_IU_TIMING_H
#define APOLLO_CPU_M68040_AP_M68040_IU_TIMING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cpu/m68040/ap_m68040_timing.h"

/* §10.6's addressing mode column, in the manual's order. */
typedef enum {
  AP_M68040_IU_DN,
  AP_M68040_IU_AN,
  AP_M68040_IU_INDIRECT,
  AP_M68040_IU_POSTINCREMENT,
  AP_M68040_IU_PREDECREMENT,
  AP_M68040_IU_DISPLACEMENT,
  AP_M68040_IU_PC_DISPLACEMENT,
  AP_M68040_IU_ABSOLUTE,
  AP_M68040_IU_IMMEDIATE,
  AP_M68040_IU_INDEXED,
  AP_M68040_IU_PC_INDEXED,
  AP_M68040_IU_BASE_INDEXED,
  AP_M68040_IU_BASE_DISPLACEMENT,
  AP_M68040_IU_MEMORY_PREINDEXED,
  AP_M68040_IU_MEMORY_PREINDEXED_OD,
  AP_M68040_IU_MEMORY_POSTINDEXED,
  AP_M68040_IU_MEMORY_POSTINDEXED_OD,
  AP_M68040_IU_MODE_COUNT,
} ap_m68040_iu_mode_t;

/* What a cell's *second* figure means, where it prints two. §10.6 uses three
 * different distinctions and they are not interchangeable:
 *
 *   shift count      "immediate count specified for shift count/shift count
 *                     specified in register, respectively"
 *   bit number       "bit instruction <ea> calculate and execute times T1/T2
 *                     apply to #<xxx>/Dn bit numbers"
 *   bit field        "immediate count specified for both width and offset and
 *                     width and/or offset specified in register, respectively"
 *
 * The last is the subtlest: it is *not* "the offset is in a register" but
 * "width and/or offset", so one register operand out of two already costs the
 * higher figure. A model that asked only about the offset would under-price
 * half the bit-field instructions a compiler emits. */
typedef enum {
  AP_M68040_IU_ALTERNATE_NONE,
  AP_M68040_IU_ALTERNATE_SHIFT_COUNT,
  AP_M68040_IU_ALTERNATE_BIT_NUMBER,
  AP_M68040_IU_ALTERNATE_BITFIELD_OPERAND,
} ap_m68040_iu_alternate_t;

typedef struct {
  /* False where the table prints a dash: the mode is invalid for this group. */
  bool valid;
  unsigned calculate;
  ap_m68040_execute_t execute;

  /* The second figure, where the table prints one. Both columns can be dual --
   * `BCHG (d16,An)` prints `2/1` for calculate as well as `1L + 3/4` for
   * execute -- so the alternate carries both. */
  ap_m68040_iu_alternate_t alternate;
  unsigned alternate_calculate;
  ap_m68040_execute_t alternate_execute;

  /* Notes c and d: "if the bit field spans a long-word boundary, add ... Two
   * memory addresses are accessed in this case." A conditional penalty rather
   * than a second figure, because it depends on the *operand's address* and
   * not on the encoding -- so no static table could fold it in. */
  unsigned boundary_calculate_penalty;
  unsigned boundary_execute_penalty;
} ap_m68040_iu_cell_t;

/* How far a column's figures can be trusted. §10.6's notes qualify whole
 * columns, not individual cells. */
typedef enum {
  AP_M68040_IU_FIGURE_EXACT,
  /* "Times listed are typical." */
  AP_M68040_IU_FIGURE_TYPICAL,
  /* "Times listed are for Dn within bounds" -- the `CHK` figures assume the
   * check *passes*. A failing check traps, and the trap costs what §10.5's
   * `TRAPcc` row costs, which this column does not include. So these are not
   * a lower bound on `CHK` in general: they are the cost of the case that does
   * not branch. */
  AP_M68040_IU_FIGURE_WITHIN_BOUNDS,
} ap_m68040_iu_confidence_t;

typedef struct {
  /* The instructions this column prices, NULL-terminated. They share a column
   * because their timings are identical. */
  const char *const *instructions;
  const ap_m68040_iu_cell_t *cells; /* AP_M68040_IU_MODE_COUNT of them */
  ap_m68040_iu_confidence_t confidence;
} ap_m68040_iu_group_t;

[[nodiscard]] size_t ap_m68040_iu_group_count(void);
[[nodiscard]] const ap_m68040_iu_group_t *ap_m68040_iu_group(size_t index);

/* Find the group pricing an instruction, or NULL if §10.6 does not cover it --
 * which is the honest answer for the many instructions §10.5 prices instead. */
[[nodiscard]] const ap_m68040_iu_group_t *
ap_m68040_iu_find(const char *instruction);

/* The cell for an instruction and mode. `valid` is false both when the
 * instruction is unknown here and when the mode is invalid for it, so a caller
 * must check it before reading the figures. */
[[nodiscard]] ap_m68040_iu_cell_t
ap_m68040_iu_timing(const char *instruction, ap_m68040_iu_mode_t mode);

/* The figures for a cell, choosing between the two the table prints when it
 * prints two. `alternate` selects the second figure and is ignored where the
 * cell prints only one, so a caller may pass it unconditionally.
 *
 * `spans_long_word` applies notes c and d, and is likewise ignored by cells
 * that carry no penalty. */
[[nodiscard]] unsigned ap_m68040_iu_calculate(ap_m68040_iu_cell_t cell,
                                              bool alternate,
                                              bool spans_long_word);
[[nodiscard]] ap_m68040_execute_t
ap_m68040_iu_execute(ap_m68040_iu_cell_t cell, bool alternate,
                     bool spans_long_word);

#endif /* APOLLO_CPU_M68040_AP_M68040_IU_TIMING_H */
