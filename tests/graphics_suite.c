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

/* A register this core still does not model reads `FF` rather than zero. Zero
 * is a value several of these registers can legitimately hold, so a driver
 * reading it would take an unmodelled register for a real one reporting a real
 * state. `FF` is what an absent part reads.
 *
 * The list shrank twice and the test was narrowed rather than deleted each
 * time. What is left: offset 0 is the **status** register, whose bits are the
 * raster -- `FINDINGS.md` C112, and the reason a `--screen c8p` boot polls
 * forever; offset 2 is the raster operation's low half, write-only on every
 * board; and offset 6 is a diagnostic memory-refresh trigger. `403` left the
 * list when the lookup table was wired. */
static void test_an_unmodelled_register_reads_ff_and_not_zero(void) {
  ap_graphics_t g;
  ap_graphics_init(&g, AP_SCREEN_COLOUR_8_PLANE);

  TEST_ASSERT_EQUAL_HEX8(0xFFu, ap_graphics_read(&g, AP_GRAPHICS_COLOUR_ADDR));
  TEST_ASSERT_EQUAL_HEX8(0xFFu,
                         ap_graphics_read(&g, AP_GRAPHICS_COLOUR_ADDR + 2u));
  TEST_ASSERT_EQUAL_HEX8(0xFFu,
                         ap_graphics_read(&g, AP_GRAPHICS_COLOUR_ADDR + 6u));
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
  /* `CR2`'s access 2 was unknown here and is not any more: the oracle names it
   * `CR2_SHIFT_ACCESS`, which makes all four of `CR2`'s values accounted for
   * while `CR0` still has two that nothing names. */
  TEST_ASSERT_EQUAL_STRING(
      "shift access", ap_graphics_cr2_access_name(AP_GRAPHICS_CR2_SHIFT_ACCESS));

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

/* ---- The raster operation ------------------------------------------------- */

/* All sixteen boolean functions of source and destination, checked against
 * their own identities rather than against a table of expected numbers -- a
 * table would just be the implementation written twice. */
static void test_every_raster_op_is_its_own_boolean_function(void) {
  const uint16_t s = 0xF0F0u;
  const uint16_t d = 0xFF00u;
  const uint8_t on = AP_GRAPHICS_CR1_ROP_EN;

  /* The op is widened explicitly. A `ap_graphics_rop_t` is a four-bit function
   * code and the parameter is the whole 32-bit register, so this is plane 0's
   * nibble and nothing else -- and under the MSVC ABI an enum is signed `int`,
   * which makes the implicit widening a signedness change that `-Wsign-conversion`
   * refuses. It compiled on Linux, where clang chose an unsigned underlying
   * type, and failed only on Windows. */
  struct { ap_graphics_rop_t op; uint16_t want; } cases[] = {
      {AP_GRAPHICS_ROP_ZERO, 0u},
      {AP_GRAPHICS_ROP_SRC_AND_DST, (uint16_t)(s & d)},
      {AP_GRAPHICS_ROP_SRC_AND_NOT_DST, (uint16_t)(s & (uint16_t)~d)},
      {AP_GRAPHICS_ROP_SRC, s},
      {AP_GRAPHICS_ROP_NOT_SRC_AND_DST, (uint16_t)((uint16_t)~s & d)},
      {AP_GRAPHICS_ROP_DST, d},
      {AP_GRAPHICS_ROP_SRC_XOR_DST, (uint16_t)(s ^ d)},
      {AP_GRAPHICS_ROP_SRC_OR_DST, (uint16_t)(s | d)},
      {AP_GRAPHICS_ROP_SRC_NOR_DST, (uint16_t)~(uint16_t)(s | d)},
      {AP_GRAPHICS_ROP_SRC_XNOR_DST, (uint16_t)~(uint16_t)(s ^ d)},
      {AP_GRAPHICS_ROP_NOT_DST, (uint16_t)~d},
      {AP_GRAPHICS_ROP_SRC_OR_NOT_DST, (uint16_t)(s | (uint16_t)~d)},
      {AP_GRAPHICS_ROP_NOT_SRC, (uint16_t)~s},
      {AP_GRAPHICS_ROP_NOT_SRC_OR_DST, (uint16_t)((uint16_t)~s | d)},
      {AP_GRAPHICS_ROP_SRC_NAND_DST, (uint16_t)~(uint16_t)(s & d)},
      {AP_GRAPHICS_ROP_ONE, 0xFFFFu},
  };
  for (unsigned i = 0; i < 16u; i++) {
    TEST_ASSERT_EQUAL_HEX16(cases[i].want,
                            ap_graphics_rop_apply(on, (uint32_t)cases[i].op, 0u, s, d));
  }
}

/* The two that a driver uses most, and that an off-by-one decode confuses with
 * their neighbours: `0011` copies the source and `0101` keeps the destination,
 * so one draws and the other does nothing at all. Both would "work" if the
 * field were misread -- a blit that ANDed instead of copying still puts pixels
 * on a screen. */
static void test_source_copies_and_destination_writes_nothing(void) {
  const uint8_t on = AP_GRAPHICS_CR1_ROP_EN;
  TEST_ASSERT_EQUAL_HEX16(
      0x1234u, ap_graphics_rop_apply(on, (uint32_t)AP_GRAPHICS_ROP_SRC, 0u, 0x1234u, 0x5678u));
  TEST_ASSERT_EQUAL_HEX16(
      0x5678u, ap_graphics_rop_apply(on, (uint32_t)AP_GRAPHICS_ROP_DST, 0u, 0x1234u, 0x5678u));
  TEST_ASSERT_EQUAL_UINT(3u, AP_GRAPHICS_ROP_SRC);
  TEST_ASSERT_EQUAL_UINT(5u, AP_GRAPHICS_ROP_DST);
}

/* `CR1`'s `ROP_EN` gates the register entirely. A driver that programmed an
 * operation and forgot the enable gets a plain copy -- not the operation, and
 * not nothing. */
static void test_a_disabled_rop_passes_the_source_through(void) {
  TEST_ASSERT_EQUAL_HEX16(
      0x1234u, ap_graphics_rop_apply(0u, (uint32_t)AP_GRAPHICS_ROP_ZERO, 0u, 0x1234u, 0x5678u));
  TEST_ASSERT_EQUAL_HEX16(
      0x1234u, ap_graphics_rop_apply(0u, (uint32_t)AP_GRAPHICS_ROP_ONE, 0u, 0x1234u, 0x5678u));
}

/* "ROP Register specifiers increased to 32 bits" -- eight planes of four, low
 * plane first, so each plane can run a different operation in one blit. */
static void test_each_plane_selects_its_own_operation(void) {
  /* Plane 0 = SRC (3), plane 1 = ZERO (0), plane 7 = ONE (F). */
  const uint32_t reg = 0xF0000003u;
  TEST_ASSERT_EQUAL_UINT(AP_GRAPHICS_ROP_SRC, ap_graphics_rop_for(reg, 0u));
  TEST_ASSERT_EQUAL_UINT(AP_GRAPHICS_ROP_ZERO, ap_graphics_rop_for(reg, 1u));
  TEST_ASSERT_EQUAL_UINT(AP_GRAPHICS_ROP_ONE, ap_graphics_rop_for(reg, 7u));

  const uint8_t on = AP_GRAPHICS_CR1_ROP_EN;
  TEST_ASSERT_EQUAL_HEX16(0xAAAAu, ap_graphics_rop_apply(on, reg, 0u, 0xAAAAu, 0x5555u));
  TEST_ASSERT_EQUAL_HEX16(0x0000u, ap_graphics_rop_apply(on, reg, 1u, 0xAAAAu, 0x5555u));
  TEST_ASSERT_EQUAL_HEX16(0xFFFFu, ap_graphics_rop_apply(on, reg, 7u, 0xAAAAu, 0x5555u));

  /* The register holds eight, so a ninth plane selects nothing. */
  TEST_ASSERT_EQUAL_UINT(AP_GRAPHICS_ROP_ZERO, ap_graphics_rop_for(reg, 8u));
}

/* §10.3's change list: D_PLANE went to 8 bits and S_PLANE to 3 on the 8-plane
 * board. The same `CR2` byte therefore means different things on the two
 * families, which is the same trap `CR1`'s top two bits carry. */
static void test_the_plane_selects_are_wider_on_the_eight_plane_board(void) {
  const uint8_t cr2 = 0xB5u; /* 1011 0101 */

  /* 4-plane: source is bits 5-4, destination bits 3-0. */
  TEST_ASSERT_EQUAL_UINT(0x3u, ap_graphics_cr2_source_plane(cr2, false));
  TEST_ASSERT_EQUAL_UINT(0x5u, ap_graphics_cr2_dest_plane(cr2, false));

  /* 8-plane: source is three bits, destination the whole byte. */
  TEST_ASSERT_EQUAL_UINT(0x5u, ap_graphics_cr2_source_plane(cr2, true));
  TEST_ASSERT_EQUAL_UINT(0xB5u, ap_graphics_cr2_dest_plane(cr2, true));
}

/* ---- The blitter's word-level data path ----------------------------------- */

/* **A destination plane is written when its `D_PLANE` bit is zero.** A model
 * reading a set bit as "write this plane" draws into exactly the planes it
 * should have left alone — on a monochrome screen, an image and its negative,
 * which looks like a polarity bug anywhere else in the pipeline. */
static void test_a_destination_plane_is_selected_by_a_zero_bit(void) {
  /* `FE` masks out every plane but 0. */
  TEST_ASSERT_TRUE(ap_graphics_plane_selected(0xFEu, 0u));
  for (unsigned p = 1; p < 8u; p++) {
    TEST_ASSERT_FALSE(ap_graphics_plane_selected(0xFEu, p));
  }
  /* All zero writes every plane, all ones writes none — which is the reading a
   * set-selects model gets exactly backwards. */
  for (unsigned p = 0; p < 8u; p++) {
    TEST_ASSERT_TRUE(ap_graphics_plane_selected(0x00u, p));
    TEST_ASSERT_FALSE(ap_graphics_plane_selected(0xFFu, p));
  }
}

/* The write enable register runs the same way inside a word: a bit **set**
 * protects the destination. A register called "write enable" that enables
 * writing where it is zero is the kind of name that survives careful reading. */
static void test_a_set_write_enable_bit_protects_the_destination(void) {
  const uint16_t src = 0xFFFFu;
  const uint16_t dst = 0x0000u;

  /* Nothing protected: the source lands whole. */
  TEST_ASSERT_EQUAL_HEX16(0xFFFFu, ap_graphics_combine(0x0000u, 0xFFFFu, src, dst));
  /* Everything protected: the destination survives whole. */
  TEST_ASSERT_EQUAL_HEX16(0x0000u, ap_graphics_combine(0xFFFFu, 0xFFFFu, src, dst));
  /* The high byte protected, the low byte written. */
  TEST_ASSERT_EQUAL_HEX16(0x00FFu, ap_graphics_combine(0xFF00u, 0xFFFFu, src, dst));
}

/* The bus's byte mask protects too, and independently: a byte the cycle does
 * not cover is not written however the write enable register is programmed. */
static void test_the_bus_mask_protects_whatever_the_register_says(void) {
  TEST_ASSERT_EQUAL_HEX16(
      0x00FFu, ap_graphics_combine(0x0000u, 0x00FFu, 0xFFFFu, 0x0000u));
  /* Protection is a union, and overlapping is **idempotent**: a register
   * protecting the high byte and a cycle that also only covers the low byte
   * still leave the low byte written. Both guarding the same half does not
   * guard the other half too, which is the reading that makes the two masks
   * look like they multiply. */
  TEST_ASSERT_EQUAL_HEX16(
      0x00FFu, ap_graphics_combine(0xFF00u, 0x00FFu, 0xFFFFu, 0x0000u));

  /* Guarding *different* halves is what protects everything. */
  TEST_ASSERT_EQUAL_HEX16(
      0x0000u, ap_graphics_combine(0x00FFu, 0x00FFu, 0xFFFFu, 0x0000u));
}

/* `CR2[7:6]`'s four access modes are four different ways of turning what was
 * read into what the raster operation sees. Only `PLANE` is a copy. */
static void test_the_access_modes_shape_the_source_word(void) {
  /* Constant: all ones whatever was read, "used for vectors". */
  TEST_ASSERT_EQUAL_HEX16(0xFFFFu,
                          ap_graphics_source_data(0u, AP_GRAPHICS_CR2_CONSTANT_ACCESS,
                                                  0u, 0x0000u));

  /* Pixel: the plane's own bit, replicated across the word. */
  TEST_ASSERT_EQUAL_HEX16(0xFFFFu,
                          ap_graphics_source_data(0u, AP_GRAPHICS_CR2_PIXEL_ACCESS,
                                                  2u, 0x0004u));
  TEST_ASSERT_EQUAL_HEX16(0x0000u,
                          ap_graphics_source_data(0u, AP_GRAPHICS_CR2_PIXEL_ACCESS,
                                                  1u, 0x0004u));

  /* Shift: the shifter's least significant bit, replicated. */
  TEST_ASSERT_EQUAL_HEX16(0xFFFFu,
                          ap_graphics_source_data(0u, AP_GRAPHICS_CR2_SHIFT_ACCESS,
                                                  7u, 0x0001u));
  TEST_ASSERT_EQUAL_HEX16(0x0000u,
                          ap_graphics_source_data(0u, AP_GRAPHICS_CR2_SHIFT_ACCESS,
                                                  7u, 0x0002u));

  /* Plane: the word itself, with no shift programmed. */
  TEST_ASSERT_EQUAL_HEX16(0x1234u,
                          ap_graphics_source_data(0u, AP_GRAPHICS_CR2_PLANE_ACCESS,
                                                  0u, 0x1234u));
}

/* `CR0`'s shift count applies to plane access, and a count of 16 or more
 * rotates the halves first — so the field reaches across the word instead of
 * shifting everything out of it. A model that just shifted would return zero
 * for every count past 15. */
static void test_a_shift_of_sixteen_or_more_rotates_before_shifting(void) {
  /* The guard latch holds the **previous** word above the current one. That
   * width is the whole reason it exists: a shifted blit takes its leading bits
   * from the word before, which is what draws a bitmap that does not begin on a
   * word boundary -- almost any text. `$AAAA` above, `$1234` below. */
  const uint32_t latch = 0xAAAA1234u;

  /* Four: an ordinary shift, and the four bits arriving at the top are the
   * previous word's bottom four -- `$A` -- not zeroes. A sixteen-bit latch
   * gives `$0123` here and the picture keeps a blank sliver at the leading edge
   * of every word. */
  TEST_ASSERT_EQUAL_HEX16(0xA123u,
                          ap_graphics_source_data(4u, AP_GRAPHICS_CR2_PLANE_ACCESS,
                                                  0u, latch));
  /* Sixteen: the halves swap and the low nibble of the count is zero, so what
   * comes back is the previous word entire. */
  TEST_ASSERT_EQUAL_HEX16(0xAAAAu,
                          ap_graphics_source_data(16u, AP_GRAPHICS_CR2_PLANE_ACCESS,
                                                  0u, latch));
  /* Twenty: rotate to `$1234AAAA`, then shift by four -- the previous word's
   * top three nibbles with the current word's *lowest* nibble arriving above
   * them. */
  TEST_ASSERT_EQUAL_HEX16(0x4AAAu,
                          ap_graphics_source_data(20u, AP_GRAPHICS_CR2_PLANE_ACCESS,
                                                  0u, latch));
  /* And a latch with nothing above it still shifts zeroes in, which is the
   * state at the start of a run of words rather than a special case. */
  TEST_ASSERT_EQUAL_HEX16(0x0123u,
                          ap_graphics_source_data(4u, AP_GRAPHICS_CR2_PLANE_ACCESS,
                                                  0u, 0x1234u));
}

/* The four steps in order, as a blit does them: shape the source, combine it
 * with the destination through the plane's operation, then merge under the
 * write enable. Assembled here because each part is tested alone above and the
 * *order* is the thing a blitter gets wrong. */
static void test_the_data_path_runs_source_then_rop_then_write_enable(void) {
  const uint8_t cr0 = 0u;
  const uint8_t cr1 = AP_GRAPHICS_CR1_ROP_EN;
  const uint32_t rop = (uint32_t)AP_GRAPHICS_ROP_SRC_XOR_DST; /* plane 0 */
  const uint16_t destination = 0xFF00u;

  const uint16_t source =
      ap_graphics_source_data(cr0, AP_GRAPHICS_CR2_CONSTANT_ACCESS, 0u, 0u);
  TEST_ASSERT_EQUAL_HEX16(0xFFFFu, source);

  const uint16_t combined =
      ap_graphics_rop_apply(cr1, rop, 0u, source, destination);
  TEST_ASSERT_EQUAL_HEX16(0x00FFu, combined); /* FFFF ^ FF00 */

  /* Write only the low byte. */
  TEST_ASSERT_EQUAL_HEX16(
      0xFF00u | 0x00FFu,
      ap_graphics_combine(0xFF00u, 0xFFFFu, combined, destination));
}

/* ---- A blit, the plane loop around all of it ------------------------------- */

/* One buffer, bytes, big-endian -- the *board's* memory, which the blitter and
 * the scanout now share. It used to be a host-order `uint16_t` array here and a
 * byte array there, and the end-to-end test had to serialise between them by
 * hand. One memory is what the hardware has. */
static uint8_t image[128];

static uint16_t img(uint32_t word) {
  return (uint16_t)(((uint16_t)image[word * 2u] << 8) | image[word * 2u + 1u]);
}

static void put_img(uint32_t word, uint16_t value) {
  image[word * 2u] = (uint8_t)(value >> 8);
  image[word * 2u + 1u] = (uint8_t)value;
}
static uint32_t latched[8];

static ap_graphics_blit_t plain_blit(void) {
  ap_graphics_blit_t b = {
      .cr0 = 0u,
      .cr1 = AP_GRAPHICS_CR1_ROP_EN,
      .access = AP_GRAPHICS_CR2_PLANE_ACCESS,
      .rop_register = 0x33333333u, /* SRC on every plane */
      .write_enable = 0x0000u,     /* nothing protected */
      .d_plane = 0x00u,            /* zero selects: every plane */
      .s_plane = 0u,
      .planes = 4u,
      .plane_stride = 8u,
  };
  return b;
}

/* The ordinary case: a word copied into every plane, each at its own stride. */
static void test_a_blit_writes_one_word_per_plane_at_its_stride(void) {
  memset(image, 0, sizeof image);
  for (unsigned p = 0; p < 8u; p++) {
    latched[p] = (uint16_t)(0x1000u + p);
  }
  const ap_graphics_blit_t b = plain_blit();

  TEST_ASSERT_EQUAL_UINT(4u, ap_graphics_blit(&b, image, sizeof image, 2u, 0xFFFFu, latched));
  TEST_ASSERT_EQUAL_HEX16(0x1000u, img(2u));
  TEST_ASSERT_EQUAL_HEX16(0x1001u, img(2u + 8u));
  TEST_ASSERT_EQUAL_HEX16(0x1002u, img(2u + 16u));
  TEST_ASSERT_EQUAL_HEX16(0x1003u, img(2u + 24u));
  /* And nothing between the planes was touched. */
  TEST_ASSERT_EQUAL_HEX16(0u, img(3u));
}

/* `D_PLANE` masks planes out, and the address still advances for the ones it
 * skips. Advancing only on a write would pack the written planes together and
 * put every one after the first in the wrong plane -- a blit that draws the
 * right shape in the wrong place, which is far harder to see than one that
 * draws nothing. */
static void test_a_masked_plane_is_skipped_without_moving_the_others(void) {
  memset(image, 0, sizeof image);
  memset(latched, 0x77, sizeof latched);
  ap_graphics_blit_t b = plain_blit();
  b.d_plane = 0x05u; /* bits 0 and 2 set: planes 0 and 2 masked out */

  TEST_ASSERT_EQUAL_UINT(2u, ap_graphics_blit(&b, image, sizeof image, 0u, 0xFFFFu, latched));
  TEST_ASSERT_EQUAL_HEX16(0x0000u, img(0u));       /* plane 0 masked */
  TEST_ASSERT_EQUAL_HEX16(0x7777u, img(8u));       /* plane 1 written */
  TEST_ASSERT_EQUAL_HEX16(0x0000u, img(16u));      /* plane 2 masked */
  TEST_ASSERT_EQUAL_HEX16(0x7777u, img(24u));      /* plane 3 written */
}

/* A destination past the memory is skipped, not wrapped. A blit that ran off
 * the end and reappeared at the top would draw a second, wrong image somewhere
 * nobody asked about. */
static void test_a_plane_past_the_memory_is_skipped_not_wrapped(void) {
  memset(image, 0, sizeof image);
  memset(latched, 0x11, sizeof latched);
  const ap_graphics_blit_t b = plain_blit();

  /* Plane 0 at word 60 fits; planes 1-3 are past the end of 64 words. */
  TEST_ASSERT_EQUAL_UINT(1u, ap_graphics_blit(&b, image, sizeof image, 60u, 0xFFFFu, latched));
  TEST_ASSERT_EQUAL_HEX16(0x1111u, img(60u));
  /* Nothing wrapped to the start. */
  TEST_ASSERT_EQUAL_HEX16(0x0000u, img(0u));
  TEST_ASSERT_EQUAL_HEX16(0x0000u, img(4u));
}

/* With `AD_BIT` set every plane takes the *source plane's* word, which is how
 * one source is broadcast to many destinations. Indexing by the destination
 * plane instead draws the right shape in the wrong colours. */
static void test_the_ad_bit_broadcasts_one_source_to_every_plane(void) {
  memset(image, 0, sizeof image);
  for (unsigned p = 0; p < 8u; p++) {
    latched[p] = (uint16_t)(0xA000u + p);
  }
  ap_graphics_blit_t b = plain_blit();
  b.cr1 |= AP_GRAPHICS_CR1_COLOUR_AD_BIT;
  b.s_plane = 2u;

  TEST_ASSERT_EQUAL_UINT(4u, ap_graphics_blit(&b, image, sizeof image, 0u, 0xFFFFu, latched));
  for (unsigned p = 0; p < 4u; p++) {
    TEST_ASSERT_EQUAL_HEX16(0xA002u, img(p * 8u));
  }
}

/* The raster operation runs per plane, so one blit can combine differently in
 * each -- which is the whole reason the ROP register is 32 bits wide. */
static void test_each_plane_combines_by_its_own_operation(void) {
  memset(image, 0, sizeof image);
  for (unsigned p = 0; p < 4u; p++) {
    put_img(p * 8u, 0xFF00u);
  }
  memset(latched, 0x0F, sizeof latched); /* 0x0F0F */

  ap_graphics_blit_t b = plain_blit();
  /* plane 0 SRC, plane 1 DST, plane 2 XOR, plane 3 ZERO */
  b.rop_register = 0x0653u;

  TEST_ASSERT_EQUAL_UINT(4u, ap_graphics_blit(&b, image, sizeof image, 0u, 0xFFFFu, latched));
  TEST_ASSERT_EQUAL_HEX16(0x0F0Fu, img(0u));            /* source */
  TEST_ASSERT_EQUAL_HEX16(0xFF00u, img(8u));            /* destination kept */
  TEST_ASSERT_EQUAL_HEX16(0x0F0Fu ^ 0xFF00u, img(16u)); /* xor */
  TEST_ASSERT_EQUAL_HEX16(0x0000u, img(24u));           /* zero */
}

/* The write enable protects within the word, across every plane at once. */
static void test_the_write_enable_applies_to_every_plane(void) {
  memset(image, 0, sizeof image);
  memset(latched, 0xFF, sizeof latched);
  ap_graphics_blit_t b = plain_blit();
  b.write_enable = 0xFF00u; /* high byte protected */

  TEST_ASSERT_EQUAL_UINT(4u, ap_graphics_blit(&b, image, sizeof image, 0u, 0xFFFFu, latched));
  for (unsigned p = 0; p < 4u; p++) {
    TEST_ASSERT_EQUAL_HEX16(0x00FFu, img(p * 8u));
  }
}

/* ## Scanout
 *
 * The geometries are `008778-03`'s, and the *buffer* widths -- the ones that
 * look like an implementation detail -- are its printed memory capacities
 * divided out: 128 KB a plane is 1024 x 1024 bits, and the 1280 x 1024
 * monochrome's 256 KB is 2048 x 1024. Asserting them here is asserting the
 * manual, not the code.
 */
static void test_each_screen_s_memory_is_the_capacity_the_manual_prints(void) {
  const struct {
    ap_screen_kind_t screen;
    unsigned planes, width, height, buffer_width;
    uint32_t plane_bytes;
  } cases[] = {
      /* "512 KB of image memory arranged in four 128-KB planes" */
      {AP_SCREEN_COLOUR_4_PLANE, 4u, 1024u, 800u, 1024u, 128u * 1024u},
      /* "Dual-port, 1-MB image memory", eight planes */
      {AP_SCREEN_COLOUR_8_PLANE, 8u, 1024u, 800u, 1024u, 128u * 1024u},
      /* "256-KB image memory", one plane */
      {AP_SCREEN_MONO_19_INCH, 1u, 1280u, 1024u, 2048u, 256u * 1024u},
      /* The oracle's; this board is not in the manual. */
      {AP_SCREEN_MONO_15_INCH, 1u, 1024u, 800u, 1024u, 128u * 1024u},
  };
  for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    ap_graphics_geometry_t g;
    TEST_ASSERT_TRUE(ap_graphics_geometry(cases[i].screen, &g));
    TEST_ASSERT_EQUAL_UINT(cases[i].planes, g.planes);
    TEST_ASSERT_EQUAL_UINT(cases[i].width, g.width);
    TEST_ASSERT_EQUAL_UINT(cases[i].height, g.height);
    TEST_ASSERT_EQUAL_UINT(cases[i].buffer_width, g.buffer_width);
    /* The whole point of the buffer width: a plane is the manual's capacity. */
    TEST_ASSERT_EQUAL_UINT32(cases[i].plane_bytes, g.plane_words * 2u);
  }
}

/* `AP_SCREEN_NONE` has no geometry rather than a zero one, so a caller cannot
 * scan out an absent card and get a black screen back -- which would be
 * indistinguishable from a fitted card showing nothing. */
static void test_an_absent_screen_has_no_geometry_at_all(void) {
  ap_graphics_geometry_t g;
  TEST_ASSERT_FALSE(ap_graphics_geometry(AP_SCREEN_NONE, &g));
}

/* A DN3500's 8-plane memory is 1 MB; enough for the tests below without
 * standing one on the stack. */
static uint8_t scanout_memory[8u * 128u * 1024u];
static uint8_t scanout_pixels[1280u * 1024u];

static void put_word(uint32_t word_index, uint16_t value) {
  scanout_memory[word_index * 2u] = (uint8_t)(value >> 8);
  scanout_memory[word_index * 2u + 1u] = (uint8_t)value;
}

static void scanout_setup(ap_graphics_t *g, ap_screen_kind_t screen) {
  memset(scanout_memory, 0, sizeof scanout_memory);
  ap_graphics_init(g, screen);
  if (ap_graphics_is_colour(screen)) {
    ap_graphics_attach_memory(g, scanout_memory, sizeof scanout_memory,
                              nullptr, 0u);
  } else {
    ap_graphics_attach_memory(g, nullptr, 0u, scanout_memory,
                              sizeof scanout_memory);
  }
}

/* Bit 15 of a word is the **leftmost** pixel. A shift-right loop starting at
 * bit 0 mirrors every sixteen-pixel group, which survives a glance at a
 * thumbnail and is wrong everywhere. */
static void test_the_high_bit_of_a_word_is_the_leftmost_pixel(void) {
  ap_graphics_t g;
  scanout_setup(&g, AP_SCREEN_MONO_15_INCH);
  put_word(0u, 0x8000u);

  TEST_ASSERT_EQUAL_UINT32(1024u * 800u,
                           ap_graphics_scanout(&g, AP_GRAPHICS_CR1_DISP_EN,
                                               scanout_pixels,
                                               sizeof scanout_pixels));
  TEST_ASSERT_EQUAL_UINT8(1u, scanout_pixels[0]);
  TEST_ASSERT_EQUAL_UINT8(0u, scanout_pixels[1]);
  TEST_ASSERT_EQUAL_UINT8(0u, scanout_pixels[15]);
}

/* The stride is the *buffer* width, not the visible one. On a 19-inch the two
 * differ by 768 pixels, so a model using the visible width puts the second row
 * 48 words early -- a shear that grows down the screen and reads as a timing
 * fault rather than an arithmetic one. */
static void test_a_row_is_the_buffer_s_width_apart_not_the_screen_s(void) {
  ap_graphics_t g;
  scanout_setup(&g, AP_SCREEN_MONO_19_INCH);
  /* First pixel of row 1, which is 2048/16 = 128 words in. */
  put_word(128u, 0x8000u);
  /* Where a visible-width stride of 1280/16 = 80 words would have looked. */
  put_word(80u, 0x4000u);

  TEST_ASSERT_EQUAL_UINT32(1280u * 1024u,
                           ap_graphics_scanout(&g, AP_GRAPHICS_CR1_DISP_EN,
                                               scanout_pixels,
                                               sizeof scanout_pixels));
  TEST_ASSERT_EQUAL_UINT8(1u, scanout_pixels[1280u]);
  /* And the word beyond the visible width is not displayed at all. */
  TEST_ASSERT_EQUAL_UINT8(0u, scanout_pixels[1281u]);
}

/* Plane 0 is bit 0 of the index. Reversed, the palette is read backwards and
 * every colour is wrong while the shapes stay right. */
static void test_plane_zero_is_the_least_significant_bit_of_the_index(void) {
  ap_graphics_t g;
  scanout_setup(&g, AP_SCREEN_COLOUR_8_PLANE);
  ap_graphics_geometry_t geometry;
  TEST_ASSERT_TRUE(ap_graphics_geometry(AP_SCREEN_COLOUR_8_PLANE, &geometry));

  /* Planes 0 and 7 set in the first pixel: index $81, which is asymmetric so a
   * reversal cannot pass. */
  put_word(0u, 0x8000u);
  put_word(7u * geometry.plane_words, 0x8000u);

  TEST_ASSERT_EQUAL_UINT32(1024u * 800u,
                           ap_graphics_scanout(&g, AP_GRAPHICS_CR1_DISP_EN,
                                               scanout_pixels,
                                               sizeof scanout_pixels));
  TEST_ASSERT_EQUAL_UINT8(0x81u, scanout_pixels[0]);
}

/* Monochrome `INV` is a *monochrome* bit. The same position on a colour
 * controller is `AD_BIT`, which the blitter uses to broadcast one source to
 * every plane -- so a scanout that honoured it there would blank a colour
 * screen whenever the driver had asked for a broadcast blit. */
static void test_inv_inverts_a_mono_screen_and_is_ad_bit_on_a_colour_one(void) {
  ap_graphics_t mono;
  scanout_setup(&mono, AP_SCREEN_MONO_15_INCH);
  const uint8_t inv = AP_GRAPHICS_CR1_DISP_EN | AP_GRAPHICS_CR1_MONO_INV;
  TEST_ASSERT_EQUAL_UINT32(1024u * 800u,
                           ap_graphics_scanout(&mono, inv, scanout_pixels,
                                               sizeof scanout_pixels));
  TEST_ASSERT_EQUAL_UINT8(1u, scanout_pixels[0]);

  ap_graphics_t colour;
  scanout_setup(&colour, AP_SCREEN_COLOUR_8_PLANE);
  /* The identical bit, on a colour card, where it is `AD_BIT`. */
  TEST_ASSERT_EQUAL_UINT32(1024u * 800u,
                           ap_graphics_scanout(&colour, inv, scanout_pixels,
                                               sizeof scanout_pixels));
  TEST_ASSERT_EQUAL_UINT8(0u, scanout_pixels[0]);
}

/* Every way of not being able to scan out reports zero rather than a partly
 * filled buffer, because a caller that ignored the count would otherwise
 * encode whatever the buffer happened to hold. */
static void test_a_scanout_that_cannot_run_writes_nothing(void) {
  ap_graphics_t g;
  scanout_setup(&g, AP_SCREEN_COLOUR_8_PLANE);

  /* A buffer too small for the picture. */
  TEST_ASSERT_EQUAL_UINT32(0u, ap_graphics_scanout(&g, AP_GRAPHICS_CR1_DISP_EN,
                                                   scanout_pixels, 1024u));
  /* No memory attached: a fitted card whose image memory is not there. */
  ap_graphics_t bare;
  ap_graphics_init(&bare, AP_SCREEN_COLOUR_8_PLANE);
  TEST_ASSERT_EQUAL_UINT32(0u, ap_graphics_scanout(&bare,
                                                   AP_GRAPHICS_CR1_DISP_EN,
                                                   scanout_pixels,
                                                   sizeof scanout_pixels));
  /* Memory too small for the eight planes the geometry needs. */
  ap_graphics_t   short_memory;
  ap_graphics_init(&short_memory, AP_SCREEN_COLOUR_8_PLANE);
  ap_graphics_attach_memory(&short_memory, scanout_memory, 128u * 1024u,
                            nullptr, 0u);
  TEST_ASSERT_EQUAL_UINT32(0u, ap_graphics_scanout(&short_memory,
                                                   AP_GRAPHICS_CR1_DISP_EN,
                                                   scanout_pixels,
                                                   sizeof scanout_pixels));
  /* And an absent screen, which has no geometry to scan out. */
  ap_graphics_t none;
  ap_graphics_init(&none, AP_SCREEN_NONE);
  ap_graphics_attach_memory(&none, scanout_memory, sizeof scanout_memory,
                            nullptr, 0u);
  TEST_ASSERT_EQUAL_UINT32(0u, ap_graphics_scanout(&none,
                                                   AP_GRAPHICS_CR1_DISP_EN,
                                                   scanout_pixels,
                                                   sizeof scanout_pixels));
}

/* `DISP_EN` is reported, never folded into the pixels. A disabled display is
 * black, and black is not an index: index 0 on a monochrome screen is white,
 * so a scanout that returned zeroes for "disabled" would mean two different
 * colours depending on the card. */
static void test_disp_en_is_reported_rather_than_painted(void) {
  TEST_ASSERT_FALSE(ap_graphics_display_enabled(0u));
  TEST_ASSERT_TRUE(ap_graphics_display_enabled(AP_GRAPHICS_CR1_DISP_EN));

  ap_graphics_t g;
  scanout_setup(&g, AP_SCREEN_MONO_15_INCH);
  put_word(0u, 0x8000u);
  /* With the bit clear the memory still reads out exactly as it stands. */
  TEST_ASSERT_EQUAL_UINT32(1024u * 800u,
                           ap_graphics_scanout(&g, 0u, scanout_pixels,
                                               sizeof scanout_pixels));
  TEST_ASSERT_EQUAL_UINT8(1u, scanout_pixels[0]);
}

/* ## The blitter and the scanout, composed
 *
 * Everything above tests one half or the other. This is the only test that
 * draws something and then looks at it, which is the shape the item's
 * verification asks for -- every register identity in this file passes on a
 * controller whose picture is mirrored, sheared or blank.
 *
 * **It also shows what is not yet joined.** `ap_graphics_blit` works on a host
 * -order `uint16_t` array and the image memory a board attaches is bytes, so
 * the test stores the blitted words big-endian itself. On real hardware that
 * is one memory; here the two halves are still separate modules and the
 * joining is the next piece of work. Writing the serialisation out by hand is
 * how the test says so.
 */
static void test_a_blit_lands_where_the_scanout_reads_it(void) {
  ap_graphics_t g;
  scanout_setup(&g, AP_SCREEN_COLOUR_8_PLANE);
  ap_graphics_geometry_t geometry;
  TEST_ASSERT_TRUE(ap_graphics_geometry(AP_SCREEN_COLOUR_8_PLANE, &geometry));

  /* One word of image memory per plane, at the second word of the third row --
   * off both axes, so a stride that ignored the buffer width and one that
   * ignored the plane size both land somewhere else. */
  const uint32_t dest = 2u * (geometry.buffer_width / 16u) + 1u;

  uint32_t source[8];
  /* Planes 0 and 2 carry the pattern, the rest are blank: index 5, which is
   * asymmetric in both directions. */
  for (unsigned p = 0; p < 8u; p++) {
    source[p] = (p == 0u || p == 2u) ? 0xC000u : 0x0000u;
  }

  ap_graphics_blit_t blit = {
      .cr0 = 0u,
      .cr1 = AP_GRAPHICS_CR1_ROP_EN,
      .access = AP_GRAPHICS_CR2_PLANE_ACCESS,
      /* Plain source copy in every plane. */
      .rop_register = 0x33333333u,
      .write_enable = 0x0000u,
      /* **Active low**: zero selects, so zero selects every plane. */
      .d_plane = 0x00u,
      .s_plane = 0u,
      .planes = 8u,
      .plane_stride = geometry.plane_words,
  };
  /* Straight into the *board's* memory -- the same buffer the scanout reads.
   * This used to blit into a separate host-order array and then serialise it
   * here by hand, which was the seam. There is one memory now. */
  TEST_ASSERT_EQUAL_UINT(8u,
                         ap_graphics_blit(&blit, scanout_memory,
                                          sizeof scanout_memory, dest, 0xFFFFu,
                                          source));

  TEST_ASSERT_EQUAL_UINT32(1024u * 800u,
                           ap_graphics_scanout(&g, AP_GRAPHICS_CR1_DISP_EN,
                                               scanout_pixels,
                                               sizeof scanout_pixels));

  /* Row 2, pixels 16 and 17: the two set bits of $C000 in the second word. */
  const uint32_t row = 2u * geometry.width;
  TEST_ASSERT_EQUAL_UINT8(5u, scanout_pixels[row + 16u]);
  TEST_ASSERT_EQUAL_UINT8(5u, scanout_pixels[row + 17u]);
  /* And nowhere else: not the pixel before, not the one after, and not the
   * same offset a row up -- which is where a visible-width stride would put
   * it. */
  TEST_ASSERT_EQUAL_UINT8(0u, scanout_pixels[row + 15u]);
  TEST_ASSERT_EQUAL_UINT8(0u, scanout_pixels[row + 18u]);
  TEST_ASSERT_EQUAL_UINT8(0u, scanout_pixels[geometry.width + 16u]);
}

/* ## The register file
 *
 * `CR0`-`CR2` used to be arguments and a write to the block was discarded.
 * That was honest while nothing could read one back, and it is what a real
 * picture was waiting on: the firmware programs the controller and *then*
 * blits, so a blitter that cannot see what was programmed cannot draw what was
 * asked for.
 */
static void test_the_control_registers_store_and_read_back(void) {
  ap_graphics_t g;
  ap_graphics_init(&g, AP_SCREEN_COLOUR_8_PLANE);

  const struct { uint32_t offset; uint8_t value; } cases[] = {
      {0x400u, 0xE3u}, /* CR0: a mode and a shift */
      {0x402u, 0x11u}, /* CR1: ROP_EN and DISP_EN */
      {0x404u, 0x5Au}, /* CR2 / CR2A */
      {0x405u, 0xA5u}, /* CR2B, 8-plane only */
  };
  for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + cases[i].offset,
                      cases[i].value);
  }
  for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    TEST_ASSERT_EQUAL_HEX8(
        cases[i].value,
        ap_graphics_read(&g, AP_GRAPHICS_COLOUR_ADDR + cases[i].offset));
  }
}

/* Every register is zero at reset, and that is not a neutral choice: `DISP_EN`
 * is `CR1` bit 0, so an unprogrammed controller has its display **off**. */
static void test_an_unprogrammed_controller_has_its_display_off(void) {
  ap_graphics_t g;
  ap_graphics_init(&g, AP_SCREEN_MONO_19_INCH);
  TEST_ASSERT_EQUAL_HEX8(0x00u,
                         ap_graphics_read(&g, AP_GRAPHICS_MONO_ADDR + 0x402u));
  TEST_ASSERT_FALSE(ap_graphics_display_enabled(g.reg.cr1));
}

/* The block is `0x408` bytes -- offsets `000` to `407` -- and an access decodes
 * as `offset & 0x407`, bit 10 and the low three bits. So the **low** group of
 * eight repeats all the way up to `3FF`, and the high group at `400`-`407` is
 * reached only at its own eight addresses. A model decoding the offset whole
 * would answer for `002` and not for `00A`, and would have to be given every
 * alias the firmware happens to use.
 *
 * The two halves of that are asserted separately, because a mask that dropped
 * bit 10 as well would still pass the first. */
static void test_the_low_group_aliases_and_the_high_group_does_not(void) {
  ap_graphics_t g;
  ap_graphics_init(&g, AP_SCREEN_MONO_15_INCH);

  /* `00A` and `3FA` are both offset 2: the raster operation's bits 15-8. */
  ap_graphics_write(&g, AP_GRAPHICS_MONO_ADDR + 0x00Au, 0x77u);
  TEST_ASSERT_EQUAL_HEX32(0x00007700u, g.reg.rop);
  ap_graphics_write(&g, AP_GRAPHICS_MONO_ADDR + 0x3FAu, 0x22u);
  TEST_ASSERT_EQUAL_HEX32(0x00002200u, g.reg.rop);

  /* Bit 10 survives the mask, so `402` is `CR1` and `002` is not. */
  ap_graphics_write(&g, AP_GRAPHICS_MONO_ADDR + 0x402u, 0x11u);
  TEST_ASSERT_EQUAL_HEX8(0x11u, g.reg.cr1);
  ap_graphics_write(&g, AP_GRAPHICS_MONO_ADDR + 0x002u, 0x44u);
  TEST_ASSERT_EQUAL_HEX8(0x11u, g.reg.cr1);

  /* And `408` is past the block entirely -- `0x408` bytes is `000` to `407`. */
  bool colour = false;
  uint32_t offset = 0;
  TEST_ASSERT_FALSE(
      ap_graphics_decode(AP_GRAPHICS_MONO_ADDR + 0x408u, &colour, &offset));
}

/* **The byte lanes are scrambled** and no reading of the addresses predicts
 * them. Each pair is high byte first, and the raster operation's pairs run low
 * half before high half:
 *
 *     0 -> WE 15-8    1 -> WE 7-0
 *     2 -> ROP 15-8   3 -> ROP 7-0   4 -> ROP 31-24   5 -> ROP 23-16
 *
 * Assembling either register in address order gets the halves the right way
 * round and the bytes within them backwards. For the ROP that gives every
 * plane its neighbour's function -- a screen that draws, in the wrong
 * operations -- which is why the value here has a different byte in every
 * lane. */
static void test_the_multi_byte_registers_are_in_scrambled_byte_order(void) {
  ap_graphics_t g;
  ap_graphics_init(&g, AP_SCREEN_COLOUR_8_PLANE);

  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 0u, 0x12u);
  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 1u, 0x34u);
  TEST_ASSERT_EQUAL_HEX16(0x1234u, g.reg.write_enable);

  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 2u, 0xBBu);
  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 3u, 0xCCu);
  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 4u, 0x99u);
  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 5u, 0xAAu);
  TEST_ASSERT_EQUAL_HEX32(0x99AABBCCu, g.reg.rop);

  /* The high half reads back, on this board and only through those two
   * offsets. */
  TEST_ASSERT_EQUAL_HEX8(0x99u,
                         ap_graphics_read(&g, AP_GRAPHICS_COLOUR_ADDR + 4u));
  TEST_ASSERT_EQUAL_HEX8(0xAAu,
                         ap_graphics_read(&g, AP_GRAPHICS_COLOUR_ADDR + 5u));
}

/* Offsets 4 and 5 are the raster operation's high half on an 8-plane board and
 * a diagnostic memory-refresh trigger on the others -- the same per-family
 * split `CR1`'s top bits have. On a monochrome card the write must not reach
 * the ROP, or a diagnostic would silently rewrite half the operation. */
static void test_the_rop_s_high_half_is_eight_plane_only(void) {
  ap_graphics_t g;
  ap_graphics_init(&g, AP_SCREEN_MONO_19_INCH);

  ap_graphics_write(&g, AP_GRAPHICS_MONO_ADDR + 2u, 0xBBu);
  ap_graphics_write(&g, AP_GRAPHICS_MONO_ADDR + 3u, 0xCCu);
  ap_graphics_write(&g, AP_GRAPHICS_MONO_ADDR + 4u, 0x99u);
  ap_graphics_write(&g, AP_GRAPHICS_MONO_ADDR + 5u, 0xAAu);

  TEST_ASSERT_EQUAL_HEX32(0x0000BBCCu, g.reg.rop);
  /* And nothing reads back there either. */
  TEST_ASSERT_EQUAL_HEX8(0xFFu,
                         ap_graphics_read(&g, AP_GRAPHICS_MONO_ADDR + 4u));
}

/* `CR3A` is not a value but a **bit port**: with bit 7 clear, bits 3-1 name a
 * bit of `CR1` and bit 0 is what to put there. That is how a driver flips one
 * control bit without a read-modify-write on a register it may not be able to
 * read.
 *
 * The bit number is `(value & 0x0F) >> 1` -- bit 0 of the port is the *data*
 * and the number sits one place up. Reading the low nibble as the number
 * instead lands two bits away every time, and the register still changes, so
 * it looks like it works. */
static void test_cr3a_sets_and_clears_one_bit_of_cr1(void) {
  ap_graphics_t g;
  ap_graphics_init(&g, AP_SCREEN_MONO_15_INCH);

  /* Set bit 4, `ROP_EN`: number 4 in bits 3-1 is $08, plus $01 to set. */
  ap_graphics_write(&g, AP_GRAPHICS_MONO_ADDR + 0x406u, 0x09u);
  TEST_ASSERT_EQUAL_HEX8(AP_GRAPHICS_CR1_ROP_EN, g.reg.cr1);

  /* Set bit 0, `DISP_EN`: number 0, plus $01. */
  ap_graphics_write(&g, AP_GRAPHICS_MONO_ADDR + 0x406u, 0x01u);
  TEST_ASSERT_TRUE(ap_graphics_display_enabled(g.reg.cr1));

  /* Clear bit 4 again, leaving bit 0 alone -- which is the whole point of a
   * bit port. */
  ap_graphics_write(&g, AP_GRAPHICS_MONO_ADDR + 0x406u, 0x08u);
  TEST_ASSERT_EQUAL_HEX8(AP_GRAPHICS_CR1_DISP_EN, g.reg.cr1);

  /* With bit 7 **set** the port does nothing to `CR1`, and still stores. */
  ap_graphics_write(&g, AP_GRAPHICS_MONO_ADDR + 0x406u, 0x89u);
  TEST_ASSERT_EQUAL_HEX8(AP_GRAPHICS_CR1_DISP_EN, g.reg.cr1);
  TEST_ASSERT_EQUAL_HEX8(0x89u,
                         ap_graphics_read(&g, AP_GRAPHICS_MONO_ADDR + 0x406u));
}

/* The other family's block decodes and holds nothing. A monochrome card must
 * not be programmable through the colour block, or a driver probing both would
 * configure the card it did not find. */
static void test_the_other_family_s_block_stores_nothing(void) {
  ap_graphics_t g;
  ap_graphics_init(&g, AP_SCREEN_MONO_15_INCH);

  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 0x400u, 0x77u);
  TEST_ASSERT_EQUAL_HEX8(0xFFu,
                         ap_graphics_read(&g, AP_GRAPHICS_COLOUR_ADDR + 0x400u));
  TEST_ASSERT_EQUAL_HEX8(0x00u,
                         ap_graphics_read(&g, AP_GRAPHICS_MONO_ADDR + 0x400u));
}

/* ## `CR0`'s mode dispatch
 *
 * A CPU write into the image memory is a **blit cycle**, not a store, and which
 * one is `CR0` bits 7-5. This is the piece that makes the firmware's own
 * drawing appear: a model that stored the word would draw nothing in every mode
 * and would look exactly like a firmware that never wrote -- which is what a
 * screenshot of this core showed before the dispatch existed.
 */

/* A single-plane board, so a cycle's effect is one word and the assertions are
 * about the mode rather than about the plane loop. Programmed through the
 * register block, as the firmware would: `ROP_EN` with a source copy, and
 * `PLANE` access so the source word passes through. */
static void mode_setup(ap_graphics_t *g, uint8_t mode) {
  scanout_setup(g, AP_SCREEN_MONO_15_INCH);
  g->reg.cr0 = (uint8_t)(mode << 5);
  g->reg.cr1 = AP_GRAPHICS_CR1_ROP_EN | AP_GRAPHICS_CR1_DISP_EN;
  g->reg.cr2 = (uint8_t)(AP_GRAPHICS_CR2_PLANE_ACCESS << 6);
  g->reg.rop = 0x33333333u; /* source, every plane */
  g->reg.write_enable = 0x0000u;
  g->blt_cycle = 0u;
  memset(g->guard_latch, 0, sizeof g->guard_latch);
}

static uint16_t mem_word(uint32_t word) {
  return (uint16_t)(((uint16_t)scanout_memory[word * 2u] << 8) |
                    scanout_memory[word * 2u + 1u]);
}

/* Mode 7, the ordinary case: the data is the source and the address the
 * destination, and one write draws. */
static void test_normal_mode_draws_the_word_it_was_given(void) {
  ap_graphics_t g;
  mode_setup(&g, AP_GRAPHICS_CR0_NORMAL);

  const ap_graphics_cycle_t cycle =
      ap_graphics_memory_cycle(&g, 5u, 0xBEEFu, 0xFFFFu);
  TEST_ASSERT_TRUE(cycle.blitted);
  TEST_ASSERT_EQUAL_UINT(1u, cycle.planes_written);
  TEST_ASSERT_EQUAL_HEX16(0xBEEFu, mem_word(5u));
}

/* Mode 2: the data *is* the write enable register, and with `CONST` access the
 * source is all ones. That is how a line is drawn -- the shape comes from the
 * addresses written, not from the data -- and it is why the data cannot also be
 * the source. */
static void test_vector_mode_takes_the_data_as_write_enables(void) {
  ap_graphics_t g;
  mode_setup(&g, AP_GRAPHICS_CR0_VECTOR);
  g.reg.cr2 = (uint8_t)(AP_GRAPHICS_CR2_CONSTANT_ACCESS << 6);

  /* A write enable of $FF00 protects the high byte, so a solid source reaches
   * the low byte only. The data is nowhere in the result. */
  const ap_graphics_cycle_t cycle =
      ap_graphics_memory_cycle(&g, 3u, 0xFF00u, 0xFFFFu);
  TEST_ASSERT_TRUE(cycle.blitted);
  TEST_ASSERT_EQUAL_HEX16(0x00FFu, mem_word(3u));
  /* And the register kept it, rather than the value being used and dropped. */
  TEST_ASSERT_EQUAL_HEX16(0xFF00u, g.reg.write_enable);
}

/* Mode 3 takes **two** bus cycles for one blit: the first write carries source
 * data and draws nothing, the second carries the write enables and the
 * destination. A model that drew on the first would put the source data at the
 * source's own address. */
static void test_cpu_source_mode_needs_two_cycles(void) {
  ap_graphics_t g;
  mode_setup(&g, AP_GRAPHICS_CR0_CPU_SOURCE_BLT);

  const ap_graphics_cycle_t first =
      ap_graphics_memory_cycle(&g, 9u, 0x1234u, 0xFFFFu);
  TEST_ASSERT_FALSE(first.blitted);
  TEST_ASSERT_EQUAL_UINT(0u, first.planes_written);
  TEST_ASSERT_EQUAL_HEX16(0x0000u, mem_word(9u));

  const ap_graphics_cycle_t second =
      ap_graphics_memory_cycle(&g, 4u, 0x0000u, 0xFFFFu);
  TEST_ASSERT_TRUE(second.blitted);
  TEST_ASSERT_EQUAL_HEX16(0x1234u, mem_word(4u));
  /* And the pair resets, so the next write starts a new blit rather than
   * completing this one again. */
  TEST_ASSERT_EQUAL_UINT(0u, g.blt_cycle);
}

/* A byte access on the **upper** lane is moved down before latching. The source
 * is a value, not a placed byte, so a driver writing the high half means the
 * value and not the position. The oracle carries this as an explicit fix for a
 * Domain/OS test and no manual states it. */
static void test_an_upper_byte_source_is_moved_down_before_latching(void) {
  ap_graphics_t g;
  mode_setup(&g, AP_GRAPHICS_CR0_CPU_SOURCE_BLT);

  (void)ap_graphics_memory_cycle(&g, 0u, 0x5A00u, 0xFF00u);
  (void)ap_graphics_memory_cycle(&g, 6u, 0x0000u, 0xFFFFu);
  TEST_ASSERT_EQUAL_HEX16(0x005Au, mem_word(6u));
}

/* Mode 0 draws nothing at all: the write names an address and the controller
 * latches the source there for the CPU to read back. A model that drew would
 * paint over the very word being read. */
static void test_cpu_destination_mode_latches_and_does_not_draw(void) {
  ap_graphics_t g;
  mode_setup(&g, AP_GRAPHICS_CR0_CPU_DEST_BLT);
  scanout_memory[14u] = 0xC0u;
  scanout_memory[15u] = 0xDEu;

  const ap_graphics_cycle_t cycle =
      ap_graphics_memory_cycle(&g, 7u, 0xFFFFu, 0xFFFFu);
  TEST_ASSERT_FALSE(cycle.blitted);
  TEST_ASSERT_EQUAL_HEX16(0xC0DEu, mem_word(7u));
  TEST_ASSERT_EQUAL_HEX32(0x0000C0DEu, g.guard_latch[0]);
}

/* Mode 4 moves a word within the image memory in one bus cycle: the address
 * lines carry the source and the data lines the destination word offset. That
 * is what makes a full-screen copy one access per word rather than two. */
static void test_double_access_mode_takes_its_destination_from_the_data(void) {
  ap_graphics_t g;
  mode_setup(&g, AP_GRAPHICS_CR0_DOUBLE_ACCESS_BLT);
  scanout_memory[20u] = 0xFAu;
  scanout_memory[21u] = 0xCEu;

  const ap_graphics_cycle_t cycle =
      ap_graphics_memory_cycle(&g, 10u, 40u, 0xFFFFu);
  TEST_ASSERT_TRUE(cycle.blitted);
  TEST_ASSERT_EQUAL_HEX16(0xFACEu, mem_word(40u));
  /* The source is untouched -- a copy, not a move. */
  TEST_ASSERT_EQUAL_HEX16(0xFACEu, mem_word(10u));
}

/* The two modes nothing names are counted rather than guessed. A run that
 * reaches one is a run whose picture cannot be trusted, and a silent store
 * would hide that behind a plausible image. */
static void test_the_unknown_modes_are_reported_and_draw_nothing(void) {
  for (uint8_t mode = 5u; mode <= 6u; mode++) {
    ap_graphics_t g;
    mode_setup(&g, mode);
    const ap_graphics_cycle_t cycle =
        ap_graphics_memory_cycle(&g, 2u, 0xFFFFu, 0xFFFFu);
    TEST_ASSERT_TRUE(cycle.unknown_mode);
    TEST_ASSERT_FALSE(cycle.blitted);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, mem_word(2u));
  }
}

/* `CR2`'s fields are not one register on all three boards. The 8-plane takes
 * its destination mask from `CR2A` and its source plane **and access mode**
 * from `CR2B` -- a different register. A model reading the access from `CR2`
 * there picks up the top two bits of the destination mask, which change with
 * every plane the driver selects. */
static void test_cr2_s_fields_come_from_the_right_register_per_board(void) {
  ap_graphics_t eight;
  ap_graphics_init(&eight, AP_SCREEN_COLOUR_8_PLANE);
  eight.reg.cr2 = 0xC0u;  /* destination mask; its top bits are not the access */
  eight.reg.cr2b = (uint8_t)((AP_GRAPHICS_CR2_PIXEL_ACCESS << 6) | 5u);
  unsigned s = 0u, d = 0u;
  ap_graphics_cr2_access_t access = AP_GRAPHICS_CR2_CONSTANT_ACCESS;
  ap_graphics_cr2_fields(&eight, &s, &d, &access);
  TEST_ASSERT_EQUAL_UINT(0xC0u, d);
  TEST_ASSERT_EQUAL_UINT(5u, s);
  TEST_ASSERT_EQUAL_INT(AP_GRAPHICS_CR2_PIXEL_ACCESS, access);

  /* A monochrome board has one plane, so its selects are fixed rather than
   * read: source 0, and a destination mask of `0E` -- active low -- leaving
   * only plane 0 selected. */
  ap_graphics_t mono;
  ap_graphics_init(&mono, AP_SCREEN_MONO_19_INCH);
  mono.reg.cr2 = 0xFFu;
  ap_graphics_cr2_fields(&mono, &s, &d, &access);
  TEST_ASSERT_EQUAL_UINT(0u, s);
  TEST_ASSERT_EQUAL_UINT(0x0Eu, d);

  /* A 4-plane board reads both out of `CR2` itself. */
  ap_graphics_t four;
  ap_graphics_init(&four, AP_SCREEN_COLOUR_4_PLANE);
  four.reg.cr2 = 0x25u; /* S_PLANE = 2 at bits 5-4, D_PLANE = 5 at bits 3-0 */
  ap_graphics_cr2_fields(&four, &s, &d, &access);
  TEST_ASSERT_EQUAL_UINT(2u, s);
  TEST_ASSERT_EQUAL_UINT(5u, d);
}

/* ## The colour lookup table, behind its two ports
 *
 * The Bt458 is not on the bus. It sits behind a **data** port at `401` and a
 * **control** port at `403`, and the control port says which of three things
 * the data port is talking to. Every select is **active low**, so a control
 * register of `FF` selects nothing.
 */

/* Reset deasserts every select, which is all ones and not zero. A control
 * register cleared at reset would leave the A/D converter selected, and the
 * first data-port write would go to a part this core does not have. */
static void test_the_lut_selects_are_all_deasserted_at_reset(void) {
  ap_graphics_t g;
  ap_graphics_init(&g, AP_SCREEN_COLOUR_8_PLANE);
  TEST_ASSERT_EQUAL_HEX8(0xFFu, g.lut_control);
  TEST_ASSERT_EQUAL_HEX8(
      0xFFu, ap_graphics_read(&g, AP_GRAPHICS_COLOUR_ADDR + 0x403u));
}

/* With `CPAL_CS` asserted the data port reaches the part directly: an address
 * and then three colour bytes, which the Bt458 commits on the blue cycle. */
static void test_the_palette_can_be_written_straight_through(void) {
  ap_graphics_t g;
  ap_graphics_init(&g, AP_SCREEN_COLOUR_8_PLANE);

  /* `CPAL_CS` low with `C1`/`C0` = 00 addresses the address register. */
  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 0x403u,
                    (uint8_t)~AP_GRAPHICS_LUT_CPAL_CS & 0xFCu);
  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 0x401u, 0x07u);
  /* `C1`/`C0` = 01 is the palette RAM. */
  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 0x403u,
                    (uint8_t)((~AP_GRAPHICS_LUT_CPAL_CS & 0xFCu) | 1u));
  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 0x401u, 0x11u);
  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 0x401u, 0x22u);
  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 0x401u, 0x33u);

  uint8_t rgb[3];
  TEST_ASSERT_TRUE(ap_bt458_palette(&g.lut, 7u, rgb));
  TEST_ASSERT_EQUAL_HEX8(0x11u, rgb[0]);
  TEST_ASSERT_EQUAL_HEX8(0x22u, rgb[1]);
  TEST_ASSERT_EQUAL_HEX8(0x33u, rgb[2]);
}

/* **The FIFO commits on the release of `CPAL_CS`, not on its level.** A driver
 * buffers a whole palette and lands it in one go, which is how the table is
 * rewritten without tearing the picture. A model watching the level would drain
 * on every write that left the select high, including ones that never asserted
 * it -- and a model writing straight through would be indistinguishable until
 * something read the palette back mid-load. */
static void test_the_fifo_commits_when_the_palette_select_is_released(void) {
  ap_graphics_t g;
  ap_graphics_init(&g, AP_SCREEN_COLOUR_8_PLANE);

  /* `FIFO_CS` asserted, `CPAL_CS` asserted too, `C1`/`C0` = 01. Writes go to
   * the FIFO because the write order tries `CPAL_CS` first only when `FIFO_CS`
   * is high -- so assert the FIFO alone. */
  const uint8_t fifo_only =
      (uint8_t)(0xFFu & ~AP_GRAPHICS_LUT_FIFO_CS);
  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 0x403u, fifo_only);
  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 0x401u, 0x00u); /* address */
  TEST_ASSERT_EQUAL_UINT(1u, g.lut_fifo_count);

  /* Nothing has reached the part yet. */
  uint8_t rgb[3];
  TEST_ASSERT_TRUE(ap_bt458_palette(&g.lut, 0u, rgb));
  TEST_ASSERT_EQUAL_HEX8(0x00u, rgb[0]);

  /* Now assert `CPAL_CS` with `C1`/`C0` = 00 for the address register, buffer
   * the colour, and release. */
  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 0x403u,
                    (uint8_t)(fifo_only & ~AP_GRAPHICS_LUT_CPAL_CS & 0xFCu));
  /* Releasing `CPAL_CS` drains what is buffered. */
  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 0x403u, 0xFCu);
  TEST_ASSERT_EQUAL_UINT(0u, g.lut_fifo_count);
}

/* `FIFO_RST` is active low: the reset is the falling edge, so a control write
 * that leaves the bit high resets nothing however often it is repeated. */
static void test_the_fifo_reset_is_a_falling_edge(void) {
  ap_graphics_t g;
  ap_graphics_init(&g, AP_SCREEN_COLOUR_8_PLANE);
  const uint8_t fifo_only = (uint8_t)(0xFFu & ~AP_GRAPHICS_LUT_FIFO_CS);

  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 0x403u, fifo_only);
  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 0x401u, 0x5Au);
  TEST_ASSERT_EQUAL_UINT(1u, g.lut_fifo_count);

  /* The bit stays high: nothing happens. */
  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 0x403u, fifo_only);
  TEST_ASSERT_EQUAL_UINT(1u, g.lut_fifo_count);

  /* Falling edge. */
  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 0x403u,
                    (uint8_t)(fifo_only & ~AP_GRAPHICS_LUT_FIFO_RST));
  TEST_ASSERT_EQUAL_UINT(0u, g.lut_fifo_count);
}

/* The read and write orders differ, and it is not a transcription slip: a write
 * tries the A/D, the palette, then the FIFO; a read tries the **FIFO first**.
 * That is what lets a driver push into the buffer and read the part back in one
 * control-register setting. */
static void test_a_read_tries_the_fifo_first_and_a_write_does_not(void) {
  ap_graphics_t g;
  ap_graphics_init(&g, AP_SCREEN_COLOUR_8_PLANE);

  /* Both the palette and the FIFO selected, direction set to read. */
  const uint8_t both = (uint8_t)(0xFFu & ~AP_GRAPHICS_LUT_FIFO_CS &
                                 ~AP_GRAPHICS_LUT_CPAL_CS & 0xFCu);
  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 0x403u, both);

  /* The write went to the *palette*, because a write tries `CPAL_CS` first. */
  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 0x401u, 0x03u);
  TEST_ASSERT_EQUAL_UINT(0u, g.lut_fifo_count);

  /* And a read comes from the FIFO, which is empty, rather than from the part
   * -- the two ends of the same setting reach different places. */
  TEST_ASSERT_EQUAL_HEX8(
      0x00u, ap_graphics_read(&g, AP_GRAPHICS_COLOUR_ADDR + 0x401u));
}

/* The A/D converter behind the third select reads a monitor's identification
 * and a brightness pot, neither of which this core has. Counted rather than
 * answered with a number nothing stands behind. */
static void test_the_a_d_converter_is_counted_rather_than_invented(void) {
  ap_graphics_t g;
  ap_graphics_init(&g, AP_SCREEN_COLOUR_8_PLANE);
  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 0x403u,
                    (uint8_t)(0xFFu & ~AP_GRAPHICS_LUT_AD_CS));
  ap_graphics_write(&g, AP_GRAPHICS_COLOUR_ADDR + 0x401u, 0x5Au);
  TEST_ASSERT_EQUAL_UINT(1u, g.lut_ad_accesses);
}

/* Only an 8-plane board has one. A 4-plane card's lookup table is a different
 * thing entirely -- sixteen entries written through three registers of the
 * controller's own -- and a monochrome one has none. */
static void test_only_the_eight_plane_board_has_a_bt458(void) {
  ap_graphics_t four;
  ap_graphics_init(&four, AP_SCREEN_COLOUR_4_PLANE);
  ap_graphics_write(&four, AP_GRAPHICS_COLOUR_ADDR + 0x403u, 0x00u);
  TEST_ASSERT_EQUAL_HEX8(0xFFu, four.lut_control);
  TEST_ASSERT_EQUAL_HEX8(
      0xFFu, ap_graphics_read(&four, AP_GRAPHICS_COLOUR_ADDR + 0x403u));
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
  RUN_TEST(test_every_raster_op_is_its_own_boolean_function);
  RUN_TEST(test_source_copies_and_destination_writes_nothing);
  RUN_TEST(test_a_disabled_rop_passes_the_source_through);
  RUN_TEST(test_each_plane_selects_its_own_operation);
  RUN_TEST(test_the_plane_selects_are_wider_on_the_eight_plane_board);
  RUN_TEST(test_a_destination_plane_is_selected_by_a_zero_bit);
  RUN_TEST(test_a_set_write_enable_bit_protects_the_destination);
  RUN_TEST(test_the_bus_mask_protects_whatever_the_register_says);
  RUN_TEST(test_the_access_modes_shape_the_source_word);
  RUN_TEST(test_a_shift_of_sixteen_or_more_rotates_before_shifting);
  RUN_TEST(test_the_data_path_runs_source_then_rop_then_write_enable);
  RUN_TEST(test_a_blit_writes_one_word_per_plane_at_its_stride);
  RUN_TEST(test_a_masked_plane_is_skipped_without_moving_the_others);
  RUN_TEST(test_a_plane_past_the_memory_is_skipped_not_wrapped);
  RUN_TEST(test_the_ad_bit_broadcasts_one_source_to_every_plane);
  RUN_TEST(test_each_plane_combines_by_its_own_operation);
  RUN_TEST(test_the_write_enable_applies_to_every_plane);
  RUN_TEST(test_each_screen_s_memory_is_the_capacity_the_manual_prints);
  RUN_TEST(test_an_absent_screen_has_no_geometry_at_all);
  RUN_TEST(test_the_high_bit_of_a_word_is_the_leftmost_pixel);
  RUN_TEST(test_a_row_is_the_buffer_s_width_apart_not_the_screen_s);
  RUN_TEST(test_plane_zero_is_the_least_significant_bit_of_the_index);
  RUN_TEST(test_inv_inverts_a_mono_screen_and_is_ad_bit_on_a_colour_one);
  RUN_TEST(test_a_scanout_that_cannot_run_writes_nothing);
  RUN_TEST(test_disp_en_is_reported_rather_than_painted);
  RUN_TEST(test_a_blit_lands_where_the_scanout_reads_it);
  RUN_TEST(test_the_control_registers_store_and_read_back);
  RUN_TEST(test_an_unprogrammed_controller_has_its_display_off);
  RUN_TEST(test_the_low_group_aliases_and_the_high_group_does_not);
  RUN_TEST(test_the_multi_byte_registers_are_in_scrambled_byte_order);
  RUN_TEST(test_the_rop_s_high_half_is_eight_plane_only);
  RUN_TEST(test_cr3a_sets_and_clears_one_bit_of_cr1);
  RUN_TEST(test_the_other_family_s_block_stores_nothing);
  RUN_TEST(test_normal_mode_draws_the_word_it_was_given);
  RUN_TEST(test_vector_mode_takes_the_data_as_write_enables);
  RUN_TEST(test_cpu_source_mode_needs_two_cycles);
  RUN_TEST(test_an_upper_byte_source_is_moved_down_before_latching);
  RUN_TEST(test_cpu_destination_mode_latches_and_does_not_draw);
  RUN_TEST(test_double_access_mode_takes_its_destination_from_the_data);
  RUN_TEST(test_the_unknown_modes_are_reported_and_draw_nothing);
  RUN_TEST(test_cr2_s_fields_come_from_the_right_register_per_board);
  RUN_TEST(test_the_lut_selects_are_all_deasserted_at_reset);
  RUN_TEST(test_the_palette_can_be_written_straight_through);
  RUN_TEST(test_the_fifo_commits_when_the_palette_select_is_released);
  RUN_TEST(test_the_fifo_reset_is_a_falling_edge);
  RUN_TEST(test_a_read_tries_the_fifo_first_and_a_write_does_not);
  RUN_TEST(test_the_a_d_converter_is_counted_rather_than_invented);
  RUN_TEST(test_only_the_eight_plane_board_has_a_bt458);
  return UNITY_END();
}
