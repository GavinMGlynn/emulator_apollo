/* MC68040 MMU registers, `[68040]` §3.1 and Figures 3-3 through 3-6, read from
 * the page images.
 *
 * Several tests exist to pin rules where this part *contradicts* the 68851,
 * because the risk in writing a third MMU is carrying the second one's
 * assumptions into it.
 */

#include "cpu/m68040/ap_m68040_regs.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------------------
 * URP and SRP, Figure 3-3.
 * ------------------------------------------------------------------------- */

static void test_a_root_table_is_512_byte_aligned(void) {
  /* "Bits 8-0 of an address loaded into the URP or the SRP must be zero" -- a
   * root table is 512 bytes, so it is aligned to its own size. */
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFE00u, ap_m68040_root_pointer(0xFFFFFFFFu));
  TEST_ASSERT_TRUE(ap_m68040_root_pointer_is_aligned(0x12345E00u));
  TEST_ASSERT_FALSE(ap_m68040_root_pointer_is_aligned(0x12345E01u));
}

/* ---------------------------------------------------------------------------
 * TCR, Figure 3-4.
 * ------------------------------------------------------------------------- */

static void test_the_tcr_has_two_implemented_bits(void) {
  /* "The 16-bit TCR contains two control bits", and "bits 13-0 are undefined
   * (reserved)". */
  const ap_m68040_tcr_t tcr = ap_m68040_tcr_decode(0xFFFFu);
  TEST_ASSERT_TRUE(tcr.enable);
  TEST_ASSERT_EQUAL_INT(AP_M68040_PAGE_8K, tcr.page_size);
  TEST_ASSERT_EQUAL_HEX16(AP_M68040_TCR_IMPLEMENTED_MASK,
                          ap_m68040_tcr_encode(&tcr));
}

static void test_the_two_tcr_bits_are_independent(void) {
  TEST_ASSERT_TRUE(ap_m68040_tcr_decode(0x8000u).enable);
  TEST_ASSERT_EQUAL_INT(AP_M68040_PAGE_4K,
                        ap_m68040_tcr_decode(0x8000u).page_size);
  TEST_ASSERT_FALSE(ap_m68040_tcr_decode(0x4000u).enable);
  TEST_ASSERT_EQUAL_INT(AP_M68040_PAGE_8K,
                        ap_m68040_tcr_decode(0x4000u).page_size);
}

static void test_the_tcr_reserved_bits_reach_no_field(void) {
  const ap_m68040_tcr_t tcr = ap_m68040_tcr_decode(0x3FFFu);
  TEST_ASSERT_FALSE(tcr.enable);
  TEST_ASSERT_EQUAL_INT(AP_M68040_PAGE_4K, tcr.page_size);
  TEST_ASSERT_EQUAL_HEX16(0u, ap_m68040_tcr_encode(&tcr));
}

/* ---------------------------------------------------------------------------
 * The transparent translation registers, Figure 3-5.
 * ------------------------------------------------------------------------- */

static void test_the_ttr_fields(void) {
  /* Base 31-24, mask 23-16, E 15, S 14-13, U1 9, U0 8, CM 6-5, W 2. */
  const ap_m68040_ttr_t ttr = ap_m68040_ttr_decode(0x12FF8324u);
  TEST_ASSERT_EQUAL_UINT(0x12u, ttr.logical_base);
  TEST_ASSERT_EQUAL_UINT(0xFFu, ttr.logical_mask);
  TEST_ASSERT_TRUE(ttr.enable);
  TEST_ASSERT_EQUAL_INT(AP_M68040_TT_USER_ONLY, ttr.supervisor_mode);
  TEST_ASSERT_TRUE(ttr.user_attribute_1);
  TEST_ASSERT_TRUE(ttr.user_attribute_0);
  TEST_ASSERT_EQUAL_INT(AP_M68040_CM_CACHABLE_COPYBACK, ttr.cache_mode);
  TEST_ASSERT_TRUE(ttr.write_protect);
}

static void test_the_ttr_reserved_bits_read_as_zero(void) {
  /* "Bits 12-10, 7, 4, 3, 1, and 0 always read as zero" -- five separate holes,
   * which is where a mask goes wrong. */
  const ap_m68040_ttr_t ttr = ap_m68040_ttr_decode(0x00001C9Bu);
  TEST_ASSERT_EQUAL_HEX32(0u,
                          ap_m68040_ttr_encode(&ttr) & ~0x00001C9Bu & 0xFFFFu);
  TEST_ASSERT_EQUAL_HEX32(
      0u, ap_m68040_ttr_encode(&ttr) & ~AP_M68040_TTR_IMPLEMENTED_MASK);
}

static void test_the_supervisor_field_is_one_meaning_in_two_encodings(void) {
  /* "00 = Match only if FC2 = 0; 01 = Match only if FC2 = 1; 1X = Ignore FC2."
   * The low bit is a don't-care only when the high bit is set, so this cannot
   * be read as a plain two-bit value. */
  TEST_ASSERT_EQUAL_INT(AP_M68040_TT_USER_ONLY,
                        ap_m68040_ttr_decode(0x0000u).supervisor_mode);
  TEST_ASSERT_EQUAL_INT(AP_M68040_TT_SUPERVISOR_ONLY,
                        ap_m68040_ttr_decode(0x2000u).supervisor_mode);
  TEST_ASSERT_EQUAL_INT(AP_M68040_TT_ANY,
                        ap_m68040_ttr_decode(0x4000u).supervisor_mode);
  TEST_ASSERT_EQUAL_INT(AP_M68040_TT_ANY,
                        ap_m68040_ttr_decode(0x6000u).supervisor_mode);
}

static void test_a_disabled_ttr_matches_nothing(void) {
  ap_m68040_ttr_t ttr = {.logical_base = 0x12u, .logical_mask = 0u,
                         .supervisor_mode = AP_M68040_TT_ANY, .enable = false};
  TEST_ASSERT_FALSE(ap_m68040_ttr_matches(&ttr, 0x12345678u, 5u));
  ttr.enable = true;
  TEST_ASSERT_TRUE(ap_m68040_ttr_matches(&ttr, 0x12345678u, 5u));
}

static void test_the_mask_widens_the_block_rather_than_narrowing_it(void) {
  /* "Setting a bit in this field causes the corresponding bit in the Logical
   * Address Base field to be ignored. Blocks of memory larger than 16 Mbytes
   * can be transparently translated by setting some of the logical address
   * mask bits to ones." The opposite of what "mask" usually suggests. */
  ap_m68040_ttr_t ttr = {.logical_base = 0x12u, .logical_mask = 0u,
                         .supervisor_mode = AP_M68040_TT_ANY, .enable = true};
  TEST_ASSERT_TRUE(ap_m68040_ttr_matches(&ttr, 0x12000000u, 5u));
  TEST_ASSERT_FALSE(ap_m68040_ttr_matches(&ttr, 0x13000000u, 5u));

  /* Ignoring the low base bit merges two 16 MB blocks into one 32 MB block. */
  ttr.logical_mask = 0x01u;
  TEST_ASSERT_TRUE(ap_m68040_ttr_matches(&ttr, 0x12000000u, 5u));
  TEST_ASSERT_TRUE(ap_m68040_ttr_matches(&ttr, 0x13000000u, 5u));
  TEST_ASSERT_FALSE(ap_m68040_ttr_matches(&ttr, 0x14000000u, 5u));

  /* An all-ones mask ignores the base entirely: the whole address space. */
  ttr.logical_mask = 0xFFu;
  for (unsigned high = 0; high < 256u; high += 17u) {
    TEST_ASSERT_TRUE(ap_m68040_ttr_matches(&ttr, high << 24, 5u));
  }
}

static void test_the_supervisor_field_filters_by_function_code(void) {
  ap_m68040_ttr_t ttr = {.logical_base = 0x12u, .logical_mask = 0xFFu,
                         .enable = true};

  ttr.supervisor_mode = AP_M68040_TT_USER_ONLY;
  TEST_ASSERT_TRUE(ap_m68040_ttr_matches(&ttr, 0u, 1u));  /* FC2 clear */
  TEST_ASSERT_FALSE(ap_m68040_ttr_matches(&ttr, 0u, 5u)); /* FC2 set */

  ttr.supervisor_mode = AP_M68040_TT_SUPERVISOR_ONLY;
  TEST_ASSERT_FALSE(ap_m68040_ttr_matches(&ttr, 0u, 1u));
  TEST_ASSERT_TRUE(ap_m68040_ttr_matches(&ttr, 0u, 5u));

  ttr.supervisor_mode = AP_M68040_TT_ANY;
  TEST_ASSERT_TRUE(ap_m68040_ttr_matches(&ttr, 0u, 1u));
  TEST_ASSERT_TRUE(ap_m68040_ttr_matches(&ttr, 0u, 5u));
}

/* ---------------------------------------------------------------------------
 * MMUSR, Figure 3-6.
 * ------------------------------------------------------------------------- */

static void test_the_mmusr_fields(void) {
  /* Physical address 31-12, B 11, G 10, U1 9, U0 8, S 7, CM 6-5, M 4,
   * [bit 3 reserved], W 2, T 1, R 0. */
  const ap_m68040_mmusr_t s = ap_m68040_mmusr_decode(0x12345FF7u);
  TEST_ASSERT_EQUAL_HEX32(0x12345000u, s.physical_address);
  TEST_ASSERT_TRUE(s.bus_error);
  TEST_ASSERT_TRUE(s.global);
  TEST_ASSERT_TRUE(s.user_attribute_1);
  TEST_ASSERT_TRUE(s.user_attribute_0);
  TEST_ASSERT_TRUE(s.supervisor);
  TEST_ASSERT_EQUAL_INT(AP_M68040_CM_NONCACHABLE, s.cache_mode);
  TEST_ASSERT_TRUE(s.modified);
  TEST_ASSERT_TRUE(s.write_protect);
  TEST_ASSERT_TRUE(s.transparent);
  TEST_ASSERT_TRUE(s.resident);
}

static void test_mmusr_bit_3_is_a_reserved_zero(void) {
  /* Figure 3-6's glyph between `M` and `W` is a zero, not a field named `O`:
   * the manual's field list runs in descending bit order -- B, G, U1, U0, S,
   * CM, M, W, T, R -- and skips bit 3 entirely. */
  const ap_m68040_mmusr_t s = ap_m68040_mmusr_decode(0x00000008u);
  TEST_ASSERT_FALSE(s.modified);
  TEST_ASSERT_FALSE(s.write_protect);
  TEST_ASSERT_EQUAL_HEX32(0u, ap_m68040_mmusr_encode(&s));
}

static void test_the_mmusr_round_trips_every_implemented_bit(void) {
  for (unsigned bit = 0; bit < 32u; bit++) {
    const uint32_t value = 1u << bit;
    if ((value & AP_M68040_MMUSR_IMPLEMENTED_MASK) == 0u) {
      continue;
    }
    const ap_m68040_mmusr_t s = ap_m68040_mmusr_decode(value);
    TEST_ASSERT_EQUAL_HEX32(value, ap_m68040_mmusr_encode(&s));
  }
}

static void test_a_bus_error_suppresses_every_other_status_bit(void) {
  /* "If the B-bit is set, all other bits are zero." A transfer error during the
   * table search means no descriptor was reached, so there is nothing to report
   * attributes from -- reporting them would invent information. */
  const ap_m68040_mmusr_t s = ap_m68040_mmusr_bus_error();
  TEST_ASSERT_TRUE(s.bus_error);
  TEST_ASSERT_EQUAL_HEX32(0x0800u, ap_m68040_mmusr_encode(&s));
  TEST_ASSERT_FALSE(s.resident);
  TEST_ASSERT_EQUAL_HEX32(0u, s.physical_address);
}

static void test_a_transparent_hit_reports_only_two_bits(void) {
  /* "If the T-bit is set, then the PTEST address matches an instruction or data
   * TTR, the R-bit is set, and all other bits are zero." A transparent block
   * has no page descriptor, so it has no attributes to copy. */
  const ap_m68040_mmusr_t s = ap_m68040_mmusr_transparent();
  TEST_ASSERT_TRUE(s.transparent);
  TEST_ASSERT_TRUE(s.resident);
  TEST_ASSERT_EQUAL_HEX32(0x0003u, ap_m68040_mmusr_encode(&s));
  TEST_ASSERT_EQUAL_HEX32(0u, s.physical_address);
  TEST_ASSERT_FALSE(s.write_protect);
}

static void test_resident_is_set_by_both_routes_to_a_translation(void) {
  /* "The R-bit is set if the PTEST address matches an instruction or data TTR
   * *or* if the table search completes by obtaining a valid page descriptor."
   * So `R` alone does not say which happened -- `T` does. */
  const ap_m68040_mmusr_t transparent = ap_m68040_mmusr_transparent();
  TEST_ASSERT_TRUE(transparent.resident);
  TEST_ASSERT_TRUE(transparent.transparent);

  const ap_m68040_mmusr_t walked = ap_m68040_mmusr_decode(0x12345001u);
  TEST_ASSERT_TRUE(walked.resident);
  TEST_ASSERT_FALSE(walked.transparent);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_root_table_is_512_byte_aligned);
  RUN_TEST(test_the_tcr_has_two_implemented_bits);
  RUN_TEST(test_the_two_tcr_bits_are_independent);
  RUN_TEST(test_the_tcr_reserved_bits_reach_no_field);
  RUN_TEST(test_the_ttr_fields);
  RUN_TEST(test_the_ttr_reserved_bits_read_as_zero);
  RUN_TEST(test_the_supervisor_field_is_one_meaning_in_two_encodings);
  RUN_TEST(test_a_disabled_ttr_matches_nothing);
  RUN_TEST(test_the_mask_widens_the_block_rather_than_narrowing_it);
  RUN_TEST(test_the_supervisor_field_filters_by_function_code);
  RUN_TEST(test_the_mmusr_fields);
  RUN_TEST(test_mmusr_bit_3_is_a_reserved_zero);
  RUN_TEST(test_the_mmusr_round_trips_every_implemented_bit);
  RUN_TEST(test_a_bus_error_suppresses_every_other_status_bit);
  RUN_TEST(test_a_transparent_hit_reports_only_two_bits);
  RUN_TEST(test_resident_is_set_by_both_routes_to_a_translation);
  return UNITY_END();
}
