/* The multi-node scheduler: N nodes on one cycle-locked ring.
 *
 * ## The rule this exists to enforce
 *
 * Every node advances **only on its own cycle boundaries**, and the ring
 * advances on its own bit clock, all against the single time base. A DN3000 at
 * 12 MHz and a DN5500 at 25 MHz on the same ring do not share a cycle, and
 * `CLAUDE.md` states the consequence: "no CPU's cycle is a legal unit of
 * account". So this schedules in `AP_TIME_BASE_HZ` units and each participant
 * carries its own period.
 *
 * ## Determinism is the whole verification
 *
 * The plan's check is a whole-ring state hash reproducible across runs *and
 * across build types*. Two things would break that and both are designed out:
 *
 *   - **Ties.** When two nodes fall due at the same instant, the order they
 *     step in must not depend on anything but the ring. Lower slot first,
 *     always. Real hardware has no such rule -- two nodes genuinely are
 *     simultaneous -- but the ring's own timing means no node can observe the
 *     order, since nothing a node does reaches another within a bit time. The
 *     arbitrary choice is therefore unobservable, which is what makes it safe
 *     to make.
 *   - **Floating point.** Periods are integers in base units. A period derived
 *     by dividing frequencies as doubles would round differently under `-O0`
 *     and `-O2` and put the compiler into the state hash.
 *
 * ## What a node is here
 *
 * Deliberately not an `ap_machine_t`. A node is a period and a callback, so
 * this module can be tested with synthetic nodes and so a ring can mix real
 * machines with instruments -- a recorder, a fault injector -- without the
 * scheduler knowing which is which. `src/core/ring` knows nothing about
 * `src/core/machine`, in the same way the core knows nothing about a frontend.
 */

#ifndef APOLLO_RING_AP_RING_SCHED_H
#define APOLLO_RING_AP_RING_SCHED_H

#include <stdbool.h>
#include <stdint.h>

#include "ring/ap_ring_medium.h"
#include "time/ap_time.h"

/* Called when this node's cycle boundary falls due. `now` is absolute time in
 * base units, so a node needs no clock of its own. */
typedef void (*ap_ring_node_step_fn)(void *context, ap_time_t now);

typedef struct {
  bool present;
  ap_time_t period; /* base units per cycle */
  ap_time_t next;   /* when this node is next due */
  ap_ring_node_step_fn step;
  void *context;
} ap_ring_participant_t;

typedef struct {
  ap_ring_medium_t medium;
  ap_ring_participant_t participant[AP_RING_MAX_NODES];
  /* The ring's own clock, which advances the medium one bit at a time. */
  ap_time_t bit_period;
  ap_time_t next_bit;
  ap_time_t now;
} ap_ring_sched_t;

/* Initialise with the ring running at the bit rate `[MAC]` §3.2 gives. */
void ap_ring_sched_init(ap_ring_sched_t *s);

/* Add a node clocked at `hz`, returning its ring slot or -1.
 *
 * Refuses a frequency the time base cannot represent exactly, rather than
 * rounding it. `CLAUDE.md`: "`ap_clock_init()` rejects an unrepresentable
 * frequency rather than rounding it" -- the same rule, for the same reason. A
 * node whose period were rounded would drift against the ring by an amount no
 * probe could attribute to anything. */
[[nodiscard]] int ap_ring_sched_add(ap_ring_sched_t *s, uint32_t hz,
                                    ap_ring_node_step_fn step, void *context);

/* Run until `until`, stepping every participant on each of its own boundaries
 * and the ring on each of its bit times, in time order. */
void ap_ring_sched_run_until(ap_ring_sched_t *s, ap_time_t until);

/* A hash over the whole ring's scheduling state: every participant's phase and
 * the medium's bit time. This is what the plan's determinism check compares,
 * and it deliberately covers *phase* rather than only elapsed time -- two runs
 * that reached the same instant by different interleavings would agree on the
 * clock and disagree here. */
[[nodiscard]] uint64_t ap_ring_sched_hash(const ap_ring_sched_t *s);

#endif /* APOLLO_RING_AP_RING_SCHED_H */
