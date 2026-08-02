/* Family-aware decode: the two opcodes that make a DN3000 and a DN3500 differ.
 *
 * This is where `ap_cpu_features()` stops being a table and starts changing
 * behaviour -- until now the features were declared and nothing read them.
 */

#include "cpu/m68020/ap_m68020_decode.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_same_word_is_legal_on_a_68020_and_not_on_a_68030(void) {
  /* The sharpest CPU family difference in the core: `CALLM` at `$06D0` runs on
   * a DN3000 and takes an illegal instruction exception on a DN3500. */
  TEST_ASSERT_FALSE(ap_cpu_instruction_is_illegal(0x06D0u, AP_CPU_M68020));
  TEST_ASSERT_TRUE(ap_cpu_instruction_is_illegal(0x06D0u, AP_CPU_M68030));
  TEST_ASSERT_TRUE(ap_cpu_instruction_is_illegal(0x06D0u, AP_CPU_M68040));
}

static void test_rtm_is_legal_on_a_68020_alone(void) {
  for (unsigned reg = 0; reg < 16u; reg++) {
    const uint16_t word = (uint16_t)(0x06C0u | reg);
    TEST_ASSERT_FALSE(ap_cpu_instruction_is_illegal(word, AP_CPU_M68020));
    TEST_ASSERT_TRUE(ap_cpu_instruction_is_illegal(word, AP_CPU_M68030));
  }
}

static void test_a_module_call_is_reported_with_its_fields(void) {
  const ap_cpu_decoded_t rtm = ap_cpu_decode(0x06CDu, AP_CPU_M68020);
  TEST_ASSERT_TRUE(rtm.is_module_call);
  TEST_ASSERT_EQUAL_INT(AP_M68020_MODULE_RTM, rtm.module.opcode);
  TEST_ASSERT_TRUE(rtm.module.rtm_address_register);
  TEST_ASSERT_EQUAL_UINT(5u, rtm.module.rtm_register);

  const ap_cpu_decoded_t callm = ap_cpu_decode(0x06D2u, AP_CPU_M68020);
  TEST_ASSERT_TRUE(callm.is_module_call);
  TEST_ASSERT_EQUAL_INT(AP_M68020_MODULE_CALLM, callm.module.opcode);
  TEST_ASSERT_EQUAL_UINT(2u, callm.module.mode);
  TEST_ASSERT_EQUAL_UINT(2u, callm.module.reg);
}

static void test_the_same_word_is_not_a_module_call_on_a_68030(void) {
  const ap_cpu_decoded_t d = ap_cpu_decode(0x06CDu, AP_CPU_M68030);
  TEST_ASSERT_FALSE(d.is_module_call);
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_ILLEGAL, d.base.kind);
}

static void test_a_callm_with_an_illegal_addressing_mode_stays_illegal(void) {
  /* `(An)+` and `-(An)` are dashed out of CALLM's table, so those words are
   * illegal even on a 68020 -- the family makes an *encoding* legal, not the
   * whole word range. */
  TEST_ASSERT_TRUE(ap_cpu_instruction_is_illegal(0x06DAu, AP_CPU_M68020));
  TEST_ASSERT_TRUE(ap_cpu_instruction_is_illegal(0x06E2u, AP_CPU_M68020));
  /* And the immediate encoding, `111/100`. */
  TEST_ASSERT_TRUE(ap_cpu_instruction_is_illegal(0x06FCu, AP_CPU_M68020));
}

static void test_the_family_never_reinterprets_a_real_instruction(void) {
  /* The wrapper only ever upgrades *illegal* to a module call. `$0680` is
   * `ADDI.L`, one bit below the module prefix, and must decode identically on
   * both families -- a wrapper that claimed word ranges rather than deferring
   * to the shared decoder would swallow it. */
  const ap_cpu_decoded_t on_020 = ap_cpu_decode(0x0680u, AP_CPU_M68020);
  const ap_cpu_decoded_t on_030 = ap_cpu_decode(0x0680u, AP_CPU_M68030);
  TEST_ASSERT_FALSE(on_020.is_module_call);
  TEST_ASSERT_EQUAL_INT(on_030.base.kind, on_020.base.kind);
}

static void test_the_two_families_agree_on_every_other_word(void) {
  /* Sweeping all 65536 opcodes: the families differ on exactly the words the
   * module decoder claims, and nowhere else. This is the check that the 68020
   * really is a superset by two opcodes rather than a separate decoder that
   * happens to look similar. */
  unsigned differ = 0;
  for (uint32_t word = 0; word <= 0xFFFFu; word++) {
    const bool illegal_020 =
        ap_cpu_instruction_is_illegal((uint16_t)word, AP_CPU_M68020);
    const bool illegal_030 =
        ap_cpu_instruction_is_illegal((uint16_t)word, AP_CPU_M68030);
    if (illegal_020 == illegal_030) {
      continue;
    }
    differ++;
    /* Every difference is the 68020 accepting what the 68030 refuses, never
     * the reverse -- the subset relation runs one way. */
    TEST_ASSERT_FALSE(illegal_020);
    TEST_ASSERT_TRUE(illegal_030);
    /* And every one is a module call. */
    TEST_ASSERT_EQUAL_INT(
        AP_M68030_DECODED_ILLEGAL,
        ap_m68030_decode((uint16_t)word).kind);
  }
  /* Sixteen `RTM` words plus the `CALLM` words whose addressing mode is a
   * control mode: modes 010, 101, 110 with eight registers each, plus 111
   * with registers 000-011. */
  const unsigned callm_words = 3u * 8u + 4u;
  TEST_ASSERT_EQUAL_UINT(16u + callm_words, differ);
}

static void test_the_68040_has_no_module_calls_either(void) {
  /* The PRM marks both instructions "(MC68020)". Only one family has them, so
   * a DN5500 refuses them exactly as a DN3500 does. */
  TEST_ASSERT_TRUE(ap_cpu_instruction_is_illegal(0x06D0u, AP_CPU_M68040));
  TEST_ASSERT_FALSE(ap_cpu_decode(0x06D0u, AP_CPU_M68040).is_module_call);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_same_word_is_legal_on_a_68020_and_not_on_a_68030);
  RUN_TEST(test_rtm_is_legal_on_a_68020_alone);
  RUN_TEST(test_a_module_call_is_reported_with_its_fields);
  RUN_TEST(test_the_same_word_is_not_a_module_call_on_a_68030);
  RUN_TEST(test_a_callm_with_an_illegal_addressing_mode_stays_illegal);
  RUN_TEST(test_the_family_never_reinterprets_a_real_instruction);
  RUN_TEST(test_the_two_families_agree_on_every_other_word);
  RUN_TEST(test_the_68040_has_no_module_calls_either);
  return UNITY_END();
}
