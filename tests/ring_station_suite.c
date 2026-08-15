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

/* **A frame crosses the ring, which nothing checked before.**
 *
 * `RING.md` 85 audited `[MAC]` chapter 2 against the code and found the frame
 * layer and the station layer unconnected: every module was correct against
 * its own section, and nothing outside `ap_ring_framer`'s own tests ever
 * called it, so no frame was ever put on the medium. Three normative parts of
 * §2.1 were absent with it -- step 3's "begins to transmit its packet", step
 * 6's "sends out a new free token to follow the frame", and step 7's "until it
 * finishes receiving its own frame".
 *
 * This is the test that would have caught it: a station is given a frame, and
 * the ring is required to carry it. */
static void test_a_queued_frame_is_driven_onto_the_ring(void) {
  ring_t r;
  build(&r, 3u);
  static uint8_t txbuf[2048];
  uint8_t header[12] = {0};
  ap_ring_header_set_destination(header, 0x00012345u);
  ap_ring_header_set_type(header, AP_RING_TYPE_USER);
  ap_ring_header_set_source(header, 0x00067890u);

  ap_ring_station_attach_tx(&r.station[0], txbuf, sizeof txbuf);
  const ap_ring_frame_fields_t fields = {
      .header = header, .header_bytes = sizeof header,
      .data = NULL, .data_bytes = 0u, .late_acknowledge = 0u};
  TEST_ASSERT_TRUE(ap_ring_station_queue_frame(&r.station[0], &fields));
  /* §2.2.2.2's minimum: the controller "will always transmit the first 12
   * bytes of a packet header", and a frame's assembly is refused outright
   * below that -- so a non-zero bit count is the framer having accepted it. */
  TEST_ASSERT_TRUE(r.station[0].tx_bit_count > 0u);

  /* A free token to acquire, then long enough for the frame to go round. */
  ap_ring_station_originate_token(&r.station[1], AP_RING_OOB_FREE_TOKEN);
  for (unsigned i = 0; i < 4000u; i++) {
    step(&r);
  }

  /* Step 3: the frame was driven, every bit of it. */
  TEST_ASSERT_TRUE(ap_ring_station_transmitted(&r.station[0]));
  TEST_ASSERT_EQUAL_UINT(r.station[0].tx_bit_count, r.station[0].tx_bit_pos);

  /* Step 7: stripping ended on the station's own frame returning, **not** on
   * the 10.9 ms timeout -- which is the arm that did not exist. A run of 4000
   * bits cannot reach 131,072, so a still-stripping station would prove the
   * timeout was the only exit. */
  TEST_ASSERT_FALSE(r.station[0].stripping);
  TEST_ASSERT_EQUAL_UINT64(0u, r.station[0].strip_timeouts);

  /* Step 6: a new free token follows the frame, so the ring is released. A
   * station that held it would leave the others unable to transmit for ever,
   * which is what the audit found. */
  TEST_ASSERT_FALSE(r.station[0].holds_ring);
  TEST_ASSERT_TRUE(r.station[1].tokens_seen > 0u);
}

/* **§2.2.2.2's receive decision, which did not exist.** "The hardware compares
 * the contents of the 32-bit destination address field with the node address
 * of the target ... a node receives a message if the destination address field
 * matches its node address, or if the broadcast bit in the type field is set."
 * `RING.md` 85b found no address on a station at all.
 *
 * Both arms are checked, and the bystander is the one that matters: a station
 * that reported every frame as its own would pass an addressed-node test and
 * be useless. */
static void test_a_frame_is_accepted_only_by_its_addressee(void) {
  ring_t r;
  build(&r, 3u);
  static uint8_t txbuf[2048];
  uint8_t header[12] = {0};
  ap_ring_header_set_destination(header, 0x00ABCDEFu);
  ap_ring_header_set_type(header, AP_RING_TYPE_USER);
  ap_ring_header_set_source(header, 0x00012345u);

  ap_ring_station_set_address(&r.station[1], 0x00ABCDEFu);
  ap_ring_station_set_address(&r.station[2], 0x00FEDCBAu);
  ap_ring_station_attach_tx(&r.station[0], txbuf, sizeof txbuf);
  const ap_ring_frame_fields_t fields = {
      .header = header, .header_bytes = sizeof header,
      .data = NULL, .data_bytes = 0u, .late_acknowledge = 0u};
  TEST_ASSERT_TRUE(ap_ring_station_queue_frame(&r.station[0], &fields));
  ap_ring_station_originate_token(&r.station[1], AP_RING_OOB_FREE_TOKEN);
  for (unsigned i = 0; i < 4000u; i++) {
    step(&r);
  }

  /* Both saw a frame go past -- the frame start is not addressed to anyone. */
  TEST_ASSERT_TRUE(r.station[1].frames_seen > 0u);
  TEST_ASSERT_TRUE(r.station[2].frames_seen > 0u);
  /* Only the addressee accepted it. */
  TEST_ASSERT_TRUE(r.station[1].frames_addressed > 0u);
  TEST_ASSERT_EQUAL_UINT64(0u, r.station[2].frames_addressed);
}

/* And the broadcast arm, which §2.2.2.2 says overrides the address: "If it is
 * set, receivers ignore the destination address field." The destination here
 * matches *neither* station, so a model that only compared addresses would
 * accept nowhere. */
static void test_a_broadcast_is_accepted_regardless_of_destination(void) {
  ring_t r;
  build(&r, 3u);
  static uint8_t txbuf[2048];
  uint8_t header[12] = {0};
  ap_ring_header_set_destination(header, 0x00000001u);
  ap_ring_header_set_type(header, AP_RING_TYPE_BROADCAST);
  ap_ring_header_set_source(header, 0x00012345u);

  ap_ring_station_set_address(&r.station[1], 0x00ABCDEFu);
  ap_ring_station_set_address(&r.station[2], 0x00FEDCBAu);
  ap_ring_station_attach_tx(&r.station[0], txbuf, sizeof txbuf);
  const ap_ring_frame_fields_t fields = {
      .header = header, .header_bytes = sizeof header,
      .data = NULL, .data_bytes = 0u, .late_acknowledge = 0u};
  TEST_ASSERT_TRUE(ap_ring_station_queue_frame(&r.station[0], &fields));
  ap_ring_station_originate_token(&r.station[1], AP_RING_OOB_FREE_TOKEN);
  for (unsigned i = 0; i < 4000u; i++) {
    step(&r);
  }
  TEST_ASSERT_TRUE(r.station[1].frames_addressed > 0u);
  TEST_ASSERT_TRUE(r.station[2].frames_addressed > 0u);
}

/* **§2.2.2.2's early acknowledge, modified as the frame passes.** "A node's
 * ring transmitter inserts an early acknowledge field; another node's receiver
 * modifies it", and Figure 2-7: intend-to-copy is set by "an addressed
 * receiver". `RING.md` 85c found nothing anywhere touching either acknowledge
 * field in flight.
 *
 * The sender is the witness: it strips its own frame, so the field it reads
 * back through its own receive path is the one the addressee rewrote on the
 * way round. Checking at the *sender* rather than at the modifier is what makes
 * this a test of the field travelling, not of a local variable. */
static void test_an_addressed_receiver_sets_intend_to_copy_in_flight(void) {
  ring_t r;
  build(&r, 3u);
  static uint8_t txbuf[2048];
  uint8_t header[12] = {0};
  ap_ring_header_set_destination(header, 0x00ABCDEFu);
  ap_ring_header_set_type(header, AP_RING_TYPE_USER);
  ap_ring_header_set_source(header, 0x00012345u);
  /* The transmitter inserts the field. `set_early_ack` writes verbatim, so the
   * parity is the caller's -- an all-zero field is **even** and therefore not
   * a legal one, which is what the first version of this test wrote and what
   * `ap_ring_ack_with_parity` exists to prevent. */
  ap_ring_header_set_early_ack(header, ap_ring_ack_with_parity(0u));
  TEST_ASSERT_TRUE(ap_ring_ack_parity_ok(header[AP_RING_HDR_EARLY_ACK]));

  ap_ring_station_set_address(&r.station[0], 0x00012345u);
  ap_ring_station_set_address(&r.station[1], 0x00ABCDEFu);
  ap_ring_station_set_address(&r.station[2], 0x00FEDCBAu);
  ap_ring_station_attach_tx(&r.station[0], txbuf, sizeof txbuf);
  const ap_ring_frame_fields_t fields = {
      .header = header, .header_bytes = sizeof header,
      .data = NULL, .data_bytes = 0u, .late_acknowledge = 0u};
  TEST_ASSERT_TRUE(ap_ring_station_queue_frame(&r.station[0], &fields));
  ap_ring_station_originate_token(&r.station[1], AP_RING_OOB_FREE_TOKEN);
  for (unsigned i = 0; i < 4000u; i++) {
    step(&r);
  }

  /* Station 1 was addressed and station 2 was not. */
  TEST_ASSERT_TRUE(r.station[1].frames_addressed > 0u);
  TEST_ASSERT_EQUAL_UINT64(0u, r.station[2].frames_addressed);

  /* And the bit arrived downstream: station 2 forwards the frame *after*
   * station 1 has rewritten it, so what station 2 captured at header byte 7
   * carries intend-to-copy. A field that was only altered locally would leave
   * this clear. */
  TEST_ASSERT_TRUE((r.station[2].rx_header[7] &
                    AP_RING_EARLY_INTEND_TO_COPY) != 0u);
  /* Odd parity is maintained, which is the half a naive rewrite breaks. */
  TEST_ASSERT_TRUE(ap_ring_ack_parity_ok(r.station[2].rx_header[7]));
}

/* **§2.2.2.5's late acknowledge, set as the frame's end goes past.** "This
 * field tells the sending node whether or not the frame has been received and
 * the packet has been copied. The transmitter inserts a late acknowledge
 * field; the receiver modifies it." Figure 2-8: *copied* is set by "a receiver
 * that has successfully copied the packet", *wait ack* by "an addressed
 * receiver that wasn't enabled to copy".
 *
 * Reaching it means tracking a frame past its header -- three separators, then
 * the CRC and a null separator -- which is what `RING.md` 88d left open. */
static void late_ack_ring(ring_t *r, uint8_t *txbuf, size_t txcap,
                          bool enabled) {
  build(r, 3u);
  static uint8_t header[12];
  for (unsigned i = 0; i < sizeof header; i++) {
    header[i] = 0u;
  }
  ap_ring_header_set_destination(header, 0x00ABCDEFu);
  ap_ring_header_set_type(header, AP_RING_TYPE_USER);
  ap_ring_header_set_source(header, 0x00012345u);
  ap_ring_header_set_early_ack(header, ap_ring_ack_with_parity(0u));

  ap_ring_station_set_address(&r->station[1], 0x00ABCDEFu);
  ap_ring_station_set_address(&r->station[2], 0x00FEDCBAu);
  ap_ring_station_set_receive_enabled(&r->station[1], enabled);
  ap_ring_station_attach_tx(&r->station[0], txbuf, txcap);
  const ap_ring_frame_fields_t fields = {
      .header = header, .header_bytes = sizeof header,
      .data = NULL, .data_bytes = 0u,
      /* The transmitter inserts the field, with legal odd parity. */
      .late_acknowledge = ap_ring_ack_with_parity(0u)};
  TEST_ASSERT_TRUE(ap_ring_station_queue_frame(&r->station[0], &fields));
  ap_ring_station_originate_token(&r->station[1], AP_RING_OOB_FREE_TOKEN);
  for (unsigned i = 0; i < 4000u; i++) {
    step(r);
  }
}

static void test_an_addressed_receiver_sets_copied_in_the_late_ack(void) {
  ring_t r;
  static uint8_t txbuf[2048];
  late_ack_ring(&r, txbuf, sizeof txbuf, true);

  /* The addressee copied it, and the bystander did neither. */
  TEST_ASSERT_TRUE(r.station[1].frames_copied > 0u);
  TEST_ASSERT_EQUAL_UINT64(0u, r.station[1].frames_wacked);
  TEST_ASSERT_EQUAL_UINT64(0u, r.station[2].frames_copied);

  /* And the field that travelled carries it. Read at the station *downstream*
   * of the modifier, so this is about a byte on the wire. */
  TEST_ASSERT_TRUE((r.station[2].rx_late & AP_RING_LATE_COPIED) != 0u);
  TEST_ASSERT_TRUE((r.station[2].rx_late & AP_RING_LATE_INTEND_TO_COPY) != 0u);
  TEST_ASSERT_TRUE(ap_ring_ack_parity_ok(r.station[2].rx_late));
}

/* The other arm, which a model that always reported success would fail: an
 * addressed receiver that is *not* enabled sets wait-ack instead of copied. */
static void test_a_receiver_not_enabled_to_copy_sets_wait_ack(void) {
  ring_t r;
  static uint8_t txbuf[2048];
  late_ack_ring(&r, txbuf, sizeof txbuf, false);

  TEST_ASSERT_TRUE(r.station[1].frames_wacked > 0u);
  TEST_ASSERT_EQUAL_UINT64(0u, r.station[1].frames_copied);
  TEST_ASSERT_TRUE((r.station[2].rx_late & AP_RING_LATE_WAIT_ACK) != 0u);
  TEST_ASSERT_EQUAL_HEX8(0u, r.station[2].rx_late & AP_RING_LATE_COPIED);
  TEST_ASSERT_TRUE(ap_ring_ack_parity_ok(r.station[2].rx_late));
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
  RUN_TEST(test_a_queued_frame_is_driven_onto_the_ring);
  RUN_TEST(test_a_frame_is_accepted_only_by_its_addressee);
  RUN_TEST(test_a_broadcast_is_accepted_regardless_of_destination);
  RUN_TEST(test_an_addressed_receiver_sets_intend_to_copy_in_flight);
  RUN_TEST(test_an_addressed_receiver_sets_copied_in_the_late_ack);
  RUN_TEST(test_a_receiver_not_enabled_to_copy_sets_wait_ack);
  return UNITY_END();
}
