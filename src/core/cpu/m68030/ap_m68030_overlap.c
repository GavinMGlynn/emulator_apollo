/* MC68030 instruction overlap. See ap_m68030_overlap.h for why the rule is here
 * and the per-instruction figures are not. */

#include "cpu/m68030/ap_m68030_overlap.h"

unsigned ap_m68030_overlap(unsigned tail_of_previous, unsigned head_of_next) {
  return tail_of_previous < head_of_next ? tail_of_previous : head_of_next;
}

ap_m68030_overlap_state_t ap_m68030_overlap_begin(void) {
  return (ap_m68030_overlap_state_t){0};
}

void ap_m68030_overlap_add_component(ap_m68030_overlap_state_t *state,
                                     unsigned head, unsigned tail,
                                     unsigned cache_case) {
  if (!state->started) {
    /* "CC1 + ..." -- the first component has nothing before it to overlap
     * with, so it contributes its whole cache-case time. */
    state->total = cache_case;
    state->previous_tail = tail;
    state->started = true;
    return;
  }

  /* "[CCn - min(Hn,Tn-1)]": the *following* component's head against the
   * *preceding* component's tail. */
  const unsigned saved = ap_m68030_overlap(state->previous_tail, head);

  /* The subtraction cannot go below zero, because a consistent entry has
   * head <= cache_case and the overlap is at most the head. Clamping anyway
   * keeps an inconsistent table from producing a total that wraps -- and
   * ap_m68030_timing_consistent is how such an entry is meant to be caught. */
  state->total += (cache_case > saved) ? (cache_case - saved) : 0u;
  state->previous_tail = tail;
}

void ap_m68030_overlap_add(ap_m68030_overlap_state_t *state,
                           const ap_m68030_timing_t *timing) {
  ap_m68030_overlap_add_component(state, timing->head, timing->tail,
                                  timing->cache_case);
}

uint64_t ap_m68030_overlap_total(const ap_m68030_overlap_state_t *state) {
  return state->total;
}

uint32_t ap_m68030_schedule(uint32_t microcode_clocks, uint32_t bus_clocks) {
  return microcode_clocks > bus_clocks ? microcode_clocks : bus_clocks;
}

ap_m68030_prefetch_cost_t
ap_m68030_prefetch_cost(const ap_m68030_timing_t *timing) {
  ap_m68030_prefetch_cost_t out = {0};
  out.prefetches = timing->prefetches;
  out.difference = timing->no_cache_case > timing->cache_case
                       ? timing->no_cache_case - timing->cache_case
                       : 0u;

  if (timing->prefetches == 0u) {
    /* No prefetch to attribute the difference to. An effective address row can
     * be like this: its no-cache case is the same as its cache case because it
     * runs no instruction bus cycles of its own. */
    out.exact = out.difference == 0u;
    return out;
  }

  out.clocks = out.difference / timing->prefetches;
  out.exact = (out.difference % timing->prefetches) == 0u;
  return out;
}

/* "All timing data assumes two-clock reads and writes", at the head of every
 * table in §11.6. */
#define BUS_CYCLE_CLOCKS 2u

unsigned ap_m68030_microcode_clocks(const ap_m68030_timing_t *timing) {
  const unsigned bus = (timing->reads + timing->writes) * BUS_CYCLE_CLOCKS;
  return timing->cache_case > bus ? timing->cache_case - bus : 0u;
}

unsigned ap_m68030_prefetch_exposure(const ap_m68030_timing_t *timing,
                                     ap_m68030_prefetch_class_t klass) {
  const unsigned difference = timing->no_cache_case > timing->cache_case
                                  ? timing->no_cache_case - timing->cache_case
                                  : 0u;
  switch (klass) {
  case AP_M68030_PREFETCH_SINGLE_WORD:
    /* Half the published difference is the odd-aligned case, which runs no
     * fetch at all, so the even-aligned case is twice the average. */
    return difference * 2u;
  case AP_M68030_PREFETCH_ALIGNMENT_INVARIANT:
    /* Both alignments run the same fetches, so nothing was averaged. */
    return difference;
  case AP_M68030_PREFETCH_UNKNOWN:
    break;
  }
  return 0u;
}

uint64_t ap_m68030_no_cache_total(const ap_m68030_timing_t *components,
                                  unsigned count) {
  uint64_t total = 0;
  for (unsigned i = 0; i < count; i++) {
    /* No head, no tail, no minimum: "the no-cache-case time assumes no
     * overlap". */
    total += components[i].no_cache_case;
  }
  return total;
}

bool ap_m68030_timing_consistent(const ap_m68030_timing_t *timing) {
  return timing->head <= timing->cache_case && timing->tail <= timing->cache_case;
}
