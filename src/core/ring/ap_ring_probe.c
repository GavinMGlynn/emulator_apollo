/* Cross-node ring probes. See `ap_ring_probe.h` for why each number is a
 * structural consequence of `[MAC]` rather than a measurement of an invention.
 */

#include "ring/ap_ring_probe.h"

#include "ring/ap_ring_mac.h"
#include "ring/ap_ring_medium.h"
#include "ring/ap_ring_station.h"

/* Bounded so a probe that never completes ends anyway and says so. A ring of
 * eight stations circulates a token in eight bit times, so this is orders of
 * magnitude beyond any correct run. */
#define PROBE_BIT_LIMIT 4096u

typedef struct {
  ap_ring_medium_t medium;
  ap_ring_station_t station[AP_RING_MAX_NODES];
  unsigned nodes;
} ring_t;

static void ring_build(ring_t *r, unsigned nodes) {
  ap_ring_medium_init(&r->medium);
  r->nodes = nodes;
  for (unsigned i = 0; i < nodes; i++) {
    const int slot = ap_ring_medium_attach(&r->medium);
    ap_ring_station_init(&r->station[i], slot);
  }
}

/* One bit time for the whole ring: every station drives, the medium moves the
 * cells one hop, every station receives.
 *
 * Drive-then-advance-then-receive, in that order and with all stations at each
 * step. Interleaving per station -- drive one, advance, receive one -- would
 * let a station see a cell its neighbour drove in the same bit time, which is
 * exactly the one-hop-per-clock property the ring's timing rests on. */
static void ring_step(ring_t *r) {
  for (unsigned i = 0; i < r->nodes; i++) {
    ap_ring_station_drive(&r->station[i], &r->medium);
  }
  ap_ring_medium_advance(&r->medium);
  for (unsigned i = 0; i < r->nodes; i++) {
    ap_ring_station_receive(&r->station[i], &r->medium);
  }
}

static bool any_biphase_error(const ring_t *r) {
  for (unsigned i = 0; i < r->nodes; i++) {
    if (r->station[i].saw_biphase_error) {
      return true;
    }
  }
  return false;
}

/* Round-trip: station 0 originates a free token; the run ends when station 0
 * sees a free token come back. */
static ap_ring_probe_result_t probe_round_trip(const char *name,
                                               unsigned nodes) {
  ap_ring_probe_result_t out = {
      .name = name, .nodes = nodes, .claimed_by = -1};
  ring_t r;
  ring_build(&r, nodes);
  ap_ring_station_originate_token(&r.station[0], AP_RING_OOB_FREE_TOKEN);

  for (unsigned t = 0; t < PROBE_BIT_LIMIT; t++) {
    ring_step(&r);
    out.bit_times++;
    if (r.station[0].tokens_seen > 0u) {
      /* The token's last bit has just come back round. Counting from the
       * first driven bit, that is the whole lap. */
      out.round_trip_bits = out.bit_times;
      out.completed = true;
      break;
    }
  }
  out.biphase_error = any_biphase_error(&r);
  return out;
}

/* Contention: two stations both want the ring when a token appears. The one
 * the token reaches first takes it, and the other must not. */
static ap_ring_probe_result_t probe_contention(const char *name,
                                               unsigned nodes) {
  ap_ring_probe_result_t out = {
      .name = name, .nodes = nodes, .claimed_by = -1};
  ring_t r;
  ring_build(&r, nodes);

  /* Stations 1 and 2 both want to transmit; the token starts at 0 and reaches
   * 1 first. No arbitration rule decides this -- the topology does. */
  r.station[1].wants_ring = true;
  r.station[2].wants_ring = true;
  ap_ring_station_originate_token(&r.station[0], AP_RING_OOB_FREE_TOKEN);

  for (unsigned t = 0; t < PROBE_BIT_LIMIT; t++) {
    ring_step(&r);
    out.bit_times++;
    unsigned claims = 0u;
    int who = -1;
    for (unsigned i = 0; i < nodes; i++) {
      if (r.station[i].holds_ring) {
        claims++;
        if (who < 0) {
          who = r.station[i].slot;
        }
      }
    }
    if (claims > 0u) {
      out.claims = claims;
      out.claimed_by = who;
      out.completed = true;
      /* Run a further full lap so a second station claiming late would still
       * be counted -- stopping at the first claim would make "only one
       * station claimed" true by construction rather than by measurement. */
      for (unsigned extra = 0; extra < nodes + AP_RING_OOB_BITS; extra++) {
        ring_step(&r);
        out.bit_times++;
      }
      out.claims = 0u;
      for (unsigned i = 0; i < nodes; i++) {
        if (r.station[i].holds_ring) {
          out.claims++;
        }
      }
      break;
    }
  }
  out.biphase_error = any_biphase_error(&r);
  return out;
}

static ap_ring_probe_result_t results[6];

const ap_ring_probe_result_t *ap_ring_probe_all(unsigned *count) {
  /* Recomputed on every call rather than cached: a probe suite that returned
   * stale results after a change to the ring would be worse than none, and
   * these are microseconds. */
  results[0] = probe_round_trip("round_trip_2", 2u);
  results[1] = probe_round_trip("round_trip_3", 3u);
  results[2] = probe_round_trip("round_trip_4", 4u);
  results[3] = probe_round_trip("round_trip_8", 8u);
  results[4] = probe_contention("contention_4", 4u);
  results[5] = probe_contention("contention_8", 8u);
  *count = 6u;
  return results;
}
