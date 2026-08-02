/* MC68030 address translation cache.
 *
 * Cited to MC68030 User's Manual 3ed §9.4 pp. 9-17 ff.
 *
 * The behaviour worth naming up front is the one with a timing consequence: a
 * write to a page that was previously only read is a *hit* that still costs a
 * full table search, because the cached entry has M clear. An emulator that
 * treats it as an ordinary hit runs that write for free and loses a real,
 * measurable cost.
 */

#include "cpu/m68030/ap_m68030_atc.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define FC_SUPERVISOR_DATA 5u
#define FC_USER_DATA 1u
#define PS_4K 12u
#define PS_256 8u

/* Physical page frames are stored as A31-A8. */
#define FRAME(addr) ((addr) >> 8)

static ap_m68030_atc_t empty(void) {
  ap_m68030_atc_t atc;
  ap_m68030_atc_flush(&atc);
  return atc;
}

static ap_m68030_atc_result_t read_at(const ap_m68030_atc_t *atc,
                                      uint32_t address) {
  return ap_m68030_atc_lookup(atc, FC_SUPERVISOR_DATA, address, PS_4K, false,
                              false);
}

static ap_m68030_atc_result_t write_at(const ap_m68030_atc_t *atc,
                                       uint32_t address) {
  return ap_m68030_atc_lookup(atc, FC_SUPERVISOR_DATA, address, PS_4K, true,
                              false);
}

static void test_a_flushed_cache_misses_everything(void) {
  ap_m68030_atc_t atc = empty();
  TEST_ASSERT_EQUAL(AP_M68030_ATC_MISS, read_at(&atc, 0x00100000).status);
}

/* "All page index bits of the logical address are transferred to the bus
 * controller without translation" -- the frame comes from the entry, the offset
 * from the logical address. */
static void test_a_hit_merges_the_page_offset_into_the_frame(void) {
  ap_m68030_atc_t atc = empty();
  ap_m68030_atc_insert(&atc, FC_SUPERVISOR_DATA, 0x00100000, PS_4K,
                       FRAME(0x00A00000), false, false, true, false);

  ap_m68030_atc_result_t r = read_at(&atc, 0x00100ABC);
  TEST_ASSERT_EQUAL(AP_M68030_ATC_HIT, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x00A00ABC, r.physical);
}

/* Any address within the same page hits the same entry. */
static void test_every_address_in_a_page_hits_the_same_entry(void) {
  ap_m68030_atc_t atc = empty();
  int index = ap_m68030_atc_insert(&atc, FC_SUPERVISOR_DATA, 0x00100000, PS_4K,
                                   FRAME(0x00A00000), false, false, true, false);
  TEST_ASSERT_EQUAL_INT(index, read_at(&atc, 0x00100000).index);
  TEST_ASSERT_EQUAL_INT(index, read_at(&atc, 0x00100FFF).index);
  TEST_ASSERT_EQUAL(AP_M68030_ATC_MISS, read_at(&atc, 0x00101000).status);
}

/* "For larger page sizes, the appropriate number of least significant bits of
 * this field are ignored" -- with 256-byte pages the neighbouring page is a
 * different entry, where with 4K pages it is the same one. */
static void test_the_page_size_decides_how_much_of_the_tag_is_compared(void) {
  ap_m68030_atc_t atc = empty();
  ap_m68030_atc_insert(&atc, FC_SUPERVISOR_DATA, 0x00100000, PS_256,
                       FRAME(0x00A00000), false, false, true, false);

  TEST_ASSERT_EQUAL(AP_M68030_ATC_HIT,
                    ap_m68030_atc_lookup(&atc, FC_SUPERVISOR_DATA, 0x001000FF,
                                         PS_256, false, false).status);
  TEST_ASSERT_EQUAL(AP_M68030_ATC_MISS,
                    ap_m68030_atc_lookup(&atc, FC_SUPERVISOR_DATA, 0x00100100,
                                         PS_256, false, false).status);
}

/* The tag includes the function code, so the same logical address in a
 * different space is a different entry. */
static void test_the_function_code_is_part_of_the_tag(void) {
  ap_m68030_atc_t atc = empty();
  ap_m68030_atc_insert(&atc, FC_SUPERVISOR_DATA, 0x00100000, PS_4K,
                       FRAME(0x00A00000), false, false, true, false);
  TEST_ASSERT_EQUAL(AP_M68030_ATC_MISS,
                    ap_m68030_atc_lookup(&atc, FC_USER_DATA, 0x00100000, PS_4K,
                                         false, false).status);
}

/* [030] 9.4 B: "set for an entry if a bus error, an invalid descriptor, a
 * supervisor violation, or a limit violation is encountered during the table
 * search ... a subsequent access to the logical address causes the MC68030 to
 * take a bus error exception." Reads fault too, not only writes. */
static void test_a_bus_error_entry_faults_on_any_access(void) {
  ap_m68030_atc_t atc = empty();
  ap_m68030_atc_insert(&atc, FC_SUPERVISOR_DATA, 0x00100000, PS_4K,
                       FRAME(0x00A00000), false, false, true, true);
  TEST_ASSERT_EQUAL(AP_M68030_ATC_FAULT, read_at(&atc, 0x00100000).status);
  TEST_ASSERT_EQUAL(AP_M68030_ATC_FAULT, write_at(&atc, 0x00100000).status);
}

/* [030] 9.4 WP: "a write access or a read-modify-write access ... causes a bus
 * error exception to be taken immediately." A read is unaffected. */
static void test_a_write_protected_entry_faults_only_on_a_write(void) {
  ap_m68030_atc_t atc = empty();
  ap_m68030_atc_insert(&atc, FC_SUPERVISOR_DATA, 0x00100000, PS_4K,
                       FRAME(0x00A00000), true, false, true, false);
  TEST_ASSERT_EQUAL(AP_M68030_ATC_HIT, read_at(&atc, 0x00100000).status);
  TEST_ASSERT_EQUAL(AP_M68030_ATC_FAULT, write_at(&atc, 0x00100000).status);
}

/* A read-modify-write is a write for protection purposes. */
static void test_a_read_modify_write_faults_on_a_write_protected_entry(void) {
  ap_m68030_atc_t atc = empty();
  ap_m68030_atc_insert(&atc, FC_SUPERVISOR_DATA, 0x00100000, PS_4K,
                       FRAME(0x00A00000), true, false, true, false);
  TEST_ASSERT_EQUAL(AP_M68030_ATC_FAULT,
                    ap_m68030_atc_lookup(&atc, FC_SUPERVISOR_DATA, 0x00100000,
                                         PS_4K, false, true).status);
}

/* The timing-relevant one. [030] 9.4 M: "If the M bit is clear and a write
 * access to this logical address is attempted, the MC68030 aborts the access
 * and initiates a table search ... This assures that the first write operation
 * to a page sets the M bit in both the ATC and the page descriptor ... even
 * when a previous read operation to the page had created an entry for that page
 * in the ATC with the M bit clear."
 *
 * So this is a hit that still costs a table search, and it is a distinct status
 * for exactly that reason. */
static void test_a_write_to_a_read_only_populated_entry_forces_a_table_search(void) {
  ap_m68030_atc_t atc = empty();
  /* A read created the entry, so M is clear. */
  ap_m68030_atc_insert(&atc, FC_SUPERVISOR_DATA, 0x00100000, PS_4K,
                       FRAME(0x00A00000), false, false, false, false);

  TEST_ASSERT_EQUAL(AP_M68030_ATC_HIT, read_at(&atc, 0x00100000).status);
  TEST_ASSERT_EQUAL(AP_M68030_ATC_MODIFY, write_at(&atc, 0x00100000).status);
}

/* Once the entry is remade with M set, writes are ordinary hits. */
static void test_a_write_to_an_already_modified_entry_is_an_ordinary_hit(void) {
  ap_m68030_atc_t atc = empty();
  ap_m68030_atc_insert(&atc, FC_SUPERVISOR_DATA, 0x00100000, PS_4K,
                       FRAME(0x00A00000), false, false, true, false);
  TEST_ASSERT_EQUAL(AP_M68030_ATC_HIT, write_at(&atc, 0x00100000).status);
}

/* B outranks WP: an entry with both must report the bus error rather than
 * appearing to be merely write-protected, since B faults reads too. */
static void test_a_bus_error_entry_faults_even_on_a_read_when_also_protected(void) {
  ap_m68030_atc_t atc = empty();
  ap_m68030_atc_insert(&atc, FC_SUPERVISOR_DATA, 0x00100000, PS_4K,
                       FRAME(0x00A00000), true, false, true, true);
  TEST_ASSERT_EQUAL(AP_M68030_ATC_FAULT, read_at(&atc, 0x00100000).status);
}

/* CI travels with the entry and drives CIOUT. */
static void test_cache_inhibit_is_reported_from_the_entry(void) {
  ap_m68030_atc_t atc = empty();
  ap_m68030_atc_insert(&atc, FC_SUPERVISOR_DATA, 0x00100000, PS_4K,
                       FRAME(0x00A00000), false, true, true, false);
  TEST_ASSERT_TRUE(read_at(&atc, 0x00100000).cache_inhibit);
}

/* PFLUSH invalidates a selected entry and leaves the others alone. */
static void test_flushing_one_entry_leaves_the_others(void) {
  ap_m68030_atc_t atc = empty();
  ap_m68030_atc_insert(&atc, FC_SUPERVISOR_DATA, 0x00100000, PS_4K,
                       FRAME(0x00A00000), false, false, true, false);
  ap_m68030_atc_insert(&atc, FC_SUPERVISOR_DATA, 0x00200000, PS_4K,
                       FRAME(0x00B00000), false, false, true, false);

  ap_m68030_atc_flush_entry(&atc, FC_SUPERVISOR_DATA, 0x00100000, PS_4K);
  TEST_ASSERT_EQUAL(AP_M68030_ATC_MISS, read_at(&atc, 0x00100000).status);
  TEST_ASSERT_EQUAL(AP_M68030_ATC_HIT, read_at(&atc, 0x00200000).status);
}

/* "If possible, when the ATC stores a new address translation, it replaces an
 * entry that is no longer valid." So filling the cache from empty must use
 * every entry before evicting any. */
static void test_all_entries_are_used_before_any_is_evicted(void) {
  ap_m68030_atc_t atc = empty();
  for (unsigned i = 0; i < AP_M68030_ATC_ENTRIES; i++) {
    ap_m68030_atc_insert(&atc, FC_SUPERVISOR_DATA, i * 0x1000u, PS_4K,
                         FRAME(0x00A00000 + i * 0x1000u), false, false, true,
                         false);
  }
  for (unsigned i = 0; i < AP_M68030_ATC_ENTRIES; i++) {
    TEST_ASSERT_EQUAL(AP_M68030_ATC_HIT, read_at(&atc, i * 0x1000u).status);
  }
}

/* Inserting the same address twice must not leave two entries for one tag: a
 * fully associative cache with a duplicated tag could answer from either, so
 * the translation would depend on search order. */
static void test_reinserting_an_address_replaces_rather_than_duplicates(void) {
  ap_m68030_atc_t atc = empty();
  ap_m68030_atc_insert(&atc, FC_SUPERVISOR_DATA, 0x00100000, PS_4K,
                       FRAME(0x00A00000), false, false, true, false);
  ap_m68030_atc_insert(&atc, FC_SUPERVISOR_DATA, 0x00100000, PS_4K,
                       FRAME(0x00C00000), false, false, true, false);

  ap_m68030_atc_result_t r = read_at(&atc, 0x00100000);
  TEST_ASSERT_EQUAL(AP_M68030_ATC_HIT, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x00C00000, r.physical);

  unsigned matches = 0;
  for (unsigned i = 0; i < AP_M68030_ATC_ENTRIES; i++) {
    if (atc.entry[i].valid && atc.entry[i].logical == (0x00100000u >> 8)) {
      matches++;
    }
  }
  TEST_ASSERT_EQUAL_UINT(1, matches);
}

/* A full cache still admits new translations -- the replacement policy is
 * PROVISIONAL in *which* entry it picks, but that it picks one is not. */
static void test_a_full_cache_still_admits_a_new_translation(void) {
  ap_m68030_atc_t atc = empty();
  for (unsigned i = 0; i < AP_M68030_ATC_ENTRIES; i++) {
    ap_m68030_atc_insert(&atc, FC_SUPERVISOR_DATA, i * 0x1000u, PS_4K,
                         FRAME(0x00A00000 + i * 0x1000u), false, false, true,
                         false);
  }
  ap_m68030_atc_insert(&atc, FC_SUPERVISOR_DATA, 0x00F00000, PS_4K,
                       FRAME(0x00D00000), false, false, true, false);
  TEST_ASSERT_EQUAL(AP_M68030_ATC_HIT, read_at(&atc, 0x00F00000).status);
}

/* The cache is exactly 22 entries. Asserted rather than assumed, because the
 * size is what makes a hit-rate measurement comparable with the real part. */
static void test_the_cache_has_twenty_two_entries(void) {
  TEST_ASSERT_EQUAL_UINT(22, AP_M68030_ATC_ENTRIES);
}

/* The history bit means "recently used", not "recently inserted". The
 * `MC68851 PMMU User's Manual` §5.2.1.3 describes the compatible ATC's second
 * bit as "a history bit to indicate that the entry has been recently used" --
 * the MC68030's own §9.4 names the bit without saying what sets it.
 *
 * The difference is behavioural, not cosmetic: without marking on a hit, an
 * entry translated a thousand times but never reloaded is evicted as though
 * untouched, which is the opposite of what a least-recently-used policy exists
 * to do. */
static void test_a_hit_marks_the_entry_recently_used(void) {
  ap_m68030_atc_t atc;
  ap_m68030_atc_flush(&atc);

  const int first = ap_m68030_atc_insert(&atc, FC_SUPERVISOR_DATA, 0x00100000,
                                         PS_4K, 0x00900000, false, false, false,
                                         false);
  /* Clear the history the insert set, so what follows is the *hit's* doing. */
  atc.entry[first].history = false;

  const ap_m68030_atc_result_t hit =
      ap_m68030_atc_lookup(&atc, FC_SUPERVISOR_DATA, 0x00100000, PS_4K, false,
                           false);
  TEST_ASSERT_EQUAL_INT(AP_M68030_ATC_HIT, hit.status);

  /* The lookup alone does not mark: that is what keeps PTEST from perturbing
   * the state it reports. */
  TEST_ASSERT_FALSE(atc.entry[first].history);

  ap_m68030_atc_mark_used(&atc, hit.index);
  TEST_ASSERT_TRUE(atc.entry[first].history);
}

/* A miss reports index -1, and marking nothing is the right answer rather than
 * an out-of-range write. */
static void test_marking_a_miss_touches_nothing(void) {
  ap_m68030_atc_t atc;
  ap_m68030_atc_flush(&atc);
  (void)ap_m68030_atc_insert(&atc, FC_SUPERVISOR_DATA, 0x00100000, PS_4K,
                             0x00900000, false, false, false, false);

  const ap_m68030_atc_result_t miss =
      ap_m68030_atc_lookup(&atc, FC_SUPERVISOR_DATA, 0x00200000, PS_4K, false,
                           false);
  TEST_ASSERT_EQUAL_INT(AP_M68030_ATC_MISS, miss.status);
  TEST_ASSERT_EQUAL_INT(-1, miss.index);

  ap_m68030_atc_mark_used(&atc, miss.index); /* must not fault or touch */
  ap_m68030_atc_mark_used(&atc, (int)AP_M68030_ATC_ENTRIES);
}

/* And the consequence: an entry that keeps being hit survives a sweep that
 * evicts one that does not. This is the property the bit exists for, and the
 * one that was absent while only inserts marked. */
static void test_a_repeatedly_hit_entry_outlives_an_idle_one(void) {
  ap_m68030_atc_t atc;
  ap_m68030_atc_flush(&atc);

  /* Fill every entry, then clear the history so none looks recently used. */
  for (unsigned i = 0; i < AP_M68030_ATC_ENTRIES; i++) {
    (void)ap_m68030_atc_insert(&atc, FC_SUPERVISOR_DATA,
                               (uint32_t)(0x00100000u + i * 0x1000u), PS_4K,
                               0x00900000u, false, false, false, false);
  }
  for (unsigned i = 0; i < AP_M68030_ATC_ENTRIES; i++) {
    atc.entry[i].history = false;
  }

  /* Hit the first entry, which marks it. */
  const ap_m68030_atc_result_t hit =
      ap_m68030_atc_lookup(&atc, FC_SUPERVISOR_DATA, 0x00100000, PS_4K, false,
                           false);
  TEST_ASSERT_EQUAL_INT(AP_M68030_ATC_HIT, hit.status);
  ap_m68030_atc_mark_used(&atc, hit.index);

  /* The next insert must not choose it: it is the one entry that says it has
   * been used. */
  const int victim = ap_m68030_atc_insert(&atc, FC_SUPERVISOR_DATA, 0x00900000,
                                          PS_4K, 0x00A00000, false, false,
                                          false, false);
  TEST_ASSERT_NOT_EQUAL_INT(hit.index, victim);
}


/* The replacement *order*, which `MC68851 PMMU User's Manual` §5.2.1.3 states
 * for the compatible ATC: "locate an invalid entry and use it. If no invalid
 * entries are found, use a psuedo least-recently-used (LRU) algorithm".
 *
 * So an invalid entry is taken before any valid one, whatever the history bits
 * say -- a model that consulted history first would evict a live translation
 * while an empty slot sat beside it. Checked by marking *every* entry used and
 * then invalidating one in the middle: history offers no candidate at all, and
 * the invalid entry must still be the one chosen. */
static void test_an_invalid_entry_is_taken_before_any_valid_one(void) {
  ap_m68030_atc_t atc;
  ap_m68030_atc_flush(&atc);

  for (unsigned i = 0; i < AP_M68030_ATC_ENTRIES; i++) {
    const int index = ap_m68030_atc_insert(&atc, 1u, 0x10000u + i * 0x1000u,
                                           12u, 0x20000u + i * 0x1000u, false,
                                           false, false, false);
    TEST_ASSERT_TRUE(index >= 0);
    ap_m68030_atc_mark_used(&atc, index);
  }

  /* One hole, and it is not the first entry -- so "the first invalid" and "the
   * first entry" are different answers and the test can tell them apart. */
  const unsigned hole = 7u;
  const uint32_t hole_logical = 0x10000u + hole * 0x1000u;
  ap_m68030_atc_flush_entry(&atc, 1u, hole_logical, 12u);

  const int chosen = ap_m68030_atc_insert(&atc, 1u, 0x90000u, 12u, 0xA0000u,
                                          false, false, false, false);
  TEST_ASSERT_TRUE(chosen >= 0);
  TEST_ASSERT_EQUAL_UINT(hole, (unsigned)chosen);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_an_invalid_entry_is_taken_before_any_valid_one);
  RUN_TEST(test_a_flushed_cache_misses_everything);
  RUN_TEST(test_a_hit_merges_the_page_offset_into_the_frame);
  RUN_TEST(test_every_address_in_a_page_hits_the_same_entry);
  RUN_TEST(test_the_page_size_decides_how_much_of_the_tag_is_compared);
  RUN_TEST(test_the_function_code_is_part_of_the_tag);
  RUN_TEST(test_a_bus_error_entry_faults_on_any_access);
  RUN_TEST(test_a_write_protected_entry_faults_only_on_a_write);
  RUN_TEST(test_a_read_modify_write_faults_on_a_write_protected_entry);
  RUN_TEST(test_a_write_to_a_read_only_populated_entry_forces_a_table_search);
  RUN_TEST(test_a_write_to_an_already_modified_entry_is_an_ordinary_hit);
  RUN_TEST(test_a_bus_error_entry_faults_even_on_a_read_when_also_protected);
  RUN_TEST(test_cache_inhibit_is_reported_from_the_entry);
  RUN_TEST(test_flushing_one_entry_leaves_the_others);
  RUN_TEST(test_all_entries_are_used_before_any_is_evicted);
  RUN_TEST(test_reinserting_an_address_replaces_rather_than_duplicates);
  RUN_TEST(test_a_full_cache_still_admits_a_new_translation);
  RUN_TEST(test_the_cache_has_twenty_two_entries);
  RUN_TEST(test_a_hit_marks_the_entry_recently_used);
  RUN_TEST(test_marking_a_miss_touches_nothing);
  RUN_TEST(test_a_repeatedly_hit_entry_outlives_an_idle_one);
  return UNITY_END();
}
