/* MC68030 addressing mode categories.
 *
 * Table 2-4's Alterable column does not survive the scan, so the module derives
 * the table from §2.3's definitions instead. That makes these tests the actual
 * check on it: each one states the rule the derivation rests on, and the last
 * two check the derived table against instructions whose own manual pages say
 * which modes they take -- an independent source for the same fact.
 */

#include "cpu/m68030/ap_m68030_category.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Every kind, so a sweep cannot miss one. */
static const ap_m68030_ea_kind_t ALL[] = {
    AP_M68030_EA_DATA_REGISTER,   AP_M68030_EA_ADDRESS_REGISTER,
    AP_M68030_EA_ADDRESS_INDIRECT, AP_M68030_EA_POSTINCREMENT,
    AP_M68030_EA_PREDECREMENT,    AP_M68030_EA_DISPLACEMENT,
    AP_M68030_EA_INDEXED,         AP_M68030_EA_ABSOLUTE_SHORT,
    AP_M68030_EA_ABSOLUTE_LONG,   AP_M68030_EA_PC_DISPLACEMENT,
    AP_M68030_EA_PC_INDEXED,      AP_M68030_EA_IMMEDIATE,
};
#define ALL_COUNT (sizeof ALL / sizeof ALL[0])

/* "Data addressing modes refer to data operands." An address register holds an
 * address, not a data operand -- which is exactly why ADD accepts every mode
 * but An, and why ADDA exists to cover that one. */
static void test_only_an_address_register_is_not_a_data_operand(void) {
  for (unsigned i = 0; i < ALL_COUNT; i++) {
    const bool expected = ALL[i] != AP_M68030_EA_ADDRESS_REGISTER;
    TEST_ASSERT_EQUAL_INT(expected, ap_m68030_ea_is_data(ALL[i]));
  }
}

/* "Memory addressing modes refer to memory operands", so the two register
 * direct modes are the only ones out. The immediate is *in*: it is fetched from
 * the instruction stream, which is memory. */
static void test_the_register_direct_modes_are_the_only_non_memory_ones(void) {
  for (unsigned i = 0; i < ALL_COUNT; i++) {
    const bool expected = ALL[i] != AP_M68030_EA_DATA_REGISTER &&
                          ALL[i] != AP_M68030_EA_ADDRESS_REGISTER;
    TEST_ASSERT_EQUAL_INT(expected, ap_m68030_ea_is_memory(ALL[i]));
  }
}

/* "Control addressing modes refer to memory operands without an associated
 * size." The increment modes carry one -- their step *is* the operand size --
 * and so does the immediate, whose length is the operand size. That is the
 * whole difference between control and memory, and it is why `JMP (A0)+` does
 * not exist: there would be no size to step by. */
static void test_control_is_memory_without_an_associated_size(void) {
  for (unsigned i = 0; i < ALL_COUNT; i++) {
    const bool sized = ALL[i] == AP_M68030_EA_POSTINCREMENT ||
                       ALL[i] == AP_M68030_EA_PREDECREMENT ||
                       ALL[i] == AP_M68030_EA_IMMEDIATE;
    const bool expected = ap_m68030_ea_is_memory(ALL[i]) && !sized;
    TEST_ASSERT_EQUAL_INT(expected, ap_m68030_ea_is_control(ALL[i]));
  }
}

/* "Alterable addressing modes refer to alterable (writable) operands." Nothing
 * PC-relative is writable and neither is an immediate.
 *
 * This is the column the scan loses, and it loses it in a way that is *not* a
 * plausible reading: the extraction gives absolute addressing `—` and PC memory
 * indirect `X`, which is the truth of both rows exchanged. */
static void test_the_unwritable_modes_are_pc_relative_and_immediate(void) {
  for (unsigned i = 0; i < ALL_COUNT; i++) {
    const bool expected = ALL[i] != AP_M68030_EA_PC_DISPLACEMENT &&
                          ALL[i] != AP_M68030_EA_PC_INDEXED &&
                          ALL[i] != AP_M68030_EA_IMMEDIATE;
    TEST_ASSERT_EQUAL_INT(expected, ap_m68030_ea_is_alterable(ALL[i]));
  }
}

/* An independent check on the Alterable column, from a different manual page:
 * `MOVE`'s destination must be data alterable, and `MOVE.W D0,$1234` is a legal
 * instruction that every assembler emits. So absolute addressing *is*
 * alterable, whatever the scan of Table 2-4 shows. */
static void test_a_move_destination_admits_absolute_and_refuses_pc_relative(
    void) {
  TEST_ASSERT_TRUE(ap_m68030_ea_is_data_alterable(AP_M68030_EA_ABSOLUTE_SHORT));
  TEST_ASSERT_TRUE(ap_m68030_ea_is_data_alterable(AP_M68030_EA_ABSOLUTE_LONG));

  /* And nothing PC-relative is a legal MOVE destination, which is the other
   * half of the exchanged pair. */
  TEST_ASSERT_FALSE(
      ap_m68030_ea_is_data_alterable(AP_M68030_EA_PC_DISPLACEMENT));
  TEST_ASSERT_FALSE(ap_m68030_ea_is_data_alterable(AP_M68030_EA_PC_INDEXED));

  /* An address register is alterable but not data, so MOVEA exists for it. */
  TEST_ASSERT_FALSE(
      ap_m68030_ea_is_data_alterable(AP_M68030_EA_ADDRESS_REGISTER));
  TEST_ASSERT_TRUE(ap_m68030_ea_is_alterable(AP_M68030_EA_ADDRESS_REGISTER));
}

/* A second independent check, from the MMU instruction pages: "Only control
 * alterable addressing modes can be used", and each page's own table lists
 * exactly `(An)`, `(d16,An)`, the indexed forms, `(xxx).W` and `(xxx).L` --
 * with every PC-relative row, both increment modes, the register direct modes
 * and the immediate marked absent. */
static void test_control_alterable_is_what_the_mmu_instruction_pages_list(void) {
  const ap_m68030_ea_kind_t allowed[] = {
      AP_M68030_EA_ADDRESS_INDIRECT, AP_M68030_EA_DISPLACEMENT,
      AP_M68030_EA_INDEXED,          AP_M68030_EA_ABSOLUTE_SHORT,
      AP_M68030_EA_ABSOLUTE_LONG,
  };
  for (unsigned i = 0; i < sizeof allowed / sizeof allowed[0]; i++) {
    TEST_ASSERT_TRUE(ap_m68030_ea_is_control_alterable(allowed[i]));
  }

  const ap_m68030_ea_kind_t refused[] = {
      AP_M68030_EA_DATA_REGISTER,   AP_M68030_EA_ADDRESS_REGISTER,
      AP_M68030_EA_POSTINCREMENT,   AP_M68030_EA_PREDECREMENT,
      AP_M68030_EA_PC_DISPLACEMENT, AP_M68030_EA_PC_INDEXED,
      AP_M68030_EA_IMMEDIATE,
  };
  for (unsigned i = 0; i < sizeof refused / sizeof refused[0]; i++) {
    TEST_ASSERT_FALSE(ap_m68030_ea_is_control_alterable(refused[i]));
  }
}

/* `LEA`'s source must be control -- it loads an address, so there must be an
 * address and no size attached to it. The two increment modes are the
 * interesting exclusions: `(An)` is legal and `(An)+` is not, which is a
 * distinction only the control category makes. */
static void test_lea_takes_control_modes_and_so_excludes_the_increments(void) {
  TEST_ASSERT_TRUE(ap_m68030_ea_is_control(AP_M68030_EA_ADDRESS_INDIRECT));
  TEST_ASSERT_FALSE(ap_m68030_ea_is_control(AP_M68030_EA_POSTINCREMENT));
  TEST_ASSERT_FALSE(ap_m68030_ea_is_control(AP_M68030_EA_PREDECREMENT));

  /* But LEA *does* take the PC-relative modes, which is what makes
   * position-independent code possible -- and is why its category is control
   * rather than control alterable. */
  TEST_ASSERT_TRUE(ap_m68030_ea_is_control(AP_M68030_EA_PC_DISPLACEMENT));
  TEST_ASSERT_TRUE(ap_m68030_ea_is_control(AP_M68030_EA_PC_INDEXED));
}

/* An invalid mode belongs to no category. Answering "yes" for any of them would
 * let a mode 111 sub-opcode the processor does not assign pass a legality
 * check, which is the one place these functions are load-bearing. */
static void test_an_invalid_mode_is_in_no_category(void) {
  TEST_ASSERT_FALSE(ap_m68030_ea_is_data(AP_M68030_EA_INVALID));
  TEST_ASSERT_FALSE(ap_m68030_ea_is_memory(AP_M68030_EA_INVALID));
  TEST_ASSERT_FALSE(ap_m68030_ea_is_control(AP_M68030_EA_INVALID));
  TEST_ASSERT_FALSE(ap_m68030_ea_is_alterable(AP_M68030_EA_INVALID));
  TEST_ASSERT_FALSE(ap_m68030_ea_is_data_alterable(AP_M68030_EA_INVALID));
  TEST_ASSERT_FALSE(ap_m68030_ea_is_memory_alterable(AP_M68030_EA_INVALID));
  TEST_ASSERT_FALSE(ap_m68030_ea_is_control_alterable(AP_M68030_EA_INVALID));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_only_an_address_register_is_not_a_data_operand);
  RUN_TEST(test_the_register_direct_modes_are_the_only_non_memory_ones);
  RUN_TEST(test_control_is_memory_without_an_associated_size);
  RUN_TEST(test_the_unwritable_modes_are_pc_relative_and_immediate);
  RUN_TEST(test_a_move_destination_admits_absolute_and_refuses_pc_relative);
  RUN_TEST(test_control_alterable_is_what_the_mmu_instruction_pages_list);
  RUN_TEST(test_lea_takes_control_modes_and_so_excludes_the_increments);
  RUN_TEST(test_an_invalid_mode_is_in_no_category);
  return UNITY_END();
}
