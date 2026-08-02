/* MC68851 table search, `[68851]` Figures 5-23 and 5-26 and the root pointer
 * selection truth table, read from the page images.
 *
 * The search is driven by a fetch callback over a small in-test memory, so a
 * whole translation tree can be built and walked without a bus.
 */

#include <string.h>

#include "cpu/m68851/ap_m68851_search.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------------------
 * A toy physical memory: a flat array of long words at known addresses.
 * ------------------------------------------------------------------------- */

/* 16 KB from MEMORY_BASE: enough to hold every table these tests build, at
 * 0x1000 through 0x4000. */
#define MEMORY_LONGS 4096u
#define MEMORY_BASE 0x1000u

typedef struct {
  uint32_t word[MEMORY_LONGS];
  bool bus_error_at_set;
  uint32_t bus_error_at;
  unsigned fetches;
} memory_t;

static bool memory_fetch(void *context, uint32_t address, unsigned bytes,
                         uint64_t *value) {
  memory_t *m = (memory_t *)context;
  m->fetches++;
  if (m->bus_error_at_set && address == m->bus_error_at) {
    return false;
  }
  const uint32_t index = (address - MEMORY_BASE) / 4u;
  if (index >= MEMORY_LONGS) {
    return false;
  }
  if (bytes == 4u) {
    *value = m->word[index];
  } else {
    /* Big endian: the upper long word first. */
    *value = ((uint64_t)m->word[index] << 32) | m->word[index + 1u];
  }
  return true;
}

static void put_long(memory_t *m, uint32_t address, uint64_t value) {
  const uint32_t index = (address - MEMORY_BASE) / 4u;
  m->word[index] = (uint32_t)(value >> 32);
  m->word[index + 1u] = (uint32_t)value;
}

static void put_short(memory_t *m, uint32_t address, uint32_t value) {
  m->word[(address - MEMORY_BASE) / 4u] = value;
}

/* Two levels of ten index bits over 4K pages: 0 + 10 + 10 + 12 = 32. */
static ap_m68851_tc_t two_level_tc(void) {
  return (ap_m68851_tc_t){
      .enable = true,
      .page_size = 0xCu,
      .initial_shift = 0,
      .table_index = {10u, 10u, 0u, 0u},
  };
}

/* A root pointer naming a short-format table with the limit suppressed. */
static ap_m68851_rp_t root_at(uint32_t table, ap_m68851_descriptor_type_t dt) {
  return (ap_m68851_rp_t){
      .descriptor_type = dt,
      .table_address = table,
      .lower_limit = false,
      .limit = 0x7FFFu, /* suppressed */
  };
}

/* ---------------------------------------------------------------------------
 * Root pointer selection: the truth table beside Figure 5-23.
 * ------------------------------------------------------------------------- */

static void test_the_root_pointer_selection_truth_table(void) {
  /* All eight rows. FC3 is the top bit of the function code, FC2 the next. */
  const struct { unsigned fc3, fc2; bool sre; ap_m68851_root_t root; } rows[] = {
      {0, 0, false, AP_M68851_ROOT_CRP}, {0, 0, true, AP_M68851_ROOT_CRP},
      {0, 1, false, AP_M68851_ROOT_CRP}, {0, 1, true, AP_M68851_ROOT_SRP},
      {1, 0, false, AP_M68851_ROOT_DRP}, {1, 0, true, AP_M68851_ROOT_DRP},
      {1, 1, false, AP_M68851_ROOT_DRP}, {1, 1, true, AP_M68851_ROOT_DRP},
  };
  for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
    const unsigned fc = (rows[i].fc3 << 3) | (rows[i].fc2 << 2);
    TEST_ASSERT_EQUAL_INT(rows[i].root,
                          ap_m68851_select_root(fc, rows[i].sre));
  }
}

static void test_a_dma_master_always_uses_the_dma_root_pointer(void) {
  /* FC3 alone decides it: no combination of the other function code bits or
   * `SRE` sends a non-CPU bus master anywhere else. */
  for (unsigned low = 0; low < 8u; low++) {
    TEST_ASSERT_EQUAL_INT(AP_M68851_ROOT_DRP,
                          ap_m68851_select_root(0x8u | low, false));
    TEST_ASSERT_EQUAL_INT(AP_M68851_ROOT_DRP,
                          ap_m68851_select_root(0x8u | low, true));
  }
}

static void test_supervisor_uses_the_cpu_root_pointer_when_sre_is_clear(void) {
  /* §6.1.3.2: "when SRE is clear, use of the supervisor root pointer is
   * disabled, and the CPU root pointer is used for supervisor space
   * translations." */
  TEST_ASSERT_EQUAL_INT(AP_M68851_ROOT_CRP, ap_m68851_select_root(6u, false));
  TEST_ASSERT_EQUAL_INT(AP_M68851_ROOT_SRP, ap_m68851_select_root(6u, true));
}

/* ---------------------------------------------------------------------------
 * Index extraction.
 * ------------------------------------------------------------------------- */

static void test_each_level_consumes_its_own_bits_of_the_address(void) {
  /* Levels take their `TIx` bits from the top down, after the initial shift.
   * A level's index therefore depends on every level above it. */
  const ap_m68851_tc_t tc = two_level_tc();
  /* 0x00A01A00: A bits [31-22] = 0x002, B bits [21-12] = 0x201. */
  TEST_ASSERT_EQUAL_UINT(0x002u, ap_m68851_search_index(&tc, 0x00A01A00u, 0u));
  TEST_ASSERT_EQUAL_UINT(0x201u, ap_m68851_search_index(&tc, 0x00A01A00u, 1u));
}

static void test_the_initial_shift_discards_the_top_bits(void) {
  /* "The number of bits to discard from the logical address, starting with bit
   * [31]." With eight discarded, level A starts at bit 23 and the top byte of
   * the address cannot affect the index at all. */
  ap_m68851_tc_t tc = two_level_tc();
  tc.initial_shift = 8u;
  tc.table_index[0] = 2u; /* 8 + 2 + 10 + 12 = 32 */
  TEST_ASSERT_EQUAL_UINT(ap_m68851_search_index(&tc, 0x00A01A00u, 0u),
                         ap_m68851_search_index(&tc, 0xFFA01A00u, 0u));
}

/* ---------------------------------------------------------------------------
 * The search itself.
 * ------------------------------------------------------------------------- */

static void test_a_two_level_search_reaches_the_page_frame(void) {
  memory_t m = {0};
  /* Level A table at 0x1000, entry 0 names a level B table at 0x2000. */
  put_short(&m, 0x1000u, 0x2000u | 0x2u); /* short table descriptor, DT = $2 */
  /* Level B table at 0x2000, entry 0 names a page at 0x50000. */
  put_short(&m, 0x2000u, 0x50000u | 0x1u); /* DT = $1: a page descriptor */

  const ap_m68851_tc_t tc = two_level_tc();
  const ap_m68851_rp_t root = root_at(0x1000u, AP_M68851_DT_VALID_4_BYTE);
  const ap_m68851_search_config_t config = {
      .tc = &tc, .root = &root, .fetch = memory_fetch, .fetch_context = &m};

  const ap_m68851_search_result_t r = ap_m68851_search(&config, 0u, 5u);
  TEST_ASSERT_EQUAL_INT(AP_M68851_SEARCH_TYPE_NORMAL, r.type);
  TEST_ASSERT_EQUAL_HEX32(0x50000u, r.physical_address);
  TEST_ASSERT_EQUAL_UINT(2u, r.levels);
}

static void test_a_page_descriptor_with_levels_remaining_terminates_early(void) {
  /* Figure 5-23 advances `x` on a page descriptor and asks whether the next
   * `TIx` is zero: if not, levels were skipped and the type is EARLY. That is
   * the type-2 case reached from the algorithm's side. */
  memory_t m = {0};
  put_short(&m, 0x1000u, 0x50000u | 0x1u); /* page descriptor at level A */

  const ap_m68851_tc_t tc = two_level_tc(); /* level B still has 10 bits */
  const ap_m68851_rp_t root = root_at(0x1000u, AP_M68851_DT_VALID_4_BYTE);
  const ap_m68851_search_config_t config = {
      .tc = &tc, .root = &root, .fetch = memory_fetch, .fetch_context = &m};

  const ap_m68851_search_result_t r = ap_m68851_search(&config, 0u, 5u);
  TEST_ASSERT_EQUAL_INT(AP_M68851_SEARCH_TYPE_EARLY, r.type);
  TEST_ASSERT_EQUAL_UINT(1u, r.levels);
}

static void test_a_page_descriptor_at_the_last_level_is_normal(void) {
  /* The same descriptor, with no level below it: the search was complete, so
   * the type is NORMAL. Only the tree's shape distinguishes the two. */
  memory_t m = {0};
  put_short(&m, 0x1000u, 0x50000u | 0x1u);

  ap_m68851_tc_t tc = two_level_tc();
  tc.table_index[0] = 20u; /* one level: 20 + 12 = 32 */
  tc.table_index[1] = 0u;
  const ap_m68851_rp_t root = root_at(0x1000u, AP_M68851_DT_VALID_4_BYTE);
  const ap_m68851_search_config_t config = {
      .tc = &tc, .root = &root, .fetch = memory_fetch, .fetch_context = &m};

  const ap_m68851_search_result_t r = ap_m68851_search(&config, 0u, 5u);
  TEST_ASSERT_EQUAL_INT(AP_M68851_SEARCH_TYPE_NORMAL, r.type);
}

static void test_a_root_pointer_page_descriptor_maps_by_a_constant_offset(void) {
  /* §6.1.1.4: "the page descriptor is formed by adding (unsigned) the value in
   * the table address field to the incoming logical address. This operation
   * yields a direct-mapping of the logical address space with a constant
   * offset." No table is walked, so no descriptor is fetched at all. */
  memory_t m = {0};
  const ap_m68851_tc_t tc = two_level_tc();
  const ap_m68851_rp_t root = root_at(0x40000u, AP_M68851_DT_PAGE_DESCRIPTOR);
  const ap_m68851_search_config_t config = {
      .tc = &tc, .root = &root, .fetch = memory_fetch, .fetch_context = &m};

  const ap_m68851_search_result_t r = ap_m68851_search(&config, 0x1234u, 5u);
  TEST_ASSERT_EQUAL_INT(AP_M68851_SEARCH_TYPE_EARLY, r.type);
  TEST_ASSERT_EQUAL_HEX32(0x41234u, r.physical_address);
  TEST_ASSERT_EQUAL_UINT(0u, m.fetches);
}

static void test_an_invalid_descriptor_ends_the_search(void) {
  memory_t m = {0};
  put_short(&m, 0x1000u, 0x0u); /* DT = $0 */

  const ap_m68851_tc_t tc = two_level_tc();
  const ap_m68851_rp_t root = root_at(0x1000u, AP_M68851_DT_VALID_4_BYTE);
  const ap_m68851_search_config_t config = {
      .tc = &tc, .root = &root, .fetch = memory_fetch, .fetch_context = &m};

  const ap_m68851_search_result_t r = ap_m68851_search(&config, 0u, 5u);
  TEST_ASSERT_EQUAL_INT(AP_M68851_SEARCH_TYPE_INVALID, r.type);
  TEST_ASSERT_EQUAL_INT(AP_M68851_SEARCH_FAULT_INVALID_DESCRIPTOR, r.fault);
}

static void test_a_bus_error_during_the_search_ends_it(void) {
  memory_t m = {0};
  m.bus_error_at_set = true;
  m.bus_error_at = 0x1000u;

  const ap_m68851_tc_t tc = two_level_tc();
  const ap_m68851_rp_t root = root_at(0x1000u, AP_M68851_DT_VALID_4_BYTE);
  const ap_m68851_search_config_t config = {
      .tc = &tc, .root = &root, .fetch = memory_fetch, .fetch_context = &m};

  const ap_m68851_search_result_t r = ap_m68851_search(&config, 0u, 5u);
  TEST_ASSERT_EQUAL_INT(AP_M68851_SEARCH_TYPE_INVALID, r.type);
  TEST_ASSERT_EQUAL_INT(AP_M68851_SEARCH_FAULT_BUS_ERROR, r.fault);
}

/* ---------------------------------------------------------------------------
 * The limit check, Figure 5-26.
 * ------------------------------------------------------------------------- */

static void test_an_index_beyond_an_upper_limit_is_a_violation(void) {
  memory_t m = {0};
  put_short(&m, 0x1000u, 0x2000u | 0x2u);

  const ap_m68851_tc_t tc = two_level_tc();
  ap_m68851_rp_t root = root_at(0x1000u, AP_M68851_DT_VALID_4_BYTE);
  root.lower_limit = false;
  root.limit = 1u; /* level A index must be <= 1 */
  const ap_m68851_search_config_t config = {
      .tc = &tc, .root = &root, .fetch = memory_fetch, .fetch_context = &m};

  /* Level A index 0: within. */
  TEST_ASSERT_NOT_EQUAL_INT(
      AP_M68851_SEARCH_FAULT_LIMIT_VIOLATION,
      ap_m68851_search(&config, 0x00000000u, 5u).fault);
  /* Level A index 2: beyond. Address bits [31-22] = 2. */
  const ap_m68851_search_result_t r =
      ap_m68851_search(&config, 0x00800000u, 5u);
  TEST_ASSERT_EQUAL_INT(AP_M68851_SEARCH_TYPE_INVALID, r.type);
  TEST_ASSERT_EQUAL_INT(AP_M68851_SEARCH_FAULT_LIMIT_VIOLATION, r.fault);
}

static void test_a_short_previous_descriptor_suppresses_the_limit_check(void) {
  /* Figure 5-26: "LAST_SIZE = 4" returns without checking. A short-format
   * descriptor has no limit field, so whether level B is bounded was decided by
   * the *format* of the descriptor found at level A -- which is the whole
   * reason the flowchart carries LAST_SIZE at all.
   *
   * Here the root pointer's own limit would reject the level A index, so the
   * check must be reached to see any effect; the level A descriptor is short,
   * which suppresses the check at level B. */
  memory_t m = {0};
  put_short(&m, 0x1000u, 0x2000u | 0x2u); /* short: LAST_SIZE becomes 4 */
  put_short(&m, 0x2000u + 0x201u * 4u, 0x50000u | 0x1u);

  const ap_m68851_tc_t tc = two_level_tc();
  const ap_m68851_rp_t root = root_at(0x1000u, AP_M68851_DT_VALID_4_BYTE);
  const ap_m68851_search_config_t config = {
      .tc = &tc, .root = &root, .fetch = memory_fetch, .fetch_context = &m};

  /* Level B index 0x201, far above any small limit -- and unchecked. */
  const ap_m68851_search_result_t r =
      ap_m68851_search(&config, 0x00201000u, 5u);
  TEST_ASSERT_EQUAL_INT(AP_M68851_SEARCH_TYPE_NORMAL, r.type);
  TEST_ASSERT_EQUAL_HEX32(0x50000u, r.physical_address);
}

static void test_function_code_lookup_suppresses_the_root_limit_check(void) {
  /* Figure 5-26's "FCL = 1 OR DRP IS RP" branch: with a function code lookup
   * the root pointer's limit is not checked, because the first index is the
   * function code rather than part of the logical address. */
  memory_t m = {0};
  put_short(&m, 0x1000u + 5u * 4u, 0x2000u | 0x2u); /* FC 5 names a table */
  put_short(&m, 0x2000u, 0x50000u | 0x1u);

  ap_m68851_tc_t tc = two_level_tc();
  tc.function_code_lookup = true;
  ap_m68851_rp_t root = root_at(0x1000u, AP_M68851_DT_VALID_4_BYTE);
  root.limit = 0u; /* would reject everything if it were checked */
  const ap_m68851_search_config_t config = {
      .tc = &tc, .root = &root, .fetch = memory_fetch, .fetch_context = &m};

  const ap_m68851_search_result_t r = ap_m68851_search(&config, 0u, 5u);
  TEST_ASSERT_NOT_EQUAL_INT(AP_M68851_SEARCH_FAULT_LIMIT_VIOLATION, r.fault);
}

static void test_a_function_code_lookup_indexes_by_the_function_code(void) {
  /* "The top level table in the translation tree should be indexed with the
   * function code." Two function codes reach different tables. */
  memory_t m = {0};
  put_short(&m, 0x1000u + 5u * 4u, 0x2000u | 0x2u);
  put_short(&m, 0x1000u + 6u * 4u, 0x3000u | 0x2u);
  put_short(&m, 0x2000u, 0x50000u | 0x1u);
  put_short(&m, 0x3000u, 0x60000u | 0x1u);

  ap_m68851_tc_t tc = two_level_tc();
  tc.function_code_lookup = true;
  const ap_m68851_rp_t root = root_at(0x1000u, AP_M68851_DT_VALID_4_BYTE);
  const ap_m68851_search_config_t config = {
      .tc = &tc, .root = &root, .fetch = memory_fetch, .fetch_context = &m};

  TEST_ASSERT_EQUAL_HEX32(0x50000u,
                          ap_m68851_search(&config, 0u, 5u).physical_address);
  TEST_ASSERT_EQUAL_HEX32(0x60000u,
                          ap_m68851_search(&config, 0u, 6u).physical_address);
}

/* ---------------------------------------------------------------------------
 * Indirection and protection.
 * ------------------------------------------------------------------------- */

static void test_a_valid_descriptor_past_the_last_level_is_an_indirection(void) {
  /* Figure 5-23: at the last level a `DT` of $2 or $3 is an *indirect*
   * descriptor rather than another table -- the same bits that were a table
   * descriptor one level up. */
  memory_t m = {0};
  put_short(&m, 0x1000u, 0x2000u | 0x2u);   /* level A -> level B table */
  put_short(&m, 0x2000u, 0x3004u | 0x2u);   /* level B -> indirect at 0x3004 */
  put_short(&m, 0x3004u, 0x50000u | 0x1u);  /* the page descriptor named */

  const ap_m68851_tc_t tc = two_level_tc();
  const ap_m68851_rp_t root = root_at(0x1000u, AP_M68851_DT_VALID_4_BYTE);
  const ap_m68851_search_config_t config = {
      .tc = &tc, .root = &root, .fetch = memory_fetch, .fetch_context = &m};

  const ap_m68851_search_result_t r = ap_m68851_search(&config, 0u, 5u);
  TEST_ASSERT_EQUAL_INT(AP_M68851_SEARCH_TYPE_INDIRECT, r.type);
  TEST_ASSERT_EQUAL_HEX32(0x50000u, r.physical_address);
  TEST_ASSERT_EQUAL_UINT(3u, r.levels);
}

static void test_an_indirection_naming_anything_but_a_page_is_invalid(void) {
  /* "DT = 'PAGE DESCRIPTOR' ... OTHERWISE: TYPE <- 'INVALID'". Figure 5-10's
   * illegal cells, and what stops a chain of indirections from looping. */
  memory_t m = {0};
  put_short(&m, 0x1000u, 0x2000u | 0x2u);
  put_short(&m, 0x2000u, 0x3004u | 0x2u);
  put_short(&m, 0x3004u, 0x4000u | 0x2u); /* another indirection */

  const ap_m68851_tc_t tc = two_level_tc();
  const ap_m68851_rp_t root = root_at(0x1000u, AP_M68851_DT_VALID_4_BYTE);
  const ap_m68851_search_config_t config = {
      .tc = &tc, .root = &root, .fetch = memory_fetch, .fetch_context = &m};

  const ap_m68851_search_result_t r = ap_m68851_search(&config, 0u, 5u);
  TEST_ASSERT_EQUAL_INT(AP_M68851_SEARCH_TYPE_INVALID, r.type);
  TEST_ASSERT_EQUAL_INT(AP_M68851_SEARCH_FAULT_INVALID_DESCRIPTOR, r.fault);
}

static void test_write_protection_accumulates_down_the_tree(void) {
  /* §5.2.1.2 calls the ATC's copy "the effective write protection determined
   * during the translation table search". A write protect at any level
   * protects everything below it, and a clear bit lower down cannot undo it --
   * which is what makes protection a property of the path, not of the leaf. */
  memory_t m = {0};
  put_short(&m, 0x1000u, 0x2000u | 0x4u | 0x2u); /* WP set on the table */
  put_short(&m, 0x2000u, 0x50000u | 0x1u);       /* WP clear on the page */

  const ap_m68851_tc_t tc = two_level_tc();
  const ap_m68851_rp_t root = root_at(0x1000u, AP_M68851_DT_VALID_4_BYTE);
  const ap_m68851_search_config_t config = {
      .tc = &tc, .root = &root, .fetch = memory_fetch, .fetch_context = &m};

  const ap_m68851_search_result_t r = ap_m68851_search(&config, 0u, 5u);
  TEST_ASSERT_EQUAL_INT(AP_M68851_SEARCH_TYPE_NORMAL, r.type);
  TEST_ASSERT_TRUE(r.write_protect);
}

static void test_the_page_descriptors_attributes_reach_the_result(void) {
  memory_t m = {0};
  put_short(&m, 0x1000u, 0x2000u | 0x2u);
  /* Page at 0x50000 with G, CI, L and M set: 0x80|0x40|0x20|0x10 = 0xF0. */
  put_short(&m, 0x2000u, 0x50000u | 0xF0u | 0x1u);

  const ap_m68851_tc_t tc = two_level_tc();
  const ap_m68851_rp_t root = root_at(0x1000u, AP_M68851_DT_VALID_4_BYTE);
  const ap_m68851_search_config_t config = {
      .tc = &tc, .root = &root, .fetch = memory_fetch, .fetch_context = &m};

  const ap_m68851_search_result_t r = ap_m68851_search(&config, 0u, 5u);
  TEST_ASSERT_TRUE(r.gate);
  TEST_ASSERT_TRUE(r.cache_inhibit);
  TEST_ASSERT_TRUE(r.lock);
  TEST_ASSERT_TRUE(r.modified);
}

static void test_a_long_format_tree_walks_the_same_way(void) {
  /* The same two-level tree in 64-bit descriptors: the search is indifferent to
   * the format except in how far it steps and where it finds `DT`. */
  memory_t m = {0};
  /* Level A: long table descriptor, DT = $3, table at 0x2000, limit
   * suppressed ($7FFF with L/U clear). */
  put_long(&m, 0x1000u, (UINT64_C(0x7FFF) << 48) | (UINT64_C(3) << 32) |
                            UINT64_C(0x2000));
  /* Level B: long page descriptor, DT = $1, page at 0x50000. */
  put_long(&m, 0x2000u, (UINT64_C(1) << 32) | UINT64_C(0x50000));

  const ap_m68851_tc_t tc = two_level_tc();
  const ap_m68851_rp_t root = root_at(0x1000u, AP_M68851_DT_VALID_8_BYTE);
  const ap_m68851_search_config_t config = {
      .tc = &tc, .root = &root, .fetch = memory_fetch, .fetch_context = &m};

  const ap_m68851_search_result_t r = ap_m68851_search(&config, 0u, 5u);
  TEST_ASSERT_EQUAL_INT(AP_M68851_SEARCH_TYPE_NORMAL, r.type);
  TEST_ASSERT_EQUAL_HEX32(0x50000u, r.physical_address);
}

static void test_formats_may_be_mixed_between_levels(void) {
  /* Figure 5-11's example: a long-format descriptor at one level and short at
   * the next. The previous descriptor's `DT` decides the width, so a tree may
   * change format as it descends -- which is the reason `SIZE` is carried
   * rather than fixed once. */
  memory_t m = {0};
  /* Level A is long and names a *short*-format table (DT = $2). */
  put_long(&m, 0x1000u, (UINT64_C(0x7FFF) << 48) | (UINT64_C(2) << 32) |
                            UINT64_C(0x2000));
  put_short(&m, 0x2000u, 0x50000u | 0x1u);

  const ap_m68851_tc_t tc = two_level_tc();
  const ap_m68851_rp_t root = root_at(0x1000u, AP_M68851_DT_VALID_8_BYTE);
  const ap_m68851_search_config_t config = {
      .tc = &tc, .root = &root, .fetch = memory_fetch, .fetch_context = &m};

  const ap_m68851_search_result_t r = ap_m68851_search(&config, 0u, 5u);
  TEST_ASSERT_EQUAL_INT(AP_M68851_SEARCH_TYPE_NORMAL, r.type);
  TEST_ASSERT_EQUAL_HEX32(0x50000u, r.physical_address);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_root_pointer_selection_truth_table);
  RUN_TEST(test_a_dma_master_always_uses_the_dma_root_pointer);
  RUN_TEST(test_supervisor_uses_the_cpu_root_pointer_when_sre_is_clear);
  RUN_TEST(test_each_level_consumes_its_own_bits_of_the_address);
  RUN_TEST(test_the_initial_shift_discards_the_top_bits);
  RUN_TEST(test_a_two_level_search_reaches_the_page_frame);
  RUN_TEST(test_a_page_descriptor_with_levels_remaining_terminates_early);
  RUN_TEST(test_a_page_descriptor_at_the_last_level_is_normal);
  RUN_TEST(test_a_root_pointer_page_descriptor_maps_by_a_constant_offset);
  RUN_TEST(test_an_invalid_descriptor_ends_the_search);
  RUN_TEST(test_a_bus_error_during_the_search_ends_it);
  RUN_TEST(test_an_index_beyond_an_upper_limit_is_a_violation);
  RUN_TEST(test_a_short_previous_descriptor_suppresses_the_limit_check);
  RUN_TEST(test_function_code_lookup_suppresses_the_root_limit_check);
  RUN_TEST(test_a_function_code_lookup_indexes_by_the_function_code);
  RUN_TEST(test_a_valid_descriptor_past_the_last_level_is_an_indirection);
  RUN_TEST(test_an_indirection_naming_anything_but_a_page_is_invalid);
  RUN_TEST(test_write_protection_accumulates_down_the_tree);
  RUN_TEST(test_the_page_descriptors_attributes_reach_the_result);
  RUN_TEST(test_a_long_format_tree_walks_the_same_way);
  RUN_TEST(test_formats_may_be_mixed_between_levels);
  return UNITY_END();
}
