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
 * 1. §10.6's rows gain an `FPn` row. §10.7.2's own pages disagree about which
 *    of `FPn`, `Dn` and `An` they print: the load table has `FPn` and no `An`,
 *    the store table has `Dn` and `An` and no `FPn`, and `FSAVE`/`FRESTORE`
 *    print all three. So this enum is the *union* -- eighteen rows -- and each
 *    table dashes what its own page does not print. A per-table enum would
 *    have made every cross-table comparison a translation.
 *
 *    The row *order* also shuffles between pages: 10-33 prints `(xxx)` and
 *    `#<xxx>` before `(d16,An)`, and 10-34 prints them after. Each page's
 *    labels were read individually rather than assumed from the one before.
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

/* The union of every §10.7.2 page's rows: see above. */
typedef enum {
  AP_M68040_FPU_FPN,
  AP_M68040_FPU_DN,
  AP_M68040_FPU_AN,
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
 * Page 10-31: `FMOVE FPn,<ea>`, the store direction.
 *
 * Its format axis groups differently from the load table's -- "Byte, Word, and
 * Long Word", "Single and Double Precision", "Extended Precision" -- so it gets
 * its own enum rather than a lossy mapping onto the load table's five. The
 * regrouping is itself the finding: on the way *in* a long word and a single
 * cost the same and byte/word differs; on the way *out* all three integer
 * widths cost the same and single joins double.
 * ------------------------------------------------------------------------- */

typedef enum {
  AP_M68040_FPU_STORE_BYTE_WORD_LONG,
  AP_M68040_FPU_STORE_SINGLE_DOUBLE,
  AP_M68040_FPU_STORE_EXTENDED,
  AP_M68040_FPU_STORE_FORMAT_COUNT
} ap_m68040_fpu_store_format_t;

[[nodiscard]] ap_m68040_fpu_cell_t
ap_m68040_fpu_store(ap_m68040_fpu_store_format_t format,
                    ap_m68040_fpu_mode_t mode);

/* ---------------------------------------------------------------------------
 * Page 10-32: three single-column tables.
 * ------------------------------------------------------------------------- */

/* "FMOVE/FMOVEM to/from 1 Control Register", note a: "same as FMOVE
 * <ea>,FPCR". One control register, so no list to depend on. */
[[nodiscard]] ap_m68040_fpu_cell_t
ap_m68040_fpu_control(ap_m68040_fpu_mode_t mode);

/* "FMOVEM <list>,<ea> and <ea>,<list>". Both directions share one column, and
 * the figures are for a **single** register: note b says "add three clocks to
 * both <ea> calculate and execute times for each additional floating-point
 * register. Add one clock to both <ea> calculate and execute times for dynamic
 * register list."
 *
 * So the table cell is a base and the list is the variable, exactly as §10.6's
 * `MOVEM` printed `2 + D' + A'`. Kept as an adjustment rather than folded in:
 * the register count is in the instruction's extension word, which no table
 * indexed by addressing mode can see. */
[[nodiscard]] ap_m68040_fpu_cell_t
ap_m68040_fpu_movem(ap_m68040_fpu_mode_t mode);

#define AP_M68040_FPU_MOVEM_PER_EXTRA_REGISTER 3u
#define AP_M68040_FPU_MOVEM_DYNAMIC_LIST 1u

/* `registers` is the total in the list and must be at least one -- the table
 * cell already prices the first, so a zero-register `FMOVEM` is not a cheaper
 * case but an unencodable one. `dynamic` is the register-list-in-a-register
 * form. Both adjustments land on calculate and on the execute *base*. */
[[nodiscard]] ap_m68040_fpu_cell_t
ap_m68040_fpu_movem_with_list(ap_m68040_fpu_cell_t cell, unsigned registers,
                              bool dynamic);

/* `FScc`, note a. */
[[nodiscard]] ap_m68040_fpu_cell_t ap_m68040_fpu_scc(ap_m68040_fpu_mode_t mode);

/* ---------------------------------------------------------------------------
 * Pages 10-33 and 10-34: `FSAVE <ea>` and `FRESTORE <ea>`.
 *
 * Keyed by the FPU *state frame* -- how much internal state there is to move --
 * which is neither an operand format nor an addressing mode but a property of
 * what the FPU was doing when it was interrupted. §9's frame formats: null and
 * idle are the empty cases, short and long are the two sizes of saved state.
 * ------------------------------------------------------------------------- */

typedef enum {
  AP_M68040_FPU_FRAME_IDLE_OR_NULL,
  AP_M68040_FPU_FRAME_SHORT,
  AP_M68040_FPU_FRAME_LONG,
  AP_M68040_FPU_FRAME_COUNT
} ap_m68040_fpu_frame_t;

[[nodiscard]] ap_m68040_fpu_cell_t
ap_m68040_fpu_save(ap_m68040_fpu_frame_t frame, ap_m68040_fpu_mode_t mode);

[[nodiscard]] ap_m68040_fpu_cell_t
ap_m68040_fpu_restore(ap_m68040_fpu_frame_t frame, ap_m68040_fpu_mode_t mode);

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
