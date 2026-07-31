/* Intel 8237A, `[8237]` order number 231466. */

#include "unity.h"

#include <string.h>

#include "device/ap_i8237.h"

void setUp(void) {}
void tearDown(void) {}

static void test_reset_masks_every_channel(void) {
  ap_i8237_t dma;
  ap_i8237_reset(&dma);

  /* "The entire register is also set by a Reset. This disables all DMA requests
   * until a clear Mask register instruction allows them to occur."
   *
   * And this is the value the oracle's own controller was measured holding:
   * register 15 reads `0F` out of reset, which is how the placement was
   * identified in the first place (`FINDINGS.md` C13). */
  TEST_ASSERT_EQUAL_HEX8(0x0F, dma.mask);

  ap_i8237_set_request_pin(&dma, 2, true);
  TEST_ASSERT_EQUAL_INT(-1, ap_i8237_service_pending(&dma));
}

static void test_an_address_register_takes_two_bytes_low_first(void) {
  ap_i8237_t dma;
  ap_i8237_reset(&dma);

  /* The behaviour that identified the part in the oracle: two bytes written to
   * a channel address register, and the low one read back first. A device that
   * merely decoded the address would return the byte last written. */
  ap_i8237_write(&dma, 12, 0x00); /* clear the flip-flop */
  ap_i8237_write(&dma, 0, 0xAB);
  ap_i8237_write(&dma, 0, 0xCD);
  TEST_ASSERT_EQUAL_HEX16(0xCDAB, dma.channel[0].base_address);

  ap_i8237_write(&dma, 12, 0x00);
  TEST_ASSERT_EQUAL_HEX8(0xAB, ap_i8237_read(&dma, 0));
  TEST_ASSERT_EQUAL_HEX8(0xCD, ap_i8237_read(&dma, 0));
}

static void test_one_flip_flop_serves_every_channel(void) {
  ap_i8237_t dma;
  ap_i8237_reset(&dma);
  ap_i8237_write(&dma, 12, 0x00);

  /* `[8237]` has one "Internal First/Last Flip-Flop", not one per channel. So a
   * two-byte sequence left half finished on one channel puts the *next*
   * channel's first byte into its high half -- a fault that surfaces a long way
   * from its cause, and the reason the clear command exists at all. */
  ap_i8237_write(&dma, 0, 0x11); /* channel 0, low half only */
  ap_i8237_write(&dma, 2, 0x22); /* channel 1 -- lands in the high half */

  TEST_ASSERT_EQUAL_HEX16(0x2200, dma.channel[1].base_address);
}

static void test_clearing_the_flip_flop_restarts_the_sequence(void) {
  ap_i8237_t dma;
  ap_i8237_reset(&dma);

  ap_i8237_write(&dma, 12, 0x00);
  ap_i8237_write(&dma, 4, 0x34); /* low */
  ap_i8237_write(&dma, 12, 0x00);
  ap_i8237_write(&dma, 4, 0x78); /* low again, not high */
  TEST_ASSERT_EQUAL_HEX16(0x0078, dma.channel[2].base_address);
}

static void test_the_mode_register_names_its_own_channel(void) {
  ap_i8237_t dma;
  ap_i8237_reset(&dma);

  /* One address programs any of the four: "bits 1-0 select the channel", in the
   * value written rather than in the address. */
  ap_i8237_write(&dma, 11, (uint8_t)(0x02u | (1u << 6) | (2u << 2)));

  TEST_ASSERT_EQUAL_UINT(AP_I8237_MODE_SINGLE, ap_i8237_mode_of(&dma, 2));
  TEST_ASSERT_EQUAL_UINT(AP_I8237_TRANSFER_READ, ap_i8237_transfer_of(&dma, 2));
  /* And the other channels are untouched. */
  TEST_ASSERT_EQUAL_UINT(AP_I8237_MODE_DEMAND, ap_i8237_mode_of(&dma, 0));
}

static void test_every_transfer_and_mode_code_decodes(void) {
  ap_i8237_t dma;
  ap_i8237_reset(&dma);

  /* Figure 5's two small fields, exhaustively -- including the "11 Illegal"
   * transfer code, which is decoded as illegal rather than silently treated as
   * a read. */
  for (unsigned t = 0; t < 4u; t++) {
    for (unsigned m = 0; m < 4u; m++) {
      ap_i8237_write(&dma, 11, (uint8_t)(1u | (t << 2) | (m << 6)));
      TEST_ASSERT_EQUAL_UINT(t, (unsigned)ap_i8237_transfer_of(&dma, 1));
      TEST_ASSERT_EQUAL_UINT(m, (unsigned)ap_i8237_mode_of(&dma, 1));
    }
  }
}

static void test_a_single_mask_write_touches_one_channel(void) {
  ap_i8237_t dma;
  ap_i8237_reset(&dma);

  /* Figure 5: bits 1-0 the channel, bit 2 set or clear. */
  ap_i8237_write(&dma, 10, 0x02); /* clear channel 2's mask */
  TEST_ASSERT_EQUAL_HEX8(0x0B, dma.mask);

  ap_i8237_write(&dma, 10, 0x06); /* set it again */
  TEST_ASSERT_EQUAL_HEX8(0x0F, dma.mask);
}

static void test_the_clear_mask_command_enables_everything(void) {
  ap_i8237_t dma;
  ap_i8237_reset(&dma);

  /* "This command clears the mask bits of all four channels, enabling them to
   * accept DMA requests." */
  ap_i8237_write(&dma, 14, 0x00);
  TEST_ASSERT_EQUAL_HEX8(0x00, dma.mask);

  ap_i8237_set_request_pin(&dma, 2, true);
  TEST_ASSERT_EQUAL_INT(2, ap_i8237_service_pending(&dma));
}

static void test_all_four_mask_bits_can_be_written_at_once(void) {
  ap_i8237_t dma;
  ap_i8237_reset(&dma);

  /* "All four bits of the Mask register may also be written with a single
   * command." */
  ap_i8237_write(&dma, 15, 0x05);
  TEST_ASSERT_EQUAL_HEX8(0x05, dma.mask);
}

static void test_the_lowest_numbered_channel_wins(void) {
  ap_i8237_t dma;
  ap_i8237_reset(&dma);
  ap_i8237_write(&dma, 14, 0x00);

  /* Fixed priority, channel 0 highest. */
  ap_i8237_set_request_pin(&dma, 3, true);
  ap_i8237_set_request_pin(&dma, 1, true);
  TEST_ASSERT_EQUAL_INT(1, ap_i8237_service_pending(&dma));
}

static void test_a_software_request_is_not_masked(void) {
  ap_i8237_t dma;
  ap_i8237_reset(&dma); /* every channel masked */

  /* "These are non-maskable and subject to prioritization by the Priority
   * Encoder network." So a software request reaches the encoder where a pin
   * would not -- which is how a memory-to-memory transfer is started on a
   * controller whose channels are all masked. */
  ap_i8237_set_request_pin(&dma, 1, true);
  TEST_ASSERT_EQUAL_INT(-1, ap_i8237_service_pending(&dma));

  ap_i8237_write(&dma, 9, (uint8_t)(1u | 0x04u)); /* set channel 1's request */
  TEST_ASSERT_EQUAL_INT(1, ap_i8237_service_pending(&dma));
}

static void test_disabling_the_controller_silences_every_channel(void) {
  ap_i8237_t dma;
  ap_i8237_reset(&dma);
  ap_i8237_write(&dma, 14, 0x00);
  ap_i8237_set_request_pin(&dma, 0, true);
  TEST_ASSERT_EQUAL_INT(0, ap_i8237_service_pending(&dma));

  ap_i8237_write(&dma, 8, AP_I8237_CMD_CONTROLLER_DISABLE);
  TEST_ASSERT_EQUAL_INT(-1, ap_i8237_service_pending(&dma));
}

static void test_the_status_register_reports_requests_live(void) {
  ap_i8237_t dma;
  ap_i8237_reset(&dma);
  ap_i8237_write(&dma, 14, 0x00);

  /* "Bits 4-7 are set whenever their corresponding channel is requesting
   * service" -- live, not latched, so dropping the pin drops the bit. */
  ap_i8237_set_request_pin(&dma, 2, true);
  TEST_ASSERT_EQUAL_HEX8(0x40, ap_i8237_read(&dma, 8) & 0xF0u);

  ap_i8237_set_request_pin(&dma, 2, false);
  TEST_ASSERT_EQUAL_HEX8(0x00, ap_i8237_read(&dma, 8) & 0xF0u);
}

static void test_reading_the_status_register_clears_terminal_counts(void) {
  ap_i8237_t dma;
  ap_i8237_reset(&dma);

  /* "These bits are cleared upon Reset and on each Status Read." The low half
   * only -- the request half is live and unaffected. */
  ap_i8237_terminal_count(&dma, 1);
  TEST_ASSERT_EQUAL_HEX8(0x02, ap_i8237_read(&dma, 8) & 0x0Fu);
  TEST_ASSERT_EQUAL_HEX8(0x00, ap_i8237_read(&dma, 8) & 0x0Fu);
}

static void test_a_terminal_count_masks_a_channel_that_does_not_autoinitialise(
    void) {
  ap_i8237_t dma;
  ap_i8237_reset(&dma);
  ap_i8237_write(&dma, 14, 0x00);
  ap_i8237_write(&dma, 11, 0x01); /* channel 1, no autoinitialise */

  /* "Each mask bit is set when its associated channel produces an EOP if the
   * channel is not programmed for Autoinitialize." A one-shot transfer disarms
   * itself. */
  ap_i8237_terminal_count(&dma, 1);
  TEST_ASSERT_EQUAL_HEX8(0x02, dma.mask & 0x02u);
}

static void test_an_autoinitialising_channel_rearms_itself(void) {
  ap_i8237_t dma;
  ap_i8237_reset(&dma);
  ap_i8237_write(&dma, 14, 0x00);
  ap_i8237_write(&dma, 11, (uint8_t)(0x01u | AP_I8237_MODE_AUTOINIT));

  ap_i8237_write(&dma, 12, 0x00);
  ap_i8237_write(&dma, 2, 0x34); /* channel 1 address = 0x1234 */
  ap_i8237_write(&dma, 2, 0x12);
  ap_i8237_write(&dma, 3, 0xFF); /* count = 0x00FF */
  ap_i8237_write(&dma, 3, 0x00);

  /* Walk the current registers away from the base, as a transfer would. */
  dma.channel[1].current_address = 0x9999;
  dma.channel[1].current_count = 0;

  /* "It may also be reinitialized by an Autoinitialize back to its original
   * value. Autoinitialize takes place only after an EOP." And the mask stays
   * clear, so the channel free-runs. */
  ap_i8237_terminal_count(&dma, 1);
  TEST_ASSERT_EQUAL_HEX16(0x1234, dma.channel[1].current_address);
  TEST_ASSERT_EQUAL_HEX16(0x00FF, dma.channel[1].current_count);
  TEST_ASSERT_EQUAL_HEX8(0x00, dma.mask & 0x02u);
}

static void test_a_master_clear_is_a_reset(void) {
  ap_i8237_t dma;
  ap_i8237_reset(&dma);
  ap_i8237_write(&dma, 14, 0x00);
  ap_i8237_write(&dma, 8, 0x55);
  ap_i8237_write(&dma, 0, 0x11); /* leave the flip-flop half way */

  /* "This software instruction has the same effect as the hardware Reset. The
   * Command, Status, Request, Temporary, and Internal First/Last Flip-Flop
   * registers are cleared and the Mask register is set." */
  ap_i8237_write(&dma, 13, 0x00);

  ap_i8237_t fresh;
  ap_i8237_reset(&fresh);
  TEST_ASSERT_EQUAL_MEMORY(&fresh, &dma, sizeof fresh);
}

static void test_two_controllers_reset_alike_hold_identical_state(void) {
  ap_i8237_t a;
  ap_i8237_t b;
  memset(&a, 0xAA, sizeof a);
  memset(&b, 0x55, sizeof b);
  ap_i8237_reset(&a);
  ap_i8237_reset(&b);
  TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof a);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_reset_masks_every_channel);
  RUN_TEST(test_an_address_register_takes_two_bytes_low_first);
  RUN_TEST(test_one_flip_flop_serves_every_channel);
  RUN_TEST(test_clearing_the_flip_flop_restarts_the_sequence);
  RUN_TEST(test_the_mode_register_names_its_own_channel);
  RUN_TEST(test_every_transfer_and_mode_code_decodes);
  RUN_TEST(test_a_single_mask_write_touches_one_channel);
  RUN_TEST(test_the_clear_mask_command_enables_everything);
  RUN_TEST(test_all_four_mask_bits_can_be_written_at_once);
  RUN_TEST(test_the_lowest_numbered_channel_wins);
  RUN_TEST(test_a_software_request_is_not_masked);
  RUN_TEST(test_disabling_the_controller_silences_every_channel);
  RUN_TEST(test_the_status_register_reports_requests_live);
  RUN_TEST(test_reading_the_status_register_clears_terminal_counts);
  RUN_TEST(test_a_terminal_count_masks_a_channel_that_does_not_autoinitialise);
  RUN_TEST(test_an_autoinitialising_channel_rearms_itself);
  RUN_TEST(test_a_master_clear_is_a_reset);
  RUN_TEST(test_two_controllers_reset_alike_hold_identical_state);
  return UNITY_END();
}
