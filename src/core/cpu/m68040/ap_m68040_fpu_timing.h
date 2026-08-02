/* MC68040 §10.7, from the page images: what the *integer* unit spends on a
 * floating-point instruction.
 *
 * ## This is not the cost of the arithmetic
 *
 * §10.7's opening paragraph is the whole reason this is a separate module from
 * `ap_m68040_fpu.c`: "the integer pipeline passes the decoded instruction to
 * the floating-point unit for execution, then supports the floating-point unit
 * by calculating effective addresses and transferring operands to and from this
 * unit. For these instructions, the execution times listed in the integer unit
 * timing section show the overhead required by the integer unit to support the
 * floating-point unit, **assuming the floating-point unit is not busy with the
 * previous floating-point instructions**."
 *
 * So every figure here is address formation and operand transfer, priced with
 * an idle FPU -- which §10.7.2's own footnote repeats: "timings are for an idle
 * FPU". What the FPU then *does* with the operand is §10.7.3's table and is
 * added separately. A model that treated these as the cost of an `FADD` would
 * report a divide and an add as costing the same, because to the integer unit
 * they do.
 *
 * ## Four facts the preamble states outright
 *
 * - "The order of operands is generally not significant for timing purposes."
 * - "Different rounding modes (i.e., round to zero, etc.) never incur a time
 *   penalty." So the rounding mode is free, and only the *precision* is not.
 * - "Instructions with an S or D (e.g., FSADD) have the same effect as setting
 *   the rounding precision to S or D." A rounding-precision suffix is a
 *   precision selection, not a separate opcode with its own timing.
 * - "All FMOVEM instructions wait for the pipe to idle before starting", which
 *   is why `FMOVEM` cannot be priced from this table at all.
 *
 * ## The mode list is not §10.6's
 *
 * Two differences, both meaning `ap_m68040_iu_mode_t` cannot be reused:
 *
 * 1. §10.6's `An` row is replaced by an `FPn` row. A floating-point operation
 *    takes no address register as an operand and does take a floating-point
 *    one, so the two tables index different things at that position.
 * 2. §10.6 names the base register `BR` -- `(BR,Xn)`, `([bd,BR],Xn)` -- and
 *    prices the PC as one of its values. §10.7.2 names it `An` throughout and
 *    pushes the PC case into a footnote: "for BR = PC, add one clock to both
 *    <ea> calculate and execute times". Same six modes, opposite convention,
 *    so the PC base is a *penalty* here and a row there.
 */

#ifndef APOLLO_CPU_M68040_AP_M68040_FPU_TIMING_H
#define APOLLO_CPU_M68040_AP_M68040_FPU_TIMING_H

#include <stdbool.h>
#include <stddef.h>

#include "cpu/m68040/ap_m68040_timing.h"

/* §10.7.2's rows, in printed order. `FPN` first and no `AN`: see above. */
typedef enum {
  AP_M68040_FPU_FPN,
  AP_M68040_FPU_DN,
  AP_M68040_FPU_INDIRECT,
  AP_M68040_FPU_POSTINCREMENT,
  AP_M68040_FPU_PREDECREMENT,
  AP_M68040_FPU_DISPLACEMENT,
  AP_M68040_FPU_PC_DISPLACEMENT,
  AP_M68040_FPU_ABSOLUTE,
  AP_M68040_FPU_IMMEDIATE,
  AP_M68040_FPU_INDEXED,
  AP_M68040_FPU_PC_INDEXED,
  AP_M68040_FPU_BASE_INDEXED,
  AP_M68040_FPU_BASE_DISPLACEMENT,
  AP_M68040_FPU_MEMORY_PREINDEXED,
  AP_M68040_FPU_MEMORY_PREINDEXED_OD,
  AP_M68040_FPU_MEMORY_POSTINDEXED,
  AP_M68040_FPU_MEMORY_POSTINDEXED_OD,
  AP_M68040_FPU_MODE_COUNT
} ap_m68040_fpu_mode_t;

/* §10.7.2's second axis: the *source operand format*, which is what the
 * instruction's size field selects. Byte and word share one column -- widening
 * a byte and widening a word cost the same -- so there is no separate byte
 * entry, and a model that offered one would be inventing a distinction the
 * table declines to make. */
typedef enum {
  AP_M68040_FPU_FORMAT_BYTE_WORD,
  AP_M68040_FPU_FORMAT_LONG,
  AP_M68040_FPU_FORMAT_SINGLE,
  AP_M68040_FPU_FORMAT_DOUBLE,
  AP_M68040_FPU_FORMAT_EXTENDED,
  AP_M68040_FPU_FORMAT_COUNT
} ap_m68040_fpu_format_t;

typedef struct {
  bool valid;
  unsigned calculate;
  ap_m68040_execute_t execute;
} ap_m68040_fpu_cell_t;

/* The `<ea>,FPn` group: "FABS, FADD, FCMP, FDIV, FMOVE, FMUL, FNEG, FSQRT,
 * FSUB, FTST". Ten mnemonics, one column set -- because to the integer unit
 * they are the same instruction, differing only in what the FPU does after the
 * operand arrives. */
[[nodiscard]] ap_m68040_fpu_cell_t
ap_m68040_fpu_support(ap_m68040_fpu_format_t format, ap_m68040_fpu_mode_t mode);

/* Whether `name` is one of the ten priced by `ap_m68040_fpu_support`. */
[[nodiscard]] bool ap_m68040_fpu_support_prices(const char *name);
[[nodiscard]] size_t ap_m68040_fpu_support_count(void);
[[nodiscard]] const char *ap_m68040_fpu_support_name(size_t index);

/* The footnote: "for BR = PC, add one clock to both <ea> calculate and execute
 * times". Applied to the cell rather than folded in, because the base register
 * is an encoding property the table cannot know. The added clock lands on the
 * execute *base*, not the lead: a lead is stall tolerance and forming a longer
 * address does not make the following instruction more able to overlap. */
[[nodiscard]] ap_m68040_fpu_cell_t
ap_m68040_fpu_with_pc_base(ap_m68040_fpu_cell_t cell);

/* Whether `mode` even has a base register for the footnote to apply to. The six
 * deep modes do; nothing else does, and quietly charging the clock on a mode
 * with no `BR` field would be a fabricated figure. */
[[nodiscard]] bool ap_m68040_fpu_mode_has_base_register(ap_m68040_fpu_mode_t mode);

/* ---------------------------------------------------------------------------
 * §10.7.1 Miscellaneous Integer Unit Support Timings.
 *
 * A different shape again: keyed by instruction and *condition outcome*, with
 * no addressing mode at all. These four instructions either take a branch or
 * do not, and the outcome is the only thing their cost depends on.
 * ------------------------------------------------------------------------- */

typedef enum {
  /* `FBcc`. */
  AP_M68040_FPU_MISC_FBCC_TAKEN,
  AP_M68040_FPU_MISC_FBCC_NOT_TAKEN,
  /* `FDBcc`. Note which way round these run: the loop *continues* when the
   * condition is false, so `cc False` is the branch-taken case and the dearer
   * of the two. Reading `FDBcc` like `FBcc` gets both figures backwards. */
  AP_M68040_FPU_MISC_FDBCC_TRUE,
  AP_M68040_FPU_MISC_FDBCC_FALSE,
  /* `FNOP`, priced only for an idle FPU -- there is no other case worth a
   * figure, since a busy FPU makes `FNOP` a wait of unbounded length. */
  AP_M68040_FPU_MISC_FNOP_IDLE,
  /* `FTRAPcc`. Only the not-taken case is priced: taking the trap costs the
   * exception, which §10.7 does not price and this module will not invent. */
  AP_M68040_FPU_MISC_FTRAPCC_NOT_TAKEN,
  AP_M68040_FPU_MISC_COUNT
} ap_m68040_fpu_misc_t;

[[nodiscard]] ap_m68040_fpu_cell_t ap_m68040_fpu_misc(ap_m68040_fpu_misc_t which);
[[nodiscard]] const char *ap_m68040_fpu_misc_instruction(ap_m68040_fpu_misc_t which);
[[nodiscard]] const char *ap_m68040_fpu_misc_condition(ap_m68040_fpu_misc_t which);

#endif /* APOLLO_CPU_M68040_AP_M68040_FPU_TIMING_H */
