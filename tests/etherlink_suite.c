/* The 3c505's host interface, against `[DEV]` -- the EtherLink Plus Developer's
 * Guide, May 1986. Findings and citations in `docs/references/ETHERNET.md`.
 *
 * What is testable today is the *shape* of the interface: the map, its two
 * asymmetries, and the sizes the manual states. The flag layout is not, because
 * §1.9 defers bit positions to a document this project does not hold -- so a
 * test asserting a position would be asserting an invention, and the last test
 * here pins that absence deliberately. */

#include "unity.h"

#include "device/ap_3c505.h"

void setUp(void) {}
void tearDown(void) {}

/* `[DEV]` §1.3.3: "The interface requires 16 locations in the Host I/O address
 * space. Jumpers are used to position the base address." */
static void test_the_card_answers_sixteen_locations_from_its_jumpered_base(
    void) {
  uint32_t offset = 99u;
  TEST_ASSERT_TRUE(ap_3c505_decode(0x300u, 0x300u, &offset));
  TEST_ASSERT_EQUAL_UINT32(0u, offset);
  TEST_ASSERT_TRUE(ap_3c505_decode(0x300u, 0x30Fu, &offset));
  TEST_ASSERT_EQUAL_UINT32(15u, offset);

  /* Sixteen, not seventeen: the location above the block is another card's. */
  TEST_ASSERT_FALSE(ap_3c505_decode(0x300u, 0x310u, &offset));
  TEST_ASSERT_FALSE(ap_3c505_decode(0x300u, 0x2FFu, &offset));

  /* And the base really is a jumper, so the same card answers elsewhere. */
  TEST_ASSERT_TRUE(ap_3c505_decode(0x320u, 0x322u, &offset));
  TEST_ASSERT_EQUAL_UINT32(2u, offset);
  TEST_ASSERT_FALSE(ap_3c505_decode(0x320u, 0x300u, NULL));
}

/* `+2` is two registers chosen by direction, and *which two* is the fact
 * `[DEV]` §1.3.3 got wrong and `[HIS]` settles. */
static void test_offset_two_is_the_status_register_and_the_aux_dma_register(
    void) {
  /* One address, two registers: status on a read, Aux DMA on a write. */
  TEST_ASSERT_EQUAL_UINT(AP_3C505_REG_STATUS, AP_3C505_REG_AUX_DMA);
  /* And the control register is neither of them. `[DEV]` §1.3.3 puts it at
   * `+2` on a write and readable at `+6`; `[DEV]`'s own §2.1 summary, §2.5 and
   * both of `[HIS]`'s tables put it at `+6` alone. A model following §1.3.3
   * writes the host's control word into the DMA burst register and reads it
   * back from the right place, which is the failure that looks like success. */
  TEST_ASSERT_EQUAL_UINT(6u, AP_3C505_REG_CONTROL);
  TEST_ASSERT_NOT_EQUAL_UINT(AP_3C505_REG_CONTROL, AP_3C505_REG_AUX_DMA);
}

/* §1.9.2 and §3.1, the sizes the manual states outright. */
static void test_the_sizes_are_the_manuals(void) {
  /* "a half duplex 20 byte FIFO" */
  TEST_ASSERT_EQUAL_UINT(20u, AP_3C505_DATA_FIFO);
  /* "The maximum PCB size the Adapter can accept in this version ROM is 64
   * bytes ... The maximum data field is 62 bytes long", the difference being
   * the command code and the length byte, which the length does not count. */
  TEST_ASSERT_EQUAL_UINT(64u, AP_3C505_PCB_MAX);
  TEST_ASSERT_EQUAL_UINT(62u, AP_3C505_PCB_DATA_MAX);
  TEST_ASSERT_EQUAL_UINT(AP_3C505_PCB_MAX, AP_3C505_PCB_DATA_MAX + 2u);
}

/* §1.3.3's factory base, through this machine's AT decode, lands where the
 * board already places the card -- `physical = 0x040000 + (ISA << 7)`. Two
 * independent placements agreeing, and this one from a manual rather than from
 * the oracle. */
static void test_the_factory_base_lands_where_the_board_places_the_card(void) {
  const uint32_t physical =
      0x040000u + (AP_3C505_IO_BASE_DEFAULT << 7);
  TEST_ASSERT_EQUAL_HEX32(0x058000u, physical);
}

/* The eleven flags `[DEV]` §1.9 named without positions. `[HIS]` has since been
 * found and gives all four registers, so the enumeration is now a reader's
 * index and the masks below are the model's. */
static void test_the_flags_are_named(void) {
  TEST_ASSERT_EQUAL_UINT(12u, (unsigned)AP_3C505_FLAG_COUNT);
  TEST_ASSERT_EQUAL_UINT(0u, (unsigned)AP_3C505_FLAG_ACRE);
  TEST_ASSERT_TRUE((unsigned)AP_3C505_FLAG_HSF2 < (unsigned)AP_3C505_FLAG_COUNT);
}

/* `[HIS]` §3-2, §3-3, §3-5, §3-6, read from the page images. Each register is
 * eight distinct bits covering the whole byte -- which is the property that
 * catches a transposition, since any two masks swapped still pass a spot
 * check. */
static void test_each_flag_register_is_eight_distinct_bits(void) {
  const uint8_t hcr[] = {AP_3C505_HCR_HSF1, AP_3C505_HCR_HSF2,
                         AP_3C505_HCR_CMDE, AP_3C505_HCR_TCEN,
                         AP_3C505_HCR_DIR,  AP_3C505_HCR_DMAE,
                         AP_3C505_HCR_FLSH, AP_3C505_HCR_ATTN};
  const uint8_t hsr[] = {AP_3C505_HSR_ASF1, AP_3C505_HSR_ASF2,
                         AP_3C505_HSR_ASF3, AP_3C505_HSR_DONE,
                         AP_3C505_HSR_DIR,  AP_3C505_HSR_ACRF,
                         AP_3C505_HSR_HCRE, AP_3C505_HSR_HRDY};
  const uint8_t acr[] = {AP_3C505_ACR_ASF1, AP_3C505_ACR_ASF2,
                         AP_3C505_ACR_ASF3, AP_3C505_ACR_LED1,
                         AP_3C505_ACR_LED2, AP_3C505_ACR_R586,
                         AP_3C505_ACR_FLSH, AP_3C505_ACR_LPBK};
  const uint8_t asr[] = {AP_3C505_ASR_HSF1, AP_3C505_ASR_HSF2,
                         AP_3C505_ASR_SWTC, AP_3C505_ASR_8_16,
                         AP_3C505_ASR_DIR,  AP_3C505_ASR_HCRF,
                         AP_3C505_ASR_ACRE, AP_3C505_ASR_ARDY};
  const uint8_t *const regs[] = {hcr, hsr, acr, asr};

  /* Each table is printed most-significant bit leftmost, so the eight entries
   * in declaration order are `01` through `80`. */
  for (unsigned r = 0u; r < 4u; r++) {
    uint8_t seen = 0u;
    for (unsigned b = 0u; b < 8u; b++) {
      TEST_ASSERT_EQUAL_HEX8(1u << b, regs[r][b]);
      seen |= regs[r][b];
    }
    TEST_ASSERT_EQUAL_HEX8(0xFFu, seen);
  }
}

/* The flags each side writes land where the *other* side reads them, at the
 * same bit. `[HIS]` §3-3 and §3-5: "routed directly". A model that renumbered
 * them on the way across would still pass a per-register test. */
static void test_the_general_purpose_flags_cross_at_the_same_bit(void) {
  /* The host writes `HSF1`/`HSF2` in `HCR`; the adapter reads them in `ASR`. */
  TEST_ASSERT_EQUAL_HEX8(AP_3C505_HCR_HSF1, AP_3C505_ASR_HSF1);
  TEST_ASSERT_EQUAL_HEX8(AP_3C505_HCR_HSF2, AP_3C505_ASR_HSF2);
  /* The adapter writes `ASF1`-`ASF3` in `ACR`; the host reads them in `HSR`. */
  TEST_ASSERT_EQUAL_HEX8(AP_3C505_ACR_ASF1, AP_3C505_HSR_ASF1);
  TEST_ASSERT_EQUAL_HEX8(AP_3C505_ACR_ASF2, AP_3C505_HSR_ASF2);
  TEST_ASSERT_EQUAL_HEX8(AP_3C505_ACR_ASF3, AP_3C505_HSR_ASF3);
  /* And the direction the host sets is visible to both. */
  TEST_ASSERT_EQUAL_HEX8(AP_3C505_HCR_DIR, AP_3C505_HSR_DIR);
  TEST_ASSERT_EQUAL_HEX8(AP_3C505_HCR_DIR, AP_3C505_ASR_DIR);
}

/* The handshake flags are named from the point of view of the side that reads
 * them, which is the trap: `HCRE` and `ACRF` are the *host*'s, `ACRE` and
 * `HCRF` the *adapter*'s, and each pair sits at a different bit. */
static void test_the_handshake_flags_belong_to_the_side_that_reads_them(void) {
  /* The host asks "has my byte been taken, and is one waiting for me?" */
  TEST_ASSERT_EQUAL_HEX8(0x40u, AP_3C505_HSR_HCRE);
  TEST_ASSERT_EQUAL_HEX8(0x20u, AP_3C505_HSR_ACRF);
  /* The adapter asks the mirror of it, and the bits are the other way up. */
  TEST_ASSERT_EQUAL_HEX8(0x40u, AP_3C505_ASR_ACRE);
  TEST_ASSERT_EQUAL_HEX8(0x20u, AP_3C505_ASR_HCRF);
}

/* `[HIS]` §3-2 states the hard reset as a decode of two control bits together,
 * so it is a property of the pair and not a third bit. */
static void test_attention_and_flush_together_are_the_hard_reset(void) {
  TEST_ASSERT_EQUAL_HEX8(AP_3C505_HCR_ATTN | AP_3C505_HCR_FLSH,
                         AP_3C505_HCR_HARD_RESET);
  /* Neither alone is one: `ATTN` by itself is the soft reset (an NMI to the
   * 80186) and `FLSH` by itself empties the FIFO. */
  TEST_ASSERT_NOT_EQUAL_HEX8(AP_3C505_HCR_ATTN, AP_3C505_HCR_HARD_RESET);
  TEST_ASSERT_NOT_EQUAL_HEX8(AP_3C505_HCR_FLSH, AP_3C505_HCR_HARD_RESET);
}

/* `[DEV]` Table 1, read from the page image rather than the text layer -- it is
 * a two-column table and the extraction interleaves the columns. Every response
 * code is its command plus `0x30`, across all seventeen implemented commands. */
static void test_a_response_code_is_its_command_plus_thirty_hex(void) {
  uint8_t response = 0u;

  /* The ends of the table and a sample from the middle. */
  TEST_ASSERT_TRUE(ap_3c505_response_for(
      AP_3C505_CMD_CONFIGURE_ADAPTER_MEMORY, &response));
  TEST_ASSERT_EQUAL_HEX8(0x31u, response);
  TEST_ASSERT_TRUE(ap_3c505_response_for(AP_3C505_CMD_TRANSMIT_PACKET,
                                         &response));
  TEST_ASSERT_EQUAL_HEX8(0x39u, response);
  TEST_ASSERT_TRUE(ap_3c505_response_for(AP_3C505_CMD_ADAPTER_INFO, &response));
  TEST_ASSERT_EQUAL_HEX8(0x41u, response);

  /* And the rule holds for every implemented command that has a response, so
   * a transcription slip in one row of the enum fails here. */
  for (uint8_t code = AP_3C505_CMD_FIRST; code <= AP_3C505_CMD_LAST; code++) {
    if (code == AP_3C505_CMD_DOWNLOAD_DATA_PIO ||
        code == AP_3C505_CMD_UPLOAD_DATA_PIO) {
      continue;
    }
    TEST_ASSERT_TRUE(ap_3c505_response_for(code, &response));
    TEST_ASSERT_EQUAL_HEX8(code + AP_3C505_RESPONSE_BIAS, response);
  }
}

/* The hole in the response space. Table 1 marks `36`/`37` `n/a` while `34`/`35`
 * are "download/upload data request" -- so the two transfers the *host* drives
 * are the two the adapter never answers. Modelling `06`/`07` as ordinary
 * commands with responses would invent two codes the manual says do not
 * exist. */
static void test_the_pio_transfers_are_the_commands_with_no_response(void) {
  uint8_t response = 0xFFu;

  TEST_ASSERT_FALSE(
      ap_3c505_response_for(AP_3C505_CMD_DOWNLOAD_DATA_PIO, &response));
  TEST_ASSERT_FALSE(
      ap_3c505_response_for(AP_3C505_CMD_UPLOAD_DATA_PIO, &response));
  TEST_ASSERT_EQUAL_HEX8(0xFFu, response); /* and it wrote nothing */

  /* Their DMA counterparts do answer, and that is the contrast the table
   * draws: `04`/`05` need the host asked for a DMA cycle, `06`/`07` do not. */
  TEST_ASSERT_TRUE(
      ap_3c505_response_for(AP_3C505_CMD_DOWNLOAD_DATA_DMA, &response));
  TEST_ASSERT_EQUAL_HEX8(0x34u, response);
  TEST_ASSERT_TRUE(
      ap_3c505_response_for(AP_3C505_CMD_UPLOAD_DATA_DMA, &response));
  TEST_ASSERT_EQUAL_HEX8(0x35u, response);
}

/* `00` names nothing, `12`-`2f` are reserved. They are in the host's half of
 * the code space without being commands, and a model that treated a reserved
 * code as implemented would answer where the ROM's behaviour is unknown. */
static void test_the_reserved_codes_are_not_implemented_commands(void) {
  TEST_ASSERT_FALSE(ap_3c505_command_is_implemented(0x00u));
  TEST_ASSERT_FALSE(ap_3c505_command_is_implemented(0x12u));
  TEST_ASSERT_FALSE(ap_3c505_command_is_implemented(AP_3C505_CMD_RESERVED_END));
  TEST_ASSERT_FALSE(ap_3c505_command_is_implemented(0x31u)); /* a response */

  TEST_ASSERT_TRUE(ap_3c505_command_is_implemented(AP_3C505_CMD_SELF_TEST));
  TEST_ASSERT_FALSE(ap_3c505_response_for(0x12u, NULL));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_card_answers_sixteen_locations_from_its_jumpered_base);
  RUN_TEST(test_offset_two_is_the_status_register_and_the_aux_dma_register);
  RUN_TEST(test_the_sizes_are_the_manuals);
  RUN_TEST(test_the_factory_base_lands_where_the_board_places_the_card);
  RUN_TEST(test_the_flags_are_named);
  RUN_TEST(test_each_flag_register_is_eight_distinct_bits);
  RUN_TEST(test_the_general_purpose_flags_cross_at_the_same_bit);
  RUN_TEST(test_the_handshake_flags_belong_to_the_side_that_reads_them);
  RUN_TEST(test_attention_and_flush_together_are_the_hard_reset);
  RUN_TEST(test_a_response_code_is_its_command_plus_thirty_hex);
  RUN_TEST(test_the_pio_transfers_are_the_commands_with_no_response);
  RUN_TEST(test_the_reserved_codes_are_not_implemented_commands);
  return UNITY_END();
}
