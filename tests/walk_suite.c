/* MC68030 translation table search.
 *
 * Cited to MC68030 User's Manual 3ed §9.2, §9.4 and §9.5.
 *
 * The count that matters is `descriptor_fetches`. The ATC costs nothing on a
 * hit (§9.4), so every clock the MMU spends is a descriptor fetch here. These
 * tests assert the count as carefully as the address, because it is what a
 * timing probe will measure.
 */

#include "cpu/m68030/ap_m68030_walk.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* A synthetic tree. Each entry maps a physical descriptor address to the
 * decoded descriptor a fetch returns, so the test controls the tree exactly
 * without needing a memory system. */
typedef struct {
  uint32_t address;
  ap_m68030_descriptor_t descriptor;
} tree_entry_t;

typedef struct {
  const tree_entry_t *entries;
  unsigned count;
  unsigned fetches;
  uint32_t bus_error_at; /* fetching this address fails; 0xFFFFFFFF for none */
} tree_t;

static bool tree_fetch(void *context, uint32_t physical, bool long_format,
                       ap_m68030_descriptor_t *out) {
  (void)long_format;
  tree_t *tree = (tree_t *)context;
  tree->fetches++;
  if (physical == tree->bus_error_at) {
    return false;
  }
  for (unsigned i = 0; i < tree->count; i++) {
    if (tree->entries[i].address == physical) {
      *out = tree->entries[i].descriptor;
      return true;
    }
  }
  /* An address the tree does not describe reads as an invalid descriptor,
   * which is what uninitialised table memory would look like. */
  *out = (ap_m68030_descriptor_t){.dt = AP_M68030_DT_INVALID};
  return true;
}

/* IS 0 + TIA 7 + TIB 7 + TIC 6 + PS 12 = 32: a three-level tree, 4K pages. */
static ap_m68030_tc_t three_level_4k(void) {
  return ap_m68030_tc_decode(UINT32_C(0x80000000) | (12u << 20) | (0u << 16) |
                             (7u << 12) | (7u << 8) | (6u << 4) | 0u);
}

static ap_m68030_root_t root_at(uint32_t address) {
  return (ap_m68030_root_t){.table_address = address,
                            .long_format = false,
                            .has_limit = false};
}

/* Table bases chosen so a descriptor address collision cannot make a wrong
 * walk look right. */
#define ROOT_TABLE 0x00010000u
#define TABLE_B 0x00020000u
#define TABLE_C 0x00030000u
#define PAGE_FRAME 0x00A00000u

/* Address 0x00000000 indexes 0 at every level, so descriptors sit at each
 * table's base. Offset 0x123 within the 4K page. */
#define TEST_ADDRESS 0x00000123u

static const tree_entry_t three_level_tree[] = {
    {ROOT_TABLE, {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_B}},
    {TABLE_B, {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_C}},
    {TABLE_C, {.dt = AP_M68030_DT_PAGE, .address_field = PAGE_FRAME >> 8}},
};

static tree_t make_tree(const tree_entry_t *entries, unsigned count) {
  return (tree_t){.entries = entries,
                  .count = count,
                  .fetches = 0,
                  .bus_error_at = 0xFFFFFFFFu};
}

/* A full three-level search reaches the page and costs exactly one fetch per
 * level -- the number a timing probe sees. */
static void test_a_three_level_search_costs_one_fetch_per_level(void) {
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(three_level_tree, 3);

  ap_m68030_walk_result_t r =
      ap_m68030_walk(&tc, &root, TEST_ADDRESS, tree_fetch, &tree);

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_HEX32(PAGE_FRAME | 0x123u, r.physical);
  TEST_ASSERT_EQUAL_UINT(3, r.descriptor_fetches);
  TEST_ASSERT_EQUAL_UINT(3, r.levels_walked);
  TEST_ASSERT_FALSE(r.early_termination);
}

/* [030] 9.5.1.1: "A table search ends when an invalid descriptor is
 * encountered", and the search must stop there rather than fetching on. */
static void test_an_invalid_descriptor_ends_the_search_early(void) {
  static const tree_entry_t tree_entries[] = {
      {ROOT_TABLE, {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_B}},
      {TABLE_B, {.dt = AP_M68030_DT_INVALID}},
  };
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(tree_entries, 2);

  ap_m68030_walk_result_t r =
      ap_m68030_walk(&tc, &root, TEST_ADDRESS, tree_fetch, &tree);

  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_TRUE(r.search.invalid);
  TEST_ASSERT_EQUAL_UINT(2, r.descriptor_fetches);
}

/* [030] 9.5.1.1: a page descriptor above the page table is an early
 * termination page descriptor, and the search ends there -- costing fewer
 * fetches, which is the point of using one. */
static void test_an_early_termination_page_descriptor_shortens_the_search(void) {
  static const tree_entry_t tree_entries[] = {
      {ROOT_TABLE, {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_B}},
      {TABLE_B, {.dt = AP_M68030_DT_PAGE, .address_field = PAGE_FRAME >> 8}},
  };
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(tree_entries, 2);

  ap_m68030_walk_result_t r =
      ap_m68030_walk(&tc, &root, TEST_ADDRESS, tree_fetch, &tree);

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_TRUE(r.early_termination);
  TEST_ASSERT_EQUAL_UINT(2, r.descriptor_fetches);
}

/* An early termination descriptor maps a block larger than a page, so every
 * logical bit below the level it was found at is offset -- including the index
 * bits the search never consumed. */
static void test_early_termination_takes_the_unconsumed_index_bits_as_offset(void) {
  static const tree_entry_t tree_entries[] = {
      {ROOT_TABLE, {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_B}},
      {TABLE_B, {.dt = AP_M68030_DT_PAGE, .address_field = PAGE_FRAME >> 8}},
  };
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(tree_entries, 2);

  /* TIA=0, TIB=0 select the same descriptors, but TIC is non-zero: those bits
   * must appear in the physical address rather than being discarded. */
  const uint32_t address = (UINT32_C(3) << 12) | 0x123u;
  ap_m68030_walk_result_t r =
      ap_m68030_walk(&tc, &root, address, tree_fetch, &tree);

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_HEX32(PAGE_FRAME | (UINT32_C(3) << 12) | 0x123u, r.physical);
}

/* [030] 9.5.1.1: in a page table, DT=$2/$3 "identifies an indirect descriptor
 * that points to a ... page descriptor". It costs an extra fetch, which is the
 * measurable price of the indirection. */
static void test_an_indirect_descriptor_costs_one_extra_fetch(void) {
  static const tree_entry_t tree_entries[] = {
      {ROOT_TABLE, {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_B}},
      {TABLE_B, {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_C}},
      {TABLE_C, {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = 0x00040000u}},
      {0x00040000u, {.dt = AP_M68030_DT_PAGE, .address_field = PAGE_FRAME >> 8}},
  };
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(tree_entries, 4);

  ap_m68030_walk_result_t r =
      ap_m68030_walk(&tc, &root, TEST_ADDRESS, tree_fetch, &tree);

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_TRUE(r.used_indirect);
  TEST_ASSERT_EQUAL_HEX32(PAGE_FRAME | 0x123u, r.physical);
  TEST_ASSERT_EQUAL_UINT(4, r.descriptor_fetches);
}

/* An indirect descriptor pointing at anything but a page descriptor is a
 * malformed tree; it must not be followed as though it were a table. */
static void test_an_indirect_descriptor_must_point_at_a_page_descriptor(void) {
  static const tree_entry_t tree_entries[] = {
      {ROOT_TABLE, {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_B}},
      {TABLE_B, {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_C}},
      {TABLE_C, {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = 0x00040000u}},
      {0x00040000u,
       {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = 0x00050000u}},
  };
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(tree_entries, 4);

  ap_m68030_walk_result_t r =
      ap_m68030_walk(&tc, &root, TEST_ADDRESS, tree_fetch, &tree);
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_TRUE(r.search.invalid);
}

/* [030] 9.5.1.1 WP: "The states of all WP bits encountered during a table
 * search are logically ORed" -- a protected pointer protects the page below it
 * even though the page descriptor itself is permissive. */
static void test_write_protection_on_a_pointer_reaches_the_page(void) {
  static const tree_entry_t tree_entries[] = {
      {ROOT_TABLE, {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_B}},
      {TABLE_B,
       {.dt = AP_M68030_DT_VALID_4BYTE,
        .address_field = TABLE_C,
        .write_protect = true}},
      {TABLE_C, {.dt = AP_M68030_DT_PAGE, .address_field = PAGE_FRAME >> 8}},
  };
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(tree_entries, 3);

  ap_m68030_walk_result_t r =
      ap_m68030_walk(&tc, &root, TEST_ADDRESS, tree_fetch, &tree);

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_TRUE(r.search.write_protected);
  TEST_ASSERT_FALSE(ap_m68030_search_permits_write(&r.search));
}

/* Supervisor and cache-inhibit accumulate the same way. */
static void test_supervisor_and_cache_inhibit_accumulate_down_the_tree(void) {
  static const tree_entry_t tree_entries[] = {
      {ROOT_TABLE,
       {.dt = AP_M68030_DT_VALID_4BYTE,
        .address_field = TABLE_B,
        .supervisor = true}},
      {TABLE_B,
       {.dt = AP_M68030_DT_VALID_4BYTE,
        .address_field = TABLE_C,
        .cache_inhibit = true}},
      {TABLE_C, {.dt = AP_M68030_DT_PAGE, .address_field = PAGE_FRAME >> 8}},
  };
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(tree_entries, 3);

  ap_m68030_walk_result_t r =
      ap_m68030_walk(&tc, &root, TEST_ADDRESS, tree_fetch, &tree);

  TEST_ASSERT_TRUE(r.search.supervisor_only);
  TEST_ASSERT_TRUE(r.search.cache_inhibited);
  TEST_ASSERT_FALSE(ap_m68030_search_permits_access(&r.search, false));
}

/* [030] 9.5.1.1 LIMIT: "An out-of-bounds access causes the B bit in the ATC
 * entry for the address to be set and causes the table search to abort" -- so
 * it stops before fetching the descriptor it would have indexed. */
static void test_an_out_of_bounds_index_aborts_before_fetching(void) {
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  root.has_limit = true;
  root.lower_limit = false;
  root.limit = 0; /* upper limit 0: only index 0 is in bounds */
  tree_t tree = make_tree(three_level_tree, 3);

  /* TIA is bits 31-25, so this address indexes 1 at the top level. */
  ap_m68030_walk_result_t r =
      ap_m68030_walk(&tc, &root, UINT32_C(1) << 25, tree_fetch, &tree);

  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_TRUE(r.search.limit_violation);
  TEST_ASSERT_EQUAL_UINT(0, r.descriptor_fetches);
}

/* An index inside the limit proceeds normally. */
static void test_an_index_within_the_limit_proceeds(void) {
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  root.has_limit = true;
  root.limit = 4;
  tree_t tree = make_tree(three_level_tree, 3);

  ap_m68030_walk_result_t r =
      ap_m68030_walk(&tc, &root, TEST_ADDRESS, tree_fetch, &tree);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_FALSE(r.search.limit_violation);
}

/* [030] 9.4 B: the bit is set for "a bus error ... encountered during the table
 * search", so a failed fetch ends the search rather than producing a
 * translation. */
static void test_a_bus_error_during_the_search_produces_no_translation(void) {
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(three_level_tree, 3);
  tree.bus_error_at = TABLE_B;

  ap_m68030_walk_result_t r =
      ap_m68030_walk(&tc, &root, TEST_ADDRESS, tree_fetch, &tree);

  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_TRUE(r.search.invalid);
  TEST_ASSERT_EQUAL_UINT(2, r.descriptor_fetches);
}

/* [030] 9.5.1.1: "$3 VALID 8 BYTE ... The MC68030 multiplies the index for the
 * next table by eight." A long-format table must be indexed with the wider
 * stride, or the walk reads the wrong descriptor. */
static void test_a_long_format_table_is_indexed_with_the_wider_stride(void) {
  /* Index 1 at the second level: at stride 8 the descriptor is at TABLE_B+8,
   * at stride 4 it would be TABLE_B+4. Only the former is a page descriptor. */
  static const tree_entry_t tree_entries[] = {
      {ROOT_TABLE, {.dt = AP_M68030_DT_VALID_8BYTE, .address_field = TABLE_B}},
      {TABLE_B + 4u, {.dt = AP_M68030_DT_INVALID}},
      {TABLE_B + 8u, {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_C}},
      {TABLE_C, {.dt = AP_M68030_DT_PAGE, .address_field = PAGE_FRAME >> 8}},
  };
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(tree_entries, 4);

  /* TIB is bits 24-18, so this indexes 1 at the second level. */
  ap_m68030_walk_result_t r =
      ap_m68030_walk(&tc, &root, (UINT32_C(1) << 18) | 0x123u, tree_fetch, &tree);

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_HEX32(PAGE_FRAME | 0x123u, r.physical);
}

/* The index scales the descriptor address by the stride, so a non-zero index
 * must read a different descriptor. */
static void test_the_index_selects_a_descriptor_by_stride(void) {
  static const tree_entry_t tree_entries[] = {
      {ROOT_TABLE + 8u,
       {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_B}},
      {TABLE_B, {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_C}},
      {TABLE_C, {.dt = AP_M68030_DT_PAGE, .address_field = PAGE_FRAME >> 8}},
  };
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(tree_entries, 3);

  /* TIA index 2, short format: ROOT_TABLE + 2*4 = ROOT_TABLE + 8. */
  ap_m68030_walk_result_t r =
      ap_m68030_walk(&tc, &root, (UINT32_C(2) << 25) | 0x123u, tree_fetch, &tree);
  TEST_ASSERT_TRUE(r.ok);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_three_level_search_costs_one_fetch_per_level);
  RUN_TEST(test_an_invalid_descriptor_ends_the_search_early);
  RUN_TEST(test_an_early_termination_page_descriptor_shortens_the_search);
  RUN_TEST(test_early_termination_takes_the_unconsumed_index_bits_as_offset);
  RUN_TEST(test_an_indirect_descriptor_costs_one_extra_fetch);
  RUN_TEST(test_an_indirect_descriptor_must_point_at_a_page_descriptor);
  RUN_TEST(test_write_protection_on_a_pointer_reaches_the_page);
  RUN_TEST(test_supervisor_and_cache_inhibit_accumulate_down_the_tree);
  RUN_TEST(test_an_out_of_bounds_index_aborts_before_fetching);
  RUN_TEST(test_an_index_within_the_limit_proceeds);
  RUN_TEST(test_a_bus_error_during_the_search_produces_no_translation);
  RUN_TEST(test_a_long_format_table_is_indexed_with_the_wider_stride);
  RUN_TEST(test_the_index_selects_a_descriptor_by_stride);
  return UNITY_END();
}
