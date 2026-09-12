/* Western Digital WD7000-ASC SCSI host adapter, `[WD7000]` 96-000494 Rev. X3.
 *
 * The host interface, which is what `src/core/device/ap_wd7000.c` implements:
 * the four registers, the status and interrupt-status bytes, the reset and
 * diagnostic sequence, the command port and its two multi-byte sequences, the
 * mailbox arithmetic, the interrupt queue and the parameter block. */

#include "unity.h"

#include <string.h>

#include "device/ap_wd7000.h"

void setUp(void) {}
void tearDown(void) {}

/* Bring a part up past its power-on diagnostics and leave it ready. */
static void diagnosed(ap_wd7000_t *asc) {
  ap_wd7000_power_on(asc);
  ap_wd7000_advance(asc, AP_WD7000_T_LONG_DIAGNOSTIC);
}

/* Write one command-port byte and let the port turn round. */
static void command(ap_wd7000_t *asc, uint8_t value) {
  ap_wd7000_write(asc, AP_WD7000_STATUS_COMMAND, value);
  ap_wd7000_advance(asc, asc->now + AP_WD7000_T_COMMAND_PORT);
}

/* The ten bytes of Table 6-2 with the values this suite uses throughout. */
static void initialize(ap_wd7000_t *asc, uint32_t mail, unsigned out,
                       unsigned in) {
  command(asc, AP_WD7000_CMD_INITIALIZE);
  command(asc, 0x07u);                       /* the ASC's own SCSI ID */
  command(asc, 0x40u);                       /* bus on, 8.0 us */
  command(asc, 0x0Fu);                       /* bus off, 1.875 us */
  command(asc, 0x00u);                       /* reserved */
  command(asc, (uint8_t)(mail >> 16));
  command(asc, (uint8_t)(mail >> 8));
  command(asc, (uint8_t)mail);
  command(asc, (uint8_t)out);
  command(asc, (uint8_t)in);
}

/* §5.2.1: "on reset the upper nibble clears and the lower nibble reads 1s",
 * because nothing drives D3-D0 -- which is why every driver masks `F0`, and
 * why `scsi14.drvr` does. */
static void test_the_low_nibble_of_the_status_port_is_not_driven(void) {
  ap_wd7000_t asc;
  ap_wd7000_power_on(&asc);
  TEST_ASSERT_EQUAL_HEX8(0x0Fu, ap_wd7000_read(&asc, AP_WD7000_STATUS_COMMAND));

  ap_wd7000_advance(&asc, AP_WD7000_T_LONG_DIAGNOSTIC);
  TEST_ASSERT_EQUAL_HEX8(0x4Fu, ap_wd7000_read(&asc, AP_WD7000_STATUS_COMMAND));
  TEST_ASSERT_EQUAL_HEX8(0x40u, ap_wd7000_read(&asc, AP_WD7000_STATUS_COMMAND) &
                                    0xF0u);
}

/* §5.1.1 and Figure B-1: the host waits for `4F`/`40` after the reset and
 * `5F`/`50` after the initialization. Those are two of the four values
 * `scsi14.drvr` accepts, and the other two are the same pair masked. */
static void test_the_four_values_the_apollo_driver_accepts(void) {
  ap_wd7000_t asc;
  diagnosed(&asc);
  const uint8_t after_reset = ap_wd7000_read(&asc, AP_WD7000_STATUS_COMMAND);
  initialize(&asc, 0x010000u, 4u, 4u);
  const uint8_t after_init = ap_wd7000_read(&asc, AP_WD7000_STATUS_COMMAND);

  TEST_ASSERT_EQUAL_HEX8(0x4Fu, after_reset);
  TEST_ASSERT_EQUAL_HEX8(0x5Fu, after_init);
  TEST_ASSERT_EQUAL_HEX8(0x40u, after_reset & 0xF0u);
  TEST_ASSERT_EQUAL_HEX8(0x50u, after_init & 0xF0u);
}

/* §5.1.1 and §6.2.14.1 say a rejected byte posts `60`; §5.2.1 says `70`. Both
 * are right and the document does not contradict itself: `60` is
 * READY | REJECTED and `70` adds INITIALIZED, and the two pages that say `60`
 * are describing a rejected *initialization* byte, which arrives before the
 * flag is set. The bit definitions produce both with no special case. */
static void test_sixty_and_seventy_are_the_same_rejection(void) {
  ap_wd7000_t asc;
  diagnosed(&asc);

  /* Before initialization, an opcode from the reserved band. */
  command(&asc, 0x40u);
  TEST_ASSERT_EQUAL_HEX8(0x6Fu, ap_wd7000_read(&asc, AP_WD7000_STATUS_COMMAND));
  TEST_ASSERT_EQUAL_HEX8(0x60u, ap_wd7000_read(&asc, AP_WD7000_STATUS_COMMAND) &
                                    0xF0u);

  initialize(&asc, 0x010000u, 1u, 1u);
  command(&asc, 0x40u);
  TEST_ASSERT_EQUAL_HEX8(0x7Fu, ap_wd7000_read(&asc, AP_WD7000_STATUS_COMMAND));
  TEST_ASSERT_EQUAL_HEX8(0x70u, ap_wd7000_read(&asc, AP_WD7000_STATUS_COMMAND) &
                                    0xF0u);
}

/* §6.2.14: "a power-up reset always runs the long walking-1s diagnostic and a
 * warm reset the short one". §5.1.1 gives the long one as about 2 seconds and
 * §6.2.14.1 the short as under 250 ms. */
static void test_the_first_diagnostic_is_the_long_one_and_later_ones_are_not(void) {
  ap_wd7000_t asc;
  ap_wd7000_power_on(&asc);
  ap_wd7000_advance(&asc, AP_WD7000_T_SHORT_DIAGNOSTIC);
  TEST_ASSERT_FALSE(asc.ready);
  ap_wd7000_advance(&asc, AP_WD7000_T_LONG_DIAGNOSTIC);
  TEST_ASSERT_TRUE(asc.ready);

  const ap_time_t warm = asc.now;
  ap_wd7000_reset(&asc);
  TEST_ASSERT_FALSE(asc.ready);
  ap_wd7000_advance(&asc, warm + AP_WD7000_T_SHORT_DIAGNOSTIC);
  TEST_ASSERT_TRUE(asc.ready);
}

/* Table 5-3: `00` is "power-on condition, no diagnostics executed" and `01` is
 * "no diagnostic error". §5.1.1: no interrupt is raised for either, so the
 * host polls the status port and then reads this register. */
static void test_the_diagnostic_code_appears_without_an_interrupt(void) {
  ap_wd7000_t asc;
  ap_wd7000_power_on(&asc);
  TEST_ASSERT_EQUAL_HEX8(AP_WD7000_DIAG_POWER_ON,
                         ap_wd7000_read(&asc, AP_WD7000_INTSTAT_ACK));
  TEST_ASSERT_FALSE(asc.interrupt);

  ap_wd7000_advance(&asc, AP_WD7000_T_LONG_DIAGNOSTIC);
  TEST_ASSERT_EQUAL_HEX8(AP_WD7000_DIAG_OK,
                         ap_wd7000_read(&asc, AP_WD7000_INTSTAT_ACK));
  TEST_ASSERT_FALSE(asc.interrupt);
  TEST_ASSERT_FALSE(ap_wd7000_irq(&asc));
}

/* §4.9.1 and Appendix A.9 port `27`: the LED is lit while the diagnostics run
 * and "1 = Test Failed", so a pass turns it off. */
static void test_the_diagnostic_led_goes_out_on_a_pass(void) {
  ap_wd7000_t asc;
  ap_wd7000_power_on(&asc);
  TEST_ASSERT_TRUE(asc.led);
  ap_wd7000_advance(&asc, AP_WD7000_T_LONG_DIAGNOSTIC);
  TEST_ASSERT_FALSE(asc.led);
}

/* §5.2.5.2: "both the ASC reset and the SCSI hardware resets can be asserted
 * simultaneously by writing a `03H` to this port followed by a `00H`" -- which
 * is `scsi14.drvr`'s first two writes to `050002`, byte for byte. */
static void test_the_drivers_reset_sequence_resets_the_part(void) {
  ap_wd7000_t asc;
  diagnosed(&asc);
  initialize(&asc, 0x010000u, 4u, 4u);
  TEST_ASSERT_TRUE(asc.initialized);

  ap_wd7000_write(&asc, AP_WD7000_CONTROL, 0x03u);
  /* The rising edge is dated at the *next* advance, because a register write
   * has no instant of its own -- so the pulse is measured from here, and a
   * test that advanced only once would be measuring zero. */
  ap_wd7000_advance(&asc, asc.now);
  ap_wd7000_advance(&asc, asc.now + AP_WD7000_T_RESET_MIN);
  TEST_ASSERT_EQUAL_HEX8(0x0Fu, ap_wd7000_read(&asc, AP_WD7000_STATUS_COMMAND));
  ap_wd7000_write(&asc, AP_WD7000_CONTROL, 0x00u);

  TEST_ASSERT_FALSE(asc.initialized);
  ap_wd7000_advance(&asc, asc.now + AP_WD7000_T_SHORT_DIAGNOSTIC);
  TEST_ASSERT_EQUAL_HEX8(0x4Fu, ap_wd7000_read(&asc, AP_WD7000_STATUS_COMMAND));
}

/* §5.1.1's table gives a minimum RESET pulse width of 25.0 us, so a narrower
 * pulse is not a reset -- the same rule `ap_sc499` enforces for `[SC499]`
 * §1.12's hold. */
static void test_a_reset_pulse_shorter_than_the_minimum_does_nothing(void) {
  ap_wd7000_t asc;
  diagnosed(&asc);
  initialize(&asc, 0x010000u, 4u, 4u);

  ap_wd7000_write(&asc, AP_WD7000_CONTROL, AP_WD7000_CTL_ASC_RESET);
  ap_wd7000_advance(&asc, asc.now);
  ap_wd7000_advance(&asc, asc.now + AP_WD7000_T_RESET_MIN - 1u);
  ap_wd7000_write(&asc, AP_WD7000_CONTROL, 0x00u);

  TEST_ASSERT_TRUE(asc.initialized);
  TEST_ASSERT_FALSE(asc.diagnosing);
}

/* §6.1.2: the ten bytes, and what each establishes. Table A-8's arithmetic
 * then places every mailbox: all the outgoing boxes first, then all the
 * incoming ones, and the counts are independent. */
static void test_the_ten_initialization_bytes_place_every_mailbox(void) {
  ap_wd7000_t asc;
  diagnosed(&asc);
  initialize(&asc, 0x0123456u, 4u, 2u);

  TEST_ASSERT_TRUE(asc.initialized);
  TEST_ASSERT_EQUAL_UINT8(0x07u, asc.scsi_id);
  TEST_ASSERT_EQUAL_UINT8(0x40u, asc.bus_on);
  TEST_ASSERT_EQUAL_UINT8(0x0Fu, asc.bus_off);
  TEST_ASSERT_EQUAL_HEX32(0x0123456u, asc.mail_base);

  TEST_ASSERT_EQUAL_HEX32(0x0123456u, ap_wd7000_ogmb_address(&asc, 0u));
  TEST_ASSERT_EQUAL_HEX32(0x0123456u + 12u, ap_wd7000_ogmb_address(&asc, 3u));
  TEST_ASSERT_EQUAL_HEX32(0u, ap_wd7000_ogmb_address(&asc, 4u));
  /* The incoming boxes start past all four outgoing ones. */
  TEST_ASSERT_EQUAL_HEX32(0x0123456u + 16u, ap_wd7000_icmb_address(&asc, 0u));
  TEST_ASSERT_EQUAL_HEX32(0x0123456u + 20u, ap_wd7000_icmb_address(&asc, 1u));
  TEST_ASSERT_EQUAL_HEX32(0u, ap_wd7000_icmb_address(&asc, 2u));
}

/* Table 6-2 byte 08/09: "max. 64 (0,1 = 1)" -- zero and one both mean one box. */
static void test_a_mailbox_count_of_zero_means_one(void) {
  ap_wd7000_t asc;
  diagnosed(&asc);
  initialize(&asc, 0x010000u, 0u, 0u);
  TEST_ASSERT_EQUAL_UINT(1u, asc.ogmb_count);
  TEST_ASSERT_EQUAL_UINT(1u, asc.icmb_count);
  TEST_ASSERT_EQUAL_HEX32(0x010004u, ap_wd7000_icmb_address(&asc, 0u));
}

/* Bytes 05-07 are MSB first. Table A-4 labels them the other way round; Table
 * 6-2, §5.3's mailbox layout and Table A-8 all say MSB first, and the mailbox
 * pointers in the same structure are MSB-first too. */
static void test_the_mail_block_address_is_msb_first(void) {
  ap_wd7000_t asc;
  diagnosed(&asc);
  initialize(&asc, 0x0ABCDEu, 1u, 1u);
  TEST_ASSERT_EQUAL_HEX32(0x0ABCDEu, asc.mail_base);
  TEST_ASSERT_NOT_EQUAL(0x0DEBCAu, asc.mail_base);
}

/* §6.1.2: "The initialization sequence can only be initiated following an ASC
 * reset ... At all other times, the command will be rejected", and §5.2.1.4
 * adds that "any later initialization is rejected unless preceded by an ASC
 * reset". */
static void test_a_second_initialization_is_rejected_without_a_reset(void) {
  ap_wd7000_t asc;
  diagnosed(&asc);
  initialize(&asc, 0x010000u, 1u, 1u);

  command(&asc, AP_WD7000_CMD_INITIALIZE);
  TEST_ASSERT_TRUE(asc.rejected);
  TEST_ASSERT_EQUAL_INT(AP_WD7000_SEQ_NONE, asc.sequence);
}

/* §5.2.1.4: before initialization, "any command other than Initialization sets
 * D5". §6.1.1 exempts the no-operation, which exists to be a liveness check. */
static void test_before_initialization_only_nop_and_init_are_accepted(void) {
  ap_wd7000_t asc;
  diagnosed(&asc);

  command(&asc, AP_WD7000_CMD_NOP);
  TEST_ASSERT_FALSE(asc.rejected);

  const uint8_t refused[] = {AP_WD7000_CMD_DISABLE_UNSOLICITED,
                             AP_WD7000_CMD_ENABLE_UNSOLICITED,
                             AP_WD7000_CMD_INT_ON_FREE_OGMB,
                             AP_WD7000_CMD_SCSI_SOFT_RESET,
                             AP_WD7000_CMD_SCSI_HARD_RESET_ACK,
                             AP_WD7000_CMD_START_OGMB,
                             AP_WD7000_CMD_SCAN};
  for (unsigned i = 0; i < sizeof refused / sizeof refused[0]; ++i) {
    command(&asc, refused[i]);
    TEST_ASSERT_TRUE(asc.rejected);
  }
}

/* Table 6-1: `07`-`7F` is a reserved band, and §5.2.1.3 calls a byte outside
 * the set "illegal command or parameter". */
static void test_the_reserved_opcode_band_is_rejected(void) {
  ap_wd7000_t asc;
  diagnosed(&asc);
  initialize(&asc, 0x010000u, 1u, 1u);

  for (unsigned opcode = 0x07u; opcode <= 0x7Fu; ++opcode) {
    command(&asc, (uint8_t)opcode);
    TEST_ASSERT_TRUE(asc.rejected);
  }
  /* And the two bands above it are not reserved: `80`-`BF` starts one mailbox
   * and `C0`-`FF` scans them. */
  command(&asc, 0x80u);
  TEST_ASSERT_FALSE(asc.rejected);
  command(&asc, 0xFFu);
  TEST_ASSERT_FALSE(asc.rejected);
}

/* §5.2.1.2 and §4.1: writing the command port clears COMMAND PORT READY, and
 * the ASC sets it again about 70 us later -- which is the guarantee §4.1
 * states as "the host is told within 70 us whether the byte was accepted". */
static void test_the_command_port_turns_round_in_seventy_microseconds(void) {
  ap_wd7000_t asc;
  diagnosed(&asc);

  ap_wd7000_write(&asc, AP_WD7000_STATUS_COMMAND, AP_WD7000_CMD_NOP);
  TEST_ASSERT_FALSE(asc.ready);
  ap_wd7000_advance(&asc, asc.now + AP_WD7000_T_COMMAND_PORT - 1u);
  TEST_ASSERT_FALSE(asc.ready);
  ap_wd7000_advance(&asc, asc.now + 1u);
  TEST_ASSERT_TRUE(asc.ready);
}

/* §6.1.1: the no-operation "toggles status bit 6 only", so it must not clear a
 * rejection the host has not read yet -- the bit that says so is D5 and the
 * command does not touch it. **Corrected reading**: it does clear it, because
 * D5 reports the *last* byte and the NOP is a byte. Asserted so the choice is
 * visible rather than incidental. */
static void test_a_no_operation_clears_the_previous_rejection(void) {
  ap_wd7000_t asc;
  diagnosed(&asc);
  command(&asc, 0x40u);
  TEST_ASSERT_TRUE(asc.rejected);
  command(&asc, AP_WD7000_CMD_NOP);
  TEST_ASSERT_FALSE(asc.rejected);
}

/* §6.1.3 and §6.1.4 are "the same switch as parameter byte 22 bit 0"
 * (§6.2.11.8), and §6.1.5 the same as bit 1. */
static void test_the_command_port_switches_are_the_parameter_blocks(void) {
  ap_wd7000_t asc;
  diagnosed(&asc);
  initialize(&asc, 0x010000u, 1u, 1u);

  command(&asc, AP_WD7000_CMD_ENABLE_UNSOLICITED);
  TEST_ASSERT_EQUAL_HEX8(AP_WD7000_USER_FLAG_UNSOLICITED,
                         asc.parameters[AP_WD7000_PARAM_USER_FLAGS] &
                             AP_WD7000_USER_FLAG_UNSOLICITED);
  command(&asc, AP_WD7000_CMD_DISABLE_UNSOLICITED);
  TEST_ASSERT_EQUAL_HEX8(0u, asc.parameters[AP_WD7000_PARAM_USER_FLAGS] &
                                 AP_WD7000_USER_FLAG_UNSOLICITED);

  command(&asc, AP_WD7000_CMD_INT_ON_FREE_OGMB);
  TEST_ASSERT_TRUE(asc.interrupt_on_free_ogmb);
  TEST_ASSERT_EQUAL_HEX8(AP_WD7000_USER_FLAG_FREE_OGMB,
                         asc.parameters[AP_WD7000_PARAM_USER_FLAGS] &
                             AP_WD7000_USER_FLAG_FREE_OGMB);
}

/* Table 6-3: the soft reset is two bytes, the second carrying the target, a
 * zero, the `R.T.` bit -- 0 an ABORT message, 1 a BUS DEVICE RESET -- and the
 * LUN. */
static void test_the_soft_reset_takes_a_second_byte(void) {
  ap_wd7000_t asc;
  diagnosed(&asc);
  initialize(&asc, 0x010000u, 1u, 1u);

  command(&asc, AP_WD7000_CMD_SCSI_SOFT_RESET);
  TEST_ASSERT_EQUAL_INT(AP_WD7000_SEQ_SOFT_RESET, asc.sequence);
  TEST_ASSERT_FALSE(asc.rejected);

  command(&asc, (uint8_t)(0x60u | AP_WD7000_SOFT_RESET_DEVICE_RESET | 0x02u));
  TEST_ASSERT_EQUAL_INT(AP_WD7000_SEQ_NONE, asc.sequence);
  TEST_ASSERT_EQUAL_HEX8(0x6Au, asc.parameter[1]);
}

/* §6.1.7: the acknowledgement puts the ASC "in a pseudo idle loop accepting
 * only reset/abort commands until a second `06` releases it". */
static void test_the_hard_reset_acknowledge_toggles_the_pseudo_idle_loop(void) {
  ap_wd7000_t asc;
  diagnosed(&asc);
  initialize(&asc, 0x010000u, 1u, 1u);

  command(&asc, AP_WD7000_CMD_SCSI_HARD_RESET_ACK);
  TEST_ASSERT_TRUE(asc.pseudo_idle);
  command(&asc, AP_WD7000_CMD_ENABLE_UNSOLICITED);
  TEST_ASSERT_TRUE(asc.rejected);
  command(&asc, AP_WD7000_CMD_SCSI_SOFT_RESET);
  TEST_ASSERT_FALSE(asc.rejected);
  command(&asc, 0x00u);

  command(&asc, AP_WD7000_CMD_SCSI_HARD_RESET_ACK);
  TEST_ASSERT_FALSE(asc.pseudo_idle);
  command(&asc, AP_WD7000_CMD_ENABLE_UNSOLICITED);
  TEST_ASSERT_FALSE(asc.rejected);
}

/* §5.2.4: the strobe at address 1 clears the hardware interrupt and frees an
 * ICMB, and "the ASC will not raise the next interrupt until the previous one
 * is acknowledged", so only the head of the queue is ever visible. */
static void test_only_one_queued_interrupt_is_visible_at_a_time(void) {
  ap_wd7000_t asc;
  diagnosed(&asc);
  initialize(&asc, 0x010000u, 1u, 1u);

  TEST_ASSERT_TRUE(ap_wd7000_post_interrupt(&asc, AP_WD7000_INT_ICMB_SERVICE));
  TEST_ASSERT_TRUE(ap_wd7000_post_interrupt(&asc,
                                            AP_WD7000_INT_ICMB_SERVICE | 1u));
  TEST_ASSERT_TRUE(asc.interrupt);
  TEST_ASSERT_EQUAL_HEX8(AP_WD7000_INT_ICMB_SERVICE,
                         ap_wd7000_read(&asc, AP_WD7000_INTSTAT_ACK));

  ap_wd7000_write(&asc, AP_WD7000_INTSTAT_ACK, 0u);
  TEST_ASSERT_TRUE(asc.interrupt);
  TEST_ASSERT_EQUAL_HEX8(AP_WD7000_INT_ICMB_SERVICE | 1u,
                         ap_wd7000_read(&asc, AP_WD7000_INTSTAT_ACK));

  ap_wd7000_write(&asc, AP_WD7000_INTSTAT_ACK, 0u);
  TEST_ASSERT_FALSE(asc.interrupt);
}

/* Table A-8's note: "up to 32 IRQs can be queued internally to the ASC". Past
 * that the queue is full, which Figure B-7 treats by marking the spot. */
static void test_the_interrupt_queue_is_thirty_two_deep(void) {
  ap_wd7000_t asc;
  diagnosed(&asc);
  for (unsigned i = 0; i < AP_WD7000_IRQ_QUEUE; ++i) {
    TEST_ASSERT_TRUE(ap_wd7000_post_interrupt(&asc, (uint8_t)(0xC0u | i)));
  }
  TEST_ASSERT_FALSE(ap_wd7000_post_interrupt(&asc, 0xC0u));
  TEST_ASSERT_EQUAL_UINT(AP_WD7000_IRQ_QUEUE, asc.queue_count);
}

/* §5.2.5.4 and §5.2.5.3: both lines are tri-stated unless their enable bit is
 * set, "so that several cards can share a channel". §5.2.1.1 exempts the
 * status byte's image flag, which is there for a host that must poll. */
static void test_the_two_lines_are_driven_only_when_enabled(void) {
  ap_wd7000_t asc;
  diagnosed(&asc);
  TEST_ASSERT_TRUE(ap_wd7000_post_interrupt(&asc, AP_WD7000_INT_ICMB_SERVICE));

  TEST_ASSERT_FALSE(ap_wd7000_irq(&asc));
  TEST_ASSERT_FALSE(ap_wd7000_drq_driven(&asc));
  TEST_ASSERT_EQUAL_HEX8(AP_WD7000_ST_INTERRUPT,
                         ap_wd7000_read(&asc, AP_WD7000_STATUS_COMMAND) &
                             AP_WD7000_ST_INTERRUPT);

  ap_wd7000_write(&asc, AP_WD7000_CONTROL,
                  AP_WD7000_CTL_IRQ_ENABLE | AP_WD7000_CTL_DMA_ENABLE);
  TEST_ASSERT_TRUE(ap_wd7000_irq(&asc));
  TEST_ASSERT_TRUE(ap_wd7000_drq_driven(&asc));
}

/* Appendix A.8 prints a default for every one of the 26 parameter bytes. The
 * sync rates default to `40`, which is asynchronous, and `45H` is only the
 * recommendation. */
static void test_every_parameter_default_is_the_one_the_appendix_prints(void) {
  ap_wd7000_t asc;
  diagnosed(&asc);

  for (unsigned i = 0; i < AP_WD7000_PARAM_SYNC_COUNT; ++i) {
    TEST_ASSERT_EQUAL_HEX8(AP_WD7000_SYNC_DEFAULT,
                           asc.parameters[AP_WD7000_PARAM_SYNC_FIRST + i]);
  }
  TEST_ASSERT_EQUAL_HEX8(0x00u, asc.parameters[16]);
  TEST_ASSERT_EQUAL_HEX8(0x0Cu, asc.parameters[AP_WD7000_PARAM_SBIC_CONTROL]);
  TEST_ASSERT_EQUAL_HEX8(0x19u, asc.parameters[AP_WD7000_PARAM_TIMEOUT]);
  TEST_ASSERT_EQUAL_HEX8(0x00u, asc.parameters[19]);
  TEST_ASSERT_EQUAL_HEX8(0x00u, asc.parameters[20]);
  TEST_ASSERT_EQUAL_HEX8(0x80u, asc.parameters[AP_WD7000_PARAM_SOURCE_ID]);
  TEST_ASSERT_EQUAL_HEX8(0x00u, asc.parameters[AP_WD7000_PARAM_USER_FLAGS]);
  TEST_ASSERT_EQUAL_HEX8(0x00u,
                         asc.parameters[AP_WD7000_PARAM_UNSOLICITED_MASK]);
  TEST_ASSERT_EQUAL_HEX8(0x02u, asc.parameters[AP_WD7000_PARAM_PARITY_RETRIES]);
  TEST_ASSERT_EQUAL_HEX8(0x00u, asc.parameters[25]);
  /* The recommendation is not the default, and the difference is bit 0 of the
   * transfer period and the buffer offset -- asynchronous against 4.0 MB/s. */
  TEST_ASSERT_NOT_EQUAL(AP_WD7000_SYNC_DEFAULT, AP_WD7000_SYNC_RECOMMENDED);
}

/* §6.2.11.4: "SCSI Timeout = timeout(ms) x SBIC clock(MHz) / 80, rounded up",
 * default 25 decimal, "about 250 mS" -- and the SBIC clock is the 8 MHz §4.1.1
 * divides the 4 MHz LCPU clock from. The arithmetic closes. */
static void test_the_default_timeout_is_two_hundred_and_fifty_milliseconds(void) {
  const unsigned sbic_mhz = 8u;
  const unsigned milliseconds = 250u;
  TEST_ASSERT_EQUAL_UINT(AP_WD7000_TIMEOUT_DEFAULT,
                         milliseconds * sbic_mhz / 80u);
  TEST_ASSERT_EQUAL_UINT(25u, AP_WD7000_TIMEOUT_DEFAULT);
}

/* Table A-1 marks BASE+2 and BASE+3 reserved on read, and nothing drives them.
 * BASE+3 is reserved both ways, so a write to it must change nothing. */
static void test_the_reserved_addresses_read_as_nothing_and_absorb_writes(void) {
  ap_wd7000_t asc;
  diagnosed(&asc);
  TEST_ASSERT_EQUAL_HEX8(0xFFu, ap_wd7000_read(&asc, AP_WD7000_CONTROL));
  TEST_ASSERT_EQUAL_HEX8(0xFFu, ap_wd7000_read(&asc, AP_WD7000_RESERVED));

  ap_wd7000_t before = asc;
  ap_wd7000_write(&asc, AP_WD7000_RESERVED, 0xA5u);
  TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &asc, sizeof asc));
}

/* Table A-2: "On power-up Reset or RESET DRV, all registers are cleared to all
 * zeros", and D7-D4 are "reserved, not used" -- so the upper nibble is stored
 * as written and drives nothing. */
static void test_the_control_registers_upper_nibble_drives_nothing(void) {
  ap_wd7000_t asc;
  diagnosed(&asc);
  ap_wd7000_write(&asc, AP_WD7000_CONTROL, AP_WD7000_CTL_UNUSED);
  TEST_ASSERT_FALSE(ap_wd7000_irq(&asc));
  TEST_ASSERT_FALSE(ap_wd7000_drq_driven(&asc));
  TEST_ASSERT_FALSE(asc.in_reset);
  TEST_ASSERT_FALSE(asc.scsi_reset);
}

/* §5.2.5.2: a SCSI port reset "resets the SBIC but not the LCPU", deliberately,
 * so the host command queue survives -- which is the opposite of bit 0. */
static void test_the_scsi_reset_does_not_reset_the_controller(void) {
  ap_wd7000_t asc;
  diagnosed(&asc);
  initialize(&asc, 0x010000u, 4u, 4u);

  ap_wd7000_write(&asc, AP_WD7000_CONTROL, AP_WD7000_CTL_SCSI_RESET);
  TEST_ASSERT_TRUE(asc.scsi_reset);
  TEST_ASSERT_TRUE(asc.initialized);
  TEST_ASSERT_FALSE(asc.diagnosing);
}

/* A cycle-stepped core advances a device many times at the same instant, and
 * never backwards. */
static void test_advancing_is_idempotent_and_refuses_to_go_backwards(void) {
  ap_wd7000_t asc;
  diagnosed(&asc);
  const ap_time_t now = asc.now;
  ap_wd7000_t before = asc;

  ap_wd7000_advance(&asc, now);
  ap_wd7000_advance(&asc, now - 1u);
  ap_wd7000_advance(&asc, 0u);
  TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &asc, sizeof asc));
}

/* Two parts brought up the same way hold identical state, which is what makes
 * the struct hashable for the identity harness. */
static void test_two_controllers_brought_up_alike_hold_identical_state(void) {
  ap_wd7000_t a;
  ap_wd7000_t b;
  diagnosed(&a);
  diagnosed(&b);
  initialize(&a, 0x020000u, 8u, 8u);
  initialize(&b, 0x020000u, 8u, 8u);
  TEST_ASSERT_EQUAL_INT(0, memcmp(&a, &b, sizeof a));
}

/* The timing constants land exactly on the base, so none of them is rounded on
 * top of being a documented bound. */
static void test_the_timing_constants_are_exact_in_base_units(void) {
  TEST_ASSERT_EQUAL_UINT64((uint64_t)AP_TIME_BASE_HZ * 70u / 1000000u,
                           AP_WD7000_T_COMMAND_PORT);
  TEST_ASSERT_EQUAL_UINT64((uint64_t)AP_TIME_BASE_HZ * 25u / 1000000u,
                           AP_WD7000_T_RESET_MIN);
  TEST_ASSERT_EQUAL_UINT64((uint64_t)AP_TIME_BASE_HZ / 4u,
                           AP_WD7000_T_SHORT_DIAGNOSTIC);
  TEST_ASSERT_EQUAL_UINT64((uint64_t)AP_TIME_BASE_HZ * 2u,
                           AP_WD7000_T_LONG_DIAGNOSTIC);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_low_nibble_of_the_status_port_is_not_driven);
  RUN_TEST(test_the_four_values_the_apollo_driver_accepts);
  RUN_TEST(test_sixty_and_seventy_are_the_same_rejection);
  RUN_TEST(test_the_first_diagnostic_is_the_long_one_and_later_ones_are_not);
  RUN_TEST(test_the_diagnostic_code_appears_without_an_interrupt);
  RUN_TEST(test_the_diagnostic_led_goes_out_on_a_pass);
  RUN_TEST(test_the_drivers_reset_sequence_resets_the_part);
  RUN_TEST(test_a_reset_pulse_shorter_than_the_minimum_does_nothing);
  RUN_TEST(test_the_ten_initialization_bytes_place_every_mailbox);
  RUN_TEST(test_a_mailbox_count_of_zero_means_one);
  RUN_TEST(test_the_mail_block_address_is_msb_first);
  RUN_TEST(test_a_second_initialization_is_rejected_without_a_reset);
  RUN_TEST(test_before_initialization_only_nop_and_init_are_accepted);
  RUN_TEST(test_the_reserved_opcode_band_is_rejected);
  RUN_TEST(test_the_command_port_turns_round_in_seventy_microseconds);
  RUN_TEST(test_a_no_operation_clears_the_previous_rejection);
  RUN_TEST(test_the_command_port_switches_are_the_parameter_blocks);
  RUN_TEST(test_the_soft_reset_takes_a_second_byte);
  RUN_TEST(test_the_hard_reset_acknowledge_toggles_the_pseudo_idle_loop);
  RUN_TEST(test_only_one_queued_interrupt_is_visible_at_a_time);
  RUN_TEST(test_the_interrupt_queue_is_thirty_two_deep);
  RUN_TEST(test_the_two_lines_are_driven_only_when_enabled);
  RUN_TEST(test_every_parameter_default_is_the_one_the_appendix_prints);
  RUN_TEST(test_the_default_timeout_is_two_hundred_and_fifty_milliseconds);
  RUN_TEST(test_the_reserved_addresses_read_as_nothing_and_absorb_writes);
  RUN_TEST(test_the_control_registers_upper_nibble_drives_nothing);
  RUN_TEST(test_the_scsi_reset_does_not_reset_the_controller);
  RUN_TEST(test_advancing_is_idempotent_and_refuses_to_go_backwards);
  RUN_TEST(test_two_controllers_brought_up_alike_hold_identical_state);
  RUN_TEST(test_the_timing_constants_are_exact_in_base_units);
  return UNITY_END();
}
