/* The five M68040 family members and how they differ, `[040]` §1.1. See the
 * header for what this table does to §5's reset straps. */

#include <stddef.h>

#include "cpu/m68040/ap_m68040_family.h"

static const ap_m68040_family_member_t members[AP_M68040_FAMILY_COUNT] = {
    [AP_M68040_MC68040] =
        {
            .name = "MC68040",
            .has_fpu = true,
            .has_mmu = true,
            .mmu_instructions = AP_M68040_MMU_INSTRUCTIONS_WORK,
            .implements_dle_mode = true,
            .implements_multiplexed_bus = true,
            .implements_buffer_impedance_selection = true,
            .has_low_power_stop = false,
            /* The part the others are compared against. */
            .pin_compatible_with_mc68040 = AP_M68040_FEATURE_PRESENT,
            .three_volt_static = AP_M68040_FEATURE_ABSENT,
            .dle_pin_name = NULL,
            .mdis_pin_name = NULL,
        },
    [AP_M68040_MC68040V] =
        {
            .name = "MC68040V",
            /* "The MC68040V and MC68LC040 ... implement the same IU and MMU as
             * the MC68040, but have no FPU." */
            .has_fpu = false,
            .has_mmu = true,
            .mmu_instructions = AP_M68040_MMU_INSTRUCTIONS_WORK,
            .implements_dle_mode = false,
            .implements_multiplexed_bus = false,
            .implements_buffer_impedance_selection = false,
            /* "The MC68040V has an additional mode of operation, the low-power
             * stop mode of operation." */
            .has_low_power_stop = true,
            /* "The MC68040V is not pin compatible with the MC68040 and contains
             * some additional features" -- three new pins, SCD, LFO and LOC. */
            .pin_compatible_with_mc68040 = AP_M68040_FEATURE_ABSENT,
            .three_volt_static = AP_M68040_FEATURE_PRESENT,
            .dle_pin_name = "JS0",
            .mdis_pin_name = NULL,
        },
    [AP_M68040_MC68LC040] =
        {
            .name = "MC68LC040",
            .has_fpu = false,
            .has_mmu = true,
            .mmu_instructions = AP_M68040_MMU_INSTRUCTIONS_WORK,
            .implements_dle_mode = false,
            .implements_multiplexed_bus = false,
            .implements_buffer_impedance_selection = false,
            .has_low_power_stop = false,
            /* "The MC68LC040 is pin compatible with the MC68040." */
            .pin_compatible_with_mc68040 = AP_M68040_FEATURE_PRESENT,
            .three_volt_static = AP_M68040_FEATURE_ABSENT,
            .dle_pin_name = "JS0",
            .mdis_pin_name = NULL,
        },
    [AP_M68040_MC68EC040] =
        {
            .name = "MC68EC040",
            /* "They implement the same IU as the MC68040, but have no FPU or
             * MMU, which embedded control applications generally do not
             * require." */
            .has_fpu = false,
            .has_mmu = false,
            .mmu_instructions = AP_M68040_MMU_INSTRUCTIONS_UNDETERMINED,
            .implements_dle_mode = false,
            .implements_multiplexed_bus = false,
            .implements_buffer_impedance_selection = false,
            .has_low_power_stop = false,
            /* "The MC68EC040 is pin compatible with the MC68040." */
            .pin_compatible_with_mc68040 = AP_M68040_FEATURE_PRESENT,
            .three_volt_static = AP_M68040_FEATURE_ABSENT,
            .dle_pin_name = "JS0",
            .mdis_pin_name = "JS1",
        },
    [AP_M68040_MC68EC040V] =
        {
            .name = "MC68EC040V",
            .has_fpu = false,
            .has_mmu = false,
            .mmu_instructions = AP_M68040_MMU_INSTRUCTIONS_UNDETERMINED,
            .implements_dle_mode = false,
            .implements_multiplexed_bus = false,
            .implements_buffer_impedance_selection = false,
            /* Table 1-4 note 6 and Table 5-6's encoding 6 both say the low-power
             * stop is on this part as well as the MC68040V, even though §1.1.1
             * mentions only the MC68040V -- that subsection is not about this
             * part. */
            .has_low_power_stop = true,
            /* §1.1.2 says the MC68EC040 is pin compatible and says nothing
             * about this one. Unstated, not inferred from the MC68040V. */
            .pin_compatible_with_mc68040 = AP_M68040_FEATURE_UNSTATED,
            /* §1.1.2's last bullet names the MC68040V where every other bullet
             * in the subsection names the EC parts -- see the header. So this
             * part's voltage and static operation are unstated in §1. */
            .three_volt_static = AP_M68040_FEATURE_UNSTATED,
            .dle_pin_name = "JS0",
            .mdis_pin_name = "JS1",
        },
};

const ap_m68040_family_member_t *
ap_m68040_family(ap_m68040_family_part_t part) {
  if (part >= AP_M68040_FAMILY_COUNT) {
    return NULL;
  }
  return &members[part];
}

bool ap_m68040_has_reset_straps(ap_m68040_family_part_t part) {
  const ap_m68040_family_member_t *member = ap_m68040_family(part);
  if (member == NULL) {
    return false;
  }
  /* All three straps or none: §1.1 revokes them in one sentence per part. */
  return member->implements_dle_mode && member->implements_multiplexed_bus &&
         member->implements_buffer_impedance_selection;
}
