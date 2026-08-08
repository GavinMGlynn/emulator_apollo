/* A ring station's MAC behaviour: transceiving, token recognition, and taking
 * the ring. The cross-node probes measure a whole ring; this checks the one
 * station the probes are built out of, so a probe's number can be trusted to
 * mean what it says. */

#include "ring/ap_ring_station.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

typedef struct {
  ap_ring_medium_t medium;
  ap_ring_station_t station[16];
  unsigned nodes;
} ring_t;

static void build(ring_t *r, unsigned nodes) {
  ap_ring_medium_init(&r->medium);
  r->nodes = nodes;
  for (unsigned i = 0; i < nodes; i++) {
    ap_ring_station_init(&r->station[i], ap_ring_medium_attach(&r->medium));
  }
}

/* One bit time: all drive, one hop, all receive. Never interleaved per
 * station -- that would let a station see a cell its neighbour drove in the
 * same bit time, and the ring's timing rests on one hop per clock. */
static void step(ring_t *r) {
  for (unsigned i = 0; i < r->nodes; i++) {
    ap_ring_station_drive(&r->station[i], &r->medium);
  }
  ap_ring_medium_advance(&r->medium);
  for (unsigned i = 0; i < r->nodes; i++) {
    ap_ring_station_receive(&r->station[i], &r->medium);
  }
}

/* `[MAC]` §3.2: stations "receive data in, and then immediately send it out".
 * Immediately is one bit time -- the elastic store's nominal delay (§3.3.2) --
 * so a token laps a ring of N stations in N bit times plus the token's own
 * width, the lap being measured to its last bit. */
static void test_a_token_laps_the_ring_in_one_bit_time_per_station(void) {
  for (unsigned nodes = 2u; nodes <= 6u; nodes++) {
    ring_t r;
    build(&r, nodes);
    ap_ring_station_originate_token(&r.station[0], AP_RING_OOB_FREE_TOKEN);

    uint64_t when = 0u;
    for (unsigned t = 0; t < 512u; t++) {
      step(&r);
      if (r.station[0].tokens_seen > 0u) {
        when = t + 1u;
        break;
      }
    }
    /* N for the lap, plus eight: the token is nine bits and the first is
     * driven on bit time one. */
    TEST_ASSERT_EQUAL_UINT64((uint64_t)nodes + 8u, when);
  }
}

/* And nothing on a quiet ring produces a bi-phase error. With no noise model,
 * an error here means the encoding or the medium is wrong -- which is exactly
 * what this assertion is for. */
static void test_a_quiet_ring_produces_no_biphase_errors(void) {
  ring_t r;
  build(&r, 4u);
  ap_ring_station_originate_token(&r.station[0], AP_RING_OOB_FREE_TOKEN);
  for (unsigned t = 0; t < 200u; t++) {
    step(&r);
  }
  for (unsigned i = 0; i < r.nodes; i++) {
    TEST_ASSERT_FALSE(r.station[i].saw_biphase_error);
  }
}

/* §2.2.1.1: a station takes the ring "by changing the state of the
 * character's last bit". One station wanting the ring takes the first free
 * token that reaches it. */
static void test_a_station_claims_the_first_free_token_that_reaches_it(void) {
  ring_t r;
  build(&r, 4u);
  r.station[2].wants_ring = true;
  ap_ring_station_originate_token(&r.station[0], AP_RING_OOB_FREE_TOKEN);

  for (unsigned t = 0; t < 200u; t++) {
    step(&r);
  }
  TEST_ASSERT_TRUE(r.station[2].holds_ring);
  TEST_ASSERT_EQUAL_UINT64(1u, r.station[2].claims_made);
  /* And nobody else took it. */
  TEST_ASSERT_FALSE(r.station[0].holds_ring);
  TEST_ASSERT_FALSE(r.station[1].holds_ring);
  TEST_ASSERT_FALSE(r.station[3].holds_ring);
}

/* The bug the contention probe found, pinned as a test.
 *
 * A free token and a claimed one differ in exactly one bit -- their last -- so
 * any test on a prefix accepts both, and a station downstream of one that had
 * already claimed would claim again. Two stations would then hold one ring,
 * which is the failure a token ring exists to make impossible. The station
 * upstream of the other must win, and it must win *because* the token reaches
 * it first: positional fairness, with no arbitration rule anywhere. */
static void test_only_the_upstream_of_two_contenders_takes_the_ring(void) {
  ring_t r;
  build(&r, 4u);
  r.station[1].wants_ring = true;
  r.station[2].wants_ring = true;
  ap_ring_station_originate_token(&r.station[0], AP_RING_OOB_FREE_TOKEN);

  for (unsigned t = 0; t < 400u; t++) {
    step(&r);
  }

  unsigned holders = 0u;
  for (unsigned i = 0; i < r.nodes; i++) {
    holders += r.station[i].holds_ring ? 1u : 0u;
  }
  TEST_ASSERT_EQUAL_UINT(1u, holders);
  TEST_ASSERT_TRUE(r.station[1].holds_ring);
  TEST_ASSERT_FALSE(r.station[2].holds_ring);
  /* The loser still wants the ring: it was not served, and it did not give up
   * either. A model that cleared its request on a passing claimed token would
   * lose the transmission silently. */
  TEST_ASSERT_TRUE(r.station[2].wants_ring);
}

/* A station that wants nothing forwards the token untouched, so the ring keeps
 * circulating it -- **on a ring long enough to hold one**.
 *
 * That qualification is a real constraint and not a test convenience. A token
 * is nine bits and each station contributes one bit of delay, so a ring of
 * fewer than nine stations is *shorter than its own token*: the head returns
 * to the originator before the tail has left, and the symbol overwrites
 * itself. `[MAC]` §3.3 says the ring's delay comes from "static elements (for
 * example, cable plant and connected nodes)", and on real hardware the cable
 * dominates -- 1 km between nodes is about 60 bit times at 12 Mbit/s, so no
 * physical ring is anywhere near this bound. This core models the station's
 * delay and not yet the cable's, so the bound is reachable here and a test
 * that ignored it would be asserting something the model cannot do. */
static void test_a_station_wanting_nothing_forwards_the_token_intact(void) {
  ring_t r;
  build(&r, 12u); /* > 9, so the ring can hold a whole token */
  ap_ring_station_originate_token(&r.station[0], AP_RING_OOB_FREE_TOKEN);

  for (unsigned t = 0; t < 300u; t++) {
    step(&r);
  }
  /* Every station has seen the free token go by, more than once. */
  for (unsigned i = 0; i < r.nodes; i++) {
    TEST_ASSERT_TRUE(r.station[i].tokens_seen > 1u);
  }
}

/* And the gap the previous test's comment names, closed: with cable modelled,
 * a *small* ring carries a token perfectly well. Three stations and four bits
 * of cable each is fifteen bit times of circumference against a nine-bit
 * token, which is the ordinary case on real hardware -- `[MAC]` §3.3 counts
 * cable plant among the delay elements and it dominates in any real plant.
 *
 * This is the test that shows the earlier failure was a modelling gap and not
 * a property of token rings. */
static void test_cable_lets_a_three_station_ring_circulate_a_token(void) {
  ring_t r;
  ap_ring_medium_init(&r.medium);
  r.nodes = 3u;
  for (unsigned i = 0; i < r.nodes; i++) {
    const int slot = ap_ring_medium_attach(&r.medium);
    ap_ring_medium_set_cable_bits(&r.medium, slot, 4u);
    ap_ring_station_init(&r.station[i], slot);
  }
  TEST_ASSERT_TRUE(ap_ring_medium_circumference_bits(&r.medium) >
                   AP_RING_OOB_BITS);

  ap_ring_station_originate_token(&r.station[0], AP_RING_OOB_FREE_TOKEN);
  for (unsigned t = 0; t < 300u; t++) {
    step(&r);
  }
  for (unsigned i = 0; i < r.nodes; i++) {
    TEST_ASSERT_TRUE(r.station[i].tokens_seen > 1u);
    TEST_ASSERT_FALSE(r.station[i].saw_biphase_error);
  }
}

/* Recognition is of the *symbol*, not of a byte: the window is nine bits wide
 * and a well-formed character is what it reports. */
static void test_the_station_recognises_a_nine_bit_symbol(void) {
  ring_t r;
  build(&r, 2u);
  ap_ring_station_originate_token(&r.station[0], AP_RING_OOB_FRAME_START);

  uint16_t symbol = 0u;
  bool found = false;
  for (unsigned t = 0; t < 64u; t++) {
    step(&r);
    if (ap_ring_station_at_symbol(&r.station[1], &symbol) &&
        symbol == AP_RING_OOB_FRAME_START) {
      found = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(found);
  /* A frame start is not a token, so nothing counted it as one. */
  TEST_ASSERT_EQUAL_UINT64(0u, r.station[1].tokens_seen);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_token_laps_the_ring_in_one_bit_time_per_station);
  RUN_TEST(test_a_quiet_ring_produces_no_biphase_errors);
  RUN_TEST(test_a_station_claims_the_first_free_token_that_reaches_it);
  RUN_TEST(test_only_the_upstream_of_two_contenders_takes_the_ring);
  RUN_TEST(test_a_station_wanting_nothing_forwards_the_token_intact);
  RUN_TEST(test_cable_lets_a_three_station_ring_circulate_a_token);
  RUN_TEST(test_the_station_recognises_a_nine_bit_symbol);
  return UNITY_END();
}
