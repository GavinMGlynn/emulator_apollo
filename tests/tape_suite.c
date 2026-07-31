/* Apollo cartridge tape as the board wires it. Placement measured;
 * `FINDINGS.md` C16-C18. */

#include "unity.h"

#include "board/ap_intr.h"
#include "board/ap_tape.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_measured_dump_is_reproduced(void) {
  ap_tape_t t;
  ap_tape_reset(&t);

  /* The oracle's controller reads `00 40 FF FF FF FF FF FF` and repeats on an
   * eight-byte period. Reproduced from this core over sixteen bytes, which
   * covers the period twice and so pins the aliasing as well as the values.
   *
   * The `40` is Ready at bit 6 -- the measurement that supplied the status
   * register's bit numbers, which the guide's own scan had lost. */
  static const uint8_t expected[16] = {
      0x00, 0x40, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0x00, 0x40, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  };
  for (unsigned i = 0; i < 16u; i++) {
    TEST_ASSERT_EQUAL_HEX8(expected[i], ap_tape_read(&t, AP_TAPE_ADDR + i));
  }
}

static void test_the_write_only_commands_are_reachable_by_writing(void) {
  ap_tape_t t;
  ap_tape_reset(&t);

  /* The dump reads `FF` at offsets 2 and 3, and for a while that looked like
   * the end of the part. They are write-triggered DMA commands: invisible to a
   * read and perfectly reachable by a write. */
  TEST_ASSERT_EQUAL_HEX8(0xFF, ap_tape_read(&t, AP_TAPE_ADDR + 2u));
  ap_tape_write(&t, AP_TAPE_ADDR + 2u, 0x00);
  TEST_ASSERT_TRUE(t.controller.dma_active);

  ap_tape_write(&t, AP_TAPE_ADDR + 3u, 0x00);
  TEST_ASSERT_FALSE(t.controller.dma_active);
}

static void test_the_upper_half_of_each_block_is_not_the_part(void) {
  ap_tape_t t;
  unsigned reg;
  ap_tape_reset(&t);

  /* Four registers in eight addresses. Folding offsets 4 to 7 back onto them
   * would give a driver four aliases the hardware does not offer -- and would
   * make a stray write to offset 4 reset the DMA logic. */
  TEST_ASSERT_TRUE(ap_tape_decode(AP_TAPE_ADDR + 3u, &reg));
  TEST_ASSERT_FALSE(ap_tape_decode(AP_TAPE_ADDR + 4u, &reg));

  ap_tape_write(&t, AP_TAPE_ADDR + 2u, 0x00);
  ap_tape_write(&t, AP_TAPE_ADDR + 7u, 0x00); /* would be RSTDMA if aliased */
  TEST_ASSERT_TRUE(t.controller.dma_active);
}

static void test_the_registers_alias_on_an_eight_byte_period(void) {
  ap_tape_t t;
  ap_tape_reset(&t);

  ap_tape_write(&t, AP_TAPE_ADDR + 8u, 0x5A); /* the data register again */
  TEST_ASSERT_EQUAL_HEX8(0x5A, ap_tape_read(&t, AP_TAPE_ADDR + 0u));
  TEST_ASSERT_EQUAL_HEX8(0x5A, ap_tape_read(&t, AP_TAPE_ADDR + 0xF8u));
}

static void test_nothing_outside_the_range_decodes(void) {
  unsigned reg;
  TEST_ASSERT_FALSE(ap_tape_decode(0x04FF00u, &reg));
  TEST_ASSERT_FALSE(ap_tape_decode(0x051000u, &reg)); /* network interface */
  TEST_ASSERT_FALSE(ap_tape_decode(0x04D000u, &reg)); /* the Winchester */
}

static void test_the_tape_raises_its_documented_interrupt(void) {
  ap_tape_t t;
  ap_intr_t intr;
  ap_tape_reset(&t);
  ap_intr_reset(&intr);

  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 0u, 0x11);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0xA0);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0x08);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0x01);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0x00);

  /* Enable interrupts and satisfy the flag's conjunction -- Ready alone does not
   * raise it, which is what the reset dump of `40` established. */
  ap_tape_write(&t, AP_TAPE_ADDR + 1u, AP_SC499_CTL_IEN);
  t.controller.exception = true;
  TEST_ASSERT_TRUE(ap_tape_irq(&t));

  ap_intr_set_request(&intr, AP_TAPE_IRQ, ap_tape_irq(&t));
  /* `008778-03` Table 2-3: "IRQ5 ... Tape Drive", so vector `A5`. */
  TEST_ASSERT_EQUAL_HEX8(0xA5, ap_intr_acknowledge(&intr));
}

/* A tiny cartridge, built by the test -- `media/` is gitignored. */
static uint8_t cartridge[AP_CT_BLOCK_SIZE * 2u];

static void arm(ap_tape_t *t) {
  for (unsigned i = 0; i < sizeof cartridge; i++) {
    cartridge[i] = (uint8_t)(0x40u + (i & 0x3Fu));
  }
  ap_tape_reset(t);
  TEST_ASSERT_TRUE(ap_tape_load(t, cartridge, sizeof cartridge,
                                AP_QIC_CARTRIDGE_DC600A));
}

/* Issue a QIC command through the controller, as a driver would: set the
 * request bit in the control register, then write the opcode to the data
 * register. */
static void issue(ap_tape_t *t, uint8_t command) {
  ap_tape_write(t, AP_TAPE_ADDR + 1u, AP_SC499_CTL_REQUEST);
  ap_tape_write(t, AP_TAPE_ADDR + 0u, command);
}

static void test_an_idle_controller_still_reads_as_measured(void) {
  ap_tape_t t;
  arm(&t);

  /* With a cartridge loaded but no transfer running, the data register is the
   * controller's own and reads `00` -- the measured value. The drive only fills
   * it during a READ, and conflating the two made this dump stop reproducing. */
  TEST_ASSERT_EQUAL_HEX8(0x00, ap_tape_read(&t, AP_TAPE_ADDR + 0u));
}

static void test_a_command_reaches_the_drive_through_the_registers(void) {
  ap_tape_t t;
  arm(&t);

  /* Control bit 6 is "Request to LSI chip", so a data-register write with it
   * set is a command rather than data. */
  issue(&t, AP_QIC_CMD_SELECT);
  TEST_ASSERT_TRUE(t.drive.selected);

  issue(&t, AP_QIC_CMD_READ);
  TEST_ASSERT_TRUE(t.drive.reading);
}

static void test_the_tape_is_read_through_the_data_register(void) {
  ap_tape_t t;
  arm(&t);
  issue(&t, AP_QIC_CMD_SELECT);
  issue(&t, AP_QIC_CMD_READ);

  /* A byte per access, in order, across the block boundary the drive works in
   * -- the controller transfers bytes and the drive blocks, so the join has to
   * carry the difference. */
  for (unsigned i = 0; i < AP_CT_BLOCK_SIZE + 4u; i++) {
    TEST_ASSERT_EQUAL_HEX8(cartridge[i], ap_tape_read(&t, AP_TAPE_ADDR + 0u));
  }
}

static void test_a_refused_command_raises_exception(void) {
  ap_tape_t t;
  arm(&t);
  issue(&t, AP_QIC_CMD_SELECT);

  /* The status register is the only channel the controller has for saying no,
   * so a command the drive refuses must show there rather than vanish. WRITE is
   * refused because there is no write-back path. */
  issue(&t, AP_QIC_CMD_WRITE);
  TEST_ASSERT_EQUAL_HEX8(AP_SC499_ST_EXC,
                         ap_tape_read(&t, AP_TAPE_ADDR + 1u) & AP_SC499_ST_EXC);
}

static void test_running_off_the_end_raises_exception(void) {
  ap_tape_t t;
  arm(&t);
  issue(&t, AP_QIC_CMD_SELECT);
  issue(&t, AP_QIC_CMD_READ);

  for (unsigned i = 0; i < sizeof cartridge; i++) {
    (void)ap_tape_read(&t, AP_TAPE_ADDR + 0u);
  }
  /* One past the end. `[SC499]`'s EXC comes "from LSI chip", and the end of a
   * cartridge is exactly such a condition -- a driver reading on gets an
   * exception rather than the tape silently wrapping. */
  (void)ap_tape_read(&t, AP_TAPE_ADDR + 0u);
  TEST_ASSERT_EQUAL_HEX8(AP_SC499_ST_EXC,
                         ap_tape_read(&t, AP_TAPE_ADDR + 1u) & AP_SC499_ST_EXC);
}

static void test_ready_and_exception_are_never_both_asserted(void) {
  ap_tape_t t;
  arm(&t);
  issue(&t, AP_QIC_CMD_SELECT);
  issue(&t, AP_QIC_CMD_READ);
  for (unsigned i = 0; i < sizeof cartridge; i++) {
    (void)ap_tape_read(&t, AP_TAPE_ADDR + 0u);
  }
  (void)ap_tape_read(&t, AP_TAPE_ADDR + 0u); /* past the end */

  /* `[SC499]` Figure 1-6: "READY shall not be asserted for an EXCEPTION
   * condition." The two are exclusive by specification, so a driver polling
   * status must never see both -- a state the device cannot be in. */
  uint8_t status = ap_tape_read(&t, AP_TAPE_ADDR + 1u);
  TEST_ASSERT_EQUAL_HEX8(AP_SC499_ST_EXC, status & AP_SC499_ST_EXC);
  TEST_ASSERT_EQUAL_HEX8(0, status & AP_SC499_ST_RDY);
}

static void test_a_command_clears_an_exception(void) {
  ap_tape_t t;
  arm(&t);
  issue(&t, AP_QIC_CMD_SELECT);
  issue(&t, AP_QIC_CMD_WRITE); /* refused, raises exception */
  TEST_ASSERT_EQUAL_HEX8(AP_SC499_ST_EXC,
                         ap_tape_read(&t, AP_TAPE_ADDR + 1u) & AP_SC499_ST_EXC);

  /* Figure 1-8: on a command issued while EXCEPTION is up the device deasserts
   * EXCEPTION and then asserts READY. So a driver recovers by commanding, not
   * by reading -- and the ready bit comes back with it. */
  issue(&t, AP_QIC_CMD_BOT);
  uint8_t status = ap_tape_read(&t, AP_TAPE_ADDR + 1u);
  TEST_ASSERT_EQUAL_HEX8(0, status & AP_SC499_ST_EXC);
  TEST_ASSERT_EQUAL_HEX8(AP_SC499_ST_RDY, status & AP_SC499_ST_RDY);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_ready_and_exception_are_never_both_asserted);
  RUN_TEST(test_a_command_clears_an_exception);
  RUN_TEST(test_an_idle_controller_still_reads_as_measured);
  RUN_TEST(test_a_command_reaches_the_drive_through_the_registers);
  RUN_TEST(test_the_tape_is_read_through_the_data_register);
  RUN_TEST(test_a_refused_command_raises_exception);
  RUN_TEST(test_running_off_the_end_raises_exception);
  RUN_TEST(test_the_measured_dump_is_reproduced);
  RUN_TEST(test_the_write_only_commands_are_reachable_by_writing);
  RUN_TEST(test_the_upper_half_of_each_block_is_not_the_part);
  RUN_TEST(test_the_registers_alias_on_an_eight_byte_period);
  RUN_TEST(test_nothing_outside_the_range_decodes);
  RUN_TEST(test_the_tape_raises_its_documented_interrupt);
  return UNITY_END();
}
