/* MC68851 address translation descriptors, `[68851]` §5.1.5 and Figures 5-10
 * and 5-12 through 5-20, read from the page images.
 *
 * The risk in this module is not the bit positions -- it is that a descriptor
 * does not know its own type or width. Both come from outside it, and the tests
 * below spend most of their effort on that.
 */

#include "cpu/m68851/ap_m68851_descriptor.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------------------
 * Figure 5-10: the type determination table.
 * ------------------------------------------------------------------------- */

static void test_figure_5_10_in_full(void) {
  /* All twelve cells, transcribed. Rows are DT, columns are search state. */
  const ap_m68851_descriptor_kind_t expected[4][3] = {
      /* $0 invalid */
      {AP_M68851_DESC_INVALID, AP_M68851_DESC_INVALID, AP_M68851_DESC_INVALID},
      /* $1 page descriptor */
      {AP_M68851_DESC_PAGE_TYPE_2, AP_M68851_DESC_PAGE_TYPE_1,
       AP_M68851_DESC_PAGE_TYPE_1},
      /* $2 short: table, then indirect, then illegal-as-invalid */
      {AP_M68851_DESC_TABLE, AP_M68851_DESC_INDIRECT, AP_M68851_DESC_INVALID},
      /* $3 long: the same three */
      {AP_M68851_DESC_TABLE, AP_M68851_DESC_INDIRECT, AP_M68851_DESC_INVALID},
  };
  const ap_m68851_search_state_t states[3] = {
      AP_M68851_SEARCH_TI_FIELDS_REMAIN,
      AP_M68851_SEARCH_TI_FIELDS_EXHAUSTED,
      AP_M68851_SEARCH_INDIRECT_DESCRIPTOR_SEEN,
  };

  for (unsigned dt = 0; dt < 4u; dt++) {
    for (unsigned s = 0; s < 3u; s++) {
      TEST_ASSERT_EQUAL_INT(
          expected[dt][s],
          ap_m68851_descriptor_kind((ap_m68851_descriptor_type_t)dt, states[s]));
    }
  }
}

static void test_the_same_dt_is_a_table_or_an_indirect_by_search_state(void) {
  /* The heart of Figure 5-10: identical bits, different meaning. A model that
   * read the type from the descriptor alone would treat every indirect
   * descriptor as a table and walk into a page frame. */
  TEST_ASSERT_EQUAL_INT(AP_M68851_DESC_TABLE,
                        ap_m68851_descriptor_kind(AP_M68851_DT_VALID_4_BYTE,
                                                  AP_M68851_SEARCH_TI_FIELDS_REMAIN));
  TEST_ASSERT_EQUAL_INT(AP_M68851_DESC_INDIRECT,
                        ap_m68851_descriptor_kind(AP_M68851_DT_VALID_4_BYTE,
                                                  AP_M68851_SEARCH_TI_FIELDS_EXHAUSTED));
}

static void test_a_page_descriptor_is_type_2_only_when_indices_remain(void) {
  /* A type-2 has a limit because there are still levels below it to bound; a
   * type-1 arises when there is nothing left to bound. */
  TEST_ASSERT_EQUAL_INT(AP_M68851_DESC_PAGE_TYPE_2,
                        ap_m68851_descriptor_kind(AP_M68851_DT_PAGE_DESCRIPTOR,
                                                  AP_M68851_SEARCH_TI_FIELDS_REMAIN));
  TEST_ASSERT_EQUAL_INT(AP_M68851_DESC_PAGE_TYPE_1,
                        ap_m68851_descriptor_kind(AP_M68851_DT_PAGE_DESCRIPTOR,
                                                  AP_M68851_SEARCH_TI_FIELDS_EXHAUSTED));
  TEST_ASSERT_EQUAL_INT(AP_M68851_DESC_PAGE_TYPE_1,
                        ap_m68851_descriptor_kind(AP_M68851_DT_PAGE_DESCRIPTOR,
                                                  AP_M68851_SEARCH_INDIRECT_DESCRIPTOR_SEEN));
}

static void test_an_indirect_pointing_at_an_indirect_is_illegal_and_invalid(void) {
  /* "The table entries marked 'illegal' are not valid configurations and are
   * treated as the 'invalid' type by the MC68851." Both facts, separately:
   * the configuration is illegal, and the part still terminates rather than
   * following the chain. */
  for (unsigned dt = 2; dt < 4u; dt++) {
    TEST_ASSERT_TRUE(ap_m68851_descriptor_is_illegal(
        (ap_m68851_descriptor_type_t)dt,
        AP_M68851_SEARCH_INDIRECT_DESCRIPTOR_SEEN));
    TEST_ASSERT_EQUAL_INT(AP_M68851_DESC_INVALID,
                          ap_m68851_descriptor_kind(
                              (ap_m68851_descriptor_type_t)dt,
                              AP_M68851_SEARCH_INDIRECT_DESCRIPTOR_SEEN));
  }
}

static void test_only_those_two_cells_are_illegal(void) {
  /* Ten of the twelve cells are legal configurations, including every invalid
   * one -- an invalid descriptor after an indirection is a normal way for a
   * search to fail, not a malformed table. */
  for (unsigned dt = 0; dt < 4u; dt++) {
    for (unsigned s = 0; s < 3u; s++) {
      const bool illegal = ap_m68851_descriptor_is_illegal(
          (ap_m68851_descriptor_type_t)dt, (ap_m68851_search_state_t)s);
      const bool expected =
          (s == AP_M68851_SEARCH_INDIRECT_DESCRIPTOR_SEEN) && (dt >= 2u);
      TEST_ASSERT_EQUAL_INT(expected, illegal);
    }
  }
}

static void test_the_previous_descriptor_decides_the_next_ones_width(void) {
  /* "The value of the previous descriptor determines whether the current
   * descriptor is of the long or short format." Reading at the wrong width is
   * not a wrong field -- it is a misaligned read of the whole table. */
  TEST_ASSERT_EQUAL_UINT(4u,
                         ap_m68851_descriptor_next_width(AP_M68851_DT_VALID_4_BYTE));
  TEST_ASSERT_EQUAL_UINT(8u,
                         ap_m68851_descriptor_next_width(AP_M68851_DT_VALID_8_BYTE));
  TEST_ASSERT_EQUAL_UINT(0u,
                         ap_m68851_descriptor_next_width(AP_M68851_DT_INVALID));
  TEST_ASSERT_EQUAL_UINT(
      0u, ap_m68851_descriptor_next_width(AP_M68851_DT_PAGE_DESCRIPTOR));
}

/* ---------------------------------------------------------------------------
 * The six formats.
 * ------------------------------------------------------------------------- */

static void test_the_short_table_descriptor(void) {
  /* Figure 5-12: table address PA31-PA4 at 31-4, U@3, WP@2, DT@1-0. */
  const ap_m68851_descriptor_t d = ap_m68851_short_table_descriptor(0x1234567Fu);
  TEST_ASSERT_EQUAL_HEX32(0x12345670u, d.address);
  TEST_ASSERT_TRUE(d.used);
  TEST_ASSERT_TRUE(d.write_protect);
  TEST_ASSERT_EQUAL_INT(AP_M68851_DT_VALID_8_BYTE, d.dt);
}

static void test_a_short_descriptor_carries_no_access_levels(void) {
  /* That is what "short" costs: no RAL, no WAL, no SG and no S. A model that
   * synthesised them from the address bits would invent protection. */
  const ap_m68851_descriptor_t d = ap_m68851_short_table_descriptor(0xFFFFFFFFu);
  TEST_ASSERT_EQUAL_UINT(0u, d.read_access_level);
  TEST_ASSERT_EQUAL_UINT(0u, d.write_access_level);
  TEST_ASSERT_FALSE(d.shared_globally);
  TEST_ASSERT_FALSE(d.supervisor);
}

static void test_the_long_table_descriptor(void) {
  /* Figure 5-13: L/U@63, LIMIT@62-48, RAL@47-45, WAL@44-42, SG@41, S@40,
   * zeros@39-36, U@35, WP@34, DT@33-32, table address@31-4. */
  const ap_m68851_descriptor_t d =
      ap_m68851_long_table_descriptor(UINT64_C(0x8ABCEF0F12345678));
  TEST_ASSERT_TRUE(d.lower_limit);
  TEST_ASSERT_EQUAL_UINT(0x0ABCu, d.limit);
  TEST_ASSERT_EQUAL_UINT(0x7u, d.read_access_level);  /* 0xE = 111 0 */
  TEST_ASSERT_EQUAL_UINT(0x3u, d.write_access_level);
  TEST_ASSERT_TRUE(d.shared_globally);
  TEST_ASSERT_TRUE(d.supervisor);
  TEST_ASSERT_TRUE(d.used);
  TEST_ASSERT_TRUE(d.write_protect);
  TEST_ASSERT_EQUAL_INT(AP_M68851_DT_VALID_8_BYTE, d.dt);
  TEST_ASSERT_EQUAL_HEX32(0x12345670u, d.address);
}

static void test_a_table_descriptor_has_no_page_attributes(void) {
  /* Bits 39-36 are drawn as zeros in Figure 5-13, where a page descriptor has
   * G, CI, L and M. None of the four describes a table -- a table is not
   * cacheable, lockable or modifiable as a unit -- so they are absent rather
   * than merely unused. */
  const ap_m68851_descriptor_t d =
      ap_m68851_long_table_descriptor(UINT64_C(0x0000FFFF00000000));
  TEST_ASSERT_FALSE(d.gate);
  TEST_ASSERT_FALSE(d.cache_inhibit);
  TEST_ASSERT_FALSE(d.lock);
  TEST_ASSERT_FALSE(d.modified);
}

static void test_the_short_page_descriptor(void) {
  /* Figure 5-14: page address PA31-PA8 at 31-8, then G@7, CI@6, L@5, M@4,
   * U@3, WP@2, DT@1-0. */
  const ap_m68851_descriptor_t d = ap_m68851_short_page_descriptor(0x123456FEu);
  TEST_ASSERT_EQUAL_HEX32(0x12345600u, d.address);
  TEST_ASSERT_TRUE(d.gate);
  TEST_ASSERT_TRUE(d.cache_inhibit);
  TEST_ASSERT_TRUE(d.lock);
  TEST_ASSERT_TRUE(d.modified);
  TEST_ASSERT_TRUE(d.used);
  TEST_ASSERT_TRUE(d.write_protect);
  TEST_ASSERT_EQUAL_INT(AP_M68851_DT_VALID_4_BYTE, d.dt);
}

static void test_each_page_attribute_sits_at_its_own_bit(void) {
  /* Six adjacent single-bit flags in one nibble-and-a-half: the easiest place
   * in the whole part to be one bit out. Each alone. */
  TEST_ASSERT_TRUE(ap_m68851_short_page_descriptor(0x80u).gate);
  TEST_ASSERT_TRUE(ap_m68851_short_page_descriptor(0x40u).cache_inhibit);
  TEST_ASSERT_TRUE(ap_m68851_short_page_descriptor(0x20u).lock);
  TEST_ASSERT_TRUE(ap_m68851_short_page_descriptor(0x10u).modified);
  TEST_ASSERT_TRUE(ap_m68851_short_page_descriptor(0x08u).used);
  TEST_ASSERT_TRUE(ap_m68851_short_page_descriptor(0x04u).write_protect);

  /* And none of them is set by any other's bit. */
  const ap_m68851_descriptor_t only_gate = ap_m68851_short_page_descriptor(0x80u);
  TEST_ASSERT_FALSE(only_gate.cache_inhibit);
  TEST_ASSERT_FALSE(only_gate.lock);
  TEST_ASSERT_FALSE(only_gate.modified);
  TEST_ASSERT_FALSE(only_gate.used);
  TEST_ASSERT_FALSE(only_gate.write_protect);
}

static void test_a_page_frame_is_256_byte_aligned(void) {
  /* Page address is PA31-PA8: the smallest page the part supports is 256 bytes,
   * so no page frame can start below that granularity. */
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFF00u,
                          ap_m68851_short_page_descriptor(0xFFFFFFFFu).address);
}

static void test_the_long_page_descriptor_carries_page_attributes(void) {
  /* Figures 5-15 and 5-16: G@39, CI@38, L@37, M@36 -- the bits a table
   * descriptor draws as zeros. */
  const ap_m68851_descriptor_t d =
      ap_m68851_long_page_descriptor(UINT64_C(0x000000F012345678), false);
  TEST_ASSERT_TRUE(d.gate);
  TEST_ASSERT_TRUE(d.cache_inhibit);
  TEST_ASSERT_TRUE(d.lock);
  TEST_ASSERT_TRUE(d.modified);
  TEST_ASSERT_EQUAL_HEX32(0x12345600u, d.address);
}

static void test_only_a_type_2_long_page_descriptor_has_a_limit(void) {
  /* "The only difference in the long format of the type-1 and type-2 page
   * descriptors is the presence of the LIMIT field and L/U bit in the long
   * format of the type-2 descriptor." On a type-1 that word is UNUSED, so
   * reading it as a limit would invent a bound out of software's storage. */
  const uint64_t value = UINT64_C(0xFFFF000200000000);

  const ap_m68851_descriptor_t type_1 =
      ap_m68851_long_page_descriptor(value, false);
  TEST_ASSERT_FALSE(type_1.lower_limit);
  TEST_ASSERT_EQUAL_UINT(0u, type_1.limit);

  const ap_m68851_descriptor_t type_2 =
      ap_m68851_long_page_descriptor(value, true);
  TEST_ASSERT_TRUE(type_2.lower_limit);
  TEST_ASSERT_EQUAL_UINT(0x7FFFu, type_2.limit);

  /* Everything else about them is identical, which is the manual's point. */
  TEST_ASSERT_EQUAL_INT(type_1.dt, type_2.dt);
  TEST_ASSERT_EQUAL_HEX32(type_1.address, type_2.address);
}

static void test_the_two_short_page_descriptor_types_are_identical(void) {
  /* "The type-1 and type-2 short format descriptors are identical" -- which is
   * why there is one decoder for them and no type flag to pass it. */
  const ap_m68851_descriptor_t d = ap_m68851_short_page_descriptor(0xFFFFFFFFu);
  TEST_ASSERT_FALSE(d.lower_limit);
  TEST_ASSERT_EQUAL_UINT(0u, d.limit);
}

static void test_the_short_indirect_descriptor(void) {
  /* Figure 5-17: descriptor address PA31-PA2 at 31-2, DT at 1-0. Four-byte
   * aligned, because it points at a descriptor rather than at a page. */
  const ap_m68851_descriptor_t d =
      ap_m68851_short_indirect_descriptor(0x12345677u);
  TEST_ASSERT_EQUAL_HEX32(0x12345674u, d.address);
  TEST_ASSERT_EQUAL_INT(AP_M68851_DT_VALID_8_BYTE, d.dt);
}

static void test_the_long_indirect_descriptor(void) {
  /* Figure 5-18: DT at 33-32 with the address in the lower long word at 31-2,
   * and everything else unused. */
  const ap_m68851_descriptor_t d =
      ap_m68851_long_indirect_descriptor(UINT64_C(0x0000000212345677));
  TEST_ASSERT_EQUAL_HEX32(0x12345674u, d.address);
  TEST_ASSERT_EQUAL_INT(AP_M68851_DT_VALID_4_BYTE, d.dt);
}

static void test_an_indirect_descriptor_carries_no_protection(void) {
  /* Neither format has RAL, WAL, WP or any page attribute: the descriptor it
   * names carries them, which is the whole point of the indirection. A model
   * that read protection out of these bits would enforce whatever software had
   * stored in its own space. */
  const ap_m68851_descriptor_t shortd =
      ap_m68851_short_indirect_descriptor(0xFFFFFFFFu);
  TEST_ASSERT_FALSE(shortd.write_protect);
  TEST_ASSERT_EQUAL_UINT(0u, shortd.read_access_level);

  const ap_m68851_descriptor_t longd =
      ap_m68851_long_indirect_descriptor(UINT64_C(0xFFFFFFFFFFFFFFFF));
  TEST_ASSERT_FALSE(longd.write_protect);
  TEST_ASSERT_EQUAL_UINT(0u, longd.read_access_level);
  TEST_ASSERT_FALSE(longd.gate);
  TEST_ASSERT_FALSE(longd.shared_globally);
}

static void test_the_three_alignments_differ_by_what_is_pointed_at(void) {
  /* A table is 16-byte aligned, a page frame 256-byte, an indirect target
   * 4-byte. Three different masks, and using one for another would silently
   * truncate or over-align an address. */
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFF0u,
                          ap_m68851_short_table_descriptor(0xFFFFFFFFu).address);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFF00u,
                          ap_m68851_short_page_descriptor(0xFFFFFFFFu).address);
  TEST_ASSERT_EQUAL_HEX32(
      0xFFFFFFFCu, ap_m68851_short_indirect_descriptor(0xFFFFFFFFu).address);
}

static void test_the_long_formats_agree_on_where_dt_lives(void) {
  /* Every long format puts DT at bits 33-32 -- in the *upper* long word, where
   * the short formats put it in bits 1-0 of their only word. That asymmetry is
   * the one thing a reader of these figures is most likely to smooth over. */
  const uint64_t dt_only = UINT64_C(3) << 32;
  TEST_ASSERT_EQUAL_INT(AP_M68851_DT_VALID_8_BYTE,
                        ap_m68851_long_table_descriptor(dt_only).dt);
  TEST_ASSERT_EQUAL_INT(AP_M68851_DT_VALID_8_BYTE,
                        ap_m68851_long_page_descriptor(dt_only, false).dt);
  TEST_ASSERT_EQUAL_INT(AP_M68851_DT_VALID_8_BYTE,
                        ap_m68851_long_indirect_descriptor(dt_only).dt);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_figure_5_10_in_full);
  RUN_TEST(test_the_same_dt_is_a_table_or_an_indirect_by_search_state);
  RUN_TEST(test_a_page_descriptor_is_type_2_only_when_indices_remain);
  RUN_TEST(test_an_indirect_pointing_at_an_indirect_is_illegal_and_invalid);
  RUN_TEST(test_only_those_two_cells_are_illegal);
  RUN_TEST(test_the_previous_descriptor_decides_the_next_ones_width);
  RUN_TEST(test_the_short_table_descriptor);
  RUN_TEST(test_a_short_descriptor_carries_no_access_levels);
  RUN_TEST(test_the_long_table_descriptor);
  RUN_TEST(test_a_table_descriptor_has_no_page_attributes);
  RUN_TEST(test_the_short_page_descriptor);
  RUN_TEST(test_each_page_attribute_sits_at_its_own_bit);
  RUN_TEST(test_a_page_frame_is_256_byte_aligned);
  RUN_TEST(test_the_long_page_descriptor_carries_page_attributes);
  RUN_TEST(test_only_a_type_2_long_page_descriptor_has_a_limit);
  RUN_TEST(test_the_two_short_page_descriptor_types_are_identical);
  RUN_TEST(test_the_short_indirect_descriptor);
  RUN_TEST(test_the_long_indirect_descriptor);
  RUN_TEST(test_an_indirect_descriptor_carries_no_protection);
  RUN_TEST(test_the_three_alignments_differ_by_what_is_pointed_at);
  RUN_TEST(test_the_long_formats_agree_on_where_dt_lives);
  return UNITY_END();
}
