/* MC68030 translation table search.
 *
 * Cited to MC68030 User's Manual 3ed §9.2, §9.4, §9.5 and §11.
 *
 * The counts that matter are `descriptor_fetches` and `history_writes`. The ATC
 * costs nothing on a hit (§9.4), so every clock the MMU spends is here, and §11
 * p. 11-56 counts a table search in reads and writes separately -- "an RMC cycle
 * to set the U bit is counted as one read and one write". These tests assert
 * both counts as carefully as the address, because they are what a timing probe
 * will measure.
 */

/* ap_m68030_tt.h is included for no reason other than to keep it includable
 * alongside this one. Both headers describe "the access", and they described it
 * under the same typedef name until that collision was found -- any module using
 * the tables *and* transparent translation, which is every real MMU, would not
 * have compiled. Including both here means the build catches it rather than the
 * next caller. */
#include "cpu/m68030/ap_m68030_tt.h"
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

/* One recorded history-bit update, so a test can assert *which* descriptor was
 * written and which bits changed, not merely how many writes happened. */
typedef struct {
  uint32_t address;
  bool set_used;
  bool set_modified;
} update_record_t;

#define MAX_UPDATES 8

typedef struct {
  const tree_entry_t *entries;
  unsigned count;
  unsigned fetches;
  uint32_t bus_error_at;        /* fetching this address fails; ~0 for none */
  uint32_t update_error_at;     /* updating this address fails; ~0 for none */
  update_record_t updates[MAX_UPDATES];
  unsigned update_count;
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

static bool tree_update(void *context, uint32_t physical, bool set_used,
                        bool set_modified) {
  tree_t *tree = (tree_t *)context;
  if (tree->update_count < MAX_UPDATES) {
    tree->updates[tree->update_count] = (update_record_t){
        .address = physical, .set_used = set_used, .set_modified = set_modified};
  }
  tree->update_count++;
  return physical != tree->update_error_at;
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

/* A supervisor read, the access most tests use: it sets no M bit, so a test
 * that says nothing about the access is testing the search alone. */
static const ap_m68030_search_access_t SUPERVISOR_READ = {
    .write = false, .read_modify_write = false, .supervisor = true};
static const ap_m68030_search_access_t SUPERVISOR_WRITE = {
    .write = true, .read_modify_write = false, .supervisor = true};
static const ap_m68030_search_access_t USER_READ = {
    .write = false, .read_modify_write = false, .supervisor = false};

/* Table bases chosen so a descriptor address collision cannot make a wrong
 * walk look right. */
#define ROOT_TABLE 0x00010000u
#define TABLE_B 0x00020000u
#define TABLE_C 0x00030000u
#define INDIRECT_TARGET 0x00040000u
#define PAGE_FRAME 0x00A00000u

/* Address 0x00000000 indexes 0 at every level, so descriptors sit at each
 * table's base. Offset 0x123 within the 4K page. */
#define TEST_ADDRESS 0x00000123u

static const tree_entry_t three_level_tree[] = {
    {ROOT_TABLE, {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_B}},
    {TABLE_B, {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_C}},
    {TABLE_C, {.dt = AP_M68030_DT_PAGE, .address_field = PAGE_FRAME >> 8}},
};

/* The same tree with every U bit already set, so a test can isolate the M bit
 * without the U updates adding writes of their own. */
static const tree_entry_t three_level_tree_used[] = {
    {ROOT_TABLE,
     {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_B, .used = true}},
    {TABLE_B,
     {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_C, .used = true}},
    {TABLE_C,
     {.dt = AP_M68030_DT_PAGE, .address_field = PAGE_FRAME >> 8, .used = true}},
};

static tree_t make_tree(const tree_entry_t *entries, unsigned count) {
  return (tree_t){.entries = entries,
                  .count = count,
                  .fetches = 0,
                  .bus_error_at = 0xFFFFFFFFu,
                  .update_error_at = 0xFFFFFFFFu,
                  .update_count = 0};
}

/* ---------------------------------------------------------------------------
 * The search itself. These pass a NULL update function, which is exactly what
 * PTEST does -- "a search that must not disturb the tree" -- so the counts they
 * assert are the search's own and nothing else.
 * ------------------------------------------------------------------------- */

/* A full three-level search reaches the page and costs exactly one fetch per
 * level -- the number a timing probe sees. */
static void test_a_three_level_search_costs_one_fetch_per_level(void) {
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(three_level_tree, 3);

  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, TEST_ADDRESS, &SUPERVISOR_READ, tree_fetch, NULL, &tree);

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

  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, TEST_ADDRESS, &SUPERVISOR_READ, tree_fetch, NULL, &tree);

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

  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, TEST_ADDRESS, &SUPERVISOR_READ, tree_fetch, NULL, &tree);

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
  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, address, &SUPERVISOR_READ, tree_fetch, NULL, &tree);

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
      {TABLE_C,
       {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = INDIRECT_TARGET}},
      {INDIRECT_TARGET,
       {.dt = AP_M68030_DT_PAGE, .address_field = PAGE_FRAME >> 8}},
  };
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(tree_entries, 4);

  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, TEST_ADDRESS, &SUPERVISOR_READ, tree_fetch, NULL, &tree);

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
      {TABLE_C,
       {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = INDIRECT_TARGET}},
      {INDIRECT_TARGET,
       {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = 0x00050000u}},
  };
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(tree_entries, 4);

  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, TEST_ADDRESS, &SUPERVISOR_READ, tree_fetch, NULL, &tree);
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

  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, TEST_ADDRESS, &SUPERVISOR_READ, tree_fetch, NULL, &tree);

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

  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, TEST_ADDRESS, &SUPERVISOR_READ, tree_fetch, NULL, &tree);

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
  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, UINT32_C(1) << 25, &SUPERVISOR_READ, tree_fetch, NULL, &tree);

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

  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, TEST_ADDRESS, &SUPERVISOR_READ, tree_fetch, NULL, &tree);
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

  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, TEST_ADDRESS, &SUPERVISOR_READ, tree_fetch, NULL, &tree);

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
      ap_m68030_walk(&tc, &root, (UINT32_C(1) << 18) | 0x123u, &SUPERVISOR_READ,
                     tree_fetch, NULL, &tree);

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
      ap_m68030_walk(&tc, &root, (UINT32_C(2) << 25) | 0x123u, &SUPERVISOR_READ,
                     tree_fetch, NULL, &tree);
  TEST_ASSERT_TRUE(r.ok);
}

/* ---------------------------------------------------------------------------
 * The history bits, [030] 9.5.1.1 and 11 p. 11-56.
 * ------------------------------------------------------------------------- */

/* "During a table search, the U bit in each descriptor that is encountered is
 * checked and set if it is not already set." Three descriptors with U clear
 * therefore cost three writes on top of the three reads -- which is why the
 * manual's timing table counts reads and writes separately. */
static void test_each_descriptor_with_u_clear_costs_one_history_write(void) {
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(three_level_tree, 3);

  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, TEST_ADDRESS, &SUPERVISOR_READ, tree_fetch, tree_update, &tree);

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_UINT(3, r.descriptor_fetches);
  TEST_ASSERT_EQUAL_UINT(3, r.history_writes);
  TEST_ASSERT_TRUE(tree.updates[0].set_used);
  TEST_ASSERT_EQUAL_HEX32(ROOT_TABLE, tree.updates[0].address);
}

/* "The processor never clears this bit", and a descriptor whose U is already
 * set needs no write -- the search costs reads only. */
static void test_a_descriptor_already_used_is_not_written_again(void) {
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(three_level_tree_used, 3);

  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, TEST_ADDRESS, &SUPERVISOR_READ, tree_fetch, tree_update, &tree);

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_UINT(3, r.descriptor_fetches);
  TEST_ASSERT_EQUAL_UINT(0, r.history_writes);
}

/* "when the table search is for a write access and the M bit of the page
 * descriptor is clear, the processor sets the bit". The same tree costs one
 * more write for a write access than for a read -- the difference counted
 * rather than assumed. */
static void test_a_write_to_an_unmodified_page_costs_one_more_write_than_a_read(
    void) {
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);

  tree_t reading = make_tree(three_level_tree_used, 3);
  ap_m68030_walk_result_t read_result =
      ap_m68030_walk(&tc, &root, TEST_ADDRESS, &SUPERVISOR_READ, tree_fetch,
                     tree_update, &reading);

  tree_t writing = make_tree(three_level_tree_used, 3);
  ap_m68030_walk_result_t write_result =
      ap_m68030_walk(&tc, &root, TEST_ADDRESS, &SUPERVISOR_WRITE, tree_fetch,
                     tree_update, &writing);

  TEST_ASSERT_EQUAL_UINT(0, read_result.history_writes);
  TEST_ASSERT_EQUAL_UINT(1, write_result.history_writes);
  /* It is the page descriptor that is written, not a pointer. */
  TEST_ASSERT_EQUAL_HEX32(TABLE_C, writing.updates[0].address);
  TEST_ASSERT_TRUE(writing.updates[0].set_modified);
  TEST_ASSERT_FALSE(writing.updates[0].set_used);
}

/* An already-modified page is not written again, which matters because that
 * update would otherwise cost a bus cycle on every write to the page. */
static void test_an_already_modified_page_is_not_written_again(void) {
  static const tree_entry_t tree_entries[] = {
      {ROOT_TABLE,
       {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_B, .used = true}},
      {TABLE_B,
       {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_C, .used = true}},
      {TABLE_C,
       {.dt = AP_M68030_DT_PAGE,
        .address_field = PAGE_FRAME >> 8,
        .used = true,
        .modified = true}},
  };
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(tree_entries, 3);

  ap_m68030_walk_result_t r =
      ap_m68030_walk(&tc, &root, TEST_ADDRESS, &SUPERVISOR_WRITE, tree_fetch,
                     tree_update, &tree);

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_UINT(0, r.history_writes);
}

/* "the processor sets the bit if the table search does not encounter a set WP
 * bit". A write-protected path leaves M alone, so the write costs nothing
 * extra -- and the access is refused by the protection state instead. */
static void test_a_write_protected_path_does_not_set_the_m_bit(void) {
  static const tree_entry_t tree_entries[] = {
      {ROOT_TABLE,
       {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_B, .used = true}},
      {TABLE_B,
       {.dt = AP_M68030_DT_VALID_4BYTE,
        .address_field = TABLE_C,
        .used = true,
        .write_protect = true}},
      {TABLE_C,
       {.dt = AP_M68030_DT_PAGE, .address_field = PAGE_FRAME >> 8, .used = true}},
  };
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(tree_entries, 3);

  ap_m68030_walk_result_t r =
      ap_m68030_walk(&tc, &root, TEST_ADDRESS, &SUPERVISOR_WRITE, tree_fetch,
                     tree_update, &tree);

  TEST_ASSERT_EQUAL_UINT(0, r.history_writes);
  TEST_ASSERT_FALSE(ap_m68030_search_permits_write(&r.search));
}

/* "An access is considered to be a write for updating purposes if either the
 * R/W or RMC signal is low" -- so the *read* half of a read-modify-write
 * already sets M, and costs the write that implies. */
static void test_the_read_half_of_a_read_modify_write_still_sets_m(void) {
  static const ap_m68030_search_access_t rmw_read = {
      .write = false, .read_modify_write = true, .supervisor = true};
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(three_level_tree_used, 3);

  ap_m68030_walk_result_t r = ap_m68030_walk(&tc, &root, TEST_ADDRESS, &rmw_read,
                                             tree_fetch, tree_update, &tree);

  TEST_ASSERT_EQUAL_UINT(1, r.history_writes);
  TEST_ASSERT_TRUE(tree.updates[0].set_modified);
}

/* "This bit is automatically set ... except after a supervisor violation is
 * detected." A user access to a supervisor-only tree updates nothing. */
static void test_a_supervisor_violation_suppresses_the_u_bit_update(void) {
  static const tree_entry_t tree_entries[] = {
      {ROOT_TABLE,
       {.dt = AP_M68030_DT_VALID_4BYTE,
        .address_field = TABLE_B,
        .supervisor = true}},
      {TABLE_B, {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_C}},
      {TABLE_C, {.dt = AP_M68030_DT_PAGE, .address_field = PAGE_FRAME >> 8}},
  };
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(tree_entries, 3);

  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, TEST_ADDRESS, &USER_READ, tree_fetch, tree_update, &tree);

  /* The violation is detected at the root descriptor, whose own S bit sets it,
   * so not even that descriptor's U is written. */
  TEST_ASSERT_EQUAL_UINT(0, r.history_writes);
  TEST_ASSERT_FALSE(ap_m68030_search_permits_access(&r.search, false));
}

/* The same tree accessed from supervisor state has no violation, so the U bits
 * are updated normally -- the suppression is a property of the access, not of
 * the tree. */
static void test_the_same_tree_updates_u_for_a_supervisor_access(void) {
  static const tree_entry_t tree_entries[] = {
      {ROOT_TABLE,
       {.dt = AP_M68030_DT_VALID_4BYTE,
        .address_field = TABLE_B,
        .supervisor = true}},
      {TABLE_B, {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_C}},
      {TABLE_C, {.dt = AP_M68030_DT_PAGE, .address_field = PAGE_FRAME >> 8}},
  };
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(tree_entries, 3);

  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, TEST_ADDRESS, &SUPERVISOR_READ, tree_fetch, tree_update, &tree);

  TEST_ASSERT_EQUAL_UINT(3, r.history_writes);
}

/* Both bits live in one descriptor, so setting both is a single
 * read-modify-write, not two. [030] 11 p. 11-56 counts "an RMC cycle to set the
 * U bit ... as one read and one write". */
static void test_setting_u_and_m_on_one_descriptor_costs_a_single_write(void) {
  static const tree_entry_t tree_entries[] = {
      {ROOT_TABLE,
       {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_B, .used = true}},
      {TABLE_B,
       {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_C, .used = true}},
      /* U clear and M clear on the page descriptor: both must change. */
      {TABLE_C, {.dt = AP_M68030_DT_PAGE, .address_field = PAGE_FRAME >> 8}},
  };
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(tree_entries, 3);

  ap_m68030_walk_result_t r =
      ap_m68030_walk(&tc, &root, TEST_ADDRESS, &SUPERVISOR_WRITE, tree_fetch,
                     tree_update, &tree);

  TEST_ASSERT_EQUAL_UINT(1, r.history_writes);
  TEST_ASSERT_EQUAL_UINT(1, tree.update_count);
  TEST_ASSERT_TRUE(tree.updates[0].set_used);
  TEST_ASSERT_TRUE(tree.updates[0].set_modified);
}

/* An invalid descriptor gets no history write. Beyond DT it is all OS-defined:
 * "short-format invalid descriptors include one or two unused fields. The
 * operating system can use these fields for its own purposes" -- writing a U
 * bit there would corrupt whatever the OS stored, such as the device address of
 * a non-resident page. */
static void test_an_invalid_descriptor_gets_no_history_write(void) {
  static const tree_entry_t tree_entries[] = {
      {ROOT_TABLE,
       {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_B, .used = true}},
      {TABLE_B, {.dt = AP_M68030_DT_INVALID}},
  };
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(tree_entries, 2);

  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, TEST_ADDRESS, &SUPERVISOR_READ, tree_fetch, tree_update, &tree);

  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_UINT(0, r.history_writes);
  TEST_ASSERT_EQUAL_UINT(0, tree.update_count);
}

/* The M bit belongs to the page descriptor an indirect descriptor points at,
 * not to the indirect descriptor itself. */
static void test_the_indirect_target_receives_the_m_bit(void) {
  static const tree_entry_t tree_entries[] = {
      {ROOT_TABLE,
       {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_B, .used = true}},
      {TABLE_B,
       {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_C, .used = true}},
      {TABLE_C,
       {.dt = AP_M68030_DT_VALID_4BYTE,
        .address_field = INDIRECT_TARGET,
        .used = true}},
      {INDIRECT_TARGET,
       {.dt = AP_M68030_DT_PAGE, .address_field = PAGE_FRAME >> 8, .used = true}},
  };
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(tree_entries, 4);

  ap_m68030_walk_result_t r =
      ap_m68030_walk(&tc, &root, TEST_ADDRESS, &SUPERVISOR_WRITE, tree_fetch,
                     tree_update, &tree);

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_UINT(1, r.history_writes);
  TEST_ASSERT_EQUAL_HEX32(INDIRECT_TARGET, tree.updates[0].address);
  TEST_ASSERT_TRUE(tree.updates[0].set_modified);
}

/* A NULL update function is a search that must not disturb the tree, which is
 * what PTEST performs: the reads still happen, no write does. */
static void test_a_search_with_no_update_function_writes_nothing(void) {
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(three_level_tree, 3);

  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, TEST_ADDRESS, &SUPERVISOR_WRITE, tree_fetch, NULL, &tree);

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_UINT(3, r.descriptor_fetches);
  TEST_ASSERT_EQUAL_UINT(0, r.history_writes);
  TEST_ASSERT_EQUAL_UINT(0, tree.update_count);
}

/* A bus error on the write half of the read-modify-write sets B just as one on
 * the read half does, and produces no translation. */
static void test_a_bus_error_updating_history_bits_fails_the_search(void) {
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(three_level_tree, 3);
  tree.update_error_at = TABLE_B;

  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, TEST_ADDRESS, &SUPERVISOR_READ, tree_fetch, tree_update, &tree);

  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_TRUE(r.search.invalid);
  TEST_ASSERT_EQUAL_UINT(2, r.history_writes);
}

/* ---------------------------------------------------------------------------
 * Filling the ATC, [030] 9.4. This is where the cost model closes: the search's
 * bus cycles are paid once, and every later access to the page is a hit that
 * "no performance penalty is associated with".
 * ------------------------------------------------------------------------- */

#define TEST_FC 5u /* supervisor data */

static ap_m68030_atc_t empty_atc(void) {
  ap_m68030_atc_t atc;
  ap_m68030_atc_flush(&atc);
  return atc;
}

/* The whole point of the join: one search, then the address is free. */
static void test_a_filled_entry_turns_the_next_access_into_a_free_hit(void) {
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(three_level_tree, 3);
  ap_m68030_atc_t atc = empty_atc();

  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, TEST_ADDRESS, &SUPERVISOR_READ, tree_fetch, tree_update, &tree);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_UINT(3, r.descriptor_fetches);

  (void)ap_m68030_walk_fill_atc(&atc, &r, &SUPERVISOR_READ, TEST_FC,
                                TEST_ADDRESS, tc.page_size_bits);

  ap_m68030_atc_result_t hit = ap_m68030_atc_lookup(
      &atc, TEST_FC, TEST_ADDRESS, tc.page_size_bits, false, false);
  TEST_ASSERT_EQUAL_INT(AP_M68030_ATC_HIT, hit.status);
  TEST_ASSERT_EQUAL_HEX32(PAGE_FRAME | 0x123u, hit.physical);
}

/* "If a limit violation is detected, the ATC is loaded with an entry having the
 * bus error (B) bit set." The fault is cached, so a faulting address does not
 * re-run the table search on every access. */
static void test_a_limit_violation_is_cached_as_a_faulting_entry(void) {
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  root.has_limit = true;
  root.limit = 0;
  tree_t tree = make_tree(three_level_tree, 3);
  ap_m68030_atc_t atc = empty_atc();

  const uint32_t address = UINT32_C(1) << 25;
  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, address, &SUPERVISOR_READ, tree_fetch, tree_update, &tree);
  TEST_ASSERT_FALSE(r.ok);

  (void)ap_m68030_walk_fill_atc(&atc, &r, &SUPERVISOR_READ, TEST_FC, address,
                                tc.page_size_bits);

  ap_m68030_atc_result_t hit = ap_m68030_atc_lookup(
      &atc, TEST_FC, address, tc.page_size_bits, false, false);
  TEST_ASSERT_EQUAL_INT(AP_M68030_ATC_FAULT, hit.status);
}

/* An invalid descriptor sets B the same way. */
static void test_an_invalid_descriptor_is_cached_as_a_faulting_entry(void) {
  static const tree_entry_t tree_entries[] = {
      {ROOT_TABLE, {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_B}},
      {TABLE_B, {.dt = AP_M68030_DT_INVALID}},
  };
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(tree_entries, 2);
  ap_m68030_atc_t atc = empty_atc();

  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, TEST_ADDRESS, &SUPERVISOR_READ, tree_fetch, tree_update, &tree);
  (void)ap_m68030_walk_fill_atc(&atc, &r, &SUPERVISOR_READ, TEST_FC,
                                TEST_ADDRESS, tc.page_size_bits);

  ap_m68030_atc_result_t hit = ap_m68030_atc_lookup(
      &atc, TEST_FC, TEST_ADDRESS, tc.page_size_bits, false, false);
  TEST_ASSERT_EQUAL_INT(AP_M68030_ATC_FAULT, hit.status);
}

/* A supervisor violation sets B too, and this is the case where the *search*
 * succeeded -- it found a page -- but the entry must still fault, because the
 * violation is a property of the access rather than of the tree. */
static void test_a_supervisor_violation_is_cached_as_a_faulting_entry(void) {
  static const tree_entry_t tree_entries[] = {
      {ROOT_TABLE,
       {.dt = AP_M68030_DT_VALID_4BYTE,
        .address_field = TABLE_B,
        .supervisor = true}},
      {TABLE_B, {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_C}},
      {TABLE_C, {.dt = AP_M68030_DT_PAGE, .address_field = PAGE_FRAME >> 8}},
  };
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(tree_entries, 3);
  ap_m68030_atc_t atc = empty_atc();

  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, TEST_ADDRESS, &USER_READ, tree_fetch, tree_update, &tree);
  TEST_ASSERT_TRUE(r.ok); /* the search itself reached a page */

  (void)ap_m68030_walk_fill_atc(&atc, &r, &USER_READ, TEST_FC, TEST_ADDRESS,
                                tc.page_size_bits);

  ap_m68030_atc_result_t hit = ap_m68030_atc_lookup(
      &atc, TEST_FC, TEST_ADDRESS, tc.page_size_bits, false, false);
  TEST_ASSERT_EQUAL_INT(AP_M68030_ATC_FAULT, hit.status);
}

/* WP accumulated by the search reaches the entry, so the cached translation
 * faults a write while still serving a read. */
static void test_the_filled_entry_carries_write_protection(void) {
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
  ap_m68030_atc_t atc = empty_atc();

  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, TEST_ADDRESS, &SUPERVISOR_READ, tree_fetch, tree_update, &tree);
  (void)ap_m68030_walk_fill_atc(&atc, &r, &SUPERVISOR_READ, TEST_FC,
                                TEST_ADDRESS, tc.page_size_bits);

  ap_m68030_atc_result_t reading = ap_m68030_atc_lookup(
      &atc, TEST_FC, TEST_ADDRESS, tc.page_size_bits, false, false);
  ap_m68030_atc_result_t writing = ap_m68030_atc_lookup(
      &atc, TEST_FC, TEST_ADDRESS, tc.page_size_bits, true, false);

  TEST_ASSERT_EQUAL_INT(AP_M68030_ATC_HIT, reading.status);
  TEST_ASSERT_EQUAL_INT(AP_M68030_ATC_FAULT, writing.status);
}

/* CI accumulated by the search reaches the entry, which is what drives CIOUT. */
static void test_the_filled_entry_carries_cache_inhibit(void) {
  static const tree_entry_t tree_entries[] = {
      {ROOT_TABLE, {.dt = AP_M68030_DT_VALID_4BYTE, .address_field = TABLE_B}},
      {TABLE_B,
       {.dt = AP_M68030_DT_VALID_4BYTE,
        .address_field = TABLE_C,
        .cache_inhibit = true}},
      {TABLE_C, {.dt = AP_M68030_DT_PAGE, .address_field = PAGE_FRAME >> 8}},
  };
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(tree_entries, 3);
  ap_m68030_atc_t atc = empty_atc();

  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, TEST_ADDRESS, &SUPERVISOR_READ, tree_fetch, tree_update, &tree);
  (void)ap_m68030_walk_fill_atc(&atc, &r, &SUPERVISOR_READ, TEST_FC,
                                TEST_ADDRESS, tc.page_size_bits);

  ap_m68030_atc_result_t hit = ap_m68030_atc_lookup(
      &atc, TEST_FC, TEST_ADDRESS, tc.page_size_bits, false, false);
  TEST_ASSERT_EQUAL_INT(AP_M68030_ATC_HIT, hit.status);
  TEST_ASSERT_TRUE(hit.cache_inhibit);
}

/* The timing consequence [030] 9.4 spells out, now demonstrable end to end: a
 * read fills the entry with M clear, so a later *write* to the same page is a
 * hit that still forces a full table search. The first search's cost is not
 * what the second access pays. */
static void test_a_read_fills_m_clear_so_a_later_write_still_costs_a_search(
    void) {
  ap_m68030_tc_t tc = three_level_4k();
  ap_m68030_root_t root = root_at(ROOT_TABLE);
  tree_t tree = make_tree(three_level_tree_used, 3);
  ap_m68030_atc_t atc = empty_atc();

  ap_m68030_walk_result_t r = ap_m68030_walk(
      &tc, &root, TEST_ADDRESS, &SUPERVISOR_READ, tree_fetch, tree_update, &tree);
  TEST_ASSERT_FALSE(r.page_modified);
  (void)ap_m68030_walk_fill_atc(&atc, &r, &SUPERVISOR_READ, TEST_FC,
                                TEST_ADDRESS, tc.page_size_bits);

  /* The read is free the second time. */
  ap_m68030_atc_result_t reading = ap_m68030_atc_lookup(
      &atc, TEST_FC, TEST_ADDRESS, tc.page_size_bits, false, false);
  TEST_ASSERT_EQUAL_INT(AP_M68030_ATC_HIT, reading.status);

  /* The write is not: M is clear, so it must go back to the tree. */
  ap_m68030_atc_result_t writing = ap_m68030_atc_lookup(
      &atc, TEST_FC, TEST_ADDRESS, tc.page_size_bits, true, false);
  TEST_ASSERT_EQUAL_INT(AP_M68030_ATC_MODIFY, writing.status);

  /* And that second search sets M, so refilling makes the write free too. */
  ap_m68030_walk_result_t again = ap_m68030_walk(
      &tc, &root, TEST_ADDRESS, &SUPERVISOR_WRITE, tree_fetch, tree_update, &tree);
  TEST_ASSERT_TRUE(again.page_modified);
  TEST_ASSERT_EQUAL_UINT(1, again.history_writes);
  (void)ap_m68030_walk_fill_atc(&atc, &again, &SUPERVISOR_WRITE, TEST_FC,
                                TEST_ADDRESS, tc.page_size_bits);

  ap_m68030_atc_result_t settled = ap_m68030_atc_lookup(
      &atc, TEST_FC, TEST_ADDRESS, tc.page_size_bits, true, false);
  TEST_ASSERT_EQUAL_INT(AP_M68030_ATC_HIT, settled.status);
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
  RUN_TEST(test_each_descriptor_with_u_clear_costs_one_history_write);
  RUN_TEST(test_a_descriptor_already_used_is_not_written_again);
  RUN_TEST(test_a_write_to_an_unmodified_page_costs_one_more_write_than_a_read);
  RUN_TEST(test_an_already_modified_page_is_not_written_again);
  RUN_TEST(test_a_write_protected_path_does_not_set_the_m_bit);
  RUN_TEST(test_the_read_half_of_a_read_modify_write_still_sets_m);
  RUN_TEST(test_a_supervisor_violation_suppresses_the_u_bit_update);
  RUN_TEST(test_the_same_tree_updates_u_for_a_supervisor_access);
  RUN_TEST(test_setting_u_and_m_on_one_descriptor_costs_a_single_write);
  RUN_TEST(test_an_invalid_descriptor_gets_no_history_write);
  RUN_TEST(test_the_indirect_target_receives_the_m_bit);
  RUN_TEST(test_a_search_with_no_update_function_writes_nothing);
  RUN_TEST(test_a_bus_error_updating_history_bits_fails_the_search);
  RUN_TEST(test_a_filled_entry_turns_the_next_access_into_a_free_hit);
  RUN_TEST(test_a_limit_violation_is_cached_as_a_faulting_entry);
  RUN_TEST(test_an_invalid_descriptor_is_cached_as_a_faulting_entry);
  RUN_TEST(test_a_supervisor_violation_is_cached_as_a_faulting_entry);
  RUN_TEST(test_the_filled_entry_carries_write_protection);
  RUN_TEST(test_the_filled_entry_carries_cache_inhibit);
  RUN_TEST(test_a_read_fills_m_clear_so_a_later_write_still_costs_a_search);
  return UNITY_END();
}
