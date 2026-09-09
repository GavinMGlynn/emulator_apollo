/* MC68040 bus operation, `[040]` §7, Tables 7-1 through 7-6.
 *
 * §4, §5 and §6 all defer to §7, so several of these tests close a question
 * one of those sections left open. The rest pin the divergences from the two
 * earlier parts in this core.
 */

#include <string.h>

#include "cpu/m68030/ap_m68030_bus.h"
#include "cpu/m68040/ap_m68040_bus.h"
#include "cpu/m68040/ap_m68040_signals.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------------------
 * Transfer size, Table 7-1 -- the encoding §5.3.6 refused to print.
 * ------------------------------------------------------------------------- */

static void test_the_fourth_size_encoding_is_a_line_not_three_bytes(void) {
  /* Table 7-1: 01 byte, 10 word, 00 long word, 11 line. The 68020 and 68030
   * put a three-byte transfer on that fourth encoding, and this core carries
   * it. A board that decoded SIZ for one of those parts would read every
   * 16-byte burst as three bytes. */
  TEST_ASSERT_EQUAL_UINT(3u, (unsigned)AP_M68040_SIZE_LINE);
  TEST_ASSERT_EQUAL_UINT(3u, (unsigned)AP_M68030_SIZE_THREE);
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)AP_M68040_SIZE_BYTE);
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)AP_M68040_SIZE_WORD);
  TEST_ASSERT_EQUAL_UINT(0u, (unsigned)AP_M68040_SIZE_LONG);
}

static void test_a_byte_selects_one_lane_by_its_offset(void) {
  TEST_ASSERT_EQUAL_UINT(0x8u, ap_m68040_byte_lanes(AP_M68040_SIZE_BYTE, 0u));
  TEST_ASSERT_EQUAL_UINT(0x4u, ap_m68040_byte_lanes(AP_M68040_SIZE_BYTE, 1u));
  TEST_ASSERT_EQUAL_UINT(0x2u, ap_m68040_byte_lanes(AP_M68040_SIZE_BYTE, 2u));
  TEST_ASSERT_EQUAL_UINT(0x1u, ap_m68040_byte_lanes(AP_M68040_SIZE_BYTE, 3u));
}

static void test_a_long_word_or_line_ignores_the_byte_offset(void) {
  /* "A1 and A0 = X" in both rows, and §7.4.2: "the selected device should
   * ignore A1 and A0 for long-word and line read transfers" -- which matters
   * because for a line read those two bits are *not* zero, they are copied
   * from the original operand address. */
  for (unsigned offset = 0; offset < 4u; offset++) {
    TEST_ASSERT_EQUAL_UINT(0xFu,
                           ap_m68040_byte_lanes(AP_M68040_SIZE_LONG, offset));
    TEST_ASSERT_EQUAL_UINT(0xFu,
                           ap_m68040_byte_lanes(AP_M68040_SIZE_LINE, offset));
  }
}

static void test_a_word_at_an_odd_offset_never_reaches_the_bus(void) {
  /* Table 7-1 lists word transfers only at A0 = 0. §7.3: the data memory unit
   * "converts misaligned operand accesses that are noncachable to a sequence of
   * aligned accesses", and Figure 7-6 shows two byte transfers. Reported as no
   * lanes rather than guessed at. */
  TEST_ASSERT_EQUAL_UINT(0xCu, ap_m68040_byte_lanes(AP_M68040_SIZE_WORD, 0u));
  TEST_ASSERT_EQUAL_UINT(0x3u, ap_m68040_byte_lanes(AP_M68040_SIZE_WORD, 2u));
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68040_byte_lanes(AP_M68040_SIZE_WORD, 1u));
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68040_byte_lanes(AP_M68040_SIZE_WORD, 3u));
}

static void test_alignment_costs_up_to_three_bus_cycles(void) {
  /* Table 7-3. A long word at offset $1 or $3 costs three cycles, at $2 costs
   * two, aligned costs one -- and the byte column is flat, which is why byte
   * I/O registers are alignment-free. */
  static const unsigned expected[4] = {1u, 3u, 2u, 3u};
  for (unsigned offset = 0; offset < 4u; offset++) {
    TEST_ASSERT_EQUAL_UINT(expected[offset],
                           ap_m68040_bus_cycles(AP_M68040_SIZE_LONG, offset));
    TEST_ASSERT_EQUAL_UINT(1u,
                           ap_m68040_bus_cycles(AP_M68040_SIZE_BYTE, offset));
  }
  TEST_ASSERT_EQUAL_UINT(1u, ap_m68040_bus_cycles(AP_M68040_SIZE_WORD, 0u));
  TEST_ASSERT_EQUAL_UINT(2u, ap_m68040_bus_cycles(AP_M68040_SIZE_WORD, 1u));
  TEST_ASSERT_EQUAL_UINT(1u, ap_m68040_bus_cycles(AP_M68040_SIZE_WORD, 2u));
  TEST_ASSERT_EQUAL_UINT(2u, ap_m68040_bus_cycles(AP_M68040_SIZE_WORD, 3u));
}

static void test_an_instruction_fetch_only_happens_at_offset_zero(void) {
  /* Table 7-3 prints N/A for the other three. "The processor always prefetches
   * instructions by reading a long word from a half-line address (A2-A0 = $0),
   * regardless of alignment." */
  TEST_ASSERT_EQUAL_UINT(1u, ap_m68040_instruction_bus_cycles(0u));
  for (unsigned offset = 1u; offset < 4u; offset++) {
    TEST_ASSERT_EQUAL_UINT(0u, ap_m68040_instruction_bus_cycles(offset));
  }
}

/* ---------------------------------------------------------------------------
 * Access types, Table 7-2.
 * ------------------------------------------------------------------------- */

static void test_the_two_acknowledge_cycles_drive_fixed_addresses(void) {
  /* Interrupt acknowledge at $FFFFFFFF with the level on TMx, breakpoint
   * acknowledge at $00000000 with TM = $0. Both TT = 3. On the earlier parts
   * both are CPU-space cycles whose address carries the level and the type, so
   * a board that decodes an acknowledge from FC7 plus the address has nothing
   * to decode here. */
  const ap_m68040_access_info_t *iack =
      ap_m68040_access(AP_M68040_ACCESS_INTERRUPT_ACK);
  TEST_ASSERT_TRUE(iack->address_is_fixed);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, iack->address);
  TEST_ASSERT_EQUAL_UINT(3u, iack->transfer_type);

  const ap_m68040_access_info_t *bkpt =
      ap_m68040_access(AP_M68040_ACCESS_BREAKPOINT_ACK);
  TEST_ASSERT_TRUE(bkpt->address_is_fixed);
  TEST_ASSERT_EQUAL_HEX32(0x00000000u, bkpt->address);
  TEST_ASSERT_EQUAL_UINT(3u, bkpt->transfer_type);

  /* And both are the acknowledge transfer type §5's Table 5-2 names. */
  TEST_ASSERT_EQUAL_UINT((unsigned)AP_M68040_TT_ACKNOWLEDGE,
                         iack->transfer_type);
}

static void test_an_alternate_access_is_always_cache_inhibited(void) {
  /* Table 7-2 gives CIOUT asserted for the alternate access alone among the
   * seven, and §7.4.1 says why: a `MOVES` to an alternate logical address space
   * is implicitly noncachable. Every other access either negates CIOUT or takes
   * it from the page descriptor. */
  unsigned asserted = 0;
  for (unsigned a = 0; a < (unsigned)AP_M68040_ACCESS_COUNT; a++) {
    if (ap_m68040_access((ap_m68040_access_t)a)->ciout ==
        AP_M68040_SIGNAL_ASSERTED) {
      asserted++;
      TEST_ASSERT_EQUAL_UINT((unsigned)AP_M68040_ACCESS_ALTERNATE, a);
    }
  }
  TEST_ASSERT_EQUAL_UINT(1u, asserted);
}

static void test_only_translated_accesses_take_upa_from_the_page(void) {
  /* Note 1: "the UPA1, UPA0, and CIOUT signals are determined by the U1, U0
   * data and CM bit fields ... corresponding to the access address." That is
   * only meaningful where a page descriptor was consulted, so the normal and
   * MOVE16 accesses take it and the other five drive $0. */
  TEST_ASSERT_EQUAL_INT(AP_M68040_SIGNAL_FROM_MMU,
                        ap_m68040_access(AP_M68040_ACCESS_NORMAL)->upa);
  TEST_ASSERT_EQUAL_INT(AP_M68040_SIGNAL_FROM_MMU,
                        ap_m68040_access(AP_M68040_ACCESS_MOVE16)->upa);
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_SIGNAL_NEGATED,
      ap_m68040_access(AP_M68040_ACCESS_TABLE_SEARCH)->upa);
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_SIGNAL_NEGATED,
      ap_m68040_access(AP_M68040_ACCESS_DATA_CACHE_PUSH)->upa);
}

static void test_a_push_is_a_long_word_or_a_line_and_nothing_else(void) {
  /* Table 7-2 gives "L/Line", and §4.6.2 gives the rule: a single dirty long
   * word is pushed as a long word, two or more as a line. The size on the bus
   * is a direct readout of how many of the four dirty bits were set. */
  const unsigned sizes =
      ap_m68040_access(AP_M68040_ACCESS_DATA_CACHE_PUSH)->sizes;
  TEST_ASSERT_TRUE((sizes & (1u << AP_M68040_SIZE_LONG)) != 0u);
  TEST_ASSERT_TRUE((sizes & (1u << AP_M68040_SIZE_LINE)) != 0u);
  TEST_ASSERT_FALSE((sizes & (1u << AP_M68040_SIZE_BYTE)) != 0u);
  TEST_ASSERT_FALSE((sizes & (1u << AP_M68040_SIZE_WORD)) != 0u);
}

static void test_move16_is_a_line_transfer_and_nothing_else(void) {
  TEST_ASSERT_EQUAL_UINT(1u << AP_M68040_SIZE_LINE,
                         ap_m68040_access(AP_M68040_ACCESS_MOVE16)->sizes);
  TEST_ASSERT_EQUAL_UINT((unsigned)AP_M68040_TT_MOVE16,
                         ap_m68040_access(AP_M68040_ACCESS_MOVE16)->transfer_type);
}

static void test_the_transfer_line_number_is_defined_for_two_access_types(void) {
  /* Note 2: "the TLNx signals are defined only for normal push accesses and
   * normal data line read accesses." §5.3.3 adds that they are wrong rather
   * than merely undefined for an instruction cache burst fill. */
  TEST_ASSERT_TRUE(
      ap_m68040_access(AP_M68040_ACCESS_DATA_CACHE_PUSH)->tln_defined);
  TEST_ASSERT_TRUE(ap_m68040_access(AP_M68040_ACCESS_NORMAL)->tln_defined);
  TEST_ASSERT_FALSE(ap_m68040_access(AP_M68040_ACCESS_MOVE16)->tln_defined);
  TEST_ASSERT_FALSE(
      ap_m68040_access(AP_M68040_ACCESS_TABLE_SEARCH)->tln_defined);
}

/* ---------------------------------------------------------------------------
 * Terminations, Tables 7-4 and 7-5.
 * ------------------------------------------------------------------------- */

static void test_the_four_terminations(void) {
  /* Table 7-5. TEA alone is a bus error; TEA *with* TA is a retry, which is the
   * combination a model reading TEA as "error" gets wrong. */
  TEST_ASSERT_EQUAL_INT(AP_M68040_TERM_WAIT, ap_m68040_terminate(false, false));
  TEST_ASSERT_EQUAL_INT(AP_M68040_TERM_NORMAL,
                        ap_m68040_terminate(true, false));
  TEST_ASSERT_EQUAL_INT(AP_M68040_TERM_BUS_ERROR,
                        ap_m68040_terminate(false, true));
  TEST_ASSERT_EQUAL_INT(AP_M68040_TERM_RETRY, ap_m68040_terminate(true, true));
}

static void test_an_unanswered_interrupt_acknowledge_is_spurious(void) {
  /* Table 7-4: TEA without TA takes the spurious interrupt exception rather
   * than a bus error -- the one place the two tables diverge. */
  TEST_ASSERT_EQUAL_INT(AP_M68040_IACK_SPURIOUS,
                        ap_m68040_iack_terminate(false, true, false));
  TEST_ASSERT_EQUAL_INT(AP_M68040_IACK_SPURIOUS,
                        ap_m68040_iack_terminate(false, true, true));
  TEST_ASSERT_EQUAL_INT(AP_M68040_IACK_RETRY,
                        ap_m68040_iack_terminate(true, true, false));
  TEST_ASSERT_EQUAL_INT(AP_M68040_IACK_WAIT,
                        ap_m68040_iack_terminate(false, false, false));
}

static void test_autovector_is_only_sampled_with_transfer_acknowledge(void) {
  /* "AVEC is only sampled with TA asserted", which is why Table 7-4 prints
   * "Don't Care" on the three rows where TA is negated or TEA is asserted. */
  TEST_ASSERT_EQUAL_INT(AP_M68040_IACK_AUTOVECTOR,
                        ap_m68040_iack_terminate(true, false, true));
  TEST_ASSERT_EQUAL_INT(AP_M68040_IACK_VECTOR,
                        ap_m68040_iack_terminate(true, false, false));
  /* AVEC changes nothing when TA is negated. */
  TEST_ASSERT_EQUAL_INT(ap_m68040_iack_terminate(false, false, false),
                        ap_m68040_iack_terminate(false, false, true));
}

static void test_the_seven_autovectors_start_at_twenty_five(void) {
  /* "The sum of the interrupt priority level plus 24 ($18)", and "there are
   * seven distinct autovectors ... corresponding to the seven levels". Level 0
   * is not a level, so it falls to the spurious vector -- which is 24 itself,
   * the number the autovectors count up from. */
  for (unsigned level = 1u; level <= 7u; level++) {
    TEST_ASSERT_EQUAL_UINT(24u + level, ap_m68040_autovector(level));
  }
  TEST_ASSERT_EQUAL_UINT(25u, ap_m68040_autovector(1u));
  TEST_ASSERT_EQUAL_UINT(31u, ap_m68040_autovector(7u));
  TEST_ASSERT_EQUAL_UINT(AP_M68040_SPURIOUS_VECTOR, ap_m68040_autovector(0u));
  TEST_ASSERT_EQUAL_UINT(24u, AP_M68040_SPURIOUS_VECTOR);
}

/* ---------------------------------------------------------------------------
 * Bus arbitration, Table 7-6.
 * ------------------------------------------------------------------------- */

static void test_park_and_alternate_ownership_share_their_pin_levels(void) {
  /* Both rows of Table 7-6 read BB asserted and BG asserted, so the table's own
   * key columns cannot separate them. §7.8.1 says what does: "whether the
   * three-state logic determines if the M68040 drives the bus and how the
   * M68040 drives BB". In park the M68040 asserts BB and drives the bus with
   * undefined values; under an alternate master it three-states BB and does not
   * drive the bus at all. */
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_ARB_PARK,
      ap_m68040_arbitration_state(true, true, true, false, false));
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_ARB_ALTERNATE_MASTER,
      ap_m68040_arbitration_state(true, false, false, false, false));
}

static void test_an_active_bus_cycle_happens_while_the_grant_is_asserted(void) {
  /* Table 7-6's Active Bus Cycle row prints BG "Negated" in its column and
   * "arbiter asserts BG" in its own Conditions cell. The body text settles it:
   * the processor asserts BB and starts the cycle after being granted the bus,
   * and "as long as BG is asserted, BB remains asserted". */
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_ARB_ACTIVE_BUS_CYCLE,
      ap_m68040_arbitration_state(true, true, true, true, false));
}

static void test_idle_and_snoop_differ_only_in_readiness(void) {
  /* "The snoop state differs from the idle state in that the M68040 is ready to
   * service snooped transfers. Otherwise, the status of BB and the bus is
   * identical." */
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_ARB_IDLE,
      ap_m68040_arbitration_state(false, false, false, false, false));
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_ARB_SNOOP,
      ap_m68040_arbitration_state(false, false, false, false, true));
}

static void test_implicit_ownership_is_the_grant_with_nothing_pending(void) {
  /* "The processor is granted the bus but there are no pending bus cycles":
   * BB three-stated, the bus driven with undefined values. */
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_ARB_IMPLICIT_OWNERSHIP,
      ap_m68040_arbitration_state(true, false, true, false, false));
}

/* ---------------------------------------------------------------------------
 * Stated constants.
 * ------------------------------------------------------------------------- */

static void test_the_reset_timings_section_six_pointed_at(void) {
  /* §6.2.7 says the drive control latches load "after RSTI has been negated,
   * and the 128-clock internal reset cycle has expired" without giving the
   * cycle. §7.10 does, along with the three other numbers. */
  TEST_ASSERT_EQUAL_UINT(128u, AP_M68040_RESET_INTERNAL_CLOCKS);
  TEST_ASSERT_EQUAL_UINT(10u, AP_M68040_RESET_MIN_ASSERT_CLOCKS);
  TEST_ASSERT_EQUAL_UINT(2u, AP_M68040_RESET_SYNCHRONISE_CLOCKS);
  TEST_ASSERT_EQUAL_UINT(512u, AP_M68040_RESET_INSTRUCTION_RSTO_CLOCKS);
}

static void test_a_burst_inhibited_line_costs_three_extra_clocks(void) {
  /* "A burst-inhibited line read completes in eight clocks instead of the five
   * required for a burst read", and the same for a write. */
  TEST_ASSERT_EQUAL_UINT(5u, AP_M68040_LINE_BURST_CLOCKS);
  TEST_ASSERT_EQUAL_UINT(8u, AP_M68040_LINE_BURST_INHIBITED_CLOCKS);
  TEST_ASSERT_EQUAL_UINT(3u, AP_M68040_LINE_BURST_INHIBITED_CLOCKS -
                                 AP_M68040_LINE_BURST_CLOCKS);
}

static void test_five_kinds_of_access_never_allocate_a_line(void) {
  /* §7.4.1 and §7.4.3 list them: table searches and updates, exception vector
   * fetches, exception stacking, and the stack deallocation of an RTE. None of
   * these is a program's own data, so caching them would evict data that is. */
  TEST_ASSERT_EQUAL_UINT(5u, (unsigned)AP_M68040_NONALLOCATING_COUNT);
  for (unsigned i = 0; i < (unsigned)AP_M68040_NONALLOCATING_COUNT; i++) {
    const char *name =
        ap_m68040_nonallocating_name((ap_m68040_nonallocating_t)i);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_TRUE(strlen(name) > 0u);
  }
  TEST_ASSERT_NULL(
      ap_m68040_nonallocating_name(AP_M68040_NONALLOCATING_COUNT));
}

static void test_the_buffer_impedances_of_the_two_drive_modes(void) {
  /* §7.11.1, and these are the two modes §5's IPLx strapping and §6's DRVCTL
   * instructions both select between. */
  TEST_ASSERT_EQUAL_UINT(6u, AP_M68040_LARGE_BUFFER_OHMS);
  TEST_ASSERT_EQUAL_UINT(25u, AP_M68040_SMALL_BUFFER_OHMS);
}

static void test_the_large_buffer_impedance_is_not_one_number(void) {
  /* §7.11.1 gives "6 ohms for both high and low drive". §11.9's worked example
   * uses 6 ohms low and 12 high, and Figure 11-8 labels the large buffer
   * "TYPICAL Z0 = 4-12 ohms" against the small buffer's flat 25. §7.11.1's
   * single symmetric figure is the loosest of the three, and reading §7 alone
   * would leave it standing. */
  TEST_ASSERT_EQUAL_UINT(6u, AP_M68040_LARGE_BUFFER_OHMS);
  TEST_ASSERT_EQUAL_UINT(12u, AP_M68040_LARGE_BUFFER_HIGH_OHMS);
  TEST_ASSERT_EQUAL_UINT(4u, AP_M68040_LARGE_BUFFER_OHMS_MIN);
  TEST_ASSERT_EQUAL_UINT(12u, AP_M68040_LARGE_BUFFER_OHMS_MAX);
  /* The small buffer is the one §7 and §11 agree about. */
  TEST_ASSERT_EQUAL_UINT(25u, AP_M68040_SMALL_BUFFER_OHMS);
}

static void test_the_speed_grades_of_the_three_parts(void) {
  /* §11.5 rates the MC68040 at 25, 33 and 40 MHz; Table 11-4 rates the
   * MC68LC040 and MC68EC040 at 20, 25 and 33. The two sets overlap in the
   * middle and differ at both ends. */
  TEST_ASSERT_TRUE(ap_m68040_is_rated_frequency(25000000u, false));
  TEST_ASSERT_TRUE(ap_m68040_is_rated_frequency(33000000u, false));
  TEST_ASSERT_TRUE(ap_m68040_is_rated_frequency(40000000u, false));
  TEST_ASSERT_FALSE(ap_m68040_is_rated_frequency(20000000u, false));

  TEST_ASSERT_TRUE(ap_m68040_is_rated_frequency(20000000u, true));
  TEST_ASSERT_TRUE(ap_m68040_is_rated_frequency(25000000u, true));
  TEST_ASSERT_TRUE(ap_m68040_is_rated_frequency(33000000u, true));
  TEST_ASSERT_FALSE(ap_m68040_is_rated_frequency(40000000u, true));
}

static void test_nothing_runs_below_twenty_megahertz(void) {
  /* §11.5's "Frequency of Operation" minimum is 20 MHz in all three columns.
   * That floor is what makes §1.1's claim about the MC68040V -- "a 3.3 volt
   * static microprocessor that operates down to 0 MHz" -- a distinction rather
   * than a restatement. */
  TEST_ASSERT_EQUAL_UINT(20000000u, AP_M68040_MIN_FREQUENCY_HZ);
  TEST_ASSERT_FALSE(ap_m68040_is_rated_frequency(16000000u, false));
  TEST_ASSERT_FALSE(ap_m68040_is_rated_frequency(0u, true));
}

static void test_the_lpstop_broadcast_is_the_third_fixed_address_cycle(void) {
  /* Appendix C's Table C-2. The V parts issue it when LPSTOP reaches the
   * execute stage; §5.3.1 and §5.3.2 both mention it in passing without giving
   * the encoding, and Appendix C is where it lands. All three fixed-address
   * cycles share TT = $3 and sit one apart at the top of memory, or at zero. */
  const ap_m68040_access_info_t *lpstop =
      ap_m68040_access(AP_M68040_ACCESS_LPSTOP_BROADCAST);
  TEST_ASSERT_TRUE(lpstop->address_is_fixed);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFEu, lpstop->address);
  TEST_ASSERT_EQUAL_UINT(3u, lpstop->transfer_type);
  TEST_ASSERT_EQUAL_HEX32(
      0xFFFFFFFFu,
      ap_m68040_access(AP_M68040_ACCESS_INTERRUPT_ACK)->address);
  TEST_ASSERT_EQUAL_HEX32(
      0x00000000u,
      ap_m68040_access(AP_M68040_ACCESS_BREAKPOINT_ACK)->address);
}

static void test_the_lpstop_broadcast_is_a_word_write(void) {
  /* "SIZ1, SIZ0 = $2" and "R/W = 0", carrying the new status register value on
   * D15-D0. Every other acknowledge-type cycle is a byte read, so this one is
   * doubly the exception. */
  const ap_m68040_access_info_t *lpstop =
      ap_m68040_access(AP_M68040_ACCESS_LPSTOP_BROADCAST);
  TEST_ASSERT_EQUAL_UINT(1u << AP_M68040_SIZE_WORD, lpstop->sizes);
  TEST_ASSERT_FALSE(lpstop->read_only);
  TEST_ASSERT_TRUE(
      ap_m68040_access(AP_M68040_ACCESS_INTERRUPT_ACK)->read_only);
  TEST_ASSERT_EQUAL_UINT(
      1u << AP_M68040_SIZE_BYTE,
      ap_m68040_access(AP_M68040_ACCESS_INTERRUPT_ACK)->sizes);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_fourth_size_encoding_is_a_line_not_three_bytes);
  RUN_TEST(test_a_byte_selects_one_lane_by_its_offset);
  RUN_TEST(test_a_long_word_or_line_ignores_the_byte_offset);
  RUN_TEST(test_a_word_at_an_odd_offset_never_reaches_the_bus);
  RUN_TEST(test_alignment_costs_up_to_three_bus_cycles);
  RUN_TEST(test_an_instruction_fetch_only_happens_at_offset_zero);
  RUN_TEST(test_the_two_acknowledge_cycles_drive_fixed_addresses);
  RUN_TEST(test_an_alternate_access_is_always_cache_inhibited);
  RUN_TEST(test_only_translated_accesses_take_upa_from_the_page);
  RUN_TEST(test_a_push_is_a_long_word_or_a_line_and_nothing_else);
  RUN_TEST(test_move16_is_a_line_transfer_and_nothing_else);
  RUN_TEST(test_the_transfer_line_number_is_defined_for_two_access_types);
  RUN_TEST(test_the_four_terminations);
  RUN_TEST(test_an_unanswered_interrupt_acknowledge_is_spurious);
  RUN_TEST(test_autovector_is_only_sampled_with_transfer_acknowledge);
  RUN_TEST(test_the_seven_autovectors_start_at_twenty_five);
  RUN_TEST(test_park_and_alternate_ownership_share_their_pin_levels);
  RUN_TEST(test_an_active_bus_cycle_happens_while_the_grant_is_asserted);
  RUN_TEST(test_idle_and_snoop_differ_only_in_readiness);
  RUN_TEST(test_implicit_ownership_is_the_grant_with_nothing_pending);
  RUN_TEST(test_the_reset_timings_section_six_pointed_at);
  RUN_TEST(test_a_burst_inhibited_line_costs_three_extra_clocks);
  RUN_TEST(test_five_kinds_of_access_never_allocate_a_line);
  RUN_TEST(test_the_buffer_impedances_of_the_two_drive_modes);
  RUN_TEST(test_the_large_buffer_impedance_is_not_one_number);
  RUN_TEST(test_the_speed_grades_of_the_three_parts);
  RUN_TEST(test_nothing_runs_below_twenty_megahertz);
  RUN_TEST(test_the_lpstop_broadcast_is_the_third_fixed_address_cycle);
  RUN_TEST(test_the_lpstop_broadcast_is_a_word_write);
  return UNITY_END();
}
