/* The five M68040 family members, `[040]` §1.1 with Table 1-4's notes.
 *
 * The tests that matter are the ones where §1 changes what a later section
 * means -- above all §5's three reset straps, which exist on one part of five.
 */

#include <string.h>

#include "cpu/m68040/ap_m68040_family.h"
#include "cpu/m68040/ap_m68040_signals.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_family_has_five_members(void) {
  /* "The MC68040, MC68040V, MC68LC040, MC68EC040, and MC68EC040V (collectively
   * called M68040)". */
  TEST_ASSERT_EQUAL_UINT(5u, (unsigned)AP_M68040_FAMILY_COUNT);
  for (unsigned p = 0; p < (unsigned)AP_M68040_FAMILY_COUNT; p++) {
    const ap_m68040_family_member_t *m =
        ap_m68040_family((ap_m68040_family_part_t)p);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_TRUE(strlen(m->name) > 0u);
  }
  TEST_ASSERT_NULL(ap_m68040_family(AP_M68040_FAMILY_COUNT));
}

static void test_only_the_mc68040_has_a_floating_point_unit(void) {
  /* "The MC68040 contains an MC68881/MC68882-compatible floating-point unit."
   * Every other member says "do not contain an FPU", and Table 1-4's note 2
   * marks every F-instruction "available only on the MC68040". */
  TEST_ASSERT_TRUE(ap_m68040_family(AP_M68040_MC68040)->has_fpu);
  TEST_ASSERT_FALSE(ap_m68040_family(AP_M68040_MC68040V)->has_fpu);
  TEST_ASSERT_FALSE(ap_m68040_family(AP_M68040_MC68LC040)->has_fpu);
  TEST_ASSERT_FALSE(ap_m68040_family(AP_M68040_MC68EC040)->has_fpu);
  TEST_ASSERT_FALSE(ap_m68040_family(AP_M68040_MC68EC040V)->has_fpu);
}

static void test_only_the_ec_parts_lack_a_memory_management_unit(void) {
  /* The MC68040V and MC68LC040 "implement the same IU and MMU as the MC68040";
   * the EC parts "have no FPU or MMU". */
  TEST_ASSERT_TRUE(ap_m68040_family(AP_M68040_MC68040V)->has_mmu);
  TEST_ASSERT_TRUE(ap_m68040_family(AP_M68040_MC68LC040)->has_mmu);
  TEST_ASSERT_FALSE(ap_m68040_family(AP_M68040_MC68EC040)->has_mmu);
  TEST_ASSERT_FALSE(ap_m68040_family(AP_M68040_MC68EC040V)->has_mmu);
}

static void test_the_mmu_instructions_are_hazardous_not_absent(void) {
  /* "PTEST and PFLUSH instructions cause an undetermined number of bus cycles;
   * the user should not execute these instructions." Table 1-4's note 8 says
   * only "not available", which is the softer of the two statements; §1.1.2 is
   * the one to model against, because "not available" reads as a trap and this
   * is not one. */
  TEST_ASSERT_EQUAL_INT(AP_M68040_MMU_INSTRUCTIONS_UNDETERMINED,
                        ap_m68040_family(AP_M68040_MC68EC040)->mmu_instructions);
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_MMU_INSTRUCTIONS_UNDETERMINED,
      ap_m68040_family(AP_M68040_MC68EC040V)->mmu_instructions);
  TEST_ASSERT_EQUAL_INT(AP_M68040_MMU_INSTRUCTIONS_WORK,
                        ap_m68040_family(AP_M68040_MC68LC040)->mmu_instructions);
}

static void test_section_fives_reset_straps_exist_on_one_part(void) {
  /* This is why §1 had to be read after §5 rather than instead of it. All four
   * derivatives "do not implement the data latch enable (DLE), multiplexed, or
   * output buffer impedance selection modes of operation. They implement only
   * the small output buffer mode of operation." Those three modes are exactly
   * what CDIS, MDIS and IPL2-IPL0 select at reset. */
  TEST_ASSERT_TRUE(ap_m68040_has_reset_straps(AP_M68040_MC68040));
  TEST_ASSERT_FALSE(ap_m68040_has_reset_straps(AP_M68040_MC68040V));
  TEST_ASSERT_FALSE(ap_m68040_has_reset_straps(AP_M68040_MC68LC040));
  TEST_ASSERT_FALSE(ap_m68040_has_reset_straps(AP_M68040_MC68EC040));
  TEST_ASSERT_FALSE(ap_m68040_has_reset_straps(AP_M68040_MC68EC040V));
}

static void test_the_three_modes_are_revoked_together(void) {
  /* One sentence per part revokes all three, so no member has some of them. */
  for (unsigned p = 0; p < (unsigned)AP_M68040_FAMILY_COUNT; p++) {
    const ap_m68040_family_member_t *m =
        ap_m68040_family((ap_m68040_family_part_t)p);
    TEST_ASSERT_EQUAL_INT(m->implements_dle_mode ? 1 : 0,
                          m->implements_multiplexed_bus ? 1 : 0);
    TEST_ASSERT_EQUAL_INT(m->implements_dle_mode ? 1 : 0,
                          m->implements_buffer_impedance_selection ? 1 : 0);
  }
}

static void test_the_dle_pin_is_renamed_not_removed(void) {
  /* This is what settles §5's three-way disagreement about DLE's scope. "The
   * DLE pin name has been changed to JS0" on the MC68040V and MC68LC040, and
   * "the DLE and MDIS pin names have been changed to JS0 and JS1" on the EC
   * parts. The pin is there on all five; DLE the *function* is MC68040-only,
   * exactly as Table 5-1 says and Table 5-7's note does not. */
  TEST_ASSERT_NULL(ap_m68040_family(AP_M68040_MC68040)->dle_pin_name);
  for (unsigned p = 1; p < (unsigned)AP_M68040_FAMILY_COUNT; p++) {
    TEST_ASSERT_EQUAL_STRING(
        "JS0", ap_m68040_family((ap_m68040_family_part_t)p)->dle_pin_name);
  }
  /* And the signal table follows Table 5-1 rather than Table 5-7. */
  TEST_ASSERT_EQUAL_INT(AP_M68040_PART_MC68040_ONLY,
                        ap_m68040_signal("DLE")->scope);
}

static void test_mdis_is_renamed_on_the_ec_parts_alone(void) {
  /* Because they are the parts with no MMU to disable. */
  TEST_ASSERT_EQUAL_STRING("JS1",
                           ap_m68040_family(AP_M68040_MC68EC040)->mdis_pin_name);
  TEST_ASSERT_EQUAL_STRING(
      "JS1", ap_m68040_family(AP_M68040_MC68EC040V)->mdis_pin_name);
  TEST_ASSERT_NULL(ap_m68040_family(AP_M68040_MC68LC040)->mdis_pin_name);
  TEST_ASSERT_NULL(ap_m68040_family(AP_M68040_MC68040V)->mdis_pin_name);
  /* Matching the signal table's scope for MDIS, from Table 5-1's note. */
  TEST_ASSERT_EQUAL_INT(AP_M68040_PART_NOT_EC040_FAMILY,
                        ap_m68040_signal("MDIS")->scope);
}

static void test_the_low_power_stop_belongs_to_both_v_parts(void) {
  /* §1.1.1 mentions only the MC68040V because that subsection is not about the
   * MC68EC040V. Table 1-4's note 6 -- LPSTOP "available only on the MC68040V
   * and MC68EC040V" -- and Table 5-6's PST encoding 6 both name the pair. */
  TEST_ASSERT_TRUE(ap_m68040_family(AP_M68040_MC68040V)->has_low_power_stop);
  TEST_ASSERT_TRUE(ap_m68040_family(AP_M68040_MC68EC040V)->has_low_power_stop);
  TEST_ASSERT_FALSE(ap_m68040_family(AP_M68040_MC68LC040)->has_low_power_stop);
  TEST_ASSERT_FALSE(ap_m68040_family(AP_M68040_MC68EC040)->has_low_power_stop);
  TEST_ASSERT_FALSE(ap_m68040_family(AP_M68040_MC68040)->has_low_power_stop);
  /* And the status encoding that reports it is the one §5.9.1's own
   * classification forgot. */
  TEST_ASSERT_FALSE(ap_m68040_pst_is_classified(AP_M68040_PST_LOW_POWER_STOP));
}

static void test_two_properties_of_the_ec040v_are_unstated(void) {
  /* §1.1.2's last bullet names the MC68040V -- word for word §1.1.1's closing
   * sentence -- in a subsection about the EC parts, so this part's voltage and
   * static operation are not stated. Nor is its pin compatibility: §1.1.2 says
   * the MC68EC040 is compatible and says nothing about the other. Recorded as
   * unstated rather than inferred from the MC68040V; Appendix C covers both V
   * parts and is where the answer would be. */
  const ap_m68040_family_member_t *ec040v =
      ap_m68040_family(AP_M68040_MC68EC040V);
  TEST_ASSERT_EQUAL_INT(AP_M68040_FEATURE_UNSTATED, ec040v->three_volt_static);
  TEST_ASSERT_EQUAL_INT(AP_M68040_FEATURE_UNSTATED,
                        ec040v->pin_compatible_with_mc68040);
  /* The MC68040V's own bullet is stated twice, so it is not in doubt. */
  TEST_ASSERT_EQUAL_INT(AP_M68040_FEATURE_PRESENT,
                        ap_m68040_family(AP_M68040_MC68040V)->three_volt_static);
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_FEATURE_ABSENT,
      ap_m68040_family(AP_M68040_MC68040V)->pin_compatible_with_mc68040);
}

static void test_only_the_mc68040v_is_not_pin_compatible(void) {
  /* "The MC68LC040 is pin compatible with the MC68040. The MC68040V is not
   * ... and contains some additional features" -- SCD, LFO and LOC. */
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_FEATURE_PRESENT,
      ap_m68040_family(AP_M68040_MC68LC040)->pin_compatible_with_mc68040);
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_FEATURE_PRESENT,
      ap_m68040_family(AP_M68040_MC68EC040)->pin_compatible_with_mc68040);
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_FEATURE_ABSENT,
      ap_m68040_family(AP_M68040_MC68040V)->pin_compatible_with_mc68040);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_family_has_five_members);
  RUN_TEST(test_only_the_mc68040_has_a_floating_point_unit);
  RUN_TEST(test_only_the_ec_parts_lack_a_memory_management_unit);
  RUN_TEST(test_the_mmu_instructions_are_hazardous_not_absent);
  RUN_TEST(test_section_fives_reset_straps_exist_on_one_part);
  RUN_TEST(test_the_three_modes_are_revoked_together);
  RUN_TEST(test_the_dle_pin_is_renamed_not_removed);
  RUN_TEST(test_mdis_is_renamed_on_the_ec_parts_alone);
  RUN_TEST(test_the_low_power_stop_belongs_to_both_v_parts);
  RUN_TEST(test_two_properties_of_the_ec040v_are_unstated);
  RUN_TEST(test_only_the_mc68040v_is_not_pin_compatible);
  return UNITY_END();
}
