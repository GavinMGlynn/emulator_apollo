/* MC68040 IEEE 1149.1A test access port, `[040]` §6, Tables 6-1 and 6-2.
 *
 * Nothing in this machine drives JTAG. The tests that earn their place are the
 * ones checking a 184-row transcription against its own structure, and the two
 * §6 facts that are about reset rather than about test.
 */

#include <string.h>

#include "cpu/m68040/ap_m68040_jtag.h"
#include "cpu/m68040/ap_m68040_signals.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------------------
 * Instruction shift register, Table 6-1.
 * ------------------------------------------------------------------------- */

static void test_the_instruction_register_is_three_bits(void) {
  /* "A 3-bit instruction shift register without parity ... shifts one of eight
   * instructions", and the BSDL agrees: INSTRUCTION_LENGTH is 3. */
  TEST_ASSERT_EQUAL_UINT(3u, AP_M68040_JTAG_IR_BITS);
  TEST_ASSERT_EQUAL_UINT(7u, (unsigned)AP_M68040_JTAG_BYPASS);
}

static void test_reset_selects_bypass_and_capture_selects_highz(void) {
  /* "The instruction shift register is reset to all ones in the TAP controller
   * test-logic-reset state, which is equivalent to selecting the BYPASS
   * instruction", and "during the capture-IR state, the binary value 001 is
   * loaded" -- which is HIGHZ, deliberately, so that a board with only TMS and
   * TCK connected can float every output driver. */
  TEST_ASSERT_EQUAL_INT(AP_M68040_JTAG_BYPASS, AP_M68040_JTAG_IR_RESET);
  TEST_ASSERT_EQUAL_INT(AP_M68040_JTAG_HIGHZ, AP_M68040_JTAG_IR_CAPTURE);
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)AP_M68040_JTAG_IR_CAPTURE);
}

static void test_four_instructions_scan_pins_and_four_bypass(void) {
  /* Table 6-1's last column. */
  const ap_m68040_jtag_instruction_t scanning[] = {
      AP_M68040_JTAG_EXTEST, AP_M68040_JTAG_SAMPLE_PRELOAD,
      AP_M68040_JTAG_DRVCTL_T, AP_M68040_JTAG_DRVCTL_S};
  unsigned found = 0;
  for (unsigned i = 0; i < 8u; i++) {
    const bool boundary =
        ap_m68040_jtag_data_register((ap_m68040_jtag_instruction_t)i) ==
        AP_M68040_JTAG_REG_BOUNDARY_SCAN;
    bool expected = false;
    for (unsigned j = 0; j < 4u; j++) {
      if ((unsigned)scanning[j] == i) {
        expected = true;
      }
    }
    TEST_ASSERT_EQUAL_INT(expected ? 1 : 0, boundary ? 1 : 0);
    if (boundary) {
      found++;
    }
  }
  TEST_ASSERT_EQUAL_UINT(4u, found);
}

static void test_stopping_the_clocks_is_licensed_by_four_instructions(void) {
  /* "The system clocks (PCLK and BCLK) cannot be stopped ... except when the
   * EXTEST, HIGHZ, DRVCTL.T, or SHUTDOWN instructions have been properly
   * invoked", and elsewhere "failure to do so could result in potential
   * internal damage to the device". This is a hardware-destruction rule, so it
   * is worth having as a predicate rather than as prose. */
  TEST_ASSERT_TRUE(ap_m68040_jtag_may_stop_clocks(AP_M68040_JTAG_EXTEST));
  TEST_ASSERT_TRUE(ap_m68040_jtag_may_stop_clocks(AP_M68040_JTAG_HIGHZ));
  TEST_ASSERT_TRUE(ap_m68040_jtag_may_stop_clocks(AP_M68040_JTAG_DRVCTL_T));
  TEST_ASSERT_TRUE(ap_m68040_jtag_may_stop_clocks(AP_M68040_JTAG_SHUTDOWN));
  TEST_ASSERT_FALSE(
      ap_m68040_jtag_may_stop_clocks(AP_M68040_JTAG_SAMPLE_PRELOAD));
  TEST_ASSERT_FALSE(ap_m68040_jtag_may_stop_clocks(AP_M68040_JTAG_DRVCTL_S));
  TEST_ASSERT_FALSE(ap_m68040_jtag_may_stop_clocks(AP_M68040_JTAG_BYPASS));
}

static void test_the_two_drive_control_instructions_differ_only_in_who_owns_the_pins(void) {
  /* Both "select one of two output drivers on a pin-by-pin basis" from the same
   * boundary scan data. DRVCTL.T "invokes the keep-alive clock, asserts the
   * internal reset, and the test logic, not the system logic, has control of
   * the I/O pins"; DRVCTL.S does none of that. Same register, opposite side of
   * the boundary. */
  TEST_ASSERT_EQUAL_INT(
      ap_m68040_jtag_data_register(AP_M68040_JTAG_DRVCTL_T),
      ap_m68040_jtag_data_register(AP_M68040_JTAG_DRVCTL_S));
  TEST_ASSERT_TRUE(ap_m68040_jtag_takes_over_pins(AP_M68040_JTAG_DRVCTL_T));
  TEST_ASSERT_FALSE(ap_m68040_jtag_takes_over_pins(AP_M68040_JTAG_DRVCTL_S));
}

static void test_the_private_instruction_leaves_the_pins_alone(void) {
  /* "Motorola reserves this instruction for manufacturing use. The instruction
   * does not change pin I/O as defined for system operation." It shares the
   * clock-entry restriction with the four that take the pins, and is not one
   * of them. */
  TEST_ASSERT_FALSE(ap_m68040_jtag_takes_over_pins(AP_M68040_JTAG_PRIVATE));
  TEST_ASSERT_EQUAL_INT(AP_M68040_JTAG_REG_BYPASS,
                        ap_m68040_jtag_data_register(AP_M68040_JTAG_PRIVATE));
}

/* ---------------------------------------------------------------------------
 * Boundary scan register, Table 6-2.
 * ------------------------------------------------------------------------- */

static void test_the_boundary_scan_register_is_184_bits(void) {
  TEST_ASSERT_EQUAL_UINT(184u, AP_M68040_JTAG_BS_BITS);
  for (unsigned b = 0; b < AP_M68040_JTAG_BS_BITS; b++) {
    const ap_m68040_bs_bit_t *cell = ap_m68040_jtag_bs_bit(b);
    TEST_ASSERT_NOT_NULL(cell);
    TEST_ASSERT_TRUE(strlen(cell->pin) > 0u);
  }
  TEST_ASSERT_NULL(ap_m68040_jtag_bs_bit(184u));
}

static void test_the_five_control_cells_are_where_the_manual_says(void) {
  /* §6.3 lists them by bit: io.ab 150, io.db 151, io.2 154, io.1 155,
   * io.0 156. Nothing else is a control cell. */
  const struct {
    unsigned bit;
    const char *name;
  } expected[AP_M68040_JTAG_BS_CONTROL_CELLS] = {
      {150u, "io.ab"}, {151u, "io.db"}, {154u, "io.2"},
      {155u, "io.1"},  {156u, "io.0"}};
  unsigned control = 0;
  for (unsigned b = 0; b < AP_M68040_JTAG_BS_BITS; b++) {
    if (ap_m68040_jtag_bs_is_control_cell(b)) {
      control++;
    }
  }
  TEST_ASSERT_EQUAL_UINT(AP_M68040_JTAG_BS_CONTROL_CELLS, control);
  for (unsigned i = 0; i < AP_M68040_JTAG_BS_CONTROL_CELLS; i++) {
    TEST_ASSERT_TRUE(ap_m68040_jtag_bs_is_control_cell(expected[i].bit));
    TEST_ASSERT_EQUAL_STRING(expected[i].name,
                             ap_m68040_jtag_bs_bit(expected[i].bit)->pin);
  }
}

static void test_every_controlled_pin_names_a_real_control_cell(void) {
  /* The transcription's own consistency check: a control_bit must point at one
   * of the five IO.Ctl cells, and a cell with no control_bit must be either an
   * output-only pin, an input, or a control cell itself. */
  for (unsigned b = 0; b < AP_M68040_JTAG_BS_BITS; b++) {
    const ap_m68040_bs_bit_t *cell = ap_m68040_jtag_bs_bit(b);
    if (cell->control_bit >= 0) {
      TEST_ASSERT_TRUE(
          ap_m68040_jtag_bs_is_control_cell((unsigned)cell->control_bit));
      TEST_ASSERT_TRUE(cell->pin_type == AP_M68040_BS_TS_OUTPUT ||
                       cell->pin_type == AP_M68040_BS_IO);
    } else {
      TEST_ASSERT_TRUE(cell->pin_type == AP_M68040_BS_OUTPUT ||
                       cell->pin_type == AP_M68040_BS_INPUT ||
                       cell->pin_type == AP_M68040_BS_CONTROL);
    }
  }
}

static void test_every_bidirectional_pin_has_an_input_and_an_output_cell(void) {
  /* "All M68040 bidirectional pins include two boundary scan data cells, an
   * input, and an output." So each I/O pin name occurs exactly twice, once as
   * O.Latch and once as I.Pin, and both name the same control cell. */
  for (unsigned b = 0; b < AP_M68040_JTAG_BS_BITS; b++) {
    const ap_m68040_bs_bit_t *cell = ap_m68040_jtag_bs_bit(b);
    if (cell->pin_type != AP_M68040_BS_IO) {
      continue;
    }
    unsigned outputs = 0;
    unsigned inputs = 0;
    for (unsigned o = 0; o < AP_M68040_JTAG_BS_BITS; o++) {
      const ap_m68040_bs_bit_t *other = ap_m68040_jtag_bs_bit(o);
      if (strcmp(other->pin, cell->pin) != 0) {
        continue;
      }
      TEST_ASSERT_EQUAL_INT(cell->control_bit, other->control_bit);
      if (other->cell == AP_M68040_BS_O_LATCH) {
        outputs++;
      }
      if (other->cell == AP_M68040_BS_I_PIN) {
        inputs++;
      }
    }
    TEST_ASSERT_EQUAL_UINT(1u, outputs);
    TEST_ASSERT_EQUAL_UINT(1u, inputs);
  }
}

static void test_the_scan_order_is_not_the_pin_order(void) {
  /* Worth pinning because it is the part a generated table would get wrong.
   * A10-A31 come first, alternating output and input; then all thirty-two data
   * output cells, then all thirty-two data input cells; then A9 down to A0,
   * alternating again. */
  TEST_ASSERT_EQUAL_STRING("A10", ap_m68040_jtag_bs_bit(9u)->pin);
  TEST_ASSERT_EQUAL_INT(AP_M68040_BS_O_LATCH, ap_m68040_jtag_bs_bit(9u)->cell);
  TEST_ASSERT_EQUAL_STRING("A10", ap_m68040_jtag_bs_bit(10u)->pin);
  TEST_ASSERT_EQUAL_INT(AP_M68040_BS_I_PIN, ap_m68040_jtag_bs_bit(10u)->cell);
  TEST_ASSERT_EQUAL_STRING("A31", ap_m68040_jtag_bs_bit(51u)->pin);

  TEST_ASSERT_EQUAL_STRING("D0", ap_m68040_jtag_bs_bit(53u)->pin);
  TEST_ASSERT_EQUAL_INT(AP_M68040_BS_O_LATCH, ap_m68040_jtag_bs_bit(53u)->cell);
  TEST_ASSERT_EQUAL_STRING("D31", ap_m68040_jtag_bs_bit(84u)->pin);
  TEST_ASSERT_EQUAL_STRING("D0", ap_m68040_jtag_bs_bit(85u)->pin);
  TEST_ASSERT_EQUAL_INT(AP_M68040_BS_I_PIN, ap_m68040_jtag_bs_bit(85u)->cell);
  TEST_ASSERT_EQUAL_STRING("D31", ap_m68040_jtag_bs_bit(116u)->pin);

  TEST_ASSERT_EQUAL_STRING("A9", ap_m68040_jtag_bs_bit(117u)->pin);
  TEST_ASSERT_EQUAL_STRING("A0", ap_m68040_jtag_bs_bit(135u)->pin);
  TEST_ASSERT_EQUAL_STRING("MDIS", ap_m68040_jtag_bs_bit(183u)->pin);
}

static void test_the_bus_lock_pair_moved_between_mask_sets(void) {
  /* Table 6-2 gives LOCKE at 146 and LOCK at 149, both controlled by io.1 at
   * bit 155. §6.6's BSDL revision list opens with "LOCK and LOCKE controlled by
   * io.1 vice io.0 (4D98D)", so on the 0.8-um masks D43B, D50D and D98D they
   * were controlled by io.0 at 156. The newer arrangement is modelled. */
  TEST_ASSERT_EQUAL_STRING("LOCKE", ap_m68040_jtag_bs_bit(146u)->pin);
  TEST_ASSERT_EQUAL_STRING("LOCK", ap_m68040_jtag_bs_bit(149u)->pin);
  TEST_ASSERT_EQUAL_INT(155, ap_m68040_jtag_bs_bit(146u)->control_bit);
  TEST_ASSERT_EQUAL_INT(155, ap_m68040_jtag_bs_bit(149u)->control_bit);
  TEST_ASSERT_EQUAL_STRING("io.1", ap_m68040_jtag_bs_bit(155u)->pin);
}

static void test_drive_control_reaches_exactly_the_output_cells(void) {
  /* Table 6-2's note 2 marks the pin-type column of every O.Latch row and of no
   * other row, so this holds by transcription rather than by construction. */
  for (unsigned b = 0; b < AP_M68040_JTAG_BS_BITS; b++) {
    const ap_m68040_bs_bit_t *cell = ap_m68040_jtag_bs_bit(b);
    TEST_ASSERT_EQUAL_INT(cell->cell == AP_M68040_BS_O_LATCH ? 1 : 0,
                          ap_m68040_jtag_bs_selects_driver(b) ? 1 : 0);
  }
  TEST_ASSERT_TRUE(ap_m68040_jtag_bs_selects_driver(0u));   /* RSTO */
  TEST_ASSERT_FALSE(ap_m68040_jtag_bs_selects_driver(183u)); /* MDIS */
  TEST_ASSERT_FALSE(ap_m68040_jtag_bs_selects_driver(150u)); /* io.ab */
}

static void test_the_scan_register_covers_every_signal_in_the_summary(void) {
  /* Table 6-2 "includes cells for all device signal pins and clock pins".
   * Cross-checking it against §5's Table 5-7 is the one place the two sections
   * can contradict each other, and they do not: every scanned pin name that is
   * a pin rather than a control cell appears in the signal summary. Power,
   * ground and the five test pins are not scanned, which is why the check runs
   * this direction only. */
  for (unsigned b = 0; b < AP_M68040_JTAG_BS_BITS; b++) {
    const ap_m68040_bs_bit_t *cell = ap_m68040_jtag_bs_bit(b);
    if (cell->pin_type == AP_M68040_BS_CONTROL) {
      continue;
    }
    /* Table 5-7 names buses as ranges; the scan names their members. Skip the
     * numbered members of a bus and check the singular signals. */
    if (cell->pin[0] == 'A' && cell->pin[1] >= '0' && cell->pin[1] <= '9') {
      continue;
    }
    if (cell->pin[0] == 'D' && cell->pin[1] >= '0' && cell->pin[1] <= '9') {
      continue;
    }
    if (strncmp(cell->pin, "TT", 2u) == 0 ||
        strncmp(cell->pin, "TM", 2u) == 0 ||
        strncmp(cell->pin, "TLN", 3u) == 0 ||
        strncmp(cell->pin, "SIZ", 3u) == 0 ||
        strncmp(cell->pin, "UPA", 3u) == 0 ||
        strncmp(cell->pin, "PST", 3u) == 0 ||
        strncmp(cell->pin, "SC", 2u) == 0 ||
        strncmp(cell->pin, "IPL", 3u) == 0) {
      continue;
    }
    TEST_ASSERT_NOT_NULL(ap_m68040_signal(cell->pin));
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_instruction_register_is_three_bits);
  RUN_TEST(test_reset_selects_bypass_and_capture_selects_highz);
  RUN_TEST(test_four_instructions_scan_pins_and_four_bypass);
  RUN_TEST(test_stopping_the_clocks_is_licensed_by_four_instructions);
  RUN_TEST(
      test_the_two_drive_control_instructions_differ_only_in_who_owns_the_pins);
  RUN_TEST(test_the_private_instruction_leaves_the_pins_alone);
  RUN_TEST(test_the_boundary_scan_register_is_184_bits);
  RUN_TEST(test_the_five_control_cells_are_where_the_manual_says);
  RUN_TEST(test_every_controlled_pin_names_a_real_control_cell);
  RUN_TEST(test_every_bidirectional_pin_has_an_input_and_an_output_cell);
  RUN_TEST(test_the_scan_order_is_not_the_pin_order);
  RUN_TEST(test_the_bus_lock_pair_moved_between_mask_sets);
  RUN_TEST(test_drive_control_reaches_exactly_the_output_cells);
  RUN_TEST(test_the_scan_register_covers_every_signal_in_the_summary);
  return UNITY_END();
}
