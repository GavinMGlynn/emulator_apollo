/* The multi-node ring scheduler. The plan's verification is a whole-ring state
 * hash reproducible across runs and across build types; these tests check the
 * scheduling rule that makes that possible, and the hash itself is compared
 * across two independently built rings running the same workload. */

#include "ring/ap_ring_sched.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* A synthetic node: records when it was stepped, and how often. Deliberately
 * not a machine -- the scheduler must work for anything with a period. */
typedef struct {
  unsigned steps;
  ap_time_t last;
  ap_time_t first;
  unsigned slot;
  unsigned order[64];
  unsigned *shared_counter;
} probe_t;

static void probe_step(void *context, ap_time_t now) {
  probe_t *p = (probe_t *)context;
  if (p->steps == 0u) {
    p->first = now;
  }
  if (p->shared_counter != NULL && p->steps < 64u) {
    p->order[p->steps] = (*p->shared_counter)++;
  }
  p->steps++;
  p->last = now;
}

/* Every node advances only on its own cycle boundaries: a 12 MHz node steps
 * twice as often as a 6 MHz one over the same interval, and both land exactly
 * on multiples of their period. */
static void test_each_node_steps_only_on_its_own_cycle_boundaries(void) {
  ap_ring_sched_t s;
  ap_ring_sched_init(&s);
  probe_t fast = {0};
  probe_t slow = {0};
  TEST_ASSERT_EQUAL_INT(0, ap_ring_sched_add(&s, 12000000u, probe_step, &fast));
  TEST_ASSERT_EQUAL_INT(1, ap_ring_sched_add(&s, 6000000u, probe_step, &slow));

  /* One microsecond of emulated time. */
  const ap_time_t micro = AP_TIME_BASE_HZ / 1000000u;
  ap_ring_sched_run_until(&s, micro);

  TEST_ASSERT_EQUAL_UINT(12u, fast.steps);
  TEST_ASSERT_EQUAL_UINT(6u, slow.steps);
  /* And on exact boundaries, not on the ring's clock or anyone else's. */
  TEST_ASSERT_EQUAL_UINT64(0u, fast.last % (AP_TIME_BASE_HZ / 12000000u));
  TEST_ASSERT_EQUAL_UINT64(0u, slow.last % (AP_TIME_BASE_HZ / 6000000u));
}

/* `CLAUDE.md`: `ap_clock_init` "rejects an unrepresentable frequency rather
 * than rounding it". The same rule applies to a ring node, and for a sharper
 * reason -- a rounded period would drift against every other node on the ring
 * by an amount no probe could attribute to anything. */
static void test_a_frequency_the_base_cannot_represent_is_refused(void) {
  ap_ring_sched_t s;
  ap_ring_sched_init(&s);
  probe_t p = {0};
  /* 336,600,000,000 is not divisible by 7,000,001. */
  TEST_ASSERT_EQUAL_INT(-1, ap_ring_sched_add(&s, 7000001u, probe_step, &p));
  /* And the slot was not consumed by the refusal. */
  TEST_ASSERT_EQUAL_INT(0, ap_ring_sched_add(&s, 12000000u, probe_step, &p));
}

/* The ring's own bit clock advances the medium, at the rate `[MAC]` §3.2
 * gives. Over a microsecond a 12 Mbit/s ring carries twelve bit times. */
static void test_the_ring_advances_on_its_own_bit_clock(void) {
  ap_ring_sched_t s;
  ap_ring_sched_init(&s);
  const ap_time_t micro = AP_TIME_BASE_HZ / 1000000u;
  ap_ring_sched_run_until(&s, micro);
  TEST_ASSERT_EQUAL_UINT64(12u, s.medium.bit_time);
}

/* Ties break by slot, always, so an interleaving cannot depend on anything but
 * the ring's own shape. Two nodes at the same frequency fall due together on
 * every boundary; the lower slot must go first every time. */
static void test_simultaneous_nodes_step_in_slot_order_every_time(void) {
  ap_ring_sched_t s;
  ap_ring_sched_init(&s);
  unsigned counter = 0u;
  probe_t a = {.shared_counter = &counter};
  probe_t b = {.shared_counter = &counter};
  TEST_ASSERT_EQUAL_INT(0, ap_ring_sched_add(&s, 1000000u, probe_step, &a));
  TEST_ASSERT_EQUAL_INT(1, ap_ring_sched_add(&s, 1000000u, probe_step, &b));

  ap_ring_sched_run_until(&s, AP_TIME_BASE_HZ / 100000u); /* 10 us */

  TEST_ASSERT_EQUAL_UINT(10u, a.steps);
  TEST_ASSERT_EQUAL_UINT(10u, b.steps);
  for (unsigned i = 0; i < 10u; i++) {
    /* a's nth step immediately precedes b's nth, on every boundary. */
    TEST_ASSERT_EQUAL_UINT(a.order[i] + 1u, b.order[i]);
  }
}

/* The determinism the plan asks for, stated as the test it asks for: the same
 * workload on two independently constructed rings gives the same hash. */
static void test_the_same_workload_twice_gives_the_same_ring_hash(void) {
  ap_ring_sched_t x;
  ap_ring_sched_t y;
  probe_t px[3] = {{0}, {0}, {0}};
  probe_t py[3] = {{0}, {0}, {0}};
  static const uint32_t hz[3] = {12000000u, 25000000u, 20000000u};

  ap_ring_sched_init(&x);
  ap_ring_sched_init(&y);
  for (unsigned i = 0; i < 3u; i++) {
    TEST_ASSERT_TRUE(ap_ring_sched_add(&x, hz[i], probe_step, &px[i]) >= 0);
    TEST_ASSERT_TRUE(ap_ring_sched_add(&y, hz[i], probe_step, &py[i]) >= 0);
  }
  TEST_ASSERT_EQUAL_HEX64(ap_ring_sched_hash(&x), ap_ring_sched_hash(&y));

  const ap_time_t micro = AP_TIME_BASE_HZ / 1000000u;
  /* Reached differently on purpose: one run in a single call, the other in
   * ten. The schedule must not depend on how the caller chopped up time. */
  ap_ring_sched_run_until(&x, 10u * micro);
  for (unsigned i = 0; i < 10u; i++) {
    ap_ring_sched_run_until(&y, (ap_time_t)(i + 1u) * micro);
  }

  TEST_ASSERT_EQUAL_HEX64(ap_ring_sched_hash(&x), ap_ring_sched_hash(&y));
  for (unsigned i = 0; i < 3u; i++) {
    TEST_ASSERT_EQUAL_UINT(px[i].steps, py[i].steps);
  }

  /* And **across build types**, which the plan asks for and which comparing
   * two rings inside one binary cannot show. Pinned as a constant: this suite
   * runs under `-O0` in the debug preset and `-O3` with LTO in the release
   * one, so the two agreeing on a literal is the check. It was measured at
   * both, not copied from one.
   *
   * A failure here means the schedule has picked up something the compiler is
   * free to vary -- a float, an uninitialised field reaching the hash, or a
   * traversal whose order depends on layout. That is worth failing loudly for,
   * since every whole-ring golden downstream would inherit it. */
  TEST_ASSERT_EQUAL_HEX64(0x9D0B2A0A2D558C97u, ap_ring_sched_hash(&x));
}

/* And the hash covers *phase*, not merely elapsed time: two rings at the same
 * instant with their nodes at different points in their cycles must differ. */
static void test_the_hash_separates_rings_that_differ_only_in_phase(void) {
  ap_ring_sched_t x;
  ap_ring_sched_t y;
  probe_t px = {0};
  probe_t py = {0};

  ap_ring_sched_init(&x);
  ap_ring_sched_init(&y);
  TEST_ASSERT_TRUE(ap_ring_sched_add(&x, 12000000u, probe_step, &px) >= 0);
  TEST_ASSERT_TRUE(ap_ring_sched_add(&y, 6000000u, probe_step, &py) >= 0);

  const ap_time_t micro = AP_TIME_BASE_HZ / 1000000u;
  ap_ring_sched_run_until(&x, micro);
  ap_ring_sched_run_until(&y, micro);

  /* Same instant, same bit time, different node phase. */
  TEST_ASSERT_EQUAL_UINT64(x.now, y.now);
  TEST_ASSERT_EQUAL_UINT64(x.medium.bit_time, y.medium.bit_time);
  TEST_ASSERT_TRUE(ap_ring_sched_hash(&x) != ap_ring_sched_hash(&y));
}

/* A ring of nodes at genuinely different clocks is the case the whole time
 * base exists for: `CLAUDE.md` says "no CPU's cycle is a legal unit of
 * account". Each still lands on its own boundaries. */
static void test_nodes_at_different_clocks_share_one_ring(void) {
  ap_ring_sched_t s;
  ap_ring_sched_init(&s);
  probe_t dn3000 = {0};
  probe_t dn3500 = {0};
  probe_t dn5500 = {0};
  TEST_ASSERT_TRUE(ap_ring_sched_add(&s, 12000000u, probe_step, &dn3000) >= 0);
  TEST_ASSERT_TRUE(ap_ring_sched_add(&s, 25000000u, probe_step, &dn3500) >= 0);
  TEST_ASSERT_TRUE(ap_ring_sched_add(&s, 25000000u, probe_step, &dn5500) >= 0);

  ap_ring_sched_run_until(&s, AP_TIME_BASE_HZ / 1000u); /* 1 ms */

  TEST_ASSERT_EQUAL_UINT(12000u, dn3000.steps);
  TEST_ASSERT_EQUAL_UINT(25000u, dn3500.steps);
  TEST_ASSERT_EQUAL_UINT(25000u, dn5500.steps);
  /* And the ring carried 12,000 bit times in that millisecond. */
  TEST_ASSERT_EQUAL_UINT64(12000u, s.medium.bit_time);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_each_node_steps_only_on_its_own_cycle_boundaries);
  RUN_TEST(test_a_frequency_the_base_cannot_represent_is_refused);
  RUN_TEST(test_the_ring_advances_on_its_own_bit_clock);
  RUN_TEST(test_simultaneous_nodes_step_in_slot_order_every_time);
  RUN_TEST(test_the_same_workload_twice_gives_the_same_ring_hash);
  RUN_TEST(test_the_hash_separates_rings_that_differ_only_in_phase);
  RUN_TEST(test_nodes_at_different_clocks_share_one_ring);
  return UNITY_END();
}
