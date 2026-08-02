/* MC68851 address translation cache, `[68851]` §5.2 and Figures 5-21 and 5-22.
 *
 * The figures give named fields and no bit numbers -- "the information
 * contained in the ATC is not directly accessible to the programmer" -- so
 * there is no layout to check. What there is to check is behaviour: the
 * three-part match rule, the replacement order, and the lock ceiling.
 */

#include <string.h>

#include "cpu/m68851/ap_m68851_atc.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define PAGE_4K 4096u
#define PAGE_256 256u

static ap_m68851_atc_entry_t mapping(uint32_t logical, uint32_t physical,
                                     unsigned fc, unsigned task) {
  return (ap_m68851_atc_entry_t){
      .logical_address = logical,
      .physical_address = physical,
      .function_code = fc,
      .task_alias = task,
      .valid = true,
  };
}

static ap_m68851_atc_t empty_atc(unsigned task_alias) {
  ap_m68851_atc_t atc;
  memset(&atc, 0, sizeof atc);
  atc.task_alias = task_alias;
  return atc;
}

/* ---------------------------------------------------------------------------
 * The match rule.
 * ------------------------------------------------------------------------- */

static void test_the_cache_holds_64_entries(void) {
  /* "There are 64 entries in the CAM array and 64 corresponding entries in the
   * RAM array." */
  TEST_ASSERT_EQUAL_UINT(64u, AP_M68851_ATC_ENTRIES);
}

static void test_a_matching_entry_is_found_anywhere_in_the_cache(void) {
  /* Fully associative: no index, so an entry matches from any slot. Putting the
   * mapping in the last slot proves the search is not an indexed lookup. */
  ap_m68851_atc_t atc = empty_atc(1u);
  atc.entry[AP_M68851_ATC_ENTRIES - 1u] = mapping(0x10000u, 0x90000u, 5u, 1u);

  const ap_m68851_atc_entry_t *hit =
      ap_m68851_atc_lookup(&atc, 0x10000u, 5u, PAGE_4K);
  TEST_ASSERT_NOT_NULL(hit);
  TEST_ASSERT_EQUAL_HEX32(0x90000u, hit->physical_address);
}

static void test_an_invalid_entry_never_matches(void) {
  ap_m68851_atc_t atc = empty_atc(1u);
  atc.entry[0] = mapping(0x10000u, 0x90000u, 5u, 1u);
  atc.entry[0].valid = false;
  TEST_ASSERT_NULL(ap_m68851_atc_lookup(&atc, 0x10000u, 5u, PAGE_4K));
}

static void test_the_page_offset_is_excluded_from_the_comparison(void) {
  /* "The lower order bits of the logical address field are ignored during
   * compare operations if the page size is larger than 256 bytes." Every
   * address within the page hits the one entry -- that is what makes it a page
   * translation rather than an address translation. */
  ap_m68851_atc_t atc = empty_atc(1u);
  atc.entry[0] = mapping(0x10000u, 0x90000u, 5u, 1u);

  TEST_ASSERT_NOT_NULL(ap_m68851_atc_lookup(&atc, 0x10000u, 5u, PAGE_4K));
  TEST_ASSERT_NOT_NULL(ap_m68851_atc_lookup(&atc, 0x10FFFu, 5u, PAGE_4K));
  /* One byte past the page: a different page, so a miss. */
  TEST_ASSERT_NULL(ap_m68851_atc_lookup(&atc, 0x11000u, 5u, PAGE_4K));
}

static void test_the_current_page_size_decides_what_an_entry_covers(void) {
  /* The offset excluded is the *current* page size, not one stored in the
   * entry -- so the same entry covers more or less ground as `TC` changes,
   * which is why writing `TC` flushes the cache. */
  ap_m68851_atc_t atc = empty_atc(1u);
  atc.entry[0] = mapping(0x10000u, 0x90000u, 5u, 1u);

  TEST_ASSERT_NOT_NULL(ap_m68851_atc_lookup(&atc, 0x10800u, 5u, PAGE_4K));
  /* With 256-byte pages the same address is outside the entry's page. */
  TEST_ASSERT_NULL(ap_m68851_atc_lookup(&atc, 0x10800u, 5u, PAGE_256));
}

static void test_the_function_code_must_match_exactly(void) {
  /* All of it, not just the supervisor bit: the DMA root pointer maps other
   * function codes, and a partial comparison would let a DMA translation
   * answer a CPU access. */
  ap_m68851_atc_t atc = empty_atc(1u);
  atc.entry[0] = mapping(0x10000u, 0x90000u, 5u, 1u);

  TEST_ASSERT_NOT_NULL(ap_m68851_atc_lookup(&atc, 0x10000u, 5u, PAGE_4K));
  for (unsigned fc = 0; fc < 8u; fc++) {
    if (fc == 5u) {
      continue;
    }
    TEST_ASSERT_NULL(ap_m68851_atc_lookup(&atc, 0x10000u, fc, PAGE_4K));
  }
}

static void test_an_entry_for_another_task_does_not_match(void) {
  /* "The task alias (TA) field must match the current TA value." This is what
   * lets entries for several tasks be resident at once without one task
   * translating through another's mapping. */
  ap_m68851_atc_t atc = empty_atc(1u);
  atc.entry[0] = mapping(0x10000u, 0x90000u, 5u, 2u);
  TEST_ASSERT_NULL(ap_m68851_atc_lookup(&atc, 0x10000u, 5u, PAGE_4K));

  atc.task_alias = 2u;
  TEST_ASSERT_NOT_NULL(ap_m68851_atc_lookup(&atc, 0x10000u, 5u, PAGE_4K));
}

static void test_a_globally_shared_entry_matches_every_task(void) {
  /* "...or the entry's SG bit must be set in order for a match to occur." One
   * entry serves all tasks, which is the performance reason a root pointer
   * carries SG at all. */
  ap_m68851_atc_t atc = empty_atc(1u);
  atc.entry[0] = mapping(0x10000u, 0x90000u, 5u, 7u);
  atc.entry[0].shared_globally = true;

  for (unsigned task = 0; task < 8u; task++) {
    atc.task_alias = task;
    TEST_ASSERT_NOT_NULL(ap_m68851_atc_lookup(&atc, 0x10000u, 5u, PAGE_4K));
  }
}

/* ---------------------------------------------------------------------------
 * Flushing.
 * ------------------------------------------------------------------------- */

static void test_a_flush_invalidates_everything(void) {
  ap_m68851_atc_t atc = empty_atc(1u);
  for (unsigned i = 0; i < AP_M68851_ATC_ENTRIES; i++) {
    atc.entry[i] = mapping(0x10000u + i * PAGE_4K, 0x90000u, 5u, 1u);
  }
  ap_m68851_atc_flush(&atc);
  TEST_ASSERT_NULL(ap_m68851_atc_lookup(&atc, 0x10000u, 5u, PAGE_4K));
}

static void test_a_flush_releases_locks(void) {
  /* An invalid entry is the replacement algorithm's first choice, so a lock
   * left behind would exempt a slot that holds nothing -- the cache would
   * shrink permanently after a flush. */
  ap_m68851_atc_t atc = empty_atc(1u);
  for (unsigned i = 0; i < AP_M68851_ATC_ENTRIES; i++) {
    atc.entry[i] = mapping(0x10000u + i * PAGE_4K, 0x90000u, 5u, 1u);
    atc.entry[i].lock = true;
  }
  ap_m68851_atc_flush(&atc);
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68851_atc_locked_count(&atc));
  TEST_ASSERT_TRUE(ap_m68851_atc_may_lock(&atc));
}

static void test_a_task_flush_spares_globally_shared_entries(void) {
  /* A write to `CRP` flushes one task's entries, and `PCSR`'s F bit reports
   * which. A globally shared entry belongs to every task, so it survives. */
  ap_m68851_atc_t atc = empty_atc(1u);
  atc.entry[0] = mapping(0x10000u, 0x90000u, 5u, 1u);
  atc.entry[1] = mapping(0x20000u, 0xA0000u, 5u, 1u);
  atc.entry[1].shared_globally = true;
  atc.entry[2] = mapping(0x30000u, 0xB0000u, 5u, 2u);

  ap_m68851_atc_flush_task(&atc, 1u);

  TEST_ASSERT_NULL(ap_m68851_atc_lookup(&atc, 0x10000u, 5u, PAGE_4K));
  TEST_ASSERT_NOT_NULL(ap_m68851_atc_lookup(&atc, 0x20000u, 5u, PAGE_4K));
  /* Another task's entry is untouched -- it was not this flush's business. */
  TEST_ASSERT_TRUE(atc.entry[2].valid);
}

/* ---------------------------------------------------------------------------
 * Replacement.
 * ------------------------------------------------------------------------- */

static void test_an_invalid_entry_is_chosen_first(void) {
  /* "Locate an invalid entry and use it." Before any LRU consideration, so a
   * cold cache fills up before it starts evicting. */
  ap_m68851_atc_t atc = empty_atc(1u);
  for (unsigned i = 0; i < AP_M68851_ATC_ENTRIES; i++) {
    atc.entry[i] = mapping(0x10000u + i * PAGE_4K, 0x90000u, 5u, 1u);
    atc.entry[i].history = true;
  }
  atc.entry[42].valid = false;
  TEST_ASSERT_EQUAL_UINT(42u, ap_m68851_atc_select_victim(&atc));
}

static void test_an_unused_entry_is_chosen_over_a_recently_used_one(void) {
  /* The pseudo-LRU: "select an entry without its L bit set" using the history
   * bit, which says whether the entry "has been recently used". */
  ap_m68851_atc_t atc = empty_atc(1u);
  for (unsigned i = 0; i < AP_M68851_ATC_ENTRIES; i++) {
    atc.entry[i] = mapping(0x10000u + i * PAGE_4K, 0x90000u, 5u, 1u);
    atc.entry[i].history = true;
  }
  atc.entry[17].history = false;
  TEST_ASSERT_EQUAL_UINT(17u, ap_m68851_atc_select_victim(&atc));
}

static void test_a_locked_entry_is_never_chosen(void) {
  /* "The internal L bit exempts the entry from replacement." Even when it is
   * the only entry not recently used. */
  ap_m68851_atc_t atc = empty_atc(1u);
  for (unsigned i = 0; i < AP_M68851_ATC_ENTRIES; i++) {
    atc.entry[i] = mapping(0x10000u + i * PAGE_4K, 0x90000u, 5u, 1u);
    atc.entry[i].history = true;
  }
  atc.entry[17].history = false;
  atc.entry[17].lock = true;
  TEST_ASSERT_NOT_EQUAL_UINT(17u, ap_m68851_atc_select_victim(&atc));
}

static void test_a_touch_marks_an_entry_recently_used(void) {
  ap_m68851_atc_t atc = empty_atc(1u);
  atc.entry[3] = mapping(0x10000u, 0x90000u, 5u, 1u);
  TEST_ASSERT_FALSE(atc.entry[3].history);
  ap_m68851_atc_touch(&atc, 3u);
  TEST_ASSERT_TRUE(atc.entry[3].history);
}

static void test_a_full_cache_of_recently_used_entries_still_yields_a_victim(void) {
  /* Pseudo-LRU, not true LRU: the history bits are one generation rather than
   * an ordering, so when every unlocked entry is marked used the cache cannot
   * tell which was used longest ago. It must still choose one -- a cache that
   * refused would stall the translation entirely. */
  ap_m68851_atc_t atc = empty_atc(1u);
  for (unsigned i = 0; i < AP_M68851_ATC_ENTRIES; i++) {
    atc.entry[i] = mapping(0x10000u + i * PAGE_4K, 0x90000u, 5u, 1u);
    atc.entry[i].history = true;
  }
  const unsigned victim = ap_m68851_atc_select_victim(&atc);
  TEST_ASSERT_TRUE(victim < AP_M68851_ATC_ENTRIES);
  TEST_ASSERT_FALSE(atc.entry[victim].lock);
}

static void test_a_fill_starts_a_new_generation_when_all_are_used(void) {
  /* Having exhausted the generation, the next fill clears the history so the
   * following eviction has something to prefer. Without this the cache would
   * evict the same slot forever. */
  ap_m68851_atc_t atc = empty_atc(1u);
  for (unsigned i = 0; i < AP_M68851_ATC_ENTRIES; i++) {
    atc.entry[i] = mapping(0x10000u + i * PAGE_4K, 0x90000u, 5u, 1u);
    atc.entry[i].history = true;
  }
  ap_m68851_atc_fill(&atc, 5u, mapping(0x80000u, 0xC0000u, 5u, 1u));

  /* The freshly filled entry counts as used; the others have been reset. */
  TEST_ASSERT_TRUE(atc.entry[5].history);
  unsigned unused = 0;
  for (unsigned i = 0; i < AP_M68851_ATC_ENTRIES; i++) {
    if (!atc.entry[i].history) {
      unused++;
    }
  }
  TEST_ASSERT_EQUAL_UINT(AP_M68851_ATC_ENTRIES - 1u, unused);
}

/* ---------------------------------------------------------------------------
 * The lock ceiling.
 * ------------------------------------------------------------------------- */

static void test_the_lock_ceiling_leaves_one_entry_replaceable(void) {
  /* "It will not be a copy of the page descriptor L bit if there are already 63
   * entries with set L bits in the ATC." Sixty-three of sixty-four, so the
   * cache can never deadlock against its own locks. */
  TEST_ASSERT_EQUAL_UINT(AP_M68851_ATC_ENTRIES - 1u,
                         AP_M68851_ATC_LOCK_CEILING);
}

static void test_a_fill_at_the_ceiling_silently_drops_the_lock(void) {
  /* "In this case, the L bit for new entries will always be clear (indicating
   * that the entry can be replaced)." The descriptor still asks for a lock and
   * the cache still refuses -- no fault, just a cleared bit. */
  ap_m68851_atc_t atc = empty_atc(1u);
  for (unsigned i = 0; i < AP_M68851_ATC_LOCK_CEILING; i++) {
    atc.entry[i] = mapping(0x10000u + i * PAGE_4K, 0x90000u, 5u, 1u);
    atc.entry[i].lock = true;
  }
  TEST_ASSERT_FALSE(ap_m68851_atc_may_lock(&atc));

  ap_m68851_atc_entry_t wants_lock = mapping(0x80000u, 0xC0000u, 5u, 1u);
  wants_lock.lock = true;
  ap_m68851_atc_fill(&atc, 63u, wants_lock);

  TEST_ASSERT_TRUE(atc.entry[63].valid);
  TEST_ASSERT_FALSE(atc.entry[63].lock);
}

static void test_a_fill_below_the_ceiling_keeps_the_lock(void) {
  ap_m68851_atc_t atc = empty_atc(1u);
  ap_m68851_atc_entry_t wants_lock = mapping(0x80000u, 0xC0000u, 5u, 1u);
  wants_lock.lock = true;
  ap_m68851_atc_fill(&atc, 0u, wants_lock);
  TEST_ASSERT_TRUE(atc.entry[0].lock);
}

static void test_the_lock_warning_reports_the_ceiling(void) {
  /* `PCSR`'s LW: "set when all entries in the ATC but one have been locked." */
  ap_m68851_atc_t atc = empty_atc(1u);
  for (unsigned i = 0; i < AP_M68851_ATC_LOCK_CEILING - 1u; i++) {
    atc.entry[i] = mapping(0x10000u + i * PAGE_4K, 0x90000u, 5u, 1u);
    atc.entry[i].lock = true;
  }
  TEST_ASSERT_FALSE(ap_m68851_atc_lock_warning(&atc));

  atc.entry[AP_M68851_ATC_LOCK_CEILING - 1u] =
      mapping(0x70000u, 0x90000u, 5u, 1u);
  atc.entry[AP_M68851_ATC_LOCK_CEILING - 1u].lock = true;
  TEST_ASSERT_TRUE(ap_m68851_atc_lock_warning(&atc));
}

/* ---------------------------------------------------------------------------
 * The B bit.
 * ------------------------------------------------------------------------- */

static void test_a_denial_is_cached_as_a_matching_entry(void) {
  /* "If access is to be denied, an ATC entry is made with the B bit set." So
   * the entry *matches* -- it must, or the denial would not be found -- and it
   * is the B bit rather than the absence of an entry that refuses the access.
   * A model that declined to cache denials would re-walk the tables on every
   * access to a restricted page. */
  ap_m68851_atc_t atc = empty_atc(1u);
  atc.entry[0] = mapping(0x10000u, 0u, 5u, 1u);
  atc.entry[0].bus_error = true;

  const ap_m68851_atc_entry_t *hit =
      ap_m68851_atc_lookup(&atc, 0x10000u, 5u, PAGE_4K);
  TEST_ASSERT_NOT_NULL(hit);
  TEST_ASSERT_TRUE(hit->bus_error);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_cache_holds_64_entries);
  RUN_TEST(test_a_matching_entry_is_found_anywhere_in_the_cache);
  RUN_TEST(test_an_invalid_entry_never_matches);
  RUN_TEST(test_the_page_offset_is_excluded_from_the_comparison);
  RUN_TEST(test_the_current_page_size_decides_what_an_entry_covers);
  RUN_TEST(test_the_function_code_must_match_exactly);
  RUN_TEST(test_an_entry_for_another_task_does_not_match);
  RUN_TEST(test_a_globally_shared_entry_matches_every_task);
  RUN_TEST(test_a_flush_invalidates_everything);
  RUN_TEST(test_a_flush_releases_locks);
  RUN_TEST(test_a_task_flush_spares_globally_shared_entries);
  RUN_TEST(test_an_invalid_entry_is_chosen_first);
  RUN_TEST(test_an_unused_entry_is_chosen_over_a_recently_used_one);
  RUN_TEST(test_a_locked_entry_is_never_chosen);
  RUN_TEST(test_a_touch_marks_an_entry_recently_used);
  RUN_TEST(test_a_full_cache_of_recently_used_entries_still_yields_a_victim);
  RUN_TEST(test_a_fill_starts_a_new_generation_when_all_are_used);
  RUN_TEST(test_the_lock_ceiling_leaves_one_entry_replaceable);
  RUN_TEST(test_a_fill_at_the_ceiling_silently_drops_the_lock);
  RUN_TEST(test_a_fill_below_the_ceiling_keeps_the_lock);
  RUN_TEST(test_the_lock_warning_reports_the_ceiling);
  RUN_TEST(test_a_denial_is_cached_as_a_matching_entry);
  return UNITY_END();
}
