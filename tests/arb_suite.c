/* MC68030 bus arbitration control unit, `[030]` §7.7.
 *
 * Every test here states a fact about the part, and every fact is either quoted
 * from §7.7 or derived from the state walk in §7.7.4. Figure 7-61 itself did
 * not survive the scan; see `ap_m68030_arb.h`. */

#include "unity.h"

#include "cpu/m68030/ap_m68030_arb.h"

void setUp(void) {}
void tearDown(void) {}

/* Clock the arbiter `n` times. */
static void tick(ap_m68030_arb_t *arb, unsigned n) {
  for (unsigned i = 0; i < n; i++) {
    ap_m68030_arb_tick(arb);
  }
}

/* Clock until BG asserts, up to a bound, and answer how many clocks it took.
 * Answers `limit + 1` if it never did, so a test can distinguish "took too
 * long" from "took the last clock we allowed". */
static unsigned clocks_until_grant(ap_m68030_arb_t *arb, unsigned limit) {
  for (unsigned i = 1; i <= limit; i++) {
    ap_m68030_arb_tick(arb);
    if (ap_m68030_arb_bus_grant(arb)) {
      return i;
    }
  }
  return limit + 1;
}

static void test_the_processor_is_bus_master_out_of_reset(void) {
  ap_m68030_arb_t arb;
  ap_m68030_arb_init(&arb);

  /* "State 0 ... in which G and T are both negated, is the state of the bus
   * arbiter while the processor is bus master." */
  TEST_ASSERT_EQUAL_UINT(AP_M68030_ARB_STATE_0, arb.state);
  TEST_ASSERT_FALSE(ap_m68030_arb_bus_grant(&arb));
  TEST_ASSERT_FALSE(ap_m68030_arb_three_state(&arb));
  TEST_ASSERT_TRUE(ap_m68030_arb_processor_is_master(&arb));
}

static void test_an_idle_bus_never_leaves_the_processor_as_master(void) {
  ap_m68030_arb_t arb;
  ap_m68030_arb_init(&arb);

  /* "Request R and acknowledge A keep the arbiter in state 0 as long as they
   * are both negated." Long enough that a synchroniser shifting a stuck bit
   * would have shown up. */
  tick(&arb, 64);

  TEST_ASSERT_EQUAL_UINT(AP_M68030_ARB_STATE_0, arb.state);
  TEST_ASSERT_TRUE(ap_m68030_arb_processor_is_master(&arb));
}

static void test_a_bus_request_is_granted_the_bus(void) {
  ap_m68030_arb_t arb;
  ap_m68030_arb_init(&arb);

  ap_m68030_arb_set_request(&arb, true);
  TEST_ASSERT_LESS_OR_EQUAL_UINT(8u, clocks_until_grant(&arb, 8u));

  TEST_ASSERT_EQUAL_UINT(AP_M68030_ARB_STATE_1, arb.state);
  TEST_ASSERT_TRUE(ap_m68030_arb_three_state(&arb));
}

/* **Grant latency lies inside the manufacturer's published envelope.**
 *
 * `MC68030EC/D` (`MC68030 Electrical Specifications`) p. 7, parameter **35**,
 * "BR Asserted to BG Asserted (RMC Not Asserted)": **1.5 min, 3.5 max Clks** —
 * and identically so at 20, 25, 33.33, 40 and 50 MHz. Parameter 37 gives BGACK
 * asserted to BG negated the same 1.5-3.5 window.
 *
 * That envelope is what the user's manual leaves as prose. §7.7.4 says only
 * that "all asynchronous inputs to the MC68030 are internally synchronized in a
 * maximum of two cycles of the processor clock", which bounds the synchroniser
 * and never states the latency it produces. The electrical specification states
 * the latency directly, as a range, measured.
 *
 * A two-clock spread between min and max is exactly one synchroniser's worth of
 * uncertainty, which is the specification agreeing that this genuinely is a
 * range rather than a figure someone declined to publish. Both plausible models
 * sit inside it: a two-clock synchroniser plus an edge gives three clocks, a
 * one-clock synchroniser gives two, and 1.5 <= 2 < 3 <= 3.5.
 *
 * So this test cannot pin the synchroniser — nothing can, from documents — but
 * it pins the thing that matters, which is that the choice keeps the part
 * inside its own published timing. A change to the synchroniser that left the
 * envelope would be wrong however defensible it looked. */
static void test_grant_latency_is_within_the_published_envelope(void) {
  ap_m68030_arb_t arb;
  ap_m68030_arb_init(&arb);
  ap_m68030_arb_set_request(&arb, true);

  unsigned clocks = 0;
  while (clocks < 8u && !ap_m68030_arb_bus_grant(&arb)) {
    ap_m68030_arb_tick(&arb);
    clocks++;
  }

  TEST_ASSERT_TRUE_MESSAGE(ap_m68030_arb_bus_grant(&arb),
                           "no grant at all within eight clocks");
  /* The envelope is 1.5 to 3.5 clocks. A clock-stepped model can only land on
   * whole clocks, so the reachable part of it is 2 or 3. */
  TEST_ASSERT_TRUE_MESSAGE(clocks >= 2u && clocks <= 3u,
                           "grant latency outside MC68030EC/D parameter 35");
}

static void test_a_request_is_not_granted_in_the_clock_it_is_asserted(void) {
  ap_m68030_arb_t arb;
  ap_m68030_arb_init(&arb);

  /* §7.7.4: R is a "internally synchronized version" of BR, and "State changes
   * occur on the next rising edge of the clock after the internal signal is
   * valid". Synchronisation is published as "a maximum of two cycles", so the
   * earliest a grant can appear is the third clock. Modelled at the maximum and
   * PROVISIONAL -- see the header -- but the *shape* is not provisional: a
   * model granting on the first clock has no synchroniser at all. */
  ap_m68030_arb_set_request(&arb, true);

  ap_m68030_arb_tick(&arb);
  TEST_ASSERT_FALSE(ap_m68030_arb_bus_grant(&arb));
  ap_m68030_arb_tick(&arb);
  TEST_ASSERT_FALSE(ap_m68030_arb_bus_grant(&arb));
  ap_m68030_arb_tick(&arb);
  TEST_ASSERT_TRUE(ap_m68030_arb_bus_grant(&arb));
}

static void test_a_grant_is_held_until_the_request_is_answered(void) {
  ap_m68030_arb_t arb;
  ap_m68030_arb_init(&arb);

  ap_m68030_arb_set_request(&arb, true);
  (void)clocks_until_grant(&arb, 8u);

  /* "The bus arbiter remains in that state until acknowledge A is asserted or
   * request R is negated." Neither has happened, so BG stays up however long
   * the device dithers. */
  tick(&arb, 32);
  TEST_ASSERT_EQUAL_UINT(AP_M68030_ARB_STATE_2, arb.state);
  TEST_ASSERT_TRUE(ap_m68030_arb_bus_grant(&arb));
}

static void test_acknowledging_a_grant_negates_it(void) {
  ap_m68030_arb_t arb;
  ap_m68030_arb_init(&arb);

  ap_m68030_arb_set_request(&arb, true);
  (void)clocks_until_grant(&arb, 8u);

  /* "Once either occurs, the arbiter changes to the center state, state 3, and
   * negates grant G." The device also drops BR, as Figure 7-60 shows it doing
   * at the moment BGACK asserts. */
  ap_m68030_arb_set_acknowledge(&arb, true);
  ap_m68030_arb_set_request(&arb, false);
  tick(&arb, 8);

  TEST_ASSERT_FALSE(ap_m68030_arb_bus_grant(&arb));
  /* The device is master, so the processor is off the bus. */
  TEST_ASSERT_TRUE(ap_m68030_arb_three_state(&arb));
  TEST_ASSERT_FALSE(ap_m68030_arb_processor_is_master(&arb));
}

static void test_the_processor_takes_the_bus_back_when_acknowledge_is_negated(
    void) {
  ap_m68030_arb_t arb;
  ap_m68030_arb_init(&arb);

  ap_m68030_arb_set_request(&arb, true);
  (void)clocks_until_grant(&arb, 8u);
  ap_m68030_arb_set_acknowledge(&arb, true);
  ap_m68030_arb_set_request(&arb, false);
  tick(&arb, 8);
  TEST_ASSERT_FALSE(ap_m68030_arb_processor_is_master(&arb));

  /* "Bus mastership terminates at the negation of BGACK." And: "When A is
   * negated, the arbiter returns to the original state, state 0, and negates
   * signal T." */
  ap_m68030_arb_set_acknowledge(&arb, false);
  tick(&arb, 8);

  TEST_ASSERT_EQUAL_UINT(AP_M68030_ARB_STATE_0, arb.state);
  TEST_ASSERT_TRUE(ap_m68030_arb_processor_is_master(&arb));
}

static void test_a_request_withdrawn_unanswered_leaves_the_processor_master(
    void) {
  ap_m68030_arb_t arb;
  ap_m68030_arb_init(&arb);

  /* §7.7.1: "If no acknowledge is received while the BR is active, the
   * processor remains bus master once BR is negated. This prevents unnecessary
   * interference with ordinary processing if the arbitration circuitry
   * inadvertently responds to noise or if an external device determines that it
   * no longer requires use of the bus before it has been granted mastership."
   *
   * The interesting part is that this is not a special case in the machine: the
   * 2->3->4 walk runs with A never asserted, and state 4 with A negated falls
   * straight back to state 0. */
  ap_m68030_arb_set_request(&arb, true);
  (void)clocks_until_grant(&arb, 8u);
  ap_m68030_arb_set_request(&arb, false);
  tick(&arb, 8);

  TEST_ASSERT_EQUAL_UINT(AP_M68030_ARB_STATE_0, arb.state);
  TEST_ASSERT_TRUE(ap_m68030_arb_processor_is_master(&arb));
  TEST_ASSERT_FALSE(ap_m68030_arb_bus_grant(&arb));
}

static void test_acknowledge_alone_takes_the_bus_without_a_request(void) {
  ap_m68030_arb_t arb;
  ap_m68030_arb_init(&arb);

  /* "As shown by the path from state 0 to state 4, BGACK alone can be used to
   * place the processor's external bus buffers in the high-impedance state,
   * providing single-wire arbitration capability." No BR, and no BG ever. */
  ap_m68030_arb_set_acknowledge(&arb, true);
  tick(&arb, 8);

  TEST_ASSERT_EQUAL_UINT(AP_M68030_ARB_STATE_4, arb.state);
  TEST_ASSERT_FALSE(ap_m68030_arb_processor_is_master(&arb));
  TEST_ASSERT_FALSE(ap_m68030_arb_bus_grant(&arb));

  ap_m68030_arb_set_acknowledge(&arb, false);
  tick(&arb, 8);
  TEST_ASSERT_TRUE(ap_m68030_arb_processor_is_master(&arb));
}

static void test_a_second_request_is_granted_before_the_first_master_leaves(
    void) {
  ap_m68030_arb_t arb;
  ap_m68030_arb_init(&arb);

  /* §7.7.3: "If a BR is still pending after the assertion of BGACK, another BG
   * is asserted within a few clocks of the negation of BG ... Note that the
   * processor does not perform any external bus cycles before it reasserts BG
   * in this case."
   *
   * The point of the re-grant is that external arbitration can pick the next
   * master while the current one is still working, so the bus never idles
   * between them. */
  ap_m68030_arb_set_request(&arb, true);
  (void)clocks_until_grant(&arb, 8u);

  /* First device takes the bus; a second device's request is still up. */
  ap_m68030_arb_set_acknowledge(&arb, true);
  tick(&arb, 8);
  TEST_ASSERT_FALSE(ap_m68030_arb_bus_grant(&arb));

  /* BGACK is still asserted -- the first master has not finished. */
  TEST_ASSERT_LESS_OR_EQUAL_UINT(8u, clocks_until_grant(&arb, 8u));
  TEST_ASSERT_TRUE(arb.bgack);
  TEST_ASSERT_FALSE(ap_m68030_arb_processor_is_master(&arb));
}

static void test_a_locked_read_modify_write_refuses_to_grant_the_bus(void) {
  ap_m68030_arb_t arb;
  ap_m68030_arb_init(&arb);

  /* §7.7.4: the indivisible sequence "causes the bus arbitration state machine
   * to ignore bus requests (assertions of BR) that occur after the first read
   * cycle of the read-modify-write sequence by not issuing bus grants". This is
   * what makes CAS and TAS indivisible against a DMA controller. */
  ap_m68030_arb_set_rmc(&arb, AP_M68030_RMC_LOCKED);
  ap_m68030_arb_set_request(&arb, true);
  tick(&arb, 32);

  TEST_ASSERT_EQUAL_UINT(AP_M68030_ARB_STATE_0, arb.state);
  TEST_ASSERT_FALSE(ap_m68030_arb_bus_grant(&arb));
  TEST_ASSERT_TRUE(ap_m68030_arb_processor_is_master(&arb));

  /* "the MC68030 does not assert BG until the entire operation has completed."
   * The request is a level and is still up, so the grant follows at once. */
  ap_m68030_arb_set_rmc(&arb, AP_M68030_RMC_NONE);
  TEST_ASSERT_LESS_OR_EQUAL_UINT(8u, clocks_until_grant(&arb, 8u));
}

static void test_a_request_during_the_first_read_of_a_lock_grants_on_release(
    void) {
  ap_m68030_arb_t arb;
  ap_m68030_arb_init(&arb);

  /* The half of a locked sequence the state machine does *not* ignore: the
   * inhibit covers requests "that occur after the first read cycle". A request
   * during the first read walks the machine as usual -- only Figure 7-61's note
   * holds the pin down, "The BG output will not be asserted while RMC is
   * asserted". */
  ap_m68030_arb_set_rmc(&arb, AP_M68030_RMC_FIRST_READ);
  ap_m68030_arb_set_request(&arb, true);
  tick(&arb, 8);

  TEST_ASSERT_FALSE(ap_m68030_arb_bus_grant(&arb));
  /* But the machine did move, which is what distinguishes this from a lock. */
  TEST_ASSERT_EQUAL_UINT(AP_M68030_ARB_STATE_2, arb.state);

  /* So the grant needs no further synchronisation once RMC negates. */
  ap_m68030_arb_set_rmc(&arb, AP_M68030_RMC_NONE);
  TEST_ASSERT_TRUE(ap_m68030_arb_bus_grant(&arb));
}

static void test_a_grant_waits_for_a_committed_bus_cycle_to_begin(void) {
  ap_m68030_arb_t arb;
  ap_m68030_arb_init(&arb);

  /* §7.7.2: "BG is asserted in response to BR; it is usually asserted as soon
   * as BR has been synchronized and recognized, except when the MC68030 has
   * made an internal decision to execute a bus cycle. Then, the assertion of BG
   * is deferred until the bus cycle has begun." */
  ap_m68030_arb_set_cycle_committed(&arb, true);
  ap_m68030_arb_set_request(&arb, true);
  tick(&arb, 32);

  TEST_ASSERT_FALSE(ap_m68030_arb_bus_grant(&arb));
  TEST_ASSERT_TRUE(ap_m68030_arb_processor_is_master(&arb));

  /* Deferred, not ignored: the held request is granted once the cycle starts. */
  ap_m68030_arb_set_cycle_committed(&arb, false);
  TEST_ASSERT_LESS_OR_EQUAL_UINT(8u, clocks_until_grant(&arb, 8u));
}

static void test_the_processor_stops_driving_the_bus_when_it_grants_it(void) {
  ap_m68030_arb_t arb;
  ap_m68030_arb_init(&arb);

  /* T asserts in state 1, alongside G -- "both grant G and signal T are
   * asserted". So the processor is off the bus from the grant, not from the
   * acknowledgement. Getting this backwards would let the CPU run cycles in the
   * window where the board thinks the buffers are already high-impedance. */
  ap_m68030_arb_set_request(&arb, true);
  (void)clocks_until_grant(&arb, 8u);

  TEST_ASSERT_EQUAL_UINT(AP_M68030_ARB_STATE_1, arb.state);
  TEST_ASSERT_TRUE(ap_m68030_arb_three_state(&arb));
  TEST_ASSERT_FALSE(ap_m68030_arb_processor_is_master(&arb));
}

static void test_the_bus_is_never_granted_and_three_stated_to_no_one(void) {
  ap_m68030_arb_t arb;
  ap_m68030_arb_init(&arb);

  /* An invariant across every reachable state rather than a quoted line: G is
   * asserted only in states 1 and 2, and T in 1 through 4, so G implies T. A
   * grant while the processor still drives the bus would be two masters. Driven
   * over an exhaustive walk of the four input combinations. */
  for (unsigned inputs = 0; inputs < 4; inputs++) {
    ap_m68030_arb_set_request(&arb, (inputs & 1u) != 0u);
    ap_m68030_arb_set_acknowledge(&arb, (inputs & 2u) != 0u);
    for (unsigned i = 0; i < 16; i++) {
      ap_m68030_arb_tick(&arb);
      if (ap_m68030_arb_bus_grant(&arb)) {
        TEST_ASSERT_TRUE(ap_m68030_arb_three_state(&arb));
        TEST_ASSERT_FALSE(ap_m68030_arb_processor_is_master(&arb));
      }
    }
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_processor_is_bus_master_out_of_reset);
  RUN_TEST(test_an_idle_bus_never_leaves_the_processor_as_master);
  RUN_TEST(test_a_bus_request_is_granted_the_bus);
  RUN_TEST(test_grant_latency_is_within_the_published_envelope);
  RUN_TEST(test_a_request_is_not_granted_in_the_clock_it_is_asserted);
  RUN_TEST(test_a_grant_is_held_until_the_request_is_answered);
  RUN_TEST(test_acknowledging_a_grant_negates_it);
  RUN_TEST(test_the_processor_takes_the_bus_back_when_acknowledge_is_negated);
  RUN_TEST(test_a_request_withdrawn_unanswered_leaves_the_processor_master);
  RUN_TEST(test_acknowledge_alone_takes_the_bus_without_a_request);
  RUN_TEST(test_a_second_request_is_granted_before_the_first_master_leaves);
  RUN_TEST(test_a_locked_read_modify_write_refuses_to_grant_the_bus);
  RUN_TEST(test_a_request_during_the_first_read_of_a_lock_grants_on_release);
  RUN_TEST(test_a_grant_waits_for_a_committed_bus_cycle_to_begin);
  RUN_TEST(test_the_processor_stops_driving_the_bus_when_it_grants_it);
  RUN_TEST(test_the_bus_is_never_granted_and_three_stated_to_no_one);
  return UNITY_END();
}
