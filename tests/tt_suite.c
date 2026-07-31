/* MC68030 transparent translation registers.
 *
 * Every assertion cites MC68030 User's Manual 3ed §9.3 or §9.7.3. The register
 * is modelled as decoded fields rather than a packed 32-bit word, because the
 * bit layout of Figure 9-37's lower half did not survive the scan -- see
 * ap_m68030_tt.h. The semantics below are all stated in prose and are what is
 * transcribed here.
 */

#include "cpu/m68030/ap_m68030_tt.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Supervisor data space, the function code Domain/OS uses for the I/O window
 * the boot PROM reaches before any translation tree exists. */
#define FC_SUPERVISOR_DATA 5u
#define FC_USER_PROGRAM 2u

/* A register covering $00000000-$00FFFFFF for supervisor data reads: no address
 * bits masked, so the minimum 16 Mbyte block. */
static ap_m68030_tt_t supervisor_data_reads(void) {
  return (ap_m68030_tt_t){
      .logical_base = 0x00,
      .logical_mask = 0x00,
      .fc_base = FC_SUPERVISOR_DATA,
      .fc_mask = 0x0,
      .enabled = true,
      .cache_inhibit = false,
      .read_transparent = true,
      .ignore_read_write = false,
  };
}

static ap_m68030_access_t access_at(uint32_t address, uint8_t fc, bool read) {
  return (ap_m68030_access_t){
      .address = address, .function_code = fc, .read = read,
      .read_modify_write = false};
}

/* [030] 9.3: a matching address "is used as a physical address, without
 * modification". */
static void test_a_matching_access_is_translated_to_the_same_address(void) {
  ap_m68030_tt_t tt = supervisor_data_reads();
  ap_m68030_access_t a = access_at(0x00123456, FC_SUPERVISOR_DATA, true);
  ap_m68030_tt_result_t r = ap_m68030_tt_translate(&tt, NULL, &a);
  TEST_ASSERT_TRUE(r.transparent);
  TEST_ASSERT_EQUAL_HEX32(0x00123456, r.physical);
}

/* [030] 9.7.3: "A reset operation clears this bit" (E), and 9.3: "A disabled
 * TTx register is completely ignored." */
static void test_a_disabled_register_never_matches(void) {
  ap_m68030_tt_t tt = supervisor_data_reads();
  tt.enabled = false;
  ap_m68030_access_t a = access_at(0x00123456, FC_SUPERVISOR_DATA, true);
  TEST_ASSERT_FALSE(ap_m68030_tt_translate(&tt, NULL, &a).transparent);
}

/* Only the eight high-order address bits take part, so the smallest block is
 * 16 Mbytes -- every address sharing A31-A24 matches. */
static void test_only_the_eight_high_order_address_bits_are_compared(void) {
  ap_m68030_tt_t tt = supervisor_data_reads();
  ap_m68030_access_t low = access_at(0x00000000, FC_SUPERVISOR_DATA, true);
  ap_m68030_access_t high = access_at(0x00FFFFFF, FC_SUPERVISOR_DATA, true);
  ap_m68030_access_t outside = access_at(0x01000000, FC_SUPERVISOR_DATA, true);
  TEST_ASSERT_TRUE(ap_m68030_tt_translate(&tt, NULL, &low).transparent);
  TEST_ASSERT_TRUE(ap_m68030_tt_translate(&tt, NULL, &high).transparent);
  TEST_ASSERT_FALSE(ap_m68030_tt_translate(&tt, NULL, &outside).transparent);
}

/* [030] 9.3: "Setting successively higher order bits in the address mask
 * increases the size of the transparently translated block." The manual's own
 * worked example: $00000000-$0FFFFFFF is base $0X with mask $0F. */
static void test_the_address_mask_widens_the_block(void) {
  ap_m68030_tt_t tt = supervisor_data_reads();
  tt.logical_mask = 0x0F;
  for (uint32_t high = 0x00; high <= 0x0F; high++) {
    ap_m68030_access_t a =
        access_at(high << 24, FC_SUPERVISOR_DATA, true);
    TEST_ASSERT_TRUE(ap_m68030_tt_translate(&tt, NULL, &a).transparent);
  }
  ap_m68030_access_t beyond = access_at(0x10000000, FC_SUPERVISOR_DATA, true);
  TEST_ASSERT_FALSE(ap_m68030_tt_translate(&tt, NULL, &beyond).transparent);
}

/* [030] 9.3: the function code must match too. */
static void test_a_different_function_code_does_not_match(void) {
  ap_m68030_tt_t tt = supervisor_data_reads();
  ap_m68030_access_t a = access_at(0x00123456, FC_USER_PROGRAM, true);
  TEST_ASSERT_FALSE(ap_m68030_tt_translate(&tt, NULL, &a).transparent);
}

/* [030] 9.3's worked example for user program space sets FC BASE to $2 and FC
 * MASK to $0; masking the field instead makes every function code eligible. */
static void test_a_full_function_code_mask_matches_every_space(void) {
  ap_m68030_tt_t tt = supervisor_data_reads();
  tt.fc_mask = 0x7;
  for (uint8_t fc = 0; fc < 8; fc++) {
    ap_m68030_access_t a = access_at(0x00123456, fc, true);
    TEST_ASSERT_TRUE(ap_m68030_tt_translate(&tt, NULL, &a).transparent);
  }
}

/* [030] 9.7.3: "R/W ... 1 - Read accesses transparent". With RWM clear, a write
 * to the same address is not transparent, which is what lets the translation
 * tables apply write protection to a range whose reads are transparent -- the
 * manual's stated reason for the feature. */
static void test_a_read_only_register_does_not_match_a_write(void) {
  ap_m68030_tt_t tt = supervisor_data_reads();
  ap_m68030_access_t write = access_at(0x00123456, FC_SUPERVISOR_DATA, false);
  TEST_ASSERT_FALSE(ap_m68030_tt_translate(&tt, NULL, &write).transparent);
}

/* [030] 9.7.3: "When RWM is set to one, both read and write accesses of a
 * matching address are transparently translated." */
static void test_masking_read_write_makes_both_directions_transparent(void) {
  ap_m68030_tt_t tt = supervisor_data_reads();
  tt.ignore_read_write = true;
  ap_m68030_access_t read = access_at(0x00123456, FC_SUPERVISOR_DATA, true);
  ap_m68030_access_t write = access_at(0x00123456, FC_SUPERVISOR_DATA, false);
  TEST_ASSERT_TRUE(ap_m68030_tt_translate(&tt, NULL, &read).transparent);
  TEST_ASSERT_TRUE(ap_m68030_tt_translate(&tt, NULL, &write).transparent);
}

/* [030] 9.3: "If the RWM bit equals zero, neither the read nor the write of any
 * read-modify-write cycle is transparently translated with the TTx register",
 * and 9.3 stresses this holds "regardless of the function code and address
 * bits". So it overrides an otherwise perfect match, in both directions. */
static void test_a_read_modify_write_is_not_transparent_unless_read_write_is_masked(void) {
  ap_m68030_tt_t tt = supervisor_data_reads();
  tt.ignore_read_write = false;

  ap_m68030_access_t rmw_read = access_at(0x00123456, FC_SUPERVISOR_DATA, true);
  rmw_read.read_modify_write = true;
  ap_m68030_access_t rmw_write = access_at(0x00123456, FC_SUPERVISOR_DATA, false);
  rmw_write.read_modify_write = true;

  TEST_ASSERT_FALSE(ap_m68030_tt_translate(&tt, NULL, &rmw_read).transparent);
  TEST_ASSERT_FALSE(ap_m68030_tt_translate(&tt, NULL, &rmw_write).transparent);
}

/* ...and with RWM set, a read-modify-write is transparent. */
static void test_a_read_modify_write_is_transparent_when_read_write_is_masked(void) {
  ap_m68030_tt_t tt = supervisor_data_reads();
  tt.ignore_read_write = true;
  ap_m68030_access_t rmw = access_at(0x00123456, FC_SUPERVISOR_DATA, true);
  rmw.read_modify_write = true;
  TEST_ASSERT_TRUE(ap_m68030_tt_translate(&tt, NULL, &rmw).transparent);
}

/* [030] 9.3: "For an access, if either of these registers match, the access is
 * transparently translated." */
static void test_either_register_matching_is_enough(void) {
  ap_m68030_tt_t tt0 = supervisor_data_reads();
  tt0.logical_base = 0xF0; /* deliberately elsewhere */
  ap_m68030_tt_t tt1 = supervisor_data_reads();

  ap_m68030_access_t a = access_at(0x00123456, FC_SUPERVISOR_DATA, true);
  TEST_ASSERT_TRUE(ap_m68030_tt_translate(&tt0, &tt1, &a).transparent);
}

/* [030] 9.3: "If both registers match, the CI bits are ORed together to
 * generate the CIOUT signal." */
static void test_cache_inhibit_is_ored_when_both_registers_match(void) {
  ap_m68030_tt_t clean = supervisor_data_reads();
  ap_m68030_tt_t inhibiting = supervisor_data_reads();
  inhibiting.cache_inhibit = true;

  ap_m68030_access_t a = access_at(0x00123456, FC_SUPERVISOR_DATA, true);

  TEST_ASSERT_FALSE(ap_m68030_tt_translate(&clean, &clean, &a).cache_inhibit);
  TEST_ASSERT_TRUE(ap_m68030_tt_translate(&clean, &inhibiting, &a).cache_inhibit);
  TEST_ASSERT_TRUE(ap_m68030_tt_translate(&inhibiting, &clean, &a).cache_inhibit);
}

/* A register that does not match must not contribute its CI bit. ORing the CI
 * of a non-matching register would inhibit caching across unrelated memory. */
static void test_a_non_matching_register_does_not_contribute_cache_inhibit(void) {
  ap_m68030_tt_t matching = supervisor_data_reads();
  ap_m68030_tt_t elsewhere = supervisor_data_reads();
  elsewhere.logical_base = 0xF0;
  elsewhere.cache_inhibit = true;

  ap_m68030_access_t a = access_at(0x00123456, FC_SUPERVISOR_DATA, true);
  ap_m68030_tt_result_t r = ap_m68030_tt_translate(&matching, &elsewhere, &a);
  TEST_ASSERT_TRUE(r.transparent);
  TEST_ASSERT_FALSE(r.cache_inhibit);
}

/* An access matching neither register falls through to the translation tables,
 * and must not be reported as transparently translated to address zero. */
static void test_an_access_matching_neither_register_is_not_transparent(void) {
  ap_m68030_tt_t tt = supervisor_data_reads();
  ap_m68030_access_t a = access_at(0x80000000, FC_SUPERVISOR_DATA, true);
  ap_m68030_tt_result_t r = ap_m68030_tt_translate(&tt, NULL, &a);
  TEST_ASSERT_FALSE(r.transparent);
  TEST_ASSERT_FALSE(r.cache_inhibit);
}

/* Both registers absent is the reset state and must be handled without a
 * dereference. */
static void test_no_registers_at_all_is_not_transparent(void) {
  ap_m68030_access_t a = access_at(0x00123456, FC_SUPERVISOR_DATA, true);
  TEST_ASSERT_FALSE(ap_m68030_tt_translate(NULL, NULL, &a).transparent);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_matching_access_is_translated_to_the_same_address);
  RUN_TEST(test_a_disabled_register_never_matches);
  RUN_TEST(test_only_the_eight_high_order_address_bits_are_compared);
  RUN_TEST(test_the_address_mask_widens_the_block);
  RUN_TEST(test_a_different_function_code_does_not_match);
  RUN_TEST(test_a_full_function_code_mask_matches_every_space);
  RUN_TEST(test_a_read_only_register_does_not_match_a_write);
  RUN_TEST(test_masking_read_write_makes_both_directions_transparent);
  RUN_TEST(test_a_read_modify_write_is_not_transparent_unless_read_write_is_masked);
  RUN_TEST(test_a_read_modify_write_is_transparent_when_read_write_is_masked);
  RUN_TEST(test_either_register_matching_is_enough);
  RUN_TEST(test_cache_inhibit_is_ored_when_both_registers_match);
  RUN_TEST(test_a_non_matching_register_does_not_contribute_cache_inhibit);
  RUN_TEST(test_an_access_matching_neither_register_is_not_transparent);
  RUN_TEST(test_no_registers_at_all_is_not_transparent);
  return UNITY_END();
}
