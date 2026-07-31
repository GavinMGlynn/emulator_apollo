/* The shared arbitration point. `[030]` §7.7 for the protocol, `008778-03`
 * §2.4.6 for the priority order. */

#include "unity.h"

#include "board/ap_arbiter.h"

void setUp(void) {}
void tearDown(void) {}

static void tick(ap_arbiter_t *a, unsigned n) {
  for (unsigned i = 0; i < n; i++) {
    ap_arbiter_tick(a);
  }
}

/* Run until the processor loses the bus, answering how many clocks it took, or
 * limit + 1 if it never did. */
static unsigned clocks_until_stalled(ap_arbiter_t *a, unsigned limit) {
  for (unsigned i = 1; i <= limit; i++) {
    ap_arbiter_tick(a);
    if (!ap_arbiter_processor_may_run(a)) {
      return i;
    }
  }
  return limit + 1u;
}

static void test_an_idle_bus_belongs_to_the_processor(void) {
  ap_arbiter_t a;
  ap_arbiter_reset(&a);

  tick(&a, 32);
  TEST_ASSERT_EQUAL_INT(AP_ARBITER_PROCESSOR, ap_arbiter_master(&a));
  TEST_ASSERT_TRUE(ap_arbiter_processor_may_run(&a));
}

static void test_a_device_takes_the_bus_from_the_processor(void) {
  ap_arbiter_t a;
  ap_arbiter_reset(&a);

  /* The inversion the whole design rests on: the processor is the lowest
   * priority claimant, so a device asking simply wins. */
  ap_arbiter_request(&a, 2, true);
  TEST_ASSERT_LESS_OR_EQUAL_UINT(16u, clocks_until_stalled(&a, 16u));

  tick(&a, 8);
  TEST_ASSERT_EQUAL_INT(2, ap_arbiter_master(&a));
  TEST_ASSERT_FALSE(ap_arbiter_processor_may_run(&a));
}

static void test_the_processor_gets_the_bus_back(void) {
  ap_arbiter_t a;
  ap_arbiter_reset(&a);
  ap_arbiter_request(&a, 2, true);
  (void)clocks_until_stalled(&a, 16u);
  tick(&a, 8);
  TEST_ASSERT_FALSE(ap_arbiter_processor_may_run(&a));

  /* "Bus mastership terminates at the negation of BGACK." */
  ap_arbiter_request(&a, 2, false);
  tick(&a, 16);
  TEST_ASSERT_EQUAL_INT(AP_ARBITER_PROCESSOR, ap_arbiter_master(&a));
  TEST_ASSERT_TRUE(ap_arbiter_processor_may_run(&a));
}

static void test_the_lowest_numbered_request_line_wins(void) {
  ap_arbiter_t a;
  ap_arbiter_reset(&a);

  /* `008778-03` §2.4.6: "They are prioritized, with DRQO having the highest
   * priority and DRQ7 having the lowest priority." */
  ap_arbiter_request(&a, 6, true);
  ap_arbiter_request(&a, 1, true);
  TEST_ASSERT_EQUAL_INT(1, ap_arbiter_highest_requester(&a));

  (void)clocks_until_stalled(&a, 16u);
  tick(&a, 8);
  TEST_ASSERT_EQUAL_INT(1, ap_arbiter_master(&a));
}

static void test_a_higher_request_does_not_preempt_a_master_mid_transfer(void) {
  ap_arbiter_t a;
  ap_arbiter_reset(&a);
  ap_arbiter_request(&a, 5, true);
  (void)clocks_until_stalled(&a, 16u);
  tick(&a, 8);
  TEST_ASSERT_EQUAL_INT(5, ap_arbiter_master(&a));

  /* "BGACK should not be negated until all bus cycles required by the alternate
   * bus master are completed." Priority decides who is granted next, not who is
   * thrown off -- a pre-empting arbiter would tear a transfer in half. */
  ap_arbiter_request(&a, 0, true);
  tick(&a, 16);
  TEST_ASSERT_EQUAL_INT(5, ap_arbiter_master(&a));

  /* And when the first finishes, the waiting higher one takes over. */
  ap_arbiter_request(&a, 5, false);
  tick(&a, 16);
  TEST_ASSERT_EQUAL_INT(0, ap_arbiter_master(&a));
}

static void test_the_processor_cannot_run_between_grant_and_acknowledgement(
    void) {
  ap_arbiter_t a;
  ap_arbiter_reset(&a);
  ap_arbiter_request(&a, 3, true);

  /* §7.7.4: T asserts in state 1, alongside the grant -- so the processor stops
   * driving the bus when it *grants*, not when the grant is taken up. Checking
   * only for a device holding mastership would let the processor run inside the
   * window §7.7.3 exists to describe. */
  unsigned stalled_at = clocks_until_stalled(&a, 16u);
  TEST_ASSERT_LESS_OR_EQUAL_UINT(16u, stalled_at);
  /* Stalled before anyone is master. */
  TEST_ASSERT_EQUAL_INT(AP_ARBITER_PROCESSOR, ap_arbiter_master(&a));
}

static void test_contention_costs_the_processor_measurable_clocks(void) {
  ap_arbiter_t a;
  ap_arbiter_reset(&a);

  /* The verification Phase 3 asks for: contention measured rather than
   * modelled. Nothing here adds a penalty -- the figure below is just how many
   * of a hundred clocks the processor was not the bus master, and it comes out
   * of the protocol.
   *
   * A device holds DRQ0 for the middle stretch of the run. */
  unsigned processor_clocks = 0;
  for (unsigned clock = 0; clock < 100u; clock++) {
    if (clock == 20u) {
      ap_arbiter_request(&a, 0, true);
    }
    if (clock == 70u) {
      ap_arbiter_request(&a, 0, false);
    }
    ap_arbiter_tick(&a);
    if (ap_arbiter_processor_may_run(&a)) {
      processor_clocks++;
    }
  }

  /* It ran for some of the hundred and not all of them, and the loss brackets
   * the request. Asserted as a range rather than a number because the exact
   * figure is a consequence of the synchroniser depth, which is itself
   * PROVISIONAL -- pinning it exactly here would freeze a value the arbitration
   * unit's own header says is not measured. */
  TEST_ASSERT_GREATER_THAN_UINT(20u, processor_clocks);
  TEST_ASSERT_LESS_THAN_UINT(60u, processor_clocks);
}

static void test_a_request_withdrawn_before_the_grant_costs_nothing(void) {
  ap_arbiter_t a;
  ap_arbiter_reset(&a);

  /* §7.7.1: "If no acknowledge is received while the BR is active, the
   * processor remains bus master once BR is negated. This prevents unnecessary
   * interference with ordinary processing if the arbitration circuitry
   * inadvertently responds to noise." */
  ap_arbiter_request(&a, 4, true);
  ap_arbiter_tick(&a);
  ap_arbiter_request(&a, 4, false);
  tick(&a, 32);

  TEST_ASSERT_EQUAL_INT(AP_ARBITER_PROCESSOR, ap_arbiter_master(&a));
  TEST_ASSERT_TRUE(ap_arbiter_processor_may_run(&a));
}

static void test_two_arbiters_driven_alike_agree(void) {
  ap_arbiter_t a;
  ap_arbiter_t b;
  ap_arbiter_reset(&a);
  ap_arbiter_reset(&b);

  for (unsigned clock = 0; clock < 64u; clock++) {
    bool on = (clock % 7u) < 3u;
    ap_arbiter_request(&a, clock % AP_ARBITER_REQUESTERS, on);
    ap_arbiter_request(&b, clock % AP_ARBITER_REQUESTERS, on);
    ap_arbiter_tick(&a);
    ap_arbiter_tick(&b);
    TEST_ASSERT_EQUAL_INT(ap_arbiter_master(&a), ap_arbiter_master(&b));
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_an_idle_bus_belongs_to_the_processor);
  RUN_TEST(test_a_device_takes_the_bus_from_the_processor);
  RUN_TEST(test_the_processor_gets_the_bus_back);
  RUN_TEST(test_the_lowest_numbered_request_line_wins);
  RUN_TEST(test_a_higher_request_does_not_preempt_a_master_mid_transfer);
  RUN_TEST(test_the_processor_cannot_run_between_grant_and_acknowledgement);
  RUN_TEST(test_contention_costs_the_processor_measurable_clocks);
  RUN_TEST(test_a_request_withdrawn_before_the_grant_costs_nothing);
  RUN_TEST(test_two_arbiters_driven_alike_agree);
  return UNITY_END();
}
