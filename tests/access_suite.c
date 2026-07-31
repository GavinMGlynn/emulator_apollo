/* MC68030 logical memory access: the order a read actually takes.
 *
 * Cited to MC68030 User's Manual 3ed §6.1.
 *
 * The whole content of this module is the order, and the order is the reverse
 * of the intuitive one: the cache answers *before* the MMU is consulted, which
 * is only possible because the 68030's caches are logically addressed. These
 * tests assert that ordering directly -- that a cache hit costs no clocks *and*
 * does not consult the MMU -- because a model that translates first produces
 * the same values with the wrong timing and the wrong faults.
 */

#include "cpu/m68030/ap_m68030_access.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define FC_SUPERVISOR_DATA 5u
#define PAGE_FRAME 0x00A00000u
#define ADDRESS 0x00001018u

/* A memory system that answers every fill from a 32-bit STERM port able to
 * burst, and counts how often it was asked. */
typedef struct {
  unsigned fills;
  unsigned table_fetches;
} memory_t;

static void memory_fill(void *context, uint32_t line_address,
                        uint8_t function_code, ap_m68030_fill_answer_t *out) {
  (void)function_code;
  memory_t *memory = (memory_t *)context;
  memory->fills++;
  out->termination = AP_M68030_TERM_STERM;
  out->burst_acknowledge = true;
  const unsigned base = ap_m68030_cache_entry_index(line_address);
  for (unsigned e = 0; e < AP_M68030_BURST_BEATS; e++) {
    out->data[e] = 0xC0DE0000u + ((base + e) % AP_M68030_BURST_BEATS);
  }
}

/* A one-level tree: the root descriptor is an early-termination page. */
static bool table_fetch(void *context, uint32_t physical, bool long_format,
                        ap_m68030_descriptor_t *out) {
  (void)physical;
  (void)long_format;
  memory_t *memory = (memory_t *)context;
  memory->table_fetches++;
  *out = (ap_m68030_descriptor_t){.dt = AP_M68030_DT_PAGE,
                                  .address_field = PAGE_FRAME >> 8,
                                  .used = true};
  return true;
}

static bool table_update(void *context, uint32_t physical, bool set_used,
                         bool set_modified) {
  (void)context;
  (void)physical;
  (void)set_used;
  (void)set_modified;
  return true;
}

static ap_m68030_tc_t three_level_4k(void) {
  return ap_m68030_tc_decode(UINT32_C(0x80000000) | (12u << 20) | (0u << 16) |
                             (7u << 12) | (7u << 8) | (6u << 4) | 0u);
}

typedef struct {
  ap_m68030_cache_t cache;
  ap_m68030_atc_t atc;
  ap_m68030_tc_t tc;
  ap_m68030_root_t root;
  memory_t memory;
} machine_t;

static machine_t make_machine(void) {
  machine_t m = {0};
  ap_m68030_cache_clear(&m.cache);
  ap_m68030_atc_flush(&m.atc);
  m.tc = three_level_4k();
  m.root = (ap_m68030_root_t){.table_address = 0x00010000u};
  return m;
}

static ap_m68030_access_ctx_t context_of(machine_t *m) {
  return (ap_m68030_access_ctx_t){
      .cache = &m->cache,
      .atc = &m->atc,
      .tc = &m->tc,
      .root = &m->root,
      .cache_enabled = true,
      .burst_enabled = true,
      .translation_enabled = true,
      .table_fetch = table_fetch,
      .table_update = table_update,
      .fill = memory_fill,
      .context = &m->memory,
  };
}

/* The first access misses everything: the MMU is consulted, a table search
 * runs, and the bus is used. */
static void test_a_cold_access_consults_the_mmu_and_pays_for_it(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);

  const ap_m68030_access_result_t r =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_FALSE(r.cache_hit);
  TEST_ASSERT_TRUE(r.mmu_consulted);
  TEST_ASSERT_TRUE(r.descriptor_fetches > 0);
  TEST_ASSERT_TRUE(r.clocks > 0);
}

/* The point of the module. "the MMU is completely ignored" on a cache hit --
 * so the second access costs no clocks *and* does not consult the MMU, which
 * are two separate claims and both are asserted. */
static void test_a_cache_hit_costs_nothing_and_skips_the_mmu(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);

  const ap_m68030_access_result_t first =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  const unsigned fetches_after_first = m.memory.table_fetches;
  const unsigned fills_after_first = m.memory.fills;

  const ap_m68030_access_result_t second =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);

  TEST_ASSERT_TRUE(second.cache_hit);
  TEST_ASSERT_EQUAL_UINT32(0, second.clocks);
  TEST_ASSERT_FALSE(second.mmu_consulted);
  TEST_ASSERT_EQUAL_HEX32(first.value, second.value);

  /* Neither the tables nor the bus were touched again. */
  TEST_ASSERT_EQUAL_UINT(fetches_after_first, m.memory.table_fetches);
  TEST_ASSERT_EQUAL_UINT(fills_after_first, m.memory.fills);
}

/* The burst filled the whole line, so the three neighbouring long words are
 * hits too -- and they likewise skip the MMU. */
static void test_the_rest_of_the_filled_line_also_skips_the_mmu(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);

  (void)ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  const unsigned fetches = m.memory.table_fetches;

  for (unsigned e = 0; e < AP_M68030_BURST_BEATS; e++) {
    const ap_m68030_access_result_t r = ap_m68030_access_read(
        &ctx, 0x00001010u + (e * 4u), FC_SUPERVISOR_DATA);
    TEST_ASSERT_TRUE(r.cache_hit);
    TEST_ASSERT_FALSE(r.mmu_consulted);
    TEST_ASSERT_EQUAL_UINT32(0, r.clocks);
  }
  TEST_ASSERT_EQUAL_UINT(fetches, m.memory.table_fetches);
}

/* With the cache disabled every access consults the MMU -- which is the same
 * effect MD's IC command exposes on real hardware, now visible end to end. */
static void test_a_disabled_cache_consults_the_mmu_every_time(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);
  ctx.cache_enabled = false;

  for (unsigned i = 0; i < 3; i++) {
    const ap_m68030_access_result_t r =
        ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
    TEST_ASSERT_FALSE(r.cache_hit);
    TEST_ASSERT_TRUE(r.mmu_consulted);
    TEST_ASSERT_TRUE(r.clocks > 0);
  }
  TEST_ASSERT_EQUAL_UINT(3, m.memory.fills);
}

/* The CDIS signal overrides CACR, so asserting it has the same effect as
 * disabling the cache in software. */
static void test_the_cache_disable_signal_overrides_the_enable_bit(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);
  ctx.cache_disable = true;

  (void)ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  const ap_m68030_access_result_t second =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);

  TEST_ASSERT_FALSE(second.cache_hit);
  TEST_ASSERT_TRUE(second.mmu_consulted);
}

/* A transparently translated access skips the tables entirely: no descriptor
 * fetch runs, and the physical address is the logical one. */
static void test_a_transparent_access_skips_the_translation_tables(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);
  ctx.cache_enabled = false; /* force the MMU to be consulted every time */

  const ap_m68030_tt_t tt = {.logical_base = 0x00,
                             .logical_mask = 0xFF,
                             .fc_mask = 0x7,
                             .enabled = true,
                             .ignore_read_write = true};
  ctx.tt0 = &tt;

  const ap_m68030_access_result_t r =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);

  TEST_ASSERT_TRUE(r.transparent);
  TEST_ASSERT_EQUAL_UINT(0, m.memory.table_fetches);
  TEST_ASSERT_EQUAL_HEX32(ADDRESS, r.physical);
}

/* A second miss to a *different* page reuses the ATC entry rather than walking
 * again, provided it is the same page -- so the table search is paid once per
 * page, not once per line. */
static void test_the_table_search_is_paid_once_per_page(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);

  (void)ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  const unsigned after_first = m.memory.table_fetches;

  /* A different cache line, same 4K page: a cache miss but an ATC hit. */
  const ap_m68030_access_result_t r =
      ap_m68030_access_read(&ctx, ADDRESS + 0x40u, FC_SUPERVISOR_DATA);

  TEST_ASSERT_FALSE(r.cache_hit);
  TEST_ASSERT_TRUE(r.mmu_consulted);
  TEST_ASSERT_EQUAL_UINT(0, r.descriptor_fetches);
  TEST_ASSERT_EQUAL_UINT(after_first, m.memory.table_fetches);
}


/* ---------------------------------------------------------------------------
 * Writes. The asymmetry with reads is the point: a read can be answered from
 * the cache alone, a write never can, because the data cache is writethrough.
 * ------------------------------------------------------------------------- */

/* A write always consults the MMU, even to a page already cached and already
 * translated -- which is also what makes write protection work on a resident
 * page. */
static void test_a_write_always_consults_the_mmu(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);

  /* Warm everything: after this the line is cached and the page translated. */
  (void)ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  const ap_m68030_access_result_t read_again =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  TEST_ASSERT_TRUE(read_again.cache_hit);
  TEST_ASSERT_FALSE(read_again.mmu_consulted);

  /* The write to that same, fully warm address still consults the MMU. */
  const ap_m68030_access_result_t write = ap_m68030_access_write(
      &ctx, ADDRESS, FC_SUPERVISOR_DATA, 0x12345678u, true);
  TEST_ASSERT_TRUE(write.ok);
  TEST_ASSERT_TRUE(write.mmu_consulted);
}

/* [030] 9.4's consequence end to end: an ATC entry created by a read has M
 * clear, so the first write to that page costs a full table search even though
 * the translation was already cached. */
static void test_a_write_to_a_read_only_warmed_page_costs_a_table_search(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);

  (void)ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  const unsigned after_read = m.memory.table_fetches;

  /* A second *read* pays nothing more -- the ATC entry serves it. */
  (void)ap_m68030_access_read(&ctx, ADDRESS + 0x40u, FC_SUPERVISOR_DATA);
  TEST_ASSERT_EQUAL_UINT(after_read, m.memory.table_fetches);

  /* The first *write* to the same page does pay, because M is clear. */
  const ap_m68030_access_result_t write = ap_m68030_access_write(
      &ctx, ADDRESS, FC_SUPERVISOR_DATA, 0x12345678u, true);
  TEST_ASSERT_TRUE(write.descriptor_fetches > 0);
  TEST_ASSERT_TRUE(m.memory.table_fetches > after_read);
}

/* And once M is set, subsequent writes to the page are ordinary ATC hits that
 * cost no further search -- so the price is paid once, not per write. */
static void test_the_modified_bit_is_paid_for_once(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);

  (void)ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  (void)ap_m68030_access_write(&ctx, ADDRESS, FC_SUPERVISOR_DATA, 1, true);
  const unsigned after_first_write = m.memory.table_fetches;

  const ap_m68030_access_result_t second = ap_m68030_access_write(
      &ctx, ADDRESS, FC_SUPERVISOR_DATA, 2, true);

  TEST_ASSERT_TRUE(second.ok);
  TEST_ASSERT_EQUAL_UINT(0, second.descriptor_fetches);
  TEST_ASSERT_EQUAL_UINT(after_first_write, m.memory.table_fetches);
}

/* A write hit updates the cache as well as memory, so a later read sees the
 * written value rather than the stale filled one. */
static void test_a_write_hit_updates_the_cached_value(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);

  const ap_m68030_access_result_t first =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  (void)ap_m68030_access_write(&ctx, ADDRESS, FC_SUPERVISOR_DATA, 0xFEEDFACEu,
                               true);

  const ap_m68030_access_result_t after =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  TEST_ASSERT_TRUE(after.cache_hit);
  TEST_ASSERT_EQUAL_HEX32(0xFEEDFACEu, after.value);
  TEST_ASSERT_NOT_EQUAL_UINT32(first.value, after.value);
}

/* A transparently translated write skips the tables, exactly as a read does. */
static void test_a_transparent_write_skips_the_tables(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);
  const ap_m68030_tt_t tt = {.logical_base = 0x00,
                             .logical_mask = 0xFF,
                             .fc_mask = 0x7,
                             .enabled = true,
                             .ignore_read_write = true};
  ctx.tt0 = &tt;

  const ap_m68030_access_result_t write = ap_m68030_access_write(
      &ctx, ADDRESS, FC_SUPERVISOR_DATA, 0x11223344u, true);

  TEST_ASSERT_TRUE(write.transparent);
  TEST_ASSERT_EQUAL_UINT(0, m.memory.table_fetches);
  TEST_ASSERT_EQUAL_UINT(0, write.descriptor_fetches);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_cold_access_consults_the_mmu_and_pays_for_it);
  RUN_TEST(test_a_cache_hit_costs_nothing_and_skips_the_mmu);
  RUN_TEST(test_the_rest_of_the_filled_line_also_skips_the_mmu);
  RUN_TEST(test_a_disabled_cache_consults_the_mmu_every_time);
  RUN_TEST(test_the_cache_disable_signal_overrides_the_enable_bit);
  RUN_TEST(test_a_transparent_access_skips_the_translation_tables);
  RUN_TEST(test_the_table_search_is_paid_once_per_page);
  RUN_TEST(test_a_write_always_consults_the_mmu);
  RUN_TEST(test_a_write_to_a_read_only_warmed_page_costs_a_table_search);
  RUN_TEST(test_the_modified_bit_is_paid_for_once);
  RUN_TEST(test_a_write_hit_updates_the_cached_value);
  RUN_TEST(test_a_transparent_write_skips_the_tables);
  return UNITY_END();
}
