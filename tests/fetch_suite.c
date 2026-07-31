/* MC68030 instruction prefetch driven from real memory.
 *
 * Cited to MC68030 User's Manual 3ed §11.2.2 and §6.1.
 *
 * pipe_suite pins the pipe's behaviour against words a test hands it. This
 * pins the same behaviour when the words come from the memory path -- which is
 * where the cost is, and therefore where the claim that half of all sequential
 * prefetches are free either holds or does not.
 */

#include "cpu/m68030/ap_m68030_fetch.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define FC_SUPERVISOR_PROGRAM 6u

typedef struct {
  unsigned fills;
} memory_t;

static void memory_fill(void *context, uint32_t line_address,
                        uint8_t function_code, ap_m68030_fill_answer_t *out) {
  (void)function_code;
  memory_t *memory = (memory_t *)context;
  memory->fills++;
  out->termination = AP_M68030_TERM_STERM;
  out->burst_acknowledge = false; /* one long word at a time, no burst */
  out->data[0] = 0x4E714E71u;     /* two NOPs */
  (void)line_address;
}

typedef struct {
  ap_m68030_cache_t cache;
  ap_m68030_atc_t atc;
  ap_m68030_tc_t tc;
  ap_m68030_root_t root;
  memory_t memory;
  ap_m68030_access_ctx_t access;
  ap_m68030_fetch_t fetch;
} machine_t;

static void make_machine(machine_t *m, bool cache_enabled) {
  ap_m68030_cache_clear(&m->cache);
  ap_m68030_atc_flush(&m->atc);
  m->memory.fills = 0;
  m->access = (ap_m68030_access_ctx_t){
      .cache = &m->cache,
      .atc = &m->atc,
      .tc = &m->tc,
      .root = &m->root,
      .cache_enabled = cache_enabled,
      .translation_enabled = false, /* untranslated: the boot PROM's world */
      .fill = memory_fill,
      .context = &m->memory,
  };
  m->fetch.access = &m->access;
  m->fetch.function_code = FC_SUPERVISOR_PROGRAM;
}

/* A prefetch from a long-word-aligned address performs a memory access; the
 * word after it comes from the holding register, free. */
static void test_the_second_word_of_a_long_word_is_free(void) {
  machine_t m = {0};
  make_machine(&m, false);
  ap_m68030_fetch_reset(&m.fetch, 0x1000u);

  const ap_m68030_fetch_result_t first = ap_m68030_fetch_prefetch(&m.fetch);
  TEST_ASSERT_TRUE(first.ok);
  TEST_ASSERT_FALSE(first.from_holding);
  TEST_ASSERT_TRUE(first.clocks > 0);

  const ap_m68030_fetch_result_t second = ap_m68030_fetch_prefetch(&m.fetch);
  TEST_ASSERT_TRUE(second.ok);
  TEST_ASSERT_TRUE(second.from_holding);
  TEST_ASSERT_EQUAL_UINT32(0, second.clocks);

  /* Two words, one memory access. */
  TEST_ASSERT_EQUAL_UINT(1, m.memory.fills);
}

/* The headline, and the reason the pipe is modelled at all: four sequential
 * words cost two fetches from an aligned start and three from an odd one. No
 * single number describes both, which is why the manual publishes an average. */
static void test_four_words_cost_two_fetches_aligned_and_three_odd(void) {
  machine_t aligned = {0};
  make_machine(&aligned, false);
  ap_m68030_fetch_reset(&aligned.fetch, 0x1000u);
  for (unsigned i = 0; i < 4; i++) {
    (void)ap_m68030_fetch_prefetch(&aligned.fetch);
  }
  TEST_ASSERT_EQUAL_UINT(2, aligned.memory.fills);

  machine_t odd = {0};
  make_machine(&odd, false);
  ap_m68030_fetch_reset(&odd.fetch, 0x1002u);
  for (unsigned i = 0; i < 4; i++) {
    (void)ap_m68030_fetch_prefetch(&odd.fetch);
  }
  TEST_ASSERT_EQUAL_UINT(3, odd.memory.fills);
}

/* The words reach the pipe and come out of stage D in order, so the saving is
 * real rather than a dropped fetch. */
static void test_the_prefetched_words_reach_the_decoded_stage(void) {
  machine_t m = {0};
  make_machine(&m, false);
  ap_m68030_fetch_reset(&m.fetch, 0x1000u);

  for (unsigned i = 0; i < 3; i++) {
    (void)ap_m68030_fetch_prefetch(&m.fetch);
    ap_m68030_pipe_advance(&m.fetch.pipe);
  }

  uint16_t word = 0;
  bool abnormal = true;
  TEST_ASSERT_TRUE(ap_m68030_pipe_decoded(&m.fetch.pipe, &word, &abnormal));
  TEST_ASSERT_EQUAL_HEX16(0x4E71u, word); /* NOP */
  TEST_ASSERT_FALSE(abnormal);
}

/* With the instruction cache enabled, a re-fetch of the same address is a cache
 * hit and costs nothing -- so a tight loop stops touching memory entirely. */
static void test_a_refetch_hits_the_instruction_cache(void) {
  machine_t m = {0};
  make_machine(&m, true);
  ap_m68030_fetch_reset(&m.fetch, 0x1000u);

  (void)ap_m68030_fetch_prefetch(&m.fetch);
  const unsigned after_first = m.memory.fills;

  /* Go back and fetch the same word again. */
  ap_m68030_fetch_reset(&m.fetch, 0x1000u);
  const ap_m68030_fetch_result_t again = ap_m68030_fetch_prefetch(&m.fetch);

  TEST_ASSERT_TRUE(again.ok);
  TEST_ASSERT_FALSE(again.from_holding); /* the holding register was reset */
  TEST_ASSERT_EQUAL_UINT32(0, again.clocks); /* but the cache answered */
  TEST_ASSERT_EQUAL_UINT(after_first, m.memory.fills);
}

/* Resetting the pipe, as a branch does, discards the holding register -- so the
 * word after a branch target is not free even if it shares a long word with
 * something fetched earlier. */
static void test_a_reset_discards_the_holding_register(void) {
  machine_t m = {0};
  make_machine(&m, false);
  ap_m68030_fetch_reset(&m.fetch, 0x1000u);
  (void)ap_m68030_fetch_prefetch(&m.fetch);

  /* Branch to the odd half of the same long word. */
  ap_m68030_fetch_reset(&m.fetch, 0x1002u);
  const ap_m68030_fetch_result_t after = ap_m68030_fetch_prefetch(&m.fetch);

  TEST_ASSERT_FALSE(after.from_holding);
  TEST_ASSERT_EQUAL_UINT(2, m.memory.fills);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_second_word_of_a_long_word_is_free);
  RUN_TEST(test_four_words_cost_two_fetches_aligned_and_three_odd);
  RUN_TEST(test_the_prefetched_words_reach_the_decoded_stage);
  RUN_TEST(test_a_refetch_hits_the_instruction_cache);
  RUN_TEST(test_a_reset_discards_the_holding_register);
  return UNITY_END();
}
