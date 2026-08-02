/* The MC68851 as a fitted part: translation through the ATC and the tables,
 * and the instructions that manage it.
 *
 * `[68851]` §5, §6.1 and Appendix A. Where the modules below it check one
 * table each, this checks that they compose -- a translation that misses,
 * walks, fills and then hits.
 */

#include <string.h>

#include "cpu/m68851/ap_m68851.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------------------
 * A toy physical memory holding a translation tree.
 * ------------------------------------------------------------------------- */

#define MEMORY_LONGS 4096u
#define MEMORY_BASE 0x1000u

typedef struct {
  uint32_t word[MEMORY_LONGS];
  unsigned fetches;
} memory_t;

static bool memory_fetch(void *context, uint32_t address, unsigned bytes,
                         uint64_t *value) {
  memory_t *m = (memory_t *)context;
  m->fetches++;
  const uint32_t index = (address - MEMORY_BASE) / 4u;
  if (index >= MEMORY_LONGS) {
    return false;
  }
  *value = (bytes == 4u) ? m->word[index]
                         : (((uint64_t)m->word[index] << 32) |
                            m->word[index + 1u]);
  return true;
}

static void put_short(memory_t *m, uint32_t address, uint32_t value) {
  m->word[(address - MEMORY_BASE) / 4u] = value;
}

/* Two levels of ten index bits over 4K pages: 0 + 10 + 10 + 12 = 32. */
static void configure(ap_m68851_t *mmu, memory_t *m) {
  ap_m68851_reset(mmu);
  mmu->tc = (ap_m68851_tc_t){.enable = true,
                             .page_size = 0xCu,
                             .initial_shift = 0,
                             .table_index = {10u, 10u, 0u, 0u}};
  mmu->crp = (ap_m68851_rp_t){.descriptor_type = AP_M68851_DT_VALID_4_BYTE,
                              .table_address = 0x1000u,
                              .lower_limit = false,
                              .limit = 0x7FFFu};
  memset(m, 0, sizeof *m);
  /* Level A entry 0 names a level B table at 0x2000; level B entry 0 names a
   * page frame at 0x50000. */
  put_short(m, 0x1000u, 0x2000u | 0x2u);
  put_short(m, 0x2000u, 0x50000u | 0x1u);
}

/* ---------------------------------------------------------------------------
 * Translation.
 * ------------------------------------------------------------------------- */

static void test_a_reset_part_translates_nothing(void) {
  /* §6.1.3.1: `E` "is cleared during reset", and with translation disabled
   * "logical addresses are routed directly from the logical address bus to the
   * physical address bus". A machine can therefore boot before its tables
   * exist -- which is the only reason the reset state is useful. */
  ap_m68851_t mmu = {0};
  memory_t m = {0};
  ap_m68851_reset(&mmu);

  const ap_m68851_translation_t t =
      ap_m68851_translate(&mmu, 0x12345u, 5u, false, memory_fetch, &m);
  TEST_ASSERT_EQUAL_INT(AP_M68851_TRANSLATE_OK, t.status);
  TEST_ASSERT_EQUAL_HEX32(0x12345u, t.physical_address);
  /* No table was walked and, crucially, no ATC entry was made: a disabled MMU
   * is not a transparent one that caches. */
  TEST_ASSERT_EQUAL_UINT(0u, m.fetches);
  TEST_ASSERT_FALSE(t.cache_hit);
}

static void test_a_miss_walks_the_tables_and_a_second_access_hits(void) {
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);

  const ap_m68851_translation_t first =
      ap_m68851_translate(&mmu, 0x00000123u, 5u, false, memory_fetch, &m);
  TEST_ASSERT_EQUAL_INT(AP_M68851_TRANSLATE_OK, first.status);
  TEST_ASSERT_EQUAL_HEX32(0x50123u, first.physical_address);
  TEST_ASSERT_FALSE(first.cache_hit);
  TEST_ASSERT_EQUAL_UINT(2u, m.fetches);

  /* The second access is answered by the cache: no further descriptor reads. */
  const unsigned after_walk = m.fetches;
  const ap_m68851_translation_t second =
      ap_m68851_translate(&mmu, 0x00000456u, 5u, false, memory_fetch, &m);
  TEST_ASSERT_TRUE(second.cache_hit);
  TEST_ASSERT_EQUAL_HEX32(0x50456u, second.physical_address);
  TEST_ASSERT_EQUAL_UINT(after_walk, m.fetches);
}

static void test_the_page_offset_survives_translation(void) {
  /* Only the frame is translated; the offset within the page passes through,
   * which is what makes this a page mapping. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);

  TEST_ASSERT_EQUAL_HEX32(
      0x50000u,
      ap_m68851_translate(&mmu, 0u, 5u, false, memory_fetch, &m)
          .physical_address);
  TEST_ASSERT_EQUAL_HEX32(
      0x50FFFu,
      ap_m68851_translate(&mmu, 0xFFFu, 5u, false, memory_fetch, &m)
          .physical_address);
}

static void test_a_denial_is_cached_so_it_is_not_walked_twice(void) {
  /* §5.2.1.2: "if access is to be denied, an ATC entry is made with the B bit
   * set". The second access to a restricted page costs no descriptor reads,
   * which is the whole reason the hardware caches failures. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  put_short(&m, 0x2000u, 0x0u); /* an invalid descriptor */

  const ap_m68851_translation_t first =
      ap_m68851_translate(&mmu, 0u, 5u, false, memory_fetch, &m);
  TEST_ASSERT_EQUAL_INT(AP_M68851_TRANSLATE_BUS_ERROR, first.status);
  const unsigned after_walk = m.fetches;

  const ap_m68851_translation_t second =
      ap_m68851_translate(&mmu, 0x100u, 5u, false, memory_fetch, &m);
  TEST_ASSERT_EQUAL_INT(AP_M68851_TRANSLATE_BUS_ERROR, second.status);
  TEST_ASSERT_TRUE(second.cache_hit);
  TEST_ASSERT_EQUAL_UINT(after_walk, m.fetches);
}

static void test_a_write_to_a_protected_page_is_refused_and_a_read_is_not(void) {
  /* The protection is a property of the access, not of the mapping, so one
   * cached entry serves both and answers them differently. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  put_short(&m, 0x2000u, 0x50000u | 0x4u | 0x1u); /* WP set */

  TEST_ASSERT_EQUAL_INT(
      AP_M68851_TRANSLATE_OK,
      ap_m68851_translate(&mmu, 0u, 5u, false, memory_fetch, &m).status);
  TEST_ASSERT_EQUAL_INT(
      AP_M68851_TRANSLATE_WRITE_PROTECTED,
      ap_m68851_translate(&mmu, 0u, 5u, true, memory_fetch, &m).status);
}

static void test_a_supervisor_access_uses_the_srp_only_when_sre_is_set(void) {
  /* The truth table, exercised through a real translation: with `SRE` clear a
   * supervisor access follows the CRP's tree, and with it set the SRP's. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  /* A second tree at 0x3000 reaching a different frame. */
  put_short(&m, 0x3000u, 0x4000u | 0x2u);
  put_short(&m, 0x4000u, 0x60000u | 0x1u);
  mmu.srp = (ap_m68851_rp_t){.descriptor_type = AP_M68851_DT_VALID_4_BYTE,
                             .table_address = 0x3000u,
                             .limit = 0x7FFFu};

  /* Function code 6 is supervisor program. */
  TEST_ASSERT_EQUAL_HEX32(
      0x50000u,
      ap_m68851_translate(&mmu, 0u, 6u, false, memory_fetch, &m)
          .physical_address);

  mmu.tc.supervisor_root_pointer_enable = true;
  ap_m68851_atc_flush(&mmu.atc);
  TEST_ASSERT_EQUAL_HEX32(
      0x60000u,
      ap_m68851_translate(&mmu, 0u, 6u, false, memory_fetch, &m)
          .physical_address);
}

/* ---------------------------------------------------------------------------
 * PMOVE and its side effects.
 * ------------------------------------------------------------------------- */

static void test_writing_tc_with_the_enable_clear_flushes_the_atc(void) {
  /* "Writing a value with its enable bit clear to this register cause a flush
   * of the entire ATC." Necessary, because `TC` decides the page size and so
   * what every existing entry covers. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  (void)ap_m68851_translate(&mmu, 0u, 5u, false, memory_fetch, &m);
  TEST_ASSERT_NOT_NULL(ap_m68851_atc_lookup(&mmu.atc, 0u, 5u, 4096u));

  TEST_ASSERT_EQUAL_INT(AP_M68851_EXECUTED,
                        ap_m68851_pmove_write(&mmu, AP_M68851_PREG_TC, 0u));
  TEST_ASSERT_NULL(ap_m68851_atc_lookup(&mmu.atc, 0u, 5u, 4096u));
}

static void test_an_inconsistent_tc_is_written_with_only_the_enable_cleared(void) {
  /* "If an exception is taken, the TC register is updated with the data except
   * that the E bit is cleared." A rejected write is not a write that did not
   * happen -- software can read back exactly what it tried, which is how it
   * diagnoses the geometry. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);

  /* Enabled, 4K pages, but the indices sum wrongly: 5 + 5 + 12 != 32. */
  ap_m68851_tc_t bad = {.enable = true,
                        .page_size = 0xCu,
                        .initial_shift = 0,
                        .table_index = {5u, 5u, 0u, 0u}};
  const uint32_t value = ap_m68851_tc_encode(&bad);

  TEST_ASSERT_EQUAL_INT(
      AP_M68851_CONFIGURATION_ERROR,
      ap_m68851_pmove_write(&mmu, AP_M68851_PREG_TC, value));
  TEST_ASSERT_FALSE(mmu.tc.enable);
  /* Everything else arrived. */
  TEST_ASSERT_EQUAL_UINT(0xCu, mmu.tc.page_size);
  TEST_ASSERT_EQUAL_UINT(5u, mmu.tc.table_index[0]);
  TEST_ASSERT_EQUAL_UINT(5u, mmu.tc.table_index[1]);
}

static void test_a_consistent_tc_is_accepted(void) {
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  ap_m68851_tc_t good = {.enable = true,
                         .page_size = 0xCu,
                         .table_index = {10u, 10u, 0u, 0u}};
  TEST_ASSERT_EQUAL_INT(
      AP_M68851_EXECUTED,
      ap_m68851_pmove_write(&mmu, AP_M68851_PREG_TC,
                            ap_m68851_tc_encode(&good)));
  TEST_ASSERT_TRUE(mmu.tc.enable);
}

static void test_writing_srp_invalidates_its_entries_even_shared_ones(void) {
  /* Appendix A: "causes all entries in the ATC that were formed with the SRP
   * (**even globally shared**) to be invalidated." This is the one place a
   * shared entry does not survive, so it cannot reuse the ordinary flush --
   * doing so would leave stale supervisor mappings behind. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  mmu.tc.supervisor_root_pointer_enable = true;
  mmu.srp = (ap_m68851_rp_t){.descriptor_type = AP_M68851_DT_VALID_4_BYTE,
                             .table_address = 0x1000u,
                             .limit = 0x7FFFu};

  /* A supervisor entry, marked shared, and a user entry that must survive. */
  mmu.atc.entry[0] = (ap_m68851_atc_entry_t){.valid = true,
                                             .function_code = 6u,
                                             .shared_globally = true,
                                             .logical_address = 0u};
  mmu.atc.entry[1] = (ap_m68851_atc_entry_t){.valid = true,
                                             .function_code = 1u,
                                             .logical_address = 0u};

  TEST_ASSERT_EQUAL_INT(
      AP_M68851_EXECUTED,
      ap_m68851_pmove_write(&mmu, AP_M68851_PREG_SRP, UINT64_C(0)));
  TEST_ASSERT_FALSE(mmu.atc.entry[0].valid);
  TEST_ASSERT_TRUE(mmu.atc.entry[1].valid);
}

static void test_writing_crp_reports_a_flush_in_pcsr(void) {
  /* §6.1.2.2: "when the MC68851 flushes entries from the ATC as the result of a
   * write to the CRP, bit [15] (F) of PCSR is set to indicate that entries with
   * the task alias shown in the TA field have been flushed." */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  (void)ap_m68851_translate(&mmu, 0u, 5u, false, memory_fetch, &m);

  TEST_ASSERT_EQUAL_INT(
      AP_M68851_EXECUTED,
      ap_m68851_pmove_write(&mmu, AP_M68851_PREG_CRP, UINT64_C(0)));
  TEST_ASSERT_TRUE(mmu.pcsr.flush);
  TEST_ASSERT_NULL(ap_m68851_atc_lookup(&mmu.atc, 0u, 5u, 4096u));
}

static void test_the_registers_round_trip_through_pmove(void) {
  /* Every register software can write and read back. `CAL`, `VAL` and `SCC`
   * are byte-wide and the root pointers 64-bit, so this also checks that the
   * widths are not truncated on the way through. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);

  (void)ap_m68851_pmove_write(&mmu, AP_M68851_PREG_CAL, 0xE0u);
  TEST_ASSERT_EQUAL_HEX64(0xE0u, ap_m68851_pmove_read(&mmu, AP_M68851_PREG_CAL));

  (void)ap_m68851_pmove_write(&mmu, AP_M68851_PREG_SCC, 0xA5u);
  TEST_ASSERT_EQUAL_HEX64(0xA5u, ap_m68851_pmove_read(&mmu, AP_M68851_PREG_SCC));

  (void)ap_m68851_pmove_write(&mmu, AP_M68851_PREG_AC, 0x00B3u);
  TEST_ASSERT_EQUAL_HEX64(0x00B3u,
                          ap_m68851_pmove_read(&mmu, AP_M68851_PREG_AC));

  const uint64_t drp = (UINT64_C(0x7FFF) << 48) | (UINT64_C(2) << 32) |
                       UINT64_C(0x12340);
  (void)ap_m68851_pmove_write(&mmu, AP_M68851_PREG_DRP, drp);
  TEST_ASSERT_EQUAL_HEX64(drp, ap_m68851_pmove_read(&mmu, AP_M68851_PREG_DRP));
}

static void test_the_cache_status_register_is_read_only(void) {
  /* §6.1.2 calls PCSR read-only. A write is not an error -- there is simply
   * nothing to write to -- so it must neither fault nor take effect. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  mmu.pcsr.task_alias = 3u;

  TEST_ASSERT_EQUAL_INT(
      AP_M68851_EXECUTED,
      ap_m68851_pmove_write(&mmu, AP_M68851_PREG_PCSR, 0xFFFFu));
  TEST_ASSERT_EQUAL_UINT(3u, mmu.pcsr.task_alias);
}

/* ---------------------------------------------------------------------------
 * PFLUSH.
 * ------------------------------------------------------------------------- */

static ap_m68851_instruction_t flush(unsigned mode, unsigned mask) {
  return ap_m68851_decode_command(
      (uint16_t)((1u << 13) | (mode << 10) | (mask << 5)));
}

static void test_flush_all_empties_the_cache(void) {
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  (void)ap_m68851_translate(&mmu, 0u, 5u, false, memory_fetch, &m);

  const ap_m68851_instruction_t all = flush(1u, 0u);
  TEST_ASSERT_EQUAL_INT(AP_M68851_EXECUTED,
                        ap_m68851_pflush(&mmu, &all, 0u, 0u));
  TEST_ASSERT_NULL(ap_m68851_atc_lookup(&mmu.atc, 0u, 5u, 4096u));
}

static void test_a_flush_by_function_code_spares_other_function_codes(void) {
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  mmu.atc.entry[0] = (ap_m68851_atc_entry_t){.valid = true, .function_code = 5u};
  mmu.atc.entry[1] = (ap_m68851_atc_entry_t){.valid = true, .function_code = 6u};

  const ap_m68851_instruction_t by_fc = flush(4u, 0xFu);
  TEST_ASSERT_EQUAL_INT(AP_M68851_EXECUTED,
                        ap_m68851_pflush(&mmu, &by_fc, 5u, 0u));
  TEST_ASSERT_FALSE(mmu.atc.entry[0].valid);
  TEST_ASSERT_TRUE(mmu.atc.entry[1].valid);
}

static void test_a_zero_mask_flushes_every_function_code(void) {
  /* "A zero indicates that the bit position is not significant", so a mask of
   * zero makes every entry match however the instruction's own function code
   * reads. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  for (unsigned i = 0; i < 4u; i++) {
    mmu.atc.entry[i] =
        (ap_m68851_atc_entry_t){.valid = true, .function_code = i + 1u};
  }

  const ap_m68851_instruction_t wide = flush(4u, 0x0u);
  TEST_ASSERT_EQUAL_INT(AP_M68851_EXECUTED,
                        ap_m68851_pflush(&mmu, &wide, 5u, 0u));
  for (unsigned i = 0; i < 4u; i++) {
    TEST_ASSERT_FALSE(mmu.atc.entry[i].valid);
  }
}

static void test_an_ordinary_flush_spares_shared_entries_and_pflushs_does_not(void) {
  /* "ATC entries whose SG bit is set will not be invalidated unless the PFLUSHS
   * is specified." */
  ap_m68851_t mmu;
  memory_t m;

  configure(&mmu, &m);
  mmu.atc.entry[0] = (ap_m68851_atc_entry_t){
      .valid = true, .function_code = 5u, .shared_globally = true};
  const ap_m68851_instruction_t plain = flush(4u, 0xFu);
  (void)ap_m68851_pflush(&mmu, &plain, 5u, 0u);
  TEST_ASSERT_TRUE(mmu.atc.entry[0].valid);

  const ap_m68851_instruction_t shared = flush(5u, 0xFu);
  (void)ap_m68851_pflush(&mmu, &shared, 5u, 0u);
  TEST_ASSERT_FALSE(mmu.atc.entry[0].valid);
}

static void test_a_flush_by_address_spares_other_pages(void) {
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  mmu.atc.entry[0] = (ap_m68851_atc_entry_t){
      .valid = true, .function_code = 5u, .logical_address = 0x10000u};
  mmu.atc.entry[1] = (ap_m68851_atc_entry_t){
      .valid = true, .function_code = 5u, .logical_address = 0x20000u};

  const ap_m68851_instruction_t by_ea = flush(6u, 0xFu);
  TEST_ASSERT_EQUAL_INT(AP_M68851_EXECUTED,
                        ap_m68851_pflush(&mmu, &by_ea, 5u, 0x10800u));
  /* Any address within the page names the page. */
  TEST_ASSERT_FALSE(mmu.atc.entry[0].valid);
  TEST_ASSERT_TRUE(mmu.atc.entry[1].valid);
}

static void test_a_malformed_flush_all_is_refused(void) {
  /* Mode `001` with a non-zero mask is an encoding the manual forbids. */
  const ap_m68851_instruction_t bad = flush(1u, 0x1u);
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  TEST_ASSERT_EQUAL_INT(AP_M68851_TAKE_LINE_F,
                        ap_m68851_pflush(&mmu, &bad, 0u, 0u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_reset_part_translates_nothing);
  RUN_TEST(test_a_miss_walks_the_tables_and_a_second_access_hits);
  RUN_TEST(test_the_page_offset_survives_translation);
  RUN_TEST(test_a_denial_is_cached_so_it_is_not_walked_twice);
  RUN_TEST(test_a_write_to_a_protected_page_is_refused_and_a_read_is_not);
  RUN_TEST(test_a_supervisor_access_uses_the_srp_only_when_sre_is_set);
  RUN_TEST(test_writing_tc_with_the_enable_clear_flushes_the_atc);
  RUN_TEST(test_an_inconsistent_tc_is_written_with_only_the_enable_cleared);
  RUN_TEST(test_a_consistent_tc_is_accepted);
  RUN_TEST(test_writing_srp_invalidates_its_entries_even_shared_ones);
  RUN_TEST(test_writing_crp_reports_a_flush_in_pcsr);
  RUN_TEST(test_the_registers_round_trip_through_pmove);
  RUN_TEST(test_the_cache_status_register_is_read_only);
  RUN_TEST(test_flush_all_empties_the_cache);
  RUN_TEST(test_a_flush_by_function_code_spares_other_function_codes);
  RUN_TEST(test_a_zero_mask_flushes_every_function_code);
  RUN_TEST(test_an_ordinary_flush_spares_shared_entries_and_pflushs_does_not);
  RUN_TEST(test_a_flush_by_address_spares_other_pages);
  RUN_TEST(test_a_malformed_flush_all_is_refused);
  return UNITY_END();
}
