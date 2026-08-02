/* MC68882 coprocessor interface registers, `[68881]` §7.2 and Table 7-2.
 *
 * Checked against the table's structure rather than by re-reading it: every
 * address in the select field classified, the don't-care bits exercised on both
 * of their values, and the footnote's two absent registers asserted absent.
 */

#include "cpu/m68882/ap_m68882_cir.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Every one of the 32 select values decodes to something -- a register, a
 * reserved location, or nothing. A gap would be an address the coprocessor is
 * reachable at and this model cannot classify. */
static void test_every_select_value_classifies(void) {
  for (uint32_t select = 0u; select < 32u; select++) {
    const ap_m68882_cir_t cir = ap_m68882_cir_select(select);
    /* `NONE` is a legal answer only for the two select values Table 7-2 leaves
     * out entirely -- $18 and $1A are the instruction address CIR's, and the
     * table's rows cover the rest. */
    if (cir == AP_M68882_CIR_NONE) {
      TEST_ASSERT_TRUE_MESSAGE(select == 0x18u || select == 0x1Au ||
                                   select >= 0x1Cu || select == 0x16u ||
                                   select == 0x17u,
                               "an unclassified select value");
    }
  }
}

/* **The don't-care bit.** Table 7-2 gives `0000x`, not `00000`: the 16-bit
 * registers occupy both of their byte addresses. Decoding all five bits exactly
 * would leave every odd address undecoded, and odd addresses are reachable. */
static void test_the_sixteen_bit_registers_answer_at_both_addresses(void) {
  const struct {
    uint32_t even;
    ap_m68882_cir_t cir;
    const char *what;
  } CASES[] = {
      {0x00u, AP_M68882_CIR_RESPONSE, "response"},
      {0x02u, AP_M68882_CIR_CONTROL, "control"},
      {0x04u, AP_M68882_CIR_SAVE, "save"},
      {0x06u, AP_M68882_CIR_RESTORE, "restore"},
      {0x0Au, AP_M68882_CIR_COMMAND, "command"},
      {0x0Eu, AP_M68882_CIR_CONDITION, "condition"},
      {0x14u, AP_M68882_CIR_REGISTER_SELECT, "register select"},
  };

  for (unsigned i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(CASES[i].cir,
                                  ap_m68882_cir_select(CASES[i].even),
                                  CASES[i].what);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CASES[i].cir,
                                  ap_m68882_cir_select(CASES[i].even + 1u),
                                  CASES[i].what);
  }
}

/* The three 32-bit registers span **four** byte addresses each -- `100xx`,
 * `110xx`, `111xx`. Matching the narrower patterns first would put `$12` in a
 * reserved location rather than inside the operand CIR. */
static void test_the_thirty_two_bit_registers_span_four_addresses(void) {
  for (uint32_t offset = 0u; offset < 4u; offset++) {
    TEST_ASSERT_EQUAL_INT(AP_M68882_CIR_OPERAND,
                          ap_m68882_cir_select(0x10u + offset));
    TEST_ASSERT_EQUAL_INT(AP_M68882_CIR_INSTRUCTION_ADDRESS,
                          ap_m68882_cir_select(0x18u + offset));
    TEST_ASSERT_EQUAL_INT(AP_M68882_CIR_OPERAND_ADDRESS,
                          ap_m68882_cir_select(0x1Cu + offset));
  }

  TEST_ASSERT_EQUAL_UINT(32u, ap_m68882_cir_width(AP_M68882_CIR_OPERAND));
  TEST_ASSERT_EQUAL_UINT(
      32u, ap_m68882_cir_width(AP_M68882_CIR_INSTRUCTION_ADDRESS));
  TEST_ASSERT_EQUAL_UINT(16u, ap_m68882_cir_width(AP_M68882_CIR_RESPONSE));
}

/* **All three selectors, and each one alone matches something else.** CPU space
 * is shared with the breakpoint acknowledge this core already runs; the type
 * field is shared with every other coprocessor; and the cpID means nothing
 * outside CPU space. A model checking any one would answer cycles meant for
 * another device. */
static void test_selection_needs_the_function_code_type_and_cpid(void) {
  const uint32_t address =
      (AP_M68882_CPU_SPACE_TYPE << 16) | (AP_M68882_DEFAULT_CPID << 13) | 0x00u;
  TEST_ASSERT_TRUE(
      ap_m68882_cir_selected(7u, address, AP_M68882_DEFAULT_CPID));

  /* Not CPU space: an ordinary supervisor data cycle at the same address. */
  TEST_ASSERT_FALSE(
      ap_m68882_cir_selected(5u, address, AP_M68882_DEFAULT_CPID));

  /* CPU space, but the breakpoint acknowledge's type field of `0000` -- which
   * is the transaction `BKPT` runs, at the same function code. */
  const uint32_t breakpoint = (AP_M68882_DEFAULT_CPID << 13) | 0x0Cu;
  TEST_ASSERT_FALSE(
      ap_m68882_cir_selected(7u, breakpoint, AP_M68882_DEFAULT_CPID));

  /* Right space and type, another coprocessor's cpID. */
  const uint32_t other =
      (AP_M68882_CPU_SPACE_TYPE << 16) | (3u << 13) | 0x00u;
  TEST_ASSERT_FALSE(ap_m68882_cir_selected(7u, other, AP_M68882_DEFAULT_CPID));
}

/* Table 7-2's access types. Getting one backwards is not a fault -- a read-only
 * register silently swallows writes and a write-only one silently answers -- so
 * every row is checked. */
static void test_table_7_2_access_types(void) {
  const struct {
    ap_m68882_cir_t cir;
    bool readable;
    bool writable;
    const char *what;
  } ROWS[] = {
      {AP_M68882_CIR_RESPONSE, true, false, "response is read"},
      {AP_M68882_CIR_CONTROL, false, true, "control is write"},
      {AP_M68882_CIR_SAVE, true, false, "save is read"},
      {AP_M68882_CIR_RESTORE, true, true, "restore is read/write"},
      {AP_M68882_CIR_COMMAND, false, true, "command is write"},
      {AP_M68882_CIR_CONDITION, false, true, "condition is write"},
      {AP_M68882_CIR_OPERAND, true, true, "operand is read/write"},
      {AP_M68882_CIR_REGISTER_SELECT, true, false, "register select is read"},
      {AP_M68882_CIR_INSTRUCTION_ADDRESS, false, true,
       "instruction address is write"},
  };

  for (unsigned i = 0; i < sizeof ROWS / sizeof ROWS[0]; i++) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(ROWS[i].readable,
                                  ap_m68882_cir_readable(ROWS[i].cir),
                                  ROWS[i].what);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ROWS[i].writable,
                                  ap_m68882_cir_writable(ROWS[i].cir),
                                  ROWS[i].what);
  }
}

/* **The footnote's two absent registers.** The operation word and operand
 * address CIRs are in Figure 7-2's map and are "not implemented" on this part:
 * "writes to these locations are ignored, and reads always return all ones".
 * A map transcribed without its footnote would give them storage and a driver
 * probing for a 68881 would find one where there is none. */
static void test_the_two_unimplemented_registers_are_absent(void) {
  TEST_ASSERT_FALSE(ap_m68882_cir_implemented(AP_M68882_CIR_OPERATION_WORD));
  TEST_ASSERT_FALSE(ap_m68882_cir_implemented(AP_M68882_CIR_OPERAND_ADDRESS));

  /* Every other named register *is* implemented, so the exclusion is those two
   * and not a blanket. */
  const ap_m68882_cir_t present[] = {
      AP_M68882_CIR_RESPONSE, AP_M68882_CIR_CONTROL,
      AP_M68882_CIR_SAVE,     AP_M68882_CIR_RESTORE,
      AP_M68882_CIR_COMMAND,  AP_M68882_CIR_CONDITION,
      AP_M68882_CIR_OPERAND,  AP_M68882_CIR_REGISTER_SELECT,
      AP_M68882_CIR_INSTRUCTION_ADDRESS};
  for (unsigned i = 0; i < sizeof present / sizeof present[0]; i++) {
    TEST_ASSERT_TRUE(ap_m68882_cir_implemented(present[i]));
  }
}

/* An unanswerable read yields **all ones**, never zero. Zero is a legal value
 * for most of these registers, so a driver could not tell "nothing answered"
 * from data -- and the manual is explicit: "read accesses of a write-only
 * register always return all ones". */
static void test_an_unanswerable_read_is_all_ones_at_its_own_width(void) {
  TEST_ASSERT_EQUAL_HEX32(
      0x0000FFFFu, ap_m68882_cir_unreadable_value(AP_M68882_CIR_COMMAND));
  /* And at the register's own width, so a 32-bit one gives 32 bits of ones. */
  TEST_ASSERT_EQUAL_HEX32(
      0xFFFFFFFFu,
      ap_m68882_cir_unreadable_value(AP_M68882_CIR_INSTRUCTION_ADDRESS));
}

/* The reserved locations at $0C and $16 are reserved rather than absent or
 * mistaken for their neighbours -- Table 7-2 lists them as rows with no name,
 * and a decoder that folded them into the register below would answer a
 * reserved address with the condition CIR. */
static void test_the_reserved_locations_are_reserved(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68882_CIR_RESERVED, ap_m68882_cir_select(0x0Cu));
  TEST_ASSERT_EQUAL_INT(AP_M68882_CIR_RESERVED, ap_m68882_cir_select(0x0Du));
  TEST_ASSERT_EQUAL_INT(AP_M68882_CIR_RESERVED, ap_m68882_cir_select(0x16u));
  TEST_ASSERT_EQUAL_INT(AP_M68882_CIR_RESERVED, ap_m68882_cir_select(0x17u));

  /* Their neighbours are still themselves. */
  TEST_ASSERT_EQUAL_INT(AP_M68882_CIR_CONDITION, ap_m68882_cir_select(0x0Eu));
  TEST_ASSERT_EQUAL_INT(AP_M68882_CIR_REGISTER_SELECT,
                        ap_m68882_cir_select(0x14u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_every_select_value_classifies);
  RUN_TEST(test_the_sixteen_bit_registers_answer_at_both_addresses);
  RUN_TEST(test_the_thirty_two_bit_registers_span_four_addresses);
  RUN_TEST(test_selection_needs_the_function_code_type_and_cpid);
  RUN_TEST(test_table_7_2_access_types);
  RUN_TEST(test_the_two_unimplemented_registers_are_absent);
  RUN_TEST(test_an_unanswerable_read_is_all_ones_at_its_own_width);
  RUN_TEST(test_the_reserved_locations_are_reserved);
  return UNITY_END();
}
