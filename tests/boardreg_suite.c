/* Apollo core-board registers.
 *
 * Every figure asserted here was measured against the oracle by
 * `tools/mame-oracle/regprobe.lua` and is recorded in `FINDINGS.md` C10. Where
 * a test states a number, the number came from that probe -- there is no
 * published bit layout for any of these registers. */

#include "unity.h"

#include <string.h>

#include "board/ap_boardreg.h"

void setUp(void) {}
void tearDown(void) {}

static void test_bit_fifteen_of_the_status_register_always_reads_set(void) {
  ap_boardreg_t regs;
  ap_boardreg_init(&regs);

  TEST_ASSERT_TRUE((ap_boardreg_read16(&regs, AP_BOARDREG_CPU_STATUS_ADDR) &
                    0x8000u) != 0u);

  /* Measured through every write the probe made, in both directions. */
  ap_boardreg_write16(&regs, AP_BOARDREG_CPU_STATUS_ADDR, 0x0000);
  TEST_ASSERT_TRUE((ap_boardreg_read16(&regs, AP_BOARDREG_CPU_STATUS_ADDR) &
                    0x8000u) != 0u);
  ap_boardreg_write16(&regs, AP_BOARDREG_CPU_STATUS_ADDR, 0x7FFF);
  TEST_ASSERT_TRUE((ap_boardreg_read16(&regs, AP_BOARDREG_CPU_STATUS_ADDR) &
                    0x8000u) != 0u);
}

static void test_writing_the_status_register_clears_what_was_latched(void) {
  ap_boardreg_t regs;
  ap_boardreg_init(&regs);

  /* The distinction the probe was able to draw and a naive reading could not:
   * the register reads `8000` after every write, which on its own is equally
   * consistent with "writes are ignored". It is not ignored -- the value
   * standing at power-on could not be restored by writing it back. */
  ap_boardreg_latch_status(&regs, 0x0100);
  TEST_ASSERT_EQUAL_HEX16(0x8100 | AP_BOARDREG_STATUS_NORMAL_MODE,
                          ap_boardreg_read16(&regs, AP_BOARDREG_CPU_STATUS_ADDR));

  ap_boardreg_write16(&regs, AP_BOARDREG_CPU_STATUS_ADDR, 0x0100);
  TEST_ASSERT_EQUAL_HEX16(0x8000,
                          ap_boardreg_read16(&regs, AP_BOARDREG_CPU_STATUS_ADDR));
}

static void test_no_written_bit_survives_in_the_status_register(void) {
  ap_boardreg_t regs;

  /* The probe drove all sixteen bits in both directions and no bit but 15 ever
   * read back set. Repeated here over the same exhaustive sweep rather than a
   * sample, because "all sixteen" is the actual measurement. */
  for (unsigned bit = 0; bit < 16; bit++) {
    ap_boardreg_init(&regs);
    ap_boardreg_write16(&regs, AP_BOARDREG_CPU_STATUS_ADDR,
                        (uint16_t)(1u << bit));
    TEST_ASSERT_EQUAL_HEX16(0x8000,
                            ap_boardreg_read16(&regs,
                                               AP_BOARDREG_CPU_STATUS_ADDR));

    ap_boardreg_init(&regs);
    ap_boardreg_write16(&regs, AP_BOARDREG_CPU_STATUS_ADDR,
                        (uint16_t)~(1u << bit));
    TEST_ASSERT_EQUAL_HEX16(0x8000,
                            ap_boardreg_read16(&regs,
                                               AP_BOARDREG_CPU_STATUS_ADDR));
  }
}

static void test_the_control_register_stores_all_sixteen_bits(void) {
  ap_boardreg_t regs;
  ap_boardreg_init(&regs);

  /* Measured: every bit classified `rw`, in both directions. */
  for (unsigned bit = 0; bit < 16; bit++) {
    uint16_t one = (uint16_t)(1u << bit);
    ap_boardreg_write16(&regs, AP_BOARDREG_CPU_CONTROL_ADDR, one);
    TEST_ASSERT_EQUAL_HEX16(one, ap_boardreg_read16(&regs,
                                                    AP_BOARDREG_CPU_CONTROL_ADDR));

    uint16_t rest = (uint16_t)~one;
    ap_boardreg_write16(&regs, AP_BOARDREG_CPU_CONTROL_ADDR, rest);
    TEST_ASSERT_EQUAL_HEX16(rest,
                            ap_boardreg_read16(&regs,
                                               AP_BOARDREG_CPU_CONTROL_ADDR));
  }
}

static void test_the_latch_page_register_stores_all_sixteen_bits(void) {
  ap_boardreg_t regs;
  ap_boardreg_init(&regs);

  for (unsigned bit = 0; bit < 16; bit++) {
    uint16_t one = (uint16_t)(1u << bit);
    ap_boardreg_write16(&regs, AP_BOARDREG_LATCH_PAGE_ADDR, one);
    TEST_ASSERT_EQUAL_HEX16(one,
                            ap_boardreg_read16(&regs,
                                               AP_BOARDREG_LATCH_PAGE_ADDR));
  }
}

static void test_the_cache_control_register_is_a_byte_not_a_word(void) {
  ap_boardreg_t regs;
  ap_boardreg_init(&regs);

  /* The most valuable thing the probe found, because Table 2-8's rows all look
   * alike and a transcription would have made this a word like its neighbours.
   * A 16-bit read returns the byte in *both* halves. */
  TEST_ASSERT_EQUAL_HEX16(0xEFEF,
                          ap_boardreg_read16(&regs,
                                             AP_BOARDREG_CACHE_CONTROL_ADDR));
  TEST_ASSERT_EQUAL_HEX8(0xEF,
                         ap_boardreg_read8(&regs,
                                           AP_BOARDREG_CACHE_CONTROL_ADDR));
}

static void test_only_bit_seven_of_the_cache_control_register_is_writable(void) {
  ap_boardreg_t regs;
  ap_boardreg_init(&regs);

  /* Measured: whichever single bit was written, the read-back depended only on
   * bit 7 of the value. `6F` with it clear, `EF` with it set. */
  ap_boardreg_write8(&regs, AP_BOARDREG_CACHE_CONTROL_ADDR, 0x00);
  TEST_ASSERT_EQUAL_HEX8(0x6F,
                         ap_boardreg_read8(&regs,
                                           AP_BOARDREG_CACHE_CONTROL_ADDR));

  ap_boardreg_write8(&regs, AP_BOARDREG_CACHE_CONTROL_ADDR, 0x80);
  TEST_ASSERT_EQUAL_HEX8(0xEF,
                         ap_boardreg_read8(&regs,
                                           AP_BOARDREG_CACHE_CONTROL_ADDR));

  /* And a value with every bit but 7 set still reads `6F`. */
  ap_boardreg_write8(&regs, AP_BOARDREG_CACHE_CONTROL_ADDR, 0x7F);
  TEST_ASSERT_EQUAL_HEX8(0x6F,
                         ap_boardreg_read8(&regs,
                                           AP_BOARDREG_CACHE_CONTROL_ADDR));
}

static void test_a_register_is_aliased_across_its_whole_range(void) {
  ap_boardreg_t regs;
  ap_boardreg_init(&regs);

  /* Table 2-8 gives each register 256 bytes, and the probe found `010201`
   * behaving identically to `010200`. So the low byte of the address is not
   * part of the decode. */
  ap_boardreg_write8(&regs, AP_BOARDREG_CACHE_CONTROL_ADDR + 1u, 0x80);
  TEST_ASSERT_EQUAL_HEX8(0xEF,
                         ap_boardreg_read8(&regs,
                                           AP_BOARDREG_CACHE_CONTROL_ADDR));

  ap_boardreg_write16(&regs, AP_BOARDREG_CPU_CONTROL_ADDR + 0xFEu, 0x1234);
  TEST_ASSERT_EQUAL_HEX16(0x1234,
                          ap_boardreg_read16(&regs,
                                             AP_BOARDREG_CPU_CONTROL_ADDR));
}

static void test_the_four_measured_registers_decode_and_no_others(void) {
  ap_boardreg_id_t id;

  TEST_ASSERT_TRUE(ap_boardreg_decode(AP_BOARDREG_CPU_STATUS_ADDR, &id));
  TEST_ASSERT_EQUAL_UINT(AP_BOARDREG_CPU_STATUS, id);
  TEST_ASSERT_TRUE(ap_boardreg_decode(AP_BOARDREG_CPU_CONTROL_ADDR, &id));
  TEST_ASSERT_EQUAL_UINT(AP_BOARDREG_CPU_CONTROL, id);
  TEST_ASSERT_TRUE(ap_boardreg_decode(AP_BOARDREG_CACHE_CONTROL_ADDR, &id));
  TEST_ASSERT_EQUAL_UINT(AP_BOARDREG_CACHE_CONTROL, id);
  TEST_ASSERT_TRUE(ap_boardreg_decode(AP_BOARDREG_LATCH_PAGE_ADDR, &id));
  TEST_ASSERT_EQUAL_UINT(AP_BOARDREG_LATCH_PAGE_ON_PARITY, id);

  /* An address in a gap of Table 2-8 -- one of the two the probe used as its
   * control. */
  TEST_ASSERT_FALSE(ap_boardreg_decode(0x016000u, &id));
}

static void test_the_two_unmeasurable_registers_are_declined_not_absent(void) {
  ap_boardreg_id_t id;

  /* The distinction this module exists to keep. Task alias and master request
   * are named by Table 2-8, so the hardware has them; they are absent from the
   * *oracle*, matching exactly the all-ones signature that two known-unmapped
   * control addresses produced. Modelling them from that would copy an oracle
   * gap in as though it were a measurement. */
  TEST_ASSERT_FALSE(ap_boardreg_decode(AP_BOARDREG_TASK_ALIAS_ADDR, &id));
  TEST_ASSERT_TRUE(ap_boardreg_is_declined(AP_BOARDREG_TASK_ALIAS_ADDR));

  TEST_ASSERT_FALSE(ap_boardreg_decode(AP_BOARDREG_MASTER_REQUEST_ADDR, &id));
  TEST_ASSERT_TRUE(ap_boardreg_is_declined(AP_BOARDREG_MASTER_REQUEST_ADDR));

  /* An unmapped address is neither decoded nor declined: three answers, not
   * two, because "no register" and "a register we refuse to guess at" are
   * different facts about the machine. */
  TEST_ASSERT_FALSE(ap_boardreg_is_declined(0x016000u));
  TEST_ASSERT_FALSE(ap_boardreg_decode(0x016000u, &id));

  /* And a modelled register is not declined. */
  TEST_ASSERT_FALSE(ap_boardreg_is_declined(AP_BOARDREG_CPU_CONTROL_ADDR));
}

static void test_the_power_on_values_are_the_measured_ones(void) {
  ap_boardreg_t regs;
  ap_boardreg_init(&regs);

  /* Read at 0.001, 0.5 and 2.0 emulated seconds, identical each time. */
  TEST_ASSERT_EQUAL_HEX16(0x8100 | AP_BOARDREG_STATUS_NORMAL_MODE,
                          ap_boardreg_read16(&regs, AP_BOARDREG_CPU_STATUS_ADDR));
  TEST_ASSERT_EQUAL_HEX16(0xF700,
                          ap_boardreg_read16(&regs,
                                             AP_BOARDREG_CPU_CONTROL_ADDR));
  TEST_ASSERT_EQUAL_HEX8(0xEF,
                         ap_boardreg_read8(&regs,
                                           AP_BOARDREG_CACHE_CONTROL_ADDR));
  TEST_ASSERT_EQUAL_HEX16(0x0000,
                          ap_boardreg_read16(&regs,
                                             AP_BOARDREG_LATCH_PAGE_ADDR));
}

static void test_two_machines_initialised_alike_read_alike(void) {
  ap_boardreg_t a;
  ap_boardreg_t b;

  /* Determinism, as everywhere else in this core: init must not leave anything
   * to whatever was on the stack. Checked over the whole struct rather than
   * through the accessors, so a field the accessors do not reach still counts. */
  memset(&a, 0xAA, sizeof a);
  memset(&b, 0x55, sizeof b);
  ap_boardreg_init(&a);
  ap_boardreg_init(&b);

  TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof a);
}

/* ## Bit 0 is the Normal/Service switch
 *
 * The measured `8100` was taken against the oracle in its *shipping*
 * configuration, which is **service** mode -- `mdsession.lua` had already
 * noticed that and says "its default is Service, so leaving it alone is a
 * choice too". Bit 0 is an input, not a power-on level, and the boot PROM takes
 * a completely different path on it. `FINDINGS.md` C114.
 */
static void test_the_normal_service_switch_is_bit_zero_and_defaults_to_normal(void) {
  ap_boardreg_t regs;
  ap_boardreg_init(&regs);

  /* A workstation runs normal, so that is the default -- **not** the oracle's
   * shipping value, which is what the raw measurement gave. */
  TEST_ASSERT_EQUAL_HEX16(AP_BOARDREG_STATUS_NORMAL_MODE,
                          (uint16_t)(regs.cpu_status &
                                     AP_BOARDREG_STATUS_NORMAL_MODE));

  ap_boardreg_set_normal_mode(&regs, false);
  TEST_ASSERT_EQUAL_HEX16(0x8100u, regs.cpu_status);
  ap_boardreg_set_normal_mode(&regs, true);
  TEST_ASSERT_EQUAL_HEX16(0x8101u, regs.cpu_status);

  /* And the switch touches nothing else, which is what makes it a switch
   * rather than a reset. */
  ap_boardreg_set_normal_mode(&regs, false);
  ap_boardreg_set_normal_mode(&regs, true);
  TEST_ASSERT_EQUAL_HEX16(0x8101u, regs.cpu_status);
}

/* ## The diagnostic LED codes
 *
 * `008778-03` §3.7: "nine LED indicators for diagnostics that can be set or
 * reset by writing to the **upper byte** of the control register". The firmware
 * byte-writes to `010100`, which on a big-endian bus *is* that upper byte, so
 * what a write carries is the LED pattern.
 *
 * A machine that fails a self-test posts a code here and flashes it for ever --
 * it has no console to complain to. Discarding those values threw away the only
 * account the firmware gives of what went wrong. `FINDINGS.md` C109 has the
 * post routine at `00251A`.
 */
static void test_posted_codes_are_kept_distinct_in_order(void) {
  ap_boardreg_t regs;
  ap_boardreg_init(&regs);

  ap_boardreg_write8(&regs, AP_BOARDREG_CPU_CONTROL_ADDR, 0xFFu);
  ap_boardreg_write8(&regs, AP_BOARDREG_CPU_CONTROL_ADDR, 0x00u);
  /* Repeats collapse: an error loop posts the same pair for ever, and a plain
   * ring of every write would hold nothing but the last two. */
  ap_boardreg_write8(&regs, AP_BOARDREG_CPU_CONTROL_ADDR, 0x00u);
  ap_boardreg_write8(&regs, AP_BOARDREG_CPU_CONTROL_ADDR, 0x00u);
  ap_boardreg_write8(&regs, AP_BOARDREG_CPU_CONTROL_ADDR, 0xEFu);
  /* ... but an alternation does not, because that is what a flashing code is. */
  ap_boardreg_write8(&regs, AP_BOARDREG_CPU_CONTROL_ADDR, 0x00u);

  TEST_ASSERT_EQUAL_UINT(4u, regs.posted_count);
  TEST_ASSERT_EQUAL_HEX8(0xFFu, regs.posted[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00u, regs.posted[1]);
  TEST_ASSERT_EQUAL_HEX8(0xEFu, regs.posted[2]);
  TEST_ASSERT_EQUAL_HEX8(0x00u, regs.posted[3]);
  /* Every write is still counted, so "how many" and "which" stay separable. */
  TEST_ASSERT_EQUAL_UINT(6u, regs.posted_total);
}

/* Kept **exactly as written**. The post routine complements what it displays
 * and the error loop's direct writes at `005EC8` and `005ED8` do not, so
 * undoing the complement here would be right for one caller and wrong for the
 * other. The reader is told which; the model does not guess. */
static void test_a_posted_code_is_not_complemented_by_the_model(void) {
  ap_boardreg_t regs;
  ap_boardreg_init(&regs);
  ap_boardreg_write8(&regs, AP_BOARDREG_CPU_CONTROL_ADDR, 0x8Du);
  TEST_ASSERT_EQUAL_HEX8(0x8Du, regs.posted[0]);
}

/* The buffer is finite and stops rather than wrapping: an error loop runs for
 * ever, and the *first* codes are the self-test's progress -- which is the part
 * worth keeping. A ring would replace them with the flash. */
static void test_the_code_buffer_keeps_the_earliest_and_stops(void) {
  ap_boardreg_t regs;
  ap_boardreg_init(&regs);
  for (unsigned i = 0; i < AP_BOARDREG_POSTED_CODES + 8u; i++) {
    ap_boardreg_write8(&regs, AP_BOARDREG_CPU_CONTROL_ADDR, (uint8_t)i);
  }
  TEST_ASSERT_EQUAL_UINT(AP_BOARDREG_POSTED_CODES, regs.posted_count);
  TEST_ASSERT_EQUAL_HEX8(0x00u, regs.posted[0]);
  TEST_ASSERT_EQUAL_UINT(AP_BOARDREG_POSTED_CODES + 8u, regs.posted_total);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_bit_fifteen_of_the_status_register_always_reads_set);
  RUN_TEST(test_writing_the_status_register_clears_what_was_latched);
  RUN_TEST(test_no_written_bit_survives_in_the_status_register);
  RUN_TEST(test_the_control_register_stores_all_sixteen_bits);
  RUN_TEST(test_the_latch_page_register_stores_all_sixteen_bits);
  RUN_TEST(test_the_cache_control_register_is_a_byte_not_a_word);
  RUN_TEST(test_only_bit_seven_of_the_cache_control_register_is_writable);
  RUN_TEST(test_a_register_is_aliased_across_its_whole_range);
  RUN_TEST(test_the_four_measured_registers_decode_and_no_others);
  RUN_TEST(test_the_two_unmeasurable_registers_are_declined_not_absent);
  RUN_TEST(test_the_power_on_values_are_the_measured_ones);
  RUN_TEST(test_two_machines_initialised_alike_read_alike);
  RUN_TEST(test_the_normal_service_switch_is_bit_zero_and_defaults_to_normal);
  RUN_TEST(test_posted_codes_are_kept_distinct_in_order);
  RUN_TEST(test_a_posted_code_is_not_complemented_by_the_model);
  RUN_TEST(test_the_code_buffer_keeps_the_earliest_and_stops);
  return UNITY_END();
}
