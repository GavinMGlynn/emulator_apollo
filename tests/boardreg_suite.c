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

  /* The latched condition goes; the switch stays, because the machine is still
   * the machine it was. Writing the value back does not restore the bit. */
  ap_boardreg_write16(&regs, AP_BOARDREG_CPU_STATUS_ADDR, 0x0100);
  TEST_ASSERT_EQUAL_HEX16(0x8000 | AP_BOARDREG_STATUS_NORMAL_MODE,
                          ap_boardreg_read16(&regs, AP_BOARDREG_CPU_STATUS_ADDR));
}

static void test_no_written_bit_survives_in_the_status_register(void) {
  ap_boardreg_t regs;

  /* The probe drove all sixteen bits in both directions and no bit but 15 ever
   * read back set. Repeated here over the same exhaustive sweep rather than a
   * sample, because "all sixteen" is the actual measurement.
   *
   * The probe ran in *service* mode, so this asserts against a machine in
   * service mode: it is the measurement's own conditions. Asserting a flat
   * `8000` against a machine in **normal** mode -- which is what this suite
   * built after the switch was found -- is what the earlier version of this
   * test did, and it quietly required the write to clear the switch input. */
  for (unsigned bit = 0; bit < 16; bit++) {
    ap_boardreg_init(&regs);
    ap_boardreg_set_normal_mode(&regs, false);
    ap_boardreg_write16(&regs, AP_BOARDREG_CPU_STATUS_ADDR,
                        (uint16_t)(1u << bit));
    TEST_ASSERT_EQUAL_HEX16(0x8000,
                            ap_boardreg_read16(&regs,
                                               AP_BOARDREG_CPU_STATUS_ADDR));

    ap_boardreg_init(&regs);
    ap_boardreg_set_normal_mode(&regs, false);
    ap_boardreg_write16(&regs, AP_BOARDREG_CPU_STATUS_ADDR,
                        (uint16_t)~(1u << bit));
    TEST_ASSERT_EQUAL_HEX16(0x8000,
                            ap_boardreg_read16(&regs,
                                               AP_BOARDREG_CPU_STATUS_ADDR));
  }
}

/* ## A write acknowledges conditions; it does not throw the switch
 *
 * `008778-03` §3.2: "Writing to the status register clears the interrupt
 * status." The boot PROM does exactly that three times -- `clr.w $10000` at
 * `00168C`, `002632` and `007440` -- and every one of them ran through a model
 * that cleared bit 0 with the rest, so a normal machine became a service one on
 * the first clear.
 */
static void test_a_status_write_clears_the_fp_trap_and_keeps_the_switch(void) {
  ap_boardreg_t regs;
  ap_boardreg_init(&regs);

  /* A normal machine with both a bus error and an FP trap standing. */
  ap_boardreg_latch_status(&regs, AP_BOARDREG_STATUS_FP_TRAP |
                                      AP_BOARDREG_STATUS_BUS_ERROR |
                                      AP_BOARDREG_STATUS_PARITY_MASK);

  ap_boardreg_write16(&regs, AP_BOARDREG_CPU_STATUS_ADDR, 0x0000);

  /* Every condition the write acknowledges is gone, the FP trap with them --
   * `FIM_$BUS_ERR` reads this register and then writes it, and Domain/OS never
   * writes `016404`, so a kept trap could never be cleared at all. What stands
   * is the switch input, which is not storage, and bit 15. */
  TEST_ASSERT_EQUAL_HEX16(AP_BOARDREG_STATUS_ALWAYS_SET |
                              AP_BOARDREG_STATUS_NORMAL_MODE,
                          ap_boardreg_read16(&regs,
                                             AP_BOARDREG_CPU_STATUS_ADDR));

  /* And in service mode the same write leaves bit 0 clear, which is why the
   * probe could never have seen this. */
  ap_boardreg_init(&regs);
  ap_boardreg_set_normal_mode(&regs, false);
  ap_boardreg_write16(&regs, AP_BOARDREG_CPU_STATUS_ADDR, 0xFFFF);
  TEST_ASSERT_EQUAL_HEX16(AP_BOARDREG_STATUS_ALWAYS_SET,
                          ap_boardreg_read16(&regs,
                                             AP_BOARDREG_CPU_STATUS_ADDR));
}

/* ## The selective clear locations, one address per condition
 *
 * `019411-A00`'s replacement for §4.2.1 lists five, and the low bits of the
 * address are the decode -- which makes this the one range in the file that is
 * *not* aliased across its 256 bytes.
 */
static void test_each_selective_clear_location_clears_only_its_own(void) {
  static const struct {
    uint32_t offset;
    uint16_t clears;
  } CASES[] = {
      {AP_BOARDREG_CLEAR_FPU_TRAP_OFFSET, AP_BOARDREG_STATUS_FP_TRAP},
      {AP_BOARDREG_CLEAR_PARITY_OFFSET, AP_BOARDREG_STATUS_PARITY_MASK},
      {AP_BOARDREG_CLEAR_BUS_ERROR_OFFSET, AP_BOARDREG_STATUS_BUS_ERROR},
  };
  const uint16_t all = AP_BOARDREG_STATUS_CONDITIONS;

  for (unsigned i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
    ap_boardreg_t regs;
    ap_boardreg_init(&regs);
    ap_boardreg_latch_status(&regs, all);

    ap_boardreg_write16(&regs,
                        AP_BOARDREG_SELECTIVE_CLEAR_ADDR + CASES[i].offset,
                        0x0000);

    /* Only its own, which is the whole point of a *selective* clear and the
     * thing an aliased decode would have destroyed. */
    TEST_ASSERT_EQUAL_HEX16((uint16_t)(all & ~CASES[i].clears),
                            (uint16_t)(regs.cpu_status & all));
  }
}

static void test_clear_all_clears_every_condition_and_nothing_else(void) {
  ap_boardreg_t regs;
  ap_boardreg_init(&regs);
  ap_boardreg_latch_status(&regs, AP_BOARDREG_STATUS_CONDITIONS);

  ap_boardreg_write16(&regs, AP_BOARDREG_SELECTIVE_CLEAR_ADDR +
                                 AP_BOARDREG_CLEAR_ALL_OFFSET,
                      0x0000);

  /* The switch survives a clear-all for the same reason it survives a status
   * write: it is an input, and no location can clear a switch. */
  TEST_ASSERT_EQUAL_HEX16(AP_BOARDREG_STATUS_ALWAYS_SET |
                              AP_BOARDREG_STATUS_NORMAL_MODE,
                          ap_boardreg_read16(&regs,
                                             AP_BOARDREG_CPU_STATUS_ADDR));
}

static void test_the_graphics_trap_location_decodes_and_clears_nothing(void) {
  ap_boardreg_t regs;
  ap_boardreg_id_t id;
  ap_boardreg_init(&regs);
  ap_boardreg_latch_status(&regs, AP_BOARDREG_STATUS_CONDITIONS);

  /* The addendum names it, so it decodes -- reporting it unmapped would be
   * wrong about the hardware. Which status bit it is has no source, so it
   * clears none: a location that cleared the wrong bit would be worse than one
   * that honestly clears nothing. */
  TEST_ASSERT_TRUE(ap_boardreg_decode(AP_BOARDREG_SELECTIVE_CLEAR_ADDR +
                                          AP_BOARDREG_CLEAR_GRAPHICS_TRAP_OFFSET,
                                      &id));
  TEST_ASSERT_EQUAL_INT(AP_BOARDREG_SELECTIVE_CLEAR, id);

  ap_boardreg_write16(&regs, AP_BOARDREG_SELECTIVE_CLEAR_ADDR +
                                 AP_BOARDREG_CLEAR_GRAPHICS_TRAP_OFFSET,
                      0x0000);
  TEST_ASSERT_EQUAL_HEX16(AP_BOARDREG_STATUS_CONDITIONS,
                          (uint16_t)(regs.cpu_status &
                                     AP_BOARDREG_STATUS_CONDITIONS));
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

/* The latch-page register is sixteen bits wide and **read only**, which is two
 * claims and this used to test the wrong one: it wrote each bit and read it
 * back, asserting a store the hardware does not have. `002398-04` p. 12-27
 * heads it "PARITY ERROR REGISTER (read only)" and gives its contents as the
 * failing page number, so what a program writes must not reach it -- see
 * `AP_BOARDREG_LATCH_PAGE_ADDR`.
 *
 * The width is still asserted, from the side that has it: the *hardware* latches
 * the page, and every bit of what it latches must read back. */
static void test_the_latch_page_register_is_read_only_and_sixteen_bits(void) {
  ap_boardreg_t regs;
  ap_boardreg_init(&regs);

  for (unsigned bit = 0; bit < 16; bit++) {
    const uint16_t one = (uint16_t)(1u << bit);
    regs.latch_page_on_parity = one;
    TEST_ASSERT_EQUAL_HEX16(one,
                            ap_boardreg_read16(&regs,
                                               AP_BOARDREG_LATCH_PAGE_ADDR));

    /* And a bus write of anything at all leaves it standing. */
    ap_boardreg_write16(&regs, AP_BOARDREG_LATCH_PAGE_ADDR,
                        (uint16_t)~one);
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

static void test_table_two_eights_last_two_registers_store_a_byte(void) {
  ap_boardreg_id_t id;
  ap_boardreg_t regs;
  ap_boardreg_init(&regs);

  /* Both are named by Table 2-8, so the hardware has them, and both were once
   * declined because the *oracle* answers all-ones at each -- the same
   * signature two known-unmapped control addresses produce, so modelling from
   * it would import an oracle gap as a measurement.
   *
   * That argument was against inventing a read-back value, and it is still
   * good. It never justified having no register at all: `008778-03` §2.4.7
   * describes the master request register's purpose and the boot PROMs write it
   * 29 times. So the storage is modelled and the semantics are not. */
  TEST_ASSERT_TRUE(ap_boardreg_decode(AP_BOARDREG_TASK_ALIAS_ADDR, &id));
  TEST_ASSERT_EQUAL_UINT(AP_BOARDREG_TASK_ALIAS, id);
  TEST_ASSERT_TRUE(ap_boardreg_decode(AP_BOARDREG_MASTER_REQUEST_ADDR, &id));
  TEST_ASSERT_EQUAL_UINT(AP_BOARDREG_MASTER_REQUEST, id);

  /* A byte written is the byte read: the firmware's sites are all `MOVE.B` and
   * `CLR.B`, and it drives bits 1, 3 and 6. */
  ap_boardreg_write8(&regs, AP_BOARDREG_MASTER_REQUEST_ADDR, 0x4Au);
  TEST_ASSERT_EQUAL_HEX8(
      0x4Au, ap_boardreg_read8(&regs, AP_BOARDREG_MASTER_REQUEST_ADDR));
  TEST_ASSERT_EQUAL_HEX8(0x4Au, ap_boardreg_master_request(&regs));

  ap_boardreg_write8(&regs, AP_BOARDREG_TASK_ALIAS_ADDR, 0x25u);
  TEST_ASSERT_EQUAL_HEX8(
      0x25u, ap_boardreg_read8(&regs, AP_BOARDREG_TASK_ALIAS_ADDR));

  /* Nothing is declined now, and an unmapped address is still neither decoded
   * nor declined. */
  TEST_ASSERT_FALSE(ap_boardreg_is_declined(AP_BOARDREG_TASK_ALIAS_ADDR));
  TEST_ASSERT_FALSE(ap_boardreg_is_declined(AP_BOARDREG_MASTER_REQUEST_ADDR));
  TEST_ASSERT_FALSE(ap_boardreg_decode(0x016000u, &id));
}

static void test_the_power_on_values_are_the_measured_ones(void) {
  ap_boardreg_t regs;
  ap_boardreg_init(&regs);

  /* Read at 0.001, 0.5 and 2.0 emulated seconds, identical each time -- and
   * every one of those is *after* the boot PROM began probing absent hardware,
   * so the `8100` it saw carried bit 8, the CPU timeout, as a latched
   * condition. The power-on level has it clear; a probe taken at those same
   * moments against this core now measures `8100` again, which is the two
   * accounts agreeing rather than one replacing the other. */
  TEST_ASSERT_EQUAL_HEX16(0x8000 | AP_BOARDREG_STATUS_NORMAL_MODE,
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
  TEST_ASSERT_EQUAL_HEX16(0x8000u, regs.cpu_status);
  ap_boardreg_set_normal_mode(&regs, true);
  TEST_ASSERT_EQUAL_HEX16(0x8001u, regs.cpu_status);

  /* And the switch touches nothing else, which is what makes it a switch
   * rather than a reset. */
  ap_boardreg_set_normal_mode(&regs, false);
  ap_boardreg_set_normal_mode(&regs, true);
  TEST_ASSERT_EQUAL_HEX16(0x8001u, regs.cpu_status);
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

/* ## Bit 4 of the cache register is the master controller's request line
 *
 * The "fixed pattern" this suite asserts elsewhere is what the register reads
 * with **no interrupt standing** -- which is every sample a register probe
 * takes, since it drives bits and reads them back on a quiet machine. Bit 4 is
 * neither storage nor fixed: it follows the master 8259's `INT`.
 *
 * The loaded `SELF_TEST` diagnostic reads `010200` at `01002848` right after
 * unmasking the cascade and requires this bit set; the oracle sets it from the
 * same line on everything that is not a DN3000.
 */
static void test_the_cache_register_reports_the_master_request(void) {
  ap_boardreg_t regs;
  ap_boardreg_init(&regs);

  /* Quiet: the measured pattern, unchanged. */
  TEST_ASSERT_EQUAL_HEX8(0u,
                         (uint8_t)(ap_boardreg_read8(&regs,
                                                     AP_BOARDREG_CACHE_CONTROL_ADDR) &
                                   AP_BOARDREG_CACHE_INTERRUPT_PENDING));

  ap_boardreg_set_interrupt_pending(&regs, true);
  TEST_ASSERT_EQUAL_HEX8(AP_BOARDREG_CACHE_INTERRUPT_PENDING,
                         (uint8_t)(ap_boardreg_read8(&regs,
                                                     AP_BOARDREG_CACHE_CONTROL_ADDR) &
                                   AP_BOARDREG_CACHE_INTERRUPT_PENDING));

  /* It is a *line*, so nothing a program writes can put it down -- including a
   * write of the bit itself, which is what a storage bit would accept. */
  ap_boardreg_write8(&regs, AP_BOARDREG_CACHE_CONTROL_ADDR, 0x00u);
  TEST_ASSERT_EQUAL_HEX8(AP_BOARDREG_CACHE_INTERRUPT_PENDING,
                         (uint8_t)(ap_boardreg_read8(&regs,
                                                     AP_BOARDREG_CACHE_CONTROL_ADDR) &
                                   AP_BOARDREG_CACHE_INTERRUPT_PENDING));

  ap_boardreg_set_interrupt_pending(&regs, false);
  TEST_ASSERT_EQUAL_HEX8(AP_BOARDREG_CACHE_FIXED,
                         ap_boardreg_read8(&regs,
                                           AP_BOARDREG_CACHE_CONTROL_ADDR));
}

/* `019411-A00` §4.2.1.18's table, transcribed whole from the page image: the
 * board sizes in megabytes for slots 0-3 -- 0 for an empty slot -- and the
 * hexadecimal value the register reads. Thirty-five rows, which is every
 * configuration the manual lists and not a sample of them.
 *
 * This is the strongest test in this file, because it is the only register here
 * whose *values* were published rather than measured. If the two-bit code were
 * read wrong -- and it is not ordered by capacity, so a plausible reading gets
 * several rows right -- the table catches it: `8 8 8 8` reads 00 under both the
 * right code and a size-ordered one, `16 4 4 4` reads A9 only under the right
 * one. */
static const struct {
  unsigned slot[AP_BOARDREG_MEM_PRESENT_SLOTS];
  uint8_t value;
} PUBLISHED_MEMORY_CONFIGURATIONS[] = {
    {{0u, 0u, 0u, 0u}, 0xFFu},   /* (No Board) */
    {{4u, 0u, 0u, 0u}, 0xFEu},   {{4u, 4u, 0u, 0u}, 0xFAu},
    {{4u, 4u, 4u, 0u}, 0xEAu},   {{4u, 4u, 4u, 4u}, 0xAAu},
    {{8u, 0u, 0u, 0u}, 0xFCu},   {{8u, 4u, 0u, 0u}, 0xF8u},
    {{8u, 4u, 4u, 0u}, 0xE8u},   {{8u, 4u, 4u, 4u}, 0xA8u},
    {{8u, 8u, 0u, 0u}, 0xF0u},   {{8u, 8u, 4u, 0u}, 0xE0u},
    {{8u, 8u, 4u, 4u}, 0xA0u},   {{8u, 8u, 8u, 0u}, 0xC0u},
    {{8u, 8u, 8u, 4u}, 0x80u},   {{8u, 8u, 8u, 8u}, 0x00u},
    {{16u, 0u, 0u, 0u}, 0xFDu},  {{16u, 4u, 0u, 0u}, 0xF9u},
    {{16u, 4u, 4u, 0u}, 0xE9u},  {{16u, 4u, 4u, 4u}, 0xA9u},
    {{16u, 8u, 0u, 0u}, 0xF1u},  {{16u, 8u, 4u, 0u}, 0xE1u},
    {{16u, 8u, 4u, 4u}, 0xA1u},  {{16u, 8u, 8u, 0u}, 0xC1u},
    {{16u, 8u, 8u, 4u}, 0x81u},  {{16u, 8u, 8u, 8u}, 0x01u},
    {{16u, 16u, 0u, 0u}, 0xF5u}, {{16u, 16u, 4u, 0u}, 0xE5u},
    {{16u, 16u, 4u, 4u}, 0xA5u}, {{16u, 16u, 8u, 0u}, 0xC5u},
    {{16u, 16u, 8u, 4u}, 0x85u}, {{16u, 16u, 8u, 8u}, 0x05u},
    {{16u, 16u, 16u, 0u}, 0xD5u},{{16u, 16u, 16u, 4u}, 0x95u},
    {{16u, 16u, 16u, 8u}, 0x15u},{{16u, 16u, 16u, 16u}, 0x55u},
};

static void test_the_memory_present_register_matches_every_published_configuration(
    void) {
  ap_boardreg_t regs;
  const unsigned rows = sizeof PUBLISHED_MEMORY_CONFIGURATIONS /
                        sizeof PUBLISHED_MEMORY_CONFIGURATIONS[0];
  TEST_ASSERT_EQUAL_UINT(35u, rows);
  for (unsigned i = 0; i < rows; i++) {
    ap_boardreg_init(&regs);
    TEST_ASSERT_TRUE(ap_boardreg_set_memory_boards(
        &regs, PUBLISHED_MEMORY_CONFIGURATIONS[i].slot,
        AP_BOARDREG_MEM_PRESENT_SLOTS));
    TEST_ASSERT_EQUAL_HEX8(PUBLISHED_MEMORY_CONFIGURATIONS[i].value,
                           ap_boardreg_memory_present(&regs));
    /* And through the bus, at the address Table 2-5 gives it. */
    TEST_ASSERT_EQUAL_HEX8(
        PUBLISHED_MEMORY_CONFIGURATIONS[i].value,
        ap_boardreg_read8(&regs, AP_BOARDREG_MEMORY_PRESENT_ADDR));
  }
}

/* "These bits are cleared (0) when memory boards are present" -- so the empty
 * machine is all ones, and that is what the register holds before anything
 * tells it otherwise. */
static void test_an_unconfigured_memory_present_register_reports_no_boards(
    void) {
  ap_boardreg_t regs;
  ap_boardreg_init(&regs);
  TEST_ASSERT_EQUAL_HEX8(AP_BOARDREG_MEM_PRESENT_EMPTY,
                         ap_boardreg_memory_present(&regs));
}

/* "This 8-bit, read-only register". Which slots hold boards is not something
 * software can assert by writing it. */
static void test_the_memory_present_register_ignores_every_write(void) {
  ap_boardreg_t regs;
  const unsigned fitted[AP_BOARDREG_MEM_PRESENT_SLOTS] = {16u, 16u, 16u, 16u};
  ap_boardreg_init(&regs);
  TEST_ASSERT_TRUE(ap_boardreg_set_memory_boards(
      &regs, fitted, AP_BOARDREG_MEM_PRESENT_SLOTS));
  for (unsigned value = 0u; value < 256u; value++) {
    ap_boardreg_write8(&regs, AP_BOARDREG_MEMORY_PRESENT_ADDR, (uint8_t)value);
    TEST_ASSERT_EQUAL_HEX8(0x55u, ap_boardreg_memory_present(&regs));
  }
  ap_boardreg_write16(&regs, AP_BOARDREG_MEMORY_PRESENT_ADDR, 0x0000u);
  TEST_ASSERT_EQUAL_HEX8(0x55u, ap_boardreg_memory_present(&regs));
}

/* All four codes are spoken for, so there is no encoding left for a size the
 * table does not list -- and a caller must be told so rather than handed a
 * value that reads like a real configuration. */
static void test_a_board_size_the_manual_does_not_list_has_no_encoding(void) {
  ap_boardreg_t regs;
  unsigned code = 0u;
  const unsigned unlisted[AP_BOARDREG_MEM_PRESENT_SLOTS] = {32u, 0u, 0u, 0u};
  ap_boardreg_init(&regs);
  TEST_ASSERT_FALSE(ap_boardreg_memory_present_code(32u, &code));
  TEST_ASSERT_FALSE(ap_boardreg_memory_present_code(1u, &code));
  TEST_ASSERT_FALSE(ap_boardreg_memory_present_code(64u, &code));
  TEST_ASSERT_FALSE(ap_boardreg_set_memory_boards(
      &regs, unlisted, AP_BOARDREG_MEM_PRESENT_SLOTS));
  /* Rejected whole: slot 0 is not left encoded from a run that failed later. */
  TEST_ASSERT_EQUAL_HEX8(AP_BOARDREG_MEM_PRESENT_EMPTY,
                         ap_boardreg_memory_present(&regs));
}

/* The pair positions, asserted on their own rather than only through the value
 * table: "Bits 0 and 1 are slot 0 ... bits 6 and 7 are slot 3." */
static void test_each_memory_slot_owns_its_own_pair_of_bits(void) {
  ap_boardreg_t regs;
  for (unsigned slot = 0; slot < AP_BOARDREG_MEM_PRESENT_SLOTS; slot++) {
    unsigned sizes[AP_BOARDREG_MEM_PRESENT_SLOTS] = {0u, 0u, 0u, 0u};
    /* 16 MB is code 01, so exactly one bit of the pair clears. */
    sizes[slot] = 16u;
    ap_boardreg_init(&regs);
    TEST_ASSERT_TRUE(ap_boardreg_set_memory_boards(
        &regs, sizes, AP_BOARDREG_MEM_PRESENT_SLOTS));
    const uint8_t expected =
        (uint8_t)(AP_BOARDREG_MEM_PRESENT_EMPTY & ~(0x2u << (slot * 2u)));
    TEST_ASSERT_EQUAL_HEX8(expected, ap_boardreg_memory_present(&regs));
  }
}

/* `019411-A00` §4.2.1.14. The DS5500's `010200` is a different register from
 * the one this core measured on a DN3500 at the same address: read-only, and
 * both its named bits derived rather than stored. */
static void test_the_ds5500_cache_status_register_is_read_only(void) {
  ap_boardreg_t regs;
  ap_boardreg_init(&regs);
  ap_boardreg_set_ds5500_cache_status(&regs, true);
  const uint8_t before =
      ap_boardreg_read8(&regs, AP_BOARDREG_CACHE_CONTROL_ADDR);
  for (unsigned value = 0u; value < 256u; value++) {
    ap_boardreg_write8(&regs, AP_BOARDREG_CACHE_CONTROL_ADDR, (uint8_t)value);
    TEST_ASSERT_EQUAL_HEX8(
        before, ap_boardreg_read8(&regs, AP_BOARDREG_CACHE_CONTROL_ADDR));
  }
  /* And a DN3500's is still writable in bit 7, so this is a model difference
   * rather than the cache register having been made read-only for everyone. */
  ap_boardreg_t series4000;
  ap_boardreg_init(&series4000);
  ap_boardreg_write8(&series4000, AP_BOARDREG_CACHE_CONTROL_ADDR, 0x80u);
  TEST_ASSERT_EQUAL_HEX8(0x80u,
                         ap_boardreg_read8(&series4000,
                                           AP_BOARDREG_CACHE_CONTROL_ADDR) &
                             0x80u);
}

/* "This bit is cleared (0) to indicate that a graphics device is in the HSI
 * connector" -- so a machine with a display clears it and a headless one does
 * not. Active low, which is the polarity the section states outright. */
static void test_hsi_present_is_cleared_when_a_graphics_device_is_fitted(void) {
  ap_boardreg_t regs;
  ap_boardreg_init(&regs);
  ap_boardreg_set_ds5500_cache_status(&regs, true);

  ap_boardreg_set_hsi_graphics(&regs, true);
  TEST_ASSERT_EQUAL_HEX8(
      0u, ap_boardreg_read8(&regs, AP_BOARDREG_CACHE_CONTROL_ADDR) &
              AP_BOARDREG_CACHE_STATUS_HSI_PRESENT);

  ap_boardreg_set_hsi_graphics(&regs, false);
  TEST_ASSERT_EQUAL_HEX8(
      AP_BOARDREG_CACHE_STATUS_HSI_PRESENT,
      ap_boardreg_read8(&regs, AP_BOARDREG_CACHE_CONTROL_ADDR) &
          AP_BOARDREG_CACHE_STATUS_HSI_PRESENT);
}

/* "This bit indicates an access to non-existant memory" -- the same condition
 * the CPU status register latches, reported rather than kept twice, so the
 * selective clear that clears one clears both. */
static void test_mem_time_follows_the_latched_bus_error(void) {
  ap_boardreg_t regs;
  ap_boardreg_init(&regs);
  ap_boardreg_set_ds5500_cache_status(&regs, true);
  TEST_ASSERT_EQUAL_HEX8(
      0u, ap_boardreg_read8(&regs, AP_BOARDREG_CACHE_CONTROL_ADDR) &
              AP_BOARDREG_CACHE_STATUS_MEM_TIME);

  ap_boardreg_latch_status(&regs, AP_BOARDREG_STATUS_BUS_ERROR);
  TEST_ASSERT_EQUAL_HEX8(
      AP_BOARDREG_CACHE_STATUS_MEM_TIME,
      ap_boardreg_read8(&regs, AP_BOARDREG_CACHE_CONTROL_ADDR) &
          AP_BOARDREG_CACHE_STATUS_MEM_TIME);

  /* `019411-A00`'s "Clear Bus Error Status" clears the condition, and so the
   * bit -- which is the whole reason it is derived and not stored. */
  ap_boardreg_write8(&regs,
                     AP_BOARDREG_SELECTIVE_CLEAR_ADDR +
                         AP_BOARDREG_CLEAR_BUS_ERROR_OFFSET,
                     0u);
  TEST_ASSERT_EQUAL_HEX8(
      0u, ap_boardreg_read8(&regs, AP_BOARDREG_CACHE_CONTROL_ADDR) &
              AP_BOARDREG_CACHE_STATUS_MEM_TIME);
}


/* ## p. 4-23's names, for the run this PROM is known to post
 *
 * `002398-04` p. 4-23 tabulates the DN3000's power-on tests against their LED
 * codes, and the DN3500 uses the same numbering -- established from the post
 * routine's call sites in `3500_BOOT_12191_7`, which supply `03` through `0B`
 * inline and `0C` as an immediate at `000930`. Ten consecutive codes against ten
 * consecutive table entries.
 *
 * The argument is the byte **as written**, complemented, because the post
 * routine ends `NOT.B D0`. */
static void test_a_posted_code_is_named_for_the_evidenced_run(void) {
  /* The two ends of the run, and one in the middle. */
  TEST_ASSERT_EQUAL_STRING("bus error",
                           ap_boardreg_post_code_name((uint8_t)~0x03u));
  TEST_ASSERT_EQUAL_STRING("mmu", ap_boardreg_post_code_name((uint8_t)~0x07u));
  TEST_ASSERT_EQUAL_STRING("dma controller 2",
                           ap_boardreg_post_code_name((uint8_t)~0x0Cu));

  /* The reference boot's first posted byte is `FF`, which complements to `00` --
   * "turn off LEDs" in the table, and outside the evidenced run. It must not be
   * named, because nothing shows this PROM posts it through the post routine. */
  TEST_ASSERT_NULL(ap_boardreg_post_code_name(0xFFu));

  /* Either side of the run, and the `8x` band the same ROM posts. Naming any of
   * these would be inventing a decode for the part that is not evidenced. */
  TEST_ASSERT_NULL(ap_boardreg_post_code_name((uint8_t)~0x02u));
  TEST_ASSERT_NULL(ap_boardreg_post_code_name((uint8_t)~0x0Du));
  TEST_ASSERT_NULL(ap_boardreg_post_code_name((uint8_t)~0x0Fu));
  TEST_ASSERT_NULL(ap_boardreg_post_code_name((uint8_t)~0x82u));
  TEST_ASSERT_NULL(ap_boardreg_post_code_name((uint8_t)~0x85u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_posted_code_is_named_for_the_evidenced_run);
  RUN_TEST(test_bit_fifteen_of_the_status_register_always_reads_set);
  RUN_TEST(test_writing_the_status_register_clears_what_was_latched);
  RUN_TEST(test_no_written_bit_survives_in_the_status_register);
  RUN_TEST(test_the_cache_register_reports_the_master_request);
  RUN_TEST(test_a_status_write_clears_the_fp_trap_and_keeps_the_switch);
  RUN_TEST(test_each_selective_clear_location_clears_only_its_own);
  RUN_TEST(test_clear_all_clears_every_condition_and_nothing_else);
  RUN_TEST(test_the_graphics_trap_location_decodes_and_clears_nothing);
  RUN_TEST(test_the_control_register_stores_all_sixteen_bits);
  RUN_TEST(test_the_latch_page_register_is_read_only_and_sixteen_bits);
  RUN_TEST(test_the_cache_control_register_is_a_byte_not_a_word);
  RUN_TEST(test_only_bit_seven_of_the_cache_control_register_is_writable);
  RUN_TEST(test_a_register_is_aliased_across_its_whole_range);
  RUN_TEST(test_the_four_measured_registers_decode_and_no_others);
  RUN_TEST(test_table_two_eights_last_two_registers_store_a_byte);
  RUN_TEST(test_the_power_on_values_are_the_measured_ones);
  RUN_TEST(test_two_machines_initialised_alike_read_alike);
  RUN_TEST(test_the_normal_service_switch_is_bit_zero_and_defaults_to_normal);
  RUN_TEST(test_posted_codes_are_kept_distinct_in_order);
  RUN_TEST(test_a_posted_code_is_not_complemented_by_the_model);
  RUN_TEST(test_the_code_buffer_keeps_the_earliest_and_stops);
  RUN_TEST(test_the_memory_present_register_matches_every_published_configuration);
  RUN_TEST(test_an_unconfigured_memory_present_register_reports_no_boards);
  RUN_TEST(test_the_memory_present_register_ignores_every_write);
  RUN_TEST(test_a_board_size_the_manual_does_not_list_has_no_encoding);
  RUN_TEST(test_each_memory_slot_owns_its_own_pair_of_bits);
  RUN_TEST(test_the_ds5500_cache_status_register_is_read_only);
  RUN_TEST(test_hsi_present_is_cleared_when_a_graphics_device_is_fitted);
  RUN_TEST(test_mem_time_follows_the_latched_bus_error);
  return UNITY_END();
}
