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
  /* The status write-back's own record, so a test can ask what reached memory
   * rather than only what the API offered to write. */
  unsigned stores;
  unsigned read_modify_writes;
} memory_t;

/* A byte store into the same toy memory. Big-endian, matching `memory_fetch`:
 * byte 3 of a long word is its least significant. */
static void memory_store(void *context, uint32_t address, uint8_t value,
                         bool read_modify_write) {
  memory_t *m = (memory_t *)context;
  m->stores++;
  if (read_modify_write) {
    m->read_modify_writes++;
  }
  const uint32_t index = (address - MEMORY_BASE) / 4u;
  if (index >= MEMORY_LONGS) {
    return;
  }
  const unsigned byte = address & 3u;
  const unsigned shift = (3u - byte) * 8u;
  m->word[index] =
      (m->word[index] & ~(0xFFu << shift)) | ((uint32_t)value << shift);
}

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
      ap_m68851_translate(&mmu, 0x12345u, &(ap_m68851_access_t){.function_code = 5u,
                          .is_write = false}, memory_fetch, &m, NULL, NULL);
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
      ap_m68851_translate(&mmu, 0x00000123u, &(ap_m68851_access_t){.function_code = 5u,
                          .is_write = false}, memory_fetch, &m, NULL, NULL);
  TEST_ASSERT_EQUAL_INT(AP_M68851_TRANSLATE_OK, first.status);
  TEST_ASSERT_EQUAL_HEX32(0x50123u, first.physical_address);
  TEST_ASSERT_FALSE(first.cache_hit);
  TEST_ASSERT_EQUAL_UINT(2u, m.fetches);

  /* The second access is answered by the cache: no further descriptor reads. */
  const unsigned after_walk = m.fetches;
  const ap_m68851_translation_t second =
      ap_m68851_translate(&mmu, 0x00000456u, &(ap_m68851_access_t){.function_code = 5u,
                          .is_write = false}, memory_fetch, &m, NULL, NULL);
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
      ap_m68851_translate(&mmu, 0u, &(ap_m68851_access_t){.function_code = 5u,
                          .is_write = false}, memory_fetch, &m, NULL, NULL)
          .physical_address);
  TEST_ASSERT_EQUAL_HEX32(
      0x50FFFu,
      ap_m68851_translate(&mmu, 0xFFFu, &(ap_m68851_access_t){.function_code = 5u,
                          .is_write = false}, memory_fetch, &m, NULL, NULL)
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
      ap_m68851_translate(&mmu, 0u, &(ap_m68851_access_t){.function_code = 5u,
                          .is_write = false}, memory_fetch, &m, NULL, NULL);
  TEST_ASSERT_EQUAL_INT(AP_M68851_TRANSLATE_BUS_ERROR, first.status);
  const unsigned after_walk = m.fetches;

  const ap_m68851_translation_t second =
      ap_m68851_translate(&mmu, 0x100u, &(ap_m68851_access_t){.function_code = 5u,
                          .is_write = false}, memory_fetch, &m, NULL, NULL);
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
      ap_m68851_translate(&mmu, 0u, &(ap_m68851_access_t){.function_code = 5u,
                          .is_write = false}, memory_fetch, &m, NULL, NULL).status);
  TEST_ASSERT_EQUAL_INT(
      AP_M68851_TRANSLATE_WRITE_PROTECTED,
      ap_m68851_translate(&mmu, 0u, &(ap_m68851_access_t){.function_code = 5u,
                          .is_write = true}, memory_fetch, &m, NULL, NULL).status);
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
      ap_m68851_translate(&mmu, 0u, &(ap_m68851_access_t){.function_code = 6u,
                          .is_write = false}, memory_fetch, &m, NULL, NULL)
          .physical_address);

  mmu.tc.supervisor_root_pointer_enable = true;
  ap_m68851_atc_flush(&mmu.atc);
  TEST_ASSERT_EQUAL_HEX32(
      0x60000u,
      ap_m68851_translate(&mmu, 0u, &(ap_m68851_access_t){.function_code = 6u,
                          .is_write = false}, memory_fetch, &m, NULL, NULL)
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
  (void)ap_m68851_translate(&mmu, 0u, &(ap_m68851_access_t){.function_code = 5u,
                          .is_write = false}, memory_fetch, &m, NULL, NULL);
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
  (void)ap_m68851_translate(&mmu, 0u, &(ap_m68851_access_t){.function_code = 5u,
                          .is_write = false}, memory_fetch, &m, NULL, NULL);

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
  (void)ap_m68851_translate(&mmu, 0u, &(ap_m68851_access_t){.function_code = 5u,
                          .is_write = false}, memory_fetch, &m, NULL, NULL);

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


/* ---------------------------------------------------------------------------
 * PLOAD, PTEST and PVALID.
 * ------------------------------------------------------------------------- */

static ap_m68851_instruction_t pload(bool read) {
  return ap_m68851_decode_command(
      (uint16_t)((1u << 13) | (0u << 10) | ((read ? 1u : 0u) << 9)));
}

static ap_m68851_instruction_t ptest(unsigned level, bool read) {
  return ap_m68851_decode_command(
      (uint16_t)((4u << 13) | (level << 10) | ((read ? 1u : 0u) << 9)));
}

static void test_pload_installs_an_entry_nothing_referenced(void) {
  /* A `PLOAD` warms the cache for an address the program has not touched, which
   * is the point: an operating system can install a mapping before the fault
   * that would otherwise create it. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  TEST_ASSERT_NULL(ap_m68851_atc_lookup(&mmu.atc, 0u, 5u, 4096u));

  const ap_m68851_instruction_t r = pload(true);
  TEST_ASSERT_EQUAL_INT(AP_M68851_EXECUTED,
                        ap_m68851_pload(&mmu, &r, 5u, 0u, memory_fetch, &m, NULL, NULL));
  TEST_ASSERT_NOT_NULL(ap_m68851_atc_lookup(&mmu.atc, 0u, 5u, 4096u));

  /* And the translation that follows is a hit. */
  const unsigned after = m.fetches;
  TEST_ASSERT_TRUE(
      ap_m68851_translate(&mmu, 0u, &(ap_m68851_access_t){.function_code = 5u,
                          .is_write = false}, memory_fetch, &m, NULL, NULL).cache_hit);
  TEST_ASSERT_EQUAL_UINT(after, m.fetches);
}

static void test_ploadw_marks_the_entry_modified_and_ploadr_does_not(void) {
  /* "PLOADR causes U bits ... to be updated as if a read access had taken
   * place. PLOADW causes U and M bits ... as if a write access had taken
   * place." So the direction bit is not a hint -- it decides what a later
   * write through this entry finds. */
  ap_m68851_t mmu;
  memory_t m;

  configure(&mmu, &m);
  const ap_m68851_instruction_t r = pload(true);
  TEST_ASSERT_EQUAL_INT(AP_M68851_EXECUTED,
                        ap_m68851_pload(&mmu, &r, 5u, 0u, memory_fetch, &m, NULL, NULL));
  TEST_ASSERT_FALSE(ap_m68851_atc_lookup(&mmu.atc, 0u, 5u, 4096u)->modified);

  configure(&mmu, &m);
  const ap_m68851_instruction_t w = pload(false);
  TEST_ASSERT_EQUAL_INT(AP_M68851_EXECUTED,
                        ap_m68851_pload(&mmu, &w, 5u, 0u, memory_fetch, &m, NULL, NULL));
  TEST_ASSERT_TRUE(ap_m68851_atc_lookup(&mmu.atc, 0u, 5u, 4096u)->modified);
}

static void test_pload_is_refused_while_translation_is_disabled(void) {
  /* §6.1.3.1: with `E` clear the part "terminates all PTEST, PLOAD, and
   * CALLM/RTM (type $1) instructions with an exception". */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  mmu.tc.enable = false;

  const ap_m68851_instruction_t r = pload(true);
  TEST_ASSERT_EQUAL_INT(AP_M68851_CONFIGURATION_ERROR,
                        ap_m68851_pload(&mmu, &r, 5u, 0u, memory_fetch, &m, NULL, NULL));
}

static void test_ptest_reports_a_good_translation_in_the_psr(void) {
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);

  const ap_m68851_instruction_t t = ptest(7u, true);
  TEST_ASSERT_EQUAL_INT(AP_M68851_EXECUTED,
                        ap_m68851_ptest(&mmu, &t, 5u, 0u, memory_fetch, &m));
  TEST_ASSERT_FALSE(mmu.psr.invalid);
  TEST_ASSERT_FALSE(mmu.psr.bus_error);
  TEST_ASSERT_FALSE(mmu.psr.limit_violation);
  /* "Set to the number of tables used in the translation of an address." */
  TEST_ASSERT_EQUAL_UINT(2u, mmu.psr.levels);
}

static void test_ptest_reports_an_invalid_descriptor(void) {
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  put_short(&m, 0x2000u, 0x0u);

  const ap_m68851_instruction_t t = ptest(7u, true);
  (void)ap_m68851_ptest(&mmu, &t, 5u, 0u, memory_fetch, &m);
  TEST_ASSERT_TRUE(mmu.psr.invalid);
}

static void test_ptest_reports_a_limit_violation_apart_from_invalidity(void) {
  /* `L` and `I` are different bits precisely so the operating system can tell
   * an addressing error by a task -- which may be a request for stack
   * extension -- from a page that is simply not there. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  mmu.crp.limit = 0u;
  mmu.crp.lower_limit = false;

  const ap_m68851_instruction_t t = ptest(7u, true);
  (void)ap_m68851_ptest(&mmu, &t, 5u, 0x00800000u, memory_fetch, &m);
  TEST_ASSERT_TRUE(mmu.psr.limit_violation);
  TEST_ASSERT_TRUE(mmu.psr.invalid);
}

static void test_a_level_ceiling_stops_the_search_without_reporting_a_fault(void) {
  /* "Continues searching the translation tables until the requested level is
   * reached." Stopping because the instruction asked has disproved nothing, so
   * `I` must stay clear -- otherwise every shallow `PTEST` would look like a
   * missing translation. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);

  const ap_m68851_instruction_t one = ptest(1u, true);
  TEST_ASSERT_EQUAL_INT(AP_M68851_EXECUTED,
                        ap_m68851_ptest(&mmu, &one, 5u, 0u, memory_fetch, &m));
  TEST_ASSERT_FALSE(mmu.psr.invalid);
  TEST_ASSERT_EQUAL_UINT(1u, mmu.psr.levels);

  /* The same address to full depth reaches the page and reports two levels. */
  const ap_m68851_instruction_t deep = ptest(7u, true);
  (void)ap_m68851_ptest(&mmu, &deep, 5u, 0u, memory_fetch, &m);
  TEST_ASSERT_EQUAL_UINT(2u, mmu.psr.levels);
}

static void test_a_level_zero_ptest_searches_only_the_atc(void) {
  /* Level zero is a different operation rather than a shallow search: no
   * descriptor is fetched at all, and a miss reports `I`. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);

  const ap_m68851_instruction_t t = ptest(0u, true);
  const unsigned before = m.fetches;
  TEST_ASSERT_EQUAL_INT(AP_M68851_EXECUTED,
                        ap_m68851_ptest(&mmu, &t, 5u, 0u, memory_fetch, &m));
  TEST_ASSERT_EQUAL_UINT(before, m.fetches);
  TEST_ASSERT_TRUE(mmu.psr.invalid);
  /* "For the PTEST instruction with a level specification of zero, this field
   * is always zero." */
  TEST_ASSERT_EQUAL_UINT(0u, mmu.psr.levels);

  /* With the entry present it hits, and still walks nothing. */
  (void)ap_m68851_translate(&mmu, 0u, &(ap_m68851_access_t){.function_code = 5u,
                          .is_write = false}, memory_fetch, &m, NULL, NULL);
  const unsigned after = m.fetches;
  (void)ap_m68851_ptest(&mmu, &t, 5u, 0u, memory_fetch, &m);
  TEST_ASSERT_EQUAL_UINT(after, m.fetches);
  TEST_ASSERT_FALSE(mmu.psr.invalid);
  TEST_ASSERT_EQUAL_UINT(0u, mmu.psr.levels);
}

static void test_pvalid_always_violates_when_module_control_is_clear(void) {
  /* §6.1.7.1: "the PVALID instruction will always cause an exception when MC is
   * clear." With module operations disabled the levels mean nothing to
   * compare, so the instruction cannot succeed. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  mmu.ac.module_control = false;
  mmu.ac.access_level_control = AP_M68851_ALC_THREE_BITS;

  TEST_ASSERT_EQUAL_INT(AP_M68851_PVALID_ACCESS_VIOLATION,
                        ap_m68851_pvalid(&mmu, 0xFFFFFFFFu, false, 0u));
}

static void test_pvalid_refuses_a_pointer_more_privileged_than_the_caller(void) {
  /* "If the operand bits are arithmetically less than the VAL bits, this
   * instruction causes a trap with the access level violation exception."
   * Lower is more privileged, so this is the confused-deputy guard: a caller
   * may not hand on a pointer it could not itself have made. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  mmu.ac.module_control = true;
  mmu.ac.access_level_control = AP_M68851_ALC_THREE_BITS;
  mmu.val = ap_m68851_access_level_encode(4u);

  /* Operand at level 2: more privileged than the caller's 4. Refused. */
  TEST_ASSERT_EQUAL_INT(AP_M68851_PVALID_ACCESS_VIOLATION,
                        ap_m68851_pvalid(&mmu, 0x40000000u, false, 0u));
  /* Operand at level 4: equal. Allowed -- the comparison is strict. */
  TEST_ASSERT_EQUAL_INT(AP_M68851_PVALID_OK,
                        ap_m68851_pvalid(&mmu, 0x80000000u, false, 0u));
  /* Operand at level 6: less privileged. Allowed. */
  TEST_ASSERT_EQUAL_INT(AP_M68851_PVALID_OK,
                        ap_m68851_pvalid(&mmu, 0xC0000000u, false, 0u));
}

static void test_pvalid_can_test_against_a_surrogate_level(void) {
  /* The register form supplies the level from a main processor address register
   * instead of `VAL`, which is how a routine validates against something other
   * than its own caller. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  mmu.ac.module_control = true;
  mmu.ac.access_level_control = AP_M68851_ALC_THREE_BITS;
  mmu.val = ap_m68851_access_level_encode(0u); /* would allow everything */

  TEST_ASSERT_EQUAL_INT(AP_M68851_PVALID_OK,
                        ap_m68851_pvalid(&mmu, 0x40000000u, false, 0u));
  /* The surrogate is stricter and refuses the same operand. */
  TEST_ASSERT_EQUAL_INT(
      AP_M68851_PVALID_ACCESS_VIOLATION,
      ap_m68851_pvalid(&mmu, 0x40000000u, true,
                       ap_m68851_access_level_encode(4u)));
}

static void test_pvalid_permits_everything_when_access_levels_are_disabled(void) {
  /* `ALC = $0` is "access level checking is disabled", so no address is more
   * privileged than another and nothing can violate. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  mmu.ac.module_control = true;
  mmu.ac.access_level_control = AP_M68851_ALC_DISABLED;
  mmu.val = ap_m68851_access_level_encode(7u);

  TEST_ASSERT_EQUAL_INT(AP_M68851_PVALID_OK,
                        ap_m68851_pvalid(&mmu, 0x00000000u, false, 0u));
}


/* ---------------------------------------------------------------------------
 * Breakpoints, §6.1.9, §6.1.10 and §8.1.
 *
 * The other half of the mechanism whose CPU side landed in Phase 2: the
 * 68020's `BKPT` runs an acknowledge cycle and this part answers it.
 * ------------------------------------------------------------------------- */

static void test_a_disabled_breakpoint_bus_errors(void) {
  /* "The BPE bit is cleared at reset", and with it clear the acknowledge cycle
   * is terminated by bus error -- which is how an unconfigured `BKPT` becomes
   * an illegal instruction rather than doing nothing. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);

  uint16_t opcode = 0x1234u;
  TEST_ASSERT_EQUAL_INT(AP_M68851_BREAKPOINT_BUS_ERROR,
                        ap_m68851_breakpoint_acknowledge(&mmu, 3u, &opcode));
}

static void test_an_enabled_breakpoint_returns_its_replacement_opcode(void) {
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  TEST_ASSERT_EQUAL_INT(
      AP_M68851_EXECUTED,
      ap_m68851_pmove_write_numbered(&mmu, AP_M68851_PREG_BAD, 3u, 0x4E71u));
  TEST_ASSERT_EQUAL_INT(
      AP_M68851_EXECUTED,
      ap_m68851_pmove_write_numbered(&mmu, AP_M68851_PREG_BAC, 3u, 0x8005u));

  uint16_t opcode = 0;
  TEST_ASSERT_EQUAL_INT(AP_M68851_BREAKPOINT_REPLACED,
                        ap_m68851_breakpoint_acknowledge(&mmu, 3u, &opcode));
  TEST_ASSERT_EQUAL_HEX16(0x4E71u, opcode);
}

static void test_the_skip_count_counts_down_to_a_bus_error(void) {
  /* "The breakpoint skip count ... specifies the number of times that the
   * replacement opcode ... is returned ... before the MC68851 signals the
   * MC68020 to initiate exception processing." A count of three fires three
   * times and then traps -- so a breakpoint can be armed to skip the first N
   * passes through a loop. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  (void)ap_m68851_pmove_write_numbered(&mmu, AP_M68851_PREG_BAD, 0u, 0x4E71u);
  (void)ap_m68851_pmove_write_numbered(&mmu, AP_M68851_PREG_BAC, 0u, 0x8003u);

  uint16_t opcode = 0;
  for (unsigned i = 0; i < 3u; i++) {
    TEST_ASSERT_EQUAL_INT(AP_M68851_BREAKPOINT_REPLACED,
                          ap_m68851_breakpoint_acknowledge(&mmu, 0u, &opcode));
  }
  TEST_ASSERT_EQUAL_INT(AP_M68851_BREAKPOINT_BUS_ERROR,
                        ap_m68851_breakpoint_acknowledge(&mmu, 0u, &opcode));
  /* And it stays trapped rather than wrapping. */
  TEST_ASSERT_EQUAL_INT(AP_M68851_BREAKPOINT_BUS_ERROR,
                        ap_m68851_breakpoint_acknowledge(&mmu, 0u, &opcode));
}

static void test_a_disabled_and_an_exhausted_breakpoint_are_indistinguishable(void) {
  /* §8.1 names both routes to one outcome: the bus error is asserted "due to
   * either the corresponding enable bit being clear or the skip count having
   * been decremented to zero". The CPU cannot tell them apart, and neither
   * should the model. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  (void)ap_m68851_pmove_write_numbered(&mmu, AP_M68851_PREG_BAC, 0u, 0x8000u);
  (void)ap_m68851_pmove_write_numbered(&mmu, AP_M68851_PREG_BAC, 1u, 0x0005u);

  uint16_t opcode = 0;
  TEST_ASSERT_EQUAL_INT(AP_M68851_BREAKPOINT_BUS_ERROR,
                        ap_m68851_breakpoint_acknowledge(&mmu, 0u, &opcode));
  TEST_ASSERT_EQUAL_INT(AP_M68851_BREAKPOINT_BUS_ERROR,
                        ap_m68851_breakpoint_acknowledge(&mmu, 1u, &opcode));
}

static void test_the_eight_breakpoints_are_independent(void) {
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  for (unsigned i = 0; i < AP_M68851_BREAKPOINTS; i++) {
    (void)ap_m68851_pmove_write_numbered(&mmu, AP_M68851_PREG_BAD, i,
                                         0x1000u + i);
    (void)ap_m68851_pmove_write_numbered(&mmu, AP_M68851_PREG_BAC, i,
                                         0x8001u);
  }
  for (unsigned i = 0; i < AP_M68851_BREAKPOINTS; i++) {
    uint16_t opcode = 0;
    TEST_ASSERT_EQUAL_INT(AP_M68851_BREAKPOINT_REPLACED,
                          ap_m68851_breakpoint_acknowledge(&mmu, i, &opcode));
    TEST_ASSERT_EQUAL_HEX16(0x1000u + i, opcode);
  }
}

static void test_the_bac_reserved_bits_read_as_zeros(void) {
  /* "All unimplemented bits (bits [8-14]) are always read as zeros and must be
   * written as zeros." */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  (void)ap_m68851_pmove_write_numbered(&mmu, AP_M68851_PREG_BAC, 2u, 0xFFFFu);
  TEST_ASSERT_EQUAL_HEX64(
      0x80FFu, ap_m68851_pmove_read_numbered(&mmu, AP_M68851_PREG_BAC, 2u));
}

static void test_reset_clears_the_enable_but_not_the_skip_count(void) {
  /* §8.1, and the reason it is worth its own test: "The BPE bit is cleared at
   * reset; the skip count field is not." A reset that cleared the counts would
   * silently rearm every breakpoint to fire on its first pass. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  (void)ap_m68851_pmove_write_numbered(&mmu, AP_M68851_PREG_BAD, 4u, 0x4E71u);
  (void)ap_m68851_pmove_write_numbered(&mmu, AP_M68851_PREG_BAC, 4u, 0x8007u);

  ap_m68851_reset(&mmu);

  TEST_ASSERT_EQUAL_HEX64(
      0x0007u, ap_m68851_pmove_read_numbered(&mmu, AP_M68851_PREG_BAC, 4u));
  TEST_ASSERT_EQUAL_HEX64(
      0x4E71u, ap_m68851_pmove_read_numbered(&mmu, AP_M68851_PREG_BAD, 4u));
}

/* ---------------------------------------------------------------------------
 * The status write-back reaching memory.
 * ------------------------------------------------------------------------- */

static uint8_t status_byte_at(const memory_t *m, uint32_t descriptor) {
  return (uint8_t)(m->word[(descriptor - MEMORY_BASE) / 4u] & 0xFFu);
}

static void test_a_translation_writes_the_used_bits_into_the_tables(void) {
  /* §5.1.5.3.11: "updates of the U and M bits are performed before the MC68851
   * allows a page to be accessed or written". Until this test the model built
   * the write list and nobody called it -- the mechanism existed and the tables
   * in memory were still untouched, which is a gap that a unit test of the
   * *helper* cannot see. This one reads the memory back. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  TEST_ASSERT_EQUAL_UINT(0u, status_byte_at(&m, 0x1000u) & AP_M68851_STATUS_USED);
  TEST_ASSERT_EQUAL_UINT(0u, status_byte_at(&m, 0x2000u) & AP_M68851_STATUS_USED);

  const ap_m68851_translation_t t = ap_m68851_translate(
      &mmu, 0x00000123u,
      &(ap_m68851_access_t){.function_code = 5u, .is_write = false}, memory_fetch, &m, memory_store, &m);
  TEST_ASSERT_EQUAL_INT(AP_M68851_TRANSLATE_OK, t.status);

  /* Both descriptors -- the pointer and the page -- come back used. */
  TEST_ASSERT_NOT_EQUAL_UINT_MESSAGE(
      0u, status_byte_at(&m, 0x1000u) & AP_M68851_STATUS_USED,
      "the pointer was walked and must be marked used");
  TEST_ASSERT_NOT_EQUAL_UINT_MESSAGE(
      0u, status_byte_at(&m, 0x2000u) & AP_M68851_STATUS_USED,
      "the page was accessed and must be marked used");
  /* A read leaves `M` alone. */
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      0u, status_byte_at(&m, 0x2000u) & AP_M68851_STATUS_MODIFIED,
      "a read must not mark a page modified");
  TEST_ASSERT_EQUAL_UINT(2u, m.stores);
  /* The page's cycle is a read-modify-write, because it sets `U` without
   * disturbing an `M` it is not itself setting; the pointer's is a plain
   * write, having no `M` at all. */
  TEST_ASSERT_EQUAL_UINT(1u, m.read_modify_writes);
}

static void test_a_write_access_marks_the_page_modified(void) {
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  const ap_m68851_translation_t t = ap_m68851_translate(
      &mmu, 0x00000123u,
      &(ap_m68851_access_t){.function_code = 5u, .is_write = true}, memory_fetch, &m, memory_store, &m);
  TEST_ASSERT_EQUAL_INT(AP_M68851_TRANSLATE_OK, t.status);
  TEST_ASSERT_NOT_EQUAL_UINT(
      0u, status_byte_at(&m, 0x2000u) & AP_M68851_STATUS_MODIFIED);
  /* The pointer above is *not* modified: only page descriptors carry `M`, and
   * setting bit 4 of a pointer would corrupt whatever field owns it. */
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      0u, status_byte_at(&m, 0x1000u) & AP_M68851_STATUS_MODIFIED,
      "a pointer must never gain a modified bit");
  /* Both bits set from clear in one cycle, so no read-modify-write is needed
   * for the page this time. */
  TEST_ASSERT_EQUAL_UINT(0u, m.read_modify_writes);
}

static void test_an_atc_hit_writes_nothing(void) {
  /* The bits were written when the entry was made. Walking the tree again to
   * set them a second time would be bus traffic the hardware never generates,
   * and would make a hot loop over one page rewrite its descriptors forever. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  (void)ap_m68851_translate(&mmu, 0x00000123u, &(ap_m68851_access_t){.function_code = 5u,
                          .is_write = false}, memory_fetch, &m,
                            memory_store, &m);
  const unsigned after_first = m.stores;
  TEST_ASSERT_TRUE(after_first > 0u);

  const ap_m68851_translation_t second = ap_m68851_translate(
      &mmu, 0x00000456u,
      &(ap_m68851_access_t){.function_code = 5u, .is_write = false}, memory_fetch, &m, memory_store, &m);
  TEST_ASSERT_TRUE_MESSAGE(second.cache_hit, "the second access should hit");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(after_first, m.stores,
                                 "an ATC hit must not touch the tables");
}

static void test_a_second_walk_writes_nothing_more(void) {
  /* "Only performing write cycles to modify these bits are required." Once the
   * bits are set, a fresh walk of the same tree costs no write cycles at all --
   * which is what makes the write-back affordable rather than a tax on every
   * miss. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  (void)ap_m68851_translate(&mmu, 0u, &(ap_m68851_access_t){.function_code = 5u,
                          .is_write = true}, memory_fetch, &m, memory_store,
                            &m);
  const unsigned after_first = m.stores;
  ap_m68851_atc_flush(&mmu.atc);
  (void)ap_m68851_translate(&mmu, 0u, &(ap_m68851_access_t){.function_code = 5u,
                          .is_write = true}, memory_fetch, &m, memory_store,
                            &m);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      after_first, m.stores,
      "a descriptor already carrying its bits costs no cycle");
}

static void test_a_null_store_leaves_the_tables_alone(void) {
  /* The documented escape for a caller with no write path. It must be a
   * deliberate choice rather than a silent default, which is why every test
   * above passes a real store and only this one does not. */
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m);
  (void)ap_m68851_translate(&mmu, 0u, &(ap_m68851_access_t){.function_code = 5u,
                          .is_write = true}, memory_fetch, &m, NULL, NULL);
  TEST_ASSERT_EQUAL_UINT(0u, m.stores);
  TEST_ASSERT_EQUAL_UINT(0u,
                         status_byte_at(&m, 0x2000u) & AP_M68851_STATUS_USED);
}

/* ---------------------------------------------------------------------------
 * Protection: the accrued `ACC_STATUS` and what it denies.
 *
 * `[68851]` §5.1.6 and Figures 5-24/5-27 for the accumulation, §6.3.1.3 and
 * §6.3.1.4 for the denials, §7.2.2 and §7.2.3.1 for the access levels.
 * ------------------------------------------------------------------------- */

static void put_long(memory_t *m, uint32_t address, uint32_t high,
                     uint32_t low) {
  const uint32_t index = (address - MEMORY_BASE) / 4u;
  m->word[index] = high;
  m->word[index + 1u] = low;
}

/* A long descriptor's upper long word: RAL 47-45, WAL 44-42, SG 41, S 40,
 * WP 34, DT 33-32 -- so bits 15-0 of the *high* word hold 47-32. */
static uint32_t long_upper(unsigned ral, unsigned wal, bool sg, bool s,
                           bool wp, unsigned dt) {
  return ((ral & 7u) << 13) | ((wal & 7u) << 10) | ((unsigned)sg << 9) |
         ((unsigned)s << 8) | ((unsigned)wp << 2) | (dt & 3u);
}

/* Two levels of ten index bits, long format throughout, with every level-A
 * descriptor permissive so that the page descriptor alone decides. */
#define PAGE_TABLE 0x3000u
#define PAGE_FRAME 0x50000u

static void configure_long(ap_m68851_t *mmu, memory_t *m) {
  ap_m68851_reset(mmu);
  mmu->tc = (ap_m68851_tc_t){.enable = true,
                             .page_size = 0xCu,
                             .initial_shift = 0,
                             .table_index = {10u, 10u, 0u, 0u}};
  mmu->crp = (ap_m68851_rp_t){.descriptor_type = AP_M68851_DT_VALID_8_BYTE,
                              .table_address = 0x1000u,
                              .lower_limit = false,
                              .limit = 0x7FFFu};
  memset(m, 0, sizeof *m);
  /* §7.2.2: "the access level of a logical address is contained in the most
   * significant one, two, or three bits" -- which are also the top of the
   * level-A index, so each access level reaches a different level-A entry. All
   * of them name the same page table and none of them restricts anything. */
  for (unsigned level = 0; level < 8u; level++) {
    const uint32_t address = (uint32_t)level << 29;
    const uint32_t entry = 0x1000u + ((address >> 22) & 0x3FFu) * 8u;
    put_long(m, entry, long_upper(7u, 7u, false, false, false, 3u),
             PAGE_TABLE);
  }
}

/* Set the page descriptor every level-A entry above points at. */
static void set_page(memory_t *m, unsigned ral, unsigned wal, bool s,
                     bool wp) {
  put_long(m, PAGE_TABLE, long_upper(ral, wal, false, s, wp, 1u), PAGE_FRAME);
}

static ap_m68851_translate_status_t access_at(ap_m68851_t *mmu, memory_t *m,
                                              unsigned level, bool is_write) {
  const ap_m68851_access_t access = {.function_code = 5u,
                                     .is_write = is_write};
  return ap_m68851_translate(mmu, (uint32_t)level << 29, &access, memory_fetch,
                             m, NULL, NULL)
      .status;
}

/* **`RAL` and `WAL` take the minimum down the path, not the last value.**
 *
 * §5.1.6: "the effective RAL of a page will be the **minimum (most privileged)
 * of all RAL fields encountered**", and Figure 5-27 draws it as
 * `IF RAL < ACC_STATUS[RAL] THEN ACC_STATUS[RAL] <- RAL`.
 *
 * Both orders, because a model that simply copied the last descriptor's field
 * would pass one of them. */
static void test_the_access_levels_take_the_minimum_down_the_path(void) {
  for (unsigned above = 0; above < 2u; above++) {
    ap_m68851_t mmu;
    memory_t m;
    configure_long(&mmu, &m);
    /* Level A restrictive and the page permissive, then the reverse. */
    const unsigned a_ral = above ? 2u : 7u;
    const unsigned page_ral = above ? 7u : 2u;
    const uint32_t entry = 0x1000u + ((0x40000000u >> 22) & 0x3FFu) * 8u;
    put_long(&m, entry, long_upper(a_ral, a_ral, false, false, false, 3u),
             PAGE_TABLE);
    set_page(&m, page_ral, page_ral, false, false);

    bool root_is_drp = false;
    (void)root_is_drp;
    const ap_m68851_search_config_t config = {
        .tc = &mmu.tc, .root = &mmu.crp, .fetch = memory_fetch,
        .fetch_context = &m};
    const ap_m68851_search_result_t found =
        ap_m68851_search(&config, 0x40000000u, 5u);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(2u, found.read_access_level,
                                   "the effective RAL is not the minimum");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(2u, found.write_access_level,
                                   "the effective WAL is not the minimum");
  }
}

/* **A path of short-format descriptors leaves both levels at `$7`.**
 *
 * §5.1.6: "if there are no long format descriptors in the path through the
 * translation tree that is used to translate an address, then ... the page is
 * not restricted to supervisor-only, and the effective RAL and WAL are both
 * `$7` (least privileged)" -- which is also Figure 5-24's initial value, so
 * this pins the identity as well as the default. */
static void test_a_short_format_path_defaults_to_the_least_privilege(void) {
  ap_m68851_t mmu;
  memory_t m;
  configure(&mmu, &m); /* the short-format tree */
  const ap_m68851_search_config_t config = {
      .tc = &mmu.tc, .root = &mmu.crp, .fetch = memory_fetch,
      .fetch_context = &m};
  const ap_m68851_search_result_t found = ap_m68851_search(&config, 0u, 5u);
  TEST_ASSERT_EQUAL_UINT(7u, found.read_access_level);
  TEST_ASSERT_EQUAL_UINT(7u, found.write_access_level);
  TEST_ASSERT_FALSE(found.supervisor_only);
}

/* **§7.2.3.1's own worked examples, both of the ones that discriminate.**
 *
 * "Now consider a page with a RAL encoding of five and a WAL encoding of four;
 * a task may read from this page using a privilege level of five but must use
 * an access level of four or lower (more privileged) to write to the page.
 * Finally, consider a page with a RAL encoding of five and a WAL encoding of
 * six; a task must use an access level of five or lower to read from **or write
 * to** this page. An attempt to write to this page using an access level of six
 * would be aborted by the MC68851 **since it is less privileged than the read
 * access level of the page**."
 *
 * The second example is the one that matters: a `WAL` looser than the `RAL`
 * buys nothing, because §7.2.3.1's opening rule is that "denying a task read
 * access to an area implies that the task also does not have sufficient
 * privilege to write to that area ... regardless of the write access level
 * associated with that area". §7.2.2 says the same thing as an equation -- "the
 * effective write access level is the most privileged (numerically least) of
 * all WAL **and RAL** fields encountered". */
static void test_the_write_level_is_the_minimum_of_ral_and_wal(void) {
  ap_m68851_t mmu;
  memory_t m;
  configure_long(&mmu, &m);
  mmu.ac.access_level_control = AP_M68851_ALC_THREE_BITS;
  mmu.cal = ap_m68851_access_level_encode(0u);

  /* RAL 5, WAL 4: read at five, write only at four or lower.
   *
   * The write comes back **write-protected**, not access-level-violating, and
   * the division is the manual's: §6.3.1.5 puts "the WP bit is set in any
   * descriptor in the table search path" and "the access level bits of the
   * logical address are less privileged ... than the value of a WAL field"
   * under one heading and one `PSR` bit. It has to be that way for the cache to
   * work -- §5.2.1.2 gives an ATC entry a `W` bit and no access levels, so a
   * page readable at this level and writeable only above it can only be
   * expressed by `W`. The read below is the half that proves it: it goes
   * through the same entry. */
  set_page(&m, 5u, 4u, false, false);
  TEST_ASSERT_EQUAL_INT_MESSAGE(AP_M68851_TRANSLATE_OK,
                                access_at(&mmu, &m, 5u, false),
                                "a read at the RAL was denied");
  TEST_ASSERT_EQUAL_INT_MESSAGE(AP_M68851_TRANSLATE_WRITE_PROTECTED,
                                access_at(&mmu, &m, 5u, true),
                                "a write above the WAL was permitted");
  TEST_ASSERT_EQUAL_INT_MESSAGE(AP_M68851_TRANSLATE_OK,
                                access_at(&mmu, &m, 4u, true),
                                "a write at the WAL was denied");

  /* RAL 5, WAL 6: the looser WAL buys nothing. This is the assertion a model
   * comparing a write against `WAL` alone fails. */
  ap_m68851_t second;
  memory_t n;
  configure_long(&second, &n);
  second.ac.access_level_control = AP_M68851_ALC_THREE_BITS;
  second.cal = ap_m68851_access_level_encode(0u);
  set_page(&n, 5u, 6u, false, false);
  TEST_ASSERT_EQUAL_INT_MESSAGE(AP_M68851_TRANSLATE_OK,
                                access_at(&second, &n, 5u, true),
                                "a write at the RAL was denied");
  /* And *this* one is an access level violation rather than a write
   * protection: exceeding `RAL` denies reads too, so it is the `B` bit's case
   * and not the `W` bit's. The read below is what makes that a claim. */
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      AP_M68851_TRANSLATE_ACCESS_LEVEL, access_at(&second, &n, 6u, true),
      "a write at six was permitted by a WAL of six, where the RAL is five");
  TEST_ASSERT_EQUAL_INT(AP_M68851_TRANSLATE_BUS_ERROR,
                        access_at(&second, &n, 6u, false));
}

/* **Access level checking is off unless `ALC` enables it**, which is the state
 * every in-scope machine is in: `ALC` "is initialized to zero during reset" and
 * nothing on a DN3500 writes it. The same page that denies above must permit
 * here, or the reference module would break a machine that merely fitted it. */
static void test_a_disabled_alc_checks_no_levels(void) {
  ap_m68851_t mmu;
  memory_t m;
  configure_long(&mmu, &m);
  TEST_ASSERT_EQUAL_UINT(AP_M68851_ALC_DISABLED, mmu.ac.access_level_control);
  set_page(&m, 0u, 0u, false, false); /* the most privileged page there is */
  TEST_ASSERT_EQUAL_INT(AP_M68851_TRANSLATE_OK,
                        access_at(&mmu, &m, 7u, true));
}

/* **§6.3.1.3: a user access to a supervisor-only page, and the denial caches.**
 *
 * "If bit FC[2] of a logical address is zero and a set S bit is encountered
 * during the table search in a long format descriptor for that address, an ATC
 * entry will be made with its **internal bus error (B) bit set**."
 *
 * The `S` attribute is a function code test and not an access level one, so it
 * applies with `ALC` disabled -- which is the only configuration this project's
 * machine would ever run. The second access is the half that shows the entry
 * cached the denial rather than the search repeating it. */
static void test_a_user_access_to_a_supervisor_page_is_denied_and_cached(void) {
  ap_m68851_t mmu;
  memory_t m;
  configure_long(&mmu, &m);
  set_page(&m, 7u, 7u, true, false);

  const ap_m68851_access_t user = {.function_code = 1u};
  const ap_m68851_translation_t first = ap_m68851_translate(
      &mmu, 0u, &user, memory_fetch, &m, NULL, NULL);
  TEST_ASSERT_EQUAL_INT(AP_M68851_TRANSLATE_SUPERVISOR_ONLY, first.status);
  TEST_ASSERT_FALSE(first.cache_hit);

  const unsigned walked = m.fetches;
  const ap_m68851_translation_t second = ap_m68851_translate(
      &mmu, 0u, &user, memory_fetch, &m, NULL, NULL);
  TEST_ASSERT_TRUE_MESSAGE(second.cache_hit,
                           "the denial was not cached in the ATC");
  TEST_ASSERT_EQUAL_INT(AP_M68851_TRANSLATE_BUS_ERROR, second.status);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(walked, m.fetches,
                                 "the tables were walked a second time");

  /* And a supervisor access to the same page is untouched by any of it --
   * `FC[2]` set is what the section turns on. A different function code is a
   * different ATC tag, so this is a fresh search rather than the cached one. */
  ap_m68851_t clean;
  memory_t c;
  configure_long(&clean, &c);
  set_page(&c, 7u, 7u, true, false);
  const ap_m68851_access_t super = {.function_code = 5u};
  TEST_ASSERT_EQUAL_INT(
      AP_M68851_TRANSLATE_OK,
      ap_m68851_translate(&clean, 0u, &super, memory_fetch, &c, NULL, NULL)
          .status);
}

/* **§6.3.1.4's first paragraph caches nothing, and that is the difference.**
 *
 * "If access levels are enabled, and the access level bits of a logical address
 * indicates a higher privilege (numerically less) than the value of the CAL
 * register, the MC68851 will assert the BERR signal. Note that the **PTEST
 * instruction will not detect this condition**, and the fault handler of the
 * main processor should compare the access level field of the fault address
 * with the value contained in the MC68851 CAL register **at the time of the
 * fault**."
 *
 * "At the time of the fault" is the tell: the comparison is against a register
 * that changes on every task switch, so an entry holding `B` for it would
 * outlive its reason. Lowering `CAL` must let the same address through with no
 * flush -- which is what separates this from §6.3.1.3 above. */
static void test_an_address_more_privileged_than_cal_is_denied_but_not_cached(
    void) {
  ap_m68851_t mmu;
  memory_t m;
  configure_long(&mmu, &m);
  mmu.ac.access_level_control = AP_M68851_ALC_THREE_BITS;
  set_page(&m, 7u, 7u, false, false);

  /* A task at level 4 reaching for an address that claims level 2. */
  mmu.cal = ap_m68851_access_level_encode(4u);
  TEST_ASSERT_EQUAL_INT(AP_M68851_TRANSLATE_ACCESS_LEVEL,
                        access_at(&mmu, &m, 2u, false));
  /* §6.1.8.4 excludes it: "the PTEST instruction will not detect this". */
  TEST_ASSERT_FALSE_MESSAGE(mmu.psr.access_level_violation,
                            "the CAL comparison reached PSR's A bit");

  /* The same address, once the task holds the privilege. No flush. */
  mmu.cal = ap_m68851_access_level_encode(2u);
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      AP_M68851_TRANSLATE_OK, access_at(&mmu, &m, 2u, false),
      "a CAL denial was cached, so lowering CAL did not release the page");
}

/* **§4.2.3.3's condition (6): a read-modify-write needs a resident entry.**
 *
 * "A read-modify-write operation is attempted to a page that does not have a
 * corresponding descriptor resident in the address translation cache, has its
 * modified bit clear, or is write-protected."
 *
 * The first disjunct is the surprising one and the one worth a test: the
 * mapping is perfectly good and the access is still denied, because the part
 * will not walk the tables while holding the bus. The retry succeeds on the
 * entry the failed attempt left behind, which is what makes the rule workable
 * rather than a deadlock. */
static void test_a_read_modify_write_needs_a_resident_entry(void) {
  ap_m68851_t mmu;
  memory_t m;
  configure_long(&mmu, &m);
  set_page(&m, 7u, 7u, false, false);

  const ap_m68851_access_t rmc = {
      .function_code = 5u, .is_write = true, .is_read_modify_write = true};

  /* The miss. The tables map it and it is denied anyway. */
  const ap_m68851_translation_t first =
      ap_m68851_translate(&mmu, 0u, &rmc, memory_fetch, &m, NULL, NULL);
  TEST_ASSERT_FALSE(first.cache_hit);
  TEST_ASSERT_EQUAL_INT(AP_M68851_TRANSLATE_RMC_DENIED, first.status);

  /* The retry, on the entry the first attempt made. Its `M` was set by the
   * write the entry recorded, so the second and third disjuncts are clear. */
  const ap_m68851_translation_t retry =
      ap_m68851_translate(&mmu, 0u, &rmc, memory_fetch, &m, NULL, NULL);
  TEST_ASSERT_TRUE(retry.cache_hit);
  TEST_ASSERT_EQUAL_INT(AP_M68851_TRANSLATE_OK, retry.status);

  /* An ordinary write to the same page never needed any of this. */
  ap_m68851_t plain;
  memory_t p;
  configure_long(&plain, &p);
  set_page(&p, 7u, 7u, false, false);
  TEST_ASSERT_EQUAL_INT(AP_M68851_TRANSLATE_OK,
                        access_at(&plain, &p, 0u, true));
}

/* **An indirect descriptor carries no protection**, and a short one's bit 2 is
 * an address bit.
 *
 * Figure 5-17 puts a short indirect descriptor's address at bits 31-2, where
 * Figure 5-12 reads bit 2 of a short *table* descriptor as `WP`. Accumulating
 * from it -- which Figure 5-27's undifferentiated branch reads as licensing --
 * write-protects every indirect target whose descriptor address happens to have
 * bit 2 set, which is half of them.
 *
 * `ap_m68851_descriptor.c` already states the rule this checks: "an indirect
 * descriptor carries no protection of its own -- the descriptor it names
 * carries it, which is the point of the indirection." */
static void test_an_indirect_descriptor_contributes_no_protection(void) {
  ap_m68851_t mmu;
  memory_t m;
  ap_m68851_reset(&mmu);
  /* One level of ten index bits, so the second descriptor is the indirect. */
  mmu.tc = (ap_m68851_tc_t){.enable = true,
                            .page_size = 0xCu,
                            .initial_shift = 0,
                            .table_index = {10u, 0u, 0u, 0u}};
  mmu.crp = (ap_m68851_rp_t){.descriptor_type = AP_M68851_DT_VALID_4_BYTE,
                             .table_address = 0x1000u,
                             .lower_limit = false,
                             .limit = 0x7FFFu};
  memset(&m, 0, sizeof m);
  /* Level A entry 0 is the indirect descriptor. Its target is at `0x2004`,
   * whose bit 2 is set -- so a decoder reading bit 2 as `WP` sees a write
   * protect where the address's own least significant bit is. */
  put_short(&m, 0x1000u, 0x2004u | 0x2u);
  put_short(&m, 0x2004u, PAGE_FRAME | 0x1u);

  const ap_m68851_search_config_t config = {
      .tc = &mmu.tc, .root = &mmu.crp, .fetch = memory_fetch,
      .fetch_context = &m};
  const ap_m68851_search_result_t found = ap_m68851_search(&config, 0u, 5u);
  TEST_ASSERT_EQUAL_INT(AP_M68851_SEARCH_TYPE_INDIRECT, found.type);
  TEST_ASSERT_EQUAL_HEX32(PAGE_FRAME, found.physical_address);
  TEST_ASSERT_FALSE_MESSAGE(
      found.write_protect,
      "an indirect descriptor's address bit 2 was read as a write protect");
}

/* **`PSR`'s `S`, `A` and `W` bits, which the accumulation made reportable.**
 *
 * §6.1.8.3 sets `S` "if a set S bit of a long format descriptor was
 * encountered"; §6.1.8.4 sets `A` "if the address tested exceeded RAL for the
 * PTESTR instruction, or exceeded WAL or RAL for the PTESTW"; §6.1.8.5 sets `W`
 * "if any descriptor encountered in the search contained a set WP bit, **or if
 * the address tested exceeded the WAL field of any long descriptor**".
 *
 * That second half of `W` is the one the accumulation unlocked: before it, `W`
 * could only ever mean a `WP` bit. */
static void test_ptest_reports_the_accrued_protection(void) {
  ap_m68851_t mmu;
  memory_t m;
  configure_long(&mmu, &m);
  mmu.ac.access_level_control = AP_M68851_ALC_THREE_BITS;
  mmu.cal = ap_m68851_access_level_encode(0u);
  set_page(&m, 4u, 4u, true, false);

  /* `PTESTW` at level six: past both RAL and WAL, and the page is
   * supervisor-only. */
  const ap_m68851_instruction_t ptestw = {
      .opcode = AP_M68851_OP_PTEST, .level = 7u, .read_from_mmu = false};
  TEST_ASSERT_EQUAL_INT(AP_M68851_EXECUTED,
                        ap_m68851_ptest(&mmu, &ptestw, 5u, 6u << 29,
                                        memory_fetch, &m));
  TEST_ASSERT_TRUE_MESSAGE(mmu.psr.supervisor_only, "PSR S not reported");
  TEST_ASSERT_TRUE_MESSAGE(mmu.psr.access_level_violation,
                           "PSR A not reported");
  TEST_ASSERT_TRUE_MESSAGE(mmu.psr.write_protected,
                           "PSR W not set by a WAL the address exceeded");

  /* At level zero none of the three holds, and `W` in particular must come
   * back clear -- a page with no `WP` bit anywhere is writeable from a
   * sufficiently privileged level. */
  set_page(&m, 4u, 4u, false, false);
  ap_m68851_atc_flush(&mmu.atc);
  TEST_ASSERT_EQUAL_INT(
      AP_M68851_EXECUTED,
      ap_m68851_ptest(&mmu, &ptestw, 5u, 0u, memory_fetch, &m));
  TEST_ASSERT_FALSE(mmu.psr.supervisor_only);
  TEST_ASSERT_FALSE(mmu.psr.access_level_violation);
  TEST_ASSERT_FALSE(mmu.psr.write_protected);
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
  RUN_TEST(test_pload_installs_an_entry_nothing_referenced);
  RUN_TEST(test_ploadw_marks_the_entry_modified_and_ploadr_does_not);
  RUN_TEST(test_pload_is_refused_while_translation_is_disabled);
  RUN_TEST(test_ptest_reports_a_good_translation_in_the_psr);
  RUN_TEST(test_ptest_reports_an_invalid_descriptor);
  RUN_TEST(test_ptest_reports_a_limit_violation_apart_from_invalidity);
  RUN_TEST(test_a_level_ceiling_stops_the_search_without_reporting_a_fault);
  RUN_TEST(test_a_level_zero_ptest_searches_only_the_atc);
  RUN_TEST(test_pvalid_always_violates_when_module_control_is_clear);
  RUN_TEST(test_pvalid_refuses_a_pointer_more_privileged_than_the_caller);
  RUN_TEST(test_pvalid_can_test_against_a_surrogate_level);
  RUN_TEST(test_pvalid_permits_everything_when_access_levels_are_disabled);
  RUN_TEST(test_a_disabled_breakpoint_bus_errors);
  RUN_TEST(test_an_enabled_breakpoint_returns_its_replacement_opcode);
  RUN_TEST(test_the_skip_count_counts_down_to_a_bus_error);
  RUN_TEST(test_a_disabled_and_an_exhausted_breakpoint_are_indistinguishable);
  RUN_TEST(test_the_eight_breakpoints_are_independent);
  RUN_TEST(test_the_bac_reserved_bits_read_as_zeros);
  RUN_TEST(test_reset_clears_the_enable_but_not_the_skip_count);
  RUN_TEST(test_a_translation_writes_the_used_bits_into_the_tables);
  RUN_TEST(test_a_write_access_marks_the_page_modified);
  RUN_TEST(test_an_atc_hit_writes_nothing);
  RUN_TEST(test_a_second_walk_writes_nothing_more);
  RUN_TEST(test_a_null_store_leaves_the_tables_alone);
  RUN_TEST(test_the_access_levels_take_the_minimum_down_the_path);
  RUN_TEST(test_a_short_format_path_defaults_to_the_least_privilege);
  RUN_TEST(test_the_write_level_is_the_minimum_of_ral_and_wal);
  RUN_TEST(test_a_disabled_alc_checks_no_levels);
  RUN_TEST(test_a_user_access_to_a_supervisor_page_is_denied_and_cached);
  RUN_TEST(test_an_address_more_privileged_than_cal_is_denied_but_not_cached);
  RUN_TEST(test_a_read_modify_write_needs_a_resident_entry);
  RUN_TEST(test_an_indirect_descriptor_contributes_no_protection);
  RUN_TEST(test_ptest_reports_the_accrued_protection);
  return UNITY_END();
}
