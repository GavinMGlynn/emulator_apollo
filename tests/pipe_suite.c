/* MC68030 instruction pipe and cache holding register.
 *
 * The headline test here is the last one: two sequential prefetches cost one
 * external bus cycle when long-word aligned and two when they are not. That
 * factor of two is the thing MC68030 User's Manual 3ed §11.3.3 averages and
 * rounds when it publishes a no-cache-case instruction time, so it is the
 * mechanism this project models instead of the published average.
 */

#include "cpu/m68030/ap_m68030_pipe.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* A recognisable long word: high-order word 0x4E71 (NOP), low-order 0x4E75
 * (RTS). Real opcodes, so a swapped-halves bug reads as the wrong instruction
 * rather than as an arbitrary number. */
#define LONGWORD UINT32_C(0x4E714E75)
#define HIGH_WORD UINT16_C(0x4E71)
#define LOW_WORD UINT16_C(0x4E75)

/* [030] 11.2.2: "the cache holding register is 32 bits wide and contains the
 * entire long word ... the high-order word is also loaded into stage B". */
static void test_an_aligned_prefetch_loads_the_high_order_word_into_stage_b(void) {
  ap_m68030_pipe_t pipe;
  ap_m68030_pipe_reset(&pipe);
  ap_m68030_pipe_fill(&pipe, 0x1000, LONGWORD, false);
  TEST_ASSERT_TRUE(pipe.b.valid);
  TEST_ASSERT_EQUAL_HEX16(HIGH_WORD, pipe.b.word);
}

/* The word a prefetch selects is chosen by address bit 1, big-endian: the
 * high-order word lives at the lower address. */
static void test_the_odd_word_of_a_long_word_is_the_low_order_half(void) {
  ap_m68030_pipe_t pipe;
  ap_m68030_pipe_reset(&pipe);
  ap_m68030_pipe_fill(&pipe, 0x1002, LONGWORD, false);
  TEST_ASSERT_EQUAL_HEX16(LOW_WORD, pipe.b.word);
}

/* [030] 11.2.2: "The instruction word for the next sequential prefetch can then
 * be accessed directly from the cache holding register, and no external bus
 * cycle or instruction cache access is required." */
static void test_the_next_sequential_word_is_held_after_an_aligned_fill(void) {
  ap_m68030_pipe_t pipe;
  ap_m68030_pipe_reset(&pipe);
  ap_m68030_pipe_fill(&pipe, 0x1000, LONGWORD, false);
  TEST_ASSERT_TRUE(ap_m68030_pipe_holds(&pipe, 0x1002));
}

/* The long word after this one is not held, so it costs a bus cycle. */
static void test_a_word_in_the_next_long_word_is_not_held(void) {
  ap_m68030_pipe_t pipe;
  ap_m68030_pipe_reset(&pipe);
  ap_m68030_pipe_fill(&pipe, 0x1000, LONGWORD, false);
  TEST_ASSERT_FALSE(ap_m68030_pipe_holds(&pipe, 0x1004));
}

static void test_nothing_is_held_before_the_first_fill(void) {
  ap_m68030_pipe_t pipe;
  ap_m68030_pipe_reset(&pipe);
  TEST_ASSERT_FALSE(ap_m68030_pipe_holds(&pipe, 0x1000));
}

/* [030] 11.2.2: a word is "completely decoded when it reaches stage D", having
 * entered at B and passed through C -- so three stages, and a word is not
 * decoded until two advances after it was loaded. */
static void test_a_word_is_decoded_only_after_it_reaches_the_third_stage(void) {
  ap_m68030_pipe_t pipe;
  ap_m68030_pipe_reset(&pipe);
  ap_m68030_pipe_fill(&pipe, 0x1000, LONGWORD, false);

  TEST_ASSERT_FALSE(ap_m68030_pipe_decoded(&pipe, NULL, NULL));
  ap_m68030_pipe_advance(&pipe); /* B -> C */
  TEST_ASSERT_FALSE(ap_m68030_pipe_decoded(&pipe, NULL, NULL));
  ap_m68030_pipe_advance(&pipe); /* C -> D */

  uint16_t word = 0;
  TEST_ASSERT_TRUE(ap_m68030_pipe_decoded(&pipe, &word, NULL));
  TEST_ASSERT_EQUAL_HEX16(HIGH_WORD, word);
}

/* Words keep their order through the pipe. */
static void test_words_emerge_from_the_pipe_in_the_order_they_entered(void) {
  ap_m68030_pipe_t pipe;
  ap_m68030_pipe_reset(&pipe);

  ap_m68030_pipe_fill(&pipe, 0x1000, LONGWORD, false);
  ap_m68030_pipe_advance(&pipe);
  ap_m68030_pipe_load_from_holding(&pipe, 0x1002);
  ap_m68030_pipe_advance(&pipe);

  uint16_t word = 0;
  TEST_ASSERT_TRUE(ap_m68030_pipe_decoded(&pipe, &word, NULL));
  TEST_ASSERT_EQUAL_HEX16(HIGH_WORD, word);

  ap_m68030_pipe_advance(&pipe);
  TEST_ASSERT_TRUE(ap_m68030_pipe_decoded(&pipe, &word, NULL));
  TEST_ASSERT_EQUAL_HEX16(LOW_WORD, word);
}

/* [030] 11.2.2: "Each of the pipe stages has a status bit that reflects whether
 * the word in the stage was loaded with data from a bus cycle that was
 * terminated abnormally." It must travel with its word, because a bus error has
 * to fault where the word is *used* -- a prefetch down a path never executed
 * must not raise anything. */
static void test_an_abnormal_termination_follows_its_word_through_the_pipe(void) {
  ap_m68030_pipe_t pipe;
  ap_m68030_pipe_reset(&pipe);
  ap_m68030_pipe_fill(&pipe, 0x1000, LONGWORD, true);
  ap_m68030_pipe_advance(&pipe);
  ap_m68030_pipe_advance(&pipe);

  bool abnormal = false;
  TEST_ASSERT_TRUE(ap_m68030_pipe_decoded(&pipe, NULL, &abnormal));
  TEST_ASSERT_TRUE(abnormal);
}

/* A word taken from a holding register that was filled abnormally is itself
 * suspect, so the status bit comes from the register and not from thin air. */
static void test_a_word_from_an_abnormally_filled_holding_register_is_marked(void) {
  ap_m68030_pipe_t pipe;
  ap_m68030_pipe_reset(&pipe);
  ap_m68030_pipe_fill(&pipe, 0x1000, LONGWORD, true);
  ap_m68030_pipe_advance(&pipe);
  ap_m68030_pipe_load_from_holding(&pipe, 0x1002);
  TEST_ASSERT_TRUE(pipe.b.abnormal);
}

/* A clean fetch must not inherit a previous fetch's abnormal status. */
static void test_a_clean_fill_clears_the_abnormal_status(void) {
  ap_m68030_pipe_t pipe;
  ap_m68030_pipe_reset(&pipe);
  ap_m68030_pipe_fill(&pipe, 0x1000, LONGWORD, true);
  ap_m68030_pipe_fill(&pipe, 0x2000, LONGWORD, false);
  TEST_ASSERT_FALSE(pipe.b.abnormal);
  TEST_ASSERT_FALSE(pipe.holding_abnormal);
}

/* Stage B is emptied by an advance, so a stale word cannot be decoded twice.
 * `valid` is what distinguishes an empty stage from one holding 0x0000, which
 * is a legal opcode word (ORI.B #imm,D0). */
static void test_an_advance_empties_the_stage_words_enter(void) {
  ap_m68030_pipe_t pipe;
  ap_m68030_pipe_reset(&pipe);
  ap_m68030_pipe_fill(&pipe, 0x1000, LONGWORD, false);
  ap_m68030_pipe_advance(&pipe);
  TEST_ASSERT_FALSE(pipe.b.valid);
}

/* Loading from a holding register that does not hold the address is a caller
 * error. Stage B is left empty rather than filled with a stale word, so the
 * mistake surfaces here instead of as a wrong opcode much later. */
static void test_loading_a_word_the_holding_register_lacks_leaves_the_stage_empty(void) {
  ap_m68030_pipe_t pipe;
  ap_m68030_pipe_reset(&pipe);
  ap_m68030_pipe_fill(&pipe, 0x1000, LONGWORD, false);
  ap_m68030_pipe_load_from_holding(&pipe, 0x2000);
  TEST_ASSERT_FALSE(pipe.b.valid);
}

/* The point of the whole module.
 *
 * [030] 11.3.3 p.11-8 publishes no-cache-case times as the average of the
 * odd- and even-aligned cases because the prefetch cost genuinely differs. Here
 * that difference is counted rather than averaged: four sequential instruction
 * words cost two external bus cycles when the run starts long-word aligned, and
 * three when it starts on an odd word -- the first fetch buys only one usable
 * word. */
static void test_alignment_decides_how_many_bus_cycles_a_run_of_words_costs(void) {
  static const struct {
    uint32_t start;
    uint32_t expected_bus_cycles;
  } cases[] = {
      {0x1000, 2}, /* long-word aligned: each fetch yields two words */
      {0x1002, 3}, /* odd word: the first fetch yields only one */
  };

  for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
    ap_m68030_pipe_t pipe;
    ap_m68030_pipe_reset(&pipe);

    uint32_t bus_cycles = 0;
    for (uint32_t i = 0; i < 4; i++) {
      uint32_t address = cases[c].start + i * 2u;
      if (ap_m68030_pipe_holds(&pipe, address)) {
        ap_m68030_pipe_load_from_holding(&pipe, address);
      } else {
        bus_cycles++;
        ap_m68030_pipe_fill(&pipe, address, LONGWORD, false);
      }
      TEST_ASSERT_TRUE(pipe.b.valid);
      ap_m68030_pipe_advance(&pipe);
    }
    TEST_ASSERT_EQUAL_UINT32(cases[c].expected_bus_cycles, bus_cycles);
  }
}

/* [030] 11.2.2: "The cache holding register provides instruction words to the
 * pipe, regardless of whether the instruction cache is enabled or disabled."
 * The holding register is not modelled as part of the cache, so there is no
 * cache-enable state that could disable it -- this test pins that as intent
 * rather than accident, since MD's IC command will turn the cache off under a
 * probe and the saving must survive. */
static void test_the_holding_register_saving_does_not_depend_on_the_cache(void) {
  ap_m68030_pipe_t pipe;
  ap_m68030_pipe_reset(&pipe);
  ap_m68030_pipe_fill(&pipe, 0x1000, LONGWORD, false);
  TEST_ASSERT_TRUE(ap_m68030_pipe_holds(&pipe, 0x1002));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_an_aligned_prefetch_loads_the_high_order_word_into_stage_b);
  RUN_TEST(test_the_odd_word_of_a_long_word_is_the_low_order_half);
  RUN_TEST(test_the_next_sequential_word_is_held_after_an_aligned_fill);
  RUN_TEST(test_a_word_in_the_next_long_word_is_not_held);
  RUN_TEST(test_nothing_is_held_before_the_first_fill);
  RUN_TEST(test_a_word_is_decoded_only_after_it_reaches_the_third_stage);
  RUN_TEST(test_words_emerge_from_the_pipe_in_the_order_they_entered);
  RUN_TEST(test_an_abnormal_termination_follows_its_word_through_the_pipe);
  RUN_TEST(test_a_word_from_an_abnormally_filled_holding_register_is_marked);
  RUN_TEST(test_a_clean_fill_clears_the_abnormal_status);
  RUN_TEST(test_an_advance_empties_the_stage_words_enter);
  RUN_TEST(test_loading_a_word_the_holding_register_lacks_leaves_the_stage_empty);
  RUN_TEST(test_alignment_decides_how_many_bus_cycles_a_run_of_words_costs);
  RUN_TEST(test_the_holding_register_saving_does_not_depend_on_the_cache);
  return UNITY_END();
}
