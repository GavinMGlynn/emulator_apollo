/* The Brooktree Bt458 colour lookup table, from the 1991 *Brooktree Product
 * Databook*'s Table 1 -- read from the page image, as every table in this
 * project is.
 *
 * The part is identified by two documents that do not mention each other:
 * `008778-03` §10.3 says the 8-plane board's lookup tables are "256 x 24" with
 * "triple 8-bit DAC's", and the databook lists the Bt458 as a "Triple 8-bit
 * RAMDAC with 256 x 24 RAM".
 */

#include "unity.h"

#include "device/ap_bt458.h"

void setUp(void) {}
void tearDown(void) {}

static ap_bt458_t lut;

/* Write a whole colour, as a driver does: three successive cycles. */
static void write_colour(ap_bt458_select_t space, uint8_t r, uint8_t g,
                         uint8_t b) {
  ap_bt458_write(&lut, space, r);
  ap_bt458_write(&lut, space, g);
  ap_bt458_write(&lut, space, b);
}

/* Table 1's four selectors are `C1` above `C0`, so the control registers are
 * `10` and the overlays `11`. Getting these two the wrong way round would put
 * colours in the masks and masks in the overlays, and both would still "work"
 * until something read one back. */
static void test_the_selector_is_c1_above_c0(void) {
  TEST_ASSERT_EQUAL_UINT(0u, AP_BT458_ADDRESS);
  TEST_ASSERT_EQUAL_UINT(1u, AP_BT458_PALETTE);
  TEST_ASSERT_EQUAL_UINT(2u, AP_BT458_CONTROL);
  TEST_ASSERT_EQUAL_UINT(3u, AP_BT458_OVERLAY);
}

/* Colour moves three bytes at a time and the address advances only on blue. */
static void test_a_colour_takes_three_cycles_and_then_advances(void) {
  ap_bt458_reset(&lut);
  ap_bt458_write(&lut, AP_BT458_ADDRESS, 0x10u);

  ap_bt458_write(&lut, AP_BT458_PALETTE, 0x11u); /* red */
  TEST_ASSERT_EQUAL_UINT(1u, ap_bt458_component(&lut));
  /* The address is checked through the struct, **not** by reading the address
   * register: that read would reset the component counter, which is the rule
   * the test below pins. Reading a device to observe it can change it, and
   * here it turns the next two writes into a fresh red and green that never
   * reach blue -- which is exactly how the first draft of this test failed. */
  TEST_ASSERT_EQUAL_HEX8(0x10u, lut.address);

  ap_bt458_write(&lut, AP_BT458_PALETTE, 0x22u); /* green */
  ap_bt458_write(&lut, AP_BT458_PALETTE, 0x33u); /* blue: commits */

  uint8_t rgb[3];
  TEST_ASSERT_TRUE(ap_bt458_palette(&lut, 0x10u, rgb));
  TEST_ASSERT_EQUAL_HEX8(0x11u, rgb[0]);
  TEST_ASSERT_EQUAL_HEX8(0x22u, rgb[1]);
  TEST_ASSERT_EQUAL_HEX8(0x33u, rgb[2]);
}

/* "During the blue write cycle, the 3 bytes of colour information are
 * concatenated into a 24-bit word and written to the location". So two bytes
 * write **nothing** -- a model that stored each byte as it arrived would be
 * indistinguishable here until a partial colour was read back. */
static void test_a_colour_that_never_reaches_blue_never_lands(void) {
  ap_bt458_reset(&lut);
  ap_bt458_write(&lut, AP_BT458_ADDRESS, 0x40u);
  ap_bt458_write(&lut, AP_BT458_PALETTE, 0xAAu);
  ap_bt458_write(&lut, AP_BT458_PALETTE, 0xBBu);

  uint8_t rgb[3];
  TEST_ASSERT_TRUE(ap_bt458_palette(&lut, 0x40u, rgb));
  TEST_ASSERT_EQUAL_HEX8(0u, rgb[0]);
  TEST_ASSERT_EQUAL_HEX8(0u, rgb[1]);
  TEST_ASSERT_EQUAL_HEX8(0u, rgb[2]);
  /* And the address has not moved. */
  TEST_ASSERT_EQUAL_HEX8(0x40u, ap_bt458_read(&lut, AP_BT458_ADDRESS));
}

/* "They are reset to zero when the MPU reads or writes to the address
 * register." A driver resynchronises a half-written colour by setting the
 * address, and a part that carried the counter across would put the next red
 * where green belongs -- for every colour after it. */
static void test_touching_the_address_register_abandons_a_part_written_colour(void) {
  ap_bt458_reset(&lut);
  ap_bt458_write(&lut, AP_BT458_ADDRESS, 0x50u);
  ap_bt458_write(&lut, AP_BT458_PALETTE, 0xAAu);
  TEST_ASSERT_EQUAL_UINT(1u, ap_bt458_component(&lut));

  ap_bt458_write(&lut, AP_BT458_ADDRESS, 0x50u);
  TEST_ASSERT_EQUAL_UINT(0u, ap_bt458_component(&lut));

  /* So a full colour now lands whole. */
  write_colour(AP_BT458_PALETTE, 1u, 2u, 3u);
  uint8_t rgb[3];
  TEST_ASSERT_TRUE(ap_bt458_palette(&lut, 0x50u, rgb));
  TEST_ASSERT_EQUAL_HEX8(1u, rgb[0]);
  TEST_ASSERT_EQUAL_HEX8(3u, rgb[2]);

  /* Reading the address register resets it too, which the datasheet says in
   * the same sentence and is easy to implement on the write path alone. */
  ap_bt458_write(&lut, AP_BT458_PALETTE, 0x99u);
  TEST_ASSERT_EQUAL_UINT(1u, ap_bt458_component(&lut));
  (void)ap_bt458_read(&lut, AP_BT458_ADDRESS);
  TEST_ASSERT_EQUAL_UINT(0u, ap_bt458_component(&lut));
}

/* Successive colours walk the table without the address being rewritten --
 * which is the whole point of the auto-increment, and how a driver loads 256
 * entries with 768 writes and one address. */
static void test_successive_colours_walk_the_palette(void) {
  ap_bt458_reset(&lut);
  ap_bt458_write(&lut, AP_BT458_ADDRESS, 0x00u);
  for (unsigned i = 0; i < 4u; i++) {
    write_colour(AP_BT458_PALETTE, (uint8_t)i, (uint8_t)(i + 0x10u),
                 (uint8_t)(i + 0x20u));
  }
  for (unsigned i = 0; i < 4u; i++) {
    uint8_t rgb[3];
    TEST_ASSERT_TRUE(ap_bt458_palette(&lut, i, rgb));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)i, rgb[0]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(i + 0x20u), rgb[2]);
  }
  TEST_ASSERT_EQUAL_HEX8(0x04u, ap_bt458_read(&lut, AP_BT458_ADDRESS));
}

/* "the address register resets to $00 after a blue read or write cycle to
 * location $FF". */
static void test_the_palette_wraps_at_the_last_entry(void) {
  ap_bt458_reset(&lut);
  ap_bt458_write(&lut, AP_BT458_ADDRESS, 0xFFu);
  write_colour(AP_BT458_PALETTE, 7u, 8u, 9u);

  uint8_t rgb[3];
  TEST_ASSERT_TRUE(ap_bt458_palette(&lut, 0xFFu, rgb));
  TEST_ASSERT_EQUAL_HEX8(7u, rgb[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00u, ap_bt458_read(&lut, AP_BT458_ADDRESS));
}

/* **The trap.** "the address register increments to $04 following a blue read
 * or write cycle to overlay register 3" -- and $04 is the read mask, in a
 * different `C1`/`C0` space. The overlays do *not* wrap to 0 like the palette
 * does, so a model that treated the two spaces alike would keep writing colours
 * where the driver had moved on to masks. */
static void test_the_overlays_run_off_their_end_into_the_read_mask(void) {
  ap_bt458_reset(&lut);
  ap_bt458_write(&lut, AP_BT458_ADDRESS, 0x03u);
  write_colour(AP_BT458_OVERLAY, 0xDEu, 0xADu, 0xBEu);

  TEST_ASSERT_EQUAL_HEX8(AP_BT458_READ_MASK, ap_bt458_read(&lut, AP_BT458_ADDRESS));
  TEST_ASSERT_EQUAL_HEX8(0x04u, AP_BT458_READ_MASK);

  /* Overlay 3 did take the colour. */
  TEST_ASSERT_EQUAL_HEX8(0xDEu, lut.overlay[3][0]);
  TEST_ASSERT_EQUAL_HEX8(0xBEu, lut.overlay[3][2]);

  /* Overlays 0-2 advance normally, so only the last one is special. */
  ap_bt458_write(&lut, AP_BT458_ADDRESS, 0x01u);
  write_colour(AP_BT458_OVERLAY, 1u, 2u, 3u);
  TEST_ASSERT_EQUAL_HEX8(0x02u, ap_bt458_read(&lut, AP_BT458_ADDRESS));
}

/* The control registers are single bytes selected by the address register, not
 * colours. `$04`-`$07` here are not palette entries `$04`-`$07`: the same
 * address in a different `C1`/`C0` space reaches a different thing entirely. */
static void test_the_control_registers_are_bytes_not_colours(void) {
  ap_bt458_reset(&lut);

  ap_bt458_write(&lut, AP_BT458_ADDRESS, AP_BT458_READ_MASK);
  ap_bt458_write(&lut, AP_BT458_CONTROL, 0xF0u);
  TEST_ASSERT_EQUAL_HEX8(0xF0u, ap_bt458_read(&lut, AP_BT458_CONTROL));
  /* One byte, and the address has not advanced -- so a second write hits the
   * same register rather than the blink mask. */
  TEST_ASSERT_EQUAL_HEX8(AP_BT458_READ_MASK, lut.address);

  ap_bt458_write(&lut, AP_BT458_ADDRESS, AP_BT458_COMMAND);
  ap_bt458_write(&lut, AP_BT458_CONTROL, 0x43u);
  TEST_ASSERT_EQUAL_HEX8(0x43u, lut.command);
  /* The read mask is untouched by it. */
  TEST_ASSERT_EQUAL_HEX8(0xF0u, lut.read_mask);

  /* And palette entry $04 is a different location from the read mask at $04. */
  ap_bt458_write(&lut, AP_BT458_ADDRESS, 0x04u);
  write_colour(AP_BT458_PALETTE, 0x11u, 0x22u, 0x33u);
  TEST_ASSERT_EQUAL_HEX8(0xF0u, lut.read_mask);
  uint8_t rgb[3];
  TEST_ASSERT_TRUE(ap_bt458_palette(&lut, 0x04u, rgb));
  TEST_ASSERT_EQUAL_HEX8(0x11u, rgb[0]);
}

/* Reading colour cycles the same counter and advances the same way, so a driver
 * that reads back a palette it wrote gets it in the order it wrote it. */
static void test_reading_a_colour_cycles_and_advances_as_writing_does(void) {
  ap_bt458_reset(&lut);
  ap_bt458_write(&lut, AP_BT458_ADDRESS, 0x80u);
  write_colour(AP_BT458_PALETTE, 0x31u, 0x41u, 0x59u);

  ap_bt458_write(&lut, AP_BT458_ADDRESS, 0x80u);
  TEST_ASSERT_EQUAL_HEX8(0x31u, ap_bt458_read(&lut, AP_BT458_PALETTE));
  TEST_ASSERT_EQUAL_HEX8(0x41u, ap_bt458_read(&lut, AP_BT458_PALETTE));
  TEST_ASSERT_EQUAL_HEX8(0x59u, ap_bt458_read(&lut, AP_BT458_PALETTE));
  TEST_ASSERT_EQUAL_HEX8(0x81u, ap_bt458_read(&lut, AP_BT458_ADDRESS));
}

/* The control-register tally exists to answer one question -- does any software
 * on this machine write registers this core stores and never decodes? -- so it
 * must count writes that *reach* a register and nothing else. */
static void test_the_control_write_tally_counts_only_real_registers(void) {
  ap_bt458_reset(&lut);

  /* Two writes to the command register at $06. */
  ap_bt458_write(&lut, AP_BT458_ADDRESS, AP_BT458_COMMAND);
  ap_bt458_write(&lut, AP_BT458_CONTROL, 0x40u);
  ap_bt458_write(&lut, AP_BT458_CONTROL, 0xC0u);
  TEST_ASSERT_EQUAL_UINT(2u, ap_bt458_control_writes(&lut, AP_BT458_COMMAND));
  TEST_ASSERT_EQUAL_UINT(0u, ap_bt458_control_writes(&lut, AP_BT458_TEST));

  /* An address outside $04-$07 with this selector reaches no register, so it
   * is not counted -- a tally that included it would answer a different
   * question from the one asked. */
  ap_bt458_write(&lut, AP_BT458_ADDRESS, 0x00u);
  ap_bt458_write(&lut, AP_BT458_CONTROL, 0xFFu);
  TEST_ASSERT_EQUAL_UINT(0u, ap_bt458_control_writes(&lut, 0x00u));
  TEST_ASSERT_EQUAL_UINT(2u, ap_bt458_control_writes(&lut, AP_BT458_COMMAND));

  /* And a palette write is not a control write, however many bytes it takes. */
  ap_bt458_write(&lut, AP_BT458_ADDRESS, 0x10u);
  ap_bt458_write(&lut, AP_BT458_PALETTE, 1u);
  ap_bt458_write(&lut, AP_BT458_PALETTE, 2u);
  ap_bt458_write(&lut, AP_BT458_PALETTE, 3u);
  TEST_ASSERT_EQUAL_UINT(2u, ap_bt458_control_writes(&lut, AP_BT458_COMMAND));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_selector_is_c1_above_c0);
  RUN_TEST(test_a_colour_takes_three_cycles_and_then_advances);
  RUN_TEST(test_a_colour_that_never_reaches_blue_never_lands);
  RUN_TEST(test_touching_the_address_register_abandons_a_part_written_colour);
  RUN_TEST(test_successive_colours_walk_the_palette);
  RUN_TEST(test_the_palette_wraps_at_the_last_entry);
  RUN_TEST(test_the_overlays_run_off_their_end_into_the_read_mask);
  RUN_TEST(test_the_control_registers_are_bytes_not_colours);
  RUN_TEST(test_reading_a_colour_cycles_and_advances_as_writing_does);
  RUN_TEST(test_the_control_write_tally_counts_only_real_registers);
  return UNITY_END();
}
