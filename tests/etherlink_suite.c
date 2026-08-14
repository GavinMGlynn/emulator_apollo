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


/* ## The mailbox, `[DEV]` §1.9
 *
 * Every test below is a fact about the protocol rather than about this model's
 * shape, because the model's shape is a choice and the protocol is not. */

/* **One byte's occupancy is one fact seen from two sides.** §1.9.1 gives the
 * host `HCRE`/`ACRF` and the adapter `ACRE`/`HCRF`, and the manual's own note
 * that the names are easy to confuse is the reason this is asserted directly:
 * a model storing four flags could report a byte both sent and not sent. */
static void test_the_command_byte_is_one_fact_both_sides_agree_on(void) {
  ap_3c505_t card;
  ap_3c505_reset(&card);

  /* Idle: the host's register is empty and nothing waits for either side. */
  TEST_ASSERT_TRUE(ap_3c505_host_status(&card) & AP_3C505_HSR_HCRE);
  TEST_ASSERT_FALSE(ap_3c505_host_status(&card) & AP_3C505_HSR_ACRF);
  TEST_ASSERT_TRUE(ap_3c505_adapter_status(&card) & AP_3C505_ASR_ACRE);
  TEST_ASSERT_FALSE(ap_3c505_adapter_status(&card) & AP_3C505_ASR_HCRF);

  /* The host sends. Its register is no longer empty, and the adapter is told
   * a byte is waiting -- the same byte, described twice. */
  ap_3c505_write(&card, AP_3C505_REG_COMMAND, AP_3C505_CMD_SELF_TEST);
  TEST_ASSERT_FALSE(ap_3c505_host_status(&card) & AP_3C505_HSR_HCRE);
  TEST_ASSERT_TRUE(ap_3c505_adapter_status(&card) & AP_3C505_ASR_HCRF);

  /* The adapter takes it, and both sides see that at once. */
  uint8_t taken = 0;
  TEST_ASSERT_TRUE(ap_3c505_adapter_take_command(&card, &taken));
  TEST_ASSERT_EQUAL_HEX8(AP_3C505_CMD_SELF_TEST, taken);
  TEST_ASSERT_TRUE(ap_3c505_host_status(&card) & AP_3C505_HSR_HCRE);
  TEST_ASSERT_FALSE(ap_3c505_adapter_status(&card) & AP_3C505_ASR_HCRF);
  TEST_ASSERT_FALSE(ap_3c505_adapter_take_command(&card, &taken));

  /* And the same in the other direction, with the response Table 1 requires. */
  uint8_t response = 0;
  TEST_ASSERT_TRUE(ap_3c505_response_for(AP_3C505_CMD_SELF_TEST, &response));
  ap_3c505_adapter_post_command(&card, response);
  TEST_ASSERT_TRUE(ap_3c505_host_status(&card) & AP_3C505_HSR_ACRF);
  TEST_ASSERT_FALSE(ap_3c505_adapter_status(&card) & AP_3C505_ASR_ACRE);
  TEST_ASSERT_EQUAL_HEX8(response,
                         ap_3c505_read(&card, AP_3C505_REG_COMMAND));
  TEST_ASSERT_FALSE(ap_3c505_host_status(&card) & AP_3C505_HSR_ACRF);
}

/* **"A half duplex 20 byte FIFO"** -- one buffer, and `DIR` points it. The
 * half-duplex word is the whole test: a byte written host-to-adapter must not
 * be readable by the host afterwards, which is what two FIFOs would allow. */
static void test_the_data_fifo_is_half_duplex_and_twenty_bytes(void) {
  ap_3c505_t card;
  ap_3c505_reset(&card);
  ap_3c505_write(&card, AP_3C505_REG_CONTROL, 0u); /* DIR clear: host->adapter */

  for (unsigned i = 0; i < AP_3C505_DATA_FIFO; i++) {
    TEST_ASSERT_TRUE(ap_3c505_host_status(&card) & AP_3C505_HSR_HRDY);
    ap_3c505_write(&card, AP_3C505_REG_DATA, (uint8_t)i);
  }
  /* Full: the host is no longer ready, and the adapter is. */
  TEST_ASSERT_FALSE(ap_3c505_host_status(&card) & AP_3C505_HSR_HRDY);
  TEST_ASSERT_TRUE(ap_3c505_adapter_status(&card) & AP_3C505_ASR_ARDY);

  /* Reading against the direction gets nothing, not the bytes just written. */
  TEST_ASSERT_EQUAL_HEX8(0xFFu, ap_3c505_read(&card, AP_3C505_REG_DATA));
}

/* **Turning the FIFO round empties it.** It is one buffer; bytes queued for one
 * direction are not deliverable in the other, and leaving them would hand the
 * adapter's data back to the host. */
static void test_changing_direction_empties_the_one_buffer(void) {
  ap_3c505_t card;
  ap_3c505_reset(&card);
  ap_3c505_write(&card, AP_3C505_REG_CONTROL, 0u);
  ap_3c505_write(&card, AP_3C505_REG_DATA, 0xA5u);

  ap_3c505_write(&card, AP_3C505_REG_CONTROL, AP_3C505_HCR_DIR);
  TEST_ASSERT_TRUE(ap_3c505_host_status(&card) & AP_3C505_HSR_DIR);
  /* Nothing to read: the byte did not survive the turn. */
  TEST_ASSERT_FALSE(ap_3c505_host_status(&card) & AP_3C505_HSR_HRDY);
  TEST_ASSERT_EQUAL_HEX8(0xFFu, ap_3c505_read(&card, AP_3C505_REG_DATA));
}

/* Either side's flush empties it, `[HIS]` §3-2 and §3-5 -- `FLSH` appears in
 * both control registers, which is only worth having if both act. */
static void test_either_side_can_flush_the_fifo(void) {
  ap_3c505_t card;
  ap_3c505_reset(&card);
  ap_3c505_write(&card, AP_3C505_REG_DATA, 0x11u);
  ap_3c505_write(&card, AP_3C505_REG_CONTROL, AP_3C505_HCR_FLSH);
  TEST_ASSERT_EQUAL_UINT(0u, card.fifo_count);

  ap_3c505_write(&card, AP_3C505_REG_CONTROL, 0u);
  ap_3c505_write(&card, AP_3C505_REG_DATA, 0x22u);
  ap_3c505_adapter_write_control(&card, AP_3C505_ACR_FLSH);
  TEST_ASSERT_EQUAL_UINT(0u, card.fifo_count);
}

/* **The hard reset is the pair, and it is a reset rather than a big flush.**
 * `ATTN|FLSH` must clear the command registers and the host's own control bits,
 * not just the data buffer -- otherwise a host that resets a wedged adapter is
 * left with the wedged adapter's byte still waiting for it. */
static void test_the_hard_reset_clears_the_mailbox_but_not_the_strapping(void) {
  ap_3c505_t card;
  ap_3c505_reset(&card);
  card.test_jumper = true;
  card.sixteen_bit = true;

  ap_3c505_write(&card, AP_3C505_REG_COMMAND, AP_3C505_CMD_SELF_TEST);
  ap_3c505_adapter_post_command(&card, 0x31u);
  ap_3c505_write(&card, AP_3C505_REG_DATA, 0x5Au);
  ap_3c505_adapter_write_control(&card, AP_3C505_ACR_ASF1);

  ap_3c505_write(&card, AP_3C505_REG_CONTROL, AP_3C505_HCR_HARD_RESET);

  TEST_ASSERT_TRUE(ap_3c505_host_status(&card) & AP_3C505_HSR_HCRE);
  TEST_ASSERT_FALSE(ap_3c505_host_status(&card) & AP_3C505_HSR_ACRF);
  TEST_ASSERT_EQUAL_UINT(0u, card.fifo_count);
  TEST_ASSERT_EQUAL_HEX8(0u, card.acr);
  TEST_ASSERT_EQUAL_HEX8(0u, card.hcr);

  /* A jumper and a slot are not state the host can clear. */
  TEST_ASSERT_TRUE(ap_3c505_adapter_status(&card) & AP_3C505_ASR_SWTC);
  TEST_ASSERT_TRUE(ap_3c505_adapter_status(&card) & AP_3C505_ASR_8_16);
}

/* §1.9.5: the general-purpose flags are "not decoded by the hardware in any
 * way", so each side's control bits must appear in the other's status register
 * unchanged and mean nothing here. */
static void test_the_general_purpose_flags_pass_through_uninterpreted(void) {
  ap_3c505_t card;
  ap_3c505_reset(&card);

  ap_3c505_write(&card, AP_3C505_REG_CONTROL,
                 AP_3C505_HCR_HSF1 | AP_3C505_HCR_HSF2);
  TEST_ASSERT_EQUAL_HEX8(AP_3C505_ASR_HSF1 | AP_3C505_ASR_HSF2,
                         ap_3c505_adapter_status(&card) &
                             (AP_3C505_ASR_HSF1 | AP_3C505_ASR_HSF2));

  ap_3c505_adapter_write_control(&card, AP_3C505_ACR_ASF1 | AP_3C505_ACR_ASF3);
  TEST_ASSERT_EQUAL_HEX8(AP_3C505_HSR_ASF1 | AP_3C505_HSR_ASF3,
                         ap_3c505_host_status(&card) &
                             (AP_3C505_HSR_ASF1 | AP_3C505_HSR_ASF2 |
                              AP_3C505_HSR_ASF3));
}

/* §1.10: two independent enables, and neither implies the other. A card that
 * interrupted on a waiting byte regardless of `CMDE` would pass a test that
 * only ever set both. */
static void test_each_interrupt_needs_its_own_enable(void) {
  ap_3c505_t card;
  ap_3c505_reset(&card);

  ap_3c505_adapter_post_command(&card, 0x31u);
  TEST_ASSERT_FALSE(ap_3c505_irq(&card)); /* a byte waits, CMDE clear */
  ap_3c505_write(&card, AP_3C505_REG_CONTROL, AP_3C505_HCR_CMDE);
  TEST_ASSERT_TRUE(ap_3c505_irq(&card));

  ap_3c505_reset(&card);
  card.dma_done = true;
  TEST_ASSERT_FALSE(ap_3c505_irq(&card)); /* terminal count, TCEN clear */
  ap_3c505_write(&card, AP_3C505_REG_CONTROL, AP_3C505_HCR_TCEN);
  TEST_ASSERT_TRUE(ap_3c505_irq(&card));
  TEST_ASSERT_TRUE(ap_3c505_host_status(&card) & AP_3C505_HSR_DONE);
}


/* **The option ROM's probe, byte for byte, against traffic measured a day
 * before this model existed.**
 *
 * `ETHERNET.md` finding 10a tapped the oracle at physical `058000`-`05800F` and
 * caught the card's own option ROM in a four-step cycle: a read-modify-write at
 * `+6` that alternately clears and sets bit 4, then a read of `+2` returning
 * `C0` while that bit is clear and `50` while it is set. Finding 10c decoded
 * those against `[HIS]`: bit 4 is `DIR`, and the two bytes are what an **empty
 * FIFO** reads in the two directions -- on a download `HRDY` set means "not
 * full, send more", on an upload `HRDY` clear means "empty, nothing to read",
 * with `HCRE` set throughout because the host has written no command.
 *
 * This model was built from the manual and knows nothing of that measurement,
 * so the two agreeing is a real check rather than a fit. It is also the
 * strongest test available for the derived-status design: `C0` and `50` differ
 * in three bits, and getting `HRDY`'s sense backwards -- the easy mistake, since
 * "ready" means opposite things by direction -- produces `50` and `C0` swapped.
 */
static void test_an_idle_card_answers_the_probe_the_oracle_measured(void) {
  ap_3c505_t card;
  ap_3c505_reset(&card);

  ap_3c505_write(&card, AP_3C505_REG_CONTROL, 0u);
  TEST_ASSERT_EQUAL_HEX8(0xC0u, ap_3c505_read(&card, AP_3C505_REG_STATUS));

  ap_3c505_write(&card, AP_3C505_REG_CONTROL, AP_3C505_HCR_DIR);
  TEST_ASSERT_EQUAL_HEX8(0x50u, ap_3c505_read(&card, AP_3C505_REG_STATUS));

  /* And the ROM's own step: `+6` is a read-modify-write on Rev 3 hardware, so
   * the value it reads back has to be the one it wrote. */
  TEST_ASSERT_EQUAL_HEX8(AP_3C505_HCR_DIR,
                         ap_3c505_read(&card, AP_3C505_REG_CONTROL));
}


/* ## The PCB transfer, `[DEV]` §3.1.1 -- and the length field does not frame it
 *
 * §3.1 gives the format (`command / length / data`) and one sentence about the
 * path: "The PCB is passed using programmed I/O through the Command Register."
 * The obvious model follows from that -- count the length field, declare the
 * PCB complete -- and it is wrong, which §3.1.1 says and this suite asserted the
 * wrong way round for one commit.
 *
 * "The Adapter uses a 64-byte circular buffer to store the host byte stream ...
 * For protection against stray bytes (from Host aborted PCB transfers), the
 * Adapter does not consider a PCB transfer complete until the Host Status Flags
 * (HSF2 and HSF1) go to state 11. Simultaneously, the TOTAL length of the PCB
 * should be in the Command Register so the true beginning of PCB can be
 * calculated."
 *
 * So completion is a **flag transition** and the frame is found by counting
 * back from a **trailing total length**. The length field inside the PCB cannot
 * locate its own start, which is the entire point: after an aborted transfer
 * the buffer holds stray bytes, and only a total sent afterwards says which of
 * them were the real PCB. */
static void test_a_pcb_is_found_by_counting_back_from_its_total_length(void) {
  ap_3c505_pcb_rx_t rx;
  ap_3c505_pcb_rx_reset(&rx);

  /* `03` Ethernet address with a three-byte data field, then the total: two
   * header bytes plus three data bytes is five. */
  ap_3c505_pcb_rx_byte(&rx, AP_3C505_CMD_GET_ETHERNET_ADDRESS);
  ap_3c505_pcb_rx_byte(&rx, 3u);
  ap_3c505_pcb_rx_byte(&rx, 0xAAu);
  ap_3c505_pcb_rx_byte(&rx, 0xBBu);
  ap_3c505_pcb_rx_byte(&rx, 0xCCu);
  ap_3c505_pcb_rx_byte(&rx, 5u);

  ap_3c505_pcb_t pcb = {0};
  TEST_ASSERT_TRUE(ap_3c505_pcb_rx_end(&rx, &pcb));
  TEST_ASSERT_EQUAL_HEX8(AP_3C505_CMD_GET_ETHERNET_ADDRESS, pcb.command);
  TEST_ASSERT_EQUAL_HEX8(3u, pcb.length);
  TEST_ASSERT_EQUAL_HEX8(0xAAu, pcb.data[0]);
  TEST_ASSERT_EQUAL_HEX8(0xCCu, pcb.data[2]);
  TEST_ASSERT_EQUAL_HEX8(5u, ap_3c505_pcb_total_length(&pcb));
}

/* **The case the design exists for**: a host aborts a transfer part way, leaves
 * stray bytes in the buffer, and sends a whole PCB afterwards. Counting forward
 * from the first byte finds the abandoned prefix; counting back from the total
 * finds the PCB. This is the test that fails against the framing this suite
 * had before §3.1.1 was read. */
static void test_stray_bytes_from_an_aborted_transfer_are_skipped(void) {
  ap_3c505_pcb_rx_t rx;
  ap_3c505_pcb_rx_reset(&rx);

  /* An abandoned PCB: a command code, a length promising four bytes, and only
   * two of them before the host gave up. */
  ap_3c505_pcb_rx_byte(&rx, AP_3C505_CMD_TRANSMIT_PACKET);
  ap_3c505_pcb_rx_byte(&rx, 4u);
  ap_3c505_pcb_rx_byte(&rx, 0xDEu);
  ap_3c505_pcb_rx_byte(&rx, 0xADu);

  /* Then a real one, complete, with its total. */
  ap_3c505_pcb_rx_byte(&rx, AP_3C505_CMD_SELF_TEST);
  ap_3c505_pcb_rx_byte(&rx, 0u);
  ap_3c505_pcb_rx_byte(&rx, 2u);

  ap_3c505_pcb_t pcb = {0};
  TEST_ASSERT_TRUE(ap_3c505_pcb_rx_end(&rx, &pcb));
  TEST_ASSERT_EQUAL_HEX8(AP_3C505_CMD_SELF_TEST, pcb.command);
  TEST_ASSERT_EQUAL_HEX8(0u, pcb.length);
}

/* A total that disagrees with the data-length field is not one PCB, however
 * plausible each byte is alone -- the two are redundant on purpose and the
 * redundancy is the check. */
static void test_a_total_that_contradicts_the_length_field_is_refused(void) {
  ap_3c505_pcb_rx_t rx;
  ap_3c505_pcb_rx_reset(&rx);
  ap_3c505_pcb_rx_byte(&rx, AP_3C505_CMD_SELF_TEST);
  ap_3c505_pcb_rx_byte(&rx, 1u); /* claims one data byte */
  ap_3c505_pcb_rx_byte(&rx, 0x99u);
  ap_3c505_pcb_rx_byte(&rx, 2u); /* total says there were none */
  TEST_ASSERT_FALSE(ap_3c505_pcb_rx_end(&rx, NULL));

  /* And a total naming more bytes than the host ever sent. */
  ap_3c505_pcb_rx_reset(&rx);
  ap_3c505_pcb_rx_byte(&rx, AP_3C505_CMD_SELF_TEST);
  ap_3c505_pcb_rx_byte(&rx, 40u);
  TEST_ASSERT_FALSE(ap_3c505_pcb_rx_end(&rx, NULL));
}

/* §3.1.1's four-state code, carried in the two flags §1.9.5 says the *hardware*
 * must not decode. Both are true at once, and that is the separation: the
 * register model passes them through, and this layer -- which replaces the
 * firmware -- is where they mean something. */
static void test_the_status_flags_carry_the_four_state_code(void) {
  ap_3c505_t card;
  ap_3c505_reset(&card);

  const ap_3c505_sf_t states[] = {
      AP_3C505_SF_UNDEFINED, AP_3C505_SF_ACCEPTED,
      AP_3C505_SF_REJECTED, AP_3C505_SF_END_OF_PCB};
  for (unsigned i = 0; i < 4u; i++) {
    ap_3c505_set_host_flags(&card, states[i]);
    TEST_ASSERT_EQUAL_INT(states[i], ap_3c505_host_flags(&card));
    ap_3c505_set_adapter_flags(&card, states[i]);
    TEST_ASSERT_EQUAL_INT(states[i], ap_3c505_adapter_flags(&card));
  }

  /* Setting a state must not disturb the rest of the control register: `DIR`
   * and the interrupt enables live in the same byte as the host's flags. */
  ap_3c505_reset(&card);
  ap_3c505_write(&card, AP_3C505_REG_CONTROL,
                 AP_3C505_HCR_DIR | AP_3C505_HCR_CMDE);
  ap_3c505_set_host_flags(&card, AP_3C505_SF_END_OF_PCB);
  TEST_ASSERT_TRUE(card.hcr & AP_3C505_HCR_DIR);
  TEST_ASSERT_TRUE(card.hcr & AP_3C505_HCR_CMDE);
  TEST_ASSERT_EQUAL_INT(AP_3C505_SF_END_OF_PCB, ap_3c505_host_flags(&card));
}

/* End to end over the hardware model, the way §3.1.2 describes it: the host
 * writes the PCB body through the command register a byte at a time under the
 * `HCRE` handshake, sets its flags to `11`, and writes the total length. */
static void test_a_pcb_crosses_the_real_command_register(void) {
  ap_3c505_t card;
  ap_3c505_reset(&card);
  ap_3c505_pcb_rx_t rx;
  ap_3c505_pcb_rx_reset(&rx);

  const uint8_t body[] = {AP_3C505_CMD_CONFIGURE_82586, 2u, 0x5Au, 0xA5u};
  for (unsigned i = 0; i < sizeof body; i++) {
    TEST_ASSERT_TRUE(ap_3c505_host_status(&card) & AP_3C505_HSR_HCRE);
    ap_3c505_write(&card, AP_3C505_REG_COMMAND, body[i]);
    uint8_t taken = 0;
    TEST_ASSERT_TRUE(ap_3c505_adapter_take_command(&card, &taken));
    ap_3c505_pcb_rx_byte(&rx, taken);
  }

  /* "Set the Host status flags to state 11 before writing the length." */
  ap_3c505_set_host_flags(&card, AP_3C505_SF_END_OF_PCB);
  TEST_ASSERT_EQUAL_INT(AP_3C505_SF_END_OF_PCB,
                        (ap_3c505_sf_t)(ap_3c505_adapter_status(&card) &
                                        (AP_3C505_ASR_HSF1 | AP_3C505_ASR_HSF2)));
  ap_3c505_write(&card, AP_3C505_REG_COMMAND, 4u);
  uint8_t total = 0;
  TEST_ASSERT_TRUE(ap_3c505_adapter_take_command(&card, &total));
  ap_3c505_pcb_rx_byte(&rx, total);

  ap_3c505_pcb_t pcb = {0};
  TEST_ASSERT_TRUE(ap_3c505_pcb_rx_end(&rx, &pcb));
  TEST_ASSERT_EQUAL_HEX8(AP_3C505_CMD_CONFIGURE_82586, pcb.command);
  TEST_ASSERT_EQUAL_HEX8(0xA5u, pcb.data[1]);

  /* And the adapter answers with acceptance, which the host reads back. */
  ap_3c505_set_adapter_flags(&card, AP_3C505_SF_ACCEPTED);
  TEST_ASSERT_EQUAL_INT(AP_3C505_SF_ACCEPTED,
                        (ap_3c505_sf_t)(ap_3c505_host_status(&card) &
                                        (AP_3C505_HSR_ASF1 | AP_3C505_HSR_ASF2)));
}

/* The other direction reassembles, §3.1.3 being §3.1.2 mirrored. */
static void test_a_pcb_sent_out_reassembles_at_the_other_end(void) {
  const ap_3c505_pcb_t out = {
      .command = 0x3Fu, .length = 2u, .data = {0x12u, 0x34u}};
  ap_3c505_pcb_tx_t tx;
  ap_3c505_pcb_tx_start(&tx, &out);

  ap_3c505_pcb_rx_t rx;
  ap_3c505_pcb_rx_reset(&rx);

  uint8_t byte = 0;
  unsigned bytes = 0;
  while (ap_3c505_pcb_tx_next(&tx, &byte)) {
    ap_3c505_pcb_rx_byte(&rx, byte);
    bytes++;
  }
  TEST_ASSERT_EQUAL_UINT(4u, bytes);
  ap_3c505_pcb_rx_byte(&rx, ap_3c505_pcb_total_length(&out));

  ap_3c505_pcb_t back = {0};
  TEST_ASSERT_TRUE(ap_3c505_pcb_rx_end(&rx, &back));
  TEST_ASSERT_EQUAL_HEX8(0x3Fu, back.command);
  TEST_ASSERT_EQUAL_HEX8(2u, back.length);
  TEST_ASSERT_EQUAL_HEX8(0x34u, back.data[1]);
}


/* §3.2.2 `33H`: "The Adapter returns the 6-byte Ethernet address ... previously
 * read from the Ethernet address PROM." The address is the adapter's own, not
 * anything in the request, so a dispatch that echoed the request would pass a
 * weaker test than this one. */
static void test_the_address_command_returns_the_adapters_own_address(void) {
  const uint8_t prom[6] = {0x02u, 0x60u, 0x8Cu, 0x11u, 0x22u, 0x33u};
  ap_3c505_adapter_t adapter;
  ap_3c505_adapter_init(&adapter, prom);

  const ap_3c505_pcb_t in = {.command = AP_3C505_CMD_GET_ETHERNET_ADDRESS,
                             .length = 0u};
  ap_3c505_pcb_t out = {0};
  TEST_ASSERT_TRUE(ap_3c505_dispatch(&adapter, &in, &out));
  TEST_ASSERT_EQUAL_HEX8(0x33u, out.command);
  TEST_ASSERT_EQUAL_HEX8(6u, out.length);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(prom, out.data, 6);

  /* And `10H` sets it, which `03H` must then report back. */
  const uint8_t assigned[6] = {0x08u, 0x00u, 0x1Eu, 0xAAu, 0xBBu, 0xCCu};
  ap_3c505_pcb_t set = {.command = AP_3C505_CMD_SET_ETHERNET_ADDRESS,
                        .length = 6u};
  for (unsigned i = 0; i < 6u; i++) {
    set.data[i] = assigned[i];
  }
  TEST_ASSERT_TRUE(ap_3c505_dispatch(&adapter, &set, &out));
  TEST_ASSERT_EQUAL_HEX8(0x40u, out.command);
  TEST_ASSERT_TRUE(ap_3c505_dispatch(&adapter, &in, &out));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(assigned, out.data, 6);
}

/* **A response code is its command plus `0x30`, and dispatch must obey the same
 * rule the command table does.** The two were established separately -- the
 * table from Table 1, these from §3.2.2's individual formats -- so agreeing is
 * a check that the implementation follows the documented set rather than its
  * author's memory of it. */
static void test_every_dispatched_response_is_its_command_plus_thirty(void) {
  ap_3c505_adapter_t adapter;
  ap_3c505_adapter_init(&adapter, NULL);

  const uint8_t commands[] = {
      AP_3C505_CMD_CONFIGURE_ADAPTER_MEMORY, AP_3C505_CMD_CONFIGURE_82586,
      AP_3C505_CMD_GET_ETHERNET_ADDRESS,     AP_3C505_CMD_LOAD_MULTICAST_LIST,
      AP_3C505_CMD_NETWORK_STATISTICS,       AP_3C505_CMD_SET_ETHERNET_ADDRESS,
  };
  for (unsigned i = 0; i < sizeof commands; i++) {
    ap_3c505_pcb_t in = {.command = commands[i], .length = 0u};
    if (commands[i] == AP_3C505_CMD_CONFIGURE_82586) {
      in.length = 2u;
    }
    if (commands[i] == AP_3C505_CMD_SET_ETHERNET_ADDRESS) {
      in.length = 6u;
    }
    ap_3c505_pcb_t out = {0};
    TEST_ASSERT_TRUE(ap_3c505_dispatch(&adapter, &in, &out));

    uint8_t expected = 0;
    TEST_ASSERT_TRUE(ap_3c505_response_for(commands[i], &expected));
    TEST_ASSERT_EQUAL_HEX8(expected, out.command);
  }
}

/* "There is no adapter response PCB for this command" -- the four transfers,
 * which is the same fact as Table 1's two `n/a` codes seen from the other side.
 * A dispatch that answered them would put a byte in the command register the
 * host is not waiting for. */
static void test_the_transfer_commands_produce_no_response(void) {
  ap_3c505_adapter_t adapter;
  ap_3c505_adapter_init(&adapter, NULL);
  const uint8_t transfers[] = {
      AP_3C505_CMD_DOWNLOAD_DATA_DMA, AP_3C505_CMD_UPLOAD_DATA_DMA,
      AP_3C505_CMD_DOWNLOAD_DATA_PIO, AP_3C505_CMD_UPLOAD_DATA_PIO};
  for (unsigned i = 0; i < sizeof transfers; i++) {
    const ap_3c505_pcb_t in = {.command = transfers[i], .length = 6u};
    ap_3c505_pcb_t out = {0};
    TEST_ASSERT_FALSE(ap_3c505_dispatch(&adapter, &in, &out));
  }
}

/* §3.2.1 `02H`'s receive mode is stored rather than acted on -- there is no
 * 82586 here to configure -- but it must survive, because what the adapter
 * accepts off the wire is decided by it. */
static void test_the_receive_mode_is_kept_as_the_host_set_it(void) {
  ap_3c505_adapter_t adapter;
  ap_3c505_adapter_init(&adapter, NULL);
  ap_3c505_pcb_t in = {.command = AP_3C505_CMD_CONFIGURE_82586, .length = 2u};
  /* Promiscuous, with the 82586's internal loopback. */
  in.data[0] = AP_3C505_RX_PROMISCUOUS |
               (AP_3C505_LOOPBACK_INTERNAL << AP_3C505_LOOPBACK_SHIFT);
  in.data[1] = 0u;
  ap_3c505_pcb_t out = {0};
  TEST_ASSERT_TRUE(ap_3c505_dispatch(&adapter, &in, &out));

  TEST_ASSERT_EQUAL_UINT(AP_3C505_RX_PROMISCUOUS,
                         adapter.receive_mode & AP_3C505_RX_MODE_MASK);
  TEST_ASSERT_EQUAL_UINT(AP_3C505_LOOPBACK_INTERNAL,
                         (adapter.receive_mode & AP_3C505_LOOPBACK_MASK) >>
                             AP_3C505_LOOPBACK_SHIFT);
}

/* "A zero length list will cause the Adapter to clear all multicast addresses",
 * and ten is the most one PCB can carry. */
static void test_the_multicast_list_loads_and_a_zero_length_clears_it(void) {
  ap_3c505_adapter_t adapter;
  ap_3c505_adapter_init(&adapter, NULL);

  ap_3c505_pcb_t in = {.command = AP_3C505_CMD_LOAD_MULTICAST_LIST,
                       .length = 12u};
  for (unsigned i = 0; i < 12u; i++) {
    in.data[i] = (uint8_t)(0x10u + i);
  }
  ap_3c505_pcb_t out = {0};
  TEST_ASSERT_TRUE(ap_3c505_dispatch(&adapter, &in, &out));
  TEST_ASSERT_EQUAL_HEX8(0x3Bu, out.command);
  TEST_ASSERT_EQUAL_UINT(2u, adapter.multicast_count);
  TEST_ASSERT_EQUAL_HEX8(0x16u, adapter.multicast[1][0]);

  in.length = 0u;
  TEST_ASSERT_TRUE(ap_3c505_dispatch(&adapter, &in, &out));
  TEST_ASSERT_EQUAL_UINT(0u, adapter.multicast_count);

  /* Eleven addresses is more than one PCB may carry, and a partial list would
   * be worse than a refusal. */
  in.length = 11u * 6u;
  TEST_ASSERT_FALSE(ap_3c505_dispatch(&adapter, &in, &out));
}

/* §3.2.2 `3AH` gives the counters, their order and their widths, and says "The
 * Adapter clears all statistics after sending the response" -- so reading them
 * twice must give the counts and then zero. */
static void test_the_statistics_are_reported_then_cleared(void) {
  ap_3c505_adapter_t adapter;
  ap_3c505_adapter_init(&adapter, NULL);
  adapter.receive_packets = 0x11223344u;
  adapter.transmit_packets = 7u;
  adapter.crc_errors = 0x0102u;

  const ap_3c505_pcb_t in = {.command = AP_3C505_CMD_NETWORK_STATISTICS,
                             .length = 0u};
  ap_3c505_pcb_t out = {0};
  TEST_ASSERT_TRUE(ap_3c505_dispatch(&adapter, &in, &out));
  TEST_ASSERT_EQUAL_HEX8(0x3Au, out.command);
  TEST_ASSERT_EQUAL_HEX8(0x0Cu, out.length);
  /* Little-endian, because the fields are `dd`/`dw` on an 80186. */
  TEST_ASSERT_EQUAL_HEX8(0x44u, out.data[0]);
  TEST_ASSERT_EQUAL_HEX8(0x11u, out.data[3]);
  TEST_ASSERT_EQUAL_HEX8(0x07u, out.data[4]);
  TEST_ASSERT_EQUAL_HEX8(0x02u, out.data[8]);

  TEST_ASSERT_TRUE(ap_3c505_dispatch(&adapter, &in, &out));
  TEST_ASSERT_EQUAL_HEX8(0u, out.data[0]);
  TEST_ASSERT_EQUAL_HEX8(0u, out.data[3]);
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
  RUN_TEST(test_the_command_byte_is_one_fact_both_sides_agree_on);
  RUN_TEST(test_the_data_fifo_is_half_duplex_and_twenty_bytes);
  RUN_TEST(test_changing_direction_empties_the_one_buffer);
  RUN_TEST(test_either_side_can_flush_the_fifo);
  RUN_TEST(test_the_hard_reset_clears_the_mailbox_but_not_the_strapping);
  RUN_TEST(test_the_general_purpose_flags_pass_through_uninterpreted);
  RUN_TEST(test_each_interrupt_needs_its_own_enable);
  RUN_TEST(test_an_idle_card_answers_the_probe_the_oracle_measured);
  RUN_TEST(test_a_pcb_is_found_by_counting_back_from_its_total_length);
  RUN_TEST(test_stray_bytes_from_an_aborted_transfer_are_skipped);
  RUN_TEST(test_a_total_that_contradicts_the_length_field_is_refused);
  RUN_TEST(test_the_status_flags_carry_the_four_state_code);
  RUN_TEST(test_a_pcb_crosses_the_real_command_register);
  RUN_TEST(test_a_pcb_sent_out_reassembles_at_the_other_end);
  RUN_TEST(test_the_address_command_returns_the_adapters_own_address);
  RUN_TEST(test_every_dispatched_response_is_its_command_plus_thirty);
  RUN_TEST(test_the_transfer_commands_produce_no_response);
  RUN_TEST(test_the_receive_mode_is_kept_as_the_host_set_it);
  RUN_TEST(test_the_multicast_list_loads_and_a_zero_length_clears_it);
  RUN_TEST(test_the_statistics_are_reported_then_cleared);
  return UNITY_END();
}
