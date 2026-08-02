/* A constructed machine: a 68030 wired to flat RAM.
 *
 * This is the side-loading probe path's foundation — a machine that needs no
 * firmware, which `tools/mame-oracle/FINDINGS.md` C4 is the reason for building
 * before the boot-PROM route rather than after it. So these tests are written
 * as the things a probe must be able to rely on: set memory up, run, read back,
 * and get the same answer twice.
 */

#include "board/ap_board.h"
#include "cpu/m68030/ap_m68030_ea_timing.h"
#include "cpu/m68030/ap_m68030_state.h"
#include "cpu/m68030/ap_m68030_timing_table.h"
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

  /* The bus error is *taken*, not merely reported -- which is what the real
   * part does, and why the run reports an exception rather than stopping. The
   * fault itself is the assertion here; where the processor went next is the
   * step module's business. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, run.status);
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
  /* MOVEQ, then `CAS2`, which decodes and has no semantics -- BKPT held this
   * role until its breakpoint acknowledge cycle landed. */
  static const uint16_t program[] = {0x7005u, 0x0CFCu, 0x4E71u};
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

/* ---------------------------------------------------------------------------
 * The two-sided check on the published figures.
 * ------------------------------------------------------------------------- */

/* Run one single-word instruction repeatedly and return the clocks each step
 * cost, after a discarded first step so the pipe fill is not charged to it.
 * `warm` runs the same instruction from one address so the cache answers;
 * `!warm` walks forward through fresh memory so every fetch misses.
 *
 * **The data cache is disabled**, because §11.6's assumption list says the
 * published figures assume it is: "The data cache is not enabled." Comparing a
 * total measured with it on against a figure computed with it off is not a
 * like-for-like comparison, and the difference is exactly one operand read per
 * repeat -- invisible for the register forms, which is why this went unnoticed
 * until the memory forms were measured. */
static void sample_instruction(uint16_t word, bool warm, uint64_t *out,
                               unsigned samples) {
  blank();
  ap_machine_t m;
  ap_machine_init(&m, ram, RAM_BYTES);
  m.data_access.cache_enabled = false;
  ap_machine_reset(&m, PROGRAM, STACK);

  for (unsigned i = 0; i < samples + 8u; i++) {
    TEST_ASSERT_TRUE(ap_machine_write(&m, PROGRAM + i * 2u, 2u, word));
  }

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_machine_step(&m).status);
  uint64_t previous = m.cpu.clocks;

  for (unsigned i = 0; i < samples; i++) {
    if (warm) {
      /* Back to the same instruction, which the cache now holds. */
      m.cpu.regs.pc = PROGRAM;
      ap_m68030_fetch_reset(&m.cpu.fetch, PROGRAM);
    }
    TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_machine_step(&m).status);
    out[i] = m.cpu.clocks - previous;
    previous = m.cpu.clocks;
  }
}

/* The check the whole transcription rests on: for every row the table covers,
 * a warm-cache run must come to that row's `CC` and a cold-cache run, averaged
 * over both prefetch alignments, to its `NCC`.
 *
 * Two published numbers bracketing the same execution. A mistranscribed figure
 * fails one; a scheduling model that adds where it should overlap fails the
 * other; and a model that ignored the bus entirely would pass the warm side and
 * fail the cold. Neither number is one this project produced.
 *
 * The instructions are chosen to be executable in isolation with no operand and
 * no side effect that stops the run -- the divides are excluded because a
 * divisor of zero raises an exception, and their figures are the manual's
 * maximum rather than a value anyway. */
static void test_every_transcribed_row_matches_both_published_columns(void) {
  static const struct {
    uint16_t word;
    const char *what;
  } CASES[] = {
      {0xD200u, "ADD.B D0,D1"},  {0x9200u, "SUB.B D0,D1"},
      {0xC200u, "AND.B D0,D1"},  {0x8200u, "OR.B D0,D1"},
      {0xB200u, "CMP.B D0,D1"},  {0x7000u, "MOVEQ #0,D0"},
      {0x5200u, "ADDQ.B #1,D0"}, {0x5300u, "SUBQ.B #1,D0"},
      {0xD0C0u, "ADDA.W D0,A0"}, {0xD1C0u, "ADDA.L D0,A0"},
      {0x90C0u, "SUBA.W D0,A0"}, {0x91C0u, "SUBA.L D0,A0"},
      {0xB0C0u, "CMPA.W D0,A0"},  {0x2200u, "MOVE.L D0,D1"},
      {0x2240u, "MOVEA.L D0,A1"},
      /* §11.6.11's single-operand register forms. */
      {0x4280u, "CLR.L D0"},      {0x4480u, "NEG.L D0"},
      {0x4680u, "NOT.L D0"},      {0x4A80u, "TST.L D0"},
      /* §11.6.12's immediate-count shifts, including the pair whose costs
       * differ by direction. */
      {0xE288u, "LSR.L #1,D0"},   {0xE388u, "LSL.L #1,D0"},
      {0xE280u, "ASR.L #1,D0"},   {0xE380u, "ASL.L #1,D0"},
      {0xE298u, "ROR.L #1,D0"},   {0xE290u, "ROXR.L #1,D0"},
      /* §11.6.16: NOP, whose 2 clocks the oracle measured independently. */
      {0x4E71u, "NOP"},
  };

  for (unsigned c = 0; c < sizeof CASES / sizeof CASES[0]; c++) {
    const ap_m68030_table_entry_t *row =
        ap_m68030_timing_for_word(CASES[c].word);
    TEST_ASSERT_NOT_NULL_MESSAGE(row, CASES[c].what);

    uint64_t warm[4];
    sample_instruction(CASES[c].word, true, warm, 4u);
    for (unsigned i = 0; i < 4u; i++) {
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(row->timing.cache_case, warm[i],
                                       CASES[c].what);
    }

    /* Cold: the published figure is "the average of the odd-word-aligned case
     * and the even-word-aligned case (rounded up)", so the average is what must
     * agree -- comparing a single step to it would be comparing a value to a
     * mean of two. */
    uint64_t cold[4];
    sample_instruction(CASES[c].word, false, cold, 4u);
    const uint64_t total = cold[0] + cold[1] + cold[2] + cold[3];
    const uint64_t rounded_average = (total + 3u) / 4u;
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(row->timing.no_cache_case,
                                     rounded_average, CASES[c].what);
  }
}

/* Run one single-word instruction whose operand is memory, with A0 pointing at
 * RAM well clear of the program so the access is a real bus cycle rather than a
 * refused one. Otherwise as `sample_instruction`: a discarded first step, then
 * the interval between consecutive steps. */
static void sample_memory_form(uint16_t word, bool warm, uint64_t *out,
                               unsigned samples) {
  blank();
  ap_machine_t m;
  ap_machine_init(&m, ram, RAM_BYTES);
  /* As above: §11.6's figures assume the data cache is not enabled, and for a
   * memory operand that assumption is the difference between a read costing two
   * clocks and costing none. */
  m.data_access.cache_enabled = false;
  ap_machine_reset(&m, PROGRAM, STACK);

  for (unsigned i = 0; i < samples + 8u; i++) {
    TEST_ASSERT_TRUE(ap_machine_write(&m, PROGRAM + i * 2u, 2u, word));
  }
  m.cpu.regs.a[0] = 0x00004000u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_machine_step(&m).status);
  uint64_t previous = m.cpu.clocks;

  for (unsigned i = 0; i < samples; i++) {
    if (warm) {
      m.cpu.regs.pc = PROGRAM;
      ap_m68030_fetch_reset(&m.cpu.fetch, PROGRAM);
    }
    TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_machine_step(&m).status);
    out[i] = m.cpu.clocks - previous;
    previous = m.cpu.clocks;
  }
}

/* **C9's row, closed.** §11.6.8 footnotes `ADD Dn,EA` with "Add Fetch Effective
 * Address Time", so its published 3 is a *component*; §11.6.1 gives `(An)`
 * another 3, and Equation (11-2) composes them to 6 in the cache case. The
 * oracle measures the whole instruction at 7.
 *
 * Both are now asserted here: **warm 6, and cold averaging 7**. The second is
 * the one that could not be checked before, and it is checked as an average
 * because §11.3.3 says the published no-cache figure is one -- "the average of
 * the odd-word-aligned case and the even-word-aligned case (rounded up)". Our
 * core alternates 6 and 8 with alignment, which is the behaviour §11.3.3
 * describes and the oracle's flat 7 does not (`FINDINGS.md` C7).
 *
 * An earlier version of this test compared our total against `CC` and `NCC` and
 * passed — because both sides were the same component. That is precisely the
 * blind spot C9 records: a check that compares like with like cannot see that
 * the like is partial. */
static void test_the_footnoted_memory_forms_compose_to_the_manuals_total(void) {
  static const struct {
    uint16_t word;
    const char *what;
  } CASES[] = {
      {0xD110u, "ADD.B D0,(A0)"}, {0x9110u, "SUB.B D0,(A0)"},
      {0xC110u, "AND.B D0,(A0)"}, {0x8110u, "OR.B D0,(A0)"},
      {0xB110u, "EOR.B D0,(A0)"},
  };

  for (unsigned c = 0; c < sizeof CASES / sizeof CASES[0]; c++) {
    const ap_m68030_table_entry_t *row =
        ap_m68030_timing_for_word(CASES[c].word);
    TEST_ASSERT_NOT_NULL_MESSAGE(row, CASES[c].what);
    /* The row says it is partial, and which table completes it. */
    TEST_ASSERT_EQUAL_INT_MESSAGE(AP_M68030_EA_TIME_FETCH,
                                  row->effective_address_time, CASES[c].what);

    const ap_m68030_ea_timing_t *fea =
        ap_m68030_ea_fetch_timing(AP_M68030_EA_ADDRESS_INDIRECT, 1u);
    TEST_ASSERT_NOT_NULL(fea);

    /* Equation (11-2): CCea + [CCop - min(Hop,Tea)] = 3 + [3 - min(0,1)] = 6. */
    ap_m68030_overlap_state_t composed = ap_m68030_overlap_begin();
    ap_m68030_ea_timing_compose(&composed, fea, &row->timing);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(6u, ap_m68030_overlap_total(&composed),
                                     CASES[c].what);

    uint64_t warm[4];
    sample_memory_form(CASES[c].word, true, warm, 4u);
    for (unsigned i = 0; i < 4u; i++) {
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(6u, warm[i], CASES[c].what);
    }

    /* And the no-cache case: NCCop + NCCea = 4 + 3 = 7, by §11.3.3's plain
     * addition, which is also what the oracle measured. */
    uint64_t cold[4];
    sample_memory_form(CASES[c].word, false, cold, 4u);
    const uint64_t total = cold[0] + cold[1] + cold[2] + cold[3];
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(7u, (total + 3u) / 4u, CASES[c].what);

    /* The alternation is real and not an artefact of averaging four equal
     * numbers: the two alignments differ by exactly the prefetch's two
     * clocks. A flat model would average to 7 as well and would be wrong about
     * every individual instruction, which is the distinction C7 records. */
    bool saw_six = false;
    bool saw_eight = false;
    for (unsigned i = 0; i < 4u; i++) {
      saw_six = saw_six || cold[i] == 6u;
      saw_eight = saw_eight || cold[i] == 8u;
    }
    TEST_ASSERT_TRUE_MESSAGE(saw_six, CASES[c].what);
    TEST_ASSERT_TRUE_MESSAGE(saw_eight, CASES[c].what);
  }
}

/* MOVE's register-source memory destinations carry **no** footnote — the table
 * gives `MOVE Rn,(An)` complete at 3, and only `MOVE SOURCE,(An)`, with a
 * memory source, needs an address time. So these are priced, and they are the
 * rows that still exercise both published columns. */
static void test_the_unfootnoted_memory_moves_match_both_columns(void) {
  static const struct {
    uint16_t word;
    const char *what;
  } CASES[] = {
      {0x2080u, "MOVE.L D0,(A0)"},
      {0x20C0u, "MOVE.L D0,(A0)+"},
  };

  for (unsigned c = 0; c < sizeof CASES / sizeof CASES[0]; c++) {
    const ap_m68030_table_entry_t *row =
        ap_m68030_timing_for_word(CASES[c].word);
    TEST_ASSERT_NOT_NULL_MESSAGE(row, CASES[c].what);
    TEST_ASSERT_EQUAL_INT_MESSAGE(AP_M68030_EA_TIME_NONE,
                                  row->effective_address_time, CASES[c].what);
    TEST_ASSERT_TRUE_MESSAGE(row->timing.no_cache_case >
                                 row->timing.cache_case,
                             CASES[c].what);

    uint64_t warm[4];
    sample_memory_form(CASES[c].word, true, warm, 4u);
    for (unsigned i = 0; i < 4u; i++) {
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(row->timing.cache_case, warm[i],
                                       CASES[c].what);
    }

    uint64_t cold[4];
    sample_memory_form(CASES[c].word, false, cold, 4u);
    const uint64_t total = cold[0] + cold[1] + cold[2] + cold[3];
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(row->timing.no_cache_case,
                                     (total + 3u) / 4u, CASES[c].what);
  }
}

/* `MOVE Rn,-(An)` is the row that would be flattened by a model with one
 * "memory destination" cost. §11.6.6 gives it `CC 4(0/0/1)` where `(An)` and
 * `(An)+` are 3, and a **tail of 2** where they have 1 — the predecrement costs
 * a clock the postincrement does not.
 *
 * Its `NCC` is also 4, equal to its `CC`, where the other two go to 4 from 3.
 * So the predecrement form is *already* long enough to hide its own fetch and
 * the others are not, which is the same fact seen from the other side. */
static void test_the_predecrement_move_costs_more_than_the_postincrement(void) {
  const ap_m68030_table_entry_t *postinc =
      ap_m68030_timing_for_word(0x20C0u); /* MOVE.L D0,(A0)+ */
  const ap_m68030_table_entry_t *predec =
      ap_m68030_timing_for_word(0x2100u); /* MOVE.L D0,-(A0) */
  TEST_ASSERT_NOT_NULL(postinc);
  TEST_ASSERT_NOT_NULL(predec);

  TEST_ASSERT_EQUAL_UINT(3u, postinc->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(4u, predec->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(1u, postinc->timing.tail);
  TEST_ASSERT_EQUAL_UINT(2u, predec->timing.tail);

  /* And the predecrement's two columns agree while the postincrement's differ:
   * four clocks of microcode already cover a prefetch, three do not. */
  TEST_ASSERT_EQUAL_UINT(predec->timing.cache_case,
                         predec->timing.no_cache_case);
  TEST_ASSERT_TRUE(postinc->timing.no_cache_case >
                   postinc->timing.cache_case);

  /* Run it: the stack pointer is not involved, so A0 walks down through RAM. */
  uint64_t warm[4];
  sample_memory_form(0x2100u, true, warm, 4u);
  for (unsigned i = 0; i < 4u; i++) {
    TEST_ASSERT_EQUAL_UINT64(predec->timing.cache_case, warm[i]);
  }
}

/* `ASL` costs six clocks where `ASR` costs four, for the same immediate count.
 * That is not a quirk of the table: "V is set if the most significant bit is
 * changed at any time during the shift operation" applies to the arithmetic
 * *left* shift and to nothing else, so ASL watches the sign bit for the whole
 * shift and ASR does not. The extra clocks are that extra work.
 *
 * `ap_m68030_alu_shift` already implements exactly that asymmetry -- it tracks
 * `msb_changed` only for a left arithmetic shift -- so this is one place where
 * a published timing and an independently written behaviour agree about which
 * instruction does more. A transcription that had them the same way round would
 * be contradicted by the ALU's own code. */
static void test_the_left_arithmetic_shift_costs_more_than_the_right(void) {
  const ap_m68030_table_entry_t *asl = ap_m68030_timing_for_word(0xE380u);
  const ap_m68030_table_entry_t *asr = ap_m68030_timing_for_word(0xE280u);
  TEST_ASSERT_NOT_NULL(asl);
  TEST_ASSERT_NOT_NULL(asr);

  TEST_ASSERT_EQUAL_UINT(6u, asl->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(4u, asr->timing.cache_case);

  /* The logical shifts have no such rule and no such difference. */
  const ap_m68030_table_entry_t *lsl = ap_m68030_timing_for_word(0xE388u);
  const ap_m68030_table_entry_t *lsr = ap_m68030_timing_for_word(0xE288u);
  TEST_ASSERT_NOT_NULL(lsl);
  TEST_ASSERT_NOT_NULL(lsr);
  TEST_ASSERT_EQUAL_UINT(lsl->timing.cache_case, lsr->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(4u, lsr->timing.cache_case);
}

/* The register-count shifts are marked `%` and `+` -- "shift count is less than
 * or equal to the size of data" and "greater than size of data" -- so their
 * cost depends on a value the table cannot publish. They are not transcribed,
 * and the lookup says so rather than returning the immediate-count row, which
 * would under-count a long shift by half. */
static void test_a_register_count_shift_is_not_transcribed(void) {
  /* LSR.L D1,D0: bit 5 set means the count is in a register. */
  TEST_ASSERT_NULL(ap_m68030_timing_for_word(0xE2A8u));
  /* And the immediate-count form beside it is. */
  TEST_ASSERT_NOT_NULL(ap_m68030_timing_for_word(0xE288u));
}

/* A branch's cost is not a function of its opcode. §11.6.15 gives a taken `Bcc`
 * 6 clocks and an untaken *byte* `Bcc` 4, so the same instruction word costs
 * differently depending on a condition evaluated at run time -- which is why
 * these rows are reached through the outcome rather than through a table
 * lookup that would have to pick one case and be wrong half the time. */
static void test_a_taken_branch_costs_more_than_an_untaken_one(void) {
  /* BEQ.B +2 with Z clear (not taken), then with Z set (taken). Both are the
   * same instruction word; only the flags differ. */
  static const uint16_t program[] = {0x6700u, 0x6702u, 0x4E71u, 0x4E71u,
                                     0x4E71u, 0x4E71u};
  blank();
  ap_machine_t m;
  ap_machine_init(&m, ram, RAM_BYTES);
  ap_machine_reset(&m, PROGRAM, STACK);
  for (unsigned i = 0; i < 6u; i++) {
    TEST_ASSERT_TRUE(ap_machine_write(&m, PROGRAM + i * 2u, 2u, 0x6702u));
  }
  (void)program;

  /* Z clear: not taken, and the byte form is 4 clocks. */
  ap_m68030_write_ccr(&m.cpu.regs, 0);
  (void)ap_machine_step(&m);
  uint64_t before = m.cpu.clocks;
  m.cpu.regs.pc = PROGRAM;
  ap_m68030_fetch_reset(&m.cpu.fetch, PROGRAM);
  (void)ap_machine_step(&m);
  const uint64_t not_taken = m.cpu.clocks - before;

  /* Z set: taken, and a taken branch is 6 whatever its displacement size. */
  ap_m68030_write_ccr(&m.cpu.regs,
                      (uint16_t)(1u << AP_M68030_SR_Z_BIT));
  m.cpu.regs.pc = PROGRAM;
  ap_m68030_fetch_reset(&m.cpu.fetch, PROGRAM);
  before = m.cpu.clocks;
  (void)ap_machine_step(&m);
  const uint64_t taken = m.cpu.clocks - before;

  TEST_ASSERT_EQUAL_UINT64(4u, not_taken);
  TEST_ASSERT_EQUAL_UINT64(6u, taken);
}

/* `DBcc` has three published cases, and the expensive one is *leaving* the
 * loop: 10 clocks with the counter expired against 6 going round again. A
 * model with one DBcc cost would make every loop's last iteration four clocks
 * cheap, which is a systematic under-count proportional to how many loops a
 * program runs. */
static void test_leaving_a_dbcc_loop_costs_more_than_going_round(void) {
  /* MOVEQ #2,D1 ; DBF D1,-2  -- three iterations, then expiry. */
  static const uint16_t program[] = {0x7202u, 0x51C9u, 0xFFFEu, 0x4E71u,
                                     0x4E71u};
  blank();
  ap_machine_t m;
  ap_machine_init(&m, ram, RAM_BYTES);
  ap_machine_reset(&m, PROGRAM, STACK);
  for (unsigned i = 0; i < 5u; i++) {
    TEST_ASSERT_TRUE(ap_machine_write(&m, PROGRAM + i * 2u, 2u, program[i]));
  }

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_machine_step(&m).status);

  uint64_t looping = 0;
  uint64_t expired = 0;
  for (unsigned i = 0; i < 3u; i++) {
    const uint64_t before = m.cpu.clocks;
    TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_machine_step(&m).status);
    const uint64_t cost = m.cpu.clocks - before;
    if (i < 2u) {
      looping = cost;
    } else {
      expired = cost;
    }
  }

  TEST_ASSERT_EQUAL_UINT64(6u, looping);
  TEST_ASSERT_EQUAL_UINT64(10u, expired);
}

/* `BSR` is unconditional, and its condition *field* is `F` -- the encoding that
 * means "never" for a `Bcc`. A branch-timing lookup reading the condition
 * without excluding BSR would price every subroutine call as an untaken branch,
 * which is the same trap the step's own execution fell into once. */
static void test_bsr_is_priced_as_a_call_not_as_an_untaken_branch(void) {
  const ap_m68030_table_entry_t *bsr =
      ap_m68030_timing_for_branch(0x6100u, true);
  const ap_m68030_table_entry_t *also_bsr =
      ap_m68030_timing_for_branch(0x6100u, false);
  TEST_ASSERT_NOT_NULL(bsr);
  TEST_ASSERT_NOT_NULL(also_bsr);

  /* The same row either way: `taken` does not apply to an unconditional call. */
  TEST_ASSERT_EQUAL_PTR(bsr, also_bsr);
  TEST_ASSERT_EQUAL_UINT(6u, bsr->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(9u, bsr->timing.no_cache_case);

  /* And it is not the untaken-byte row, which is what reading the condition
   * field naively would have produced. */
  const ap_m68030_table_entry_t *untaken =
      ap_m68030_timing_for_branch(0x6702u, false);
  TEST_ASSERT_NOT_NULL(untaken);
  TEST_ASSERT_EQUAL_UINT(4u, untaken->timing.cache_case);
}


/* Time is counted in `AP_TIME_BASE_HZ` units, never CPU cycles: several nodes of
 * different models share one ring, so no CPU's cycle is a legal unit of account.
 * The conversion happens once, in the run loop, and this pins that it happens at
 * all and at the right rate. */
static void test_the_machine_keeps_time_in_base_units(void) {
  static const uint16_t program[] = {0x4E71u, 0x4E71u, 0x4E71u, 0x4E71u};
  blank();
  ap_machine_t m;
  ap_machine_init(&m, ram, RAM_BYTES);
  ap_machine_reset(&m, PROGRAM, STACK);
  load(&m, program, 4);
  TEST_ASSERT_TRUE(ap_machine_set_cpu_hz(&m, 25000000u));

  TEST_ASSERT_EQUAL_UINT64(0u, ap_machine_now(&m));
  const ap_machine_run_t run = ap_machine_run(&m, 2u);
  TEST_ASSERT_EQUAL_UINT(2u, run.executed);

  /* Whatever the instructions cost, the machine's clock is that many CPU
   * cycles expressed in base units — not the cycle count itself. */
  const ap_time_t expected =
      ap_clock_duration(&m.cpu_clock, m.cpu.clocks);
  TEST_ASSERT_EQUAL_UINT64(expected, ap_machine_now(&m));
  TEST_ASSERT_TRUE(ap_machine_now(&m) > 0u);
}

/* A rate the base cannot represent is refused rather than rounded. Rounding
 * would put a machine a fraction of a cycle out per tick and hide it in a unit
 * nobody reads directly — which is the whole reason the base is derived from
 * every clock in the machine instead of chosen. */
static void test_an_unrepresentable_cpu_rate_is_refused(void) {
  static const uint16_t program[] = {0x4E71u, 0x4E71u};
  blank();
  ap_machine_t m;
  ap_machine_init(&m, ram, RAM_BYTES);
  ap_machine_reset(&m, PROGRAM, STACK);
  load(&m, program, 2);

  TEST_ASSERT_TRUE(ap_machine_set_cpu_hz(&m, 25000000u));
  TEST_ASSERT_FALSE(ap_machine_set_cpu_hz(&m, 7u));
}

/* A machine whose clock was never set produces no time at all, which is
 * visibly wrong rather than quietly approximate. A default rate would be a
 * figure nobody chose appearing in every measurement. */
static void test_an_unset_clock_produces_no_time(void) {
  static const uint16_t program[] = {0x4E71u, 0x4E71u};
  blank();
  ap_machine_t m;
  ap_machine_init(&m, ram, RAM_BYTES);
  ap_machine_reset(&m, PROGRAM, STACK);
  load(&m, program, 2);

  (void)ap_machine_run(&m, 2u);
  TEST_ASSERT_EQUAL_UINT64(0u, ap_machine_now(&m));
}

/* ---------------------------------------------------------------------------
 * The whole-machine state hash: the board's half, the clock, and what is
 * reported beside the number.
 *
 * `board_state_suite` sweeps the devices field by field. What is checked here is
 * the join: that a machine's hash reaches the board it is attached to, that the
 * elapsed time counts, that the counters do not, and the property the item
 * exists for -- the same workload twice gives the same number.
 * ------------------------------------------------------------------------- */

/* Two boards, so a workload can be run twice on two different sets of devices
 * at two different addresses. Static because a board carries the translation
 * map, the calendar's RAM and a tape block buffer. */
static ap_board_t first_board;
static ap_board_t second_board;

static const ap_mc146818_time_t board_epoch = {
    .year = 1987u, .month = 7u, .day = 31u, .day_of_week = 6u,
    .hour = 21u, .minute = 9u, .second = 21u,
};

/* A program on a board machine lives in main memory at `AP_BOARD_RAM_BASE`, not
 * at zero -- zero is the boot PROM. The operator's write is flat, so word `i` of
 * the program is written at offset `i * 2` and executed at the RAM base. */
#define BOARD_PROGRAM (AP_BOARD_RAM_BASE + 0x0100u)
#define BOARD_STACK (AP_BOARD_RAM_BASE + 0x9000u)
#define BOARD_PROGRAM_OFFSET 0x0100u

static void build_board_machine(ap_machine_t *machine, ap_board_t *board,
                                uint8_t *memory, const uint16_t *words,
                                unsigned count) {
  for (uint32_t i = 0; i < RAM_BYTES; i++) {
    memory[i] = 0;
  }
  TEST_ASSERT_TRUE(
      ap_board_init(board, memory, RAM_BYTES, &board_epoch, 0x012345u));

  ap_machine_init(machine, memory, RAM_BYTES);
  ap_machine_reset(machine, BOARD_PROGRAM, BOARD_STACK);
  for (unsigned i = 0; i < count; i++) {
    TEST_ASSERT_TRUE(ap_machine_write(
        machine, BOARD_PROGRAM_OFFSET + i * 2u, 2u, words[i]));
  }
  /* Attached after the program is laid down, because the operator's write is
   * flat and the board's is mapped. */
  ap_machine_set_board(machine, board);
}

/* MOVE.B #$11,($00010401).L -- serial 1, channel A
 * MOVE.B #$22,($00010801).L -- the interval timer's control register
 * MOVE.B #$33,($00017000).L -- the address translation map
 * STOP #$2700 */
static const uint16_t device_workload[] = {
    0x13FCu, 0x0011u, 0x0001u, 0x0401u, 0x13FCu, 0x0022u, 0x0001u, 0x0801u,
    0x13FCu, 0x0033u, 0x0001u, 0x7000u, 0x4E72u, 0x2700u,
};

/* The item's own verification, and the reason the board half had to be built:
 * the same workload twice gives the same number. Two machines on two different
 * RAM buffers with two different boards, so anything of the host's that reached
 * the hash would show here first. */
static void test_the_same_workload_twice_gives_the_same_hash(void) {
  ap_machine_t a;
  ap_machine_t b;
  build_board_machine(&a, &first_board, ram, device_workload,
                      sizeof device_workload / sizeof device_workload[0]);
  build_board_machine(&b, &second_board, other_ram, device_workload,
                      sizeof device_workload / sizeof device_workload[0]);
  TEST_ASSERT_TRUE(ap_machine_set_cpu_hz(&a, 25000000u));
  TEST_ASSERT_TRUE(ap_machine_set_cpu_hz(&b, 25000000u));

  const uint64_t start = ap_machine_hash(&a);
  TEST_ASSERT_EQUAL_HEX64(start, ap_machine_hash(&b));

  /* Step by step rather than only at the end: a hash that agreed at the end
   * could still have taken two different routes there, and a divergence found
   * on the step it happened is the difference between a bug and a search. */
  for (unsigned i = 0; i < 4u; i++) {
    (void)ap_machine_run(&a, 1u);
    (void)ap_machine_run(&b, 1u);
    TEST_ASSERT_EQUAL_HEX64(ap_machine_hash(&a), ap_machine_hash(&b));
  }

  /* And it moved. A hash that never changed would satisfy every equality above
   * perfectly and detect nothing at all. */
  TEST_ASSERT_NOT_EQUAL_UINT64(start, ap_machine_hash(&a));

  /* The workload reached the devices, which is what makes this a test of the
   * board's half rather than of the CPU's. A program that faulted on its first
   * instruction would agree with itself just as well. */
  TEST_ASSERT_TRUE(first_board.region_writes[AP_BOARD_REGION_SIO] > 0u);
  TEST_ASSERT_TRUE(first_board.region_writes[AP_BOARD_REGION_TIMER] > 0u);
  TEST_ASSERT_TRUE(
      first_board.region_writes[AP_BOARD_REGION_TRANSLATION_MAP] > 0u);
  TEST_ASSERT_EQUAL_UINT(first_board.region_writes[AP_BOARD_REGION_SIO],
                         second_board.region_writes[AP_BOARD_REGION_SIO]);
}

/* The board is *in* the machine's hash. Without this the devices could diverge
 * completely -- a different byte in a DUART, a different interrupt in service --
 * while the machine reported itself unchanged. */
static void test_the_machine_hash_covers_the_board(void) {
  ap_machine_t m;
  build_board_machine(&m, &first_board, ram, device_workload, 4u);
  const uint64_t before = ap_machine_hash(&m);

  first_board.registers.cpu_control ^= 0x0001u;
  TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_machine_hash(&m));
}

/* A probe machine on flat RAM must not hash as a DN3500 whose every device
 * happens to be at reset. Absence is a marker, the same rule the processor
 * applies to an absent access context. */
static void test_a_machine_with_a_board_does_not_hash_as_one_without(void) {
  ap_machine_t with;
  build_board_machine(&with, &first_board, ram, device_workload, 4u);

  ap_machine_t without;
  ap_machine_init(&without, other_ram, RAM_BYTES);
  ap_machine_reset(&without, BOARD_PROGRAM, BOARD_STACK);
  for (unsigned i = 0; i < 4u; i++) {
    TEST_ASSERT_TRUE(ap_machine_write(&without, BOARD_PROGRAM_OFFSET + i * 2u,
                                      2u, device_workload[i]));
  }

  TEST_ASSERT_NOT_EQUAL_UINT64(ap_machine_hash(&with),
                               ap_machine_hash(&without));
}

/* Elapsed time is state. Two machines that reach identical processor state --
 * the same registers, the same caches, the same clock count -- at *different
 * instants* are not the same machine, and on a core whose whole claim is
 * emergent timing that is precisely the divergence a fast mode introduces.
 *
 * Run the same program at two clock rates: the processor's own hash agrees,
 * because the CPU counts cycles and not time, and the machine's must not. */
static void test_two_machines_at_different_clock_rates_hash_differently(void) {
  static const uint16_t program[] = {0x7003u, 0x2200u, 0x4E71u};

  ap_machine_t fast;
  ap_machine_init(&fast, ram, RAM_BYTES);
  ap_machine_reset(&fast, PROGRAM, STACK);
  load(&fast, program, 3);
  TEST_ASSERT_TRUE(ap_machine_set_cpu_hz(&fast, 25000000u));

  ap_machine_t slow;
  ap_machine_init(&slow, other_ram, RAM_BYTES);
  ap_machine_reset(&slow, PROGRAM, STACK);
  for (unsigned i = 0; i < 3u; i++) {
    TEST_ASSERT_TRUE(
        ap_machine_write(&slow, PROGRAM + i * 2u, 2u, program[i]));
  }
  TEST_ASSERT_TRUE(ap_machine_set_cpu_hz(&slow, 20000000u));

  (void)ap_machine_run(&fast, 2u);
  (void)ap_machine_run(&slow, 2u);

  /* The processor reached the same place by the same number of cycles. */
  TEST_ASSERT_EQUAL_HEX64(ap_m68030_state_hash(&fast.cpu),
                          ap_m68030_state_hash(&slow.cpu));
  TEST_ASSERT_NOT_EQUAL_UINT64(ap_machine_now(&fast), ap_machine_now(&slow));

  /* And the machine did not. */
  TEST_ASSERT_NOT_EQUAL_UINT64(ap_machine_hash(&fast), ap_machine_hash(&slow));
}

/* The counters are our record of watching the machine, not state it has. In the
 * hash they would make adding an instrument change every golden with no
 * emulated behaviour changing. Nothing is lost: `ap_machine_state` reports them
 * beside the number, and `tests/goldens/probes.txt` pins the bus-error count as
 * its own column. */
static void test_the_counters_are_reported_beside_the_hash_not_inside_it(void) {
  ap_machine_t m;
  build_board_machine(&m, &first_board, ram, device_workload, 4u);
  const uint64_t before = ap_machine_hash(&m);

  m.bus_errors += 129u;
  first_board.unmapped_reads += 129u;
  first_board.region_reads[AP_BOARD_REGION_SIO] += 250244u;
  TEST_ASSERT_EQUAL_HEX64(before, ap_machine_hash(&m));

  TEST_ASSERT_EQUAL_UINT(129u, ap_machine_state(&m).bus_errors);
}

/* "With emulated cycle count and PC reported beside it." A hash says whether
 * two runs are the same and nothing about where they parted; the clock and the
 * PC are what turn a disagreement into a place to look. */
static void test_the_state_report_carries_the_clock_and_the_pc(void) {
  ap_machine_t m;
  build_board_machine(&m, &first_board, ram, device_workload,
                      sizeof device_workload / sizeof device_workload[0]);
  TEST_ASSERT_TRUE(ap_machine_set_cpu_hz(&m, 25000000u));
  (void)ap_machine_run(&m, 3u);

  const ap_machine_state_t state = ap_machine_state(&m);
  TEST_ASSERT_EQUAL_HEX64(ap_machine_hash(&m), state.hash);
  TEST_ASSERT_EQUAL_UINT64(m.cpu.clocks, state.clocks);
  TEST_ASSERT_EQUAL_UINT64(ap_machine_now(&m), state.now);
  TEST_ASSERT_EQUAL_HEX32(m.cpu.regs.pc, state.pc);

  /* The run actually went somewhere, so this is not three fields all agreeing
   * at zero. */
  TEST_ASSERT_TRUE(state.clocks > 0u);
  TEST_ASSERT_TRUE(state.now > 0u);
  TEST_ASSERT_NOT_EQUAL_UINT32(BOARD_PROGRAM, state.pc);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_machine_keeps_time_in_base_units);
  RUN_TEST(test_an_unrepresentable_cpu_rate_is_refused);
  RUN_TEST(test_an_unset_clock_produces_no_time);
  RUN_TEST(test_a_probe_can_set_up_run_and_read_back);
  RUN_TEST(test_every_transcribed_row_matches_both_published_columns);
  RUN_TEST(test_the_footnoted_memory_forms_compose_to_the_manuals_total);
  RUN_TEST(test_the_unfootnoted_memory_moves_match_both_columns);
  RUN_TEST(test_the_predecrement_move_costs_more_than_the_postincrement);
  RUN_TEST(test_the_left_arithmetic_shift_costs_more_than_the_right);
  RUN_TEST(test_a_register_count_shift_is_not_transcribed);
  RUN_TEST(test_a_taken_branch_costs_more_than_an_untaken_one);
  RUN_TEST(test_leaving_a_dbcc_loop_costs_more_than_going_round);
  RUN_TEST(test_bsr_is_priced_as_a_call_not_as_an_untaken_branch);
  RUN_TEST(test_reset_leaves_the_state_a_reset_leaves);
  RUN_TEST(test_an_access_beyond_the_ram_faults_rather_than_wrapping);
  RUN_TEST(test_a_runaway_program_ends_at_its_limit);
  RUN_TEST(test_a_run_stops_on_an_unimplemented_instruction_and_says_so);
  RUN_TEST(test_writing_memory_does_not_leave_a_stale_cache_line);
  RUN_TEST(test_two_machines_run_the_same_way_hash_alike);
  RUN_TEST(test_the_machine_hash_covers_the_memory_a_run_left_behind);
  RUN_TEST(test_the_instruction_and_data_caches_are_not_the_same_cache);
  RUN_TEST(test_the_mmu_registers_the_machine_reads_are_the_ones_pmove_writes);
  RUN_TEST(test_the_same_workload_twice_gives_the_same_hash);
  RUN_TEST(test_the_machine_hash_covers_the_board);
  RUN_TEST(test_a_machine_with_a_board_does_not_hash_as_one_without);
  RUN_TEST(test_two_machines_at_different_clock_rates_hash_differently);
  RUN_TEST(test_the_counters_are_reported_beside_the_hash_not_inside_it);
  RUN_TEST(test_the_state_report_carries_the_clock_and_the_pc);
  return UNITY_END();
}
