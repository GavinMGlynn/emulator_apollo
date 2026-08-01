/* The special status word, and which bus fault frame a fault requires.
 *
 * This is the register that makes a bus fault repairable rather than merely
 * fatal: it tells a handler whether the instruction stream faulted, the data
 * stream faulted, or both, and for a data fault it carries the size, direction
 * and address space needed to redo the access. Demand paging is built on it.
 */

#include "cpu/m68030/ap_m68030_ssw.h"

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Figure 8-9 places each flag at a named bit, and a handler reads them by
 * position. A transposition here would be invisible until real firmware
 * inspected the word. */
static void test_each_flag_lands_on_the_bit_figure_8_9_gives_it(void) {
  const ap_m68030_ssw_t stage_c = {.stage_c_fault = true};
  const ap_m68030_ssw_t stage_b = {.stage_b_fault = true};
  const ap_m68030_ssw_t data = {.data_fault = true};
  const ap_m68030_ssw_t rmw = {.read_modify_write = true};
  const ap_m68030_ssw_t read = {.read = true};

  /* FC at 15 brings RC at 13 with it, and FB at 14 brings RB at 12. */
  TEST_ASSERT_EQUAL_HEX16(0xA000u, ap_m68030_ssw_encode(&stage_c));
  TEST_ASSERT_EQUAL_HEX16(0x5000u, ap_m68030_ssw_encode(&stage_b));
  TEST_ASSERT_EQUAL_HEX16(0x0100u, ap_m68030_ssw_encode(&data));
  TEST_ASSERT_EQUAL_HEX16(0x0080u, ap_m68030_ssw_encode(&rmw));
  TEST_ASSERT_EQUAL_HEX16(0x0040u, ap_m68030_ssw_encode(&read));
}

/* "A rerun bit is always set when the corresponding fault bit is set." A word
 * with FB set and RB clear claims stage B is invalid but needs no prefetch,
 * which leaves a stale instruction word in the pipe when the handler returns.
 * The encoder must not be able to produce it, however the caller fills the
 * struct. */
static void test_a_fault_bit_always_brings_its_rerun_bit(void) {
  const ap_m68030_ssw_t both_faults = {.stage_c_fault = true,
                                       .stage_b_fault = true};
  const uint16_t word = ap_m68030_ssw_encode(&both_faults);

  const ap_m68030_ssw_t back = ap_m68030_ssw_decode(word);
  TEST_ASSERT_TRUE(back.stage_c_rerun);
  TEST_ASSERT_TRUE(back.stage_b_rerun);
}

/* The converse must stay expressible: "If an address error exception occurs,
 * the fault bits written to the stack frame are not set ... and the rerun bits
 * alone show the cause of the exception." A rerun without a fault is how an
 * address error is told from a bus error, so forcing the pair both ways would
 * make the two indistinguishable. */
static void test_a_rerun_without_a_fault_is_how_an_address_error_reads(void) {
  const ap_m68030_ssw_t address_error = {.stage_c_rerun = true,
                                         .stage_b_rerun = true};
  const ap_m68030_ssw_t back =
      ap_m68030_ssw_decode(ap_m68030_ssw_encode(&address_error));

  TEST_ASSERT_TRUE(back.stage_c_rerun);
  TEST_ASSERT_TRUE(back.stage_b_rerun);
  TEST_ASSERT_FALSE(back.stage_c_fault);
  TEST_ASSERT_FALSE(back.stage_b_fault);
}

/* Table 7-3: SIZ1/SIZ0 counts the bytes *remaining*, so a long word is zero.
 * Reading it as a plain byte count makes every long-word fault look like a
 * four-byte one the field cannot express, and every handler repair the wrong
 * width. */
static void test_the_size_field_counts_bytes_remaining_so_long_is_zero(void) {
  TEST_ASSERT_EQUAL_UINT(AP_M68030_SSW_SIZE_BYTE, ap_m68030_ssw_size_for(1u));
  TEST_ASSERT_EQUAL_UINT(AP_M68030_SSW_SIZE_WORD, ap_m68030_ssw_size_for(2u));
  TEST_ASSERT_EQUAL_UINT(AP_M68030_SSW_SIZE_THREE_BYTE,
                         ap_m68030_ssw_size_for(3u));
  TEST_ASSERT_EQUAL_UINT(AP_M68030_SSW_SIZE_LONG, ap_m68030_ssw_size_for(4u));

  /* And back, so the pair is an identity over every width the part moves. */
  for (unsigned bytes = 1u; bytes <= 4u; bytes++) {
    TEST_ASSERT_EQUAL_UINT(
        bytes, ap_m68030_ssw_size_bytes(ap_m68030_ssw_size_for(bytes)));
  }
}

/* FC2-FC0 is "address space for data cycle", and a handler redoing the access
 * must use the same space -- supervisor data is a different address space from
 * user data on this machine, not a permission flag. */
static void test_the_function_code_survives_the_round_trip(void) {
  for (uint8_t code = 0u; code < 8u; code++) {
    const ap_m68030_ssw_t ssw = {.data_fault = true, .function_code = code};
    const ap_m68030_ssw_t back =
        ap_m68030_ssw_decode(ap_m68030_ssw_encode(&ssw));
    TEST_ASSERT_EQUAL_UINT8(code, back.function_code);
  }
}

/* §8.2.2: "Data read faults only generate the long bus fault frame". The short
 * frame has no data input buffer, so there is nowhere to put the value the
 * faulted read owed the instruction -- the fault would be stacked and still be
 * unrepairable. */
static void test_a_faulted_data_read_requires_the_long_frame(void) {
  const ap_m68030_ssw_t read_fault = {.data_fault = true, .read = true};
  TEST_ASSERT_EQUAL_INT(AP_M68030_FRAME_LONG_BUS_FAULT,
                        ap_m68030_bus_fault_frame(&read_fault));
}

/* A write fault has its value in the data output buffer, which the short frame
 * does carry, so it does not force the long one. Without this the test above
 * would pass for an implementation that always answered "long". */
static void test_a_faulted_data_write_does_not_require_the_long_frame(void) {
  const ap_m68030_ssw_t write_fault = {.data_fault = true, .read = false};
  TEST_ASSERT_EQUAL_INT(AP_M68030_FRAME_SHORT_BUS_FAULT,
                        ap_m68030_bus_fault_frame(&write_fault));
}

/* An instruction stream fault touches no data cycle at all, so the data half of
 * the word stays clear and the short frame suffices. */
static void test_an_instruction_stream_fault_takes_the_short_frame(void) {
  const ap_m68030_ssw_t prefetch_fault = {.stage_c_fault = true};
  TEST_ASSERT_EQUAL_INT(AP_M68030_FRAME_SHORT_BUS_FAULT,
                        ap_m68030_bus_fault_frame(&prefetch_fault));
  TEST_ASSERT_FALSE(
      ap_m68030_ssw_decode(ap_m68030_ssw_encode(&prefetch_fault)).data_fault);
}

/* "Data and instruction stream faults may be pending simultaneously; the fault
 * handler should be able to recognize any combination of the FC, FB, RC, RB,
 * and DF bits." So the two halves are independent and one must not clear the
 * other. */
static void test_a_data_and_an_instruction_fault_can_stand_together(void) {
  const ap_m68030_ssw_t both = {
      .stage_c_fault = true,
      .data_fault = true,
      .read = true,
      .size = AP_M68030_SSW_SIZE_BYTE,
      .function_code = 5u, /* supervisor data */
  };
  const ap_m68030_ssw_t back = ap_m68030_ssw_decode(ap_m68030_ssw_encode(&both));

  TEST_ASSERT_TRUE(back.stage_c_fault);
  TEST_ASSERT_TRUE(back.stage_c_rerun);
  TEST_ASSERT_TRUE(back.data_fault);
  TEST_ASSERT_TRUE(back.read);
  TEST_ASSERT_EQUAL_UINT(AP_M68030_SSW_SIZE_BYTE, back.size);
  TEST_ASSERT_EQUAL_UINT8(5u, back.function_code);
  /* And a read fault still asks for the long frame when a prefetch faulted
   * too -- the instruction half does not downgrade the data half's needs. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_FRAME_LONG_BUS_FAULT,
                        ap_m68030_bus_fault_frame(&back));
}

/* The bits Figure 8-9 marks "for internal use only" have no source in this
 * model, so decoding must not surface them as though they meant something. */
static void test_the_internal_use_bits_are_not_reported(void) {
  /* Every internal bit set, and nothing else: 11, 10, 9 and 3. */
  const ap_m68030_ssw_t back = ap_m68030_ssw_decode(0x0E08u);

  TEST_ASSERT_FALSE(back.stage_c_fault);
  TEST_ASSERT_FALSE(back.stage_b_fault);
  TEST_ASSERT_FALSE(back.data_fault);
  TEST_ASSERT_FALSE(back.read);
  TEST_ASSERT_EQUAL_UINT(AP_M68030_SSW_SIZE_LONG, back.size);
  TEST_ASSERT_EQUAL_UINT8(0u, back.function_code);
}

/* Table 8-6's field offsets. The short frame is 16 words and the long one 46,
 * so every offset the short frame defines must fall inside it -- an offset past
 * the end would write over the caller's stack rather than into the frame. */
static void test_every_short_frame_field_falls_inside_the_short_frame(void) {
  const unsigned short_bytes =
      ap_m68030_frame_words(AP_M68030_FRAME_SHORT_BUS_FAULT) * 2u;
  const unsigned long_bytes =
      ap_m68030_frame_words(AP_M68030_FRAME_LONG_BUS_FAULT) * 2u;

  TEST_ASSERT_EQUAL_UINT(32u, short_bytes);
  TEST_ASSERT_EQUAL_UINT(92u, long_bytes);

  TEST_ASSERT_TRUE(AP_M68030_BUS_FAULT_SSW + 2u <= short_bytes);
  TEST_ASSERT_TRUE(AP_M68030_BUS_FAULT_STAGE_C + 2u <= short_bytes);
  TEST_ASSERT_TRUE(AP_M68030_BUS_FAULT_STAGE_B + 2u <= short_bytes);
  TEST_ASSERT_TRUE(AP_M68030_BUS_FAULT_ADDRESS + 4u <= short_bytes);
  TEST_ASSERT_TRUE(AP_M68030_BUS_FAULT_DATA_OUTPUT + 4u <= short_bytes);

  /* And the long-frame-only fields must fall outside the short frame, since
   * that is precisely why a read fault cannot use it. */
  TEST_ASSERT_TRUE(AP_M68030_BUS_FAULT_DATA_INPUT >= short_bytes);
  TEST_ASSERT_TRUE(AP_M68030_BUS_FAULT_STAGE_B_ADDRESS >= short_bytes);
  TEST_ASSERT_TRUE(AP_M68030_BUS_FAULT_DATA_INPUT + 4u <= long_bytes);
  TEST_ASSERT_TRUE(AP_M68030_BUS_FAULT_VERSION + 2u <= long_bytes);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_each_flag_lands_on_the_bit_figure_8_9_gives_it);
  RUN_TEST(test_a_fault_bit_always_brings_its_rerun_bit);
  RUN_TEST(test_a_rerun_without_a_fault_is_how_an_address_error_reads);
  RUN_TEST(test_the_size_field_counts_bytes_remaining_so_long_is_zero);
  RUN_TEST(test_the_function_code_survives_the_round_trip);
  RUN_TEST(test_a_faulted_data_read_requires_the_long_frame);
  RUN_TEST(test_a_faulted_data_write_does_not_require_the_long_frame);
  RUN_TEST(test_an_instruction_stream_fault_takes_the_short_frame);
  RUN_TEST(test_a_data_and_an_instruction_fault_can_stand_together);
  RUN_TEST(test_the_internal_use_bits_are_not_reported);
  RUN_TEST(test_every_short_frame_field_falls_inside_the_short_frame);
  return UNITY_END();
}
