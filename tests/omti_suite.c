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

/* Run out whatever the controller is executing, however long it is.
 *
 * §5.4.1's TEST DRIVE READY "will wait up to 50 seconds for the drive to come
 * ready", so on a controller with **no drive attached** -- which most of this
 * suite's fixtures are, because they are testing the register interface rather
 * than a surface -- that command no longer completes in the instant it is
 * issued. The tests below are about the completion byte, the LUN bits and the
 * interrupt, none of which is a timing claim, so they wait rather than assert
 * the old instantaneous arrival. Advancing to `completion_at` keeps them exact
 * without naming the duration in each one. */
static void settle(ap_omti_t *o) {
  if (ap_omti_disk_phase(o) == AP_OMTI_PHASE_EXECUTING) {
    ap_omti_advance(o, o->completion_at);
  }
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

/* `[OMTI]` §4.3's SELECTION STATE in full, and the reason the test above is not
 * enough: "the controller responds to a selection request by asserting the BSY
 * bit ... First, the C/D bit of the STATUS register is set. Then the REQ bit is
 * set, asking for the first command byte to be written to the DATA OUT register
 * in BYTE mode."
 *
 * `ap_omti.c` says in as many words that "a model asserting only `BSY` leaves
 * the host waiting for a request that never comes" -- and asserting only `BSY`
 * is all the test above measured. `I/O` must stay clear: the transfer is *to*
 * the controller. */
static void test_selecting_the_controller_asks_for_the_first_command_byte(void) {
  ap_omti_t o;
  ap_omti_reset(&o);
  wait_out_reset(&o);

  ap_omti_disk_write(&o, AP_OMTI_DISK_CONFIG, 0x00);
  const uint8_t status = ap_omti_disk_read(&o, AP_OMTI_DISK_STATUS);
  TEST_ASSERT_EQUAL_HEX8(AP_OMTI_ST_BSY, status & AP_OMTI_ST_BSY);
  TEST_ASSERT_EQUAL_HEX8(AP_OMTI_ST_CD, status & AP_OMTI_ST_CD);
  TEST_ASSERT_EQUAL_HEX8(AP_OMTI_ST_REQ, status & AP_OMTI_ST_REQ);
  TEST_ASSERT_EQUAL_HEX8(0u, status & AP_OMTI_ST_IO);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_COMMAND, ap_omti_disk_phase(&o));
}

/* "The IDLE STATE is the only time the controller will respond to a select
 * request" -- §4.3, and the general rule of which the reset-window refusal
 * below is one case. A stray select part way through a descriptor block must
 * not restart the sequence, or a driver's spurious write would silently discard
 * a command it had half sent and the next byte would land at offset one. */
static void test_a_select_part_way_through_a_command_is_ignored(void) {
  ap_omti_t o;
  ap_omti_reset(&o);
  wait_out_reset(&o);

  ap_omti_disk_write(&o, AP_OMTI_DISK_CONFIG, 0x00);
  /* Two bytes of a six-byte descriptor block, then the stray select. */
  ap_omti_disk_write(&o, AP_OMTI_DISK_DATA, 0x08u); /* READ */
  ap_omti_disk_write(&o, AP_OMTI_DISK_DATA, 0x00u);
  ap_omti_disk_write(&o, AP_OMTI_DISK_CONFIG, 0x00);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_COMMAND, ap_omti_disk_phase(&o));

  /* And the block still completes on its original byte count: four more bytes
   * finish it. If the select had restarted the sequence the controller would
   * still be collecting here. */
  ap_omti_disk_write(&o, AP_OMTI_DISK_DATA, 0x00u);
  ap_omti_disk_write(&o, AP_OMTI_DISK_DATA, 0x00u);
  ap_omti_disk_write(&o, AP_OMTI_DISK_DATA, 0x01u);
  ap_omti_disk_write(&o, AP_OMTI_DISK_DATA, 0x00u);
  TEST_ASSERT_NOT_EQUAL_INT(AP_OMTI_PHASE_COMMAND, ap_omti_disk_phase(&o));
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

/* §5.4.14 prints §5.4.13's cap table with a row §5.4.13 omits -- 256 bytes per
 * sector, 31 blocks -- and the fourth row is what turns three points into a
 * rule. `ap_omti.h` derives the cap as "the largest count whose bytes are
 * strictly less than the buffer"; this checks that against all four printed
 * rows at the 8K buffer they were printed for, and then against the boundary
 * this part's 32K produces.
 *
 * The arithmetic is deliberately written out rather than calling the macro with
 * a different buffer: the macro is the claim, and a test that computed it the
 * same way would agree with itself. */
static void test_the_buffer_cap_rule_reproduces_all_four_printed_rows(void) {
  static const struct {
    unsigned sector_size;
    unsigned blocks;
  } printed[4] = {{256u, 31u}, {512u, 15u}, {1024u, 7u}, {1056u, 7u}};

  for (unsigned i = 0; i < 4u; i++) {
    /* The largest N with N * size < 8192. */
    unsigned n = 0;
    while ((n + 1u) * printed[i].sector_size < 8192u) {
      n++;
    }
    TEST_ASSERT_EQUAL_UINT(printed[i].blocks, n);
  }

  /* And this part, which reports 32K and has 1056-byte sectors: 31 blocks.
   * `floor(8192/1056)` against `floor(8192/1056) - 1` was the ambiguity the
   * `PROVISIONAL` named, and the 256 row settles it at the plain floor --
   * 31 x 1056 = 32736 fits a 32K buffer and 32 x 1056 does not. The value is
   * unchanged by the correction; what changed is that it now follows from a
   * rule with four supporting rows rather than from one of two readings. */
  TEST_ASSERT_EQUAL_UINT(31u, AP_OMTI_MAX_BUFFER_BLOCKS);
  TEST_ASSERT_TRUE(AP_OMTI_MAX_BUFFER_BLOCKS * AP_AWD_SECTOR_BYTES <
                   AP_OMTI_BUFFER_RAM_BYTES);
  TEST_ASSERT_FALSE((AP_OMTI_MAX_BUFFER_BLOCKS + 1u) * AP_AWD_SECTOR_BYTES <
                    AP_OMTI_BUFFER_RAM_BYTES);
}

static void test_the_last_block_the_buffer_holds_is_accepted(void) {
  /* The pair the off-by-one lived in: 63 goes through and 64 is refused, where
   * the cap was `buffer / size` and 64 went through. */
  ap_omti_t o;
  ap_omti_reset(&o);
  const uint8_t cdb[6] = {0x0E, 0, 0, 0, (uint8_t)AP_OMTI_MAX_BUFFER_BLOCKS, 0};
  issue(&o, cdb);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_disk_phase(&o));
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

  /* A command with a result phase enters it. `SENSE INTERRUPT STATUS` has no
   * *execution* phase, so `NDMA` stays clear whatever the mode -- `[765A]` p.7
   * confines the bit to the execution phase. */
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
  settle(&o);
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
  settle(&enabled);
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
  settle(&collected);
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

/* §6.2's SRT, and Table 7-7's floor under it.
 *
 * The step rate is programmed by SPECIFY and the drive cannot beat its own
 * "3 msec minimum", so the effective rate is the slower of the two. Three
 * cases, and the middle one is the reason the constant did not have to change:
 * a driver programming `1101` -- §6.2's 3 ms -- lands exactly on the figure
 * this core already verified against Table 7-7's published 94 ms average. */
static void specify(ap_omti_t *o, uint8_t srt) {
  ap_omti_fdc_write(o, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SPECIFY);
  ap_omti_fdc_write(o, AP_OMTI_FDC_DATA, (uint8_t)(srt << 4));
  ap_omti_fdc_write(o, AP_OMTI_FDC_DATA, 0x00u);
}

/* Seek from cylinder 0 to 1: one step plus the settle, so the step time is the
 * whole of the difference between these cases. */
static ap_time_t one_step_seek(uint8_t srt, bool program) {
  ap_omti_t o;
  ap_omti_reset(&o);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR, AP_OMTI_DOR_NOT_RESET);
  if (program) {
    specify(&o, srt);
  }
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SEEK);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, 0x00u);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, 0x01u);
  return o.fdc_seek_at[0] - o.now;
}

static void test_a_programmed_step_rate_paces_the_seek(void) {
  /* `1101` is §6.2's 3 ms on the 1.2 Mbyte drive, and Table 7-7's floor is the
   * same 3 ms, so this is the case where the two agree. */
  TEST_ASSERT_EQUAL_UINT64(AP_OMTI_FDC_TRACK_TO_TRACK + AP_OMTI_FDC_SETTLING,
                           one_step_seek(0x0Du, true));

  /* `1000` is 8 ms, slower than the drive's minimum, so the host's rate wins. */
  TEST_ASSERT_EQUAL_UINT64(AP_TIME_BASE_HZ / 1000u * 8u + AP_OMTI_FDC_SETTLING,
                           one_step_seek(0x08u, true));

  /* `1111` is 1 ms, faster than the mechanism can move, so the floor wins. A
   * model without the floor would report a seek this drive cannot perform. */
  TEST_ASSERT_EQUAL_UINT64(AP_OMTI_FDC_TRACK_TO_TRACK + AP_OMTI_FDC_SETTLING,
                           one_step_seek(0x0Fu, true));
}

/* And with no SPECIFY at all the drive minimum is used, which is what every
 * seek test written before the rate was programmable measured. `[8640]` §5
 * names a default without printing it, so this core uses the one rate that
 * cannot claim a speed the mechanism does not have rather than inventing it. */
static void test_an_unprogrammed_controller_steps_at_the_drive_minimum(void) {
  TEST_ASSERT_EQUAL_UINT64(AP_OMTI_FDC_TRACK_TO_TRACK + AP_OMTI_FDC_SETTLING,
                           one_step_seek(0x00u, false));
}

/* §5.4.1: "The controller will wait up to 50 seconds for the drive to come
 * ready", printed again as §2.5's `1701-C`. An interface with nothing on it
 * never asserts ready, so the controller spends the whole timeout before
 * reporting `04 Drive Not Ready` -- where this core used to report it in the
 * instant the command was issued.
 *
 * The duration is asserted from both sides, because a test that only checks it
 * has finished by the deadline passes against a controller that never waited. */
static void test_a_drive_that_is_never_ready_costs_the_whole_timeout(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  static const uint8_t cdb[6] = {0x00, 0x00, 0, 0, 0, 0}; /* TEST DRIVE READY */
  issue(&o, cdb);
  const ap_time_t issued = o.now;
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_EXECUTING, ap_omti_disk_phase(&o));

  ap_omti_advance(&o, issued + AP_OMTI_READY_TIMEOUT - 1u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_EXECUTING, ap_omti_disk_phase(&o));

  ap_omti_advance(&o, issued + AP_OMTI_READY_TIMEOUT);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&o));
  TEST_ASSERT_EQUAL_HEX8(0x02u,
                         ap_omti_disk_read(&o, AP_OMTI_DISK_DATA) & 0x02u);
}

/* Fifty seconds exactly, in the one unit this machine counts in. Written as a
 * separate assertion because the test above would pass just as well if the
 * constant were fifty milliseconds -- it checks the model honours the constant,
 * not that the constant is the manual's number. */
static void test_the_ready_timeout_is_fifty_seconds(void) {
  TEST_ASSERT_EQUAL_UINT64(AP_TIME_BASE_HZ * 50u, AP_OMTI_READY_TIMEOUT);
  TEST_ASSERT_EQUAL_UINT64(0u, AP_OMTI_READY_TIMEOUT % AP_TIME_BASE_HZ);
  TEST_ASSERT_EQUAL_UINT64(50u, AP_OMTI_READY_TIMEOUT / AP_TIME_BASE_HZ);
}

/* §5.1.1, byte 1: "Bit 5 identifies the Logical Unit Number (LUN)." One drive
 * is attached here, so LUN 1 names a unit that is not fitted and the controller
 * must say so. This model served every command from the attached drive whatever
 * LUN it carried, so a Domain/OS boot was told a second Winchester was present
 * and healthy -- `DRIVE 1 PASSED.` where the hardware prints `(NOT FOUND)`. */
/* And the two figures the access time is built from, for the same reason.
 *
 * `test_the_access_time_is_a_seek_a_half_turn_and_the_transfer` in `awd_suite`
 * asserts that the model honours these constants; nothing asserts that the
 * constants are the *drive's* numbers, so a seek of thirty microseconds would
 * pass it. They are cited outside this core now -- `PROJECT_STATUS.md` accounts
 * for 40.06% of the DS5500 restore's idle time as 491 of these accesses -- and a
 * number that carries an argument needs an assertion under it.
 *
 * `008778-03` Table 6-5 -- the *part's* approval table, naming the Maxtor
 * EXT-4380 this core's image is -- by way of `ap_omti.h`'s derivation:
 * 1/3-stroke seek **30 msec**, which that section argues *is* the average seek;
 * average latency **8.33 msec**; nominal **3600** rpm. Not `002398-04` p. 6-3,
 * whose nineteen-drive summary prints 27 and which the header records as
 * deliberately not followed. */
static void test_the_access_time_is_the_drives_published_figures(void) {
  /* Thirty milliseconds, and the base represents it with no remainder. */
  TEST_ASSERT_EQUAL_UINT64(30u, AP_OMTI_AVERAGE_SEEK * 1000u / AP_TIME_BASE_HZ);
  TEST_ASSERT_EQUAL_UINT64(0u, AP_OMTI_AVERAGE_SEEK % (AP_TIME_BASE_HZ / 1000u));

  /* 3600 revolutions in a minute, said as the rotation rather than as the
   * constant's own arithmetic repeated back at it. */
  TEST_ASSERT_EQUAL_UINT64(AP_TIME_BASE_HZ * 60u,
                           AP_OMTI_ROTATION_TIME * AP_OMTI_DRIVE_RPM);
  /* And the latency is half a turn: 8,333 us to the microsecond. */
  TEST_ASSERT_EQUAL_UINT64(AP_OMTI_ROTATION_TIME, AP_OMTI_AVERAGE_LATENCY * 2u);
  TEST_ASSERT_EQUAL_UINT64(
      8333u, AP_OMTI_AVERAGE_LATENCY * 1000000u / AP_TIME_BASE_HZ);
}

static void test_test_drive_ready_fails_for_a_lun_with_no_drive(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  /* No drive attached at all: LUN 0 must fail too, which is the behaviour that
   * already worked and is asserted here so the LUN change cannot silently
   * invert it. */
  static const uint8_t lun0[6] = {0x00, 0x00, 0, 0, 0, 0};
  issue(&o, lun0);
  settle(&o);
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
  settle(&o);
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
  settle(&o);
  TEST_ASSERT_EQUAL_HEX8(0x00u, ap_omti_disk_read(&o, AP_OMTI_DISK_DATA) & 0x20u);

  ap_omti_reset(&o);
  static const uint8_t lun1[6] = {0x00, 0x20, 0, 0, 0, 0};
  issue(&o, lun1);
  settle(&o);
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

/* ## `SPECIFY`'s fourth field, from the part's own datasheets
 *
 * `[765A]` `NEC_uPD765A_Datasheet.pdf` and `[8272A]`
 * `Intel_8272A_Datasheet_Nov86.pdf`, walked whole 2026-09-09. The decode used
 * to drop byte 2 bit 0. */

/* `[765A]` p.16: "The choice of DMA or NON-DMA operation is made by the ND
 * (NON-DMA) bit. When this bit is high (ND = 1) the NON-DMA mode is selected,
 * and when ND = 0 the DMA mode is selected." */
static void test_specify_records_the_non_dma_bit(void) {
  ap_omti_t o;
  ap_omti_reset(&o);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR, AP_OMTI_DOR_NOT_RESET);
  TEST_ASSERT_FALSE(o.fdc_non_dma);

  /* SRT = 8, HUT = 3, HLT = 0x40, ND = 1. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SPECIFY);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, 0x83u);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, 0x81u);
  TEST_ASSERT_EQUAL_HEX8(0x08u, o.fdc_srt);
  TEST_ASSERT_EQUAL_HEX8(0x03u, o.fdc_hut);
  TEST_ASSERT_EQUAL_HEX8(0x40u, o.fdc_hlt);
  TEST_ASSERT_TRUE(o.fdc_non_dma);

  /* And ND = 0 selects DMA, with the same three timers. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SPECIFY);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, 0x83u);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, 0x80u);
  TEST_ASSERT_EQUAL_HEX8(0x40u, o.fdc_hlt);
  TEST_ASSERT_FALSE(o.fdc_non_dma);
}

/* `[765A]` p.7 on the Main Status Register's DB5: "This bit is set only during
 * execution phase in non-DMA mode ... It operates only during NON-DMA mode of
 * operation." So the *part's* `ND`, not the board's DOR enable, is what the bit
 * follows -- the two used to be conflated. */
static void test_the_execution_mode_bit_follows_the_parts_own_nd_bit(void) {
  ap_omti_t o;
  ap_omti_reset(&o);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR, AP_OMTI_DOR_NOT_RESET);

  /* Non-DMA selected on the chip, and the board's DMA enable *set* -- the
   * combination that told the old model "DMA" and the part "non-DMA". */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SPECIFY);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, 0x00u);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, 0x01u);
  TEST_ASSERT_TRUE(o.fdc_non_dma);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR,
                    (uint8_t)(AP_OMTI_DOR_NOT_RESET | AP_OMTI_DOR_INT_DMA));

  /* Drive it into a data phase and require the bit. */
  o.fdc_phase = AP_OMTI_PHASE_DATA_IN;
  TEST_ASSERT_EQUAL_HEX8(AP_OMTI_MSR_NDMA,
                         ap_omti_fdc_read(&o, AP_OMTI_FDC_MSR) &
                             AP_OMTI_MSR_NDMA);

  /* ND clear is DMA mode: the same data phase, the same DOR, and now the bit
   * stays down -- which is the whole difference. A second controller rather
   * than a second `SPECIFY`, because the one above is sitting in a data phase
   * and would swallow the command bytes as data. */
  ap_omti_t dma;
  ap_omti_reset(&dma);
  ap_omti_fdc_write(&dma, AP_OMTI_FDC_DOR, AP_OMTI_DOR_NOT_RESET);
  ap_omti_fdc_write(&dma, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SPECIFY);
  ap_omti_fdc_write(&dma, AP_OMTI_FDC_DATA, 0x00u);
  ap_omti_fdc_write(&dma, AP_OMTI_FDC_DATA, 0x00u);
  TEST_ASSERT_FALSE(dma.fdc_non_dma);
  ap_omti_fdc_write(&dma, AP_OMTI_FDC_DOR,
                    (uint8_t)(AP_OMTI_DOR_NOT_RESET | AP_OMTI_DOR_INT_DMA));
  dma.fdc_phase = AP_OMTI_PHASE_DATA_IN;
  TEST_ASSERT_EQUAL_HEX8(0u, ap_omti_fdc_read(&dma, AP_OMTI_FDC_MSR) &
                                 AP_OMTI_MSR_NDMA);
}

/* `[765A]` p.3, of the RST pin: "**Does not effect SRT, HUT or HLT in Specify
 * command.**" `[8272A]` Table 1 says the same in different words -- "does not
 * clear the last specify command" -- and `[765AB]` p.2 a third time. The
 * part's *own* datasheet, `[765]`, is **silent**: its RST row stops at "Resets
 * output lines to FDD to '0' (low)", which is why walking the primary alone
 * could never have established this.
 *
 * On this board the RST pin is the Digital Output Register's bit 2, and that
 * path already left the three timers alone -- but by omission, with nothing
 * saying so and nothing pinning it. This is the citation and the pin. */
static void test_a_reset_does_not_disturb_the_specify_timers(void) {
  ap_omti_t o;
  ap_omti_reset(&o);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR, AP_OMTI_DOR_NOT_RESET);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SPECIFY);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, 0xC5u); /* SRT = C, HUT = 5 */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, 0x7Fu); /* HLT = 3F, ND = 1 */
  TEST_ASSERT_EQUAL_HEX8(0x0Cu, o.fdc_srt);
  TEST_ASSERT_EQUAL_HEX8(0x05u, o.fdc_hut);
  TEST_ASSERT_EQUAL_HEX8(0x3Fu, o.fdc_hlt);
  TEST_ASSERT_TRUE(o.fdc_non_dma);
  TEST_ASSERT_TRUE(o.fdc_step_rate_set);

  /* Bit 2 low is the RST pin asserted. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR, 0u);
  TEST_ASSERT_TRUE(ap_omti_fdc_in_reset(&o));
  TEST_ASSERT_EQUAL_HEX8(0x0Cu, o.fdc_srt);
  TEST_ASSERT_EQUAL_HEX8(0x05u, o.fdc_hut);
  TEST_ASSERT_EQUAL_HEX8(0x3Fu, o.fdc_hlt);
  TEST_ASSERT_TRUE(o.fdc_step_rate_set);

  /* And they are still there on the way back out, which is what a driver that
   * resets the controller without reprogramming it depends on. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR, AP_OMTI_DOR_NOT_RESET);
  TEST_ASSERT_FALSE(ap_omti_fdc_in_reset(&o));
  TEST_ASSERT_EQUAL_HEX8(0x0Cu, o.fdc_srt);
  TEST_ASSERT_EQUAL_HEX8(0x05u, o.fdc_hut);
  TEST_ASSERT_EQUAL_HEX8(0x3Fu, o.fdc_hlt);
  TEST_ASSERT_TRUE(o.fdc_non_dma);
  TEST_ASSERT_TRUE(o.fdc_step_rate_set);
}

/* `[765AB]` Table 4, p.16, carries a **sixteenth** command the other three
 * datasheets do not: `VERSION`, `X X X 1 0 0 0 0`, whose result is one byte --
 * "90H indicates 765B, 80H indicates 765A / A-2". The Invalid row on the same
 * page gives ST0 = 80H.
 *
 * So on a uPD765 or uPD765A, `VERSION` and an invalid command are
 * **indistinguishable**, and this core's fifteen-command model answers `0x10`
 * exactly as those parts do. It is wrong only for a 765B, and `[OMTI]` §1.3.1
 * says only "NEC765 or equivalent". Asserted so the equivalence is a checked
 * property rather than a coincidence. */
static void test_the_version_opcode_answers_as_a_765a_does(void) {
  ap_omti_t o;
  ap_omti_reset(&o);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR, AP_OMTI_DOR_NOT_RESET);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, 0x10u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_fdc_phase(&o));
  TEST_ASSERT_EQUAL_HEX8(0x80u, ap_omti_fdc_read(&o, AP_OMTI_FDC_DATA));
}

/* `[765A]` p.16 and `[8272A]` p.20: "If the Track 0 signal is still low after
 * **77 Step Pulse** have been issued, the FDC sets the SE (SEEK END) and EC
 * (EQUIPMENT CHECK) flags of Status Register 0 to both 1s, and terminates the
 * command after bits 7 and 6 of Status Register 0 is set to 0 and 1
 * respectively."
 *
 * The interest is that 77 is smaller than this drive: `[S3K]` Table 7-7 gives
 * it 80 cylinders. So the part and the mechanism disagree, and a `RECALIBRATE`
 * from the outer two cylinders has to fail. */
static void test_recalibrate_gives_up_after_seventy_seven_step_pulses(void) {
  ap_omti_t o;
  ap_omti_reset(&o);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR, AP_OMTI_DOR_NOT_RESET);

  /* From cylinder 79, which only this drive's geometry makes reachable. */
  o.fdc_cylinder[0] = 79u;
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, AP_OMTI_FDC_RECALIBRATE);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, 0x00u);
  ap_omti_advance(&o, o.fdc_seek_at[0]);

  /* 77 pulses from 79 leaves the head on 2, not on 0. */
  TEST_ASSERT_EQUAL_UINT8(2u, o.fdc_cylinder[0]);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SENSE_INTERRUPT);
  TEST_ASSERT_EQUAL_HEX8((uint8_t)(AP_OMTI_ST0_IC_ABRUPT |
                                   AP_OMTI_ST0_SEEK_END |
                                   AP_OMTI_ST0_EQUIPMENT),
                         ap_omti_fdc_read(&o, AP_OMTI_FDC_DATA));
  TEST_ASSERT_EQUAL_UINT8(2u, ap_omti_fdc_read(&o, AP_OMTI_FDC_DATA));

  /* And the driver's answer is to recalibrate again, which now succeeds --
   * the two-`RECALIBRATE` idiom the 80-cylinder drives needed. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, AP_OMTI_FDC_RECALIBRATE);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, 0x00u);
  ap_omti_advance(&o, o.fdc_seek_at[0]);
  TEST_ASSERT_EQUAL_UINT8(0u, o.fdc_cylinder[0]);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SENSE_INTERRUPT);
  TEST_ASSERT_EQUAL_HEX8((uint8_t)(AP_OMTI_ST0_IC_NORMAL |
                                   AP_OMTI_ST0_SEEK_END),
                         ap_omti_fdc_read(&o, AP_OMTI_FDC_DATA));
}

/* The boundary: 77 pulses is exactly enough from cylinder 77. */
static void test_recalibrate_from_cylinder_seventy_seven_still_reaches_zero(
    void) {
  ap_omti_t o;
  ap_omti_reset(&o);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR, AP_OMTI_DOR_NOT_RESET);
  o.fdc_cylinder[0] = (uint8_t)AP_OMTI_FDC_RECALIBRATE_STEPS;
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, AP_OMTI_FDC_RECALIBRATE);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, 0x00u);
  ap_omti_advance(&o, o.fdc_seek_at[0]);
  TEST_ASSERT_EQUAL_UINT8(0u, o.fdc_cylinder[0]);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SENSE_INTERRUPT);
  TEST_ASSERT_EQUAL_HEX8((uint8_t)(AP_OMTI_ST0_IC_NORMAL |
                                   AP_OMTI_ST0_SEEK_END),
                         ap_omti_fdc_read(&o, AP_OMTI_FDC_DATA));
}

/* **A reset raises a ready-change interrupt, because `RDY` is tied asserted on
 * this board.** `[765A]` p.3: "If RDY pin is held high during Reset, FDC will
 * generate interrupt 1-25 ms later. To clear this interrupt use Sense Interrupt
 * Status command."; `[765AB]` p.2 gives the delay as "within 1.024 ms", which
 * is what this core uses and why is in `AP_OMTI_FDC_RESET_INTERRUPT`.
 *
 * That the clause applies at all was the open question until 2026-09-09: the
 * PC/AT 34-pin floppy interface carries no READY line, so pin 35 is tied, and
 * it must be tied *asserted* or every read and write would terminate `NR`. */
static void test_leaving_reset_raises_the_ready_change_interrupt(void) {
  ap_omti_t o;
  ap_omti_reset(&o);
  ap_omti_advance(&o, 1u);

  /* Held in reset, nothing is owed. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR, 0u);
  ap_omti_advance(&o, AP_OMTI_FDC_RESET_INTERRUPT * 4u);
  TEST_ASSERT_FALSE(o.fdc_seek_done[0]);

  /* Out of reset, and the interrupt is not immediate. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR, AP_OMTI_DOR_NOT_RESET);
  const ap_time_t armed = o.now;
  ap_omti_advance(&o, armed + AP_OMTI_FDC_RESET_INTERRUPT - 1u);
  TEST_ASSERT_FALSE(o.fdc_seek_done[0]);

  ap_omti_advance(&o, armed + AP_OMTI_FDC_RESET_INTERRUPT);
  TEST_ASSERT_TRUE(o.fdc_seek_done[0]);

  /* Table 5's cause: `SE = 0` with the interrupt code `11` -- "Ready Line
   * changed state, either polarity" -- so no `SEEK END`. */
  TEST_ASSERT_EQUAL_HEX8(AP_OMTI_ST0_IC_NOT_READY, o.fdc_seek_st0[0]);
  TEST_ASSERT_EQUAL_HEX8(0u, o.fdc_seek_st0[0] & AP_OMTI_ST0_SEEK_END);

  /* And `SENSE INTERRUPT STATUS` is what clears it. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SENSE_INTERRUPT);
  TEST_ASSERT_EQUAL_HEX8(AP_OMTI_ST0_IC_NOT_READY,
                         ap_omti_fdc_read(&o, AP_OMTI_FDC_DATA));
  (void)ap_omti_fdc_read(&o, AP_OMTI_FDC_DATA);
  TEST_ASSERT_FALSE(o.fdc_seek_done[0]);
}

/* **And it does not gate the command stream**, which a seek's interrupt does.
 * `[765]` p.16 names "a **Seek or Recalibrate** Interrupt" as what makes the
 * next command invalid, and `[765A]` Table 5 tells the two apart by `SEEK END`.
 * Gating on any pending interrupt made the first command after every reset
 * invalid, which `afd_suite`'s scan test caught at once. */
static void test_the_reset_interrupt_does_not_make_the_next_command_invalid(
    void) {
  ap_omti_t o;
  ap_omti_reset(&o);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR, AP_OMTI_DOR_NOT_RESET);
  ap_omti_advance(&o, o.now + AP_OMTI_FDC_RESET_INTERRUPT);
  TEST_ASSERT_TRUE(o.fdc_seek_done[0]);

  /* A command that is not `SENSE INTERRUPT STATUS` is still accepted. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SENSE_DRIVE);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, 0x00u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_fdc_phase(&o));
  TEST_ASSERT_NOT_EQUAL_HEX8(AP_OMTI_ST0_IC_INVALID,
                             ap_omti_fdc_read(&o, AP_OMTI_FDC_DATA));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_two_floppy_control_registers_are_not_one_register);
  RUN_TEST(test_the_boot_proms_floppy_initialisation_needs_two_registers);
  RUN_TEST(test_sense_drive_status_does_not_report_the_unit);
  RUN_TEST(test_sector_address_conversion_uses_sixteen_heads);
  RUN_TEST(test_a_programmed_step_rate_paces_the_seek);
  RUN_TEST(test_an_unprogrammed_controller_steps_at_the_drive_minimum);
  RUN_TEST(test_a_drive_that_is_never_ready_costs_the_whole_timeout);
  RUN_TEST(test_the_ready_timeout_is_fifty_seconds);
  RUN_TEST(test_the_access_time_is_the_drives_published_figures);
  RUN_TEST(test_test_drive_ready_fails_for_a_lun_with_no_drive);
  RUN_TEST(test_the_completion_byte_carries_the_commands_lun);
  RUN_TEST(test_the_measured_fixed_disk_ports_are_reproduced);
  RUN_TEST(test_the_status_bits_seven_and_six_cannot_be_cleared);
  RUN_TEST(test_selecting_the_controller_makes_it_busy);
  RUN_TEST(test_selecting_the_controller_asks_for_the_first_command_byte);
  RUN_TEST(test_a_select_part_way_through_a_command_is_ignored);
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
  RUN_TEST(test_the_buffer_cap_rule_reproduces_all_four_printed_rows);
  RUN_TEST(test_the_last_block_the_buffer_holds_is_accepted);
  RUN_TEST(test_writing_the_sector_buffer_does_not_touch_the_drive);
  RUN_TEST(test_every_command_the_esdi_set_accepts_reaches_an_implementation);
  RUN_TEST(test_a_command_outside_the_esdi_set_reports_invalid_command);
  RUN_TEST(test_the_msr_reports_non_dma_mode_and_the_motors);
  RUN_TEST(test_specify_records_the_non_dma_bit);
  RUN_TEST(test_the_execution_mode_bit_follows_the_parts_own_nd_bit);
  RUN_TEST(test_a_reset_does_not_disturb_the_specify_timers);
  RUN_TEST(test_leaving_reset_raises_the_ready_change_interrupt);
  RUN_TEST(test_the_reset_interrupt_does_not_make_the_next_command_invalid);
  RUN_TEST(test_the_version_opcode_answers_as_a_765a_does);
  RUN_TEST(test_recalibrate_gives_up_after_seventy_seven_step_pulses);
  RUN_TEST(test_recalibrate_from_cylinder_seventy_seven_still_reaches_zero);

  RUN_TEST(test_the_floppy_command_modifiers_are_read);
  RUN_TEST(test_the_floppy_drives_its_own_interrupt_and_dma_lines);
  RUN_TEST(test_a_completed_command_asks_for_an_interrupt_when_enabled);
  RUN_TEST(test_the_data_phase_asks_for_dma_only_when_dma_is_enabled);
  RUN_TEST(test_two_controllers_reset_alike_hold_identical_state);
  return UNITY_END();
}
