/* MC68851 status and protection registers, `[68851]` §6.1.2 and §6.1.4 through
 * §6.1.8, Figures 6-2 and 6-4 through 6-7, read from the page images.
 *
 * These are the registers the 68020's `CALLM` and `RTM` drive, so several tests
 * below are really about the seam between the two chips.
 */

#include "cpu/m68851/ap_m68851_regs.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------------------
 * PCSR, Figure 6-2.
 * ------------------------------------------------------------------------- */

static void test_the_pcsr_fields(void) {
  /* F@15, LW@14, zeros@13-3, TA@2-0. */
  const ap_m68851_pcsr_t pcsr = ap_m68851_pcsr_decode(0xC005u);
  TEST_ASSERT_TRUE(pcsr.flush);
  TEST_ASSERT_TRUE(pcsr.lock_warning);
  TEST_ASSERT_EQUAL_UINT(5u, pcsr.task_alias);
}

static void test_the_pcsr_reserved_bits_reach_no_field(void) {
  const ap_m68851_pcsr_t pcsr = ap_m68851_pcsr_decode(0x3FF8u);
  TEST_ASSERT_FALSE(pcsr.flush);
  TEST_ASSERT_FALSE(pcsr.lock_warning);
  TEST_ASSERT_EQUAL_UINT(0u, pcsr.task_alias);
  TEST_ASSERT_EQUAL_HEX16(0u, ap_m68851_pcsr_encode(&pcsr));
}

static void test_the_pcsr_round_trips_every_implemented_bit(void) {
  for (unsigned bit = 0; bit < 16u; bit++) {
    const uint16_t value = (uint16_t)(1u << bit);
    if ((value & AP_M68851_PCSR_IMPLEMENTED_MASK) == 0u) {
      continue;
    }
    const ap_m68851_pcsr_t pcsr = ap_m68851_pcsr_decode(value);
    TEST_ASSERT_EQUAL_HEX16(value, ap_m68851_pcsr_encode(&pcsr));
  }
}

/* ---------------------------------------------------------------------------
 * PSR, Figure 6-7.
 * ------------------------------------------------------------------------- */

static void test_each_psr_flag_sits_at_its_own_bit(void) {
  /* Nine adjacent single-bit flags, B down to C, then a gap, then N. Any one
   * of them read a bit out would attribute a fault to the wrong cause. */
  TEST_ASSERT_TRUE(ap_m68851_psr_decode(0x8000u).bus_error);
  TEST_ASSERT_TRUE(ap_m68851_psr_decode(0x4000u).limit_violation);
  TEST_ASSERT_TRUE(ap_m68851_psr_decode(0x2000u).supervisor_only);
  TEST_ASSERT_TRUE(ap_m68851_psr_decode(0x1000u).access_level_violation);
  TEST_ASSERT_TRUE(ap_m68851_psr_decode(0x0800u).write_protected);
  TEST_ASSERT_TRUE(ap_m68851_psr_decode(0x0400u).invalid);
  TEST_ASSERT_TRUE(ap_m68851_psr_decode(0x0200u).modified);
  TEST_ASSERT_TRUE(ap_m68851_psr_decode(0x0100u).gate);
  TEST_ASSERT_TRUE(ap_m68851_psr_decode(0x0080u).globally_sharable);

  /* And exactly one at a time: the bus error bit sets nothing else. */
  const ap_m68851_psr_t only_berr = ap_m68851_psr_decode(0x8000u);
  TEST_ASSERT_FALSE(only_berr.limit_violation);
  TEST_ASSERT_FALSE(only_berr.supervisor_only);
  TEST_ASSERT_FALSE(only_berr.globally_sharable);
  TEST_ASSERT_EQUAL_UINT(0u, only_berr.levels);
}

static void test_the_psr_level_count_is_three_bits(void) {
  /* N is bits 2-0, so up to seven levels are reportable -- more than the four
   * `TC` can describe, because the function code lookup and any indirection
   * add levels the table indices do not name. */
  TEST_ASSERT_EQUAL_UINT(7u, ap_m68851_psr_decode(0x0007u).levels);
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68851_psr_decode(0x0000u).levels);
}

static void test_the_psr_reserved_bits_reach_no_field(void) {
  /* Bits 6-3 are drawn as zeros between C and N. */
  const ap_m68851_psr_t psr = ap_m68851_psr_decode(0x0078u);
  TEST_ASSERT_EQUAL_UINT(0u, psr.levels);
  TEST_ASSERT_FALSE(psr.globally_sharable);
  TEST_ASSERT_EQUAL_HEX16(0u, ap_m68851_psr_encode(&psr));
}

static void test_the_psr_round_trips_every_implemented_bit(void) {
  for (unsigned bit = 0; bit < 16u; bit++) {
    const uint16_t value = (uint16_t)(1u << bit);
    if ((value & AP_M68851_PSR_IMPLEMENTED_MASK) == 0u) {
      continue;
    }
    const ap_m68851_psr_t psr = ap_m68851_psr_decode(value);
    TEST_ASSERT_EQUAL_HEX16(value, ap_m68851_psr_encode(&psr));
  }
}

/* ---------------------------------------------------------------------------
 * AC, Figure 6-6.
 * ------------------------------------------------------------------------- */

static void test_the_ac_fields(void) {
  /* MC@7, ALC@5-4, MDS@1-0, everything else zero. */
  const ap_m68851_ac_t ac = ap_m68851_ac_decode(0x00B3u);
  TEST_ASSERT_TRUE(ac.module_control);
  TEST_ASSERT_EQUAL_INT(AP_M68851_ALC_THREE_BITS, ac.access_level_control);
  TEST_ASSERT_EQUAL_INT(AP_M68851_MDS_64_BYTE, ac.module_descriptor_size);
}

static void test_the_ac_reserved_bits_reach_no_field(void) {
  /* Bits 15-8, 6, 3 and 2: two of the holes are single bits between live
   * fields, which is where a mask goes wrong. */
  const ap_m68851_ac_t ac = ap_m68851_ac_decode(0xFF4Cu);
  TEST_ASSERT_FALSE(ac.module_control);
  TEST_ASSERT_EQUAL_INT(AP_M68851_ALC_DISABLED, ac.access_level_control);
  TEST_ASSERT_EQUAL_INT(AP_M68851_MDS_ALL_INVALID, ac.module_descriptor_size);
  TEST_ASSERT_EQUAL_HEX16(0u, ap_m68851_ac_encode(&ac));
}

static void test_the_ac_round_trips_every_implemented_bit(void) {
  for (unsigned bit = 0; bit < 16u; bit++) {
    const uint16_t value = (uint16_t)(1u << bit);
    if ((value & AP_M68851_AC_IMPLEMENTED_MASK) == 0u) {
      continue;
    }
    const ap_m68851_ac_t ac = ap_m68851_ac_decode(value);
    TEST_ASSERT_EQUAL_HEX16(value, ap_m68851_ac_encode(&ac));
  }
}

static void test_the_four_access_level_control_encodings(void) {
  /* §6.1.7.2: the field is a count of *address bits*, so the level count is
   * two raised to it -- and `$0` disables checking rather than meaning one
   * level, which is the encoding a naive reading gets wrong. */
  ap_m68851_ac_t ac = {0};
  ac.access_level_control = AP_M68851_ALC_DISABLED;
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68851_ac_access_levels(&ac));
  ac.access_level_control = AP_M68851_ALC_ONE_BIT;
  TEST_ASSERT_EQUAL_UINT(2u, ap_m68851_ac_access_levels(&ac));
  ac.access_level_control = AP_M68851_ALC_TWO_BITS;
  TEST_ASSERT_EQUAL_UINT(4u, ap_m68851_ac_access_levels(&ac));
  ac.access_level_control = AP_M68851_ALC_THREE_BITS;
  TEST_ASSERT_EQUAL_UINT(8u, ap_m68851_ac_access_levels(&ac));
}

static void test_the_access_level_ceiling_matches_the_cal_register_width(void) {
  /* Eight levels is the maximum `ALC` can select and three bits is all `CAL`
   * implements. The two facts have to agree or one of them is transcribed
   * wrongly. */
  ap_m68851_ac_t ac = {.access_level_control = AP_M68851_ALC_THREE_BITS};
  TEST_ASSERT_EQUAL_UINT(8u, ap_m68851_ac_access_levels(&ac));
  TEST_ASSERT_EQUAL_UINT(7u, ap_m68851_access_level_decode(0xFFu));
}

static void test_the_four_module_descriptor_sizes(void) {
  /* §6.1.7.3. This is a rule about the 68020's data structure enforced by the
   * MMU: it fixes the boundary a module descriptor may fall on. */
  ap_m68851_ac_t ac = {0};
  ac.module_descriptor_size = AP_M68851_MDS_16_BYTE;
  TEST_ASSERT_EQUAL_UINT32(16u, ap_m68851_ac_module_descriptor_alignment(&ac));
  ac.module_descriptor_size = AP_M68851_MDS_32_BYTE;
  TEST_ASSERT_EQUAL_UINT32(32u, ap_m68851_ac_module_descriptor_alignment(&ac));
  ac.module_descriptor_size = AP_M68851_MDS_64_BYTE;
  TEST_ASSERT_EQUAL_UINT32(64u, ap_m68851_ac_module_descriptor_alignment(&ac));
}

static void test_module_descriptor_size_zero_invalidates_every_address(void) {
  /* "$0 -- All Module Descriptors are Invalid." No alignment satisfies it,
   * which is not at all the same as every alignment satisfying it -- and zero
   * is the address most likely to slip through the second reading. */
  const ap_m68851_ac_t ac = {.module_descriptor_size =
                                 AP_M68851_MDS_ALL_INVALID};
  TEST_ASSERT_EQUAL_UINT32(0u, ap_m68851_ac_module_descriptor_alignment(&ac));
  TEST_ASSERT_FALSE(ap_m68851_ac_module_descriptor_aligned(&ac, 0u));
  TEST_ASSERT_FALSE(ap_m68851_ac_module_descriptor_aligned(&ac, 0x1000u));
}

static void test_module_descriptor_alignment_is_checked_against_the_boundary(void) {
  const ap_m68851_ac_t ac = {.module_descriptor_size = AP_M68851_MDS_32_BYTE};
  TEST_ASSERT_TRUE(ap_m68851_ac_module_descriptor_aligned(&ac, 0x1000u));
  TEST_ASSERT_TRUE(ap_m68851_ac_module_descriptor_aligned(&ac, 0x1020u));
  TEST_ASSERT_FALSE(ap_m68851_ac_module_descriptor_aligned(&ac, 0x1010u));
  /* A 16-byte-aligned descriptor is not enough when the field says 32. */
  TEST_ASSERT_FALSE(ap_m68851_ac_module_descriptor_aligned(&ac, 0x1030u));
}

/* ---------------------------------------------------------------------------
 * CAL, VAL and SCC, Figures 6-4 and 6-5.
 * ------------------------------------------------------------------------- */

static void test_an_access_level_lives_in_the_upper_three_bits(void) {
  /* "The register is eight bits wide, but only the upper three bits are
   * implemented." Upper, so it lines up with the top of a logical address --
   * a model that stored the level at the bottom would compare it against the
   * wrong field. */
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68851_access_level_decode(0x00u));
  TEST_ASSERT_EQUAL_UINT(1u, ap_m68851_access_level_decode(0x20u));
  TEST_ASSERT_EQUAL_UINT(7u, ap_m68851_access_level_decode(0xE0u));
  TEST_ASSERT_EQUAL_HEX8(0xE0u, ap_m68851_access_level_encode(7u));
}

static void test_the_unimplemented_access_level_bits_are_ignored(void) {
  /* "Unimplemented bits always read as zeros and are ignored when written." */
  TEST_ASSERT_EQUAL_UINT(ap_m68851_access_level_decode(0xE0u),
                         ap_m68851_access_level_decode(0xFFu));
  TEST_ASSERT_EQUAL_HEX8(0u, ap_m68851_access_level_encode(0u));
  for (unsigned level = 0; level < 8u; level++) {
    TEST_ASSERT_EQUAL_UINT(level, ap_m68851_access_level_decode(
                                      ap_m68851_access_level_encode(level)));
  }
}

static void test_a_call_that_does_not_increase_privilege_changes_no_stack(void) {
  /* The rule's premise is "m < n (greater privilege)". A call to an equal or
   * lesser privilege is outside it, whatever SCC holds. */
  TEST_ASSERT_FALSE(ap_m68851_scc_changes_stack(0xFFu, 3u, 3u));
  TEST_ASSERT_FALSE(ap_m68851_scc_changes_stack(0xFFu, 3u, 5u));
}

static void test_a_set_bit_anywhere_in_the_range_changes_the_stack(void) {
  /* "If any bit of SCC between n and m (inclusive) is set." The interesting
   * case is a bit at neither endpoint: calling from level 5 to level 1 with
   * only level 3 set still changes the stack, because the call crosses it. */
  TEST_ASSERT_TRUE(ap_m68851_scc_changes_stack(0x08u, 5u, 1u)); /* bit 3 */
  /* Both endpoints, inclusive. */
  TEST_ASSERT_TRUE(ap_m68851_scc_changes_stack(0x20u, 5u, 1u)); /* bit 5 = n */
  TEST_ASSERT_TRUE(ap_m68851_scc_changes_stack(0x02u, 5u, 1u)); /* bit 1 = m */
}

static void test_a_bit_outside_the_range_changes_no_stack(void) {
  /* Levels the call does not cross are irrelevant, which is what makes this a
   * range test rather than a test of the register as a whole. */
  TEST_ASSERT_FALSE(ap_m68851_scc_changes_stack(0x40u, 5u, 1u)); /* bit 6 */
  TEST_ASSERT_FALSE(ap_m68851_scc_changes_stack(0x01u, 5u, 1u)); /* bit 0 */
  TEST_ASSERT_FALSE(ap_m68851_scc_changes_stack(0x41u, 5u, 1u)); /* both */
}

static void test_an_empty_scc_never_changes_the_stack(void) {
  for (unsigned current = 0; current < 8u; current++) {
    for (unsigned target = 0; target < 8u; target++) {
      TEST_ASSERT_FALSE(ap_m68851_scc_changes_stack(0x00u, current, target));
    }
  }
}

static void test_a_full_scc_changes_the_stack_on_every_privilege_increase(void) {
  /* The complement: with every bit set, exactly the calls that increase
   * privilege change the stack and no others. */
  for (unsigned current = 0; current < 8u; current++) {
    for (unsigned target = 0; target < 8u; target++) {
      TEST_ASSERT_EQUAL_INT(target < current,
                            ap_m68851_scc_changes_stack(0xFFu, current, target));
    }
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_pcsr_fields);
  RUN_TEST(test_the_pcsr_reserved_bits_reach_no_field);
  RUN_TEST(test_the_pcsr_round_trips_every_implemented_bit);
  RUN_TEST(test_each_psr_flag_sits_at_its_own_bit);
  RUN_TEST(test_the_psr_level_count_is_three_bits);
  RUN_TEST(test_the_psr_reserved_bits_reach_no_field);
  RUN_TEST(test_the_psr_round_trips_every_implemented_bit);
  RUN_TEST(test_the_ac_fields);
  RUN_TEST(test_the_ac_reserved_bits_reach_no_field);
  RUN_TEST(test_the_ac_round_trips_every_implemented_bit);
  RUN_TEST(test_the_four_access_level_control_encodings);
  RUN_TEST(test_the_access_level_ceiling_matches_the_cal_register_width);
  RUN_TEST(test_the_four_module_descriptor_sizes);
  RUN_TEST(test_module_descriptor_size_zero_invalidates_every_address);
  RUN_TEST(test_module_descriptor_alignment_is_checked_against_the_boundary);
  RUN_TEST(test_an_access_level_lives_in_the_upper_three_bits);
  RUN_TEST(test_the_unimplemented_access_level_bits_are_ignored);
  RUN_TEST(test_a_call_that_does_not_increase_privilege_changes_no_stack);
  RUN_TEST(test_a_set_bit_anywhere_in_the_range_changes_the_stack);
  RUN_TEST(test_a_bit_outside_the_range_changes_no_stack);
  RUN_TEST(test_an_empty_scc_never_changes_the_stack);
  RUN_TEST(test_a_full_scc_changes_the_stack_on_every_privilege_increase);
  return UNITY_END();
}
