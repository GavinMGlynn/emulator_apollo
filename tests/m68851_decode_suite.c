/* MC68851 instruction decode, `[68851]` Appendix A, read from the page images.
 *
 * The extracted text of these bit rows is unusable -- zeros come out as
 * letters and columns collapse -- so every field position here comes from the
 * rendered page.
 */

#include "cpu/m68851/ap_m68851_decode.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------------------
 * The function code specification field.
 * ------------------------------------------------------------------------- */

static void test_the_immediate_form_carries_four_bits(void) {
  /* "1DDDD -- function code is specified as four bits DDDD." */
  for (unsigned fc = 0; fc < 16u; fc++) {
    const ap_m68851_fc_spec_t spec = ap_m68851_decode_fc(0x10u | fc);
    TEST_ASSERT_EQUAL_INT(AP_M68851_FC_IMMEDIATE, spec.source);
    TEST_ASSERT_EQUAL_UINT(fc, spec.immediate);
  }
}

static void test_the_data_register_form_names_a_register(void) {
  /* "01RRR -- function code is contained in CPU data register RRR." */
  for (unsigned reg = 0; reg < 8u; reg++) {
    const ap_m68851_fc_spec_t spec = ap_m68851_decode_fc(0x08u | reg);
    TEST_ASSERT_EQUAL_INT(AP_M68851_FC_DATA_REGISTER, spec.source);
    TEST_ASSERT_EQUAL_UINT(reg, spec.data_register);
  }
}

static void test_the_two_cpu_register_forms(void) {
  /* "00000 -- function code is contained in CPU SFC register"; "00001 -- ...
   * DFC register." Two single encodings, not a range. */
  TEST_ASSERT_EQUAL_INT(AP_M68851_FC_SFC, ap_m68851_decode_fc(0x00u).source);
  TEST_ASSERT_EQUAL_INT(AP_M68851_FC_DFC, ap_m68851_decode_fc(0x01u).source);
}

static void test_the_field_is_a_prefix_code(void) {
  /* The trap: a decoder that tested the top bit, then the next, then treated
   * the remainder as a register number would map `00000` to data register 0.
   * It must not -- `00000` is the SFC form and `01000` is register 0, and the
   * two are different instructions. */
  TEST_ASSERT_EQUAL_INT(AP_M68851_FC_SFC, ap_m68851_decode_fc(0x00u).source);
  const ap_m68851_fc_spec_t r0 = ap_m68851_decode_fc(0x08u);
  TEST_ASSERT_EQUAL_INT(AP_M68851_FC_DATA_REGISTER, r0.source);
  TEST_ASSERT_EQUAL_UINT(0u, r0.data_register);
}

static void test_the_unlisted_encodings_are_undefined(void) {
  /* `00010` through `00111`: the manual lists four forms and no others, so
   * these are undefined rather than aliases of one of them. */
  for (unsigned field = 0x02u; field < 0x08u; field++) {
    TEST_ASSERT_EQUAL_INT(AP_M68851_FC_UNDEFINED,
                          ap_m68851_decode_fc(field).source);
  }
}

static void test_every_field_value_decodes(void) {
  /* All 32, each to exactly one source. Sweeping the field rather than sampling
   * it, because the prefix boundaries are where this goes wrong. */
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
   * function codes $0 through $7 can be specified in this manner." So the CPU
   * register forms cannot reach a function code with FC3 set, and an
   * instruction using them cannot address DMA entries at all. */
  const ap_m68851_fc_spec_t dma = ap_m68851_decode_fc(0x10u | 0x8u);
  TEST_ASSERT_TRUE(ap_m68851_fc_reaches_dma(&dma));

  const ap_m68851_fc_spec_t cpu = ap_m68851_decode_fc(0x10u | 0x5u);
  TEST_ASSERT_FALSE(ap_m68851_fc_reaches_dma(&cpu));

  const ap_m68851_fc_spec_t sfc = ap_m68851_decode_fc(0x00u);
  const ap_m68851_fc_spec_t dfc = ap_m68851_decode_fc(0x01u);
  const ap_m68851_fc_spec_t reg = ap_m68851_decode_fc(0x0Fu);
  TEST_ASSERT_FALSE(ap_m68851_fc_reaches_dma(&sfc));
  TEST_ASSERT_FALSE(ap_m68851_fc_reaches_dma(&dfc));
  TEST_ASSERT_FALSE(ap_m68851_fc_reaches_dma(&reg));
}

/* ---------------------------------------------------------------------------
 * PFLUSH: `001` | Mode(3) | 0 | Mask(4) | FC(5).
 * ------------------------------------------------------------------------- */

/* Assemble a command word from its fields, so the tests read as encodings. */
static uint16_t pflush_word(unsigned mode, unsigned mask, unsigned fc) {
  return (uint16_t)((1u << 13) | (mode << 10) | (mask << 5) | fc);
}

static void test_the_pflush_fields_sit_where_appendix_a_draws_them(void) {
  const ap_m68851_pflush_t p =
      ap_m68851_decode_pflush(pflush_word(6u, 0xFu, 0x15u));
  TEST_ASSERT_EQUAL_INT(AP_M68851_PFLUSH_FC_EA, p.mode);
  TEST_ASSERT_EQUAL_UINT(0xFu, p.mask);
  TEST_ASSERT_EQUAL_INT(AP_M68851_FC_IMMEDIATE, p.fc.source);
  TEST_ASSERT_EQUAL_UINT(0x5u, p.fc.immediate);
}

static void test_the_five_defined_pflush_modes(void) {
  /* "001 flush all; 100 by function code only; 101 by function code including
   * shared entries; 110 by function code and effective address; 111 ... and
   * effective address including shared entries." */
  TEST_ASSERT_EQUAL_INT(AP_M68851_PFLUSH_ALL,
                        ap_m68851_decode_pflush(pflush_word(1u, 0u, 0u)).mode);
  TEST_ASSERT_EQUAL_INT(AP_M68851_PFLUSH_FC,
                        ap_m68851_decode_pflush(pflush_word(4u, 0u, 0u)).mode);
  TEST_ASSERT_EQUAL_INT(AP_M68851_PFLUSH_FC_SHARED,
                        ap_m68851_decode_pflush(pflush_word(5u, 0u, 0u)).mode);
  TEST_ASSERT_EQUAL_INT(AP_M68851_PFLUSH_FC_EA,
                        ap_m68851_decode_pflush(pflush_word(6u, 0u, 0u)).mode);
  TEST_ASSERT_EQUAL_INT(AP_M68851_PFLUSH_FC_EA_SHARED,
                        ap_m68851_decode_pflush(pflush_word(7u, 0u, 0u)).mode);
}

static void test_the_unlisted_pflush_modes_are_undefined(void) {
  /* `000`, `010` and `011` appear in no list. The mode is not a field of
   * independent option bits -- `010` is not "shared entries, nothing else" --
   * so an unlisted value is undefined rather than a combination. */
  const unsigned unlisted[] = {0u, 2u, 3u};
  for (unsigned i = 0; i < 3u; i++) {
    TEST_ASSERT_EQUAL_INT(
        AP_M68851_PFLUSH_UNDEFINED,
        ap_m68851_decode_pflush(pflush_word(unlisted[i], 0u, 0u)).mode);
  }
}

static void test_a_flush_all_must_have_a_zero_mask_and_function_code(void) {
  /* "If mode = 001 (flush all entries), mask must be 0000" and "function code
   * must be 00000". A flush-all naming a function code contradicts itself, so
   * the manual forbids the encoding rather than ignoring the fields. */
  const ap_m68851_pflush_t good =
      ap_m68851_decode_pflush(pflush_word(1u, 0u, 0u));
  TEST_ASSERT_TRUE(ap_m68851_pflush_is_valid(&good));

  const ap_m68851_pflush_t bad_mask =
      ap_m68851_decode_pflush(pflush_word(1u, 0x1u, 0u));
  TEST_ASSERT_FALSE(ap_m68851_pflush_is_valid(&bad_mask));

  const ap_m68851_pflush_t bad_fc =
      ap_m68851_decode_pflush(pflush_word(1u, 0u, 0x15u));
  TEST_ASSERT_FALSE(ap_m68851_pflush_is_valid(&bad_fc));
}

static void test_the_other_modes_accept_any_mask_and_function_code(void) {
  /* The constraint is specific to flush-all; a flush by function code is
   * supposed to name one. */
  for (unsigned mode = 4u; mode <= 7u; mode++) {
    const ap_m68851_pflush_t p =
        ap_m68851_decode_pflush(pflush_word(mode, 0xFu, 0x15u));
    TEST_ASSERT_TRUE(ap_m68851_pflush_is_valid(&p));
  }
}

static void test_only_the_shared_modes_flush_shared_entries(void) {
  /* "ATC entries whose SG bit is set will not be invalidated unless the
   * PFLUSHS is specified" -- a shared entry surviving an ordinary flush is the
   * point of sharing it. */
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
   * indicates that the bit position is not significant". So a flush addresses a
   * set, and the mask chooses how big it is. */

  /* Mask 0: every function code matches, whatever the instruction names. */
  for (unsigned entry = 0; entry < 16u; entry++) {
    TEST_ASSERT_TRUE(ap_m68851_pflush_matches_fc(0x0u, 0x5u, entry));
  }

  /* Mask $F: exactly one. */
  for (unsigned entry = 0; entry < 16u; entry++) {
    TEST_ASSERT_EQUAL_INT(entry == 0x5u,
                          ap_m68851_pflush_matches_fc(0xFu, 0x5u, entry));
  }

  /* Mask $8: the supervisor/user distinction alone -- one bit significant, so
   * the flush names eight function codes at once. */
  TEST_ASSERT_TRUE(ap_m68851_pflush_matches_fc(0x8u, 0x5u, 0x1u));
  TEST_ASSERT_TRUE(ap_m68851_pflush_matches_fc(0x8u, 0x5u, 0x7u));
  TEST_ASSERT_FALSE(ap_m68851_pflush_matches_fc(0x8u, 0x5u, 0x9u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_immediate_form_carries_four_bits);
  RUN_TEST(test_the_data_register_form_names_a_register);
  RUN_TEST(test_the_two_cpu_register_forms);
  RUN_TEST(test_the_field_is_a_prefix_code);
  RUN_TEST(test_the_unlisted_encodings_are_undefined);
  RUN_TEST(test_every_field_value_decodes);
  RUN_TEST(test_only_the_immediate_form_can_name_a_dma_function_code);
  RUN_TEST(test_the_pflush_fields_sit_where_appendix_a_draws_them);
  RUN_TEST(test_the_five_defined_pflush_modes);
  RUN_TEST(test_the_unlisted_pflush_modes_are_undefined);
  RUN_TEST(test_a_flush_all_must_have_a_zero_mask_and_function_code);
  RUN_TEST(test_the_other_modes_accept_any_mask_and_function_code);
  RUN_TEST(test_only_the_shared_modes_flush_shared_entries);
  RUN_TEST(test_only_the_address_modes_match_on_the_address);
  RUN_TEST(test_the_mask_makes_a_flush_name_a_set_of_function_codes);
  return UNITY_END();
}
