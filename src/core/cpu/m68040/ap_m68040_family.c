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
            .ptest_effect = AP_M68040_MMU_INSTRUCTION_WORKS,
            .pflush_effect = AP_M68040_MMU_INSTRUCTION_WORKS,
            .supports_8k_pages = true,
            /* "The MC68040 cannot generate or read this stack" frame. */
            .has_format_4_stack_frame = false,
            .boundary_scan_bits = AP_M68040_BOUNDARY_SCAN_BITS,
            .boundary_scan_published = true,
            .internal_reset_clocks = 128u,
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
            .ptest_effect = AP_M68040_MMU_INSTRUCTION_WORKS,
            .pflush_effect = AP_M68040_MMU_INSTRUCTION_WORKS,
            .supports_8k_pages = true,
            .has_format_4_stack_frame = true,
            /* C.6.2: 188 bits, and the definitions "are not currently
             * available" -- a gap the manual declares rather than leaves. */
            .boundary_scan_bits = AP_M68040_V_BOUNDARY_SCAN_BITS,
            .boundary_scan_published = false,
            /* C.4's "124 clocks maximum", against its own Figure C-3's 128. */
            .internal_reset_clocks = 124u,
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
            .ptest_effect = AP_M68040_MMU_INSTRUCTION_WORKS,
            .pflush_effect = AP_M68040_MMU_INSTRUCTION_WORKS,
            .supports_8k_pages = true,
            .has_format_4_stack_frame = true,
            .boundary_scan_bits = AP_M68040_BOUNDARY_SCAN_BITS,
            .boundary_scan_published = true,
            .internal_reset_clocks = 128u,
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
            /* B.6: "execution of the PTEST instruction causes random bus
             * cycles to occur. Execution of the PFLUSH instruction produces
             * indeterminate results. Neither instruction causes the MC68EC040
             * to generate an exception." Two different failures. */
            .ptest_effect = AP_M68040_MMU_INSTRUCTION_RANDOM_BUS_CYCLES,
            .pflush_effect = AP_M68040_MMU_INSTRUCTION_INDETERMINATE,
            /* "A page is defined as a 4-Kbyte block of external memory ... The
             * MC68EC040 does not support 8-Kbyte pages." */
            .supports_8k_pages = false,
            .has_format_4_stack_frame = true,
            .boundary_scan_bits = AP_M68040_BOUNDARY_SCAN_BITS,
            .boundary_scan_published = true,
            .internal_reset_clocks = 128u,
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
            /* §1 left this unstated; Appendix C settles it. "There is no
             * PCLK or TRST pin on either device" and both gain SCD, LFO and
             * LOC, so neither V part is pin compatible -- and §12.2.4 gives
             * them their own pinout, with JS2 where PCLK was. */
            .pin_compatible_with_mc68040 = AP_M68040_FEATURE_ABSENT,
            /* Also settled by Appendix C: "the MC68040V and MC68EC040V are
             * Motorola's 3.3 volt, static versions of the MC68040" and "both
             * devices operate to 0 Hz". §1.1.2's last bullet should have named
             * the pair and named only the MC68040V. */
            .three_volt_static = AP_M68040_FEATURE_PRESENT,
            .dle_pin_name = "JS0",
            .mdis_pin_name = "JS1",
            .ptest_effect = AP_M68040_MMU_INSTRUCTION_RANDOM_BUS_CYCLES,
            .pflush_effect = AP_M68040_MMU_INSTRUCTION_INDETERMINATE,
            .supports_8k_pages = false,
            .has_format_4_stack_frame = true,
            .boundary_scan_bits = AP_M68040_V_BOUNDARY_SCAN_BITS,
            .boundary_scan_published = false,
            .internal_reset_clocks = 124u,
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
