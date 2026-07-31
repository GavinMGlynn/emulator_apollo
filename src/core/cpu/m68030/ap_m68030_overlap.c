/* MC68030 instruction overlap. See ap_m68030_overlap.h for why the rule is here
 * and the per-instruction figures are not. */

#include "cpu/m68030/ap_m68030_overlap.h"

unsigned ap_m68030_overlap(unsigned tail_of_previous, unsigned head_of_next) {
  return tail_of_previous < head_of_next ? tail_of_previous : head_of_next;
}

ap_m68030_overlap_state_t ap_m68030_overlap_begin(void) {
  return (ap_m68030_overlap_state_t){0};
}

void ap_m68030_overlap_add(ap_m68030_overlap_state_t *state,
                           const ap_m68030_timing_t *timing) {
  if (!state->started) {
    /* "CC1 + ..." -- the first instruction has nothing before it to overlap
     * with, so it contributes its whole cache-case time. */
    state->total = timing->cache_case;
    state->previous_tail = timing->tail;
    state->started = true;
    return;
  }

  /* "[CCn - min(Hn,Tn-1)]": the *following* instruction's head against the
   * *preceding* instruction's tail. */
  const unsigned saved = ap_m68030_overlap(state->previous_tail, timing->head);

  /* The subtraction cannot go below zero, because a consistent entry has
   * head <= cache_case and the overlap is at most the head. Clamping anyway
   * keeps an inconsistent table from producing a total that wraps -- and
   * ap_m68030_timing_consistent is how such an entry is meant to be caught. */
  state->total += (timing->cache_case > saved) ? (timing->cache_case - saved) : 0u;
  state->previous_tail = timing->tail;
}

uint64_t ap_m68030_overlap_total(const ap_m68030_overlap_state_t *state) {
  return state->total;
}

uint32_t ap_m68030_schedule(uint32_t microcode_clocks, uint32_t bus_clocks) {
  return microcode_clocks > bus_clocks ? microcode_clocks : bus_clocks;
}

bool ap_m68030_timing_consistent(const ap_m68030_timing_t *timing) {
  return timing->head <= timing->cache_case && timing->tail <= timing->cache_case;
}
