/* Apollo DMA controllers as the board wires them. Placement measured;
 * `FINDINGS.md` C13. */

#include "unity.h"

#include "board/ap_dma.h"

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

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_first_controller_is_byte_consecutive);
  RUN_TEST(test_the_second_controller_is_a_word_apart);
  RUN_TEST(test_each_stride_is_pinned_by_a_readable_register);
  RUN_TEST(test_a_write_only_register_reads_zero_not_a_value);
  RUN_TEST(test_the_two_controllers_are_independent);
  RUN_TEST(test_nothing_outside_the_two_ranges_decodes);
  return UNITY_END();
}
