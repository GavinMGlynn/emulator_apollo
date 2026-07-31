/* MC68030 effective address timings, `[030]` §11.6.1 and §11.6.3.
 *
 * These are the other side of the composition the instruction tables leave
 * half-stated. As with the instruction transcription, they are checked against
 * structure rather than by re-reading: the relationships that must hold between
 * the two tables, and between rows within each.
 */

#include "cpu/m68030/ap_m68030_ea_timing.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* A register operand has no address to compute and nothing to read, so it costs
 * nothing in either table. That is what makes `ADD D0,D1` a complete figure
 * while `ADD D0,(A0)` is not — and it is why the register forms could be
 * transcribed and used before these tables existed at all. */
static void test_a_register_operand_costs_nothing_either_way(void) {
  const ap_m68030_ea_kind_t registers[] = {AP_M68030_EA_DATA_REGISTER,
                                           AP_M68030_EA_ADDRESS_REGISTER};
  for (unsigned i = 0; i < 2u; i++) {
    const ap_m68030_ea_timing_t *fetch =
        ap_m68030_ea_fetch_timing(registers[i], 4u);
    const ap_m68030_ea_timing_t *calculate =
        ap_m68030_ea_calculate_timing(registers[i]);
    TEST_ASSERT_NOT_NULL(fetch);
    TEST_ASSERT_NOT_NULL(calculate);
    TEST_ASSERT_EQUAL_UINT(0u, fetch->timing.cache_case);
    TEST_ASSERT_EQUAL_UINT(0u, calculate->timing.cache_case);

    /* And the table gives their head and tail as `-`, not 0: there is no
     * address computation to overlap *with*, which is a different statement
     * from an overlap of zero. */
    TEST_ASSERT_FALSE(fetch->head_applies);
    TEST_ASSERT_FALSE(calculate->head_applies);
  }
}

/* Fetching always costs at least as much as calculating, for every mode: the
 * fetch does the same address computation and then reads. A transcription with
 * the two tables swapped would break this everywhere at once. */
static void test_fetching_never_costs_less_than_calculating(void) {
  const ap_m68030_ea_kind_t modes[] = {
      AP_M68030_EA_DATA_REGISTER,   AP_M68030_EA_ADDRESS_REGISTER,
      AP_M68030_EA_ADDRESS_INDIRECT, AP_M68030_EA_POSTINCREMENT,
      AP_M68030_EA_PREDECREMENT,    AP_M68030_EA_DISPLACEMENT,
      AP_M68030_EA_ABSOLUTE_SHORT,  AP_M68030_EA_ABSOLUTE_LONG,
      AP_M68030_EA_INDEXED,
  };
  for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; i++) {
    const ap_m68030_ea_timing_t *fetch = ap_m68030_ea_fetch_timing(modes[i], 4u);
    const ap_m68030_ea_timing_t *calculate =
        ap_m68030_ea_calculate_timing(modes[i]);
    TEST_ASSERT_NOT_NULL(fetch);
    TEST_ASSERT_NOT_NULL(calculate);
    TEST_ASSERT_TRUE_MESSAGE(
        fetch->timing.cache_case >= calculate->timing.cache_case, fetch->mode);
  }
}

/* The difference between the two tables for `(An)` is the operand read: 3 to
 * fetch against 2 to calculate. That single clock is the size of the error C9
 * measured writ small, and it is why an instruction's footnote — which of the
 * two tables it uses — is load-bearing rather than a formality. */
static void test_the_two_tables_differ_by_the_operand_read(void) {
  const ap_m68030_ea_timing_t *fetch =
      ap_m68030_ea_fetch_timing(AP_M68030_EA_ADDRESS_INDIRECT, 4u);
  const ap_m68030_ea_timing_t *calculate =
      ap_m68030_ea_calculate_timing(AP_M68030_EA_ADDRESS_INDIRECT);
  TEST_ASSERT_NOT_NULL(fetch);
  TEST_ASSERT_NOT_NULL(calculate);

  TEST_ASSERT_EQUAL_UINT(3u, fetch->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(2u, calculate->timing.cache_case);
}

/* "2+op head": the calculate table writes several heads as the *operation's*
 * head plus a figure, not as a constant. That is the table expressing a
 * dependency between the two halves of Equation (11-2), and a transcription
 * that flattened it to 2 would drop whatever the operation contributes.
 *
 * Not every row is relative — `(An)+` is a plain 0 — so the flag distinguishes
 * rather than applying to the whole table. */
static void test_the_calculate_heads_that_are_relative_say_so(void) {
  const ap_m68030_ea_timing_t *indirect =
      ap_m68030_ea_calculate_timing(AP_M68030_EA_ADDRESS_INDIRECT);
  const ap_m68030_ea_timing_t *postincrement =
      ap_m68030_ea_calculate_timing(AP_M68030_EA_POSTINCREMENT);
  TEST_ASSERT_NOT_NULL(indirect);
  TEST_ASSERT_NOT_NULL(postincrement);

  TEST_ASSERT_TRUE(indirect->head_adds_operation);
  TEST_ASSERT_FALSE(postincrement->head_adds_operation);

  /* The fetch table has no relative heads at all. */
  const ap_m68030_ea_timing_t *fetch =
      ap_m68030_ea_fetch_timing(AP_M68030_EA_ADDRESS_INDIRECT, 4u);
  TEST_ASSERT_NOT_NULL(fetch);
  TEST_ASSERT_FALSE(fetch->head_adds_operation);
}

/* The immediate rows are split by operand size, and byte and word cost the
 * same: Table 2-3's "Low-order byte of the extension word" means a byte
 * immediate still occupies a whole word of instruction stream. A long costs
 * twice, being two words. */
static void test_an_immediate_costs_by_the_words_it_occupies(void) {
  const ap_m68030_ea_timing_t *byte =
      ap_m68030_ea_fetch_timing(AP_M68030_EA_IMMEDIATE, 1u);
  const ap_m68030_ea_timing_t *word =
      ap_m68030_ea_fetch_timing(AP_M68030_EA_IMMEDIATE, 2u);
  const ap_m68030_ea_timing_t *long_word =
      ap_m68030_ea_fetch_timing(AP_M68030_EA_IMMEDIATE, 4u);
  TEST_ASSERT_NOT_NULL(byte);
  TEST_ASSERT_NOT_NULL(word);
  TEST_ASSERT_NOT_NULL(long_word);

  TEST_ASSERT_EQUAL_UINT(2u, byte->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(2u, word->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(4u, long_word->timing.cache_case);
}

/* There is no immediate row in the calculate table, and the lookup says so
 * rather than returning zero. An operand in the instruction stream has no
 * address to compute, so an instruction footnoted for that table cannot take
 * one — and a zero would read as "free" rather than as "not a thing". */
static void test_an_immediate_has_no_address_to_calculate(void) {
  TEST_ASSERT_NULL(ap_m68030_ea_calculate_timing(AP_M68030_EA_IMMEDIATE));
  TEST_ASSERT_NOT_NULL(ap_m68030_ea_fetch_timing(AP_M68030_EA_IMMEDIATE, 2u));
}

/* The absolute long is the one fetch row whose two columns differ, its second
 * extension word being another prefetch. Everywhere else in this table the
 * cache and no-cache cases agree, because an effective address computation is
 * long enough to hide its own fetch. */
static void test_only_the_long_absolute_differs_between_the_columns(void) {
  const ap_m68030_ea_kind_t modes[] = {
      AP_M68030_EA_ADDRESS_INDIRECT, AP_M68030_EA_POSTINCREMENT,
      AP_M68030_EA_PREDECREMENT,     AP_M68030_EA_ABSOLUTE_SHORT,
      AP_M68030_EA_INDEXED,
  };
  for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; i++) {
    const ap_m68030_ea_timing_t *entry =
        ap_m68030_ea_fetch_timing(modes[i], 4u);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(entry->timing.cache_case,
                                   entry->timing.no_cache_case, entry->mode);
  }

  const ap_m68030_ea_timing_t *absolute_long =
      ap_m68030_ea_fetch_timing(AP_M68030_EA_ABSOLUTE_LONG, 4u);
  TEST_ASSERT_NOT_NULL(absolute_long);
  TEST_ASSERT_EQUAL_UINT(4u, absolute_long->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(5u, absolute_long->timing.no_cache_case);
}

/* Every row is internally consistent by the same rule the instruction rows
 * obey: a head or tail may equal the cache case but never exceed it. */
static void test_every_row_is_internally_consistent(void) {
  const ap_m68030_ea_kind_t modes[] = {
      AP_M68030_EA_ADDRESS_INDIRECT, AP_M68030_EA_POSTINCREMENT,
      AP_M68030_EA_PREDECREMENT,     AP_M68030_EA_DISPLACEMENT,
      AP_M68030_EA_ABSOLUTE_SHORT,   AP_M68030_EA_ABSOLUTE_LONG,
      AP_M68030_EA_INDEXED,
  };
  for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; i++) {
    const ap_m68030_ea_timing_t *fetch = ap_m68030_ea_fetch_timing(modes[i], 4u);
    TEST_ASSERT_NOT_NULL(fetch);
    TEST_ASSERT_TRUE_MESSAGE(ap_m68030_timing_consistent(&fetch->timing),
                             fetch->mode);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_register_operand_costs_nothing_either_way);
  RUN_TEST(test_fetching_never_costs_less_than_calculating);
  RUN_TEST(test_the_two_tables_differ_by_the_operand_read);
  RUN_TEST(test_the_calculate_heads_that_are_relative_say_so);
  RUN_TEST(test_an_immediate_costs_by_the_words_it_occupies);
  RUN_TEST(test_an_immediate_has_no_address_to_calculate);
  RUN_TEST(test_only_the_long_absolute_differs_between_the_columns);
  RUN_TEST(test_every_row_is_internally_consistent);
  return UNITY_END();
}
