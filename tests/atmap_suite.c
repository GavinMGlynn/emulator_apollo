/* Apollo address translation map, `[ADD]` §4.2.1.4 and `[S3K]` §1.2, §2.5.
 *
 * Every figure asserted here is quoted at its test. The map is small enough
 * that it can be checked exhaustively rather than by sampling, and two of these
 * tests do exactly that. */

#include "unity.h"

#include "board/ap_atmap.h"
#include "device/ap_mc146818.h"
#include "board/ap_board.h"
#include "model/ap_model.h"

void setUp(void) {}
void tearDown(void) {}

static void test_a_map_entry_supplies_the_high_sixteen_bits_of_the_address(
    void) {
  ap_atmap_t map;
  ap_atmap_init(&map);

  /* "The 16-bit Address Translation Map entry (a physical page number, bits
   * <25:10>) is concatenated with the page offset (DMA address bits <9:0>),
   * which yields a 26-bit physical address." */
  /* At whatever index the DMA address selects: the index has a base as well as
   * a span, and this test is about the *concatenation*. */
  const uint32_t dma = (3u << 10) | 0x2AB;
  map.entry[ap_atmap_index(dma, AP_ATMAP_TRANSFER_8BIT)] = 0x1234;

  uint32_t physical = ap_atmap_translate(&map, dma, AP_ATMAP_TRANSFER_8BIT);

  TEST_ASSERT_EQUAL_HEX32((0x1234u << 10) | 0x2AB, physical);
}

static void test_the_map_translates_into_a_twenty_six_bit_address_space(void) {
  ap_atmap_t map;
  ap_atmap_init(&map);

  /* "the DS3500 or DS4000 physical address space (64 MB)". An entry is 16 bits
   * and the page shift is 10, so a full entry reaches bit 25 and no further --
   * the top of a 26-bit space exactly, with nothing left over to truncate. */
  map.entry[ap_atmap_index(0x3FFu, AP_ATMAP_TRANSFER_8BIT)] = 0xFFFF;
  uint32_t physical = ap_atmap_translate(&map, 0x3FF, AP_ATMAP_TRANSFER_8BIT);

  TEST_ASSERT_EQUAL_HEX32(0x3FFFFFFu, physical);
  TEST_ASSERT_TRUE(physical < (1u << 26));
}

static void test_an_eight_bit_transfer_indexes_sixty_four_entries(void) {
  /* "address bits <15:10> provide an index into the Address Translation Map;
   * they select one of the 64 entries contained within it." */
  TEST_ASSERT_EQUAL_UINT(64u,
                         ap_atmap_reachable_entries(AP_ATMAP_TRANSFER_8BIT));

  /* Six bits of index: bit 15 is the top one that reaches the map. */
  /* Six bits of span, based at the window: 512-575, not 0-63. */
  TEST_ASSERT_EQUAL_UINT(AP_ATMAP_WINDOW_FIRST_ENTRY, ap_atmap_index(0u, AP_ATMAP_TRANSFER_8BIT));
  TEST_ASSERT_EQUAL_UINT(AP_ATMAP_WINDOW_FIRST_ENTRY + 63u,
                         ap_atmap_index(0xFC00u, AP_ATMAP_TRANSFER_8BIT));
  /* Bit 16 is above the 8-bit controller's span and must not index. */
  TEST_ASSERT_EQUAL_UINT(AP_ATMAP_WINDOW_FIRST_ENTRY,
                         ap_atmap_index(0x10000u, AP_ATMAP_TRANSFER_8BIT));
}

static void test_a_sixteen_bit_transfer_indexes_one_hundred_and_twenty_eight(
    void) {
  /* "address bits <16:10> provide an index into the Address Translation Map;
   * they select one of the 128 entries contained within it." One more bit than
   * the 8-bit case, because a 16-bit controller's address space is twice as
   * large -- 128 KB against 64 KB. */
  TEST_ASSERT_EQUAL_UINT(128u,
                         ap_atmap_reachable_entries(AP_ATMAP_TRANSFER_16BIT));

  TEST_ASSERT_EQUAL_UINT(AP_ATMAP_WINDOW_FIRST_ENTRY + 127u,
                         ap_atmap_index(0x1FC00u, AP_ATMAP_TRANSFER_16BIT));
  /* Bit 16 *does* index here, which is the whole difference between the two. */
  TEST_ASSERT_EQUAL_UINT(AP_ATMAP_WINDOW_FIRST_ENTRY + 64u,
                         ap_atmap_index(0x10000u, AP_ATMAP_TRANSFER_16BIT));
}

static void test_a_sixteen_bit_transfer_cannot_address_an_odd_byte(void) {
  ap_atmap_t map;
  ap_atmap_init(&map);
  map.entry[ap_atmap_index(0x100u, AP_ATMAP_TRANSFER_16BIT)] = 0x0040;

  /* "concatenated with the page offset (DMA address bits <9:1>)" -- nine bits,
   * against the 8-bit case's ten. A 16-bit DMA controller counts words and has
   * no address bit 0 to drive, so every physical address it can produce is
   * even. Reading the offset as <9:0> here would silently give this transfer an
   * addressing resolution the hardware does not have. */
  uint32_t even = ap_atmap_translate(&map, 0x100u, AP_ATMAP_TRANSFER_16BIT);
  uint32_t odd = ap_atmap_translate(&map, 0x101u, AP_ATMAP_TRANSFER_16BIT);

  TEST_ASSERT_EQUAL_HEX32(even, odd);
  TEST_ASSERT_EQUAL_HEX32(0u, even & 1u);
}

static void test_an_eight_bit_transfer_does_address_an_odd_byte(void) {
  ap_atmap_t map;
  ap_atmap_init(&map);
  map.entry[ap_atmap_index(0x101u, AP_ATMAP_TRANSFER_8BIT)] = 0x0040;

  /* The other side of the same fact: <9:0> is ten bits and bit 0 survives. The
   * pair is the check -- either test alone passes against a model that lost the
   * distinction in the direction it happens not to probe. */
  uint32_t physical = ap_atmap_translate(&map, 0x101u, AP_ATMAP_TRANSFER_8BIT);

  TEST_ASSERT_EQUAL_HEX32((0x0040u << 10) | 0x101u, physical);
  TEST_ASSERT_EQUAL_HEX32(1u, physical & 1u);
}

static void test_a_page_is_one_kilobyte(void) {
  ap_atmap_t map;
  ap_atmap_init(&map);
  map.entry[ap_atmap_index(0u, AP_ATMAP_TRANSFER_8BIT)] = 0x0001;
  map.entry[ap_atmap_index(0x400u, AP_ATMAP_TRANSFER_8BIT)] = 0x0002;

  /* The offset is bits <9:0>, so a page is 1 KB and entry 1 begins at DMA
   * address 0x400. Checked at the boundary from both sides rather than by
   * asserting the constant, because the constant is what would be wrong. */
  TEST_ASSERT_EQUAL_UINT(AP_ATMAP_WINDOW_FIRST_ENTRY,
                         ap_atmap_index(0x3FFu, AP_ATMAP_TRANSFER_8BIT));
  TEST_ASSERT_EQUAL_UINT(AP_ATMAP_WINDOW_FIRST_ENTRY + 1u,
                         ap_atmap_index(0x400u, AP_ATMAP_TRANSFER_8BIT));
  TEST_ASSERT_EQUAL_UINT(AP_ATMAP_PAGE_SIZE, 1024u);
}

static void test_noncontiguous_pages_appear_contiguous_to_the_controller(void) {
  ap_atmap_t map;
  ap_atmap_init(&map);

  /* `[ADD]`'s first stated function: the map "allows the DS3500 ... to perform
   * DMA to or from noncontiguous physical memory (while it appears that the DMA
   * transfer is taking place from contiguous physical memory)".
   *
   * So this is the behaviour the module exists for, and it is worth asserting
   * as such: three consecutive DMA pages pointed at scattered physical pages,
   * read as one run by the controller. */
  map.entry[ap_atmap_index(0x000u, AP_ATMAP_TRANSFER_8BIT)] = 0x0100;
  map.entry[ap_atmap_index(0x400u, AP_ATMAP_TRANSFER_8BIT)] = 0x2000;
  map.entry[ap_atmap_index(0x800u, AP_ATMAP_TRANSFER_8BIT)] = 0x0007;

  TEST_ASSERT_EQUAL_HEX32(0x0100u << 10,
                          ap_atmap_translate(&map, 0x000, AP_ATMAP_TRANSFER_8BIT));
  TEST_ASSERT_EQUAL_HEX32(0x2000u << 10,
                          ap_atmap_translate(&map, 0x400, AP_ATMAP_TRANSFER_8BIT));
  TEST_ASSERT_EQUAL_HEX32(0x0007u << 10,
                          ap_atmap_translate(&map, 0x800, AP_ATMAP_TRANSFER_8BIT));

  /* And crossing a page boundary lands in the next *physical* page, not the
   * next contiguous address -- which is the point. */
  TEST_ASSERT_EQUAL_HEX32((0x2000u << 10) | 1u,
                          ap_atmap_translate(&map, 0x401, AP_ATMAP_TRANSFER_8BIT));
}

static void test_every_dma_address_translates_within_the_physical_space(void) {
  ap_atmap_t map;
  ap_atmap_init(&map);
  for (unsigned i = 0; i < AP_ATMAP_ENTRIES; i++) {
    map.entry[i] = 0xFFFF;
  }

  /* Exhaustive over the whole 128 KB a 16-bit controller can drive: no address
   * escapes 26 bits, whatever is programmed. A map that let one through would
   * be writing outside physical memory. */
  for (uint32_t address = 0; address < 0x20000u; address++) {
    TEST_ASSERT_TRUE(ap_atmap_translate(&map, address, AP_ATMAP_TRANSFER_16BIT) <
                     (1u << 26));
    TEST_ASSERT_TRUE(ap_atmap_index(address, AP_ATMAP_TRANSFER_16BIT) <
                     AP_ATMAP_ENTRIES);
  }
}

static void test_the_map_decodes_at_the_documented_region(void) {
  /* `[S3K]` §2.5: `017000` - `0177FF`. */
  TEST_ASSERT_EQUAL_HEX32(0x017000u, AP_ATMAP_BASE);
  TEST_ASSERT_EQUAL_HEX32(0x0177FFu, AP_ATMAP_LIMIT);

  TEST_ASSERT_FALSE(ap_atmap_in_range(0x016FFFu));
  TEST_ASSERT_TRUE(ap_atmap_in_range(0x017000u));
  TEST_ASSERT_TRUE(ap_atmap_in_range(0x0177FFu));
  TEST_ASSERT_FALSE(ap_atmap_in_range(0x017800u));
}

/* ## The map is the whole region, and the machine's own diagnostic says so
 *
 * This asserted the opposite: 2 KB of region against 256 bytes of entries, with
 * the difference "pinned in place" as a gap neither manual describes. The gap
 * was real and the conclusion drawn from it was wrong -- `019411-A00` §4.2.1.4
 * counts how many entries a *transfer* can index, not how many the map has.
 *
 * `SELF_TEST`'s DMA test settles it at `01002BF6`: `MOVE.W A0,(A0)+` across
 * `017000`-`0177FE`, then a walk back down requiring every word to still hold
 * its own address. That passes only if all 1024 are distinct.
 */
static void test_every_word_of_the_region_is_its_own_entry(void) {
  TEST_ASSERT_EQUAL_UINT(2048u, AP_ATMAP_LIMIT - AP_ATMAP_BASE + 1u);
  TEST_ASSERT_EQUAL_UINT(2048u, AP_ATMAP_ENTRIES * 2u);

  TEST_ASSERT_TRUE(ap_atmap_decodes_to_entry(AP_ATMAP_BASE));
  TEST_ASSERT_TRUE(ap_atmap_decodes_to_entry(AP_ATMAP_LIMIT - 1u));
  TEST_ASSERT_FALSE(ap_atmap_decodes_to_entry(AP_ATMAP_LIMIT + 1u));
}

/* The diagnostic's own walk, which is the thing that failed on the machine:
 * fill every word with its own address, then read every one back. Aliased at
 * 128 entries this finds `0177FE`'s value at `0176FE`, which is exactly the
 * `Expected= 000176FE, Actual= 000077FE` the boot printed. */
static void test_the_diagnostics_walk_finds_every_word_distinct(void) {
  ap_atmap_t map;
  ap_atmap_init(&map);

  for (uint32_t at = AP_ATMAP_BASE; at < AP_ATMAP_LIMIT; at += 2u) {
    ap_atmap_write(&map, at, (uint16_t)(at & 0xFFFFu));
  }
  for (uint32_t at = AP_ATMAP_LIMIT - 1u; at >= AP_ATMAP_BASE; at -= 2u) {
    TEST_ASSERT_EQUAL_HEX16((uint16_t)(at & 0xFFFFu),
                            ap_atmap_read(&map, at));
    if (at == AP_ATMAP_BASE + 1u) {
      break;
    }
  }
}

static void test_an_entry_reads_back_as_written(void) {
  ap_atmap_t map;
  ap_atmap_init(&map);

  ap_atmap_write(&map, AP_ATMAP_BASE, 0xBEEF);
  ap_atmap_write(&map, AP_ATMAP_BASE + 2u, 0xCAFE);

  TEST_ASSERT_EQUAL_HEX16(0xBEEF, ap_atmap_read(&map, AP_ATMAP_BASE));
  TEST_ASSERT_EQUAL_HEX16(0xCAFE, ap_atmap_read(&map, AP_ATMAP_BASE + 2u));
  /* And the entries are the same storage the translation reads. */
  TEST_ASSERT_EQUAL_HEX16(0xBEEF, map.entry[0]);
  TEST_ASSERT_EQUAL_HEX16(0xCAFE, map.entry[1]);
}

static void test_a_fresh_map_translates_everything_to_page_zero(void) {
  ap_atmap_t map;
  ap_atmap_init(&map);

  /* Zeroed rather than left as whatever was on the stack. The map has no
   * documented reset value, so this is our choice and not the hardware's --
   * but a deterministic core cannot start from uninitialised memory, and page
   * zero is the choice that makes an unprogrammed entry obviously unprogrammed
   * rather than plausibly valid. */
  for (unsigned i = 0; i < AP_ATMAP_ENTRIES; i++) {
    TEST_ASSERT_EQUAL_HEX16(0, map.entry[i]);
  }
  TEST_ASSERT_EQUAL_HEX32(0x2AB, ap_atmap_translate(&map, 0xF2AB,
                                                    AP_ATMAP_TRANSFER_8BIT));
}

static void test_only_the_series_4000_generation_has_a_translation_map(void) {
  /* `[S3K]` §1.2: "The Series 4000, unlike the Series 3000, incorporates an
   * address translation map in its architecture."
   *
   * `[ADD]` §4.2.1.4 replaces that section and enumerates: "The Address
   * Translation Map exists in the DS3500, DS4000, DS4500, and DS5500." The
   * DN3500 is on that list, which is why this is not an optional subsystem for
   * the reference machine. */
  TEST_ASSERT_TRUE(ap_model_by_name("dn3500")->has_address_translation_map);
  TEST_ASSERT_TRUE(ap_model_by_name("dn4500")->has_address_translation_map);
  TEST_ASSERT_TRUE(ap_model_by_name("dn5500")->has_address_translation_map);

  /* Stated outright for the Series 3000, by two sources. */
  TEST_ASSERT_FALSE(ap_model_by_name("dn3000")->has_address_translation_map);

  /* And by absence from `[ADD]`'s enumeration for the DN2500 -- the one entry
   * here resting on silence rather than a statement. */
  TEST_ASSERT_FALSE(ap_model_by_name("dn2500")->has_address_translation_map);
}

static void test_a_headless_server_matches_the_board_it_is_built_from(void) {
  /* The DSP models are the same boards without a display, so the map follows
   * the board and not the display. Worth pinning: a per-model flag set by hand
   * is exactly the kind of table that drifts between a workstation and its
   * server variant. */
  static const char *const pairs[][2] = {
      {"dn3000", "dsp3000"},
      {"dn3500", "dsp3500"},
      {"dn4500", "dsp4500"},
      {"dn5500", "dsp5500"},
  };
  for (unsigned i = 0; i < sizeof pairs / sizeof pairs[0]; i++) {
    TEST_ASSERT_EQUAL(ap_model_by_name(pairs[i][0])->has_address_translation_map,
                      ap_model_by_name(pairs[i][1])->has_address_translation_map);
  }
}

/* A map entry is sixteen bits and a 68030 writes it as two byte cycles, so the
 * board has to put each byte in its own half -- big-endian, so the even address
 * is the high one.
 *
 * It did not. Both halves were written with the whole byte, so an entry set the
 * only way a program can ended up holding its *second* byte in both halves:
 * every physical page number above `00FF` was silently truncated, and a DMA
 * transfer aimed at main memory at `01000000` landed in the boot PROM at zero.
 * Reads had the mirror of it, returning the low half whichever byte was asked
 * for.
 *
 * Found by a transfer that did not arrive rather than by reading the code, and
 * pinned here rather than only there: the board's byte lanes are the fact, and
 * a DMA test proves it only by consequence. */
static void test_an_entry_written_as_two_bytes_keeps_both_halves(void) {
  static uint8_t ram[0x2000];
  static const ap_mc146818_time_t epoch = {
      .year = 1987u, .month = 7u, .day = 31u, .day_of_week = 6u,
      .hour = 21u, .minute = 9u, .second = 21u,
  };
  static ap_board_t board;
  TEST_ASSERT_TRUE(ap_board_init(&board, ram, sizeof ram, &epoch, 0x012345u));

  bool ok = false;
  ap_board_write(&board, AP_ATMAP_BASE + 0u, 0x40u, &ok);
  TEST_ASSERT_TRUE(ok);
  ap_board_write(&board, AP_ATMAP_BASE + 1u, 0x00u, &ok);
  TEST_ASSERT_TRUE(ok);

  /* The whole entry, not the last byte written twice. */
  TEST_ASSERT_EQUAL_HEX16(0x4000u, ap_atmap_read(&board.translation_map,
                                                 AP_ATMAP_BASE));
  TEST_ASSERT_EQUAL_HEX8(0x40u, ap_board_read(&board, AP_ATMAP_BASE + 0u, &ok));
  TEST_ASSERT_EQUAL_HEX8(0x00u, ap_board_read(&board, AP_ATMAP_BASE + 1u, &ok));

  /* And it translates to where that page number points -- `4000 << 10` is
   * `01000000`, which is where this machine's main memory begins. So the
   * consequence the defect had is the thing asserted, not just the storage. */
  board.translation_map.entry[ap_atmap_index(0u, AP_ATMAP_TRANSFER_8BIT)] =
      0x4000u;
  TEST_ASSERT_EQUAL_HEX32(AP_BOARD_RAM_BASE + 0x40u,
                          ap_atmap_translate(&board.translation_map, 0x0040u,
                                             AP_ATMAP_TRANSFER_8BIT));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_an_entry_written_as_two_bytes_keeps_both_halves);
  RUN_TEST(test_a_map_entry_supplies_the_high_sixteen_bits_of_the_address);
  RUN_TEST(test_the_map_translates_into_a_twenty_six_bit_address_space);
  RUN_TEST(test_an_eight_bit_transfer_indexes_sixty_four_entries);
  RUN_TEST(test_a_sixteen_bit_transfer_indexes_one_hundred_and_twenty_eight);
  RUN_TEST(test_a_sixteen_bit_transfer_cannot_address_an_odd_byte);
  RUN_TEST(test_an_eight_bit_transfer_does_address_an_odd_byte);
  RUN_TEST(test_a_page_is_one_kilobyte);
  RUN_TEST(test_noncontiguous_pages_appear_contiguous_to_the_controller);
  RUN_TEST(test_every_dma_address_translates_within_the_physical_space);
  RUN_TEST(test_the_map_decodes_at_the_documented_region);
  RUN_TEST(test_every_word_of_the_region_is_its_own_entry);
  RUN_TEST(test_the_diagnostics_walk_finds_every_word_distinct);
  RUN_TEST(test_an_entry_reads_back_as_written);
  RUN_TEST(test_a_fresh_map_translates_everything_to_page_zero);
  RUN_TEST(test_only_the_series_4000_generation_has_a_translation_map);
  RUN_TEST(test_a_headless_server_matches_the_board_it_is_built_from);
  return UNITY_END();
}
