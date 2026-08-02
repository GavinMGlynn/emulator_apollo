/* MC68040 instruction timing composition, `[68040]` §10.1 and Table 10-2.
 *
 * The 68030's timing was modelled in Phase 2 as `(r/p/w)` triples composed by
 * Equations 11-1 and 11-2. The 68040's is a different shape, and several tests
 * here exist to pin the differences rather than the numbers.
 */

#include "cpu/m68040/ap_m68040_timing.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------------------
 * Table 10-2.
 * ------------------------------------------------------------------------- */

static void test_the_register_and_immediate_modes_reach_no_memory(void) {
  TEST_ASSERT_EQUAL_UINT(
      0u, ap_m68040_ea_accesses_fetching(AP_M68040_EA_DATA_REGISTER));
  TEST_ASSERT_EQUAL_UINT(
      0u, ap_m68040_ea_accesses_fetching(AP_M68040_EA_ADDRESS_REGISTER));
  TEST_ASSERT_EQUAL_UINT(
      0u, ap_m68040_ea_accesses_fetching(AP_M68040_EA_IMMEDIATE));
}

static void test_the_simple_memory_modes_cost_one_access(void) {
  const ap_m68040_ea_t modes[] = {
      AP_M68040_EA_INDIRECT,          AP_M68040_EA_POSTINCREMENT,
      AP_M68040_EA_PREDECREMENT,      AP_M68040_EA_DISPLACEMENT,
      AP_M68040_EA_PC_DISPLACEMENT,   AP_M68040_EA_ABSOLUTE,
      AP_M68040_EA_INDEXED,           AP_M68040_EA_PC_INDEXED,
      AP_M68040_EA_BASE_INDEXED,      AP_M68040_EA_BASE_DISPLACEMENT};
  for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; i++) {
    TEST_ASSERT_EQUAL_UINT(1u, ap_m68040_ea_accesses_fetching(modes[i]));
  }
}

static void test_the_memory_indirect_modes_cost_two(void) {
  /* One access to follow the indirection, one for the operand. */
  const ap_m68040_ea_t modes[] = {AP_M68040_EA_MEMORY_PREINDEXED,
                                  AP_M68040_EA_MEMORY_PREINDEXED_OD,
                                  AP_M68040_EA_MEMORY_POSTINDEXED,
                                  AP_M68040_EA_MEMORY_POSTINDEXED_OD};
  for (unsigned i = 0; i < 4u; i++) {
    TEST_ASSERT_EQUAL_UINT(2u, ap_m68040_ea_accesses_fetching(modes[i]));
  }
}

static void test_only_the_indirect_modes_read_when_sending_an_address(void) {
  /* Table 10-2's second column is zero everywhere except the memory indirect
   * modes, which must still follow the indirection to know the address they
   * hand on. Every other mode computes its address from registers and
   * extension words alone. */
  for (unsigned i = 0; i < AP_M68040_EA_COUNT; i++) {
    const ap_m68040_ea_t ea = (ap_m68040_ea_t)i;
    const bool indirect = ea == AP_M68040_EA_MEMORY_PREINDEXED ||
                          ea == AP_M68040_EA_MEMORY_PREINDEXED_OD ||
                          ea == AP_M68040_EA_MEMORY_POSTINDEXED ||
                          ea == AP_M68040_EA_MEMORY_POSTINDEXED_OD;
    TEST_ASSERT_EQUAL_UINT(indirect ? 1u : 0u,
                           ap_m68040_ea_accesses_sending(ea));
  }
}

static void test_sending_an_address_never_costs_more_than_fetching(void) {
  /* Fetching the operand is strictly more work than handing on its address, so
   * the second column can never exceed the first. */
  for (unsigned i = 0; i < AP_M68040_EA_COUNT; i++) {
    const ap_m68040_ea_t ea = (ap_m68040_ea_t)i;
    TEST_ASSERT_TRUE(ap_m68040_ea_accesses_sending(ea) <=
                     ap_m68040_ea_accesses_fetching(ea));
  }
}

/* ---------------------------------------------------------------------------
 * The fetch stage, which the tables do not list.
 * ------------------------------------------------------------------------- */

static void test_the_fetch_stage_costs_a_clock_even_with_no_operand(void) {
  /* "An instruction requires one clock to pass through the <ea> fetch stage
   * even if no operand is fetched." The floor is the part a naive reading of
   * "one clock per access" drops -- `Dn` costs zero accesses and one clock. */
  TEST_ASSERT_EQUAL_UINT(1u, ap_m68040_fetch_clocks(0u));
  TEST_ASSERT_EQUAL_UINT(
      1u, ap_m68040_fetch_clocks(
              ap_m68040_ea_accesses_fetching(AP_M68040_EA_DATA_REGISTER)));
}

static void test_the_fetch_stage_costs_a_clock_per_access(void) {
  TEST_ASSERT_EQUAL_UINT(1u, ap_m68040_fetch_clocks(1u));
  TEST_ASSERT_EQUAL_UINT(2u, ap_m68040_fetch_clocks(2u));
  TEST_ASSERT_EQUAL_UINT(
      2u, ap_m68040_fetch_clocks(ap_m68040_ea_accesses_fetching(
              AP_M68040_EA_MEMORY_PREINDEXED)));
}

/* ---------------------------------------------------------------------------
 * Lead and base, and the interlock.
 * ------------------------------------------------------------------------- */

static void test_the_manuals_worked_example(void) {
  /* "If an execution time is listed as 2L + 1, the lead time is two clocks and
   * the base time is one for a total execution time of three." */
  const ap_m68040_execute_t two_l_one = {.lead = 2u, .base = 1u};
  TEST_ASSERT_EQUAL_UINT(3u, ap_m68040_execute_total(two_l_one));
}

static void test_a_lead_absorbs_stalls_up_to_its_own_size(void) {
  /* "The lead time is the number of clocks the instruction can stall when
   * entering the execution stage without delaying the instruction execution."
   * So a stall inside the lead costs nothing at all. */
  const ap_m68040_execute_t two_l_one = {.lead = 2u, .base = 1u};
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68040_interlock_penalty(two_l_one, 0u));
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68040_interlock_penalty(two_l_one, 1u));
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68040_interlock_penalty(two_l_one, 2u));
}

static void test_a_stall_beyond_the_lead_lengthens_the_calculate_stage(void) {
  /* The manual's example: "if the execution time listed is 2L + 1 and the
   * instruction stalls for three clocks, then the <ea> calculate time increases
   * by one clock". Three of stall against two of lead leaves one.
   *
   * The manual writes that as "3 - 1 = 2L", which is loose arithmetic for a
   * rule it states unambiguously in words. The rule is what is modelled. */
  const ap_m68040_execute_t two_l_one = {.lead = 2u, .base = 1u};
  TEST_ASSERT_EQUAL_UINT(1u, ap_m68040_interlock_penalty(two_l_one, 3u));
  TEST_ASSERT_EQUAL_UINT(2u, ap_m68040_interlock_penalty(two_l_one, 4u));
}

static void test_a_zero_lead_passes_every_stall_straight_through(void) {
  /* An instruction with no lead tolerates nothing, so each stalled clock
   * lengthens the calculate stage. */
  const ap_m68040_execute_t no_lead = {.lead = 0u, .base = 1u};
  for (unsigned stall = 0; stall < 5u; stall++) {
    TEST_ASSERT_EQUAL_UINT(stall,
                           ap_m68040_interlock_penalty(no_lead, stall));
  }
}

static void test_the_interlock_applies_only_to_extension_word_modes(void) {
  /* "The <ea> calculate and execute stages operate in an interlocked manner for
   * all instructions using the brief and full extension word formats." The
   * simple modes carry no such word and are not interlocked, so a stall does
   * not reach their calculate stage. */
  TEST_ASSERT_TRUE(ap_m68040_ea_is_interlocked(AP_M68040_EA_INDEXED));
  TEST_ASSERT_TRUE(ap_m68040_ea_is_interlocked(AP_M68040_EA_BASE_DISPLACEMENT));
  TEST_ASSERT_TRUE(
      ap_m68040_ea_is_interlocked(AP_M68040_EA_MEMORY_POSTINDEXED_OD));

  TEST_ASSERT_FALSE(ap_m68040_ea_is_interlocked(AP_M68040_EA_DATA_REGISTER));
  TEST_ASSERT_FALSE(ap_m68040_ea_is_interlocked(AP_M68040_EA_INDIRECT));
  TEST_ASSERT_FALSE(ap_m68040_ea_is_interlocked(AP_M68040_EA_DISPLACEMENT));
  TEST_ASSERT_FALSE(ap_m68040_ea_is_interlocked(AP_M68040_EA_ABSOLUTE));
}

static void test_every_interlocked_mode_uses_an_index_register(void) {
  /* The brief and full extension word formats are exactly the modes with an
   * index register or a base displacement, so the two ways of describing the
   * set must agree. */
  for (unsigned i = 0; i < AP_M68040_EA_COUNT; i++) {
    const ap_m68040_ea_t ea = (ap_m68040_ea_t)i;
    const bool indexed = ea == AP_M68040_EA_INDEXED ||
                         ea == AP_M68040_EA_PC_INDEXED ||
                         ea == AP_M68040_EA_BASE_INDEXED ||
                         ea == AP_M68040_EA_BASE_DISPLACEMENT ||
                         ea == AP_M68040_EA_MEMORY_PREINDEXED ||
                         ea == AP_M68040_EA_MEMORY_PREINDEXED_OD ||
                         ea == AP_M68040_EA_MEMORY_POSTINDEXED ||
                         ea == AP_M68040_EA_MEMORY_POSTINDEXED_OD;
    TEST_ASSERT_EQUAL_INT(indexed, ap_m68040_ea_is_interlocked(ea));
  }
}

/* ---------------------------------------------------------------------------
 * The program-counter base register.
 * ------------------------------------------------------------------------- */

static void test_a_pc_base_register_adds_to_the_lead_not_the_base(void) {
  /* §10.1's supposition 1: "For BR = PC, 1 and 1L clocks to the <ea> calculate
   * and execution times." The execution addition is `1L` -- to the *lead* --
   * so it buys stall tolerance rather than costing a clock outright, and the
   * total execution time is one longer only because the lead is part of it. */
  const ap_m68040_execute_t plain = {.lead = 2u, .base = 1u};
  const ap_m68040_execute_t pc = ap_m68040_pc_relative_execute(plain);
  TEST_ASSERT_EQUAL_UINT(3u, pc.lead);
  TEST_ASSERT_EQUAL_UINT(1u, pc.base);
  TEST_ASSERT_EQUAL_UINT(1u, AP_M68040_PC_RELATIVE_CALCULATE);

  /* And the extra lead really does absorb one more clock of stall. */
  TEST_ASSERT_EQUAL_UINT(1u, ap_m68040_interlock_penalty(plain, 3u));
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68040_interlock_penalty(pc, 3u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_register_and_immediate_modes_reach_no_memory);
  RUN_TEST(test_the_simple_memory_modes_cost_one_access);
  RUN_TEST(test_the_memory_indirect_modes_cost_two);
  RUN_TEST(test_only_the_indirect_modes_read_when_sending_an_address);
  RUN_TEST(test_sending_an_address_never_costs_more_than_fetching);
  RUN_TEST(test_the_fetch_stage_costs_a_clock_even_with_no_operand);
  RUN_TEST(test_the_fetch_stage_costs_a_clock_per_access);
  RUN_TEST(test_the_manuals_worked_example);
  RUN_TEST(test_a_lead_absorbs_stalls_up_to_its_own_size);
  RUN_TEST(test_a_stall_beyond_the_lead_lengthens_the_calculate_stage);
  RUN_TEST(test_a_zero_lead_passes_every_stall_straight_through);
  RUN_TEST(test_the_interlock_applies_only_to_extension_word_modes);
  RUN_TEST(test_every_interlocked_mode_uses_an_index_register);
  RUN_TEST(test_a_pc_base_register_adds_to_the_lead_not_the_base);
  return UNITY_END();
}
