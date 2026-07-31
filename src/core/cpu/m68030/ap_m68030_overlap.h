/* MC68030 instruction overlap: the rule, without the numbers.
 *
 * `[030]` §11.2 and §11.3, cited below.
 *
 * ## What this is, and what it deliberately is not
 *
 * The per-instruction head, tail and cache-case figures live in §11.6's tables,
 * and this module does **not** transcribe them. `docs/references/M68030_TIMING.md`
 * records why: "No published NCC number is a value any single execution of that
 * instruction ever takes", being a mean of the odd- and even-word-aligned cases
 * rounded up. A core that looked a number up and added it would be reproducing
 * an average the hardware never exhibits, and refining the table could not fix
 * it. The figures this core reports must come from measurement.
 *
 * What *is* transcribable is the composition rule, which is arithmetic rather
 * than measurement, and which any set of figures must obey. Building it now
 * means the numbers have somewhere to arrive, and means the rule is tested
 * against the manual's own worked example rather than against itself.
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
} ap_m68030_timing_t;

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

/* Whether a set of figures is self-consistent: "the heads of some instructions
 * equal the total instruction-cache-case time", so head may equal the cache
 * case but cannot exceed it, and the same for the tail. A table entry failing
 * this was mis-transcribed, and catching it here is cheaper than watching a
 * sequence total come out negative. */
[[nodiscard]] bool ap_m68030_timing_consistent(const ap_m68030_timing_t *timing);

#endif /* APOLLO_CPU_M68030_AP_M68030_OVERLAP_H */
