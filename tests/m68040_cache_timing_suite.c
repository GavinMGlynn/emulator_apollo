/* MC68040 cache maintenance timing, `[68040]` §10.3 and Tables 10-3 and 10-4.
 *
 * These figures are formulae with two free parameters, so most of these tests
 * are about the shape rather than the value.
 */

#include "cpu/m68040/ap_m68040_cache.h"
#include "cpu/m68040/ap_m68040_cache_timing.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------------------
 * Table 10-3.
 * ------------------------------------------------------------------------- */

static void test_the_three_cinv_forms(void) {
  /* "CINVL 9 + Idle; CINVP 266 + Idle; CINVA 9 + Idle." */
  TEST_ASSERT_EQUAL_UINT(9u, ap_m68040_cinv_clocks(AP_M68040_CINV_LINE, 0u));
  TEST_ASSERT_EQUAL_UINT(266u, ap_m68040_cinv_clocks(AP_M68040_CINV_PAGE, 0u));
  TEST_ASSERT_EQUAL_UINT(9u, ap_m68040_cinv_clocks(AP_M68040_CINV_ALL, 0u));
}

static void test_invalidating_everything_costs_no_more_than_one_line(void) {
  /* The counter-intuitive row: `CINVA` is as cheap as `CINVL`, because clearing
   * every valid bit at once needs no search -- while `CINVP` must examine every
   * line to find the ones its page owns, and costs nearly thirty times more. */
  TEST_ASSERT_EQUAL_UINT(ap_m68040_cinv_clocks(AP_M68040_CINV_LINE, 0u),
                         ap_m68040_cinv_clocks(AP_M68040_CINV_ALL, 0u));
  TEST_ASSERT_TRUE(ap_m68040_cinv_clocks(AP_M68040_CINV_PAGE, 0u) >
                   ap_m68040_cinv_clocks(AP_M68040_CINV_ALL, 0u) * 25u);
}

static void test_idle_adds_to_every_cinv_form(void) {
  /* "Idle refers to the number of clocks required for all pending writes and
   * instruction prefetches to complete", and it depends on what ran before:
   * "the total time required to execute a cache invalidate instruction is
   * dependent on the previous instruction stream." So no `CINV` has a fixed
   * cost, which is the point of carrying the parameter. */
  for (unsigned idle = 0; idle < 20u; idle += 7u) {
    TEST_ASSERT_EQUAL_UINT(9u + idle,
                           ap_m68040_cinv_clocks(AP_M68040_CINV_LINE, idle));
    TEST_ASSERT_EQUAL_UINT(266u + idle,
                           ap_m68040_cinv_clocks(AP_M68040_CINV_PAGE, idle));
  }
}

/* ---------------------------------------------------------------------------
 * Table 10-4.
 * ------------------------------------------------------------------------- */

static void test_the_cpush_best_cases(void) {
  /* "Best case corresponds to a cache containing no dirty entries", so nothing
   * is pushed and neither parameter appears in the figure. */
  TEST_ASSERT_EQUAL_UINT(6u, ap_m68040_cpush_best_case(AP_M68040_CPUSH_LINE));
  TEST_ASSERT_EQUAL_UINT(
      267u, ap_m68040_cpush_best_case(AP_M68040_CPUSH_PAGE_OR_ALL));
}

static void test_the_cpush_worst_cases(void) {
  /* "6 + Line + Idle" and "11 + 256 x Line + Idle". */
  TEST_ASSERT_EQUAL_UINT(
      6u, ap_m68040_cpush_worst_case(AP_M68040_CPUSH_LINE, 0u, 0u));
  TEST_ASSERT_EQUAL_UINT(
      6u + 5u + 3u,
      ap_m68040_cpush_worst_case(AP_M68040_CPUSH_LINE, 5u, 3u));
  TEST_ASSERT_EQUAL_UINT(
      11u + 256u * 5u + 3u,
      ap_m68040_cpush_worst_case(AP_M68040_CPUSH_PAGE_OR_ALL, 5u, 3u));
}

static void test_the_worst_case_multiplier_is_the_cache_line_count(void) {
  /* Table 10-4's "256 x Line" is every line in the cache pushed -- 64 sets of
   * four ways, from §4.1. The timing table and the cache chapter state the same
   * geometry without either citing the other, so this asserts they agree
   * rather than hard-coding 256 twice. */
  TEST_ASSERT_EQUAL_UINT(
      AP_M68040_CACHE_SETS * AP_M68040_CACHE_WAYS,
      ap_m68040_cpush_worst_case_lines(AP_M68040_CPUSH_PAGE_OR_ALL));
  TEST_ASSERT_EQUAL_UINT(
      256u, ap_m68040_cpush_worst_case_lines(AP_M68040_CPUSH_PAGE_OR_ALL));
  /* And a line push moves the whole 4-Kbyte cache. */
  TEST_ASSERT_EQUAL_UINT(
      AP_M68040_CACHE_BYTES,
      ap_m68040_cpush_worst_case_lines(AP_M68040_CPUSH_PAGE_OR_ALL) *
          AP_M68040_CACHE_LINE_BYTES);
}

static void test_a_line_push_moves_exactly_one_line(void) {
  TEST_ASSERT_EQUAL_UINT(1u,
                         ap_m68040_cpush_worst_case_lines(AP_M68040_CPUSH_LINE));
}

static void test_each_best_case_is_its_own_worst_case_formula(void) {
  /* The two columns are printed as unrelated figures, and they are not: each
   * best case is the worst-case formula evaluated at the cheapest line
   * transfer that form can have.
   *
   *   CPUSHL   6   = 6 + Line + Idle          at Line = 0, Idle = 0
   *   CPUSHP   267 = 11 + 256 x Line + Idle   at Line = 1, Idle = 0
   *
   * The two differ because "best case corresponds to a cache containing no
   * dirty entries": `CPUSHL` then transfers nothing at all, while the page and
   * all forms must still *examine* every one of the 256 lines to discover they
   * are clean, at a clock apiece. That reading is what makes 11 + 256 come to
   * exactly the printed 267 rather than approximately -- and an exact match on
   * a three-digit figure is strong evidence both rows were read correctly. */
  TEST_ASSERT_EQUAL_UINT(ap_m68040_cpush_best_case(AP_M68040_CPUSH_LINE),
                         ap_m68040_cpush_worst_case(AP_M68040_CPUSH_LINE, 0u,
                                                    0u));
  TEST_ASSERT_EQUAL_UINT(
      ap_m68040_cpush_best_case(AP_M68040_CPUSH_PAGE_OR_ALL),
      ap_m68040_cpush_worst_case(AP_M68040_CPUSH_PAGE_OR_ALL, 1u, 0u));
}

static void test_a_realistic_line_transfer_dwarfs_the_best_case(void) {
  /* Once a line transfer costs more than a clock, the page form's worst case
   * runs away from its best -- which is the practical content of the table:
   * flushing a dirty cache is bounded by memory, not by the processor. */
  TEST_ASSERT_TRUE(
      ap_m68040_cpush_worst_case(AP_M68040_CPUSH_PAGE_OR_ALL, 5u, 0u) >
      4u * ap_m68040_cpush_best_case(AP_M68040_CPUSH_PAGE_OR_ALL));
}

static void test_the_parameters_are_the_callers_to_supply(void) {
  /* §10.3 refuses an equation for `CPUSH`: "it is impossible to provide an
   * equation for execution time that works for all code sequences." `Line` is
   * a property of the user's memory and `Idle` of the preceding instruction
   * stream, so a core that folded a guess into a constant would be inventing
   * the one number the manual declines to give.
   *
   * The check is that both parameters really do reach the result. */
  TEST_ASSERT_NOT_EQUAL_UINT(
      ap_m68040_cpush_worst_case(AP_M68040_CPUSH_LINE, 0u, 0u),
      ap_m68040_cpush_worst_case(AP_M68040_CPUSH_LINE, 1u, 0u));
  TEST_ASSERT_NOT_EQUAL_UINT(
      ap_m68040_cpush_worst_case(AP_M68040_CPUSH_LINE, 0u, 0u),
      ap_m68040_cpush_worst_case(AP_M68040_CPUSH_LINE, 0u, 1u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_three_cinv_forms);
  RUN_TEST(test_invalidating_everything_costs_no_more_than_one_line);
  RUN_TEST(test_idle_adds_to_every_cinv_form);
  RUN_TEST(test_the_cpush_best_cases);
  RUN_TEST(test_the_cpush_worst_cases);
  RUN_TEST(test_the_worst_case_multiplier_is_the_cache_line_count);
  RUN_TEST(test_a_line_push_moves_exactly_one_line);
  RUN_TEST(test_each_best_case_is_its_own_worst_case_formula);
  RUN_TEST(test_a_realistic_line_transfer_dwarfs_the_best_case);
  RUN_TEST(test_the_parameters_are_the_callers_to_supply);
  return UNITY_END();
}
