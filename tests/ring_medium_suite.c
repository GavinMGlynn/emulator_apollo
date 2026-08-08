/* The ring medium, tested with synthetic nodes only -- which is the plan
 * item's stated verification. No node core is involved: this suite drives
 * cells in and reads them out, so what it checks is the *topology and timing*
 * of the medium and nothing about any node's behaviour. */

#include "ring/ap_ring_mac.h"
#include "ring/ap_ring_medium.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* A distinguishable cell, so a test can tell which node's signal arrived. */
static ap_ring_cell_t mark(unsigned n) {
  return (ap_ring_cell_t){.clock_window = (n & 1u) != 0u,
                          .data_window = (n & 2u) != 0u};
}

static bool same(ap_ring_cell_t a, ap_ring_cell_t b) {
  return a.clock_window == b.clock_window && a.data_window == b.data_window;
}

static void test_attaching_hands_out_slots_in_cable_order(void) {
  ap_ring_medium_t m;
  ap_ring_medium_init(&m);
  TEST_ASSERT_EQUAL_INT(0, ap_ring_medium_attach(&m));
  TEST_ASSERT_EQUAL_INT(1, ap_ring_medium_attach(&m));
  TEST_ASSERT_EQUAL_INT(2, ap_ring_medium_attach(&m));
  TEST_ASSERT_TRUE(ap_ring_medium_attached(&m, 1));

  /* A detached slot is reused rather than appended, so a node that leaves and
   * returns lands back where it was. Renumbering instead would silently change
   * which node is upstream of which -- and `[MAC]` §3.3.1's PLL relationship
   * is defined between *adjacent* nodes. */
  ap_ring_medium_detach(&m, 1);
  TEST_ASSERT_FALSE(ap_ring_medium_attached(&m, 1));
  TEST_ASSERT_EQUAL_INT(1, ap_ring_medium_attach(&m));
}

/* The signal moves one hop per bit clock, from slot i to slot i+1, wrapping.
 * Exactly one hop: the ring is one clock domain, and a cell crossing two nodes
 * in a bit time would break the timing argument the whole network rests on. */
static void test_a_cell_moves_exactly_one_hop_per_bit_clock(void) {
  ap_ring_medium_t m;
  ap_ring_medium_init(&m);
  const int a = ap_ring_medium_attach(&m);
  const int b = ap_ring_medium_attach(&m);
  const int c = ap_ring_medium_attach(&m);

  ap_ring_medium_transmit(&m, a, mark(1));
  ap_ring_medium_transmit(&m, b, mark(2));
  ap_ring_medium_transmit(&m, c, mark(3));
  ap_ring_medium_advance(&m);

  /* Each node hears its upstream neighbour, and c wraps round to a. */
  TEST_ASSERT_TRUE(same(mark(3), ap_ring_medium_receive(&m, a)));
  TEST_ASSERT_TRUE(same(mark(1), ap_ring_medium_receive(&m, b)));
  TEST_ASSERT_TRUE(same(mark(2), ap_ring_medium_receive(&m, c)));
}

/* And a cell goes all the way round in as many bit clocks as there are nodes,
 * which is the property a token depends on. Each node retransmits what it
 * heard -- "they receive data in, and then immediately send it out",
 * `[MAC]` §3.2. */
static void test_a_cell_returns_to_its_sender_after_one_lap(void) {
  ap_ring_medium_t m;
  ap_ring_medium_init(&m);
  const int a = ap_ring_medium_attach(&m);
  const int b = ap_ring_medium_attach(&m);
  const int c = ap_ring_medium_attach(&m);
  const int slots[] = {a, b, c};

  ap_ring_medium_transmit(&m, a, mark(3));
  ap_ring_medium_transmit(&m, b, mark(0));
  ap_ring_medium_transmit(&m, c, mark(0));

  for (unsigned lap = 0; lap < 3u; lap++) {
    ap_ring_medium_advance(&m);
    /* Every node transceives: what it heard, it drives next. */
    for (unsigned i = 0; i < 3u; i++) {
      ap_ring_medium_transmit(&m, slots[i],
                              ap_ring_medium_receive(&m, slots[i]));
    }
  }
  TEST_ASSERT_TRUE(same(mark(3), ap_ring_medium_receive(&m, a)));
  TEST_ASSERT_EQUAL_UINT64(3u, m.bit_time);
}

/* `[MAC]` §3.5: a bypassed node's relays join its input coax to its output
 * coax, so the ring stays intact through it -- the signal passes rather than
 * the ring rerouting around it. */
static void test_a_bypassed_node_passes_the_ring_through(void) {
  ap_ring_medium_t m;
  ap_ring_medium_init(&m);
  const int a = ap_ring_medium_attach(&m);
  const int b = ap_ring_medium_attach(&m);
  const int c = ap_ring_medium_attach(&m);

  ap_ring_medium_set_bypass(&m, b, true);
  ap_ring_medium_transmit(&m, a, mark(1));
  ap_ring_medium_transmit(&m, b, mark(2)); /* driven, but b is bypassed */

  /* One bit clock, not two: b's relays are cable, so a is c's upstream
   * *driver* and the hop is a single one. A bypassed node adds no bit delay --
   * which is the same fact that makes it contribute none to the ring's total
   * delay below. */
  ap_ring_medium_advance(&m);

  TEST_ASSERT_TRUE(same(mark(1), ap_ring_medium_receive(&m, c)));
  TEST_ASSERT_EQUAL_UINT64(1u, m.bit_time);
}

/* The same relays "connect the node's transmit output to its receive input",
 * which is what lets a bypassed node run loopback self-tests -- the ring
 * firmware's own self-test, and this controller's first real check. A model
 * with only the pass-through half would make that impossible to run. */
static void test_a_bypassed_node_hears_its_own_transmission(void) {
  ap_ring_medium_t m;
  ap_ring_medium_init(&m);
  const int a = ap_ring_medium_attach(&m);
  const int b = ap_ring_medium_attach(&m);

  ap_ring_medium_transmit(&m, a, mark(1));
  ap_ring_medium_set_bypass(&m, b, true);
  ap_ring_medium_transmit(&m, b, mark(2));
  ap_ring_medium_advance(&m);

  /* b hears itself, not a. */
  TEST_ASSERT_TRUE(same(mark(2), ap_ring_medium_receive(&m, b)));
}

/* A detached slot is a gap in the cable: it neither drives nor delays, so its
 * neighbours become adjacent. */
static void test_a_detached_slot_is_a_gap_and_not_a_node(void) {
  ap_ring_medium_t m;
  ap_ring_medium_init(&m);
  const int a = ap_ring_medium_attach(&m);
  const int b = ap_ring_medium_attach(&m);
  const int c = ap_ring_medium_attach(&m);

  ap_ring_medium_detach(&m, b);
  ap_ring_medium_transmit(&m, a, mark(1));
  ap_ring_medium_advance(&m);

  /* With b gone, a is directly upstream of c. */
  TEST_ASSERT_TRUE(same(mark(1), ap_ring_medium_receive(&m, c)));
}

/* `[MAC]` §3.3: "the total delay around the network must be exactly an
 * integral -- rather than a fractional -- number of bit-times". With each node
 * contributing its elastic-store delay, that is a property of the ring the
 * caller built, and one it can ask about. */
static void test_ring_stability_is_the_integral_bit_time_condition(void) {
  ap_ring_medium_t m;
  ap_ring_medium_init(&m);
  for (unsigned i = 0; i < 4u; i++) {
    (void)ap_ring_medium_attach(&m);
  }

  /* Four nodes at the nominal one-bit delay: four bit-times, stable. */
  TEST_ASSERT_EQUAL_INT(400, ap_ring_medium_delay_centibits(
                                 &m, AP_RING_ESB_NOMINAL_CENTIBITS));
  TEST_ASSERT_TRUE(
      ap_ring_medium_stable(&m, AP_RING_ESB_NOMINAL_CENTIBITS));

  /* At 1.25 bits each the total is five bit-times -- also integral, and the
   * point of the elastic store: the fractional parts sum to a whole. */
  TEST_ASSERT_TRUE(ap_ring_medium_stable(&m, 125));
  /* At 1.1 bits each it is 4.4 bit-times, which is not. */
  TEST_ASSERT_FALSE(ap_ring_medium_stable(&m, 110));

  /* A bypassed node's relays are cable, not a retiming element, so it stops
   * contributing delay. */
  ap_ring_medium_set_bypass(&m, 0, true);
  TEST_ASSERT_EQUAL_INT(300, ap_ring_medium_delay_centibits(
                                 &m, AP_RING_ESB_NOMINAL_CENTIBITS));
}

/* Nothing crossing this interface is a pointer into another node: a cell goes
 * in by value and comes out by value, which is what a process-separated
 * transport would have to carry. Asserted by construction here -- the medium
 * survives a caller mutating the cell it handed over. */
static void test_cells_cross_the_interface_by_value(void) {
  ap_ring_medium_t m;
  ap_ring_medium_init(&m);
  const int a = ap_ring_medium_attach(&m);
  const int b = ap_ring_medium_attach(&m);

  ap_ring_cell_t cell = mark(1);
  ap_ring_medium_transmit(&m, a, cell);
  cell.clock_window = !cell.clock_window; /* the caller's copy changes */
  ap_ring_medium_advance(&m);

  TEST_ASSERT_TRUE(same(mark(1), ap_ring_medium_receive(&m, b)));
}

/* Out-of-range slots are refused rather than corrupting a neighbour. */
static void test_an_invalid_slot_is_refused(void) {
  ap_ring_medium_t m;
  ap_ring_medium_init(&m);
  const int a = ap_ring_medium_attach(&m);

  ap_ring_medium_transmit(&m, 99, mark(3));
  ap_ring_medium_transmit(&m, -1, mark(3));
  ap_ring_medium_detach(&m, 99);
  TEST_ASSERT_TRUE(ap_ring_medium_attached(&m, a));
  TEST_ASSERT_FALSE(ap_ring_medium_attached(&m, 99));
}

/* `[MAC]` §3.3 counts "cable plant" among the static elements contributing ring
 * delay, alongside the connected nodes. A cable of C bit times delays a cell by
 * C on top of the hop itself. */
static void test_a_cable_delays_a_cell_by_its_length(void) {
  ap_ring_medium_t m;
  ap_ring_medium_init(&m);
  const int a = ap_ring_medium_attach(&m);
  const int b = ap_ring_medium_attach(&m);
  ap_ring_medium_set_cable_bits(&m, a, 5u);

  /* `mark(1)` and not `mark(3)`: a cable is filled with *idle*, and idle is a
   * run of encoded Zeros, which alternates between `{T,T}` and `{F,F}` -- so
   * `mark(3)` and `mark(0)` are cells the cable already contains. A marker has
   * to be one idle never produces, or "it has not arrived yet" is untestable.
   * Choosing `mark(3)` here made this test fail against a correct medium. */
  ap_ring_medium_transmit(&m, a, mark(1));
  /* Five bit times of cable: nothing of a's has reached b yet. */
  for (unsigned t = 0; t < 5u; t++) {
    ap_ring_medium_advance(&m);
    TEST_ASSERT_FALSE(same(mark(1), ap_ring_medium_receive(&m, b)));
  }
  ap_ring_medium_advance(&m);
  TEST_ASSERT_TRUE(same(mark(1), ap_ring_medium_receive(&m, b)));
}

/* A length the medium cannot model is refused rather than truncated: a caller
 * asking for more has misread the medium, and silently shortening their cable
 * would move every timing figure they then measured. */
static void test_a_cable_longer_than_the_medium_models_is_refused(void) {
  ap_ring_medium_t m;
  ap_ring_medium_init(&m);
  const int a = ap_ring_medium_attach(&m);
  ap_ring_medium_set_cable_bits(&m, a, AP_RING_MAX_CABLE_BITS + 1u);
  TEST_ASSERT_EQUAL_UINT(0u, m.node[a].cable_bits);
  ap_ring_medium_set_cable_bits(&m, a, AP_RING_MAX_CABLE_BITS);
  TEST_ASSERT_EQUAL_UINT(AP_RING_MAX_CABLE_BITS, m.node[a].cable_bits);
}

/* Circumference is what has to exceed a token's width for a ring to carry one,
 * and it is the station's own bit plus the cable it drives. Three nodes with
 * four bits of cable each is fifteen -- comfortably over a nine-bit token,
 * where three nodes with no cable would be three. */
static void test_cable_makes_a_small_ring_longer_than_its_token(void) {
  ap_ring_medium_t m;
  ap_ring_medium_init(&m);
  for (unsigned i = 0; i < 3u; i++) {
    const int slot = ap_ring_medium_attach(&m);
    ap_ring_medium_set_cable_bits(&m, slot, 4u);
  }
  TEST_ASSERT_EQUAL_UINT(15u, ap_ring_medium_circumference_bits(&m));
  TEST_ASSERT_TRUE(ap_ring_medium_circumference_bits(&m) > AP_RING_OOB_BITS);

  /* A bypassed node contributes neither its bit nor its cable, for the same
   * reason it contributes no delay. */
  ap_ring_medium_set_bypass(&m, 0, true);
  TEST_ASSERT_EQUAL_UINT(10u, ap_ring_medium_circumference_bits(&m));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_cable_delays_a_cell_by_its_length);
  RUN_TEST(test_a_cable_longer_than_the_medium_models_is_refused);
  RUN_TEST(test_cable_makes_a_small_ring_longer_than_its_token);
  RUN_TEST(test_attaching_hands_out_slots_in_cable_order);
  RUN_TEST(test_a_cell_moves_exactly_one_hop_per_bit_clock);
  RUN_TEST(test_a_cell_returns_to_its_sender_after_one_lap);
  RUN_TEST(test_a_bypassed_node_passes_the_ring_through);
  RUN_TEST(test_a_bypassed_node_hears_its_own_transmission);
  RUN_TEST(test_a_detached_slot_is_a_gap_and_not_a_node);
  RUN_TEST(test_ring_stability_is_the_integral_bit_time_condition);
  RUN_TEST(test_cells_cross_the_interface_by_value);
  RUN_TEST(test_an_invalid_slot_is_refused);
  return UNITY_END();
}
