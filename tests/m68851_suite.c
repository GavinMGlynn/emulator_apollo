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
      ap_m68851_translate(&mmu, 0x12345u, 5u, false, memory_fetch, &m, NULL, NULL);
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
      ap_m68851_translate(&mmu, 0x00000123u, 5u, false, memory_fetch, &m, NULL, NULL);
  TEST_ASSERT_EQUAL_INT(AP_M68851_TRANSLATE_OK, first.status);
  TEST_ASSERT_EQUAL_HEX32(0x50123u, first.physical_address);
  TEST_ASSERT_FALSE(first.cache_hit);
  TEST_ASSERT_EQUAL_UINT(2u, m.fetches);

  /* The second access is answered by the cache: no further descriptor reads. */
  const unsigned after_walk = m.fetches;
  const ap_m68851_translation_t second =
      ap_m68851_translate(&mmu, 0x00000456u, 5u, false, memory_fetch, &m, NULL, NULL);
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
      ap_m68851_translate(&mmu, 0u, 5u, false, memory_fetch, &m, NULL, NULL)
          .physical_address);
  TEST_ASSERT_EQUAL_HEX32(
      0x50FFFu,
      ap_m68851_translate(&mmu, 0xFFFu, 5u, false, memory_fetch, &m, NULL, NULL)
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
      ap_m68851_translate(&mmu, 0u, 5u, false, memory_fetch, &m, NULL, NULL);
  TEST_ASSERT_EQUAL_INT(AP_M68851_TRANSLATE_BUS_ERROR, first.status);
  const unsigned after_walk = m.fetches;

  const ap_m68851_translation_t second =
      ap_m68851_translate(&mmu, 0x100u, 5u, false, memory_fetch, &m, NULL, NULL);
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
      ap_m68851_translate(&mmu, 0u, 5u, false, memory_fetch, &m, NULL, NULL).status);
  TEST_ASSERT_EQUAL_INT(
      AP_M68851_TRANSLATE_WRITE_PROTECTED,
      ap_m68851_translate(&mmu, 0u, 5u, true, memory_fetch, &m, NULL, NULL).status);
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
      ap_m68851_translate(&mmu, 0u, 6u, false, memory_fetch, &m, NULL, NULL)
          .physical_address);

  mmu.tc.supervisor_root_pointer_enable = true;
  ap_m68851_atc_flush(&mmu.atc);
  TEST_ASSERT_EQUAL_HEX32(
      0x60000u,
      ap_m68851_translate(&mmu, 0u, 6u, false, memory_fetch, &m, NULL, NULL)
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
  (void)ap_m68851_translate(&mmu, 0u, 5u, false, memory_fetch, &m, NULL, NULL);
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
  (void)ap_m68851_translate(&mmu, 0u, 5u, false, memory_fetch, &m, NULL, NULL);

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
  (void)ap_m68851_translate(&mmu, 0u, 5u, false, memory_fetch, &m, NULL, NULL);

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
      ap_m68851_translate(&mmu, 0u, 5u, false, memory_fetch, &m, NULL, NULL).cache_hit);
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
  (void)ap_m68851_translate(&mmu, 0u, 5u, false, memory_fetch, &m, NULL, NULL);
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
      &mmu, 0x00000123u, 5u, false, memory_fetch, &m, memory_store, &m);
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
      &mmu, 0x00000123u, 5u, true, memory_fetch, &m, memory_store, &m);
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
  (void)ap_m68851_translate(&mmu, 0x00000123u, 5u, false, memory_fetch, &m,
                            memory_store, &m);
  const unsigned after_first = m.stores;
  TEST_ASSERT_TRUE(after_first > 0u);

  const ap_m68851_translation_t second = ap_m68851_translate(
      &mmu, 0x00000456u, 5u, false, memory_fetch, &m, memory_store, &m);
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
  (void)ap_m68851_translate(&mmu, 0u, 5u, true, memory_fetch, &m, memory_store,
                            &m);
  const unsigned after_first = m.stores;
  ap_m68851_atc_flush(&mmu.atc);
  (void)ap_m68851_translate(&mmu, 0u, 5u, true, memory_fetch, &m, memory_store,
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
  (void)ap_m68851_translate(&mmu, 0u, 5u, true, memory_fetch, &m, NULL, NULL);
  TEST_ASSERT_EQUAL_UINT(0u, m.stores);
  TEST_ASSERT_EQUAL_UINT(0u,
                         status_byte_at(&m, 0x2000u) & AP_M68851_STATUS_USED);
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
  return UNITY_END();
}
