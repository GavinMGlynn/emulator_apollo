/* The MC68040's `PTEST`, from `[PRM]` pp. 6-70 and 6-71 read as page images.
 *
 * The table search, the ATC and the MMUSR encoding each have their own suite;
 * what this one is for is what `PTEST` *does with* them, and the four things it
 * does that an ordinary access does not: it never answers out of the ATC, it
 * flushes the entry it would have answered from, it reports rather than faults,
 * and it declines four of the eight function codes.
 */
#include <string.h>

#include "unity.h"

#include "cpu/m68040/ap_m68040_ptest.h"

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

/* `m68040_mmu_suite`'s tree, deliberately: root at 0x1000, pointer table at
 * 0x2000, page table at 0x3000, frame at 0x50000. Three suites agreeing about
 * what a resident translation looks like is worth more than three trees. */
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
  g_tc = 0x8000u; /* E set, 4-Kbyte pages */
  g_ttr[0] = 0u;
  g_ttr[1] = 0u;
  g_urp = 0x1000u;
  g_srp = 0x1000u;
  ap_m68040_atc_init(&g_atc);
}

/* The encoding, transcribed from the figure on p. 6-71:
 * `1 1 1 1 0 1 0 1 0 1 R/W 0 1 REGISTER`. This is the assertion that the DS5500
 * firmware's `F56C` is the instruction this module claims it is, and the one a
 * transposed bit in the figure would break. */
static void test_the_encoding_is_f548_for_write_and_f568_for_read(void) {
  TEST_ASSERT_TRUE(ap_m68040_is_ptest(0xF548u));
  TEST_ASSERT_TRUE(ap_m68040_is_ptest(0xF568u));
  TEST_ASSERT_TRUE(ap_m68040_is_ptest(0xF56Cu));
  TEST_ASSERT_FALSE(ap_m68040_ptest_is_read(0xF548u));
  TEST_ASSERT_TRUE(ap_m68040_ptest_is_read(0xF56Cu));
  TEST_ASSERT_EQUAL_UINT(4u, ap_m68040_ptest_register(0xF56Cu));
  TEST_ASSERT_EQUAL_UINT(7u, ap_m68040_ptest_register(0xF54Fu));
}

/* `PFLUSH` is `$F500`-`$F51F` and `PTEST` is `$F548`-`$F54F` and
 * `$F568`-`$F56F`. Neither mask may claim the other's words, and the two are
 * near enough that a wrong mask would look right on the instruction that
 * prompted the work. */
static void test_the_mask_does_not_claim_pflush(void) {
  for (uint16_t word = 0xF500u; word <= 0xF51Fu; word++) {
    TEST_ASSERT_FALSE(ap_m68040_is_ptest(word));
  }
  for (uint16_t word = 0xF548u; word <= 0xF54Fu; word++) {
    TEST_ASSERT_TRUE(ap_m68040_is_ptest(word));
    TEST_ASSERT_NOT_EQUAL(0xF500u, word & 0xFFE0u);
  }
}

/* "Resident (R) -- Set if ... the table search completes by obtaining a valid
 * page descriptor", and the physical address field carries "the upper bits of
 * the translated physical address". */
static void test_a_resident_page_reports_r_and_the_frame(void) {
  memory_t m;
  build(&m);
  reset_registers();
  const ap_m68040_mmu_t v = mmu();

  const ap_m68040_ptest_result_t r =
      ap_m68040_ptest(&v, 0x00000123u, 5u, false, memory_fetch, memory_update, &m);

  TEST_ASSERT_TRUE(r.defined);
  TEST_ASSERT_TRUE(r.mmusr.resident);
  TEST_ASSERT_FALSE(r.mmusr.bus_error);
  TEST_ASSERT_FALSE(r.mmusr.transparent);
  TEST_ASSERT_EQUAL_HEX32(0x50000u, r.mmusr.physical_address);
  TEST_ASSERT_TRUE(r.filled);
}

/* "Completion of PTEST results in the creation of a new address translation
 * cache entry" -- including when the completion is an invalid descriptor, which
 * is the case the firmware that asked for this instruction is testing for. */
static void test_a_nonresident_page_reports_r_clear_and_still_fills(void) {
  memory_t m;
  build(&m);
  put(&m, 0x3000u, 0x50000u); /* PDT 00: invalid */
  reset_registers();
  const ap_m68040_mmu_t v = mmu();

  const ap_m68040_ptest_result_t r =
      ap_m68040_ptest(&v, 0x00000123u, 5u, false, memory_fetch, memory_update, &m);

  TEST_ASSERT_TRUE(r.defined);
  TEST_ASSERT_FALSE(r.mmusr.resident);
  TEST_ASSERT_TRUE(r.filled);
}

/* **The one that separates `PTEST` from a translation.** "A matching entry in
 * the address translation cache ... will be flushed by PTEST", so a second
 * `PTEST` of an address the first one cached must read the tables again. An
 * implementation that called the ordinary translate path would pass every other
 * test in this file and fail this one. */
static void test_a_second_ptest_searches_again_rather_than_hitting_the_atc(void) {
  memory_t m;
  build(&m);
  reset_registers();
  const ap_m68040_mmu_t v = mmu();

  (void)ap_m68040_ptest(&v, 0x00000123u, 5u, false, memory_fetch, memory_update, &m);
  const unsigned first = m.fetches;
  TEST_ASSERT_TRUE(first > 0u);

  const ap_m68040_ptest_result_t again =
      ap_m68040_ptest(&v, 0x00000123u, 5u, false, memory_fetch, memory_update, &m);

  TEST_ASSERT_EQUAL_UINT(first, again.fetches);
  TEST_ASSERT_EQUAL_UINT(first * 2u, m.fetches);
}

/* "Set if a transfer error is encountered during the table search for the PTEST
 * instruction. If this bit is set, all other bits are zero." */
static void test_a_transfer_error_reports_b_alone(void) {
  memory_t m;
  build(&m);
  m.fail = true;
  reset_registers();
  const ap_m68040_mmu_t v = mmu();

  const ap_m68040_ptest_result_t r =
      ap_m68040_ptest(&v, 0x00000123u, 5u, false, memory_fetch, memory_update, &m);

  TEST_ASSERT_TRUE(r.defined);
  TEST_ASSERT_TRUE(r.mmusr.bus_error);
  TEST_ASSERT_EQUAL_HEX32(0u, ap_m68040_mmusr_encode(&r.mmusr) & ~0x800u);
  TEST_ASSERT_FALSE(r.filled);
}

/* "Transparent Translation Register Hit (T) -- Set if the PTEST address matches
 * an instruction or data transparent translation register and the R-bit is set;
 * all other bits are zero", and it is the first of the four terminating
 * conditions, so no descriptor is read. */
static void test_a_transparent_match_reports_t_and_r_and_reads_nothing(void) {
  memory_t m;
  build(&m);
  reset_registers();
  /* `m68040_mmu_suite`'s transparent block with its base moved to zero: base
   * 0x00 in bits 31-24, mask 0x00, `E` set, S-field 10 so either privilege
   * matches, CM 10. */
  g_ttr[0] = 0x0000C040u;
  const ap_m68040_mmu_t v = mmu();

  const ap_m68040_ptest_result_t r =
      ap_m68040_ptest(&v, 0x00000123u, 5u, false, memory_fetch, memory_update, &m);

  TEST_ASSERT_TRUE(r.defined);
  TEST_ASSERT_TRUE(r.mmusr.transparent);
  TEST_ASSERT_TRUE(r.mmusr.resident);
  TEST_ASSERT_EQUAL_HEX32(0x3u, ap_m68040_mmusr_encode(&r.mmusr));
  TEST_ASSERT_EQUAL_UINT(0u, m.fetches);
}

/* "A PTEST instruction with a DFC value of 0, 3, 4, or 7 is undefined and will
 * return an unknown value in the MMUSR." Undefined is reported as undefined, so
 * the caller can leave the register alone rather than write an invented value
 * into it. */
static void test_the_four_undefined_function_codes_produce_no_result(void) {
  memory_t m;
  build(&m);
  reset_registers();
  const ap_m68040_mmu_t v = mmu();

  const unsigned undefined[] = {0u, 3u, 4u, 7u};
  for (unsigned i = 0; i < 4u; i++) {
    const ap_m68040_ptest_result_t r =
        ap_m68040_ptest(&v, 0x00000123u, undefined[i], false, memory_fetch, memory_update, &m);
    TEST_ASSERT_FALSE(r.defined);
  }
  TEST_ASSERT_EQUAL_UINT(0u, m.fetches);
}

/* `[040]` §3.1: "PTEST results are undefined if the MMU is disabled and no
 * table search occurs." Both halves: the TTRs are checked first and answer
 * independently of the E-bit, so it is only when *neither* applies that there
 * is nothing to report. */
static void test_a_disabled_mmu_with_no_transparent_match_is_undefined(void) {
  memory_t m;
  build(&m);
  reset_registers();
  g_tc = 0u; /* E clear */
  const ap_m68040_mmu_t v = mmu();

  TEST_ASSERT_FALSE(
      ap_m68040_ptest(&v, 0x00000123u, 5u, false, memory_fetch, memory_update, &m).defined);

  g_ttr[0] = 0x0000C040u;
  const ap_m68040_ptest_result_t transparent =
      ap_m68040_ptest(&v, 0x00000123u, 5u, false, memory_fetch, memory_update, &m);
  TEST_ASSERT_TRUE(transparent.defined);
  TEST_ASSERT_TRUE(transparent.mmusr.transparent);
}

/* "PTESTW simulates a write access and also sets the M-bit in the descriptors,
 * the address translation cache entry, and the MMU status register" -- where
 * `PTESTR` reports the descriptor's own M and leaves it. */
static void test_ptestw_sets_m_where_ptestr_reports_it(void) {
  memory_t m;
  build(&m);
  reset_registers();
  const ap_m68040_mmu_t v = mmu();

  TEST_ASSERT_FALSE(
      ap_m68040_ptest(&v, 0x00000123u, 5u, false, memory_fetch, memory_update, &m)
          .mmusr.modified);
  TEST_ASSERT_TRUE(
      ap_m68040_ptest(&v, 0x00000123u, 5u, true, memory_fetch, memory_update, &m)
          .mmusr.modified);
}

/* "Supervisor Protection (S) -- Set if the S-bit in the page descriptor is set.
 * This bit does not indicate that a violation has occurred." So a supervisor
 * page tested with a user function code reports `R` *and* `S`: `PTEST` reports,
 * it does not refuse. This is the distinction that makes the instruction useful
 * to a supervisor asking about a user address. */
static void test_a_supervisor_page_tested_from_user_space_still_reports_resident(void) {
  memory_t m;
  build(&m);
  put(&m, 0x3000u, 0x50000u | 0x80u | 0x1u); /* S set in the page descriptor */
  reset_registers();
  const ap_m68040_mmu_t v = mmu();

  const ap_m68040_ptest_result_t r =
      ap_m68040_ptest(&v, 0x00000123u, 1u, false, memory_fetch, memory_update, &m);

  TEST_ASSERT_TRUE(r.mmusr.resident);
  TEST_ASSERT_TRUE(r.mmusr.supervisor);
}

/* "Write Protect (W) -- Set if the W-bit is set in any of the descriptors
 * encountered during the table search", so a `W` on a *table* descriptor
 * reaches the MMUSR even though the page's own is clear. */
static void test_write_protection_from_a_table_descriptor_reaches_the_mmusr(void) {
  memory_t m;
  build(&m);
  put(&m, 0x2000u, 0x3000u | 0x4u | 0x2u); /* W in the pointer descriptor */
  reset_registers();
  const ap_m68040_mmu_t v = mmu();

  const ap_m68040_ptest_result_t r =
      ap_m68040_ptest(&v, 0x00000123u, 5u, false, memory_fetch, memory_update, &m);

  TEST_ASSERT_TRUE(r.mmusr.resident);
  TEST_ASSERT_TRUE(r.mmusr.write_protect);
}

/* The root pointer follows the function code's `FC2`, as an ordinary access's
 * does: "the supervisor root pointer ... is used for supervisor accesses and
 * the user root pointer for user accesses". */
static void test_the_root_pointer_follows_the_function_code(void) {
  memory_t m;
  build(&m);
  reset_registers();
  g_urp = 0x4000u; /* an empty root: the user side finds nothing */
  const ap_m68040_mmu_t v = mmu();

  TEST_ASSERT_TRUE(
      ap_m68040_ptest(&v, 0x00000123u, 5u, false, memory_fetch, memory_update, &m)
          .mmusr.resident);
  TEST_ASSERT_FALSE(
      ap_m68040_ptest(&v, 0x00000123u, 1u, false, memory_fetch, memory_update, &m)
          .mmusr.resident);
}


/* ---------------------------------------------------------------------------
 * `PTEST` writes the tables. `[PRM]` p. 6-70: "The PTESTR instruction simulates
 * a read access and sets the U-bit in each descriptor during table searches;
 * PTESTW simulates a write access and also sets the M-bit in the descriptors,
 * the address translation cache entry, and the MMU status register."
 * ------------------------------------------------------------------------- */

static void test_ptestr_sets_u_in_every_descriptor_and_leaves_m_alone(void) {
  memory_t m;
  build(&m);
  reset_registers();
  const ap_m68040_ptest_result_t r =
      ap_m68040_ptest(&(ap_m68040_mmu_t){.tc = &g_tc,
                                         .ttr = g_ttr,
                                         .urp = &g_urp,
                                         .srp = &g_srp,
                                         .atc = &g_atc},
                      0x00000123u, 5u, false, memory_fetch, memory_update, &m);

  TEST_ASSERT_TRUE(r.defined);
  TEST_ASSERT_TRUE((get(&m, 0x1000u) & (UINT32_C(1) << 3)) != 0u);
  TEST_ASSERT_TRUE((get(&m, 0x2000u) & (UINT32_C(1) << 3)) != 0u);
  TEST_ASSERT_TRUE((get(&m, 0x3000u) & (UINT32_C(1) << 3)) != 0u);
  TEST_ASSERT_TRUE((get(&m, 0x3000u) & (UINT32_C(1) << 4)) == 0u);
  TEST_ASSERT_FALSE(r.mmusr.modified);
}

static void test_ptestw_sets_m_in_the_descriptor_the_entry_and_the_register(void) {
  /* All three of the places the sentence names, checked in one test because the
   * sentence names them in one breath. */
  memory_t m;
  build(&m);
  reset_registers();
  const ap_m68040_mmu_t v = mmu();
  const ap_m68040_ptest_result_t r = ap_m68040_ptest(
      &v, 0x00000123u, 5u, true, memory_fetch, memory_update, &m);

  TEST_ASSERT_TRUE(r.defined);
  TEST_ASSERT_TRUE((get(&m, 0x3000u) & (UINT32_C(1) << 4)) != 0u);
  TEST_ASSERT_TRUE(r.mmusr.modified);
  const unsigned set = ap_m68040_atc_set(0x00000123u, AP_M68040_PAGE_4K);
  bool found = false;
  for (unsigned way = 0; way < AP_M68040_ATC_WAYS; way++) {
    if (g_atc.entry[set][way].valid) {
      found = true;
      TEST_ASSERT_TRUE(g_atc.entry[set][way].modified);
    }
  }
  TEST_ASSERT_TRUE(found);
}

static void test_ptestw_on_a_write_protected_page_reports_m_without_setting_it(void) {
  /* Table 3-1's WP = 1 write rows: `U` is set, `M` is not -- and `PTEST`
   * reports rather than faults, so the register still says what it found. */
  memory_t m;
  build(&m);
  put(&m, 0x3000u, 0x50000u | 0x1u | (UINT32_C(1) << 2));
  reset_registers();
  const ap_m68040_mmu_t v = mmu();
  const ap_m68040_ptest_result_t r = ap_m68040_ptest(
      &v, 0x00000123u, 5u, true, memory_fetch, memory_update, &m);

  TEST_ASSERT_TRUE(r.defined);
  TEST_ASSERT_TRUE(r.mmusr.resident);
  TEST_ASSERT_TRUE(r.mmusr.write_protect);
  TEST_ASSERT_FALSE(r.mmusr.modified);
  TEST_ASSERT_TRUE((get(&m, 0x3000u) & (UINT32_C(1) << 4)) == 0u);
  TEST_ASSERT_TRUE((get(&m, 0x3000u) & (UINT32_C(1) << 3)) != 0u);
}

static void test_a_ptest_the_manual_leaves_undefined_writes_nothing(void) {
  /* A DFC of 0, 3, 4 or 7 returns before any search, so there is no descriptor
   * to have been encountered. */
  memory_t m;
  build(&m);
  reset_registers();
  const ap_m68040_mmu_t v = mmu();
  const ap_m68040_ptest_result_t r = ap_m68040_ptest(
      &v, 0x00000123u, 0u, true, memory_fetch, memory_update, &m);

  TEST_ASSERT_FALSE(r.defined);
  TEST_ASSERT_EQUAL_HEX32(0x2000u | 0x2u, get(&m, 0x1000u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_encoding_is_f548_for_write_and_f568_for_read);
  RUN_TEST(test_the_mask_does_not_claim_pflush);
  RUN_TEST(test_a_resident_page_reports_r_and_the_frame);
  RUN_TEST(test_a_nonresident_page_reports_r_clear_and_still_fills);
  RUN_TEST(test_a_second_ptest_searches_again_rather_than_hitting_the_atc);
  RUN_TEST(test_a_transfer_error_reports_b_alone);
  RUN_TEST(test_a_transparent_match_reports_t_and_r_and_reads_nothing);
  RUN_TEST(test_the_four_undefined_function_codes_produce_no_result);
  RUN_TEST(test_a_disabled_mmu_with_no_transparent_match_is_undefined);
  RUN_TEST(test_ptestw_sets_m_where_ptestr_reports_it);
  RUN_TEST(test_a_supervisor_page_tested_from_user_space_still_reports_resident);
  RUN_TEST(test_write_protection_from_a_table_descriptor_reaches_the_mmusr);
  RUN_TEST(test_the_root_pointer_follows_the_function_code);
  RUN_TEST(test_ptestr_sets_u_in_every_descriptor_and_leaves_m_alone);
  RUN_TEST(test_ptestw_sets_m_in_the_descriptor_the_entry_and_the_register);
  RUN_TEST(test_ptestw_on_a_write_protected_page_reports_m_without_setting_it);
  RUN_TEST(test_a_ptest_the_manual_leaves_undefined_writes_nothing);
  return UNITY_END();
}
