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

typedef struct {
  /* False where the table prints a dash: the mode is invalid for this group. */
  bool valid;
  unsigned calculate;
  ap_m68040_execute_t execute;
  /* Some shift and rotate cells print two figures, as `3/4`, with the footnote
   * "immediate count specified for shift count/shift count specified in
   * register, respectively". The first is `execute`; this is the second, and
   * `has_register_count` says whether the distinction applies at all.
   *
   * It applies only to the `Dn` row -- a shift of a memory operand is always by
   * one -- so most cells leave it false, and a caller that ignored it would
   * under-price exactly the register-count shifts a compiler emits most. */
  bool has_register_count;
  ap_m68040_execute_t register_count_execute;
} ap_m68040_iu_cell_t;

typedef struct {
  /* The instructions this column prices, NULL-terminated. They share a column
   * because their timings are identical. */
  const char *const *instructions;
  const ap_m68040_iu_cell_t *cells; /* AP_M68040_IU_MODE_COUNT of them */
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

/* The execute figure for a cell, choosing between the two the table prints when
 * it prints two. `register_count` is ignored where the distinction does not
 * apply, so a caller may pass it unconditionally. */
[[nodiscard]] ap_m68040_execute_t
ap_m68040_iu_execute(ap_m68040_iu_cell_t cell, bool register_count);

#endif /* APOLLO_CPU_M68040_AP_M68040_IU_TIMING_H */
