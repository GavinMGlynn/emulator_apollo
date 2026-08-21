/* OMTI 862X ESDI/floppy controller, `[OMTI]` Jan 1987. */

#include "unity.h"

#include <stdio.h>
#include <string.h>

#include "device/ap_omti.h"
#include "device/ap_omti_cdb.h"

void setUp(void) {}
void tearDown(void) {}

/* The host's half of §4.3's warning: "The host must wait 100 usec after a
 * -RESET before issuing a SELECT."
 *
 * Every test that drives a command has to do this, because a freshly reset
 * controller is in the reset state and not idle -- and "the IDLE STATE is the
 * only time the controller will respond to a select request". Written as a
 * helper rather than repeated so that the wait is one statement of one rule;
 * before the reset state existed, this suite selected immediately and the
 * model let it, which is the permissive direction the item was about. */
static void wait_out_reset(ap_omti_t *o) {
  ap_omti_advance(o, AP_OMTI_RESET_TIME);
}


static void test_the_measured_fixed_disk_ports_are_reproduced(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  /* The oracle's idle controller reads `FF C0 FC 00` across the four ports.
   * The `C0` is the confirmation: Table 4-2 gives bits 7 and 6 as "Not Used
   * (Set to 1)" and every other bit as a condition an idle controller does not
   * meet, so `C0` is the only value the table permits here. Manual and machine
   * agreeing on a byte. */
  static const uint8_t expected[4] = {0xFF, 0xC0, 0xFC, 0x00};
  for (unsigned i = 0; i < 4u; i++) {
    TEST_ASSERT_EQUAL_HEX8(expected[i], ap_omti_disk_read(&o, i));
  }
}

static void test_the_status_bits_seven_and_six_cannot_be_cleared(void) {
  ap_omti_t o;
  ap_omti_reset(&o);
  wait_out_reset(&o);

  /* Table 4-2 gives them as constants, not state. Re-asserted on every read so
   * that nothing -- not a reset, not a select -- can put the register in a state
   * the table says is impossible. */
  ap_omti_disk_write(&o, AP_OMTI_DISK_CONFIG, 0x00); /* SELECT */
  TEST_ASSERT_EQUAL_HEX8(AP_OMTI_ST_FIXED,
                         ap_omti_disk_read(&o, AP_OMTI_DISK_STATUS) &
                             AP_OMTI_ST_FIXED);
}

static void test_selecting_the_controller_makes_it_busy(void) {
  ap_omti_t o;
  ap_omti_reset(&o);
  wait_out_reset(&o);

  /* Table 4-1's write side of port 2 is "SELECT (Function)", and Table 4-2's
   * BSY bit is "1 = Controller Selected". So the function has an observable
   * effect rather than merely being accepted. */
  TEST_ASSERT_EQUAL_HEX8(0, ap_omti_disk_read(&o, AP_OMTI_DISK_STATUS) &
                                AP_OMTI_ST_BSY);
  ap_omti_disk_write(&o, AP_OMTI_DISK_CONFIG, 0x00);
  TEST_ASSERT_EQUAL_HEX8(AP_OMTI_ST_BSY,
                         ap_omti_disk_read(&o, AP_OMTI_DISK_STATUS) &
                             AP_OMTI_ST_BSY);
}

static void test_the_reset_port_is_a_function_not_a_store(void) {
  ap_omti_t fresh;
  ap_omti_t used;
  ap_omti_reset(&fresh);
  ap_omti_reset(&used);

  /* Table 4-1: "RESET (Function)". The value written is not a parameter, so two
   * controllers reset with different values must be identical -- and identical
   * to one that was never disturbed. */
  ap_omti_disk_write(&used, AP_OMTI_DISK_CONFIG, 0x00); /* select, sets BSY */
  ap_omti_disk_write(&used, AP_OMTI_DISK_MASK, 0x5A);
  ap_omti_disk_write(&used, AP_OMTI_DISK_STATUS, 0xA5); /* RESET */

  TEST_ASSERT_EQUAL_MEMORY(&fresh, &used, sizeof fresh);
}

static void test_the_data_register_changes_width_with_the_command_bit(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  /* §4.2: "This is an 8 or 16 bit register depending on the state of the
   * controller (determined by the C/D bit in the STATUS register)."
   *
   * The width is exposed rather than hidden because a model with a fixed-width
   * data register would carry commands correctly and corrupt every data word,
   * or the reverse -- and neither failure shows up until a transfer runs. */
  TEST_ASSERT_FALSE(ap_omti_data_is_byte(&o));
  o.status |= AP_OMTI_ST_CD;
  TEST_ASSERT_TRUE(ap_omti_data_is_byte(&o));
}

static void test_the_measured_floppy_block_is_reproduced(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  /* The oracle's floppy half reads `FF FF FF FF 00 FF 00 80`. The last byte is
   * the one the manual predicts: Table 4-3's Digital Input bit 7 comes "from pin
   * 34 of the floppy disk control cable and is normally used for diskette change
   * status", and a drive with no media asserts it. */
  static const uint8_t expected[8] = {0xFF, 0xFF, 0xFF, 0xFF,
                                      0x00, 0xFF, 0x00, 0x80};
  for (unsigned i = 0; i < 8u; i++) {
    TEST_ASSERT_EQUAL_HEX8(expected[i], ap_omti_fdc_read(&o, i));
  }
}

static void test_clearing_the_output_register_holds_the_floppy_in_reset(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  /* Table 4-3, Digital Output bit 2: "Reset floppy disk function when 0. The
   * floppy disk function comes out of reset when this bit is set to 1."
   *
   * That bit runs the opposite way to every other control bit in the part, so a
   * driver clearing the register to stop the motors also asserts reset. A model
   * that missed the inversion would come out of reset exactly when the hardware
   * went into it. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR, 0x00);
  TEST_ASSERT_TRUE(ap_omti_fdc_in_reset(&o));

  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR, AP_OMTI_DOR_NOT_RESET);
  TEST_ASSERT_FALSE(ap_omti_fdc_in_reset(&o));

  /* Enabling the motors without setting bit 2 still holds it in reset, which is
   * the case a driver actually gets wrong. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR, AP_OMTI_DOR_DRIVE_A_MOTOR);
  TEST_ASSERT_TRUE(ap_omti_fdc_in_reset(&o));
}

static void test_the_two_halves_share_nothing(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  /* §4.1: "two independent sets of registers", and §3.4 has them operating
   * concurrently. Resetting the fixed disk must not disturb the floppy's
   * programming, or a disk command would silently stop the drive motors. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR,
                    (uint8_t)(AP_OMTI_DOR_NOT_RESET | AP_OMTI_DOR_DRIVE_A_MOTOR));

  /* A floppy command part-way through its command phase: two bytes of SEEK's
   * three. This used to be a byte written to the data register and read back,
   * which stopped meaning anything once that register became a command port --
   * a write there now *starts* a command, and the byte that comes back is its
   * result. A command in flight is the stronger thing to leave undisturbed
   * anyway. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SEEK);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, 0x00);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_COMMAND, ap_omti_fdc_phase(&o));

  wait_out_reset(&o);
  ap_omti_disk_write(&o, AP_OMTI_DISK_CONFIG, 0x00);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_COMMAND, ap_omti_fdc_phase(&o));
  TEST_ASSERT_FALSE(ap_omti_fdc_in_reset(&o));

  /* And the stronger case, which this test originally missed by exercising
   * SELECT alone: the fixed disk's *reset* must leave the floppy running. A
   * disk reset that stopped the drive motors would be a fault with no register
   * to explain it. */
  ap_omti_disk_write(&o, AP_OMTI_DISK_STATUS, 0x00);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_COMMAND, ap_omti_fdc_phase(&o));
  TEST_ASSERT_FALSE(ap_omti_fdc_in_reset(&o));

  /* The seek completes across all of that, on the cylinder it was given -- and
   * it takes the seventeen cylinders' worth of stepping that `008778-03`
   * Table 7-7 says it does, so the clock has to be advanced onto the arrival
   * before `SENSE INTERRUPT STATUS` has anything to report. Asked any earlier,
   * the part answers "never started", which is the documented reply to a sense
   * with no seek outstanding. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, 0x11);
  /* **Two deadlines are outstanding here, and that is new.** The disk RESET
   * above starts §4.3's 100 µs reset state, so `ap_omti_interrupt_next_change`
   * -- which returns the soonest instant *anything* on this board can move --
   * no longer necessarily returns the seek. Drain them in order rather than
   * advancing once and assuming which one was reached. */
  for (ap_time_t at = ap_omti_interrupt_next_change(&o); at != AP_TIME_NEVER;
       at = ap_omti_interrupt_next_change(&o)) {
    ap_omti_advance(&o, at);
  }
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SENSE_INTERRUPT);
  (void)ap_omti_fdc_read(&o, AP_OMTI_FDC_DATA);
  TEST_ASSERT_EQUAL_HEX8(0x11, ap_omti_fdc_read(&o, AP_OMTI_FDC_DATA));
}

/* Issue a six-byte CDB the way §4.3's command state does: select, then a byte
 * at a time, checking the controller asks for each one. */
static void issue(ap_omti_t *o, const uint8_t cdb[6]) {
  wait_out_reset(o);
  ap_omti_disk_write(o, AP_OMTI_DISK_CONFIG, 0x00); /* SELECT */
  for (unsigned i = 0; i < 6u; i++) {
    /* The command phase: C/D set, travelling from the host, and requested --
     * `CD` with the two fixed bits, which is what the boot PROM checks after
     * every byte it writes. */
    TEST_ASSERT_EQUAL_HEX8(0xCD, ap_omti_disk_read(o, AP_OMTI_DISK_STATUS));
    ap_omti_disk_write(o, AP_OMTI_DISK_DATA, cdb[i]);
  }
}

/* ## §4.3's reset state, and the 100 µs the model had nowhere to put
 *
 * p. 4-3 prints the warning **twice on one page** -- once under the RESET
 * register and once under the protocol -- which is a document insisting. The
 * model had five of the manual's six logical states and was missing this one,
 * so there was no state with a length to hang the duration on, and a host that
 * selected immediately got a working command where the hardware gives
 * undefined behaviour. That is the permissive direction, and it is how an
 * intermittent failure hides from a deterministic core. */
static void test_a_reset_controller_is_not_idle_for_one_hundred_microseconds(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  /* "The RESET STATE is entered by applying power to the controller
   * (power - on -reset)" -- so this is true of a machine that has only just
   * been switched on, before any register has been touched. */
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_RESET, ap_omti_disk_phase(&o));

  /* One unit short of the deadline is still the reset state: the wait is a
   * duration and not a formality. */
  ap_omti_advance(&o, AP_OMTI_RESET_TIME - 1u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_RESET, ap_omti_disk_phase(&o));

  /* "It will then enter the idle state." */
  ap_omti_advance(&o, AP_OMTI_RESET_TIME);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_IDLE, ap_omti_disk_phase(&o));
}

static void test_a_select_inside_the_reset_window_is_refused(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  /* "The IDLE STATE is the only time the controller will respond to a select
   * request", and during the reset state it is not idle. The refusal is the
   * existing guard rather than a new special case, which is the point of
   * giving RESET a phase instead of a flag. */
  ap_omti_disk_write(&o, AP_OMTI_DISK_CONFIG, 0x00); /* SELECT, too early */
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_RESET, ap_omti_disk_phase(&o));
  TEST_ASSERT_EQUAL_HEX8(0, ap_omti_disk_read(&o, AP_OMTI_DISK_STATUS) &
                                AP_OMTI_ST_BSY);

  /* And the same write, after the wait, is honoured -- so the refusal is about
   * the window and not about the write. */
  ap_omti_advance(&o, AP_OMTI_RESET_TIME);
  ap_omti_disk_write(&o, AP_OMTI_DISK_CONFIG, 0x00);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_COMMAND, ap_omti_disk_phase(&o));
  TEST_ASSERT_EQUAL_HEX8(AP_OMTI_ST_BSY,
                         ap_omti_disk_read(&o, AP_OMTI_DISK_STATUS) &
                             AP_OMTI_ST_BSY);
}

/* The register write is one of the three documented entries to the reset
 * state, and it must start the window as surely as power-on does -- otherwise
 * a driver that resets a running controller could select immediately, which is
 * exactly the sequence the warning is printed for. */
static void test_writing_the_reset_register_restarts_the_hundred_microseconds(void) {
  ap_omti_t o;
  ap_omti_reset(&o);
  ap_omti_advance(&o, AP_OMTI_RESET_TIME);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_IDLE, ap_omti_disk_phase(&o));

  ap_omti_disk_write(&o, AP_OMTI_DISK_STATUS, 0x00); /* RESET (Function) */
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_RESET, ap_omti_disk_phase(&o));

  /* Measured from the write, not from power-on: the controller is a state
   * machine and not a stopwatch started once. */
  ap_omti_advance(&o, AP_OMTI_RESET_TIME);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_RESET, ap_omti_disk_phase(&o));
  ap_omti_advance(&o, AP_OMTI_RESET_TIME * 2u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_IDLE, ap_omti_disk_phase(&o));
}

/* A scheduler that does not know about the window would run past it, and this
 * core's whole claim is that nothing is special-cased outside the part. */
static void test_the_reset_window_is_offered_to_the_scheduler(void) {
  ap_omti_t o;
  ap_omti_reset(&o);
  TEST_ASSERT_EQUAL_UINT64(AP_OMTI_RESET_TIME,
                           ap_omti_interrupt_next_change(&o));
  ap_omti_advance(&o, AP_OMTI_RESET_TIME);
  TEST_ASSERT_EQUAL_UINT64(AP_TIME_NEVER, ap_omti_interrupt_next_change(&o));
}

/* ## `0E READ DATA FROM SECTOR BUFFER`, and the block a reset leaves behind
 *
 * §5.4.13: the transfer is the sector size times byte 4's block count, the
 * controller does not touch the drive, and issued after a reset before any
 * other command the buffer holds the controller's own identification. The boot
 * PROM's Winchester test 1 is precisely this sequence.
 */
static void test_read_sector_buffer_enters_the_data_phase_without_a_drive(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  static const uint8_t cdb[6] = {0x0E, 0, 0, 0, 1, 0};
  issue(&o, cdb);

  /* Data in, not status: C/D **clear**, I/O set, busy and requested. `CB` is
   * the byte the firmware waits for, and this model used to answer `EF` --
   * status phase with an interrupt, which is what it does with a command it
   * does not implement. No drive is fitted, and the command does not want one. */
  TEST_ASSERT_EQUAL_HEX8(0xCB, ap_omti_disk_read(&o, AP_OMTI_DISK_STATUS));
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_disk_phase(&o));
}

static void test_a_reset_leaves_the_identification_block_in_the_buffer(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  static const uint8_t cdb[6] = {0x0E, 0, 0, 0, 1, 0};
  issue(&o, cdb);

  uint8_t block[0x16];
  for (unsigned i = 0; i < sizeof block; i++) {
    block[i] = ap_omti_disk_read(&o, AP_OMTI_DISK_DATA);
  }

  /* `8x2xVW.WMMDDYY` resolved for the part the DN3500 has. */
  TEST_ASSERT_EQUAL_MEMORY(AP_OMTI_IDENTIFICATION, block,
                           AP_OMTI_IDENTIFICATION_BYTES);

  /* The four power-on error bytes, zero on a healthy controller -- and the two
   * words the boot PROM actually compares. A controller reporting a ROM
   * checksum or buffer RAM error here fails the self-test, which is the whole
   * purpose of the block. */
  for (unsigned i = 0; i < 4u; i++) {
    TEST_ASSERT_EQUAL_HEX8(0, block[AP_OMTI_ID_ERROR_FLAGS + i]);
  }
  /* Bits 7 and 6 set: 32K, per §5.4.13's own table. */
  TEST_ASSERT_EQUAL_HEX8(AP_OMTI_ID_BUFFER_32K, block[AP_OMTI_ID_BUFFER_SIZE]);
}

/* ## The data port is sixteen bits, and a word is one cycle
 *
 * Served as two byte reads, the second byte comes from the *status* register
 * and the word can never be what the firmware is waiting for -- it came back
 * `FFFF`. The byte order within the word is `PROVISIONAL`; see
 * `device/ap_omti.h`. This asserts the part that is not: a word read takes two
 * bytes of the buffer and advances by two.
 */
static void test_a_word_read_of_the_data_port_takes_two_buffer_bytes(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  static const uint8_t cdb[6] = {0x0E, 0, 0, 0, 1, 0};
  issue(&o, cdb);

  const uint16_t first = ap_omti_disk_read16(&o);
  const uint16_t second = ap_omti_disk_read16(&o);

  /* Two bytes per read, the earlier one in the **high** half, so the buffer
   * arrives in the order it holds. Settled by the boot PROM: `sysboot` loads to
   * `010FD800` with a long word the firmware names, and reads back as
   * `SYSBOOT VER ` only this way round. The oracle packs it the other way.
   *
   * `8621` are the first four bytes of the identification block. */
  TEST_ASSERT_EQUAL_HEX16(0x3836u, first);  /* '8', '6' */
  TEST_ASSERT_EQUAL_HEX16(0x3231u, second); /* '2', '1' */
  TEST_ASSERT_EQUAL_MEMORY(AP_OMTI_IDENTIFICATION,
                           ((const uint8_t[]){(uint8_t)(first >> 8),
                                              (uint8_t)(first & 0xFFu),
                                              (uint8_t)(second >> 8),
                                              (uint8_t)(second & 0xFFu)}),
                           4u);
}

static void test_a_block_count_past_the_buffer_is_refused(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  /* §5.4.13's block count is capped by the **controller's buffer**, which
   * §5.4.19 states in those words and byte 14 of the identification block
   * enumerates four sizes for. Past what a 32K part holds, the count is refused
   * rather than truncated: a host told the transfer succeeded would read the
   * tail of some earlier command's buffer as data.
   *
   * This test used to assert that *eight* was refused, from a table that
   * belongs to an 8K part -- and Domain/OS issues eight. The suite was encoding
   * the same misreading as the code, which is why a green tree proved nothing
   * about it. */
  const uint8_t past = (uint8_t)(AP_OMTI_MAX_BUFFER_BLOCKS + 1u);
  const uint8_t cdb[6] = {0x0E, 0, 0, 0, past, 0};
  issue(&o, cdb);

  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&o));
}

/* ## `0F WRITE DATA TO SECTOR BUFFER`, the direction `0E` is not
 *
 * §5.4.14, and the sentence that decides the whole arm: "the controller does
 * not access the disk drive during the execution of this command". A data-out
 * phase that ends by writing a sector would be `0A WRITE`; this one ends by
 * having filled the buffer, and the two are told apart by the block count.
 */
static void test_writing_the_sector_buffer_does_not_touch_the_drive(void) {
  ap_omti_t o;
  ap_omti_reset(&o); /* No drive: `omti.drive` is NULL. */

  static const uint8_t cdb[6] = {0x0F, 0, 0, 0, 1, 0};
  issue(&o, cdb);

  /* Data *out*: `C/D` clear as in every data phase, `I/O` **clear** because the
   * bytes travel to the controller, busy and requested. `CB` -- the byte `0E`
   * answers -- differs in exactly the `I/O` bit. */
  TEST_ASSERT_EQUAL_HEX8(0xC9, ap_omti_disk_read(&o, AP_OMTI_DISK_STATUS));
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_OUT, ap_omti_disk_phase(&o));

  for (unsigned i = 0; i < AP_AWD_SECTOR_BYTES; i++) {
    /* The phase holds for every byte but the last, which is what makes the
     * length the block count's and not a sector's by accident. */
    TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_OUT, ap_omti_disk_phase(&o));
    ap_omti_disk_write(&o, AP_OMTI_DISK_DATA, (uint8_t)(i & 0xFFu));
  }

  /* Complete, and complete *without an error*. With no drive fitted, a model
   * that wrote the buffer through to the disk could only have failed here --
   * so a clean completion is the assertion that it did not try. */
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&o));
  TEST_ASSERT_EQUAL_HEX8(0u, ap_omti_disk_read(&o, AP_OMTI_DISK_DATA));

  /* And the bytes are *there*: `0E` reads back what `0F` put in. The pair is
   * the reason both commands exist -- §5.4.13 names `0E` as the collection half
   * of a buffer transfer done in programmed I/O. */
  (void)ap_omti_disk_read(&o, AP_OMTI_DISK_STATUS);
  static const uint8_t back[6] = {0x0E, 0, 0, 0, 1, 0};
  issue(&o, back);
  for (unsigned i = 0; i < 8u; i++) {
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(i & 0xFFu),
                           ap_omti_disk_read(&o, AP_OMTI_DISK_DATA));
  }
}


/* ## "I do not support that command" is not "your geometry is wrong"
 *
 * Appendix A, "Sense Code Summary and Description", gives the two codes one
 * line apart:
 *
 *   20 Invalid Command       "the controller decoded a command code that it
 *                             does not support"
 *   21 Illegal Disk Address  "a command with a Sector Address beyond the
 *                             capacity of the drive"
 *
 * This model reported everything it had not implemented as `21`, and the cost
 * was two boots: Domain/OS believed the geometry claim, took the recovery path
 * built for it, and died several layers from the command that actually failed.
 * `1E` and `0F` each had to be excavated from that distance separately.
 */
/* Run whatever phase the command left the controller in to its end, so the next
 * command can be issued. A data phase is a handshake and this is the host's
 * half of it; the bound is the largest transfer §5 defines. */
static void drain(ap_omti_t *o) {
  for (unsigned i = 0; i < 20000u; i++) {
    switch (ap_omti_disk_phase(o)) {
    case AP_OMTI_PHASE_DATA_IN:
      (void)ap_omti_disk_read(o, AP_OMTI_DISK_DATA);
      break;
    case AP_OMTI_PHASE_DATA_OUT:
      ap_omti_disk_write(o, AP_OMTI_DISK_DATA, 0u);
      break;
    case AP_OMTI_PHASE_EXECUTING:
      /* The drive is positioning. A test that is not about access time says so
       * by advancing straight to the deadline, which is the one place in this
       * suite that has to know commands take any. */
      ap_omti_advance(o, o->completion_at);
      break;
    case AP_OMTI_PHASE_RESET:
      /* §4.3's 100 µs. A test that is not about the reset window says so by
       * advancing straight through it, exactly as it does for a seek. */
      ap_omti_advance(o, o->completion_at);
      break;
    case AP_OMTI_PHASE_IDLE:
    case AP_OMTI_PHASE_COMMAND:
    case AP_OMTI_PHASE_STATUS:
      return;
    }
  }
  TEST_FAIL_MESSAGE("a data phase never ended");
}

/* The command set has no holes: **every** opcode §5 accepts reaches a case.
 *
 * This is the test that stops the loop the OMTI work had fallen into -- `1E`
 * implemented, boot, `0F` named, implemented, boot, `1F` named. Each round cost
 * a twenty-minute boot to learn one opcode, and each one was already printed in
 * a manual on disk. Asserted over `ap_omti_cdb_accepted_by_esdi` itself rather
 * than a list written out here, so a command added to the accepted set without
 * an implementation fails immediately instead of at the next boot.
 *
 * No drive is attached, so most of these report `04 DRIVE NOT READY`. That is
 * the point: the assertion is only that the controller *decoded* the command,
 * and `20` is the one answer that says it did not. */
static void test_every_command_the_esdi_set_accepts_reaches_an_implementation(void) {
  unsigned accepted = 0;
  for (unsigned command = 0; command < 256u; command++) {
    if (!ap_omti_cdb_accepted_by_esdi((uint8_t)command)) {
      continue;
    }
    accepted++;

    ap_omti_t o;
    ap_omti_reset(&o);
    /* One block, and a zero address -- valid for every command that takes one,
     * and ignored by the ones that do not. `COPY` is ten bytes, which
     * `ap_omti_cdb_length` knows and this follows rather than assuming six. */
    uint8_t cdb[AP_OMTI_CDB_LONG] = {0};
    cdb[0] = (uint8_t)command;
    cdb[4] = 1u;
    wait_out_reset(&o);
    ap_omti_disk_write(&o, AP_OMTI_DISK_CONFIG, 0x00); /* SELECT */
    for (unsigned i = 0; i < ap_omti_cdb_length((uint8_t)command); i++) {
      ap_omti_disk_write(&o, AP_OMTI_DISK_DATA, cdb[i]);
    }
    drain(&o);

    TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&o));
    (void)ap_omti_disk_read(&o, AP_OMTI_DISK_DATA);
    (void)ap_omti_disk_read(&o, AP_OMTI_DISK_STATUS);

    static const uint8_t sense[6] = {0x03, 0, 0, 0, 0, 0};
    issue(&o, sense);
    char why[64];
    (void)snprintf(why, sizeof why, "command %02X reached the default arm",
                   command);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0x20, ap_omti_disk_read(&o, AP_OMTI_DISK_DATA),
                                  why);
  }
  /* And the loop actually ran: a set that accepted nothing would pass every
   * assertion above without making a single one. */
  TEST_ASSERT_EQUAL_UINT(28u, accepted);
}

static void test_a_command_outside_the_esdi_set_reports_invalid_command(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  /* `0C INITIALIZE DRIVE CHARACTERISTICS` is ST506-only. The controller does
   * not decode it at all, which is Appendix A's `20` in the most literal
   * reading it has. */
  static const uint8_t cdb[6] = {0x0C, 0, 0, 0, 0, 0};
  issue(&o, cdb);

  TEST_ASSERT_EQUAL_HEX8(0x02, ap_omti_disk_read(&o, AP_OMTI_DISK_DATA));
  (void)ap_omti_disk_read(&o, AP_OMTI_DISK_STATUS);

  static const uint8_t sense[6] = {0x03, 0, 0, 0, 0, 0};
  issue(&o, sense);
  TEST_ASSERT_EQUAL_HEX8(0x20, ap_omti_disk_read(&o, AP_OMTI_DISK_DATA));
}

/* The floppy's own two lines, `IRQ6` and `DRQ2`, which the board placed and
 * left undriven because this half had nothing to derive them from.
 *
 * It has Table 4-3's Digital Output Register bit 3, which gates both -- the
 * same shape as the fixed disk's `IREQ` on its MASK register. `IRQ6` follows
 * the **result** phase, the FDC's completion; `DRQ2` the **execution** phase, a
 * byte in flight. Two different conditions, which is what the board's comment
 * said and why they are two functions. */
static void test_the_floppy_drives_its_own_interrupt_and_dma_lines(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  /* Out of reset, with the enable bit clear, neither line is up. */
  TEST_ASSERT_FALSE(ap_omti_fdc_irq(&o));
  TEST_ASSERT_FALSE(ap_omti_fdc_dma_request(&o));

  /* SENSE INTERRUPT STATUS has a result phase and no execution phase, so it
   * raises the interrupt and not the DMA request. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR,
                    (uint8_t)(AP_OMTI_DOR_NOT_RESET | AP_OMTI_DOR_INT_DMA));
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SENSE_INTERRUPT);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_fdc_phase(&o));
  TEST_ASSERT_TRUE(ap_omti_fdc_irq(&o));
  TEST_ASSERT_FALSE(ap_omti_fdc_dma_request(&o));

  /* Reading the result bytes takes it down: the completion has been collected. */
  (void)ap_omti_fdc_read(&o, AP_OMTI_FDC_DATA);
  (void)ap_omti_fdc_read(&o, AP_OMTI_FDC_DATA);
  TEST_ASSERT_FALSE(ap_omti_fdc_irq(&o));

  /* And the enable bit is real: with it clear the same state raises nothing,
   * or a polled driver would be interrupted by a controller it never armed. */
  ap_omti_t polled;
  ap_omti_reset(&polled);
  ap_omti_fdc_write(&polled, AP_OMTI_FDC_DOR, AP_OMTI_DOR_NOT_RESET);
  ap_omti_fdc_write(&polled, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SENSE_INTERRUPT);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_fdc_phase(&polled));
  TEST_ASSERT_FALSE(ap_omti_fdc_irq(&polled));
}

/* Table 4-3's two register bits that were stored and never read: `NDMA`, "non-
 * DMA mode, execution phase only", and the two motor enables. */
static void test_the_msr_reports_non_dma_mode_and_the_motors(void) {
  ap_omti_t o;
  ap_omti_reset(&o);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR, AP_OMTI_DOR_NOT_RESET);

  /* No command: no execution phase, so no `NDMA` however the mode is set. */
  TEST_ASSERT_EQUAL_HEX8(0u, ap_omti_fdc_read(&o, AP_OMTI_FDC_MSR) &
                                 AP_OMTI_MSR_NDMA);

  /* A command with a result phase enters it, and with the DOR's interrupt/DMA
   * enable clear that is non-DMA mode. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SENSE_INTERRUPT);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_fdc_phase(&o));
  TEST_ASSERT_EQUAL_HEX8(0u, ap_omti_fdc_read(&o, AP_OMTI_FDC_MSR) &
                                 AP_OMTI_MSR_NDMA);

  /* The motors: stored, and now readable. */
  TEST_ASSERT_FALSE(ap_omti_fdc_motor_on(&o, 0u));
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR,
                    (uint8_t)(AP_OMTI_DOR_NOT_RESET |
                              AP_OMTI_DOR_DRIVE_A_MOTOR));
  TEST_ASSERT_TRUE(ap_omti_fdc_motor_on(&o, 0u));
  TEST_ASSERT_FALSE(ap_omti_fdc_motor_on(&o, 1u));
}

/* §6.3's three command modifiers, which were defined and never read -- so every
 * floppy command ran as if all three were clear. */
static void test_the_floppy_command_modifiers_are_read(void) {
  ap_omti_t o;
  ap_omti_reset(&o);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR, AP_OMTI_DOR_NOT_RESET);

  /* No command: no modifiers. */
  TEST_ASSERT_FALSE(ap_omti_fdc_multitrack(&o));

  /* READ DATA with all three set. The opcode is the low five bits, so the
   * modifiers ride above it and must not be mistaken for a different command. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA,
                    (uint8_t)(AP_OMTI_FDC_READ_DATA | AP_OMTI_FDC_MT |
                              AP_OMTI_FDC_MF | AP_OMTI_FDC_SK));
  TEST_ASSERT_TRUE(ap_omti_fdc_multitrack(&o));
  TEST_ASSERT_TRUE(ap_omti_fdc_mfm(&o));
  TEST_ASSERT_TRUE(ap_omti_fdc_skip_deleted(&o));

  /* And the same command without them. */
  ap_omti_t plain;
  ap_omti_reset(&plain);
  ap_omti_fdc_write(&plain, AP_OMTI_FDC_DOR, AP_OMTI_DOR_NOT_RESET);
  ap_omti_fdc_write(&plain, AP_OMTI_FDC_DATA, AP_OMTI_FDC_READ_DATA);
  TEST_ASSERT_FALSE(ap_omti_fdc_multitrack(&plain));
  TEST_ASSERT_FALSE(ap_omti_fdc_mfm(&plain));
  TEST_ASSERT_FALSE(ap_omti_fdc_skip_deleted(&plain));
}

/* `IRQ14`, which the board could not wire because nothing here derived it.
 *
 * §4.2 gives the raise -- "If the INTERRUPT ENABLE bit was previously set in
 * the MASK register, the REQ bit is set in the STATUS byte, along with IRQ14 on
 * the system bus" -- and §4.3 gives the clear, "the controller clears the IREQ
 * and IRQ14 (if enabled)" when the status byte is read. Both are conditions on
 * state this part already keeps, so the line is a derivation rather than a
 * latch, and there is nothing to invent.
 *
 * It is worth its own test because the boot PROM's driver *polls*: a machine
 * with no interrupt line at all loaded an operating system off this controller
 * without complaint, and only Domain/OS's own driver -- which waits -- noticed,
 * by printing `DISK TIMEOUT`. A device whose absence the firmware cannot detect
 * is exactly the kind that stays absent. */
static void test_a_completed_command_asks_for_an_interrupt_when_enabled(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  /* Idle, so nothing is asking. */
  TEST_ASSERT_FALSE(ap_omti_disk_irq(&o));

  /* A command completes with the interrupt disabled, and `IREQ` stays **down**.
   *
   * That is the half of §4.2's ambiguous sentence this core first got wrong.
   * Domain/OS is what settles it: its driver polls the status register waiting
   * for exactly `CF` -- `BSY|C/D|I/O|REQ` with `IREQ` clear -- and an
   * unconditional `IREQ` left the controller at `EF` for ever, which is the
   * number the operating system printed as `DISK CONTROLLER STATE = EF` before
   * giving up. `omti8621.cpp` sets `IREQ` inside
   * `if (m_mask_port & OMTI_MASK_INTE)` and nowhere else. */
  static const uint8_t cdb[6] = {0x00, 0, 0, 0, 0, 0}; /* TEST DRIVE READY */
  issue(&o, cdb);
  TEST_ASSERT_EQUAL_HEX8(0u, ap_omti_disk_read(&o, AP_OMTI_DISK_STATUS) &
                                 AP_OMTI_ST_IREQ);
  TEST_ASSERT_EQUAL_HEX8(0xCFu, ap_omti_disk_read(&o, AP_OMTI_DISK_STATUS));
  TEST_ASSERT_FALSE(ap_omti_disk_irq(&o));

  /* Enabled *before* the command, and now both the bit and the line are up. */
  ap_omti_t enabled;
  ap_omti_reset(&enabled);
  ap_omti_disk_write(&enabled, AP_OMTI_DISK_MASK,
                     AP_OMTI_MASK_INTERRUPT_ENABLE);
  issue(&enabled, cdb);
  TEST_ASSERT_EQUAL_HEX8(AP_OMTI_ST_IREQ,
                         ap_omti_disk_read(&enabled, AP_OMTI_DISK_STATUS) &
                             AP_OMTI_ST_IREQ);
  TEST_ASSERT_TRUE(ap_omti_disk_irq(&enabled));

  /* And turning the enable off takes the bit down with it, so a driver that
   * switches to polling does not find a completion it has already collected. */
  ap_omti_disk_write(&enabled, AP_OMTI_DISK_MASK, 0u);
  TEST_ASSERT_EQUAL_HEX8(0u, ap_omti_disk_read(&enabled, AP_OMTI_DISK_STATUS) &
                                 AP_OMTI_ST_IREQ);
  TEST_ASSERT_FALSE(ap_omti_disk_irq(&enabled));

  /* Enabling interrupts *after* a command completed does not raise `IREQ`
   * retrospectively: the bit is set at completion or not at all. */
  ap_omti_disk_write(&o, AP_OMTI_DISK_MASK, AP_OMTI_MASK_INTERRUPT_ENABLE);
  TEST_ASSERT_FALSE(ap_omti_disk_irq(&o));

  /* And reading the status byte drops it, because that is what clears `IREQ`.
   * A line that stayed up after the host collected the completion would be
   * taken again the moment the handler returned. */
  ap_omti_t collected;
  ap_omti_reset(&collected);
  ap_omti_disk_write(&collected, AP_OMTI_DISK_MASK,
                     AP_OMTI_MASK_INTERRUPT_ENABLE);
  issue(&collected, cdb);
  TEST_ASSERT_TRUE(ap_omti_disk_irq(&collected));
  (void)ap_omti_disk_read(&collected, AP_OMTI_DISK_DATA);
  TEST_ASSERT_FALSE(ap_omti_disk_irq(&collected));
}


/* `DREQ`, the other line the board could not wire.
 *
 * §4.3 gates it on the MASK byte's DMA ENABLE -- "If the DMA ENABLE bit in the
 * MASK byte has been previously set, data will be transferred in DMA mode ...
 * it will set the DREQ bit" -- so a controller in programmed I/O must *not*
 * assert it, and one in DMA mode must, for exactly as long as the data phase
 * lasts. `board/ap_disk.h` deferred the line because nothing knew a transfer
 * was in progress while only the register sets were modelled; the command sets
 * know, and this is the bit they set. */
static void test_the_data_phase_asks_for_dma_only_when_dma_is_enabled(void) {
  ap_omti_t polled;
  ap_omti_reset(&polled);

  /* Programmed I/O: a data phase with no request in it. A controller asserting
   * `DREQ` here would ask for a cycle nobody arranged. */
  static const uint8_t sense[6] = {0x03, 0, 0, 0, 0, 0}; /* REQUEST SENSE */
  issue(&polled, sense);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_disk_phase(&polled));
  TEST_ASSERT_FALSE(ap_omti_disk_dma_request(&polled));

  /* The same command with DMA enabled, and now it asks. */
  ap_omti_t dma;
  ap_omti_reset(&dma);
  ap_omti_disk_write(&dma, AP_OMTI_DISK_MASK, AP_OMTI_MASK_DMA_ENABLE);
  issue(&dma, sense);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_disk_phase(&dma));
  TEST_ASSERT_TRUE(ap_omti_disk_dma_request(&dma));

  /* And it stops asking once the phase is over: a request left standing would
   * run the whole of memory through a finished transfer. */
  for (unsigned i = 0; i < sizeof dma.sense; i++) {
    (void)ap_omti_disk_read(&dma, AP_OMTI_DISK_DATA);
  }
  TEST_ASSERT_FALSE(ap_omti_disk_dma_request(&dma));
}

static void test_two_controllers_reset_alike_hold_identical_state(void) {
  ap_omti_t a;
  ap_omti_t b;
  memset(&a, 0xAA, sizeof a);
  memset(&b, 0x55, sizeof b);
  ap_omti_reset(&a);
  ap_omti_reset(&b);
  TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof a);
}

/* §5.1.1, byte 1: "Bit 5 identifies the Logical Unit Number (LUN)." One drive
 * is attached here, so LUN 1 names a unit that is not fitted and the controller
 * must say so. This model served every command from the attached drive whatever
 * LUN it carried, so a Domain/OS boot was told a second Winchester was present
 * and healthy -- `DRIVE 1 PASSED.` where the hardware prints `(NOT FOUND)`. */
static void test_test_drive_ready_fails_for_a_lun_with_no_drive(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  /* No drive attached at all: LUN 0 must fail too, which is the behaviour that
   * already worked and is asserted here so the LUN change cannot silently
   * invert it. */
  static const uint8_t lun0[6] = {0x00, 0x00, 0, 0, 0, 0};
  issue(&o, lun0);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&o));
  /* §5.3's completion byte, bit 1: an error. A fresh controller for the second
   * command, since the status phase has to be read out before another CDB is
   * accepted and `issue` asserts the command phase before every byte. */
  TEST_ASSERT_EQUAL_HEX8(0x02u,
                         ap_omti_disk_read(&o, AP_OMTI_DISK_DATA) & 0x02u);

  ap_omti_reset(&o);
  /* Byte 1 bit 5 set: LUN 1. */
  static const uint8_t lun1[6] = {0x00, 0x20, 0, 0, 0, 0};
  issue(&o, lun1);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&o));
  TEST_ASSERT_EQUAL_HEX8(0x02u,
                         ap_omti_disk_read(&o, AP_OMTI_DISK_DATA) & 0x02u);
}

/* §5.3's status register: "Bit 5 -- Indicates the LUN address of the device
 * associated with this command." Only bit 1, the command status, was ever set,
 * so a driver reading the completion byte was told every command belonged to
 * unit 0. */
static void test_the_completion_byte_carries_the_commands_lun(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  static const uint8_t lun0[6] = {0x00, 0x00, 0, 0, 0, 0};
  issue(&o, lun0);
  TEST_ASSERT_EQUAL_HEX8(0x00u, ap_omti_disk_read(&o, AP_OMTI_DISK_DATA) & 0x20u);

  ap_omti_reset(&o);
  static const uint8_t lun1[6] = {0x00, 0x20, 0, 0, 0, 0};
  issue(&o, lun1);
  TEST_ASSERT_EQUAL_HEX8(0x20u, ap_omti_disk_read(&o, AP_OMTI_DISK_DATA) & 0x20u);
}

/* §5.2 bit 5, sector address conversion. The CDB's address is in a host
 * geometry of sixteen heads and the jumpered sectors per track, and the
 * controller re-expresses it in the drive's -- so with the bit set the same CDB
 * reaches a different block, on any drive whose geometry is not the
 * conversion's. This board's drives have fifteen and eight heads. */
static void test_sector_address_conversion_uses_sixteen_heads(void) {
  /* The 348 MB Maxtor of `image/ap_awd.h`: 1223 cylinders, 15 heads, 18
   * sectors. */
  const ap_awd_geometry_t maxtor = {.cylinders = 1223u, .heads = 15u,
                                    .sectors = 18u};

  uint32_t plain = 0u;
  TEST_ASSERT_TRUE(ap_awd_lba(maxtor, 2u, 3u, 4u, &plain));
  TEST_ASSERT_EQUAL_UINT32((2u * 15u + 3u) * 18u + 4u, plain);

  const uint32_t converted =
      (2u * AP_OMTI_CONVERSION_HEADS + 3u) * AP_OMTI_CONVERSION_SECTORS + 4u;
  TEST_ASSERT_EQUAL_UINT32((2u * 16u + 3u) * 18u + 4u, converted);
  TEST_ASSERT_TRUE(converted != plain);

  /* The constants are the manual's: sixteen heads always, and the sectors per
   * track the jumper table gives for this board -- 18, which is the only entry
   * matching both Apollo drives' own geometry. */
  TEST_ASSERT_EQUAL_UINT(16u, AP_OMTI_CONVERSION_HEADS);
  TEST_ASSERT_EQUAL_UINT(18u, AP_OMTI_CONVERSION_SECTORS);
  TEST_ASSERT_EQUAL_UINT(18u, maxtor.sectors);
}

/* Table 4-3 gives AT `3F6` write as the Additional Control Register and `3F7`
 * write as the Diskette Control Register. Two registers -- and this model kept
 * both in one byte, so a data-rate selection cleared whatever write
 * precompensation had been programmed. `002398-04` p. 12-14 is what made it
 * visible, by being the only document that says what `3F6` contains. */
static void test_the_two_floppy_control_registers_are_not_one_register(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  /* p. 12-14: write precompensation in bits 2-0, interface pin 2 -- density and
   * speed control -- at bit 3, pins 4 and 6 above it. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_CONTROL, 0x2Du); /* precomp 5, pins 2 and 6 */
  TEST_ASSERT_EQUAL_HEX8(5u, ap_omti_fdc_precompensation(&o));
  TEST_ASSERT_TRUE(ap_omti_fdc_control_pin(&o, 2u));
  TEST_ASSERT_FALSE(ap_omti_fdc_control_pin(&o, 4u));
  TEST_ASSERT_TRUE(ap_omti_fdc_control_pin(&o, 6u));

  /* The 8640 manual's §5.1 data rates. Selecting one must not disturb the
   * register above, which is the whole of what was wrong. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DIR, AP_OMTI_FDC_RATE_250K);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_FDC_RATE_250K, ap_omti_fdc_data_rate(&o));
  TEST_ASSERT_EQUAL_HEX8(5u, ap_omti_fdc_precompensation(&o));
  TEST_ASSERT_TRUE(ap_omti_fdc_control_pin(&o, 6u));

  /* Zero at reset is 500 Kbit/s, which is the only rate this machine's drive
   * runs at -- `008778-03` §7.2, and `AP_OMTI_FDC_TRANSFER_BYTES_PER_SEC` is
   * that figure in bytes. So the register powers up already selecting the
   * drive's rate, which is why nothing here is timed off it. */
  ap_omti_reset(&o);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_FDC_RATE_500K, ap_omti_fdc_data_rate(&o));
  TEST_ASSERT_EQUAL_UINT(62500u, AP_OMTI_FDC_TRANSFER_BYTES_PER_SEC);
}

/* ## The boot PROM's own floppy initialisation, which proves the split
 *
 * `3500_BOOT_12191_7` at `003266`:
 *
 *     MOVE.B #$1C,$0002(A0)    ; 3F2 Digital Output
 *     MOVE.L #$00061A80,D0     ; a delay
 *     SUBQ.L #1,D0
 *     BPL.S  $003272
 *     MOVE.B #$00,$0007(A0)    ; 3F7 Diskette Control
 *     MOVE.B #$02,$0006(A0)    ; 3F6 Additional Control
 *
 * Two registers, two different values, in adjacent instructions. **That is the
 * proof the split was right**: with both landing in one byte -- which is what
 * this model did until `002398-04` p. 12-14 was walked against it -- the second
 * write would overwrite the first, and a driver reading the data rate back
 * would get `2`, which is 250 Kbit/s on a drive that runs at 500.
 *
 * The fix was made from documents alone. Here is the machine's own firmware
 * making the same distinction, four sessions of reasoning later and in six
 * bytes. */
static void test_the_boot_proms_floppy_initialisation_needs_two_registers(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR, 0x1Cu);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DIR, 0x00u);     /* 3F7 */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_CONTROL, 0x02u); /* 3F6 */

  /* `$1C` is drive A's motor, interrupts and DMA enabled, and out of reset --
   * the three the firmware needs before it can do anything at all. */
  TEST_ASSERT_TRUE(ap_omti_fdc_motor_on(&o, 0u));
  TEST_ASSERT_FALSE(ap_omti_fdc_in_reset(&o));

  /* And both of the last two writes survive. The rate is the one the drive
   * runs at, not the precompensation value wearing its address. */
  TEST_ASSERT_EQUAL_INT(AP_OMTI_FDC_RATE_500K, ap_omti_fdc_data_rate(&o));
  TEST_ASSERT_EQUAL_HEX8(2u, ap_omti_fdc_precompensation(&o));
}

/* §6.4.4: ST3's bit 0 is "not used - always 1" and bit 1 "not used - always
 * zero". This built the byte as `ALWAYS | unit`, which is neither that register
 * nor `002398-04` p. 12-14's `UN1`/`UN0` -- it was both at once, and answered
 * `03` for drive B. The header records which reading is followed and what would
 * settle it; this asserts that the byte is one of them rather than a mixture. */
static void test_sense_drive_status_does_not_report_the_unit(void) {
  ap_omti_t o;

  for (unsigned unit = 0u; unit < 2u; unit++) {
    ap_omti_reset(&o);
    /* Out of reset, and with a drive selected: §6.3.9 takes the unit in the
     * command's second byte, bits 1-0. */
    ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR, AP_OMTI_DOR_NOT_RESET);
    ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SENSE_DRIVE);
    ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, (uint8_t)unit);
    const uint8_t st3 = ap_omti_fdc_read(&o, AP_OMTI_FDC_DATA);

    /* Track 0 because the heads have never moved, write protect because no
     * media is fitted, and bit 0's constant. Nothing in bits 1-0 beyond it. */
    TEST_ASSERT_EQUAL_HEX8(0u, st3 & 0x02u);
    TEST_ASSERT_EQUAL_HEX8(AP_OMTI_ST3_ALWAYS, st3 & AP_OMTI_ST3_ALWAYS);
    TEST_ASSERT_EQUAL_HEX8(AP_OMTI_ST3_TRACK_0, st3 & AP_OMTI_ST3_TRACK_0);
    /* And the three the handbook names and this part calls constant stay
     * clear, which is the reading being followed made assertable. */
    TEST_ASSERT_EQUAL_HEX8(0u, st3 & AP_OMTI_ST3_FAULT);
    TEST_ASSERT_EQUAL_HEX8(0u, st3 & AP_OMTI_ST3_READY);
    TEST_ASSERT_EQUAL_HEX8(0u, st3 & AP_OMTI_ST3_TWO_SIDED);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_two_floppy_control_registers_are_not_one_register);
  RUN_TEST(test_the_boot_proms_floppy_initialisation_needs_two_registers);
  RUN_TEST(test_sense_drive_status_does_not_report_the_unit);
  RUN_TEST(test_sector_address_conversion_uses_sixteen_heads);
  RUN_TEST(test_test_drive_ready_fails_for_a_lun_with_no_drive);
  RUN_TEST(test_the_completion_byte_carries_the_commands_lun);
  RUN_TEST(test_the_measured_fixed_disk_ports_are_reproduced);
  RUN_TEST(test_the_status_bits_seven_and_six_cannot_be_cleared);
  RUN_TEST(test_selecting_the_controller_makes_it_busy);
  RUN_TEST(test_a_reset_controller_is_not_idle_for_one_hundred_microseconds);
  RUN_TEST(test_a_select_inside_the_reset_window_is_refused);
  RUN_TEST(test_writing_the_reset_register_restarts_the_hundred_microseconds);
  RUN_TEST(test_the_reset_window_is_offered_to_the_scheduler);
  RUN_TEST(test_the_reset_port_is_a_function_not_a_store);
  RUN_TEST(test_the_data_register_changes_width_with_the_command_bit);
  RUN_TEST(test_the_measured_floppy_block_is_reproduced);
  RUN_TEST(test_clearing_the_output_register_holds_the_floppy_in_reset);
  RUN_TEST(test_the_two_halves_share_nothing);
  RUN_TEST(test_read_sector_buffer_enters_the_data_phase_without_a_drive);
  RUN_TEST(test_a_reset_leaves_the_identification_block_in_the_buffer);
  RUN_TEST(test_a_word_read_of_the_data_port_takes_two_buffer_bytes);
  RUN_TEST(test_a_block_count_past_the_buffer_is_refused);
  RUN_TEST(test_writing_the_sector_buffer_does_not_touch_the_drive);
  RUN_TEST(test_every_command_the_esdi_set_accepts_reaches_an_implementation);
  RUN_TEST(test_a_command_outside_the_esdi_set_reports_invalid_command);
  RUN_TEST(test_the_msr_reports_non_dma_mode_and_the_motors);
  RUN_TEST(test_the_floppy_command_modifiers_are_read);
  RUN_TEST(test_the_floppy_drives_its_own_interrupt_and_dma_lines);
  RUN_TEST(test_a_completed_command_asks_for_an_interrupt_when_enabled);
  RUN_TEST(test_the_data_phase_asks_for_dma_only_when_dma_is_enabled);
  RUN_TEST(test_two_controllers_reset_alike_hold_identical_state);
  return UNITY_END();
}
