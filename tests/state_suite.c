/* MC68030 state hashing.
 *
 * The failure mode this module must not have is a field that changes while the
 * hash does not: the identity harness would then report two diverging machines
 * as the same one, and every optimisation checked under it would be checked
 * against nothing. So the bulk of this suite is a sweep — perturb one field,
 * demand the hash moves — and a field added without a sweep entry is a gap
 * visible in this file rather than one nobody can see.
 */

#include "cpu/m68030/ap_m68030_state.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define FC_SUPERVISOR_PROGRAM 6u

typedef struct {
  ap_m68030_cache_t instruction_cache;
  ap_m68030_cache_t data_cache;
  ap_m68030_atc_t atc;
  ap_m68030_tc_t tc;
  ap_m68030_root_t root;
  ap_m68030_access_ctx_t instruction_access;
  ap_m68030_access_ctx_t data_access;
  ap_m68030_cpu_t cpu;
} machine_t;

static void make_machine(machine_t *m) {
  ap_m68030_cache_clear(&m->instruction_cache);
  ap_m68030_cache_clear(&m->data_cache);
  ap_m68030_atc_flush(&m->atc);

  m->instruction_access = (ap_m68030_access_ctx_t){
      .cache = &m->instruction_cache,
      .atc = &m->atc,
      .tc = &m->tc,
      .root = &m->root,
      .cache_enabled = true,
  };
  m->data_access = m->instruction_access;
  m->data_access.cache = &m->data_cache;

  m->cpu = (ap_m68030_cpu_t){0};
  m->cpu.fetch.access = &m->instruction_access;
  m->cpu.fetch.function_code = FC_SUPERVISOR_PROGRAM;
  m->cpu.data = &m->data_access;
  m->cpu.data_function_code = AP_M68030_FC_SUPERVISOR_DATA;
}

/* The same state hashes the same, and two machines built the same way agree --
 * which is the property everything else rests on, and the one that fails first
 * if a host pointer ever reaches the hash. Two machines at different addresses
 * must still agree. */
static void test_two_identically_built_machines_hash_alike(void) {
  machine_t first = {0};
  machine_t second = {0};
  make_machine(&first);
  make_machine(&second);

  TEST_ASSERT_EQUAL_HEX64(ap_m68030_state_hash(&first.cpu),
                          ap_m68030_state_hash(&second.cpu));

  /* And hashing twice is not itself a change. */
  TEST_ASSERT_EQUAL_HEX64(ap_m68030_state_hash(&first.cpu),
                          ap_m68030_state_hash(&first.cpu));
}

/* The sweep. Every field of the programming model, perturbed one at a time. A
 * register the hash skipped would let a diverging run report as identical. */
static void test_every_register_reaches_the_hash(void) {
  for (unsigned i = 0; i < 8u; i++) {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.regs.d[i] = 1u;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
  for (unsigned i = 0; i < 7u; i++) {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.regs.a[i] = 1u;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
}

/* All three stack pointers are fed, not A7 through whichever is active: two
 * states differing only in the *inactive* stack are still different machines,
 * and a model hashing A7 alone would call them equal. */
static void test_the_inactive_stack_pointers_still_reach_the_hash(void) {
  machine_t m = {0};
  make_machine(&m);
  /* Supervisor state, so A7 is the ISP and the USP is inactive. */
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  const uint64_t before = ap_m68030_state_hash(&m.cpu);

  m.cpu.regs.usp = 0x00007000u;
  TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));

  machine_t n = {0};
  make_machine(&n);
  n.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  n.cpu.regs.msp = 0x00008000u;
  TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&n.cpu));
}

/* The rest of the programming model and the processor's own state, each
 * perturbed alone. */
static void test_every_processor_field_reaches_the_hash(void) {
  {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.regs.pc = 4u;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
  {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
  {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.regs.vbr = 0x3000u;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
  {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.regs.sfc = 5u;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
  {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.regs.dfc = 5u;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
  {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.caar = 0x40u;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
  {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.mmusr = 0x0400u;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
  {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.stopped = true;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
  {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.external_resets = 1u;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
  {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.interrupt_level = 4u;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
  {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.previous_interrupt_level = 4u;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
  {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.pending_vector = 5u;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
  {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.extension_words = 2u;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
  {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.data_function_code = 1u;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
}

/* The MMU registers, which are CPU state on this part. */
static void test_every_mmu_register_reaches_the_hash(void) {
  {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.tc.enable = true;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
  {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.tc.table_index[2] = 4u;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
  {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.crp.table_address = 0x8000u;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
  {
    /* The two root pointers are fed separately: a machine with them exchanged
     * translates supervisor and user accesses through the wrong trees. */
    machine_t m = {0};
    make_machine(&m);
    m.cpu.crp.table_address = 0x8000u;
    const uint64_t before = ap_m68030_state_hash(&m.cpu);

    machine_t n = {0};
    make_machine(&n);
    n.cpu.srp.table_address = 0x8000u;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&n.cpu));
  }
  {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.tt0.enabled = true;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
  {
    /* And TT0 against TT1, for the same reason. */
    machine_t m = {0};
    make_machine(&m);
    m.cpu.tt0.logical_base = 0x40u;
    const uint64_t before = ap_m68030_state_hash(&m.cpu);

    machine_t n = {0};
    make_machine(&n);
    n.cpu.tt1.logical_base = 0x40u;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&n.cpu));
  }
  {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.cacr.enable_data = true;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
}

/* The pipe is architectural: a branch empties it, and whether the holding
 * register is valid decides whether the next word costs a bus cycle. Two
 * machines with the same registers and different pipes are not the same. */
static void test_the_pipe_and_holding_register_reach_the_hash(void) {
  {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.fetch.pipe.d.valid = true;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
  {
    /* The same word in a different stage is a different state: it is one
     * prefetch further from being executed. */
    machine_t m = {0};
    make_machine(&m);
    m.cpu.fetch.pipe.b.word = 0x4E71u;
    const uint64_t before = ap_m68030_state_hash(&m.cpu);

    machine_t n = {0};
    make_machine(&n);
    n.cpu.fetch.pipe.c.word = 0x4E71u;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&n.cpu));
  }
  {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.fetch.pipe.holding_valid = true;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
  {
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.fetch.address = 0x1000u;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
  {
    /* What prefetching has cost. Timing state in the same sense as
     * `cpu->clocks`, and not derivable from it: two runs reaching the same
     * total by different splits between instruction and operand bus cycles are
     * different runs, and that split is what §11.6's model turns on. */
    machine_t m = {0};
    make_machine(&m);
    const uint64_t before = ap_m68030_state_hash(&m.cpu);
    m.cpu.fetch.bus_clocks += 2u;
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
  }
}

/* The caches and the ATC, and the two caches told apart from each other: a
 * machine with the instruction and data caches exchanged behaves differently
 * and must not hash the same. */
static void test_both_caches_and_the_atc_reach_the_hash(void) {
  machine_t m = {0};
  make_machine(&m);
  const uint64_t empty = ap_m68030_state_hash(&m.cpu);

  m.instruction_cache.line[3].valid[1] = true;
  m.instruction_cache.line[3].entry[1] = 0x12345678u;
  const uint64_t instruction_filled = ap_m68030_state_hash(&m.cpu);
  TEST_ASSERT_NOT_EQUAL_UINT64(empty, instruction_filled);

  machine_t n = {0};
  make_machine(&n);
  n.data_cache.line[3].valid[1] = true;
  n.data_cache.line[3].entry[1] = 0x12345678u;
  /* The same line filled on the *other* side. */
  TEST_ASSERT_NOT_EQUAL_UINT64(instruction_filled, ap_m68030_state_hash(&n.cpu));

  machine_t o = {0};
  make_machine(&o);
  (void)ap_m68030_atc_insert(&o.atc, 5u, 0x00020000u, 8u, 0x00090000u, false,
                             false, false, false);
  TEST_ASSERT_NOT_EQUAL_UINT64(empty, ap_m68030_state_hash(&o.cpu));
}

/* An invalid ATC entry still holds the history bit the replacement algorithm
 * reads, so two ATCs differing only there choose different victims on the next
 * miss -- a divergence that would otherwise appear a long way from its cause. */
static void test_an_invalid_atc_entry_still_carries_its_history_bit(void) {
  machine_t m = {0};
  make_machine(&m);
  const uint64_t before = ap_m68030_state_hash(&m.cpu);

  m.atc.entry[0].history = true;
  TEST_ASSERT_FALSE(m.atc.entry[0].valid);
  TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
}

/* "Timing is state." Two runs reaching the same registers by different numbers
 * of bus cycles are not the same run on a machine whose whole claim is emergent
 * timing, and a hash that omitted the clock would call them equal -- which is
 * precisely the divergence a fast mode introduces. */
static void test_the_accumulated_clock_reaches_the_hash(void) {
  machine_t m = {0};
  make_machine(&m);
  const uint64_t before = ap_m68030_state_hash(&m.cpu);

  m.cpu.clocks = 1u;
  TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_m68030_state_hash(&m.cpu));
}

/* A missing access context is a marker rather than nothing: "no data side at
 * all" and "a data side whose cache is empty" are different machines, and
 * feeding nothing for the first would make them hash alike. */
static void test_an_absent_access_context_is_not_an_empty_one(void) {
  machine_t m = {0};
  make_machine(&m);
  const uint64_t present = ap_m68030_state_hash(&m.cpu);

  m.cpu.data = NULL;
  TEST_ASSERT_NOT_EQUAL_UINT64(present, ap_m68030_state_hash(&m.cpu));
}

/* The hash must cover what an access can *reach*, and no more. A cache clear
 * clears valid bits and leaves the tag and data behind -- by design, and the
 * module says so -- so an invalid entry holds whatever the last occupant left
 * and no lookup can return it.
 *
 * Hashing it anyway is a *false positive*: two machines that behave identically
 * hash differently. That is worse than a miss, because a harness which rejects
 * identical machines cannot be used at all -- and it is how this was found, by
 * two machines built the same way on two different RAM buffers disagreeing. */
static void test_stale_data_behind_an_invalid_cache_entry_is_not_state(void) {
  machine_t m = {0};
  make_machine(&m);
  const uint64_t clean = ap_m68030_state_hash(&m.cpu);

  /* A line the processor cannot reach: no entry valid, but a tag and data left
   * over from a previous occupant. */
  m.instruction_cache.line[5].tag = 0xDEADBEEFu;
  m.instruction_cache.line[5].entry[0] = 0x12345678u;
  m.instruction_cache.line[5].entry[3] = 0x9ABCDEF0u;
  TEST_ASSERT_EQUAL_HEX64(clean, ap_m68030_state_hash(&m.cpu));

  /* Validate one entry and it becomes reachable, so it counts -- along with the
   * tag, which is per line and shared. */
  m.instruction_cache.line[5].valid[0] = true;
  TEST_ASSERT_NOT_EQUAL_UINT64(clean, ap_m68030_state_hash(&m.cpu));
}

/* The same for the ATC, with one exception that must survive: an invalid
 * entry's translation is unreachable, but its **history bit** is read by the
 * replacement algorithm whatever the valid bit says, so two ATCs differing only
 * there choose different victims on the next miss. */
static void test_only_the_history_bit_survives_an_invalid_atc_entry(void) {
  machine_t m = {0};
  make_machine(&m);
  const uint64_t clean = ap_m68030_state_hash(&m.cpu);

  /* An unreachable translation left behind by a flush. */
  m.atc.entry[2].function_code = 5u;
  m.atc.entry[2].logical = 0x0002A000u;
  m.atc.entry[2].physical = 0x00090000u;
  m.atc.entry[2].write_protect = true;
  TEST_ASSERT_FALSE(m.atc.entry[2].valid);
  TEST_ASSERT_EQUAL_HEX64(clean, ap_m68030_state_hash(&m.cpu));

  /* The history bit is different: it is read regardless. */
  m.atc.entry[2].history = true;
  TEST_ASSERT_NOT_EQUAL_UINT64(clean, ap_m68030_state_hash(&m.cpu));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_two_identically_built_machines_hash_alike);
  RUN_TEST(test_every_register_reaches_the_hash);
  RUN_TEST(test_the_inactive_stack_pointers_still_reach_the_hash);
  RUN_TEST(test_every_processor_field_reaches_the_hash);
  RUN_TEST(test_every_mmu_register_reaches_the_hash);
  RUN_TEST(test_the_pipe_and_holding_register_reach_the_hash);
  RUN_TEST(test_both_caches_and_the_atc_reach_the_hash);
  RUN_TEST(test_an_invalid_atc_entry_still_carries_its_history_bit);
  RUN_TEST(test_stale_data_behind_an_invalid_cache_entry_is_not_state);
  RUN_TEST(test_only_the_history_bit_survives_an_invalid_atc_entry);
  RUN_TEST(test_the_accumulated_clock_reaches_the_hash);
  RUN_TEST(test_an_absent_access_context_is_not_an_empty_one);
  return UNITY_END();
}
