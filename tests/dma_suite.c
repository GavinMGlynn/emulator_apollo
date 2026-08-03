/* Apollo DMA controllers as the board wires them. Placement measured;
 * `FINDINGS.md` C13. */

#include "unity.h"

#include "board/ap_board.h"
#include "board/ap_dma.h"
#include "device/ap_i8237.h"
#include "device/ap_mc146818.h"
#include "board/ap_atmap.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_first_controller_is_byte_consecutive(void) {
  unsigned unit;
  unsigned reg;

  TEST_ASSERT_TRUE(ap_dma_decode(AP_DMA1_ADDR, &unit, &reg));
  TEST_ASSERT_EQUAL_UINT(0u, unit);
  TEST_ASSERT_EQUAL_UINT(0u, reg);

  TEST_ASSERT_TRUE(ap_dma_decode(AP_DMA1_ADDR + 15u, &unit, &reg));
  TEST_ASSERT_EQUAL_UINT(15u, reg);

  /* Aliased every sixteen bytes -- measured, the mask register reading `0F` at
   * offset 15 and again at 31. */
  TEST_ASSERT_TRUE(ap_dma_decode(AP_DMA1_ADDR + 31u, &unit, &reg));
  TEST_ASSERT_EQUAL_UINT(15u, reg);
}

static void test_the_second_controller_is_a_word_apart(void) {
  unsigned unit;
  unsigned reg;

  /* Stride 2, so register 15 lands at offset 30 and not 15. This is the whole
   * difference between the two, and it is measured rather than inferred from
   * the neighbour -- the dump reads `00` at offset 15 here. */
  TEST_ASSERT_TRUE(ap_dma_decode(AP_DMA2_ADDR + 30u, &unit, &reg));
  TEST_ASSERT_EQUAL_UINT(1u, unit);
  TEST_ASSERT_EQUAL_UINT(15u, reg);

  TEST_ASSERT_TRUE(ap_dma_decode(AP_DMA2_ADDR + 15u, &unit, &reg));
  TEST_ASSERT_EQUAL_UINT(7u, reg);
}

static void test_each_stride_is_pinned_by_a_readable_register(void) {
  ap_dma_t dma;
  ap_dma_reset(&dma);

  /* The placement is pinned through a register the part will actually read
   * back, rather than by reproducing the oracle's dump byte for byte.
   *
   * The dump identified the *stride* -- a distinguishable byte at offset 15 on
   * one controller and offset 30 on the other -- and that is all it identified.
   * This finding first glossed that byte as the mask register read back, which
   * `[8237]` Figure 6 contradicts: register 15 is "Illegal" to read. The gloss
   * was wrong and the measurement was not, so the test pins the measurement. */
  ap_dma_write(&dma, AP_DMA1_ADDR + 12u, 0x00); /* clear flip-flop */
  ap_dma_write(&dma, AP_DMA1_ADDR + 6u, 0x21);  /* channel 3 address, low */
  ap_dma_write(&dma, AP_DMA1_ADDR + 6u, 0x43);  /* high */
  ap_dma_write(&dma, AP_DMA1_ADDR + 12u, 0x00);
  TEST_ASSERT_EQUAL_HEX8(0x21, ap_dma_read(&dma, AP_DMA1_ADDR + 6u));
  /* Sixteen bytes on is the same register again. */
  TEST_ASSERT_EQUAL_HEX8(0x43, ap_dma_read(&dma, AP_DMA1_ADDR + 22u));

  /* And on the second controller the same register is a word apart. */
  ap_dma_write(&dma, AP_DMA2_ADDR + 24u, 0x00);
  ap_dma_write(&dma, AP_DMA2_ADDR + 12u, 0x65);
  ap_dma_write(&dma, AP_DMA2_ADDR + 12u, 0x87);
  TEST_ASSERT_EQUAL_HEX16(0x8765, dma.controller[1].channel[3].base_address);
}

static void test_a_write_only_register_reads_zero_not_a_value(void) {
  ap_dma_t dma;
  ap_dma_reset(&dma);

  /* `[8237]` Figure 6 permits exactly two reads -- status at 8 and temporary at
   * 13 -- and marks every other address "Illegal". The oracle answers `0F` at
   * register 15; this core answers zero.
   *
   * Neither is specified, and ours is the one that does not invent a register
   * value for an access the part does not define. Recorded as a divergence in
   * `FINDINGS.md` C13 rather than quietly matched, because matching it would
   * mean asserting the mask register is readable, which the datasheet denies. */
  ap_dma_write(&dma, AP_DMA1_ADDR + 15u, 0x0F);
  TEST_ASSERT_EQUAL_HEX8(0x00, ap_dma_read(&dma, AP_DMA1_ADDR + 15u));
  TEST_ASSERT_EQUAL_HEX8(0x0F, dma.controller[0].mask);
}

static void test_the_two_controllers_are_independent(void) {
  ap_dma_t dma;
  ap_dma_reset(&dma);

  /* Programming one must not touch the other. The 8237A holds its byte-pointer
   * flip-flop per part, so a half-finished sequence on the first controller
   * must not shift the second's -- a shared one would corrupt whichever was
   * programmed next, which is exactly the class of fault that was found inside
   * the part itself. */
  ap_dma_write(&dma, AP_DMA1_ADDR + 12u, 0x00); /* clear flip-flop, unit 0 */
  ap_dma_write(&dma, AP_DMA1_ADDR + 0u, 0x34);  /* low half only */

  ap_dma_write(&dma, AP_DMA2_ADDR + 24u, 0x00); /* clear flip-flop, unit 1 */
  ap_dma_write(&dma, AP_DMA2_ADDR + 0u, 0x78);
  ap_dma_write(&dma, AP_DMA2_ADDR + 0u, 0x56);

  TEST_ASSERT_EQUAL_HEX16(0x5678, dma.controller[1].channel[0].base_address);
  /* And unit 0 is still waiting for its high byte. */
  ap_dma_write(&dma, AP_DMA1_ADDR + 0u, 0x12);
  TEST_ASSERT_EQUAL_HEX16(0x1234, dma.controller[0].channel[0].base_address);
}

static void test_nothing_outside_the_two_ranges_decodes(void) {
  unsigned unit;
  unsigned reg;
  TEST_ASSERT_FALSE(ap_dma_decode(0x010B00u, &unit, &reg));
  TEST_ASSERT_FALSE(ap_dma_decode(0x010E00u, &unit, &reg));
  /* The calendar and the timer are neighbours and must not be caught. */
  TEST_ASSERT_FALSE(ap_dma_decode(0x010900u, &unit, &reg));
  TEST_ASSERT_FALSE(ap_dma_decode(0x010800u, &unit, &reg));
}

/* ---------------------------------------------------------------------------
 * The board driving a transfer: one arbitration point, and an address the map
 * supplies the top of.
 * ------------------------------------------------------------------------- */

static const ap_mc146818_time_t DMA_EPOCH = {
    .year = 1987u, .month = 7u, .day = 31u, .day_of_week = 6u,
    .hour = 21u, .minute = 9u, .second = 21u,
};

/* Main memory begins at `01000000` and the map's entries are physical page
 * numbers, so an entry has to point somewhere real for a transfer to land. */
#define DMA_RAM_BYTES 0x2000u
static uint8_t dma_ram[DMA_RAM_BYTES];
static ap_board_t dma_board;

static void arm_channel(ap_board_t *b, unsigned channel, uint8_t mode_bits,
                        uint16_t address, uint16_t count) {
  bool ok = false;
  const uint32_t base = AP_DMA1_ADDR;
  ap_board_write(b, base + AP_I8237_REG_MODE, (uint8_t)(mode_bits | channel), &ok);
  ap_board_write(b, base + AP_I8237_REG_CLEAR_FLIPFLOP, 0u, &ok);
  ap_board_write(b, base + channel * 2u, (uint8_t)(address & 0xFFu), &ok);
  ap_board_write(b, base + channel * 2u, (uint8_t)(address >> 8), &ok);
  ap_board_write(b, base + channel * 2u + 1u, (uint8_t)(count & 0xFFu), &ok);
  ap_board_write(b, base + channel * 2u + 1u, (uint8_t)(count >> 8), &ok);
  ap_board_write(b, base + AP_I8237_REG_MASK_SINGLE, (uint8_t)channel, &ok);
  /* A software request, which is what lets a probe start a transfer with no
   * device wired to the channel. `[8237]`: the request register's bits are
   * "set or reset separately under software control". */
  ap_board_write(b, base + AP_I8237_REG_REQUEST,
                 (uint8_t)(0x04u | channel), &ok);
}

/* Controller 1's request output arrives on controller 2's channel 0, and that
 * channel has to be programmed for cascade mode and unmasked before anything on
 * controller 1 can reach the bus at all -- `008778-03` §2.4's DRQ4, and what an
 * AT's firmware does at boot. A test that skipped it would be testing a machine
 * whose BIOS had not run. */
static void program_cascade(ap_board_t *b) {
  bool ok = false;
  ap_board_write(b, AP_DMA2_ADDR + AP_I8237_REG_MODE * 2u,
                 (uint8_t)((AP_I8237_MODE_CASCADE << 6) | AP_DMA_CASCADE_CHANNEL),
                 &ok);
  ap_board_write(b, AP_DMA2_ADDR + AP_I8237_REG_MASK_SINGLE * 2u,
                 (uint8_t)AP_DMA_CASCADE_CHANNEL, &ok);
}

static void build(void) {
  for (unsigned i = 0; i < DMA_RAM_BYTES; i++) {
    dma_ram[i] = 0;
  }
  TEST_ASSERT_TRUE(
      ap_board_init(&dma_board, dma_ram, DMA_RAM_BYTES, &DMA_EPOCH, 0x012345u));
  program_cascade(&dma_board);
}

/* The whole point of the arbitration point: while a controller holds the bus
 * the processor cannot run a cycle. Nothing here computes a delay -- the
 * processor is losing an arbitration, which is what `[030]` §7.7 makes it the
 * lowest-priority claimant for. */
static void test_a_dma_controller_takes_the_bus_from_the_processor(void) {
  build();
  TEST_ASSERT_TRUE(ap_board_processor_may_run(&dma_board));

  /* Verify mode: it generates addresses and moves nothing, so it needs no
   * device on the channel -- which is what makes contention measurable before
   * the channel assignments have been. */
  arm_channel(&dma_board, 1u, (uint8_t)((AP_I8237_MODE_BLOCK << 6) | (0u << 2)),
              0x0000u, 7u);

  bool stalled = false;
  for (unsigned i = 0; i < 32u && !stalled; i++) {
    ap_board_bus_tick(&dma_board);
    stalled = !ap_board_processor_may_run(&dma_board);
  }
  TEST_ASSERT_TRUE(stalled);

  /* And it is transferring while it holds it. */
  ap_board_bus_tick(&dma_board);
  TEST_ASSERT_TRUE(dma_board.dma_transfers > 0u);
}

/* The processor gets the bus back when the transfer ends, which is the terminal
 * count clearing the software request -- not anybody deciding to release. */
static void test_the_processor_gets_the_bus_back_at_terminal_count(void) {
  build();
  arm_channel(&dma_board, 1u, (uint8_t)((AP_I8237_MODE_BLOCK << 6) | (0u << 2)),
              0x0000u, 3u);

  for (unsigned i = 0; i < 64u; i++) {
    ap_board_bus_tick(&dma_board);
  }

  /* A count of 3 is four transfers, then the count expires and the channel
   * disarms itself. */
  TEST_ASSERT_EQUAL_UINT(4u, dma_board.dma_transfers);
  TEST_ASSERT_TRUE(ap_board_processor_may_run(&dma_board));
}

/* A DMA address is not a physical address on this machine: the part drives
 * sixteen bits and the translation map supplies bits 25-10. An entry pointing
 * at a page of main memory is what makes a transfer land there. */
static void test_a_transfer_lands_where_the_map_points(void) {
  build();
  bool ok = false;

  /* Entry 0 -> physical page of `01000000`, which is `AP_BOARD_RAM_BASE`
   * shifted down by the map's 1 KB page. Written through the map's own
   * register window, as software would. */
  const uint16_t page = (uint16_t)(AP_BOARD_RAM_BASE >> 10);
  ap_board_write(&dma_board, AP_ATMAP_BASE + 0u, (uint8_t)(page >> 8), &ok);
  ap_board_write(&dma_board, AP_ATMAP_BASE + 1u, (uint8_t)(page & 0xFFu), &ok);

  /* A read transfer: memory to the device. No device is wired, so the byte goes
   * nowhere and is counted -- but the *memory* side is the part under test, and
   * it is addressed through the map. */
  dma_ram[0x40] = 0x5Au;
  arm_channel(&dma_board, 1u, (uint8_t)((AP_I8237_MODE_BLOCK << 6) | (2u << 2)),
              0x0040u, 0u);

  for (unsigned i = 0; i < 64u; i++) {
    ap_board_bus_tick(&dma_board);
  }

  TEST_ASSERT_EQUAL_UINT(1u, dma_board.dma_transfers);
  TEST_ASSERT_EQUAL_UINT(1u, dma_board.dma_unwired_transfers);

  /* And a write transfer into the same page reaches the RAM the map chose. */
  build();
  ap_board_write(&dma_board, AP_ATMAP_BASE + 0u, (uint8_t)(page >> 8), &ok);
  ap_board_write(&dma_board, AP_ATMAP_BASE + 1u, (uint8_t)(page & 0xFFu), &ok);
  arm_channel(&dma_board, 1u, (uint8_t)((AP_I8237_MODE_BLOCK << 6) | (1u << 2)),
              0x0100u, 0u);
  for (unsigned i = 0; i < 64u; i++) {
    ap_board_bus_tick(&dma_board);
  }
  /* All ones: what nothing driving this bus reads, and what an empty AT slot
   * already answers on this board. */
  TEST_ASSERT_EQUAL_UINT(1u, dma_board.dma_transfers);
  TEST_ASSERT_EQUAL_HEX8(0xFFu, dma_ram[0x100]);
  TEST_ASSERT_EQUAL_UINT(1u, dma_board.dma_unwired_transfers);
}

/* A verify transfer needs no device and so is not counted as unwired: it is a
 * complete transfer, not a failed one. That distinction is what lets the
 * contention measurement be taken before the channel assignments are known. */
static void test_a_verify_transfer_is_not_an_unwired_one(void) {
  build();
  arm_channel(&dma_board, 2u, (uint8_t)((AP_I8237_MODE_BLOCK << 6) | (0u << 2)),
              0x0000u, 2u);

  for (unsigned i = 0; i < 64u; i++) {
    ap_board_bus_tick(&dma_board);
  }

  TEST_ASSERT_EQUAL_UINT(3u, dma_board.dma_transfers);
  TEST_ASSERT_EQUAL_UINT(0u, dma_board.dma_unwired_transfers);
}

/* Without the cascade programmed, nothing on controller 1 reaches the bus --
 * which is the hardware and not a limitation of this model. `008778-03` §2.4:
 * "DRQ4 is used on the system board to cascade Channels 0 through 3." A machine
 * whose firmware has not set that up has one working DMA controller, not two. */
static void test_controller_one_cannot_reach_the_bus_without_the_cascade(void) {
  for (unsigned i = 0; i < DMA_RAM_BYTES; i++) {
    dma_ram[i] = 0;
  }
  TEST_ASSERT_TRUE(
      ap_board_init(&dma_board, dma_ram, DMA_RAM_BYTES, &DMA_EPOCH, 0x012345u));
  /* Deliberately no `program_cascade`. */
  arm_channel(&dma_board, 1u, (uint8_t)((AP_I8237_MODE_BLOCK << 6) | (0u << 2)),
              0x0000u, 7u);

  for (unsigned i = 0; i < 64u; i++) {
    ap_board_bus_tick(&dma_board);
  }
  TEST_ASSERT_EQUAL_UINT(0u, dma_board.dma_transfers);
  TEST_ASSERT_TRUE(ap_board_processor_may_run(&dma_board));

  /* And programming it opens the route with nothing else changing. */
  program_cascade(&dma_board);
  for (unsigned i = 0; i < 64u; i++) {
    ap_board_bus_tick(&dma_board);
  }
  TEST_ASSERT_TRUE(dma_board.dma_transfers > 0u);
}

/* Table 2-4's priority column runs 1-4 for DRQ0-3 and 5-7 for DRQ5-7, which is
 * not the numeric order of the lines. It falls out of the cascade: channels 0
 * through 3 arrive on DRQ4, and DRQ4 is the second controller's *highest*
 * channel, so all four outrank DRQ5 through DRQ7. Asserted as the consequence
 * it is -- a controller-1 channel wins against a controller-2 channel of lower
 * number. */
static void test_the_cascade_puts_controller_ones_channels_first(void) {
  build();

  /* DRQ2 -- the floppy's line, controller 1 channel 2, Table 2-4 priority 3. */
  arm_channel(&dma_board, AP_DMA_FLOPPY_CHANNEL,
              (uint8_t)((AP_I8237_MODE_BLOCK << 6) | (0u << 2)), 0x0000u, 3u);

  /* And DRQ5 -- controller 2 channel 1, priority 5. A *lower* channel number on
   * the second controller than the cascade's neighbour, so a model that merely
   * compared channel numbers within one part would pick it. */
  bool ok = false;
  ap_board_write(&dma_board, AP_DMA2_ADDR + AP_I8237_REG_MODE * 2u,
                 (uint8_t)((AP_I8237_MODE_BLOCK << 6) | 1u), &ok);
  ap_board_write(&dma_board, AP_DMA2_ADDR + AP_I8237_REG_CLEAR_FLIPFLOP * 2u, 0u,
                 &ok);
  ap_board_write(&dma_board, AP_DMA2_ADDR + 2u * 2u, 0u, &ok); /* address */
  ap_board_write(&dma_board, AP_DMA2_ADDR + 2u * 2u, 0u, &ok);
  ap_board_write(&dma_board, AP_DMA2_ADDR + 3u * 2u, 3u, &ok); /* count */
  ap_board_write(&dma_board, AP_DMA2_ADDR + 3u * 2u, 0u, &ok);
  ap_board_write(&dma_board, AP_DMA2_ADDR + AP_I8237_REG_MASK_SINGLE * 2u, 1u,
                 &ok);
  ap_board_write(&dma_board, AP_DMA2_ADDR + AP_I8237_REG_REQUEST * 2u,
                 (uint8_t)(0x04u | 1u), &ok);

  /* Tick until the floppy's channel reaches terminal count -- which masks it,
   * since it is not autoinitialising -- and ask what the second controller's own
   * channel has managed in the meantime. */
  const uint8_t floppy_bit = (uint8_t)(1u << AP_DMA_FLOPPY_CHANNEL);
  unsigned ticks = 0;
  while ((dma_board.dma.controller[0].mask & floppy_bit) == 0u &&
         ticks < 64u) {
    ap_board_bus_tick(&dma_board);
    ticks++;
  }
  TEST_ASSERT_TRUE((dma_board.dma.controller[0].mask & floppy_bit) != 0u);

  /* Nothing. Four transfers, all the cascade's, while a channel of lower number
   * on the other controller waited -- which is Table 2-4's priority column. */
  TEST_ASSERT_EQUAL_UINT(4u, dma_board.dma_transfers);
  TEST_ASSERT_EQUAL_HEX16(
      3u, dma_board.dma.controller[1].channel[1].current_count);

  /* And it is waiting rather than shut out: once the cascade goes quiet it gets
   * the bus and runs to its own terminal count.
   *
   * Asked as "is it masked" rather than "has the count fallen", because the
   * count does not stop at zero -- `[8237]`'s terminal count *is* the borrow out
   * of zero, so a finished channel reads `FFFF`, which is larger than where it
   * started. A count comparison here passes while the transfer is running and
   * fails once it succeeds. */
  for (unsigned i = 0; i < 64u; i++) {
    ap_board_bus_tick(&dma_board);
  }
  TEST_ASSERT_TRUE((dma_board.dma.controller[1].mask & 0x02u) != 0u);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_controller_one_cannot_reach_the_bus_without_the_cascade);
  RUN_TEST(test_the_cascade_puts_controller_ones_channels_first);
  RUN_TEST(test_a_dma_controller_takes_the_bus_from_the_processor);
  RUN_TEST(test_the_processor_gets_the_bus_back_at_terminal_count);
  RUN_TEST(test_a_transfer_lands_where_the_map_points);
  RUN_TEST(test_a_verify_transfer_is_not_an_unwired_one);
  RUN_TEST(test_the_first_controller_is_byte_consecutive);
  RUN_TEST(test_the_second_controller_is_a_word_apart);
  RUN_TEST(test_each_stride_is_pinned_by_a_readable_register);
  RUN_TEST(test_a_write_only_register_reads_zero_not_a_value);
  RUN_TEST(test_the_two_controllers_are_independent);
  RUN_TEST(test_nothing_outside_the_two_ranges_decodes);
  return UNITY_END();
}
