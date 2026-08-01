/* Apollo display controller identification.
 *
 * Only the device ID register is modelled. That is a complete answer to the
 * boot PROM's question -- "is a screen fitted, and which" -- rather than a stub,
 * and the tests are about the identification alone.
 */

#include <stddef.h>
#include <string.h>

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


/* The graphics memories, and the ordering hazard that was live until they were
 * named: both fall **inside** the AT bus memory window, so a board that checked
 * the window first would report the machine's own frame buffer as an empty
 * expansion slot. The I/O window has the same hazard and already has a test;
 * this is the memory window's.
 *
 * Nothing about the device suites would have caught it -- they call the device
 * directly, and the device was right. Only a test of the *map* can see it. */
static void test_the_graphics_memories_decode(void) {
  bool colour = false;
  uint32_t offset = 0;

  TEST_ASSERT_TRUE(ap_graphics_decode_memory(0x0A0000u, &colour, &offset));
  TEST_ASSERT_TRUE(colour);
  TEST_ASSERT_EQUAL_UINT(0u, offset);
  TEST_ASSERT_TRUE(ap_graphics_decode_memory(0x0BFFFFu, &colour, &offset));
  TEST_ASSERT_TRUE(colour);

  TEST_ASSERT_TRUE(ap_graphics_decode_memory(0xFA0000u, &colour, &offset));
  TEST_ASSERT_FALSE(colour);
  TEST_ASSERT_TRUE(ap_graphics_decode_memory(0xFDFFFFu, &colour, &offset));
  TEST_ASSERT_FALSE(colour);

  /* And nothing either side of them. */
  TEST_ASSERT_FALSE(ap_graphics_decode_memory(0x09FFFFu, &colour, &offset));
  TEST_ASSERT_FALSE(ap_graphics_decode_memory(0x0C0000u, &colour, &offset));
  TEST_ASSERT_FALSE(ap_graphics_decode_memory(0xF9FFFFu, &colour, &offset));
  TEST_ASSERT_FALSE(ap_graphics_decode_memory(0xFE0000u, &colour, &offset));

  /* The register blocks are not memory and the memories are not registers. */
  TEST_ASSERT_FALSE(ap_graphics_decode_memory(0x05D800u, &colour, &offset));
  TEST_ASSERT_FALSE(ap_graphics_decode(0x0A0000u, &colour, &offset));
}

/* With no card fitted the memory reads `FF`, the same answer the register
 * blocks give and for the same reason: it is what an absent part reads. It is
 * deliberately not storage yet — memory that accepted writes and displayed
 * nothing would let a test pass that proves nothing about a screen. */
static void test_an_absent_card_s_graphics_memory_reads_ff(void) {
  ap_graphics_t g;
  ap_graphics_init(&g, AP_SCREEN_NONE);
  TEST_ASSERT_EQUAL_HEX8(0xFFu, ap_graphics_read(&g, 0x0A0000u));
  TEST_ASSERT_EQUAL_HEX8(0xFFu, ap_graphics_read(&g, 0xFA0000u));

  /* And a fitted one reads FF too, for now: the storage is the controller's
   * work, and answering anything else would claim a frame buffer exists. */
  ap_graphics_init(&g, AP_SCREEN_COLOUR_8_PLANE);
  TEST_ASSERT_EQUAL_HEX8(0xFFu, ap_graphics_read(&g, 0x0A0000u));
}


/* Storage, and nothing more. A write and a read back prove the memory works and
 * say **nothing** about a display -- which is the honest limit of this module
 * until the controller lands, and is asserted here so the round-trip cannot be
 * mistaken for a working screen. */
static void test_the_graphics_memory_stores_when_a_card_is_fitted(void) {
  static uint8_t colour[0x20000];
  static uint8_t mono[0x40000];
  ap_graphics_t g;
  ap_graphics_init(&g, AP_SCREEN_COLOUR_8_PLANE);
  ap_graphics_attach_memory(&g, colour, sizeof colour, mono, sizeof mono);

  ap_graphics_write(&g, 0x0A0000u, 0x5Au);
  ap_graphics_write(&g, 0x0BFFFFu, 0xA5u);
  TEST_ASSERT_EQUAL_HEX8(0x5Au, ap_graphics_read(&g, 0x0A0000u));
  TEST_ASSERT_EQUAL_HEX8(0xA5u, ap_graphics_read(&g, 0x0BFFFFu));

  /* The monochrome memory belongs to a monochrome card, and this is a colour
   * one: attaching storage does not fit a screen. */
  ap_graphics_write(&g, 0xFA0000u, 0x33u);
  TEST_ASSERT_EQUAL_HEX8(0xFFu, ap_graphics_read(&g, 0xFA0000u));
}

/* Three different ways for there to be nothing behind an address, all reading
 * `FF` because all three mean the same thing. Separated because it would be
 * easy to handle only the first and leave the others reading whatever the
 * pointer happened to be. */
static void test_every_way_of_having_no_memory_reads_ff(void) {
  static uint8_t small[16];
  ap_graphics_t g;

  /* No card of that family. */
  ap_graphics_init(&g, AP_SCREEN_NONE);
  ap_graphics_attach_memory(&g, small, sizeof small, small, sizeof small);
  ap_graphics_write(&g, 0x0A0000u, 0x5Au);
  TEST_ASSERT_EQUAL_HEX8(0xFFu, ap_graphics_read(&g, 0x0A0000u));

  /* Card fitted, no memory attached. */
  ap_graphics_init(&g, AP_SCREEN_COLOUR_4_PLANE);
  ap_graphics_write(&g, 0x0A0000u, 0x5Au);
  TEST_ASSERT_EQUAL_HEX8(0xFFu, ap_graphics_read(&g, 0x0A0000u));

  /* Card and memory, but past the end of what was attached. A write there must
   * not run off the buffer, which is why the bound is checked and not assumed
   * from the region size. */
  ap_graphics_init(&g, AP_SCREEN_COLOUR_4_PLANE);
  ap_graphics_attach_memory(&g, small, sizeof small, NULL, 0u);
  TEST_ASSERT_EQUAL_HEX8(0xFFu, ap_graphics_read(&g, 0x0A0000u + sizeof small));
  ap_graphics_write(&g, 0x0A0000u + sizeof small, 0x5Au);
  TEST_ASSERT_EQUAL_HEX8(0xFFu, ap_graphics_read(&g, 0x0A0000u + sizeof small));
  /* And the byte just inside still works, so the bound is the right one. */
  ap_graphics_write(&g, 0x0A0000u + sizeof small - 1u, 0x77u);
  TEST_ASSERT_EQUAL_HEX8(0x77u,
                         ap_graphics_read(&g, 0x0A0000u + sizeof small - 1u));
}


/* `CR0` bits 7-5 and `CR2` bits 7-6. A mode field read from the wrong bits is
 * the kind of defect that survives every test of the thing above it: the
 * blitter would run a real mode, just not the one asked for, and only a picture
 * would show it. So the bits are pinned here, before anything uses them. */
static void test_the_control_register_mode_fields_are_where_they_are(void) {
  /* Each mode in its own top-three-bits position, with the low bits set to
   * something so a decode that ignored the shift would be caught. */
  for (unsigned mode = 0; mode < 8u; mode++) {
    const uint8_t cr0 = (uint8_t)((mode << 5) | 0x1Fu);
    TEST_ASSERT_EQUAL_UINT(mode, (unsigned)ap_graphics_cr0_mode(cr0));
  }
  for (unsigned access = 0; access < 4u; access++) {
    const uint8_t cr2 = (uint8_t)((access << 6) | 0x3Fu);
    TEST_ASSERT_EQUAL_UINT(access, (unsigned)ap_graphics_cr2_access(cr2));
  }

  /* The named ones, so a renumbering cannot pass silently. */
  TEST_ASSERT_EQUAL_UINT(AP_GRAPHICS_CR0_NORMAL, ap_graphics_cr0_mode(0xE0u));
  TEST_ASSERT_EQUAL_UINT(AP_GRAPHICS_CR0_VECTOR, ap_graphics_cr0_mode(0x40u));
  TEST_ASSERT_EQUAL_UINT(AP_GRAPHICS_CR2_PLANE_ACCESS,
                         ap_graphics_cr2_access(0xC0u));
}

/* The oracle's own source lists CR0 modes 5 and 6, and CR2 access 2, as `???`.
 * That is the state of the knowledge rather than a gap in the transcription, so
 * they must read as unknown and not as a plausible label -- a guess here would
 * be indistinguishable from a fact until firmware exercised it. */
static void test_the_unknown_modes_say_they_are_unknown(void) {
  TEST_ASSERT_NOT_NULL(
      strstr(ap_graphics_cr0_mode_name(AP_GRAPHICS_CR0_UNKNOWN_5), "unknown"));
  TEST_ASSERT_NOT_NULL(
      strstr(ap_graphics_cr0_mode_name(AP_GRAPHICS_CR0_UNKNOWN_6), "unknown"));
  TEST_ASSERT_NOT_NULL(strstr(
      ap_graphics_cr2_access_name(AP_GRAPHICS_CR2_UNKNOWN_2), "unknown"));

  /* And every mode has a name, so a trace cannot print a blank at the moment it
   * matters. */
  for (unsigned mode = 0; mode < 8u; mode++) {
    const char *name = ap_graphics_cr0_mode_name((ap_graphics_cr0_mode_t)mode);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_TRUE(strlen(name) > 0u);
  }
}


/* `CR0` carries two fields, and modelling only the mode leaves the other
 * reading as part of neither. Bits 4-0 are a shift count, and the two must not
 * bleed into each other: a mode decode that forgot to shift would pick up the
 * shift bits, and a shift that forgot to mask would pick up the mode. */
static void test_cr0_carries_a_mode_and_a_shift_that_do_not_overlap(void) {
  for (unsigned mode = 0; mode < 8u; mode++) {
    for (unsigned shift = 0; shift < 32u; shift += 7u) {
      const uint8_t cr0 = (uint8_t)((mode << 5) | shift);
      TEST_ASSERT_EQUAL_UINT(mode, (unsigned)ap_graphics_cr0_mode(cr0));
      TEST_ASSERT_EQUAL_UINT(shift, ap_graphics_cr0_shift(cr0));
    }
  }
  /* Every bit accounted for: mode and shift together are the whole byte. */
  TEST_ASSERT_EQUAL_UINT(0xFFu,
                         (unsigned)((7u << 5) | AP_GRAPHICS_CR0_SHIFT_MASK));
}

/* `CR1`'s top two bits mean different things per family: INV and DADDR_16 on a
 * monochrome controller, AD_BIT and DV_CK on a colour one. Named per family
 * rather than given one set of names with a comment, because a single name
 * would be silently wrong on half the machines -- and wrong in the direction
 * that still runs, since the bit would be read, believed, and mean something
 * else entirely. */
static void test_cr1_s_top_bits_are_named_per_family(void) {
  /* Same positions, different meanings -- which is the point, and is why the
   * names must differ even though the values do not. */
  TEST_ASSERT_EQUAL_HEX8(AP_GRAPHICS_CR1_MONO_INV, AP_GRAPHICS_CR1_COLOUR_AD_BIT);
  TEST_ASSERT_EQUAL_HEX8(AP_GRAPHICS_CR1_MONO_DADDR_16,
                         AP_GRAPHICS_CR1_COLOUR_DV_CK);

  /* The lower six are common to both, and together with the top two they
   * account for every bit of the register. */
  const unsigned common = AP_GRAPHICS_CR1_DH_CK | AP_GRAPHICS_CR1_ROP_EN |
                          AP_GRAPHICS_CR1_RESET | AP_GRAPHICS_CR1_DP_CK |
                          AP_GRAPHICS_CR1_SYNC_EN | AP_GRAPHICS_CR1_DISP_EN;
  TEST_ASSERT_EQUAL_HEX8(0x3Fu, common);
  TEST_ASSERT_EQUAL_HEX8(
      0xFFu, common | AP_GRAPHICS_CR1_MONO_INV | AP_GRAPHICS_CR1_MONO_DADDR_16);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_each_screen_reports_the_id_the_firmware_compares_against);
  RUN_TEST(test_the_other_family_s_block_reads_ff);
  RUN_TEST(test_an_absent_screen_still_decodes_and_reads_ff);
  RUN_TEST(test_the_blocks_are_the_ranges_the_map_gives_them);
  RUN_TEST(test_the_graphics_memories_decode);
  RUN_TEST(test_an_absent_card_s_graphics_memory_reads_ff);
  RUN_TEST(test_the_graphics_memory_stores_when_a_card_is_fitted);
  RUN_TEST(test_every_way_of_having_no_memory_reads_ff);
  RUN_TEST(test_the_control_register_mode_fields_are_where_they_are);
  RUN_TEST(test_the_unknown_modes_say_they_are_unknown);
  RUN_TEST(test_cr0_carries_a_mode_and_a_shift_that_do_not_overlap);
  RUN_TEST(test_cr1_s_top_bits_are_named_per_family);
  RUN_TEST(test_an_unmodelled_register_reads_ff_and_not_zero);
  RUN_TEST(test_a_write_is_absorbed_and_does_not_change_the_id);
  return UNITY_END();
}
