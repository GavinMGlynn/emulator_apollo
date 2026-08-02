/* Probes run on a constructed machine.
 *
 * The probe *results* are pinned by a golden, which is what asserts the
 * cross-platform and cross-build-type identity claim. This suite checks the
 * things a golden cannot: a golden happily pins a probe that never terminates,
 * or that faults, or that differs run to run -- it would simply record the
 * wrong answer consistently.
 */

#include "probe/ap_probe.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define RAM_BYTES 0x00010000u
static uint8_t ram[RAM_BYTES];
static uint8_t other_ram[RAM_BYTES];

/* Every probe must end because its program said so, not because it ran out of
 * limit. A probe that hits its limit reports whatever it happened to be doing,
 * and its "instructions executed" is the limit rather than a measurement --
 * which is exactly what two of these did before they were given terminators. */
static void test_every_probe_terminates_on_its_own(void) {
  unsigned count = 0;
  const ap_probe_t *probes = ap_probe_all(&count);
  TEST_ASSERT_TRUE(count > 0u);

  for (unsigned i = 0; i < count; i++) {
    const ap_probe_result_t result = ap_probe_run(&probes[i], ram, RAM_BYTES, AP_MODEL_DN3500);
    TEST_ASSERT_EQUAL_INT_MESSAGE(AP_M68030_STEP_STOPPED, result.status,
                                  probes[i].name);
    TEST_ASSERT_TRUE_MESSAGE(result.executed < probes[i].limit,
                             probes[i].name);
  }
}

/* No probe touches memory the machine does not have. A bus error in a probe is
 * a broken probe rather than a finding, and a golden would pin it happily. */
static void test_no_probe_runs_off_its_memory(void) {
  unsigned count = 0;
  const ap_probe_t *probes = ap_probe_all(&count);

  for (unsigned i = 0; i < count; i++) {
    const ap_probe_result_t result = ap_probe_run(&probes[i], ram, RAM_BYTES, AP_MODEL_DN3500);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, result.bus_errors, probes[i].name);
  }
}

/* Running a probe twice gives the same answer, on different memory. If this
 * fails the golden is pinning a coin toss -- and it is the property that makes
 * a probe suite worth running in CI at all. */
static void test_a_probe_gives_the_same_answer_twice(void) {
  unsigned count = 0;
  const ap_probe_t *probes = ap_probe_all(&count);

  for (unsigned i = 0; i < count; i++) {
    const ap_probe_result_t first = ap_probe_run(&probes[i], ram, RAM_BYTES, AP_MODEL_DN3500);
    const ap_probe_result_t second =
        ap_probe_run(&probes[i], other_ram, RAM_BYTES, AP_MODEL_DN3500);

    TEST_ASSERT_EQUAL_HEX64_MESSAGE(first.hash, second.hash, probes[i].name);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(first.executed, second.executed,
                                   probes[i].name);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(first.clocks, second.clocks,
                                     probes[i].name);
  }
}

/* A probe's result must not depend on what the previous probe left behind, or
 * the suite's answer would depend on its order -- and reordering the list, or
 * adding one, would move goldens that have nothing to do with the change. */
static void test_a_probes_result_does_not_depend_on_what_ran_before(void) {
  unsigned count = 0;
  const ap_probe_t *probes = ap_probe_all(&count);
  TEST_ASSERT_TRUE(count >= 2u);

  /* The last probe, run alone. */
  const ap_probe_result_t alone = ap_probe_run(&probes[count - 1u], ram,
                                               RAM_BYTES, AP_MODEL_DN3500);

  /* And the same probe after every other one has used the same memory. */
  for (unsigned i = 0; i < count - 1u; i++) {
    (void)ap_probe_run(&probes[i], ram, RAM_BYTES, AP_MODEL_DN3500);
  }
  const ap_probe_result_t after = ap_probe_run(&probes[count - 1u], ram,
                                               RAM_BYTES, AP_MODEL_DN3500);

  TEST_ASSERT_EQUAL_HEX64(alone.hash, after.hash);
}

/* Two probes must not be the same probe. Distinct hashes are the cheap check
 * that the list is what it looks like -- a copy-paste that left two entries
 * running the same words would otherwise sit in the golden looking deliberate. */
static void test_no_two_probes_are_the_same_probe(void) {
  unsigned count = 0;
  const ap_probe_t *probes = ap_probe_all(&count);

  for (unsigned i = 0; i < count; i++) {
    const ap_probe_result_t a = ap_probe_run(&probes[i], ram, RAM_BYTES, AP_MODEL_DN3500);
    for (unsigned j = i + 1u; j < count; j++) {
      const ap_probe_result_t b = ap_probe_run(&probes[j], other_ram, RAM_BYTES, AP_MODEL_DN3500);
      TEST_ASSERT_NOT_EQUAL_UINT64(a.hash, b.hash);
    }
  }
}

/* Every probe carries a name and a purpose, because a golden diff is read by
 * someone who did not write the probe: a number that moved is only actionable
 * if what it covers is written down. */
static void test_every_probe_says_what_it_is_for(void) {
  unsigned count = 0;
  const ap_probe_t *probes = ap_probe_all(&count);

  for (unsigned i = 0; i < count; i++) {
    TEST_ASSERT_NOT_NULL(probes[i].name);
    TEST_ASSERT_NOT_NULL(probes[i].purpose);
    TEST_ASSERT_TRUE(probes[i].name[0] != '\0');
    TEST_ASSERT_TRUE(probes[i].purpose[0] != '\0');
    TEST_ASSERT_TRUE(probes[i].word_count > 0u);
    TEST_ASSERT_TRUE(probes[i].limit > 0u);
  }
}

/* Every status has a stable name. A golden showing a bare enumerator would need
 * looking up, and one showing "(null)" would be a crash away from useless. */
static void test_every_status_has_a_name(void) {
  const ap_m68030_step_status_t all[] = {
      AP_M68030_STEP_EXECUTED, AP_M68030_STEP_UNIMPLEMENTED,
      AP_M68030_STEP_ILLEGAL,  AP_M68030_STEP_FAULT,
      AP_M68030_STEP_EXCEPTION, AP_M68030_STEP_STOPPED,
  };
  for (unsigned i = 0; i < sizeof all / sizeof all[0]; i++) {
    const char *name = ap_probe_status_name(all[i]);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_TRUE(name[0] != '\0');
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_every_probe_terminates_on_its_own);
  RUN_TEST(test_no_probe_runs_off_its_memory);
  RUN_TEST(test_a_probe_gives_the_same_answer_twice);
  RUN_TEST(test_a_probes_result_does_not_depend_on_what_ran_before);
  RUN_TEST(test_no_two_probes_are_the_same_probe);
  RUN_TEST(test_every_probe_says_what_it_is_for);
  RUN_TEST(test_every_status_has_a_name);
  return UNITY_END();
}
