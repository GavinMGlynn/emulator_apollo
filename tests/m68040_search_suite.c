/* MC68040 table search, `[68040]` §3.2 and Figures 3-8 and 3-9.
 *
 * A three-level tree built in an array and walked, plus tests that check the
 * manual's two independent statements of the geometry against each other.
 */

#include <string.h>

#include "cpu/m68040/ap_m68040_search.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define MEMORY_LONGS 4096u
#define MEMORY_BASE 0x1000u

typedef struct {
  uint32_t word[MEMORY_LONGS];
  unsigned fetches;
  bool fail_at_set;
  uint32_t fail_at;
} memory_t;

static bool memory_fetch(void *context, uint32_t address, uint32_t *value) {
  memory_t *m = (memory_t *)context;
  m->fetches++;
  if (m->fail_at_set && address == m->fail_at) {
    return false;
  }
  const uint32_t index = (address - MEMORY_BASE) / 4u;
  if (index >= MEMORY_LONGS) {
    return false;
  }
  *value = m->word[index];
  return true;
}

static void put(memory_t *m, uint32_t address, uint32_t value) {
  m->word[(address - MEMORY_BASE) / 4u] = value;
}

/* Root table at 0x1000, pointer table at 0x2000, page table at 0x3000, and a
 * page frame at 0x50000. Descriptor types: UDT 10 is resident, PDT 01 is. */
static void build(memory_t *m) {
  memset(m, 0, sizeof *m);
  put(m, 0x1000u, 0x2000u | 0x2u);
  put(m, 0x2000u, 0x3000u | 0x2u);
  put(m, 0x3000u, 0x50000u | 0x1u);
}

static ap_m68040_search_config_t config_for(memory_t *m,
                                            ap_m68040_page_size_t page_size) {
  return (ap_m68040_search_config_t){.root_pointer = 0x1000u,
                                     .page_size = page_size,
                                     .fetch = memory_fetch,
                                     .fetch_context = m};
}

/* ---------------------------------------------------------------------------
 * The logical address format, Figure 3-8.
 * ------------------------------------------------------------------------- */

static void test_the_three_index_fields(void) {
  /* RI bits 31-25, PI bits 24-18, PGI bits 17-12 at 4K. */
  const uint32_t address = 0xFFFFFFFFu;
  TEST_ASSERT_EQUAL_UINT(0x7Fu, ap_m68040_root_index(address));
  TEST_ASSERT_EQUAL_UINT(0x7Fu, ap_m68040_pointer_index(address));
  TEST_ASSERT_EQUAL_UINT(0x3Fu,
                         ap_m68040_page_index(address, AP_M68040_PAGE_4K));
  TEST_ASSERT_EQUAL_UINT(0x1Fu,
                         ap_m68040_page_index(address, AP_M68040_PAGE_8K));
}

static void test_the_fields_do_not_overlap(void) {
  /* Each field is read from its own bits: setting one leaves the others zero,
   * which is what a shift-by-one error would break. */
  TEST_ASSERT_EQUAL_UINT(1u, ap_m68040_root_index(0x02000000u));
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68040_pointer_index(0x02000000u));
  TEST_ASSERT_EQUAL_UINT(1u, ap_m68040_pointer_index(0x00040000u));
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68040_root_index(0x00040000u));
  TEST_ASSERT_EQUAL_UINT(
      1u, ap_m68040_page_index(0x00001000u, AP_M68040_PAGE_4K));
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68040_pointer_index(0x00001000u));
}

static void test_the_geometry_accounts_for_the_whole_address(void) {
  /* 7 + 7 + 6 + 12 = 32 at 4K, and 7 + 7 + 5 + 13 = 32 at 8K. The page size
   * moves one bit between PGI and the offset and changes nothing else. */
  TEST_ASSERT_EQUAL_UINT(32u, 7u + 7u + 6u + 12u);
  TEST_ASSERT_EQUAL_UINT(32u, 7u + 7u + 5u + 13u);
}

static void test_the_concatenation_widths_match_the_descriptor_masks(void) {
  /* §3.2.1 states the geometry a second way: the PI field "multiplied by 4 ...
   * concatenated with the fetched root-level descriptor's upper 23 bits", and
   * for 8-Kbyte pages the PGI field with "the upper 25 bits". Each identity
   * must come to 32 against the address-field widths transcribed from Figure
   * 3-11 -- two independent statements of one geometry, checked here against
   * each other rather than each against my reading of it. */
  const unsigned root_address_bits = 23u; /* bits 31-9 */
  TEST_ASSERT_EQUAL_UINT(32u, root_address_bits + 7u + 2u);

  const unsigned pointer_4k_bits = 24u; /* bits 31-8 */
  TEST_ASSERT_EQUAL_UINT(32u, pointer_4k_bits + 6u + 2u);

  const unsigned pointer_8k_bits = 25u; /* bits 31-7 */
  TEST_ASSERT_EQUAL_UINT(32u, pointer_8k_bits + 5u + 2u);

  /* And the masks really are those widths. */
  TEST_ASSERT_EQUAL_HEX32(
      0xFFFFFE00u, ap_m68040_root_descriptor(0xFFFFFFFFu).table_address);
  TEST_ASSERT_EQUAL_HEX32(
      0xFFFFFF00u,
      ap_m68040_pointer_descriptor(0xFFFFFFFFu, AP_M68040_PAGE_4K)
          .table_address);
  TEST_ASSERT_EQUAL_HEX32(
      0xFFFFFF80u,
      ap_m68040_pointer_descriptor(0xFFFFFFFFu, AP_M68040_PAGE_8K)
          .table_address);
}

/* ---------------------------------------------------------------------------
 * The search.
 * ------------------------------------------------------------------------- */

static void test_a_three_level_search_reaches_the_page_frame(void) {
  memory_t m;
  build(&m);
  const ap_m68040_search_config_t config = config_for(&m, AP_M68040_PAGE_4K);

  const ap_m68040_search_result_t r = ap_m68040_search(&config, 0u);
  TEST_ASSERT_EQUAL_INT(AP_M68040_SEARCH_RESIDENT, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x50000u, r.physical_address);
  /* Exactly three levels: the tree depth is fixed, so this is never four. */
  TEST_ASSERT_EQUAL_UINT(3u, r.fetches);
}

static void test_the_page_offset_survives_translation(void) {
  memory_t m;
  build(&m);
  const ap_m68040_search_config_t config = config_for(&m, AP_M68040_PAGE_4K);
  TEST_ASSERT_EQUAL_HEX32(0x50FFFu,
                          ap_m68040_search(&config, 0xFFFu).physical_address);
}

static void test_an_invalid_descriptor_at_any_level_ends_the_search(void) {
  /* "00 or 01 = Invalid" for a table descriptor, "00 = Invalid" for a page
   * descriptor. Each level tested in turn, since a search that only checked
   * the last would walk into rubbish. */
  for (unsigned level = 0; level < 3u; level++) {
    memory_t m;
    build(&m);
    const uint32_t table[3] = {0x1000u, 0x2000u, 0x3000u};
    put(&m, table[level], 0u);
    const ap_m68040_search_config_t config = config_for(&m, AP_M68040_PAGE_4K);

    const ap_m68040_search_result_t r = ap_m68040_search(&config, 0u);
    TEST_ASSERT_EQUAL_INT(AP_M68040_SEARCH_INVALID, r.status);
    TEST_ASSERT_EQUAL_UINT(level + 1u, r.fetches);
  }
}

static void test_a_udt_of_01_is_also_invalid(void) {
  /* "00 or 01 = Invalid" -- the low bit is free, so `01` must not be mistaken
   * for a resident descriptor with an odd address. */
  memory_t m;
  build(&m);
  put(&m, 0x1000u, 0x2000u | 0x1u);
  const ap_m68040_search_config_t config = config_for(&m, AP_M68040_PAGE_4K);
  TEST_ASSERT_EQUAL_INT(AP_M68040_SEARCH_INVALID,
                        ap_m68040_search(&config, 0u).status);
}

static void test_a_transfer_error_ends_the_search(void) {
  memory_t m;
  build(&m);
  m.fail_at_set = true;
  m.fail_at = 0x2000u;
  const ap_m68040_search_config_t config = config_for(&m, AP_M68040_PAGE_4K);
  TEST_ASSERT_EQUAL_INT(AP_M68040_SEARCH_BUS_ERROR,
                        ap_m68040_search(&config, 0u).status);
}

static void test_an_indirect_descriptor_is_followed_once(void) {
  /* "10 = Indirect ... bits 31-2 contain the physical address of the page
   * descriptor." One more fetch, and the address is 4-byte aligned rather than
   * page aligned. */
  memory_t m;
  build(&m);
  put(&m, 0x3000u, 0x4004u | 0x2u); /* indirect, naming 0x4004 */
  put(&m, 0x4004u, 0x60000u | 0x1u);
  const ap_m68040_search_config_t config = config_for(&m, AP_M68040_PAGE_4K);

  const ap_m68040_search_result_t r = ap_m68040_search(&config, 0u);
  TEST_ASSERT_EQUAL_INT(AP_M68040_SEARCH_RESIDENT, r.status);
  TEST_ASSERT_TRUE(r.indirect);
  TEST_ASSERT_EQUAL_HEX32(0x60000u, r.physical_address);
  TEST_ASSERT_EQUAL_UINT(4u, r.fetches);
}

static void test_an_indirection_naming_another_indirection_is_invalid(void) {
  /* "This encoding is invalid for a page descriptor pointed to by an indirect
   * descriptor" -- so a chain terminates rather than looping, the same rule the
   * 68851 states from the other side of its Figure 5-10. */
  memory_t m;
  build(&m);
  put(&m, 0x3000u, 0x4004u | 0x2u);
  put(&m, 0x4004u, 0x5004u | 0x2u); /* another indirect */
  const ap_m68040_search_config_t config = config_for(&m, AP_M68040_PAGE_4K);

  const ap_m68040_search_result_t r = ap_m68040_search(&config, 0u);
  TEST_ASSERT_EQUAL_INT(AP_M68040_SEARCH_INVALID, r.status);
  TEST_ASSERT_EQUAL_UINT(4u, r.fetches);
}

static void test_write_protection_accumulates_down_the_tree(void) {
  /* "Setting the W-bit in a table descriptor write protects all pages accessed
   * with that descriptor." A clear bit lower down cannot undo it, so protection
   * is a property of the path. */
  memory_t m;
  build(&m);
  put(&m, 0x1000u, 0x2000u | 0x4u | 0x2u); /* W set at the root */
  const ap_m68040_search_config_t config = config_for(&m, AP_M68040_PAGE_4K);

  const ap_m68040_search_result_t r = ap_m68040_search(&config, 0u);
  TEST_ASSERT_EQUAL_INT(AP_M68040_SEARCH_RESIDENT, r.status);
  TEST_ASSERT_TRUE(r.write_protect);
}

static void test_the_page_attributes_reach_the_result(void) {
  memory_t m;
  build(&m);
  /* G, U1, U0, S set, CM = copyback, M set. */
  put(&m, 0x3000u,
      0x50000u | 0x400u | 0x200u | 0x100u | 0x80u | 0x20u | 0x10u | 0x1u);
  const ap_m68040_search_config_t config = config_for(&m, AP_M68040_PAGE_4K);

  const ap_m68040_search_result_t r = ap_m68040_search(&config, 0u);
  TEST_ASSERT_TRUE(r.global);
  TEST_ASSERT_TRUE(r.user_attribute_1);
  TEST_ASSERT_TRUE(r.user_attribute_0);
  TEST_ASSERT_TRUE(r.supervisor);
  TEST_ASSERT_TRUE(r.modified);
  TEST_ASSERT_EQUAL_INT(AP_M68040_CM_CACHABLE_COPYBACK, r.cache_mode);
}

static void test_the_indices_select_different_descriptors(void) {
  /* Two addresses differing only in the page index reach different page
   * descriptors, which is what proves the index is being used to step. */
  memory_t m;
  build(&m);
  put(&m, 0x3000u + 4u, 0x70000u | 0x1u);
  const ap_m68040_search_config_t config = config_for(&m, AP_M68040_PAGE_4K);

  TEST_ASSERT_EQUAL_HEX32(0x50000u,
                          ap_m68040_search(&config, 0x0000u).physical_address);
  TEST_ASSERT_EQUAL_HEX32(0x70000u,
                          ap_m68040_search(&config, 0x1000u).physical_address);
}

static void test_the_page_size_changes_which_descriptor_an_address_reaches(void) {
  /* At 4K the page index is bits 17-12, at 8K bits 17-13 -- so address 0x1000
   * selects descriptor 1 at 4K and descriptor 0 at 8K. The tables are otherwise
   * identical, so any difference is the geometry. */
  memory_t m;
  build(&m);
  put(&m, 0x3000u + 4u, 0x70000u | 0x1u);

  const ap_m68040_search_config_t small = config_for(&m, AP_M68040_PAGE_4K);
  const ap_m68040_search_config_t large = config_for(&m, AP_M68040_PAGE_8K);
  TEST_ASSERT_EQUAL_HEX32(0x70000u,
                          ap_m68040_search(&small, 0x1000u).physical_address);
  TEST_ASSERT_EQUAL_HEX32(0x51000u,
                          ap_m68040_search(&large, 0x1000u).physical_address);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_three_index_fields);
  RUN_TEST(test_the_fields_do_not_overlap);
  RUN_TEST(test_the_geometry_accounts_for_the_whole_address);
  RUN_TEST(test_the_concatenation_widths_match_the_descriptor_masks);
  RUN_TEST(test_a_three_level_search_reaches_the_page_frame);
  RUN_TEST(test_the_page_offset_survives_translation);
  RUN_TEST(test_an_invalid_descriptor_at_any_level_ends_the_search);
  RUN_TEST(test_a_udt_of_01_is_also_invalid);
  RUN_TEST(test_a_transfer_error_ends_the_search);
  RUN_TEST(test_an_indirect_descriptor_is_followed_once);
  RUN_TEST(test_an_indirection_naming_another_indirection_is_invalid);
  RUN_TEST(test_write_protection_accumulates_down_the_tree);
  RUN_TEST(test_the_page_attributes_reach_the_result);
  RUN_TEST(test_the_indices_select_different_descriptors);
  RUN_TEST(test_the_page_size_changes_which_descriptor_an_address_reaches);
  return UNITY_END();
}
