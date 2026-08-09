/* MC68030 MMU status register.
 *
 * Cited to MC68030 User's Manual 3ed §9.7.4 and Table 9-3, and to the M68000
 * Family Programmer's Reference Manual 1992 PTEST page for the bit positions.
 *
 * The register wears one name but means two different things: Table 9-3 gives a
 * separate column for PTEST level 0 (which searches the ATC) and for PTEST
 * levels 1-7 (which searches the tables). Most of these tests exist to pin the
 * difference, because a bit that one form defines the other clears outright.
 */

#include "cpu/m68030/ap_m68030_mmusr.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define TEST_FC_SUPERVISOR 5u /* FC2 set: supervisor data */
#define TEST_FC_USER 1u       /* FC2 clear: user data */
#define PAGE_BITS 12u
#define TEST_ADDRESS 0x00012345u
#define PAGE_FRAME 0x00A00000u

static ap_m68030_atc_t empty_atc(void) {
  ap_m68030_atc_t atc;
  ap_m68030_atc_flush(&atc);
  return atc;
}

/* ---------------------------------------------------------------------------
 * The bit layout, from the PRM's PTEST page.
 * ------------------------------------------------------------------------- */

/* B(15) L(14) S(13) W(11) I(10) M(9) T(6) N(2-0), with 12, 8, 7, 5, 4 and 3
 * carrying no field. Each bit is checked on its own so a transposition cannot
 * hide behind another bit being right. */
static void test_each_field_packs_to_its_documented_bit(void) {
  TEST_ASSERT_EQUAL_HEX16(
      0x8000, ap_m68030_mmusr_pack(&(ap_m68030_mmusr_t){.bus_error = true}));
  TEST_ASSERT_EQUAL_HEX16(
      0x4000,
      ap_m68030_mmusr_pack(&(ap_m68030_mmusr_t){.limit_violation = true}));
  TEST_ASSERT_EQUAL_HEX16(
      0x2000,
      ap_m68030_mmusr_pack(&(ap_m68030_mmusr_t){.supervisor_violation = true}));
  TEST_ASSERT_EQUAL_HEX16(
      0x0800,
      ap_m68030_mmusr_pack(&(ap_m68030_mmusr_t){.write_protected = true}));
  TEST_ASSERT_EQUAL_HEX16(
      0x0400, ap_m68030_mmusr_pack(&(ap_m68030_mmusr_t){.invalid = true}));
  TEST_ASSERT_EQUAL_HEX16(
      0x0200, ap_m68030_mmusr_pack(&(ap_m68030_mmusr_t){.modified = true}));
  TEST_ASSERT_EQUAL_HEX16(
      0x0040, ap_m68030_mmusr_pack(&(ap_m68030_mmusr_t){.transparent = true}));
  TEST_ASSERT_EQUAL_HEX16(
      0x0005, ap_m68030_mmusr_pack(&(ap_m68030_mmusr_t){.levels = 5}));
}

/* N is three bits, so a level count that does not fit is truncated rather than
 * spilling into the fields above it. */
static void test_the_level_count_occupies_only_its_three_bits(void) {
  TEST_ASSERT_EQUAL_HEX16(
      0x0007, ap_m68030_mmusr_pack(&(ap_m68030_mmusr_t){.levels = 7}));
  TEST_ASSERT_EQUAL_HEX16(
      0x0000, ap_m68030_mmusr_pack(&(ap_m68030_mmusr_t){.levels = 8}));
}

/* The bits the figure shows as zero must stay zero however the struct is
 * filled: nothing may leak into 12, 8, 7, 5, 4 or 3. */
static void test_the_unassigned_bits_are_never_set(void) {
  const ap_m68030_mmusr_t everything = {.bus_error = true,
                                        .limit_violation = true,
                                        .supervisor_violation = true,
                                        .write_protected = true,
                                        .invalid = true,
                                        .modified = true,
                                        .transparent = true,
                                        .levels = 7};
  const uint16_t word = ap_m68030_mmusr_pack(&everything);
  TEST_ASSERT_EQUAL_HEX16(0, word & 0x11B8u);
}

static void test_pack_and_unpack_round_trip(void) {
  const ap_m68030_mmusr_t original = {.bus_error = true,
                                      .supervisor_violation = true,
                                      .modified = true,
                                      .levels = 3};
  const ap_m68030_mmusr_t back =
      ap_m68030_mmusr_unpack(ap_m68030_mmusr_pack(&original));

  TEST_ASSERT_TRUE(back.bus_error);
  TEST_ASSERT_TRUE(back.supervisor_violation);
  TEST_ASSERT_TRUE(back.modified);
  TEST_ASSERT_FALSE(back.limit_violation);
  TEST_ASSERT_FALSE(back.transparent);
  TEST_ASSERT_EQUAL_UINT8(3, back.levels);
}

/* ---------------------------------------------------------------------------
 * PTEST level 0: the ATC column of Table 9-3.
 * ------------------------------------------------------------------------- */

/* "The I bit is set if the translation for the specified logical address is not
 * resident in the ATC." */
static void test_an_address_absent_from_the_atc_reports_invalid(void) {
  ap_m68030_atc_t atc = empty_atc();
  const ap_m68030_mmusr_t r = ap_m68030_mmusr_probe_atc(
      &atc, TEST_FC_SUPERVISOR, TEST_ADDRESS, PAGE_BITS, false);

  TEST_ASSERT_TRUE(r.invalid);
  TEST_ASSERT_FALSE(r.bus_error);
}

/* A resident entry reports its WP and M, and does not report invalid. */
static void test_a_resident_entry_reports_its_protection_and_modified_bits(void) {
  ap_m68030_atc_t atc = empty_atc();
  (void)ap_m68030_atc_insert(&atc, TEST_FC_SUPERVISOR, TEST_ADDRESS, PAGE_BITS,
                             PAGE_FRAME >> 8, true /* WP */,
                             false /* CI */, true /* M */, false /* B */);

  const ap_m68030_mmusr_t r = ap_m68030_mmusr_probe_atc(
      &atc, TEST_FC_SUPERVISOR, TEST_ADDRESS, PAGE_BITS, false);

  TEST_ASSERT_FALSE(r.invalid);
  TEST_ASSERT_TRUE(r.write_protected);
  TEST_ASSERT_TRUE(r.modified);
}

/* "This bit is set if the bus error bit is set in the ATC entry", and I is set
 * with it -- so a faulting entry reports both. */
static void test_an_entry_with_b_set_reports_bus_error_and_invalid(void) {
  ap_m68030_atc_t atc = empty_atc();
  (void)ap_m68030_atc_insert(&atc, TEST_FC_SUPERVISOR, TEST_ADDRESS, PAGE_BITS,
                             0, false, false, false, true /* B */);

  const ap_m68030_mmusr_t r = ap_m68030_mmusr_probe_atc(
      &atc, TEST_FC_SUPERVISOR, TEST_ADDRESS, PAGE_BITS, false);

  TEST_ASSERT_TRUE(r.bus_error);
  TEST_ASSERT_TRUE(r.invalid);
}

/* "This bit is cleared" -- L, S and N describe a table search, and a level 0
 * probe never performs one, so they must be zero even for an entry that was
 * created by a search which did hit a limit. */
static void test_a_level_0_probe_never_reports_limit_supervisor_or_levels(void) {
  ap_m68030_atc_t atc = empty_atc();
  (void)ap_m68030_atc_insert(&atc, TEST_FC_SUPERVISOR, TEST_ADDRESS, PAGE_BITS,
                             PAGE_FRAME >> 8, false, false, false, true);

  const ap_m68030_mmusr_t r = ap_m68030_mmusr_probe_atc(
      &atc, TEST_FC_SUPERVISOR, TEST_ADDRESS, PAGE_BITS, false);

  TEST_ASSERT_FALSE(r.limit_violation);
  TEST_ASSERT_FALSE(r.supervisor_violation);
  TEST_ASSERT_EQUAL_UINT8(0, r.levels);
}

/* "If the T bit is set, all remaining MMUSR bits are undefined." A transparent
 * match therefore reports T and nothing else -- notably not invalid, even
 * though the address is absent from the ATC. */
static void test_a_transparent_match_reports_only_the_t_bit(void) {
  ap_m68030_atc_t atc = empty_atc();
  const ap_m68030_mmusr_t r = ap_m68030_mmusr_probe_atc(
      &atc, TEST_FC_SUPERVISOR, TEST_ADDRESS, PAGE_BITS, true);

  TEST_ASSERT_TRUE(r.transparent);
  TEST_ASSERT_FALSE(r.invalid);
  TEST_ASSERT_EQUAL_HEX16(0x0040, ap_m68030_mmusr_pack(&r));
}

/* The ATC tag includes the function code, so a probe with the wrong FC misses
 * the entry rather than reporting someone else's translation. */
static void test_a_probe_with_a_different_function_code_misses(void) {
  ap_m68030_atc_t atc = empty_atc();
  (void)ap_m68030_atc_insert(&atc, TEST_FC_SUPERVISOR, TEST_ADDRESS, PAGE_BITS,
                             PAGE_FRAME >> 8, false, false, false, false);

  const ap_m68030_mmusr_t r = ap_m68030_mmusr_probe_atc(
      &atc, TEST_FC_USER, TEST_ADDRESS, PAGE_BITS, false);
  TEST_ASSERT_TRUE(r.invalid);
}

/* ---------------------------------------------------------------------------
 * PTEST levels 1-7: the table-search column of Table 9-3.
 * ------------------------------------------------------------------------- */

static ap_m68030_walk_result_t clean_search(void) {
  ap_m68030_walk_result_t r = {0};
  ap_m68030_search_reset(&r.search);
  r.ok = true;
  r.levels_walked = 3;
  return r;
}

/* "This 3-bit field contains the actual number of tables accessed during the
 * search", and T "is set to zero" for this form. */
static void test_a_successful_search_reports_its_level_count_and_no_t(void) {
  ap_m68030_walk_result_t search = clean_search();
  const ap_m68030_mmusr_t r =
      ap_m68030_mmusr_from_search(&search, TEST_FC_SUPERVISOR);

  TEST_ASSERT_EQUAL_UINT8(3, r.levels);
  TEST_ASSERT_FALSE(r.transparent);
  TEST_ASSERT_FALSE(r.invalid);
}

/* A search that stopped because PTEST asked it to is not a failed search.
 * "The search ends at the specified level" is one of the two normal endings,
 * beside the tables terminating -- so I stays clear and N reports the tables
 * actually accessed. A truncated probe reported as invalid would tell a fault
 * handler its tree is broken when it merely asked a shallow question. */
static void test_a_search_stopped_at_its_level_is_not_invalid(void) {
  ap_m68030_walk_result_t search = clean_search();
  search.ok = false;
  search.truncated = true;
  search.levels_walked = 1;

  const ap_m68030_mmusr_t r =
      ap_m68030_mmusr_from_search(&search, TEST_FC_SUPERVISOR);

  TEST_ASSERT_FALSE(r.invalid);
  TEST_ASSERT_FALSE(r.bus_error);
  TEST_ASSERT_FALSE(r.limit_violation);
  TEST_ASSERT_EQUAL_UINT8(1, r.levels);
}

/* "The I bit is set if ... either the B or L bits of the MMUSR are set during
 * the table search." I is deliberately broader than "found an invalid
 * descriptor", so a limit violation sets both L and I. */
static void test_a_limit_violation_sets_both_l_and_i(void) {
  ap_m68030_walk_result_t search = clean_search();
  search.ok = false;
  ap_m68030_search_fail_limit(&search.search);

  const ap_m68030_mmusr_t r =
      ap_m68030_mmusr_from_search(&search, TEST_FC_SUPERVISOR);

  TEST_ASSERT_TRUE(r.limit_violation);
  TEST_ASSERT_TRUE(r.invalid);
}

/* B and I are separate causes that both set I: a real bus error reports B,
 * where an invalid DT field does not. This is the distinction the ATC's single
 * B bit cannot express. */
static void test_a_bus_error_reports_b_where_an_invalid_descriptor_does_not(void) {
  ap_m68030_walk_result_t errored = clean_search();
  errored.ok = false;
  errored.bus_error = true;
  ap_m68030_search_fail_invalid(&errored.search);

  ap_m68030_walk_result_t invalid = clean_search();
  invalid.ok = false;
  ap_m68030_search_fail_invalid(&invalid.search);

  const ap_m68030_mmusr_t from_error =
      ap_m68030_mmusr_from_search(&errored, TEST_FC_SUPERVISOR);
  const ap_m68030_mmusr_t from_invalid =
      ap_m68030_mmusr_from_search(&invalid, TEST_FC_SUPERVISOR);

  TEST_ASSERT_TRUE(from_error.bus_error);
  TEST_ASSERT_TRUE(from_error.invalid);
  TEST_ASSERT_FALSE(from_invalid.bus_error);
  TEST_ASSERT_TRUE(from_invalid.invalid);
}

/* "This bit is set if the S bit of a long format table descriptor or long
 * format page descriptor encountered during the search is set, and the FC2 bit
 * of the function code specified by the PTEST instruction is not equal to one."
 * So S reports the access against the tree, not the tree alone. */
static void test_supervisor_violation_depends_on_the_probed_function_code(void) {
  ap_m68030_walk_result_t search = clean_search();
  ap_m68030_search_accumulate(&search.search, false, true /* S */, false);

  const ap_m68030_mmusr_t as_user =
      ap_m68030_mmusr_from_search(&search, TEST_FC_USER);
  const ap_m68030_mmusr_t as_supervisor =
      ap_m68030_mmusr_from_search(&search, TEST_FC_SUPERVISOR);

  TEST_ASSERT_TRUE(as_user.supervisor_violation);
  TEST_ASSERT_FALSE(as_supervisor.supervisor_violation);
}

/* WP accumulated anywhere in the tree reaches W, and the page's M reaches M. */
static void test_write_protection_and_modified_reach_the_register(void) {
  ap_m68030_walk_result_t search = clean_search();
  ap_m68030_search_accumulate(&search.search, true /* WP */, false, false);
  search.page_modified = true;

  const ap_m68030_mmusr_t r =
      ap_m68030_mmusr_from_search(&search, TEST_FC_SUPERVISOR);

  TEST_ASSERT_TRUE(r.write_protected);
  TEST_ASSERT_TRUE(r.modified);
}

/* "The W bit is undefined if the I bit is set", and the same for S and M. They
 * are cleared so our output cannot imply a guarantee the manual does not make.
 * A search that both write-protects and fails must therefore report I alone. */
static void test_an_invalid_search_reports_no_protection_bits(void) {
  ap_m68030_walk_result_t search = clean_search();
  search.ok = false;
  ap_m68030_search_accumulate(&search.search, true /* WP */, true /* S */,
                              false);
  search.page_modified = true;
  ap_m68030_search_fail_invalid(&search.search);

  const ap_m68030_mmusr_t r =
      ap_m68030_mmusr_from_search(&search, TEST_FC_USER);

  TEST_ASSERT_TRUE(r.invalid);
  TEST_ASSERT_FALSE(r.write_protected);
  TEST_ASSERT_FALSE(r.supervisor_violation);
  TEST_ASSERT_FALSE(r.modified);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_each_field_packs_to_its_documented_bit);
  RUN_TEST(test_the_level_count_occupies_only_its_three_bits);
  RUN_TEST(test_the_unassigned_bits_are_never_set);
  RUN_TEST(test_pack_and_unpack_round_trip);
  RUN_TEST(test_an_address_absent_from_the_atc_reports_invalid);
  RUN_TEST(test_a_resident_entry_reports_its_protection_and_modified_bits);
  RUN_TEST(test_an_entry_with_b_set_reports_bus_error_and_invalid);
  RUN_TEST(test_a_level_0_probe_never_reports_limit_supervisor_or_levels);
  RUN_TEST(test_a_transparent_match_reports_only_the_t_bit);
  RUN_TEST(test_a_probe_with_a_different_function_code_misses);
  RUN_TEST(test_a_successful_search_reports_its_level_count_and_no_t);
  RUN_TEST(test_a_search_stopped_at_its_level_is_not_invalid);
  RUN_TEST(test_a_limit_violation_sets_both_l_and_i);
  RUN_TEST(test_a_bus_error_reports_b_where_an_invalid_descriptor_does_not);
  RUN_TEST(test_supervisor_violation_depends_on_the_probed_function_code);
  RUN_TEST(test_write_protection_and_modified_reach_the_register);
  RUN_TEST(test_an_invalid_search_reports_no_protection_bits);
  return UNITY_END();
}
