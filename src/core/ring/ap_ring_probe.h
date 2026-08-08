/* Cross-node ring probes: measurements over a whole ring rather than over one
 * machine.
 *
 * The CPU probes in `probe/ap_probe.h` execute instructions and report clocks.
 * These execute nothing: they build a ring of stations, run it for a bounded
 * number of bit times, and report what the ring did. Different question, so a
 * different result block -- folding them into the instruction probes would put
 * two unrelated units in one golden.
 *
 * ## Why these numbers are checkable at all with no oracle
 *
 * Each is a *structural* consequence of something `[MAC]` states, not a
 * measurement of behaviour we invented:
 *
 *   - Round-trip is N bit times for N stations, because §3.2's transceive is
 *     "receive data in, and then immediately send it out" and §3.3.2 puts the
 *     nominal delay of that step at one bit.
 *   - Latency per node inserted is therefore exactly one bit time, and the
 *     probe measures it as a *difference* between ring sizes rather than
 *     asserting it -- so a model that got the constant wrong but the slope
 *     right still shows the slope.
 *   - Under contention the upstream station of the two claims the token first,
 *     because the token reaches it first. That follows from the topology and
 *     needs no arbitration rule, which is the point: a token ring's fairness
 *     is positional.
 */

#ifndef APOLLO_RING_AP_RING_PROBE_H
#define APOLLO_RING_AP_RING_PROBE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  const char *name;
  unsigned nodes;
  /* Bit times from the token leaving its originator to its return. */
  uint64_t round_trip_bits;
  /* Bit times the run took in total, so a probe that never completed is
   * distinguishable from one that completed instantly. */
  uint64_t bit_times;
  /* Which slot took the ring, or -1 if none did. */
  int claimed_by;
  unsigned claims;
  /* Set if any station saw a bi-phase error -- which, on a ring with no noise
   * model, means the encoding or the medium is wrong. */
  bool biphase_error;
  bool completed;
} ap_ring_probe_result_t;

/* Every ring probe, in a fixed order. Fixed because the block is a golden and
 * a reordering would read as a change in every line. */
[[nodiscard]] const ap_ring_probe_result_t *ap_ring_probe_all(unsigned *count);

#endif /* APOLLO_RING_AP_RING_PROBE_H */
