/* MC68030 exception processing: vectors, priority and stack frames.
 *
 * Cited to MC68030 User's Manual 3ed §8, Tables 8-1, 8-5 and 8-6.
 *
 * Table 8-1 is used as a *check* here rather than transcribed: the offsets are
 * computed as vector x 4 and the table's own published values are asserted
 * against that, so a wrong arithmetic rule fails rather than being copied in.
 */

#include "cpu/m68030/ap_m68030_exception.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------------------
 * Vectors, Table 8-1.
 * ------------------------------------------------------------------------- */

/* The table's own hex column, spot-checked across its whole range including
 * both ends. */
static void test_vector_offsets_match_the_published_table(void) {
  TEST_ASSERT_EQUAL_HEX32(0x000, ap_m68030_vector_offset(0));
  TEST_ASSERT_EQUAL_HEX32(0x008, ap_m68030_vector_offset(2));  /* bus error */
  TEST_ASSERT_EQUAL_HEX32(0x00C, ap_m68030_vector_offset(3));  /* address error */
  TEST_ASSERT_EQUAL_HEX32(0x03C, ap_m68030_vector_offset(15));
  TEST_ASSERT_EQUAL_HEX32(0x060, ap_m68030_vector_offset(24)); /* spurious */
  TEST_ASSERT_EQUAL_HEX32(0x07C, ap_m68030_vector_offset(31)); /* level 7 av */
  TEST_ASSERT_EQUAL_HEX32(0x080, ap_m68030_vector_offset(32)); /* TRAP #0 */
  TEST_ASSERT_EQUAL_HEX32(0x0BC, ap_m68030_vector_offset(47)); /* TRAP #15 */
  TEST_ASSERT_EQUAL_HEX32(0x0E0, ap_m68030_vector_offset(56)); /* MMU config */
  TEST_ASSERT_EQUAL_HEX32(0x100, ap_m68030_vector_offset(64)); /* user base */
  TEST_ASSERT_EQUAL_HEX32(0x3FC, ap_m68030_vector_offset(255));
}

/* "Level 1 Interrupt Autovector" is 25 through "Level 7" at 31, and the
 * spurious interrupt sits just below at 24. */
static void test_the_autovectors_run_from_level_one_to_seven(void) {
  TEST_ASSERT_EQUAL_UINT(25, ap_m68030_autovector(1));
  TEST_ASSERT_EQUAL_UINT(31, ap_m68030_autovector(7));
  TEST_ASSERT_EQUAL_HEX32(0x064, ap_m68030_vector_offset(ap_m68030_autovector(1)));
  TEST_ASSERT_EQUAL_HEX32(0x07C, ap_m68030_vector_offset(ap_m68030_autovector(7)));
  TEST_ASSERT_EQUAL_UINT(24, AP_M68030_VECTOR_SPURIOUS_INTERRUPT);
}

/* "TRAP #0-15 Instruction Vectors", 32 through 47. */
static void test_the_trap_vectors_occupy_thirty_two_to_forty_seven(void) {
  TEST_ASSERT_EQUAL_UINT(32, ap_m68030_trap_vector(0));
  TEST_ASSERT_EQUAL_UINT(47, ap_m68030_trap_vector(15));
}

/* ---------------------------------------------------------------------------
 * Priority, Table 8-5.
 * ------------------------------------------------------------------------- */

/* "0.0 is the highest priority, 4.2 is the lowest." The whole documented
 * ordering, checked as a chain rather than pairwise by hand. */
static void test_the_documented_priority_order_holds_end_to_end(void) {
  const unsigned ordered[] = {
      AP_M68030_VECTOR_RESET_PC,            /* 0.0 */
      AP_M68030_VECTOR_ADDRESS_ERROR,       /* 1.0 */
      AP_M68030_VECTOR_BUS_ERROR,           /* 1.1 */
      AP_M68030_VECTOR_ZERO_DIVIDE,         /* 2.0 */
      AP_M68030_VECTOR_ILLEGAL_INSTRUCTION, /* 3.0 */
      AP_M68030_VECTOR_TRACE,               /* 4.1 */
  };
  for (unsigned i = 0; i + 1 < sizeof(ordered) / sizeof(ordered[0]); i++) {
    TEST_ASSERT_TRUE(ap_m68030_priority_precedes(
        ap_m68030_exception_priority(ordered[i]),
        ap_m68030_exception_priority(ordered[i + 1])));
  }
}

/* Address error is 1.0 and bus error 1.1, so they share a group but not a
 * priority -- the one pair most easily collapsed into "both group 1". */
static void test_address_error_outranks_bus_error_within_group_one(void) {
  const ap_m68030_priority_t address =
      ap_m68030_exception_priority(AP_M68030_VECTOR_ADDRESS_ERROR);
  const ap_m68030_priority_t bus =
      ap_m68030_exception_priority(AP_M68030_VECTOR_BUS_ERROR);

  TEST_ASSERT_EQUAL_UINT8(1, address.group);
  TEST_ASSERT_EQUAL_UINT8(1, bus.group);
  TEST_ASSERT_TRUE(ap_m68030_priority_precedes(address, bus));
  TEST_ASSERT_FALSE(ap_m68030_priority_precedes(bus, address));
}

/* Trace is 4.1 and interrupt 4.2, which is the pair the manual works through in
 * prose: "if simultaneous trap, trace, and interrupt exceptions are pending,
 * the exception processing for the trap occurs first, followed immediately by
 * exception processing for the trace and then for the interrupt." */
static void test_trap_precedes_trace_which_precedes_interrupt(void) {
  const ap_m68030_priority_t trap =
      ap_m68030_exception_priority(ap_m68030_trap_vector(3));
  const ap_m68030_priority_t trace =
      ap_m68030_exception_priority(AP_M68030_VECTOR_TRACE);
  const ap_m68030_priority_t interrupt =
      ap_m68030_exception_priority(ap_m68030_autovector(4));

  TEST_ASSERT_TRUE(ap_m68030_priority_precedes(trap, trace));
  TEST_ASSERT_TRUE(ap_m68030_priority_precedes(trace, interrupt));
}

/* ---------------------------------------------------------------------------
 * Stack frames, Table 8-6.
 * ------------------------------------------------------------------------- */

/* The frame names carry their own sizes: "FOUR WORD", "SIX WORD",
 * "(10 WORDS)", "(16 WORDS)", "(46 WORDS)". */
static void test_each_frame_format_has_its_documented_size(void) {
  TEST_ASSERT_EQUAL_UINT(4, ap_m68030_frame_words(AP_M68030_FRAME_SHORT));
  TEST_ASSERT_EQUAL_UINT(4, ap_m68030_frame_words(AP_M68030_FRAME_THROWAWAY));
  TEST_ASSERT_EQUAL_UINT(6, ap_m68030_frame_words(AP_M68030_FRAME_SIX_WORD));
  TEST_ASSERT_EQUAL_UINT(
      10, ap_m68030_frame_words(AP_M68030_FRAME_COPROCESSOR_MID));
  TEST_ASSERT_EQUAL_UINT(
      16, ap_m68030_frame_words(AP_M68030_FRAME_SHORT_BUS_FAULT));
  TEST_ASSERT_EQUAL_UINT(
      46, ap_m68030_frame_words(AP_M68030_FRAME_LONG_BUS_FAULT));
}

/* The stacked word is the format in 15-12 over the vector *offset*, not the
 * vector number. TRAP #0 is vector 32, offset $080, so the word is $2080 --
 * $2020 would be a frame RTE accepts that returns to the wrong handler. */
static void test_the_format_word_carries_the_offset_not_the_vector_number(void) {
  TEST_ASSERT_EQUAL_HEX16(0x2080, ap_m68030_frame_format_word(
                                      AP_M68030_FRAME_SIX_WORD,
                                      ap_m68030_trap_vector(0)));
  /* Bus error, vector 2, offset $008, in a long bus fault frame. */
  TEST_ASSERT_EQUAL_HEX16(0xB008,
                          ap_m68030_frame_format_word(
                              AP_M68030_FRAME_LONG_BUS_FAULT,
                              AP_M68030_VECTOR_BUS_ERROR));
  /* A four-word frame for an interrupt autovector at level 6: vector 30,
   * offset $078. */
  TEST_ASSERT_EQUAL_HEX16(0x0078,
                          ap_m68030_frame_format_word(AP_M68030_FRAME_SHORT,
                                                      ap_m68030_autovector(6)));
}

/* RTE "examines the stack frame ... to determine if it is a valid frame", so
 * both fields must come back out of the word. */
static void test_the_format_word_round_trips(void) {
  const uint16_t word = ap_m68030_frame_format_word(
      AP_M68030_FRAME_SHORT_BUS_FAULT, AP_M68030_VECTOR_ADDRESS_ERROR);

  TEST_ASSERT_EQUAL_INT(AP_M68030_FRAME_SHORT_BUS_FAULT,
                        ap_m68030_frame_format_of(word));
  TEST_ASSERT_EQUAL_HEX32(0x00C, ap_m68030_frame_vector_offset_of(word));
}

/* The MC68030 defines six formats. The rest are a format error, vector 14 --
 * $3, $4 and $7 among them, which other members of the family do define, so
 * accepting them would silently import another processor's frame. */
static void test_only_the_six_defined_formats_are_valid(void) {
  const unsigned defined[] = {0x0, 0x1, 0x2, 0x9, 0xA, 0xB};
  bool is_defined[16] = {false};
  for (unsigned i = 0; i < sizeof(defined) / sizeof(defined[0]); i++) {
    is_defined[defined[i]] = true;
  }
  for (unsigned format = 0; format < 16; format++) {
    const uint16_t word = (uint16_t)(format << 12);
    TEST_ASSERT_EQUAL_INT(is_defined[format],
                          ap_m68030_frame_format_defined(word));
  }
}

/* ---------------------------------------------------------------------------
 * Interrupt recognition, §8.1.9 and Table 8-4.
 * ------------------------------------------------------------------------- */

/* Levels 1-6 are recognised when the request "exceeds the current interrupt
 * priority mask in the status register". Equal is not greater. */
static void test_a_masked_level_is_recognised_only_above_the_mask(void) {
  TEST_ASSERT_TRUE(ap_m68030_interrupt_recognised(6, 0, 5));
  TEST_ASSERT_FALSE(ap_m68030_interrupt_recognised(6, 0, 6));
  TEST_ASSERT_FALSE(ap_m68030_interrupt_recognised(5, 0, 6));
  TEST_ASSERT_TRUE(ap_m68030_interrupt_recognised(1, 0, 0));
}

/* "Indicates that no interrupt is requested." */
static void test_level_zero_is_never_an_interrupt(void) {
  TEST_ASSERT_FALSE(ap_m68030_interrupt_recognised(0, 0, 0));
}

/* "Level 7 interrupts cannot be masked by the interrupt priority mask", so a
 * mask of 7 does not stop one. */
static void test_level_seven_is_not_masked_by_a_mask_of_seven(void) {
  TEST_ASSERT_TRUE(ap_m68030_interrupt_recognised(7, 3, 7));
}

/* "they are transition sensitive. The processor recognizes an interrupt request
 * each time the external interrupt request level changes from some lower level
 * to level 7." Holding the line at 7 is not a new interrupt; dropping it and
 * raising it again is. That is exactly what distinguishes level 7 from the
 * others, and it is the manual's own worked example: with level 6 "the external
 * request can be lowered to level 3 and then raised back to level 6 and a
 * second level 6 interrupt is not processed", whereas at level 7 "a second
 * level 7 interrupt is processed". */
static void test_level_seven_is_transition_sensitive(void) {
  /* Already at 7 and still at 7: no new interrupt. */
  TEST_ASSERT_FALSE(ap_m68030_interrupt_recognised(7, 7, 7));
  /* Lowered to 3 and raised back to 7: a second interrupt. */
  TEST_ASSERT_TRUE(ap_m68030_interrupt_recognised(7, 3, 7));

  /* The level 6 contrast, with the mask left at 6 as the handler would: the
   * request returning to 6 is not recognised however it got there. */
  TEST_ASSERT_FALSE(ap_m68030_interrupt_recognised(6, 3, 6));
  TEST_ASSERT_FALSE(ap_m68030_interrupt_recognised(6, 6, 6));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_vector_offsets_match_the_published_table);
  RUN_TEST(test_the_autovectors_run_from_level_one_to_seven);
  RUN_TEST(test_the_trap_vectors_occupy_thirty_two_to_forty_seven);
  RUN_TEST(test_the_documented_priority_order_holds_end_to_end);
  RUN_TEST(test_address_error_outranks_bus_error_within_group_one);
  RUN_TEST(test_trap_precedes_trace_which_precedes_interrupt);
  RUN_TEST(test_each_frame_format_has_its_documented_size);
  RUN_TEST(test_the_format_word_carries_the_offset_not_the_vector_number);
  RUN_TEST(test_the_format_word_round_trips);
  RUN_TEST(test_only_the_six_defined_formats_are_valid);
  RUN_TEST(test_a_masked_level_is_recognised_only_above_the_mask);
  RUN_TEST(test_level_zero_is_never_an_interrupt);
  RUN_TEST(test_level_seven_is_not_masked_by_a_mask_of_seven);
  RUN_TEST(test_level_seven_is_transition_sensitive);
  return UNITY_END();
}
