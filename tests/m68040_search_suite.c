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
  /* The history-bit writeback, recorded as well as applied: what a search did
   * to the tables is half of Table 3-1 and how it did it is the other half. */
  unsigned updates;
  unsigned locked_updates;
  uint32_t last_update_address;
  bool last_set_used;
  bool last_set_modified;
  bool last_locked;
  bool fail_update;
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

/* The write half of a history update: bit 3 is `U` and bit 4 is `M` in both
 * descriptor formats, Figures 3-11 and 3-12. */
static bool memory_update(void *context, uint32_t address, bool set_used,
                          bool set_modified, bool locked) {
  memory_t *m = (memory_t *)context;
  m->updates++;
  if (locked) {
    m->locked_updates++;
  }
  m->last_update_address = address;
  m->last_set_used = set_used;
  m->last_set_modified = set_modified;
  m->last_locked = locked;
  if (m->fail_update) {
    return false;
  }
  const uint32_t index = (address - MEMORY_BASE) / 4u;
  if (index >= MEMORY_LONGS) {
    return false;
  }
  if (set_used) {
    m->word[index] |= UINT32_C(1) << 3;
  }
  if (set_modified) {
    m->word[index] |= UINT32_C(1) << 4;
  }
  return true;
}

/* A search configured as one real access, which is the only configuration the
 * history rules mean anything in. */
static ap_m68040_search_config_t updating_config(memory_t *m, bool write,
                                                 bool supervisor) {
  ap_m68040_search_config_t config = config_for(m, AP_M68040_PAGE_4K);
  config.write = write;
  config.supervisor = supervisor;
  config.update = memory_update;
  config.update_context = m;
  return config;
}

static uint32_t descriptor(const memory_t *m, uint32_t address) {
  return m->word[(address - MEMORY_BASE) / 4u];
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


/* ---------------------------------------------------------------------------
 * The history bits, `[040]` §3.2.5 and Table 3-1, read as page images.
 * ------------------------------------------------------------------------- */

#define DESC_U (UINT32_C(1) << 3)
#define DESC_M (UINT32_C(1) << 4)
#define DESC_W (UINT32_C(1) << 2)
#define DESC_S (UINT32_C(1) << 7)

static void test_a_read_sets_u_in_every_descriptor_it_walks(void) {
  /* "During a table search, the U-bit in each encountered descriptor is checked
   * and set if not already set." Three descriptors, three writes. */
  memory_t m;
  build(&m);
  const ap_m68040_search_config_t config = updating_config(&m, false, true);

  const ap_m68040_search_result_t r = ap_m68040_search(&config, 0x0000u);

  TEST_ASSERT_EQUAL_INT(AP_M68040_SEARCH_RESIDENT, r.status);
  TEST_ASSERT_EQUAL_UINT(3u, r.updates);
  TEST_ASSERT_EQUAL_UINT(3u, m.updates);
  TEST_ASSERT_TRUE((descriptor(&m, 0x1000u) & DESC_U) != 0u);
  TEST_ASSERT_TRUE((descriptor(&m, 0x2000u) & DESC_U) != 0u);
  TEST_ASSERT_TRUE((descriptor(&m, 0x3000u) & DESC_U) != 0u);
  /* A read never sets M. */
  TEST_ASSERT_TRUE((descriptor(&m, 0x3000u) & DESC_M) == 0u);
  TEST_ASSERT_FALSE(r.modified);
}

static void test_every_history_write_on_a_read_is_a_locked_rmw(void) {
  /* Table 3-1 stands "Locked RMW Access to Set U" against both read rows that
   * write anything, and a table descriptor has no M to set at all. */
  memory_t m;
  build(&m);
  const ap_m68040_search_config_t config = updating_config(&m, false, true);
  const ap_m68040_search_result_t r = ap_m68040_search(&config, 0x0000u);
  TEST_ASSERT_EQUAL_UINT(3u, r.locked_updates);
}

static void test_a_second_search_writes_nothing_once_u_is_set(void) {
  /* "For a table descriptor, a write cycle that sets the U-bit occurs only if
   * the U-bit was clear." */
  memory_t m;
  build(&m);
  const ap_m68040_search_config_t config = updating_config(&m, false, true);
  (void)ap_m68040_search(&config, 0x0000u);
  m.updates = 0;
  const ap_m68040_search_result_t again = ap_m68040_search(&config, 0x0000u);
  TEST_ASSERT_EQUAL_UINT(0u, again.updates);
  TEST_ASSERT_EQUAL_UINT(0u, m.updates);
}

/* Table 3-1, row by row. `wp` is the note's "accumulated write-protect status",
 * set here on the page descriptor itself; the read rows are marked `X` in the
 * WP column, so both values are run against them. */
typedef struct {
  bool used;
  bool modified;
  bool write_protect;
  bool write;
  unsigned expected_updates;  /* one write cycle, or none */
  bool expected_locked;
  bool expected_used;
  bool expected_modified;
} history_row_t;

static void test_table_3_1_row_by_row(void) {
  static const history_row_t rows[] = {
      /* Read, WP = X: run against both. */
      {false, false, false, false, 1u, true, true, false},
      {false, false, true, false, 1u, true, true, false},
      {false, true, false, false, 1u, true, true, true},
      {false, true, true, false, 1u, true, true, true},
      {true, false, false, false, 0u, false, true, false},
      {true, false, true, false, 0u, false, true, false},
      {true, true, false, false, 0u, false, true, true},
      {true, true, true, false, 0u, false, true, true},
      /* Write, WP = 0: the two unlocked rows live here. */
      {false, false, false, true, 1u, false, true, true},
      {false, true, false, true, 1u, true, true, true},
      {true, false, false, true, 1u, false, true, true},
      {true, true, false, true, 0u, false, true, true},
      /* Write, WP = 1. */
      {false, false, true, true, 1u, true, true, false},
      {false, true, true, true, 1u, true, true, true},
      {true, false, true, true, 0u, false, true, false},
      {true, true, true, true, 0u, false, true, true},
  };

  for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
    const history_row_t *row = &rows[i];
    memory_t m;
    build(&m);
    /* The two table descriptors start used, so the only write cycle a row can
     * produce is the page descriptor's. */
    put(&m, 0x1000u, 0x2000u | 0x2u | DESC_U);
    put(&m, 0x2000u, 0x3000u | 0x2u | DESC_U);
    put(&m, 0x3000u,
        0x50000u | 0x1u | (row->used ? DESC_U : 0u) |
            (row->modified ? DESC_M : 0u) |
            (row->write_protect ? DESC_W : 0u));

    const ap_m68040_search_config_t config =
        updating_config(&m, row->write, true);
    const ap_m68040_search_result_t r = ap_m68040_search(&config, 0x0000u);

    TEST_ASSERT_EQUAL_INT(AP_M68040_SEARCH_RESIDENT, r.status);
    TEST_ASSERT_EQUAL_UINT(row->expected_updates, r.updates);
    TEST_ASSERT_EQUAL_UINT(row->expected_updates ? (row->expected_locked ? 1u : 0u)
                                                 : 0u,
                           r.locked_updates);
    const uint32_t final = descriptor(&m, 0x3000u);
    TEST_ASSERT_EQUAL_UINT(row->expected_used ? 1u : 0u,
                           (final & DESC_U) != 0u ? 1u : 0u);
    TEST_ASSERT_EQUAL_UINT(row->expected_modified ? 1u : 0u,
                           (final & DESC_M) != 0u ? 1u : 0u);
    TEST_ASSERT_EQUAL_UINT(row->expected_modified ? 1u : 0u, r.modified ? 1u : 0u);
    /* The state Table 3-1 never produces, and that
     * `ap_m68040_page_descriptor_is_incoherent` names from the other side: no
     * row ends with M set and U clear. */
    TEST_ASSERT_FALSE((final & DESC_M) != 0u && (final & DESC_U) == 0u);
  }
}

static void test_write_protection_inherited_from_a_table_descriptor_suppresses_m(void) {
  /* The note under Table 3-1: "WP indicates the **accumulated** write-protect
   * status", so a W set two levels up denies M to a write at the leaf. */
  memory_t m;
  build(&m);
  put(&m, 0x1000u, 0x2000u | 0x2u | DESC_W);
  const ap_m68040_search_config_t config = updating_config(&m, true, true);

  const ap_m68040_search_result_t r = ap_m68040_search(&config, 0x0000u);

  TEST_ASSERT_TRUE(r.write_protect);
  TEST_ASSERT_FALSE(r.modified);
  TEST_ASSERT_TRUE((descriptor(&m, 0x3000u) & DESC_M) == 0u);
  TEST_ASSERT_TRUE((descriptor(&m, 0x3000u) & DESC_U) != 0u);
}

static void test_a_supervisor_violation_suppresses_m_but_not_u(void) {
  /* §3.2.5: the processor sets M "if the table search does not encounter a set
   * W-bit **or a supervisor violation**". The clause is attached to M alone --
   * Table 3-1 has no supervisor column and every row sets U. */
  memory_t m;
  build(&m);
  put(&m, 0x3000u, 0x50000u | 0x1u | DESC_S);
  const ap_m68040_search_config_t config = updating_config(&m, true, false);

  const ap_m68040_search_result_t r = ap_m68040_search(&config, 0x0000u);

  TEST_ASSERT_TRUE(r.supervisor);
  TEST_ASSERT_FALSE(r.modified);
  TEST_ASSERT_TRUE((descriptor(&m, 0x3000u) & DESC_M) == 0u);
  TEST_ASSERT_TRUE((descriptor(&m, 0x3000u) & DESC_U) != 0u);
}

static void test_a_supervisor_page_reached_by_a_supervisor_write_is_modified(void) {
  /* The control for the test above: the same descriptor, the same write, and
   * the only thing that varies is the privilege the access carries. */
  memory_t m;
  build(&m);
  put(&m, 0x3000u, 0x50000u | 0x1u | DESC_S);
  const ap_m68040_search_config_t config = updating_config(&m, true, true);

  const ap_m68040_search_result_t r = ap_m68040_search(&config, 0x0000u);

  TEST_ASSERT_TRUE(r.modified);
  TEST_ASSERT_TRUE((descriptor(&m, 0x3000u) & DESC_M) != 0u);
}

static void test_an_invalid_descriptor_is_never_written(void) {
  /* An invalid descriptor's other thirty bits belong to the operating system,
   * so bit 3 is not a U bit. The two descriptors above it are still updated:
   * they were encountered. */
  memory_t m;
  build(&m);
  put(&m, 0x3000u, 0x50000u); /* PDT 00 */
  const ap_m68040_search_config_t config = updating_config(&m, true, true);

  const ap_m68040_search_result_t r = ap_m68040_search(&config, 0x0000u);

  TEST_ASSERT_EQUAL_INT(AP_M68040_SEARCH_INVALID, r.status);
  TEST_ASSERT_EQUAL_UINT(2u, r.updates);
  TEST_ASSERT_EQUAL_HEX32(0x50000u, descriptor(&m, 0x3000u));
}

static void test_an_invalid_root_descriptor_is_never_written(void) {
  memory_t m;
  build(&m);
  put(&m, 0x1000u, 0x2000u); /* UDT 00 */
  const ap_m68040_search_config_t config = updating_config(&m, false, true);

  const ap_m68040_search_result_t r = ap_m68040_search(&config, 0x0000u);

  TEST_ASSERT_EQUAL_INT(AP_M68040_SEARCH_INVALID, r.status);
  TEST_ASSERT_EQUAL_UINT(0u, r.updates);
  TEST_ASSERT_EQUAL_HEX32(0x2000u, descriptor(&m, 0x1000u));
}

static void test_the_indirect_descriptor_is_not_written_and_its_target_is(void) {
  /* Figure 3-12 gives an indirect page descriptor as DESCRIPTOR ADDRESS in bits
   * 31-2 and PDT in 1-0: bit 3 is part of the pointer, so a U written there
   * would move the page. The descriptor it names carries U and M. */
  memory_t m;
  build(&m);
  put(&m, 0x3000u, 0x4000u | 0x2u); /* PDT 10, indirect, target 0x4000 */
  put(&m, 0x4000u, 0x60000u | 0x1u);
  const ap_m68040_search_config_t config = updating_config(&m, true, true);

  const ap_m68040_search_result_t r = ap_m68040_search(&config, 0x0000u);

  TEST_ASSERT_EQUAL_INT(AP_M68040_SEARCH_RESIDENT, r.status);
  TEST_ASSERT_TRUE(r.indirect);
  TEST_ASSERT_EQUAL_HEX32(0x4000u | 0x2u, descriptor(&m, 0x3000u));
  TEST_ASSERT_TRUE((descriptor(&m, 0x4000u) & DESC_U) != 0u);
  TEST_ASSERT_TRUE((descriptor(&m, 0x4000u) & DESC_M) != 0u);
  TEST_ASSERT_EQUAL_HEX32(0x4000u, m.last_update_address);
}

static void test_a_null_update_leaves_the_tables_untouched(void) {
  /* What an observer passes. The translation is the same one; only the tree is
   * spared. */
  memory_t m;
  build(&m);
  ap_m68040_search_config_t config = updating_config(&m, true, true);
  config.update = NULL;

  const ap_m68040_search_result_t r = ap_m68040_search(&config, 0x0000u);

  TEST_ASSERT_EQUAL_INT(AP_M68040_SEARCH_RESIDENT, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x50000u, r.physical_address);
  TEST_ASSERT_EQUAL_UINT(0u, r.updates);
  TEST_ASSERT_EQUAL_UINT(0u, m.updates);
  TEST_ASSERT_EQUAL_HEX32(0x2000u | 0x2u, descriptor(&m, 0x1000u));
  TEST_ASSERT_EQUAL_HEX32(0x50000u | 0x1u, descriptor(&m, 0x3000u));
  /* And with nothing written back, M is the descriptor's own -- not the
   * access's. */
  TEST_ASSERT_FALSE(r.modified);
}

static void test_a_transfer_error_on_a_history_write_ends_the_search(void) {
  /* The update is part of the search -- "the U-bit and M-bit are updated before
   * the M68040 allows a page to be accessed or written" -- so a failure on the
   * write half is a transfer error like a failure on a fetch. */
  memory_t m;
  build(&m);
  m.fail_update = true;
  const ap_m68040_search_config_t config = updating_config(&m, false, true);

  const ap_m68040_search_result_t r = ap_m68040_search(&config, 0x0000u);

  TEST_ASSERT_EQUAL_INT(AP_M68040_SEARCH_BUS_ERROR, r.status);
  /* It stopped at the first descriptor, so only one fetch followed it. */
  TEST_ASSERT_EQUAL_UINT(1u, r.fetches);
  TEST_ASSERT_EQUAL_UINT(1u, r.updates);
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
  RUN_TEST(test_a_read_sets_u_in_every_descriptor_it_walks);
  RUN_TEST(test_every_history_write_on_a_read_is_a_locked_rmw);
  RUN_TEST(test_a_second_search_writes_nothing_once_u_is_set);
  RUN_TEST(test_table_3_1_row_by_row);
  RUN_TEST(test_write_protection_inherited_from_a_table_descriptor_suppresses_m);
  RUN_TEST(test_a_supervisor_violation_suppresses_m_but_not_u);
  RUN_TEST(test_a_supervisor_page_reached_by_a_supervisor_write_is_modified);
  RUN_TEST(test_an_invalid_descriptor_is_never_written);
  RUN_TEST(test_an_invalid_root_descriptor_is_never_written);
  RUN_TEST(test_the_indirect_descriptor_is_not_written_and_its_target_is);
  RUN_TEST(test_a_null_update_leaves_the_tables_untouched);
  RUN_TEST(test_a_transfer_error_on_a_history_write_ends_the_search);
  return UNITY_END();
}
