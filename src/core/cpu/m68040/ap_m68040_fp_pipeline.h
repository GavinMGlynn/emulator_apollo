/* MC68040 §10.7.3, from the page images: the timings *inside* the FPU.
 *
 * §10.7.2 priced what the integer unit spends fetching an operand and handing
 * it over. This is the other half -- what the floating-point unit then does
 * with it -- and it is indexed by nothing the addressing modes know about.
 *
 * ## Three stages, and a busy time that is not the execution time
 *
 * §10.7.3's opening sentence defines a notation used nowhere else in Section 10:
 * "times in parentheses are the total time that the stage uses to execute an
 * instruction even though the stage can pass data to the next stage earlier. So
 * that 2(3) in the conversion stage means that the instruction takes two cycles
 * to execute, but this stage is actually busy for three cycles."
 *
 * Two different numbers, both true, answering different questions. The bare
 * figure is *latency* -- when the next stage may start. The parenthesised one is
 * *occupancy* -- when another instruction may enter this stage. A model that
 * kept only the first would let two instructions occupy one stage at once; a
 * model that kept only the second would serialise a pipeline that overlaps. So
 * both are stored, and a figure printed without parentheses has them equal.
 *
 * ## Half cycles are real, so the unit here is the half cycle
 *
 * `FDIV` executes in **37.5** cycles, `FMOVE` to an integer converts in 1.5,
 * and one busy time is printed 12.5. Every figure in §10.7.3 is a whole
 * multiple of a half cycle and several are not whole cycles, so the numbers are
 * held in **half cycles as exact integers** rather than as a floating-point
 * count that would make `37.5 + 37.5` a question about rounding. Every accessor
 * says `_halves` in its name for the same reason: this is the one table in
 * Section 10 whose unit is not the clock, and a reader who forgets that is off
 * by a factor of two.
 *
 * A cycle here is an FPU cycle, which §10.7's preamble makes the same clock the
 * integer unit counts -- these figures add to §10.7.2's, they do not scale them.
 *
 * ## The index is the operand, not the address
 *
 * A row is selected by five things: the mnemonic, the opclass, the source
 * *size*, the rounding *precision*, and the **class of the operands themselves**
 * -- whether they are normalised, zero, infinite or NANs. That last one is why
 * this table cannot be folded into §10.7.2's: the cost depends on the values, so
 * it is not knowable until the operands are in hand. A zero or a NAN short-
 * circuits the arithmetic entirely, which is why almost every such row prices
 * the execution and normalisation stages at zero and pays only the conversion.
 */

#ifndef APOLLO_CPU_M68040_AP_M68040_FP_PIPELINE_H
#define APOLLO_CPU_M68040_AP_M68040_FP_PIPELINE_H

#include <stdbool.h>
#include <stddef.h>

/* Sizes and precisions are *sets*: the table prints "S,D" and "B,W,L" as single
 * rows, so a row matches any member. Bit per format. */
typedef enum {
  AP_M68040_FP_SIZE_NONE = 0u, /* the table's dash: no size field applies */
  AP_M68040_FP_SIZE_B = 1u << 0,
  AP_M68040_FP_SIZE_W = 1u << 1,
  AP_M68040_FP_SIZE_L = 1u << 2,
  AP_M68040_FP_SIZE_S = 1u << 3,
  AP_M68040_FP_SIZE_D = 1u << 4,
  AP_M68040_FP_SIZE_X = 1u << 5,
} ap_m68040_fp_size_t;

#define AP_M68040_FP_PRECISION_ANY \
  (AP_M68040_FP_SIZE_S | AP_M68040_FP_SIZE_D | AP_M68040_FP_SIZE_X)

/* The operand-class column, one entry per label the table prints. These are the
 * table's own index, not a classification of ours -- `Norm,Zero` and
 * `Zero,Zero` are separate rows with different figures, and `(Zero|Inf|NAN)`
 * is one row covering three classes because they cost the same. Keeping the
 * printed labels means a row is checkable against the page. */
typedef enum {
  AP_M68040_FP_OPERANDS_ANY,          /* "Any", or the dash on FMOVEM */
  AP_M68040_FP_OPERANDS_NORM_NORM,    /* "Norm,Norm" */
  AP_M68040_FP_OPERANDS_NORM_ZERO,    /* "Norm,Zero" */
  AP_M68040_FP_OPERANDS_ZERO_ZERO,    /* "Zero,Zero" */
  AP_M68040_FP_OPERANDS_ANY_ZERO,     /* "-,Zero" */
  AP_M68040_FP_OPERANDS_ANY_INF,      /* "-,Inf" */
  AP_M68040_FP_OPERANDS_ANY_NAN,      /* "-,NAN" */
  AP_M68040_FP_OPERANDS_NORM,         /* "Norm", monadic */
  AP_M68040_FP_OPERANDS_NAN,          /* "NAN", monadic */
  AP_M68040_FP_OPERANDS_ZERO_INF_NAN, /* "(Zero|Inf|NAN)" */
  AP_M68040_FP_OPERANDS_NORM_ZERO_INF,/* "(Norm|Zero|Inf)" */
  AP_M68040_FP_OPERANDS_ZERO_INF,     /* "(Zero|Inf)" */
  AP_M68040_FP_OPERANDS_POSITIVE,     /* "(+Norm|Zero)" and "+(Norm|Zero)" */
  AP_M68040_FP_OPERANDS_NEGATIVE,     /* "-Norm" and "-(Norm|Zero)" */
  AP_M68040_FP_OPERANDS_COUNT
} ap_m68040_fp_operands_t;

/* One pipeline stage's two figures, both in half cycles.
 *
 * `busy` is never less than `latency`: a stage cannot finish occupying itself
 * before it has produced its result. Where the table prints no parenthesis the
 * two are equal, which is the honest reading -- an unparenthesised figure is
 * not a missing busy time but a stage that frees itself the moment it is
 * done. */
typedef struct {
  unsigned latency_halves;
  unsigned busy_halves;
} ap_m68040_fp_stage_t;

typedef struct {
  const char *instruction;
  unsigned opclass;
  ap_m68040_fp_size_t size;      /* `AP_M68040_FP_SIZE_NONE` for the dash */
  unsigned precision;            /* a mask; `AP_M68040_FP_PRECISION_ANY` */
  ap_m68040_fp_operands_t operands;
  ap_m68040_fp_stage_t conversion;
  ap_m68040_fp_stage_t execution;
  ap_m68040_fp_stage_t normalization;
  /* `FMOVEM` alone prints "2 + (2 per reg)" and "2 + (3 per reg)": the
   * conversion stage grows with the register list, exactly as §10.7.2's
   * `FMOVEM` cell did. Zero for every other row. */
  unsigned conversion_per_register_halves;
} ap_m68040_fp_row_t;

[[nodiscard]] size_t ap_m68040_fp_row_count(void);
[[nodiscard]] const ap_m68040_fp_row_t *ap_m68040_fp_row(size_t index);

/* The first row matching all five keys, or NULL. `size` and `precision` are
 * single values from the caller and are matched against the row's *set*, so a
 * query of `D` selects a row printed "S,D". A `size` of
 * `AP_M68040_FP_SIZE_NONE` matches only the rows that print a dash. */
[[nodiscard]] const ap_m68040_fp_row_t *
ap_m68040_fp_find(const char *instruction, unsigned opclass,
                  ap_m68040_fp_size_t size, ap_m68040_fp_size_t precision,
                  ap_m68040_fp_operands_t operands);

/* Total latency through all three stages, in half cycles. This is what an
 * instruction costs once its operand has arrived; it adds to §10.7.2's execute
 * figure rather than replacing it. */
[[nodiscard]] unsigned ap_m68040_fp_total_latency_halves(const ap_m68040_fp_row_t *row);

/* `FMOVEM`'s conversion for a list of `registers`. Unlike §10.7.2's `FMOVEM`
 * adjustment, the printed figure here is the *empty* case -- "2 + (2 per reg)"
 * is a base of 2 plus the per-register term, so the count starts at zero and no
 * register is already paid for. The two tables' `FMOVEM` figures therefore
 * count differently, and reading either one's convention onto the other
 * misprices every list. */
[[nodiscard]] unsigned
ap_m68040_fp_movem_conversion_halves(const ap_m68040_fp_row_t *row,
                                     unsigned registers);

#endif /* APOLLO_CPU_M68040_AP_M68040_FP_PIPELINE_H */
