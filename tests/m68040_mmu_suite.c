/* The MC68040's MMU joined: transparent translation, the ATC and the table
 * search in `[040]` §3.5's order.
 *
 * Every part of this was built and tested separately before the join existed --
 * `m68040_regs_suite`, `m68040_atc_suite`, `m68040_search_suite` -- so what
 * this suite is for is the *order* and the *decisions between* them, which is
 * exactly what nothing exercised.
 */
#include <string.h>

#include "unity.h"

#include "cpu/m68040/ap_m68040_mmu.h"

void setUp(void) {}
void tearDown(void) {}

#define MEMORY_LONGS 8192u
#define MEMORY_BASE 0x1000u

typedef struct {
  uint32_t word[MEMORY_LONGS];
  unsigned fetches;
  bool fail;
} memory_t;

static bool memory_fetch(void *context, uint32_t address, uint32_t *value) {
  memory_t *m = (memory_t *)context;
  m->fetches++;
  if (m->fail) {
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

static uint32_t get(const memory_t *m, uint32_t address) {
  return m->word[(address - MEMORY_BASE) / 4u];
}

/* `U` is bit 3 and `M` bit 4 in both descriptor formats, Figures 3-11 and
 * 3-12. */
static bool memory_update(void *context, uint32_t address, bool set_used,
                          bool set_modified, bool locked) {
  memory_t *m = (memory_t *)context;
  (void)locked;
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

/* Root at 0x1000, pointer table at 0x2000, page table at 0x3000, page frame at
 * 0x50000 -- `m68040_search_suite`'s tree, so the two suites agree about what a
 * resident translation looks like. UDT 10 is resident, PDT 01 is. */
static void build(memory_t *m) {
  memset(m, 0, sizeof *m);
  put(m, 0x1000u, 0x2000u | 0x2u);
  put(m, 0x2000u, 0x3000u | 0x2u);
  put(m, 0x3000u, 0x50000u | 0x1u);
}

static uint32_t g_tc;
static uint32_t g_ttr[2];
static uint32_t g_urp;
static uint32_t g_srp;
static ap_m68040_atc_t g_atc;

static ap_m68040_mmu_t mmu(void) {
  return (ap_m68040_mmu_t){
      .tc = &g_tc, .ttr = g_ttr, .urp = &g_urp, .srp = &g_srp, .atc = &g_atc};
}

static void reset_registers(void) {
  g_tc = 0u;
  g_ttr[0] = 0u;
  g_ttr[1] = 0u;
  g_urp = 0x1000u;
  g_srp = 0x1000u;
  ap_m68040_atc_init(&g_atc);
}

/* `[040]` §3.5: with the E-bit clear and no transparent block matching, "the
 * logical address is used as the physical address". Not a fault and not a
 * translation -- the case a machine spends its whole reset in. */
static void test_translation_disabled_passes_the_address_through(void) {
  memory_t m;
  build(&m);
  reset_registers();
  const ap_m68040_mmu_t v = mmu();

  const ap_m68040_mmu_result_t r =
      ap_m68040_mmu_translate(&v, 0x12345678u, 5u, false, memory_fetch, memory_update, &m);
  TEST_ASSERT_EQUAL_INT(AP_M68040_MMU_UNTRANSLATED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x12345678u, r.physical);
  TEST_ASSERT_EQUAL_UINT(0u, m.fetches);
}

/* §3.1.3: the transparent translation registers "operate independently of the
 * E-bit in the TCR". So a TTR answers on a machine whose paged translation is
 * off -- and this is the assertion that makes the join's *order* right, because
 * a version that checked the E-bit first would never reach them.
 *
 * The register is the one Domain/OS's DS5500 loader actually writes:
 * `0100C040` -- base `01`, mask `00`, enabled, both privilege modes. */
static void test_a_transparent_block_answers_with_translation_disabled(void) {
  memory_t m;
  build(&m);
  reset_registers();
  g_ttr[0] = 0x0100C040u;
  const ap_m68040_mmu_t v = mmu();

  const ap_m68040_mmu_result_t r =
      ap_m68040_mmu_translate(&v, 0x01234567u, 5u, false, memory_fetch, memory_update, &m);
  TEST_ASSERT_EQUAL_INT(AP_M68040_MMU_TRANSPARENT, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x01234567u, r.physical);
  TEST_ASSERT_EQUAL_UINT(0u, m.fetches);

  /* And an address outside the block is not transparent: base `01` with mask
   * `00` covers `01xxxxxx` and nothing else. */
  const ap_m68040_mmu_result_t outside =
      ap_m68040_mmu_translate(&v, 0x02234567u, 5u, false, memory_fetch, memory_update, &m);
  TEST_ASSERT_EQUAL_INT(AP_M68040_MMU_UNTRANSLATED, outside.status);
}

/* A write into a write-protected transparent block faults, which is the only
 * attribute a transparent block carries that can. */
static void test_a_write_protected_transparent_block_faults_a_write(void) {
  memory_t m;
  build(&m);
  reset_registers();
  g_ttr[0] = 0x0100C040u | 0x4u; /* W, bit 2 */
  const ap_m68040_mmu_t v = mmu();

  TEST_ASSERT_EQUAL_INT(
      AP_M68040_MMU_TRANSPARENT,
      ap_m68040_mmu_translate(&v, 0x01234567u, 5u, false, memory_fetch, memory_update, &m)
          .status);
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_MMU_FAULT,
      ap_m68040_mmu_translate(&v, 0x01234567u, 5u, true, memory_fetch, memory_update, &m)
          .status);
}

/* The E-bit set, no TTR matching: a table search, and the frame plus the page
 * offset. `0x8000` is Figure 3-4's E with P clear -- 4-Kbyte pages -- which is
 * the exact value the DS5500 loader writes. */
static void test_an_enabled_mmu_searches_and_returns_frame_plus_offset(void) {
  memory_t m;
  build(&m);
  reset_registers();
  g_tc = 0x8000u;
  const ap_m68040_mmu_t v = mmu();

  const ap_m68040_mmu_result_t r =
      ap_m68040_mmu_translate(&v, 0x00000123u, 5u, false, memory_fetch, memory_update, &m);
  TEST_ASSERT_EQUAL_INT(AP_M68040_MMU_TRANSLATED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x50123u, r.physical);
  TEST_ASSERT_TRUE(r.fetches > 0u);
  TEST_ASSERT_TRUE(r.filled);
}

/* The second access to the same page is an ATC hit: same answer, no descriptor
 * read. That is the whole reason the ATC is in the path, and a join that filled
 * the ATC but never looked in it would pass every test above. */
static void test_a_second_access_hits_the_atc_and_reads_no_descriptor(void) {
  memory_t m;
  build(&m);
  reset_registers();
  g_tc = 0x8000u;
  const ap_m68040_mmu_t v = mmu();

  (void)ap_m68040_mmu_translate(&v, 0x00000123u, 5u, false, memory_fetch, memory_update, &m);
  const unsigned after_first = m.fetches;
  TEST_ASSERT_TRUE(after_first > 0u);

  const ap_m68040_mmu_result_t again =
      ap_m68040_mmu_translate(&v, 0x00000456u, 5u, false, memory_fetch, memory_update, &m);
  TEST_ASSERT_EQUAL_INT(AP_M68040_MMU_TRANSLATED, again.status);
  TEST_ASSERT_EQUAL_HEX32(0x50456u, again.physical);
  TEST_ASSERT_EQUAL_UINT(after_first, m.fetches);
  TEST_ASSERT_FALSE(again.filled);
}

/* An invalid descriptor faults **and is cached**: "when an invalid descriptor
 * is encountered, an ATC entry is created for the logical address with the
 * resident bit in the MMUSR clear". So the second attempt faults without
 * searching again -- which is what caching a failure is for, and what a join
 * that cached only successes would get wrong. */
static void test_a_nonresident_page_faults_and_the_failure_is_cached(void) {
  memory_t m;
  build(&m);
  put(&m, 0x3000u, 0x50000u); /* PDT 00: invalid */
  reset_registers();
  g_tc = 0x8000u;
  const ap_m68040_mmu_t v = mmu();

  const ap_m68040_mmu_result_t first =
      ap_m68040_mmu_translate(&v, 0x00000123u, 5u, false, memory_fetch, memory_update, &m);
  TEST_ASSERT_EQUAL_INT(AP_M68040_MMU_FAULT, first.status);
  TEST_ASSERT_TRUE(first.filled);
  const unsigned after_first = m.fetches;

  const ap_m68040_mmu_result_t second =
      ap_m68040_mmu_translate(&v, 0x00000123u, 5u, false, memory_fetch, memory_update, &m);
  TEST_ASSERT_EQUAL_INT(AP_M68040_MMU_FAULT, second.status);
  TEST_ASSERT_EQUAL_UINT(after_first, m.fetches);
}

/* A bus error during a descriptor fetch is **not** cached -- there is nothing
 * to cache, since the search learned nothing about the page. */
static void test_a_bus_error_during_a_search_is_not_cached(void) {
  memory_t m;
  build(&m);
  reset_registers();
  g_tc = 0x8000u;
  m.fail = true;
  const ap_m68040_mmu_t v = mmu();

  const ap_m68040_mmu_result_t r =
      ap_m68040_mmu_translate(&v, 0x00000123u, 5u, false, memory_fetch, memory_update, &m);
  TEST_ASSERT_EQUAL_INT(AP_M68040_MMU_FAULT, r.status);
  TEST_ASSERT_FALSE(r.filled);
}

/* §3.2: "the supervisor root pointer is used for supervisor accesses and the
 * user root pointer for user accesses". Two roots pointing at different trees
 * must give different answers for the same address -- the check that the
 * function code reaches the root selection at all. */
static void test_the_root_pointer_follows_the_function_code(void) {
  memory_t m;
  build(&m);
  /* A second tree at 0x4000 whose page frame is 0x60000. */
  put(&m, 0x4000u, 0x5000u | 0x2u);
  put(&m, 0x5000u, 0x6000u | 0x2u);
  put(&m, 0x6000u, 0x60000u | 0x1u);
  reset_registers();
  g_tc = 0x8000u;
  g_urp = 0x4000u;
  const ap_m68040_mmu_t v = mmu();

  /* FC2 set is supervisor, and takes `SRP`. */
  TEST_ASSERT_EQUAL_HEX32(
      0x50123u,
      ap_m68040_mmu_translate(&v, 0x00000123u, 5u, false, memory_fetch, memory_update, &m)
          .physical);
  /* FC2 clear is user, and takes `URP`. The ATC tags carry FC2, so this is a
   * miss rather than the entry above. */
  TEST_ASSERT_EQUAL_HEX32(
      0x60123u,
      ap_m68040_mmu_translate(&v, 0x00000123u, 1u, false, memory_fetch, memory_update, &m)
          .physical);
}

/* "Setting the W-bit in a table descriptor write protects all pages accessed
 * with that descriptor", so protection accumulates down the path rather than
 * living on the leaf -- and a read of the same page still succeeds. */
static void test_write_protection_from_a_table_descriptor_faults_a_write(void) {
  memory_t m;
  build(&m);
  put(&m, 0x2000u, 0x3000u | 0x2u | 0x4u); /* W on the pointer descriptor */
  reset_registers();
  g_tc = 0x8000u;
  const ap_m68040_mmu_t v = mmu();

  TEST_ASSERT_EQUAL_INT(
      AP_M68040_MMU_TRANSLATED,
      ap_m68040_mmu_translate(&v, 0x00000123u, 5u, false, memory_fetch, memory_update, &m)
          .status);
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_MMU_FAULT,
      ap_m68040_mmu_translate(&v, 0x00000123u, 5u, true, memory_fetch, memory_update, &m)
          .status);
}


/* ---------------------------------------------------------------------------
 * The history bits reach the tables through the join, `[040]` §3.2.5.
 * ------------------------------------------------------------------------- */

static void test_a_translated_write_sets_u_and_m_in_the_tables(void) {
  memory_t m;
  build(&m);
  reset_registers();
  g_tc = 0x8000u;
  const ap_m68040_mmu_t v = mmu();

  const ap_m68040_mmu_result_t r = ap_m68040_mmu_translate(
      &v, 0x00000123u, 5u, true, memory_fetch, memory_update, &m);

  TEST_ASSERT_EQUAL_INT(AP_M68040_MMU_TRANSLATED, r.status);
  TEST_ASSERT_TRUE((get(&m, 0x1000u) & (UINT32_C(1) << 3)) != 0u);
  TEST_ASSERT_TRUE((get(&m, 0x2000u) & (UINT32_C(1) << 3)) != 0u);
  TEST_ASSERT_TRUE((get(&m, 0x3000u) & (UINT32_C(1) << 3)) != 0u);
  TEST_ASSERT_TRUE((get(&m, 0x3000u) & (UINT32_C(1) << 4)) != 0u);
}

static void test_a_translated_read_leaves_m_clear_in_the_tables(void) {
  memory_t m;
  build(&m);
  reset_registers();
  g_tc = 0x8000u;
  const ap_m68040_mmu_t v = mmu();

  const ap_m68040_mmu_result_t r = ap_m68040_mmu_translate(
      &v, 0x00000123u, 5u, false, memory_fetch, memory_update, &m);

  TEST_ASSERT_EQUAL_INT(AP_M68040_MMU_TRANSLATED, r.status);
  TEST_ASSERT_TRUE((get(&m, 0x3000u) & (UINT32_C(1) << 3)) != 0u);
  TEST_ASSERT_TRUE((get(&m, 0x3000u) & (UINT32_C(1) << 4)) == 0u);
}

static void test_the_atc_entry_caches_the_descriptors_m_and_not_the_access(void) {
  /* The entry's `M` used to be `search.modified || write`, which set it on a
   * write to a page Table 3-1 forbids setting `M` on. Write-protected, so the
   * translation faults *and* the bit must stay clear on both sides. */
  memory_t m;
  build(&m);
  put(&m, 0x3000u, 0x50000u | 0x1u | (UINT32_C(1) << 2)); /* W set */
  reset_registers();
  g_tc = 0x8000u;
  const ap_m68040_mmu_t v = mmu();

  const ap_m68040_mmu_result_t r = ap_m68040_mmu_translate(
      &v, 0x00000123u, 5u, true, memory_fetch, memory_update, &m);

  TEST_ASSERT_EQUAL_INT(AP_M68040_MMU_FAULT, r.status);
  TEST_ASSERT_EQUAL_INT(AP_M68040_MMU_FAULT_PROTECTION, r.reason);
  TEST_ASSERT_TRUE((get(&m, 0x3000u) & (UINT32_C(1) << 4)) == 0u);
  const unsigned set = ap_m68040_atc_set(0x00000123u, AP_M68040_PAGE_4K);
  bool found = false;
  for (unsigned way = 0; way < AP_M68040_ATC_WAYS; way++) {
    if (g_atc.entry[set][way].valid) {
      found = true;
      TEST_ASSERT_FALSE(g_atc.entry[set][way].modified);
    }
  }
  TEST_ASSERT_TRUE(found);
}

static void test_a_null_update_translates_without_touching_the_tables(void) {
  /* What `ap_machine_translate`'s observer passes, and the reason the argument
   * exists at all: `--dump-logical` must not set a bit the machine did not. */
  memory_t m;
  build(&m);
  reset_registers();
  g_tc = 0x8000u;
  const ap_m68040_mmu_t v = mmu();

  const ap_m68040_mmu_result_t r = ap_m68040_mmu_translate(
      &v, 0x00000123u, 5u, true, memory_fetch, NULL, &m);

  TEST_ASSERT_EQUAL_INT(AP_M68040_MMU_TRANSLATED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x2000u | 0x2u, get(&m, 0x1000u));
  TEST_ASSERT_EQUAL_HEX32(0x50000u | 0x1u, get(&m, 0x3000u));
}

static void test_a_transparent_translation_writes_no_descriptor(void) {
  /* A TTR match ends the access before any table is read, so there is nothing
   * encountered to set `U` on. */
  memory_t m;
  build(&m);
  reset_registers();
  g_tc = 0x8000u;
  g_ttr[0] = 0x0000C000u | 0x8000u; /* enabled, both privilege modes */
  const ap_m68040_mmu_t v = mmu();

  const ap_m68040_mmu_result_t r = ap_m68040_mmu_translate(
      &v, 0x00000123u, 5u, true, memory_fetch, memory_update, &m);

  TEST_ASSERT_EQUAL_INT(AP_M68040_MMU_TRANSPARENT, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x2000u | 0x2u, get(&m, 0x1000u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_translation_disabled_passes_the_address_through);
  RUN_TEST(test_a_transparent_block_answers_with_translation_disabled);
  RUN_TEST(test_a_write_protected_transparent_block_faults_a_write);
  RUN_TEST(test_an_enabled_mmu_searches_and_returns_frame_plus_offset);
  RUN_TEST(test_a_second_access_hits_the_atc_and_reads_no_descriptor);
  RUN_TEST(test_a_nonresident_page_faults_and_the_failure_is_cached);
  RUN_TEST(test_a_bus_error_during_a_search_is_not_cached);
  RUN_TEST(test_the_root_pointer_follows_the_function_code);
  RUN_TEST(test_write_protection_from_a_table_descriptor_faults_a_write);
  RUN_TEST(test_a_translated_write_sets_u_and_m_in_the_tables);
  RUN_TEST(test_a_translated_read_leaves_m_clear_in_the_tables);
  RUN_TEST(test_the_atc_entry_caches_the_descriptors_m_and_not_the_access);
  RUN_TEST(test_a_null_update_translates_without_touching_the_tables);
  RUN_TEST(test_a_transparent_translation_writes_no_descriptor);
  return UNITY_END();
}
