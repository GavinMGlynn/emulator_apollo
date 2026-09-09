/* MC68040 external signals, `[040]` §5, Tables 5-1 through 5-7.
 *
 * §5 yields encodings and reset strapping; the cycle those signals move
 * through is §7. The tests that matter are the ones that pin the divergence
 * from the two earlier parts in this core -- the function code is gone from the
 * bus -- and the three pins that mean something else entirely at reset.
 */

#include <string.h>

#include "cpu/m68040/ap_m68040_signals.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------------------
 * Transfer type, Table 5-2.
 * ------------------------------------------------------------------------- */

static void test_only_normal_and_move16_transfers_are_snooped(void) {
  /* "Only normal and MOVE16 accesses can be snooped." An alternate master's
   * MOVES or acknowledge transfer passes the caches however SCx is driven. */
  TEST_ASSERT_TRUE(ap_m68040_transfer_is_snoopable(AP_M68040_TT_NORMAL));
  TEST_ASSERT_TRUE(ap_m68040_transfer_is_snoopable(AP_M68040_TT_MOVE16));
  TEST_ASSERT_FALSE(ap_m68040_transfer_is_snoopable(AP_M68040_TT_ALTERNATE));
  TEST_ASSERT_FALSE(ap_m68040_transfer_is_snoopable(AP_M68040_TT_ACKNOWLEDGE));
}

/* ---------------------------------------------------------------------------
 * Transfer modifier, Tables 5-3 and 5-4.
 * ------------------------------------------------------------------------- */

static void test_the_four_program_selectable_codes_keep_their_numbers(void) {
  /* User data 1, user code 2, supervisor data 5, supervisor code 6 -- the same
   * four values the 68020 and 68030 drive on FC2-FC0. That is what makes the
   * rest of Table 5-3 easy to model wrongly. */
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)AP_M68040_TM_USER_DATA);
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)AP_M68040_TM_USER_CODE);
  TEST_ASSERT_EQUAL_UINT(5u, (unsigned)AP_M68040_TM_SUPERVISOR_DATA);
  TEST_ASSERT_EQUAL_UINT(6u, (unsigned)AP_M68040_TM_SUPERVISOR_CODE);
}

static void test_cpu_space_is_not_a_transfer_modifier_on_this_part(void) {
  /* Function code 7 is CPU space on both earlier parts and **Reserved** here.
   * Interrupt and breakpoint acknowledge moved to their own transfer type. */
  TEST_ASSERT_EQUAL_UINT(7u, (unsigned)AP_M68040_TM_RESERVED);
  TEST_ASSERT_EQUAL_UINT(3u, (unsigned)AP_M68040_TT_ACKNOWLEDGE);
}

static void test_three_undefined_function_codes_became_internal_traffic(void) {
  /* 0, 3 and 4 were reserved on the earlier parts and now name a data cache
   * push and the two halves of a table search -- the reason a 68040 bus trace
   * shows transfers no instruction asked for. */
  TEST_ASSERT_TRUE(
      ap_m68040_modifier_is_internal(AP_M68040_TM_DATA_CACHE_PUSH));
  TEST_ASSERT_TRUE(
      ap_m68040_modifier_is_internal(AP_M68040_TM_TABLE_SEARCH_DATA));
  TEST_ASSERT_TRUE(
      ap_m68040_modifier_is_internal(AP_M68040_TM_TABLE_SEARCH_CODE));
  TEST_ASSERT_FALSE(ap_m68040_modifier_is_internal(AP_M68040_TM_USER_DATA));
  TEST_ASSERT_FALSE(
      ap_m68040_modifier_is_internal(AP_M68040_TM_SUPERVISOR_CODE));
}

static void test_move16_uses_only_the_two_data_encodings(void) {
  /* Table 5-3's starred rows: "MOVE16 accesses use only these encodings."
   * MOVE16 moves a line and a line is data. */
  TEST_ASSERT_TRUE(
      ap_m68040_modifier_valid_for_move16(AP_M68040_TM_USER_DATA));
  TEST_ASSERT_TRUE(
      ap_m68040_modifier_valid_for_move16(AP_M68040_TM_SUPERVISOR_DATA));
  TEST_ASSERT_FALSE(
      ap_m68040_modifier_valid_for_move16(AP_M68040_TM_USER_CODE));
  TEST_ASSERT_FALSE(
      ap_m68040_modifier_valid_for_move16(AP_M68040_TM_SUPERVISOR_CODE));
  TEST_ASSERT_FALSE(
      ap_m68040_modifier_valid_for_move16(AP_M68040_TM_DATA_CACHE_PUSH));
}

static void test_the_alternate_access_carries_the_other_four_codes(void) {
  /* Table 5-4: logical function codes 0, 3, 4 and 7 -- exactly the four that
   * are *not* reachable as a normal access. The other four encodings are
   * Reserved because they would be redundant. */
  unsigned fc = 0xFFu;
  for (unsigned tm = 0; tm < 8u; tm++) {
    const bool valid = ap_m68040_alternate_function_code(tm, &fc);
    const bool expected = tm == 0u || tm == 3u || tm == 4u || tm == 7u;
    TEST_ASSERT_EQUAL_INT(expected ? 1 : 0, valid ? 1 : 0);
    if (valid) {
      TEST_ASSERT_EQUAL_UINT(tm, fc);
    }
  }
}

static void test_a_reserved_alternate_encoding_leaves_the_output_alone(void) {
  unsigned fc = 0x5Au;
  TEST_ASSERT_FALSE(ap_m68040_alternate_function_code(1u, &fc));
  TEST_ASSERT_EQUAL_UINT(0x5Au, fc);
}

/* ---------------------------------------------------------------------------
 * Reset strapping, §5.7.1, §5.8.1 and §5.10.
 * ------------------------------------------------------------------------- */

static void test_cdis_low_at_reset_selects_the_multiplexed_bus(void) {
  /* "The level on CDIS is latched and used to select the normal bus mode (CDIS
   * high) or multiplexed bus mode (CDIS low)" -- address and data physically
   * tied together, which is nothing to do with disabling a cache. */
  const ap_m68040_reset_options_t low =
      ap_m68040_latch_reset_options(false, true, 0x7u);
  TEST_ASSERT_TRUE(low.multiplexed_bus);
  const ap_m68040_reset_options_t high =
      ap_m68040_latch_reset_options(true, true, 0x7u);
  TEST_ASSERT_FALSE(high.multiplexed_bus);
}

static void test_mdis_low_at_reset_selects_dle_mode(void) {
  /* The memory interface says when to latch read data, instead of the
   * processor latching on BCLK. Again unrelated to the pin's running job. */
  const ap_m68040_reset_options_t low =
      ap_m68040_latch_reset_options(true, false, 0x7u);
  TEST_ASSERT_TRUE(low.dle_mode);
  TEST_ASSERT_FALSE(ap_m68040_latch_reset_options(true, true, 0x7u).dle_mode);
}

static void test_the_interrupt_pins_are_buffer_sizing_at_reset(void) {
  /* Table 5-5, and its note: "high input level = small buffers enabled; low
   * input level = large buffers enabled". IPL2 sizes the data bus, IPL1 the
   * address bus and transfer attributes, IPL0 the miscellaneous controls. A
   * machine that drives IPLx during reset as though it were an interrupt level
   * is choosing drive strengths. */
  const ap_m68040_reset_options_t all_high =
      ap_m68040_latch_reset_options(true, true, 0x7u);
  for (unsigned g = 0; g < 3u; g++) {
    TEST_ASSERT_FALSE(all_high.large_buffers[g]);
  }
  const ap_m68040_reset_options_t all_low =
      ap_m68040_latch_reset_options(true, true, 0x0u);
  for (unsigned g = 0; g < 3u; g++) {
    TEST_ASSERT_TRUE(all_low.large_buffers[g]);
  }
  /* IPL2 alone low: the data bus takes the large buffer and nothing else. */
  const ap_m68040_reset_options_t data_only =
      ap_m68040_latch_reset_options(true, true, 0x3u);
  TEST_ASSERT_TRUE(data_only.large_buffers[AP_M68040_DRIVER_GROUP_DATA_BUS]);
  TEST_ASSERT_FALSE(
      data_only.large_buffers[AP_M68040_DRIVER_GROUP_ADDRESS_AND_ATTRS]);
  TEST_ASSERT_FALSE(
      data_only.large_buffers[AP_M68040_DRIVER_GROUP_MISC_CONTROL]);
}

static void test_each_interrupt_pin_names_its_own_group(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68040_DRIVER_GROUP_DATA_BUS,
                        ap_m68040_driver_group_for_ipl(2u));
  TEST_ASSERT_EQUAL_INT(AP_M68040_DRIVER_GROUP_ADDRESS_AND_ATTRS,
                        ap_m68040_driver_group_for_ipl(1u));
  TEST_ASSERT_EQUAL_INT(AP_M68040_DRIVER_GROUP_MISC_CONTROL,
                        ap_m68040_driver_group_for_ipl(0u));
}

/* ---------------------------------------------------------------------------
 * Processor status, Table 5-6.
 * ------------------------------------------------------------------------- */

static void test_the_status_encodings_are_their_table_values(void) {
  TEST_ASSERT_EQUAL_UINT(0x5u, (unsigned)AP_M68040_PST_HALTED);
  TEST_ASSERT_EQUAL_UINT(0xDu, (unsigned)AP_M68040_PST_STOPPED);
  TEST_ASSERT_EQUAL_UINT(0xEu, (unsigned)AP_M68040_PST_RTE_EXECUTING);
  TEST_ASSERT_EQUAL_UINT(0xFu, (unsigned)AP_M68040_PST_EXCEPTION_STACKING);
}

static void test_a_double_bus_fault_is_visible_on_four_pins(void) {
  /* Encoding 5, "Halted State (Double Bus Fault)". This part has **no HALT
   * pin** -- neither Table 5-1 nor Table 5-7 lists one -- where the 68020 and
   * 68030 have a bidirectional HALT the processor drives when a double bus
   * fault stops it. So the same condition that a board reads off a dedicated
   * pin on the earlier parts has to be decoded from four status pins here. */
  TEST_ASSERT_TRUE(ap_m68040_pst_persists(AP_M68040_PST_HALTED));
  TEST_ASSERT_FALSE(ap_m68040_pst_ends_instruction(AP_M68040_PST_HALTED));
  TEST_ASSERT_NULL(ap_m68040_signal("HALT"));
}

static void test_six_encodings_last_exactly_one_bus_clock(void) {
  /* "The encodings 1, 2, 3, 9, A, and B ... exist for only one BCLK period per
   * instruction, and are mutually exclusive." Everything else persists. */
  const ap_m68040_pst_t boundary[] = {
      AP_M68040_PST_USER_END,
      AP_M68040_PST_USER_BRANCH_NOT_TAKEN,
      AP_M68040_PST_USER_BRANCH_TAKEN,
      AP_M68040_PST_SUPERVISOR_END,
      AP_M68040_PST_SUPERVISOR_BRANCH_NOT_TAKEN,
      AP_M68040_PST_SUPERVISOR_BRANCH_TAKEN};
  unsigned found = 0;
  for (unsigned pst = 0; pst < 16u; pst++) {
    bool expected = false;
    for (unsigned i = 0; i < 6u; i++) {
      if ((unsigned)boundary[i] == pst) {
        expected = true;
      }
    }
    const bool actual = ap_m68040_pst_ends_instruction((ap_m68040_pst_t)pst);
    TEST_ASSERT_EQUAL_INT(expected ? 1 : 0, actual ? 1 : 0);
    if (actual) {
      found++;
      /* The two classes are disjoint. */
      TEST_ASSERT_FALSE(ap_m68040_pst_persists((ap_m68040_pst_t)pst));
    }
  }
  TEST_ASSERT_EQUAL_UINT(6u, found);
}

static void test_the_manuals_own_classification_omits_two_encodings(void) {
  /* §5.9.1 lists 0, 8, 4, 5, C, D, E, F as persisting and 1, 2, 3, 9, A, B as
   * one-clock -- fourteen of sixteen. 6 (low-power stop, on the V parts) and 7
   * (reserved) appear in Table 5-6 and in neither list. */
  unsigned unclassified = 0;
  for (unsigned pst = 0; pst < 16u; pst++) {
    if (!ap_m68040_pst_is_classified((ap_m68040_pst_t)pst)) {
      unclassified++;
    }
  }
  TEST_ASSERT_EQUAL_UINT(2u, unclassified);
  TEST_ASSERT_FALSE(ap_m68040_pst_is_classified(AP_M68040_PST_LOW_POWER_STOP));
  TEST_ASSERT_FALSE(ap_m68040_pst_is_classified(AP_M68040_PST_RESERVED));
  /* Low-power stop is a state the processor stays in, so it is reported as
   * persisting on its own terms despite the omission. */
  TEST_ASSERT_TRUE(ap_m68040_pst_persists(AP_M68040_PST_LOW_POWER_STOP));
}

static void test_pst3_is_not_a_privilege_bit(void) {
  /* Five encodings are labelled "Supervisor" in Table 5-6. Four more have PST3
   * set -- stopped, RTE executing, exception stacking, supervisor table
   * search -- and of those only the table search carries the word. Treating
   * PST3 as a privilege bit would report the other three as supervisor state,
   * which the table does not say. */
  TEST_ASSERT_TRUE(
      ap_m68040_pst_is_supervisor(AP_M68040_PST_SUPERVISOR_TABLE_SEARCH));
  TEST_ASSERT_FALSE(ap_m68040_pst_is_supervisor(AP_M68040_PST_STOPPED));
  TEST_ASSERT_FALSE(ap_m68040_pst_is_supervisor(AP_M68040_PST_RTE_EXECUTING));
  TEST_ASSERT_FALSE(
      ap_m68040_pst_is_supervisor(AP_M68040_PST_EXCEPTION_STACKING));
  TEST_ASSERT_FALSE(ap_m68040_pst_is_supervisor(AP_M68040_PST_USER_START));
}

/* ---------------------------------------------------------------------------
 * Signal summary, Tables 5-1 and 5-7.
 * ------------------------------------------------------------------------- */

static void test_the_signal_summary_has_all_forty_rows(void) {
  const ap_m68040_signal_t *rows = ap_m68040_signals();
  TEST_ASSERT_EQUAL_UINT(40u, AP_M68040_SIGNAL_COUNT);
  for (unsigned i = 0; i < AP_M68040_SIGNAL_COUNT; i++) {
    TEST_ASSERT_NOT_NULL(rows[i].mnemonic);
    TEST_ASSERT_NOT_NULL(rows[i].name);
    TEST_ASSERT_TRUE(strlen(rows[i].mnemonic) > 0u);
  }
}

static void test_no_mnemonic_appears_twice(void) {
  const ap_m68040_signal_t *rows = ap_m68040_signals();
  for (unsigned i = 0; i < AP_M68040_SIGNAL_COUNT; i++) {
    for (unsigned j = i + 1u; j < AP_M68040_SIGNAL_COUNT; j++) {
      TEST_ASSERT_NOT_EQUAL(0, strcmp(rows[i].mnemonic, rows[j].mnemonic));
    }
  }
}

static void test_read_write_is_the_only_signal_active_in_both_senses(void) {
  /* Table 5-7 gives R/W as "High/Low" and every other row one level or none. */
  const ap_m68040_signal_t *rows = ap_m68040_signals();
  unsigned both = 0;
  for (unsigned i = 0; i < AP_M68040_SIGNAL_COUNT; i++) {
    if (rows[i].active == AP_M68040_ACTIVE_BOTH) {
      both++;
      TEST_ASSERT_EQUAL_STRING("R/W", rows[i].mnemonic);
    }
  }
  TEST_ASSERT_EQUAL_UINT(1u, both);
}

static void test_memory_inhibit_is_an_output_that_never_three_states(void) {
  /* MI is the output reset does not negate -- "all outputs, except MI" -- and
   * Table 5-7 marks it not three-state, so it is driven whenever the part is
   * powered. Both facts are the same design decision: an alternate master must
   * never see memory answer for a line this part may hold dirty. */
  const ap_m68040_signal_t *mi = ap_m68040_signal("MI");
  TEST_ASSERT_NOT_NULL(mi);
  TEST_ASSERT_EQUAL_INT(AP_M68040_SIGNAL_OUTPUT, mi->type);
  TEST_ASSERT_FALSE(mi->three_state);
  TEST_ASSERT_EQUAL_INT(AP_M68040_ACTIVE_LOW, mi->active);
}

static void test_the_three_signals_that_are_not_on_every_part(void) {
  /* Availability from Table 5-1's notes, which are the revised ones: Table
   * 5-7's own notes omit the V parts for DLE and MDIS both. */
  TEST_ASSERT_EQUAL_INT(AP_M68040_PART_MC68040_ONLY,
                        ap_m68040_signal("DLE")->scope);
  TEST_ASSERT_EQUAL_INT(AP_M68040_PART_NOT_EC040_FAMILY,
                        ap_m68040_signal("MDIS")->scope);
  TEST_ASSERT_EQUAL_INT(AP_M68040_PART_NOT_V_PARTS,
                        ap_m68040_signal("PCLK")->scope);
  TEST_ASSERT_EQUAL_INT(AP_M68040_PART_NOT_V_PARTS,
                        ap_m68040_signal("TRST")->scope);
  /* The interrupt pins carry a note in both tables that is about power-up
   * behaviour, not availability, so they are on every part. */
  TEST_ASSERT_EQUAL_INT(AP_M68040_PART_ALL,
                        ap_m68040_signal("IPL2-IPL0")->scope);
}

static void test_an_unknown_mnemonic_is_reported_rather_than_guessed(void) {
  TEST_ASSERT_NULL(ap_m68040_signal("FC2-FC0"));
  TEST_ASSERT_NULL(ap_m68040_signal("DSACK1"));
  TEST_ASSERT_NULL(ap_m68040_signal(""));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_only_normal_and_move16_transfers_are_snooped);
  RUN_TEST(test_the_four_program_selectable_codes_keep_their_numbers);
  RUN_TEST(test_cpu_space_is_not_a_transfer_modifier_on_this_part);
  RUN_TEST(test_three_undefined_function_codes_became_internal_traffic);
  RUN_TEST(test_move16_uses_only_the_two_data_encodings);
  RUN_TEST(test_the_alternate_access_carries_the_other_four_codes);
  RUN_TEST(test_a_reserved_alternate_encoding_leaves_the_output_alone);
  RUN_TEST(test_cdis_low_at_reset_selects_the_multiplexed_bus);
  RUN_TEST(test_mdis_low_at_reset_selects_dle_mode);
  RUN_TEST(test_the_interrupt_pins_are_buffer_sizing_at_reset);
  RUN_TEST(test_each_interrupt_pin_names_its_own_group);
  RUN_TEST(test_the_status_encodings_are_their_table_values);
  RUN_TEST(test_a_double_bus_fault_is_visible_on_four_pins);
  RUN_TEST(test_six_encodings_last_exactly_one_bus_clock);
  RUN_TEST(test_the_manuals_own_classification_omits_two_encodings);
  RUN_TEST(test_pst3_is_not_a_privilege_bit);
  RUN_TEST(test_the_signal_summary_has_all_forty_rows);
  RUN_TEST(test_no_mnemonic_appears_twice);
  RUN_TEST(test_read_write_is_the_only_signal_active_in_both_senses);
  RUN_TEST(test_memory_inhibit_is_an_output_that_never_three_states);
  RUN_TEST(test_the_three_signals_that_are_not_on_every_part);
  RUN_TEST(test_an_unknown_mnemonic_is_reported_rather_than_guessed);
  return UNITY_END();
}
