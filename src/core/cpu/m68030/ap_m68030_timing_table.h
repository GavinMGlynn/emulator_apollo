/* MC68030 published instruction timings: `[030]` §11.6, transcribed.
 *
 * ## What is here and what is deliberately not
 *
 * Only the rows whose instruction-cache case reads `(0/0/0)` — no operand
 * reads, no writes, and no instruction bus cycles because the instruction is in
 * the cache. `docs/references/M68030_TIMING.md` records why those rows and no
 * others: there is nothing in such a number but microcode, which is exactly the
 * quantity this core is missing, and §11.3.1 defines `CC` without any averaging.
 *
 * The `NCC` column is **not** transcribed at all. It is "the average of the
 * odd-word-aligned case and the even-word-aligned case (rounded up)", so no
 * published NCC number is a value any single execution ever takes. A core that
 * added one would reproduce an average the hardware never exhibits.
 *
 * Rows with a non-zero read or write count are also absent. Their `CC` includes
 * operand bus cycles at the table's assumed two clocks each, and this core
 * produces those itself from actual bus state — adding the table's figure whole
 * would count them twice. Subtracting them is arithmetic on published numbers
 * rather than a judgement, but it is a separate step and is not taken here.
 *
 * ## The table's own markers, kept rather than flattened
 *
 * §11.6.8's footnotes: `*` "Add Fetch Effective Address Time", `**` "Add Fetch
 * Immediate Effective Address Time", `+` "Indicates Maximum Time (Actual time
 * is data dependent)".
 *
 * The `+` rows — the divides — are the case `CLAUDE.md` singles out: "a
 * data-dependent instruction published only as a range". They are carried with
 * `data_dependent` set and are **`PROVISIONAL`**: the published figure is a
 * maximum, not the time any particular divide takes, and a core that used it as
 * a point value would be slow by a data-dependent amount rather than wrong by a
 * fixed one. Closing that needs measurement per operand pair.
 *
 * ## Why the manual's own worked example is not the check on this
 *
 * §11.3.4 works an example whose second instruction is labelled `SUBA.L` and
 * carries `SUBA.W`'s numbers — the table gives `SUBA.L Rn,An` as 2/0/2 and
 * `SUBA.W Rn,An` as 4/0/4, and the word-costs-4 pattern repeats in six rows
 * across `ADDA`, `SUBA` and `CMPA`. Using that example to check this
 * transcription would have written `SUBA.L` in wrong. See
 * `docs/references/M68030_TIMING.md`; `overlap_suite` still uses the example
 * for the *arithmetic* of Equation (11-1), which the mislabelling does not
 * affect.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_TIMING_TABLE_H
#define APOLLO_CPU_M68030_AP_M68030_TIMING_TABLE_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_overlap.h"

/* Which effective address table a row's footnote sends the reader to. */
typedef enum {
  AP_M68030_EA_TIME_NONE = 0,  /* the figure is the whole cost */
  AP_M68030_EA_TIME_FETCH,     /* `*`  -- §11.6.1, transcribed */
  AP_M68030_EA_TIME_FETCH_IMMEDIATE, /* `**` -- §11.6.2, not transcribed */
} ap_m68030_ea_time_t;

typedef struct {
  /* The form exactly as §11.6 writes it, so a row can be found in the manual
   * without decoding what someone thought it meant. */
  const char *form;

  ap_m68030_timing_t timing;

  /* The table's `+`: the figure is a maximum and the actual time is data
   * dependent. PROVISIONAL — see the header comment. */
  bool data_dependent;

  /* The table's `*` or `**`, kept apart rather than folded into one flag: they
   * name *different tables*. `*` is "Add Fetch Effective Address Time",
   * §11.6.1, which this project transcribes; `**` is "Add Fetch Immediate
   * Effective Address Time", §11.6.2, which it does not.
   *
   * They were one boolean until the composition needed to act on it, and that
   * was fine while both meant only "this figure is a component". Now that one
   * of them can be priced and the other cannot, collapsing them would price a
   * `**` row off the wrong table -- a plausible number, from the wrong page. */
  ap_m68030_ea_time_t effective_address_time;

  /* How this row's prefetch activity divides between the two alignment cases,
   * which decides whether `NCC - CC` can be turned into a cost. A fact about
   * the instruction -- its length in words, and whether it changes flow -- so
   * it is carried here rather than derived from the figures it is used with. */
  ap_m68030_prefetch_class_t prefetch_class;
} ap_m68030_table_entry_t;

[[nodiscard]] const ap_m68030_table_entry_t *
ap_m68030_timing_table(unsigned *count);

/* The published timing for one single-word register-form instruction, or NULL
 * when the table does not cover it.
 *
 * NULL for most of the opcode map, and that is the honest answer rather than a
 * gap to paper over: what is transcribed is what has been read, and a caller
 * that needs a figure this does not have must go to the manual rather than to a
 * default. */
[[nodiscard]] const ap_m68030_table_entry_t *
ap_m68030_timing_for_word(uint16_t instruction);

/* ---------------------------------------------------------------------------
 * The branches, whose cost is not a function of the instruction word.
 *
 * §11.6.15 gives a taken `Bcc` 6 clocks and an untaken byte `Bcc` 4 -- the
 * difference being the pipe refilling on a change of flow. So no lookup by
 * opcode can answer for a branch: the same word costs differently depending on
 * a condition evaluated at run time, which is exactly why these rows were left
 * out of `ap_m68030_timing_for_word` rather than given whichever case seemed
 * more common.
 *
 * The table is also *not* symmetric in size. A taken branch is one row whatever
 * its displacement, while an untaken one distinguishes byte from word from
 * long -- because an untaken branch still has to have read its displacement,
 * and a taken one has thrown its pipe away regardless.
 * ------------------------------------------------------------------------- */

/* `Bcc` and `BSR`. `taken` is the run-time outcome; `BSR` is unconditional and
 * ignores it. */
[[nodiscard]] const ap_m68030_table_entry_t *
ap_m68030_timing_for_branch(uint16_t instruction, bool taken);

/* `DBcc`, which has three published cases rather than two: the condition true
 * (the loop ends), the condition false with the counter not yet expired (it
 * loops), and the condition false with the counter expired (it falls through).
 * The last is the expensive one at 10 clocks against 6 -- terminating a loop
 * costs more than going round it. */
[[nodiscard]] const ap_m68030_table_entry_t *
ap_m68030_timing_for_dbcc(bool condition_true, bool count_expired);

#endif /* APOLLO_CPU_M68030_AP_M68030_TIMING_TABLE_H */
