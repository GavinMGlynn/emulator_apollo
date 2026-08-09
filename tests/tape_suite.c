/* Apollo cartridge tape as the board wires it. Placement measured;
 * `FINDINGS.md` C16-C18. */

#include "unity.h"

#include "board/ap_intr.h"
#include "board/ap_tape.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_measured_dump_is_reproduced(void) {
  ap_tape_t t;
  ap_tape_init(&t);

  /* The oracle's controller reads `00 40 FF FF FF FF FF FF` and repeats on an
   * eight-byte period. Reproduced from this core over sixteen bytes, which
   * covers the period twice and so pins the aliasing as well as the values.
   *
   * **This core reads `70` where the oracle reads `40`.** The `40` was read
   * here as "Ready at bit 6", which required RDY to be active high; the page
   * image gives it as active *low*, so the same byte means the drive is **not**
   * ready. Two bits then differ, for two separate and stated reasons:
   *
   *   bit 4, DONE: `[SC499]` says a reset "sets DONE to 1" and says it twice.
   *   Followed here; `sc499.cpp` sets only RDY. A deliberate divergence.
   *
   *   bit 5, EXC: the oracle comes up with EXCEPTION asserted, this core does
   *   not. `[SC499]` says nothing either way, so nothing is claimed -- see
   *   `ap_tape_reset`, which records it as open rather than picking a side.
   *
   *   bits 2-0: `[SC499]` p. 12 says "(BITS 0-2 Not Used)", and *not used* is
   *   not zero -- nothing drives those lines, so they read as one, which is the
   *   same rule this board already applies to the AT window at large. This core
   *   read them zero, making its idle status `70` where the hardware's is `77`.
   *   The evidence is the driver rather than the oracle: Domain/OS's tape reset
   *   waits for the status register to read exactly `F7` and then exactly `57`
   *   (`CMPI.W #$00F7` at `3C459F5A`, `#$0057` at `3C459F82`), and **both
   *   constants have these three bits set** -- so real hardware must present
   *   them as one or the driver could never have worked. MAME happens to agree
   *   from `m_status = ~(SC499_STAT_DIR | SC499_STAT_EXC)`, but that is an
   *   artefact of the complement rather than a model of the bus, and it
   *   disagrees with its own reset value of `40`.
   *
   * The aliasing, the two `00` bytes and the six `FF` bytes are unchanged
   * measurement. */
  static const uint8_t expected[16] = {
      0x00, 0x77, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0x00, 0x77, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  };
  for (unsigned i = 0; i < 16u; i++) {
    TEST_ASSERT_EQUAL_HEX8(expected[i], ap_tape_read(&t, AP_TAPE_ADDR + i));
  }
}

static void test_the_write_only_commands_are_reachable_by_writing(void) {
  ap_tape_t t;
  ap_tape_init(&t);

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
  ap_tape_init(&t);

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
  ap_tape_init(&t);

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
  ap_tape_init(&t);
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

/* The suite's clock. §1.13.2's handshake takes time now, so a test that issues
 * a command and looks at the result immediately is asking what the device looks
 * like mid-handshake -- which is a real question, and not the one most of these
 * tests are asking. */
static ap_time_t clock_now;

static void arm(ap_tape_t *t) {
  for (unsigned i = 0; i < sizeof cartridge; i++) {
    cartridge[i] = (uint8_t)(0x40u + (i & 0x3Fu));
  }
  clock_now = 0u;
  ap_tape_init(t);
  TEST_ASSERT_TRUE(ap_tape_load(t, cartridge, sizeof cartridge,
                                AP_QIC_CARTRIDGE_DC600A, true));
}

/* Issue a QIC command through the controller, as a driver would: set the
 * request bit in the control register, then write the opcode to the data
 * register. */
static void issue(ap_tape_t *t, uint8_t command) {
  ap_tape_write(t, AP_TAPE_ADDR + 1u, AP_SC499_CTL_REQUEST);
  ap_tape_write(t, AP_TAPE_ADDR + 0u, command);
  /* Then wait for READY, which is what a driver does before it issues the next
   * command. The longest figure covers whichever one this entered by. */
  clock_now += ap_sc499_handshake_duration(AP_SC499_ENTRY_READY) +
               ap_sc499_handshake_duration(AP_SC499_ENTRY_DIRECTION);
  ap_tape_advance(t, clock_now);
}

/* READY marks the block boundary, which the data path used to hide.
 *
 * `[SC499]` §1.13.1: "The READY line is activated when the device is ready for
 * a **data block** transfer", and Figure 1-5 shows it going down once the
 * controller starts a block (T4) and back up at T15, "Device READY For Next
 * Data Block", `100 us. < T14--->T15`.
 *
 * `ensure_block` used to fetch the next block transparently, so a host saw an
 * unbroken byte stream and READY never moved during a transfer -- a driver
 * waiting for the edge between blocks waited for an edge that never came. */
static void test_ready_drops_and_returns_at_each_data_block(void) {
  ap_tape_t t;
  arm(&t);
  issue(&t, AP_QIC_CMD_SELECT);
  issue(&t, AP_QIC_CMD_READ);

  /* The first byte of a block pulls a block from the drive, so READY drops.
   * `RDY` is **active low**, so the line being down is the bit reading 1. */
  (void)ap_tape_read(&t, AP_TAPE_ADDR + 0u);
  TEST_ASSERT_EQUAL_HEX8(AP_SC499_ST_RDY,
                         ap_tape_read(&t, AP_TAPE_ADDR + 1u) &
                             AP_SC499_ST_RDY);

  /* It stays down for less than the documented gap ... */
  clock_now += ap_sc499_handshake_duration(AP_SC499_ENTRY_DATA_BLOCK) / 2u;
  ap_tape_advance(&t, clock_now);
  TEST_ASSERT_EQUAL_HEX8(AP_SC499_ST_RDY,
                         ap_tape_read(&t, AP_TAPE_ADDR + 1u) &
                             AP_SC499_ST_RDY);

  /* ... and comes back once it has passed. */
  clock_now += ap_sc499_handshake_duration(AP_SC499_ENTRY_DATA_BLOCK);
  ap_tape_advance(&t, clock_now);
  TEST_ASSERT_EQUAL_HEX8(0u, ap_tape_read(&t, AP_TAPE_ADDR + 1u) &
                                 AP_SC499_ST_RDY);

  /* Reading on within the same block does **not** move it: the boundary is the
   * block, not the byte. That is the distinction §1.13.1 draws and the one this
   * core had wrong in its own header. */
  for (unsigned i = 0; i < 8u; i++) {
    (void)ap_tape_read(&t, AP_TAPE_ADDR + 0u);
  }
  TEST_ASSERT_EQUAL_HEX8(0u, ap_tape_read(&t, AP_TAPE_ADDR + 1u) &
                                 AP_SC499_ST_RDY);
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

/* RDY and EXC are asserted **low** -- `[SC499]`'s page image carries a polarity
 * column its text layer drops, and Linux and the oracle both agree. Named here
 * rather than written as a flipped hex constant at each site, because
 * "asserted" is what each test means and `== 0` is only how it is spelled. */
static bool exception_asserted(ap_tape_t *t) {
  return (ap_tape_read(t, AP_TAPE_ADDR + 1u) & AP_SC499_ST_EXC) == 0u;
}

static bool ready_asserted(ap_tape_t *t) {
  return (ap_tape_read(t, AP_TAPE_ADDR + 1u) & AP_SC499_ST_RDY) == 0u;
}

static void test_a_refused_command_raises_exception(void) {
  ap_tape_t t;
  arm(&t);
  issue(&t, AP_QIC_CMD_SELECT);

  /* The status register is the only channel the controller has for saying no,
   * so a command the drive refuses must show there rather than vanish. WRITE is
   * refused because there is no write-back path. */
  /* WRITE FILE MARK, not WRITE: WRITE now places a block on a writable
   * cartridge, so it is no longer an example of a refused command. A raw block
   * image has no file marks, so this one still is. */
  issue(&t, AP_QIC_CMD_WRITE_FILE_MARK);
  TEST_ASSERT_TRUE(exception_asserted(&t));
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
  TEST_ASSERT_TRUE(exception_asserted(&t));
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
  TEST_ASSERT_TRUE(exception_asserted(&t));
  TEST_ASSERT_FALSE(ready_asserted(&t));
}

static void test_a_command_clears_an_exception(void) {
  ap_tape_t t;
  arm(&t);
  issue(&t, AP_QIC_CMD_SELECT);
  issue(&t, AP_QIC_CMD_WRITE_FILE_MARK); /* refused, raises exception */
  TEST_ASSERT_TRUE(exception_asserted(&t));

  /* Figure 1-8: on a command issued while EXCEPTION is up the device deasserts
   * EXCEPTION and then asserts READY. So a driver recovers by commanding, not
   * by reading -- and the ready bit comes back with it. */
  issue(&t, AP_QIC_CMD_BOT);
  TEST_ASSERT_FALSE(exception_asserted(&t));
  TEST_ASSERT_TRUE(ready_asserted(&t));
}

static void test_reading_the_tape_makes_the_device_hold_the_bus(void) {
  ap_tape_t t;
  arm(&t);
  issue(&t, AP_QIC_CMD_SELECT);
  issue(&t, AP_QIC_CMD_READ);

  /* Figures 1-6 and 1-10 both open with the device changing DIRECTION to
   * deliver data. It holds the bus afterwards, which is precisely the state
   * Figure 1-9's command transfer exists to resolve. */
  (void)ap_tape_read(&t, AP_TAPE_ADDR + 0u);
  TEST_ASSERT_EQUAL_UINT(AP_SC499_ENTRY_DIRECTION,
                         ap_sc499_command_entry(&t.controller));

  /* And a command takes it back, per Figure 1-9's T4. */
  issue(&t, AP_QIC_CMD_BOT);
  TEST_ASSERT_EQUAL_UINT(AP_SC499_ENTRY_READY,
                         ap_sc499_command_entry(&t.controller));
}

/* §1.13.2's handshake takes time, and a driver that does not wait sees it. This
 * is the whole of what the timing adds over the ordering: a command issued is
 * not a command finished. */
static void test_a_command_is_not_finished_when_it_is_issued(void) {
  ap_tape_t t;
  arm(&t);
  issue(&t, AP_QIC_CMD_SELECT);

  /* Issue without the wait `issue` normally does. */
  ap_tape_write(&t, AP_TAPE_ADDR + 1u, AP_SC499_CTL_REQUEST);
  ap_tape_write(&t, AP_TAPE_ADDR + 0u, AP_QIC_CMD_BOT);

  /* READY is down at once -- the device has taken the command and is working.
   * A driver polling here correctly waits. */
  TEST_ASSERT_FALSE(ready_asserted(&t));
  TEST_ASSERT_TRUE(ap_sc499_executing(&t.controller));

  /* Still down a microsecond in: Figure 1-7's execution bound is half a
   * second, so this is nowhere near. */
  clock_now += 19800u;
  ap_tape_advance(&t, clock_now);
  TEST_ASSERT_FALSE(ready_asserted(&t));

  /* And up once the figure's interval has passed. */
  clock_now += ap_sc499_handshake_duration(AP_SC499_ENTRY_READY);
  ap_tape_advance(&t, clock_now);
  TEST_ASSERT_TRUE(ready_asserted(&t));
  TEST_ASSERT_FALSE(ap_sc499_executing(&t.controller));
}

/* Figure 1-8's recovery is the interval that is *shortest*, and the exception
 * stays up across it -- a driver reading status mid-handshake sees the device
 * still holding the condition, because it is. */
static void test_an_exception_survives_until_its_figure_completes(void) {
  ap_tape_t t;
  arm(&t);
  issue(&t, AP_QIC_CMD_SELECT);
  issue(&t, AP_QIC_CMD_WRITE_FILE_MARK); /* refused, raises exception */
  TEST_ASSERT_TRUE(exception_asserted(&t));

  ap_tape_write(&t, AP_TAPE_ADDR + 1u, AP_SC499_CTL_REQUEST);
  ap_tape_write(&t, AP_TAPE_ADDR + 0u, AP_QIC_CMD_BOT);
  TEST_ASSERT_TRUE(exception_asserted(&t));

  clock_now += ap_sc499_handshake_duration(AP_SC499_ENTRY_EXCEPTION) - 1u;
  ap_tape_advance(&t, clock_now);
  TEST_ASSERT_TRUE(exception_asserted(&t));

  clock_now += 1u;
  ap_tape_advance(&t, clock_now);
  TEST_ASSERT_FALSE(exception_asserted(&t));
  TEST_ASSERT_TRUE(ready_asserted(&t));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_reading_the_tape_makes_the_device_hold_the_bus);
  RUN_TEST(test_a_command_is_not_finished_when_it_is_issued);
  RUN_TEST(test_an_exception_survives_until_its_figure_completes);
  RUN_TEST(test_ready_and_exception_are_never_both_asserted);
  RUN_TEST(test_a_command_clears_an_exception);
  RUN_TEST(test_ready_drops_and_returns_at_each_data_block);
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
