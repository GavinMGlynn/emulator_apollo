/* MC68851 instruction decode, `[68851]` Appendix A, read from the page images.
 *
 * The extracted text of these bit rows is unusable -- zeros come out as letters
 * and columns collapse -- so every field position here comes from a rendered
 * page, one per instruction. That mattered more than usual: the command word's
 * opclass `001` carries four different instructions, and reading only
 * `PFLUSH`'s page makes three of its eight mode values look undefined.
 */

#include "cpu/m68851/ap_m68851_decode.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Assemble a command word from opclass, the three bits below it, the R/W bit
 * and the low nine, so the tests read as encodings rather than hex. */
static uint16_t command_word(unsigned opclass, unsigned field, bool rw,
                             unsigned low) {
  return (uint16_t)((opclass << 13) | (field << 10) | ((rw ? 1u : 0u) << 9) |
                    (low & 0x1FFu));
}

/* ---------------------------------------------------------------------------
 * The operation word.
 * ------------------------------------------------------------------------- */

static void test_the_mmu_is_coprocessor_zero(void) {
  /* `1111 000 000` then an effective address, so the F-line decoder routes by
   * cpID exactly as it routes cpID 1 to the 68882. */
  TEST_ASSERT_TRUE(ap_m68851_is_mmu_operation_word(0xF000u));
  TEST_ASSERT_TRUE(ap_m68851_is_mmu_operation_word(0xF03Fu)); /* any ea */
  /* cpID 1 is the floating-point unit, not this part. */
  TEST_ASSERT_FALSE(ap_m68851_is_mmu_operation_word(0xF200u));
  /* Type field non-zero is a conditional or a save/restore, not this form. */
  TEST_ASSERT_FALSE(ap_m68851_is_mmu_operation_word(0xF040u));
  TEST_ASSERT_FALSE(ap_m68851_is_mmu_operation_word(0x4E71u)); /* NOP */
}

/* ---------------------------------------------------------------------------
 * The function code specification field.
 * ------------------------------------------------------------------------- */

static void test_the_immediate_form_carries_four_bits(void) {
  for (unsigned fc = 0; fc < 16u; fc++) {
    const ap_m68851_fc_spec_t spec = ap_m68851_decode_fc(0x10u | fc);
    TEST_ASSERT_EQUAL_INT(AP_M68851_FC_IMMEDIATE, spec.source);
    TEST_ASSERT_EQUAL_UINT(fc, spec.immediate);
  }
}

static void test_the_data_register_form_names_a_register(void) {
  for (unsigned reg = 0; reg < 8u; reg++) {
    const ap_m68851_fc_spec_t spec = ap_m68851_decode_fc(0x08u | reg);
    TEST_ASSERT_EQUAL_INT(AP_M68851_FC_DATA_REGISTER, spec.source);
    TEST_ASSERT_EQUAL_UINT(reg, spec.data_register);
  }
}

static void test_the_field_is_a_prefix_code(void) {
  /* `00000` is the SFC form and `01000` is data register 0. A decoder that
   * tested the top bit, then the next, then read the remainder as a register
   * would map `00000` to register 0 -- a different instruction. */
  TEST_ASSERT_EQUAL_INT(AP_M68851_FC_SFC, ap_m68851_decode_fc(0x00u).source);
  TEST_ASSERT_EQUAL_INT(AP_M68851_FC_DFC, ap_m68851_decode_fc(0x01u).source);
  const ap_m68851_fc_spec_t r0 = ap_m68851_decode_fc(0x08u);
  TEST_ASSERT_EQUAL_INT(AP_M68851_FC_DATA_REGISTER, r0.source);
  TEST_ASSERT_EQUAL_UINT(0u, r0.data_register);
}

static void test_every_field_value_decodes(void) {
  /* All 32, each to exactly one source. Sweeping rather than sampling, because
   * the prefix boundaries are where this goes wrong. */
  unsigned counts[5] = {0};
  for (unsigned field = 0; field < 32u; field++) {
    counts[ap_m68851_decode_fc(field).source]++;
  }
  TEST_ASSERT_EQUAL_UINT(16u, counts[AP_M68851_FC_IMMEDIATE]);
  TEST_ASSERT_EQUAL_UINT(8u, counts[AP_M68851_FC_DATA_REGISTER]);
  TEST_ASSERT_EQUAL_UINT(1u, counts[AP_M68851_FC_SFC]);
  TEST_ASSERT_EQUAL_UINT(1u, counts[AP_M68851_FC_DFC]);
  TEST_ASSERT_EQUAL_UINT(6u, counts[AP_M68851_FC_UNDEFINED]);
}

static void test_only_the_immediate_form_can_name_a_dma_function_code(void) {
  /* "Since the SFC of the MC68020 has only three implemented bits, only
   * function codes $0 through $7 can be specified in this manner." So an
   * instruction using a CPU register form cannot address DMA entries. */
  const ap_m68851_fc_spec_t dma = ap_m68851_decode_fc(0x18u);
  TEST_ASSERT_TRUE(ap_m68851_fc_reaches_dma(&dma));
  const ap_m68851_fc_spec_t cpu = ap_m68851_decode_fc(0x15u);
  const ap_m68851_fc_spec_t sfc = ap_m68851_decode_fc(0x00u);
  const ap_m68851_fc_spec_t reg = ap_m68851_decode_fc(0x0Fu);
  TEST_ASSERT_FALSE(ap_m68851_fc_reaches_dma(&cpu));
  TEST_ASSERT_FALSE(ap_m68851_fc_reaches_dma(&sfc));
  TEST_ASSERT_FALSE(ap_m68851_fc_reaches_dma(&reg));
}

/* ---------------------------------------------------------------------------
 * Opclass `001`: four instructions in one class.
 * ------------------------------------------------------------------------- */

static void test_opclass_001_holds_four_different_instructions(void) {
  /* The whole point of this module. All eight mode values are used, by three
   * instructions -- so a decoder built from `PFLUSH`'s page alone would reject
   * `PLOAD` and both `PVALID` forms as undefined. */
  const struct { unsigned mode; ap_m68851_opcode_t opcode; } modes[] = {
      {0u, AP_M68851_OP_PLOAD},  {1u, AP_M68851_OP_PFLUSH},
      {2u, AP_M68851_OP_PVALID}, {3u, AP_M68851_OP_PVALID},
      {4u, AP_M68851_OP_PFLUSH}, {5u, AP_M68851_OP_PFLUSH},
      {6u, AP_M68851_OP_PFLUSH}, {7u, AP_M68851_OP_PFLUSH},
  };
  for (unsigned i = 0; i < 8u; i++) {
    const ap_m68851_instruction_t d =
        ap_m68851_decode_command(command_word(1u, modes[i].mode, false, 0u));
    TEST_ASSERT_EQUAL_INT(modes[i].opcode, d.opcode);
  }
}

static void test_pload_carries_a_direction_and_a_function_code(void) {
  /* `001 | 000 | R/W | 0000 | FC`. "PLOADR causes U bits ... to be updated as
   * if a read access had taken place. PLOADW causes U and M bits ... as if a
   * write access had taken place" -- so the bit is not cosmetic, it decides
   * which table bits the search writes back. */
  const ap_m68851_instruction_t r =
      ap_m68851_decode_command(command_word(1u, 0u, true, 0x15u));
  TEST_ASSERT_EQUAL_INT(AP_M68851_OP_PLOAD, r.opcode);
  TEST_ASSERT_TRUE(r.read_from_mmu);
  TEST_ASSERT_EQUAL_INT(AP_M68851_FC_IMMEDIATE, r.fc.source);
  TEST_ASSERT_EQUAL_UINT(0x5u, r.fc.immediate);

  const ap_m68851_instruction_t w =
      ap_m68851_decode_command(command_word(1u, 0u, false, 0x15u));
  TEST_ASSERT_FALSE(w.read_from_mmu);
}

static void test_the_four_pflush_modes_and_flush_all(void) {
  const struct { unsigned mode; ap_m68851_pflush_mode_t expected; } cases[] = {
      {1u, AP_M68851_PFLUSH_ALL},
      {4u, AP_M68851_PFLUSH_FC},
      {5u, AP_M68851_PFLUSH_FC_SHARED},
      {6u, AP_M68851_PFLUSH_FC_EA},
      {7u, AP_M68851_PFLUSH_FC_EA_SHARED},
  };
  for (unsigned i = 0; i < 5u; i++) {
    const ap_m68851_instruction_t d =
        ap_m68851_decode_command(command_word(1u, cases[i].mode, false, 0u));
    TEST_ASSERT_EQUAL_INT(AP_M68851_OP_PFLUSH, d.opcode);
    TEST_ASSERT_EQUAL_INT(cases[i].expected, d.pflush_mode);
  }
}

static void test_the_pflush_mask_sits_at_bits_8_to_5(void) {
  const ap_m68851_instruction_t d =
      ap_m68851_decode_command(command_word(1u, 6u, false, 0x1E0u | 0x15u));
  TEST_ASSERT_EQUAL_UINT(0xFu, d.mask);
  TEST_ASSERT_EQUAL_UINT(0x5u, d.fc.immediate);
}

static void test_a_flush_all_must_have_a_zero_mask_and_function_code(void) {
  /* "If mode = 001 (flush all entries), mask must be 0000" and "function code
   * must be 00000". A flush-all naming a function code contradicts itself. */
  const ap_m68851_instruction_t good =
      ap_m68851_decode_command(command_word(1u, 1u, false, 0u));
  TEST_ASSERT_TRUE(ap_m68851_instruction_is_valid(&good));

  const ap_m68851_instruction_t bad_mask =
      ap_m68851_decode_command(command_word(1u, 1u, false, 0x20u));
  TEST_ASSERT_FALSE(ap_m68851_instruction_is_valid(&bad_mask));

  const ap_m68851_instruction_t bad_fc =
      ap_m68851_decode_command(command_word(1u, 1u, false, 0x15u));
  TEST_ASSERT_FALSE(ap_m68851_instruction_is_valid(&bad_fc));
}

static void test_the_other_flush_modes_accept_any_mask_and_function_code(void) {
  /* The constraint belongs to flush-all alone; a flush by function code is
   * supposed to name one. */
  const unsigned modes[] = {4u, 5u, 6u, 7u};
  for (unsigned i = 0; i < 4u; i++) {
    const ap_m68851_instruction_t d =
        ap_m68851_decode_command(command_word(1u, modes[i], false, 0x1F5u));
    TEST_ASSERT_TRUE(ap_m68851_instruction_is_valid(&d));
  }
}

static void test_only_the_shared_modes_flush_shared_entries(void) {
  /* "ATC entries whose SG bit is set will not be invalidated unless the
   * PFLUSHS is specified" -- surviving an ordinary flush is the point of
   * sharing an entry. */
  TEST_ASSERT_FALSE(ap_m68851_pflush_includes_shared(AP_M68851_PFLUSH_FC));
  TEST_ASSERT_FALSE(ap_m68851_pflush_includes_shared(AP_M68851_PFLUSH_FC_EA));
  TEST_ASSERT_TRUE(
      ap_m68851_pflush_includes_shared(AP_M68851_PFLUSH_FC_SHARED));
  TEST_ASSERT_TRUE(
      ap_m68851_pflush_includes_shared(AP_M68851_PFLUSH_FC_EA_SHARED));
}

static void test_only_the_address_modes_match_on_the_address(void) {
  TEST_ASSERT_FALSE(ap_m68851_pflush_uses_address(AP_M68851_PFLUSH_FC));
  TEST_ASSERT_FALSE(ap_m68851_pflush_uses_address(AP_M68851_PFLUSH_FC_SHARED));
  TEST_ASSERT_TRUE(ap_m68851_pflush_uses_address(AP_M68851_PFLUSH_FC_EA));
  TEST_ASSERT_TRUE(
      ap_m68851_pflush_uses_address(AP_M68851_PFLUSH_FC_EA_SHARED));
}

static void test_the_mask_makes_a_flush_name_a_set_of_function_codes(void) {
  /* "(ATC function code bits and <mask>) = (<fc> and <mask>)", with "a zero
   * indicates that the bit position is not significant". */
  for (unsigned entry = 0; entry < 16u; entry++) {
    TEST_ASSERT_TRUE(ap_m68851_pflush_matches_fc(0x0u, 0x5u, entry));
    TEST_ASSERT_EQUAL_INT(entry == 0x5u,
                          ap_m68851_pflush_matches_fc(0xFu, 0x5u, entry));
  }
  /* One significant bit: eight function codes at once. */
  TEST_ASSERT_TRUE(ap_m68851_pflush_matches_fc(0x8u, 0x5u, 0x1u));
  TEST_ASSERT_FALSE(ap_m68851_pflush_matches_fc(0x8u, 0x5u, 0x9u));
}

static void test_the_two_pvalid_forms(void) {
  /* Mode `010` tests against `VAL`; mode `011` tests against a main processor
   * address register named in the low three bits. One instruction to the
   * assembler, two encodings here. */
  const ap_m68851_instruction_t against_val =
      ap_m68851_decode_command(command_word(1u, 2u, false, 0u));
  TEST_ASSERT_EQUAL_INT(AP_M68851_OP_PVALID, against_val.opcode);
  TEST_ASSERT_FALSE(against_val.valid_against_register);

  const ap_m68851_instruction_t against_an =
      ap_m68851_decode_command(command_word(1u, 3u, false, 5u));
  TEST_ASSERT_EQUAL_INT(AP_M68851_OP_PVALID, against_an.opcode);
  TEST_ASSERT_TRUE(against_an.valid_against_register);
  TEST_ASSERT_EQUAL_UINT(5u, against_an.valid_register);
}

/* ---------------------------------------------------------------------------
 * PMOVE, opclasses `010` and `011`.
 * ------------------------------------------------------------------------- */

static void test_the_eight_translation_and_protection_registers(void) {
  /* `010 | PReg | R/W | 000000000`, with PReg running TC, DRP, SRP, CRP, CAL,
   * VAL, SCC, AC. Note that the root pointers are *not* in numeric order with
   * CRP first -- DRP is 001 and CRP is 011. */
  const ap_m68851_preg_t expected[8] = {
      AP_M68851_PREG_TC,  AP_M68851_PREG_DRP, AP_M68851_PREG_SRP,
      AP_M68851_PREG_CRP, AP_M68851_PREG_CAL, AP_M68851_PREG_VAL,
      AP_M68851_PREG_SCC, AP_M68851_PREG_AC};
  for (unsigned reg = 0; reg < 8u; reg++) {
    const ap_m68851_instruction_t d =
        ap_m68851_decode_command(command_word(2u, reg, false, 0u));
    TEST_ASSERT_EQUAL_INT(AP_M68851_OP_PMOVE, d.opcode);
    TEST_ASSERT_EQUAL_INT(expected[reg], d.preg);
  }
}

static void test_the_pmove_direction_bit(void) {
  /* "0 -- Transfer <ea> to MC68851 register; 1 -- Transfer MC68851 register to
   * <ea>." */
  TEST_ASSERT_TRUE(
      ap_m68851_decode_command(command_word(2u, 0u, true, 0u)).read_from_mmu);
  TEST_ASSERT_FALSE(
      ap_m68851_decode_command(command_word(2u, 0u, false, 0u)).read_from_mmu);
}

static void test_opclass_011_holds_the_status_and_breakpoint_registers(void) {
  /* Two `PMOVE` formats in one opclass, told apart by `PReg`: `000`/`001` are
   * PSR and PCSR, `100`/`101` the breakpoint registers. */
  TEST_ASSERT_EQUAL_INT(
      AP_M68851_PREG_PSR,
      ap_m68851_decode_command(command_word(3u, 0u, false, 0u)).preg);
  TEST_ASSERT_EQUAL_INT(
      AP_M68851_PREG_PCSR,
      ap_m68851_decode_command(command_word(3u, 1u, true, 0u)).preg);
  TEST_ASSERT_EQUAL_INT(
      AP_M68851_PREG_BAD,
      ap_m68851_decode_command(command_word(3u, 4u, false, 0u)).preg);
  TEST_ASSERT_EQUAL_INT(
      AP_M68851_PREG_BAC,
      ap_m68851_decode_command(command_word(3u, 5u, false, 0u)).preg);
}

static void test_the_breakpoint_number_sits_at_bits_4_to_2(void) {
  /* `011 | PReg | R/W | 0000 | Num | 00`: the low two bits are zero, so the
   * number is not simply the bottom of the word. */
  const ap_m68851_instruction_t d =
      ap_m68851_decode_command(command_word(3u, 4u, false, 0x14u));
  TEST_ASSERT_EQUAL_INT(AP_M68851_PREG_BAD, d.preg);
  TEST_ASSERT_EQUAL_UINT(5u, d.breakpoint_number);
}

static void test_the_unused_opclass_011_register_values_are_undefined(void) {
  /* `010`, `011`, `110` and `111` name no register in either format. */
  const unsigned unused[] = {2u, 3u, 6u, 7u};
  for (unsigned i = 0; i < 4u; i++) {
    TEST_ASSERT_EQUAL_INT(
        AP_M68851_OP_UNDEFINED,
        ap_m68851_decode_command(command_word(3u, unused[i], false, 0u)).opcode);
  }
}

static void test_only_the_root_pointers_are_64_bit(void) {
  /* Appendix A footnotes the addressing table: "PMOVE from CRP, SRP, DRP not
   * allowed with these modes" -- the register-direct ones -- because they are
   * 64 bits and will not fit in one CPU register. */
  TEST_ASSERT_TRUE(ap_m68851_preg_is_64_bit(AP_M68851_PREG_CRP));
  TEST_ASSERT_TRUE(ap_m68851_preg_is_64_bit(AP_M68851_PREG_SRP));
  TEST_ASSERT_TRUE(ap_m68851_preg_is_64_bit(AP_M68851_PREG_DRP));
  TEST_ASSERT_FALSE(ap_m68851_preg_is_64_bit(AP_M68851_PREG_TC));
  TEST_ASSERT_FALSE(ap_m68851_preg_is_64_bit(AP_M68851_PREG_AC));
  TEST_ASSERT_FALSE(ap_m68851_preg_is_64_bit(AP_M68851_PREG_PSR));
}

/* ---------------------------------------------------------------------------
 * PTEST, opclass `100`.
 * ------------------------------------------------------------------------- */

static void test_the_ptest_fields(void) {
  /* `100 | Level | R/W | AReg | FC`. The three bits below the opclass are a
   * *level* here where PMOVE reads them as a register -- the same position,
   * a different meaning, decided by the opclass alone. */
  const ap_m68851_instruction_t d =
      ap_m68851_decode_command(command_word(4u, 7u, true, 0x1E0u | 0x15u));
  TEST_ASSERT_EQUAL_INT(AP_M68851_OP_PTEST, d.opcode);
  TEST_ASSERT_EQUAL_UINT(7u, d.level);
  TEST_ASSERT_TRUE(d.read_from_mmu);
  TEST_ASSERT_EQUAL_UINT(0xFu, d.address_register);
  TEST_ASSERT_EQUAL_UINT(0x5u, d.fc.immediate);
}

static void test_a_ptest_level_of_zero_searches_only_the_atc(void) {
  /* §6.1.8 draws the distinction throughout: "for the PTEST instruction with a
   * level specification of zero" the status bits report what the ATC held,
   * and several are always clear because no table was walked. The level is
   * three bits, so zero through seven. */
  for (unsigned level = 0; level < 8u; level++) {
    const ap_m68851_instruction_t d =
        ap_m68851_decode_command(command_word(4u, level, false, 0u));
    TEST_ASSERT_EQUAL_INT(AP_M68851_OP_PTEST, d.opcode);
    TEST_ASSERT_EQUAL_UINT(level, d.level);
  }
}

static void test_the_unused_opclasses_are_undefined(void) {
  /* `000`, `101`, `110` and `111` name no instruction in Appendix A. */
  const unsigned unused[] = {0u, 5u, 6u, 7u};
  for (unsigned i = 0; i < 4u; i++) {
    TEST_ASSERT_EQUAL_INT(
        AP_M68851_OP_UNDEFINED,
        ap_m68851_decode_command(command_word(unused[i], 0u, false, 0u)).opcode);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_mmu_is_coprocessor_zero);
  RUN_TEST(test_the_immediate_form_carries_four_bits);
  RUN_TEST(test_the_data_register_form_names_a_register);
  RUN_TEST(test_the_field_is_a_prefix_code);
  RUN_TEST(test_every_field_value_decodes);
  RUN_TEST(test_only_the_immediate_form_can_name_a_dma_function_code);
  RUN_TEST(test_opclass_001_holds_four_different_instructions);
  RUN_TEST(test_pload_carries_a_direction_and_a_function_code);
  RUN_TEST(test_the_four_pflush_modes_and_flush_all);
  RUN_TEST(test_the_pflush_mask_sits_at_bits_8_to_5);
  RUN_TEST(test_a_flush_all_must_have_a_zero_mask_and_function_code);
  RUN_TEST(test_the_other_flush_modes_accept_any_mask_and_function_code);
  RUN_TEST(test_only_the_shared_modes_flush_shared_entries);
  RUN_TEST(test_only_the_address_modes_match_on_the_address);
  RUN_TEST(test_the_mask_makes_a_flush_name_a_set_of_function_codes);
  RUN_TEST(test_the_two_pvalid_forms);
  RUN_TEST(test_the_eight_translation_and_protection_registers);
  RUN_TEST(test_the_pmove_direction_bit);
  RUN_TEST(test_opclass_011_holds_the_status_and_breakpoint_registers);
  RUN_TEST(test_the_breakpoint_number_sits_at_bits_4_to_2);
  RUN_TEST(test_the_unused_opclass_011_register_values_are_undefined);
  RUN_TEST(test_only_the_root_pointers_are_64_bit);
  RUN_TEST(test_the_ptest_fields);
  RUN_TEST(test_a_ptest_level_of_zero_searches_only_the_atc);
  RUN_TEST(test_the_unused_opclasses_are_undefined);
  return UNITY_END();
}
