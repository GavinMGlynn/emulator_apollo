/* MC68030 bus cycle timing.
 *
 * Every number asserted here is cited to the MC68030 User's Manual 3ed, and the
 * test names state the hardware fact rather than the function called. These are
 * the clock counts the whole CPU phase rests on: if a minimum asynchronous read
 * is not three clocks, no instruction timing above it can be right.
 */

#include "cpu/m68030/ap_m68030_bus.h"
#include "time/ap_time.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Run a cycle to completion, answering with `term` once `answer_after` clocks
 * have elapsed. Returns the total clocks the cycle took. `answer_after` of 0
 * means the device is ready immediately. */
static uint32_t run_cycle(ap_m68030_bus_t *bus, ap_m68030_term_t term,
                          uint32_t answer_after) {
  uint32_t clocks = 0;
  while (ap_m68030_bus_active(bus)) {
    if (clocks == answer_after) {
      ap_m68030_bus_terminate(bus, term);
    }
    (void)ap_m68030_bus_tick(bus);
    clocks++;
    TEST_ASSERT_LESS_THAN_UINT32(64, clocks); /* runaway guard */
  }
  return clocks;
}

static void begin_read(ap_m68030_bus_t *bus) {
  ap_m68030_bus_begin(bus, 0x00010000u, 5, AP_M68030_SIZE_LONG, true, true);
}

/* [030] 7.3.1: the asynchronous read cycle runs S0..S5, six half-clock states,
 * so three clocks when the device answers without wait states. */
static void test_a_minimum_asynchronous_read_takes_three_clocks(void) {
  ap_m68030_bus_t bus;
  begin_read(&bus);
  TEST_ASSERT_EQUAL_UINT32(3, run_cycle(&bus, AP_M68030_TERM_DSACK, 0));
}

/* [030] 7.3.4 p.7-48: STERM "provides a two-clock (minimum) bus cycle for
 * 32-bit ports". */
static void test_a_synchronous_read_terminated_by_sterm_takes_two_clocks(void) {
  ap_m68030_bus_t bus;
  begin_read(&bus);
  TEST_ASSERT_EQUAL_UINT32(2, run_cycle(&bus, AP_M68030_TERM_STERM, 0));
}

/* [030] 7.3.4 p.7-48: "a synchronous cycle terminated with STERM with one wait
 * cycle is a three-clock bus cycle". */
static void test_a_synchronous_cycle_with_one_wait_state_takes_three_clocks(void) {
  ap_m68030_bus_t bus;
  begin_read(&bus);
  TEST_ASSERT_EQUAL_UINT32(3, run_cycle(&bus, AP_M68030_TERM_STERM, 2));
  TEST_ASSERT_EQUAL_UINT32(1, bus.wait_states);
}

/* [030] 7.3.1 S3: "If DSACKx is not recognized by the start of state 3 (S3),
 * the processor inserts wait states instead of proceeding to states 4 and 5."
 * A wait state is a whole clock, so each one adds exactly one. */
static void test_each_wait_state_adds_exactly_one_clock_to_an_asynchronous_read(void) {
  for (uint32_t waits = 0; waits <= 4; waits++) {
    ap_m68030_bus_t bus;
    begin_read(&bus);
    /* Answering at clock 1 is zero-wait; each later clock is one more wait. */
    uint32_t clocks = run_cycle(&bus, AP_M68030_TERM_DSACK, 1 + waits);
    TEST_ASSERT_EQUAL_UINT32(3 + waits, clocks);
    TEST_ASSERT_EQUAL_UINT32(waits, bus.wait_states);
  }
}

/* [030] 7.3.1 S0/S1: ECS marks the start of an external cycle and is asserted
 * for one-half clock, being negated during S1. Since a tick is a whole clock
 * ending in S1, ECS must already be low when the first tick returns. */
static void test_ecs_is_negated_by_the_end_of_the_first_clock(void) {
  ap_m68030_bus_t bus;
  begin_read(&bus);
  (void)ap_m68030_bus_tick(&bus);
  TEST_ASSERT_FALSE(bus.ecs);
  TEST_ASSERT_FALSE(bus.ocs);
}

/* [030] 7.3.1 S1: AS is asserted in S1, which is inside the first clock. */
static void test_address_strobe_is_asserted_by_the_end_of_the_first_clock(void) {
  ap_m68030_bus_t bus;
  begin_read(&bus);
  (void)ap_m68030_bus_tick(&bus);
  TEST_ASSERT_TRUE(bus.as);
}

/* [030] 7.3.1 S2: DBEN is asserted in S2, which is in the second clock -- so it
 * is still low at the end of the first. */
static void test_data_buffer_enable_follows_address_strobe_by_one_clock(void) {
  ap_m68030_bus_t bus;
  begin_read(&bus);
  (void)ap_m68030_bus_tick(&bus);
  TEST_ASSERT_FALSE(bus.dben);
  ap_m68030_bus_terminate(&bus, AP_M68030_TERM_DSACK);
  (void)ap_m68030_bus_tick(&bus);
  TEST_ASSERT_TRUE(bus.dben);
}

/* [030] 7.3.1 S5: "The processor negates AS, DS, and DBEN during state 5". */
static void test_the_strobes_are_all_negated_when_a_read_cycle_completes(void) {
  ap_m68030_bus_t bus;
  begin_read(&bus);
  (void)run_cycle(&bus, AP_M68030_TERM_DSACK, 0);
  TEST_ASSERT_FALSE(bus.as);
  TEST_ASSERT_FALSE(bus.ds);
  TEST_ASSERT_FALSE(bus.dben);
  TEST_ASSERT_TRUE(bus.complete);
  TEST_ASSERT_FALSE(ap_m68030_bus_active(&bus));
}

static void begin_write(ap_m68030_bus_t *bus) {
  ap_m68030_bus_begin(bus, 0x00010000u, 5, AP_M68030_SIZE_LONG, false, true);
}

/* A write is the same *length* as a read -- the state sequence and termination
 * rules are identical ([030] 7.3.2), so only the strobe timing differs. */
static void test_a_minimum_asynchronous_write_also_takes_three_clocks(void) {
  ap_m68030_bus_t bus;
  begin_write(&bus);
  TEST_ASSERT_EQUAL_UINT32(3, run_cycle(&bus, AP_M68030_TERM_DSACK, 0));
}

/* [030] 7.3.2 S1: on a write "the processor asserts AS ... The processor also
 * asserts DBEN during S1", where a read does not assert DBEN until S2. */
static void test_a_write_asserts_data_buffer_enable_a_clock_earlier_than_a_read(void) {
  ap_m68030_bus_t write;
  begin_write(&write);
  (void)ap_m68030_bus_tick(&write);
  TEST_ASSERT_TRUE(write.dben);

  ap_m68030_bus_t read;
  begin_read(&read);
  (void)ap_m68030_bus_tick(&read);
  TEST_ASSERT_FALSE(read.dben);
}

/* [030] 7.3.2 S3: on a write "the processor asserts DS during S3, indicating
 * that the data is stable on the data bus" -- two states later than a read,
 * which asserts DS in S1. Asserting DS early on a write would tell a device its
 * data was stable before it had been driven. */
static void test_a_write_asserts_data_strobe_two_states_later_than_a_read(void) {
  ap_m68030_bus_t write;
  begin_write(&write);
  (void)ap_m68030_bus_tick(&write); /* ends in S1 */
  TEST_ASSERT_FALSE(write.ds);
  ap_m68030_bus_terminate(&write, AP_M68030_TERM_DSACK);
  (void)ap_m68030_bus_tick(&write); /* ends in S3 */
  TEST_ASSERT_TRUE(write.ds);

  ap_m68030_bus_t read;
  begin_read(&read);
  (void)ap_m68030_bus_tick(&read);
  TEST_ASSERT_TRUE(read.ds);
}

/* [030] 7.3.2 S5: a write negates AS and DS, but "R/W, SIZ0-SIZ1, FC0-FC2, and
 * DBEN also remain valid throughout S5" -- so unlike a read, DBEN is still
 * asserted as the cycle ends. */
static void test_a_write_holds_data_buffer_enable_through_the_final_state(void) {
  ap_m68030_bus_t bus;
  begin_write(&bus);
  (void)run_cycle(&bus, AP_M68030_TERM_DSACK, 0);
  TEST_ASSERT_FALSE(bus.as);
  TEST_ASSERT_FALSE(bus.ds);
  TEST_ASSERT_TRUE(bus.dben);
}

/* Beginning a new cycle clears the previous one's held signals, so a write's
 * DBEN cannot leak into the cycle that follows it. */
static void test_a_new_cycle_clears_the_signals_held_by_the_previous_one(void) {
  ap_m68030_bus_t bus;
  begin_write(&bus);
  (void)run_cycle(&bus, AP_M68030_TERM_DSACK, 0);
  TEST_ASSERT_TRUE(bus.dben);
  begin_read(&bus);
  TEST_ASSERT_FALSE(bus.dben);
  TEST_ASSERT_FALSE(bus.as);
}

/* A bus error ends the cycle rather than transferring, and does not silently
 * become a normal completion. */
static void test_a_bus_error_ends_the_cycle_without_a_data_transfer(void) {
  ap_m68030_bus_t bus;
  begin_read(&bus);
  (void)run_cycle(&bus, AP_M68030_TERM_BERR, 0);
  TEST_ASSERT_EQUAL(AP_M68030_TERM_BERR, bus.termination);
  TEST_ASSERT_FALSE(ap_m68030_bus_active(&bus));
}

/* OCS accompanies ECS only on the first external cycle of an operand operation
 * ([030] 7.3.1 S0), so a cycle that is not one must never assert it. */
static void test_operand_cycle_start_is_not_asserted_on_a_continuation_cycle(void) {
  ap_m68030_bus_t bus;
  ap_m68030_bus_begin(&bus, 0x00010004u, 5, AP_M68030_SIZE_LONG, true, false);
  TEST_ASSERT_FALSE(bus.ocs);
}

/* The bus is modelled in half-clock states, so every CPU clock in this machine
 * must have an even period in AP_TIME_BASE_HZ units for a half-clock to be
 * exactly representable. This is asserted rather than assumed: adding a CPU
 * whose period is odd would make the state model silently lossy. */
static void test_every_cpu_clock_has_an_exactly_representable_half_clock(void) {
  static const uint32_t cpu_hz[] = {12000000u, 20000000u, 25000000u, 33000000u};
  for (size_t i = 0; i < sizeof cpu_hz / sizeof cpu_hz[0]; i++) {
    ap_clock_t clk;
    TEST_ASSERT_TRUE(ap_clock_init(&clk, cpu_hz[i]));
    TEST_ASSERT_EQUAL_UINT64(0, clk.period % 2u);
  }
}

/* A cycle that is never answered must not quietly complete. */
static void test_a_cycle_with_no_termination_never_completes(void) {
  ap_m68030_bus_t bus;
  begin_read(&bus);
  for (int i = 0; i < 32; i++) {
    (void)ap_m68030_bus_tick(&bus);
  }
  TEST_ASSERT_TRUE(ap_m68030_bus_active(&bus));
  TEST_ASSERT_FALSE(bus.complete);
}


/* ---------------------------------------------------------------------------
 * Burst operation cycles, [030] 7.3.7. A burst is what makes modelling the
 * caches worth anything for timing: it is the difference between filling a line
 * in five clocks and in eight.
 * ------------------------------------------------------------------------- */

/* Run a burst to completion with the device always ready. Returns total clocks. */
static uint32_t run_burst(ap_m68030_bus_t *bus) {
  uint32_t clocks = 0;
  while (ap_m68030_bus_active(bus)) {
    ap_m68030_bus_terminate(bus, AP_M68030_TERM_STERM);
    (void)ap_m68030_bus_tick(bus);
    clocks++;
    TEST_ASSERT_LESS_THAN_UINT32(64, clocks);
  }
  return clocks;
}

/* The headline number, and the reason the caches are modelled at all. The first
 * long word is an ordinary two-clock synchronous read; then "the processor
 * continues to accept data on every clock during which STERM is asserted", so
 * the remaining three cost one clock each. Five against the eight that four
 * separate two-clock reads would cost -- counted, not asserted. */
static void test_a_full_burst_line_fill_takes_five_clocks(void) {
  ap_m68030_bus_t bus;
  begin_read(&bus);
  ap_m68030_bus_request_burst(&bus);
  ap_m68030_bus_acknowledge_burst(&bus, true);

  TEST_ASSERT_EQUAL_UINT32(5, run_burst(&bus));
  TEST_ASSERT_EQUAL_UINT(AP_M68030_BURST_BEATS, bus.burst_beats);
}

/* The comparison the five-clock figure is only meaningful against: the same
 * four long words fetched as four separate synchronous cycles. */
static void test_four_separate_synchronous_reads_take_eight_clocks(void) {
  uint32_t total = 0;
  for (unsigned i = 0; i < AP_M68030_BURST_BEATS; i++) {
    ap_m68030_bus_t bus;
    ap_m68030_bus_begin(&bus, 0x00010000u + (i * 4u), 5, AP_M68030_SIZE_LONG,
                        true, i == 0);
    total += run_cycle(&bus, AP_M68030_TERM_STERM, 0);
  }
  TEST_ASSERT_EQUAL_UINT32(8, total);
}

/* "burst mode is only initiated if both of these signals are asserted for a
 * synchronous cycle." Each of the three requirements missing on its own leaves
 * an ordinary two-clock cycle rather than a burst. */
static void test_a_burst_needs_cbreq_cback_and_sterm_together(void) {
  ap_m68030_bus_t bus;

  /* CBACK withheld: "CBACK ... can be asserted independently of the CBREQ
   * signal", and without it the request goes unanswered. */
  begin_read(&bus);
  ap_m68030_bus_request_burst(&bus);
  ap_m68030_bus_acknowledge_burst(&bus, false);
  TEST_ASSERT_EQUAL_UINT32(2, run_burst(&bus));
  TEST_ASSERT_FALSE(bus.bursting);

  /* CBREQ never asserted: a volunteering device changes nothing. */
  begin_read(&bus);
  ap_m68030_bus_acknowledge_burst(&bus, true);
  TEST_ASSERT_EQUAL_UINT32(2, run_burst(&bus));
  TEST_ASSERT_FALSE(bus.bursting);

  /* DSACK rather than STERM: burst runs "only from 32-bit ports that terminate
   * bus cycles with STERM", so an asynchronous port gets its ordinary
   * three-clock cycle and no burst. */
  begin_read(&bus);
  ap_m68030_bus_request_burst(&bus);
  ap_m68030_bus_acknowledge_burst(&bus, true);
  TEST_ASSERT_EQUAL_UINT32(3, run_cycle(&bus, AP_M68030_TERM_DSACK, 0));
  TEST_ASSERT_FALSE(bus.bursting);
}

/* "CBREQ is negated after STERM is asserted for the third cycle, indicating
 * that the MC68030 only requests one more long word (the fourth cycle)." */
static void test_cbreq_is_negated_after_the_third_long_word(void) {
  ap_m68030_bus_t bus;
  begin_read(&bus);
  ap_m68030_bus_request_burst(&bus);
  ap_m68030_bus_acknowledge_burst(&bus, true);

  unsigned negated_at = 0;
  while (ap_m68030_bus_active(&bus)) {
    ap_m68030_bus_terminate(&bus, AP_M68030_TERM_STERM);
    (void)ap_m68030_bus_tick(&bus);
    if (!bus.cbreq && negated_at == 0) {
      negated_at = bus.burst_beats;
    }
  }
  TEST_ASSERT_EQUAL_UINT(3, negated_at);
}

/* A device that is not ready inserts a wait clock without advancing the burst:
 * data is accepted only "on every clock during which STERM is asserted". */
static void test_a_burst_beat_without_sterm_is_a_wait_state(void) {
  ap_m68030_bus_t bus;
  begin_read(&bus);
  ap_m68030_bus_request_burst(&bus);
  ap_m68030_bus_acknowledge_burst(&bus, true);

  /* Two clocks to transfer the first long word. */
  ap_m68030_bus_terminate(&bus, AP_M68030_TERM_STERM);
  (void)ap_m68030_bus_tick(&bus);
  (void)ap_m68030_bus_tick(&bus);
  TEST_ASSERT_TRUE(bus.bursting);
  TEST_ASSERT_EQUAL_UINT(1, bus.burst_beats);

  /* A clock with STERM withdrawn moves nothing. */
  ap_m68030_bus_terminate(&bus, AP_M68030_TERM_NONE);
  (void)ap_m68030_bus_tick(&bus);
  TEST_ASSERT_EQUAL_UINT(1, bus.burst_beats);
  TEST_ASSERT_EQUAL_UINT32(1, bus.wait_states);
}

/* "until the burst is complete or an abnormal termination occurs": a bus error
 * part-way through ends the line fill short rather than completing it. */
static void test_a_bus_error_ends_a_burst_early(void) {
  ap_m68030_bus_t bus;
  begin_read(&bus);
  ap_m68030_bus_request_burst(&bus);
  ap_m68030_bus_acknowledge_burst(&bus, true);

  ap_m68030_bus_terminate(&bus, AP_M68030_TERM_STERM);
  (void)ap_m68030_bus_tick(&bus);
  (void)ap_m68030_bus_tick(&bus);
  ap_m68030_bus_terminate(&bus, AP_M68030_TERM_BERR);
  (void)ap_m68030_bus_tick(&bus);

  TEST_ASSERT_FALSE(ap_m68030_bus_active(&bus));
  TEST_ASSERT_TRUE(bus.complete);
  TEST_ASSERT_LESS_THAN_UINT(AP_M68030_BURST_BEATS, bus.burst_beats);
}


/* RMC spans a *pair* of cycles, `[030]` §7.3.5's flowchart beginning "ASSERT
 * READ-MODIFY-WRITE CYCLE (RMC)" before the read and ending "NEGATE RMC" after
 * the write. So beginning a new cycle inside one must not drop it -- a model
 * that cleared the signal with the rest at S0 would leave the write half
 * unprotected, which is exactly the window a DMA controller would take. */
static void test_rmc_survives_the_cycle_boundary_inside_it(void) {
  ap_m68030_bus_t bus = {0};
  ap_m68030_bus_set_rmc(&bus, true);

  ap_m68030_bus_begin(&bus, 0x1000u, 5u, AP_M68030_SIZE_LONG, true, true);
  for (unsigned i = 0; i < 3u && !bus.complete; i++) {
    ap_m68030_bus_terminate(&bus, AP_M68030_TERM_STERM);
    (void)ap_m68030_bus_tick(&bus);
  }
  TEST_ASSERT_TRUE(bus.complete);
  TEST_ASSERT_TRUE(bus.rmc);

  /* The write half of the same operation. */
  ap_m68030_bus_begin(&bus, 0x1000u, 5u, AP_M68030_SIZE_LONG, false, false);
  TEST_ASSERT_TRUE(bus.rmc);

  ap_m68030_bus_set_rmc(&bus, false);
  TEST_ASSERT_FALSE(bus.rmc);
}

/* "Although the operation is synchronous, the burst mode is never used during
 * read-modify-write cycles" (§7.3.6). The request is refused outright rather
 * than asserted and then ignored: CBREQ is a pin, and a processor that raised
 * it and disregarded CBACK would be a different processor. */
static void test_a_burst_is_never_requested_inside_an_rmc_operation(void) {
  ap_m68030_bus_t bus = {0};
  ap_m68030_bus_begin(&bus, 0x2000u, 5u, AP_M68030_SIZE_LONG, true, true);
  ap_m68030_bus_request_burst(&bus);
  TEST_ASSERT_TRUE(bus.cbreq); /* the control: outside an RMC it is requested */

  ap_m68030_bus_t locked = {0};
  ap_m68030_bus_set_rmc(&locked, true);
  ap_m68030_bus_begin(&locked, 0x2000u, 5u, AP_M68030_SIZE_LONG, true, true);
  ap_m68030_bus_request_burst(&locked);
  TEST_ASSERT_FALSE(locked.cbreq);

  /* And with CBACK offered anyway, the cycle stays an ordinary one rather than
   * becoming a burst -- so the refusal is not merely cosmetic. */
  ap_m68030_bus_acknowledge_burst(&locked, true);
  for (unsigned i = 0; i < 3u && !locked.complete; i++) {
    ap_m68030_bus_terminate(&locked, AP_M68030_TERM_STERM);
    (void)ap_m68030_bus_tick(&locked);
  }
  TEST_ASSERT_FALSE(locked.bursting);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_rmc_survives_the_cycle_boundary_inside_it);
  RUN_TEST(test_a_burst_is_never_requested_inside_an_rmc_operation);
  RUN_TEST(test_a_minimum_asynchronous_read_takes_three_clocks);
  RUN_TEST(test_a_synchronous_read_terminated_by_sterm_takes_two_clocks);
  RUN_TEST(test_a_synchronous_cycle_with_one_wait_state_takes_three_clocks);
  RUN_TEST(test_each_wait_state_adds_exactly_one_clock_to_an_asynchronous_read);
  RUN_TEST(test_ecs_is_negated_by_the_end_of_the_first_clock);
  RUN_TEST(test_address_strobe_is_asserted_by_the_end_of_the_first_clock);
  RUN_TEST(test_data_buffer_enable_follows_address_strobe_by_one_clock);
  RUN_TEST(test_a_full_burst_line_fill_takes_five_clocks);
  RUN_TEST(test_four_separate_synchronous_reads_take_eight_clocks);
  RUN_TEST(test_a_burst_needs_cbreq_cback_and_sterm_together);
  RUN_TEST(test_cbreq_is_negated_after_the_third_long_word);
  RUN_TEST(test_a_burst_beat_without_sterm_is_a_wait_state);
  RUN_TEST(test_a_bus_error_ends_a_burst_early);
  RUN_TEST(test_the_strobes_are_all_negated_when_a_read_cycle_completes);
  RUN_TEST(test_a_minimum_asynchronous_write_also_takes_three_clocks);
  RUN_TEST(test_a_write_asserts_data_buffer_enable_a_clock_earlier_than_a_read);
  RUN_TEST(test_a_write_asserts_data_strobe_two_states_later_than_a_read);
  RUN_TEST(test_a_write_holds_data_buffer_enable_through_the_final_state);
  RUN_TEST(test_a_new_cycle_clears_the_signals_held_by_the_previous_one);
  RUN_TEST(test_a_bus_error_ends_the_cycle_without_a_data_transfer);
  RUN_TEST(test_operand_cycle_start_is_not_asserted_on_a_continuation_cycle);
  RUN_TEST(test_every_cpu_clock_has_an_exactly_representable_half_clock);
  RUN_TEST(test_a_cycle_with_no_termination_never_completes);
  return UNITY_END();
}
