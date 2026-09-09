/* Apollo cartridge tape as the board wires it. Placement measured;
 * `FINDINGS.md` C16-C18. */

#include "unity.h"

#include "board/ap_intr.h"
#include "board/ap_tape.h"

void setUp(void) {}
void tearDown(void) {}

static void test_a_cold_power_on_runs_the_confidence_test_and_asserts_exception(void) {
  ap_tape_t tape;
  ap_tape_init(&tape);

  /* `[SC499]` §1.8.1: the power-on confidence test checks microprocessor RAM,
   * the LSI controller, the 16K RAM and the data separator, and reports success
   * "by the assertion of `EXC-` within five seconds". `[08845]` Table 2.0's
   * `KK` row is what says this card runs it -- Apollo's asterisk on "IN = TEST
   * AT POWER-ON OR RESET", where OUT is TEST DISABLED.
   *
   * Before the deadline it asserts nothing, which is the state a driver polls
   * through: `[SC499]` Figure 1-24's DONE loops on READY-or-EXCEPTION. */
  ap_tape_advance(&tape, 1u);
  TEST_ASSERT_FALSE(tape.controller.exception);

  ap_tape_advance(&tape, 1u + AP_SC499_T_RESET_TO_EXCEPTION);
  TEST_ASSERT_TRUE(tape.controller.exception);

  /* And it is inside the published bound, which is the half of §1.8.1 that is a
   * number rather than a behaviour. */
  TEST_ASSERT_TRUE(AP_SC499_T_RESET_TO_EXCEPTION <= AP_SC499_US(5000000));
}

static void test_the_machines_reset_runs_the_confidence_test_too(void) {
  ap_tape_t tape;
  ap_tape_init(&tape);
  ap_tape_advance(&tape, 1u);
  ap_tape_advance(&tape, 1u + AP_SC499_T_RESET_TO_EXCEPTION);
  TEST_ASSERT_TRUE(tape.controller.exception);

  /* §1.12 makes the machine's own reset RESET DRV, "the power-on reset from the
   * IBM PC power supply", and `[08845]`'s `KK` covers it with "or reset" -- so
   * a reset re-runs the test rather than leaving the card silent. The exception
   * is cleared by the reset itself and comes back at the deadline, so a driver
   * that resets the card twice sees two of them rather than one that never
   * went away. */
  ap_tape_reset(&tape);
  ap_tape_advance(&tape, 2u + AP_SC499_T_RESET_TO_EXCEPTION);
  TEST_ASSERT_FALSE(tape.controller.exception);

  ap_tape_advance(&tape, 2u + 2u * AP_SC499_T_RESET_TO_EXCEPTION);
  TEST_ASSERT_TRUE(tape.controller.exception);
}

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
   *   bit 7, IRQF: **active low**, as the page image prints it. An idle
   *   controller has nothing asserted, so the flag is inactive and the bit
   *   stands at one. This core had it active high on the strength of a dump of
   *   the *oracle*, which is MAME's model of the bit rather than the hardware's
   *   behaviour; the guest settles it, since `F7` is exactly what the tape
   *   driver waits for.
   *
   * The aliasing, the two `00` bytes and the six `FF` bytes are unchanged
   * measurement. */
  static const uint8_t expected[16] = {
      0x00, 0xF7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0x00, 0xF7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
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
  TEST_ASSERT_EQUAL_HEX8(0x5A, ap_tape_dma_read(&t));
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

  /* **Let the power-on confidence test finish**, which every test below wants
   * and none of them used to say. `[SC499]` §1.8.1 has the card report its POC
   * "by the assertion of `EXC-` within five seconds", and until 2026-09-09 this
   * core ran no POC at all, so a card was born silent and these tests were
   * written against that. It is not a detail that can be skipped: the POC
   * deadline is 200 ms and `AP_SC499_T_COMMAND_EXECUTION` is 500 ms, so the
   * exception lands *inside* the first command every one of them issues.
   *
   * Two advances, because the arm is dated at the first: `ap_sc499_reset`
   * clears `now` along with everything else, so there is no instant to date
   * from until a caller supplies one.
   *
   * The exception is deliberately **left standing**. Clearing it here would be
   * reaching into the part; the hardware's own way out is the next command --
   * `[SC499]` Figure 1-8's "Device Deasserts EXCEPTION", which is the entry a
   * command takes when there is one to lift -- and every test's first `issue`
   * does exactly that. */
  ap_tape_advance(t, 1u);
  clock_now = 1u + AP_SC499_T_RESET_TO_EXCEPTION;
  ap_tape_advance(t, clock_now);
  TEST_ASSERT_TRUE(t->controller.exception);
}

/* Issue a QIC command through the controller, as a driver would: set the
 * request bit in the control register, then write the opcode to the data
 * register. */
static void issue(ap_tape_t *t, uint8_t command) {
  /* `[SC499]` Figure 1-26, the guide's own SEND COMMAND flow chart, and
   * `QIC-02` §3.6.3's eight numbered events. Four edges, not two:
   *
   *     COMMAND BYTE TO DATA BUS DRIVERS   T1
   *     ASSERT REQUEST                     T2
   *     READY?  -- wait for it             T4
   *     DROP REQUEST                       T5
   *     READY?* -- wait for it to go       T7   *20 usec loop max
   *
   * This helper did the first two and stopped, which is exactly what the device
   * did, so every test in this file agreed with the model about a handshake
   * neither of them finished. */
  ap_tape_write(t, AP_TAPE_ADDR + 0u, command);
  ap_tape_write(t, AP_TAPE_ADDR + 1u, AP_SC499_CTL_REQUEST);
  /* Wait for READY. The longest figure covers whichever one this entered by. */
  clock_now += ap_sc499_handshake_duration(AP_SC499_ENTRY_READY) +
               ap_sc499_handshake_duration(AP_SC499_ENTRY_DIRECTION);
  ap_tape_advance(t, clock_now);
  ap_tape_write(t, AP_TAPE_ADDR + 1u, 0u);
  clock_now += AP_SC499_T_CLOSE_MIN + AP_SC499_T_READY_REOPEN;
  ap_tape_advance(t, clock_now);
}

/* Take one byte of a device-to-host block the way Figure 1-25 does: read the
 * bus, assert REQUEST to say it has been taken, wait, drop REQUEST, and wait
 * for the device to put the next one up. */
static uint8_t take_byte(ap_tape_t *t) {
  const uint8_t byte = ap_tape_read(t, AP_TAPE_ADDR + 0u);
  ap_tape_write(t, AP_TAPE_ADDR + 1u, AP_SC499_CTL_REQUEST);
  ap_tape_write(t, AP_TAPE_ADDR + 1u, 0u);
  clock_now += AP_SC499_T_CLOSE_MIN + AP_SC499_T_READY_REOPEN;
  ap_tape_advance(t, clock_now);
  return byte;
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
  (void)ap_tape_dma_read(&t);
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
    (void)ap_tape_dma_read(&t);
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
  TEST_ASSERT_EQUAL_HEX8(0x00, ap_tape_dma_read(&t));
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

/* **Renamed 2026-09-09, because the old name was the defect.** This was
 * `test_the_tape_is_read_through_the_data_register`, and the tape is not: it is
 * read through `DACK`. A programmed read of `BASE+0` returns the card's own
 * data register -- the status block, when one is open -- and takes nothing off
 * the tape. See `ap_tape.c`, and `test_a_programmed_read_takes_no_tape_byte`
 * below for the half this one used to assert backwards. */
static void test_the_tape_is_read_a_byte_at_a_time_through_dack(void) {
  ap_tape_t t;
  arm(&t);
  issue(&t, AP_QIC_CMD_SELECT);
  issue(&t, AP_QIC_CMD_READ);

  /* A byte per access, in order, across the block boundary the drive works in
   * -- the controller transfers bytes and the drive blocks, so the join has to
   * carry the difference. */
  for (unsigned i = 0; i < AP_CT_BLOCK_SIZE + 4u; i++) {
    TEST_ASSERT_EQUAL_HEX8(cartridge[i], ap_tape_dma_read(&t));
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
  /* ERASE, not WRITE and no longer WRITE FILE MARK: WRITE places a block on a
   * writable cartridge and WRITE FILE MARK writes a mark, now that the format
   * is known to have them. ERASE is still refused, because it would rewrite a
   * whole distribution image. */
  issue(&t, AP_QIC_CMD_ERASE);
  TEST_ASSERT_TRUE(exception_asserted(&t));
}

/* **A read the drive ends also ends the DMA transfer.**
 *
 * `[SC499]` §1.9 calls DONE "Done, from DMA logic" and §1.11 makes DMAGO the
 * start of a transfer, so DONE up is a sequencer with nothing in flight. The
 * 8237's terminal count is one way a transfer ends; the **drive** running out
 * is the other, and this core modelled only the first.
 *
 * It cannot be otherwise. A READ ends at a file mark, a mark falls where the
 * tape's structure puts it, and so the last DMAGO of every file is short by
 * construction -- a card that only raised DONE at the host's byte count could
 * never let a host read a file to its end.
 *
 * Measured on the SR10.4 boot cartridge: with the file mark stopping the read
 * at block 16 the transfer stalled one byte into its seventeenth block --
 * `dma1 ch1 count 01FE (base 01FF)` -- with EXCEPTION asserted, `exs 8100`
 * (`ST0 | FIL`), and DONE still clear. `002398-04` p. 4-17's `FF`, "timeout
 * waiting for controller done", is what the firmware printed.
 * `FINDINGS.md` C266. */
static void test_a_read_the_drive_ends_also_ends_the_dma(void) {
  ap_tape_t t;
  arm(&t);
  issue(&t, AP_QIC_CMD_SELECT);
  issue(&t, AP_QIC_CMD_READ);

  /* A transfer is in flight: DMAGO down, and the drive feeding it. */
  ap_tape_write(&t, AP_TAPE_ADDR + 2u, 0u);
  TEST_ASSERT_FALSE(t.controller.done);
  (void)ap_tape_dma_read(&t);
  TEST_ASSERT_FALSE(t.controller.done);

  /* Run the two blocks of this cartridge out. `arm` builds no file mark, so
   * this is the end-of-tape half of the same event -- one signal to a host,
   * told apart by the status block. */
  /* Three blocks, not two: the cartridge holds two and the first is handed over
   * twice. See `ap_tape_t::first_block_pending`. */
  for (unsigned i = 1; i < AP_CT_BLOCK_SIZE * 3u; i++) {
    clock_now += ap_sc499_handshake_duration(AP_SC499_ENTRY_DATA_BLOCK);
    ap_tape_advance(&t, clock_now);
    (void)ap_tape_dma_read(&t);
  }
  TEST_ASSERT_FALSE(t.controller.done);

  /* And the byte past the end ends it: nothing is in flight any more. */
  TEST_ASSERT_EQUAL_HEX8(0xFFu, ap_tape_dma_read(&t));
  TEST_ASSERT_TRUE(t.controller.done);
  TEST_ASSERT_TRUE(t.controller.exception);
  TEST_ASSERT_TRUE((ap_tape_read(&t, AP_TAPE_ADDR + 1u) & AP_SC499_ST_DONE) !=
                   0u);
}

/* **The drive stops asking before the cycle, not after it.**
 *
 * `QIC-02 Rev D` §3.6.6's T38 asserts EXCEPTION at the file mark, and the drive
 * asserts it because the tape passed one -- not because a host asked for a byte
 * it cannot have. Modelled as a *failed read*, the ending only happened when
 * something demanded data, and under DMA a demand costs a bus cycle and
 * delivers a byte: the SR10.4 boot's seventeenth transfer moved one invented
 * `FF` into the host's buffer before the mark stopped it, `dma1 ch1 count 01FE
 * (base 01FF)`, and MD reported `002398-04` p. 4-17's `36`, "bad block
 * transferred" -- which is what a short block is.
 *
 * A card cannot transfer a byte the drive never sent. `FINDINGS.md` C267. */
static void test_the_drive_stops_asking_at_a_file_mark(void) {
  ap_tape_t t;
  arm(&t);
  /* The second of the two blocks is a mark, so the first is a whole file. */
  for (unsigned i = 0; i < AP_CT_BLOCK_SIZE; i++) {
    cartridge[AP_CT_BLOCK_SIZE + i] =
        (uint8_t)(AP_CT_FILE_MARK_WORD >> (8u * (3u - (i & 3u))));
  }
  issue(&t, AP_QIC_CMD_SELECT);
  issue(&t, AP_QIC_CMD_READ);
  ap_tape_write(&t, AP_TAPE_ADDR + 2u, 0u); /* DMAGO */
  TEST_ASSERT_FALSE(t.controller.done);

  /* The whole first block comes out under DMA, **one byte-time apart**: the
   * drive is 90,000 bytes a second and the line goes down in between, which is
   * what stops a block crossing the interface faster than the media can supply
   * it. `FINDINGS.md` C268. */
  for (unsigned i = 0; i < AP_CT_BLOCK_SIZE; i++) {
    if (i > 0u) {
      /* The wait is taken *before* the byte, so the loop leaves the tape where
       * the last byte left it rather than one byte-time past the end -- which
       * is where the ending below is asserted from. */
      clock_now += AP_SC499_T_BYTE;
      ap_tape_advance(&t, clock_now);
    }
    TEST_ASSERT_TRUE(ap_tape_dma_request(&t));
    TEST_ASSERT_EQUAL_HEX8(cartridge[i], ap_tape_dma_read(&t));
    /* And down again until the next is due: the line is paced, not a level
     * held for a whole block. */
    TEST_ASSERT_FALSE(ap_tape_dma_request(&t));
  }

  /* And then it goes down, **without a byte having been taken from the mark**.
   * A request the drive cannot answer is a bus cycle whose only product is an
   * invented byte. */
  TEST_ASSERT_FALSE(ap_tape_dma_request(&t));
  TEST_ASSERT_TRUE(t.drive.reading);

  /* The ending lands with the clock, where the tape reaches the mark: `FIL`
   * latched, EXCEPTION up, and the sequencer with nothing in flight. One tick
   * is enough -- the ending is a fact about the tape's position, not a
   * deadline. */
  clock_now += 1u;
  ap_tape_advance(&t, clock_now);
  TEST_ASSERT_FALSE(t.drive.reading);
  TEST_ASSERT_TRUE(t.drive.file_mark);
  TEST_ASSERT_TRUE(t.controller.exception);
  TEST_ASSERT_TRUE(t.controller.done);
  /* Past the mark, so the next READ begins the next file. */
  TEST_ASSERT_EQUAL_UINT64(2u, t.drive.position);

  /* And the exception stands, with READY down under it -- Figure 1-6's rule.
   * That a *block boundary's* completion must not lift it is `sc499_suite`'s to
   * assert directly: with the media time charged per byte the gap is 100 us and
   * a block takes 5.69 ms, so a boundary is never still in flight when a read
   * ends, and this test can no longer build the collision that found it. */
  clock_now += ap_sc499_handshake_duration(AP_SC499_ENTRY_DATA_BLOCK) * 2u;
  ap_tape_advance(&t, clock_now);
  TEST_ASSERT_TRUE(t.controller.exception);
  TEST_ASSERT_FALSE(t.controller.ready);

  /* **And the host's next DMAGO does not hang.** A driver that reads a file by
   * repeating `[SC499]` §1.11's steps 2 to 5 issues one DMAGO per block and
   * only learns the file has ended when one comes back short -- so there is
   * always one DMAGO after the last block. It lowers DONE, and if nothing
   * raised it again the driver would wait for ever, which is `002398-04`
   * p. 4-17's `FF`. A sequencer with no transfer in front of it has nothing in
   * flight. */
  ap_tape_write(&t, AP_TAPE_ADDR + 2u, 0u);
  TEST_ASSERT_FALSE(t.controller.done);
  TEST_ASSERT_FALSE(ap_tape_dma_request(&t));
  clock_now += 1u;
  ap_tape_advance(&t, clock_now);
  TEST_ASSERT_TRUE(t.controller.done);
}

static void test_running_off_the_end_raises_exception(void) {
  ap_tape_t t;
  arm(&t);
  issue(&t, AP_QIC_CMD_SELECT);
  issue(&t, AP_QIC_CMD_READ);

  /* One block more than the cartridge holds, because the **first block is
   * handed over twice** -- see `ap_tape_t::first_block_pending`, measured from
   * the firmware's own DMA programming and needed by a second implementation,
   * with no document explaining it. */
  for (unsigned i = 0; i < sizeof cartridge + AP_CT_BLOCK_SIZE; i++) {
    (void)ap_tape_dma_read(&t);
  }
  /* One past the end. `[SC499]`'s EXC comes "from LSI chip", and the end of a
   * cartridge is exactly such a condition -- a driver reading on gets an
   * exception rather than the tape silently wrapping. */
  (void)ap_tape_dma_read(&t);
  TEST_ASSERT_TRUE(exception_asserted(&t));
}

static void test_ready_and_exception_are_never_both_asserted(void) {
  ap_tape_t t;
  arm(&t);
  issue(&t, AP_QIC_CMD_SELECT);
  issue(&t, AP_QIC_CMD_READ);
  /* The cartridge plus the doubled first block; see the test above. */
  for (unsigned i = 0; i < sizeof cartridge + AP_CT_BLOCK_SIZE; i++) {
    (void)ap_tape_dma_read(&t);
  }
  (void)ap_tape_dma_read(&t); /* past the end */

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
  issue(&t, AP_QIC_CMD_ERASE); /* refused, raises exception */
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
  (void)ap_tape_dma_read(&t);
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
  issue(&t, AP_QIC_CMD_ERASE); /* refused, raises exception */
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

/* **A written block reaches the cartridge**, which nothing carried it to.
 *
 * §1.13.1's WRITE entry is the mirror of its READ entry -- the host streams
 * bytes and the device takes them a *block* at a time -- and the read direction
 * has had `ensure_block` since Phase 4. The write direction had no counterpart:
 * `09 WRITE` armed the drive, the bytes went into the SC499's data register,
 * and `ap_qic_write_block` was reached by nothing at all, so a host could write
 * a whole cartridge and none of it would arrive.
 *
 * Written against the image the drive is holding, so it fails if the block is
 * assembled but never handed over, and fails differently if it is handed over
 * misaligned. */
/* **The command byte goes on the bus before REQUEST rises, and this core
 * required the opposite.**
 *
 * `[SC499]` §1.13.2 numbers the steps of all three command transfers, and each
 * opens the same way — Figure 1-8, the exception entry a just-reset drive takes:
 *
 *     T1 - Bus Data Valid
 *     T2 - Controller Asserts REQUEST     0 us. < T1 -> T2
 *     T3 - Device Deasserts EXCEPTION
 *
 * `ap_tape_write` took a data-register write as a command only when REQUEST was
 * **already** set, so a host following the figure had its byte stored in the
 * controller's data register and executed by nothing. The SR10.4 boot
 * cartridge's firmware follows the figure: it writes `C0` to the data register
 * at one instruction and `40` to the control register at the next, and this
 * core answered by handing `C0` straight back (`FINDINGS.md` C262).
 *
 * The suite's own `issue` helper uses the other order, which is why every test
 * here passed throughout — a harness that agreed with the model rather than
 * with the manual. */
static void test_a_command_byte_may_precede_the_request_that_takes_it(void) {
  ap_tape_t t;
  arm(&t);

  /* T1 then T2, which is the figure's order and not `issue`'s. */
  ap_tape_write(&t, AP_TAPE_ADDR + 0u, AP_QIC_CMD_SELECT);
  ap_tape_write(&t, AP_TAPE_ADDR + 1u, AP_SC499_CTL_REQUEST);
  clock_now += ap_sc499_handshake_duration(AP_SC499_ENTRY_READY) +
               ap_sc499_handshake_duration(AP_SC499_ENTRY_DIRECTION);
  ap_tape_advance(&t, clock_now);
  /* The drive took it: SELECT is what makes a drive report itself present. */
  TEST_ASSERT_TRUE(t.drive.selected);

  /* And READ STATUS the same way, whose effect is visible without reading a
   * byte: the block becomes pending. */
  ap_tape_write(&t, AP_TAPE_ADDR + 1u, 0u); /* T5/T6: REQUEST back down */
  ap_tape_write(&t, AP_TAPE_ADDR + 0u, AP_QIC_CMD_READ_STATUS);
  ap_tape_write(&t, AP_TAPE_ADDR + 1u, AP_SC499_CTL_REQUEST);
  TEST_ASSERT_TRUE(t.drive.status_pending);
}

/* **The device turns the bus round for the block it is about to deliver.**
 *
 * `QIC-02` §3.6.1 numbers the whole READ STATUS exchange, and the bus turns at
 * T8 -- after the host has released REQUEST (T5) and the device has answered by
 * dropping READY (T7), and before the first byte reaches the bus (T9):
 *
 *     T5  - HOST RESETS REQUEST
 *     T7  - CONTROLLER RESETS READY      20 < T5 -> T7 < 100 us
 *     T8  - CONTROLLER CHANGES BUS DIRECTION
 *     T9  - 1ST STATUS BYTE TO BUS
 *     T10 - CONTROLLER SETS READY        T7 -> T10 > 20 us
 *
 * This core set `direction` as it handed a byte over, so a host that polls for
 * DIRECTION before reading waited for a signal only its own read would produce.
 * Measured on the SR10.4 boot cartridge: after READ STATUS the firmware polls
 * the status register **4,097 times** at one PC -- a 4096-iteration timeout and
 * its exit -- seeing `37` every time, which is READY asserted, no exception,
 * DONE set and DIRECTION **clear** (`FINDINGS.md` C263). */
static void test_a_status_command_turns_the_bus_round_before_any_byte(void) {
  ap_tape_t t;
  arm(&t);
  issue(&t, AP_QIC_CMD_SELECT);
  TEST_ASSERT_FALSE(t.controller.direction);

  issue(&t, AP_QIC_CMD_READ_STATUS);
  /* T8 and T9, and nothing has been read yet. */
  TEST_ASSERT_TRUE(t.controller.direction);
  TEST_ASSERT_TRUE(t.status_valid);
  /* The drive's arming is spent: the block has been fetched, once. */
  TEST_ASSERT_FALSE(t.drive.status_pending);
}

/* **READY is an interlock, and this core drove one of its four edges.**
 *
 * `QIC-02` §3.6.3's SELECT is the plainest command there is and READY moves
 * four times: down when REQUEST rises (T3), up when the command is done (T4),
 * down again when the host releases REQUEST (T7), and up once more when the
 * device is ready for the next command (T8). This core had T3 and T4.
 *
 * **A driver waits at T7.** `[SC499]` Figure 1-26, the guide's own SEND COMMAND
 * flow chart, is `ASSERT REQUEST` -> `READY?` -> `DROP REQUEST` -> `READY?*`
 * looping while the answer is still *yes*, footnoted "20 usec loop max, see
 * timing". Against a device that never drops READY that loop cannot end, and
 * the SR10.4 boot firmware sits in one: 4,097 reads of the status register at a
 * single PC with `37` every time -- READY asserted (`FINDINGS.md` C264).
 *
 * `AP_SC499_T_CLOSE_MIN` and `_MAX` were defined for this edge, asserted about
 * by `sc499_suite`, and produced by nothing. */
static void test_releasing_request_takes_ready_down_and_brings_it_back(void) {
  ap_tape_t t;
  arm(&t);
  issue(&t, AP_QIC_CMD_SELECT);
  TEST_ASSERT_TRUE(t.controller.ready);

  /* T2 then T4: the command is taken and completed. */
  ap_tape_write(&t, AP_TAPE_ADDR + 0u, AP_QIC_CMD_SELECT);
  ap_tape_write(&t, AP_TAPE_ADDR + 1u, AP_SC499_CTL_REQUEST);
  TEST_ASSERT_FALSE(t.controller.ready);
  clock_now += ap_sc499_handshake_duration(AP_SC499_ENTRY_READY);
  ap_tape_advance(&t, clock_now);
  TEST_ASSERT_TRUE(t.controller.ready);

  /* T5. READY is still up an instant later -- the edge is the device's answer,
   * not the host's write. */
  ap_tape_write(&t, AP_TAPE_ADDR + 1u, 0u);
  clock_now += AP_SC499_T_CLOSE_MIN - 1u;
  ap_tape_advance(&t, clock_now);
  TEST_ASSERT_TRUE(t.controller.ready);

  /* T7, which is the edge the driver's loop is waiting for. */
  clock_now += 1u;
  ap_tape_advance(&t, clock_now);
  TEST_ASSERT_FALSE(t.controller.ready);

  /* T8: and back, ready for the next command. */
  clock_now += AP_SC499_T_READY_REOPEN - 1u;
  ap_tape_advance(&t, clock_now);
  TEST_ASSERT_FALSE(t.controller.ready);
  clock_now += 1u;
  ap_tape_advance(&t, clock_now);
  TEST_ASSERT_TRUE(t.controller.ready);
}

/* **READ STATUS's six bytes come out of the data register, and the host clocks
 * them out with REQUEST.**
 *
 * `[SC499]` §1.13.1: after a READ STATUS "the device transfers the standard six
 * bytes to the host", through the same data register a block goes through.
 * `ap_qic_read_status` composes that block and clears the conditions it
 * reports -- and its only caller was `qic_suite`, so on a machine it was never
 * delivered. `check_what_is_called_by_nobody`'s pattern for the fourth time
 * here, and the one an earlier sweep this same day **missed**, because it
 * counted a test caller as a caller.
 *
 * **The first fix advanced the block on the host's read, and that is not how a
 * byte is taken.** `[SC499]` Figure 1-25 is the guide's own READ STATUS driver:
 *
 *     READY? -> READ DATA BUS -> ASSERT REQ -> 20 usec -> READY? (loop while
 *     yes) -> DROP REQ -> ALL 6 BYTES?
 *
 * The read is a sample of a byte the device is holding; REQUEST is what says it
 * has been taken. So a rising REQUEST means two different things depending on
 * which way the bus is pointing -- a command when the host holds it, an
 * acknowledge when the device does -- and this core read every one of them as a
 * command, executing the stale byte in the data register and abandoning the
 * block it was acknowledging.
 *
 * The first byte carries `POR`, the power-on condition a reset leaves behind:
 * that is what a firmware issuing READ STATUS in answer to an exception is
 * asking for, and reading it is what clears it. */
static void test_read_status_delivers_its_six_bytes_through_the_data_register(
    void) {
  ap_tape_t t;
  arm(&t);
  issue(&t, AP_QIC_CMD_SELECT);
  /* A drive that has been reset holds "power on/reset occurred". */
  TEST_ASSERT_TRUE(t.drive.power_on);
  /* The drive as it stands *before* the block is fetched, so the six bytes can
   * be checked against what this drive would compose rather than against a
   * freshly initialised one, which is a different drive. */
  ap_qic_t before = t.drive;
  issue(&t, AP_QIC_CMD_READ_STATUS);

  /* Reading without taking gets the same byte again, which is what a bus does. */
  TEST_ASSERT_EQUAL_HEX8(ap_tape_dma_read(&t),
                         ap_tape_dma_read(&t));

  uint8_t block[AP_QIC_STATUS_BYTES];
  for (unsigned i = 0; i < AP_QIC_STATUS_BYTES; i++) {
    /* The device holds the bus for the whole block: T8 to T21. */
    TEST_ASSERT_TRUE(t.controller.direction);
    block[i] = take_byte(&t);
  }

  /* T21: the last byte has been taken and the bus goes back. */
  TEST_ASSERT_FALSE(t.controller.direction);
  /* T22: and READY with it, ready for the next command. */
  TEST_ASSERT_TRUE(t.controller.ready);

  /* `POR` is bit 0 of status **byte 1**, the second byte on the wire, and
   * reading the block is what clears it -- so the condition is reported exactly
   * once, which is the whole contract. */
  TEST_ASSERT_TRUE((block[1] & AP_QIC_EXS_POWER_ON) != 0u);
  TEST_ASSERT_FALSE(t.drive.power_on);
  TEST_ASSERT_FALSE(t.drive.status_pending);

  /* And the register goes back to being the data register: a second sweep is
   * not a second status block. */
  TEST_ASSERT_FALSE(t.status_valid);

  /* The six are the drive's own block, in order -- not the first byte six
   * times, which is what an acknowledge that did not advance would give. */
  uint8_t expected[AP_QIC_STATUS_BYTES];
  before.status_pending = true;
  TEST_ASSERT_TRUE(ap_qic_read_status(&before, expected));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, block, AP_QIC_STATUS_BYTES);
}

/* **A command abandons a status block nobody took.**
 *
 * `ap_qic_command` arms `status_pending` for a READ STATUS and no command
 * clears it. The board abandons its own half of an unfinished transfer when a
 * new command arrives -- the part-read block, the part-delivered status offset
 * -- and left the drive's arming standing, so the *next* release of REQUEST
 * opened a delivery for a block nobody asked for. Every REQUEST after that
 * would then read as a byte acknowledge instead of a command, which is
 * `FINDINGS.md` C264's failure arriving by another door.
 *
 * No figure describes a host abandoning a status sequence: Figure 1-25 always
 * takes all six bytes. So this is a choice among undefined behaviours, and the
 * one that keeps the board's half and the drive's half saying the same thing. */
/* **RSTSAC resets the drive too, and this core reset only the card.**
 *
 * `[SC499]` §1.12 lists what resets the controller's microprocessor -- the two
 * supply rails, and "c. RSTSAC is set" -- and then says it outright:
 *
 *     NOTE
 *     Microprocessor RESET will also cause a tape drive reset.
 *
 * So a host that pulses RSTSAC gets a drive at load point holding a power-on
 * condition, not one left exactly where it was. Measured on the SR10.4
 * cartridge boot, where Domain/OS resets the card and then reports `bad rewind`
 * -- the run's report had the drive at block 98,263 with its exception word
 * `0000`, when a just-reset drive owes `POR` and `BOM` and sits at BOT.
 * `FINDINGS.md` C270.
 *
 * The walk record for §1.12 recorded the 25 us hold and not the NOTE beside it,
 * which is why this was missed until a boot went looking. */
static void test_the_controller_reset_resets_the_drive_too(void) {
  ap_tape_t t;
  arm(&t);
  issue(&t, AP_QIC_CMD_SELECT);
  issue(&t, AP_QIC_CMD_READ);

  /* Move the tape off load point and spend the power-on condition, so neither
   * is true by accident when the reset is asked for. */
  for (unsigned i = 0; i < AP_CT_BLOCK_SIZE * 2u; i++) {
    (void)ap_tape_dma_read(&t);
  }
  TEST_ASSERT_TRUE(t.drive.position > 0u);
  issue(&t, AP_QIC_CMD_READ_STATUS);
  uint8_t block[AP_QIC_STATUS_BYTES];
  for (unsigned i = 0; i < AP_QIC_STATUS_BYTES; i++) {
    block[i] = take_byte(&t);
  }
  TEST_ASSERT_FALSE(t.drive.power_on);

  /* §1.12's RSTSAC: "Activated by writing a 1 to Control Register Bit 7." */
  ap_tape_write(&t, AP_TAPE_ADDR + 1u, AP_SC499_CTL_RESET);

  /* The drive is at load point, owes its power-on condition, and is selected by
   * default -- `QIC-02` §4.2.1's "defaults to drive 0 for subsequent
   * commands", which `ap_qic_reset` already implements. */
  TEST_ASSERT_EQUAL_UINT64(0u, t.drive.position);
  TEST_ASSERT_TRUE(t.drive.power_on);
  TEST_ASSERT_TRUE(t.drive.selected);
  TEST_ASSERT_FALSE(t.drive.reading);
  /* And the cartridge is still in: a reset is a command to the drive, not to
   * the operator. */
  TEST_ASSERT_TRUE(t.drive.loaded);
}

static void test_a_new_command_abandons_a_status_block_nobody_took(void) {
  ap_tape_t t;
  arm(&t);
  issue(&t, AP_QIC_CMD_SELECT);

  /* Armed and then walked away from: the command byte and REQUEST, and no
   * release, so the delivery never opens. */
  ap_tape_write(&t, AP_TAPE_ADDR + 0u, AP_QIC_CMD_READ_STATUS);
  ap_tape_write(&t, AP_TAPE_ADDR + 1u, AP_SC499_CTL_REQUEST);
  clock_now += ap_sc499_handshake_duration(AP_SC499_ENTRY_READY);
  ap_tape_advance(&t, clock_now);
  TEST_ASSERT_TRUE(t.drive.status_pending);

  /* Something else entirely, in the figures' own order. */
  issue(&t, AP_QIC_CMD_BOT);
  TEST_ASSERT_FALSE(t.drive.status_pending);
  /* And the release that ends it opens no delivery, so the bus stays the
   * host's and the next REQUEST is a command. */
  TEST_ASSERT_FALSE(t.status_valid);
  TEST_ASSERT_FALSE(t.controller.direction);
}

static void test_a_written_block_reaches_the_cartridge(void) {
  ap_tape_t t;
  arm(&t);
  issue(&t, AP_QIC_CMD_SELECT);
  issue(&t, AP_QIC_CMD_WRITE);

  /* Control bit 6 is "Request to LSI chip": a data write with it set is a
   * *command*, so a driver clears it before it streams. That is the protocol
   * and not a detail of this test -- leaving it set feeds the opcode path.
   *
   * A block of a pattern that is nowhere in `arm`'s fill, so a block that was
   * never written cannot pass by coincidence. */
  ap_tape_write(&t, AP_TAPE_ADDR + 1u, 0u);
  for (unsigned i = 0; i < AP_CT_BLOCK_SIZE; i++) {
    ap_tape_write(&t, AP_TAPE_ADDR + 0u, (uint8_t)(0xA5u ^ (i & 0xFFu)));
  }

  /* The whole block is in the cartridge, at block 0, byte for byte. */
  for (unsigned i = 0; i < AP_CT_BLOCK_SIZE; i++) {
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(0xA5u ^ (i & 0xFFu)), cartridge[i]);
  }

  /* And the boundary was marked, exactly as a read block marks it: READY drops
   * when the device takes a block and returns a figure later. */
  TEST_ASSERT_FALSE(ready_asserted(&t));
  clock_now += ap_sc499_handshake_duration(AP_SC499_ENTRY_READY);
  ap_tape_advance(&t, clock_now);
  TEST_ASSERT_TRUE(ready_asserted(&t));
}

/* A partial block is not written: a `.ct` is a whole number of 512-byte blocks,
 * so there is nowhere to put one, and padding it would put bytes on the tape
 * the host never sent. */
static void test_a_partial_block_is_not_written(void) {
  ap_tape_t t;
  arm(&t);
  issue(&t, AP_QIC_CMD_SELECT);
  issue(&t, AP_QIC_CMD_WRITE);

  ap_tape_write(&t, AP_TAPE_ADDR + 1u, 0u); /* out of command mode, as above */
  for (unsigned i = 0; i < AP_CT_BLOCK_SIZE - 1u; i++) {
    ap_tape_write(&t, AP_TAPE_ADDR + 0u, 0x5Au);
  }
  /* `arm` filled the image with `0x40 + (i & 0x3F)`, so byte 0 is `0x40`. */
  TEST_ASSERT_EQUAL_HEX8(0x40u, cartridge[0]);
}

/* **Tape data leaves this card through `DACK` and through nothing else**, which
 * is what a programmed read of `BASE+0` must not do: take a byte.
 *
 * This is the `E0007` defect pinned. A programmed read used to hand over a tape
 * byte whenever a READ was armed, and on the SR10.4 cartridge boot **94 bytes**
 * went out that way against 35,662,848 through `DACK` -- exactly the drift that
 * left every block header 77 bytes out of place, so the kernel rejected every
 * one and could not find `bscom/rbak_shell`. With this the same boot reaches
 * `RBAK_BS reloading system software from cartridge tape....`
 *
 * `[SC499]` does not describe the pattern -- Figures 1-12 and 1-14 have the
 * host poll *status* and never data across a transfer -- so the oracle settled
 * it: MAME's `sc499_device` loads its `m_data` register from the six
 * status-block bytes alone and its `read_data_port()` never touches the block
 * index, where `dack_r()` is the one path that advances it. */
static void test_a_programmed_read_takes_no_tape_byte(void) {
  ap_tape_t t;
  arm(&t);
  issue(&t, AP_QIC_CMD_SELECT);
  issue(&t, AP_QIC_CMD_READ);

  /* Twenty reads by address move the tape not at all, and answer `00` -- the
   * idle value this port was measured to return. */
  for (unsigned i = 0; i < 20u; i++) {
    TEST_ASSERT_EQUAL_HEX8(0x00u, ap_tape_read(&t, AP_TAPE_ADDR + 0u));
  }
  TEST_ASSERT_EQUAL_UINT(0u, t.offset);

  /* And the stream is still at its first byte for the path that owns it. */
  TEST_ASSERT_EQUAL_HEX8(cartridge[0], ap_tape_dma_read(&t));
  TEST_ASSERT_EQUAL_UINT(1u, t.offset);

  /* Interleaving them changes nothing: only `DACK` advances. */
  for (unsigned i = 0; i < 5u; i++) {
    (void)ap_tape_read(&t, AP_TAPE_ADDR + 0u);
  }
  TEST_ASSERT_EQUAL_HEX8(cartridge[1], ap_tape_dma_read(&t));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_cold_power_on_runs_the_confidence_test_and_asserts_exception);
  RUN_TEST(test_the_machines_reset_runs_the_confidence_test_too);
  RUN_TEST(test_reading_the_tape_makes_the_device_hold_the_bus);
  RUN_TEST(test_a_command_is_not_finished_when_it_is_issued);
  RUN_TEST(test_an_exception_survives_until_its_figure_completes);
  RUN_TEST(test_ready_and_exception_are_never_both_asserted);
  RUN_TEST(test_a_command_clears_an_exception);
  RUN_TEST(test_ready_drops_and_returns_at_each_data_block);
  RUN_TEST(test_an_idle_controller_still_reads_as_measured);
  RUN_TEST(test_a_command_reaches_the_drive_through_the_registers);
  RUN_TEST(test_the_tape_is_read_a_byte_at_a_time_through_dack);
  RUN_TEST(test_a_programmed_read_takes_no_tape_byte);
  RUN_TEST(test_a_refused_command_raises_exception);
  RUN_TEST(test_running_off_the_end_raises_exception);
  RUN_TEST(test_a_read_the_drive_ends_also_ends_the_dma);
  RUN_TEST(test_the_drive_stops_asking_at_a_file_mark);
  RUN_TEST(test_the_measured_dump_is_reproduced);
  RUN_TEST(test_the_write_only_commands_are_reachable_by_writing);
  RUN_TEST(test_the_upper_half_of_each_block_is_not_the_part);
  RUN_TEST(test_the_registers_alias_on_an_eight_byte_period);
  RUN_TEST(test_nothing_outside_the_range_decodes);
  RUN_TEST(test_the_tape_raises_its_documented_interrupt);
  RUN_TEST(test_a_command_byte_may_precede_the_request_that_takes_it);
  RUN_TEST(test_a_status_command_turns_the_bus_round_before_any_byte);
  RUN_TEST(test_releasing_request_takes_ready_down_and_brings_it_back);
  RUN_TEST(test_read_status_delivers_its_six_bytes_through_the_data_register);
  RUN_TEST(test_a_new_command_abandons_a_status_block_nobody_took);
  RUN_TEST(test_the_controller_reset_resets_the_drive_too);
  RUN_TEST(test_a_written_block_reaches_the_cartridge);
  RUN_TEST(test_a_partial_block_is_not_written);
  return UNITY_END();
}
