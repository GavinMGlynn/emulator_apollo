/* MC68030 operand access through a decoded effective address.
 *
 * Cited to the M68000 Family Programmer's Reference Manual 1992.
 *
 * The two rules under test differ from each other in a way that is easy to get
 * backwards, and neither mistake faults. A data register write is *partial*: a
 * byte or word operation leaves the rest of the register alone. An address
 * register write never is: a word operand is sign-extended and all 32 bits
 * change.
 */

#include "cpu/m68030/ap_m68030_operand.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define FC_SUPERVISOR_DATA 5u

typedef struct {
  unsigned fills;
  uint32_t written;
  bool saw_write;
} memory_t;

static void memory_fill(void *context, uint32_t line_address,
                        uint8_t function_code, ap_m68030_fill_answer_t *out) {
  (void)function_code;
  (void)line_address;
  memory_t *memory = (memory_t *)context;
  memory->fills++;
  out->termination = AP_M68030_TERM_STERM;
  out->burst_acknowledge = false;
  out->data[0] = 0xAABBCCDDu;
}

typedef struct {
  ap_m68030_cache_t cache;
  ap_m68030_atc_t atc;
  ap_m68030_tc_t tc;
  ap_m68030_root_t root;
  memory_t memory;
  ap_m68030_access_ctx_t access;
} machine_t;

static void make_machine(machine_t *m) {
  ap_m68030_cache_clear(&m->cache);
  ap_m68030_atc_flush(&m->atc);
  m->access = (ap_m68030_access_ctx_t){
      .cache = &m->cache,
      .atc = &m->atc,
      .tc = &m->tc,
      .root = &m->root,
      .cache_enabled = true,
      .translation_enabled = false,
      .fill = memory_fill,
      .context = &m->memory,
  };
}

static ap_m68030_address_t data_register(unsigned reg) {
  return (ap_m68030_address_t){.in_register = true, .reg = reg, .valid = true};
}

static ap_m68030_address_t address_register(unsigned reg) {
  return (ap_m68030_address_t){
      .in_register = true, .address_register = true, .reg = reg, .valid = true};
}

/* A data register read takes only the low bytes the operand covers. */
static void test_a_data_register_read_is_sized(void) {
  machine_t m = {0};
  make_machine(&m);
  ap_m68030_regs_t regs = {0};
  regs.d[1] = 0x11223344u;
  const ap_m68030_address_t where = data_register(1);

  TEST_ASSERT_EQUAL_HEX32(
      0x44u, ap_m68030_operand_read(&regs, &m.access, &where, 1, 0).value);
  TEST_ASSERT_EQUAL_HEX32(
      0x3344u, ap_m68030_operand_read(&regs, &m.access, &where, 2, 0).value);
  TEST_ASSERT_EQUAL_HEX32(
      0x11223344u, ap_m68030_operand_read(&regs, &m.access, &where, 4, 0).value);
}

/* The first rule: a byte or word write to a data register leaves the rest of
 * the register alone. Applying the address register rule here would destroy
 * data the program still needs, and would not fault. */
static void test_a_data_register_write_preserves_the_upper_bits(void) {
  machine_t m = {0};
  make_machine(&m);
  ap_m68030_regs_t regs = {0};
  regs.d[2] = 0x11223344u;
  const ap_m68030_address_t where = data_register(2);

  (void)ap_m68030_operand_write(&regs, &m.access, &where, 1, 0xFFu, 0);
  TEST_ASSERT_EQUAL_HEX32(0x112233FFu, regs.d[2]);

  (void)ap_m68030_operand_write(&regs, &m.access, &where, 2, 0xEEEEu, 0);
  TEST_ASSERT_EQUAL_HEX32(0x1122EEEEu, regs.d[2]);

  /* A long write replaces everything, as it must. */
  (void)ap_m68030_operand_write(&regs, &m.access, &where, 4, 0x99887766u, 0);
  TEST_ASSERT_EQUAL_HEX32(0x99887766u, regs.d[2]);
}

/* The second rule, and the opposite of the first: "the source operand is
 * sign-extended to a long operand and the operation is performed on the address
 * register using all 32 bits." */
static void test_an_address_register_write_sign_extends_the_whole_register(void) {
  machine_t m = {0};
  make_machine(&m);
  ap_m68030_regs_t regs = {0};
  ap_m68030_write_sr(&regs, 1u << AP_M68030_SR_S_BIT);
  ap_m68030_write_address_register(&regs, 3, 0x11223344u);
  const ap_m68030_address_t where = address_register(3);

  /* A negative word fills the upper half with ones. */
  (void)ap_m68030_operand_write(&regs, &m.access, &where, 2, 0xFFFFu, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu,
                          ap_m68030_read_address_register(&regs, 3));

  /* A positive word clears it, rather than leaving the previous upper half. */
  (void)ap_m68030_operand_write(&regs, &m.access, &where, 2, 0x1234u, 0);
  TEST_ASSERT_EQUAL_HEX32(0x00001234u,
                          ap_m68030_read_address_register(&regs, 3));
}

/* The two rules side by side on the same value, which is the comparison that
 * catches applying one where the other belongs. */
static void test_the_two_register_rules_differ_on_the_same_operand(void) {
  machine_t m = {0};
  make_machine(&m);
  ap_m68030_regs_t regs = {0};
  ap_m68030_write_sr(&regs, 1u << AP_M68030_SR_S_BIT);
  regs.d[0] = 0x11110000u;
  ap_m68030_write_address_register(&regs, 0, 0x11110000u);

  const ap_m68030_address_t d = data_register(0);
  const ap_m68030_address_t a = address_register(0);

  (void)ap_m68030_operand_write(&regs, &m.access, &d, 2, 0xFFFFu, 0);
  (void)ap_m68030_operand_write(&regs, &m.access, &a, 2, 0xFFFFu, 0);

  TEST_ASSERT_EQUAL_HEX32(0x1111FFFFu, regs.d[0]);          /* partial */
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu,
                          ap_m68030_read_address_register(&regs, 0)); /* whole */
}

/* Sign extension is exposed because several instructions do it explicitly. */
static void test_sign_extension_by_size(void) {
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, ap_m68030_sign_extend(0xFFu, 1));
  TEST_ASSERT_EQUAL_HEX32(0x0000007Fu, ap_m68030_sign_extend(0x7Fu, 1));
  TEST_ASSERT_EQUAL_HEX32(0xFFFF8000u, ap_m68030_sign_extend(0x8000u, 2));
  TEST_ASSERT_EQUAL_HEX32(0x12345678u, ap_m68030_sign_extend(0x12345678u, 4));
}

/* A memory operand goes through the access path, so it costs clocks the first
 * time and nothing the second. */
static void test_a_memory_operand_uses_the_access_path(void) {
  machine_t m = {0};
  make_machine(&m);
  ap_m68030_regs_t regs = {0};
  const ap_m68030_address_t where = {.address = 0x2000u, .valid = true};

  const ap_m68030_operand_result_t first =
      ap_m68030_operand_read(&regs, &m.access, &where, 4, FC_SUPERVISOR_DATA);
  TEST_ASSERT_TRUE(first.ok);
  TEST_ASSERT_EQUAL_HEX32(0xAABBCCDDu, first.value);
  TEST_ASSERT_TRUE(first.clocks > 0);

  const ap_m68030_operand_result_t second =
      ap_m68030_operand_read(&regs, &m.access, &where, 4, FC_SUPERVISOR_DATA);
  TEST_ASSERT_EQUAL_UINT32(0, second.clocks);
  TEST_ASSERT_EQUAL_UINT(1, m.memory.fills);
}

/* An address the calculation could not finish is not silently read as zero. */
static void test_an_unfinished_address_is_a_fault_not_a_zero(void) {
  machine_t m = {0};
  make_machine(&m);
  ap_m68030_regs_t regs = {0};

  const ap_m68030_address_t pending = {.valid = true,
                                       .indirection_pending = true};
  TEST_ASSERT_TRUE(
      ap_m68030_operand_read(&regs, &m.access, &pending, 4, 0).fault);

  const ap_m68030_address_t invalid = {.valid = false};
  TEST_ASSERT_TRUE(
      ap_m68030_operand_read(&regs, &m.access, &invalid, 4, 0).fault);
}

/* An immediate has no location to read from or write to: the instruction unit
 * supplies its value. Returning a zero would look like a real operand. */
static void test_an_immediate_is_not_read_from_memory(void) {
  machine_t m = {0};
  make_machine(&m);
  ap_m68030_regs_t regs = {0};
  const ap_m68030_address_t immediate = {.immediate = true, .valid = true};

  TEST_ASSERT_TRUE(
      ap_m68030_operand_read(&regs, &m.access, &immediate, 4, 0).fault);
  TEST_ASSERT_TRUE(
      ap_m68030_operand_write(&regs, &m.access, &immediate, 4, 1, 0).fault);
}

/* The access path answers in long words, so a byte operand has to be selected
 * out of one by position. The 68030 is big endian: the long word $AABBCCDD
 * holds $AA at offset 0 and $DD at offset 3.
 *
 * This is the shape of a real bug that shipped here: the read masked the low
 * bits instead, so every byte read returned $DD whatever its address, and
 * nothing faulted -- the wrong byte simply arrived. Three addresses in four
 * were wrong, and the fourth is the one a casual test picks. */
static void test_a_byte_read_takes_the_byte_its_address_names(void) {
  static const uint8_t expected[4] = {0xAAu, 0xBBu, 0xCCu, 0xDDu};
  for (unsigned offset = 0; offset < 4u; offset++) {
    machine_t m = {0};
    make_machine(&m);
    ap_m68030_regs_t regs = {0};
    const ap_m68030_address_t where = {.address = 0x2000u + offset,
                                       .valid = true};

    const ap_m68030_operand_result_t read = ap_m68030_operand_read(
        &regs, &m.access, &where, 1u, FC_SUPERVISOR_DATA);

    TEST_ASSERT_TRUE(read.ok);
    TEST_ASSERT_EQUAL_HEX32(expected[offset], read.value);
  }
}

/* The same for a word: the aligned halves of $AABBCCDD are $AABB and $CCDD. */
static void test_a_word_read_takes_the_half_its_address_names(void) {
  machine_t m = {0};
  make_machine(&m);
  ap_m68030_regs_t regs = {0};

  const ap_m68030_address_t high = {.address = 0x2000u, .valid = true};
  const ap_m68030_operand_result_t upper =
      ap_m68030_operand_read(&regs, &m.access, &high, 2u, FC_SUPERVISOR_DATA);
  TEST_ASSERT_TRUE(upper.ok);
  TEST_ASSERT_EQUAL_HEX32(0xAABBu, upper.value);

  const ap_m68030_address_t low = {.address = 0x2002u, .valid = true};
  const ap_m68030_operand_result_t lower =
      ap_m68030_operand_read(&regs, &m.access, &low, 2u, FC_SUPERVISOR_DATA);
  TEST_ASSERT_TRUE(lower.ok);
  TEST_ASSERT_EQUAL_HEX32(0xCCDDu, lower.value);
}

/* An operand straddling two long words needs two bus cycles. The 68030 does
 * perform them -- misalignment is not a fault on this part -- but this path
 * issues one, so it declines rather than returning half an operand. The
 * decline is the contract being pinned, and it is a named gap. */
static void test_an_operand_straddling_two_long_words_is_declined(void) {
  machine_t m = {0};
  make_machine(&m);
  ap_m68030_regs_t regs = {0};

  /* A word at offset 3 covers offsets 3 and 4, so it crosses. */
  const ap_m68030_address_t crossing = {.address = 0x2003u, .valid = true};
  const ap_m68030_operand_result_t word = ap_m68030_operand_read(
      &regs, &m.access, &crossing, 2u, FC_SUPERVISOR_DATA);
  TEST_ASSERT_FALSE(word.ok);
  TEST_ASSERT_TRUE(word.fault);

  /* A long word at any offset but zero crosses. */
  const ap_m68030_address_t odd_long = {.address = 0x2001u, .valid = true};
  const ap_m68030_operand_result_t wide = ap_m68030_operand_read(
      &regs, &m.access, &odd_long, 4u, FC_SUPERVISOR_DATA);
  TEST_ASSERT_FALSE(wide.ok);

  /* And an aligned long word does not. */
  const ap_m68030_address_t aligned = {.address = 0x2000u, .valid = true};
  const ap_m68030_operand_result_t whole = ap_m68030_operand_read(
      &regs, &m.access, &aligned, 4u, FC_SUPERVISOR_DATA);
  TEST_ASSERT_TRUE(whole.ok);
  TEST_ASSERT_EQUAL_HEX32(0xAABBCCDDu, whole.value);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_data_register_read_is_sized);
  RUN_TEST(test_a_data_register_write_preserves_the_upper_bits);
  RUN_TEST(test_an_address_register_write_sign_extends_the_whole_register);
  RUN_TEST(test_the_two_register_rules_differ_on_the_same_operand);
  RUN_TEST(test_sign_extension_by_size);
  RUN_TEST(test_a_memory_operand_uses_the_access_path);
  RUN_TEST(test_an_unfinished_address_is_a_fault_not_a_zero);
  RUN_TEST(test_an_immediate_is_not_read_from_memory);
  RUN_TEST(test_a_byte_read_takes_the_byte_its_address_names);
  RUN_TEST(test_a_word_read_takes_the_half_its_address_names);
  RUN_TEST(test_an_operand_straddling_two_long_words_is_declined);
  return UNITY_END();
}
