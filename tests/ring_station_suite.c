/* A ring station's MAC behaviour: transceiving, token recognition, and taking
 * the ring. The cross-node probes measure a whole ring; this checks the one
 * station the probes are built out of, so a probe's number can be trusted to
 * mean what it says. */

#include "ring/ap_ring_phy.h"
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

/* `[MAC]` §2.1 step 7 is the one recovery figure the manual puts a number on:
 * a node strips "until it finishes receiving its own frame, or until a 10.9
 * msec (2^14 byte) timeout occurs". The two forms must agree, and they do --
 * 2^14 bytes is 131,072 bits and at 12 Mbit/s that is 10.923 ms. Asserted
 * because `pdftotext` flattens the exponent to "214 byte", which reads as a
 * plausible and entirely wrong number; this was taken from the page image. */
static void test_the_stripping_timeout_matches_both_forms_the_manual_gives(
    void) {
  TEST_ASSERT_EQUAL_UINT(16384u, AP_RING_STRIP_TIMEOUT_BYTES);
  TEST_ASSERT_EQUAL_UINT(131072u, AP_RING_STRIP_TIMEOUT_BITS);
  /* The manual quotes 10.9 ms; the exact figure is 10.9227, so the comparison
   * is to a tenth of a millisecond -- the precision the manual states. An
   * equality against a rounded microsecond count would be asserting our own
   * rounding, not the manual's number. */
  const uint64_t micros =
      (uint64_t)AP_RING_STRIP_TIMEOUT_BITS * 1000000u / AP_RING_DATA_HZ;
  TEST_ASSERT_EQUAL_UINT64(109u, micros / 100u);
}

/* §2.1 step 3: taking the ring "breaks ring recirculation" and stripping
 * begins with it -- one event, not two. And step 4: the stripped stream is
 * padded with Zeros. */
static void test_claiming_the_ring_begins_stripping(void) {
  ring_t r;
  build(&r, 4u);
  r.station[1].wants_ring = true;
  ap_ring_station_originate_token(&r.station[0], AP_RING_OOB_FREE_TOKEN);

  for (unsigned t = 0; t < 100u; t++) {
    step(&r);
  }
  TEST_ASSERT_TRUE(r.station[1].holds_ring);
  TEST_ASSERT_TRUE(r.station[1].stripping);
  /* Downstream sees the padding, so no token is circulating any more: the
   * holder has broken recirculation, which is what step 3 says it does. */
  TEST_ASSERT_TRUE(r.station[1].bits_stripping > 0u);
}

/* Node removal mid-run. `[MAC]` §3.5's relays take a node out without breaking
 * the ring, so the survivors keep circulating their token -- which is the
 * whole point of a passive bypass. */
static void test_a_node_leaving_mid_run_does_not_break_the_ring(void) {
  ring_t r;
  ap_ring_medium_init(&r.medium);
  r.nodes = 4u;
  for (unsigned i = 0; i < r.nodes; i++) {
    const int slot = ap_ring_medium_attach(&r.medium);
    ap_ring_medium_set_cable_bits(&r.medium, slot, 4u);
    ap_ring_station_init(&r.station[i], slot);
  }
  ap_ring_station_originate_token(&r.station[0], AP_RING_OOB_FREE_TOKEN);
  for (unsigned t = 0; t < 200u; t++) {
    step(&r);
  }
  TEST_ASSERT_TRUE(r.station[3].tokens_seen > 0u);

  /* Node 2 goes out mid-run, and the *signal path* must survive it -- that is
   * what §3.5's relays promise and all this test claims.
   *
   * It deliberately does **not** claim the circulating token survives. It does
   * not: a token that happens to be inside the departing node, or in the cable
   * it drives, is lost when the node leaves, and the measured behaviour here is
   * that the far side stops seeing tokens entirely. That is not a defect but
   * the token loss §2.2.1.1 exists to recover from, and asserting the opposite
   * -- as this test first did -- asserts something a real ring cannot promise.
   * Recovery is the next test's subject. */
  ap_ring_medium_set_bypass(&r.medium, r.station[2].slot, true);
  const uint64_t before = r.station[3].tokens_seen;
  ap_ring_station_originate_token(&r.station[0], AP_RING_OOB_FREE_TOKEN);
  for (unsigned t = 0; t < 200u; t++) {
    step(&r);
  }
  /* A token put on after the removal reaches the far side, so the path is
   * intact and the bypassed node is being crossed rather than swallowing it. */
  TEST_ASSERT_TRUE(r.station[3].tokens_seen > before);
  TEST_ASSERT_FALSE(r.station[3].saw_biphase_error);
}

/* And a node rejoining mid-run. Its slot is kept while it is bypassed, so the
 * cable order is unchanged and it comes back where it was -- §3.3.1's PLL
 * relationship is between adjacent nodes, so who is adjacent must not drift. */
static void test_a_node_rejoining_lands_back_in_the_same_cable_order(void) {
  ring_t r;
  build(&r, 4u);
  const int slot = r.station[2].slot;
  ap_ring_medium_set_bypass(&r.medium, slot, true);
  for (unsigned t = 0; t < 50u; t++) {
    step(&r);
  }
  ap_ring_medium_set_bypass(&r.medium, slot, false);
  TEST_ASSERT_EQUAL_INT(slot, r.station[2].slot);
  TEST_ASSERT_TRUE(ap_ring_medium_attached(&r.medium, slot));
}

/* §2.2.1.1: "If no token exists on the network (for example, if the ring has
 * broken), any node that wants to transmit can generate a claimed token (after
 * a specified timeout) in order to force transmission."
 *
 * The manual says *that* there is a timeout and never what it is, so the value
 * is PROVISIONAL. What is testable is the behaviour: a station that wants the
 * ring and never sees a token eventually forces one, and does not force one
 * while tokens are going by. */
static void test_a_station_forces_a_token_only_when_none_is_circulating(void) {
  /* A ring with no token at all: nobody originates one. */
  ring_t r;
  build(&r, 3u);
  r.station[1].wants_ring = true;
  for (uint64_t t = 0; t < AP_RING_TOKEN_LOSS_TIMEOUT_BITS + 32u; t++) {
    step(&r);
  }
  TEST_ASSERT_EQUAL_UINT64(1u, r.station[1].forced_tokens);
  TEST_ASSERT_TRUE(r.station[1].holds_ring);

  /* And on a ring that *has* a token, a waiting station takes the token
   * rather than forcing a second one -- which is how a token ring ends up
   * with two, and the reason a claimed token resets the loss timer too. */
  ring_t q;
  ap_ring_medium_init(&q.medium);
  q.nodes = 3u;
  for (unsigned i = 0; i < q.nodes; i++) {
    const int slot = ap_ring_medium_attach(&q.medium);
    ap_ring_medium_set_cable_bits(&q.medium, slot, 4u);
    ap_ring_station_init(&q.station[i], slot);
  }
  q.station[1].wants_ring = true;
  ap_ring_station_originate_token(&q.station[0], AP_RING_OOB_FREE_TOKEN);
  for (uint64_t t = 0; t < AP_RING_TOKEN_LOSS_TIMEOUT_BITS + 32u; t++) {
    step(&q);
  }
  TEST_ASSERT_EQUAL_UINT64(0u, q.station[1].forced_tokens);
  /* It *claimed* rather than forced. `holds_ring` is not the thing to assert
   * over this long a run: the station claims early, strips, and the stripping
   * timeout then releases the ring -- correctly, per §2.1 step 7 -- so by the
   * end it holds nothing. Asserting `holds_ring` here was asserting that the
   * documented timeout does not work. */
  TEST_ASSERT_EQUAL_UINT64(1u, q.station[1].claims_made);
  TEST_ASSERT_EQUAL_UINT64(1u, q.station[1].strip_timeouts);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_stripping_timeout_matches_both_forms_the_manual_gives);
  RUN_TEST(test_claiming_the_ring_begins_stripping);
  RUN_TEST(test_a_node_leaving_mid_run_does_not_break_the_ring);
  RUN_TEST(test_a_node_rejoining_lands_back_in_the_same_cable_order);
  RUN_TEST(test_a_station_forces_a_token_only_when_none_is_circulating);
  RUN_TEST(test_a_token_laps_the_ring_in_one_bit_time_per_station);
  RUN_TEST(test_a_quiet_ring_produces_no_biphase_errors);
  RUN_TEST(test_a_station_claims_the_first_free_token_that_reaches_it);
  RUN_TEST(test_only_the_upstream_of_two_contenders_takes_the_ring);
  RUN_TEST(test_a_station_wanting_nothing_forwards_the_token_intact);
  RUN_TEST(test_cable_lets_a_three_station_ring_circulate_a_token);
  RUN_TEST(test_the_station_recognises_a_nine_bit_symbol);
  return UNITY_END();
}
