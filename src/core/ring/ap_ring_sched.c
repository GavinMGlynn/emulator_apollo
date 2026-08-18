/* The multi-node ring scheduler. See `ap_ring_sched.h` for why ties break by
 * slot and why nothing here is a float. */

#include "ring/ap_ring_sched.h"

#include "state/ap_hash.h"

void ap_ring_sched_init(ap_ring_sched_t *s) {
  ap_ring_medium_init(&s->medium);
  for (unsigned i = 0; i < AP_RING_MAX_NODES; i++) {
    s->participant[i] = (ap_ring_participant_t){0};
  }
  /* The ring's bit clock. Exact by construction -- `AP_RING_BIT_CELL_TICKS` is
   * the base divided by the data rate, and the base is chosen so that division
   * is exact. */
  s->bit_period = AP_RING_BIT_CELL_TICKS;
  s->next_bit = s->bit_period;
  s->now = 0u;
  s->used = 0u;
}

int ap_ring_sched_add(ap_ring_sched_t *s, uint32_t hz,
                      ap_ring_node_step_fn step, void *context) {
  ap_clock_t clock;
  if (!ap_clock_init(&clock, hz)) {
    /* Refused rather than rounded: a node whose period were rounded would
     * drift against the ring by an amount no probe could attribute. */
    return -1;
  }
  const int slot = ap_ring_medium_attach(&s->medium);
  if (slot < 0) {
    return -1;
  }
  s->participant[slot] = (ap_ring_participant_t){
      .present = true,
      .period = clock.period,
      /* First boundary is one period from now, not at `now`: a node that
       * stepped at time zero would run a cycle before any time had passed. */
      .next = s->now + clock.period,
      .step = step,
      .context = context,
  };
  if ((unsigned)slot >= s->used) {
    s->used = (unsigned)slot + 1u;
  }
  return slot;
}

void ap_ring_sched_run_until(ap_ring_sched_t *s, ap_time_t until) {
  for (;;) {
    /* The earliest pending event. The ring's bit clock competes on equal
     * terms with the nodes -- it is a clock domain like any other, and giving
     * it priority would make a bit arrive before a node that was due at the
     * same instant could drive it. */
    ap_time_t when = s->next_bit;
    int who = -1; /* -1 means the ring's own bit clock */

    for (unsigned i = 0; i < s->used; i++) {
      if (!s->participant[i].present) {
        continue;
      }
      /* Strictly earlier, so a node tying with the bit clock loses to it, and
       * a node tying with a lower-numbered node loses to that. Deterministic
       * and total; see the header for why the choice is unobservable. */
      if (s->participant[i].next < when) {
        when = s->participant[i].next;
        who = (int)i;
      }
    }

    if (when > until) {
      break;
    }

    s->now = when;
    if (who < 0) {
      ap_ring_medium_advance(&s->medium);
      s->next_bit += s->bit_period;
      continue;
    }

    ap_ring_participant_t *p = &s->participant[who];
    /* Advanced *before* the callback runs, so a node that inspects the
     * schedule -- or adds another node -- sees a consistent state rather than
     * its own stale deadline. */
    p->next += p->period;
    if (p->step != NULL) {
      p->step(p->context, when);
    }
  }
  s->now = until;
}

uint64_t ap_ring_sched_hash(const ap_ring_sched_t *s) {
  ap_hash_t h = ap_hash_begin();
  ap_hash_u64(&h, s->now);
  ap_hash_u64(&h, s->medium.bit_time);
  ap_hash_u64(&h, s->next_bit);
  for (unsigned i = 0; i < AP_RING_MAX_NODES; i++) {
    const ap_ring_participant_t *p = &s->participant[i];
    ap_hash_u8(&h, p->present ? 1u : 0u);
    if (!p->present) {
      continue;
    }
    /* Phase, not merely elapsed time: two runs reaching the same instant by
     * different interleavings agree on the clock and disagree here, which is
     * the whole point of hashing the schedule rather than the wall time. */
    ap_hash_u64(&h, p->period);
    ap_hash_u64(&h, p->next);
    /* And where each node sits in the cable order, since the ring's topology
     * is part of its state. */
    ap_hash_u8(&h, s->medium.node[i].attached ? 1u : 0u);
    ap_hash_u8(&h, s->medium.node[i].bypass.bypassed ? 1u : 0u);
  }
  return ap_hash_end(&h);
}
