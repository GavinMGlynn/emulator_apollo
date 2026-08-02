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
 * ## Equation (11-2) is Equation (11-1) over *components*
 *
 * §11.3.4 gives a second, "more specific" formula for the instructions whose
 * effective address time must be added from a separate table:
 *
 *     CCea1 + [CCop1 - min(Hop1,Tea1)] + [CCea2 - min(Hea2,Top1)] +
 *       [CCop2 - min(Hop2,Tea2)] + [CCea3 - min(Hea3,Top2)] + ...
 *
 * It reads as a different rule and it is not one. Every term has the same
 * shape — a component's cache case less the lesser of its own head and the
 * *previous component's* tail — and the only thing (11-2) adds is that an
 * instruction contributes **two** components, its effective address and its
 * operation, in that order. So one accumulator serves both equations, and
 * (11-1) is simply (11-2) for instructions whose effective address costs
 * nothing.
 *
 * That is why `ap_m68030_overlap_add_component` exists beneath
 * `ap_m68030_overlap_add`: the component is the unit the manual actually
 * composes, and an instruction is a pair of them. Writing (11-2) as its own
 * accumulator would duplicate the rule and leave two places for it to drift.
 *
 * A register operand contributes **no component at all**, rather than one
 * costing zero. §11.6.1 writes its head and tail as `-` and not as 0, and the
 * manual's own five-instruction example ends with `NEG D3` reaching back past
 * the previous instruction's operation for its overlap. Injecting a zero-tail
 * component there would consume that overlap and silently over-count.
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

  /* The `r` and `w` of the *cache* case's `(r/p/w)`: the operand read and write
   * cycles the published figure contains. "The read, prefetch, and write cycles
   * are included in the total clock cycle number", and "all timing data assumes
   * two-clock reads and writes" -- both stated at the head of every table in
   * §11.6.
   *
   * These are what make a published figure decomposable. `CC` is not pure
   * microcode for any row that touches memory; it is microcode *plus* its own
   * operand bus cycles at two clocks each, and this core produces those itself
   * from real bus state. Transcribing `r` and `w` is what lets the bus half be
   * subtracted out and the remainder scheduled against what the core actually
   * measured -- which is the difference between a cycle-table model and one
   * whose bus time is emergent.
   *
   * Taken from the cache case rather than the no-cache case because the two
   * differ only in `p`: no row in §11.6 reads or writes a different number of
   * operands depending on whether its instruction was cached. */
  unsigned reads;
  unsigned writes;

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

/* ---------------------------------------------------------------------------
 * Decomposing a published figure into microcode and bus.
 *
 * This is the quantity the execution-time item was missing, and the reason
 * `CC + bus time` over-counted: `CC` already contains the instruction's own
 * operand cycles. §11.6's tables give them beside every figure as `(r/p/w)` and
 * state the price -- "all timing data assumes two-clock reads and writes" -- so
 * the split is arithmetic on published numbers rather than a model.
 *
 *     microcode = CC - 2(r + w)
 *
 * What is left is the microsequencer's own time, which is what this core has no
 * other way to know. The bus half it measures for itself, so a wait-stated
 * cycle or a cache hit still moves the answer.
 * ------------------------------------------------------------------------- */

/* The microcode time inside a published cache-case figure. Zero rather than a
 * negative for a row whose bus cycles exceed its total, which cannot happen in
 * a correct transcription -- `timing_table_suite` asserts it over every row, so
 * a mistyped `r` or `w` fails there rather than silently pricing an instruction
 * at nothing. */
[[nodiscard]] unsigned ap_m68030_microcode_clocks(
    const ap_m68030_timing_t *timing);

/* How a row's prefetch activity divides between the two alignment cases, which
 * is what decides whether `NCC - CC` can be turned into a cost this core can
 * apply. It follows from the instruction's length in words and from whether it
 * changes flow -- both facts about the instruction, not fitted numbers.
 *
 * The cache holding register serves a long word, so a word at an odd offset
 * within one was fetched by the *previous* instruction's cycle and is free to
 * this one. Counting fetches for an n-word instruction at each alignment:
 *
 *   n = 1   even: 1, odd: 0   -- they differ, and the odd case is free
 *   n = 2   even: 1, odd: 1   -- alike
 *   n = 3   even: 2, odd: 1   -- differ by one
 *   n = 4   even: 2, odd: 2   -- alike
 *
 * so an even word count makes the two alignments identical and an odd one makes
 * them differ. */
typedef enum {
  /* One word, no change of flow. The odd alignment runs no fetch at all, so the
   * published average is half the even case. */
  AP_M68030_PREFETCH_SINGLE_WORD,
  /* An even number of words, no change of flow. Both alignments run the same
   * number of fetches, so there is nothing being averaged and the published
   * difference is the exposure itself. */
  AP_M68030_PREFETCH_EVEN_WORDS,
  /* Everything else: three or more words at an odd count, where the two
   * alignments differ by one fetch and recovering a per-fetch cost needs the
   * quantity `docs/references/M68030_TIMING.md` withdrew; and every change of
   * flow, where it is the *target's* alignment that decides the count and the
   * pipe refills either way. */
  AP_M68030_PREFETCH_UNKNOWN,
} ap_m68030_prefetch_class_t;

/* What one instruction's prefetch activity costs when it happens, in clocks.
 *
 * `NCC - CC` is the published difference between the two cache cases, and
 * §11.3.3 says what it is a difference *of*: the no-cache figure is "the
 * average of the odd-word-aligned case and the even-word-aligned case (rounded
 * up)". So the class above is what turns that average back into a value:
 *
 *   SINGLE_WORD   exposure = 2 (NCC - CC)   -- half the average is the odd
 *                                              case, which is zero
 *   EVEN_WORDS    exposure =    NCC - CC    -- no averaging to undo
 *   UNKNOWN       exposure = 0, declined
 *
 * For `SINGLE_WORD` the answer comes to 0 or 2 -- such a prefetch either hides
 * completely under the instruction's microcode or not at all -- which is a
 * falsifiable claim about the published tables. `timing_table_suite` computes
 * it over every row.
 *
 * **`UNKNOWN` declines rather than approximating**, which leaves those rows
 * exact in a warm cache and a lower bound in a cold one. That is the same
 * convention the footnoted rows had before their tables existed: a figure short
 * by a known amount, marked, rather than a plausible one that is wrong. `BSR`
 * at 1.5 clocks per prefetch and `LINK.L` at 0.5 are what these rows look like
 * when the published pair is divided by `p`, and why that division was
 * withdrawn. */
[[nodiscard]] unsigned ap_m68030_prefetch_exposure(
    const ap_m68030_timing_t *timing, ap_m68030_prefetch_class_t klass);

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

/* Add one *component* -- an effective address or an operation. The first
 * contributes its whole cache-case time, there being nothing before it to
 * overlap with, and each later one contributes its cache case less the overlap
 * with its predecessor. This is the whole of both equations; see the header. */
void ap_m68030_overlap_add_component(ap_m68030_overlap_state_t *state,
                                     unsigned head, unsigned tail,
                                     unsigned cache_case);

/* Add one instruction whose effective address costs nothing, which is
 * Equation (11-1)'s case. An instruction needing an effective address time
 * composes through `ap_m68030_ea_timing_compose` instead -- it lives with the
 * effective address tables because resolving the "2+op head" notation needs
 * them, and this module deliberately holds no figures. */
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

/* ---------------------------------------------------------------------------
 * The no-cache case, which composes by plain addition.
 *
 * §11.3.3 states it and works it: for `MOVE.L (d16,An,Dn),Dn` the average
 * no-cache-case time is "2 + 7 = 9 clocks", the operation's own figure plus the
 * effective address's, with no overlap term at all -- "it should be noted again
 * that the no-cache-case time assumes no overlap". The same section adds the
 * two instructions of its example the same way: "9 + 7 = 16 clocks".
 *
 * So the two published columns compose by two different rules, and this is the
 * second one. It is deliberately not the same function as the accumulator
 * above: feeding NCC figures through head and tail would subtract an overlap
 * the published number already excludes.
 *
 * **What such a total is, and is not.** Each NCC is "the average of the
 * odd-word-aligned case and the even-word-aligned case (rounded up)", so a sum
 * of them is a sum of averages -- an estimate of a stream, not a figure any
 * single execution takes. §11.3.3's own example is the demonstration: the MOVE
 * costs 8 clocks even-aligned and 10 odd, and the published 9 is neither. A
 * core reporting 9 for it would be reporting a number the hardware never
 * exhibits, which is why this exists to be *checked against* rather than used.
 * ------------------------------------------------------------------------- */

/* Sum the no-cache-case times of `count` components, in order. Components, not
 * instructions: an instruction needing an effective address time contributes
 * two, exactly as in Equation (11-2). */
[[nodiscard]] uint64_t ap_m68030_no_cache_total(
    const ap_m68030_timing_t *components, unsigned count);

/* Whether a set of figures is self-consistent: "the heads of some instructions
 * equal the total instruction-cache-case time", so head may equal the cache
 * case but cannot exceed it, and the same for the tail. A table entry failing
 * this was mis-transcribed, and catching it here is cheaper than watching a
 * sequence total come out negative. */
[[nodiscard]] bool ap_m68030_timing_consistent(const ap_m68030_timing_t *timing);

#endif /* APOLLO_CPU_M68030_AP_M68030_OVERLAP_H */
