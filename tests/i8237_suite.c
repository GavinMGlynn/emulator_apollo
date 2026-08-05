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

/* ---------------------------------------------------------------------------
 * The transfer cycle
 * ------------------------------------------------------------------------- */

#define DMA_MEM_BYTES 0x100u

typedef struct {
  uint8_t memory[DMA_MEM_BYTES];
  uint8_t device_next;             /* what the peripheral hands over */
  uint8_t device_taken[16];        /* what it was given */
  unsigned device_reads;
  unsigned device_writes;
  unsigned memory_reads;
  unsigned memory_writes;
} rig_t;

static uint8_t rig_memory_read(void *ctx, uint16_t address) {
  rig_t *r = (rig_t *)ctx;
  r->memory_reads++;
  return r->memory[address % DMA_MEM_BYTES];
}
static void rig_memory_write(void *ctx, uint16_t address, uint8_t value) {
  rig_t *r = (rig_t *)ctx;
  r->memory_writes++;
  r->memory[address % DMA_MEM_BYTES] = value;
}
static uint8_t rig_device_read(void *ctx, unsigned channel) {
  (void)channel;
  rig_t *r = (rig_t *)ctx;
  r->device_reads++;
  return r->device_next++;
}
static void rig_device_write(void *ctx, unsigned channel, uint8_t value) {
  (void)channel;
  rig_t *r = (rig_t *)ctx;
  if (r->device_writes < 16u) {
    r->device_taken[r->device_writes] = value;
  }
  r->device_writes++;
}

static ap_i8237_bus_t rig_bus(rig_t *r) {
  return (ap_i8237_bus_t){
      .memory_read = rig_memory_read,
      .memory_write = rig_memory_write,
      .device_read = rig_device_read,
      .device_write = rig_device_write,
      .context = r,
  };
}

/* Program one channel: mode, address, count, unmasked, requesting. `[8237]`
 * requires the flip-flop cleared before the two-byte pairs, which is the whole
 * reason register 12 exists. */
static void arm(ap_i8237_t *d, unsigned channel, uint8_t mode_bits,
                uint16_t address, uint16_t count) {
  ap_i8237_write(d, AP_I8237_REG_MODE, (uint8_t)(mode_bits | channel));
  ap_i8237_write(d, AP_I8237_REG_CLEAR_FLIPFLOP, 0u);
  ap_i8237_write(d, channel * 2u, (uint8_t)(address & 0xFFu));
  ap_i8237_write(d, channel * 2u, (uint8_t)(address >> 8));
  ap_i8237_write(d, channel * 2u + 1u, (uint8_t)(count & 0xFFu));
  ap_i8237_write(d, channel * 2u + 1u, (uint8_t)(count >> 8));
  ap_i8237_write(d, AP_I8237_REG_MASK_SINGLE, (uint8_t)channel);
  ap_i8237_set_request_pin(d, channel, true);
}

/* "Write transfers move data from an I/O device to memory" -- named for what
 * the memory sees, which is the opposite of how the word reads. */
static void test_a_write_transfer_moves_a_device_byte_into_memory(void) {
  ap_i8237_t d;
  ap_i8237_reset(&d);
  rig_t r = {0};
  r.device_next = 0x5Au;
  const ap_i8237_bus_t bus = rig_bus(&r);

  /* Single mode, write transfer, increment. */
  arm(&d, 2u, (uint8_t)((AP_I8237_MODE_SINGLE << 6) | (1u << 2)), 0x0040u, 3u);

  const ap_i8237_cycle_t c = ap_i8237_transfer(&d, &bus);
  TEST_ASSERT_TRUE(c.ran);
  TEST_ASSERT_EQUAL_UINT(2u, c.channel);
  TEST_ASSERT_EQUAL_HEX16(0x0040u, c.address);
  TEST_ASSERT_TRUE(c.wrote_memory);
  TEST_ASSERT_FALSE(c.terminal_count);
  TEST_ASSERT_EQUAL_HEX8(0x5Au, r.memory[0x40]);
  TEST_ASSERT_EQUAL_UINT(1u, r.device_reads);

  /* "automatically incremented ... after each transfer" */
  TEST_ASSERT_EQUAL_HEX16(0x0041u, d.channel[2].current_address);
  TEST_ASSERT_EQUAL_HEX16(2u, d.channel[2].current_count);
}

/* And the other direction, which addresses memory and merely selects the
 * device: the callback takes a channel, because `DACK` is what picks the
 * peripheral and no address ever reaches it. */
static void test_a_read_transfer_hands_a_memory_byte_to_the_device(void) {
  ap_i8237_t d;
  ap_i8237_reset(&d);
  rig_t r = {0};
  r.memory[0x10] = 0xC3u;
  const ap_i8237_bus_t bus = rig_bus(&r);

  arm(&d, 0u, (uint8_t)((AP_I8237_MODE_SINGLE << 6) | (2u << 2)), 0x0010u, 0u);

  const ap_i8237_cycle_t c = ap_i8237_transfer(&d, &bus);
  TEST_ASSERT_TRUE(c.ran);
  TEST_ASSERT_FALSE(c.wrote_memory);
  TEST_ASSERT_EQUAL_UINT(1u, r.device_writes);
  TEST_ASSERT_EQUAL_HEX8(0xC3u, r.device_taken[0]);
}

/* "The number of transfers is one more than the number programmed", and the
 * terminal count comes when the count "goes from zero to FFFFH". A model that
 * ended at zero would move one byte too few every time, which is the sort of
 * error that looks like an off-by-one in the driver. */
static void test_a_count_of_n_moves_n_plus_one_bytes(void) {
  ap_i8237_t d;
  ap_i8237_reset(&d);
  rig_t r = {0};
  r.device_next = 1u;
  const ap_i8237_bus_t bus = rig_bus(&r);

  arm(&d, 1u, (uint8_t)((AP_I8237_MODE_BLOCK << 6) | (1u << 2)), 0x0000u, 3u);

  unsigned moved = 0;
  bool ended = false;
  for (unsigned i = 0; i < 8u && !ended; i++) {
    const ap_i8237_cycle_t c = ap_i8237_transfer(&d, &bus);
    if (!c.ran) {
      break;
    }
    moved++;
    ended = c.terminal_count;
  }

  TEST_ASSERT_TRUE(ended);
  TEST_ASSERT_EQUAL_UINT(4u, moved);
  TEST_ASSERT_EQUAL_HEX8(1u, r.memory[0]);
  TEST_ASSERT_EQUAL_HEX8(4u, r.memory[3]);

  /* And the terminal count did what the register rules say: status bit set,
   * and the channel masked because it is not autoinitialising. */
  TEST_ASSERT_EQUAL_HEX8(0x02u, (uint8_t)(d.status & 0x0Fu));
  TEST_ASSERT_EQUAL_HEX8(0x02u, (uint8_t)(d.mask & 0x02u));
}

/* Autoinitialise reloads and does *not* mask, so the channel free-runs -- the
 * difference between a transfer that must be reprogrammed and one that does
 * not. Already tested at the register level; here it is the transfer that
 * reaches it. */
static void test_an_autoinitialising_channel_reloads_and_stays_armed(void) {
  ap_i8237_t d;
  ap_i8237_reset(&d);
  rig_t r = {0};
  const ap_i8237_bus_t bus = rig_bus(&r);

  arm(&d, 3u,
      (uint8_t)((AP_I8237_MODE_BLOCK << 6) | AP_I8237_MODE_AUTOINIT | (1u << 2)),
      0x0020u, 1u);

  (void)ap_i8237_transfer(&d, &bus);
  const ap_i8237_cycle_t last = ap_i8237_transfer(&d, &bus);
  TEST_ASSERT_TRUE(last.terminal_count);

  TEST_ASSERT_EQUAL_HEX16(0x0020u, d.channel[3].current_address);
  TEST_ASSERT_EQUAL_HEX16(1u, d.channel[3].current_count);
  TEST_ASSERT_EQUAL_HEX8(0u, (uint8_t)(d.mask & 0x08u));
}

/* Decrement mode walks the other way, which is bit 5 of the mode byte and not
 * a property of the direction of the data. */
static void test_decrement_mode_walks_the_address_downwards(void) {
  ap_i8237_t d;
  ap_i8237_reset(&d);
  rig_t r = {0};
  const ap_i8237_bus_t bus = rig_bus(&r);

  arm(&d, 0u,
      (uint8_t)((AP_I8237_MODE_SINGLE << 6) | AP_I8237_MODE_DECREMENT |
                (1u << 2)),
      0x0080u, 2u);

  (void)ap_i8237_transfer(&d, &bus);
  TEST_ASSERT_EQUAL_HEX16(0x007Fu, d.channel[0].current_address);
}

/* "Verify transfers are pseudo transfers ... the memory and I/O control lines
 * all remain inactive." The address and count still advance, which is why this
 * is a transfer that happened rather than one that was refused. */
static void test_a_verify_transfer_advances_but_moves_nothing(void) {
  ap_i8237_t d;
  ap_i8237_reset(&d);
  rig_t r = {0};
  const ap_i8237_bus_t bus = rig_bus(&r);

  arm(&d, 0u, (uint8_t)((AP_I8237_MODE_SINGLE << 6) | (0u << 2)), 0x0004u, 1u);

  const ap_i8237_cycle_t c = ap_i8237_transfer(&d, &bus);
  TEST_ASSERT_TRUE(c.ran);
  TEST_ASSERT_EQUAL_UINT(0u, r.device_reads);
  TEST_ASSERT_EQUAL_UINT(0u, r.device_writes);
  TEST_ASSERT_EQUAL_HEX16(0x0005u, d.channel[0].current_address);
}

/* A cascade channel transfers nothing: it passes the request up, and the bus it
 * wins belongs to whatever asked through it. An illegal mode transfers nothing
 * either, because Figure 5 defines no behaviour for `11` and this core does not
 * supply one. Memory-to-memory is refused outright rather than half-run. */
static void test_three_modes_run_no_transfer_at_all(void) {
  rig_t r = {0};
  const ap_i8237_bus_t bus = rig_bus(&r);

  ap_i8237_t cascade;
  ap_i8237_reset(&cascade);
  arm(&cascade, 0u, (uint8_t)(AP_I8237_MODE_CASCADE << 6), 0x0000u, 1u);
  TEST_ASSERT_FALSE(ap_i8237_transfer(&cascade, &bus).ran);

  ap_i8237_t illegal;
  ap_i8237_reset(&illegal);
  arm(&illegal, 0u, (uint8_t)((AP_I8237_MODE_SINGLE << 6) | (3u << 2)), 0u, 1u);
  TEST_ASSERT_FALSE(ap_i8237_transfer(&illegal, &bus).ran);

  TEST_ASSERT_EQUAL_UINT(0u, r.device_reads);
  TEST_ASSERT_EQUAL_UINT(0u, r.device_writes);
}

/* ## Memory to memory: channel 0 reads, channel 1 writes, and only 1 counts
 *
 * `[8237]`: the command bit "selects channels 0 and 1 to operate as
 * memory-to-memory transfer channels", the transfer is "initiated by setting
 * the software DREQ for channel 0", the byte goes through the temporary
 * register, and "**the channel 1 current Word Count is decremented**".
 *
 * This was declined outright, on the grounds that a transfer needs a bus to
 * arbitrate for. It has one; what it lacked was a reason, until the loaded
 * diagnostic's `CPU (dma) Test #1` programmed a block move and compared the
 * halves.
 */
static void test_memory_to_memory_moves_through_the_temporary_register(void) {
  rig_t r = {0};
  const ap_i8237_bus_t bus = rig_bus(&r);
  ap_i8237_t d;
  ap_i8237_reset(&d);

  arm(&d, 0u, (uint8_t)(AP_I8237_MODE_BLOCK << 6), 0x1000u, 3u);
  arm(&d, 1u, (uint8_t)(AP_I8237_MODE_BLOCK << 6), 0x2000u, 3u);
  ap_i8237_write(&d, AP_I8237_REG_STATUS_COMMAND, AP_I8237_CMD_MEM_TO_MEM);
  /* The software request for channel 0, which is what starts it. */
  ap_i8237_write(&d, AP_I8237_REG_REQUEST, 0x04u);

  const ap_i8237_cycle_t first = ap_i8237_transfer(&d, &bus);
  TEST_ASSERT_TRUE(first.ran);
  /* The cycle is reported against channel **1**: that is the one whose address
   * the byte lands at and whose count ends the service. */
  TEST_ASSERT_EQUAL_UINT(1u, first.channel);
  TEST_ASSERT_EQUAL_HEX16(0x2000u, first.address);
  TEST_ASSERT_TRUE(first.wrote_memory);

  /* Memory both ways and no device touched: a memory-to-memory transfer has no
   * peripheral, which is what made the board's device hooks answer `FF`. */
  TEST_ASSERT_EQUAL_UINT(1u, r.memory_reads);
  TEST_ASSERT_EQUAL_UINT(1u, r.memory_writes);
  TEST_ASSERT_EQUAL_UINT(0u, r.device_reads);
  TEST_ASSERT_EQUAL_UINT(0u, r.device_writes);

  /* Both addresses advanced, and **only channel 1's count**. */
  TEST_ASSERT_EQUAL_HEX16(0x1001u, d.channel[0].current_address);
  TEST_ASSERT_EQUAL_HEX16(0x2001u, d.channel[1].current_address);
  TEST_ASSERT_EQUAL_HEX16(3u, d.channel[0].current_count);
  TEST_ASSERT_EQUAL_HEX16(2u, d.channel[1].current_count);
}

/* "Channel 0 may be programmed to retain the same address for all transfers.
 * This allows a single word to be written to a block of memory." */
static void test_holding_channel_zeros_address_fills_a_block(void) {
  rig_t r = {0};
  const ap_i8237_bus_t bus = rig_bus(&r);
  ap_i8237_t d;
  ap_i8237_reset(&d);

  arm(&d, 0u, (uint8_t)(AP_I8237_MODE_BLOCK << 6), 0x1000u, 3u);
  arm(&d, 1u, (uint8_t)(AP_I8237_MODE_BLOCK << 6), 0x2000u, 3u);
  ap_i8237_write(&d, AP_I8237_REG_STATUS_COMMAND,
                 (uint8_t)(AP_I8237_CMD_MEM_TO_MEM |
                           AP_I8237_CMD_CH0_ADDRESS_HOLD));
  ap_i8237_write(&d, AP_I8237_REG_REQUEST, 0x04u);

  (void)ap_i8237_transfer(&d, &bus);
  (void)ap_i8237_transfer(&d, &bus);

  TEST_ASSERT_EQUAL_HEX16(0x1000u, d.channel[0].current_address);
  TEST_ASSERT_EQUAL_HEX16(0x2002u, d.channel[1].current_address);
}

/* The service ends on channel 1's count borrowing out of zero, and the status
 * bit that appears is channel 1's -- not channel 0's, which never moved. */
static void test_the_service_ends_on_channel_ones_terminal_count(void) {
  rig_t r = {0};
  const ap_i8237_bus_t bus = rig_bus(&r);
  ap_i8237_t d;
  ap_i8237_reset(&d);

  arm(&d, 0u, (uint8_t)(AP_I8237_MODE_BLOCK << 6), 0x1000u, 0u);
  arm(&d, 1u, (uint8_t)(AP_I8237_MODE_BLOCK << 6), 0x2000u, 0u);
  ap_i8237_write(&d, AP_I8237_REG_STATUS_COMMAND, AP_I8237_CMD_MEM_TO_MEM);
  ap_i8237_write(&d, AP_I8237_REG_REQUEST, 0x04u);

  const ap_i8237_cycle_t only = ap_i8237_transfer(&d, &bus);
  TEST_ASSERT_TRUE(only.ran);
  TEST_ASSERT_TRUE(only.terminal_count);
  TEST_ASSERT_EQUAL_HEX8(0x02u,
                         (uint8_t)(ap_i8237_read(&d,
                                                 AP_I8237_REG_STATUS_COMMAND) &
                                   0x0Fu));
}

/* A masked channel is not asking, so nothing runs however hard its pin is
 * pulled -- the priority encoder's rule, reached through the transfer. */
static void test_a_masked_channel_transfers_nothing(void) {
  ap_i8237_t d;
  ap_i8237_reset(&d);
  rig_t r = {0};
  const ap_i8237_bus_t bus = rig_bus(&r);

  arm(&d, 2u, (uint8_t)((AP_I8237_MODE_SINGLE << 6) | (1u << 2)), 0x0040u, 3u);
  ap_i8237_write(&d, AP_I8237_REG_MASK_SINGLE, (uint8_t)(0x04u | 2u));

  TEST_ASSERT_FALSE(ap_i8237_transfer(&d, &bus).ran);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_write_transfer_moves_a_device_byte_into_memory);
  RUN_TEST(test_a_read_transfer_hands_a_memory_byte_to_the_device);
  RUN_TEST(test_a_count_of_n_moves_n_plus_one_bytes);
  RUN_TEST(test_an_autoinitialising_channel_reloads_and_stays_armed);
  RUN_TEST(test_decrement_mode_walks_the_address_downwards);
  RUN_TEST(test_a_verify_transfer_advances_but_moves_nothing);
  RUN_TEST(test_three_modes_run_no_transfer_at_all);
  RUN_TEST(test_memory_to_memory_moves_through_the_temporary_register);
  RUN_TEST(test_holding_channel_zeros_address_fills_a_block);
  RUN_TEST(test_the_service_ends_on_channel_ones_terminal_count);
  RUN_TEST(test_a_masked_channel_transfers_nothing);
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
