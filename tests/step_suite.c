/* MC68030 instruction step: fetch, decode, execute, advance.
 *
 * A program runs through the whole stack here -- pipe, cache, MMU, bus,
 * decoders -- so these tests are the first that observe the machine rather than
 * a part of it.
 *
 * The property most worth protecting is that an instruction this model cannot
 * execute reports UNIMPLEMENTED, which is neither success nor an illegal
 * instruction. Silently doing nothing would make a program appear to run while
 * producing wrong results and a wrong clock count; reporting illegal would be a
 * lie about the hardware and would send a probe down an exception path the real
 * machine never takes.
 */

#include "cpu/m68030/ap_m68030_step.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define FC_SUPERVISOR_PROGRAM 6u
#define PROGRAM_BASE 0x00001000u

/* A memory system holding a little program, supplied a long word at a time. */
#define STORE_SLOTS 16u

typedef struct {
  const uint16_t *words;
  unsigned count;
  unsigned fills;
  /* A tiny writable region, so a store is observable by a later load -- which
   * is what proves the writethrough actually reached memory. */
  uint32_t store_address[STORE_SLOTS];
  uint32_t store_value[STORE_SLOTS];
  unsigned stores;
} memory_t;

static void memory_store(void *context, uint32_t physical, uint32_t value,
                         unsigned size) {
  (void)size;
  memory_t *memory = (memory_t *)context;
  if (memory->stores < STORE_SLOTS) {
    memory->store_address[memory->stores] = physical;
    memory->store_value[memory->stores] = value;
  }
  memory->stores++;
}

static void memory_fill(void *context, uint32_t line_address,
                        uint8_t function_code, ap_m68030_fill_answer_t *out) {
  (void)function_code;
  memory_t *memory = (memory_t *)context;
  memory->fills++;
  out->termination = AP_M68030_TERM_STERM;
  out->burst_acknowledge = false;

  /* A previously stored long word wins over the program image. */
  for (unsigned i = memory->stores; i-- > 0 && i < STORE_SLOTS;) {
    if (memory->store_address[i] == line_address) {
      out->data[0] = memory->store_value[i];
      return;
    }
  }

  /* Assemble the long word at line_address from the program. */
  const uint32_t offset = line_address - PROGRAM_BASE;
  const unsigned index = (unsigned)(offset / 2u);
  uint16_t high = 0x4E71u; /* NOP fills the gaps */
  uint16_t low = 0x4E71u;
  if (index < memory->count) {
    high = memory->words[index];
  }
  if (index + 1u < memory->count) {
    low = memory->words[index + 1u];
  }
  out->data[0] = ((uint32_t)high << 16) | low;
}

typedef struct {
  ap_m68030_cache_t cache;
  ap_m68030_atc_t atc;
  ap_m68030_tc_t tc;
  ap_m68030_root_t root;
  memory_t memory;
  ap_m68030_access_ctx_t access;
  ap_m68030_cache_t data_cache;
  ap_m68030_access_ctx_t data_access;
  ap_m68030_cpu_t cpu;
} machine_t;

static void load(machine_t *m, const uint16_t *words, unsigned count) {
  ap_m68030_cache_clear(&m->cache);
  ap_m68030_atc_flush(&m->atc);
  m->memory.words = words;
  m->memory.count = count;
  m->memory.fills = 0;
  m->memory.stores = 0;
  m->access = (ap_m68030_access_ctx_t){
      .cache = &m->cache,
      .atc = &m->atc,
      .tc = &m->tc,
      .root = &m->root,
      .cache_enabled = true,
      .translation_enabled = false,
      .fill = memory_fill,
      .store = memory_store,
      .context = &m->memory,
  };
  /* The data side is a different cache from the instruction side, sharing the
   * same memory system -- which is what the real part has. */
  ap_m68030_cache_clear(&m->data_cache);
  m->data_access = m->access;
  m->data_access.cache = &m->data_cache;

  m->cpu.fetch.access = &m->access;
  m->cpu.fetch.function_code = FC_SUPERVISOR_PROGRAM;
  m->cpu.data = &m->data_access;
  m->cpu.data_function_code = 5u; /* supervisor data */
  ap_m68030_cpu_reset(&m->cpu, PROGRAM_BASE);
}

/* NOP executes and advances the PC by two. */
static void test_a_nop_executes_and_advances_the_pc(void) {
  static const uint16_t program[] = {0x4E71u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX16(0x4E71u, r.instruction);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 2u, m.cpu.regs.pc);
}

/* MOVEQ sign-extends its byte to a long: $FF becomes -1, not 255. */
static void test_moveq_sign_extends_to_a_long(void) {
  static const uint16_t program[] = {0x70FFu, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, m.cpu.regs.d[0]);
}

/* "N — Set if the result is negative; Z — Set if the result is zero; V —
 * Always cleared; C — Always cleared", and X is not affected. */
static void test_moveq_sets_the_documented_condition_codes(void) {
  static const uint16_t negative[] = {0x70FFu, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, negative, 4);
  /* Set every condition code first, so "cleared" is observable. */
  ap_m68030_write_ccr(&m.cpu.regs, AP_M68030_CCR_MASK);

  (void)ap_m68030_step(&m.cpu);
  const uint16_t ccr = ap_m68030_read_ccr(&m.cpu.regs);

  TEST_ASSERT_TRUE(ccr & (1u << AP_M68030_SR_N_BIT));
  TEST_ASSERT_FALSE(ccr & (1u << AP_M68030_SR_Z_BIT));
  TEST_ASSERT_FALSE(ccr & (1u << AP_M68030_SR_V_BIT));
  TEST_ASSERT_FALSE(ccr & (1u << AP_M68030_SR_C_BIT));
  /* X survives, which is the one that is *not* touched. */
  TEST_ASSERT_TRUE(ccr & (1u << AP_M68030_SR_X_BIT));
}

/* A zero operand sets Z and clears N. */
static void test_moveq_of_zero_sets_the_zero_flag(void) {
  static const uint16_t program[] = {0x7000u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  (void)ap_m68030_step(&m.cpu);
  const uint16_t ccr = ap_m68030_read_ccr(&m.cpu.regs);
  TEST_ASSERT_TRUE(ccr & (1u << AP_M68030_SR_Z_BIT));
  TEST_ASSERT_FALSE(ccr & (1u << AP_M68030_SR_N_BIT));
  TEST_ASSERT_EQUAL_HEX32(0, m.cpu.regs.d[0]);
}

/* Several instructions in sequence, each advancing the PC. */
static void test_a_short_program_runs_to_its_end(void) {
  static const uint16_t program[] = {0x7001u, 0x7202u, 0x4E71u, 0x7403u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);

  for (unsigned i = 0; i < 4; i++) {
    const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);
    TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  }

  TEST_ASSERT_EQUAL_HEX32(1, m.cpu.regs.d[0]);
  TEST_ASSERT_EQUAL_HEX32(2, m.cpu.regs.d[1]);
  TEST_ASSERT_EQUAL_HEX32(3, m.cpu.regs.d[2]);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 8u, m.cpu.regs.pc);
}

/* BRA takes the branch and the PC lands on the target. */
static void test_a_branch_always_is_taken(void) {
  /* BRA +4 at the base: target is base + 2 + 4. */
  static const uint16_t program[] = {0x6004u, 0x4E71u, 0x4E71u, 0x7042u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_TRUE(r.branch_taken);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 6u, m.cpu.regs.pc);

  /* And the instruction there really is the MOVEQ. */
  const ap_m68030_step_result_t next = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_HEX16(0x7042u, next.instruction);
  TEST_ASSERT_EQUAL_HEX32(0x42u, m.cpu.regs.d[0]);
}

/* A conditional branch consults the condition codes the previous instruction
 * set -- which is the first time two instructions have interacted. */
static void test_a_conditional_branch_reads_the_previous_result(void) {
  /* MOVEQ #0,D0 sets Z; BEQ +2 is then taken. */
  static const uint16_t taken[] = {0x7000u, 0x6702u, 0x4E71u, 0x7042u,
                                   0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, taken, 6);

  (void)ap_m68030_step(&m.cpu); /* MOVEQ #0 */
  const ap_m68030_step_result_t branch = ap_m68030_step(&m.cpu);
  TEST_ASSERT_TRUE(branch.branch_taken);

  /* MOVEQ #1,D0 clears Z, so the same BEQ is not taken. */
  static const uint16_t not_taken[] = {0x7001u, 0x6702u, 0x4E71u, 0x7042u,
                                       0x4E71u, 0x4E71u};
  machine_t n = {0};
  load(&n, not_taken, 6);

  (void)ap_m68030_step(&n.cpu);
  const ap_m68030_step_result_t fallthrough = ap_m68030_step(&n.cpu);
  TEST_ASSERT_FALSE(fallthrough.branch_taken);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 4u, n.cpu.regs.pc);
}

/* The property that lets this module ship incomplete: an instruction with no
 * semantics yet is reported, not skipped and not called illegal. */
static void test_an_unimplemented_instruction_is_reported_not_skipped(void) {
  /* MULU.W (A0),D0 decodes perfectly well -- family 1100, opmode 011 -- and
   * has no semantics here. ADD used to serve this test and now executes, which
   * is the right reason for a test like this to need changing. */
  static const uint16_t program[] = {0xC0D0u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_UNIMPLEMENTED, r.status);
  TEST_ASSERT_NOT_EQUAL_INT(AP_M68030_STEP_ILLEGAL, r.status);
  TEST_ASSERT_NOT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  /* It decoded correctly -- the gap is in execution, not decode. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_ARITH, r.kind);
  /* And the PC did not move past it. */
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE, m.cpu.regs.pc);
}

/* An encoding no family claims is genuinely illegal, which is a different
 * outcome from merely unimplemented. */
static void test_an_illegal_encoding_is_distinct_from_unimplemented(void) {
  static const uint16_t program[] = {0x4E7Fu, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_ILLEGAL, r.status);
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_ILLEGAL, r.kind);
}

/* Instruction fetch costs clocks, and running the same code again costs fewer
 * because the instruction cache answers -- the first end-to-end observation of
 * the cache paying off in a running program. */
static void test_a_loop_costs_less_the_second_time_around(void) {
  static const uint16_t program[] = {0x7001u, 0x7202u, 0x7403u, 0x7604u};
  machine_t m = {0};
  load(&m, program, 4);

  uint64_t first_pass = 0;
  for (unsigned i = 0; i < 4; i++) {
    first_pass += ap_m68030_step(&m.cpu).clocks;
  }
  const unsigned fills_after_first = m.memory.fills;

  /* Run the same four instructions again from the top. */
  ap_m68030_cpu_reset(&m.cpu, PROGRAM_BASE);
  uint64_t second_pass = 0;
  for (unsigned i = 0; i < 4; i++) {
    second_pass += ap_m68030_step(&m.cpu).clocks;
  }

  TEST_ASSERT_TRUE(first_pass > 0);
  TEST_ASSERT_EQUAL_UINT64(0, second_pass);
  TEST_ASSERT_EQUAL_UINT(fills_after_first, m.memory.fills);
}


/* ---------------------------------------------------------------------------
 * MOVE and MOVEA, in the addressing modes reachable without an extension word.
 * ------------------------------------------------------------------------- */

/* MOVE.L D0,D1 through the operand layer. */
static void test_move_copies_between_data_registers(void) {
  static const uint16_t program[] = {0x7042u, 0x2200u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  (void)ap_m68030_step(&m.cpu); /* MOVEQ #$42,D0 */
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu); /* MOVE.L D0,D1 */

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x42u, m.cpu.regs.d[1]);
}

/* A word MOVE writes only the low half of the destination data register, so the
 * upper half of whatever was there survives. */
static void test_a_word_move_leaves_the_upper_half_of_the_destination(void) {
  /* MOVEQ #-1,D1 fills D1; MOVEQ #0,D0; MOVE.W D0,D1 clears only its low half. */
  static const uint16_t program[] = {0x72FFu, 0x7000u, 0x3200u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0xFFFF0000u, m.cpu.regs.d[1]);
}

/* MOVEA.W sign-extends into all 32 bits of the address register -- the opposite
 * of the rule above, on the same operand size. */
static void test_movea_word_sign_extends_the_whole_address_register(void) {
  /* MOVEQ #-1,D0 then MOVEA.W D0,A1. */
  static const uint16_t program[] = {0x70FFu, 0x3240u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  ap_m68030_write_sr(&m.cpu.regs, 1u << AP_M68030_SR_S_BIT);

  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu,
                          ap_m68030_read_address_register(&m.cpu.regs, 1));
}

/* "N set if negative, Z set if zero, V and C always cleared", X untouched. */
static void test_move_sets_the_documented_condition_codes(void) {
  static const uint16_t program[] = {0x7000u, 0x2200u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  (void)ap_m68030_step(&m.cpu); /* MOVEQ #0,D0 */
  ap_m68030_write_ccr(&m.cpu.regs, AP_M68030_CCR_MASK); /* all set */
  (void)ap_m68030_step(&m.cpu); /* MOVE.L D0,D1 */

  const uint16_t ccr = ap_m68030_read_ccr(&m.cpu.regs);
  TEST_ASSERT_TRUE(ccr & (1u << AP_M68030_SR_Z_BIT));
  TEST_ASSERT_FALSE(ccr & (1u << AP_M68030_SR_N_BIT));
  TEST_ASSERT_FALSE(ccr & (1u << AP_M68030_SR_V_BIT));
  TEST_ASSERT_FALSE(ccr & (1u << AP_M68030_SR_C_BIT));
  TEST_ASSERT_TRUE(ccr & (1u << AP_M68030_SR_X_BIT));
}

/* MOVEA affects no condition codes at all, which is the reason it is
 * distinguished from MOVE. */
static void test_movea_leaves_the_condition_codes_alone(void) {
  static const uint16_t program[] = {0x7000u, 0x3240u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  ap_m68030_write_sr(&m.cpu.regs, 1u << AP_M68030_SR_S_BIT);

  (void)ap_m68030_step(&m.cpu);
  ap_m68030_write_ccr(&m.cpu.regs, AP_M68030_CCR_MASK);
  (void)ap_m68030_step(&m.cpu); /* MOVEA.W D0,A1 */

  TEST_ASSERT_EQUAL_HEX16(AP_M68030_CCR_MASK,
                          ap_m68030_read_ccr(&m.cpu.regs));
}

/* A MOVE through memory: store a register, then load it back, with the
 * postincrement and predecrement side effects applied. */
static void test_a_move_through_memory_round_trips(void) {
  /* MOVEQ #$55,D0 ; MOVE.L D0,(A0)+ ; MOVE.L -(A0),D1 */
  static const uint16_t program[] = {0x7055u, 0x20C0u, 0x2220u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  ap_m68030_write_sr(&m.cpu.regs, 1u << AP_M68030_SR_S_BIT);
  ap_m68030_write_address_register(&m.cpu.regs, 0, 0x4000u);

  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t store = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, store.status);
  /* The postincrement moved A0 by the operand size. */
  TEST_ASSERT_EQUAL_HEX32(0x4004u,
                          ap_m68030_read_address_register(&m.cpu.regs, 0));

  const ap_m68030_step_result_t reload = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, reload.status);
  TEST_ASSERT_EQUAL_HEX32(0x4000u,
                          ap_m68030_read_address_register(&m.cpu.regs, 0));
  TEST_ASSERT_EQUAL_HEX32(0x55u, m.cpu.regs.d[1]);
}

/* ---------------------------------------------------------------------------
 * Extension words, fetched from the instruction stream through the same pipe
 * the instruction word came from.
 * ------------------------------------------------------------------------- */

/* A long immediate is two extension words, high half first. */
static void test_a_long_immediate_source_is_two_words_high_first(void) {
  static const uint16_t program[] = {0x203Cu, 0x1234u, 0x5678u, 0x4E71u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x12345678u, m.cpu.regs.d[0]);
  /* Six bytes consumed: the instruction word and two extension words. */
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 6u, m.cpu.regs.pc);
}

/* Table 2-3 through running code: a byte immediate is the *low-order byte* of a
 * whole extension word, so the instruction is four bytes and the high half of
 * the word is not part of the operand. */
static void test_a_byte_immediate_takes_the_low_half_of_its_word(void) {
  static const uint16_t program[] = {0x103Cu, 0xAA55u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x55u, m.cpu.regs.d[0]);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 4u, m.cpu.regs.pc);
}

/* A displacement source: the word after the instruction is a signed
 * displacement from the address register. */
static void test_a_displacement_source_reads_its_extension_word(void) {
  /* MOVE.L D0,(A1) puts a known value in memory, then MOVE.L (-4,A1),D2
   * reads it back through a negative displacement. */
  static const uint16_t program[] = {0x7055u, 0x22C0u, 0x2429u, 0xFFFCu,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);
  ap_m68030_write_sr(&m.cpu.regs, 1u << AP_M68030_SR_S_BIT);
  ap_m68030_write_address_register(&m.cpu.regs, 1, 0x4000u);

  (void)ap_m68030_step(&m.cpu); /* MOVEQ #$55,D0 */
  (void)ap_m68030_step(&m.cpu); /* MOVE.L D0,(A1)+ : writes $4000, A1 -> $4004 */
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu); /* MOVE.L (-4,A1),D2 */

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x55u, m.cpu.regs.d[2]);
}

/* An absolute long destination is two extension words, and the store reaches
 * that address. */
static void test_an_absolute_long_destination_is_two_words(void) {
  /* $23C0 : destination register 001, mode 111 -- absolute long. $21C0 is
   * register 000 and is absolute *short*, which is the reversed destination
   * field biting for the third time in this project. */
  static const uint16_t program[] = {0x7077u, 0x23C0u, 0x0000u, 0x5000u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);

  (void)ap_m68030_step(&m.cpu); /* MOVEQ #$77,D0 */
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_UINT(1, m.memory.stores);
  TEST_ASSERT_EQUAL_HEX32(0x5000u, m.memory.store_address[0]);
  TEST_ASSERT_EQUAL_HEX32(0x77u, m.memory.store_value[0]);
  /* Instruction word plus two extension words. */
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 8u, m.cpu.regs.pc);
}

/* "(xxx).W" is sign-extended, so a short absolute above $8000 addresses the top
 * of memory rather than the middle of it. */
static void test_a_short_absolute_address_is_sign_extended(void) {
  static const uint16_t program[] = {0x7011u, 0x11C0u, 0x8000u, 0x4E71u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);

  (void)ap_m68030_step(&m.cpu); /* MOVEQ #$11,D0 */
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu); /* MOVE.B D0,($8000).W */

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_UINT(1, m.memory.stores);
  TEST_ASSERT_EQUAL_HEX32(0xFFFF8000u, m.memory.store_address[0]);
}

/* Both operands taking extension words: the source's come first, then the
 * destination's -- the ordering ap_m68030_instruction_length describes and this
 * performs. */
static void test_both_operands_take_their_extension_words_in_order(void) {
  /* MOVE.W (2,A0),(6,A1) : source displacement, then destination displacement. */
  static const uint16_t program[] = {0x3368u, 0x0002u, 0x0006u, 0x4E71u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);
  ap_m68030_write_sr(&m.cpu.regs, 1u << AP_M68030_SR_S_BIT);
  ap_m68030_write_address_register(&m.cpu.regs, 0, 0x4000u);
  ap_m68030_write_address_register(&m.cpu.regs, 1, 0x6000u);

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);

  /* The destination displacement was 6, not 2 -- which is what a decoder that
   * read the words in the wrong order would produce. */
  TEST_ASSERT_EQUAL_UINT(1, m.memory.stores);
  TEST_ASSERT_EQUAL_HEX32(0x6006u, m.memory.store_address[0]);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 6u, m.cpu.regs.pc);
}


/* ---------------------------------------------------------------------------
 * Arithmetic and logic, with the ALU wired in.
 * ------------------------------------------------------------------------- */

/* ADD.L D0,D1 in the register direction: the register is the destination. */
static void test_add_accumulates_into_the_register(void) {
  /* MOVEQ #5,D0 ; MOVEQ #3,D1 ; ADD.L D0,D1 */
  static const uint16_t program[] = {0x7005u, 0x7203u, 0xD280u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(8u, m.cpu.regs.d[1]);
}

/* The direction bit decides which operand is the destination, and with it which
 * way round a subtraction goes. SUB.L D0,D1 is D1 - D0, not D0 - D1 --
 * reversing it negates the result and inverts the carry. */
static void test_subtraction_goes_destination_minus_source(void) {
  /* MOVEQ #3,D0 ; MOVEQ #10,D1 ; SUB.L D0,D1  ->  D1 = 7 */
  static const uint16_t program[] = {0x7003u, 0x720Au, 0x9280u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_HEX32(7u, m.cpu.regs.d[1]);
  /* 10 - 3 borrows nothing. */
  TEST_ASSERT_FALSE(ap_m68030_read_ccr(&m.cpu.regs) &
                    (1u << AP_M68030_SR_C_BIT));
}

/* And the borrow appears when the subtraction goes below zero. */
static void test_a_subtraction_below_zero_borrows(void) {
  /* MOVEQ #10,D0 ; MOVEQ #3,D1 ; SUB.L D0,D1  ->  D1 = -7, C set */
  static const uint16_t program[] = {0x700Au, 0x7203u, 0x9280u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFF9u, m.cpu.regs.d[1]);
  TEST_ASSERT_TRUE(ap_m68030_read_ccr(&m.cpu.regs) &
                   (1u << AP_M68030_SR_C_BIT));
  TEST_ASSERT_TRUE(ap_m68030_read_ccr(&m.cpu.regs) &
                   (1u << AP_M68030_SR_N_BIT));
}

/* CMP writes nothing and exists for its condition codes alone. */
static void test_cmp_leaves_its_operands_alone(void) {
  /* MOVEQ #5,D0 ; MOVEQ #5,D1 ; CMP.L D0,D1 */
  static const uint16_t program[] = {0x7005u, 0x7205u, 0xB280u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  /* Both registers survive. */
  TEST_ASSERT_EQUAL_HEX32(5u, m.cpu.regs.d[0]);
  TEST_ASSERT_EQUAL_HEX32(5u, m.cpu.regs.d[1]);
  /* Equal operands set Z. */
  TEST_ASSERT_TRUE(ap_m68030_read_ccr(&m.cpu.regs) &
                   (1u << AP_M68030_SR_Z_BIT));
}

/* A compare followed by a conditional branch -- the pattern every loop is built
 * from, and the first time three instructions have had to agree. */
static void test_a_compare_and_branch_agree(void) {
  /* MOVEQ #7,D0 ; MOVEQ #7,D1 ; CMP.L D0,D1 ; BEQ +2 ; MOVEQ #$FF,D2 */
  static const uint16_t program[] = {0x7007u, 0x7207u, 0xB280u, 0x6702u,
                                     0x74FFu, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 8);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t branch = ap_m68030_step(&m.cpu);

  TEST_ASSERT_TRUE(branch.branch_taken);
  /* The branch skipped the MOVEQ, so D2 is untouched. */
  const ap_m68030_step_result_t after = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, after.status);
  TEST_ASSERT_EQUAL_HEX32(0u, m.cpu.regs.d[2]);
}

/* The memory direction writes the result back to the effective address rather
 * than to the register. */
static void test_the_memory_direction_writes_back_to_memory(void) {
  /* MOVEQ #1,D0 ; ADD.L D0,(A0) */
  static const uint16_t program[] = {0x7001u, 0xD190u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  ap_m68030_write_sr(&m.cpu.regs, 1u << AP_M68030_SR_S_BIT);
  ap_m68030_write_address_register(&m.cpu.regs, 0, 0x4000u);

  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  /* The register is unchanged; memory received the sum. */
  TEST_ASSERT_EQUAL_HEX32(1u, m.cpu.regs.d[0]);
  TEST_ASSERT_EQUAL_UINT(1, m.memory.stores);
  TEST_ASSERT_EQUAL_HEX32(0x4000u, m.memory.store_address[0]);
}

/* A byte operation on a data register leaves the upper bytes alone, here
 * through the arithmetic path rather than MOVE's. */
static void test_a_byte_add_leaves_the_upper_bytes_of_the_register(void) {
  /* MOVE.L #$11223344,D1 ; MOVEQ #1,D0 ; ADD.B D0,D1 */
  static const uint16_t program[] = {0x223Cu, 0x1122u, 0x3344u, 0x7001u,
                                     0xD200u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 8);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x11223345u, m.cpu.regs.d[1]);
}

/* AND and OR clear V and C whatever they were, per Table 3-18. */
static void test_the_logical_operations_clear_v_and_c(void) {
  /* MOVEQ #-1,D0 ; MOVEQ #0,D1 ; AND.L D0,D1 */
  static const uint16_t program[] = {0x70FFu, 0x7200u, 0xC280u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);
  ap_m68030_write_ccr(&m.cpu.regs, AP_M68030_CCR_MASK);
  (void)ap_m68030_step(&m.cpu);

  const uint16_t ccr = ap_m68030_read_ccr(&m.cpu.regs);
  TEST_ASSERT_FALSE(ccr & (1u << AP_M68030_SR_V_BIT));
  TEST_ASSERT_FALSE(ccr & (1u << AP_M68030_SR_C_BIT));
  TEST_ASSERT_TRUE(ccr & (1u << AP_M68030_SR_Z_BIT));
  /* X is not affected by a logical operation, so it survives. */
  TEST_ASSERT_TRUE(ccr & (1u << AP_M68030_SR_X_BIT));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_nop_executes_and_advances_the_pc);
  RUN_TEST(test_moveq_sign_extends_to_a_long);
  RUN_TEST(test_moveq_sets_the_documented_condition_codes);
  RUN_TEST(test_moveq_of_zero_sets_the_zero_flag);
  RUN_TEST(test_a_short_program_runs_to_its_end);
  RUN_TEST(test_a_branch_always_is_taken);
  RUN_TEST(test_a_conditional_branch_reads_the_previous_result);
  RUN_TEST(test_an_unimplemented_instruction_is_reported_not_skipped);
  RUN_TEST(test_an_illegal_encoding_is_distinct_from_unimplemented);
  RUN_TEST(test_a_loop_costs_less_the_second_time_around);
  RUN_TEST(test_move_copies_between_data_registers);
  RUN_TEST(test_a_word_move_leaves_the_upper_half_of_the_destination);
  RUN_TEST(test_movea_word_sign_extends_the_whole_address_register);
  RUN_TEST(test_move_sets_the_documented_condition_codes);
  RUN_TEST(test_movea_leaves_the_condition_codes_alone);
  RUN_TEST(test_a_move_through_memory_round_trips);
  RUN_TEST(test_a_long_immediate_source_is_two_words_high_first);
  RUN_TEST(test_a_byte_immediate_takes_the_low_half_of_its_word);
  RUN_TEST(test_a_displacement_source_reads_its_extension_word);
  RUN_TEST(test_an_absolute_long_destination_is_two_words);
  RUN_TEST(test_a_short_absolute_address_is_sign_extended);
  RUN_TEST(test_both_operands_take_their_extension_words_in_order);
  RUN_TEST(test_add_accumulates_into_the_register);
  RUN_TEST(test_subtraction_goes_destination_minus_source);
  RUN_TEST(test_a_subtraction_below_zero_borrows);
  RUN_TEST(test_cmp_leaves_its_operands_alone);
  RUN_TEST(test_a_compare_and_branch_agree);
  RUN_TEST(test_the_memory_direction_writes_back_to_memory);
  RUN_TEST(test_a_byte_add_leaves_the_upper_bytes_of_the_register);
  RUN_TEST(test_the_logical_operations_clear_v_and_c);
  return UNITY_END();
}
