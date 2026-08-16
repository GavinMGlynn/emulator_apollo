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
  /* **The Host Control Register survives, and this assertion used to say it was
   * cleared.** §1.12: the adapter reset "is similar to the power on reset
   * except that the Host Control Register is not affected" -- clearing it would
   * drop the `ATTN`/`FLSH` the host is holding the adapter in reset with. */
  TEST_ASSERT_EQUAL_HEX8(AP_3C505_HCR_HARD_RESET, card.hcr);

  /* **`acr` is *not* zero here, and this assertion used to say it was.** The
   * reset does clear what the adapter had written -- the `ASF1` above is gone
   * -- but the board does not then sit with its flags down: the card's own
   * option ROM hard-resets it and immediately polls for `(HSR & 3) == 3`
   * (`3000_3C505_010728-00` `$3C2`-`$3D8`), so a healthy adapter comes out of
   * reset *initialising*. The old `acr == 0` encoded "nothing drives the
   * adapter half", which was true of the model and not of the hardware. */
  TEST_ASSERT_EQUAL_HEX8(AP_3C505_ACR_ASF1 | AP_3C505_ACR_ASF2, card.acr);
  TEST_ASSERT_EQUAL_UINT8(3u, ap_3c505_host_status(&card) & 0x03u);

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


/* `[DEV]` §1.9.4's truth table, host side: the request is `HRDY` in **both**
 * directions, and `DIR` selects only whether the cycle reads or writes. The two
 * `ARDY` rows are the adapter's request, not the host's, and a model that read
 * the table down the wrong column would gate the host's line on the flag that
 * is *clear* exactly when the host's is set. That is what this checks: an empty
 * FIFO is ready to be filled on a download and has nothing to give on an
 * upload, so the request must follow the first and not the second. */
static void test_the_host_dma_request_follows_hrdy_in_both_directions(void) {
  ap_3c505_t card;
  ap_3c505_reset(&card);

  /* Download, DIR clear: an empty FIFO has room, so the card asks. */
  ap_3c505_write(&card, AP_3C505_REG_CONTROL, AP_3C505_HCR_DMAE);
  TEST_ASSERT_TRUE(ap_3c505_host_status(&card) & AP_3C505_HSR_HRDY);
  TEST_ASSERT_TRUE(ap_3c505_dma_request(&card));

  /* Upload, DIR set: the same empty FIFO has nothing, so it does not.
   * `ARDY` is set here -- the adapter has room to write -- which is precisely
   * the row that must not drive this line. */
  ap_3c505_write(&card, AP_3C505_REG_CONTROL,
                 (uint8_t)(AP_3C505_HCR_DMAE | AP_3C505_HCR_DIR));
  TEST_ASSERT_TRUE(ap_3c505_adapter_status(&card) & AP_3C505_ASR_ARDY);
  TEST_ASSERT_FALSE(ap_3c505_host_status(&card) & AP_3C505_HSR_HRDY);
  TEST_ASSERT_FALSE(ap_3c505_dma_request(&card));
}

/* §1.9.4's first sentence: transfers "are enabled using the DMAE bit", and
 * "since the DMA channel floats when this bit is cleared ... another I/O card
 * may use the same DMA channel". A card that asked with the enable clear would
 * be driving a line it is not connected to. */
static void test_a_cleared_dma_enable_takes_the_card_off_the_channel(void) {
  ap_3c505_t card;
  ap_3c505_reset(&card);

  TEST_ASSERT_FALSE(ap_3c505_dma_request(&card)); /* DMAE clear at reset */
  ap_3c505_write(&card, AP_3C505_REG_CONTROL, AP_3C505_HCR_DMAE);
  TEST_ASSERT_TRUE(ap_3c505_dma_request(&card));
  ap_3c505_write(&card, AP_3C505_REG_CONTROL, 0u);
  TEST_ASSERT_FALSE(ap_3c505_dma_request(&card));
}

/* §1.9.4 condition 3, and `[HIS]` p. 3-5 in the same words: "If the Burst bit
 * is not set, demand mode DMA transfers by the host will pause every 9
 * transfers to allow the PC to refresh its dynamic RAMs. If the Burst bit is
 * set, no such pause will occur."
 *
 * The pause is **one** sample long -- §1.9.4 says the channel is relinquished
 * "for one host CPU cycle" -- so the tenth sample is inactive and the eleventh
 * asks again. A model that latched the pause would stall the transfer, and one
 * that never paused would differ from the hardware only under demand mode,
 * which is exactly where a driver would meet it. */
static void test_demand_mode_pauses_every_ninth_transfer_unless_burst(void) {
  ap_3c505_t card;
  ap_3c505_reset(&card);
  ap_3c505_write(&card, AP_3C505_REG_CONTROL, AP_3C505_HCR_DMAE);

  /* Nine download cycles, each preceded by the request the board samples. */
  for (unsigned i = 0; i < AP_3C505_DMA_DEMAND_BURST; i++) {
    TEST_ASSERT_TRUE(ap_3c505_dma_request(&card));
    ap_3c505_dma_write(&card, (uint8_t)i);
  }
  /* The tenth sample is the pause, and the eleventh is asking again. */
  TEST_ASSERT_FALSE(ap_3c505_dma_request(&card));
  TEST_ASSERT_TRUE(ap_3c505_dma_request(&card));

  /* With `BRST` set there is no pause at all. The FIFO is twenty bytes and the
   * run is nine, so nothing here is limited by capacity. */
  ap_3c505_reset(&card);
  ap_3c505_write(&card, AP_3C505_REG_AUX_DMA, AP_3C505_AUX_BRST);
  ap_3c505_write(&card, AP_3C505_REG_CONTROL, AP_3C505_HCR_DMAE);
  for (unsigned i = 0; i < AP_3C505_DMA_DEMAND_BURST; i++) {
    TEST_ASSERT_TRUE(ap_3c505_dma_request(&card));
    ap_3c505_dma_write(&card, (uint8_t)i);
  }
  TEST_ASSERT_TRUE(ap_3c505_dma_request(&card));
}

/* `[HIS]` p. 3-4: "The DONE flag is set when a DMA transfer between the host
 * and the Data Register is complete ... The DONE bit is cleared by clearing the
 * DMAE bit in the Host Control Register." Both halves, and the second is the
 * one a model invents wrongly -- there is no other documented way to clear it,
 * so a card that cleared `DONE` on the next transfer, or on a status read,
 * would drop an interrupt the host has not acknowledged. */
static void test_done_is_set_by_terminal_count_and_cleared_by_the_enable(void) {
  ap_3c505_t card;
  ap_3c505_reset(&card);
  ap_3c505_write(&card, AP_3C505_REG_CONTROL, AP_3C505_HCR_DMAE);

  TEST_ASSERT_FALSE(ap_3c505_host_status(&card) & AP_3C505_HSR_DONE);
  ap_3c505_dma_terminal_count(&card);
  TEST_ASSERT_TRUE(ap_3c505_host_status(&card) & AP_3C505_HSR_DONE);

  /* Condition 1: the transfer is over, so the line drops even though the FIFO
   * still has room and `HRDY` is still set. */
  TEST_ASSERT_TRUE(ap_3c505_host_status(&card) & AP_3C505_HSR_HRDY);
  TEST_ASSERT_FALSE(ap_3c505_dma_request(&card));

  /* Reading the status does not clear it; clearing `DMAE` does. */
  TEST_ASSERT_TRUE(ap_3c505_host_status(&card) & AP_3C505_HSR_DONE);
  ap_3c505_write(&card, AP_3C505_REG_CONTROL, 0u);
  TEST_ASSERT_FALSE(ap_3c505_host_status(&card) & AP_3C505_HSR_DONE);
}

/* §1.9.4: "the Adapter and the Host may perform DMA transfers independent of
 * one another. That is, one may use polled I/O while the other performs DMA."
 * So a DMA cycle and a host `in`/`out` reach the same twenty-byte FIFO, and a
 * byte written by one comes back to the other. A second data path would be a
 * second register, which this card does not have. */
static void test_a_dma_cycle_and_polled_io_share_one_data_register(void) {
  ap_3c505_t card;
  ap_3c505_reset(&card);
  ap_3c505_write(&card, AP_3C505_REG_CONTROL, AP_3C505_HCR_DMAE);

  /* Downloaded by DMA, taken out by the adapter's side of the same FIFO. */
  ap_3c505_dma_write(&card, 0xA5u);
  ap_3c505_dma_write(&card, 0x5Au);
  TEST_ASSERT_TRUE(ap_3c505_adapter_status(&card) & AP_3C505_ASR_ARDY);

  /* Turn the FIFO round: the direction change empties it, which is the
   * half-duplex rule this suite already holds up elsewhere. */
  ap_3c505_write(&card, AP_3C505_REG_CONTROL,
                 (uint8_t)(AP_3C505_HCR_DMAE | AP_3C505_HCR_DIR));
  TEST_ASSERT_FALSE(ap_3c505_host_status(&card) & AP_3C505_HSR_HRDY);

  /* An upload staged by polled I/O and collected by DMA, the other way round
   * from the first half. */
  ap_3c505_write(&card, AP_3C505_REG_CONTROL, AP_3C505_HCR_DMAE);
  ap_3c505_write(&card, AP_3C505_REG_DATA, 0x3Cu);
  ap_3c505_write(&card, AP_3C505_REG_CONTROL,
                 (uint8_t)(AP_3C505_HCR_DMAE | AP_3C505_HCR_DIR));
  /* The turn emptied it again, so stage the byte in the upload direction as the
   * adapter would and read it back through a DMA cycle. */
  card.fifo[0] = 0x3Cu;
  card.fifo_count = 1u;
  TEST_ASSERT_TRUE(ap_3c505_dma_request(&card));
  TEST_ASSERT_EQUAL_UINT8(0x3Cu, ap_3c505_dma_read(&card));
  TEST_ASSERT_FALSE(ap_3c505_dma_request(&card));
}

/* **The adapter's power-on, specified by the card's own option ROM.**
 *
 * `3000_3C505_010728-00`'s `entry_05` hard-resets the board -- `HCR = $C0`,
 * then `HCR = $00` -- and then polls `HSR`'s low two bits twice over: `$3CC`
 * loops until `(HSR & 3) == 3` and `$3F0` until `(HSR & 3) == 0`, each with its
 * own timeout and each branching to the same `$E08008F2` failure. So a healthy
 * board raises both adapter status flags while it initialises and drops them
 * when it is ready, and a model that never raised them fails the first poll
 * while one that never dropped them fails the second.
 *
 * The order is what the test pins: `11` must be readable immediately after the
 * reset write, because the first poll's budget is `50 x arg` iterations of a
 * four-instruction loop. */
static void test_the_adapter_signals_its_power_on_through_the_status_flags(
    void) {
  ap_3c505_t card;
  ap_3c505_adapter_t adapter;
  ap_3c505_reset(&card);
  ap_3c505_adapter_init(&adapter, NULL);

  /* Idle: nothing initialising, so the flags read `00` and the *first* poll
   * would time out. This is the state the self-test used to meet. */
  TEST_ASSERT_EQUAL_UINT8(0u, ap_3c505_host_status(&card) & 0x03u);

  /* The hard reset, exactly as `$3C2`/`$3C8` write it. */
  ap_3c505_write(&card, AP_3C505_REG_CONTROL, AP_3C505_HCR_HARD_RESET);
  TEST_ASSERT_EQUAL_UINT8(3u, ap_3c505_host_status(&card) & 0x03u);
  ap_3c505_write(&card, AP_3C505_REG_CONTROL, 0u);
  /* Still `11` after the release: the adapter is booting, not reset. */
  TEST_ASSERT_EQUAL_UINT8(3u, ap_3c505_host_status(&card) & 0x03u);

  /* The adapter finishing, which is the second poll's condition. */
  TEST_ASSERT_TRUE(ap_3c505_pump(&card, &adapter));
  TEST_ASSERT_EQUAL_UINT8(0u, ap_3c505_host_status(&card) & 0x03u);

  /* And it is a one-shot: a later pump has no power-on left to finish, so it
   * must not keep answering true and must not disturb the flags. */
  TEST_ASSERT_FALSE(ap_3c505_pump(&card, &adapter));
  TEST_ASSERT_EQUAL_UINT8(0u, ap_3c505_host_status(&card) & 0x03u);
}

/* The `DIR` loop the same self-test opens with, ten times round: `$382` clears
 * `HCR` bit 4 and requires `HSR` bit 4 clear, then sets it and requires it set.
 * This is `ETHERNET.md` finding 10a's measured probe handshake seen from the
 * firmware's side -- the traffic was tapped from the oracle before this model
 * existed, and here is the code that produced it. */
static void test_the_status_direction_bit_follows_the_control_one(void) {
  ap_3c505_t card;
  ap_3c505_reset(&card);

  for (unsigned i = 0; i < 10u; i++) {
    ap_3c505_write(&card, AP_3C505_REG_CONTROL, 0u);
    TEST_ASSERT_EQUAL_UINT8(0u, ap_3c505_host_status(&card) &
                                    AP_3C505_HSR_DIR);
    ap_3c505_write(&card, AP_3C505_REG_CONTROL, AP_3C505_HCR_DIR);
    TEST_ASSERT_EQUAL_UINT8(AP_3C505_HSR_DIR,
                            ap_3c505_host_status(&card) & AP_3C505_HSR_DIR);
  }
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
  adapter.no_resource_errors = 0x0304u;
  adapter.overrun_errors = 0x0506u;

  const ap_3c505_pcb_t in = {.command = AP_3C505_CMD_NETWORK_STATISTICS,
                             .length = 0u};
  ap_3c505_pcb_t out = {0};
  TEST_ASSERT_TRUE(ap_3c505_dispatch(&adapter, &in, &out));
  TEST_ASSERT_EQUAL_HEX8(0x3Au, out.command);
  /* `10H`, not the `0CH` §3.2.2 prints beside the field list: `[HIS]` Appendix
   * F says the two packet counters became double words in Revision 2.0, and
   * six words is exactly the `0CH` that was left behind. All six fit. */
  TEST_ASSERT_EQUAL_HEX8(0x10u, out.length);
  /* Little-endian, because the fields are `dd`/`dw` on an 80186. */
  TEST_ASSERT_EQUAL_HEX8(0x44u, out.data[0]);
  TEST_ASSERT_EQUAL_HEX8(0x11u, out.data[3]);
  TEST_ASSERT_EQUAL_HEX8(0x07u, out.data[4]);
  TEST_ASSERT_EQUAL_HEX8(0x02u, out.data[8]);
  /* The two that used to fall off the end of the short length. */
  TEST_ASSERT_EQUAL_HEX8(0x04u, out.data[12]);
  TEST_ASSERT_EQUAL_HEX8(0x03u, out.data[13]);
  TEST_ASSERT_EQUAL_HEX8(0x06u, out.data[14]);
  TEST_ASSERT_EQUAL_HEX8(0x05u, out.data[15]);

  TEST_ASSERT_TRUE(ap_3c505_dispatch(&adapter, &in, &out));
  TEST_ASSERT_EQUAL_HEX8(0u, out.data[0]);
  TEST_ASSERT_EQUAL_HEX8(0u, out.data[3]);
  TEST_ASSERT_EQUAL_HEX8(0u, out.data[12]);
  TEST_ASSERT_EQUAL_HEX8(0u, out.data[15]);
}


/* A wire that records what it was handed, so a transmit can be checked against
 * the frame the host actually downloaded. */
typedef struct {
  uint8_t frame[AP_3C505_FRAME_MAX];
  unsigned length;
  unsigned calls;
  bool answer;
} recording_wire_t;

static bool recording_transmit(void *context, const uint8_t *frame,
                               unsigned length) {
  recording_wire_t *wire = (recording_wire_t *)context;
  wire->calls++;
  wire->length = length;
  for (unsigned i = 0; i < length && i < AP_3C505_FRAME_MAX; i++) {
    wire->frame[i] = frame[i];
  }
  return wire->answer;
}

/* **`09H` arms, the data phase transmits, and `39H` reports.** §3.2.1: "If the
 * PCB is accepted, the Host should DMA download the packet data through the
 * Data Register. When the transmit is complete, the Adapter responds with PCB
 * 39H." So dispatch must answer *nothing* for `09H` -- a response then would be
 * a byte the host is not waiting for -- and the frame must not reach the wire
 * until its last byte has arrived. */
static void test_a_transmit_reaches_the_wire_only_when_its_last_byte_does(void) {
  recording_wire_t wire = {.answer = true};
  ap_3c505_adapter_t adapter;
  ap_3c505_adapter_init(&adapter, NULL);
  adapter.wire.context = &wire;
  adapter.wire.transmit = recording_transmit;

  ap_3c505_pcb_t in = {.command = AP_3C505_CMD_TRANSMIT_PACKET, .length = 6u};
  in.data[0] = 0x00u; in.data[1] = 0x20u; /* offset  */
  in.data[2] = 0x00u; in.data[3] = 0x40u; /* segment */
  in.data[4] = 4u;    in.data[5] = 0u;    /* packet length */

  ap_3c505_pcb_t out = {0};
  TEST_ASSERT_FALSE(ap_3c505_dispatch(&adapter, &in, &out));
  TEST_ASSERT_TRUE(adapter.transmitting);

  const uint8_t frame[4] = {0xDEu, 0xADu, 0xBEu, 0xEFu};
  for (unsigned i = 0; i < 3u; i++) {
    TEST_ASSERT_FALSE(ap_3c505_transmit_byte(&adapter, frame[i], &out));
    TEST_ASSERT_EQUAL_UINT(0u, wire.calls); /* nothing on the wire yet */
  }
  TEST_ASSERT_TRUE(ap_3c505_transmit_byte(&adapter, frame[3], &out));

  TEST_ASSERT_EQUAL_UINT(1u, wire.calls);
  TEST_ASSERT_EQUAL_UINT(4u, wire.length);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(frame, wire.frame, 4);

  /* §3.2.2 `39H`: offset, segment, completion status, 82586 status. */
  TEST_ASSERT_EQUAL_HEX8(0x39u, out.command);
  TEST_ASSERT_EQUAL_HEX8(0x08u, out.length);
  TEST_ASSERT_EQUAL_HEX8(0x20u, out.data[1]); /* the offset, little-endian */
  TEST_ASSERT_EQUAL_HEX8(0x00u, out.data[4]); /* "0 = successful" */
  TEST_ASSERT_EQUAL_HEX8(0x00u, out.data[5]);
  TEST_ASSERT_EQUAL_UINT(1u, adapter.transmit_packets);
}

/* A card whose transceiver is unplugged is a real state, not an error: the
 * frame is still downloaded and `39H` still comes back, carrying a non-zero
 * completion status. */
static void test_a_wire_that_refuses_is_reported_in_the_completion_status(void) {
  recording_wire_t wire = {.answer = false};
  ap_3c505_adapter_t adapter;
  ap_3c505_adapter_init(&adapter, NULL);
  adapter.wire.context = &wire;
  adapter.wire.transmit = recording_transmit;

  ap_3c505_pcb_t in = {.command = AP_3C505_CMD_TRANSMIT_PACKET, .length = 6u};
  in.data[4] = 2u;
  ap_3c505_pcb_t out = {0};
  TEST_ASSERT_FALSE(ap_3c505_dispatch(&adapter, &in, &out));
  TEST_ASSERT_FALSE(ap_3c505_transmit_byte(&adapter, 0x01u, &out));
  TEST_ASSERT_TRUE(ap_3c505_transmit_byte(&adapter, 0x02u, &out));

  TEST_ASSERT_EQUAL_HEX8(0x39u, out.command);
  TEST_ASSERT_NOT_EQUAL(0u, out.data[4] | out.data[5]);
  TEST_ASSERT_EQUAL_UINT(0u, adapter.transmit_packets);
}

/* One station address for the receive tests, so a frame can be addressed to it
 * -- or deliberately not, which is what `02H`'s receive mode decides. */
static const uint8_t station[6] = {0x02u, 0x60u, 0x8Cu, 0x77u, 0x88u, 0x99u};

/* **A frame with nothing waiting for it is a counted loss, not a delivery.**
 * §3.2.2 `3AH` has a "no resources error counter", and this is what it counts:
 * the adapter received a packet and the host had no outstanding `08H`. */
static void test_a_frame_with_no_receive_armed_counts_as_no_resources(void) {
  ap_3c505_adapter_t adapter;
  ap_3c505_adapter_init(&adapter, station);
  /* Addressed to this station, so it passes `02H`'s receive filter and reaches
   * the question this test is about: there is no outstanding `08H`. An
   * unaddressed frame would be dropped earlier and count nothing. */
  const uint8_t frame[8] = {0x02u, 0x60u, 0x8Cu, 0x77u, 0x88u, 0x99u, 1u, 2u};
  ap_3c505_pcb_t out = {0};

  TEST_ASSERT_FALSE(ap_3c505_deliver_frame(&adapter, frame, 8u, &out));
  TEST_ASSERT_EQUAL_UINT(1u, adapter.no_resource_errors);
  TEST_ASSERT_EQUAL_UINT(0u, adapter.receive_packets);
}

/* `08H` arms, a frame arrives, `38H` reports, and the host reads the frame back
 * through the data register. */
static void test_an_armed_receive_stages_the_frame_and_reports_it(void) {
  ap_3c505_adapter_t adapter;
  ap_3c505_adapter_init(&adapter, station);

  ap_3c505_pcb_t in = {.command = AP_3C505_CMD_RECEIVE_PACKET, .length = 8u};
  in.data[0] = 0x10u; in.data[1] = 0x00u;  /* offset */
  in.data[2] = 0x00u; in.data[3] = 0x30u;  /* segment */
  in.data[4] = 64u;   in.data[5] = 0u;     /* buffer length */
  in.data[6] = 0u;    in.data[7] = 0u;     /* timeout: zero is no timeout */

  ap_3c505_pcb_t out = {0};
  TEST_ASSERT_FALSE(ap_3c505_dispatch(&adapter, &in, &out));
  TEST_ASSERT_TRUE(adapter.receive_armed);

  const uint8_t frame[8] = {0x02u, 0x60u, 0x8Cu, 0x77u,
                            0x88u, 0x99u, 0xA3u, 0xA4u};
  TEST_ASSERT_TRUE(ap_3c505_deliver_frame(&adapter, frame, 8u, &out));
  TEST_ASSERT_EQUAL_HEX8(0x38u, out.command);
  TEST_ASSERT_EQUAL_HEX8(0x10u, out.length);
  TEST_ASSERT_EQUAL_HEX8(8u, out.data[4]); /* bytes to be DMA'ed */
  TEST_ASSERT_EQUAL_HEX8(8u, out.data[6]); /* actual packet length */
  TEST_ASSERT_EQUAL_HEX8(0u, out.data[8]); /* completion status */
  TEST_ASSERT_EQUAL_UINT(1u, adapter.receive_packets);

  uint8_t byte = 0;
  for (unsigned i = 0; i < 8u; i++) {
    TEST_ASSERT_TRUE(ap_3c505_receive_byte(&adapter, &byte));
    TEST_ASSERT_EQUAL_HEX8(frame[i], byte);
  }
  TEST_ASSERT_FALSE(ap_3c505_receive_byte(&adapter, &byte));

  /* The request is consumed: one `08H` yields one packet. */
  TEST_ASSERT_FALSE(adapter.receive_armed);
  TEST_ASSERT_FALSE(ap_3c505_deliver_frame(&adapter, frame, 8u, &out));
}

/* "The number of bytes DMA'ed will not exceed the buffer length specified in
 * the receive packet command PCB 8H; extra packet data is discarded." So an
 * oversized frame is truncated rather than refused, and `38H` reports the two
 * lengths separately -- what the host is getting, and what arrived. */
static void test_a_frame_longer_than_the_buffer_is_truncated_and_both_reported(void) {
  ap_3c505_adapter_t adapter;
  ap_3c505_adapter_init(&adapter, station);

  ap_3c505_pcb_t in = {.command = AP_3C505_CMD_RECEIVE_PACKET, .length = 8u};
  in.data[4] = 4u; /* a four-byte buffer */
  ap_3c505_pcb_t out = {0};
  TEST_ASSERT_FALSE(ap_3c505_dispatch(&adapter, &in, &out));

  uint8_t frame[10];
  for (unsigned i = 0; i < 6u; i++) {
    frame[i] = station[i]; /* addressed here, so the filter passes it */
  }
  for (unsigned i = 6; i < 10u; i++) {
    frame[i] = (uint8_t)(0xB0u + i);
  }
  TEST_ASSERT_TRUE(ap_3c505_deliver_frame(&adapter, frame, 10u, &out));
  TEST_ASSERT_EQUAL_HEX8(4u, out.data[4]);  /* DMA'ed */
  TEST_ASSERT_EQUAL_HEX8(10u, out.data[6]); /* actually arrived */

  uint8_t byte = 0;
  for (unsigned i = 0; i < 4u; i++) {
    TEST_ASSERT_TRUE(ap_3c505_receive_byte(&adapter, &byte));
    TEST_ASSERT_EQUAL_HEX8(frame[i], byte);
  }
  TEST_ASSERT_FALSE(ap_3c505_receive_byte(&adapter, &byte));
}


/* **§3.1.3's sequence, paced by the host.** The adapter sends code, length and
 * data through the command register, then sets its flags to `11` and sends the
 * total length. A byte only moves when the host has taken the previous one --
 * §1.9.1's handshake is a single byte each way, so a pump that wrote regardless
 * would lose bytes of the PCB silently and the host would reassemble rubbish. */
static void test_a_queued_pcb_is_paced_out_by_the_host_taking_bytes(void) {
  ap_3c505_t card;
  ap_3c505_adapter_t adapter;
  ap_3c505_reset(&card);
  ap_3c505_adapter_init(&adapter, NULL);

  const ap_3c505_pcb_t response = {
      .command = 0x33u, .length = 2u, .data = {0xAAu, 0xBBu}};
  ap_3c505_adapter_post_pcb(&adapter, &response);

  ap_3c505_pcb_rx_t rx;
  ap_3c505_pcb_rx_reset(&rx);

  /* One byte moves, and a second does not until the host reads. */
  TEST_ASSERT_TRUE(ap_3c505_pump(&card, &adapter));
  TEST_ASSERT_FALSE(ap_3c505_pump(&card, &adapter));
  TEST_ASSERT_TRUE(ap_3c505_host_status(&card) & AP_3C505_HSR_ACRF);

  unsigned bytes = 0;
  while (bytes < 16u) {
    if ((ap_3c505_host_status(&card) & AP_3C505_HSR_ACRF) == 0u) {
      if (!ap_3c505_pump(&card, &adapter)) {
        break;
      }
    }
    ap_3c505_pcb_rx_byte(&rx, ap_3c505_read(&card, AP_3C505_REG_COMMAND));
    bytes++;
    (void)ap_3c505_pump(&card, &adapter);
  }

  /* Code, length, two data bytes and the total: five. */
  TEST_ASSERT_EQUAL_UINT(5u, bytes);

  /* And the flags say end-of-PCB by the time the total arrives, which is what
   * tells the host the byte is a length and not more data. */
  TEST_ASSERT_EQUAL_INT(AP_3C505_SF_END_OF_PCB, ap_3c505_adapter_flags(&card));

  ap_3c505_pcb_t back = {0};
  TEST_ASSERT_TRUE(ap_3c505_pcb_rx_end(&rx, &back));
  TEST_ASSERT_EQUAL_HEX8(0x33u, back.command);
  TEST_ASSERT_EQUAL_HEX8(2u, back.length);
  TEST_ASSERT_EQUAL_HEX8(0xBBu, back.data[1]);
}

/* Writing one byte of a host PCB and letting the adapter collect it. */
static void host_sends(ap_3c505_t *card, ap_3c505_adapter_t *adapter,
                       uint8_t byte) {
  TEST_ASSERT_TRUE(ap_3c505_host_status(card) & AP_3C505_HSR_HCRE);
  ap_3c505_write(card, AP_3C505_REG_COMMAND, byte);
  for (unsigned guard = 0; guard < 16u; guard++) {
    if (ap_3c505_host_status(card) & AP_3C505_HSR_HCRE) {
      return;
    }
    (void)ap_3c505_pump(card, adapter);
  }
  TEST_FAIL_MESSAGE("the adapter never took the command byte");
}

/* The two halves joined. Nothing here touches the adapter's side of the
 * mailbox: the host writes `03H` through the command register exactly as the
 * driver does, `ap_3c505_pump` is the only other thing that runs, and `33H`
 * comes back with the address PROM in it.
 *
 * This is the test the suite did not have, and its absence is why the suite
 * was green over a card that could not answer a command.
 * `test_a_pcb_crosses_the_real_command_register` reaches the same PCB, but it
 * calls `ap_3c505_adapter_take_command` and `ap_3c505_pcb_rx_byte` itself --
 * the test supplied the wiring the device was missing, and so tested the
 * assembler rather than the path. */
static void test_a_command_written_by_the_host_is_answered_by_the_adapter(
    void) {
  const uint8_t prom[6] = {0x02u, 0x60u, 0x8Cu, 0x44u, 0x55u, 0x66u};
  ap_3c505_t card;
  ap_3c505_adapter_t adapter;
  ap_3c505_reset(&card);
  ap_3c505_adapter_init(&adapter, prom);

  /* §3.1.2: command and length, then the flags to `11` and the total length as
   * one further byte. `03H` carries no data. */
  host_sends(&card, &adapter, AP_3C505_CMD_GET_ETHERNET_ADDRESS);
  host_sends(&card, &adapter, 0u);
  ap_3c505_set_host_flags(&card, AP_3C505_SF_END_OF_PCB);
  host_sends(&card, &adapter, 2u);

  /* And the response assembles itself, unasked. §3.1.3: the total length is
   * flagged `11` and is not part of the PCB. */
  uint8_t response[AP_3C505_PCB_MAX] = {0};
  unsigned got = 0;
  bool ended = false;
  for (unsigned guard = 0; guard < 256u && !ended; guard++) {
    if ((ap_3c505_host_status(&card) & AP_3C505_HSR_ACRF) == 0u) {
      (void)ap_3c505_pump(&card, &adapter);
      continue;
    }
    ended = ap_3c505_adapter_flags(&card) == AP_3C505_SF_END_OF_PCB;
    const uint8_t byte = ap_3c505_read(&card, AP_3C505_REG_COMMAND);
    if (!ended && got < sizeof response) {
      response[got++] = byte;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(ended, "the adapter never answered the command");
  TEST_ASSERT_EQUAL_HEX8(0x33u, response[0]);
  TEST_ASSERT_EQUAL_HEX8(6u, response[1]);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(prom, &response[2], 6);
}

/* §3.1.1's other half, and the one a driver actually blocks on: "To indicate
 * the acceptance of the PCB, the Adapter uses status flag state 01 after the
 * Host signals end-of-PCB. To indicate rejection, the Adapter uses status state
 * 10." §3.1.2's last step is "Wait for adapter state 01 (accept) or 10
 * (reject)", so a card that never sets either leaves the host in a 50 ms
 * timeout for every command it sends. */
static void test_a_received_pcb_is_accepted_or_rejected_in_the_flags(void) {
  ap_3c505_t card;
  ap_3c505_adapter_t adapter;
  ap_3c505_reset(&card);
  ap_3c505_adapter_init(&adapter, station);

  host_sends(&card, &adapter, AP_3C505_CMD_GET_ETHERNET_ADDRESS);
  host_sends(&card, &adapter, 0u);
  ap_3c505_set_host_flags(&card, AP_3C505_SF_END_OF_PCB);
  host_sends(&card, &adapter, 2u);
  TEST_ASSERT_EQUAL_INT(AP_3C505_SF_ACCEPTED, ap_3c505_adapter_flags(&card));

  /* And a command it does not implement: `12H`-`2FH` are reserved. */
  ap_3c505_reset(&card);
  ap_3c505_adapter_init(&adapter, station);
  host_sends(&card, &adapter, 0x20u);
  host_sends(&card, &adapter, 0u);
  ap_3c505_set_host_flags(&card, AP_3C505_SF_END_OF_PCB);
  host_sends(&card, &adapter, 2u);
  TEST_ASSERT_EQUAL_INT(AP_3C505_SF_REJECTED, ap_3c505_adapter_flags(&card));
}

/* Arming one `08H`, since one receive request yields exactly one packet. */
static void arm_receive(ap_3c505_adapter_t *adapter) {
  ap_3c505_pcb_t in = {.command = AP_3C505_CMD_RECEIVE_PACKET, .length = 8u};
  in.data[4] = 64u; /* buffer length */
  ap_3c505_pcb_t out = {0};
  (void)ap_3c505_dispatch(adapter, &in, &out);
}

/* §3.2.1 `02H`: "bit 2,1,0: receive mode (000); 000 = station only, 001 = plus
 * broadcast, 010 = multicast, 100 = promiscuous". The mode was being stored and
 * never consulted, so every frame on the wire was this station's. */
static void test_the_receive_mode_decides_which_frames_are_taken(void) {
  ap_3c505_adapter_t adapter;
  ap_3c505_adapter_init(&adapter, station);
  ap_3c505_pcb_t out = {0};

  uint8_t mine[8] = {0}, other[8] = {0}, bcast[8] = {0}, group[8] = {0};
  for (unsigned i = 0; i < 6u; i++) {
    mine[i] = station[i];
    other[i] = (uint8_t)(station[i] ^ 0xFFu);
    bcast[i] = 0xFFu;
    group[i] = (uint8_t)(0x01u + i);
  }
  other[0] &= (uint8_t)~0x01u; /* an individual address, not a group one */

  /* The default, `000`: this station's own address and nothing else. */
  arm_receive(&adapter);
  TEST_ASSERT_TRUE(ap_3c505_deliver_frame(&adapter, mine, 8u, &out));
  arm_receive(&adapter);
  TEST_ASSERT_FALSE(ap_3c505_deliver_frame(&adapter, other, 8u, &out));
  TEST_ASSERT_FALSE(ap_3c505_deliver_frame(&adapter, bcast, 8u, &out));
  /* A frame for somebody else is not a packet this card lost: it never reaches
   * the armed-receive test, so `3AH`'s no-resources counter stays put. */
  TEST_ASSERT_EQUAL_UINT(0u, adapter.no_resource_errors);

  /* `001`, plus broadcast -- cumulative, as "plus" says. */
  adapter.receive_mode = 0x01u;
  TEST_ASSERT_TRUE(ap_3c505_deliver_frame(&adapter, bcast, 8u, &out));
  arm_receive(&adapter);
  TEST_ASSERT_FALSE(ap_3c505_deliver_frame(&adapter, other, 8u, &out));

  /* `010`, multicast, and only for an address `0BH` actually loaded: the 82586
   * filters against its list, not against the group bit alone. */
  adapter.receive_mode = 0x02u;
  TEST_ASSERT_FALSE(ap_3c505_deliver_frame(&adapter, group, 8u, &out));
  for (unsigned i = 0; i < 6u; i++) {
    adapter.multicast[0][i] = group[i];
  }
  adapter.multicast_count = 1u;
  TEST_ASSERT_TRUE(ap_3c505_deliver_frame(&adapter, group, 8u, &out));

  /* `100`, promiscuous: addressed here or not. */
  adapter.receive_mode = 0x04u;
  arm_receive(&adapter);
  TEST_ASSERT_TRUE(ap_3c505_deliver_frame(&adapter, other, 8u, &out));
}

/* §3.2.2 `3FH`: status `0` is "no errors" and carries no failure data, so the
 * data field is the status word alone. The three things `0FH` exercises -- a
 * firmware ROM, adapter DRAM and an 82586 -- are none of them modelled here, so
 * none of them can be faulty; passing is the determined answer, not a hopeful
 * one. */
static void test_the_self_test_reports_no_errors(void) {
  ap_3c505_adapter_t adapter;
  ap_3c505_adapter_init(&adapter, station);
  const ap_3c505_pcb_t in = {.command = AP_3C505_CMD_SELF_TEST, .length = 0u};
  ap_3c505_pcb_t out = {0};
  TEST_ASSERT_TRUE(ap_3c505_dispatch(&adapter, &in, &out));
  TEST_ASSERT_EQUAL_HEX8(0x3Fu, out.command);
  TEST_ASSERT_EQUAL_HEX8(2u, out.length);
  TEST_ASSERT_EQUAL_HEX8(0u, out.data[0]);
  TEST_ASSERT_EQUAL_HEX8(0u, out.data[1]);
}

/* `09H` is accepted and answers nothing until the transmit completes, so
 * "produced no response PCB" and "rejected" are different facts. Reading them
 * as one would reject every transmit the host ever armed. */
static void test_an_armed_transmit_is_accepted_though_it_answers_nothing(void) {
  ap_3c505_adapter_t adapter;
  ap_3c505_adapter_init(&adapter, station);
  ap_3c505_pcb_t in = {.command = AP_3C505_CMD_TRANSMIT_PACKET, .length = 6u};
  in.data[4] = 2u; /* packet length */
  ap_3c505_pcb_t out = {0};

  TEST_ASSERT_EQUAL_INT(AP_3C505_PCB_ACCEPTED,
                        ap_3c505_execute(&adapter, &in, &out));
  TEST_ASSERT_TRUE(adapter.transmitting);

  /* Whereas a reserved code really is a rejection. */
  const ap_3c505_pcb_t bad = {.command = 0x20u, .length = 0u};
  TEST_ASSERT_EQUAL_INT(AP_3C505_PCB_REJECTED,
                        ap_3c505_execute(&adapter, &bad, &out));
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
  RUN_TEST(test_the_host_dma_request_follows_hrdy_in_both_directions);
  RUN_TEST(test_a_cleared_dma_enable_takes_the_card_off_the_channel);
  RUN_TEST(test_demand_mode_pauses_every_ninth_transfer_unless_burst);
  RUN_TEST(test_done_is_set_by_terminal_count_and_cleared_by_the_enable);
  RUN_TEST(test_a_dma_cycle_and_polled_io_share_one_data_register);
  RUN_TEST(test_the_adapter_signals_its_power_on_through_the_status_flags);
  RUN_TEST(test_the_status_direction_bit_follows_the_control_one);
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
  RUN_TEST(test_a_transmit_reaches_the_wire_only_when_its_last_byte_does);
  RUN_TEST(test_a_wire_that_refuses_is_reported_in_the_completion_status);
  RUN_TEST(test_a_frame_with_no_receive_armed_counts_as_no_resources);
  RUN_TEST(test_an_armed_receive_stages_the_frame_and_reports_it);
  RUN_TEST(test_a_frame_longer_than_the_buffer_is_truncated_and_both_reported);
  RUN_TEST(test_a_queued_pcb_is_paced_out_by_the_host_taking_bytes);
  RUN_TEST(test_a_command_written_by_the_host_is_answered_by_the_adapter);
  RUN_TEST(test_a_received_pcb_is_accepted_or_rejected_in_the_flags);
  RUN_TEST(test_the_receive_mode_decides_which_frames_are_taken);
  RUN_TEST(test_the_self_test_reports_no_errors);
  RUN_TEST(test_an_armed_transmit_is_accepted_though_it_answers_nothing);
  return UNITY_END();
}
