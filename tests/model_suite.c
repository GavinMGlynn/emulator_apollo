/* The model table is the single place the machine's variance lives. These tests
 * state the facts the rest of the emulator is allowed to assume about it, and
 * the documentation discipline it must keep. */

#include <string.h>

#include "model/ap_model.h"
#include "time/ap_time.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_table_holds_every_declared_model(void) {
  TEST_ASSERT_EQUAL_size_t((size_t)AP_MODEL_COUNT, ap_model_count());
  for (size_t i = 0; i < ap_model_count(); ++i) {
    const ap_model_t *m = ap_model_by_id((ap_model_id_t)i);
    TEST_ASSERT_NOT_NULL(m);
    /* The entry must be the one the index asked for: a designated-initialiser
     * table silently yields a zeroed row if an id is ever missed. */
    TEST_ASSERT_EQUAL_INT((int)i, (int)m->id);
    TEST_ASSERT_NOT_NULL(m->name);
    TEST_ASSERT_NOT_NULL(m->description);
  }
}

static void test_every_model_name_is_unique(void) {
  for (size_t i = 0; i < ap_model_count(); ++i) {
    for (size_t j = i + 1u; j < ap_model_count(); ++j) {
      const ap_model_t *a = ap_model_by_id((ap_model_id_t)i);
      const ap_model_t *b = ap_model_by_id((ap_model_id_t)j);
      TEST_ASSERT_NOT_EQUAL_INT(0, strcmp(a->name, b->name));
    }
  }
}

static void test_every_model_is_findable_by_name(void) {
  for (size_t i = 0; i < ap_model_count(); ++i) {
    const ap_model_t *m = ap_model_by_id((ap_model_id_t)i);
    TEST_ASSERT_EQUAL_PTR(m, ap_model_by_name(m->name));
  }
}

static void test_an_unknown_model_name_is_not_found(void) {
  TEST_ASSERT_NULL(ap_model_by_name("dn9999"));
  TEST_ASSERT_NULL(ap_model_by_name(""));
  TEST_ASSERT_NULL(ap_model_by_name(nullptr));
}

static void test_an_out_of_range_model_id_is_not_found(void) {
  TEST_ASSERT_NULL(ap_model_by_id(AP_MODEL_COUNT));
  TEST_ASSERT_NULL(ap_model_by_id((ap_model_id_t)999));
}

/* Every node on the ring must be schedulable on the shared time base. A model
 * whose clock the base cannot represent exactly would drift against the ring. */
static void test_every_model_clock_is_exact_in_the_time_base(void) {
  for (size_t i = 0; i < ap_model_count(); ++i) {
    const ap_model_t *m = ap_model_by_id((ap_model_id_t)i);
    ap_clock_t clk;
    TEST_ASSERT_TRUE_MESSAGE(ap_clock_init(&clk, m->cpu_hz), m->name);
    TEST_ASSERT_EQUAL_UINT64(AP_TIME_BASE_HZ / m->cpu_hz, clk.period);
  }
}

/* The 68020 has no on-chip MMU: the DN3000 and its DSP3000 sibling are the only
 * models needing the external 68851, which is a separate subsystem. */
static void test_only_the_68020_models_use_an_external_pmmu(void) {
  for (size_t i = 0; i < ap_model_count(); ++i) {
    const ap_model_t *m = ap_model_by_id((ap_model_id_t)i);
    if (m->cpu == AP_CPU_M68020) {
      TEST_ASSERT_EQUAL_INT(AP_MMU_M68851, (int)m->mmu);
    } else {
      TEST_ASSERT_NOT_EQUAL_INT(AP_MMU_M68851, (int)m->mmu);
    }
  }
}

/* A DSP is the same board without the display: that is what makes it the cheap
 * node type to run many of on an emulated ring. */
static void test_every_dsp_model_is_headless(void) {
  for (size_t i = 0; i < ap_model_count(); ++i) {
    const ap_model_t *m = ap_model_by_id((ap_model_id_t)i);
    if (strncmp(m->name, "dsp", 3u) == 0) {
      TEST_ASSERT_EQUAL_INT(AP_DISPLAY_NONE, (int)m->display);
    } else {
      TEST_ASSERT_NOT_EQUAL_INT(AP_DISPLAY_NONE, (int)m->display);
    }
  }
}

/* Each DSP must match its DN sibling's CPU, clock, MMU and memory exactly --
 * that is the whole claim being made by modelling it as a subset. */
static void test_each_dsp_matches_its_dn_sibling(void) {
  static const struct {
    ap_model_id_t dsp;
    ap_model_id_t dn;
  } pairs[] = {
      {AP_MODEL_DSP3000, AP_MODEL_DN3000},
      {AP_MODEL_DSP3500, AP_MODEL_DN3500},
      {AP_MODEL_DSP4500, AP_MODEL_DN4500},
      {AP_MODEL_DSP5500, AP_MODEL_DN5500},
  };
  for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; ++i) {
    const ap_model_t *s = ap_model_by_id(pairs[i].dsp);
    const ap_model_t *d = ap_model_by_id(pairs[i].dn);
    TEST_ASSERT_EQUAL_INT((int)d->cpu, (int)s->cpu);
    TEST_ASSERT_EQUAL_UINT32(d->cpu_hz, s->cpu_hz);
    TEST_ASSERT_EQUAL_INT((int)d->mmu, (int)s->mmu);
    TEST_ASSERT_EQUAL_INT((int)d->fpu, (int)s->fpu);
    TEST_ASSERT_EQUAL_UINT32(d->ram_base, s->ram_base);
    TEST_ASSERT_EQUAL_UINT32(d->ram_max_bytes, s->ram_max_bytes);
  }
}

/* The point of the project: every model in scope is a ring node. */
static void test_every_model_supports_the_apollo_token_ring(void) {
  for (size_t i = 0; i < ap_model_count(); ++i) {
    const ap_model_t *m = ap_model_by_id((ap_model_id_t)i);
    TEST_ASSERT_TRUE_MESSAGE(m->has_ring, m->name);
  }
}

/* DN3500 is the reference superset, so it must be a model we can actually check
 * against a runnable oracle. */
static void test_the_reference_superset_has_a_runnable_oracle(void) {
  const ap_model_t *m = ap_model_by_name("dn3500");
  TEST_ASSERT_NOT_NULL(m);
  TEST_ASSERT_EQUAL_INT(AP_ORACLE_MAME, (int)m->oracle);
  TEST_ASSERT_NULL(m->provisional);
}

/* Documentation discipline, enforced rather than hoped for: if a runnable
 * oracle can execute the model, nothing about it should still be a guess. */
static void test_no_model_with_a_runnable_oracle_is_provisional(void) {
  for (size_t i = 0; i < ap_model_count(); ++i) {
    const ap_model_t *m = ap_model_by_id((ap_model_id_t)i);
    if (m->oracle == AP_ORACLE_MAME) {
      TEST_ASSERT_NULL_MESSAGE(m->provisional, m->name);
    }
  }
}

/* Memory must be describable: a zero-length or misplaced region would make the
 * bus map meaningless. */
static void test_every_model_declares_a_nonempty_main_memory(void) {
  for (size_t i = 0; i < ap_model_count(); ++i) {
    const ap_model_t *m = ap_model_by_id((ap_model_id_t)i);
    TEST_ASSERT_GREATER_THAN_UINT32(0u, m->ram_max_bytes);
    /* Main memory always sits above the 1 MB I/O and boot PROM region. */
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(0x100000u, m->ram_base);
  }
}


/* ---------------------------------------------------------------------------
 * Derived CPU features, `[68020]` §7.1.1 and Appendix D, `[68030]` §6 and §9.
 * ------------------------------------------------------------------------- */

/* Both parts hold 256 bytes of instruction cache and organise it differently:
 * the 68020 is "a direct-mapped cache of 64 long word entries", the 68030 is
 * sixteen lines of four. Same size, different line -- and the line is what
 * decides how far one fill reaches. */
static void test_the_68020_and_68030_caches_are_the_same_size_and_not_the_same(void) {
  const ap_cpu_features_t m68020 = ap_cpu_features(AP_CPU_M68020);
  const ap_cpu_features_t m68030 = ap_cpu_features(AP_CPU_M68030);

  TEST_ASSERT_EQUAL_UINT(256u, m68020.instruction_cache_bytes);
  TEST_ASSERT_EQUAL_UINT(256u, m68030.instruction_cache_bytes);
  TEST_ASSERT_EQUAL_UINT(1u, m68020.instruction_cache_line_longs);
  TEST_ASSERT_EQUAL_UINT(4u, m68030.instruction_cache_line_longs);
}

/* The 68020 caches instructions only. Giving it a data cache would make every
 * operand access cheaper than the hardware's, and silently. */
static void test_only_the_68020_lacks_a_data_cache(void) {
  TEST_ASSERT_EQUAL_UINT(0u, ap_cpu_features(AP_CPU_M68020).data_cache_bytes);
  TEST_ASSERT_TRUE(ap_cpu_features(AP_CPU_M68030).data_cache_bytes > 0u);
  TEST_ASSERT_TRUE(ap_cpu_features(AP_CPU_M68040).data_cache_bytes > 0u);
}

/* Burst filling needs synchronous termination, so a part with no `STERM` cannot
 * burst. The implication runs one way and must hold for every family. */
static void test_no_cpu_bursts_without_a_synchronous_bus(void) {
  const ap_cpu_t families[] = {AP_CPU_M68020, AP_CPU_M68030, AP_CPU_M68040};
  for (unsigned i = 0; i < sizeof families / sizeof families[0]; i++) {
    const ap_cpu_features_t features = ap_cpu_features(families[i]);
    if (features.has_burst_fill) {
      TEST_ASSERT_TRUE(features.has_synchronous_bus);
    }
  }
}

/* CALLM and RTM are marked "(MC68020)" in the PRM and exist on no other part.
 * This is what makes the same opcode illegal on a DN3500 and legal on a
 * DN3000. */
static void test_only_the_68020_has_the_module_call_instructions(void) {
  TEST_ASSERT_TRUE(ap_cpu_features(AP_CPU_M68020).has_module_calls);
  TEST_ASSERT_FALSE(ap_cpu_features(AP_CPU_M68030).has_module_calls);
  TEST_ASSERT_FALSE(ap_cpu_features(AP_CPU_M68040).has_module_calls);
}

/* The features are derived from the family, so the model table's own `mmu`
 * field must agree with them: a model claiming a 68020 and an on-chip MMU is a
 * table that contradicts the part. */
static void test_every_models_mmu_agrees_with_its_cpus_features(void) {
  for (size_t i = 0; i < ap_model_count(); i++) {
    const ap_model_t *model = ap_model_by_id((ap_model_id_t)i);
    TEST_ASSERT_NOT_NULL(model);
    const ap_cpu_features_t features = ap_cpu_features(model->cpu);
    const bool table_says_onchip = (model->mmu != AP_MMU_M68851);
    TEST_ASSERT_EQUAL_INT(features.has_onchip_mmu, table_says_onchip);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_table_holds_every_declared_model);
  RUN_TEST(test_every_model_name_is_unique);
  RUN_TEST(test_every_model_is_findable_by_name);
  RUN_TEST(test_an_unknown_model_name_is_not_found);
  RUN_TEST(test_an_out_of_range_model_id_is_not_found);
  RUN_TEST(test_every_model_clock_is_exact_in_the_time_base);
  RUN_TEST(test_only_the_68020_models_use_an_external_pmmu);
  RUN_TEST(test_every_dsp_model_is_headless);
  RUN_TEST(test_each_dsp_matches_its_dn_sibling);
  RUN_TEST(test_every_model_supports_the_apollo_token_ring);
  RUN_TEST(test_the_reference_superset_has_a_runnable_oracle);
  RUN_TEST(test_no_model_with_a_runnable_oracle_is_provisional);
  RUN_TEST(test_every_model_declares_a_nonempty_main_memory);
  RUN_TEST(test_the_68020_and_68030_caches_are_the_same_size_and_not_the_same);
  RUN_TEST(test_only_the_68020_lacks_a_data_cache);
  RUN_TEST(test_no_cpu_bursts_without_a_synchronous_bus);
  RUN_TEST(test_only_the_68020_has_the_module_call_instructions);
  RUN_TEST(test_every_models_mmu_agrees_with_its_cpus_features);
  return UNITY_END();
}
