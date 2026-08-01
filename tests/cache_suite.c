/* MC68030 on-chip instruction and data caches.
 *
 * Cited to MC68030 User's Manual 3ed §6.
 *
 * The fact most of these tests exist to protect is that the valid bit is per
 * *entry*, not per line: "The tag field for each line contains a valid bit for
 * each entry in the line; each entry is independently replaceable." Modelling
 * it per line would make a burst fill and a single-entry fill
 * indistinguishable, and those cost very different numbers of bus cycles.
 */

#include "cpu/m68030/ap_m68030_cache.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define FC_SUPERVISOR_DATA 5u
#define FC_USER_DATA 1u

/* Line 1, entry 2: A7-A4 = 1, A3-A2 = 2. */
#define ADDRESS 0x00001018u

static ap_m68030_cache_t empty_cache(void) {
  ap_m68030_cache_t cache = {0};
  ap_m68030_cache_clear(&cache);
  return cache;
}

/* ---------------------------------------------------------------------------
 * Address decomposition, from Figures 6-2 and 6-3.
 * ------------------------------------------------------------------------- */

/* "The cache control circuitry selects the tag using bits A7-A4", and "address
 * bits A3-A2 select the valid bit for the appropriate long word". */
static void test_the_address_splits_into_tag_line_and_long_word(void) {
  TEST_ASSERT_EQUAL_UINT(1, ap_m68030_cache_line_index(0x00001018u));
  TEST_ASSERT_EQUAL_UINT(2, ap_m68030_cache_entry_index(0x00001018u));
  /* A7-A4 wraps at 16 lines: 0x100 is line 0 again. */
  TEST_ASSERT_EQUAL_UINT(0, ap_m68030_cache_line_index(0x00000100u));
  TEST_ASSERT_EQUAL_UINT(15, ap_m68030_cache_line_index(0x000000F0u));
  /* A1-A0 are the byte within the long word and select nothing. */
  TEST_ASSERT_EQUAL_UINT(2, ap_m68030_cache_entry_index(0x0000101Bu));
}

/* The tag is A31-A8 with the function code above it, in both caches. */
static void test_the_function_code_is_part_of_the_tag(void) {
  TEST_ASSERT_NOT_EQUAL(ap_m68030_cache_tag(ADDRESS, FC_SUPERVISOR_DATA),
                        ap_m68030_cache_tag(ADDRESS, FC_USER_DATA));
  /* Addresses differing only below A8 share a tag -- that is the line. */
  TEST_ASSERT_EQUAL_HEX32(ap_m68030_cache_tag(0x00001000u, FC_USER_DATA),
                          ap_m68030_cache_tag(0x000010FCu, FC_USER_DATA));
}

/* ---------------------------------------------------------------------------
 * Lookup and fill.
 * ------------------------------------------------------------------------- */

static void test_a_filled_entry_hits_and_returns_its_long_word(void) {
  ap_m68030_cache_t cache = empty_cache();
  ap_m68030_cache_fill_entry(&cache, ADDRESS, FC_SUPERVISOR_DATA, 0xDEADBEEF);

  uint32_t value = 0;
  TEST_ASSERT_TRUE(
      ap_m68030_cache_lookup(&cache, ADDRESS, FC_SUPERVISOR_DATA, &value));
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, value);
}

/* A different function code is a different entry, which is what lets the caches
 * survive a supervisor/user switch without a flush. */
static void test_the_same_address_in_another_space_misses(void) {
  ap_m68030_cache_t cache = empty_cache();
  ap_m68030_cache_fill_entry(&cache, ADDRESS, FC_SUPERVISOR_DATA, 0xDEADBEEF);

  uint32_t value = 0;
  TEST_ASSERT_FALSE(
      ap_m68030_cache_lookup(&cache, ADDRESS, FC_USER_DATA, &value));
}

/* Filling one entry must not validate its three neighbours. This is the test
 * that fails if validity is modelled per line. */
static void test_filling_one_entry_leaves_the_others_invalid(void) {
  ap_m68030_cache_t cache = empty_cache();
  ap_m68030_cache_fill_entry(&cache, ADDRESS, FC_SUPERVISOR_DATA, 0x11111111);

  uint32_t value = 0;
  /* Same line (A7-A4 = 1), the other three long words. */
  TEST_ASSERT_FALSE(ap_m68030_cache_lookup(&cache, 0x00001010u,
                                           FC_SUPERVISOR_DATA, &value));
  TEST_ASSERT_FALSE(ap_m68030_cache_lookup(&cache, 0x00001014u,
                                           FC_SUPERVISOR_DATA, &value));
  TEST_ASSERT_FALSE(ap_m68030_cache_lookup(&cache, 0x0000101Cu,
                                           FC_SUPERVISOR_DATA, &value));
}

/* A burst "replace[s] an entire cache line", so all four entries become valid
 * from one operation -- the difference a per-line valid bit would erase. */
static void test_a_burst_fill_validates_the_whole_line(void) {
  ap_m68030_cache_t cache = empty_cache();
  const uint32_t values[AP_M68030_CACHE_ENTRIES] = {0xA0, 0xA1, 0xA2, 0xA3};
  ap_m68030_cache_fill_line(&cache, ADDRESS, FC_SUPERVISOR_DATA, values);

  uint32_t value = 0;
  for (unsigned e = 0; e < AP_M68030_CACHE_ENTRIES; e++) {
    const uint32_t address = 0x00001010u + (e * 4u);
    TEST_ASSERT_TRUE(
        ap_m68030_cache_lookup(&cache, address, FC_SUPERVISOR_DATA, &value));
    TEST_ASSERT_EQUAL_HEX32(0xA0 + e, value);
  }
}

/* A line holds one tag, so filling an entry whose tag differs must invalidate
 * the rest of the line -- they described a different address. */
static void test_a_tag_change_invalidates_the_rest_of_the_line(void) {
  ap_m68030_cache_t cache = empty_cache();
  const uint32_t values[AP_M68030_CACHE_ENTRIES] = {0xA0, 0xA1, 0xA2, 0xA3};
  ap_m68030_cache_fill_line(&cache, ADDRESS, FC_SUPERVISOR_DATA, values);

  /* Same line index (A7-A4 = 1), different tag (A31-A8 differs). */
  ap_m68030_cache_fill_entry(&cache, 0x00002018u, FC_SUPERVISOR_DATA, 0xBEEF);

  uint32_t value = 0;
  TEST_ASSERT_TRUE(
      ap_m68030_cache_lookup(&cache, 0x00002018u, FC_SUPERVISOR_DATA, &value));
  TEST_ASSERT_EQUAL_HEX32(0xBEEF, value);
  /* The old occupants are gone, not merely shadowed. */
  TEST_ASSERT_FALSE(ap_m68030_cache_lookup(&cache, 0x00001010u,
                                           FC_SUPERVISOR_DATA, &value));
}

/* ---------------------------------------------------------------------------
 * Clearing, §6.3.1.3 and §6.3.1.4.
 * ------------------------------------------------------------------------- */

/* "The processor clears only the specified long word by clearing the valid bit
 * for the entry", so its neighbours survive. */
static void test_clearing_one_entry_leaves_the_rest_of_the_line(void) {
  ap_m68030_cache_t cache = empty_cache();
  const uint32_t values[AP_M68030_CACHE_ENTRIES] = {0xA0, 0xA1, 0xA2, 0xA3};
  ap_m68030_cache_fill_line(&cache, ADDRESS, FC_SUPERVISOR_DATA, values);

  ap_m68030_cache_clear_entry(&cache, ADDRESS);

  uint32_t value = 0;
  TEST_ASSERT_FALSE(
      ap_m68030_cache_lookup(&cache, ADDRESS, FC_SUPERVISOR_DATA, &value));
  TEST_ASSERT_TRUE(
      ap_m68030_cache_lookup(&cache, 0x00001010u, FC_SUPERVISOR_DATA, &value));
}

/* ---------------------------------------------------------------------------
 * Writes: the data cache is writethrough, §6.1.2 and §6.1.2.1.
 * ------------------------------------------------------------------------- */

/* "When a hit occurs on a write cycle, the data is written both to the cache
 * and to external memory ... even if the cache is frozen." Freeze stops
 * replacement, not updating -- an easy one to get backwards. */
static void test_a_write_hit_updates_the_entry_even_when_frozen(void) {
  ap_m68030_cache_t cache = empty_cache();
  ap_m68030_cache_fill_entry(&cache, ADDRESS, FC_SUPERVISOR_DATA, 0x11111111);

  const ap_m68030_cache_write_t result =
      ap_m68030_cache_write(&cache, ADDRESS, FC_SUPERVISOR_DATA, 0x22222222,
                            true, false /* WA */, true /* frozen */, 4u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_CACHE_WRITE_HIT, result);
  uint32_t value = 0;
  TEST_ASSERT_TRUE(
      ap_m68030_cache_lookup(&cache, ADDRESS, FC_SUPERVISOR_DATA, &value));
  TEST_ASSERT_EQUAL_HEX32(0x22222222, value);
}

/* "write cycles that miss do not alter the data cache contents" when WA = 0. */
static void test_a_write_miss_without_allocation_leaves_the_cache_alone(void) {
  ap_m68030_cache_t cache = empty_cache();

  const ap_m68030_cache_write_t result =
      ap_m68030_cache_write(&cache, ADDRESS, FC_SUPERVISOR_DATA, 0x22222222,
                            true, false /* WA */, false, 4u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_CACHE_WRITE_UNTOUCHED, result);
  uint32_t value = 0;
  TEST_ASSERT_FALSE(
      ap_m68030_cache_lookup(&cache, ADDRESS, FC_SUPERVISOR_DATA, &value));
}

/* WA = 1 and an aligned long word: "the corresponding tag is replaced, and only
 * the long word being written is marked as valid. The other three entries in
 * the cache line are invalidated." */
static void test_write_allocation_validates_only_the_long_word_written(void) {
  ap_m68030_cache_t cache = empty_cache();
  const uint32_t values[AP_M68030_CACHE_ENTRIES] = {0xA0, 0xA1, 0xA2, 0xA3};
  /* A different tag on the same line, so the write is a tag miss. */
  ap_m68030_cache_fill_line(&cache, 0x00002010u, FC_SUPERVISOR_DATA, values);

  const ap_m68030_cache_write_t result =
      ap_m68030_cache_write(&cache, ADDRESS, FC_SUPERVISOR_DATA, 0x33333333,
                            true, true /* WA */, false, 4u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_CACHE_WRITE_ALLOCATED, result);
  uint32_t value = 0;
  TEST_ASSERT_TRUE(
      ap_m68030_cache_lookup(&cache, ADDRESS, FC_SUPERVISOR_DATA, &value));
  TEST_ASSERT_EQUAL_HEX32(0x33333333, value);
  /* The other three, and the line's previous occupants, are invalid. */
  TEST_ASSERT_FALSE(ap_m68030_cache_lookup(&cache, 0x00001010u,
                                           FC_SUPERVISOR_DATA, &value));
  TEST_ASSERT_FALSE(ap_m68030_cache_lookup(&cache, 0x00002010u,
                                           FC_SUPERVISOR_DATA, &value));
}

/* The other half of the same rule, and the half the scan ran into one sentence:
 * "on a misaligned long-word write or on a byte or word write, the data is not
 * written in the cache, the tag is unaltered, and the valid bit(s) are
 * cleared." A sub-long-word write can only ever remove information. */
static void test_a_sub_long_word_write_miss_invalidates_rather_than_fills(void) {
  ap_m68030_cache_t cache = empty_cache();
  const uint32_t values[AP_M68030_CACHE_ENTRIES] = {0xA0, 0xA1, 0xA2, 0xA3};
  ap_m68030_cache_fill_line(&cache, 0x00002010u, FC_SUPERVISOR_DATA, values);
  const uint32_t tag_before =
      ap_m68030_cache_tag(0x00002010u, FC_SUPERVISOR_DATA);

  const ap_m68030_cache_write_t result = ap_m68030_cache_write(
      &cache, ADDRESS, FC_SUPERVISOR_DATA, 0x44444444,
      false /* not an aligned long word */, true /* WA */, false, 2u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_CACHE_WRITE_INVALIDATED, result);
  uint32_t value = 0;
  /* Nothing was written for the new address... */
  TEST_ASSERT_FALSE(
      ap_m68030_cache_lookup(&cache, ADDRESS, FC_SUPERVISOR_DATA, &value));
  /* ...the tag is unaltered, so the line still belongs to the old address... */
  TEST_ASSERT_EQUAL_HEX32(tag_before, cache.line[1].tag);
  /* ...but the indexed entry's valid bit was cleared. */
  TEST_ASSERT_FALSE(ap_m68030_cache_lookup(&cache, 0x00002018u,
                                           FC_SUPERVISOR_DATA, &value));
  /* Its neighbours are untouched. */
  TEST_ASSERT_TRUE(ap_m68030_cache_lookup(&cache, 0x00002010u,
                                          FC_SUPERVISOR_DATA, &value));
}

/* "If the data cache is disabled or frozen, the WA bit is ignored." */
static void test_a_frozen_cache_ignores_write_allocation(void) {
  ap_m68030_cache_t cache = empty_cache();

  const ap_m68030_cache_write_t result =
      ap_m68030_cache_write(&cache, ADDRESS, FC_SUPERVISOR_DATA, 0x55555555,
                            true, true /* WA */, true /* frozen */, 4u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_CACHE_WRITE_UNTOUCHED, result);
}

/* ---------------------------------------------------------------------------
 * CACR, §6.3.1.
 * ------------------------------------------------------------------------- */

static void test_each_cacr_field_packs_to_its_documented_bit(void) {
  TEST_ASSERT_EQUAL_HEX32(
      0x2000, ap_m68030_cacr_pack(&(ap_m68030_cacr_t){.write_allocate = true}));
  TEST_ASSERT_EQUAL_HEX32(
      0x1000,
      ap_m68030_cacr_pack(&(ap_m68030_cacr_t){.data_burst_enable = true}));
  TEST_ASSERT_EQUAL_HEX32(
      0x0200, ap_m68030_cacr_pack(&(ap_m68030_cacr_t){.freeze_data = true}));
  TEST_ASSERT_EQUAL_HEX32(
      0x0100, ap_m68030_cacr_pack(&(ap_m68030_cacr_t){.enable_data = true}));
  TEST_ASSERT_EQUAL_HEX32(
      0x0010,
      ap_m68030_cacr_pack(&(ap_m68030_cacr_t){.instruction_burst_enable = true}));
  TEST_ASSERT_EQUAL_HEX32(
      0x0002,
      ap_m68030_cacr_pack(&(ap_m68030_cacr_t){.freeze_instruction = true}));
  TEST_ASSERT_EQUAL_HEX32(
      0x0001,
      ap_m68030_cacr_pack(&(ap_m68030_cacr_t){.enable_instruction = true}));
}

/* CD, CED, CI and CEI "are always read as zero", so writing them must not make
 * them readable -- they are actions, not state. */
static void test_the_clear_bits_never_read_back(void) {
  ap_m68030_cacr_t cacr = {0};
  ap_m68030_cache_t icache = empty_cache();
  ap_m68030_cache_t dcache = empty_cache();

  ap_m68030_cacr_write(&cacr, 0x0C0Cu, &icache, &dcache, 0);
  TEST_ASSERT_EQUAL_HEX32(0, ap_m68030_cacr_pack(&cacr) & 0x0C0Cu);
}

/* "The processor clears all valid bits in the [instruction] cache at the time a
 * MOVEC instruction loads a one into the CI bit", and CD does the same for the
 * data cache -- each touching only its own. */
static void test_clearing_one_cache_leaves_the_other(void) {
  ap_m68030_cacr_t cacr = {0};
  ap_m68030_cache_t icache = empty_cache();
  ap_m68030_cache_t dcache = empty_cache();
  ap_m68030_cache_fill_entry(&icache, ADDRESS, FC_SUPERVISOR_DATA, 0x11111111);
  ap_m68030_cache_fill_entry(&dcache, ADDRESS, FC_SUPERVISOR_DATA, 0xD1D1D1D1);

  ap_m68030_cacr_write(&cacr, UINT32_C(1) << AP_M68030_CACR_CI_BIT, &icache,
                       &dcache, 0);

  uint32_t value = 0;
  TEST_ASSERT_FALSE(
      ap_m68030_cache_lookup(&icache, ADDRESS, FC_SUPERVISOR_DATA, &value));
  TEST_ASSERT_TRUE(
      ap_m68030_cache_lookup(&dcache, ADDRESS, FC_SUPERVISOR_DATA, &value));
}

/* CEI clears the single entry the CAAR names, "regardless of the states of the
 * EI and FI bits". */
static void test_clear_entry_uses_the_caar_index(void) {
  ap_m68030_cacr_t cacr = {0};
  ap_m68030_cache_t icache = empty_cache();
  const uint32_t values[AP_M68030_CACHE_ENTRIES] = {0xA0, 0xA1, 0xA2, 0xA3};
  ap_m68030_cache_fill_line(&icache, ADDRESS, FC_SUPERVISOR_DATA, values);

  ap_m68030_cacr_write(&cacr, UINT32_C(1) << AP_M68030_CACR_CEI_BIT, &icache,
                       NULL, ADDRESS);

  uint32_t value = 0;
  TEST_ASSERT_FALSE(
      ap_m68030_cache_lookup(&icache, ADDRESS, FC_SUPERVISOR_DATA, &value));
  TEST_ASSERT_TRUE(
      ap_m68030_cache_lookup(&icache, 0x00001010u, FC_SUPERVISOR_DATA, &value));
}

/* "Disabling the data cache does not flush the entries. If it is enabled again,
 * the previously valid entries remain valid and can be used." */
static void test_disabling_a_cache_does_not_flush_it(void) {
  ap_m68030_cacr_t cacr = {0};
  ap_m68030_cache_t dcache = empty_cache();
  ap_m68030_cache_fill_entry(&dcache, ADDRESS, FC_SUPERVISOR_DATA, 0x77777777);

  ap_m68030_cacr_write(&cacr, 0, NULL, &dcache, 0); /* ED clear, no clear bits */

  uint32_t value = 0;
  TEST_ASSERT_FALSE(cacr.enable_data);
  TEST_ASSERT_TRUE(
      ap_m68030_cache_lookup(&dcache, ADDRESS, FC_SUPERVISOR_DATA, &value));
}

/* "The assertion of CDIS disables the caches, regardless of the state of the
 * enable bits in CACR", and CIOUT makes the caches "ignored for the access". */
static void test_cdis_and_ciout_override_the_enable_bit(void) {
  TEST_ASSERT_TRUE(ap_m68030_cache_enabled(true, false, false));
  TEST_ASSERT_FALSE(ap_m68030_cache_enabled(true, true, false));
  TEST_ASSERT_FALSE(ap_m68030_cache_enabled(true, false, true));
  TEST_ASSERT_FALSE(ap_m68030_cache_enabled(false, false, false));
}


/* ---------------------------------------------------------------------------
 * CBREQ: whether a miss asks for a whole line, [030] 7.3.7. This is the cache's
 * half of the bus-timing join -- a burst costs 5 clocks against 8 for four
 * separate synchronous reads, so getting the *request* wrong misprices a line
 * fill even when the data ends up correct.
 * ------------------------------------------------------------------------- */

/* First documented condition: "The logical address and function code signals
 * ... do not match the indexed tag field". */
static void test_a_tag_mismatch_requests_a_burst(void) {
  ap_m68030_cache_t cache = empty_cache();
  const uint32_t values[AP_M68030_CACHE_ENTRIES] = {0xA0, 0xA1, 0xA2, 0xA3};
  ap_m68030_cache_fill_line(&cache, 0x00002010u, FC_SUPERVISOR_DATA, values);

  TEST_ASSERT_TRUE(ap_m68030_cache_burst_request(
      &cache, ADDRESS, FC_SUPERVISOR_DATA, true, true, false, false));
}

/* Second condition, and the one most easily left out: the tag *matches* but
 * "all four long words corresponding to the indexed tag ... are marked
 * invalid". Without this a cleared cache refills one long word at a time and
 * never takes a burst at all. */
static void test_a_matching_tag_with_no_valid_entries_still_bursts(void) {
  ap_m68030_cache_t cache = empty_cache();
  const uint32_t values[AP_M68030_CACHE_ENTRIES] = {0xA0, 0xA1, 0xA2, 0xA3};
  ap_m68030_cache_fill_line(&cache, ADDRESS, FC_SUPERVISOR_DATA, values);
  ap_m68030_cache_clear(&cache); /* tags survive, valid bits do not */

  TEST_ASSERT_TRUE(ap_m68030_cache_burst_request(
      &cache, ADDRESS, FC_SUPERVISOR_DATA, true, true, false, false));
}

/* The complement: a matching tag with even one valid entry does not burst,
 * because the line is already partly populated. */
static void test_a_matching_tag_with_one_valid_entry_does_not_burst(void) {
  ap_m68030_cache_t cache = empty_cache();
  ap_m68030_cache_fill_entry(&cache, 0x00001010u, FC_SUPERVISOR_DATA, 0xA0);

  TEST_ASSERT_FALSE(ap_m68030_cache_burst_request(
      &cache, ADDRESS, FC_SUPERVISOR_DATA, true, true, false, false));
}

/* "If the appropriate cache is not enabled or if the cache freeze bit for the
 * cache is set, the processor does not assert CBREQ", the burst enable bit
 * gates the whole mechanism, and "CBREQ is not asserted during the read or
 * write cycles of any read-modify-write operation". Each suppressor is checked
 * against an access that would otherwise burst. */
static void test_each_condition_suppresses_the_burst_request(void) {
  ap_m68030_cache_t cache = empty_cache();

  /* Baseline: an empty cache would burst. */
  TEST_ASSERT_TRUE(ap_m68030_cache_burst_request(
      &cache, ADDRESS, FC_SUPERVISOR_DATA, true, true, false, false));

  /* DBE/IBE clear. */
  TEST_ASSERT_FALSE(ap_m68030_cache_burst_request(
      &cache, ADDRESS, FC_SUPERVISOR_DATA, false, true, false, false));
  /* Cache disabled. */
  TEST_ASSERT_FALSE(ap_m68030_cache_burst_request(
      &cache, ADDRESS, FC_SUPERVISOR_DATA, true, false, false, false));
  /* Cache frozen. */
  TEST_ASSERT_FALSE(ap_m68030_cache_burst_request(
      &cache, ADDRESS, FC_SUPERVISOR_DATA, true, true, true, false));
  /* Read-modify-write. */
  TEST_ASSERT_FALSE(ap_m68030_cache_burst_request(
      &cache, ADDRESS, FC_SUPERVISOR_DATA, true, true, false, true));
}


/* ---------------------------------------------------------------------------
 * The miss cost, end to end. This is what the plan item's verification asked
 * for: a hit costs no external bus cycle, a miss costs what the bus charges.
 * ------------------------------------------------------------------------- */

typedef struct {
  ap_m68030_term_t termination;
  bool cback;
  unsigned calls;
  uint32_t last_line_address;
} memory_t;

static void memory_fill(void *context, uint32_t line_address,
                        uint8_t function_code, ap_m68030_fill_answer_t *out) {
  (void)function_code;
  memory_t *memory = (memory_t *)context;
  memory->calls++;
  memory->last_line_address = line_address;
  out->termination = memory->termination;
  out->burst_acknowledge = memory->cback;
  /* Every long word in the machine reads as 0xC0DE0000 + its entry index, so a
   * test can tell *which* long word it was handed. The driver passes the line
   * base for a burst and the accessed long word for a single fill, so indexing
   * from whatever address arrives covers both. */
  const unsigned base = ap_m68030_cache_entry_index(line_address);
  for (unsigned e = 0; e < AP_M68030_BURST_BEATS; e++) {
    out->data[e] = 0xC0DE0000u + ((base + e) % AP_M68030_BURST_BEATS);
  }
}

static memory_t sterm_burst_memory(void) {
  return (memory_t){.termination = AP_M68030_TERM_STERM, .cback = true};
}

/* The headline claim, now measured rather than asserted: a miss into an empty
 * cache costs a burst line fill, and the hit that follows costs nothing. */
static void test_a_miss_costs_a_burst_and_the_next_hit_costs_nothing(void) {
  ap_m68030_cache_t cache = empty_cache();
  memory_t memory = sterm_burst_memory();

  const ap_m68030_cache_access_t miss = ap_m68030_cache_read(
      &cache, ADDRESS, FC_SUPERVISOR_DATA, true, true, false, false,
      memory_fill, &memory);

  TEST_ASSERT_FALSE(miss.hit);
  TEST_ASSERT_TRUE(miss.burst);
  TEST_ASSERT_EQUAL_UINT32(5, miss.clocks);
  TEST_ASSERT_EQUAL_UINT(AP_M68030_BURST_BEATS, miss.long_words);
  /* Entry 2 of the line, since ADDRESS selects it. */
  TEST_ASSERT_EQUAL_HEX32(0xC0DE0002u, miss.value);

  const ap_m68030_cache_access_t hit = ap_m68030_cache_read(
      &cache, ADDRESS, FC_SUPERVISOR_DATA, true, true, false, false,
      memory_fill, &memory);

  TEST_ASSERT_TRUE(hit.hit);
  TEST_ASSERT_EQUAL_UINT32(0, hit.clocks);
  TEST_ASSERT_EQUAL_HEX32(0xC0DE0002u, hit.value);
  /* The memory system was not asked a second time. */
  TEST_ASSERT_EQUAL_UINT(1, memory.calls);
}

/* The burst filled the whole line, so its three neighbours are hits too -- one
 * fill answering four accesses is the entire point of bursting. */
static void test_one_burst_fill_serves_the_whole_line(void) {
  ap_m68030_cache_t cache = empty_cache();
  memory_t memory = sterm_burst_memory();

  (void)ap_m68030_cache_read(&cache, ADDRESS, FC_SUPERVISOR_DATA, true, true,
                             false, false, memory_fill, &memory);

  for (unsigned e = 0; e < AP_M68030_BURST_BEATS; e++) {
    const ap_m68030_cache_access_t access = ap_m68030_cache_read(
        &cache, 0x00001010u + (e * 4u), FC_SUPERVISOR_DATA, true, true, false,
        false, memory_fill, &memory);
    TEST_ASSERT_TRUE(access.hit);
    TEST_ASSERT_EQUAL_UINT32(0, access.clocks);
    TEST_ASSERT_EQUAL_HEX32(0xC0DE0000u + e, access.value);
  }
  TEST_ASSERT_EQUAL_UINT(1, memory.calls);
}

/* A device that cannot burst answers without CBACK, so the same miss costs an
 * ordinary two-clock cycle and fills one entry rather than four. */
static void test_a_miss_without_cback_costs_a_single_cycle(void) {
  ap_m68030_cache_t cache = empty_cache();
  memory_t memory = sterm_burst_memory();
  memory.cback = false;

  const ap_m68030_cache_access_t miss = ap_m68030_cache_read(
      &cache, ADDRESS, FC_SUPERVISOR_DATA, true, true, false, false,
      memory_fill, &memory);

  TEST_ASSERT_FALSE(miss.burst);
  TEST_ASSERT_EQUAL_UINT32(2, miss.clocks);
  TEST_ASSERT_EQUAL_UINT(1, miss.long_words);

  /* Only the accessed entry was filled: its neighbour still misses. */
  const ap_m68030_cache_access_t neighbour = ap_m68030_cache_read(
      &cache, 0x00001010u, FC_SUPERVISOR_DATA, true, true, false, false,
      memory_fill, &memory);
  TEST_ASSERT_FALSE(neighbour.hit);
}

/* A disabled cache runs the access but keeps nothing, so every access pays
 * again -- which is what makes the instruction cache a measurable timing
 * effect, and what MD's IC command toggles on real hardware. */
static void test_a_disabled_cache_pays_for_every_access(void) {
  ap_m68030_cache_t cache = empty_cache();
  memory_t memory = sterm_burst_memory();

  for (unsigned i = 0; i < 3; i++) {
    const ap_m68030_cache_access_t access = ap_m68030_cache_read(
        &cache, ADDRESS, FC_SUPERVISOR_DATA, false /* disabled */, true, false,
        false, memory_fill, &memory);
    TEST_ASSERT_FALSE(access.hit);
    TEST_ASSERT_EQUAL_UINT32(2, access.clocks);
  }
  TEST_ASSERT_EQUAL_UINT(3, memory.calls);
}

/* "When the FI bit is set and a miss occurs ... the entry (or line) is not
 * replaced": a frozen cache performs the access and keeps nothing. */
static void test_a_frozen_cache_fetches_but_does_not_keep(void) {
  ap_m68030_cache_t cache = empty_cache();
  memory_t memory = sterm_burst_memory();

  const ap_m68030_cache_access_t first = ap_m68030_cache_read(
      &cache, ADDRESS, FC_SUPERVISOR_DATA, true, true, true /* frozen */, false,
      memory_fill, &memory);
  TEST_ASSERT_FALSE(first.hit);
  TEST_ASSERT_EQUAL_HEX32(0xC0DE0002u, first.value);

  const ap_m68030_cache_access_t second = ap_m68030_cache_read(
      &cache, ADDRESS, FC_SUPERVISOR_DATA, true, true, true, false, memory_fill,
      &memory);
  TEST_ASSERT_FALSE(second.hit);
  TEST_ASSERT_EQUAL_UINT(2, memory.calls);
}

/* A faulted fill caches nothing, so the fault is not silently turned into a
 * cached value that later accesses would hit. */
static void test_a_bus_error_on_a_fill_caches_nothing(void) {
  ap_m68030_cache_t cache = empty_cache();
  memory_t memory = sterm_burst_memory();
  memory.termination = AP_M68030_TERM_BERR;

  const ap_m68030_cache_access_t access = ap_m68030_cache_read(
      &cache, ADDRESS, FC_SUPERVISOR_DATA, true, true, false, false,
      memory_fill, &memory);

  TEST_ASSERT_TRUE(access.bus_error);
  TEST_ASSERT_EQUAL_UINT(0, access.long_words);

  uint32_t value = 0;
  TEST_ASSERT_FALSE(
      ap_m68030_cache_lookup(&cache, ADDRESS, FC_SUPERVISOR_DATA, &value));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_address_splits_into_tag_line_and_long_word);
  RUN_TEST(test_the_function_code_is_part_of_the_tag);
  RUN_TEST(test_a_filled_entry_hits_and_returns_its_long_word);
  RUN_TEST(test_the_same_address_in_another_space_misses);
  RUN_TEST(test_filling_one_entry_leaves_the_others_invalid);
  RUN_TEST(test_a_burst_fill_validates_the_whole_line);
  RUN_TEST(test_a_tag_change_invalidates_the_rest_of_the_line);
  RUN_TEST(test_clearing_one_entry_leaves_the_rest_of_the_line);
  RUN_TEST(test_a_write_hit_updates_the_entry_even_when_frozen);
  RUN_TEST(test_a_write_miss_without_allocation_leaves_the_cache_alone);
  RUN_TEST(test_write_allocation_validates_only_the_long_word_written);
  RUN_TEST(test_a_sub_long_word_write_miss_invalidates_rather_than_fills);
  RUN_TEST(test_a_frozen_cache_ignores_write_allocation);
  RUN_TEST(test_each_cacr_field_packs_to_its_documented_bit);
  RUN_TEST(test_the_clear_bits_never_read_back);
  RUN_TEST(test_clearing_one_cache_leaves_the_other);
  RUN_TEST(test_clear_entry_uses_the_caar_index);
  RUN_TEST(test_disabling_a_cache_does_not_flush_it);
  RUN_TEST(test_cdis_and_ciout_override_the_enable_bit);
  RUN_TEST(test_a_tag_mismatch_requests_a_burst);
  RUN_TEST(test_a_matching_tag_with_no_valid_entries_still_bursts);
  RUN_TEST(test_a_matching_tag_with_one_valid_entry_does_not_burst);
  RUN_TEST(test_each_condition_suppresses_the_burst_request);
  RUN_TEST(test_a_miss_costs_a_burst_and_the_next_hit_costs_nothing);
  RUN_TEST(test_one_burst_fill_serves_the_whole_line);
  RUN_TEST(test_a_miss_without_cback_costs_a_single_cycle);
  RUN_TEST(test_a_disabled_cache_pays_for_every_access);
  RUN_TEST(test_a_frozen_cache_fetches_but_does_not_keep);
  RUN_TEST(test_a_bus_error_on_a_fill_caches_nothing);
  return UNITY_END();
}
