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

/* The two shapes in §1.3.3's table that a reader would otherwise assume away.
 * They are the manual's, not this core's. */
static void test_offset_two_is_two_registers_and_control_reads_elsewhere(void) {
  /* `+2` is the status register on a read and the control register on a
   * write -- one address, two registers, chosen by direction. */
  TEST_ASSERT_EQUAL_UINT(AP_3C505_REG_STATUS, AP_3C505_REG_CONTROL_WRITE);
  /* And the control register is *read* at `+6`, not at the offset it is
   * written to. A model that read it back at `+2` would return the status
   * register and look plausible doing it. */
  TEST_ASSERT_NOT_EQUAL_UINT(AP_3C505_REG_CONTROL_WRITE,
                             AP_3C505_REG_CONTROL_READ);
  TEST_ASSERT_EQUAL_UINT(6u, AP_3C505_REG_CONTROL_READ);
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

/* **The absence is the assertion.** `[DEV]` §1.9 defers the bit-level layout to
 * a *3C505 Hardware Interface Specification* this project does not hold, so the
 * eleven flags are named and unpositioned. This test exists so that giving one
 * a bit number without a source fails here first, rather than becoming a fact
 * by being used. */
static void test_the_flags_are_named_without_positions(void) {
  TEST_ASSERT_EQUAL_UINT(12u, (unsigned)AP_3C505_FLAG_COUNT);
  /* An enumerator, deliberately not a mask: 11 named flags plus the count, and
   * nothing in the header converts one to a bit. */
  TEST_ASSERT_EQUAL_UINT(0u, (unsigned)AP_3C505_FLAG_ACRE);
  TEST_ASSERT_TRUE((unsigned)AP_3C505_FLAG_HSF2 < (unsigned)AP_3C505_FLAG_COUNT);
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
  RUN_TEST(test_offset_two_is_two_registers_and_control_reads_elsewhere);
  RUN_TEST(test_the_sizes_are_the_manuals);
  RUN_TEST(test_the_factory_base_lands_where_the_board_places_the_card);
  RUN_TEST(test_the_flags_are_named_without_positions);
  RUN_TEST(test_a_response_code_is_its_command_plus_thirty_hex);
  RUN_TEST(test_the_pio_transfers_are_the_commands_with_no_response);
  RUN_TEST(test_the_reserved_codes_are_not_implemented_commands);
  return UNITY_END();
}
