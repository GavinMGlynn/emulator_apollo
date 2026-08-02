/* MC68040 miscellaneous integer instruction timings.
 *
 * `MC68040 User's Manual (1993)` §10.5, transcribed from the page images.
 *
 * ## Why the page images, again
 *
 * `pdftotext` renders this table with `Bcc` as `Bee`, `NOP` as `NOpa`, `MOVEP`
 * as `MOVEpc` -- and, worst of all, **`MOVEQ` as `MOVEa`**. That last one is
 * not a typo a reader would notice: `MOVE` is a real instruction with a real
 * row elsewhere in §10.4, so an extracted table would have silently given
 * `MOVE` the timing of `MOVEQ`. The figures themselves survive extraction; the
 * instruction names do not.
 *
 * ## Not every figure here is a number
 *
 * Three of the table's six notes qualify the timings rather than explaining
 * them, and a core that reported all of these as exact would be claiming a
 * precision the manual withholds:
 *
 *   a. "Times listed are **minimum**."
 *   b. "Times listed are **typical**."
 *   e. "**Typical measurement** for three-level table search with no descriptor
 *      writes, no entries cached, and four-clock memory access times."
 *
 * Note `e` is the most conditional figure in the table: `PTESTR`/`PTESTW` at
 * 25 and `11L + 14` describes one particular table search against one
 * particular memory, and a machine with different memory would not reproduce
 * it. It is transcribed because it is what the manual says, and marked so that
 * nothing downstream mistakes it for a measurement of *this* machine.
 *
 * The remaining notes describe pipeline behaviour: `c` marks the instructions
 * that interlock the `<ea> calculate` and execute stages, `d` says "successive
 * in-line MOVE16 instructions each add eight clocks to the <ea> calculate and
 * execute times" -- a cost that depends on the *previous* instruction, which no
 * per-instruction figure can carry -- and `f` interlocks and additionally
 * behaves like note `a` when its exception is taken.
 */

#ifndef APOLLO_CPU_M68040_AP_M68040_MISC_TIMING_H
#define APOLLO_CPU_M68040_AP_M68040_MISC_TIMING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cpu/m68040/ap_m68040_timing.h"

/* How far a figure can be trusted. */
typedef enum {
  /* No note: the figure is what the instruction costs. */
  AP_M68040_TIMING_EXACT,
  AP_M68040_TIMING_MINIMUM, /* note a, and note f when the exception is taken */
  AP_M68040_TIMING_TYPICAL, /* notes b and e */
} ap_m68040_timing_confidence_t;

typedef struct {
  const char *instruction;
  /* The table's `Condition` column, or NULL where it prints a dash. */
  const char *condition;
  unsigned calculate;
  ap_m68040_execute_t execute;
  ap_m68040_timing_confidence_t confidence;
  /* Note c, f, and the two that imply it: whether this instruction interlocks
   * the `<ea> calculate` and execute stages regardless of addressing mode. */
  bool interlocks;
  /* Note d: `MOVE16` alone, where "successive in-line MOVE16 instructions each
   * add eight clocks to the <ea> calculate and execute times". */
  bool successive_penalty;
} ap_m68040_misc_timing_t;

#define AP_M68040_MOVE16_SUCCESSIVE_PENALTY 8u

/* The table, in the manual's order. */
[[nodiscard]] const ap_m68040_misc_timing_t *ap_m68040_misc_timings(void);
[[nodiscard]] size_t ap_m68040_misc_timing_count(void);

/* Find a row by instruction and condition. `condition` may be NULL to match the
 * first row for the instruction. Returns NULL when the table has no such row --
 * which is the honest answer for an instruction §10.5 does not cover, since the
 * other sections price those. */
[[nodiscard]] const ap_m68040_misc_timing_t *
ap_m68040_misc_timing_find(const char *instruction, const char *condition);

#endif /* APOLLO_CPU_M68040_AP_M68040_MISC_TIMING_H */
