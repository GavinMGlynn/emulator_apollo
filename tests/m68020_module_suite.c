/* MC68020 module call and return, `[68020]` Appendix D and the PRM's CALLM and
 * RTM pages.
 *
 * The formats here were read from the page images of Figures D-1, D-2 and D-3.
 * That mattered: the extracted text of Figure D-2 had lost bit 15, so the D/A
 * bit and the register field would both have been placed one column out.
 */

#include "cpu/m68020/ap_m68020_module.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* CALLM and RTM share ten bits of opcode and are told apart by the effective
 * address field -- using values CALLM is forbidden to use. A decoder that
 * checked only the prefix would run one as the other. */
static void test_the_two_instructions_share_an_opcode_prefix(void) {
  TEST_ASSERT_EQUAL_UINT16(AP_M68020_CALLM_BASE, AP_M68020_RTM_BASE);
}

static void test_rtm_occupies_the_register_direct_modes(void) {
  /* `0000 0110 1100 DRRR`: sixteen words, the eight data registers and the
   * eight address registers. */
  for (unsigned reg = 0; reg < 8u; reg++) {
    const ap_m68020_module_decode_t data =
        ap_m68020_module_decode((uint16_t)(AP_M68020_RTM_BASE | reg));
    TEST_ASSERT_EQUAL_INT(AP_M68020_MODULE_RTM, data.opcode);
    TEST_ASSERT_FALSE(data.rtm_address_register);
    TEST_ASSERT_EQUAL_UINT(reg, data.rtm_register);

    const ap_m68020_module_decode_t address =
        ap_m68020_module_decode((uint16_t)(AP_M68020_RTM_BASE | 8u | reg));
    TEST_ASSERT_EQUAL_INT(AP_M68020_MODULE_RTM, address.opcode);
    TEST_ASSERT_TRUE(address.rtm_address_register);
    TEST_ASSERT_EQUAL_UINT(reg, address.rtm_register);
  }
}

static void test_callm_takes_only_control_addressing_modes(void) {
  /* CALLM's table dashes out `Dn`, `An`, `(An)+`, `-(An)` and `#<data>`.
   * Postincrement and predecrement are the ones a careless decoder would let
   * through, because they are neither register direct nor obviously wrong. */
  const struct {
    unsigned mode;
    unsigned reg;
    bool legal;
  } cases[] = {
      {2u, 3u, true},  /* (An) */
      {3u, 3u, false}, /* (An)+ -- dashed out */
      {4u, 3u, false}, /* -(An) -- dashed out */
      {5u, 3u, true},  /* (d16,An) */
      {6u, 3u, true},  /* (d8,An,Xn) and the full formats */
      {7u, 0u, true},  /* (xxx).W */
      {7u, 1u, true},  /* (xxx).L */
      {7u, 2u, true},  /* (d16,PC) */
      {7u, 3u, true},  /* (d8,PC,Xn) */
      {7u, 4u, false}, /* #<data> -- dashed out */
  };

  for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    const uint16_t word =
        (uint16_t)(AP_M68020_CALLM_BASE | (cases[i].mode << 3) | cases[i].reg);
    const ap_m68020_module_decode_t decoded = ap_m68020_module_decode(word);
    if (cases[i].legal) {
      TEST_ASSERT_EQUAL_INT(AP_M68020_MODULE_CALLM, decoded.opcode);
      TEST_ASSERT_EQUAL_UINT(cases[i].mode, decoded.mode);
      TEST_ASSERT_EQUAL_UINT(cases[i].reg, decoded.reg);
    } else {
      /* Not a format error: the processor never reaches a descriptor. */
      TEST_ASSERT_EQUAL_INT(AP_M68020_MODULE_NOT_A_MODULE_INSTRUCTION,
                            decoded.opcode);
    }
  }
}

static void test_a_word_outside_the_prefix_is_not_a_module_instruction(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68020_MODULE_NOT_A_MODULE_INSTRUCTION,
                        ap_m68020_module_decode(0x4E71u).opcode); /* NOP */
  /* One bit below the prefix: `0000 0110 10...` is ADDI.L, a real instruction
   * that must not be mistaken for a module call. */
  TEST_ASSERT_EQUAL_INT(AP_M68020_MODULE_NOT_A_MODULE_INSTRUCTION,
                        ap_m68020_module_decode(0x0680u).opcode);
}

static void test_the_descriptor_control_fields(void) {
  /* Figure D-1: Opt 31-29, Type 28-24, Access Level 23-16. */
  const ap_m68020_module_control_t control =
      ap_m68020_module_control(0xA1AB0000u);
  TEST_ASSERT_EQUAL_UINT(5u, control.opt);     /* 101 */
  TEST_ASSERT_EQUAL_UINT(0x01u, control.type);
  TEST_ASSERT_EQUAL_UINT(0xABu, control.access_level);
}

static void test_the_reserved_half_of_the_descriptor_reaches_no_field(void) {
  /* "(Reserved, Must be Zero)" -- bits 15-0. A model that let them into the
   * access level would pass rubbish to external hardware. */
  const ap_m68020_module_control_t zero = ap_m68020_module_control(0x00000000u);
  const ap_m68020_module_control_t noisy =
      ap_m68020_module_control(0x0000FFFFu);
  TEST_ASSERT_EQUAL_UINT(zero.opt, noisy.opt);
  TEST_ASSERT_EQUAL_UINT(zero.type, noisy.type);
  TEST_ASSERT_EQUAL_UINT(zero.access_level, noisy.access_level);
}

static void test_only_types_00_and_01_are_recognised(void) {
  /* "The MC68020 only recognizes descriptors of type $00 and $01, all others
   * cause a format exception." */
  for (unsigned type = 0; type < 0x20u; type++) {
    const ap_m68020_module_control_t control = {
        .opt = AP_M68020_MODULE_OPT_ON_STACK, .type = type, .access_level = 0};
    const ap_m68020_module_status_t status = ap_m68020_module_validate(&control);
    if (type <= 1u) {
      TEST_ASSERT_EQUAL_INT(AP_M68020_MODULE_OK, status);
    } else if (type >= 0x10u) {
      TEST_ASSERT_EQUAL_INT(AP_M68020_MODULE_DISABLED, status);
    } else {
      TEST_ASSERT_EQUAL_INT(AP_M68020_MODULE_BAD_TYPE, status);
    }
  }
}

static void test_the_disable_range_is_a_single_bit(void) {
  /* "This provides the user with a means of disabling any module by setting a
   * single bit in its descriptor, without loss of any descriptor information."
   * So setting bit 4 of the type disables a module and clearing it restores it
   * exactly -- which is only true if bit 4 alone decides. */
  for (unsigned type = 0; type < 0x10u; type++) {
    const ap_m68020_module_control_t enabled = {
        .opt = AP_M68020_MODULE_OPT_ON_STACK, .type = type, .access_level = 0};
    const ap_m68020_module_control_t disabled = {
        .opt = AP_M68020_MODULE_OPT_ON_STACK,
        .type = type | 0x10u,
        .access_level = 0};
    TEST_ASSERT_EQUAL_INT(AP_M68020_MODULE_DISABLED,
                          ap_m68020_module_validate(&disabled));
    TEST_ASSERT_NOT_EQUAL_INT(AP_M68020_MODULE_DISABLED,
                              ap_m68020_module_validate(&enabled));
  }
}

static void test_only_options_000_and_100_are_recognised(void) {
  /* "The MC68020 recognizes only the options of 000 and 100, all others cause a
   * format exception." Note 100 and not 001: the recognised pair is not the two
   * smallest values, which is exactly the sort of thing a model gets wrong. */
  for (unsigned opt = 0; opt < 8u; opt++) {
    const ap_m68020_module_control_t control = {
        .opt = opt,
        .type = AP_M68020_MODULE_TYPE_NO_ACCESS_CHANGE,
        .access_level = 0};
    const ap_m68020_module_status_t status = ap_m68020_module_validate(&control);
    if (opt == 0u || opt == 4u) {
      TEST_ASSERT_EQUAL_INT(AP_M68020_MODULE_OK, status);
    } else {
      TEST_ASSERT_EQUAL_INT(AP_M68020_MODULE_BAD_OPT, status);
    }
  }
}

static void test_only_a_type_01_descriptor_changes_access(void) {
  /* "The access level field is used only with the type $01 descriptor." A type
   * $00 call runs no access-level bus cycles, which on a machine with no
   * access-control hardware is the difference between working and bus erroring. */
  const ap_m68020_module_control_t plain = {
      .opt = 0, .type = AP_M68020_MODULE_TYPE_NO_ACCESS_CHANGE, .access_level = 7};
  const ap_m68020_module_control_t privileged = {
      .opt = 0, .type = AP_M68020_MODULE_TYPE_ACCESS_CHANGE, .access_level = 7};
  TEST_ASSERT_FALSE(ap_m68020_module_changes_access(&plain));
  TEST_ASSERT_TRUE(ap_m68020_module_changes_access(&privileged));
}

static void test_arguments_are_copied_only_for_option_000_with_a_stack_change(void) {
  const ap_m68020_module_control_t on_stack = {
      .opt = AP_M68020_MODULE_OPT_ON_STACK, .type = 0, .access_level = 0};
  const ap_m68020_module_control_t indirect = {
      .opt = AP_M68020_MODULE_OPT_INDIRECT, .type = 0, .access_level = 0};

  TEST_ASSERT_TRUE(ap_m68020_module_copies_arguments(&on_stack, true));
  /* No stack change: the arguments are already where the callee will look. */
  TEST_ASSERT_FALSE(ap_m68020_module_copies_arguments(&on_stack, false));
  /* "Hence, the arguments are not copied" -- whatever the stack does. */
  TEST_ASSERT_FALSE(ap_m68020_module_copies_arguments(&indirect, true));
  TEST_ASSERT_FALSE(ap_m68020_module_copies_arguments(&indirect, false));
}

static void test_the_module_entry_word_fields(void) {
  /* Figure D-2 from the page image: D/A at bit 15, Register at 14-12. */
  const ap_m68020_module_entry_t data = ap_m68020_module_entry(0x3000u);
  TEST_ASSERT_FALSE(data.address_register);
  TEST_ASSERT_EQUAL_UINT(3u, data.reg);

  const ap_m68020_module_entry_t address = ap_m68020_module_entry(0xB000u);
  TEST_ASSERT_TRUE(address.address_register);
  TEST_ASSERT_EQUAL_UINT(3u, address.reg);
}

static void test_the_entry_words_low_twelve_bits_reach_no_field(void) {
  /* Bits 11-0 are drawn as zeros, and the operation word of the first
   * instruction is the *next* word rather than these bits. */
  TEST_ASSERT_EQUAL_UINT(ap_m68020_module_entry(0xB000u).reg,
                         ap_m68020_module_entry(0xBFFFu).reg);
  TEST_ASSERT_TRUE(ap_m68020_module_entry(0xBFFFu).address_register);
}

static void test_the_entry_word_can_select_a7(void) {
  /* "If the called module does not wish the module data area pointer to be
   * loaded into a register, the module entry word can select register A7, and
   * the loaded value will be overwritten with the correct stack pointer value."
   * So A7 is legal and is the documented way to discard the pointer. */
  const ap_m68020_module_entry_t entry = ap_m68020_module_entry(0xF000u);
  TEST_ASSERT_TRUE(entry.address_register);
  TEST_ASSERT_EQUAL_UINT(7u, entry.reg);
}

static void test_the_frame_control_word_is_the_descriptors_upper_half(void) {
  /* Figure D-3 packs Opt at 15-13, Type at 12-8 and the saved access level at
   * 7-0: the same three widths as Figure D-1, shifted down by the descriptor's
   * reserved half. "The Opt and Type fields ... are copied to the frame from
   * the module descriptor by the CALLM instruction", and this is what that
   * copy is -- the descriptor's first long word's top sixteen bits verbatim. */
  const uint32_t descriptor = 0xA1AB0000u;
  const ap_m68020_module_control_t control =
      ap_m68020_module_control(descriptor);
  const uint16_t frame_word = (uint16_t)(descriptor >> 16);

  TEST_ASSERT_EQUAL_UINT(control.opt, (unsigned)((frame_word >> 13) & 0x7u));
  TEST_ASSERT_EQUAL_UINT(control.type, (unsigned)((frame_word >> 8) & 0x1Fu));
  TEST_ASSERT_EQUAL_UINT(control.access_level, (unsigned)(frame_word & 0xFFu));
}

static void test_the_module_stack_frame_layout(void) {
  /* Figure D-3, in order and without gaps, ending at the arguments. */
  TEST_ASSERT_EQUAL_UINT(0x00u, AP_M68020_FRAME_OPT_TYPE);
  TEST_ASSERT_EQUAL_UINT(0x02u, AP_M68020_FRAME_CCR);
  TEST_ASSERT_EQUAL_UINT(0x04u, AP_M68020_FRAME_ARGUMENT_COUNT);
  /* $06 is "(Reserved)" and holds nothing. */
  TEST_ASSERT_EQUAL_UINT(0x08u, AP_M68020_FRAME_DESCRIPTOR_POINTER);
  TEST_ASSERT_EQUAL_UINT(0x0Cu, AP_M68020_FRAME_SAVED_PC);
  TEST_ASSERT_EQUAL_UINT(0x10u, AP_M68020_FRAME_SAVED_DATA_AREA);
  TEST_ASSERT_EQUAL_UINT(0x14u, AP_M68020_FRAME_SAVED_SP);
  /* "Arguments (Optional)" begin at +$18, so the fixed frame is 24 bytes. */
  TEST_ASSERT_EQUAL_UINT(0x18u, AP_M68020_FRAME_BYTES);
}

static void test_the_descriptor_layout(void) {
  TEST_ASSERT_EQUAL_UINT(0x00u, AP_M68020_DESCRIPTOR_CONTROL);
  TEST_ASSERT_EQUAL_UINT(0x04u, AP_M68020_DESCRIPTOR_ENTRY_WORD_POINTER);
  TEST_ASSERT_EQUAL_UINT(0x08u, AP_M68020_DESCRIPTOR_DATA_AREA_POINTER);
  TEST_ASSERT_EQUAL_UINT(0x0Cu, AP_M68020_DESCRIPTOR_STACK_POINTER);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_two_instructions_share_an_opcode_prefix);
  RUN_TEST(test_rtm_occupies_the_register_direct_modes);
  RUN_TEST(test_callm_takes_only_control_addressing_modes);
  RUN_TEST(test_a_word_outside_the_prefix_is_not_a_module_instruction);
  RUN_TEST(test_the_descriptor_control_fields);
  RUN_TEST(test_the_reserved_half_of_the_descriptor_reaches_no_field);
  RUN_TEST(test_only_types_00_and_01_are_recognised);
  RUN_TEST(test_the_disable_range_is_a_single_bit);
  RUN_TEST(test_only_options_000_and_100_are_recognised);
  RUN_TEST(test_only_a_type_01_descriptor_changes_access);
  RUN_TEST(test_arguments_are_copied_only_for_option_000_with_a_stack_change);
  RUN_TEST(test_the_module_entry_word_fields);
  RUN_TEST(test_the_entry_words_low_twelve_bits_reach_no_field);
  RUN_TEST(test_the_entry_word_can_select_a7);
  RUN_TEST(test_the_frame_control_word_is_the_descriptors_upper_half);
  RUN_TEST(test_the_module_stack_frame_layout);
  RUN_TEST(test_the_descriptor_layout);
  return UNITY_END();
}
