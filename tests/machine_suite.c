/* A constructed machine: a 68030 wired to flat RAM.
 *
 * This is the side-loading probe path's foundation — a machine that needs no
 * firmware, which `tools/mame-oracle/FINDINGS.md` C4 is the reason for building
 * before the boot-PROM route rather than after it. So these tests are written
 * as the things a probe must be able to rely on: set memory up, run, read back,
 * and get the same answer twice.
 */

#include "machine/ap_machine.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define RAM_BYTES 0x00010000u
#define PROGRAM 0x00001000u
#define STACK 0x00009000u

static uint8_t ram[RAM_BYTES];

static void blank(void) {
  for (uint32_t i = 0; i < RAM_BYTES; i++) {
    ram[i] = 0;
  }
}

/* Lay a program down word by word, as a probe encoder would. */
static void load(ap_machine_t *machine, const uint16_t *words, unsigned count) {
  for (unsigned i = 0; i < count; i++) {
    TEST_ASSERT_TRUE(ap_machine_write(machine, PROGRAM + i * 2u, 2u, words[i]));
  }
}

/* The whole cycle a probe performs: construct, poke, run, read back. If this
 * works, a probe needs no firmware and no boot. */
static void test_a_probe_can_set_up_run_and_read_back(void) {
  /* MOVEQ #$42,D0 ; MOVE.L D0,$2000 */
  static const uint16_t program[] = {0x7042u, 0x23C0u, 0x0000u, 0x2000u,
                                     0x4E71u};
  blank();
  ap_machine_t m;
  ap_machine_init(&m, ram, RAM_BYTES);
  ap_machine_reset(&m, PROGRAM, STACK);
  load(&m, program, 5);

  const ap_machine_run_t run = ap_machine_run(&m, 2u);

  TEST_ASSERT_EQUAL_UINT(2u, run.executed);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, run.status);
  TEST_ASSERT_EQUAL_HEX32(0x42u, m.cpu.regs.d[0]);

  uint32_t stored = 0;
  TEST_ASSERT_TRUE(ap_machine_read(&m, 0x2000u, 4u, &stored));
  TEST_ASSERT_EQUAL_HEX32(0x42u, stored);

  /* And nothing went outside the memory the machine has. */
  TEST_ASSERT_EQUAL_UINT(0u, m.bus_errors);
}

/* "Supervisor state with interrupts masked at 7, which is what reset leaves."
 * A probe assuming user state would find every privileged instruction trapping,
 * and one assuming an open interrupt mask would be interrupted by anything a
 * later item wires up. */
static void test_reset_leaves_the_state_a_reset_leaves(void) {
  blank();
  ap_machine_t m;
  ap_machine_init(&m, ram, RAM_BYTES);
  ap_machine_reset(&m, PROGRAM, STACK);

  TEST_ASSERT_TRUE(ap_m68030_supervisor(&m.cpu.regs));
  TEST_ASSERT_EQUAL_UINT(7u, ap_m68030_interrupt_mask(&m.cpu.regs));
  TEST_ASSERT_EQUAL_INT(AP_M68030_TRACE_NONE,
                        ap_m68030_trace_mode(&m.cpu.regs));
  TEST_ASSERT_EQUAL_HEX32(PROGRAM, m.cpu.regs.pc);
  TEST_ASSERT_EQUAL_HEX32(STACK, ap_m68030_read_a7(&m.cpu.regs));
  TEST_ASSERT_EQUAL_UINT64(0u, m.cpu.clocks);
}

/* "Outside the RAM is a bus error, not a wrap and not a zero." Wrapping invents
 * an alias the hardware does not have; reading zero makes an out-of-range probe
 * look like a working one that found empty memory. Both are worse than a fault,
 * and the fault is counted so a probe can say which it was. */
static void test_an_access_beyond_the_ram_faults_rather_than_wrapping(void) {
  blank();
  ap_machine_t m;
  ap_machine_init(&m, ram, RAM_BYTES);
  ap_machine_reset(&m, PROGRAM, STACK);

  /* The operator's view refuses outright. */
  TEST_ASSERT_FALSE(ap_machine_write(&m, RAM_BYTES, 4u, 0x12345678u));
  uint32_t value = 0;
  TEST_ASSERT_FALSE(ap_machine_read(&m, RAM_BYTES, 4u, &value));

  /* And a long word *straddling* the top is refused too: an access is checked
   * as a range, not as a start address, or it would read past the buffer. */
  TEST_ASSERT_FALSE(ap_machine_write(&m, RAM_BYTES - 2u, 4u, 0x12345678u));
  TEST_ASSERT_TRUE(ap_machine_write(&m, RAM_BYTES - 4u, 4u, 0x12345678u));

  /* The processor's view faults and says so: MOVE.L $1000000,D0, far outside. */
  static const uint16_t program[] = {0x2039u, 0x0100u, 0x0000u, 0x4E71u};
  load(&m, program, 4);
  const ap_machine_run_t run = ap_machine_run(&m, 1u);

  TEST_ASSERT_EQUAL_UINT(0u, run.executed);
  TEST_ASSERT_TRUE(m.bus_errors > 0u);
}

/* "A limit is required rather than optional: a probe that loops forever must
 * end as a failed probe rather than as a hung harness." */
static void test_a_runaway_program_ends_at_its_limit(void) {
  /* BRA.B -2: a branch to itself. */
  static const uint16_t program[] = {0x60FEu, 0x4E71u};
  blank();
  ap_machine_t m;
  ap_machine_init(&m, ram, RAM_BYTES);
  ap_machine_reset(&m, PROGRAM, STACK);
  load(&m, program, 2);

  const ap_machine_run_t run = ap_machine_run(&m, 50u);

  TEST_ASSERT_EQUAL_UINT(50u, run.executed);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, run.status);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM, m.cpu.regs.pc);
}

/* A run ends when the processor stops making progress, and reports why — which
 * is what makes "how far did this probe get" answerable. */
static void test_a_run_stops_on_an_unimplemented_instruction_and_says_so(void) {
  /* MOVEQ, then BKPT #0, which decodes and has no semantics. */
  static const uint16_t program[] = {0x7005u, 0x4848u, 0x4E71u};
  blank();
  ap_machine_t m;
  ap_machine_init(&m, ram, RAM_BYTES);
  ap_machine_reset(&m, PROGRAM, STACK);
  load(&m, program, 3);

  const ap_machine_run_t run = ap_machine_run(&m, 20u);

  TEST_ASSERT_EQUAL_UINT(1u, run.executed);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_UNIMPLEMENTED, run.status);
  TEST_ASSERT_EQUAL_HEX32(5u, m.cpu.regs.d[0]);
}

/* An operator write must not leave a stale cache line behind. A probe sets its
 * program up *after* the machine exists, so a machine that let a cached line
 * survive would run the old contents while reporting the new ones — and would
 * do it only sometimes, depending on what a previous probe had touched. */
static void test_writing_memory_does_not_leave_a_stale_cache_line(void) {
  static const uint16_t first[] = {0x7001u, 0x4E71u, 0x4E71u, 0x4E71u};
  static const uint16_t second[] = {0x7002u, 0x4E71u, 0x4E71u, 0x4E71u};
  blank();
  ap_machine_t m;
  ap_machine_init(&m, ram, RAM_BYTES);
  ap_machine_reset(&m, PROGRAM, STACK);

  load(&m, first, 4);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_machine_step(&m).status);
  TEST_ASSERT_EQUAL_HEX32(1u, m.cpu.regs.d[0]);

  /* Rewrite the same address and run it again from the top. */
  ap_machine_reset(&m, PROGRAM, STACK);
  load(&m, second, 4);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_machine_step(&m).status);
  TEST_ASSERT_EQUAL_HEX32(2u, m.cpu.regs.d[0]);
}

/* Two machines built and run the same way agree, on every step — the property a
 * probe's reproducibility rests on, and the one that fails first if anything
 * about the host reaches the state. Two different RAM buffers, so a leaked
 * pointer would show. */
static uint8_t other_ram[RAM_BYTES];

static void test_two_machines_run_the_same_way_hash_alike(void) {
  static const uint16_t program[] = {0x7003u, 0x2200u, 0x23C1u, 0x0000u,
                                     0x2000u, 0x4E71u};
  blank();
  for (uint32_t i = 0; i < RAM_BYTES; i++) {
    other_ram[i] = 0;
  }

  ap_machine_t a;
  ap_machine_init(&a, ram, RAM_BYTES);
  ap_machine_reset(&a, PROGRAM, STACK);
  load(&a, program, 6);

  ap_machine_t b;
  ap_machine_init(&b, other_ram, RAM_BYTES);
  ap_machine_reset(&b, PROGRAM, STACK);
  for (unsigned i = 0; i < 6u; i++) {
    TEST_ASSERT_TRUE(ap_machine_write(&b, PROGRAM + i * 2u, 2u, program[i]));
  }

  TEST_ASSERT_EQUAL_HEX64(ap_machine_hash(&a), ap_machine_hash(&b));
  for (unsigned i = 0; i < 4u; i++) {
    (void)ap_machine_step(&a);
    (void)ap_machine_step(&b);
    TEST_ASSERT_EQUAL_HEX64(ap_machine_hash(&a), ap_machine_hash(&b));
  }
}

/* The machine hash covers the RAM, because a run that left different memory
 * behind is a different run however well its registers agree. Without that, a
 * probe checking a program's *output* would be checking nothing. */
static void test_the_machine_hash_covers_the_memory_a_run_left_behind(void) {
  blank();
  ap_machine_t m;
  ap_machine_init(&m, ram, RAM_BYTES);
  ap_machine_reset(&m, PROGRAM, STACK);
  const uint64_t before = ap_machine_hash(&m);

  TEST_ASSERT_TRUE(ap_machine_write(&m, 0x3000u, 1u, 0x01u));
  TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_machine_hash(&m));
}

/* The two caches are separate objects because the part has two. A machine
 * sharing one would hide every interaction between fetching an instruction and
 * reading data. */
static void test_the_instruction_and_data_caches_are_not_the_same_cache(void) {
  blank();
  ap_machine_t m;
  ap_machine_init(&m, ram, RAM_BYTES);
  ap_machine_reset(&m, PROGRAM, STACK);

  TEST_ASSERT_EQUAL_PTR(&m.instruction_cache, m.instruction_access.cache);
  TEST_ASSERT_EQUAL_PTR(&m.data_cache, m.data_access.cache);

  /* Run something that fetches and loads, then check both filled: a shared
   * cache would show the work in one place. */
  static const uint16_t program[] = {0x2039u, 0x0000u, 0x3000u, 0x4E71u};
  load(&m, program, 4);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_machine_step(&m).status);

  bool instruction_filled = false;
  bool data_filled = false;
  for (unsigned line = 0; line < AP_M68030_CACHE_LINES; line++) {
    for (unsigned entry = 0; entry < AP_M68030_CACHE_ENTRIES; entry++) {
      instruction_filled |= m.instruction_cache.line[line].valid[entry];
      data_filled |= m.data_cache.line[line].valid[entry];
    }
  }
  TEST_ASSERT_TRUE(instruction_filled);
  TEST_ASSERT_TRUE(data_filled);
}

/* Both access contexts point at the CPU's own MMU registers, so a `PMOVE` takes
 * effect on translation rather than only on a register nobody reads. Without
 * this the MMU could be configured by a program and ignored by the machine —
 * the kind of gap that shows up as a boot that nearly works. */
static void test_the_mmu_registers_the_machine_reads_are_the_ones_pmove_writes(
    void) {
  blank();
  ap_machine_t m;
  ap_machine_init(&m, ram, RAM_BYTES);
  ap_machine_reset(&m, PROGRAM, STACK);

  TEST_ASSERT_EQUAL_PTR(&m.cpu.tc, m.instruction_access.tc);
  TEST_ASSERT_EQUAL_PTR(&m.cpu.tc, m.data_access.tc);
  TEST_ASSERT_EQUAL_PTR(&m.cpu.crp, m.data_access.root);
  TEST_ASSERT_EQUAL_PTR(&m.cpu.tt0, m.data_access.tt0);
  TEST_ASSERT_EQUAL_PTR(&m.cpu.tt1, m.data_access.tt1);

  /* PMOVE (A0),TC with a consistent value, and the machine sees it. */
  static const uint16_t program[] = {0xF010u, 0x4000u, 0x4E71u};
  load(&m, program, 3);
  TEST_ASSERT_TRUE(ap_machine_write(&m, 0x5000u, 4u, 0x80808880u));
  m.cpu.regs.a[0] = 0x5000u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_machine_step(&m).status);
  TEST_ASSERT_TRUE(m.instruction_access.tc->enable);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_probe_can_set_up_run_and_read_back);
  RUN_TEST(test_reset_leaves_the_state_a_reset_leaves);
  RUN_TEST(test_an_access_beyond_the_ram_faults_rather_than_wrapping);
  RUN_TEST(test_a_runaway_program_ends_at_its_limit);
  RUN_TEST(test_a_run_stops_on_an_unimplemented_instruction_and_says_so);
  RUN_TEST(test_writing_memory_does_not_leave_a_stale_cache_line);
  RUN_TEST(test_two_machines_run_the_same_way_hash_alike);
  RUN_TEST(test_the_machine_hash_covers_the_memory_a_run_left_behind);
  RUN_TEST(test_the_instruction_and_data_caches_are_not_the_same_cache);
  RUN_TEST(test_the_mmu_registers_the_machine_reads_are_the_ones_pmove_writes);
  return UNITY_END();
}
