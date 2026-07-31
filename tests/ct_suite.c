/* Apollo cartridge tape images. Format measured in `FINDINGS.md` C24.
 *
 * Every image here is built by the test. `media/` is gitignored because Apollo
 * distribution media is not ours to redistribute, so a suite that read from it
 * would pass on one machine and fail on every other. The bytes that came from
 * the real cartridge are reproduced as constants instead. */

#include "unity.h"

#include <string.h>

#include "image/ap_ct.h"

void setUp(void) {}
void tearDown(void) {}

/* Block 0 of the measured Domain/OS boot cartridge, as far as it was read:
 * four big-endian words, then the identification. */
static void build_boot_block(uint8_t *block) {
  memset(block, 0, AP_CT_BLOCK_SIZE);
  static const uint8_t header[] = {
      0x00, 0x13, 0xD8, 0x00, 0x00, 0x13, 0xD8, 0x2A,
      0x00, 0x13, 0xF6, 0xBC, 0x56, 0xAC, 0x0D, 0x83,
  };
  memcpy(block, header, sizeof header);
  memcpy(block + 0x10, "SYSBOOT REV ", 12);
  memcpy(block + 0x20, " M68K    ", 9);
  /* The 68000 code that follows: a PC-relative LEA and a MOVE.L. */
  static const uint8_t code[] = {0x41, 0xFA, 0xFF, 0xD4, 0x20, 0x08};
  memcpy(block + 0x2A, code, sizeof code);
}

static void test_an_image_must_be_a_whole_number_of_blocks(void) {
  ap_ct_t ct;
  static uint8_t image[AP_CT_BLOCK_SIZE * 2u];

  TEST_ASSERT_TRUE(ap_ct_open(&ct, image, sizeof image));
  TEST_ASSERT_EQUAL_UINT64(2u, ap_ct_blocks(&ct));

  /* A remainder means the image is not what it claims to be -- truncated, or
   * decompressed wrongly. Refused rather than rounded, so nobody reads a short
   * final block padded with whatever followed it. */
  TEST_ASSERT_FALSE(ap_ct_open(&ct, image, sizeof image - 1u));
  TEST_ASSERT_FALSE(ap_ct_open(&ct, image, 0u));
  TEST_ASSERT_FALSE(ap_ct_open(&ct, NULL, sizeof image));
}

static void test_the_measured_cartridge_size_is_a_whole_number_of_blocks(void) {
  /* 53,678,592 bytes is exactly 104,841 blocks with no remainder, which is what
   * established the format as raw blocks in the first place. Asserted as
   * arithmetic so the claim is checkable without the file. */
  TEST_ASSERT_EQUAL_UINT64(0u, 53678592u % AP_CT_BLOCK_SIZE);
  TEST_ASSERT_EQUAL_UINT64(104841u, 53678592u / AP_CT_BLOCK_SIZE);
}

static void test_a_block_is_copied_whole_or_not_at_all(void) {
  ap_ct_t ct;
  static uint8_t image[AP_CT_BLOCK_SIZE * 3u];
  uint8_t out[AP_CT_BLOCK_SIZE];

  for (unsigned i = 0; i < sizeof image; i++) {
    image[i] = (uint8_t)(i & 0xFFu);
  }
  TEST_ASSERT_TRUE(ap_ct_open(&ct, image, sizeof image));

  TEST_ASSERT_TRUE(ap_ct_read_block(&ct, 1u, out));
  TEST_ASSERT_EQUAL_MEMORY(image + AP_CT_BLOCK_SIZE, out, AP_CT_BLOCK_SIZE);

  /* Past the end fails rather than short-reading, so a caller can never act on
   * half a block believing it has one. */
  memset(out, 0xAA, sizeof out);
  TEST_ASSERT_FALSE(ap_ct_read_block(&ct, 3u, out));
  TEST_ASSERT_EQUAL_HEX8(0xAA, out[0]);
}

static void test_the_boot_record_returns_the_measured_words(void) {
  ap_ct_t ct;
  static uint8_t image[AP_CT_BLOCK_SIZE * 2u];
  ap_ct_boot_t boot;

  memset(image, 0, sizeof image);
  build_boot_block(image);
  TEST_ASSERT_TRUE(ap_ct_open(&ct, image, sizeof image));
  TEST_ASSERT_TRUE(ap_ct_boot_record(&ct, &boot));

  TEST_ASSERT_EQUAL_HEX32(0x0013D800u, boot.word[0]);
  TEST_ASSERT_EQUAL_HEX32(0x0013D82Au, boot.word[1]);
  TEST_ASSERT_EQUAL_HEX32(0x0013F6BCu, boot.word[2]);
  TEST_ASSERT_EQUAL_HEX32(0x56AC0D83u, boot.word[3]);
}

static void test_the_words_are_returned_unnamed(void) {
  ap_ct_t ct;
  static uint8_t image[AP_CT_BLOCK_SIZE];
  ap_ct_boot_t boot;

  build_boot_block(image);
  TEST_ASSERT_TRUE(ap_ct_open(&ct, image, sizeof image));
  TEST_ASSERT_TRUE(ap_ct_boot_record(&ct, &boot));

  /* The arithmetic that makes "load, entry, end" a plausible reading -- word 1
   * is word 0 plus the header length, and word 2 gives a 7868-byte image.
   *
   * Checked here rather than encoded in the struct's field names, because C24
   * records the reading as an inference. Three addresses of something else fit
   * the same arithmetic, and a field called `load_address` would turn the guess
   * into an assertion for every later reader. */
  TEST_ASSERT_EQUAL_HEX32(0x2Au, boot.word[1] - boot.word[0]);
  TEST_ASSERT_EQUAL_UINT32(7868u, boot.word[2] - boot.word[0]);
}

static void test_a_bootable_cartridge_announces_itself(void) {
  ap_ct_t ct;
  static uint8_t image[AP_CT_BLOCK_SIZE];
  ap_ct_boot_t boot;

  build_boot_block(image);
  TEST_ASSERT_TRUE(ap_ct_open(&ct, image, sizeof image));
  TEST_ASSERT_TRUE(ap_ct_boot_record(&ct, &boot));

  /* "SYSBOOT REV" and "M68K" mean a cartridge can be recognised as bootable,
   * and as 68000, without executing a byte of it. */
  TEST_ASSERT_TRUE(boot.bootable);
  TEST_ASSERT_TRUE(boot.m68k);
}

static void test_a_data_cartridge_parses_and_says_it_is_not_bootable(void) {
  ap_ct_t ct;
  static uint8_t image[AP_CT_BLOCK_SIZE];
  ap_ct_boot_t boot;

  memset(image, 0x5A, sizeof image);
  TEST_ASSERT_TRUE(ap_ct_open(&ct, image, sizeof image));

  /* Parsing succeeds and the flags report the truth, rather than the parse
   * failing: the distribution has several non-boot cartridges and reading one
   * is not an error. */
  TEST_ASSERT_TRUE(ap_ct_boot_record(&ct, &boot));
  TEST_ASSERT_FALSE(boot.bootable);
  TEST_ASSERT_FALSE(boot.m68k);
}

static void test_the_identification_is_matched_past_its_embedded_nuls(void) {
  ap_ct_t ct;
  static uint8_t image[AP_CT_BLOCK_SIZE];
  ap_ct_boot_t boot;

  /* The measured field is "SYSBOOT REV \0\0\0\0 M68K    ": the processor name
   * sits *after* four NULs. A string comparison would stop at the first and
   * never see it, so the match is a fixed-span compare at a known offset. */
  build_boot_block(image);
  TEST_ASSERT_TRUE(ap_ct_open(&ct, image, sizeof image));
  TEST_ASSERT_TRUE(ap_ct_boot_record(&ct, &boot));
  TEST_ASSERT_EQUAL_HEX8(0x00, image[0x1C]);
  TEST_ASSERT_TRUE(boot.m68k);
}

static void test_the_boot_image_names_what_the_code_confirms(void) {
  ap_ct_t ct;
  static uint8_t big[AP_CT_BLOCK_SIZE * 32u];
  ap_ct_boot_image_t boot;

  memset(big, 0, sizeof big);
  build_boot_block(big);
  TEST_ASSERT_TRUE(ap_ct_open(&ct, big, sizeof big));
  TEST_ASSERT_TRUE(ap_ct_boot_image(&ct, &boot));

  /* Named, because C24 confirmed the reading from the boot code: its first
   * instruction is a PC-relative LEA computing word 0 when executed at word 1. */
  TEST_ASSERT_EQUAL_HEX32(0x0013D800u, boot.load_address);
  TEST_ASSERT_EQUAL_HEX32(0x0013D82Au, boot.entry_point);
  TEST_ASSERT_EQUAL_UINT32(7868u, boot.length);

  /* The header is part of the image -- the load address points at it, not past
   * it, which is why the entry point is the load address plus 0x2A. */
  TEST_ASSERT_EQUAL_PTR(big, boot.data);
  TEST_ASSERT_EQUAL_UINT32(0x2Au, boot.entry_point - boot.load_address);
}

static void test_the_first_instruction_computes_the_load_address(void) {
  ap_ct_t ct;
  static uint8_t big[AP_CT_BLOCK_SIZE * 32u];
  ap_ct_boot_image_t boot;

  memset(big, 0, sizeof big);
  build_boot_block(big);
  TEST_ASSERT_TRUE(ap_ct_open(&ct, big, sizeof big));
  TEST_ASSERT_TRUE(ap_ct_boot_image(&ct, &boot));

  /* The confirmation itself, as arithmetic on the actual bytes: `41FA` is
   * LEA (d16,PC),A0 and the 68000 computes the displacement against the
   * extension word's address. If this stops holding, the layout this module
   * names has stopped being the layout the code assumes. */
  const uint8_t *code = big + 0x2A;
  TEST_ASSERT_EQUAL_HEX8(0x41, code[0]);
  TEST_ASSERT_EQUAL_HEX8(0xFA, code[1]);
  int32_t d16 = (int16_t)(((uint16_t)code[2] << 8) | code[3]);
  uint32_t extension_word = boot.entry_point + 2u;
  TEST_ASSERT_EQUAL_HEX32(boot.load_address,
                          (uint32_t)((int32_t)extension_word + d16));
}

static void test_a_data_cartridge_yields_no_boot_image(void) {
  ap_ct_t ct;
  static uint8_t data_only[AP_CT_BLOCK_SIZE];
  ap_ct_boot_image_t boot;

  memset(data_only, 0x5A, sizeof data_only);
  TEST_ASSERT_TRUE(ap_ct_open(&ct, data_only, sizeof data_only));

  /* A data cartridge's first block is not a header, and its words would decode
   * into a plausible address and length. Requiring the identification is what
   * stops that being loaded and jumped into. */
  TEST_ASSERT_FALSE(ap_ct_boot_image(&ct, &boot));
}

static void test_a_header_larger_than_its_cartridge_is_refused(void) {
  ap_ct_t ct;
  static uint8_t small[AP_CT_BLOCK_SIZE];
  ap_ct_boot_image_t boot;

  build_boot_block(small);
  TEST_ASSERT_TRUE(ap_ct_open(&ct, small, sizeof small));

  /* The header claims 7868 bytes and the cartridge holds 512. Loading what
   * there is would put a partial program in memory and jump into it -- which
   * fails somewhere unrelated, much later, with nothing pointing back here. */
  TEST_ASSERT_FALSE(ap_ct_boot_image(&ct, &boot));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_boot_image_names_what_the_code_confirms);
  RUN_TEST(test_the_first_instruction_computes_the_load_address);
  RUN_TEST(test_a_data_cartridge_yields_no_boot_image);
  RUN_TEST(test_a_header_larger_than_its_cartridge_is_refused);
  RUN_TEST(test_an_image_must_be_a_whole_number_of_blocks);
  RUN_TEST(test_the_measured_cartridge_size_is_a_whole_number_of_blocks);
  RUN_TEST(test_a_block_is_copied_whole_or_not_at_all);
  RUN_TEST(test_the_boot_record_returns_the_measured_words);
  RUN_TEST(test_the_words_are_returned_unnamed);
  RUN_TEST(test_a_bootable_cartridge_announces_itself);
  RUN_TEST(test_a_data_cartridge_parses_and_says_it_is_not_bootable);
  RUN_TEST(test_the_identification_is_matched_past_its_embedded_nuls);
  return UNITY_END();
}
