/* Intel 8259A, `[8259]` order number 231468-003.
 *
 * Each test states a fact about the part and quotes the line that fixes it.
 * Where a fact comes from prose recovering a figure the scan lost, the test
 * says so -- those are the ones a legible copy of Figure 8 could correct. */

#include "unity.h"

#include <string.h>

#include "device/ap_i8259.h"

void setUp(void) {}
void tearDown(void) {}

/* The initialization the Apollo board uses: cascaded (so ICW3), 8086 vectoring
 * (so ICW4), edge triggered. `base` is ICW2. */
static void initialize(ap_i8259_t *pic, uint8_t base, bool master,
                       uint8_t icw3) {
  ap_i8259_init(pic);
  ap_i8259_write(pic, false, 0x11); /* ICW1: D4 marker, IC4 = 1, SNGL = 0 */
  ap_i8259_write(pic, true, base);  /* ICW2 */
  ap_i8259_write(pic, true, icw3);  /* ICW3 */
  ap_i8259_write(pic, true,
                 (uint8_t)(0x01u | (master ? 0x04u : 0x00u))); /* ICW4: uPM */
}

/* Raise a line and let it settle, which in edge mode is a transition. */
static void pulse(ap_i8259_t *pic, unsigned line) {
  ap_i8259_set_request(pic, line, false);
  ap_i8259_set_request(pic, line, true);
}

static uint8_t full_acknowledge(ap_i8259_t *pic) {
  (void)ap_i8259_acknowledge_first(pic);
  return ap_i8259_acknowledge_second(pic);
}

static void test_an_uninitialised_controller_requests_nothing(void) {
  ap_i8259_t pic;
  ap_i8259_init(&pic);

  /* `[8259]` clears the IMR in ICW1, not at power-on. A part that answered
   * before being given a vector base would answer with vector zero, so this
   * core masks everything until told otherwise. */
  pulse(&pic, 3);
  TEST_ASSERT_FALSE(ap_i8259_interrupt_pending(&pic));
  TEST_ASSERT_FALSE(ap_i8259_vectoring_supported(&pic));
}

static void test_the_first_command_word_clears_the_mask(void) {
  ap_i8259_t pic;
  ap_i8259_init(&pic);
  ap_i8259_write(&pic, true, 0xFF); /* try to mask before ICW1 */

  ap_i8259_write(&pic, false, 0x11);
  /* "(b) The Interrupt Mask Register is cleared." */
  TEST_ASSERT_EQUAL_HEX8(0x00, ap_i8259_read(&pic, true));
}

static void test_the_first_command_word_makes_line_zero_highest_priority(void) {
  ap_i8259_t pic;
  initialize(&pic, 0x40, true, 0x04);

  /* "(c) IR7 input is assigned priority 7", and "After the initialization
   * sequence, IR0 has the highest priority and IR7 the lowest." */
  pulse(&pic, 7);
  pulse(&pic, 0);
  TEST_ASSERT_EQUAL_UINT(0u, ap_i8259_acknowledge_first(&pic));
}

static void test_the_initialization_sequence_skips_icw3_when_single(void) {
  ap_i8259_t pic;
  ap_i8259_init(&pic);

  /* `[8259]` Figure 6 and ICW1's own SNGL: "If SNGL = 1 no ICW3 will be
   * issued." So the word after ICW2 is ICW4, and a model that always expected
   * ICW3 would consume ICW4 as a cascade mask and then treat the first OCW as
   * ICW4 -- silently, with the part left in the wrong mode. */
  ap_i8259_write(&pic, false, 0x13); /* ICW1: SNGL = 1, IC4 = 1 */
  ap_i8259_write(&pic, true, 0x40);  /* ICW2 */
  ap_i8259_write(&pic, true, 0x01);  /* ICW4, not ICW3 */

  TEST_ASSERT_TRUE(ap_i8259_vectoring_supported(&pic));
}

static void test_the_initialization_sequence_skips_icw4_when_not_asked(void) {
  ap_i8259_t pic;
  ap_i8259_init(&pic);

  /* "If IC4 = 0, then all functions selected in ICW4 are set to zero." */
  ap_i8259_write(&pic, false, 0x12); /* ICW1: SNGL = 1, IC4 = 0 */
  ap_i8259_write(&pic, true, 0x40);  /* ICW2 -- sequence ends here */

  TEST_ASSERT_FALSE(ap_i8259_vectoring_supported(&pic));
  /* The next A0 = 1 write is OCW1, the mask, not ICW4. */
  ap_i8259_write(&pic, true, 0xAA);
  TEST_ASSERT_EQUAL_HEX8(0xAA, ap_i8259_read(&pic, true));
}

static void test_two_controllers_initialise_independently(void) {
  ap_i8259_t master;
  ap_i8259_t slave;

  /* The part is cascaded in pairs, so anything remembered between ICW1 and
   * ICW4 must be per-instance. A shared "is an ICW4 expected" would let the
   * master's ICW1 decide the slave's sequence -- and the failure would look
   * like a mode bug in the slave, a long way from its cause. */
  ap_i8259_init(&master);
  ap_i8259_init(&slave);

  ap_i8259_write(&master, false, 0x11); /* master: IC4 = 1 */
  ap_i8259_write(&slave, false, 0x12);  /* slave: SNGL = 1, IC4 = 0 */

  ap_i8259_write(&master, true, 0x40);
  ap_i8259_write(&slave, true, 0x48);

  /* The slave's sequence is over; the master still wants ICW3 then ICW4. */
  ap_i8259_write(&slave, true, 0x55); /* OCW1 */
  TEST_ASSERT_EQUAL_HEX8(0x55, ap_i8259_read(&slave, true));

  ap_i8259_write(&master, true, 0x04); /* ICW3 */
  ap_i8259_write(&master, true, 0x01); /* ICW4 */
  TEST_ASSERT_TRUE(ap_i8259_vectoring_supported(&master));
  TEST_ASSERT_FALSE(ap_i8259_vectoring_supported(&slave));
}

static void test_the_vector_is_the_programmed_base_plus_the_level(void) {
  ap_i8259_t pic;
  initialize(&pic, 0x68, true, 0x04);

  /* "In an 8086 system A15-A11 are inserted in the five most significant bits
   * of the vectoring byte and the 8259A sets the three least significant bits
   * according to the interrupt level."
   *
   * And independently, `008778-03` §3.2 tabulates the Apollo vector byte as
   * `T7 T6 T5 T4 T3` followed by the level in the low three bits. Two manuals,
   * one byte. */
  pulse(&pic, 5);
  TEST_ASSERT_EQUAL_HEX8(0x6D, full_acknowledge(&pic));
}

static void test_the_low_three_bits_of_the_vector_base_are_ignored(void) {
  ap_i8259_t pic;
  initialize(&pic, 0x6F, true, 0x04);

  /* "A10-A5 are ignored" -- the level supplies the bottom three bits, so a
   * base with them set must not disturb the answer. */
  pulse(&pic, 2);
  TEST_ASSERT_EQUAL_HEX8(0x6A, full_acknowledge(&pic));
}

static void test_a_masked_level_does_not_interrupt(void) {
  ap_i8259_t pic;
  initialize(&pic, 0x40, true, 0x04);

  /* "M = 1 indicates the channel is masked (inhibited)". */
  ap_i8259_write(&pic, true, 0x08); /* mask IR3 */
  pulse(&pic, 3);
  TEST_ASSERT_FALSE(ap_i8259_interrupt_pending(&pic));

  /* And the request is still latched: masking hides it, it does not discard
   * it. "Masking an IR channel does not affect the other channels
   * operation." */
  ap_i8259_write(&pic, true, 0x00);
  TEST_ASSERT_TRUE(ap_i8259_interrupt_pending(&pic));
}

static void test_a_level_in_service_blocks_the_same_and_lower(void) {
  ap_i8259_t pic;
  initialize(&pic, 0x40, true, 0x04);

  /* "While the IS bit is set, all further interrupts of the same or lower
   * priority are inhibited, while higher levels will generate an interrupt." */
  pulse(&pic, 4);
  TEST_ASSERT_EQUAL_UINT(4u, ap_i8259_acknowledge_first(&pic));
  (void)ap_i8259_acknowledge_second(&pic);

  pulse(&pic, 6); /* lower priority */
  TEST_ASSERT_FALSE(ap_i8259_interrupt_pending(&pic));

  pulse(&pic, 1); /* higher priority */
  TEST_ASSERT_TRUE(ap_i8259_interrupt_pending(&pic));
  TEST_ASSERT_EQUAL_UINT(1u, ap_i8259_acknowledge_first(&pic));
}

static void test_a_non_specific_end_of_interrupt_clears_the_highest(void) {
  ap_i8259_t pic;
  initialize(&pic, 0x40, true, 0x04);

  /* "the 8259A will automatically reset the highest IS bit of those that are
   * set". Encoding from prose: "A non-specific EOI can be issued with OCW2
   * (EOI = 1, SL = 0, R = 0)" -- 0x20. */
  pulse(&pic, 4);
  (void)full_acknowledge(&pic);
  pulse(&pic, 1);
  (void)full_acknowledge(&pic);

  ap_i8259_write(&pic, false, 0x20);

  /* IR1 was the higher, so it went; IR4 is still in service and still blocks
   * IR6. */
  pulse(&pic, 6);
  TEST_ASSERT_FALSE(ap_i8259_interrupt_pending(&pic));
  ap_i8259_write(&pic, false, 0x20);
  TEST_ASSERT_TRUE(ap_i8259_interrupt_pending(&pic));
}

static void test_a_specific_end_of_interrupt_clears_the_named_level(void) {
  ap_i8259_t pic;
  initialize(&pic, 0x40, true, 0x04);

  /* "(EOI = 1, SL = 1, R = 0, and L0-L2 is the binary level of the IS bit to be
   * reset)" -- 0x60 | level. */
  pulse(&pic, 4);
  (void)full_acknowledge(&pic);
  pulse(&pic, 1);
  (void)full_acknowledge(&pic);

  /* Clear the *lower* one, which a non-specific EOI could not have done. */
  ap_i8259_write(&pic, false, (uint8_t)(0x60u | 4u));

  pulse(&pic, 6);
  /* IR1 still in service, so IR6 is still blocked. */
  TEST_ASSERT_FALSE(ap_i8259_interrupt_pending(&pic));
  ap_i8259_write(&pic, false, (uint8_t)(0x60u | 1u));
  TEST_ASSERT_TRUE(ap_i8259_interrupt_pending(&pic));
}

static void test_rotating_on_end_of_interrupt_moves_the_serviced_to_bottom(
    void) {
  ap_i8259_t pic;
  initialize(&pic, 0x40, true, 0x04);

  /* "the Rotation on Non-Specific EOI Command (R = 1, SL = 0, EOI = 1)" --
   * 0xA0. "if IR5 is programmed as the bottom priority device, then IR6 will
   * have the highest one." */
  pulse(&pic, 4);
  (void)full_acknowledge(&pic);
  ap_i8259_write(&pic, false, 0xA0);

  /* IR4 is now bottom, so IR5 is top: with both 5 and 3 pending, 5 wins --
   * which under the initial fixed order it would not have. */
  pulse(&pic, 3);
  pulse(&pic, 5);
  TEST_ASSERT_EQUAL_UINT(5u, ap_i8259_acknowledge_first(&pic));
}

static void test_set_priority_changes_the_order_without_an_end_of_interrupt(
    void) {
  ap_i8259_t pic;
  initialize(&pic, 0x40, true, 0x04);

  /* "The Set Priority command is issued in OCW2 where: R = 1, SL = 1, L0-L2 is
   * the binary priority level code of the bottom priority device" -- 0xC0.
   * "Observe that in this mode internal status is updated by software control
   * during OCW2. However, it is independent of the End of Interrupt (EOI)
   * command." Nothing is in service here, and the order still moves. */
  ap_i8259_write(&pic, false, (uint8_t)(0xC0u | 2u)); /* IR2 bottom, IR3 top */

  pulse(&pic, 0);
  pulse(&pic, 3);
  TEST_ASSERT_EQUAL_UINT(3u, ap_i8259_acknowledge_first(&pic));
}

static void test_automatic_end_of_interrupt_clears_on_the_second_pulse(void) {
  ap_i8259_t pic;
  ap_i8259_init(&pic);
  ap_i8259_write(&pic, false, 0x13); /* SNGL, IC4 */
  ap_i8259_write(&pic, true, 0x40);
  ap_i8259_write(&pic, true, 0x03); /* ICW4: uPM = 1, AEOI = 1 */

  /* "the 8259A will automatically perform a non-specific EOI operation at the
   * trailing edge of the last interrupt acknowledge pulse ... second in 8086."
   * So after the first pulse the level blocks, and after the second it does
   * not. */
  pulse(&pic, 2);
  (void)ap_i8259_acknowledge_first(&pic);
  pulse(&pic, 5);
  TEST_ASSERT_FALSE(ap_i8259_interrupt_pending(&pic));

  (void)ap_i8259_acknowledge_second(&pic);
  TEST_ASSERT_TRUE(ap_i8259_interrupt_pending(&pic));
}

static void test_an_edge_triggered_line_held_high_interrupts_once(void) {
  ap_i8259_t pic;
  initialize(&pic, 0x40, true, 0x04);

  /* "If LTIM = 0, an interrupt request will be recognized by a low to high
   * transition on an IR input. The IR input can remain high without generating
   * another interrupt." */
  ap_i8259_set_request(&pic, 3, true);
  TEST_ASSERT_EQUAL_UINT(3u, ap_i8259_acknowledge_first(&pic));
  (void)ap_i8259_acknowledge_second(&pic);
  ap_i8259_write(&pic, false, 0x20); /* EOI */

  /* Still high, but no new edge. */
  TEST_ASSERT_FALSE(ap_i8259_interrupt_pending(&pic));

  ap_i8259_set_request(&pic, 3, false);
  ap_i8259_set_request(&pic, 3, true);
  TEST_ASSERT_TRUE(ap_i8259_interrupt_pending(&pic));
}

static void test_a_level_triggered_line_held_high_interrupts_again(void) {
  ap_i8259_t pic;
  ap_i8259_init(&pic);
  ap_i8259_write(&pic, false, 0x1B); /* ICW1: LTIM = 1, SNGL = 1, IC4 = 1 */
  ap_i8259_write(&pic, true, 0x40);
  ap_i8259_write(&pic, true, 0x01);

  /* The other half of the same fact, and the pair is the point: either test
   * alone passes a model stuck in the mode it happens to probe. */
  ap_i8259_set_request(&pic, 3, true);
  (void)full_acknowledge(&pic);
  ap_i8259_write(&pic, false, 0x20); /* EOI */

  TEST_ASSERT_TRUE(ap_i8259_interrupt_pending(&pic));
}

static void test_a_request_too_short_to_acknowledge_becomes_level_seven(void) {
  ap_i8259_t pic;
  initialize(&pic, 0x40, true, 0x04);

  /* "If no interrupt request is present at step 4 of either sequence (i.e., the
   * request was too short in duration) the 8259A will issue an interrupt level
   * 7. Both the vectoring bytes and the CAS lines will look like an interrupt
   * level 7 was requested." */
  pulse(&pic, 3);
  ap_i8259_set_request(&pic, 3, false); /* withdrawn before INTA */
  TEST_ASSERT_FALSE(ap_i8259_interrupt_pending(&pic));

  TEST_ASSERT_EQUAL_UINT(7u, ap_i8259_acknowledge_first(&pic));
  TEST_ASSERT_EQUAL_HEX8(0x47, ap_i8259_acknowledge_second(&pic));
}

static void test_a_spurious_acknowledgement_puts_nothing_in_service(void) {
  ap_i8259_t pic;
  initialize(&pic, 0x40, true, 0x04);

  /* The half of the spurious-interrupt behaviour that matters and that the
   * quoted sentence does not spell out: level 7 is *reported*, but nothing is
   * in service, so no EOI is owed. Modelling it as an ordinary acknowledgement
   * would leave a phantom ISR bit blocking every lower level for ever. */
  (void)full_acknowledge(&pic);

  ap_i8259_write(&pic, false, 0x0B); /* OCW3: read ISR */
  TEST_ASSERT_EQUAL_HEX8(0x00, ap_i8259_read(&pic, false));

  pulse(&pic, 6);
  TEST_ASSERT_TRUE(ap_i8259_interrupt_pending(&pic));
}

static void test_the_status_read_selects_between_request_and_service(void) {
  ap_i8259_t pic;
  initialize(&pic, 0x40, true, 0x04);

  /* "The IRR can be read when, prior to the RD pulse, a Read Register Command
   * is issued with OCW3 (RR = 1, RIS = 0.)" and ISR with RIS = 1. */
  pulse(&pic, 2);
  pulse(&pic, 5);
  (void)full_acknowledge(&pic); /* IR2 into service */

  ap_i8259_write(&pic, false, 0x0A); /* RR = 1, RIS = 0 */
  TEST_ASSERT_EQUAL_HEX8(0x20, ap_i8259_read(&pic, false));

  ap_i8259_write(&pic, false, 0x0B); /* RR = 1, RIS = 1 */
  TEST_ASSERT_EQUAL_HEX8(0x04, ap_i8259_read(&pic, false));

  /* "the 8259A remembers whether the IRR or ISR has been previously selected
   * by the OCW3" -- no fresh OCW3 needed. */
  TEST_ASSERT_EQUAL_HEX8(0x04, ap_i8259_read(&pic, false));
}

static void test_after_initialization_a_status_read_returns_the_request(void) {
  ap_i8259_t pic;
  initialize(&pic, 0x40, true, 0x04);

  /* "(e) Special Mask Mode is cleared and Status Read is set to IRR", and
   * "After initialization the 8259A is set to IRR." */
  pulse(&pic, 6);
  TEST_ASSERT_EQUAL_HEX8(0x40, ap_i8259_read(&pic, false));
}

static void test_the_poll_command_acknowledges_on_the_next_read(void) {
  ap_i8259_t pic;
  initialize(&pic, 0x40, true, 0x04);

  /* "The 8259A treats the next RD pulse to the 8259A as an interrupt
   * acknowledge, sets the appropriate IS bit if there is a request, and reads
   * the priority level." The word is "I" in D7 and the level in D2-D0. */
  pulse(&pic, 5);
  ap_i8259_write(&pic, false, 0x0C); /* OCW3 with P = 1 */
  TEST_ASSERT_EQUAL_HEX8(0x85, ap_i8259_read(&pic, false));

  /* It really did acknowledge: the level is in service. */
  ap_i8259_write(&pic, false, 0x0B);
  TEST_ASSERT_EQUAL_HEX8(0x20, ap_i8259_read(&pic, false));
}

static void test_a_poll_with_nothing_pending_reports_no_interrupt(void) {
  ap_i8259_t pic;
  initialize(&pic, 0x40, true, 0x04);

  /* "I: Equal to 1 if there is an interrupt." Zero when there is not -- and
   * notably *not* the spurious level 7 that a real INTA would produce, because
   * a poll is a read and not an acknowledge cycle. */
  ap_i8259_write(&pic, false, 0x0C);
  TEST_ASSERT_EQUAL_HEX8(0x00, ap_i8259_read(&pic, false));
}

static void test_the_special_mask_mode_lets_lower_levels_through(void) {
  ap_i8259_t pic;
  initialize(&pic, 0x40, true, 0x04);

  /* "In the special Mask Mode, when a mask bit is set in OCW1, it inhibits
   * further interrupts at that level and enables interrupts from all other
   * levels (lower as well as higher) that are not masked." */
  pulse(&pic, 2);
  (void)full_acknowledge(&pic);

  pulse(&pic, 5);
  TEST_ASSERT_FALSE(ap_i8259_interrupt_pending(&pic));

  ap_i8259_write(&pic, true, 0x04);  /* mask the in-service level */
  ap_i8259_write(&pic, false, 0x68); /* OCW3: ESMM = 1, SMM = 1 */
  TEST_ASSERT_TRUE(ap_i8259_interrupt_pending(&pic));

  /* And clearing it puts the block back. */
  ap_i8259_write(&pic, false, 0x48); /* ESMM = 1, SMM = 0 */
  TEST_ASSERT_FALSE(ap_i8259_interrupt_pending(&pic));
}

static void test_the_special_fully_nested_mode_admits_the_same_level(void) {
  ap_i8259_t pic;
  ap_i8259_init(&pic);
  ap_i8259_write(&pic, false, 0x11);
  ap_i8259_write(&pic, true, 0x40);
  ap_i8259_write(&pic, true, 0x04);
  ap_i8259_write(&pic, true, 0x11); /* ICW4: uPM = 1, SFNM = 1 */

  /* The mode exists so a master does not shut out a cascaded slave: without it
   * the slave's level is in service in the master, and a *higher* interrupt
   * from that same slave arrives on the same master line and would be blocked
   * as "same priority". */
  pulse(&pic, 2);
  (void)full_acknowledge(&pic);

  pulse(&pic, 2);
  TEST_ASSERT_TRUE(ap_i8259_interrupt_pending(&pic));
  TEST_ASSERT_EQUAL_UINT(2u, ap_i8259_poll_level(&pic));
}

static void test_without_the_special_mode_the_same_level_is_blocked(void) {
  ap_i8259_t pic;
  initialize(&pic, 0x40, true, 0x04); /* SFNM = 0 */

  /* The control for the test above. Without it, that test passes against a
   * model that never blocks the same level at all. */
  pulse(&pic, 2);
  (void)full_acknowledge(&pic);
  pulse(&pic, 2);
  TEST_ASSERT_FALSE(ap_i8259_interrupt_pending(&pic));
}

static void test_reinitialisation_is_recognised_at_any_point(void) {
  ap_i8259_t pic;
  initialize(&pic, 0x40, true, 0x04);
  pulse(&pic, 3);
  (void)full_acknowledge(&pic);

  /* "Whenever a command is issued with A0 = 0 and D4 = 1, this is interpreted
   * as Initialization Command Word 1 (ICW1)." Whenever -- so the test comes
   * before any OCW decode, and software can restart a wedged part. */
  ap_i8259_write(&pic, false, 0x11);
  ap_i8259_write(&pic, false, 0x0B);
  TEST_ASSERT_EQUAL_HEX8(0x00, ap_i8259_read(&pic, false)); /* ISR cleared */
}

static void test_two_controllers_initialised_alike_hold_identical_state(void) {
  ap_i8259_t a;
  ap_i8259_t b;

  /* Determinism: no field may be left as whatever was on the stack. */
  memset(&a, 0xAA, sizeof a);
  memset(&b, 0x55, sizeof b);
  ap_i8259_init(&a);
  ap_i8259_init(&b);
  TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof a);

  initialize(&a, 0x40, true, 0x04);
  initialize(&b, 0x40, true, 0x04);
  TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof a);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_an_uninitialised_controller_requests_nothing);
  RUN_TEST(test_the_first_command_word_clears_the_mask);
  RUN_TEST(test_the_first_command_word_makes_line_zero_highest_priority);
  RUN_TEST(test_the_initialization_sequence_skips_icw3_when_single);
  RUN_TEST(test_the_initialization_sequence_skips_icw4_when_not_asked);
  RUN_TEST(test_two_controllers_initialise_independently);
  RUN_TEST(test_the_vector_is_the_programmed_base_plus_the_level);
  RUN_TEST(test_the_low_three_bits_of_the_vector_base_are_ignored);
  RUN_TEST(test_a_masked_level_does_not_interrupt);
  RUN_TEST(test_a_level_in_service_blocks_the_same_and_lower);
  RUN_TEST(test_a_non_specific_end_of_interrupt_clears_the_highest);
  RUN_TEST(test_a_specific_end_of_interrupt_clears_the_named_level);
  RUN_TEST(test_rotating_on_end_of_interrupt_moves_the_serviced_to_bottom);
  RUN_TEST(test_set_priority_changes_the_order_without_an_end_of_interrupt);
  RUN_TEST(test_automatic_end_of_interrupt_clears_on_the_second_pulse);
  RUN_TEST(test_an_edge_triggered_line_held_high_interrupts_once);
  RUN_TEST(test_a_level_triggered_line_held_high_interrupts_again);
  RUN_TEST(test_a_request_too_short_to_acknowledge_becomes_level_seven);
  RUN_TEST(test_a_spurious_acknowledgement_puts_nothing_in_service);
  RUN_TEST(test_the_status_read_selects_between_request_and_service);
  RUN_TEST(test_after_initialization_a_status_read_returns_the_request);
  RUN_TEST(test_the_poll_command_acknowledges_on_the_next_read);
  RUN_TEST(test_a_poll_with_nothing_pending_reports_no_interrupt);
  RUN_TEST(test_the_special_mask_mode_lets_lower_levels_through);
  RUN_TEST(test_the_special_fully_nested_mode_admits_the_same_level);
  RUN_TEST(test_without_the_special_mode_the_same_level_is_blocked);
  RUN_TEST(test_reinitialisation_is_recognised_at_any_point);
  RUN_TEST(test_two_controllers_initialised_alike_hold_identical_state);
  return UNITY_END();
}
