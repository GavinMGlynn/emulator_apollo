/* MC68030 translation table descriptors and accumulated search state.
 *
 * Cited to MC68030 User's Manual 3ed §9.5.1.1 pp. 9-20 ff.
 *
 * The two facts worth stating up front, because both are easy to implement
 * wrongly and neither shows up until an OS is running:
 *
 *  - DT=$2 and DT=$3 mean *different things* depending on the level they are
 *    found at. In a pointer table they describe the next table's format; in a
 *    page table the identical encoding is an indirect descriptor.
 *  - Protection accumulates down the tree. "The states of all WP bits
 *    encountered during a table search are logically ORed", so a permissive
 *    page reached through a protected pointer is still protected.
 */

#include "cpu/m68030/ap_m68030_desc.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* [030] 9.5.1.1: "$0 INVALID ... A table search ends when an invalid descriptor
 * is encountered." */
static void test_an_invalid_descriptor_ends_the_search(void) {
  TEST_ASSERT_EQUAL(AP_M68030_ROLE_INVALID,
                    ap_m68030_desc_role(AP_M68030_DT_INVALID, false));
  TEST_ASSERT_EQUAL(AP_M68030_ROLE_INVALID,
                    ap_m68030_desc_role(AP_M68030_DT_INVALID, true));
  TEST_ASSERT_TRUE(ap_m68030_desc_terminates(AP_M68030_ROLE_INVALID));
}

/* "The page descriptor is a normal page descriptor when it resides in a page
 * table ... A page descriptor at a higher level is an early termination page
 * descriptor. A table search ends when a page descriptor of either type is
 * encountered." */
static void test_a_page_descriptor_above_the_page_table_is_early_termination(void) {
  TEST_ASSERT_EQUAL(AP_M68030_ROLE_PAGE,
                    ap_m68030_desc_role(AP_M68030_DT_PAGE, true));
  TEST_ASSERT_EQUAL(AP_M68030_ROLE_EARLY_PAGE,
                    ap_m68030_desc_role(AP_M68030_DT_PAGE, false));
  TEST_ASSERT_TRUE(ap_m68030_desc_terminates(AP_M68030_ROLE_PAGE));
  TEST_ASSERT_TRUE(ap_m68030_desc_terminates(AP_M68030_ROLE_EARLY_PAGE));
}

/* The context-dependent encoding, and the reason ap_m68030_desc_role() takes
 * the level at all: "When used in a page table (bottom level of a translation
 * tree), this code identifies an indirect descriptor". A walk that ignored the
 * level would follow an indirect descriptor as if it were a pointer table. */
static void test_the_valid_encodings_are_tables_above_and_indirect_below(void) {
  TEST_ASSERT_EQUAL(AP_M68030_ROLE_TABLE,
                    ap_m68030_desc_role(AP_M68030_DT_VALID_4BYTE, false));
  TEST_ASSERT_EQUAL(AP_M68030_ROLE_TABLE,
                    ap_m68030_desc_role(AP_M68030_DT_VALID_8BYTE, false));
  TEST_ASSERT_EQUAL(AP_M68030_ROLE_INDIRECT,
                    ap_m68030_desc_role(AP_M68030_DT_VALID_4BYTE, true));
  TEST_ASSERT_EQUAL(AP_M68030_ROLE_INDIRECT,
                    ap_m68030_desc_role(AP_M68030_DT_VALID_8BYTE, true));
}

/* An indirect descriptor redirects the search rather than ending it. */
static void test_a_table_or_indirect_descriptor_does_not_end_the_search(void) {
  TEST_ASSERT_FALSE(ap_m68030_desc_terminates(AP_M68030_ROLE_TABLE));
  TEST_ASSERT_FALSE(ap_m68030_desc_terminates(AP_M68030_ROLE_INDIRECT));
}

/* "$2 VALID 4 BYTE ... multiplies the index for the next table by four";
 * "$3 VALID 8 BYTE ... by eight". */
static void test_the_next_table_stride_follows_the_descriptor_format(void) {
  TEST_ASSERT_EQUAL_UINT32(4, ap_m68030_desc_next_table_stride(AP_M68030_DT_VALID_4BYTE));
  TEST_ASSERT_EQUAL_UINT32(8, ap_m68030_desc_next_table_stride(AP_M68030_DT_VALID_8BYTE));
  TEST_ASSERT_EQUAL_UINT32(0, ap_m68030_desc_next_table_stride(AP_M68030_DT_PAGE));
  TEST_ASSERT_EQUAL_UINT32(0, ap_m68030_desc_next_table_stride(AP_M68030_DT_INVALID));
}

/* [030] 9.5.1.1 LIU: "When the bit is cleared, the limit is an unsigned upper
 * limit, and the index value must be less than or equal to the LIMIT." */
static void test_an_upper_limit_admits_indices_up_to_and_including_it(void) {
  TEST_ASSERT_TRUE(ap_m68030_desc_index_within_limit(10, false, 0));
  TEST_ASSERT_TRUE(ap_m68030_desc_index_within_limit(10, false, 10));
  TEST_ASSERT_FALSE(ap_m68030_desc_index_within_limit(10, false, 11));
}

/* "When the LIU bit is set, the LIMIT field contains the unsigned lower limit;
 * the index value ... must be greater than or equal to the value in the LIMIT
 * field." */
static void test_a_lower_limit_admits_indices_from_it_upward(void) {
  TEST_ASSERT_FALSE(ap_m68030_desc_index_within_limit(10, true, 9));
  TEST_ASSERT_TRUE(ap_m68030_desc_index_within_limit(10, true, 10));
  TEST_ASSERT_TRUE(ap_m68030_desc_index_within_limit(10, true, 11));
}

/* LIMIT is a 15-bit field, so bits above it are not part of the bound. */
static void test_only_fifteen_bits_of_the_limit_field_are_used(void) {
  TEST_ASSERT_TRUE(ap_m68030_desc_index_within_limit(0x8000u, false, 0));
  TEST_ASSERT_FALSE(ap_m68030_desc_index_within_limit(0x8000u, false, 1));
}

/* [030] 9.5.1.1 PAGE ADDRESS: a 24-bit field naming a 256-byte-aligned base. */
static void test_a_page_address_field_scales_to_a_physical_base(void) {
  TEST_ASSERT_EQUAL_HEX32(0x00123400,
                          ap_m68030_desc_page_address(0x001234, 8));
}

/* "When the page size is larger than 256 bytes, one or more of the least
 * significant bits of this field are not used. The number of unused bits is
 * equal to the PS field value in the TC register minus eight." With 4K pages
 * that is four unused bits, so the base is 4K aligned. */
static void test_a_larger_page_ignores_low_bits_of_the_page_address_field(void) {
  /* Field 0x00123F -> 0x00123F00, which is not 4K aligned; the unused low bits
   * of the field must drop out, leaving 0x00123000. */
  TEST_ASSERT_EQUAL_HEX32(0x00123000,
                          ap_m68030_desc_page_address(0x00123F, 12));
}

/* The headline protection rule: "The states of all WP bits encountered during a
 * table search are logically ORed". A page whose own descriptor is permissive
 * is still write-protected if any pointer above it was protected. */
static void test_write_protection_anywhere_in_the_tree_protects_the_page(void) {
  ap_m68030_search_t s;
  ap_m68030_search_reset(&s);
  ap_m68030_search_accumulate(&s, true, false, false);  /* a protected pointer */
  ap_m68030_search_accumulate(&s, false, false, false); /* a permissive page */
  TEST_ASSERT_TRUE(s.write_protected);
  TEST_ASSERT_FALSE(ap_m68030_search_permits_write(&s));
}

/* "this protection is absolute" -- supervisor does not override WP. */
static void test_write_protection_is_absolute_even_for_supervisor(void) {
  ap_m68030_search_t s;
  ap_m68030_search_reset(&s);
  ap_m68030_search_accumulate(&s, true, false, false);
  TEST_ASSERT_FALSE(ap_m68030_search_permits_write(&s));
  TEST_ASSERT_TRUE(ap_m68030_search_permits_access(&s, true));
}

/* An unprotected search permits a write. */
static void test_an_unprotected_search_permits_a_write(void) {
  ap_m68030_search_t s;
  ap_m68030_search_reset(&s);
  ap_m68030_search_accumulate(&s, false, false, false);
  TEST_ASSERT_TRUE(ap_m68030_search_permits_write(&s));
}

/* S accumulates the same way: an access "is not restricted to supervisor-only
 * unless the access is restricted by some other level of the translation
 * tree". */
static void test_a_supervisor_only_pointer_restricts_the_whole_path(void) {
  ap_m68030_search_t s;
  ap_m68030_search_reset(&s);
  ap_m68030_search_accumulate(&s, false, true, false);
  ap_m68030_search_accumulate(&s, false, false, false);
  TEST_ASSERT_FALSE(ap_m68030_search_permits_access(&s, false));
  TEST_ASSERT_TRUE(ap_m68030_search_permits_access(&s, true));
}

/* CI accumulates too, matching the TT registers' ORed CI. */
static void test_cache_inhibit_accumulates_across_the_search(void) {
  ap_m68030_search_t s;
  ap_m68030_search_reset(&s);
  ap_m68030_search_accumulate(&s, false, false, true);
  ap_m68030_search_accumulate(&s, false, false, false);
  TEST_ASSERT_TRUE(s.cache_inhibited);
}

/* "An out-of-bounds access causes the B bit in the ATC entry for the address to
 * be set and causes the table search to abort." */
static void test_a_limit_violation_denies_the_access(void) {
  ap_m68030_search_t s;
  ap_m68030_search_reset(&s);
  ap_m68030_search_fail_limit(&s);
  TEST_ASSERT_TRUE(s.limit_violation);
  TEST_ASSERT_FALSE(ap_m68030_search_permits_access(&s, true));
  TEST_ASSERT_FALSE(ap_m68030_search_permits_write(&s));
}

static void test_an_invalid_search_denies_the_access(void) {
  ap_m68030_search_t s;
  ap_m68030_search_reset(&s);
  ap_m68030_search_fail_invalid(&s);
  TEST_ASSERT_FALSE(ap_m68030_search_permits_access(&s, true));
}

/* [030] 9.5.1.1 M: set "before a write operation to a page for which the M bit
 * is zero". */
static void test_a_write_to_an_unmodified_page_sets_the_modified_bit(void) {
  ap_m68030_search_t s;
  ap_m68030_search_reset(&s);
  TEST_ASSERT_TRUE(ap_m68030_search_should_set_modified(&s, true, false, false, false));
}

/* "An access is considered to be a write for updating purposes if either the
 * R/W or RMC signal is low" -- so the read half of a read-modify-write already
 * counts as a write for the M bit. */
static void test_a_read_modify_write_counts_as_a_write_for_the_modified_bit(void) {
  ap_m68030_search_t s;
  ap_m68030_search_reset(&s);
  TEST_ASSERT_TRUE(ap_m68030_search_should_set_modified(&s, false, true, false, false));
}

/* A plain read never sets it. */
static void test_a_read_does_not_set_the_modified_bit(void) {
  ap_m68030_search_t s;
  ap_m68030_search_reset(&s);
  TEST_ASSERT_FALSE(ap_m68030_search_should_set_modified(&s, false, false, false, false));
}

/* "except after a descriptor with the WP bit set is encountered" -- a
 * write-protected page must not be marked modified by a write that was denied. */
static void test_a_write_protected_page_is_not_marked_modified(void) {
  ap_m68030_search_t s;
  ap_m68030_search_reset(&s);
  ap_m68030_search_accumulate(&s, true, false, false);
  TEST_ASSERT_FALSE(ap_m68030_search_should_set_modified(&s, true, false, true, false));
}

/* "or after a supervisor violation is encountered". */
static void test_a_supervisor_violation_does_not_mark_the_page_modified(void) {
  ap_m68030_search_t s;
  ap_m68030_search_reset(&s);
  ap_m68030_search_accumulate(&s, false, true, false);
  TEST_ASSERT_FALSE(ap_m68030_search_should_set_modified(&s, true, false, false, false));
  /* ...but a legitimate supervisor write to the same page still does. */
  TEST_ASSERT_TRUE(ap_m68030_search_should_set_modified(&s, true, false, true, false));
}

/* "for which the M bit is zero" -- an already-modified page is not rewritten,
 * which matters because the update costs a bus cycle. */
static void test_an_already_modified_page_is_not_written_again(void) {
  ap_m68030_search_t s;
  ap_m68030_search_reset(&s);
  TEST_ASSERT_FALSE(ap_m68030_search_should_set_modified(&s, true, false, true, true));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_an_invalid_descriptor_ends_the_search);
  RUN_TEST(test_a_page_descriptor_above_the_page_table_is_early_termination);
  RUN_TEST(test_the_valid_encodings_are_tables_above_and_indirect_below);
  RUN_TEST(test_a_table_or_indirect_descriptor_does_not_end_the_search);
  RUN_TEST(test_the_next_table_stride_follows_the_descriptor_format);
  RUN_TEST(test_an_upper_limit_admits_indices_up_to_and_including_it);
  RUN_TEST(test_a_lower_limit_admits_indices_from_it_upward);
  RUN_TEST(test_only_fifteen_bits_of_the_limit_field_are_used);
  RUN_TEST(test_a_page_address_field_scales_to_a_physical_base);
  RUN_TEST(test_a_larger_page_ignores_low_bits_of_the_page_address_field);
  RUN_TEST(test_write_protection_anywhere_in_the_tree_protects_the_page);
  RUN_TEST(test_write_protection_is_absolute_even_for_supervisor);
  RUN_TEST(test_an_unprotected_search_permits_a_write);
  RUN_TEST(test_a_supervisor_only_pointer_restricts_the_whole_path);
  RUN_TEST(test_cache_inhibit_accumulates_across_the_search);
  RUN_TEST(test_a_limit_violation_denies_the_access);
  RUN_TEST(test_an_invalid_search_denies_the_access);
  RUN_TEST(test_a_write_to_an_unmodified_page_sets_the_modified_bit);
  RUN_TEST(test_a_read_modify_write_counts_as_a_write_for_the_modified_bit);
  RUN_TEST(test_a_read_does_not_set_the_modified_bit);
  RUN_TEST(test_a_write_protected_page_is_not_marked_modified);
  RUN_TEST(test_a_supervisor_violation_does_not_mark_the_page_modified);
  RUN_TEST(test_an_already_modified_page_is_not_written_again);
  return UNITY_END();
}
