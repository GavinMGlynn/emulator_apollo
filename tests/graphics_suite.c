/* Apollo display controller identification.
 *
 * Only the device ID register is modelled. That is a complete answer to the
 * boot PROM's question -- "is a screen fitted, and which" -- rather than a stub,
 * and the tests are about the identification alone.
 */

#include "board/ap_graphics.h"

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* The four types the firmware knows, by the value it compares against. The boot
 * PROM tests `08` then `0A` at the colour block and `09` then `0B` at the
 * monochrome one, so these numbers are not ours to choose. */
static void test_each_screen_reports_the_id_the_firmware_compares_against(void) {
  const struct {
    ap_screen_kind_t screen;
    bool colour;
    uint8_t id;
  } cases[] = {
      {AP_SCREEN_COLOUR_4_PLANE, true, 8u},
      {AP_SCREEN_COLOUR_8_PLANE, true, 10u},
      {AP_SCREEN_MONO_19_INCH, false, 9u},
      {AP_SCREEN_MONO_15_INCH, false, 11u},
  };
  for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    ap_graphics_t g;
    ap_graphics_init(&g, cases[i].screen);
    const uint32_t block = cases[i].colour ? AP_GRAPHICS_COLOUR_ADDR
                                           : AP_GRAPHICS_MONO_ADDR;
    TEST_ASSERT_EQUAL_HEX8(cases[i].id,
                           ap_graphics_read(&g, block + AP_GRAPHICS_DEVICE_ID));
  }
}

/* The block that does not match the fitted screen reads `FF`. This is how the
 * firmware tells which of the two controllers is present: it reads both, and
 * only one answers with an ID. A model where both blocks reported the same
 * screen would satisfy the test above and tell the firmware a colour machine
 * has a monochrome controller as well. */
static void test_the_other_family_s_block_reads_ff(void) {
  ap_graphics_t colour;
  ap_graphics_init(&colour, AP_SCREEN_COLOUR_8_PLANE);
  TEST_ASSERT_EQUAL_HEX8(
      10u, ap_graphics_read(&colour, AP_GRAPHICS_COLOUR_ADDR + 1u));
  TEST_ASSERT_EQUAL_HEX8(
      0xFFu, ap_graphics_read(&colour, AP_GRAPHICS_MONO_ADDR + 1u));

  ap_graphics_t mono;
  ap_graphics_init(&mono, AP_SCREEN_MONO_19_INCH);
  TEST_ASSERT_EQUAL_HEX8(9u,
                         ap_graphics_read(&mono, AP_GRAPHICS_MONO_ADDR + 1u));
  TEST_ASSERT_EQUAL_HEX8(
      0xFFu, ap_graphics_read(&mono, AP_GRAPHICS_COLOUR_ADDR + 1u));
}

/* The one that matters most, and the one whose absence sent an investigation
 * after a phantom bug in the CPU's exception path.
 *
 * With no screen fitted the blocks still **decode**. The ID register reads
 * `FF`, matching none of the four types, and the firmware concludes there is no
 * display and carries on. A machine that bus-errored here would make the
 * firmware take an exception the real one never takes -- and everything
 * downstream of that looks like a defect in whatever the handler touches.
 *
 * "Nothing is fitted" and "nothing is there" are different answers. */
static void test_an_absent_screen_still_decodes_and_reads_ff(void) {
  ap_graphics_t g;
  ap_graphics_init(&g, AP_SCREEN_NONE);

  bool colour = false;
  uint32_t offset = 0;
  TEST_ASSERT_TRUE(
      ap_graphics_decode(AP_GRAPHICS_COLOUR_ADDR + 1u, &colour, &offset));
  TEST_ASSERT_TRUE(colour);
  TEST_ASSERT_TRUE(
      ap_graphics_decode(AP_GRAPHICS_MONO_ADDR + 1u, &colour, &offset));
  TEST_ASSERT_FALSE(colour);

  TEST_ASSERT_EQUAL_HEX8(0xFFu,
                         ap_graphics_read(&g, AP_GRAPHICS_COLOUR_ADDR + 1u));
  TEST_ASSERT_EQUAL_HEX8(0xFFu,
                         ap_graphics_read(&g, AP_GRAPHICS_MONO_ADDR + 1u));
}

/* `05D800-05DC07` and `05E800-05EC07` inclusive, which is `0x408` bytes each --
 * not a power of two, and not aliased. Decoding it as a rounded-up power of two
 * would claim addresses the map gives to nothing, and the two blocks are close
 * enough together that a generous monochrome block would swallow part of the
 * gap before the colour one. */
static void test_the_blocks_are_the_ranges_the_map_gives_them(void) {
  bool colour = false;
  uint32_t offset = 0;

  TEST_ASSERT_TRUE(ap_graphics_decode(0x05D800u, &colour, &offset));
  TEST_ASSERT_EQUAL_UINT(0u, offset);
  TEST_ASSERT_TRUE(ap_graphics_decode(0x05DC07u, &colour, &offset));
  TEST_ASSERT_EQUAL_UINT(0x407u, offset);
  TEST_ASSERT_FALSE(ap_graphics_decode(0x05DC08u, &colour, &offset));

  TEST_ASSERT_TRUE(ap_graphics_decode(0x05E800u, &colour, &offset));
  TEST_ASSERT_TRUE(ap_graphics_decode(0x05EC07u, &colour, &offset));
  TEST_ASSERT_FALSE(ap_graphics_decode(0x05EC08u, &colour, &offset));

  /* And the gap between them belongs to neither. */
  TEST_ASSERT_FALSE(ap_graphics_decode(0x05E000u, &colour, &offset));
}

/* Every register other than the ID is unmodelled, and reads `FF` rather than
 * zero. Zero is a value several of these registers can legitimately hold, so a
 * driver reading it would take an unmodelled register for a real one reporting
 * a real state. `FF` is what an absent part reads. */
static void test_an_unmodelled_register_reads_ff_and_not_zero(void) {
  ap_graphics_t g;
  ap_graphics_init(&g, AP_SCREEN_COLOUR_8_PLANE);

  TEST_ASSERT_EQUAL_HEX8(0xFFu, ap_graphics_read(&g, AP_GRAPHICS_COLOUR_ADDR));
  TEST_ASSERT_EQUAL_HEX8(0xFFu,
                         ap_graphics_read(&g, AP_GRAPHICS_COLOUR_ADDR + 2u));
  TEST_ASSERT_EQUAL_HEX8(0xFFu,
                         ap_graphics_read(&g, AP_GRAPHICS_COLOUR_ADDR + 0x407u));
}

/* A write is accepted and discarded. It must not change the ID -- a register
 * that stored whatever was written to it would let a driver's probe write
 * convince the machine it had a different screen. */
static void test_a_write_is_absorbed_and_does_not_change_the_id(void) {
  ap_graphics_t g;
  ap_graphics_init(&g, AP_SCREEN_MONO_15_INCH);

  ap_graphics_write(&g, AP_GRAPHICS_MONO_ADDR + 1u, 0x08u);
  ap_graphics_write(&g, AP_GRAPHICS_MONO_ADDR + 4u, 0x5Au);

  TEST_ASSERT_EQUAL_HEX8(11u, ap_graphics_read(&g, AP_GRAPHICS_MONO_ADDR + 1u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_each_screen_reports_the_id_the_firmware_compares_against);
  RUN_TEST(test_the_other_family_s_block_reads_ff);
  RUN_TEST(test_an_absent_screen_still_decodes_and_reads_ff);
  RUN_TEST(test_the_blocks_are_the_ranges_the_map_gives_them);
  RUN_TEST(test_an_unmodelled_register_reads_ff_and_not_zero);
  RUN_TEST(test_a_write_is_absorbed_and_does_not_change_the_id);
  return UNITY_END();
}
