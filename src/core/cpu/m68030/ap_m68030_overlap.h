/* MC68030 instruction overlap: the rule, without the numbers.
 *
 * `[030]` §11.2 and §11.3, cited below.
 *
 * ## What this is, and what it deliberately is not
 *
 * This module holds the *rules* by which timing figures compose, and none of
 * the figures. Those live in `ap_m68030_timing_table.h`, which transcribes the
 * rows of §11.6 whose cache case is pure microcode and refuses the rest.
 *
 * The separation is not tidiness. The rules here are arithmetic that any set of
 * figures must obey, and they are tested against the manual's own worked
 * example rather than against numbers this project produced -- which is the only
 * kind of check worth having for a rule with no measurements behind it.
 *
 * `docs/references/M68030_TIMING.md` records which published column may be used
 * and which may not: `NCC` is "the average of the odd-word-aligned case and the
 * even-word-aligned case (rounded up)", so no NCC figure is a value any single
 * execution takes, while `CC` for a `(0/0/0)` row carries no bus time at all and
 * is therefore pure microcode.
 *
 * ## The rule
 *
 * "a portion of time at the beginning of the execution of instruction B can
 * overlap the end of the execution time of instruction A. This time period is
 * called the head of instruction B. The portion of time at the end of
 * instruction A that can overlap the beginning of instruction B is called the
 * tail of instruction A. The total overlap time between instructions A and B
 * consists of the lesser of the tail of instruction A or the head of instruction
 * B."
 *
 * So for a sequence, Equation (11-1):
 *
 *     CC1 + [CC2 - min(H2,T1)] + [CC3 - min(H3,T2)] + ...
 *
 * Note which way round the pair goes: it is the *following* instruction's head
 * against the *preceding* instruction's tail. Taking min(H1,T2) instead reads
 * plausibly and is wrong in a way no single instruction reveals — it only shows
 * up on a sequence whose two instructions have asymmetric heads and tails,
 * which is most of them.
 *
 * ## Zero net execution time is a documented outcome
 *
 * "The nature of the instruction overlap and the fact that the heads of some
 * instructions equal the total instruction-cache-case time for those
 * instructions makes a zero net execution time possible. The execution time of
 * an instruction is completely absorbed by overlap with the previous
 * instruction." A model that clamped every instruction to at least one clock
 * would be wrong, and wrong in the direction that hides a fast mode's error.
 *
 * ## Head and tail compose only with CC
 *
 * §11.3.3: the average no-cache case assumes no overlap either, so the head and
 * tail values do not apply to it. Feeding NCC figures through this rule would
 * subtract an overlap the published number already excludes, twice-counting the
 * saving. The accumulator therefore takes cache-case figures and says so.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_OVERLAP_H
#define APOLLO_CPU_M68030_AP_M68030_OVERLAP_H

#include <stdbool.h>
#include <stdint.h>

/* The eight independently scheduled resources of §11.2, named because §11.2's
 * point is that "very little of the scheduling is directly related to
 * instruction boundaries", and a model that thinks in instructions rather than
 * resources cannot express that. Present as a vocabulary for the timing work
 * that follows; nothing here schedules them yet. */
typedef enum {
  AP_M68030_RESOURCE_MICROSEQUENCER,   /* §11.2.1 */
  AP_M68030_RESOURCE_INSTRUCTION_PIPE, /* §11.2.2 */
  AP_M68030_RESOURCE_INSTRUCTION_CACHE,/* §11.2.3 */
  AP_M68030_RESOURCE_DATA_CACHE,       /* §11.2.4 */
  AP_M68030_RESOURCE_FETCH_PENDING,    /* §11.2.5.1 */
  AP_M68030_RESOURCE_WRITE_PENDING,    /* §11.2.5.2 */
  AP_M68030_RESOURCE_MICRO_BUS,        /* §11.2.5.3 */
  AP_M68030_RESOURCE_MMU,              /* §11.2.6 */
} ap_m68030_resource_t;

#define AP_M68030_RESOURCES 8u

/* One instruction's cache-case figures, as §11.6's tables publish them. */
typedef struct {
  unsigned head;       /* Hn: what can be absorbed by the previous tail */
  unsigned tail;       /* Tn: what the next instruction's head can absorb */
  unsigned cache_case; /* CCn */
  /* NCCn. Carried not to be *used* as a figure -- it is an average of the two
   * alignment cases and no single execution takes it -- but to be *checked
   * against*: a cold-cache run of the core, averaged over both alignments, must
   * come to this. Two published numbers bracketing the same execution is a far
   * stronger check on a transcription than either alone. */
  unsigned no_cache_case;

  /* The `p` of the no-cache case's `(r/p/w)`: "the maximum number of
   * instruction bus cycles performed by the instruction, including all
   * prefetches to keep the instruction pipe filled".
   *
   * Carried so that `NCC − CC` can be divided by it *in code*, over every row,
   * rather than by eye over a chosen few. A claim that the quotient is uniform
   * was made from eleven rows and falsified by three others already in the same
   * table; `ap_m68030_prefetch_cost` and its test are what make that
   * impossible to repeat. */
  unsigned prefetches;
} ap_m68030_timing_t;

/* The marginal cost of one prefetch, from the published pair.
 *
 * `exact` is false when `NCC − CC` is not divisible by `p`. That is not an
 * error in the row: `p` is itself "the average of the odd-word-aligned case and
 * the even-word-aligned case (rounded up)", so a true count of one-and-a-half
 * is published as two and the division inherits the rounding. `BSR` and
 * `LINK.L` are both like this. What matters is that such a row is *visible*
 * here rather than silently rounded by whoever reads it. */
typedef struct {
  unsigned difference;  /* NCC − CC */
  unsigned prefetches;  /* p */
  unsigned clocks;      /* the quotient, meaningful only when `exact` */
  bool exact;
} ap_m68030_prefetch_cost_t;

[[nodiscard]] ap_m68030_prefetch_cost_t
ap_m68030_prefetch_cost(const ap_m68030_timing_t *timing);

/* "The total overlap time between instructions A and B consists of the lesser
 * of the tail of instruction A or the head of instruction B." */
[[nodiscard]] unsigned ap_m68030_overlap(unsigned tail_of_previous,
                                         unsigned head_of_next);

/* Accumulates Equation (11-1) across a sequence. */
typedef struct {
  uint64_t total;         /* clocks so far */
  unsigned previous_tail; /* Tn of the instruction just added */
  bool started;
} ap_m68030_overlap_state_t;

[[nodiscard]] ap_m68030_overlap_state_t ap_m68030_overlap_begin(void);

/* Add one instruction. The first contributes its whole cache-case time -- there
 * is nothing before it to overlap with -- and each later one contributes its
 * cache case less the overlap with its predecessor. */
void ap_m68030_overlap_add(ap_m68030_overlap_state_t *state,
                           const ap_m68030_timing_t *timing);

[[nodiscard]] uint64_t
ap_m68030_overlap_total(const ap_m68030_overlap_state_t *state);

/* ---------------------------------------------------------------------------
 * Scheduling microcode against the bus.
 *
 * `[030]` §11.2: the processor is "eight independently scheduled resources",
 * and "very little of the scheduling is directly related to instruction
 * boundaries". The microsequencer and the bus controller are two of them, and
 * they run *concurrently* -- so an instruction's cost is not its microcode time
 * plus its bus time.
 *
 * The tables say so directly. `ADD Rn,Dn` is `CC 2(0/0/0)` and `NCC 2(0/1/0)`:
 * one more instruction bus cycle, worth two clocks, and the same total. That
 * prefetch cost nothing because it happened while the microcode ran. And
 * `ADD Dn,EA` is `CC 3(0/0/1)` against `NCC 4(0/1/1)`, where the extra prefetch
 * adds *one* clock -- so it is not a maximum of two independent totals either,
 * unless the bus time is counted whole.
 *
 * It is. Reading both rows as `max(microcode, bus)`:
 *
 *     ADD Rn,Dn   CC:  max(2, 0) = 2      NCC: max(2, 2) = 2
 *     ADD Dn,EA   CC:  max(3, 2) = 3      NCC: max(3, 4) = 4
 *
 * where the bus figure is two clocks per cycle, the table's own assumption.
 * Both columns of both rows fall out, and so does every other transcribed row.
 *
 * ## This is a two-resource approximation, and its cost to close is known
 *
 * Eight resources are not two. A full model would let a bus cycle overlap only
 * the part of the microcode not waiting on it, which `max` does not express: it
 * assumes every bus cycle can hide under any microcode. Where an instruction
 * *needs* its operand before it can continue, that is optimistic.
 *
 * It is kept because it reproduces both published columns for every row that
 * has been transcribed, and because the alternative is inventing a structure
 * the manual does not publish. The check is exactly that: cold-cache totals
 * must equal `NCC` and warm-cache totals `CC`. A row where they stop agreeing
 * is where this approximation runs out, and is a measurement worth having.
 * ------------------------------------------------------------------------- */

/* Clocks for an instruction whose microcode takes `microcode_clocks` and whose
 * bus activity takes `bus_clocks`, run concurrently. */
[[nodiscard]] uint32_t ap_m68030_schedule(uint32_t microcode_clocks,
                                          uint32_t bus_clocks);

/* Whether a set of figures is self-consistent: "the heads of some instructions
 * equal the total instruction-cache-case time", so head may equal the cache
 * case but cannot exceed it, and the same for the tail. A table entry failing
 * this was mis-transcribed, and catching it here is cheaper than watching a
 * sequence total come out negative. */
[[nodiscard]] bool ap_m68030_timing_consistent(const ap_m68030_timing_t *timing);

#endif /* APOLLO_CPU_M68030_AP_M68030_OVERLAP_H */
