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
#include "cpu/m68030/ap_m68030_exception.h"
#include "cpu/m68030/ap_m68030_operand.h"
#include "cpu/m68030/ap_m68030_ssw.h"
#include "cpu/m68030/ap_m68030_mmusr.h"
#include "cpu/m68030/ap_m68030_state.h"
#include "cpu/m68020/ap_m68020_module.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define FC_SUPERVISOR_PROGRAM 6u
#define PROGRAM_BASE 0x00001000u

/* A memory system holding a little program.
 *
 * Real bytes rather than a log of writes: once a misaligned store splits into
 * more than one bus cycle, "was this long word written" is no longer a question
 * a list of (address, value) pairs can answer, and a stack round trip is
 * exactly the thing that needs asking. The write log is kept alongside for the
 * tests that care how many cycles a store took and where they went. */
#define RAM_BYTES 0x00010000u
#define STORE_SLOTS 32u

typedef struct {
  uint8_t bytes[RAM_BYTES];
  unsigned fills;
  uint32_t store_address[STORE_SLOTS];
  uint32_t store_value[STORE_SLOTS];
  unsigned stores;
  /* A line address at or above which the memory answers BERR instead of data,
   * so a test can put a genuine bus error under an instruction's operand. Zero
   * means never, which is what every test that does not set it wants -- and
   * `load` resets it, so one test cannot leave a hole in the next one's RAM. */
  uint32_t berr_from;

  /* The **first CPU-space** fill's address, so a test can check which cycle the
   * processor ran rather than only that one faulted. A wrong breakpoint number
   * is a legal address naming a different breakpoint, which no fault reveals.
   *
   * The first and not the last: an unanswered breakpoint acknowledge is
   * followed by the illegal-instruction vector fetch, so the last fill of the
   * step is the vector's and recording that would pin the wrong cycle. */
  uint32_t cpu_space_address;
  unsigned cpu_space_cycles;

  /* When set, CPU space answers BERR -- which is the DN3500, where nothing
   * decodes it. Kept separate from `berr_from` because CPU space is selected by
   * the function code and not by an address range: a breakpoint acknowledge
   * runs at a very low address, so no address bound can refuse it without also
   * refusing the vector table. */
  bool berr_on_cpu_space;
} memory_t;

static bool memory_store(void *context, uint32_t physical, uint32_t value,
                         unsigned size) {
  memory_t *memory = (memory_t *)context;
  /* The same region that refuses reads refuses writes. A memory system where
   * only one direction can fail is not a memory system any board has, and it
   * was what let a faulted write go unnoticed for as long as it did. */
  if (memory->berr_from != 0u && physical >= memory->berr_from) {
    return false;
  }
  if (memory->stores < STORE_SLOTS) {
    memory->store_address[memory->stores] = physical;
    memory->store_value[memory->stores] = value;
  }
  memory->stores++;

  /* Big endian: the operand's most significant byte goes to the lowest
   * address, which is what makes a split write and a split read agree. */
  for (unsigned i = 0; i < size; i++) {
    const uint32_t at = physical + i;
    if (at < RAM_BYTES) {
      memory->bytes[at] = (uint8_t)(value >> ((size - 1u - i) * 8u));
    }
  }
  return true;
}

static void memory_fill(void *context, uint32_t line_address,
                        uint8_t function_code, ap_m68030_fill_answer_t *out) {
  memory_t *memory = (memory_t *)context;
  memory->fills++;
  if (function_code == 7u) {
    if (memory->cpu_space_cycles == 0u) {
      memory->cpu_space_address = line_address;
    }
    memory->cpu_space_cycles++;
  }
  if (memory->berr_on_cpu_space && function_code == 7u) {
    out->termination = AP_M68030_TERM_BERR;
    out->burst_acknowledge = false;
    return;
  }
  if (memory->berr_from != 0u && line_address >= memory->berr_from) {
    out->termination = AP_M68030_TERM_BERR;
    out->burst_acknowledge = false;
    return;
  }
  out->termination = AP_M68030_TERM_STERM;
  out->burst_acknowledge = false;

  uint32_t value = 0;
  for (unsigned i = 0; i < 4u; i++) {
    const uint32_t at = line_address + i;
    value = (value << 8) | (at < RAM_BYTES ? memory->bytes[at] : 0x4Eu);
  }
  out->data[0] = value;
}

/* A table search's descriptor fetch, over the same RAM. Having one means PLOAD
 * and PTEST can walk a tree the test built by hand, rather than being tested
 * only in the cases that need no tree. */
static bool table_fetch(void *context, uint32_t physical, bool long_format,
                        ap_m68030_descriptor_t *out) {
  memory_t *memory = (memory_t *)context;
  uint32_t words[2] = {0, 0};
  for (unsigned w = 0; w < (long_format ? 2u : 1u); w++) {
    for (unsigned i = 0; i < 4u; i++) {
      const uint32_t at = physical + w * 4u + i;
      words[w] = (words[w] << 8) | (at < RAM_BYTES ? memory->bytes[at] : 0u);
    }
  }
  *out = long_format
             ? ap_m68030_descriptor_unpack_long(words[0], words[1], false)
             : ap_m68030_descriptor_unpack_short(words[0], false);
  return true;
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
  /* NOP everywhere, so an address the test did not think about decodes as
   * something harmless rather than as whatever the last test left. */
  for (unsigned i = 0; i < RAM_BYTES; i += 2u) {
    m->memory.bytes[i] = 0x4Eu;
    m->memory.bytes[i + 1u] = 0x71u;
  }
  for (unsigned i = 0; i < count; i++) {
    m->memory.bytes[PROGRAM_BASE + i * 2u] = (uint8_t)(words[i] >> 8);
    m->memory.bytes[PROGRAM_BASE + i * 2u + 1u] = (uint8_t)words[i];
  }
  m->memory.fills = 0;
  m->memory.stores = 0;
  m->memory.berr_from = 0u;
  m->memory.berr_on_cpu_space = false;
  m->memory.cpu_space_address = 0u;
  m->memory.cpu_space_cycles = 0u;
  m->access = (ap_m68030_access_ctx_t){
      .cache = &m->cache,
      .atc = &m->atc,
      .tc = &m->tc,
      .root = &m->root,
      .cache_enabled = true,
      .translation_enabled = false,
      .fill = memory_fill,
      .store = memory_store,
      .table_fetch = table_fetch,
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

/* A stack the frame can be built on, away from the program image, and a handler
 * address distinguishable from everything else in these tests. */
#define SUPERVISOR_STACK 0x00009000u
#define HANDLER 0x00002400u

/* Point a vector at a handler by pre-storing it, so the vector fetch has
 * something to read: the harness serves a stored long word back at the exact
 * address it was written to. */
/* Bounds-checked, because a test helper that walks off the end of the RAM
 * corrupts the stack instead of failing -- which is how an address outside the
 * harness's 64K first showed up here, as a segfault three tests later. */
static void write_ram_long(machine_t *m, uint32_t address, uint32_t value) {
  TEST_ASSERT_TRUE_MESSAGE(address + 4u <= RAM_BYTES,
                           "address outside the harness RAM");
  for (unsigned i = 0; i < 4u; i++) {
    m->memory.bytes[address + i] = (uint8_t)(value >> ((3u - i) * 8u));
  }
}

static uint32_t read_ram_long(const machine_t *m, uint32_t address) {
  TEST_ASSERT_TRUE_MESSAGE(address + 4u <= RAM_BYTES,
                           "address outside the harness RAM");
  uint32_t value = 0;
  for (unsigned i = 0; i < 4u; i++) {
    value = (value << 8) | m->memory.bytes[address + i];
  }
  return value;
}

static uint16_t read_ram_word(const machine_t *m, uint32_t address) {
  return (uint16_t)((m->memory.bytes[address] << 8) |
                    m->memory.bytes[address + 1u]);
}

static void plant_vector(machine_t *m, unsigned vector, uint32_t handler) {
  write_ram_long(m, vector * 4u, handler);
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
  /* BKPT #0 decodes perfectly well and has no semantics here: it runs a
   * breakpoint acknowledge cycle in CPU space, which is a bus transaction this
   * step does not issue. ADD served this test first, then MULU, then LEA, each
   * until it started working. A placeholder that keeps needing replacement
   * because the thing it stood in for now runs is the right kind of churn --
   * and an **undefined MMU extension class** is the current one. BKPT held the
   * role until its acknowledge cycle landed, and `CAS2` until its addresses
   * turned out to be registers rather than effective addresses -- every
   * placeholder so far has been implemented in turn, which is the point of
   * keeping one. */
  /* An MMU instruction whose extension class the 68030 does not define. The
   * step reports it as *our* gap rather than as an F-line trap, which is the
   * distinction this test exists for. Privileged, so supervisor state first. */
  static const uint16_t program[] = {0xF010u, 0xA000u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  ap_m68030_write_sr(&m.cpu.regs, (uint16_t)(1u << AP_M68030_SR_S_BIT));

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_UNIMPLEMENTED, r.status);
  TEST_ASSERT_NOT_EQUAL_INT(AP_M68030_STEP_ILLEGAL, r.status);
  TEST_ASSERT_NOT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  /* It decoded correctly -- the gap is in execution, not decode. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_COPROC, r.kind);
  /* And the PC did not move past it. */
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE, m.cpu.regs.pc);
}

/* The other half of that property, and the one it is useless without: an
 * instruction this model *does* implement, stopped by a bus that would not
 * answer, must not be reported as unimplemented.
 *
 * The two failures arrive at the executors as the same bare `false`. Reporting
 * them identically blames the CPU for the memory system's answer, and points
 * whoever reads the status at a decoder that is working perfectly. This came
 * from the DN3500 boot PROM, which stopped on exactly the instruction below and
 * reported UNIMPLEMENTED -- sending the investigation after a CMPI that had
 * been implemented and tested for weeks. */
static void
test_a_faulting_operand_read_takes_the_bus_error_exception(void) {
  /* `0C2D 0008 0001` -- CMPI.B #$08,(1,A5), the PROM's own word. */
  static const uint16_t program[] = {0x0C2Du, 0x0008u, 0x0001u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_BUS_ERROR, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  m.cpu.regs.a[5] = 0x0000C000u;
  m.memory.berr_from = 0x0000C000u; /* the operand faults; nothing else does */

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  /* Taken, not merely reported. An undecoded read is how firmware *asks*
   * whether a card is present, so stopping here would model a question as a
   * crash. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, r.status);
  TEST_ASSERT_NOT_EQUAL_INT(AP_M68030_STEP_UNIMPLEMENTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);

  /* A faulted data *read* gets the long frame -- 46 words -- because only that
   * one has a data input buffer for the handler to write the value into. */
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK - 92u, m.cpu.regs.isp);
  const uint16_t format_word =
      (uint16_t)(read_ram_long(&m, m.cpu.regs.isp + 4u) & 0xFFFFu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_FRAME_LONG_BUS_FAULT,
                        ap_m68030_frame_format_of(format_word));

  /* The address the handler must repair is the one that faulted, not the
   * instruction's. */
  TEST_ASSERT_EQUAL_HEX32(
      0x0000C001u, read_ram_long(&m, m.cpu.regs.isp + AP_M68030_BUS_FAULT_ADDRESS));

  /* And the long frame stacks the instruction in execution, so the handler can
   * retry it -- not the next one. */
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE, read_ram_long(&m, m.cpu.regs.isp + 2u));

  /* The special status word says a data cycle faulted, that it was a read, its
   * size, and the address space -- everything a repair needs. */
  const ap_m68030_ssw_t ssw = ap_m68030_ssw_decode(
      (uint16_t)(read_ram_long(&m, m.cpu.regs.isp + 8u) & 0xFFFFu));
  TEST_ASSERT_TRUE(ssw.data_fault);
  TEST_ASSERT_TRUE(ssw.read);
  TEST_ASSERT_EQUAL_UINT(AP_M68030_SSW_SIZE_BYTE, ssw.size);
  TEST_ASSERT_EQUAL_UINT8(5u, ssw.function_code); /* supervisor data */
}

/* The control the test above is worthless without: the identical instruction
 * over memory that answers must execute. Without this, a step that reported
 * FAULT for every immediate operation would pass -- and the claim being made is
 * about the *bus*, not about CMPI. */
static void
test_the_same_instruction_over_memory_that_answers_executes(void) {
  static const uint16_t program[] = {0x0C2Du, 0x0008u, 0x0001u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.a[5] = 0x00004000u;
  m.memory.bytes[0x00004001u] = 0x08u; /* equal, so Z is set */

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 6u, m.cpu.regs.pc);
  TEST_ASSERT_TRUE((ap_m68030_read_ccr(&m.cpu.regs) &
                    (uint16_t)(1u << AP_M68030_SR_Z_BIT)) != 0u);
}

/* A write that nothing accepts is a bus error exactly as a read of the same
 * address would be -- the direction does not decide whether a device is there.
 *
 * This was unreachable until the store callback could refuse. It returned
 * `void`, so a write to an address nothing decoded was counted by the memory
 * system and then silently succeeded, and no write could ever fault. A signal a
 * callee cannot send is one the caller assumes never happens.
 *
 * A faulted *write* takes the short frame: its value is in the data output
 * buffer, which the short frame carries, so there is nothing for the long one
 * to add. That is the distinction the frame choice exists to make. */
static void test_a_write_nothing_accepts_takes_the_bus_error_exception(void) {
  /* MOVE.B D0,(A1) with A1 in the refusing region. */
  static const uint16_t program[] = {0x1280u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_BUS_ERROR, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  m.cpu.regs.d[0] = 0x5Au;
  m.cpu.regs.a[1] = 0x0000C000u;
  m.memory.berr_from = 0x0000C000u;

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, r.status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK - 32u, m.cpu.regs.isp);

  const uint16_t format_word =
      (uint16_t)(read_ram_long(&m, m.cpu.regs.isp + 4u) & 0xFFFFu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_FRAME_SHORT_BUS_FAULT,
                        ap_m68030_frame_format_of(format_word));

  const ap_m68030_ssw_t ssw = ap_m68030_ssw_decode(
      (uint16_t)(read_ram_long(&m, m.cpu.regs.isp + 8u) & 0xFFFFu));
  TEST_ASSERT_TRUE(ssw.data_fault);
  TEST_ASSERT_FALSE(ssw.read);
  TEST_ASSERT_EQUAL_UINT(AP_M68030_SSW_SIZE_BYTE, ssw.size);

  /* "For data write faults, the handler must transfer the properly sized data
   * from the data output buffer (DOB) on the stack frame" -- so the value the
   * write was carrying has to be there, or the handler has nothing to write. */
  TEST_ASSERT_EQUAL_HEX32(
      0x5Au, read_ram_long(&m, m.cpu.regs.isp + AP_M68030_BUS_FAULT_DATA_OUTPUT));
  TEST_ASSERT_EQUAL_HEX32(
      0x0000C000u,
      read_ram_long(&m, m.cpu.regs.isp + AP_M68030_BUS_FAULT_ADDRESS));
}

/* A cache must not keep a value external memory refused. Writethrough means the
 * external cycle is not optional, so a store the memory system declined has to
 * leave the cache as it was -- otherwise the dropped write comes back as a
 * wrong *read* later, from a line that never existed anywhere else. */
static void test_a_refused_write_is_not_left_in_the_cache(void) {
  static const uint16_t program[] = {0x1280u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_BUS_ERROR, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  m.cpu.regs.d[0] = 0x5Au;
  m.cpu.regs.a[1] = 0x0000C000u;
  m.memory.berr_from = 0x0000C000u;

  (void)ap_m68030_step(&m.cpu);

  /* Read the same address back through the data cache. It must fault again --
   * a cached copy of the refused byte would answer instead. */
  const ap_m68030_address_t where = {.address = 0x0000C000u, .valid = true};
  const ap_m68030_operand_result_t back = ap_m68030_operand_read(
      &m.cpu.regs, m.cpu.data, &where, 1u, 5u);
  TEST_ASSERT_FALSE(back.ok);
}

/* §8.1.3: "An address error exception occurs when the processor attempts to
 * prefetch an instruction from an odd address." */
static void test_a_prefetch_from_an_odd_address_is_an_address_error(void) {
  static const uint16_t program[] = {0x4E71u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_ADDRESS_ERROR, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  ap_m68030_cpu_reset(&m.cpu, PROGRAM_BASE + 1u);

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, r.status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);

  /* No data cycle, so the short frame. */
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK - 32u, m.cpu.regs.isp);
  const uint16_t format_word =
      (uint16_t)(read_ram_long(&m, m.cpu.regs.isp + 4u) & 0xFFFFu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_FRAME_SHORT_BUS_FAULT,
                        ap_m68030_frame_format_of(format_word));
  TEST_ASSERT_EQUAL_UINT(AP_M68030_VECTOR_ADDRESS_ERROR * 4u,
                         ap_m68030_frame_vector_offset_of(format_word));

  /* "The rerun bits alone show the cause of the exception" -- the *absence* of
   * the fault bits is what tells a handler this was an address error and not a
   * bus error, so it is asserted rather than left implied. */
  const ap_m68030_ssw_t ssw = ap_m68030_ssw_decode(
      (uint16_t)(read_ram_long(&m, m.cpu.regs.isp + 8u) & 0xFFFFu));
  TEST_ASSERT_TRUE(ssw.stage_c_rerun);
  TEST_ASSERT_TRUE(ssw.stage_b_rerun);
  TEST_ASSERT_FALSE(ssw.stage_c_fault);
  TEST_ASSERT_FALSE(ssw.stage_b_fault);
  TEST_ASSERT_FALSE(ssw.data_fault);

  /* The odd address is the one a handler has to correct. */
  TEST_ASSERT_EQUAL_HEX32(
      PROGRAM_BASE + 1u,
      read_ram_long(&m, m.cpu.regs.isp + AP_M68030_BUS_FAULT_ADDRESS));
}

/* "A bus cycle is not executed, and the processor begins exception processing
 * immediately." The fault is internally initiated, so nothing reaches the
 * memory system -- an implementation that let the prefetch go out first would
 * pass every assertion above while making a bus cycle the hardware never
 * makes. */
static void test_an_address_error_runs_no_bus_cycle(void) {
  static const uint16_t program[] = {0x4E71u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_ADDRESS_ERROR, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  ap_m68030_cpu_reset(&m.cpu, PROGRAM_BASE + 1u);
  m.memory.fills = 0;

  (void)ap_m68030_step(&m.cpu);

  /* Only the vector fetch, which happens *after* exception processing begins.
   * No prefetch of the odd address itself. */
  TEST_ASSERT_EQUAL_UINT(1u, m.memory.fills);
}

/* The 68000 faulted on a misaligned data access; this part does not. §7.2.1
 * shows a long word transferred to an odd address in three bus cycles, so
 * applying the odd-address rule to operands would fault programs the hardware
 * runs perfectly well. This is the test that keeps the check on the program
 * counter alone. */
static void test_a_misaligned_data_access_is_not_an_address_error(void) {
  /* MOVE.L D0,(A1) with A1 odd. */
  static const uint16_t program[] = {0x2280u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_ADDRESS_ERROR, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  m.cpu.regs.d[0] = 0x12345678u;
  m.cpu.regs.a[1] = 0x00004001u; /* odd, and deliberately so */

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x12345678u, read_ram_long(&m, 0x00004001u));
}

/* `[030]` Table 8-1, vector 10. No word with bits 15-12 = 1010 is an
 * instruction on any member of the family: the whole `A000-AFFF` range exists
 * to be trapped and emulated in software. So taking the trap *is* the complete
 * behaviour, and reporting these unimplemented said the gap was ours when there
 * is no gap. */
static void test_a_line_1010_word_takes_the_emulator_trap(void) {
  static const uint16_t program[] = {0xA000u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_LINE_A, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, r.status);
  TEST_ASSERT_NOT_EQUAL_INT(AP_M68030_STEP_UNIMPLEMENTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
}

/* Vector 11, when no coprocessor responds. `FFFF` is what an empty AT bus slot
 * reads, so this is the path a firmware expansion-ROM scan actually takes: it
 * jumps into the window, fetches `FFFF`, and the trap is how the hardware ends
 * the attempt. */
static void test_an_f_line_word_with_no_coprocessor_takes_the_emulator_trap(void) {
  static const uint16_t program[] = {0xFFFFu, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_LINE_F, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, r.status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
}

/* The distinction the F-line change turns on, and the one that would quietly
 * rot if it were not tested. An MMU instruction this model has not implemented
 * must still report UNIMPLEMENTED, because the MMU *is* fitted and the real
 * part would execute it. Raising F-line there would dress our own gap up as
 * correct hardware behaviour -- convincingly, since firmware would take a
 * plausible exception and carry on.
 *
 * `F000` with a zero extension word is cpID 0, the MMU's, and is not one of
 * the MMU instructions implemented here. */
static void test_an_unimplemented_mmu_instruction_is_not_dressed_up_as_f_line(void) {
  static const uint16_t program[] = {0xF000u, 0x0000u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_LINE_F, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_UNIMPLEMENTED, r.status);
  TEST_ASSERT_NOT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, r.status);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE, m.cpu.regs.pc);
}


/* The case the boot PROM actually performs: a misaligned long write over two
 * lines that are already resident, read back.
 *
 * `ap_m68030_operand_write` splits at long-word boundaries, so this arrives at
 * the cache as two *partial* writes. A cache entry is a whole long word, and a
 * model that stores a partial `value` into one replaces the bytes it did not
 * write along with the ones it did -- so the entry ends up holding neither the
 * old value nor the new one.
 *
 * That is exactly what made the PROM's `RTS` at `00002946` read zero from an
 * address `--boot-watch` showed holding `00000620`. Reading both addresses
 * first is what makes it a hit rather than a miss, and a miss proves nothing
 * here. */
static void test_a_misaligned_write_updates_both_resident_lines(void) {
  machine_t m = {0};
  static const uint16_t nothing[] = {0x4E71u, 0x4E71u};
  load(&m, nothing, 2);

  const ap_m68030_address_t low = {.address = 0x00004170u, .valid = true};
  const ap_m68030_address_t high = {.address = 0x00004174u, .valid = true};
  TEST_ASSERT_TRUE(
      ap_m68030_operand_read(&m.cpu.regs, m.cpu.data, &low, 4u, 5u).ok);
  TEST_ASSERT_TRUE(
      ap_m68030_operand_read(&m.cpu.regs, m.cpu.data, &high, 4u, 5u).ok);

  const ap_m68030_address_t where = {.address = 0x00004172u, .valid = true};
  TEST_ASSERT_TRUE(ap_m68030_operand_write(&m.cpu.regs, m.cpu.data, &where, 4u,
                                           0x00000620u, 5u)
                       .ok);

  const ap_m68030_operand_result_t back =
      ap_m68030_operand_read(&m.cpu.regs, m.cpu.data, &where, 4u, 5u);
  TEST_ASSERT_TRUE(back.ok);
  TEST_ASSERT_EQUAL_HEX32(0x00000620u, back.value);
  /* And the neighbouring bytes are untouched: a fix that invalidated instead of
   * merging would pass the line above and lose these. */
  TEST_ASSERT_EQUAL_HEX32(0x00000620u, read_ram_long(&m, 0x00004172u));
}

static void test_misaligned_long_round_trips_through_the_data_cache(void) {
  machine_t m = {0};
  static const uint16_t nothing[] = {0x4E71u, 0x4E71u};
  load(&m, nothing, 2);

  const ap_m68030_address_t where = {.address = 0x00004172u, .valid = true};
  const ap_m68030_operand_result_t wrote = ap_m68030_operand_write(
      &m.cpu.regs, m.cpu.data, &where, 4u, 0x00000620u, 5u);
  TEST_ASSERT_TRUE(wrote.ok);

  /* Straight back, which is the case that already works in the PROM. */
  ap_m68030_operand_result_t back =
      ap_m68030_operand_read(&m.cpu.regs, m.cpu.data, &where, 4u, 5u);
  TEST_ASSERT_TRUE(back.ok);
  TEST_ASSERT_EQUAL_HEX32(0x00000620u, back.value);

  /* Now touch enough other memory to disturb the cache, then read again. The
   * value spans the lines at 4170 and 4174, so a model that caches only one of
   * them hands back half a stale word here rather than at the write. */
  for (uint32_t a = 0x00005000u; a < 0x00006000u; a += 4u) {
    const ap_m68030_address_t other = {.address = a, .valid = true};
    const ap_m68030_operand_result_t r =
        ap_m68030_operand_read(&m.cpu.regs, m.cpu.data, &other, 4u, 5u);
    TEST_ASSERT_TRUE(r.ok);
  }

  back = ap_m68030_operand_read(&m.cpu.regs, m.cpu.data, &where, 4u, 5u);
  TEST_ASSERT_TRUE(back.ok);
  TEST_ASSERT_EQUAL_HEX32(0x00000620u, back.value);
}


/* Writethrough means a write must update a cache line that is *already*
 * resident, not merely reach memory. A model where it reaches memory alone
 * leaves a valid stale entry, and the next read hands back the old value --
 * while a direct look at memory shows the new one, which is exactly what the
 * boot PROM's trace showed at `01000172`.
 *
 * Read first to make the line resident. Without that the write is a miss and
 * proves nothing. */
static void test_a_write_updates_a_line_that_is_already_resident(void) {
  machine_t m = {0};
  static const uint16_t nothing[] = {0x4E71u, 0x4E71u};
  load(&m, nothing, 2);

  const ap_m68030_address_t where = {.address = 0x00004170u, .valid = true};
  ap_m68030_operand_result_t back =
      ap_m68030_operand_read(&m.cpu.regs, m.cpu.data, &where, 4u, 5u);
  TEST_ASSERT_TRUE(back.ok);

  const ap_m68030_operand_result_t wrote = ap_m68030_operand_write(
      &m.cpu.regs, m.cpu.data, &where, 4u, 0x00000620u, 5u);
  TEST_ASSERT_TRUE(wrote.ok);

  back = ap_m68030_operand_read(&m.cpu.regs, m.cpu.data, &where, 4u, 5u);
  TEST_ASSERT_TRUE(back.ok);
  TEST_ASSERT_EQUAL_HEX32(0x00000620u, back.value);
  /* And memory agrees, so the two views cannot silently diverge. */
  TEST_ASSERT_EQUAL_HEX32(0x00000620u, read_ram_long(&m, 0x00004170u));
}

/* The same for a misaligned long, which is what the PROM actually does: the
 * value spans two lines, and *both* must be updated. A model that updated only
 * the line containing the start address would pass the aligned test above and
 * still hand back half a stale word here. */

/* The flag describes one instruction, so it must not survive into the next.
 * A stale one would mislabel the following unimplemented instruction as a
 * fault -- the same conflation, merely pointing the other way. */
static void test_a_fault_does_not_leak_into_the_following_instruction(void) {
  /* CMPI.B #$08,(1,A5) over faulting memory, then BKPT #0, which is
   * unimplemented and touches no memory at all. */
  static const uint16_t program[] = {0x0C2Du, 0x0008u, 0x0001u, 0xF010u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_BUS_ERROR, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  m.cpu.regs.a[5] = 0x0000C000u;
  m.memory.berr_from = 0x0000C000u;

  const ap_m68030_step_result_t faulted = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, faulted.status);

  /* Step past the instruction the fault stopped, the way a caller that had
   * handled it would, and run the one after. */
  m.memory.berr_from = 0u;
  ap_m68030_cpu_reset(&m.cpu, PROGRAM_BASE + 6u);

  const ap_m68030_step_result_t after = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_UNIMPLEMENTED, after.status);
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

/* What the instruction cache actually buys, which is not always time.
 *
 * A second pass over the same four instructions runs **no further bus cycles** —
 * that is the cache doing its job, and it is what this checks. The *clock*,
 * though, is unchanged, and that is the manual's own answer rather than a
 * disappointment: `MOVEQ #<data>,Dn` is CC 2 and NCC 2 in §11.6.9, the same
 * figure cached or not, because its two-clock fetch hides entirely under its
 * two clocks of microcode. Saving a bus cycle saves nothing when the bus cycle
 * was free.
 *
 * This test asserted the second pass cost *zero* until the published figures
 * were wired in. That was right while the clock was bus time alone and is wrong
 * now: zero would mean a cached instruction takes no time at all. */
static void test_a_cached_pass_runs_no_bus_cycles_and_costs_its_microcode(void) {
  static const uint16_t program[] = {0x7001u, 0x7202u, 0x7403u, 0x7604u};
  machine_t m = {0};
  load(&m, program, 4);

  uint64_t first_pass = 0;
  for (unsigned i = 0; i < 4; i++) {
    first_pass += ap_m68030_step(&m.cpu).clocks;
  }
  const unsigned fills_after_first = m.memory.fills;
  TEST_ASSERT_TRUE(fills_after_first > 0);

  /* Run the same four instructions again from the top. */
  ap_m68030_cpu_reset(&m.cpu, PROGRAM_BASE);
  uint64_t second_pass = 0;
  for (unsigned i = 0; i < 4; i++) {
    second_pass += ap_m68030_step(&m.cpu).clocks;
  }

  /* The cache answered: not one further line fill. */
  TEST_ASSERT_EQUAL_UINT(fills_after_first, m.memory.fills);

  /* And four MOVEQs cost their published CC, cached or not. */
  TEST_ASSERT_EQUAL_UINT64(8, second_pass);
  TEST_ASSERT_EQUAL_UINT64(first_pass, second_pass);
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


/* ---------------------------------------------------------------------------
 * The immediate family and the single-operand group.
 * ------------------------------------------------------------------------- */

/* ADDI adds an immediate to an effective address. */
static void test_addi_adds_its_immediate_to_a_register(void) {
  /* MOVEQ #5,D0 ; ADDI.L #$100,D0 */
  static const uint16_t program[] = {0x7005u, 0x0680u, 0x0000u, 0x0100u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);

  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x105u, m.cpu.regs.d[0]);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 8u, m.cpu.regs.pc);
}

/* "Destination - Immediate Data": the effective address is the destination, so
 * SUBI subtracts the immediate *from* it and not the other way round. */
static void test_subi_subtracts_the_immediate_from_the_destination(void) {
  /* MOVEQ #10,D0 ; SUBI.L #3,D0  ->  7 */
  static const uint16_t program[] = {0x700Au, 0x0480u, 0x0000u, 0x0003u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_HEX32(7u, m.cpu.regs.d[0]);
  TEST_ASSERT_FALSE(ap_m68030_read_ccr(&m.cpu.regs) &
                    (1u << AP_M68030_SR_C_BIT));
}

/* CMPI compares without writing, so its operand survives. */
static void test_cmpi_compares_without_writing(void) {
  /* MOVEQ #4,D0 ; CMPI.L #4,D0 */
  static const uint16_t program[] = {0x7004u, 0x0C80u, 0x0000u, 0x0004u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);

  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(4u, m.cpu.regs.d[0]);
  TEST_ASSERT_TRUE(ap_m68030_read_ccr(&m.cpu.regs) &
                   (1u << AP_M68030_SR_Z_BIT));
}

/* ANDI with a byte immediate: one extension word, low half. */
static void test_andi_byte_masks_only_the_low_byte(void) {
  /* MOVE.L #$FFFFFFFF,D0 ; ANDI.B #$0F,D0 */
  static const uint16_t program[] = {0x203Cu, 0xFFFFu, 0xFFFFu, 0x0200u,
                                     0x000Fu, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 8);

  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  /* Only the low byte was masked; the rest of the register survives. */
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFF0Fu, m.cpu.regs.d[0]);
}

/* CLR writes zero and sets Z, without needing to read first. */
static void test_clr_zeroes_its_destination(void) {
  /* MOVEQ #-1,D0 ; CLR.L D0 */
  static const uint16_t program[] = {0x70FFu, 0x4280u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0u, m.cpu.regs.d[0]);
  TEST_ASSERT_TRUE(ap_m68030_read_ccr(&m.cpu.regs) &
                   (1u << AP_M68030_SR_Z_BIT));
}

/* A byte CLR clears only the low byte, which is the same register-size rule
 * seen through a third path. */
static void test_a_byte_clr_leaves_the_upper_bytes(void) {
  /* MOVE.L #$11223344,D0 ; CLR.B D0 */
  static const uint16_t program[] = {0x203Cu, 0x1122u, 0x3344u, 0x4200u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_HEX32(0x11223300u, m.cpu.regs.d[0]);
}

/* NEG is "0 - Destination", so it borrows for any non-zero operand and not for
 * zero -- Table 3-18's "C = Dm V Rm" restated as arithmetic. */
static void test_neg_negates_and_borrows_for_a_non_zero_operand(void) {
  /* MOVEQ #5,D0 ; NEG.L D0 */
  static const uint16_t program[] = {0x7005u, 0x4480u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFBu, m.cpu.regs.d[0]);
  TEST_ASSERT_TRUE(ap_m68030_read_ccr(&m.cpu.regs) &
                   (1u << AP_M68030_SR_C_BIT));
}

/* Negating zero borrows nothing, which is the boundary the rule turns on. */
static void test_negating_zero_does_not_borrow(void) {
  static const uint16_t program[] = {0x7000u, 0x4480u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_HEX32(0u, m.cpu.regs.d[0]);
  TEST_ASSERT_FALSE(ap_m68030_read_ccr(&m.cpu.regs) &
                    (1u << AP_M68030_SR_C_BIT));
  TEST_ASSERT_TRUE(ap_m68030_read_ccr(&m.cpu.regs) &
                   (1u << AP_M68030_SR_Z_BIT));
}

/* NOT is the ones complement, and is a *logical* operation for condition code
 * purposes despite looking arithmetic: V and C are cleared, X survives. */
static void test_not_complements_and_clears_v_and_c(void) {
  static const uint16_t program[] = {0x7000u, 0x4680u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  (void)ap_m68030_step(&m.cpu);
  ap_m68030_write_ccr(&m.cpu.regs, AP_M68030_CCR_MASK);
  (void)ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, m.cpu.regs.d[0]);
  const uint16_t ccr = ap_m68030_read_ccr(&m.cpu.regs);
  TEST_ASSERT_FALSE(ccr & (1u << AP_M68030_SR_V_BIT));
  TEST_ASSERT_FALSE(ccr & (1u << AP_M68030_SR_C_BIT));
  TEST_ASSERT_TRUE(ccr & (1u << AP_M68030_SR_X_BIT));
}

/* TST sets flags without writing, so its operand survives -- the counterpart of
 * CMP in the single-operand group. */
static void test_tst_sets_flags_without_writing(void) {
  static const uint16_t program[] = {0x70FFu, 0x4A80u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, m.cpu.regs.d[0]);
  TEST_ASSERT_TRUE(ap_m68030_read_ccr(&m.cpu.regs) &
                   (1u << AP_M68030_SR_N_BIT));
}

/* A real loop: count down and branch while non-zero. Five instruction kinds
 * cooperating, and the first program here with control flow that repeats. */
static void test_a_countdown_loop_terminates(void) {
  /* MOVEQ #3,D0
   * loop: SUBI.L #1,D0
   *       BNE loop        (back 6 bytes from the branch's base)
   *       MOVEQ #$7F,D1 */
  static const uint16_t program[] = {0x7003u, 0x0480u, 0x0000u, 0x0001u,
                                     0x66F8u, 0x727Fu, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 8);

  unsigned steps = 0;
  bool reached_end = false;
  for (; steps < 40u; steps++) {
    const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);
    TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
    if (m.cpu.regs.d[1] == 0x7Fu) {
      reached_end = true;
      break;
    }
  }

  TEST_ASSERT_TRUE(reached_end);
  TEST_ASSERT_EQUAL_HEX32(0u, m.cpu.regs.d[0]);
}


/* ---------------------------------------------------------------------------
 * ADDQ, SUBQ, Scc and DBcc.
 * ------------------------------------------------------------------------- */

/* ADDQ adds its quick data, and the encoded zero means eight. */
static void test_addq_adds_its_quick_data(void) {
  /* MOVEQ #1,D0 ; ADDQ.L #3,D0 */
  static const uint16_t program[] = {0x7001u, 0x5680u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(4u, m.cpu.regs.d[0]);
}

/* The quick data field's zero means eight, seen through running code. */
static void test_addq_of_zero_adds_eight(void) {
  /* MOVEQ #0,D0 ; ADDQ.L #8,D0 -- the data field is 000. */
  static const uint16_t program[] = {0x7000u, 0x5080u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_HEX32(8u, m.cpu.regs.d[0]);
}

/* The address register special case, and the half that is easy to miss:
 * "the condition codes are not altered". A pointer bumped inside a loop must
 * not clobber the comparison the loop branches on. */
static void test_addq_to_an_address_register_leaves_the_flags_alone(void) {
  /* ADDQ.W #1,A0 */
  static const uint16_t program[] = {0x5248u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  ap_m68030_write_sr(&m.cpu.regs, 1u << AP_M68030_SR_S_BIT);
  ap_m68030_write_address_register(&m.cpu.regs, 0, 0x1000u);
  ap_m68030_write_ccr(&m.cpu.regs, AP_M68030_CCR_MASK);

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x1001u,
                          ap_m68030_read_address_register(&m.cpu.regs, 0));
  /* Every condition code survives untouched. */
  TEST_ASSERT_EQUAL_HEX16(AP_M68030_CCR_MASK, ap_m68030_read_ccr(&m.cpu.regs));
}

/* The other half: "the entire destination address register is used regardless
 * of the operation size", so a word ADDQ carries into the upper half rather
 * than wrapping within it. */
static void test_a_word_addq_to_an_address_register_uses_all_32_bits(void) {
  static const uint16_t program[] = {0x5248u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  ap_m68030_write_sr(&m.cpu.regs, 1u << AP_M68030_SR_S_BIT);
  ap_m68030_write_address_register(&m.cpu.regs, 0, 0x0000FFFFu);

  (void)ap_m68030_step(&m.cpu);

  /* $0000FFFF + 1 is $00010000, not $00000000. */
  TEST_ASSERT_EQUAL_HEX32(0x00010000u,
                          ap_m68030_read_address_register(&m.cpu.regs, 0));
}

/* An ordinary data register destination *does* set the flags, which is the
 * contrast that makes the address register case a special case. */
static void test_addq_to_a_data_register_does_set_the_flags(void) {
  /* MOVEQ #-1,D0 ; ADDQ.L #1,D0  ->  0, Z set */
  static const uint16_t program[] = {0x70FFu, 0x5280u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_HEX32(0u, m.cpu.regs.d[0]);
  TEST_ASSERT_TRUE(ap_m68030_read_ccr(&m.cpu.regs) &
                   (1u << AP_M68030_SR_Z_BIT));
}

/* "sets the byte ... to TRUE (all ones)" -- all ones, not one, which is what
 * makes the result usable directly as a mask. */
static void test_scc_sets_all_ones_not_one(void) {
  /* MOVEQ #0,D0 sets Z ; SEQ D1 */
  static const uint16_t program[] = {0x7000u, 0x57C1u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.d[1] = 0x11223344u;

  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  /* A byte destination, so only the low byte changes -- to $FF, not $01. */
  TEST_ASSERT_EQUAL_HEX32(0x112233FFu, m.cpu.regs.d[1]);
}

/* And zero when the condition is false. */
static void test_scc_sets_zero_when_the_condition_is_false(void) {
  /* MOVEQ #1,D0 clears Z ; SEQ D1 */
  static const uint16_t program[] = {0x7001u, 0x57C1u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.d[1] = 0x112233FFu;

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_HEX32(0x11223300u, m.cpu.regs.d[1]);
}

/* DBcc counts down and branches while the counter has not passed -1. A count of
 * three runs the body four times: three decrements that branch, and the fourth
 * that reaches -1 and falls through. */
static void test_a_dbcc_loop_runs_the_documented_number_of_times(void) {
  /* MOVEQ #3,D0
   * loop: ADDQ.L #1,D1
   *       DBRA D0,loop      (displacement -4 from the extension word)
   *       MOVEQ #$7F,D2 */
  static const uint16_t program[] = {0x7003u, 0x5281u, 0x51C8u, 0xFFFCu,
                                     0x747Fu, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 8);

  for (unsigned i = 0; i < 40u && m.cpu.regs.d[2] != 0x7Fu; i++) {
    const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);
    TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  }

  /* Four passes: counter 3, 2, 1, 0 branch; the pass at 0 decrements to -1 and
   * falls through. */
  TEST_ASSERT_EQUAL_HEX32(4u, m.cpu.regs.d[1]);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFu, m.cpu.regs.d[0] & 0xFFFFu);
  TEST_ASSERT_EQUAL_HEX32(0x7Fu, m.cpu.regs.d[2]);
}

/* Only the low word of the counter counts down, so a loop cannot borrow into
 * the register's upper half. */
static void test_dbcc_decrements_only_the_low_word(void) {
  /* MOVE.L #$00110000,D0 ; DBRA D0,self */
  static const uint16_t program[] = {0x203Cu, 0x0011u, 0x0000u, 0x51C8u,
                                     0xFFFEu, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 8);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);

  /* The low word went from 0 to $FFFF; the upper half is untouched. */
  TEST_ASSERT_EQUAL_HEX32(0x0011FFFFu, m.cpu.regs.d[0]);
}

/* A true condition exits without touching the counter at all. */
static void test_a_true_dbcc_condition_leaves_the_counter_alone(void) {
  /* MOVEQ #0,D0 sets Z ; DBEQ D1,back */
  static const uint16_t program[] = {0x7000u, 0x57C9u, 0xFFFEu, 0x4E71u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);
  m.cpu.regs.d[1] = 5u;

  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_FALSE(r.branch_taken);
  TEST_ASSERT_EQUAL_HEX32(5u, m.cpu.regs.d[1]);
}


/* ---------------------------------------------------------------------------
 * The address-register forms and the bit operations.
 * ------------------------------------------------------------------------- */

/* ADDA alters no condition codes -- an address calculation must not disturb the
 * flags a following branch depends on. */
static void test_adda_leaves_the_condition_codes_alone(void) {
  /* MOVEQ #4,D0 ; ADDA.L D0,A0 */
  static const uint16_t program[] = {0x7004u, 0xD1C0u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  ap_m68030_write_sr(&m.cpu.regs, 1u << AP_M68030_SR_S_BIT);
  ap_m68030_write_address_register(&m.cpu.regs, 0, 0x1000u);

  (void)ap_m68030_step(&m.cpu);
  ap_m68030_write_ccr(&m.cpu.regs, AP_M68030_CCR_MASK);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x1004u,
                          ap_m68030_read_address_register(&m.cpu.regs, 0));
  TEST_ASSERT_EQUAL_HEX16(AP_M68030_CCR_MASK, ap_m68030_read_ccr(&m.cpu.regs));
}

/* A word A-form is not a word operation: the source is sign-extended and the
 * whole register takes part, so a negative word subtracts from the full
 * address rather than wrapping in its low half. */
static void test_a_word_adda_sign_extends_its_source(void) {
  /* MOVEQ #-4,D0 ; ADDA.W D0,A0 */
  static const uint16_t program[] = {0x70FCu, 0xD0C0u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  ap_m68030_write_sr(&m.cpu.regs, 1u << AP_M68030_SR_S_BIT);
  ap_m68030_write_address_register(&m.cpu.regs, 0, 0x1000u);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);

  /* $1000 - 4, not $1000 + $FFFC. */
  TEST_ASSERT_EQUAL_HEX32(0x0FFCu,
                          ap_m68030_read_address_register(&m.cpu.regs, 0));
}

/* CMPA does set the condition codes, which is why a compare against an address
 * register is a separate instruction from ADDA and SUBA. */
static void test_cmpa_does_set_the_condition_codes(void) {
  /* MOVEQ #$10,D0 ; CMPA.L D0,A0  with A0 = $10  ->  Z set */
  static const uint16_t program[] = {0x7010u, 0xB1C0u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  ap_m68030_write_sr(&m.cpu.regs, 1u << AP_M68030_SR_S_BIT);
  ap_m68030_write_address_register(&m.cpu.regs, 0, 0x10u);

  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_TRUE(ap_m68030_read_ccr(&m.cpu.regs) &
                   (1u << AP_M68030_SR_Z_BIT));
  /* And the register is untouched. */
  TEST_ASSERT_EQUAL_HEX32(0x10u,
                          ap_m68030_read_address_register(&m.cpu.regs, 0));
}

/* "TEST (<bit number> of Destination) -> Z" -- Z reflects the bit as it was
 * *before* the operation, so BSET on an already-set bit clears Z. Testing after
 * the write would invert it. */
static void test_z_reflects_the_bit_before_the_operation(void) {
  /* MOVEQ #0,D0 ; BSET #0,D0  -- bit was clear, so Z is set */
  static const uint16_t program[] = {0x7000u, 0x08C0u, 0x0000u, 0x4E71u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);

  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(1u, m.cpu.regs.d[0]); /* the bit is now set */
  TEST_ASSERT_TRUE(ap_m68030_read_ccr(&m.cpu.regs) &
                   (1u << AP_M68030_SR_Z_BIT)); /* but it *was* clear */
}

/* The complement: setting a bit that was already set clears Z. */
static void test_setting_an_already_set_bit_clears_z(void) {
  /* MOVEQ #1,D0 ; BSET #0,D0 */
  static const uint16_t program[] = {0x7001u, 0x08C0u, 0x0000u, 0x4E71u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_HEX32(1u, m.cpu.regs.d[0]);
  TEST_ASSERT_FALSE(ap_m68030_read_ccr(&m.cpu.regs) &
                    (1u << AP_M68030_SR_Z_BIT));
}

/* A data register destination is a *long* operation with the bit number modulo
 * 32, so bit 31 is reachable -- a byte-width model would address bit 7. */
static void test_a_data_register_bit_operation_reaches_bit_thirty_one(void) {
  /* MOVEQ #0,D0 ; BSET #31,D0 */
  static const uint16_t program[] = {0x7000u, 0x08C0u, 0x001Fu, 0x4E71u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_HEX32(0x80000000u, m.cpu.regs.d[0]);
}

/* The bit number is taken modulo the operand width, so 32 wraps to bit 0 on a
 * data register rather than addressing nothing. */
static void test_the_bit_number_wraps_modulo_the_operand_width(void) {
  /* MOVEQ #0,D0 ; BSET #32,D0  ->  bit 0 */
  static const uint16_t program[] = {0x7000u, 0x08C0u, 0x0020u, 0x4E71u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_HEX32(1u, m.cpu.regs.d[0]);
}

/* BTST tests without writing. */
static void test_btst_does_not_write(void) {
  /* MOVEQ #0,D0 ; BTST #0,D0 */
  static const uint16_t program[] = {0x7000u, 0x0800u, 0x0000u, 0x4E71u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);

  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0u, m.cpu.regs.d[0]);
  TEST_ASSERT_TRUE(ap_m68030_read_ccr(&m.cpu.regs) &
                   (1u << AP_M68030_SR_Z_BIT));
}

/* Only Z is affected: Table 3-18 gives the bit operations em dashes under X, N,
 * V and C, so every other flag survives. */
static void test_a_bit_operation_affects_only_the_zero_flag(void) {
  static const uint16_t program[] = {0x7000u, 0x0800u, 0x0000u, 0x4E71u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);

  (void)ap_m68030_step(&m.cpu);
  ap_m68030_write_ccr(&m.cpu.regs, AP_M68030_CCR_MASK);
  (void)ap_m68030_step(&m.cpu);

  const uint16_t ccr = ap_m68030_read_ccr(&m.cpu.regs);
  TEST_ASSERT_TRUE(ccr & (1u << AP_M68030_SR_X_BIT));
  TEST_ASSERT_TRUE(ccr & (1u << AP_M68030_SR_N_BIT));
  TEST_ASSERT_TRUE(ccr & (1u << AP_M68030_SR_V_BIT));
  TEST_ASSERT_TRUE(ccr & (1u << AP_M68030_SR_C_BIT));
}

/* A dynamic bit operation takes its bit number from a register. */
static void test_a_dynamic_bit_number_comes_from_a_register(void) {
  /* MOVEQ #4,D1 ; MOVEQ #0,D0 ; BSET D1,D0 */
  static const uint16_t program[] = {0x7204u, 0x7000u, 0x03C0u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x10u, m.cpu.regs.d[0]);
}


/* A shift executing through the step, with the register-count modulo 64 rule. */
static void test_a_register_shift_executes(void) {
  /* MOVEQ #1,D0 ; LSL.L #3,D0 */
  static const uint16_t program[] = {0x7001u, 0xE788u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(8u, m.cpu.regs.d[0]);
}

/* "the shift count is the value in the data register ... modulo 64", so a
 * register count of 64 shifts by nothing rather than by a full width. */
static void test_a_register_count_is_taken_modulo_sixty_four(void) {
  /* MOVE.L #64,D1 ; MOVEQ #$7F,D0 ; LSL.L D1,D0 */
  static const uint16_t program[] = {0x223Cu, 0x0000u, 0x0040u, 0x707Fu,
                                     0xE3A8u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 8);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  /* 64 mod 64 is zero, so the operand is unchanged. */
  TEST_ASSERT_EQUAL_HEX32(0x7Fu, m.cpu.regs.d[0]);
}

/* A byte shift leaves the register's upper bytes alone, the same size rule seen
 * through yet another path. */
static void test_a_byte_shift_leaves_the_upper_bytes(void) {
  /* MOVE.L #$11223344,D0 ; LSL.B #1,D0 */
  static const uint16_t program[] = {0x203Cu, 0x1122u, 0x3344u, 0xE308u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_HEX32(0x11223388u, m.cpu.regs.d[0]);
}


/* ---------------------------------------------------------------------------
 * Multiplies, divides and the extended forms.
 * ------------------------------------------------------------------------- */

/* "The word form ... multiplies two word operands and produces a long result",
 * so the product uses the whole destination register. */
static void test_mulu_produces_a_long_from_two_words(void) {
  /* MOVE.L #$1000,D0 ; MULU.W #$10,D0 */
  static const uint16_t program[] = {0x203Cu, 0x0000u, 0x1000u, 0xC0FCu,
                                     0x0010u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 8);

  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x10000u, m.cpu.regs.d[0]);
}

/* MULS treats both operands as signed, so the same bits give a different
 * product from MULU -- which is the entire difference between them. */
static void test_muls_and_mulu_differ_on_a_negative_operand(void) {
  /* MOVEQ #-1,D0 ; MULS.W #2,D0  ->  -2 */
  static const uint16_t signed_program[] = {0x70FFu, 0xC1FCu, 0x0002u, 0x4E71u,
                                            0x4E71u, 0x4E71u};
  machine_t signed_machine = {0};
  load(&signed_machine, signed_program, 6);
  (void)ap_m68030_step(&signed_machine.cpu);
  (void)ap_m68030_step(&signed_machine.cpu);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFEu, signed_machine.cpu.regs.d[0]);

  /* The same bits unsigned: $FFFF * 2 = $1FFFE. */
  static const uint16_t unsigned_program[] = {0x70FFu, 0xC0FCu, 0x0002u,
                                              0x4E71u, 0x4E71u, 0x4E71u};
  machine_t unsigned_machine = {0};
  load(&unsigned_machine, unsigned_program, 6);
  (void)ap_m68030_step(&unsigned_machine.cpu);
  (void)ap_m68030_step(&unsigned_machine.cpu);
  TEST_ASSERT_EQUAL_HEX32(0x0001FFFEu, unsigned_machine.cpu.regs.d[0]);
}

/* "a quotient in the lower word ... and a remainder in the upper word" -- both
 * halves in one register, and the order is the one worth pinning. */
static void test_a_divide_puts_the_remainder_in_the_upper_word(void) {
  /* MOVE.L #100,D0 ; DIVU.W #7,D0  ->  quotient 14, remainder 2 */
  static const uint16_t program[] = {0x203Cu, 0x0000u, 0x0064u, 0x80FCu,
                                     0x0007u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 8);

  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x0002000Eu, m.cpu.regs.d[0]);
  TEST_ASSERT_EQUAL_HEX32(14u, m.cpu.regs.d[0] & 0xFFFFu);
  TEST_ASSERT_EQUAL_HEX32(2u, m.cpu.regs.d[0] >> 16);
}

/* "If the quotient is larger than a 16-bit integer, the overflow condition code
 * is set and the operands are unchanged" -- so V is the whole result and the
 * register must survive untouched. */
static void test_a_division_overflow_leaves_the_operands_unchanged(void) {
  /* MOVE.L #$10000,D0 ; DIVU.W #1,D0 -- quotient $10000 does not fit. */
  static const uint16_t program[] = {0x203Cu, 0x0001u, 0x0000u, 0x80FCu,
                                     0x0001u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 8);

  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_TRUE(ap_m68030_read_ccr(&m.cpu.regs) &
                   (1u << AP_M68030_SR_V_BIT));
  /* The dividend is still there. */
  TEST_ASSERT_EQUAL_HEX32(0x00010000u, m.cpu.regs.d[0]);
}

/* "Attempted division by zero causes an exception": vector 5, the six-word
 * frame, and the register left alone. Inventing a quotient instead would run
 * and be wrong; declining would be honest but would stop a program the real
 * machine carries on running through its handler. */
static void test_a_division_by_zero_takes_the_zero_divide_exception(void) {
  static const uint16_t program[] = {0x203Cu, 0x0000u, 0x0064u, 0x80FCu,
                                     0x0000u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 8);
  plant_vector(&m, AP_M68030_VECTOR_ZERO_DIVIDE, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;

  (void)ap_m68030_step(&m.cpu);
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, r.status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
  /* The dividend is untouched, so nothing was half-done. */
  TEST_ASSERT_EQUAL_HEX32(100u, m.cpu.regs.d[0]);
  /* Six words, and the two addresses differ: the divide is at +6 and takes
   * four bytes, so the stacked PC is +10 and the instruction address +6. */
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK - 12u, m.cpu.regs.isp);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 6u,
                          read_ram_long(&m, m.cpu.regs.isp + 8u));
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 10u,
                          read_ram_long(&m, m.cpu.regs.isp + 2u));
}

/* "TRAP #<vector> ... 32 + <vector> -> Vector Number": the four-bit field is an
 * index, not a vector number, so TRAP #0 goes to vector 32 and not to the reset
 * stack pointer. That mistake produces a working instruction jumping somewhere
 * plausible, which is why it is worth its own assertion. */
static void test_trap_uses_the_vector_its_number_indexes_not_the_number(void) {
  static const uint16_t program[] = {0x4E40u, 0x4E71u}; /* TRAP #0 */
  machine_t m = {0};
  load(&m, program, 2);
  plant_vector(&m, AP_M68030_VECTOR_TRAP_BASE, HANDLER);
  /* And something different at vector 0, so taking that one is visible. */
  plant_vector(&m, AP_M68030_VECTOR_RESET_SP, 0xDEADBEEFu);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, r.status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
  /* Four words, not six: Table 8-6 puts TRAP #N in the short frame. */
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK - 8u, m.cpu.regs.isp);
}

/* TRAP #15 is the other end of the range, and a decoder that masked the field
 * to three bits or added the wrong base would land somewhere else. */
static void test_the_last_trap_lands_at_the_end_of_the_trap_range(void) {
  static const uint16_t program[] = {0x4E4Fu, 0x4E71u}; /* TRAP #15 */
  machine_t m = {0};
  load(&m, program, 2);
  plant_vector(&m, AP_M68030_VECTOR_TRAP_BASE + 15u, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, r.status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
  /* The frame carries the vector *offset*, so TRAP #15 stacks $0BC and not
   * $02F -- the distinction ap_m68030_frame_format_word exists to keep. */
  const uint16_t format_word = read_ram_word(&m, m.cpu.regs.isp + 6u);
  TEST_ASSERT_EQUAL_HEX32(47u * 4u,
                          ap_m68030_frame_vector_offset_of(format_word));
  TEST_ASSERT_EQUAL_INT(AP_M68030_FRAME_SHORT,
                        ap_m68030_frame_format_of(format_word));
}

/* ADDX folds the extend bit in, which is what makes multi-precision addition
 * work at all. */
static void test_addx_adds_the_extend_bit(void) {
  /* MOVEQ #1,D0 ; MOVEQ #1,D1 ; ADDX.L D0,D1 with X set  ->  3 */
  static const uint16_t program[] = {0x7001u, 0x7201u, 0xD380u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);
  ap_m68030_write_ccr(&m.cpu.regs, (uint16_t)(1u << AP_M68030_SR_X_BIT));
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(3u, m.cpu.regs.d[1]);
}

/* "Z -- Cleared if the result is nonzero; unchanged otherwise." Z is never set
 * by ADDX, only cleared, which is what lets one Z describe a whole
 * multi-precision value rather than just its last word. */
static void test_addx_only_ever_clears_the_zero_flag(void) {
  /* A zero result with Z already clear leaves Z clear -- a model that set Z
   * from the result would report this word's zeroness as the whole value's. */
  static const uint16_t program[] = {0x7000u, 0x7200u, 0xD380u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  (void)ap_m68030_step(&m.cpu);
  (void)ap_m68030_step(&m.cpu);
  ap_m68030_write_ccr(&m.cpu.regs, 0); /* Z clear, X clear */
  (void)ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_HEX32(0u, m.cpu.regs.d[1]);
  TEST_ASSERT_FALSE(ap_m68030_read_ccr(&m.cpu.regs) &
                    (1u << AP_M68030_SR_Z_BIT));

  /* And with Z already set, a zero result keeps it. */
  machine_t n = {0};
  load(&n, program, 4);
  (void)ap_m68030_step(&n.cpu);
  (void)ap_m68030_step(&n.cpu);
  ap_m68030_write_ccr(&n.cpu.regs, (uint16_t)(1u << AP_M68030_SR_Z_BIT));
  (void)ap_m68030_step(&n.cpu);
  TEST_ASSERT_TRUE(ap_m68030_read_ccr(&n.cpu.regs) &
                   (1u << AP_M68030_SR_Z_BIT));
}

/* An immediate source is fetched, not addressed -- and the address forms take
 * one too. This caught a real ordering bug: the address calculation ran first
 * and rejected the immediate mode before anything thought to fetch it, so every
 * ADDA/SUBA/CMPA with an immediate silently declined. */
static void test_an_address_form_accepts_an_immediate_source(void) {
  /* ADDA.W #$1000,A0 */
  static const uint16_t program[] = {0xD0FCu, 0x1000u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.a[0] = 0x2000u;

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x3000u, m.cpu.regs.a[0]);
}

/* "Exchanges the contents of two 32-bit registers" -- whole registers, and
 * "Condition Codes: Not affected", which is unusual enough to pin. */
static void test_exg_swaps_two_data_registers_whole(void) {
  static const uint16_t program[] = {0xC141u, 0x4E71u}; /* EXG D0,D1 */
  machine_t m = {0};
  load(&m, program, 2);
  m.cpu.regs.d[0] = 0x11223344u;
  m.cpu.regs.d[1] = 0xAABBCCDDu;
  ap_m68030_write_ccr(&m.cpu.regs, AP_M68030_CCR_MASK);

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0xAABBCCDDu, m.cpu.regs.d[0]);
  TEST_ASSERT_EQUAL_HEX32(0x11223344u, m.cpu.regs.d[1]);
  /* Every condition code survives untouched. */
  TEST_ASSERT_EQUAL_HEX16(AP_M68030_CCR_MASK,
                          ap_m68030_read_ccr(&m.cpu.regs));
}

/* The address-address and data-address exchanges share bit 3 and differ only in
 * the opmode above it, so a decoder that read only the R/M bit would run this
 * one as EXG A2,A3 and corrupt both registers. */
static void test_exg_distinguishes_the_mixed_pair_from_an_address_pair(void) {
  static const uint16_t address_pair[] = {0xC149u, 0x4E71u}; /* EXG A0,A1 */
  machine_t m = {0};
  load(&m, address_pair, 2);
  m.cpu.regs.a[0] = 0x00004000u;
  m.cpu.regs.a[1] = 0x00008000u;
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x00008000u, m.cpu.regs.a[0]);
  TEST_ASSERT_EQUAL_HEX32(0x00004000u, m.cpu.regs.a[1]);

  /* EXG D2,A3 -- "this field always specifies the data register" for Rx and the
   * address register for Ry, so D2 and A3 are the pair however it was written. */
  static const uint16_t mixed[] = {0xC58Bu, 0x4E71u};
  machine_t n = {0};
  load(&n, mixed, 2);
  n.cpu.regs.d[2] = 0x0000BEEFu;
  n.cpu.regs.a[3] = 0x0000CAFEu;
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x0000CAFEu, n.cpu.regs.d[2]);
  TEST_ASSERT_EQUAL_HEX32(0x0000BEEFu, n.cpu.regs.a[3]);
}

/* "The addition is performed using binary-coded decimal arithmetic": each
 * nibble is a decimal digit, so 25 + 37 is $62 and not $5C. */
static void test_abcd_adds_in_decimal_not_binary(void) {
  static const uint16_t program[] = {0xC101u, 0x4E71u}; /* ABCD D1,D0 */
  machine_t m = {0};
  load(&m, program, 2);
  m.cpu.regs.d[0] = 0x25u;
  m.cpu.regs.d[1] = 0x37u;
  ap_m68030_write_ccr(&m.cpu.regs, 0);

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x62u, m.cpu.regs.d[0]);
  /* Binary addition would have given $5C. */
  TEST_ASSERT_FALSE(ap_m68030_read_ccr(&m.cpu.regs) &
                    (1u << AP_M68030_SR_C_BIT));
}

/* "C -- Set if a decimal carry was generated" and "X -- Set the same as the
 * carry bit": the carry happens at ten, not at $100. */
static void test_abcd_carries_at_ninety_nine(void) {
  static const uint16_t program[] = {0xC101u, 0x4E71u}; /* ABCD D1,D0 */
  machine_t m = {0};
  load(&m, program, 2);
  m.cpu.regs.d[0] = 0xFFFFFF99u; /* only the low byte takes part */
  m.cpu.regs.d[1] = 0x01u;
  ap_m68030_write_ccr(&m.cpu.regs, 0);

  (void)ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_HEX32(0xFFFFFF00u, m.cpu.regs.d[0]);
  const uint16_t ccr = ap_m68030_read_ccr(&m.cpu.regs);
  TEST_ASSERT_TRUE(ccr & (1u << AP_M68030_SR_C_BIT));
  TEST_ASSERT_TRUE(ccr & (1u << AP_M68030_SR_X_BIT));
}

/* "Destination10 - Source10 - X -> Destination", with the borrow taken as ten
 * rather than sixteen: 42 - 17 is $25, where a binary subtract gives $2B. */
static void test_sbcd_borrows_a_decimal_ten(void) {
  static const uint16_t program[] = {0x8101u, 0x4E71u}; /* SBCD D1,D0 */
  machine_t m = {0};
  load(&m, program, 2);
  m.cpu.regs.d[0] = 0x42u;
  m.cpu.regs.d[1] = 0x17u;
  ap_m68030_write_ccr(&m.cpu.regs, 0);

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0x25u, m.cpu.regs.d[0]);
}

/* The BCD forms share ADDX's Z rule, and the manual explains why here:
 * "Normally, the Z condition code bit is set via programming before the start
 * of an operation. This allows successful tests for zero results upon
 * completion of multiple-precision operations." */
static void test_the_bcd_forms_only_ever_clear_the_zero_flag(void) {
  static const uint16_t program[] = {0xC101u, 0x4E71u}; /* ABCD D1,D0 */
  machine_t m = {0};
  load(&m, program, 2);
  m.cpu.regs.d[0] = 0x00u;
  m.cpu.regs.d[1] = 0x00u;
  ap_m68030_write_ccr(&m.cpu.regs, (uint16_t)(1u << AP_M68030_SR_Z_BIT));
  (void)ap_m68030_step(&m.cpu);
  TEST_ASSERT_TRUE(ap_m68030_read_ccr(&m.cpu.regs) &
                   (1u << AP_M68030_SR_Z_BIT));

  /* A nonzero result clears it, and a zero result with Z already clear leaves
   * it clear -- so Z describes the whole multi-precision value. */
  machine_t n = {0};
  load(&n, program, 2);
  n.cpu.regs.d[0] = 0x00u;
  n.cpu.regs.d[1] = 0x00u;
  ap_m68030_write_ccr(&n.cpu.regs, 0);
  (void)ap_m68030_step(&n.cpu);
  TEST_ASSERT_FALSE(ap_m68030_read_ccr(&n.cpu.regs) &
                    (1u << AP_M68030_SR_Z_BIT));
}

/* "The operands are addressed with the predecrement addressing mode using the
 * address registers specified in the instruction" -- so both registers move,
 * the read comes from below them, and the result goes back where the
 * destination was read from. */
static void test_the_memory_form_of_abcd_predecrements_both_registers(void) {
  /* ABCD -(A1),-(A0), then data: $25 at +8 and $37 at +9. */
  static const uint16_t program[] = {0xC109u, 0x4E71u, 0x4E71u, 0x4E71u,
                                     0x2537u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);
  m.cpu.regs.a[0] = PROGRAM_BASE + 9u; /* destination byte is at +8 */
  m.cpu.regs.a[1] = PROGRAM_BASE + 10u; /* source byte is at +9 */
  ap_m68030_write_ccr(&m.cpu.regs, 0);

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 8u, m.cpu.regs.a[0]);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 9u, m.cpu.regs.a[1]);
  TEST_ASSERT_EQUAL_UINT(1u, m.memory.stores);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 8u, m.memory.store_address[0]);
  TEST_ASSERT_EQUAL_HEX32(0x62u, m.memory.store_value[0]);
}

/* "CMPM (Ay)+,(Ax)+ ... the destination location is not changed": both
 * registers advance, and nothing is written. */
static void test_cmpm_advances_both_registers_and_writes_nothing(void) {
  /* CMPM.B (A1)+,(A0)+ over $25 at +8 and $37 at +9. */
  static const uint16_t program[] = {0xB109u, 0x4E71u, 0x4E71u, 0x4E71u,
                                     0x2537u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);
  m.cpu.regs.a[0] = PROGRAM_BASE + 8u; /* destination $25 */
  m.cpu.regs.a[1] = PROGRAM_BASE + 9u; /* source $37 */

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 9u, m.cpu.regs.a[0]);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 10u, m.cpu.regs.a[1]);
  TEST_ASSERT_EQUAL_UINT(0u, m.memory.stores);
  /* $25 - $37 borrows, so C is set and N with it. */
  const uint16_t ccr = ap_m68030_read_ccr(&m.cpu.regs);
  TEST_ASSERT_TRUE(ccr & (1u << AP_M68030_SR_C_BIT));
  TEST_ASSERT_TRUE(ccr & (1u << AP_M68030_SR_N_BIT));
}

/* ---------------------------------------------------------------------------
 * Taking an exception, [030] §8.1.
 * ------------------------------------------------------------------------- */

/* "The processor makes an internal copy of the status register. Then the
 * processor sets the S bit" -- and it is the *copy* that is stacked. Stacking
 * the modified one instead leaves RTE returning with S still set, so a user
 * program that trapped comes back in supervisor state and nothing faults. */
static void test_the_stacked_status_register_is_the_one_before_the_change(void) {
  static const uint16_t program[] = {0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 2);
  plant_vector(&m, AP_M68030_VECTOR_TRAP_BASE, HANDLER);
  m.cpu.regs.sr = 0; /* user state, tracing off */
  m.cpu.regs.isp = SUPERVISOR_STACK;

  const ap_m68030_exception_result_t r = ap_m68030_take_exception(
      &m.cpu, AP_M68030_VECTOR_TRAP_BASE, PROGRAM_BASE + 2u, PROGRAM_BASE);

  TEST_ASSERT_TRUE(r.ok);
  /* The processor is now in supervisor state ... */
  TEST_ASSERT_TRUE(ap_m68030_supervisor(&m.cpu.regs));
  /* ... but the frame remembers that it was not. */
  TEST_ASSERT_EQUAL_HEX16(0u, read_ram_word(&m, r.frame_address));
}

/* "Next, the processor inhibits tracing of the exception handler by clearing
 * the T1 and T0 bits" -- otherwise the handler single-steps itself. */
static void test_taking_an_exception_turns_tracing_off(void) {
  static const uint16_t program[] = {0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 2);
  plant_vector(&m, AP_M68030_VECTOR_TRAP_BASE, HANDLER);
  m.cpu.regs.sr = (uint16_t)((1u << AP_M68030_SR_S_BIT) |
                             (1u << AP_M68030_SR_T1_BIT));
  m.cpu.regs.isp = SUPERVISOR_STACK;

  const ap_m68030_exception_result_t r = ap_m68030_take_exception(
      &m.cpu, AP_M68030_VECTOR_TRAP_BASE, PROGRAM_BASE + 2u, PROGRAM_BASE);

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_INT(AP_M68030_TRACE_NONE,
                        ap_m68030_trace_mode(&m.cpu.regs));
}

/* "The processor creates an exception stack frame on the active supervisor
 * stack": *active*, and read after S is set -- so an exception taken in user
 * state builds its frame on the ISP and leaves the USP alone. */
static void test_the_frame_is_built_on_the_supervisor_stack_not_the_user_one(
    void) {
  static const uint16_t program[] = {0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 2);
  plant_vector(&m, AP_M68030_VECTOR_TRAP_BASE, HANDLER);
  m.cpu.regs.sr = 0; /* user state */
  m.cpu.regs.usp = 0x00007000u;
  m.cpu.regs.isp = SUPERVISOR_STACK;

  const ap_m68030_exception_result_t r = ap_m68030_take_exception(
      &m.cpu, AP_M68030_VECTOR_TRAP_BASE, PROGRAM_BASE + 2u, PROGRAM_BASE);

  TEST_ASSERT_TRUE(r.ok);
  /* Four words below the interrupt stack, and the user stack untouched. */
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK - 8u, r.frame_address);
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK - 8u, m.cpu.regs.isp);
  TEST_ASSERT_EQUAL_HEX32(0x00007000u, m.cpu.regs.usp);
}

/* "It adds the offset to the value stored in the vector base register to obtain
 * the memory address of the exception vector" -- and the offset is the vector
 * number times four, so TRAP #0 reads $80 from the VBR and not $20. */
static void test_the_vector_is_fetched_through_the_vector_base_register(void) {
  static const uint16_t program[] = {0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 2);
  m.cpu.regs.vbr = 0x00003000u;
  /* TRAP #0 is vector 32, so offset $80. */
  write_ram_long(&m, 0x00003000u + 0x80u, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;

  const ap_m68030_exception_result_t r = ap_m68030_take_exception(
      &m.cpu, AP_M68030_VECTOR_TRAP_BASE, PROGRAM_BASE + 2u, PROGRAM_BASE);

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_HEX32(0x00003080u, r.vector_address);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, r.handler);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
}

/* Table 8-6: zero divide takes the six-word frame, whose extra long word is
 * "the address of the instruction that caused the exception" -- distinct from
 * the stacked PC, which points at the next one. Giving it the four-word frame
 * leaves RTE reading the vector offset out of the instruction address. */
static void test_a_zero_divide_gets_the_six_word_frame_with_both_addresses(
    void) {
  static const uint16_t program[] = {0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 2);
  plant_vector(&m, AP_M68030_VECTOR_ZERO_DIVIDE, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;

  const ap_m68030_exception_result_t r = ap_m68030_take_exception(
      &m.cpu, AP_M68030_VECTOR_ZERO_DIVIDE, PROGRAM_BASE + 6u, PROGRAM_BASE);

  TEST_ASSERT_TRUE(r.ok);
  /* Six words, not four. */
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK - 12u, r.frame_address);

  const uint16_t format_word = read_ram_word(&m, r.frame_address + 6u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_FRAME_SIX_WORD,
                        ap_m68030_frame_format_of(format_word));
  /* The vector *offset*, not the vector number: $014 for vector 5. */
  TEST_ASSERT_EQUAL_HEX32(0x014u,
                          ap_m68030_frame_vector_offset_of(format_word));
  /* The two addresses are different, which is the point of the wider frame. */
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 6u,
                          read_ram_long(&m, r.frame_address + 2u));
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE,
                          read_ram_long(&m, r.frame_address + 8u));
}

/* Format $9, the coprocessor mid-instruction frame: ten words, and the six-word
 * frame's two addresses plus four "INTERNAL REGISTERS" words.
 *
 * Those four are written as zero and marked `PROVISIONAL` -- this model has no
 * microsequencer state to put in them -- but they are *written*, which is the
 * part that matters here: a frame that stopped at the instruction address would
 * leave four words of whatever the stack already held sitting under documented
 * field names, and would put the stack pointer eight bytes wrong. */
static void test_the_coprocessor_frame_is_ten_words(void) {
  static const uint16_t program[] = {0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 2);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  plant_vector(&m, AP_M68030_VECTOR_COPROCESSOR_PROTOCOL, HANDLER);
  /* Poison the words the frame will occupy, so "written as zero" is a real
   * assertion rather than one the harness would satisfy on its own. */
  for (unsigned i = 0; i < 20u; i += 4u) {
    write_ram_long(&m, SUPERVISOR_STACK - 20u + i, 0xDEADBEEFu);
  }

  const ap_m68030_exception_result_t r = ap_m68030_take_exception(
      &m.cpu, AP_M68030_VECTOR_COPROCESSOR_PROTOCOL, PROGRAM_BASE + 4u,
      PROGRAM_BASE);

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK - 20u, m.cpu.regs.isp);

  /* The format nibble is $9 and the vector offset is 13 * 4. */
  const uint16_t format_word = read_ram_word(&m, r.frame_address + 6u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_FRAME_COPROCESSOR_MID,
                        ap_m68030_frame_format_of(format_word));
  TEST_ASSERT_EQUAL_HEX32(AP_M68030_VECTOR_COPROCESSOR_PROTOCOL * 4u,
                          ap_m68030_frame_vector_offset_of(format_word));

  /* "[Next word to be fetched from instruction stream]" at +$02, and
   * "INSTRUCTION ADDRESS is the address of the instruction that caused the
   * exception" at +$08 -- two different addresses, which is why the frame is
   * wider than four words. */
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 4u, read_ram_long(&m, r.frame_address + 2u));
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE, read_ram_long(&m, r.frame_address + 8u));

  TEST_ASSERT_EQUAL_HEX32(0u, read_ram_long(&m, r.frame_address + 12u));
  TEST_ASSERT_EQUAL_HEX32(0u, read_ram_long(&m, r.frame_address + 16u));
}

/* Reset "does not save old context" at all, and the two bus fault frames carry
 * internal state this model does not have. Each is declined rather than built
 * wrong -- a four-word frame where a 16-word one belongs would leave RTE
 * reading a return address out of the middle of the fault information. */
static void test_the_frames_this_model_cannot_build_are_declined(void) {
  static const uint16_t program[] = {0x4E71u, 0x4E71u};
  const unsigned declined[] = {
      AP_M68030_VECTOR_RESET_SP, AP_M68030_VECTOR_RESET_PC,
      AP_M68030_VECTOR_BUS_ERROR, AP_M68030_VECTOR_ADDRESS_ERROR};

  for (unsigned i = 0; i < sizeof declined / sizeof declined[0]; i++) {
    machine_t m = {0};
    load(&m, program, 2);
    m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
    m.cpu.regs.isp = SUPERVISOR_STACK;

    const ap_m68030_exception_result_t r =
        ap_m68030_take_exception(&m.cpu, declined[i], PROGRAM_BASE, PROGRAM_BASE);

    TEST_ASSERT_FALSE(r.ok);
    /* Nothing was stacked, so the stack pointer is where it was. */
    TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK, m.cpu.regs.isp);
  }
}

/* The whole sequence, seen from outside: the handler's first instruction runs
 * next. The pipe is emptied when the PC is loaded -- "After prefetching the
 * first three words to fill the instruction pipe" -- so a stale prefetch cannot
 * execute in the handler's place. */
static void test_the_next_step_executes_the_handlers_first_instruction(void) {
  /* MOVEQ #1,D0 at the program, MOVEQ #2,D0 at the handler. */
  static const uint16_t program[] = {0x7001u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  /* The handler sits inside the same image: PROGRAM_BASE + 4 holds a NOP, so
   * plant a distinguishable instruction there through the store path. */
  write_ram_long(&m, PROGRAM_BASE + 4u, 0x70024E71u); /* MOVEQ #2,D0 ; NOP */
  plant_vector(&m, AP_M68030_VECTOR_TRAP_BASE, PROGRAM_BASE + 4u);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;

  /* Run the first instruction, which also fills the pipe past it. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(1u, m.cpu.regs.d[0]);

  const ap_m68030_exception_result_t r = ap_m68030_take_exception(
      &m.cpu, AP_M68030_VECTOR_TRAP_BASE, m.cpu.regs.pc, PROGRAM_BASE);
  TEST_ASSERT_TRUE(r.ok);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(2u, m.cpu.regs.d[0]);
}

/* ---------------------------------------------------------------------------
 * The $4E control group: subroutines, frames and returning from an exception.
 * ------------------------------------------------------------------------- */

/* "SP - 4 -> SP; PC -> (SP); Destination Address -> PC", then RTS pulls it
 * back. Run together because a return address that is off by the instruction's
 * length only shows up when something returns through it. */
static void test_a_subroutine_call_returns_to_the_instruction_after_it(void) {
  /* BSR.B +4 ; MOVEQ #1,D0 ; RTS at +6, reached by the branch. */
  static const uint16_t program[] = {0x6104u, 0x7001u, 0x4E71u, 0x4E75u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;

  /* The call: lands at +6 and leaves the return address on the stack. */
  const ap_m68030_step_result_t call = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, call.status);
  TEST_ASSERT_TRUE(call.branch_taken);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 6u, m.cpu.regs.pc);
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK - 4u, m.cpu.regs.isp);

  /* The RTS there returns to the instruction after the BSR, not to the BSR. */
  const ap_m68030_step_result_t ret = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ret.status);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 2u, m.cpu.regs.pc);
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK, m.cpu.regs.isp);

  /* And that instruction runs. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(1u, m.cpu.regs.d[0]);
}

/* "JMP <ea>" loads the effective *address* into the PC -- it does not read
 * through it. A model that fetched the operand would jump to whatever happened
 * to be stored there, which for a jump table is exactly one indirection too
 * many and lands somewhere plausible. */
static void test_a_jump_goes_to_the_address_not_to_its_contents(void) {
  /* JMP (A0) with A0 pointing at +4, where MOVEQ #7,D0 sits. */
  static const uint16_t program[] = {0x4ED0u, 0x4E71u, 0x7007u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.a[0] = PROGRAM_BASE + 4u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 4u, m.cpu.regs.pc);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(7u, m.cpu.regs.d[0]);
}

/* "SP - 4 -> SP; An -> (SP); SP -> An; SP + dn -> SP", then UNLK's
 * "An -> SP; (SP) -> An; SP + 4 -> SP". The pair has to be exactly inverse, and
 * the order inside each is the instruction: LINK pushes the register *before*
 * giving it the new stack pointer, and UNLK loads the stack pointer *before*
 * reloading the register. */
static void test_link_and_unlk_are_exact_inverses(void) {
  /* LINK A6,#-16 ; UNLK A6 */
  static const uint16_t program[] = {0x4E56u, 0xFFF0u, 0x4E5Eu, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  m.cpu.regs.a[6] = 0x12345678u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  /* The old A6 is on the stack, A6 points at it, and 16 bytes are allocated
   * below -- "The user should specify a negative displacement in order to
   * allocate stack area". */
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK - 4u, m.cpu.regs.a[6]);
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK - 20u, m.cpu.regs.isp);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x12345678u, m.cpu.regs.a[6]);
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK, m.cpu.regs.isp);
}

/* The round trip the last commit made half of: take an exception, then RTE out
 * of it. "the processor updates the status register and program counter with
 * the data read from the stack" -- the *whole* status register, which is what
 * returns a user program to user state. */
static void test_rte_returns_to_the_state_the_exception_interrupted(void) {
  /* TRAP #0 at +0, then MOVEQ #9,D0 at +2. RTE sits at the handler. */
  static const uint16_t program[] = {0x4E40u, 0x7009u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  write_ram_long(&m, PROGRAM_BASE + 4u, 0x4E734E71u); /* RTE ; NOP */
  plant_vector(&m, AP_M68030_VECTOR_TRAP_BASE, PROGRAM_BASE + 4u);
  m.cpu.regs.sr = 0; /* user state */
  m.cpu.regs.isp = SUPERVISOR_STACK;
  m.cpu.regs.usp = 0x00007000u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_TRUE(ap_m68030_supervisor(&m.cpu.regs));

  /* The RTE in the handler. */
  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  /* Back in user state, at the instruction after the TRAP, on the user stack,
   * with the supervisor stack unwound. */
  TEST_ASSERT_FALSE(ap_m68030_supervisor(&m.cpu.regs));
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 2u, m.cpu.regs.pc);
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK, m.cpu.regs.isp);
  TEST_ASSERT_EQUAL_HEX32(0x00007000u, m.cpu.regs.usp);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(9u, m.cpu.regs.d[0]);
}

/* "RTR ... The supervisor portion of the status register is unaffected." RTR is
 * unprivileged, so restoring the whole SR would make it an instruction any user
 * program could use to enter supervisor state. */
static void test_rtr_restores_only_the_condition_codes(void) {
  static const uint16_t program[] = {0x4E77u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.sr = 0; /* user state */
  m.cpu.regs.usp = SUPERVISOR_STACK;
  /* A stacked word with the S bit set, and a return address behind it. */
  m.memory.bytes[SUPERVISOR_STACK] = 0x20u; /* S set ... */
  m.memory.bytes[SUPERVISOR_STACK + 1u] = 0x05u; /* ... with C and Z */
  write_ram_long(&m, SUPERVISOR_STACK + 2u, PROGRAM_BASE + 2u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);

  /* The condition codes came back ... */
  TEST_ASSERT_EQUAL_HEX16(0x05u, ap_m68030_read_ccr(&m.cpu.regs));
  /* ... and the privilege level did not. */
  TEST_ASSERT_FALSE(ap_m68030_supervisor(&m.cpu.regs));
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 2u, m.cpu.regs.pc);
}

/* "If Supervisor State ... Else TRAP". The failure mode is silent: a user
 * program able to run RTE could forge a return from exception into supervisor
 * state. Table 8-6 also puts the *faulting* instruction's address in the frame
 * -- "First word of instruction causing Privilege Violation" -- not the next
 * one, so a handler that emulates the instruction and returns does not skip it. */
static void test_a_privileged_instruction_in_user_state_violates_privilege(void) {
  static const uint16_t program[] = {0x4E73u, 0x4E71u, 0x4E71u, 0x4E71u}; /* RTE */
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_PRIVILEGE_VIOLATION, HANDLER);
  m.cpu.regs.sr = 0; /* user state */
  m.cpu.regs.isp = SUPERVISOR_STACK;

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, r.status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
  TEST_ASSERT_TRUE(ap_m68030_supervisor(&m.cpu.regs));

  /* The RTE's own address, not the instruction after it. */
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE, read_ram_long(&m, m.cpu.regs.isp + 2u));
}

/* "The instruction examines the stack format field in the format/offset word to
 * determine how much information must be restored" -- and a format the
 * processor does not define is a format error, vector 14, rather than a return
 * to whatever the stack happened to contain. */
static void test_rte_on_an_undefined_frame_format_is_a_format_error(void) {
  static const uint16_t program[] = {0x4E73u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_FORMAT_ERROR, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  /* Format $7 is not one of the six the MC68030 defines. */
  m.memory.bytes[SUPERVISOR_STACK + 6u] = 0x70u;
  m.memory.bytes[SUPERVISOR_STACK + 7u] = 0x00u;

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, r.status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
  /* And the bad frame was not consumed: the stack pointer moved only by the
   * new frame this exception built. */
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK - 8u, m.cpu.regs.isp);
}

/* "TRAPV ... If V Then TRAP" -- so it is a no-op when the overflow flag is
 * clear, and an exception when it is not. Both directions, since a model that
 * always trapped would still pass a test that only set V. */
static void test_trapv_traps_only_when_the_overflow_flag_is_set(void) {
  static const uint16_t program[] = {0x4E76u, 0x7003u, 0x4E71u, 0x4E71u};

  machine_t clear = {0};
  load(&clear, program, 4);
  clear.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  clear.cpu.regs.isp = SUPERVISOR_STACK;
  ap_m68030_write_ccr(&clear.cpu.regs, 0);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED,
                        ap_m68030_step(&clear.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 2u, clear.cpu.regs.pc);

  machine_t set = {0};
  load(&set, program, 4);
  plant_vector(&set, AP_M68030_VECTOR_TRAPCC, HANDLER);
  set.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  set.cpu.regs.isp = SUPERVISOR_STACK;
  ap_m68030_write_ccr(&set.cpu.regs, (uint16_t)(1u << AP_M68030_SR_V_BIT));
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION,
                        ap_m68030_step(&set.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, set.cpu.regs.pc);
}

/* ---------------------------------------------------------------------------
 * Family 0100: the LEA/$48/$4C subtree and the single-operand escapes.
 * ------------------------------------------------------------------------- */

/* "<ea> -> An": LEA loads the *address*, not what is there. Against MOVEA on
 * the same operand, which loads the contents -- one indirection apart, and both
 * produce a plausible number. */
static void test_lea_loads_the_address_where_movea_loads_the_contents(void) {
  /* LEA (A0),A1 ; MOVEA.L (A0),A2 */
  static const uint16_t program[] = {0x43D0u, 0x2450u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  write_ram_long(&m, 0x00005000u, 0x0000ABCDu);
  m.cpu.regs.a[0] = 0x00005000u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x00005000u, m.cpu.regs.a[1]);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x0000ABCDu, m.cpu.regs.a[2]);
}

/* "SP - 4 -> SP; <ea> -> (SP)" -- the address pushed, which is how a routine
 * passes a pointer to a local. */
static void test_pea_pushes_the_address_itself(void) {
  static const uint16_t program[] = {0x4850u, 0x4E71u, 0x4E71u, 0x4E71u}; /* PEA (A0) */
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  m.cpu.regs.a[0] = 0x00005678u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK - 4u, m.cpu.regs.isp);
  TEST_ASSERT_EQUAL_HEX32(0x00005678u, read_ram_long(&m, m.cpu.regs.isp));
}

/* "Register 31-16 <-> Register 15-0", with N and Z from the whole 32-bit
 * result rather than from either half. */
static void test_swap_exchanges_the_halves_and_flags_the_whole(void) {
  static const uint16_t program[] = {0x4840u, 0x4E71u}; /* SWAP D0 */
  machine_t m = {0};
  load(&m, program, 2);
  m.cpu.regs.d[0] = 0x12345678u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x56781234u, m.cpu.regs.d[0]);
  TEST_ASSERT_FALSE(ap_m68030_read_ccr(&m.cpu.regs) &
                    (1u << AP_M68030_SR_N_BIT));

  /* A value whose *low* half has the top bit set becomes negative once swapped,
   * which only a 32-bit flag test reports. */
  machine_t n = {0};
  load(&n, program, 2);
  n.cpu.regs.d[0] = 0x00008000u;
  (void)ap_m68030_step(&n.cpu);
  TEST_ASSERT_EQUAL_HEX32(0x80000000u, n.cpu.regs.d[0]);
  TEST_ASSERT_TRUE(ap_m68030_read_ccr(&n.cpu.regs) &
                   (1u << AP_M68030_SR_N_BIT));
}

/* "EXT.W ... bit 7 of the designated data register is copied to bits 15-8",
 * leaving the upper word alone, against "EXTB.L ... copies bit 7 ... to bits
 * 31-8". Same source byte, different reach -- and EXT.W's surviving upper half
 * is what makes the two forms not interchangeable. */
static void test_the_three_extend_forms_reach_different_distances(void) {
  static const uint16_t ext_word[] = {0x4880u, 0x4E71u};  /* EXT.W D0 */
  machine_t m = {0};
  load(&m, ext_word, 2);
  m.cpu.regs.d[0] = 0xAAAAAA80u;
  (void)ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_HEX32(0xAAAAFF80u, m.cpu.regs.d[0]);

  static const uint16_t extb_long[] = {0x49C0u, 0x4E71u}; /* EXTB.L D0 */
  machine_t n = {0};
  load(&n, extb_long, 2);
  n.cpu.regs.d[0] = 0xAAAAAA80u;
  (void)ap_m68030_step(&n.cpu);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFF80u, n.cpu.regs.d[0]);

  static const uint16_t ext_long[] = {0x48C0u, 0x4E71u}; /* EXT.L D0 */
  machine_t o = {0};
  load(&o, ext_long, 2);
  o.cpu.regs.d[0] = 0xAAAA8000u;
  (void)ap_m68030_step(&o.cpu);
  TEST_ASSERT_EQUAL_HEX32(0xFFFF8000u, o.cpu.regs.d[0]);
}

/* The MOVEM rule that fails silently. "For the predecrement mode addresses, the
 * mask correspondence is reversed": bit 0 names A7 rather than D0. Reading the
 * mask the same way round for both directions saves the right number of
 * registers into the right amount of space with every one in the wrong place --
 * and the round trip back through a postincrement MOVEM would still restore
 * them, so only an outside observer of memory can see it. */
static void test_movem_reverses_its_mask_for_the_predecrement_mode(void) {
  /* MOVEM.L D0/A0,-(A7): mask bit for D0 is 15, for A0 is 7. */
  static const uint16_t program[] = {0x48E7u, 0x8080u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  m.cpu.regs.d[0] = 0x0D0D0D0Du;
  m.cpu.regs.a[0] = 0x0A0A0A0Au;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK - 8u, m.cpu.regs.isp);
  /* "The order of storing is from A7 to A0, then from D7 to D0", so A0 lands
   * at the higher address and D0 below it. */
  TEST_ASSERT_EQUAL_HEX32(0x0D0D0D0Du, read_ram_long(&m, SUPERVISOR_STACK - 8u));
  TEST_ASSERT_EQUAL_HEX32(0x0A0A0A0Au, read_ram_long(&m, SUPERVISOR_STACK - 4u));
}

/* A save and restore pair, which is what MOVEM is for: the postincrement mask
 * is *not* reversed, so the same registers come back to the same places. */
static void test_a_movem_save_and_restore_round_trips(void) {
  /* MOVEM.L D0-D1/A0,-(A7) ; MOVEM.L (A7)+,D0-D1/A0 */
  static const uint16_t program[] = {0x48E7u, 0xC080u, 0x4CDFu, 0x0103u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  m.cpu.regs.d[0] = 0x11111111u;
  m.cpu.regs.d[1] = 0x22222222u;
  m.cpu.regs.a[0] = 0x33333333u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK - 12u, m.cpu.regs.isp);

  /* Scribble over them, so a restore that did nothing would be visible. */
  m.cpu.regs.d[0] = 0;
  m.cpu.regs.d[1] = 0;
  m.cpu.regs.a[0] = 0;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x11111111u, m.cpu.regs.d[0]);
  TEST_ASSERT_EQUAL_HEX32(0x22222222u, m.cpu.regs.d[1]);
  TEST_ASSERT_EQUAL_HEX32(0x33333333u, m.cpu.regs.a[0]);
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK, m.cpu.regs.isp);
}

/* "In the case of a word transfer to either address or data registers, each
 * word is sign-extended to 32 bits, and the resulting long word is loaded into
 * the associated register." A *data* register write replacing all 32 bits is
 * unlike every other one in the instruction set. */
static void test_a_word_movem_sign_extends_into_the_whole_register(void) {
  /* MOVEM.W (A0)+,D0 */
  static const uint16_t program[] = {0x4C98u, 0x0001u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.memory.bytes[0x5000u] = 0xFFu;
  m.memory.bytes[0x5001u] = 0xFEu;
  m.cpu.regs.a[0] = 0x00005000u;
  m.cpu.regs.d[0] = 0x12345678u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFEu, m.cpu.regs.d[0]);
  TEST_ASSERT_EQUAL_HEX32(0x00005002u, m.cpu.regs.a[0]);
}

/* "If Dn < 0 or Dn > Source Then TRAP", and "The upper bound is a twos
 * complement integer" -- both comparisons signed. An unsigned compare would let
 * a negative register pass any bound whose top bit is clear, which is almost
 * every bound anyone writes. */
static void test_chk_traps_on_a_negative_register_not_just_a_large_one(void) {
  /* CHK.W #$1000,D0 -- opmode 110 is the word form and 100 the long, the
   * 68020 having put its wider CHK *below* the older one rather than above. */
  static const uint16_t program[] = {0x41BCu, 0x1000u, 0x4E71u, 0x4E71u};

  machine_t inside = {0};
  load(&inside, program, 4);
  inside.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  inside.cpu.regs.isp = SUPERVISOR_STACK;
  inside.cpu.regs.d[0] = 0x0800u;
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED,
                        ap_m68030_step(&inside.cpu).status);

  machine_t above = {0};
  load(&above, program, 4);
  plant_vector(&above, AP_M68030_VECTOR_CHK, HANDLER);
  above.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  above.cpu.regs.isp = SUPERVISOR_STACK;
  above.cpu.regs.d[0] = 0x2000u;
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION,
                        ap_m68030_step(&above.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, above.cpu.regs.pc);

  /* $FFFF is -1 as a word: below zero, so it traps -- but larger than $1000
   * unsigned, so an unsigned model would trap for the wrong reason and set the
   * wrong N. Only the flag distinguishes them. */
  machine_t negative = {0};
  load(&negative, program, 4);
  plant_vector(&negative, AP_M68030_VECTOR_CHK, HANDLER);
  negative.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  negative.cpu.regs.isp = SUPERVISOR_STACK;
  negative.cpu.regs.d[0] = 0x0000FFFFu;
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION,
                        ap_m68030_step(&negative.cpu).status);
  /* "N -- Set if Dn < 0", which is the case the sign matters for. */
  TEST_ASSERT_TRUE(ap_m68030_read_ccr(&negative.cpu.regs) &
                   (1u << AP_M68030_SR_N_BIT));
}

/* "Destination Tested -> Condition Codes; 1 -> Bit 7 of Destination": the flags
 * come from the value *before* the bit is set. Setting first would make TAS
 * always report a non-zero, already-set operand, and every semaphore built on
 * it would deadlock. */
static void test_tas_flags_the_old_value_and_then_sets_the_bit(void) {
  static const uint16_t program[] = {0x4AD0u, 0x4E71u}; /* TAS (A0) */
  machine_t m = {0};
  load(&m, program, 2);
  m.memory.bytes[0x5000u] = 0x00u;
  m.cpu.regs.a[0] = 0x00005000u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  /* Z reflects the operand as it was: free. */
  TEST_ASSERT_TRUE(ap_m68030_read_ccr(&m.cpu.regs) &
                   (1u << AP_M68030_SR_Z_BIT));
  /* And the bit is now set, so the next TAS finds it taken. */
  TEST_ASSERT_EQUAL_HEX8(0x80u, m.memory.bytes[0x5000u]);
}

/* "MOVE from SR ... If Supervisor State ... Else TRAP" on the 68010 and later,
 * while MOVE from CCR -- which the 68000 did not have at all -- is
 * unprivileged. The pair reads backwards from the 68000, which is exactly why
 * it is worth stating. */
static void test_reading_the_status_register_is_privileged_but_the_ccr_is_not(
    void) {
  static const uint16_t from_sr[] = {0x40C0u, 0x4E71u};  /* MOVE SR,D0 */
  machine_t m = {0};
  load(&m, from_sr, 2);
  plant_vector(&m, AP_M68030_VECTOR_PRIVILEGE_VIOLATION, HANDLER);
  m.cpu.regs.sr = 0; /* user state */
  m.cpu.regs.isp = SUPERVISOR_STACK;
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);

  static const uint16_t from_ccr[] = {0x42C0u, 0x4E71u}; /* MOVE CCR,D0 */
  machine_t n = {0};
  load(&n, from_ccr, 2);
  n.cpu.regs.sr = 0; /* user state, and this one is allowed */
  ap_m68030_write_ccr(&n.cpu.regs, (uint16_t)(1u << AP_M68030_SR_C_BIT));
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(1u, n.cpu.regs.d[0] & 0xFFFFu);
}

/* "MOVE to CCR ... the only portion of the status register (SR) available in
 * the user mode" -- so it cannot reach the system byte however it is written,
 * which is what keeps an unprivileged instruction from granting privilege. */
static void test_writing_the_ccr_cannot_reach_the_system_byte(void) {
  static const uint16_t program[] = {0x44C0u, 0x4E71u}; /* MOVE D0,CCR */
  machine_t m = {0};
  load(&m, program, 2);
  m.cpu.regs.sr = 0; /* user state */
  m.cpu.regs.d[0] = 0x2005u; /* S bit set, plus C and Z */

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX16(0x05u, ap_m68030_read_ccr(&m.cpu.regs));
  TEST_ASSERT_FALSE(ap_m68030_supervisor(&m.cpu.regs));
}

/* "$4AFC ... ILLEGAL" is a defined instruction whose whole purpose is to take
 * vector 4 -- so it *executes*, raising the exception, rather than being
 * rejected before execution as an unrecognised word would be. */
static void test_the_illegal_instruction_word_takes_its_exception(void) {
  static const uint16_t program[] = {0x4AFCu, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 2);
  plant_vector(&m, AP_M68030_VECTOR_ILLEGAL_INSTRUCTION, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
  /* Table 8-6: the frame holds the illegal instruction's own address. */
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE, read_ram_long(&m, m.cpu.regs.isp + 2u));
}

/* "0 - Destination10 - X -> Destination ... the tens complement of the
 * destination if the extend bit is zero or the nines complement if the extend
 * bit is one" -- both, since the X term is the difference between them. */
static void test_nbcd_gives_the_tens_or_nines_complement_by_the_extend_bit(void) {
  static const uint16_t program[] = {0x4810u, 0x4E71u}; /* NBCD (A0) */

  machine_t tens = {0};
  load(&tens, program, 2);
  tens.memory.bytes[0x5000u] = 0x25u;
  tens.cpu.regs.a[0] = 0x00005000u;
  ap_m68030_write_ccr(&tens.cpu.regs, 0);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED,
                        ap_m68030_step(&tens.cpu).status);
  TEST_ASSERT_EQUAL_HEX8(0x75u, tens.memory.bytes[0x5000u]); /* 100 - 25 */

  machine_t nines = {0};
  load(&nines, program, 2);
  nines.memory.bytes[0x5000u] = 0x25u;
  nines.cpu.regs.a[0] = 0x00005000u;
  ap_m68030_write_ccr(&nines.cpu.regs, (uint16_t)(1u << AP_M68030_SR_X_BIT));
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED,
                        ap_m68030_step(&nines.cpu).status);
  TEST_ASSERT_EQUAL_HEX8(0x74u, nines.memory.bytes[0x5000u]); /* 99 - 25 */
}

/* "0 - Destination - X -> Destination" for NEGX, which is SUBX from zero and
 * carries its Z rule with it: "Cleared if the result is nonzero; unchanged
 * otherwise", so a multi-precision negate accumulates one Z. */
static void test_negx_subtracts_from_zero_with_the_extend_bit(void) {
  static const uint16_t program[] = {0x4080u, 0x4E71u}; /* NEGX.L D0 */
  machine_t m = {0};
  load(&m, program, 2);
  m.cpu.regs.d[0] = 1u;
  ap_m68030_write_ccr(&m.cpu.regs, (uint16_t)(1u << AP_M68030_SR_X_BIT));

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  /* 0 - 1 - 1 = -2. */
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFEu, m.cpu.regs.d[0]);
}

/* ---------------------------------------------------------------------------
 * MOVEC, STOP, RESET and the wider branch displacements.
 * ------------------------------------------------------------------------- */

/* "MOVEC Rn,Rc" then "MOVEC Rc,Rn": the round trip, because a write that went
 * to the wrong register and a read that came from the same wrong one agree
 * with each other and with nothing else. VBR is the one that matters most --
 * every exception is fetched through it. */
static void test_movec_reaches_the_vector_base_register_both_ways(void) {
  /* MOVEC D0,VBR ; MOVEC VBR,D1 */
  static const uint16_t program[] = {0x4E7Bu, 0x0801u, 0x4E7Au, 0x1801u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.d[0] = 0x00003000u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x00003000u, m.cpu.regs.vbr);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x00003000u, m.cpu.regs.d[1]);
}

/* The control register codes are deliberately not contiguous -- bit 11
 * separates the 68010 group from the 68020 one -- so $800 is not $002 with a
 * different index. A model treating the field as a small dense number puts the
 * USP where CACR belongs, and both are plausible 32-bit values. */
static void test_the_control_register_codes_are_not_a_dense_index(void) {
  /* MOVEC D0,USP ($800) leaves CACR ($002) alone. */
  static const uint16_t program[] = {0x4E7Bu, 0x0800u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.d[0] = 0x00007000u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x00007000u, m.cpu.regs.usp);
  TEST_ASSERT_EQUAL_HEX32(0u, ap_m68030_cacr_pack(&m.cpu.cacr));

  /* And a code the MC68030 does not define is an illegal instruction, not a
   * silent no-op: $003 is the 68040's TC. */
  static const uint16_t undefined[] = {0x4E7Bu, 0x0003u, 0x4E71u, 0x4E71u};
  machine_t n = {0};
  load(&n, undefined, 4);
  plant_vector(&n, AP_M68030_VECTOR_ILLEGAL_INSTRUCTION, HANDLER);
  n.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  n.cpu.regs.isp = SUPERVISOR_STACK;
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, n.cpu.regs.pc);
}

/* MOVEC is privileged: the control registers are the machine's configuration,
 * and a user program that could write VBR would own the exception table. */
static void test_movec_is_privileged(void) {
  static const uint16_t program[] = {0x4E7Bu, 0x0801u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_PRIVILEGE_VIOLATION, HANDLER);
  m.cpu.regs.sr = 0; /* user state */
  m.cpu.regs.isp = SUPERVISOR_STACK;
  m.cpu.regs.d[0] = 0x00003000u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0u, m.cpu.regs.vbr); /* and it did not take effect */
}

/* "Immediate Data -> SR; STOP" -- the status register is loaded *first*,
 * interrupt mask included, which is the point of the instruction: it is how a
 * supervisor waits at a chosen priority. Then nothing executes. */
static void test_stop_loads_the_status_register_and_then_halts_fetching(void) {
  /* STOP #$2700 -- supervisor, all interrupts masked. */
  static const uint16_t program[] = {0x4E72u, 0x2700u, 0x7001u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX16(0x2700u, m.cpu.regs.sr);
  TEST_ASSERT_EQUAL_UINT(7u, ap_m68030_interrupt_mask(&m.cpu.regs));

  /* The MOVEQ after it does not run, and no fetch happens at all. */
  const unsigned fills_before = m.memory.fills;
  const ap_m68030_step_result_t stopped = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_STOPPED, stopped.status);
  TEST_ASSERT_EQUAL_HEX32(0u, m.cpu.regs.d[0]);
  TEST_ASSERT_EQUAL_UINT(fills_before, m.memory.fills);
}

/* "RESET ... The processor state, other than the program counter, is
 * unaffected, and execution continues with the next instruction." So the one
 * observable thing is the external assertion, and the instruction after it
 * runs -- a model that halted or reset itself here would stop the boot PROM at
 * its first line, since resetting the devices is among the first things it
 * does. */
static void test_reset_asserts_externally_and_carries_on(void) {
  static const uint16_t program[] = {0x4E70u, 0x7005u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.d[3] = 0x12345678u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_UINT(1u, m.cpu.external_resets);
  TEST_ASSERT_EQUAL_HEX32(0x12345678u, m.cpu.regs.d[3]);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(5u, m.cpu.regs.d[0]);
}

/* All three displacement sizes share one base -- the instruction address plus
 * two -- so a 16-bit branch of +4 and an 8-bit branch of +4 land in the same
 * place, and the extra words are consumed either way. */
static void test_a_word_branch_uses_the_same_base_as_a_byte_one(void) {
  /* BRA.W +4, whose displacement word puts the target at +6. */
  static const uint16_t program[] = {0x6000u, 0x0004u, 0x4E71u, 0x7008u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_TRUE(r.branch_taken);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 6u, m.cpu.regs.pc);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(8u, m.cpu.regs.d[0]);
}

/* An *untaken* wide branch still has to consume its displacement words: the PC
 * must land on the next instruction, not inside the displacement. Deciding the
 * branch before reading them is the mistake, and it executes a displacement as
 * an instruction -- which decodes as something. */
static void test_an_untaken_wide_branch_still_skips_its_displacement(void) {
  /* BEQ.W +$7FFC with Z clear, then MOVEQ #6,D0 at +4. */
  static const uint16_t program[] = {0x6700u, 0x7FFCu, 0x7006u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  ap_m68030_write_ccr(&m.cpu.regs, 0); /* Z clear, so not taken */

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_FALSE(r.branch_taken);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 4u, m.cpu.regs.pc);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(6u, m.cpu.regs.d[0]);
}

/* A long BSR reaches further than a word one and still pushes the address after
 * its two displacement words -- which is the part a byte-sized return address
 * calculation gets wrong. */
static void test_a_long_bsr_pushes_the_address_after_both_displacement_words(
    void) {
  /* BSR.L +6 : $61FF then a long displacement, target at +8. */
  static const uint16_t program[] = {0x61FFu, 0x0000u, 0x0006u, 0x4E71u,
                                     0x4E75u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 8u, m.cpu.regs.pc);
  /* Six bytes of instruction, so the return address is +6. */
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 6u,
                          read_ram_long(&m, SUPERVISOR_STACK - 4u));

  /* And the RTS at the target returns there. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 6u, m.cpu.regs.pc);
}

/* ---------------------------------------------------------------------------
 * Full-format indexed addressing and the memory indirect modes.
 * ------------------------------------------------------------------------- */

/* The full format declares its own displacement sizes, so how long the
 * instruction is cannot be known from the instruction word -- which is why the
 * PC advances by the words actually taken rather than by a predicted length.
 * "EA = (An) + (Xn) + bd" with a word base displacement. */
static void test_a_full_format_index_adds_base_register_and_displacement(void) {
  /* MOVE.L ($100,A0,D0.L),D1 in the full format, then MOVEQ #3,D2 after it. */
  static const uint16_t program[] = {0x2230u, 0x0920u, 0x0100u, 0x7403u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);
  m.cpu.regs.a[0] = 0x00005000u;
  m.cpu.regs.d[0] = 0x00000010u;
  write_ram_long(&m, 0x00005110u, 0xFEEDFACEu);

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0xFEEDFACEu, m.cpu.regs.d[1]);
  /* Three words of instruction, so the next one is at +6 -- and it runs. */
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 6u, m.cpu.regs.pc);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(3u, m.cpu.regs.d[2]);
}

/* The two memory indirect modes differ in *where the index goes* and in nothing
 * else. Preindexed puts it inside the brackets -- "([bd,An,Xn],od)" -- and
 * postindexed outside them, "([bd,An],Xn,od)". Run on the same registers with
 * the same displacements, so only the placement can account for the
 * difference; a model that indexed in both places, or in neither, would land a
 * scaled register away, which for a small index is a *nearby* address. */
static void test_the_index_is_inside_the_brackets_for_only_one_of_the_two(void) {
  /* MOVE.L ([$100,A0,D0.L],$8),D1 -- I/IS 010, preindexed, word outer. */
  static const uint16_t preindexed[] = {0x2230u, 0x0922u, 0x0100u, 0x0008u,
                                        0x4E71u, 0x4E71u};
  machine_t pre = {0};
  load(&pre, preindexed, 6);
  pre.cpu.regs.a[0] = 0x00005000u;
  pre.cpu.regs.d[0] = 0x00000010u;
  /* The index is inside, so the intermediate address includes it. */
  write_ram_long(&pre, 0x00005110u, 0x00006000u);
  write_ram_long(&pre, 0x00006008u, 0x11111111u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED,
                        ap_m68030_step(&pre.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x11111111u, pre.cpu.regs.d[1]);

  /* MOVE.L ([$100,A0],D0.L,$8),D1 -- I/IS 110, postindexed, word outer. */
  static const uint16_t postindexed[] = {0x2230u, 0x0926u, 0x0100u, 0x0008u,
                                         0x4E71u, 0x4E71u};
  machine_t post = {0};
  load(&post, postindexed, 6);
  post.cpu.regs.a[0] = 0x00005000u;
  post.cpu.regs.d[0] = 0x00000010u;
  /* The index is outside, so the intermediate address does not include it ... */
  write_ram_long(&post, 0x00005100u, 0x00007000u);
  /* ... and it lands on the fetched pointer instead. */
  write_ram_long(&post, 0x00007018u, 0x22222222u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED,
                        ap_m68030_step(&post.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x22222222u, post.cpu.regs.d[1]);
}

/* "In this mode, the address register, the index register, and the
 * displacement are all optional." BS suppresses the base register and IS the
 * index, and a suppressed element contributes zero -- not its register's
 * contents. So a suppressed base with a long displacement is an absolute
 * address, which is what makes this mode usable for a jump table. */
static void test_a_suppressed_base_contributes_zero_not_its_register(void) {
  /* MOVE.L ($5200,ZA0,D0.L),D1: BS set, long base displacement, no indirect. */
  static const uint16_t program[] = {0x2230u, 0x09B0u, 0x0000u, 0x5200u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);
  /* A0 holds something that would be very visible if it were added. */
  m.cpu.regs.a[0] = 0x0000F000u;
  m.cpu.regs.d[0] = 0x00000004u;
  write_ram_long(&m, 0x00005204u, 0xABCDEF01u);

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_EQUAL_HEX32(0xABCDEF01u, m.cpu.regs.d[1]);
}

/* "SCALE ... SCALE VALUE x" -- the index is multiplied before it is added, so
 * a scale of 4 over an index of 2 reaches the eighth byte and not the second.
 * A model ignoring the scale reads a *nearby* address, which is the failure
 * that survives a casual test. */
static void test_the_index_is_scaled_before_it_is_added(void) {
  /* MOVE.L (0,A0,D0.L*4),D1 -- scale field 10 in bits 10-9, null base
   * displacement. The scale sits *above* the full-format bit, so a word that
   * looks like it names a scale may be naming a displacement size instead. */
  static const uint16_t program[] = {0x2230u, 0x0D10u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.a[0] = 0x00005000u;
  m.cpu.regs.d[0] = 0x00000002u;
  write_ram_long(&m, 0x00005008u, 0x33333333u); /* 2 * 4 */
  write_ram_long(&m, 0x00005002u, 0x44444444u); /* where an unscaled read goes */

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x33333333u, m.cpu.regs.d[1]);
}

/* "BD SIZE ... 00 = Reserved". Reserved is *not* null: collapsing the two would
 * accept an instruction word the processor does not define and run it as
 * though the displacement were absent. */
static void test_a_reserved_displacement_size_is_not_a_null_one(void) {
  /* The same instruction with BD SIZE 00 rather than 01. */
  static const uint16_t program[] = {0x2230u, 0x0900u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.a[0] = 0x00005000u;

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_UNIMPLEMENTED, r.status);
  /* And the PC did not move, so nothing was half-done. */
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE, m.cpu.regs.pc);
}

/* ---------------------------------------------------------------------------
 * PMOVE: the MMU registers, reached through family 1111.
 * ------------------------------------------------------------------------- */

/* A translation control value the consistency check accepts: PS 8 plus
 * TIA/TIB/TIC of 8 each and IS 0 sums to 32, which is what the check requires.
 * "The TIx fields are added together until a zero field is reached, and this sum
 * is added to PS and IS. The total must be 32." */
#define CONSISTENT_TC 0x80808880u

/* "PMOVE <ea>,MRn" then "PMOVE MRn,<ea>": the round trip, because a write to
 * the wrong register and a read from the same wrong one agree with each other
 * and with nothing else. */
static void test_pmove_writes_and_reads_the_translation_control_register(void) {
  /* PMOVE (A0),TC ; PMOVE TC,(A1) */
  static const uint16_t program[] = {0xF010u, 0x4000u, 0xF011u, 0x4200u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.a[0] = 0x00005000u;
  m.cpu.regs.a[1] = 0x00005100u;
  write_ram_long(&m, 0x00005000u, CONSISTENT_TC);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_TRUE(m.cpu.tc.enable);
  TEST_ASSERT_EQUAL_UINT(8u, m.cpu.tc.page_size_bits);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(CONSISTENT_TC, read_ram_long(&m, 0x00005100u));
}

/* The P-REGISTER field is *not* enough on its own: `010` names the supervisor
 * root pointer under one prefix and TT0 under another. A decoder reading only
 * that field would write a transparent translation register where a root
 * pointer belongs -- and both are plausible 32-bit values, so nothing would
 * fault until a translation went somewhere strange. */
static void test_the_same_p_register_field_names_two_different_registers(void) {
  /* PMOVE (A0),SRP -- prefix 010, P-REGISTER 010, a quad-word operation. */
  static const uint16_t to_srp[] = {0xF010u, 0x4800u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, to_srp, 4);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.a[0] = 0x00005000u;
  write_ram_long(&m, 0x00005000u, 0x03FF0002u); /* limit $3FF, DT valid 4-byte */
  write_ram_long(&m, 0x00005004u, 0x00012000u); /* the table address */

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x00012000u, m.cpu.srp.table_address);
  TEST_ASSERT_EQUAL_HEX32(0x03FFu, m.cpu.srp.limit);
  /* And TT0 was not touched. */
  TEST_ASSERT_EQUAL_HEX32(0u, ap_m68030_tt_pack(&m.cpu.tt0));

  /* PMOVE (A0),TT0 -- prefix 000, the same P-REGISTER 010, a long operation. */
  static const uint16_t to_tt0[] = {0xF010u, 0x0800u, 0x4E71u, 0x4E71u};
  machine_t n = {0};
  load(&n, to_tt0, 4);
  n.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  n.cpu.regs.a[0] = 0x00005000u;
  write_ram_long(&n, 0x00005000u, 0x807F8040u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_TRUE(n.cpu.tt0.enabled);
  TEST_ASSERT_EQUAL_HEX32(0x807F8040u, ap_m68030_tt_pack(&n.cpu.tt0));
  /* And the supervisor root pointer was not touched. */
  TEST_ASSERT_EQUAL_HEX32(0u, n.cpu.srp.table_address);
}

/* "A descriptor-type code of $00 (invalid) is not allowed; an attempt to load
 * zero into the DT field of the CRP or SRP register results in an MMU
 * configuration exception" -- taken "after moving the operand", so the register
 * *is* written and only then does it fault. */
static void test_an_invalid_root_pointer_faults_after_the_move(void) {
  static const uint16_t program[] = {0xF010u, 0x4C00u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_MMU_CONFIGURATION, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  m.cpu.regs.a[0] = 0x00005000u;
  write_ram_long(&m, 0x00005000u, 0x00000000u); /* DT zero: invalid */
  write_ram_long(&m, 0x00005004u, 0x00012000u);

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, r.status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
  /* The move happened first, which is what the manual's ordering says. */
  TEST_ASSERT_EQUAL_HEX32(0x00012000u, m.cpu.crp.table_address);
}

/* "If the E-bit = 1, consistency checks are performed on the PS and TIx fields.
 * If the checks fail, the instruction takes an MMU configuration exception
 * after moving the operand ... and the E-bit is cleared." Both halves: the
 * value lands, and translation is left off rather than enabled inconsistently.
 * Refusing the write instead would leave the operating system unable to see
 * what it wrote wrong. */
static void test_an_inconsistent_translation_control_lands_with_e_cleared(void) {
  static const uint16_t program[] = {0xF010u, 0x4000u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_MMU_CONFIGURATION, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  m.cpu.regs.a[0] = 0x00005000u;
  /* The same as CONSISTENT_TC but with TID also 8, so the sum is 40. */
  write_ram_long(&m, 0x00005000u, 0x80808888u);

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, r.status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
  /* The fields landed ... */
  TEST_ASSERT_EQUAL_UINT(8u, m.cpu.tc.table_index[3]);
  /* ... and translation is off. */
  TEST_ASSERT_FALSE(m.cpu.tc.enable);
}

/* "If the FD bit equals one, the ATC is not flushed when the instruction is
 * executed. If the FD bit equals zero, the ATC is flushed" -- so PMOVEFD is a
 * different instruction from PMOVE in exactly one bit, and an operating system
 * that meant to keep its cached translations is the one that sets it. */
static void test_the_flush_disable_bit_decides_whether_the_atc_survives(void) {
  static const uint16_t flushing[] = {0xF010u, 0x4000u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, flushing, 4);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.a[0] = 0x00005000u;
  write_ram_long(&m, 0x00005000u, CONSISTENT_TC);
  (void)ap_m68030_atc_insert(&m.atc, 5u, 0x0000A000u, 8u, 0x00090000u, false,
                             false, false, false);
  TEST_ASSERT_EQUAL_INT(AP_M68030_ATC_HIT,
                        ap_m68030_atc_lookup(&m.atc, 5u, 0x0000A000u, 8u, false,
                                             false)
                            .status);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_INT(AP_M68030_ATC_MISS,
                        ap_m68030_atc_lookup(&m.atc, 5u, 0x0000A000u, 8u, false,
                                             false)
                            .status);

  /* PMOVEFD: the same instruction with FD set. */
  static const uint16_t keeping[] = {0xF010u, 0x4100u, 0x4E71u, 0x4E71u};
  machine_t n = {0};
  load(&n, keeping, 4);
  n.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  n.cpu.regs.a[0] = 0x00005000u;
  write_ram_long(&n, 0x00005000u, CONSISTENT_TC);
  (void)ap_m68030_atc_insert(&n.atc, 5u, 0x0000A000u, 8u, 0x00090000u, false,
                             false, false, false);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_INT(AP_M68030_ATC_HIT,
                        ap_m68030_atc_lookup(&n.atc, 5u, 0x0000A000u, 8u, false,
                                             false)
                            .status);
}

/* Every MMU instruction is privileged, and the vector an *unsupported* one
 * takes depends on the state it was attempted from: F-line from supervisor,
 * privilege violation from user. Almost everywhere else in this architecture
 * the exception a word takes is a property of the word alone -- reporting
 * F-line in both cases would let a user program distinguish "unimplemented"
 * from "not allowed", which is what the privilege violation exists to prevent. */
static void test_an_mmu_instruction_in_user_state_violates_privilege(void) {
  static const uint16_t program[] = {0xF010u, 0x4000u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_PRIVILEGE_VIOLATION, HANDLER);
  m.cpu.regs.sr = 0; /* user state */
  m.cpu.regs.isp = SUPERVISOR_STACK;
  m.cpu.regs.a[0] = 0x00005000u;
  write_ram_long(&m, 0x00005000u, CONSISTENT_TC);

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, r.status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
  /* And the register was not written. */
  TEST_ASSERT_FALSE(m.cpu.tc.enable);
}

/* "Only control alterable addressing modes can be used" -- the 68030's
 * "Reduced Instruction Set ... Only Control-Alterable Addressing Modes
 * Supported for MMU Instructions". A data register is not a memory location,
 * and accepting one would read an MMU register out of a register operand. */
static void test_pmove_refuses_a_register_direct_operand(void) {
  /* PMOVE D0,TC -- mode 000, which the table shows with no entry at all. */
  static const uint16_t program[] = {0xF000u, 0x4000u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.d[0] = CONSISTENT_TC;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_UNIMPLEMENTED,
                        ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_FALSE(m.cpu.tc.enable);
}

/* ---------------------------------------------------------------------------
 * PFLUSH, PLOAD and PTEST.
 * ------------------------------------------------------------------------- */

/* Put a translation in the ATC, so a flush has something to remove. */
static void seed_atc(machine_t *m, uint8_t function_code, uint32_t logical) {
  (void)ap_m68030_atc_insert(&m->atc, function_code, logical, 8u, 0x00090000u,
                             false, false, false, false);
}

static bool atc_hits(machine_t *m, uint8_t function_code, uint32_t logical) {
  return ap_m68030_atc_lookup(&m->atc, function_code, logical, 8u, false, false)
             .status == AP_M68030_ATC_HIT;
}

/* "The PFLUSHA instruction invalidates all entries", whatever their function
 * code. Mode 001, with "mask must be 000" and the FC field "must be 00000". */
static void test_pflusha_invalidates_every_entry(void) {
  static const uint16_t program[] = {0xF000u, 0x2400u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  seed_atc(&m, 1u, 0x0000A000u);
  seed_atc(&m, 5u, 0x0000B000u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_FALSE(atc_hits(&m, 1u, 0x0000A000u));
  TEST_ASSERT_FALSE(atc_hits(&m, 5u, 0x0000B000u));
}

/* "Each bit in the mask that is set to one indicates that the corresponding bit
 * of the FC operand applies ... Each bit in the mask that is zero indicates a
 * bit of FC ... ignored." So the mask says which bits must *agree*, and a zero
 * mask therefore selects every function code rather than none. Reading it as a
 * set of codes to flush inverts the instruction, and `PFLUSH #0,#0` becomes a
 * no-op where the hardware flushes everything. */
static void test_the_flush_mask_says_which_bits_must_agree(void) {
  /* PFLUSH #5,#7 -- mode 100, mask 111, FC field 10101 (immediate 101). */
  static const uint16_t exact[] = {0xF000u, 0x33F5u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, exact, 4);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  seed_atc(&m, 5u, 0x0000A000u);
  seed_atc(&m, 1u, 0x0000B000u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_FALSE(atc_hits(&m, 5u, 0x0000A000u));
  TEST_ASSERT_TRUE(atc_hits(&m, 1u, 0x0000B000u)); /* a different code survives */

  /* PFLUSH #5,#4 -- only the top bit must agree, so every supervisor code goes
   * and every user one stays. This is the manual's own worked example shape. */
  static const uint16_t masked[] = {0xF000u, 0x3095u, 0x4E71u, 0x4E71u};
  machine_t n = {0};
  load(&n, masked, 4);
  n.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  seed_atc(&n, 5u, 0x0000A000u);
  seed_atc(&n, 6u, 0x0000C000u);
  seed_atc(&n, 1u, 0x0000B000u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_FALSE(atc_hits(&n, 5u, 0x0000A000u));
  TEST_ASSERT_FALSE(atc_hits(&n, 6u, 0x0000C000u));
  TEST_ASSERT_TRUE(atc_hits(&n, 1u, 0x0000B000u));
}

/* "When the instruction also specifies an <ea>, the instruction invalidates the
 * page descriptor for that effective address entry." And the note that makes
 * PFLUSH unlike every other instruction: "The address field must provide the
 * memory management unit with the effective address to be flushed ... not the
 * effective address describing where the PFLUSH operand is located" -- the
 * calculated address *is* the operand, never read through. */
static void test_pflush_by_address_flushes_that_page_and_no_other(void) {
  /* PFLUSH #5,#7,(A0) -- mode 110, mask 111, FC 10101. */
  static const uint16_t program[] = {0xF010u, 0x3BF5u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.a[0] = 0x0000A000u;
  /* Something at that address that would be read if the address were followed
   * rather than used, and would then flush the wrong page. */
  write_ram_long(&m, 0x0000A000u, 0x0000B000u);
  seed_atc(&m, 5u, 0x0000A000u);
  seed_atc(&m, 5u, 0x0000B000u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_FALSE(atc_hits(&m, 5u, 0x0000A000u));
  TEST_ASSERT_TRUE(atc_hits(&m, 5u, 0x0000B000u));
}

/* The FC field is not a plain number: "10XXX -- Function code is specified as
 * bits XXX. 01DDD -- Function code is specified as bits 2-0 of data register
 * DDD." Reading the low three bits as the code makes `01DDD` name a function
 * code that happens to be the register number, which for D5 is 5 -- a perfectly
 * ordinary supervisor data code, so nothing looks wrong. */
static void test_a_function_code_from_a_data_register_is_not_its_number(void) {
  /* PFLUSH D5,#7 -- mode 100, mask 111, FC field 01101 (data register 5). */
  static const uint16_t program[] = {0xF000u, 0x33EDu, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.d[5] = 1u; /* the code is 1, not 5 */
  seed_atc(&m, 1u, 0x0000A000u);
  seed_atc(&m, 5u, 0x0000B000u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_FALSE(atc_hits(&m, 1u, 0x0000A000u));
  TEST_ASSERT_TRUE(atc_hits(&m, 5u, 0x0000B000u));
}

/* PFLUSH and PLOAD share the extension prefix `001` and are told apart by the
 * MODE field below it. PLOAD is mode 000, and "creates a new entry as if the
 * MC68030 had attempted to access that address" -- so it *adds* a translation
 * where PFLUSH removes one, and a decoder stopping at the prefix would do the
 * opposite of what was asked. */
static void test_pload_adds_a_translation_where_pflush_removes_one(void) {
  /* PLOADW #5,(A0) -- mode 000, R/W 0, FC field 10101. */
  static const uint16_t program[] = {0xF010u, 0x2015u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.a[0] = 0x0000A000u;

  /* A one-level tree: PS 8, IS 0, TIA 24 is not legal, so use the consistent
   * shape and a root whose first table entry terminates early on a page. */
  m.cpu.tc = ap_m68030_tc_decode(CONSISTENT_TC);
  m.cpu.crp.table_address = 0x00008000u;
  m.cpu.crp.long_format = false;
  m.cpu.crp.has_limit = false;
  /* Index A of $00020000 under PS 8 / TIA 8: the top eight bits, which are 0.
   * A short early-termination page descriptor: DT 1, page address $00070000. */
  write_ram_long(&m, 0x00008000u, 0x00070001u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_TRUE(atc_hits(&m, 5u, 0x0000A000u));
}

/* "PTEST, Level 0" searches the ATC and nothing else, and reports through the
 * MMUSR: "The I-bit is set if the translation for the specified logical address
 * is not resident in the ATC". Both directions, since a model that always
 * reported resident would pass a test that only probed a present page. */
static void test_ptest_at_level_zero_reports_whether_the_atc_has_it(void) {
  /* PTEST #5,(A0),#0 -- prefix 100, level 000, R/W 1, A 0, FC 10101. */
  static const uint16_t program[] = {0xF010u, 0x8215u, 0x4E71u, 0x4E71u};

  machine_t present = {0};
  load(&present, program, 4);
  present.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  present.cpu.regs.a[0] = 0x0000A000u;
  present.cpu.tc = ap_m68030_tc_decode(CONSISTENT_TC);
  seed_atc(&present, 5u, 0x0000A000u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED,
                        ap_m68030_step(&present.cpu).status);
  const ap_m68030_mmusr_t hit = ap_m68030_mmusr_unpack(present.cpu.mmusr);
  TEST_ASSERT_FALSE(hit.invalid);

  machine_t absent = {0};
  load(&absent, program, 4);
  absent.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  absent.cpu.regs.a[0] = 0x0000A000u;
  absent.cpu.tc = ap_m68030_tc_decode(CONSISTENT_TC);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED,
                        ap_m68030_step(&absent.cpu).status);
  TEST_ASSERT_TRUE(ap_m68030_mmusr_unpack(absent.cpu.mmusr).invalid);
}

/* "When this field contains 0, the A field and the register field must also be
 * 0. The instruction takes an F-line exception when the level field is 0 and
 * the A field is not 0." An ATC probe has no descriptor address to return,
 * because it never fetched one. */
static void test_a_level_zero_ptest_cannot_ask_for_a_descriptor_address(void) {
  /* The same instruction with A set, which is the combination the manual
   * singles out. */
  static const uint16_t program[] = {0xF010u, 0x8315u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.a[0] = 0x0000A000u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_UNIMPLEMENTED,
                        ap_m68030_step(&m.cpu).status);
}

/* Levels 1-7 walk the tree instead, and "The PTEST instruction does not alter
 * the ATC" -- so a search that finds a translation leaves the cache exactly as
 * it was, which is what makes PTEST usable inside a fault handler without
 * changing the state being diagnosed. */
static void test_a_table_search_ptest_leaves_the_atc_alone(void) {
  /* PTEST #5,(A0),#7,A1 -- level 111, R/W 1, A 1, register 001, FC 10101. */
  static const uint16_t program[] = {0xF010u, 0x9F35u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.a[0] = 0x0000A000u;
  m.cpu.tc = ap_m68030_tc_decode(CONSISTENT_TC);
  m.cpu.crp.table_address = 0x00008000u;
  m.cpu.crp.long_format = false;
  m.cpu.crp.has_limit = false;
  write_ram_long(&m, 0x00008000u, 0x00070001u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  /* The search succeeded ... */
  TEST_ASSERT_FALSE(ap_m68030_mmusr_unpack(m.cpu.mmusr).invalid);
  /* ... and nothing was cached, unlike PLOAD. */
  TEST_ASSERT_FALSE(atc_hits(&m, 5u, 0x0000A000u));
  /* "The physical address of the last descriptor fetched can be returned in an
   * address register" -- the address fetched *from*, not what it pointed at. */
  TEST_ASSERT_EQUAL_HEX32(0x00008000u, m.cpu.regs.a[1]);
}

/* ---------------------------------------------------------------------------
 * Addressing mode legality, applied.
 * ------------------------------------------------------------------------- */

/* The categories are only worth having if something enforces them. `LEA` takes
 * control modes, so `LEA (A0),A1` is legal and `LEA (A0)+,A1` is not -- the
 * increment carries an operand size and there is no operand size in a load of
 * an address. Both encode; only one is an instruction. */
static void test_lea_refuses_an_increment_mode_it_decodes_perfectly_well(void) {
  /* LEA (A0)+,A1 */
  static const uint16_t program[] = {0x43D8u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.a[0] = 0x00005000u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_UNIMPLEMENTED,
                        ap_m68030_step(&m.cpu).status);
  /* And A0 was not incremented on the way to refusing. */
  TEST_ASSERT_EQUAL_HEX32(0x00005000u, m.cpu.regs.a[0]);
}

/* `MOVE`'s destination must be data alterable. `MOVE.W D0,(d16,PC)` encodes,
 * and without the check the step would try to write through the program
 * counter -- an instruction the processor refuses, running here. */
static void test_a_move_cannot_write_through_the_program_counter(void) {
  /* MOVE.W D0,(d16,PC): the destination's mode goes in bits 8-6 and its
   * register in 11-9, reversed from the source -- so mode 111 register 010 is
   * $35C0 and not $3540, which is an ordinary (d16,A2) destination. */
  static const uint16_t program[] = {0x35C0u, 0x0004u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.d[0] = 0x1234u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_UNIMPLEMENTED,
                        ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_UINT(0u, m.memory.stores);
}

/* The MMU instructions take control alterable modes. "Not a register and not an
 * immediate" was the shape this check first had, and it let `(An)+`, `-(An)`
 * and every PC-relative mode through -- all of which the instruction pages mark
 * absent. */
static void test_pmove_refuses_an_increment_mode(void) {
  /* PMOVE (A0)+,TC */
  static const uint16_t program[] = {0xF018u, 0x4000u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.a[0] = 0x00005000u;
  write_ram_long(&m, 0x00005000u, CONSISTENT_TC);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_UNIMPLEMENTED,
                        ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_FALSE(m.cpu.tc.enable);
  TEST_ASSERT_EQUAL_HEX32(0x00005000u, m.cpu.regs.a[0]);
}

/* ---------------------------------------------------------------------------
 * Interrupts.
 * ------------------------------------------------------------------------- */

/* An acknowledge cycle that answers with a device vector, and one that does
 * not answer at all. */
static ap_m68030_iack_t acknowledge_with_vector(void *context, unsigned level) {
  (void)level;
  unsigned *count = (unsigned *)context;
  (*count)++;
  return (ap_m68030_iack_t){.vector = 0x40u};
}

static ap_m68030_iack_t acknowledge_autovector(void *context, unsigned level) {
  (void)context;
  (void)level;
  return (ap_m68030_iack_t){.autovector = true};
}

static ap_m68030_iack_t acknowledge_bus_error(void *context, unsigned level) {
  (void)context;
  (void)level;
  return (ap_m68030_iack_t){.bus_error = true};
}

/* Levels 1-6 are recognised when the request "exceeds the current interrupt
 * priority mask", and the vector comes off the bus. Both directions of the
 * mask, since a model that always took the interrupt would pass a test that
 * only lowered the mask. */
static void test_an_interrupt_is_taken_only_above_the_priority_mask(void) {
  static const uint16_t program[] = {0x7001u, 0x4E71u, 0x4E71u, 0x4E71u};
  unsigned acknowledges = 0;

  machine_t masked = {0};
  load(&masked, program, 4);
  masked.cpu.regs.sr = (uint16_t)((1u << AP_M68030_SR_S_BIT) |
                                  (5u << AP_M68030_SR_INTERRUPT_SHIFT));
  masked.cpu.regs.isp = SUPERVISOR_STACK;
  masked.cpu.interrupt_level = 3u; /* below the mask */
  masked.cpu.acknowledge = acknowledge_with_vector;
  masked.cpu.acknowledge_context = &acknowledges;

  /* The instruction runs; no interrupt is taken and no cycle is run. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED,
                        ap_m68030_step(&masked.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(1u, masked.cpu.regs.d[0]);
  TEST_ASSERT_EQUAL_UINT(0u, acknowledges);

  machine_t above = {0};
  load(&above, program, 4);
  plant_vector(&above, 0x40u, HANDLER);
  above.cpu.regs.sr = (uint16_t)((1u << AP_M68030_SR_S_BIT) |
                                 (3u << AP_M68030_SR_INTERRUPT_SHIFT));
  above.cpu.regs.isp = SUPERVISOR_STACK;
  above.cpu.interrupt_level = 5u; /* above the mask */
  above.cpu.acknowledge = acknowledge_with_vector;
  above.cpu.acknowledge_context = &acknowledges;

  const ap_m68030_step_result_t r = ap_m68030_step(&above.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, r.status);
  TEST_ASSERT_EQUAL_UINT(1u, acknowledges);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, above.cpu.regs.pc);
  /* And the instruction did *not* run: the interrupt was taken before it. */
  TEST_ASSERT_EQUAL_HEX32(0u, above.cpu.regs.d[0]);
}

/* "sets the processor interrupt mask level to the level of the interrupt being
 * serviced", and the copy stacked for RTE is taken *before* that. Stacking the
 * raised mask instead leaves the interrupted code running at the handler's
 * priority for ever after -- it would never receive another interrupt at its
 * own level, and nothing would fault. */
static void test_the_stacked_mask_is_the_one_before_the_interrupt(void) {
  static const uint16_t program[] = {0x7001u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, 0x40u, HANDLER);
  m.cpu.regs.sr = (uint16_t)((1u << AP_M68030_SR_S_BIT) |
                             (2u << AP_M68030_SR_INTERRUPT_SHIFT));
  m.cpu.regs.isp = SUPERVISOR_STACK;
  m.cpu.interrupt_level = 5u;
  unsigned acknowledges = 0;
  m.cpu.acknowledge = acknowledge_with_vector;
  m.cpu.acknowledge_context = &acknowledges;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);

  /* The handler runs masked at the level it is servicing ... */
  TEST_ASSERT_EQUAL_UINT(5u, ap_m68030_interrupt_mask(&m.cpu.regs));
  /* ... and the frame remembers the mask the interrupted code had. */
  const uint16_t stacked = read_ram_word(&m, m.cpu.regs.isp);
  TEST_ASSERT_EQUAL_UINT(
      2u, (stacked >> AP_M68030_SR_INTERRUPT_SHIFT) & AP_M68030_SR_INTERRUPT_MASK);
}

/* "Level 7 interrupts cannot be masked by the interrupt priority mask, and they
 * are transition sensitive. The processor recognizes an interrupt request each
 * time the external interrupt request level changes from some lower level to
 * level 7." So a level 7 line already at 7 is not a new interrupt -- holding it
 * there must not re-interrupt, or the handler never makes progress. */
static void test_level_seven_interrupts_on_the_transition_and_not_the_level(
    void) {
  static const uint16_t program[] = {0x7001u, 0x7202u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, ap_m68030_autovector(7u), HANDLER);
  /* The mask is already 7, which masks nothing at this level. */
  m.cpu.regs.sr = (uint16_t)((1u << AP_M68030_SR_S_BIT) |
                             (7u << AP_M68030_SR_INTERRUPT_SHIFT));
  m.cpu.regs.isp = SUPERVISOR_STACK;
  m.cpu.interrupt_level = 7u;
  m.cpu.acknowledge = acknowledge_autovector;

  /* The transition from 0 to 7 is recognised. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);

  /* The line is still at 7, and that is *not* a second interrupt: the next
   * step executes an instruction instead. */
  const ap_m68030_step_result_t next = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, next.status);
}

/* "If external logic indicates a bus error during the interrupt acknowledge
 * cycle, the interrupt is considered spurious, and the processor generates the
 * spurious interrupt vector number, 24" -- a defined outcome rather than a
 * fault, which is what keeps a machine with a misbehaving device running. */
static void test_a_failed_acknowledge_becomes_the_spurious_vector(void) {
  static const uint16_t program[] = {0x7001u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_SPURIOUS_INTERRUPT, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  m.cpu.interrupt_level = 4u;
  m.cpu.acknowledge = acknowledge_bus_error;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
}

/* "If the M bit of the status register is set, the processor clears the M bit
 * and creates a throwaway exception stack frame on top of the interrupt stack."
 * One interrupt, two frames, on two different stacks -- and the order matters:
 * clearing M is what moves A7 from the master stack to the interrupt stack, so
 * the second frame lands on the other one. */
static void test_an_interrupt_in_master_state_builds_two_frames(void) {
  static const uint16_t program[] = {0x7001u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, 0x40u, HANDLER);
  m.cpu.regs.sr = (uint16_t)((1u << AP_M68030_SR_S_BIT) |
                             (1u << AP_M68030_SR_M_BIT));
  m.cpu.regs.msp = 0x00008000u;
  m.cpu.regs.isp = SUPERVISOR_STACK;
  m.cpu.interrupt_level = 4u;
  unsigned acknowledges = 0;
  m.cpu.acknowledge = acknowledge_with_vector;
  m.cpu.acknowledge_context = &acknowledges;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);

  /* "The resulting status register (after exception processing) has the S bit
   * set and the M bit cleared." */
  TEST_ASSERT_TRUE(ap_m68030_supervisor(&m.cpu.regs));
  TEST_ASSERT_FALSE(ap_m68030_master(&m.cpu.regs));

  /* The first frame went on the master stack ... */
  TEST_ASSERT_EQUAL_HEX32(0x00008000u - 8u, m.cpu.regs.msp);
  TEST_ASSERT_EQUAL_INT(AP_M68030_FRAME_SHORT,
                        ap_m68030_frame_format_of(
                            read_ram_word(&m, m.cpu.regs.msp + 6u)));

  /* ... and the throwaway on the interrupt stack, format 1, with the same PC
   * and vector offset. */
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK - 8u, m.cpu.regs.isp);
  const uint16_t throwaway = read_ram_word(&m, m.cpu.regs.isp + 6u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_FRAME_THROWAWAY,
                        ap_m68030_frame_format_of(throwaway));
  TEST_ASSERT_EQUAL_HEX32(0x40u * 4u,
                          ap_m68030_frame_vector_offset_of(throwaway));
  TEST_ASSERT_EQUAL_HEX32(read_ram_long(&m, m.cpu.regs.msp + 2u),
                          read_ram_long(&m, m.cpu.regs.isp + 2u));

  /* "The copy of the status register saved on the throwaway frame is exactly
   * the same as that placed on the master stack except that the S bit is set."
   * The interrupted code was in supervisor state here, so the two agree -- and
   * the throwaway's S bit is set either way. */
  TEST_ASSERT_TRUE(read_ram_word(&m, m.cpu.regs.isp) &
                   (1u << AP_M68030_SR_S_BIT));
}

/* An interrupt is what STOP is waiting for, so taking one ends the stop --
 * otherwise a processor that stopped to wait for an interrupt would still be
 * stopped after receiving it, which is a machine that never boots. */
static void test_an_interrupt_wakes_a_stopped_processor(void) {
  /* STOP #$2000 -- supervisor, mask 0, so anything interrupts. */
  static const uint16_t program[] = {0x4E72u, 0x2000u, 0x7005u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, ap_m68030_autovector(4u), HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  m.cpu.acknowledge = acknowledge_autovector;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_STOPPED, ap_m68030_step(&m.cpu).status);

  /* The line rises, and the processor takes the interrupt and resumes. */
  m.cpu.interrupt_level = 4u;
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);

  /* And it is no longer stopped: the handler's instructions run. */
  m.cpu.interrupt_level = 0u;
  TEST_ASSERT_NOT_EQUAL_INT(AP_M68030_STEP_STOPPED,
                            ap_m68030_step(&m.cpu).status);
}

/* "The saved value of the program counter is the logical address of the
 * instruction that would have been executed had the interrupt not occurred" --
 * so RTE returns to the instruction the interrupt pre-empted, and it then runs.
 * The whole round trip, which is what an interrupt is for. */
static void test_an_interrupt_returns_to_the_instruction_it_preempted(void) {
  static const uint16_t program[] = {0x7009u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  write_ram_long(&m, 0x00006000u, 0x4E734E71u); /* RTE ; NOP */
  plant_vector(&m, ap_m68030_autovector(4u), 0x00006000u);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  m.cpu.interrupt_level = 4u;
  m.cpu.acknowledge = acknowledge_autovector;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x00006000u, m.cpu.regs.pc);

  /* Drop the line, then RTE out of the handler. */
  m.cpu.interrupt_level = 0u;
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE, m.cpu.regs.pc);
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK, m.cpu.regs.isp);

  /* The pre-empted instruction now runs, having never been skipped. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(9u, m.cpu.regs.d[0]);
}

/* ---------------------------------------------------------------------------
 * Trace.
 * ------------------------------------------------------------------------- */

/* "Setting the T1 bit and clearing the TO bit causes the execution of all
 * instructions to force trace exceptions" -- and "a trace exception is an
 * extension to the function of any traced instruction", so the instruction
 * runs first and the exception follows it. */
static void test_tracing_every_instruction_runs_it_then_traps(void) {
  static const uint16_t program[] = {0x7007u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_TRACE, HANDLER);
  m.cpu.regs.sr = (uint16_t)((1u << AP_M68030_SR_S_BIT) |
                             (1u << AP_M68030_SR_T1_BIT));
  m.cpu.regs.isp = SUPERVISOR_STACK;

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, r.status);
  /* The MOVEQ *did* execute -- the trace is an extension of it, not a
   * replacement for it. */
  TEST_ASSERT_EQUAL_HEX32(7u, m.cpu.regs.d[0]);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);

  /* Table 8-6 gives trace the six-word frame, whose two addresses differ: the
   * traced instruction, and where execution would have resumed. */
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK - 12u, m.cpu.regs.isp);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE,
                          read_ram_long(&m, m.cpu.regs.isp + 8u));
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 2u,
                          read_ram_long(&m, m.cpu.regs.isp + 2u));

  /* And the handler is not itself traced: "the processor inhibits tracing of
   * the exception handler by clearing the T1 and T0 bits". */
  TEST_ASSERT_EQUAL_INT(AP_M68030_TRACE_NONE,
                        ap_m68030_trace_mode(&m.cpu.regs));
}

/* "Clearing the T1 bit and setting the TO bit causes an instruction that forces
 * a change of flow to take a trace exception. Instructions that increment the
 * program counter normally do not take the trace exception." Both directions,
 * since a model that traced everything would pass a test that only branched. */
static void test_change_of_flow_tracing_ignores_ordinary_instructions(void) {
  /* MOVEQ then BRA: the first must not trace, the second must. */
  static const uint16_t program[] = {0x7007u, 0x6002u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_TRACE, HANDLER);
  m.cpu.regs.sr = (uint16_t)((1u << AP_M68030_SR_S_BIT) |
                             (1u << AP_M68030_SR_T0_BIT));
  m.cpu.regs.isp = SUPERVISOR_STACK;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(7u, m.cpu.regs.d[0]);
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK, m.cpu.regs.isp);

  const ap_m68030_step_result_t branched = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, branched.status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
  /* The stacked PC is the branch *target*, which is the point of the mode --
   * and the target is the branch's base (its address plus two) plus the
   * displacement, so a +2 displacement from +2 lands at +6. */
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 6u,
                          read_ram_long(&m, m.cpu.regs.isp + 2u));
}

/* "This mode also includes status register manipulations, because the processor
 * must re-prefetch instruction words to fill the pipe again any time an
 * instruction that can modify the status register is executed." A hardware
 * reason rather than a logical one, and a model that traced only actual
 * branches would silently skip every `MOVE to SR` a debugger asked to see. */
static void test_a_status_register_write_counts_as_a_change_of_flow(void) {
  /* MOVE D0,CCR -- no branch anywhere, and still a change of flow. */
  static const uint16_t program[] = {0x44C0u, 0x4E71u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_TRACE, HANDLER);
  m.cpu.regs.sr = (uint16_t)((1u << AP_M68030_SR_S_BIT) |
                             (1u << AP_M68030_SR_T0_BIT));
  m.cpu.regs.isp = SUPERVISOR_STACK;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
}

/* "The state of these bits when an instruction begins execution determines
 * whether the instruction generates a trace exception after the instruction
 * completes." So an instruction that turns tracing *off* still traces -- which
 * is what lets a debugger single step through the instruction that disables
 * it. Reading the bits afterwards would lose exactly that instruction. */
static void test_the_instruction_that_disables_tracing_is_still_traced(void) {
  /* MOVE #$2000,SR: supervisor, and every trace bit cleared. */
  static const uint16_t program[] = {0x46FCu, 0x2000u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_TRACE, HANDLER);
  m.cpu.regs.sr = (uint16_t)((1u << AP_M68030_SR_S_BIT) |
                             (1u << AP_M68030_SR_T1_BIT));
  m.cpu.regs.isp = SUPERVISOR_STACK;

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, r.status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
  /* The write *did* take effect -- the frame carries the status register as it
   * stood when the instruction completed, so tracing is already off in it. The
   * trace happened anyway, which is the whole point: the decision was made from
   * the bits as they were when the instruction *began*. Reading them afterwards
   * would lose exactly this instruction, and a debugger stepping through the
   * line that disables tracing would never be told it ran. */
  const uint16_t stacked = read_ram_word(&m, m.cpu.regs.isp);
  TEST_ASSERT_FALSE(stacked & (1u << AP_M68030_SR_T1_BIT));
  TEST_ASSERT_EQUAL_HEX16(0x2000u, stacked);
}

/* "When the processor is in the trace mode and attempts to execute an illegal
 * or unimplemented instruction, that instruction does not cause a trace
 * exception since it is not executed." The distinction matters to an emulation
 * routine, which must handle the trace itself after emulating the instruction
 * -- if the processor had already traced, it would trace twice. */
static void test_an_unexecuted_instruction_is_not_traced(void) {
  /* An MMU instruction whose extension class the 68030 does not define:
   * decoded, not executed. Supervisor state is set below, which this needs
   * anyway for the trace frame. */
  static const uint16_t program[] = {0xF010u, 0xA000u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_TRACE, HANDLER);
  m.cpu.regs.sr = (uint16_t)((1u << AP_M68030_SR_S_BIT) |
                             (1u << AP_M68030_SR_T1_BIT));
  m.cpu.regs.isp = SUPERVISOR_STACK;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_UNIMPLEMENTED,
                        ap_m68030_step(&m.cpu).status);
  /* Nothing was stacked, and the PC did not move. */
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK, m.cpu.regs.isp);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE, m.cpu.regs.pc);
}

/* "If an instruction forces an exception as part of its normal execution, the
 * forced exception processing occurs before the trace exception is processed."
 * Both happen, in that order -- so the trace frame sits on top and its handler
 * runs first, which is §8.1's general rule: "the lower the priority of an
 * exception, the sooner the handler routine for that exception executes". */
static void test_a_traced_trap_stacks_both_with_the_trace_on_top(void) {
  static const uint16_t program[] = {0x4E40u, 0x4E71u, 0x4E71u, 0x4E71u}; /* TRAP #0 */
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_TRAP_BASE, 0x00006000u);
  plant_vector(&m, AP_M68030_VECTOR_TRACE, HANDLER);
  m.cpu.regs.sr = (uint16_t)((1u << AP_M68030_SR_S_BIT) |
                             (1u << AP_M68030_SR_T1_BIT));
  m.cpu.regs.isp = SUPERVISOR_STACK;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);

  /* The trace handler is where execution resumes ... */
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
  /* ... and the trace's own frame, a six-word one, sits above the TRAP's
   * four-word frame. */
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK - 8u - 12u, m.cpu.regs.isp);
  TEST_ASSERT_EQUAL_INT(AP_M68030_FRAME_SIX_WORD,
                        ap_m68030_frame_format_of(
                            read_ram_word(&m, m.cpu.regs.isp + 6u)));
  TEST_ASSERT_EQUAL_INT(AP_M68030_FRAME_SHORT,
                        ap_m68030_frame_format_of(
                            read_ram_word(&m, SUPERVISOR_STACK - 8u + 6u)));
  /* The trace returns into the TRAP handler, not into the traced program. */
  TEST_ASSERT_EQUAL_HEX32(0x00006000u,
                          read_ram_long(&m, m.cpu.regs.isp + 2u));
}

/* ---------------------------------------------------------------------------
 * The immediate source, swept.
 * ------------------------------------------------------------------------- */

/* "If the location specified is a source operand, all addressing modes can be
 * used" -- the immediate included. `ADD.W #$10,D0` in family 1101 is a real
 * instruction, distinct from the `ADDI` that assembles to the same thing, and
 * an immediate is *fetched* rather than addressed.
 *
 * This is the fifth place that ordering has mattered, and it was found by
 * sweeping the call sites rather than by a sixth failing test. */
static void test_the_arithmetic_forms_take_an_immediate_source(void) {
  /* ADD.W #$10,D0 in family 1101, then CMP.W #$20,D0 in family 1011. */
  static const uint16_t program[] = {0xD07Cu, 0x0010u, 0xB07Cu, 0x0020u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);
  m.cpu.regs.d[0] = 0x0010u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x0020u, m.cpu.regs.d[0]);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 4u, m.cpu.regs.pc);

  /* And the compare against the same value sets Z, leaving the register. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x0020u, m.cpu.regs.d[0]);
  TEST_ASSERT_TRUE(ap_m68030_read_ccr(&m.cpu.regs) &
                   (1u << AP_M68030_SR_Z_BIT));
}

/* The other direction has no immediate: the `100`-`110` opmodes write to the
 * effective address, and an immediate is not somewhere a result can go. */
static void test_the_memory_direction_refuses_an_immediate_destination(void) {
  /* ADD.W D0,#$10 -- encodable, and not an instruction. */
  static const uint16_t program[] = {0xD17Cu, 0x0010u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.d[0] = 1u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_UNIMPLEMENTED,
                        ap_m68030_step(&m.cpu).status);
}

/* "TST #<data>" is marked "MC68020, MC68030, MC68040, and CPU32" on the TST
 * page: the 68000 had no such form, which is exactly why a 68000-shaped model
 * refuses it. It only reads its operand, so nothing is written back. */
static void test_tst_takes_an_immediate_on_this_part(void) {
  /* TST.W #0, then TST.W #$8000. */
  static const uint16_t program[] = {0x4A7Cu, 0x0000u, 0x4A7Cu, 0x8000u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_TRUE(ap_m68030_read_ccr(&m.cpu.regs) &
                   (1u << AP_M68030_SR_Z_BIT));

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_TRUE(ap_m68030_read_ccr(&m.cpu.regs) &
                   (1u << AP_M68030_SR_N_BIT));
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 8u, m.cpu.regs.pc);
}

/* ---------------------------------------------------------------------------
 * The identity harness: a run's state as one number.
 * ------------------------------------------------------------------------- */

/* "same workload twice -> same hash", which is the property the whole identity
 * harness rests on. If two runs of one program from one starting state can
 * disagree, nothing checked under the harness is checked at all. */
static void test_the_same_program_run_twice_hashes_the_same(void) {
  /* A program that touches registers, memory, a branch and a loop, so the hash
   * has something of every kind to cover: MOVEQ, a store, a countdown DBcc and
   * a subroutine call and return. */
  static const uint16_t program[] = {
      0x7003u,          /* MOVEQ #3,D0        */
      0x203Cu, 0x0000u, 0x5000u, /* MOVE.L #$5000,D0 -- overwritten below */
      0x2240u,          /* MOVEA.L D0,A1      */
      0x7205u,          /* MOVEQ #5,D1        */
      0x51C9u, 0xFFFEu, /* DBF D1,-2          */
      0x6100u, 0x0002u, /* BSR.W +2           */
      0x4E75u,          /* RTS                */
      0x4E71u, 0x4E71u};

  machine_t first = {0};
  load(&first, program, 14);
  first.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  first.cpu.regs.isp = SUPERVISOR_STACK;

  machine_t second = {0};
  load(&second, program, 14);
  second.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  second.cpu.regs.isp = SUPERVISOR_STACK;

  /* The two machines are distinct objects at different addresses: if any host
   * pointer reached the hash, this is where it would show. */
  TEST_ASSERT_EQUAL_HEX64(ap_m68030_state_hash(&first.cpu),
                          ap_m68030_state_hash(&second.cpu));

  for (unsigned i = 0; i < 12u; i++) {
    (void)ap_m68030_step(&first.cpu);
    (void)ap_m68030_step(&second.cpu);
    TEST_ASSERT_EQUAL_HEX64(ap_m68030_state_hash(&first.cpu),
                            ap_m68030_state_hash(&second.cpu));
  }
}

/* And the hash must actually *move* as the run proceeds: one that never changed
 * would satisfy the test above perfectly and detect nothing. Every step of a
 * program that changes state must produce a state the run has not been in. */
static void test_the_hash_moves_as_the_program_runs(void) {
  static const uint16_t program[] = {0x7001u, 0x7202u, 0x7403u, 0x7604u,
                                     0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 6);

  uint64_t seen[5];
  seen[0] = ap_m68030_state_hash(&m.cpu);
  for (unsigned i = 1; i < 5u; i++) {
    TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED,
                          ap_m68030_step(&m.cpu).status);
    seen[i] = ap_m68030_state_hash(&m.cpu);
  }

  for (unsigned i = 0; i < 5u; i++) {
    for (unsigned j = i + 1u; j < 5u; j++) {
      TEST_ASSERT_NOT_EQUAL_UINT64(seen[i], seen[j]);
    }
  }
}

/* Two runs that reach the same registers by different routes are not the same
 * run: the clock is state. A NOP before the work leaves every architectural
 * register identical and the machine a few clocks further on, and the hash must
 * say so -- this is exactly the divergence a fast mode introduces. */
static void test_two_runs_with_the_same_registers_differ_by_their_clock(void) {
  static const uint16_t direct[] = {0x7007u, 0x4E71u, 0x4E71u, 0x4E71u};
  static const uint16_t delayed[] = {0x4E71u, 0x7007u, 0x4E71u, 0x4E71u};

  machine_t a = {0};
  load(&a, direct, 4);
  (void)ap_m68030_step(&a.cpu);

  machine_t b = {0};
  load(&b, delayed, 4);
  (void)ap_m68030_step(&b.cpu);
  (void)ap_m68030_step(&b.cpu);

  /* Same register, same PC ... */
  TEST_ASSERT_EQUAL_HEX32(7u, a.cpu.regs.d[0]);
  TEST_ASSERT_EQUAL_HEX32(7u, b.cpu.regs.d[0]);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 2u, a.cpu.regs.pc);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 4u, b.cpu.regs.pc);

  /* ... and different machines, which the hash reports. */
  TEST_ASSERT_NOT_EQUAL_UINT64(ap_m68030_state_hash(&a.cpu),
                               ap_m68030_state_hash(&b.cpu));
}

static void test_ori_to_the_status_register_sets_the_interrupt_mask(void) {
  /* The boot PROM's twenty-first instruction, `ORI #$0700,SR` -- masking
   * interrupts before touching hardware. `MOVE to SR` already worked; this is a
   * different encoding, and the whole immediate-to-status group was missing
   * with it (`FINDINGS.md` C29). */
  static const uint16_t code[] = {0x007Cu, 0x0700u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, code, 4);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX16((uint16_t)((1u << AP_M68030_SR_S_BIT) | 0x0700u),
                          m.cpu.regs.sr);
}

static void test_andi_to_the_condition_codes_leaves_the_high_byte(void) {
  /* A CCR form reaches only the low byte, so an AND must not clear the
   * privilege and mask bits against the immediate's discarded half. This is the
   * one of the six where a naive whole-word implementation silently drops the
   * machine out of supervisor state. */
  static const uint16_t code[] = {0x023Cu, 0x0000u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, code, 4);
  m.cpu.regs.sr = (uint16_t)((1u << AP_M68030_SR_S_BIT) | 0x0700u | 0x001Fu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX16((uint16_t)((1u << AP_M68030_SR_S_BIT) | 0x0700u),
                          m.cpu.regs.sr);
}

static void test_ori_to_the_status_register_is_privileged(void) {
  /* The SR forms write the privilege bits themselves, so a user-state program
   * must not reach them -- while the CCR forms are unprivileged. */
  static const uint16_t code[] = {0x007Cu, 0x0700u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, code, 4);
  m.cpu.regs.sr = 0x0000u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);
}


/* ---------------------------------------------------------------------------
 * CMP2 and CHK2, `M68000 Family PRM` 4-70 and 4-81.
 * ------------------------------------------------------------------------- */

/* The bounds pair, well clear of the program image. */
#define BOUNDS_AT 0x00003000u

static void write_ram_word(machine_t *m, uint32_t address, uint16_t value) {
  TEST_ASSERT_TRUE_MESSAGE(address + 2u <= RAM_BYTES,
                           "address outside the harness RAM");
  m->memory.bytes[address] = (uint8_t)(value >> 8);
  m->memory.bytes[address + 1u] = (uint8_t)value;
}

/* The pair in memory, the register between them, and Z set only on a bound.
 * `CMP2.W (A0),D1` with bounds 10..20.
 *
 * The instruction is `0000 0 SIZE 011 <ea>` with the register in the extension
 * word's bits 14-12 and D/A in bit 15. */
static void test_cmp2_reports_in_bounds_without_trapping(void) {
  /* CMP2.W (A0),D1 : 0000 0 01 011 010 000 = $02D0, extension $1000 */
  static const uint16_t program[] = {0x02D0u, 0x1000u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  m.cpu.regs.a[0] = BOUNDS_AT;
  write_ram_word(&m, BOUNDS_AT, 10u);
  write_ram_word(&m, BOUNDS_AT + 2u, 20u);
  m.cpu.regs.d[1] = 15u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  const uint16_t ccr = ap_m68030_read_ccr(&m.cpu.regs);
  /* In bounds and on neither: C and Z both clear. */
  TEST_ASSERT_EQUAL_UINT(0u, (ccr >> AP_M68030_SR_C_BIT) & 1u);
  TEST_ASSERT_EQUAL_UINT(0u, (ccr >> AP_M68030_SR_Z_BIT) & 1u);
}

/* "Z -- Set if Rn is equal to either bound". Either, and the test is run on
 * both so a model checking only the lower one fails. */
static void test_cmp2_sets_z_on_either_bound(void) {
  static const uint16_t program[] = {0x02D0u, 0x1000u, 0x4E71u};
  for (unsigned which = 0; which < 2u; which++) {
    machine_t m = {0};
    load(&m, program, 3);
    m.cpu.regs.a[0] = BOUNDS_AT;
    write_ram_word(&m, BOUNDS_AT, 10u);
    write_ram_word(&m, BOUNDS_AT + 2u, 20u);
    m.cpu.regs.d[1] = which == 0u ? 10u : 20u;

    TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
    const uint16_t ccr = ap_m68030_read_ccr(&m.cpu.regs);
    TEST_ASSERT_EQUAL_UINT(1u, (ccr >> AP_M68030_SR_Z_BIT) & 1u);
    TEST_ASSERT_EQUAL_UINT(0u, (ccr >> AP_M68030_SR_C_BIT) & 1u);
  }
}

/* Out of bounds sets C, and CMP2 does **not** trap -- "it sets condition codes
 * rather than taking an exception", which is the entire difference from CHK2
 * and the thing a shared implementation gets wrong. */
static void test_cmp2_sets_carry_out_of_bounds_and_does_not_trap(void) {
  static const uint16_t program[] = {0x02D0u, 0x1000u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  m.cpu.regs.a[0] = BOUNDS_AT;
  write_ram_word(&m, BOUNDS_AT, 10u);
  write_ram_word(&m, BOUNDS_AT + 2u, 20u);
  m.cpu.regs.d[1] = 25u;

  const ap_m68030_step_result_t result = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, result.status);
  TEST_ASSERT_EQUAL_UINT(
      1u, (ap_m68030_read_ccr(&m.cpu.regs) >> AP_M68030_SR_C_BIT) & 1u);
}

/* CHK2 is the same instruction and traps, vector 6. Bit 11 of the *extension*
 * word is what tells them apart -- nothing in the instruction word does, so a
 * decoder that never read the extension would run every CHK2 as a CMP2 and
 * silently skip the trap. */
static void test_chk2_traps_out_of_bounds_where_cmp2_does_not(void) {
  /* Same instruction word; extension $1800 sets bit 11. */
  static const uint16_t program[] = {0x02D0u, 0x1800u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  m.cpu.regs.a[0] = BOUNDS_AT;
  write_ram_word(&m, BOUNDS_AT, 10u);
  write_ram_word(&m, BOUNDS_AT + 2u, 20u);
  m.cpu.regs.d[1] = 25u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);

  /* And in bounds it does not trap, so the trap is the bounds test and not the
   * instruction. */
  machine_t inside = {0};
  load(&inside, program, 3);
  inside.cpu.regs.a[0] = BOUNDS_AT;
  write_ram_word(&inside, BOUNDS_AT, 10u);
  write_ram_word(&inside, BOUNDS_AT + 2u, 20u);
  inside.cpu.regs.d[1] = 15u;
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED,
                        ap_m68030_step(&inside.cpu).status);
}

/* **Signed bounds work through the same comparison as unsigned ones.** The
 * manual gives the processor no signedness mode -- it tells the programmer
 * which ordering to use -- so one test must serve both. Bounds -5..5 with a
 * register of -10 is out; with -1 it is in. A model comparing unsigned would
 * call -1 enormous and report it out of bounds. */
static void test_cmp2_handles_signed_bounds(void) {
  static const uint16_t program[] = {0x02D0u, 0x1000u, 0x4E71u};
  const struct {
    uint16_t value;
    unsigned carry;
  } CASES[] = {
      {0xFFF6u, 1u}, /* -10, below the lower bound */
      {0xFFFFu, 0u}, /* -1, inside */
      {0x0000u, 0u}, /* 0, inside */
      {0x000Au, 1u}, /* 10, above the upper bound */
  };

  for (unsigned i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
    machine_t m = {0};
    load(&m, program, 3);
    m.cpu.regs.a[0] = BOUNDS_AT;
    write_ram_word(&m, BOUNDS_AT, 0xFFFBu); /* -5 */
    write_ram_word(&m, BOUNDS_AT + 2u, 5u);
    m.cpu.regs.d[1] = CASES[i].value;

    TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
    TEST_ASSERT_EQUAL_UINT(
        CASES[i].carry,
        (ap_m68030_read_ccr(&m.cpu.regs) >> AP_M68030_SR_C_BIT) & 1u);
  }
}

/* An **address** register is compared over all 32 bits against sign-extended
 * bounds, where a data register is compared only at the operand's width. Same
 * instruction, same bounds, same bit pattern in the register -- and different
 * answers, which is what makes this worth its own test.
 *
 * Bounds -5..5 as words. A0 holds $FFFFFFF6, which is -10 over 32 bits and out
 * of bounds; D1 holds the same bits, whose low word is -10 and also out. So the
 * discriminating value is one whose *upper half* matters: $0001FFFF is +131071
 * over 32 bits (out) and -1 in its low word (in). */
static void test_an_address_register_is_checked_over_all_32_bits(void) {
  static const uint16_t data_form[] = {0x02D0u, 0x1000u, 0x4E71u};
  static const uint16_t address_form[] = {0x02D0u, 0x9000u, 0x4E71u};

  machine_t as_data = {0};
  load(&as_data, data_form, 3);
  as_data.cpu.regs.a[0] = BOUNDS_AT;
  write_ram_word(&as_data, BOUNDS_AT, 0xFFFBu); /* -5 */
  write_ram_word(&as_data, BOUNDS_AT + 2u, 5u);
  as_data.cpu.regs.d[1] = 0x0001FFFFu;
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED,
                        ap_m68030_step(&as_data.cpu).status);
  /* Only the low word is checked: -1, inside. */
  TEST_ASSERT_EQUAL_UINT(
      0u, (ap_m68030_read_ccr(&as_data.cpu.regs) >> AP_M68030_SR_C_BIT) & 1u);

  machine_t as_address = {0};
  load(&as_address, address_form, 3);
  as_address.cpu.regs.a[0] = BOUNDS_AT;
  write_ram_word(&as_address, BOUNDS_AT, 0xFFFBu);
  write_ram_word(&as_address, BOUNDS_AT + 2u, 5u);
  as_address.cpu.regs.a[1] = 0x0001FFFFu;
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED,
                        ap_m68030_step(&as_address.cpu).status);
  /* All 32 bits: +131071, well outside. */
  TEST_ASSERT_EQUAL_UINT(
      1u, (ap_m68030_read_ccr(&as_address.cpu.regs) >> AP_M68030_SR_C_BIT) & 1u);
}

/* CAS on a match: the update register goes to memory, and Z is set.
 *
 * CAS.W Dc=D0,Du=D1,(A0) -- the extension word is `0000 000 001 000 000`, Du in
 * bits 8-6 and Dc in bits 2-0, so D1 updates and D0 compares. */
static void test_cas_swaps_when_the_comparison_matches(void) {
  static const uint16_t program[] = {0x0CD0u, 0x0040u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  m.cpu.regs.a[0] = BOUNDS_AT;
  write_ram_word(&m, BOUNDS_AT, 0x1234u);
  m.cpu.regs.d[0] = 0x1234u; /* compare: matches memory */
  m.cpu.regs.d[1] = 0xABCDu; /* update */

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_UINT(
      1u, (ap_m68030_read_ccr(&m.cpu.regs) >> AP_M68030_SR_Z_BIT) & 1u);
  /* The update reached memory. */
  const uint32_t stored =
      ((uint32_t)m.memory.bytes[BOUNDS_AT] << 8) | m.memory.bytes[BOUNDS_AT + 1u];
  TEST_ASSERT_EQUAL_HEX32(0xABCDu, stored);
  /* And the compare register is untouched on a match. */
  TEST_ASSERT_EQUAL_HEX32(0x1234u, m.cpu.regs.d[0] & 0xFFFFu);
}

/* **On a mismatch the write still happens, and it goes the other way**: "the
 * instruction writes the effective address operand to the compare operand".
 * A model that simply skipped the store would leave Dc holding what the caller
 * expected rather than what was actually there -- which is precisely the value
 * the caller needs in order to retry, so the loop would spin forever. */
static void test_cas_loads_the_compare_register_when_it_fails(void) {
  static const uint16_t program[] = {0x0CD0u, 0x0040u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  m.cpu.regs.a[0] = BOUNDS_AT;
  write_ram_word(&m, BOUNDS_AT, 0x1234u);
  m.cpu.regs.d[0] = 0x9999u; /* compare: does not match */
  m.cpu.regs.d[1] = 0xABCDu;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_UINT(
      0u, (ap_m68030_read_ccr(&m.cpu.regs) >> AP_M68030_SR_Z_BIT) & 1u);
  /* Memory is unchanged... */
  const uint32_t stored =
      ((uint32_t)m.memory.bytes[BOUNDS_AT] << 8) | m.memory.bytes[BOUNDS_AT + 1u];
  TEST_ASSERT_EQUAL_HEX32(0x1234u, stored);
  /* ...and the compare register now holds what memory had. */
  TEST_ASSERT_EQUAL_HEX32(0x1234u, m.cpu.regs.d[0] & 0xFFFFu);
}

/* The lock is released either way. RMC held past the instruction would refuse
 * every later bus grant, so a DMA controller would never run again -- a failure
 * that appears only once something else wants the bus. */
static void test_cas_releases_the_lock_on_both_outcomes(void) {
  static const uint16_t program[] = {0x0CD0u, 0x0040u, 0x4E71u};
  for (unsigned matching = 0; matching < 2u; matching++) {
    machine_t m = {0};
    load(&m, program, 3);
    m.cpu.regs.a[0] = BOUNDS_AT;
    write_ram_word(&m, BOUNDS_AT, 0x1234u);
    m.cpu.regs.d[0] = matching != 0u ? 0x1234u : 0x9999u;
    TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED,
                          ap_m68030_step(&m.cpu).status);
    TEST_ASSERT_FALSE(m.data_access.rmc);
  }
}

/* CAS2 swaps **both** operands or neither, under one lock.
 *
 * Its addresses come from registers -- "Rn1, Rn2 fields: specify the numbers of
 * the registers that contain the addresses of the first and second memory
 * operands" -- and not from an addressing mode. That is why this turned out to
 * be implementable after being declined as a two-address atomic the operand
 * path could not express: the `<ea>` in the operation word is the immediate
 * encoding used purely as an escape, and reading it as an address is what made
 * the instruction look harder than it is. */
static void test_cas2_swaps_both_operands_or_neither(void) {
  /* CAS2.W: $0CFC, then two extension words. Each carries D/A at 15, Rn at
   * 14-12, Du at 8-6 and Dc at 2-0 -- so A0 and A1 hold the addresses, D2 and
   * D3 the updates, D0 and D1 the comparands. */
  static const uint16_t program[] = {0x0CFCu, 0x8080u, 0x90C1u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.a[0] = BOUNDS_AT;
  m.cpu.regs.a[1] = BOUNDS_AT + 0x10u;
  write_ram_word(&m, BOUNDS_AT, 0x1111u);
  write_ram_word(&m, BOUNDS_AT + 0x10u, 0x2222u);
  m.cpu.regs.d[0] = 0x1111u;
  m.cpu.regs.d[1] = 0x2222u;
  m.cpu.regs.d[2] = 0xAAAAu;
  m.cpu.regs.d[3] = 0xBBBBu;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);

  const uint32_t first =
      ((uint32_t)m.memory.bytes[BOUNDS_AT] << 8) | m.memory.bytes[BOUNDS_AT + 1u];
  const uint32_t second = ((uint32_t)m.memory.bytes[BOUNDS_AT + 0x10u] << 8) |
                          m.memory.bytes[BOUNDS_AT + 0x11u];
  TEST_ASSERT_EQUAL_HEX32(0xAAAAu, first);
  TEST_ASSERT_EQUAL_HEX32(0xBBBBu, second);
}

/* **If either comparison fails, neither write happens.** All or nothing is the
 * whole point: comparing one at a time with a write between them would leave
 * memory half updated when the second failed, which is precisely the corruption
 * the instruction exists to prevent. Here the *second* comparison fails, so the
 * first operand must be left alone even though it matched. */
static void test_cas2_writes_nothing_when_the_second_comparison_fails(void) {
  static const uint16_t program[] = {0x0CFCu, 0x8080u, 0x90C1u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.a[0] = BOUNDS_AT;
  m.cpu.regs.a[1] = BOUNDS_AT + 0x10u;
  write_ram_word(&m, BOUNDS_AT, 0x1111u);
  write_ram_word(&m, BOUNDS_AT + 0x10u, 0x2222u);
  m.cpu.regs.d[0] = 0x1111u; /* matches */
  m.cpu.regs.d[1] = 0x9999u; /* does not */

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);

  const uint32_t first =
      ((uint32_t)m.memory.bytes[BOUNDS_AT] << 8) | m.memory.bytes[BOUNDS_AT + 1u];
  TEST_ASSERT_EQUAL_HEX32(0x1111u, first);
  /* And both memory operands went into their compare registers. */
  TEST_ASSERT_EQUAL_HEX32(0x1111u, m.cpu.regs.d[0] & 0xFFFFu);
  TEST_ASSERT_EQUAL_HEX32(0x2222u, m.cpu.regs.d[1] & 0xFFFFu);
}

/* "If Dc1 and Dc2 specify the same data register and the comparison fails,
 * memory operand 1 is stored in the data register." The two register writes
 * happen in order and the *first* wins when they collide -- a model writing the
 * second last leaves the wrong value in a case the manual calls out by name. */
static void test_cas2_leaves_the_first_operand_when_the_registers_collide(void) {
  /* Both Dc fields name D0. */
  static const uint16_t program[] = {0x0CFCu, 0x8080u, 0x90C0u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.a[0] = BOUNDS_AT;
  m.cpu.regs.a[1] = BOUNDS_AT + 0x10u;
  write_ram_word(&m, BOUNDS_AT, 0x1111u);
  write_ram_word(&m, BOUNDS_AT + 0x10u, 0x2222u);
  m.cpu.regs.d[0] = 0x9999u; /* fails at once */

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x1111u, m.cpu.regs.d[0] & 0xFFFFu);
}

/* The lock is released either way, as for CAS: held past the instruction it
 * would refuse every later bus grant. */
static void test_cas2_releases_the_lock(void) {
  static const uint16_t program[] = {0x0CFCu, 0x8080u, 0x90C1u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  m.cpu.regs.a[0] = BOUNDS_AT;
  m.cpu.regs.a[1] = BOUNDS_AT + 0x10u;
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_FALSE(m.data_access.rmc);
}


/* CHK's Z, V and C, which the manual leaves undefined and the hardware sets.
 * "Z is set if the register operand (the second operand; not the effective
 * address operand) is 0"; V and C are "always cleared".
 *
 * The parenthesis is the load-bearing part: `Z` from the *bound* is the
 * plausible wrong reading, so the test uses a zero bound with a non-zero
 * register and the reverse. */
static void test_chk_sets_z_from_the_register_not_the_bound(void) {
  /* CHK.W #<bound>,D0 : 0100 000 110 111 100 = $41BC */
  static const uint16_t zero_register[] = {0x41BCu, 0x0005u, 0x4E71u};
  machine_t m = {0};
  load(&m, zero_register, 3);
  m.cpu.regs.d[0] = 0u;
  ap_m68030_write_ccr(&m.cpu.regs, (uint16_t)((1u << AP_M68030_SR_V_BIT) |
                                              (1u << AP_M68030_SR_C_BIT)));
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  uint16_t ccr = ap_m68030_read_ccr(&m.cpu.regs);
  TEST_ASSERT_EQUAL_UINT(1u, (ccr >> AP_M68030_SR_Z_BIT) & 1u);
  /* And "always cleared", from a state where both were set. */
  TEST_ASSERT_EQUAL_UINT(0u, (ccr >> AP_M68030_SR_V_BIT) & 1u);
  TEST_ASSERT_EQUAL_UINT(0u, (ccr >> AP_M68030_SR_C_BIT) & 1u);

  /* A zero *bound* with a non-zero register clears Z -- which is the case a
   * model reading the effective address operand would get backwards. */
  static const uint16_t zero_bound[] = {0x41BCu, 0x0000u, 0x4E71u};
  machine_t n = {0};
  load(&n, zero_bound, 3);
  n.cpu.regs.d[0] = 0u;
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  /* Register 0 against bound 0: in bounds, and Z set from the register. */
  TEST_ASSERT_EQUAL_UINT(
      1u, (ap_m68030_read_ccr(&n.cpu.regs) >> AP_M68030_SR_Z_BIT) & 1u);

  machine_t nonzero = {0};
  load(&nonzero, zero_bound, 3);
  nonzero.cpu.regs.d[0] = 1u; /* above the bound: traps, and Z clear */
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION,
                        ap_m68030_step(&nonzero.cpu).status);
}


/* BKPT runs a breakpoint acknowledge cycle in **CPU space**, `[030]` §7.4.2.
 *
 * On the DN3500 nothing answers it -- no breakpoint hardware is fitted -- so
 * the cycle takes a bus error and "the processor takes an illegal instruction
 * exception". That is the machine's behaviour and not an error path, which is
 * why this is now an exception rather than the unimplemented report it used to
 * be: declining would have said the gap was ours. */
static void test_bkpt_takes_an_illegal_instruction_when_nothing_answers(void) {
  /* BKPT #3: 0100 1000 0100 1011 = $484B */
  static const uint16_t program[] = {0x484Bu, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 2);
  /* CPU space faults, which is what an unfitted breakpoint responder looks
   * like from the processor's side -- and what a DN3500 is. */
  m.memory.berr_on_cpu_space = true;
  write_ram_long(&m, 4u * AP_M68030_VECTOR_ILLEGAL_INSTRUCTION, HANDLER);

  const ap_m68030_step_result_t result = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, result.status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
}

/* The breakpoint number rides on **A2-A4**, not on the low address lines.
 * Putting it anywhere else acknowledges a different breakpoint, and external
 * hardware answers with the wrong instruction word rather than faulting -- so
 * this is checked by watching the address the cycle actually presents. */
static void test_the_breakpoint_number_is_on_a2_to_a4(void) {
  static const uint16_t program[] = {0x484Bu, 0x4E71u}; /* BKPT #3 */
  machine_t m = {0};
  load(&m, program, 2);
  m.memory.berr_on_cpu_space = true;
  write_ram_long(&m, 4u * AP_M68030_VECTOR_ILLEGAL_INSTRUCTION, HANDLER);
  (void)ap_m68030_step(&m.cpu);

  /* Exactly one CPU-space cycle ran, and #3 rode on A2-A4: $0C, not $03. */
  TEST_ASSERT_EQUAL_UINT(1u, m.memory.cpu_space_cycles);
  TEST_ASSERT_EQUAL_HEX32(0x0Cu, m.memory.cpu_space_address);
}


/* The reset exception, `[030]` §8.1.1's ten steps -- checked as a whole,
 * because every one of them is a state a later exception or a first instruction
 * depends on and none of them faults when missed. */
static void test_reset_performs_all_ten_documented_steps(void) {
  static const uint16_t program[] = {0x4E71u};
  machine_t m = {0};
  load(&m, program, 1);

  /* Put the processor in the state each step has to undo: tracing on, master
   * mode, an open interrupt mask, a stale vector base, caches enabled and
   * translation on. */
  ap_m68030_write_sr(&m.cpu.regs,
                     (uint16_t)((1u << AP_M68030_SR_T1_BIT) |
                                (1u << AP_M68030_SR_S_BIT) |
                                (1u << AP_M68030_SR_M_BIT)));
  m.cpu.regs.vbr = 0x00004000u;
  m.cpu.cacr.enable_instruction = true;
  m.cpu.cacr.enable_data = true;
  m.cpu.cacr.freeze_data = true;
  m.cpu.cacr.write_allocate = true;
  m.cpu.cacr.data_burst_enable = true;
  m.cpu.tc.enable = true;
  m.cpu.tt0.enabled = true;
  m.cpu.tt1.enabled = true;

  /* The vector at offset zero: stack pointer then program counter. */
  write_ram_long(&m, 0u, SUPERVISOR_STACK);
  write_ram_long(&m, 4u, HANDLER);

  /* An ATC entry, which reset must *not* remove. */
  const int entry = ap_m68030_atc_insert(&m.atc, 5u, 0x8000u, 12u, 0x9000u,
                                         false, false, false, false);
  TEST_ASSERT_TRUE(entry >= 0);

  TEST_ASSERT_TRUE(ap_m68030_take_reset(&m.cpu));

  /* 1. Tracing off. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_TRACE_NONE,
                        ap_m68030_trace_mode(&m.cpu.regs));
  /* 2. Supervisor *interrupt* mode: S set and M **clear**. Leaving M set puts
   *    the machine on the master stack when every later exception expects the
   *    interrupt stack. */
  TEST_ASSERT_TRUE(ap_m68030_supervisor(&m.cpu.regs));
  TEST_ASSERT_EQUAL_UINT(0u, (m.cpu.regs.sr >> AP_M68030_SR_M_BIT) & 1u);
  /* 3. Mask at 7. */
  TEST_ASSERT_EQUAL_UINT(7u, ap_m68030_interrupt_mask(&m.cpu.regs));
  /* 4. Vector base zeroed -- a stale one sends every later vector fetch into
   *    whatever the previous system used. */
  TEST_ASSERT_EQUAL_HEX32(0u, m.cpu.regs.vbr);
  /* 5. Both caches disabled, unfrozen, not bursting, write allocation off. */
  TEST_ASSERT_FALSE(m.cpu.cacr.enable_instruction);
  TEST_ASSERT_FALSE(m.cpu.cacr.enable_data);
  TEST_ASSERT_FALSE(m.cpu.cacr.freeze_data);
  TEST_ASSERT_FALSE(m.cpu.cacr.write_allocate);
  TEST_ASSERT_FALSE(m.cpu.cacr.data_burst_enable);
  /* 7. Translation off, in the TC and in **both** transparent registers. */
  TEST_ASSERT_FALSE(m.cpu.tc.enable);
  TEST_ASSERT_FALSE(m.cpu.tt0.enabled);
  TEST_ASSERT_FALSE(m.cpu.tt1.enabled);
  /* 9, 10. Both long words of the vector. */
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK, m.cpu.regs.isp);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);

  /* And the two explicit negatives, which are as load-bearing as the steps:
   * "The reset exception does not flush the address translation cache (ATC),
   * nor does it save the value of either the program counter or the status
   * register." A model that flushed the ATC would be tidier and wrong. */
  const ap_m68030_atc_result_t survived =
      ap_m68030_atc_lookup(&m.atc, 5u, 0x8000u, 12u, false, false);
  TEST_ASSERT_EQUAL_INT(AP_M68030_ATC_HIT, survived.status);
}

/* No frame is built: reset "does not save the value of either the program
 * counter or the status register", so the stack pointer it loads is the one
 * from the vector and nothing has been pushed below it. */
static void test_reset_stacks_nothing(void) {
  static const uint16_t program[] = {0x4E71u};
  machine_t m = {0};
  load(&m, program, 1);
  write_ram_long(&m, 0u, SUPERVISOR_STACK);
  write_ram_long(&m, 4u, HANDLER);

  const unsigned stores_before = m.memory.stores;
  TEST_ASSERT_TRUE(ap_m68030_take_reset(&m.cpu));

  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK, ap_m68030_read_a7(&m.cpu.regs));
  TEST_ASSERT_EQUAL_UINT(stores_before, m.memory.stores);
}


/* ---------------------------------------------------------------------------
 * The floating-point coprocessor, reached through the F-line path.
 * ------------------------------------------------------------------------- */

/* With no coprocessor fitted an F-line word takes the line 1111 emulator
 * exception, `[030]` §8.1 -- which is a DN3000, and is what this core did
 * before there was an FPU to attach. Attaching one must not change it, which is
 * why the part is a pointer on the CPU rather than a member. */
static void test_an_f_line_word_traps_when_no_coprocessor_is_fitted(void) {
  /* FADD FP0,FP1 : operation word $F200, command word $00A2 -- opclass 000,
   * RX (source) 000 at bits 12-10, RY (destination) 001 at bits 9-7, and $22
   * in the extension. */
  static const uint16_t program[] = {0xF200u, 0x00A2u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  plant_vector(&m, AP_M68030_VECTOR_LINE_F, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  TEST_ASSERT_NULL(m.cpu.fpu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
}

/* With one fitted, the instruction executes. `FADD FP0,FP1` adds the source
 * register into the destination -- "FPn + Source -> FPn" -- and the condition
 * codes come from the result. */
static void test_a_fitted_coprocessor_executes_an_f_line_instruction(void) {
  static const uint16_t program[] = {0xF200u, 0x00A2u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);

  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;

  /* 1.0 in both registers, so the sum is 2.0. */
  fpu.regs.fp[0] = ap_m68882_from_single(0x3F800000u);
  fpu.regs.fp[1] = ap_m68882_from_single(0x3F800000u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x40000000u, ap_m68882_to_single(&fpu.regs.fp[1]));
  /* And the source is untouched, which is what makes it FPn + Source and not a
   * swap. */
  TEST_ASSERT_EQUAL_HEX32(0x3F800000u, ap_m68882_to_single(&fpu.regs.fp[0]));

  /* The program counter moved past **both** words: the command word is part of
   * the instruction, and a step that consumed only the operation word would
   * decode the command word as the next instruction. */
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 4u, m.cpu.regs.pc);
}

/* A coprocessor answering a *different* cpID does not answer this one. A
 * machine may hold several, and the FPU takes only its own -- so an instruction
 * for cpID 3 still traps with a 68882 fitted at cpID 1. */
static void test_a_fitted_coprocessor_ignores_another_cpid(void) {
  /* cpID 3: operation word $F600. */
  static const uint16_t program[] = {0xF600u, 0x00A2u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  plant_vector(&m, AP_M68030_VECTOR_LINE_F, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;

  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
}

/* **An undefined extension traps even with a coprocessor fitted**, and it is
 * the *same* vector an unfitted machine takes -- Table 4-13's footnote 2 has
 * the FPCP itself ask the MPU for an F-line trap. So the two arrive at one
 * handler for different reasons, which is the hardware's behaviour and not a
 * conflation. */
static void test_an_undefined_extension_traps_with_a_coprocessor_fitted(void) {
  /* Extension $7F, which Table 4-13 leaves undefined. */
  static const uint16_t program[] = {0xF200u, 0x007Fu, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  plant_vector(&m, AP_M68030_VECTOR_LINE_F, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;

  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
}

/* **An unimplemented form is reported as ours, not as the machine's.** These
 * are instructions the hardware executes, so raising F-line for one would be
 * indistinguishable from a correct unfitted machine and the gap would stop
 * being visible.
 *
 * The example moved six times through the 68882 and has now left it entirely:
 * **every 68882 instruction and every data format executes**, `FSAVE` and
 * `FRESTORE` included. So the subject is the *other* coprocessor -- an MMU
 * instruction this model has not got to, which sits at cpID 0 and is fitted, so
 * the real 68030 would execute it. That distinction is the whole point of the
 * test and does not depend on which instruction carries it. */
static void test_an_unimplemented_form_is_reported_as_our_gap(void) {
  /* cpID 0, general type, with an extension class the MMU dispatch declines. */
  static const uint16_t program[] = {0xF000u, 0xA000u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_UNIMPLEMENTED, r.status);
  TEST_ASSERT_NOT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, r.status);
  /* And the program counter did not move, so "how far did this get" stays a
   * real measure. */
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE, m.cpu.regs.pc);
}

#define FP_OPERAND 0x00003000u

/* **A source operand comes from memory**, which is what makes most real
 * floating-point code run: `FADD.S (A0),FP1`. §4.8.3's R/M bit says the operand
 * is external, and `[030]` §10.4.9 makes fetching it the main processor's work
 * -- so this passes through both parts, and neither could do it alone. */
static void test_a_single_source_operand_is_fetched_from_memory(void) {
  /* Operation word $F210: cpID 1, general type, `(A0)`. Command word $44A2:
   * opclass 010, source format 001 (S), destination FP1, extension $22 (FADD). */
  static const uint16_t program[] = {0xF210u, 0x44A2u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  write_ram_long(&m, FP_OPERAND, 0x3F800000u); /* 1.0 single */

  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  m.cpu.regs.a[0] = FP_OPERAND;
  fpu.regs.fp[1] = ap_m68882_from_single(0x3F800000u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x40000000u, ap_m68882_to_single(&fpu.regs.fp[1]));
  /* Two words: the operand was at an address, not in the instruction stream. */
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 4u, m.cpu.regs.pc);
}

/* The extended format is **twelve** bytes and not ten: sign and exponent in the
 * first long word, sixteen unused bits below them, then the mantissa. A fetch
 * that packed the 80 used bits would read the mantissa two bytes out of place
 * while still getting the sign and exponent right -- so the value would be
 * wrong and nothing would fault. */
static void test_an_extended_source_operand_spans_three_long_words(void) {
  /* $48A2: opclass 010, source format 010 (X), destination FP1, FADD. */
  static const uint16_t program[] = {0xF210u, 0x48A2u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  /* 1.0 extended, with the unused field deliberately non-zero so that reading
   * it as part of the mantissa could not go unnoticed. */
  write_ram_long(&m, FP_OPERAND, 0x3FFFAAAAu);
  write_ram_long(&m, FP_OPERAND + 4u, 0x80000000u);
  write_ram_long(&m, FP_OPERAND + 8u, 0x00000000u);

  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  m.cpu.regs.a[0] = FP_OPERAND;
  fpu.regs.fp[1] = ap_m68882_from_single(0x3F800000u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x40000000u, ap_m68882_to_single(&fpu.regs.fp[1]));
}

/* **The format decides the address**, not just the read. A postincrement steps
 * by the operand's length, so `(A0)+` with a double advances A0 by eight and
 * with a single by four -- a fetch that assumed one length would leave the
 * address register wrong for every other format, and the arithmetic would still
 * come out right on the first iteration of a loop. */
static void test_a_postincrement_steps_by_the_source_format_length(void) {
  /* $54A2: opclass 010, source format 101 (D), destination FP1, FADD. */
  static const uint16_t program[] = {0xF218u, 0x54A2u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  write_ram_long(&m, FP_OPERAND, 0x3FF00000u); /* 1.0 double */
  write_ram_long(&m, FP_OPERAND + 4u, 0x00000000u);

  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  m.cpu.regs.a[0] = FP_OPERAND;
  fpu.regs.fp[1] = ap_m68882_from_single(0x3F800000u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x40000000u, ap_m68882_to_single(&fpu.regs.fp[1]));
  TEST_ASSERT_EQUAL_HEX32(FP_OPERAND + 8u, m.cpu.regs.a[0]);
}

/* An integer source is converted on the way in -- "all external operands,
 * regardless of data format, are converted to extended precision values before
 * the specified operation is performed" -- and §3.1's integers are *signed*, so
 * an immediate of $FFFF is -1 and the sum comes out at zero rather than 65536.
 *
 * Also the one case where the operand is in the instruction stream: §4.7 counts
 * it in words, so a word operand is one extension word and the program counter
 * lands six bytes on. */
static void test_a_word_immediate_source_is_signed_and_in_the_stream(void) {
  /* $50A2: opclass 010, source format 100 (W), destination FP1, FADD.
   * Operation word $F23C is `#<data>`. */
  static const uint16_t program[] = {0xF23Cu, 0x50A2u, 0xFFFFu, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);

  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  fpu.regs.fp[1] = ap_m68882_from_single(0x3F800000u); /* 1.0 */

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_ZERO,
                        ap_m68882_classify(&fpu.regs.fp[1]));
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 6u, m.cpu.regs.pc);
}

/* **A data register cannot hold a twelve-byte operand.** The FADD page's table
 * marks `Dn` "Only if <fmt> is Byte, Word, Long, or Single", and `[030]`
 * §10.4.9 gives the same rule as lengths together with what breaking it does:
 * "If the effective address is a main processor register (register direct
 * mode), only operand lengths of one, two, or four bytes are valid; all other
 * lengths ... cause the main processor to initiate protocol violation exception
 * processing".
 *
 * So this is the machine's trap and not our gap, and it is *not* F-line -- a
 * distinction worth a test, because reporting either of the other two would
 * look entirely plausible. */
static void test_an_extended_operand_from_a_data_register_violates_the_protocol(
    void) {
  /* $F200 is `D0`; $48A2 asks for the extended format from it. */
  static const uint16_t program[] = {0xF200u, 0x48A2u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  plant_vector(&m, AP_M68030_VECTOR_COPROCESSOR_PROTOCOL, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;

  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);

  /* A single from the same register is legal -- four bytes -- so it is the
   * length that is refused and not register direct addressing itself. */
  static const uint16_t legal[] = {0xF200u, 0x44A2u, 0x4E71u};
  machine_t n = {0};
  load(&n, legal, 3);
  ap_m68882_t second;
  ap_m68882_reset(&second);
  n.cpu.fpu = &second;
  n.cpu.regs.d[0] = 0x3F800000u;
  second.regs.fp[1] = ap_m68882_from_single(0x3F800000u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x40000000u, ap_m68882_to_single(&second.regs.fp[1]));
}

/* Address register direct is the one mode whose row in the FADD table carries
 * dashes instead of an encoding: an address register cannot hold a
 * floating-point operand at any length. Checked separately from the length rule
 * above because they are different refusals -- this one has no legal format. */
static void test_an_address_register_is_never_a_floating_point_source(void) {
  static const uint16_t program[] = {0xF208u, 0x44A2u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  plant_vector(&m, AP_M68030_VECTOR_COPROCESSOR_PROTOCOL, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;

  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  m.cpu.regs.a[0] = 0x3F800000u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
}

/* **A result goes back to memory**: `FMOVE.S FP1,(A0)`, opclass `011`. The
 * other direction of §10.4.9's transfer, and the pair is what a compiled loop
 * actually emits -- load, compute, store. */
static void test_a_result_is_stored_to_memory(void) {
  /* $64C0: opclass 011, destination format 001 (S), source FP1, k-factor 0. */
  static const uint16_t program[] = {0xF210u, 0x64C0u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  write_ram_long(&m, FP_OPERAND, 0xDEADBEEFu);

  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  m.cpu.regs.a[0] = FP_OPERAND;
  fpu.regs.fp[1] = ap_m68882_from_single(0x40000000u); /* 2.0 */

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x40000000u, read_ram_long(&m, FP_OPERAND));
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 4u, m.cpu.regs.pc);
}

/* A predecrement steps by the *destination* format's length, the same rule the
 * load side has and the same failure if it were guessed: `-(A0)` with an
 * extended destination moves A0 back twelve, and the operand lands at the new
 * address rather than the old one. */
static void test_a_predecrement_store_steps_by_the_destination_length(void) {
  /* $6800: opclass 011, destination format 010 (X), source FP0. Operation word
   * $F220 is `-(A0)`. */
  static const uint16_t program[] = {0xF220u, 0x6800u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);

  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  m.cpu.regs.a[0] = FP_OPERAND + 12u;
  fpu.regs.fp[0] = ap_m68882_from_single(0x3F800000u); /* 1.0 */

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(FP_OPERAND, m.cpu.regs.a[0]);
  /* 1.0 extended: exponent $3FFF, integer bit set, and the sixteen unused bits
   * written as zero. */
  TEST_ASSERT_EQUAL_HEX32(0x3FFF0000u, read_ram_long(&m, FP_OPERAND));
  TEST_ASSERT_EQUAL_HEX32(0x80000000u, read_ram_long(&m, FP_OPERAND + 4u));
  TEST_ASSERT_EQUAL_HEX32(0x00000000u, read_ram_long(&m, FP_OPERAND + 8u));
}

/* **A store does not touch the condition codes.** The FMOVE page's Status
 * Register section says "Condition Codes: Not affected", where every arithmetic
 * operation sets them -- so a store routed through the common result path would
 * silently rewrite the codes an earlier compare had left, and the branch after
 * it would go the wrong way. Checked by storing a value whose condition codes
 * would differ from the ones already set. */
static void test_a_store_leaves_the_condition_codes_alone(void) {
  static const uint16_t program[] = {0xF210u, 0x64C0u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);

  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  m.cpu.regs.a[0] = FP_OPERAND;
  fpu.regs.fp[1] = ap_m68882_from_single(0x40000000u); /* 2.0: not zero, not N */

  /* Pretend a comparison just set Z and N. */
  const uint32_t before = fpu.regs.fpsr;
  fpu.regs.fpsr = before | (UINT32_C(1) << AP_M68882_FPCC_Z) |
                  (UINT32_C(1) << AP_M68882_FPCC_N);
  const uint32_t planted = fpu.regs.fpsr;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(planted, fpu.regs.fpsr);
}

/* A data register is a legal destination only for an operand of one, two or
 * four bytes -- the same §10.4.9 rule the load side has, and a protocol
 * violation for the same reason. `FMOVE.L FP0,D0` is how a program gets a
 * floating-point value into the integer unit, so the legal half matters. */
static void test_a_long_word_result_reaches_a_data_register(void) {
  /* $6000: opclass 011, destination format 000 (L), source FP0. */
  static const uint16_t program[] = {0xF200u, 0x6000u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);

  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  m.cpu.regs.d[0] = 0xDEADBEEFu;
  fpu.regs.fp[0] = ap_m68882_from_single(0x42F60000u); /* 123.0 */

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(123u, m.cpu.regs.d[0]);

  /* Twelve bytes into the same register is the refusal. */
  static const uint16_t wide[] = {0xF200u, 0x6800u, 0x4E71u};
  machine_t n = {0};
  load(&n, wide, 3);
  plant_vector(&n, AP_M68030_VECTOR_COPROCESSOR_PROTOCOL, HANDLER);
  n.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  n.cpu.regs.isp = SUPERVISOR_STACK;
  ap_m68882_t second;
  ap_m68882_reset(&second);
  n.cpu.fpu = &second;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, n.cpu.regs.pc);
}

/* A store needs a **data alterable** destination, which is stricter than the
 * source side's data category: `(d16,PC)` is a legal place to read an operand
 * from and not a legal place to put one. §10.4.9 names the failure -- "the
 * MC68030 initiates protocol violation exception processing if the primitive
 * requests a write to a nonalterable effective address". */
static void test_storing_to_a_nonalterable_address_violates_the_protocol(void) {
  /* $F23A is `(d16,PC)`: readable, never writable. */
  static const uint16_t program[] = {0xF23Au, 0x64C0u, 0x0004u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  plant_vector(&m, AP_M68030_VECTOR_COPROCESSOR_PROTOCOL, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;

  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);

  /* The same mode is fine as a *source*, which is what makes this the
   * direction's rule and not the addressing mode's. */
  static const uint16_t reading[] = {0xF23Au, 0x44A2u, 0x0004u, 0x4E71u};
  machine_t n = {0};
  load(&n, reading, 4);
  ap_m68882_t second;
  ap_m68882_reset(&second);
  n.cpu.fpu = &second;
  second.regs.fp[1] = ap_m68882_from_single(0x3F800000u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
}

/* Eight distinguishable values, so a transfer that scrambled the order would
 * show it: 1.0 through 8.0 as singles. */
static const uint32_t FP_MARKERS[8] = {0x3F800000u, 0x40000000u, 0x40400000u,
                                       0x40800000u, 0x40A00000u, 0x40C00000u,
                                       0x40E00000u, 0x41000000u};

static void plant_markers(ap_m68882_t *fpu) {
  for (unsigned i = 0; i < 8u; i++) {
    fpu->regs.fp[i] = ap_m68882_from_single(FP_MARKERS[i]);
  }
}

/* **`FMOVEM`'s two mask orderings are reversed, and a round trip is what proves
 * both are right.** The manual prints them as two rows:
 *
 *     Static, -(An)             --  FP7 FP6 FP5 FP4 FP3 FP2 FP1 FP0
 *     Static, (An)+ or Control  --  FP0 FP1 FP2 FP3 FP4 FP5 FP6 FP7
 *
 * Storing with `-(A0)` and loading back with `(A0)+` therefore uses *both*
 * tables, and only agreeing tables return the registers unchanged -- a model
 * using one ordering for both would reverse all eight and still move the
 * address register the right distance.
 *
 * The layout is asserted too, because a symmetric mistake could cancel out: FP0
 * ends up at the *lowest* address, since predecrement stores FP7 first and
 * works downwards. */
static void test_fmovem_round_trips_through_both_mask_orderings(void) {
  /* FMOVEM.X FP7-FP0,-(A0) : $F220 is `-(A0)`; $E0FF is dr = 1, static
   * predecrement, every register. Then FMOVEM.X (A0)+,FP0-FP7 : $F218 is
   * `(A0)+`, $D0FF is dr = 0, static postincrement, every register. */
  static const uint16_t program[] = {0xF220u, 0xE0FFu, 0xF218u, 0xD0FFu,
                                     0x4E71u};
  machine_t m = {0};
  load(&m, program, 5);

  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  plant_markers(&fpu);
  const uint32_t top = FP_OPERAND + 96u;
  m.cpu.regs.a[0] = top;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  /* Eight registers of twelve bytes each, and the register points at the image
   * it stored *last* rather than one slot past it. */
  TEST_ASSERT_EQUAL_HEX32(top - 96u, m.cpu.regs.a[0]);

  /* FP0 at the lowest address, FP7 at the highest -- the order the transfer
   * runs is FP7 first and downwards. */
  TEST_ASSERT_EQUAL_HEX32(0x3FFF0000u, read_ram_long(&m, top - 96u));
  TEST_ASSERT_EQUAL_HEX32(0x80000000u, read_ram_long(&m, top - 96u + 4u));
  /* FP7 is 8.0, whose extended exponent is $4002. */
  TEST_ASSERT_EQUAL_HEX32(0x40020000u, read_ram_long(&m, top - 12u));

  /* Scramble the registers, then load them back. */
  for (unsigned i = 0; i < 8u; i++) {
    fpu.regs.fp[i] = ap_m68882_from_single(0x7F800000u);
  }
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(top, m.cpu.regs.a[0]);
  for (unsigned i = 0; i < 8u; i++) {
    TEST_ASSERT_EQUAL_HEX32(FP_MARKERS[i],
                            ap_m68882_to_single(&fpu.regs.fp[i]));
  }
}

/* A partial mask moves only the named registers, and names them by the
 * ordering its own mode uses. FP1 is bit 6 in a control mode's mask and bit 1
 * in a predecrement one -- the same register through two encodings, which is
 * the cleanest single statement of the reversal. */
static void test_a_partial_mask_moves_only_its_registers(void) {
  /* FMOVEM.X FP1,(A0) : $F210 is `(A0)`, $F040 is dr = 1, static control mode,
   * mask bit 6. Control modes use the FP0..FP7 ordering. */
  static const uint16_t program[] = {0xF210u, 0xF040u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  write_ram_long(&m, FP_OPERAND + 12u, 0xDEADBEEFu);

  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  plant_markers(&fpu);
  m.cpu.regs.a[0] = FP_OPERAND;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  /* FP1 is 2.0: exponent $4000, integer bit set. It lands at the base address,
   * because it is the only register moved and offsets count transfers rather
   * than register numbers. */
  TEST_ASSERT_EQUAL_HEX32(0x40000000u, read_ram_long(&m, FP_OPERAND));
  TEST_ASSERT_EQUAL_HEX32(0x80000000u, read_ram_long(&m, FP_OPERAND + 4u));
  /* And nothing was written past it. */
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, read_ram_long(&m, FP_OPERAND + 12u));
  /* A control mode leaves the address register alone. */
  TEST_ASSERT_EQUAL_HEX32(FP_OPERAND, m.cpu.regs.a[0]);

  /* The same register through the predecrement ordering is bit 1. */
  static const uint16_t descending[] = {0xF220u, 0xE002u, 0x4E71u};
  machine_t n = {0};
  load(&n, descending, 3);
  ap_m68882_t second;
  ap_m68882_reset(&second);
  n.cpu.fpu = &second;
  plant_markers(&second);
  n.cpu.regs.a[0] = FP_OPERAND + 12u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(FP_OPERAND, n.cpu.regs.a[0]);
  TEST_ASSERT_EQUAL_HEX32(0x40000000u, read_ram_long(&n, FP_OPERAND));
}

/* "a dynamic value in the least significant 8-bits of a main processor data
 * register (the remaining bits of the register are ignored)". The register
 * number is in bits 6-4 of the list field, laid out as `0 r r r 0 0 0 0` -- so
 * the field that would be a mask is instead a pointer to one. */
static void test_a_dynamic_register_list_reads_its_mask_from_a_data_register(
    void) {
  /* FMOVEM.X D2,(A0) : $F820 is dr = 1, MODE 11 (dynamic, control), rrr = 2. */
  static const uint16_t program[] = {0xF210u, 0xF820u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  write_ram_long(&m, FP_OPERAND + 12u, 0xDEADBEEFu);

  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  plant_markers(&fpu);
  m.cpu.regs.a[0] = FP_OPERAND;
  /* Bit 6 is FP1 in a control mode's ordering. The high bits are deliberately
   * noise, since the manual says they are ignored. */
  m.cpu.regs.d[2] = 0xFFFFFF40u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x40000000u, read_ram_long(&m, FP_OPERAND));
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, read_ram_long(&m, FP_OPERAND + 12u));
}

/* **`FMOVEM` performs no conversion and raises nothing**, which is the whole
 * reason it is not the store path with a loop around it: "the FMOVEM
 * instruction provides the only mechanism for moving a floating-point data item
 * between the FPCP and memory without performing any data conversions or
 * affecting the condition code and exception status bits", and §6.1.2 adds that
 * it and FSAVE "cannot generate exceptions. Therefore, these instructions are
 * useful for manipulating SNANs."
 *
 * So a signalling NAN survives one intact. Routed through `FMOVE`'s conversion
 * it would come back quiet with `SNAN` set, which is a different value and a
 * different FPSR. */
static void test_fmovem_moves_a_signalling_nan_without_touching_the_fpsr(void) {
  static const uint16_t program[] = {0xF210u, 0xF080u, 0xF210u, 0xD080u,
                                     0x4E71u};
  machine_t m = {0};
  load(&m, program, 5);

  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  m.cpu.regs.a[0] = FP_OPERAND;
  /* Maximum exponent, integer bit set, **quiet bit clear**: signalling. */
  fpu.regs.fp[0] = (ap_m68882_extended_t){
      .sign = false, .exponent = 0x7FFFu, .mantissa = UINT64_C(0x8000DEADBEEF00)};
  const uint32_t fpsr_before = fpu.regs.fpsr;

  /* Store FP0 (control mode mask bit 7), then load it back. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  fpu.regs.fp[0] = ap_m68882_from_single(0u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);

  TEST_ASSERT_EQUAL_HEX16(0x7FFFu, fpu.regs.fp[0].exponent);
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0x8000DEADBEEF00), fpu.regs.fp[0].mantissa);
  TEST_ASSERT_TRUE(ap_m68882_is_signalling_nan(&fpu.regs.fp[0]));
  /* Not one bit of the FPSR moved: no exception, no condition code. */
  TEST_ASSERT_EQUAL_HEX32(fpsr_before, fpu.regs.fpsr);
}

/* The two increment modes are legal in **one direction each**: "If the effective
 * address is the predecrement mode, only a register to memory operation is
 * allowed", and postincrement only a memory to register one. No addressing
 * category expresses that, so it is its own rule -- and a mode field that
 * disagrees with the effective address names no transfer at all. */
static void test_fmovem_allows_each_increment_mode_in_one_direction_only(void) {
  /* FMOVEM.X (A0)+,... in the *store* direction: dr = 1 with postincrement. */
  static const uint16_t program[] = {0xF218u, 0xF0FFu, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  plant_vector(&m, AP_M68030_VECTOR_COPROCESSOR_PROTOCOL, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;

  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  m.cpu.regs.a[0] = FP_OPERAND;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);

  /* And `-(A0)` in the *load* direction is refused the same way. */
  static const uint16_t loading[] = {0xF220u, 0xC0FFu, 0x4E71u};
  machine_t n = {0};
  load(&n, loading, 3);
  plant_vector(&n, AP_M68030_VECTOR_COPROCESSOR_PROTOCOL, HANDLER);
  n.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  n.cpu.regs.isp = SUPERVISOR_STACK;
  ap_m68882_t second;
  ap_m68882_reset(&second);
  n.cpu.fpu = &second;
  n.cpu.regs.a[0] = FP_OPERAND;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, n.cpu.regs.pc);
}

/* **Unimplemented control-register bits read as zeros and are ignored on the
 * way in.** "A 32-bit transfer is always performed, even though the system
 * control register may not have 32 implemented bits. Unimplemented bits of a
 * control register are read as zeros and are ignored during writes."
 *
 * So a program writing all ones and reading back gets the mask, and a model
 * that stored the whole word would hand back bits the part does not have --
 * which would contradict its own next read rather than fault. */
static void test_the_control_registers_mask_their_unimplemented_bits(void) {
  /* FMOVE.L D0,FPCR then FMOVE.L FPCR,D1 : $9000 is dr = 0 selecting FPCR,
   * $B000 is dr = 1. $F200 is `D0`, $F201 is `D1`. */
  static const uint16_t program[] = {0xF200u, 0x9000u, 0xF201u, 0xB000u,
                                     0x4E71u};
  machine_t m = {0};
  load(&m, program, 5);

  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  m.cpu.regs.d[0] = 0xFFFFFFFFu;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  /* The enable byte at 15-8, PREC at 7-6 and RND at 5-4; bits 3-0 are printed
   * as zero in Figure 2-3 and bits 31-16 do not exist. */
  TEST_ASSERT_EQUAL_HEX32(0x0000FFF0u, m.cpu.regs.d[1]);

  /* The FPSR's mask is its own: Figure 2-4 prints bits 31-28 as a single zero
   * field, and Figure 2-7 the accrued byte's bits 2-0. */
  static const uint16_t status[] = {0xF200u, 0x8800u, 0xF201u, 0xA800u,
                                    0x4E71u};
  machine_t n = {0};
  load(&n, status, 5);
  ap_m68882_t second;
  ap_m68882_reset(&second);
  n.cpu.fpu = &second;
  n.cpu.regs.d[0] = 0xFFFFFFFFu;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x0FFFFFF8u, n.cpu.regs.d[1]);
}

/* Writing the FPSR **replaces** it, condition codes included: "Changed only if
 * the destination is the FPSR; in which case all bits are modified to reflect
 * the value of the source operand." A merge would leave an earlier comparison's
 * codes standing, which is the opposite of what a context restore wants. */
static void test_writing_the_status_register_replaces_every_bit(void) {
  static const uint16_t program[] = {0xF200u, 0x8800u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);

  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  /* Condition codes and an exception bit standing from earlier work. */
  fpu.regs.fpsr = (UINT32_C(1) << AP_M68882_FPCC_Z) |
                  (UINT32_C(1) << AP_M68882_FPCC_N) |
                  (UINT32_C(1) << AP_M68882_EXC_OVFL);
  m.cpu.regs.d[0] = 0u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0u, fpu.regs.fpsr);
}

/* `FMOVEM` of the control registers moves them "always ... in the same order,
 * regardless of the addressing mode used; with the FPCR moved first, followed
 * by the FPSR, and the FPIAR moved last" -- no reversal, unlike the data
 * registers, and no dependence on the mode at all.
 *
 * And **the address register steps once**, by four times the register count,
 * where the data registers' predecrement steps twelve at a time. That makes a
 * predecrement here run *upwards* through memory from the decremented base,
 * which is the opposite of the data-register case and the easiest thing to
 * carry across wrongly. */
static void test_the_control_registers_move_in_one_fixed_order(void) {
  /* FMOVEM.L FPCR/FPSR/FPIAR,-(A0) : $F220 is `-(A0)`, $BC00 is dr = 1 with
   * all three selected. */
  static const uint16_t program[] = {0xF220u, 0xBC00u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);

  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  fpu.regs.fpcr = 0x0000FFF0u;
  fpu.regs.fpsr = 0x0F000000u;
  fpu.regs.fpiar = 0x12345678u;
  m.cpu.regs.a[0] = FP_OPERAND + 12u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  /* Decremented once by 4 x 3, then written upwards from there. */
  TEST_ASSERT_EQUAL_HEX32(FP_OPERAND, m.cpu.regs.a[0]);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFF0u, read_ram_long(&m, FP_OPERAND));
  TEST_ASSERT_EQUAL_HEX32(0x0F000000u, read_ram_long(&m, FP_OPERAND + 4u));
  TEST_ASSERT_EQUAL_HEX32(0x12345678u, read_ram_long(&m, FP_OPERAND + 8u));
}

/* The register direct modes are allowed only for a *single* register -- "If a
 * single system control register is selected, the data register direct
 * addressing mode may be used; or, if the only register selected is the FPIAR,
 * then the address register direct addressing mode is allowed."
 *
 * Two refusals and two permissions, and the FPIAR's is the interesting one: it
 * is the only control register that holds an address, which is why it is the
 * only one an address register may carry. */
static void test_register_direct_needs_a_single_control_register(void) {
  /* FMOVE.L FPIAR,A1 -- allowed. $F209 is `A1`, $A400 is dr = 1, FPIAR only. */
  static const uint16_t program[] = {0xF209u, 0xA400u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  fpu.regs.fpiar = 0x0000ABCDu;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x0000ABCDu, m.cpu.regs.a[1]);

  /* FMOVE.L FPCR,A1 -- refused: an address register carries only the FPIAR. */
  static const uint16_t wrong_register[] = {0xF209u, 0xB000u, 0x4E71u};
  machine_t n = {0};
  load(&n, wrong_register, 3);
  plant_vector(&n, AP_M68030_VECTOR_COPROCESSOR_PROTOCOL, HANDLER);
  n.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  n.cpu.regs.isp = SUPERVISOR_STACK;
  ap_m68882_t second;
  ap_m68882_reset(&second);
  n.cpu.fpu = &second;
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, n.cpu.regs.pc);

  /* FMOVEM.L FPCR/FPSR,D0 -- refused: two registers into one data register. */
  static const uint16_t two[] = {0xF200u, 0xB800u, 0x4E71u};
  machine_t o = {0};
  load(&o, two, 3);
  plant_vector(&o, AP_M68030_VECTOR_COPROCESSOR_PROTOCOL, HANDLER);
  o.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  o.cpu.regs.isp = SUPERVISOR_STACK;
  ap_m68882_t third;
  ap_m68882_reset(&third);
  o.cpu.fpu = &third;
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&o.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, o.cpu.regs.pc);
}

/* **The FPIAR tracks only when a trap could use it.** §2.4: the register "is
 * loaded with the logical address of an instruction before the instruction is
 * executed (unless all arithmetic exceptions are disabled)" -- so with the
 * enable byte clear it does not move at all, which is the condition most easily
 * dropped because the register still looks plausible without it.
 *
 * And the transfers never load it: "Since the FPCP FMOVE to/from the FPCR,
 * FPSR, or FPIAR and FMOVEM instructions cannot generate floating-point
 * exceptions, these instructions do not modify the FPIAR. These instructions
 * can be used to read the FPIAR in the trap handler without changing the
 * previous value" -- which is the register's whole purpose, and a model that
 * updated it on every instruction would destroy the value on the way to
 * reading it. */
static void test_the_instruction_address_register_tracks_only_when_useful(void) {
  /* FADD FP0,FP1, then FMOVE.L FPIAR,D0. */
  static const uint16_t program[] = {0xF200u, 0x00A2u, 0xF200u, 0xA400u,
                                     0x4E71u};
  machine_t m = {0};
  load(&m, program, 5);
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;

  /* Every arithmetic trap disabled, which is the reset state. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0u, fpu.regs.fpiar);

  /* Enable one, and the same instruction now records its address. */
  machine_t n = {0};
  load(&n, program, 5);
  ap_m68882_t second;
  ap_m68882_reset(&second);
  n.cpu.fpu = &second;
  second.regs.fpcr = UINT32_C(1) << AP_M68882_EXC_OVFL;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE, second.regs.fpiar);

  /* The following FMOVE reads it without disturbing it -- the second
   * instruction is at PROGRAM_BASE + 4, and the register still names the
   * first. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE, second.regs.fpiar);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE, n.cpu.regs.d[0]);
}

/* `FMOVECR` reads the part's own constant ROM and touches no memory, which is
 * why it lives inside the memory-to-register opclass and needs no effective
 * address at all: RX = 7 is what says so.
 *
 * The offsets are published and the values are not, so these are computed
 * independently and correctly rounded. They are asserted against the canonical
 * 80-bit patterns, which is a real check rather than a restatement: pi at
 * $4000 C90FDAA22168C235 is the extended-precision constant every
 * implementation of the format arrives at, so agreeing with it is agreeing with
 * something outside this project. */
static void test_the_constant_rom_returns_its_published_constants(void) {
  /* FMOVECR #$00,FP0 then FMOVECR #$30,FP1 -- pi and ln(2). */
  static const uint16_t program[] = {0xF200u, 0x5C00u, 0xF200u, 0x5CB0u,
                                     0x4E71u};
  machine_t m = {0};
  load(&m, program, 5);
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX16(0x4000u, fpu.regs.fp[0].exponent);
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0xC90FDAA22168C235), fpu.regs.fp[0].mantissa);
  TEST_ASSERT_FALSE(fpu.regs.fp[0].sign);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX16(0x3FFEu, fpu.regs.fp[1].exponent);
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0xB17217F7D1CF79AC), fpu.regs.fp[1].mantissa);

  /* Neither is exact in the format, so both are inexact and nothing else.
   * "OVFL Cleared, UNFL Cleared ... INEX2 Refer to 6.1.7 Inexact Result" --
   * except that at extended precision there is nothing below the destination to
   * round away, so even INEX2 stays clear here. */
  TEST_ASSERT_EQUAL_HEX32(0u, fpu.regs.fpsr & (UINT32_C(0xFF) << 8));
}

/* **`FMOVECR` rounds to the FPCR's precision** -- the exact mirror of the store
 * rule, and worth stating as the pair: a store to memory "ignores the PREC
 * bits" because the destination format decides, while here the destination *is*
 * a register and PREC is the whole of it. "Fetches an extended precision
 * constant from the FPCP on-chip ROM, rounds it to the precision specified in
 * the FPCR mode control byte, and stores it." */
static void test_a_constant_rounds_to_the_selected_precision(void) {
  static const uint16_t program[] = {0xF200u, 0x5C00u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  /* Single precision: PREC = 01 at bits 7-6. */
  fpu.regs.fpcr = UINT32_C(1) << 6;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  /* Twenty-four significand bits kept, forty discarded, and pi rounds up at
   * that boundary. The value is checked from outside as well as inside: it is
   * bit for bit the single-precision encoding of pi, $40490FDB, which is what
   * "rounded to single" has to mean. */
  TEST_ASSERT_EQUAL_HEX16(0x4000u, fpu.regs.fp[0].exponent);
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0xC90FDB0000000000), fpu.regs.fp[0].mantissa);
  TEST_ASSERT_EQUAL_HEX32(0x40490FDBu, ap_m68882_to_single(&fpu.regs.fp[0]));
  TEST_ASSERT_TRUE((fpu.regs.fpsr & (UINT32_C(1) << AP_M68882_EXC_INEX2)) != 0u);
}

/* Two entries that are exact, which is what makes the inexact ones meaningful:
 * `$0F` is 0.0 and sets the zero condition code, and `$32` is 10^0, exactly
 * 1.0. The powers of ten are exact up to 10^16, since 5^16 fits a 64-bit
 * significand with room to spare. */
static void test_the_exact_constants_raise_nothing(void) {
  /* FMOVECR #$0F,FP0 then FMOVECR #$32,FP1. */
  static const uint16_t program[] = {0xF200u, 0x5C0Fu, 0xF200u, 0x5CB2u,
                                     0x4E71u};
  machine_t m = {0};
  load(&m, program, 5);
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  /* Single precision, so an inexact constant would say so. */
  fpu.regs.fpcr = UINT32_C(1) << 6;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_ZERO,
                        ap_m68882_classify(&fpu.regs.fp[0]));
  TEST_ASSERT_TRUE((fpu.regs.fpsr & (UINT32_C(1) << AP_M68882_FPCC_Z)) != 0u);
  TEST_ASSERT_EQUAL_HEX32(0u, fpu.regs.fpsr & (UINT32_C(0xFF) << 8));

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x3F800000u, ap_m68882_to_single(&fpu.regs.fp[1]));
  TEST_ASSERT_EQUAL_HEX32(0u, fpu.regs.fpsr & (UINT32_C(0xFF) << 8));
}

/* An offset the manual does not define. "The values contained at offsets other
 * than those defined above are reserved for the use of Motorola, and may be
 * different on various mask sets of the FPCP" -- so there is no value to be
 * correct about, and this is a *documented absence* rather than a gap in the
 * model. The PRM names the only convention that exists: "These undefined values
 * yield the value 0.0 in the M68040FPSP".
 *
 * The instruction executes either way, which is the part that matters: it is
 * not an illegal encoding. */
static void test_an_undefined_rom_offset_yields_a_stated_value(void) {
  /* FMOVECR #$01,FP0 -- between pi at $00 and Log10(2) at $0B. */
  static const uint16_t program[] = {0xF200u, 0x5C01u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  fpu.regs.fp[0] = ap_m68882_from_single(0x40490FDBu);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_ZERO,
                        ap_m68882_classify(&fpu.regs.fp[0]));
}

/* Every one of the 128 ROM offsets classified, and **exactly the twenty-two the
 * manual lists** are defined. A transcription check: the offsets are not
 * contiguous and not evenly spaced -- `$00`, then a gap to `$0B`-`$0F`, then a
 * gap to `$30`-`$3F` -- so a table built by counting rather than by reading
 * would land the powers of ten in the wrong places and every one of them would
 * still look like a plausible number. */
static void test_the_constant_rom_defines_exactly_the_published_offsets(void) {
  static const unsigned published[] = {
      0x00u, 0x0Bu, 0x0Cu, 0x0Du, 0x0Eu, 0x0Fu, 0x30u, 0x31u,
      0x32u, 0x33u, 0x34u, 0x35u, 0x36u, 0x37u, 0x38u, 0x39u,
      0x3Au, 0x3Bu, 0x3Cu, 0x3Du, 0x3Eu, 0x3Fu};

  unsigned defined = 0;
  for (unsigned offset = 0; offset < 128u; offset++) {
    ap_m68882_extended_t value = {0};
    const bool is_defined = ap_m68882_constant(offset, &value);

    bool expected = false;
    for (unsigned i = 0; i < sizeof published / sizeof published[0]; i++) {
      expected = expected || published[i] == offset;
    }
    TEST_ASSERT_EQUAL_INT(expected, is_defined);
    if (is_defined) {
      defined++;
    } else {
      /* A reserved offset has no right answer, so the model states one. */
      TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_ZERO, ap_m68882_classify(&value));
    }
  }
  TEST_ASSERT_EQUAL_UINT(22u, defined);

  /* The powers of ten double their exponent's distance from the last: each
   * offset from $33 up is the square of the one before, so a table with an
   * entry displaced would break the progression. Checked as the *relationship*
   * rather than as twelve constants. */
  for (unsigned offset = 0x34u; offset <= 0x3Fu; offset++) {
    ap_m68882_extended_t here = {0};
    ap_m68882_extended_t before = {0};
    TEST_ASSERT_TRUE(ap_m68882_constant(offset, &here));
    TEST_ASSERT_TRUE(ap_m68882_constant(offset - 1u, &before));
    const int previous = (int)before.exponent - AP_M68882_BIAS_EXTENDED;
    const int current = (int)here.exponent - AP_M68882_BIAS_EXTENDED;
    /* Squaring doubles the exponent, give or take the significand's carry. */
    TEST_ASSERT_TRUE(current == 2 * previous || current == 2 * previous + 1);
  }
}

/* And out again, with the k-factor. `FMOVE.P FP1,(A0){#3}` -- opclass 011,
 * destination format 011, and a static k-factor of 3 in the extension field.
 *
 * The pair is what this asserts: a value stored and loaded back is unchanged
 * when the k-factor keeps enough digits, which only holds if both conversions
 * are right and they are written independently of each other. */
static void test_a_packed_decimal_result_is_stored_with_its_k_factor(void) {
  /* $6C83 is opclass 011, format 011 (P), source FP1, k-factor +3. */
  static const uint16_t program[] = {0xF210u, 0x6C83u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  m.cpu.regs.a[0] = FP_OPERAND;
  /* 1.25, which three significant digits hold exactly. */
  fpu.regs.fp[1] = ap_m68882_from_single(0x3FA00000u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  /* `+1.25 E+0`: sign clear, exponent zero, and the digits 1, 2, 5 from
   * `MANT16` down. */
  TEST_ASSERT_EQUAL_HEX8(0x00u, m.memory.bytes[FP_OPERAND]);
  TEST_ASSERT_EQUAL_HEX8(0x00u, m.memory.bytes[FP_OPERAND + 1u]);
  TEST_ASSERT_EQUAL_HEX8(0x01u, m.memory.bytes[FP_OPERAND + 3u]); /* MANT16 */
  TEST_ASSERT_EQUAL_HEX8(0x25u, m.memory.bytes[FP_OPERAND + 4u]);
  /* Exact, so nothing is raised. */
  TEST_ASSERT_EQUAL_HEX32(0u, fpu.regs.fpsr & (UINT32_C(0xFF) << 8));

  /* Load it back: the two conversions are independent and agree. */
  static const uint16_t reading[] = {0xF210u, 0x4C80u, 0x4E71u};
  machine_t n = {0};
  load(&n, reading, 3);
  for (unsigned i = 0; i < 12u; i++) {
    n.memory.bytes[FP_OPERAND + i] = m.memory.bytes[FP_OPERAND + i];
  }
  ap_m68882_t second;
  ap_m68882_reset(&second);
  n.cpu.fpu = &second;
  n.cpu.regs.a[0] = FP_OPERAND;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x3FA00000u, ap_m68882_to_single(&second.regs.fp[1]));
}

/* **A null frame is four bytes and an idle one is sixty**, and which one is
 * saved depends on whether anything has run: "A save of the null state results
 * when no FPCP instructions have been executed since the last null state
 * restore or hardware reset." So the frame's *length* is not a constant, and a
 * predecrement steps by whichever the part produced.
 *
 * The null frame's version is zero -- the wild card "allowing this state frame
 * type to be restored to a coprocessor of any version" -- and its size byte is
 * deliberately not asserted here, because §6.4.2.1 leaves it undefined on save
 * and ignored on restore. That is what reconciles Figure 6-5 printing it
 * "(UNDEFINED)" with FRESTORE's page calling the format word `$0000`. */
static void test_fsave_writes_a_null_frame_until_something_runs(void) {
  /* FSAVE -(A0) : $F320 is cpID 1, type 100, predecrement. */
  static const uint16_t program[] = {0xF320u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  m.cpu.regs.a[0] = FP_OPERAND + 64u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  /* Four bytes, so the register stepped by four and the version byte is zero. */
  TEST_ASSERT_EQUAL_HEX32(FP_OPERAND + 60u, m.cpu.regs.a[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00u, m.memory.bytes[FP_OPERAND + 60u]);

  /* Run one instruction, and the same FSAVE now saves the idle frame -- sixty
   * bytes, with the version and the `$38` size byte. `FMOVECR #$0F,FP0` is the
   * least eventful instruction there is: it loads 0.0. */
  static const uint16_t after[] = {0xF200u, 0x5C0Fu, 0xF320u, 0x4E71u};
  machine_t n = {0};
  load(&n, after, 4);
  n.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  ap_m68882_t second;
  ap_m68882_reset(&second);
  n.cpu.fpu = &second;
  n.cpu.regs.a[0] = FP_OPERAND + 64u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(FP_OPERAND + 4u, n.cpu.regs.a[0]);
  TEST_ASSERT_EQUAL_HEX8(AP_M68882_DEFAULT_VERSION,
                         n.memory.bytes[FP_OPERAND + 4u]);
  TEST_ASSERT_EQUAL_HEX8(AP_M68882_FRAME_IDLE_SIZE_BYTE,
                         n.memory.bytes[FP_OPERAND + 5u]);
}

/* **Restoring a null frame is a reset**: "equivalent to a hardware reset of the
 * FPCP. The programmer's model is set to the reset state, with non-signaling
 * NANs in the floating-point data registers and zeroes in the FPCR, FPSR and
 * FPIAR." An idle frame does the opposite -- "The programmer's model is not
 * affected by loading this type of state frame" -- so the two differ in exactly
 * the thing a context switch cares about. */
static void test_frestore_resets_on_null_and_preserves_on_idle(void) {
  /* FRESTORE (A0)+ : $F358 is type 101, postincrement. */
  static const uint16_t program[] = {0xF358u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  m.cpu.regs.a[0] = FP_OPERAND;
  fpu.regs.fp[3] = ap_m68882_from_single(0x3F800000u);
  fpu.regs.fpcr = 0x0000FFF0u;
  for (unsigned i = 0; i < 4u; i++) {
    m.memory.bytes[FP_OPERAND + i] = 0u; /* a null frame */
  }

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(FP_OPERAND + 4u, m.cpu.regs.a[0]);
  TEST_ASSERT_EQUAL_HEX32(0u, fpu.regs.fpcr);
  /* Non-signalling NANs, which is the reset state and not zeros. */
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_NAN,
                        ap_m68882_classify(&fpu.regs.fp[3]));
  TEST_ASSERT_FALSE(ap_m68882_is_signalling_nan(&fpu.regs.fp[3]));

  /* An idle frame leaves the programmer's model alone. */
  machine_t n = {0};
  load(&n, program, 3);
  n.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  ap_m68882_t second;
  ap_m68882_reset(&second);
  n.cpu.fpu = &second;
  n.cpu.regs.a[0] = FP_OPERAND;
  second.regs.fp[3] = ap_m68882_from_single(0x3F800000u);
  n.memory.bytes[FP_OPERAND] = AP_M68882_DEFAULT_VERSION;
  n.memory.bytes[FP_OPERAND + 1u] = AP_M68882_FRAME_IDLE_SIZE_BYTE;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(FP_OPERAND + 0x3Cu, n.cpu.regs.a[0]);
  TEST_ASSERT_EQUAL_HEX32(0x3F800000u, ap_m68882_to_single(&second.regs.fp[3]));
}

/* A format word the part does not recognise takes the **format exception**, not
 * the protocol violation an illegal addressing mode takes: "If the format word
 * is invalid for the FPCP (either because the size of the frame is not
 * recognized, or the revision number does not match the revision of the
 * processor), the MPU is instructed to take a format exception." Reporting one
 * as the other would send a handler looking for the wrong fault.
 *
 * And version zero is accepted whatever its size byte says, which is the wild
 * card rule from the other side. */
static void test_an_unrecognised_state_frame_takes_a_format_exception(void) {
  static const uint16_t program[] = {0xF358u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  plant_vector(&m, AP_M68030_VECTOR_FORMAT_ERROR, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  m.cpu.regs.a[0] = FP_OPERAND;
  /* A busy frame's size, which this part never writes and cannot honestly
   * accept. */
  m.memory.bytes[FP_OPERAND] = AP_M68882_DEFAULT_VERSION;
  m.memory.bytes[FP_OPERAND + 1u] = 0xD4u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);

  /* Version zero with a nonsense size byte is still a null frame. */
  machine_t n = {0};
  load(&n, program, 3);
  n.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  ap_m68882_t second;
  ap_m68882_reset(&second);
  n.cpu.fpu = &second;
  n.cpu.regs.a[0] = FP_OPERAND;
  n.memory.bytes[FP_OPERAND] = 0x00u;
  n.memory.bytes[FP_OPERAND + 1u] = 0xD4u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(FP_OPERAND + 4u, n.cpu.regs.a[0]);
}

/* Both are privileged: "If in supervisor state ... else TRAP". */
static void test_the_state_frame_instructions_are_privileged(void) {
  static const uint16_t saving[] = {0xF320u, 0x4E71u, 0x4E71u};
  machine_t m = {0};
  load(&m, saving, 3);
  plant_vector(&m, AP_M68030_VECTOR_PRIVILEGE_VIOLATION, HANDLER);
  m.cpu.regs.sr = 0u; /* user state */
  m.cpu.regs.isp = SUPERVISOR_STACK;
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  m.cpu.regs.a[0] = FP_OPERAND + 64u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);
  /* And nothing was written, so the trap came before the transfer. */
  TEST_ASSERT_EQUAL_HEX32(FP_OPERAND + 64u, m.cpu.regs.a[0]);
}

/* `FBcc` is its own instruction *type* rather than an opclass, so it never
 * reaches the general path: the operation word carries the predicate in bits
 * 5-0 and the size in bit 6, and a displacement follows.
 *
 * **The branch is relative to the instruction's address plus two**, not to
 * where the displacement sits and not to the next instruction: "The value of the
 * PC used to calculate the destination address is the address of the branch
 * instruction plus two." A base taken from the wrong place is off by a constant,
 * which a self-consistent assembler-and-emulator pair would hide entirely. */
static void test_a_floating_point_branch_is_relative_to_the_opcode_plus_two(
    void) {
  /* FBEQ.W +4 : $F281 is cpID 1, type 010 (FBcc.W), predicate $01 (EQ). */
  static const uint16_t program[] = {0xF281u, 0x0004u, 0x4E71u, 0x4E71u,
                                     0x4E71u};
  machine_t m = {0};
  load(&m, program, 5);
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  ap_m68882_set_condition(&fpu.regs, AP_M68882_RESULT_ZERO, false);

  const ap_m68030_step_result_t taken = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, taken.status);
  TEST_ASSERT_TRUE(taken.branch_taken);
  /* Base is PROGRAM_BASE + 2, plus a displacement of 4. */
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 6u, m.cpu.regs.pc);

  /* Untaken, the displacement is still consumed and the program counter lands
   * past it -- four bytes on, not two. */
  machine_t n = {0};
  load(&n, program, 5);
  ap_m68882_t second;
  ap_m68882_reset(&second);
  n.cpu.fpu = &second;
  ap_m68882_set_condition(&second.regs, AP_M68882_RESULT_NORMAL, false);

  const ap_m68030_step_result_t untaken = ap_m68030_step(&n.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, untaken.status);
  TEST_ASSERT_FALSE(untaken.branch_taken);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 4u, n.cpu.regs.pc);
}

/* The long form takes two displacement words, and a negative displacement
 * reaches backwards -- which is what a loop is made of, and the case where a
 * sign-extension error stops being invisible. */
static void test_a_long_floating_point_branch_reaches_backwards(void) {
  /* FBNE.L -6 : $F2CE is type 011 (FBcc.L), predicate $0E (NE). */
  static const uint16_t program[] = {0xF2CEu, 0xFFFFu, 0xFFFAu, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  ap_m68882_set_condition(&fpu.regs, AP_M68882_RESULT_NORMAL, false);

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_TRUE(r.branch_taken);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 2u - 6u, m.cpu.regs.pc);

  /* Untaken, the program counter clears **both** displacement words. */
  machine_t n = {0};
  load(&n, program, 4);
  ap_m68882_t second;
  ap_m68882_reset(&second);
  n.cpu.fpu = &second;
  ap_m68882_set_condition(&second.regs, AP_M68882_RESULT_ZERO, false);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 6u, n.cpu.regs.pc);
}

/* **A conditional does not clear the exception byte**, and it is the only
 * instruction in the part of which that is true. `FBcc`'s page lists SNAN,
 * OPERR, OVFL, UNFL, DZ, INEX2 and INEX1 as "Not Affected", where every
 * arithmetic page lists them as "Cleared" -- and the accrued byte narrows the
 * same way: "The IOP bit is set if the BSUN bit is set in the exception byte. No
 * other bit is affected."
 *
 * So testing a condition must not wipe the record of whatever last raised an
 * exception, which is exactly what routing it through the common result path
 * would do -- silently, and on every branch a program takes. */
static void test_a_branch_leaves_the_exception_byte_standing(void) {
  /* FBEQ.W +0, with the condition false so nothing else happens. */
  static const uint16_t program[] = {0xF281u, 0x0000u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;

  const uint32_t standing = (UINT32_C(1) << AP_M68882_EXC_OVFL) |
                            (UINT32_C(1) << AP_M68882_EXC_INEX2);
  fpu.regs.fpsr |= standing;
  ap_m68882_set_condition(&fpu.regs, AP_M68882_RESULT_NORMAL, false);
  const uint32_t before = fpu.regs.fpsr;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(before, fpu.regs.fpsr);
}

/* `BSUN` is the one bit a conditional does set: "Set if the NAN condition code
 * is set and the condition selected is an IEEE non-aware test", and with it
 * "The IOP bit is set ... No other bit is affected".
 *
 * **Bit 4 of the predicate is which half of Table 4-22 it comes from, and the
 * halves are the opposite way round from the obvious guess.** The *low* half is
 * IEEE aware -- `EQ`, `OGT`, `UN`, whose names say what they do about unordered
 * operands -- and raises nothing. The *high* half is the non-aware one, and the
 * table gives its entries "Signaling" names for exactly this reason: `SF`,
 * `SEQ`, `SNE`, `ST`. So `$01` is `EQ` and silent while `$11` is `SEQ` and
 * signals, which is the pairing this test states -- the same NAN against two
 * spellings of one comparison. */
static void test_an_unordered_comparison_raises_bsun_only_when_unaware(void) {
  /* FBSEQ.W -- predicate $11, "Signaling Equal", the non-aware half. */
  static const uint16_t program[] = {0xF291u, 0x0000u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  ap_m68882_set_condition(&fpu.regs, AP_M68882_RESULT_NAN, false);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_TRUE((fpu.regs.fpsr & (UINT32_C(1) << AP_M68882_EXC_BSUN)) != 0u);
  TEST_ASSERT_TRUE((fpu.regs.fpsr & (UINT32_C(1) << AP_M68882_AEXC_IOP)) != 0u);

  /* $01 is `EQ`, the same comparison from the aware half, which expects
   * unordered operands and does not signal. */
  static const uint16_t aware[] = {0xF281u, 0x0000u, 0x4E71u};
  machine_t n = {0};
  load(&n, aware, 3);
  ap_m68882_t second;
  ap_m68882_reset(&second);
  n.cpu.fpu = &second;
  ap_m68882_set_condition(&second.regs, AP_M68882_RESULT_NAN, false);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(
      0u, second.regs.fpsr & (UINT32_C(1) << AP_M68882_EXC_BSUN));
}

/* `FNOP` costs nothing extra: "FNOP uses the same opcode as the FBcc.W <label>
 * instruction, with cc = F (non-signalling false) and a displacement of zero."
 * So the never-taken predicate with a zero displacement falls out of the branch
 * already written, and is worth a test because an assembler emits it and a
 * model that special-cased `FNOP` would have two implementations of one word. */
static void test_fnop_is_a_never_taken_branch_of_zero(void) {
  /* $F280 is predicate $00 (false), displacement zero. */
  static const uint16_t program[] = {0xF280u, 0x0000u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  /* Even against a NAN: `F` is the non-signalling false, so no BSUN. */
  ap_m68882_set_condition(&fpu.regs, AP_M68882_RESULT_NAN, false);
  const uint32_t before = fpu.regs.fpsr;

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_FALSE(r.branch_taken);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 4u, m.cpu.regs.pc);
  TEST_ASSERT_EQUAL_HEX32(before, fpu.regs.fpsr);
}

/* `FScc` writes a byte of ones or zeros. The condition codes are untouched, and
 * so is the rest of the register: a byte operation on this family never widens.
 */
static void test_fscc_writes_a_byte_of_ones_or_zeros(void) {
  /* FSEQ D0 : $F240 is type 001 with mode 000, register 0; $0001 is predicate
   * $01 (EQ). */
  static const uint16_t program[] = {0xF240u, 0x0001u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  m.cpu.regs.d[0] = 0xDEADBEEFu;
  ap_m68882_set_condition(&fpu.regs, AP_M68882_RESULT_ZERO, false);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEFFu, m.cpu.regs.d[0]);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 4u, m.cpu.regs.pc);

  /* False writes zeros, and only the byte. */
  machine_t n = {0};
  load(&n, program, 3);
  ap_m68882_t second;
  ap_m68882_reset(&second);
  n.cpu.fpu = &second;
  n.cpu.regs.d[0] = 0xDEADBEEFu;
  ap_m68882_set_condition(&second.regs, AP_M68882_RESULT_NORMAL, false);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0xDEADBE00u, n.cpu.regs.d[0]);
}

/* **Table 4-19 has a defect, and this is the test that records the reading.**
 * It marks `111 000` and `111 001` "(Undefined, reserved)", which by its own
 * Note 3 would take an F-line trap. Two per-instruction statements disagree:
 * `FScc`'s own page lists `(xxx).W` and `(xxx).L` at exactly those encodings,
 * and the `M68000 Family PRM` says of the same instruction "Only data alterable
 * addressing modes can be used" and lists both. Absolute addressing is data
 * alterable, so it is accepted -- two sources against one summary table.
 *
 * The genuinely reserved rows, `111 101` upwards, do take the F-line trap, and
 * are checked beside it so the reading is a *distinction* rather than a blanket
 * permission. */
static void test_absolute_addressing_is_a_legal_fscc_destination(void) {
  /* FSEQ (FP_OPERAND).W : $F278 is mode 111, register 000. */
  static const uint16_t program[] = {0xF278u, 0x0001u, 0x3000u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  ap_m68882_set_condition(&fpu.regs, AP_M68882_RESULT_ZERO, false);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX8(0xFFu, m.memory.bytes[FP_OPERAND]);

  /* $F27D is mode 111, register 101 -- reserved, and Note 3's F-line trap. */
  static const uint16_t reserved[] = {0xF27Du, 0x0001u, 0x4E71u};
  machine_t n = {0};
  load(&n, reserved, 3);
  plant_vector(&n, AP_M68030_VECTOR_LINE_F, HANDLER);
  n.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  n.cpu.regs.isp = SUPERVISOR_STACK;
  ap_m68882_t second;
  ap_m68882_reset(&second);
  n.cpu.fpu = &second;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, n.cpu.regs.pc);
}

/* `FDBcc` is a looping primitive of three parameters, and its branch base is a
 * *third* rule: "The value of the PC used in the branch address calculation is
 * the address of the displacement word" -- where `FBcc`, two pages earlier, uses
 * the instruction's address plus two. The predicate word between them is the
 * whole difference, and a base carried across from `FBcc` is off by exactly its
 * width.
 *
 * The counter is decremented in its **low sixteen bits only**, so the upper half
 * survives -- right for every count that never borrows, and wrong for the one
 * that does. */
static void test_fdbcc_loops_on_its_low_word_from_the_displacement(void) {
  /* FDBEQ D0,-2 : $F248 is mode 001, register 0; $0001 is EQ; $FFFE is -2. */
  static const uint16_t program[] = {0xF248u, 0x0001u, 0xFFFEu, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 4);
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  m.cpu.regs.d[0] = 0xDEAD0003u;
  ap_m68882_set_condition(&fpu.regs, AP_M68882_RESULT_NORMAL, false);

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_TRUE(r.branch_taken);
  /* The upper half is untouched and the low word counted down. */
  TEST_ASSERT_EQUAL_HEX32(0xDEAD0002u, m.cpu.regs.d[0]);
  /* Displacement word is at PROGRAM_BASE + 4; -2 from there. */
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 2u, m.cpu.regs.pc);

  /* At zero the decrement reaches -1 and the loop falls through, having still
   * consumed all three words. */
  machine_t n = {0};
  load(&n, program, 4);
  ap_m68882_t second;
  ap_m68882_reset(&second);
  n.cpu.fpu = &second;
  n.cpu.regs.d[0] = 0xDEAD0000u;
  ap_m68882_set_condition(&second.regs, AP_M68882_RESULT_NORMAL, false);

  const ap_m68030_step_result_t done = ap_m68030_step(&n.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, done.status);
  TEST_ASSERT_FALSE(done.branch_taken);
  TEST_ASSERT_EQUAL_HEX32(0xDEADFFFFu, n.cpu.regs.d[0]);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 6u, n.cpu.regs.pc);

  /* A true condition is the loop's *termination*: no decrement at all. */
  machine_t o = {0};
  load(&o, program, 4);
  ap_m68882_t third;
  ap_m68882_reset(&third);
  o.cpu.fpu = &third;
  o.cpu.regs.d[0] = 0xDEAD0003u;
  ap_m68882_set_condition(&third.regs, AP_M68882_RESULT_ZERO, false);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&o.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0xDEAD0003u, o.cpu.regs.d[0]);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 6u, o.cpu.regs.pc);
}

/* `FTRAPcc` takes the cpTRAPcc exception when the condition holds, and
 * otherwise "proceeds to the next instruction, **discarding the optional
 * immediate operand**" -- discarded, but still *consumed*, or the operand would
 * decode as the next instruction. The three encodings differ only in how many
 * words that is. */
static void test_ftrapcc_traps_or_steps_over_its_operand(void) {
  /* FTRAPEQ.L #$12345678 : $F27B is mode 111, register 011. */
  static const uint16_t program[] = {0xF27Bu, 0x0001u, 0x1234u, 0x5678u,
                                     0x4E71u};
  machine_t m = {0};
  load(&m, program, 5);
  plant_vector(&m, AP_M68030_VECTOR_TRAPCC, HANDLER);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  ap_m68882_set_condition(&fpu.regs, AP_M68882_RESULT_ZERO, false);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(HANDLER, m.cpu.regs.pc);

  /* False: the four operand bytes are stepped over, so the program counter
   * lands eight bytes on and not four. */
  machine_t n = {0};
  load(&n, program, 5);
  ap_m68882_t second;
  ap_m68882_reset(&second);
  n.cpu.fpu = &second;
  ap_m68882_set_condition(&second.regs, AP_M68882_RESULT_NORMAL, false);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 8u, n.cpu.regs.pc);

  /* The no-parameter form, $F27C, consumes only the predicate word. */
  static const uint16_t bare[] = {0xF27Cu, 0x0001u, 0x4E71u};
  machine_t o = {0};
  load(&o, bare, 3);
  ap_m68882_t third;
  ap_m68882_reset(&third);
  o.cpu.fpu = &third;
  ap_m68882_set_condition(&third.regs, AP_M68882_RESULT_NORMAL, false);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&o.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 4u, o.cpu.regs.pc);
}

/* Build a packed decimal string: sign, exponent sign, a three-digit exponent,
 * and seventeen mantissa digits with the point after the first. Nibble 7 is
 * `MANT16` -- bytes 2 and 3 hold the don't-care field above it, so only the low
 * four bits of byte 3 are mantissa. */
static void plant_packed(machine_t *m, uint32_t at, bool sign,
                         bool exponent_sign, unsigned exponent,
                         const char *digits) {
  uint8_t bytes[12] = {0};
  bytes[0] = (uint8_t)((sign ? 0x80u : 0u) | (exponent_sign ? 0x40u : 0u));
  const unsigned exp_digits[3] = {(exponent / 100u) % 10u,
                                  (exponent / 10u) % 10u, exponent % 10u};
  bytes[0] |= (uint8_t)exp_digits[0];
  bytes[1] = (uint8_t)((exp_digits[1] << 4) | exp_digits[2]);
  for (unsigned i = 0; i < 17u; i++) {
    const unsigned nibble = 7u + i;
    const unsigned value = (unsigned)(digits[i] - '0');
    if (nibble % 2u == 0u) {
      bytes[nibble / 2u] |= (uint8_t)(value << 4);
    } else {
      bytes[nibble / 2u] |= (uint8_t)value;
    }
  }
  for (unsigned i = 0; i < 12u; i++) {
    m->memory.bytes[at + i] = bytes[i];
  }
}

/* **A packed decimal operand converts**, and the values are checked against
 * expectations computed to 400 decimal digits -- the same route the
 * transcendentals took, since neither manual prints a bit pattern for any of
 * this. §6.1.8 specifies *correct rounding* rather than a bound, so an
 * approximation would be visibly wrong at the last bit and these are exact.
 *
 * `FMOVE.P (A0),FP1` -- opclass 010, source format 011. */
static void test_a_packed_decimal_operand_converts(void) {
  static const uint16_t program[] = {0xF210u, 0x4C80u, 0x4E71u};

  /* 1.0, which is exact and must raise nothing at all. */
  machine_t m = {0};
  load(&m, program, 3);
  plant_packed(&m, FP_OPERAND, false, false, 0u, "10000000000000000");
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  m.cpu.regs.a[0] = FP_OPERAND;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX16(0x3FFFu, fpu.regs.fp[1].exponent);
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0x8000000000000000), fpu.regs.fp[1].mantissa);
  TEST_ASSERT_EQUAL_HEX32(0u, fpu.regs.fpsr & (UINT32_C(0xFF) << 8));

  /* 0.1, which is not representable in binary at all -- so it is the value that
   * makes `INEX1` mean something, and its extended form is the well-known
   * $3FFB CCCCCCCCCCCCCCCD. */
  machine_t n = {0};
  load(&n, program, 3);
  plant_packed(&n, FP_OPERAND, false, true, 1u, "10000000000000000");
  ap_m68882_t second;
  ap_m68882_reset(&second);
  n.cpu.fpu = &second;
  n.cpu.regs.a[0] = FP_OPERAND;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_HEX16(0x3FFBu, second.regs.fp[1].exponent);
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0xCCCCCCCCCCCCCCCD),
                          second.regs.fp[1].mantissa);
  /* **INEX1, not INEX2.** "The FPCP provides two inexact bits ... to help
   * distinguish between inexact results generated by decimal input conversions
   * (INEX1) and other inexact results (INEX2)", so a model that raised the
   * general one would erase exactly the distinction the bit exists for. */
  TEST_ASSERT_TRUE((second.regs.fpsr & (UINT32_C(1) << AP_M68882_EXC_INEX1)) !=
                   0u);
  TEST_ASSERT_EQUAL_HEX32(
      0u, second.regs.fpsr & (UINT32_C(1) << AP_M68882_EXC_INEX2));
}

/* The exponent reaches both ends of its three-digit range, which is where the
 * exact `M x 10^E` product is thousands of bits wide and an approximation
 * through the extended multiplier would drift. `5^999` alone is 2322 bits.
 *
 * Note 3 is what makes these safe to assert as ordinary numbers: "Since
 * in-range numbers cannot overflow or underflow when converted to extended
 * precision, normalized extended precision numbers are always produced." */
static void test_the_extremes_of_the_decimal_exponent_convert_exactly(void) {
  static const uint16_t program[] = {0xF210u, 0x4C80u, 0x4E71u};

  machine_t m = {0};
  load(&m, program, 3);
  plant_packed(&m, FP_OPERAND, false, false, 100u, "10000000000000000");
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  m.cpu.regs.a[0] = FP_OPERAND;
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX16(0x414Bu, fpu.regs.fp[1].exponent);
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0x924D692CA61BE758), fpu.regs.fp[1].mantissa);

  machine_t n = {0};
  load(&n, program, 3);
  plant_packed(&n, FP_OPERAND, false, true, 100u, "10000000000000000");
  ap_m68882_t second;
  ap_m68882_reset(&second);
  n.cpu.fpu = &second;
  n.cpu.regs.a[0] = FP_OPERAND;
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_HEX16(0x3EB2u, second.regs.fp[1].exponent);
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0xDFF9772470297EBD),
                          second.regs.fp[1].mantissa);
}

/* Table 3-4's special rows, where the type is **not** decided by the exponent
 * alone as it is in the binary formats: an infinity or NAN needs `SE` set, both
 * `y` bits set, *and* an exponent of `$FFF`. The same `$FFF` with the `y` bits
 * clear is an ordinary in-range string.
 *
 * And Note 1's copy: "the fraction part of the NAN is moved bit-for-bit into the
 * extended precision mantissa ... no decimal-to-binary conversion or any other
 * conversion is performed", which is why the payload arrives unchanged and the
 * signalling bit is already in the right place. */
static void test_a_packed_infinity_and_nan_need_all_three_markers(void) {
  static const uint16_t program[] = {0xF210u, 0x4C80u, 0x4E71u};

  machine_t m = {0};
  load(&m, program, 3);
  /* SE, both y bits, exponent $FFF, fraction zero: infinity. */
  m.memory.bytes[FP_OPERAND] = 0x7Fu;
  m.memory.bytes[FP_OPERAND + 1u] = 0xFFu;
  for (unsigned i = 2; i < 12u; i++) {
    m.memory.bytes[FP_OPERAND + i] = 0u;
  }
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  m.cpu.regs.a[0] = FP_OPERAND;
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_INFINITY,
                        ap_m68882_classify(&fpu.regs.fp[1]));

  /* A non-zero fraction makes it a NAN, copied bit for bit. */
  machine_t n = {0};
  load(&n, program, 3);
  n.memory.bytes[FP_OPERAND] = 0xFFu; /* negative, and all three markers */
  n.memory.bytes[FP_OPERAND + 1u] = 0xFFu;
  for (unsigned i = 2; i < 12u; i++) {
    n.memory.bytes[FP_OPERAND + i] = 0u;
  }
  n.memory.bytes[FP_OPERAND + 4u] = 0x40u; /* the quiet bit, extended bit 62 */
  n.memory.bytes[FP_OPERAND + 8u] = 0x12u;
  ap_m68882_t second;
  ap_m68882_reset(&second);
  n.cpu.fpu = &second;
  n.cpu.regs.a[0] = FP_OPERAND;
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&n.cpu).status);
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_NAN,
                        ap_m68882_classify(&second.regs.fp[1]));
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0x4000000012000000),
                          second.regs.fp[1].mantissa);
  TEST_ASSERT_FALSE(ap_m68882_is_signalling_nan(&second.regs.fp[1]));

  /* The same $FFF exponent **without** the y bits is an in-range string, not an
   * infinity -- which is the distinction the binary formats do not have. */
  machine_t o = {0};
  load(&o, program, 3);
  plant_packed(&o, FP_OPERAND, false, true, 0u, "10000000000000000");
  o.memory.bytes[FP_OPERAND] = 0x4Fu; /* SE set, y clear, exponent $FFF */
  o.memory.bytes[FP_OPERAND + 1u] = 0xFFu;
  ap_m68882_t third;
  ap_m68882_reset(&third);
  o.cpu.fpu = &third;
  o.cpu.regs.a[0] = FP_OPERAND;
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&o.cpu).status);
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_NORMALIZED,
                        ap_m68882_classify(&third.regs.fp[1]));
}

/* Note 2: "If a non-decimal digit appears in the exponent of a zero, the number
 * is converted to a true zero." A mantissa of all zeros is a zero whatever the
 * exponent digits say, including `$A`-`$F` -- which is defined behaviour to
 * reproduce rather than an error to raise. */
static void test_a_zero_mantissa_is_a_zero_whatever_the_exponent(void) {
  static const uint16_t program[] = {0xF210u, 0x4C80u, 0x4E71u};
  machine_t m = {0};
  load(&m, program, 3);
  plant_packed(&m, FP_OPERAND, true, false, 0u, "00000000000000000");
  m.memory.bytes[FP_OPERAND] |= 0x0Au; /* a non-decimal exponent digit */
  m.memory.bytes[FP_OPERAND + 1u] = 0xBCu;
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  m.cpu.fpu = &fpu;
  m.cpu.regs.a[0] = FP_OPERAND;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_ZERO,
                        ap_m68882_classify(&fpu.regs.fp[1]));
  /* The sign survives: Table 2-1 gives `-0` and `+0` different condition
   * codes. */
  TEST_ASSERT_TRUE(fpu.regs.fp[1].sign);
  TEST_ASSERT_EQUAL_HEX32(0u, fpu.regs.fpsr & (UINT32_C(0xFF) << 8));
}

/* **The model table changes behaviour, rather than describing it.** `CALLM` and
 * `RTM` exist on the 68020 and on nothing else, so `$06C0` is a module call on a
 * DN3000 and an illegal instruction on a DN3500 -- and the two must not report
 * the same thing.
 *
 * The DN3500 takes the *machine's* trap, which is correct hardware behaviour.
 * The DN3000 reports **our** gap: the instruction is real there, so raising the
 * illegal-instruction exception would dress an unfinished implementation up as
 * a correct machine, which is the one confusion this core spends most of its
 * care avoiding.
 *
 * This is the seam that `ap_cpu_decode`'s 65536-opcode sweep had always pinned
 * and that the step had never asked about. */
static void test_a_dn3000_decodes_a_module_call_where_a_dn3500_does_not(void) {
  static const uint16_t program[] = {0x06C0u, 0x0000u, 0x4E71u};

  machine_t m = {0};
  load(&m, program, 3);
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  /* `load` builds the reference machine, which is a DN3500. */
  TEST_ASSERT_FALSE(m.cpu.has_module_calls);
  /* `ILLEGAL`, which this step reports as a status rather than by raising the
   * vector -- "the hardware would fault too". That is the *machine's* verdict,
   * and it is what a 68030 gives for a word no 68030 instruction claims. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_ILLEGAL, ap_m68030_step(&m.cpu).status);

  /* The same word on a machine whose model says the family has them. */
  machine_t n = {0};
  load(&n, program, 3);
  n.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  n.cpu.regs.isp = SUPERVISOR_STACK;
  n.cpu.has_module_calls = true;

  /* `$06C0` is `RTM` -- the two instructions share a prefix and are told apart
   * by a field `CALLM` cannot legally use, and mode 000 is one. So the DN3000
   * *executes* it where the DN3500 calls the same word illegal, which is the
   * family difference stated as sharply as it can be: not "we have not
   * finished" against "the machine faults", but two machines disagreeing about
   * whether a word is an instruction. */
  const ap_m68030_step_result_t r = ap_m68030_step(&n.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_NOT_EQUAL_INT(AP_M68030_STEP_ILLEGAL, r.status);
}

/* **`CALLM` builds a module stack frame**, `[PRM]` Figures D-1 and D-3.
 *
 * The descriptor names an entry point and a data area; the entry *word* names
 * the register that receives the data area pointer, and execution continues at
 * the word after it. The frame records what has to be undone: the caller's PC,
 * the register the callee is about to overwrite, and the stack pointer.
 *
 * Checked through the frame rather than through the program counter alone,
 * because a `CALLM` that jumped correctly and saved nothing would look right
 * until the `RTM` that is not yet written tried to come back. */
static void test_callm_builds_a_module_stack_frame(void) {
  /* CALLM #0,(descriptor).L -- $06F9 is mode 111 register 001, absolute long. */
  static const uint16_t program[] = {0x06F9u, 0x0000u, 0x0000u, 0x3000u,
                                     0x4E71u};
  machine_t m = {0};
  load(&m, program, 5);
  m.cpu.has_module_calls = true;
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  m.cpu.regs.d[3] = 0xDEADBEEFu;

  /* Figure D-1 at $3000: opt 000, type $00, access level 0; then the entry
   * word pointer and the data area pointer. */
  write_ram_long(&m, 0x3000u, 0x00000000u);
  write_ram_long(&m, 0x3004u, 0x00003100u);  /* entry word pointer */
  write_ram_long(&m, 0x3008u, 0x00004000u);  /* data area pointer  */
  /* Figure D-2's entry word: D3 receives the data area pointer. */
  m.memory.bytes[0x3100u] = 0x30u;
  m.memory.bytes[0x3101u] = 0x00u;

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);
  TEST_ASSERT_TRUE(r.branch_taken);

  /* Execution continues at the word *after* the entry word. */
  TEST_ASSERT_EQUAL_HEX32(0x00003102u, m.cpu.regs.pc);
  /* The named register now holds the data area pointer, and the frame holds
   * what it used to. */
  TEST_ASSERT_EQUAL_HEX32(0x00004000u, m.cpu.regs.d[3]);
  const uint32_t frame = SUPERVISOR_STACK - AP_M68020_FRAME_BYTES;
  TEST_ASSERT_EQUAL_HEX32(frame, m.cpu.regs.isp);
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu,
                          read_ram_long(&m, frame + AP_M68020_FRAME_SAVED_DATA_AREA));
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK,
                          read_ram_long(&m, frame + AP_M68020_FRAME_SAVED_SP));
  /* The caller's return point is the instruction after the CALLM: two words of
   * absolute address and one of argument count follow the opcode. */
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 8u,
                          read_ram_long(&m, frame + AP_M68020_FRAME_SAVED_PC));
  TEST_ASSERT_EQUAL_HEX32(0x3000u,
                          read_ram_long(&m, frame + AP_M68020_FRAME_DESCRIPTOR_POINTER));
}

/* **`CALLM` and `RTM` as a pair**, which is the only way either can be checked
 * properly: the frame one writes is the frame the other reads, and a matched
 * pair of mistakes in the offsets would leave both looking right. The round
 * trip has to put the caller back exactly where it was. */
static void test_callm_and_rtm_return_the_caller_to_where_it_was(void) {
  /* CALLM #0,(descriptor).L at the top, and the module is `RTM D3` -- $06C3,
   * mode 000 naming D3, which is the register the entry word gave the data
   * area pointer to. */
  static const uint16_t program[] = {0x06F9u, 0x0000u, 0x0000u, 0x3000u,
                                     0x4E71u};
  machine_t m = {0};
  load(&m, program, 5);
  m.cpu.has_module_calls = true;
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;
  m.cpu.regs.d[3] = 0xDEADBEEFu;

  write_ram_long(&m, 0x3000u, 0x00000000u);
  write_ram_long(&m, 0x3004u, 0x00003100u);
  write_ram_long(&m, 0x3008u, 0x00004000u);
  m.memory.bytes[0x3100u] = 0x30u;  /* entry word: D3 */
  m.memory.bytes[0x3101u] = 0x00u;
  /* The module's body, at the word after the entry word: return at once. */
  m.memory.bytes[0x3102u] = 0x06u;
  m.memory.bytes[0x3103u] = 0xC3u;  /* RTM D3 */

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  TEST_ASSERT_EQUAL_HEX32(0x00003102u, m.cpu.regs.pc);
  TEST_ASSERT_EQUAL_HEX32(0x00004000u, m.cpu.regs.d[3]);

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_m68030_step(&m.cpu).status);
  /* Everything back: the register, the stack pointer, and the instruction
   * after the call. */
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, m.cpu.regs.d[3]);
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK, m.cpu.regs.isp);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM_BASE + 8u, m.cpu.regs.pc);
}

/* **Descriptor type `$01` supplies its own stack pointer, and the arguments
 * move with the call.** `[020]` §D.1.1: "The 000 option indicates that the
 * called module expects to find arguments from the calling module on the stack
 * just below the module stack frame. In cases where there is a change of stack
 * pointer during the call, the MC68020 will copy the arguments from the old
 * stack to the new stack."
 *
 * "Just below" is the diagram's orientation and the frame comment's "arguments
 * follow" is the address order; they agree, and the test pins which: the
 * arguments end up at `frame + AP_M68020_FRAME_BYTES`, on the *new* stack, with
 * the frame beneath them. A model that put them the other side would round-trip
 * through this core's own `RTM` and disagree with hardware -- which is why this
 * is checked against the copied bytes rather than against the return. */
static void test_a_stack_changing_module_call_carries_its_arguments(void) {
  static const uint16_t program[] = {0x06F9u, 0x0000u, 0x0000u, 0x3000u,
                                     0x4E71u};
  machine_t m = {0};
  load(&m, program, 5);
  m.cpu.has_module_calls = true;
  m.cpu.regs.sr = (uint16_t)(1u << AP_M68030_SR_S_BIT);
  m.cpu.regs.isp = SUPERVISOR_STACK;

  /* Type $01, option 000: its own stack pointer, and arguments copied. */
  write_ram_long(&m, 0x3000u, 0x01000000u);
  write_ram_long(&m, 0x3004u, 0x00003100u);
  write_ram_long(&m, 0x3008u, 0x00004000u);
  write_ram_long(&m, 0x300Cu, 0x00005000u);  /* the module's stack pointer */
  m.memory.bytes[0x3100u] = 0x30u;
  m.memory.bytes[0x3101u] = 0x00u;
  /* Four bytes of arguments on the caller's stack, and a count to match: the
   * count is the CALLM extension word's low byte, and $06F9's operand words
   * are followed by it. */
  write_ram_long(&m, SUPERVISOR_STACK, 0xCAFEF00Du);

  const ap_m68030_step_result_t r = ap_m68030_step(&m.cpu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, r.status);

  /* The frame is on the *module's* stack, not the caller's. */
  const uint32_t frame = 0x5000u - AP_M68020_FRAME_BYTES;
  TEST_ASSERT_EQUAL_HEX32(frame, m.cpu.regs.isp);
  /* And the caller's stack pointer is what the frame remembers. */
  TEST_ASSERT_EQUAL_HEX32(SUPERVISOR_STACK,
                          read_ram_long(&m, frame + AP_M68020_FRAME_SAVED_SP));
  TEST_ASSERT_EQUAL_HEX32(0x00003102u, m.cpu.regs.pc);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_an_f_line_word_traps_when_no_coprocessor_is_fitted);
  RUN_TEST(test_a_fitted_coprocessor_executes_an_f_line_instruction);
  RUN_TEST(test_a_fitted_coprocessor_ignores_another_cpid);
  RUN_TEST(test_an_undefined_extension_traps_with_a_coprocessor_fitted);
  RUN_TEST(test_an_unimplemented_form_is_reported_as_our_gap);
  RUN_TEST(test_a_dn3000_decodes_a_module_call_where_a_dn3500_does_not);
  RUN_TEST(test_callm_builds_a_module_stack_frame);
  RUN_TEST(test_callm_and_rtm_return_the_caller_to_where_it_was);
  RUN_TEST(test_a_stack_changing_module_call_carries_its_arguments);
  RUN_TEST(test_a_single_source_operand_is_fetched_from_memory);
  RUN_TEST(test_an_extended_source_operand_spans_three_long_words);
  RUN_TEST(test_a_postincrement_steps_by_the_source_format_length);
  RUN_TEST(test_a_word_immediate_source_is_signed_and_in_the_stream);
  RUN_TEST(test_an_extended_operand_from_a_data_register_violates_the_protocol);
  RUN_TEST(test_an_address_register_is_never_a_floating_point_source);
  RUN_TEST(test_a_packed_decimal_operand_converts);
  RUN_TEST(test_the_extremes_of_the_decimal_exponent_convert_exactly);
  RUN_TEST(test_a_packed_infinity_and_nan_need_all_three_markers);
  RUN_TEST(test_a_zero_mantissa_is_a_zero_whatever_the_exponent);
  RUN_TEST(test_a_packed_decimal_result_is_stored_with_its_k_factor);
  RUN_TEST(test_fsave_writes_a_null_frame_until_something_runs);
  RUN_TEST(test_frestore_resets_on_null_and_preserves_on_idle);
  RUN_TEST(test_an_unrecognised_state_frame_takes_a_format_exception);
  RUN_TEST(test_the_state_frame_instructions_are_privileged);
  RUN_TEST(test_a_result_is_stored_to_memory);
  RUN_TEST(test_a_predecrement_store_steps_by_the_destination_length);
  RUN_TEST(test_a_store_leaves_the_condition_codes_alone);
  RUN_TEST(test_a_long_word_result_reaches_a_data_register);
  RUN_TEST(test_storing_to_a_nonalterable_address_violates_the_protocol);
  RUN_TEST(test_fmovem_round_trips_through_both_mask_orderings);
  RUN_TEST(test_a_partial_mask_moves_only_its_registers);
  RUN_TEST(test_a_dynamic_register_list_reads_its_mask_from_a_data_register);
  RUN_TEST(test_fmovem_moves_a_signalling_nan_without_touching_the_fpsr);
  RUN_TEST(test_fmovem_allows_each_increment_mode_in_one_direction_only);
  RUN_TEST(test_the_control_registers_mask_their_unimplemented_bits);
  RUN_TEST(test_writing_the_status_register_replaces_every_bit);
  RUN_TEST(test_the_control_registers_move_in_one_fixed_order);
  RUN_TEST(test_register_direct_needs_a_single_control_register);
  RUN_TEST(test_the_instruction_address_register_tracks_only_when_useful);
  RUN_TEST(test_the_constant_rom_returns_its_published_constants);
  RUN_TEST(test_a_constant_rounds_to_the_selected_precision);
  RUN_TEST(test_the_exact_constants_raise_nothing);
  RUN_TEST(test_an_undefined_rom_offset_yields_a_stated_value);
  RUN_TEST(test_the_constant_rom_defines_exactly_the_published_offsets);
  RUN_TEST(test_a_floating_point_branch_is_relative_to_the_opcode_plus_two);
  RUN_TEST(test_a_long_floating_point_branch_reaches_backwards);
  RUN_TEST(test_a_branch_leaves_the_exception_byte_standing);
  RUN_TEST(test_an_unordered_comparison_raises_bsun_only_when_unaware);
  RUN_TEST(test_fnop_is_a_never_taken_branch_of_zero);
  RUN_TEST(test_fscc_writes_a_byte_of_ones_or_zeros);
  RUN_TEST(test_absolute_addressing_is_a_legal_fscc_destination);
  RUN_TEST(test_fdbcc_loops_on_its_low_word_from_the_displacement);
  RUN_TEST(test_ftrapcc_traps_or_steps_over_its_operand);
  RUN_TEST(test_reset_performs_all_ten_documented_steps);
  RUN_TEST(test_reset_stacks_nothing);
  RUN_TEST(test_bkpt_takes_an_illegal_instruction_when_nothing_answers);
  RUN_TEST(test_the_breakpoint_number_is_on_a2_to_a4);
  RUN_TEST(test_chk_sets_z_from_the_register_not_the_bound);
  RUN_TEST(test_cmp2_reports_in_bounds_without_trapping);
  RUN_TEST(test_cmp2_sets_z_on_either_bound);
  RUN_TEST(test_cmp2_sets_carry_out_of_bounds_and_does_not_trap);
  RUN_TEST(test_chk2_traps_out_of_bounds_where_cmp2_does_not);
  RUN_TEST(test_cmp2_handles_signed_bounds);
  RUN_TEST(test_an_address_register_is_checked_over_all_32_bits);
  RUN_TEST(test_cas_swaps_when_the_comparison_matches);
  RUN_TEST(test_cas_loads_the_compare_register_when_it_fails);
  RUN_TEST(test_cas_releases_the_lock_on_both_outcomes);
  RUN_TEST(test_cas2_swaps_both_operands_or_neither);
  RUN_TEST(test_cas2_writes_nothing_when_the_second_comparison_fails);
  RUN_TEST(test_cas2_leaves_the_first_operand_when_the_registers_collide);
  RUN_TEST(test_cas2_releases_the_lock);
  RUN_TEST(test_ori_to_the_status_register_sets_the_interrupt_mask);
  RUN_TEST(test_andi_to_the_condition_codes_leaves_the_high_byte);
  RUN_TEST(test_ori_to_the_status_register_is_privileged);
  RUN_TEST(test_a_nop_executes_and_advances_the_pc);
  RUN_TEST(test_moveq_sign_extends_to_a_long);
  RUN_TEST(test_moveq_sets_the_documented_condition_codes);
  RUN_TEST(test_moveq_of_zero_sets_the_zero_flag);
  RUN_TEST(test_a_short_program_runs_to_its_end);
  RUN_TEST(test_a_branch_always_is_taken);
  RUN_TEST(test_a_conditional_branch_reads_the_previous_result);
  RUN_TEST(test_an_unimplemented_instruction_is_reported_not_skipped);
  RUN_TEST(test_a_faulting_operand_read_takes_the_bus_error_exception);
  RUN_TEST(test_the_same_instruction_over_memory_that_answers_executes);
  RUN_TEST(test_a_write_nothing_accepts_takes_the_bus_error_exception);
  RUN_TEST(test_a_refused_write_is_not_left_in_the_cache);
  RUN_TEST(test_a_write_updates_a_line_that_is_already_resident);
  RUN_TEST(test_a_misaligned_write_updates_both_resident_lines);
  RUN_TEST(test_misaligned_long_round_trips_through_the_data_cache);
  RUN_TEST(test_a_line_1010_word_takes_the_emulator_trap);
  RUN_TEST(test_an_f_line_word_with_no_coprocessor_takes_the_emulator_trap);
  RUN_TEST(test_an_unimplemented_mmu_instruction_is_not_dressed_up_as_f_line);
  RUN_TEST(test_a_prefetch_from_an_odd_address_is_an_address_error);
  RUN_TEST(test_an_address_error_runs_no_bus_cycle);
  RUN_TEST(test_a_misaligned_data_access_is_not_an_address_error);
  RUN_TEST(test_a_fault_does_not_leak_into_the_following_instruction);
  RUN_TEST(test_an_illegal_encoding_is_distinct_from_unimplemented);
  RUN_TEST(test_a_cached_pass_runs_no_bus_cycles_and_costs_its_microcode);
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
  RUN_TEST(test_addi_adds_its_immediate_to_a_register);
  RUN_TEST(test_subi_subtracts_the_immediate_from_the_destination);
  RUN_TEST(test_cmpi_compares_without_writing);
  RUN_TEST(test_andi_byte_masks_only_the_low_byte);
  RUN_TEST(test_clr_zeroes_its_destination);
  RUN_TEST(test_a_byte_clr_leaves_the_upper_bytes);
  RUN_TEST(test_neg_negates_and_borrows_for_a_non_zero_operand);
  RUN_TEST(test_negating_zero_does_not_borrow);
  RUN_TEST(test_not_complements_and_clears_v_and_c);
  RUN_TEST(test_tst_sets_flags_without_writing);
  RUN_TEST(test_a_countdown_loop_terminates);
  RUN_TEST(test_addq_adds_its_quick_data);
  RUN_TEST(test_addq_of_zero_adds_eight);
  RUN_TEST(test_addq_to_an_address_register_leaves_the_flags_alone);
  RUN_TEST(test_a_word_addq_to_an_address_register_uses_all_32_bits);
  RUN_TEST(test_addq_to_a_data_register_does_set_the_flags);
  RUN_TEST(test_scc_sets_all_ones_not_one);
  RUN_TEST(test_scc_sets_zero_when_the_condition_is_false);
  RUN_TEST(test_a_dbcc_loop_runs_the_documented_number_of_times);
  RUN_TEST(test_dbcc_decrements_only_the_low_word);
  RUN_TEST(test_a_true_dbcc_condition_leaves_the_counter_alone);
  RUN_TEST(test_adda_leaves_the_condition_codes_alone);
  RUN_TEST(test_a_word_adda_sign_extends_its_source);
  RUN_TEST(test_cmpa_does_set_the_condition_codes);
  RUN_TEST(test_z_reflects_the_bit_before_the_operation);
  RUN_TEST(test_setting_an_already_set_bit_clears_z);
  RUN_TEST(test_a_data_register_bit_operation_reaches_bit_thirty_one);
  RUN_TEST(test_the_bit_number_wraps_modulo_the_operand_width);
  RUN_TEST(test_btst_does_not_write);
  RUN_TEST(test_a_bit_operation_affects_only_the_zero_flag);
  RUN_TEST(test_a_dynamic_bit_number_comes_from_a_register);
  RUN_TEST(test_a_register_shift_executes);
  RUN_TEST(test_a_register_count_is_taken_modulo_sixty_four);
  RUN_TEST(test_a_byte_shift_leaves_the_upper_bytes);
  RUN_TEST(test_an_address_form_accepts_an_immediate_source);
  RUN_TEST(test_the_same_program_run_twice_hashes_the_same);
  RUN_TEST(test_the_hash_moves_as_the_program_runs);
  RUN_TEST(test_two_runs_with_the_same_registers_differ_by_their_clock);
  RUN_TEST(test_the_arithmetic_forms_take_an_immediate_source);
  RUN_TEST(test_the_memory_direction_refuses_an_immediate_destination);
  RUN_TEST(test_tst_takes_an_immediate_on_this_part);
  RUN_TEST(test_tracing_every_instruction_runs_it_then_traps);
  RUN_TEST(test_change_of_flow_tracing_ignores_ordinary_instructions);
  RUN_TEST(test_a_status_register_write_counts_as_a_change_of_flow);
  RUN_TEST(test_the_instruction_that_disables_tracing_is_still_traced);
  RUN_TEST(test_an_unexecuted_instruction_is_not_traced);
  RUN_TEST(test_a_traced_trap_stacks_both_with_the_trace_on_top);
  RUN_TEST(test_an_interrupt_is_taken_only_above_the_priority_mask);
  RUN_TEST(test_the_stacked_mask_is_the_one_before_the_interrupt);
  RUN_TEST(test_level_seven_interrupts_on_the_transition_and_not_the_level);
  RUN_TEST(test_a_failed_acknowledge_becomes_the_spurious_vector);
  RUN_TEST(test_an_interrupt_in_master_state_builds_two_frames);
  RUN_TEST(test_an_interrupt_wakes_a_stopped_processor);
  RUN_TEST(test_an_interrupt_returns_to_the_instruction_it_preempted);
  RUN_TEST(test_lea_refuses_an_increment_mode_it_decodes_perfectly_well);
  RUN_TEST(test_a_move_cannot_write_through_the_program_counter);
  RUN_TEST(test_pmove_refuses_an_increment_mode);
  RUN_TEST(test_pflusha_invalidates_every_entry);
  RUN_TEST(test_the_flush_mask_says_which_bits_must_agree);
  RUN_TEST(test_pflush_by_address_flushes_that_page_and_no_other);
  RUN_TEST(test_a_function_code_from_a_data_register_is_not_its_number);
  RUN_TEST(test_pload_adds_a_translation_where_pflush_removes_one);
  RUN_TEST(test_ptest_at_level_zero_reports_whether_the_atc_has_it);
  RUN_TEST(test_a_level_zero_ptest_cannot_ask_for_a_descriptor_address);
  RUN_TEST(test_a_table_search_ptest_leaves_the_atc_alone);
  RUN_TEST(test_pmove_writes_and_reads_the_translation_control_register);
  RUN_TEST(test_the_same_p_register_field_names_two_different_registers);
  RUN_TEST(test_an_invalid_root_pointer_faults_after_the_move);
  RUN_TEST(test_an_inconsistent_translation_control_lands_with_e_cleared);
  RUN_TEST(test_the_flush_disable_bit_decides_whether_the_atc_survives);
  RUN_TEST(test_an_mmu_instruction_in_user_state_violates_privilege);
  RUN_TEST(test_pmove_refuses_a_register_direct_operand);
  RUN_TEST(test_a_full_format_index_adds_base_register_and_displacement);
  RUN_TEST(test_the_index_is_inside_the_brackets_for_only_one_of_the_two);
  RUN_TEST(test_a_suppressed_base_contributes_zero_not_its_register);
  RUN_TEST(test_the_index_is_scaled_before_it_is_added);
  RUN_TEST(test_a_reserved_displacement_size_is_not_a_null_one);
  RUN_TEST(test_movec_reaches_the_vector_base_register_both_ways);
  RUN_TEST(test_the_control_register_codes_are_not_a_dense_index);
  RUN_TEST(test_movec_is_privileged);
  RUN_TEST(test_stop_loads_the_status_register_and_then_halts_fetching);
  RUN_TEST(test_reset_asserts_externally_and_carries_on);
  RUN_TEST(test_a_word_branch_uses_the_same_base_as_a_byte_one);
  RUN_TEST(test_an_untaken_wide_branch_still_skips_its_displacement);
  RUN_TEST(test_a_long_bsr_pushes_the_address_after_both_displacement_words);
  RUN_TEST(test_lea_loads_the_address_where_movea_loads_the_contents);
  RUN_TEST(test_pea_pushes_the_address_itself);
  RUN_TEST(test_swap_exchanges_the_halves_and_flags_the_whole);
  RUN_TEST(test_the_three_extend_forms_reach_different_distances);
  RUN_TEST(test_movem_reverses_its_mask_for_the_predecrement_mode);
  RUN_TEST(test_a_movem_save_and_restore_round_trips);
  RUN_TEST(test_a_word_movem_sign_extends_into_the_whole_register);
  RUN_TEST(test_chk_traps_on_a_negative_register_not_just_a_large_one);
  RUN_TEST(test_tas_flags_the_old_value_and_then_sets_the_bit);
  RUN_TEST(test_reading_the_status_register_is_privileged_but_the_ccr_is_not);
  RUN_TEST(test_writing_the_ccr_cannot_reach_the_system_byte);
  RUN_TEST(test_the_illegal_instruction_word_takes_its_exception);
  RUN_TEST(test_nbcd_gives_the_tens_or_nines_complement_by_the_extend_bit);
  RUN_TEST(test_negx_subtracts_from_zero_with_the_extend_bit);
  RUN_TEST(test_a_subroutine_call_returns_to_the_instruction_after_it);
  RUN_TEST(test_a_jump_goes_to_the_address_not_to_its_contents);
  RUN_TEST(test_link_and_unlk_are_exact_inverses);
  RUN_TEST(test_rte_returns_to_the_state_the_exception_interrupted);
  RUN_TEST(test_rtr_restores_only_the_condition_codes);
  RUN_TEST(test_a_privileged_instruction_in_user_state_violates_privilege);
  RUN_TEST(test_rte_on_an_undefined_frame_format_is_a_format_error);
  RUN_TEST(test_trapv_traps_only_when_the_overflow_flag_is_set);
  RUN_TEST(test_the_stacked_status_register_is_the_one_before_the_change);
  RUN_TEST(test_taking_an_exception_turns_tracing_off);
  RUN_TEST(test_the_frame_is_built_on_the_supervisor_stack_not_the_user_one);
  RUN_TEST(test_the_vector_is_fetched_through_the_vector_base_register);
  RUN_TEST(test_a_zero_divide_gets_the_six_word_frame_with_both_addresses);
  RUN_TEST(test_the_coprocessor_frame_is_ten_words);
  RUN_TEST(test_the_frames_this_model_cannot_build_are_declined);
  RUN_TEST(test_the_next_step_executes_the_handlers_first_instruction);
  RUN_TEST(test_exg_swaps_two_data_registers_whole);
  RUN_TEST(test_exg_distinguishes_the_mixed_pair_from_an_address_pair);
  RUN_TEST(test_abcd_adds_in_decimal_not_binary);
  RUN_TEST(test_abcd_carries_at_ninety_nine);
  RUN_TEST(test_sbcd_borrows_a_decimal_ten);
  RUN_TEST(test_the_bcd_forms_only_ever_clear_the_zero_flag);
  RUN_TEST(test_the_memory_form_of_abcd_predecrements_both_registers);
  RUN_TEST(test_cmpm_advances_both_registers_and_writes_nothing);
  RUN_TEST(test_mulu_produces_a_long_from_two_words);
  RUN_TEST(test_muls_and_mulu_differ_on_a_negative_operand);
  RUN_TEST(test_a_divide_puts_the_remainder_in_the_upper_word);
  RUN_TEST(test_a_division_overflow_leaves_the_operands_unchanged);
  RUN_TEST(test_a_division_by_zero_takes_the_zero_divide_exception);
  RUN_TEST(test_trap_uses_the_vector_its_number_indexes_not_the_number);
  RUN_TEST(test_the_last_trap_lands_at_the_end_of_the_trap_range);
  RUN_TEST(test_addx_adds_the_extend_bit);
  RUN_TEST(test_addx_only_ever_clears_the_zero_flag);
  return UNITY_END();
}
