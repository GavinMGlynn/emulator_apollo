/* The Apollo Token Ring controller's register interface.
 *
 * Every test here replays something `[ROM3500]` actually does, at the ROM
 * address `RING.md` cites for it. The board has no datasheet and no oracle --
 * MAME carries Domain networking over a 3c505 instead -- so the firmware's own
 * accesses are the entire specification, and a test that went beyond them would
 * be testing an invention. */

#include "unity.h"

#include "device/ap_i8254.h"
#include "device/ap_ring_ctl.h"
#include "ring/ap_ring_frame.h"

/* A minimal ring for the wire tests: a medium, two stations, and the
 * controller joined to the first. Same shape as `ring_station_suite`'s, so a
 * frame that crosses here crosses for the same reasons it does there. */
typedef struct {
  ap_ring_medium_t medium;
  ap_ring_station_t station[2];
  ap_ring_ctl_t ctl;
} wired_t;

static void wired_build(wired_t *w) {
  ap_ring_medium_init(&w->medium);
  for (unsigned i = 0; i < 2u; i++) {
    ap_ring_station_init(&w->station[i], ap_ring_medium_attach(&w->medium));
  }
  ap_ring_ctl_reset(&w->ctl, true);
  ap_ring_ctl_set_node_id(&w->ctl, 0x00012345u);
  ap_ring_ctl_attach_ring(&w->ctl, &w->station[0], &w->medium);
}

static void wired_step(wired_t *w) {
  for (unsigned i = 0; i < 2u; i++) {
    ap_ring_station_drive(&w->station[i], &w->medium);
  }
  ap_ring_medium_advance(&w->medium);
  for (unsigned i = 0; i < 2u; i++) {
    ap_ring_station_receive(&w->station[i], &w->medium);
  }
}

void setUp(void) {}
void tearDown(void) {}

/* Finding 38. `$CA0` maps a unit number to *two* base pointers, and anything
 * outside them is `ring: init error`. */
static void test_a_unit_is_both_of_its_at_windows(void) {
  static const struct {
    uint32_t address;
    unsigned unit;
    bool second;
  } cases[] = {
      {0x051000u, 0u, false}, {0x059000u, 0u, true},
      {0x052000u, 1u, false}, {0x05A000u, 1u, true},
      /* The far end of a window is still inside it: `+C06` is the highest
       * offset the firmware touches and it must decode. */
      {0x059C06u, 0u, true},  {0x05AFFFu, 1u, true},
  };
  for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    unsigned unit = 99u;
    bool second = false;
    uint32_t offset = 0u;
    TEST_ASSERT_TRUE(
        ap_ring_ctl_decode(cases[i].address, &unit, &second, &offset));
    TEST_ASSERT_EQUAL_UINT(cases[i].unit, unit);
    TEST_ASSERT_EQUAL_INT((int)cases[i].second, (int)second);
    TEST_ASSERT_EQUAL_HEX32(cases[i].address & 0xFFFu, offset);
  }

  /* The gaps between the windows belong to other AT slots. `$53000`-`$58FFF`
   * is the whole span between unit 1's first window and unit 0's second. */
  TEST_ASSERT_FALSE(ap_ring_ctl_decode(0x050FFFu, NULL, NULL, NULL));
  TEST_ASSERT_FALSE(ap_ring_ctl_decode(0x053000u, NULL, NULL, NULL));
  TEST_ASSERT_FALSE(ap_ring_ctl_decode(0x058FFFu, NULL, NULL, NULL));
  TEST_ASSERT_FALSE(ap_ring_ctl_decode(0x05B000u, NULL, NULL, NULL));
}

/* Finding 39, `[ROM3500]` `000CD0`-`000CEA`: init reads a *byte* from `(a2)`
 * and accepts only `$36` or `$37`. */
static void test_the_id_register_answers_one_of_the_two_values_init_accepts(
    void) {
  ap_ring_ctl_t ctl;
  ap_ring_ctl_reset(&ctl, true);

  const uint8_t id = ap_ring_ctl_read8(&ctl, true, AP_RING_CTL_BANK_ID);
  TEST_ASSERT_TRUE(id == AP_RING_CTL_ID_6 || id == AP_RING_CTL_ID_7);
}

/* Finding 40, `[ROM3500]` `0000AE`-`0000C2`: `move.w $400(a2),d0` then
 * `and.w #$8000,d0` then a branch. With the bit clear init "returns success
 * having touched nothing else", so an empty slot is *not* an error -- and the
 * ID check must fail too, or a machine with no ring board would report one. */
static void test_an_empty_slot_reads_as_absent_rather_than_as_an_error(void) {
  ap_ring_ctl_t ctl;
  ap_ring_ctl_reset(&ctl, false);

  const uint16_t status =
      ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_STATUS);
  TEST_ASSERT_EQUAL_HEX16(0u, status & AP_RING_CTL_STATUS_PRESENT);

  const uint8_t id = ap_ring_ctl_read8(&ctl, true, AP_RING_CTL_BANK_ID);
  TEST_ASSERT_TRUE(id != AP_RING_CTL_ID_6 && id != AP_RING_CTL_ID_7);
}

/* The rest of finding 40: with bit 15 set the firmware clears `(a2)`, `+402`,
 * `+404`, `+400` in that order, and later writes `$800` to `+400`.
 *
 * The trap this catches is the clear of `+400`: a model that let the host's
 * zero take the presence bit with it would report the board absent from that
 * moment on, and every later probe of the same word would fail. */
static void test_the_init_clear_sequence_does_not_erase_the_presence_gate(
    void) {
  ap_ring_ctl_t ctl;
  ap_ring_ctl_reset(&ctl, true);

  TEST_ASSERT_EQUAL_HEX16(
      AP_RING_CTL_STATUS_PRESENT,
      ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_STATUS) &
          AP_RING_CTL_STATUS_PRESENT);

  ap_ring_ctl_write16(&ctl, true, AP_RING_CTL_BANK_ID, 0u);
  ap_ring_ctl_write16(&ctl, true, AP_RING_CTL_BANK_STATUS + 2u, 0u);
  ap_ring_ctl_write16(&ctl, true, AP_RING_CTL_BANK_STATUS + 4u, 0u);
  ap_ring_ctl_write16(&ctl, true, AP_RING_CTL_BANK_STATUS, 0u);

  TEST_ASSERT_EQUAL_HEX16(
      AP_RING_CTL_STATUS_PRESENT,
      ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_STATUS) &
          AP_RING_CTL_STATUS_PRESENT);
  /* And the ID survives the clear of `(a2)`: the firmware reads it again. */
  TEST_ASSERT_EQUAL_HEX8(AP_RING_CTL_ID_6,
                         ap_ring_ctl_read8(&ctl, true, AP_RING_CTL_BANK_ID));

  /* `00010A`: the later `$800`. This used to assert the board **kept** what the
   * host wrote, because bit 11's purpose was open question A and storage is
   * what "unknown" was modelled as. It is no longer unknown: the firmware's own
   * subtests 01, 11 and 16 require the bit set at reset and clear after a write
   * of *any* value, and `[EH]`'s ring section says the same for the DN3xx
   * board -- "writing anything to this register clears the transmit
   * interrupt". So the assertion is inverted, and the two sources are named
   * because a test that merely tracked the model would drift with it. */
  ap_ring_ctl_reset(&ctl, true);
  TEST_ASSERT_EQUAL_HEX16(
      AP_RING_CTL_STATUS_BIT11,
      ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_STATUS) &
          AP_RING_CTL_STATUS_BIT11);

  ap_ring_ctl_write16(&ctl, true, AP_RING_CTL_BANK_STATUS,
                      AP_RING_CTL_STATUS_BIT11);
  TEST_ASSERT_EQUAL_HEX16(
      0u, ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_STATUS) &
              AP_RING_CTL_STATUS_BIT11);

  /* Any value, not just a one in that bit -- which is what separates this from
   * write-one-to-clear, the reading the firmware refuted. */
  ap_ring_ctl_reset(&ctl, true);
  ap_ring_ctl_write16(&ctl, true, AP_RING_CTL_BANK_STATUS, 0x0100u);
  TEST_ASSERT_EQUAL_HEX16(
      0u, ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_STATUS) &
              AP_RING_CTL_STATUS_BIT11);

  /* And the gate still stands through all of it. */
  TEST_ASSERT_EQUAL_HEX16(
      AP_RING_CTL_STATUS_PRESENT,
      ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_STATUS) &
          AP_RING_CTL_STATUS_PRESENT);
}

/* Finding 41, `[ROM3500]` `0000C6`-`0000E4`: `$30`, `$70`, `$B0` to `+806` and
 * `$30`, `$70` to `+C06`, then `$E4` to `+806` and a `btst #14` on a word read
 * of `+802`.
 *
 * `$E4` is the read-back command, which exists only on the 8254 -- so this
 * sequence is simultaneously the board's initialisation and the evidence for
 * what the part is. */
static void test_the_firmwares_timer_initialisation_reaches_two_8254s(void) {
  ap_ring_ctl_t ctl;
  ap_ring_ctl_reset(&ctl, true);

  ap_ring_ctl_write8(&ctl, true, AP_RING_CTL_BANK_TIMER_A + 6u, 0x30u);
  ap_ring_ctl_write8(&ctl, true, AP_RING_CTL_BANK_TIMER_A + 6u, 0x70u);
  ap_ring_ctl_write8(&ctl, true, AP_RING_CTL_BANK_TIMER_A + 6u, 0xB0u);
  ap_ring_ctl_write8(&ctl, true, AP_RING_CTL_BANK_TIMER_B + 6u, 0x30u);
  ap_ring_ctl_write8(&ctl, true, AP_RING_CTL_BANK_TIMER_B + 6u, 0x70u);

  /* Both parts took the words, and they are genuinely two parts: `+C00`'s
   * counter 2 was never programmed and `+800`'s was. */
  for (unsigned i = 0; i < AP_I8254_COUNTERS; i++) {
    TEST_ASSERT_EQUAL_UINT(0u, ap_i8254_mode(&ctl.a2.timer_a, i));
    TEST_ASSERT_TRUE(ctl.a2.timer_a.counter[i].null_count);
  }
  TEST_ASSERT_EQUAL_HEX8(0xB0u, ctl.a2.timer_a.counter[2].control);
  TEST_ASSERT_EQUAL_HEX8(0x00u, ctl.a2.timer_b.counter[2].control);

  /* `$E4` latches counter 1's status, and the firmware reads it as a **word**
   * from `+802`. The register byte lands in the high half -- which is what puts
   * the status byte's D6 at the word's bit 14. */
  ap_ring_ctl_write8(&ctl, true, AP_RING_CTL_BANK_TIMER_A + 6u, 0xE4u);
  const uint16_t word =
      ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_TIMER_A + 2u);
  TEST_ASSERT_EQUAL_HEX16(1u << 14, word & (uint16_t)(1u << 14));
  /* And the control word as written is in the same half: `70 & 3F` = `30`. */
  TEST_ASSERT_EQUAL_HEX8(0x30u, (uint8_t)(word >> 8) & 0x3Fu);
}

/* The registers are byte-wide at *even* offsets, so the odd half of a word
 * access must not reach the part. An 8254 read unlatches and advances the
 * LSB/MSB cursor, so a model that read the device twice per `move.w` would hand
 * the firmware two halves of different reads. */
static void test_a_word_access_touches_the_timer_exactly_once(void) {
  ap_ring_ctl_t ctl;
  ap_ring_ctl_reset(&ctl, true);

  /* Counter 0, LSB then MSB, mode 0, count `$1234`. */
  ap_ring_ctl_write8(&ctl, true, AP_RING_CTL_BANK_TIMER_A + 6u, 0x30u);
  ap_ring_ctl_write8(&ctl, true, AP_RING_CTL_BANK_TIMER_A, 0x34u);
  ap_ring_ctl_write8(&ctl, true, AP_RING_CTL_BANK_TIMER_A, 0x12u);
  TEST_ASSERT_EQUAL_HEX16(0x1234u, ctl.a2.timer_a.counter[0].counter);

  /* Latch it, then take the count with two word reads. If the odd byte reached
   * the part, the first word would consume both halves and the second would
   * return a live count instead of the latched MSB. */
  ap_ring_ctl_write8(&ctl, true, AP_RING_CTL_BANK_TIMER_A + 6u, 0x00u);
  const uint16_t first = ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_TIMER_A);
  ap_ring_ctl_clock(&ctl, true);
  const uint16_t second =
      ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_TIMER_A);
  TEST_ASSERT_EQUAL_HEX8(0x34u, (uint8_t)(first >> 8));
  TEST_ASSERT_EQUAL_HEX8(0x12u, (uint8_t)(second >> 8));
}

/* Finding 38 again, from the other side: the two windows are separate register
 * sets, not aliases of one. The firmware clears `a1`'s `+2`/`+4`/`+6`/`+400`/
 * `+402` and drives `a2`'s timers, so a model that aliased them would have the
 * clear wipe the timers it had just programmed. */
static void test_the_two_windows_are_separate_register_sets(void) {
  ap_ring_ctl_t ctl;
  ap_ring_ctl_reset(&ctl, true);

  ap_ring_ctl_write8(&ctl, true, AP_RING_CTL_BANK_TIMER_A + 6u, 0xB0u);
  /* A value with bit 10 clear, because `+402` is a command register and a `$4`
   * in the command lane now *means* something -- `BEEF` would be taken as an
   * operation and self-clear the lane. This test is about the two windows being
   * separate, so it uses a word that is not a command. */
  ap_ring_ctl_write16(&ctl, true, AP_RING_CTL_BANK_STATUS + 2u, 0xB800u);

  TEST_ASSERT_EQUAL_HEX8(0xB0u, ctl.a2.timer_a.counter[2].control);
  TEST_ASSERT_EQUAL_HEX8(0x00u, ctl.a1.timer_a.counter[2].control);
  /* `+402`'s **high** lane is the command byte and round-trips; its low lane is
   * status, which finding 48 said it carried and subtest 13 pinned. */
  TEST_ASSERT_EQUAL_HEX16(
      0xB800u, ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_STATUS + 2u) &
                   0xFF00u);

  /* **The first window's `+402` is not a second copy of that register**, and
   * this assertion used to say it was. `002398-04` p. 12-29 gives `51402` as
   * `GPS_CLR` on write and "(unused - PROM)" on read -- a write-only clear
   * behind the node ID PROM -- so a write to it does not come back. The old
   * form wrote the *first* window here and required the value to round-trip,
   * which passed only because the model treated the whole bank as storage. */
  ap_ring_ctl_write16(&ctl, false, AP_RING_CTL_BANK_STATUS + 2u, 0xB800u);
  TEST_ASSERT_EQUAL_HEX16(
      0x0000u, ap_ring_ctl_read16(&ctl, false, AP_RING_CTL_BANK_STATUS + 2u) &
                   0xFF00u);
}

/* The boundary between what carries across board generations and what does not.
 *
 * `002398-04` documents the **DN3000** ring board, and this core models the
 * DN3500's AT board. Their *status* registers agree across three independent
 * sources -- pp. 12-30/12-31, the AT firmware's own constants, and
 * `ring8a.drvr`'s tables (`RING.md` 93, 97) -- so carrying those across is
 * evidence rather than convenience. Their *command* encodings do not agree, and
 * finding 55b refused the tempting match once already.
 *
 * This asserts the arithmetic that refuses it, because the refusal is the kind
 * of negative result a later reader will otherwise re-derive: the AT firmware's
 * command bytes decode to nothing under the DN3000's XMIT_CMD, and the one
 * value that *does* decode is recorded so it is not mistaken for a mapping. */
static void test_the_at_boards_command_bytes_are_not_the_dn3000s_bits(void) {
  /* `move.b #$1,$402`, `#$2`, `#$6` -- the high lane on a big-endian part. */
  const uint16_t xmit_cmds[] = {0x0100u, 0x0200u, 0x0600u};
  const uint16_t xmit_defined = AP_RING_CTL_XMIT_CMD_FEN |
                                AP_RING_CTL_XMIT_CMD_TEN |
                                AP_RING_CTL_XMIT_CMD_INE;
  for (unsigned i = 0; i < sizeof xmit_cmds / sizeof xmit_cmds[0]; i++) {
    TEST_ASSERT_EQUAL_HEX16(0u, xmit_cmds[i] & xmit_defined);
  }
  /* `transmit enable` would need `$40` in that lane, which no firmware here
   * writes -- the concrete form of "the encodings do not correspond". */
  TEST_ASSERT_EQUAL_HEX16(0x4000u, AP_RING_CTL_XMIT_CMD_TEN);

  /* MISC_CMD and RCV_CMD *do* apply to this board, which is no longer an
   * isolated numeric match: Domain/OS's kernel ring driver writes `$800` and
   * `$900` to MISC_CMD -- `nct`, and `nct` with `lpb` -- and `$800` to RCV_CMD,
   * while the AT boot firmware's `move.b #$8,$404` is the same `$0800`
   * (`RING.md` 103). Two independent drivers and the page. */
  TEST_ASSERT_EQUAL_HEX16(AP_RING_CTL_RCV_CMD_RCV, 0x0800u);
  TEST_ASSERT_EQUAL_HEX16(0x0900u,
                          AP_RING_CTL_MISC_CMD_NCT | AP_RING_CTL_MISC_CMD_LPB);

  /* And the three transmit command values both drivers write, which is the
   * shape p. 12-32 describes -- three functions, the third being the second
   * with a higher bit added, its "force transmit is a modifier to transmit
   * enable". Asserted as *values*, not as a bit mapping, because no source
   * states this board's layout. */
  TEST_ASSERT_EQUAL_HEX16(0x0600u, 0x0400u | 0x0200u);

  /* p. 12-32's MISC_CMD, read at 400 dpi after a 200 dpi render put every bit
   * one place low (`RING.md` 97/98 commit). `lpb` is the loopback finding 79
   * needs and 93d cites, so its position is load-bearing. */
  TEST_ASSERT_EQUAL_HEX16(0x1000u, AP_RING_CTL_MISC_CMD_BPM);
  TEST_ASSERT_EQUAL_HEX16(0x0100u, AP_RING_CTL_MISC_CMD_LPB);
}

/* The first window's eight write-only registers, walked against the page that
 * tabulates them -- `002398-04` p. 12-29 -- rather than against a failure.
 *
 * None of this was modelled: `+000` and `+400`-`+406` were inert or generic
 * storage, and `+006` was actively wrong, setting the RAM pointer on a window
 * whose `+006` the manual calls `TIMO_ACK`. `RAM_ADDR` is `59006`, in the
 * *other* window. The AT firmware never writes the first window's `+006`, so no
 * boot, no self-test and no green suite could have found it; the table walk
 * did, which is the argument for walking tables.
 *
 * The three clear registers are corroborated by the AT firmware itself:
 * `$944`'s tail (`r3500.lst` `0009BA`-`0009C6`) clears `d3` and writes it to
 * `$4(a3)`, `$400(a3)`, `$402(a3)`, `$406(a3)` -- RCV_ACK and exactly the three
 * *clear* registers, skipping `$404`, the bank's one *request* register. */
static void test_the_first_windows_write_only_registers_clear_what_they_name(
    void) {
  ap_ring_ctl_t ctl;
  ap_ring_ctl_reset(&ctl, true);

  /* Drive every bit these registers own, so a clear that misses one shows. */
  ctl.a2.status |= (uint16_t)(AP_RING_CTL_STATUS_STICKY_ERRORS |
                              AP_RING_CTL_STATUS_GPS | AP_RING_CTL_STATUS_TMI);

  ap_ring_ctl_write16(&ctl, false, AP_RING_CTL_W1_GPS_CLR, 0u);
  TEST_ASSERT_EQUAL_HEX16(0u, ctl.a2.status & AP_RING_CTL_STATUS_GPS);
  /* And it clears *only* `gps`: the sticky errors are a different register's
   * business, which is the whole reason the page gives them one each. */
  TEST_ASSERT_EQUAL_HEX16(AP_RING_CTL_STATUS_STICKY_ERRORS,
                          ctl.a2.status & AP_RING_CTL_STATUS_STICKY_ERRORS);

  ap_ring_ctl_write16(&ctl, false, AP_RING_CTL_W1_ERR_BITS_CLR, 0u);
  TEST_ASSERT_EQUAL_HEX16(0u,
                          ctl.a2.status & AP_RING_CTL_STATUS_STICKY_ERRORS);
  TEST_ASSERT_EQUAL_HEX16(AP_RING_CTL_STATUS_TMI,
                          ctl.a2.status & AP_RING_CTL_STATUS_TMI);

  ap_ring_ctl_write16(&ctl, false, AP_RING_CTL_W1_TIMO_ACK, 0u);
  TEST_ASSERT_EQUAL_HEX16(0u, ctl.a2.status & AP_RING_CTL_STATUS_TMI);

  /* And `TIMO_ACK` is not the RAM pointer. Writing a pointer-shaped value to
   * the first window's `+006` must not arm the buffer, which is what the bug
   * this test was written for did. */
  ap_ring_ctl_write16(&ctl, false, AP_RING_CTL_W1_TIMO_ACK, 0x0040u);
  TEST_ASSERT_EQUAL_HEX16(0u, ctl.a1.pointer);

  /* The *second* window's `+006` still is the pointer, so the fix is a split
   * between the windows rather than the loss of a register. */
  ap_ring_ctl_write16(&ctl, true, 0x006u, 0x0040u);
  TEST_ASSERT_EQUAL_HEX16(0x0040u, ctl.a2.pointer);
}

/* `+402` and `+404` are byte-wide command registers whose **bits** have no known
 * meaning -- finding 48 settles the width and direction and the values the
 * firmware writes, and nothing more. They are storage, and this test says
 * exactly that: it asserts a read-back, not a behaviour, so that inventing one
 * later fails here first.
 *
 * `+406` was in this list until finding 46 took it out -- it is the buffer's
 * data port, tested above. The rule that failed here is worth keeping: an
 * offset whose meaning is unknown is not the same as an offset that is inert,
 * and calling the first the second is how a port gets modelled as a latch. */
static void test_the_unknown_command_slots_are_storage_and_nothing_more(void) {
  ap_ring_ctl_t ctl;
  ap_ring_ctl_reset(&ctl, true);

  ap_ring_ctl_write16(&ctl, true, AP_RING_CTL_BANK_STATUS + 2u, 0x1111u);
  ap_ring_ctl_write16(&ctl, true, AP_RING_CTL_BANK_STATUS + 4u, 0x2222u);

  /* Storage in the command lane, and **not** "nothing more": the low lane of
   * `+402` answers with status the firmware requires (`RING.md` 63), so this
   * test now says which half is storage rather than assuming both are. */
  TEST_ASSERT_EQUAL_HEX16(
      0x1100u, ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_STATUS + 2u) &
                   0xFF00u);
  TEST_ASSERT_EQUAL_HEX16(
      AP_RING_CTL_COMMAND_STATUS_IDLE,
      ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_STATUS + 2u) & 0x00FFu);
  /* `+404` the same way, from subtest 15: command lane storage, status lane
   * answering. The two command registers turn out to share one shape. */
  TEST_ASSERT_EQUAL_HEX16(
      0x2200u, ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_STATUS + 4u) &
                   0xFF00u);
  TEST_ASSERT_EQUAL_HEX16(
      AP_RING_CTL_COMMAND2_STATUS_IDLE,
      ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_STATUS + 4u) & 0x00FFu);

  /* Writing them changes nothing else: no side effect on the gate, the ID or
   * the timers. The constant is the **idle** word rather than the presence bit
   * alone -- this assertion is about the absence of a side effect, and naming
   * the reset value it happened to have made it fail when the firmware's own
   * subtest 01 established what a reset board really reads. */
  TEST_ASSERT_EQUAL_HEX16(
      AP_RING_CTL_STATUS_IDLE,
      ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_STATUS));
  TEST_ASSERT_TRUE(AP_RING_CTL_STATUS_IDLE & AP_RING_CTL_STATUS_PRESENT);
  TEST_ASSERT_EQUAL_HEX8(AP_RING_CTL_ID_6,
                         ap_ring_ctl_read8(&ctl, true, AP_RING_CTL_BANK_ID));

  /* Finding 44's bank-0 registers, the same way: `+002` and `+004` are written
   * by the self-test and `+004` is read once at `000944` with its value
   * discarded, so a read-back is all that is claimed. */
  ap_ring_ctl_write16(&ctl, true, AP_RING_CTL_BANK_ID + 2u, 0x4444u);
  ap_ring_ctl_write16(&ctl, true, AP_RING_CTL_BANK_ID + 4u, 0x0010u);
  TEST_ASSERT_EQUAL_HEX16(
      0x4444u, ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_ID + 2u));
  TEST_ASSERT_EQUAL_HEX16(
      0x0010u, ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_ID + 4u));
  /* And none of it disturbed the ID, which `0002E8`'s `move.w #$0,(a4)` also
   * has to leave alone. */
  TEST_ASSERT_EQUAL_HEX8(AP_RING_CTL_ID_6,
                         ap_ring_ctl_read8(&ctl, true, AP_RING_CTL_BANK_ID));
}

/* ## The firmware's own memory test, replayed
 *
 * `[ROM3500]` `00033C`-`000440` walks the dual-ported RAM through the `+406`
 * port in four patterns. This is that loop, instruction for instruction, with
 * the 68000 helpers `$BE0` (write), `$C18` (read and compare), `$C0A` (write
 * rotating) and `$C4E` (read rotating) as C functions. It is the strongest
 * check available for a board with no datasheet and no oracle: the hardware's
 * own acceptance test, run against the model.
 *
 * Two 68000 details have to survive the translation or the patterns diverge:
 * `addq.b #$2,d2` modifies **only the low byte** and does not carry into the
 * high one, and `not.w`/`rol.w` are 16-bit. A C translation that used ordinary
 * arithmetic would generate a different sequence and still pass against itself.
 */

static uint16_t rom_not_w(uint16_t d) { return (uint16_t)~d; }

static uint16_t rom_addq_b_2(uint16_t d) {
  /* `addq.b` touches the low byte alone -- `$00FF + 2` is `$0001`, not
   * `$0101`. */
  return (uint16_t)((d & 0xFF00u) | (uint8_t)((d & 0xFFu) + 2u));
}

static uint16_t rom_rol_w_1(uint16_t d) {
  return (uint16_t)((uint16_t)(d << 1) | (uint16_t)(d >> 15));
}

/* `$BE0`: write `count` words, advancing the pattern by `d4`'s two flags. */
static void rom_write_pattern(ap_ring_ctl_t *ctl, uint16_t d2, uint16_t d4,
                              unsigned count) {
  for (unsigned i = 0; i < count; i++) {
    const uint16_t out = (d4 & 1u) != 0u ? rom_not_w(d2) : d2;
    ap_ring_ctl_write16(ctl, true, AP_RING_CTL_BANK_STATUS + 6u, out);
    d2 = (d4 & 2u) != 0u ? (uint16_t)(d2 + 1u)
                         : (uint16_t)(rom_addq_b_2(d2) + 0x200u);
  }
}

/* `$C18`: the discarded read, then `count` reads compared against the same
 * sequence. Returns true when every word matched. */
static bool rom_read_pattern(ap_ring_ctl_t *ctl, uint16_t d2, uint16_t d4,
                             unsigned count) {
  (void)ap_ring_ctl_read16(ctl, true, AP_RING_CTL_BANK_STATUS + 6u);
  for (unsigned i = 0; i < count; i++) {
    const uint16_t got =
        ap_ring_ctl_read16(ctl, true, AP_RING_CTL_BANK_STATUS + 6u);
    const uint16_t want = (d4 & 1u) != 0u ? rom_not_w(d2) : d2;
    if (got != want) {
      return false;
    }
    d2 = (d4 & 2u) != 0u ? (uint16_t)(d2 + 1u)
                         : (uint16_t)(rom_addq_b_2(d2) + 0x200u);
  }
  return true;
}

static void rom_point_at_zero(ap_ring_ctl_t *ctl) {
  /* `move.w #$0,$6(a4)` before every pass. */
  ap_ring_ctl_write16(ctl, true, AP_RING_CTL_BANK_ID + 6u, 0u);
}

static bool rom_pass(ap_ring_ctl_t *ctl, uint16_t d2, uint16_t d4) {
  const unsigned count = 0x7FFFu + 1u; /* `d3 = $7FFF`, `dbra` runs d3+1 */
  rom_point_at_zero(ctl);
  rom_write_pattern(ctl, d2, d4, count);
  rom_point_at_zero(ctl);
  return rom_read_pattern(ctl, d2, d4, count);
}

static void test_the_firmwares_own_memory_test_passes(void) {
  ap_ring_ctl_t ctl;
  ap_ring_ctl_reset(&ctl, true);

  /* `00033A`-`000394`: `d2` from 1, `+$101` each round, until `$203`. Two
   * passes per round, `d4` = 0 then 1. */
  for (uint16_t d2 = 1u; d2 != 0x203u; d2 = (uint16_t)(d2 + 0x101u)) {
    TEST_ASSERT_TRUE(rom_pass(&ctl, d2, 0u));
    TEST_ASSERT_TRUE(rom_pass(&ctl, d2, 1u));
  }
  /* `000396`-`0003E6`: `d2 = 0` with `d4` = 2, then `d4` = 3. */
  TEST_ASSERT_TRUE(rom_pass(&ctl, 0u, 2u));
  TEST_ASSERT_TRUE(rom_pass(&ctl, 0u, 3u));

  /* `0003EA`-`000440`: `$1111` rotated, written by `$C0A` and read by `$C4E`,
   * looping until the rotate brings it back. */
  uint16_t d2 = 0x1111u;
  do {
    for (unsigned half = 0; half < 2u; half++) {
      const unsigned count = 0x7FFFu + 1u;
      uint16_t pattern = d2;
      rom_point_at_zero(&ctl);
      for (unsigned i = 0; i < count; i++) {
        ap_ring_ctl_write16(&ctl, true, AP_RING_CTL_BANK_STATUS + 6u, pattern);
        pattern = rom_rol_w_1(pattern);
      }
      pattern = d2;
      rom_point_at_zero(&ctl);
      (void)ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_STATUS + 6u);
      for (unsigned i = 0; i < count; i++) {
        TEST_ASSERT_EQUAL_HEX16(
            pattern, ap_ring_ctl_read16(&ctl, true,
                                        AP_RING_CTL_BANK_STATUS + 6u));
        pattern = rom_rol_w_1(pattern);
      }
      d2 = rom_not_w(d2); /* `not.w d2` between the two halves */
    }
    d2 = rom_rol_w_1(d2);
  } while (d2 != 0x1111u);
}

/* Finding 46a, isolated. The port answers with the word the *previous* access
 * fetched, so the firmware's discarded first read is load-bearing: a model that
 * returned the addressed word immediately would be one word out for all 64 KB
 * and the test above would fail on its first pattern. */
static void test_the_data_port_answers_one_word_behind(void) {
  ap_ring_ctl_t ctl;
  ap_ring_ctl_reset(&ctl, true);

  ap_ring_ctl_write16(&ctl, true, AP_RING_CTL_BANK_ID + 6u, 0u);
  for (uint16_t i = 0; i < 4u; i++) {
    ap_ring_ctl_write16(&ctl, true, AP_RING_CTL_BANK_STATUS + 6u,
                        (uint16_t)(0xA000u + i));
  }

  /* Writing the pointer must not prefetch -- if it did, the discard below would
   * consume word 0 and everything after it would be one place early. */
  ap_ring_ctl_write16(&ctl, true, AP_RING_CTL_BANK_ID + 6u, 0u);
  (void)ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_STATUS + 6u);
  for (uint16_t i = 0; i < 4u; i++) {
    TEST_ASSERT_EQUAL_HEX16(
        (uint16_t)(0xA000u + i),
        ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_STATUS + 6u));
  }
}

/* Finding 50a: the `a1` window is written and never read anywhere in the ROM,
 * so its `+406` is not the buffer port. A model that made both windows the same
 * port would have `$976`'s clear of `$406(a3)` overwrite the buffer the test
 * above had just filled. */
static void test_the_first_windows_data_slot_is_not_the_buffer_port(void) {
  ap_ring_ctl_t ctl;
  ap_ring_ctl_reset(&ctl, true);

  ap_ring_ctl_write16(&ctl, true, AP_RING_CTL_BANK_ID + 6u, 0u);
  ap_ring_ctl_write16(&ctl, true, AP_RING_CTL_BANK_STATUS + 6u, 0x1234u);

  /* `$976`: `move.w d3,$406(a3)` with `d3` cleared. */
  ap_ring_ctl_write16(&ctl, false, AP_RING_CTL_BANK_STATUS + 6u, 0u);
  TEST_ASSERT_EQUAL_HEX16(0x1234u, ctl.buffer[0]);
}


/* **The firmware's own subtest 02, as a test.**
 *
 * `[ROM3500]` `$BE0` fills and `$C18` reads back: set `+006 = 0`, write
 * `d3+1 = $8000` words to `+406`, set `+006 = 0` again, take **one discarded
 * read** (`000C1C`, whose `d1` the loop immediately overwrites), then compare
 * from the next read on. Reproduced here because a mismatch in this sequence is
 * what the self-test reports as `SUBTEST 02`, and a test is a faster way to ask
 * than another 262,000-instruction run. */
static void test_the_data_port_round_trips_the_firmwares_own_pattern(void) {
  ap_ring_ctl_t ctl;
  ap_ring_ctl_reset(&ctl, true);

  /* **Driven a byte at a time, which is the layer that had the defect.** The
   * board splits every `move.w` into two byte accesses, and the port advances
   * on each one -- so a test that calls `read16`/`write16` directly passes
   * against a model that consumes two buffer words per firmware word. It did.
   * These helpers are what the board calls. */
  const uint32_t port = AP_RING_CTL_BANK_STATUS + 6u;
  const uint32_t ptr = AP_RING_CTL_BANK_ID + 6u;

  for (unsigned pass = 0; pass < 2u; pass++) {
    /* Two patterns in succession, because subtest 03 fills with zero *after*
     * subtest 02 filled with ones -- the case where anything left over from the
     * previous pass shows up as the wrong answer. */
    const uint16_t pattern = (pass == 0u) ? 0x0001u : 0x0000u;

    ap_ring_ctl_write16(&ctl, true, ptr, 0u);
    for (unsigned i = 0; i < AP_RING_CTL_BUFFER_WORDS; i++) {
      ap_ring_ctl_write8(&ctl, true, port, (uint8_t)(pattern >> 8));
      ap_ring_ctl_write8(&ctl, true, port + 1u, (uint8_t)(pattern & 0xFFu));
    }

    ap_ring_ctl_write16(&ctl, true, ptr, 0u);
    /* The firmware's discarded first read, `000C1C`. */
    (void)ap_ring_ctl_read8(&ctl, true, port);
    (void)ap_ring_ctl_read8(&ctl, true, port + 1u);

    for (unsigned i = 0; i < 8u; i++) {
      const uint8_t high = ap_ring_ctl_read8(&ctl, true, port);
      const uint8_t low = ap_ring_ctl_read8(&ctl, true, port + 1u);
      const uint16_t got = (uint16_t)(((uint16_t)high << 8) | low);
      TEST_ASSERT_EQUAL_HEX16(pattern, got);
    }
  }
}

/* **The first window reads the node ID; the second reads the board type.**
 * `[EH]` p. 12-29 (`RING.md` 93) tabulates bus `220`-`226` as `Node_ID3` (msb)
 * through `Node_ID0` (lsb) and `59000` as `BOARD_TYPE`. This core answered the
 * board type from **both** windows until that page was read, and nothing
 * caught it: finding 50a established the firmware never reads the first
 * window, so the register was unexercised and answering the wrong thing.
 *
 * The byte sits in the high lane, this board's convention throughout, with the
 * odd half undriven -- the same shape finding 62 established for the ID. */
static void test_the_first_window_reads_the_node_id(void) {
  ap_ring_ctl_t ctl;
  ap_ring_ctl_reset(&ctl, true);
  ap_ring_ctl_set_node_id(&ctl, 0x00012345u);

  /* Node_ID3 is the most significant of the four, and a 24-bit node leaves it
   * zero -- which is what `ap_nodeid.h` records the PROM's own dump showing. */
  TEST_ASSERT_EQUAL_HEX16(0x00FFu, ap_ring_ctl_read16(&ctl, false, 0u));
  TEST_ASSERT_EQUAL_HEX16(0x01FFu, ap_ring_ctl_read16(&ctl, false, 2u));
  TEST_ASSERT_EQUAL_HEX16(0x23FFu, ap_ring_ctl_read16(&ctl, false, 4u));
  TEST_ASSERT_EQUAL_HEX16(0x45FFu, ap_ring_ctl_read16(&ctl, false, 6u));

  /* And the second window still answers the board type, which is what finding
   * 39's `cmpi.b #$36` gate reads and what the self-test depends on. */
  TEST_ASSERT_EQUAL_HEX16((uint16_t)((AP_RING_CTL_ID_6 << 8) | 0x00FFu),
                          ap_ring_ctl_read16(&ctl, true, 0u));

  /* An unfitted board drives nothing, on either window. */
  ap_ring_ctl_reset(&ctl, false);
  TEST_ASSERT_EQUAL_HEX16(0xFFFFu, ap_ring_ctl_read16(&ctl, false, 2u));
}

/* The three idle words are the manual's bit tables, not magic numbers.
 *
 * Each was reached from the firmware -- subtest 01's `$F806`, subtest 13's
 * `$F0`, subtest 15's `$E0` -- and each sat in the header as a literal for as
 * long as its bits had no names. `002398-04` pp. 12-30 and 12-31 name every one
 * of them, and each constant turns out to be exactly "a just-reset board":
 * disconnected, enabled, not busy, no errors. Asserting the decomposition is
 * what keeps the names and the numbers from drifting apart -- a renaming that
 * moved a bit would still satisfy every behavioural test in this suite, because
 * they all compare against the constant rather than against its meaning.
 *
 * `+402`'s `pe` is a *selector* and `+404`'s is a status bit, which is why only
 * the low byte of XMIT_STAT is decomposed here: bits 14-8 have no single
 * meaning to assert. */
static void test_the_idle_words_are_the_manuals_bits_and_not_magic(void) {
  /* `+402`: not connected, transmit enabled, not initialize busy, not
   * transmit busy -- all four active low, so all four read set. */
  TEST_ASSERT_EQUAL_HEX16(AP_RING_CTL_COMMAND_STATUS_IDLE,
                          AP_RING_CTL_XMIT_NCT | AP_RING_CTL_XMIT_XEN |
                              AP_RING_CTL_XMIT_IBY | AP_RING_CTL_XMIT_XBY);

  /* `+404`: the same three, plus no bi-phase or elastic-store error and no
   * receive counter output. Those five are active *high*, so `E0` has them
   * clear -- which is what makes this a different assertion from the one
   * above rather than the same shape twice. */
  TEST_ASSERT_EQUAL_HEX16(AP_RING_CTL_COMMAND2_STATUS_IDLE,
                          AP_RING_CTL_RCV_NCT | AP_RING_CTL_RCV_REN |
                              AP_RING_CTL_RCV_RBY);
  TEST_ASSERT_EQUAL_HEX16(0u, AP_RING_CTL_COMMAND2_STATUS_IDLE &
                                  (AP_RING_CTL_RCV_BPE | AP_RING_CTL_RCV_ESB |
                                   AP_RING_CTL_RCV_RC2 | AP_RING_CTL_RCV_RC1 |
                                   AP_RING_CTL_RCV_RC0));

  /* `+400`: subtest 01's seven bits, plus `tmi`. The firmware's mask stops at
   * `$F806` so it never constrains bit 0; `RING_PROC` does -- it branches past
   * its error call when the bit is set, so a healthy board reads 1
   * (`RING.md` 111). `gps` stays clear: p. 12-30 marks it `<=1`, active high,
   * and an idle board has seen no good packet. */
  TEST_ASSERT_EQUAL_HEX16(AP_RING_CTL_STATUS_IDLE,
                          AP_RING_CTL_STATUS_PRESENT | AP_RING_CTL_STATUS_TMO |
                              AP_RING_CTL_STATUS_XBY | AP_RING_CTL_STATUS_RBY |
                              AP_RING_CTL_STATUS_IOV | AP_RING_CTL_STATUS_XI |
                              AP_RING_CTL_STATUS_RI | AP_RING_CTL_STATUS_TMI);
  TEST_ASSERT_EQUAL_HEX16(0u, AP_RING_CTL_STATUS_IDLE & AP_RING_CTL_STATUS_GPS);
  /* And the firmware's own mask still passes, which is what makes this a
   * reading of a bit it never checked rather than a change to one it did. */
  TEST_ASSERT_EQUAL_HEX16(0xF806u, AP_RING_CTL_STATUS_IDLE & 0xF806u);

  /* `pke` and `de` are transposed between the two status registers. Asserting
   * it is the point: a header written from one table and applied to both would
   * pass every other test in this file. */
  TEST_ASSERT_EQUAL_HEX16(0x0800u, AP_RING_CTL_XMIT_PKE);
  TEST_ASSERT_EQUAL_HEX16(0x0400u, AP_RING_CTL_XMIT_DE);
  TEST_ASSERT_EQUAL_HEX16(0x0400u, AP_RING_CTL_RCV_PKE);
  TEST_ASSERT_EQUAL_HEX16(0x0800u, AP_RING_CTL_RCV_DE);
}

/* **The wire: a frame written into the board's buffer reaches another node.**
 *
 * This is the gap `RING.md` 85e opened and every later finding worked around.
 * `ap_ring_station` was called by nobody outside its own module and tests, so
 * the whole audited protocol stack was unreachable from the register interface
 * the Domain/OS driver actually writes to. The assertion that matters is at
 * **station 1**: the frame has to have travelled, not merely been assembled. */
static void test_a_transmit_command_puts_the_buffers_frame_on_the_ring(void) {
  static wired_t w;
  static uint8_t txbuf[2048];
  wired_build(&w);
  ap_ring_station_attach_tx(&w.station[0], txbuf, sizeof txbuf);
  ap_ring_station_set_address(&w.station[1], 0x00ABCDEFu);

  /* A §2.2.2 header in the board's buffer, at word 0x40. */
  uint8_t header[AP_RING_CTL_XMIT_HEADER_BYTES] = {0};
  ap_ring_header_set_destination(header, 0x00ABCDEFu);
  ap_ring_header_set_type(header, AP_RING_TYPE_USER);
  ap_ring_header_set_source(header, 0x00012345u);
  for (unsigned i = 0; i < AP_RING_CTL_XMIT_HEADER_WORDS; i++) {
    w.ctl.buffer[0x40u + i] =
        (uint16_t)((header[i * 2u] << 8) | header[i * 2u + 1u]);
  }
  /* `XMIT_ADDR` is **byte-swapped** (p. 12-32), so word `0x0040` is written as
   * `0x4000`. Taking the register at face value would address word `0x4000`,
   * 16,320 words away, and transmit a frame of zeros -- well-formed rubbish. */
  ap_ring_ctl_write16(&w.ctl, true, AP_RING_CTL_W2_XMIT_ADDR, 0x4000u);

  /* Connect, enable receive, then transmit -- `RING.md` 103e's sequence. */
  ap_ring_ctl_write16(&w.ctl, true, AP_RING_CTL_BANK_STATUS,
                      AP_RING_CTL_MISC_CMD_NCT);
  ap_ring_ctl_write16(&w.ctl, true, AP_RING_CTL_BANK_STATUS + 4u,
                      AP_RING_CTL_RCV_CMD_RCV);
  ap_ring_ctl_write16(&w.ctl, true, AP_RING_CTL_BANK_STATUS + 2u, 0x0200u);

  ap_ring_station_originate_token(&w.station[1], AP_RING_OOB_FREE_TOKEN);
  for (unsigned i = 0; i < 4000u; i++) {
    wired_step(&w);
  }

  /* It travelled, and the addressee took it. */
  TEST_ASSERT_TRUE(w.station[1].frames_seen > 0u);
  TEST_ASSERT_TRUE(w.station[1].frames_addressed > 0u);
  TEST_ASSERT_TRUE(ap_ring_station_transmitted(&w.station[0]));
}

/* And a command that is not a transmit does not transmit one.
 *
 * `$0100` is the third value both drivers write (`RING.md` 103d) and it starts
 * nothing. A model that queued on any write to the command register would pass
 * the test above and put a frame on the ring every time the firmware
 * initialised the board. */
static void test_only_the_transmit_command_values_queue_a_frame(void) {
  static wired_t w;
  static uint8_t txbuf[2048];
  static const uint16_t quiet[] = {0x0000u, 0x0100u, 0x0001u, 0x8000u};
  for (unsigned c = 0; c < sizeof quiet / sizeof quiet[0]; c++) {
    wired_build(&w);
    ap_ring_station_attach_tx(&w.station[0], txbuf, sizeof txbuf);
    ap_ring_ctl_write16(&w.ctl, true, AP_RING_CTL_BANK_STATUS + 2u, quiet[c]);
    TEST_ASSERT_FALSE(w.station[0].tx_armed);
  }
  /* Both transmit values do arm it: `$0200` and the forced `$0600`. */
  static const uint16_t sends[] = {0x0200u, 0x0600u};
  for (unsigned c = 0; c < sizeof sends / sizeof sends[0]; c++) {
    wired_build(&w);
    ap_ring_station_attach_tx(&w.station[0], txbuf, sizeof txbuf);
    ap_ring_ctl_write16(&w.ctl, true, AP_RING_CTL_BANK_STATUS + 2u, sends[c]);
    TEST_ASSERT_TRUE(w.station[0].tx_armed);
  }
}

/* The two command bits that drive the station, from `RING.md` 103c -- both
 * confirmed on this board by two independent drivers plus the page. */
static void test_the_command_registers_drive_the_relay_and_the_receiver(void) {
  static wired_t w;
  wired_build(&w);

  /* `RCV_CMD` bit 11. `receive_enabled` defaults on (finding 89c), so the
   * clear direction is the one that proves the wire rather than the default. */
  ap_ring_ctl_write16(&w.ctl, true, AP_RING_CTL_BANK_STATUS + 4u, 0u);
  TEST_ASSERT_FALSE(w.station[0].receive_enabled);
  ap_ring_ctl_write16(&w.ctl, true, AP_RING_CTL_BANK_STATUS + 4u,
                      AP_RING_CTL_RCV_CMD_RCV);
  TEST_ASSERT_TRUE(w.station[0].receive_enabled);

  /* `MISC_CMD` bit 11 `nct` operates §3.5's bypass relay, which
   * `ap_ring_medium` has modelled both halves of since finding 30 with nothing
   * driving it. Connect means *not* bypassed. */
  ap_ring_ctl_write16(&w.ctl, true, AP_RING_CTL_BANK_STATUS,
                      AP_RING_CTL_MISC_CMD_NCT);
  TEST_ASSERT_FALSE(w.medium.node[w.station[0].slot].bypass.bypassed);
  ap_ring_ctl_write16(&w.ctl, true, AP_RING_CTL_BANK_STATUS, 0u);
  TEST_ASSERT_TRUE(w.medium.node[w.station[0].slot].bypass.bypassed);

  /* And attaching gives the station the board's node ID: `[MAC]` §2.2.2.2
   * compares against "the node address of the target", and the board's node
   * comes from the ID PROM (finding 93i). Two numbers, one source. */
  TEST_ASSERT_EQUAL_HEX32(0x00012345u, w.station[0].address);

  /* **`ap_ring_ctl_reset` is the initialiser too, so it clears the wire** --
   * and this assertion is the one that matters, because the alternative was
   * tried and segfaulted. Preserving the pointers across the reset, so a board
   * reset would not "unplug the cable", reads them out of a caller's
   * *uninitialised* stack local on the first call; the garbage survives the
   * `memset` and the first `MISC_CMD` write dereferences it. Attach after
   * reset, never before. */
  ap_ring_ctl_reset(&w.ctl, true);
  TEST_ASSERT_NULL(w.ctl.station);
  TEST_ASSERT_NULL(w.ctl.medium);
}

/* **The other half of the wire: a frame from another node lands in the buffer
 * at `RCV_ADDR`, and `ri` goes pending.**
 *
 * Where and how much are the firmware's own answers, not a choice. `$944` sets
 * `RCV_ADDR = $10` (finding 98d) and finding 50's loopback then does
 * `+006 = $10` and reads **four words** back through `+406`, reassembling a
 * long to compare against what it sent. Eight bytes at that address is exactly
 * what the board's diagnostic expects to find. */
static void test_a_received_frame_lands_at_rcv_addr_and_raises_ri(void) {
  static wired_t w;
  static uint8_t txbuf[2048];
  wired_build(&w);
  /* Station 1 sends; the controller's node is the destination. */
  ap_ring_station_attach_tx(&w.station[1], txbuf, sizeof txbuf);
  uint8_t header[12] = {0};
  ap_ring_header_set_destination(header, 0x00012345u);
  ap_ring_header_set_type(header, AP_RING_TYPE_USER);
  ap_ring_header_set_source(header, 0x00ABCDEFu);
  const ap_ring_frame_fields_t fields = {.header = header,
                                         .header_bytes = sizeof header,
                                         .data = NULL,
                                         .data_bytes = 0u,
                                         .late_acknowledge = 0u};
  TEST_ASSERT_TRUE(ap_ring_station_queue_frame(&w.station[1], &fields));

  /* `RCV_ADDR = $10`, byte-swapped in the register as `$1000`. */
  ap_ring_ctl_write16(&w.ctl, true, AP_RING_CTL_W2_RCV_ADDR, 0x1000u);
  ap_ring_ctl_write16(&w.ctl, true, AP_RING_CTL_BANK_STATUS,
                      AP_RING_CTL_MISC_CMD_NCT);
  ap_ring_ctl_write16(&w.ctl, true, AP_RING_CTL_BANK_STATUS + 4u,
                      AP_RING_CTL_RCV_CMD_RCV);

  /* `ri` is active low -- "RCV intr pending <=0" -- so it starts set. */
  TEST_ASSERT_TRUE((w.ctl.a2.status & AP_RING_CTL_STATUS_RI) != 0u);

  ap_ring_station_originate_token(&w.station[0], AP_RING_OOB_FREE_TOKEN);
  for (unsigned i = 0; i < 4000u; i++) {
    wired_step(&w);
    ap_ring_ctl_poll_ring(&w.ctl);
  }

  TEST_ASSERT_TRUE(w.station[0].frames_copied > 0u);
  /* The destination the sender addressed is what the buffer now holds, at
   * word `$10` and not word `$1000` -- the byte swap again. */
  TEST_ASSERT_EQUAL_HEX16(0x0001u, w.ctl.buffer[0x10u]);
  TEST_ASSERT_EQUAL_HEX16(0x2345u, w.ctl.buffer[0x11u]);
  TEST_ASSERT_EQUAL_HEX16(0u, w.ctl.buffer[0x1000u]);
  /* And the interrupt is pending, which for an active-low bit is *clear*. */
  TEST_ASSERT_EQUAL_HEX16(0u, w.ctl.a2.status & AP_RING_CTL_STATUS_RI);

  /* `RCV_ACK` at the first window's `+4` puts it back -- finding 74a's half of
   * the pair, which until now acknowledged an interrupt nothing ever raised. */
  ap_ring_ctl_write16(&w.ctl, false, AP_RING_CTL_W1_RCV_ACK, 0u);
  TEST_ASSERT_TRUE((w.ctl.a2.status & AP_RING_CTL_STATUS_RI) != 0u);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_transmit_command_puts_the_buffers_frame_on_the_ring);
  RUN_TEST(test_a_received_frame_lands_at_rcv_addr_and_raises_ri);
  RUN_TEST(test_only_the_transmit_command_values_queue_a_frame);
  RUN_TEST(test_the_command_registers_drive_the_relay_and_the_receiver);
  RUN_TEST(test_the_idle_words_are_the_manuals_bits_and_not_magic);
  RUN_TEST(test_the_first_windows_write_only_registers_clear_what_they_name);
  RUN_TEST(test_the_at_boards_command_bytes_are_not_the_dn3000s_bits);
  RUN_TEST(test_the_data_port_round_trips_the_firmwares_own_pattern);
  RUN_TEST(test_a_unit_is_both_of_its_at_windows);
  RUN_TEST(test_the_id_register_answers_one_of_the_two_values_init_accepts);
  RUN_TEST(test_an_empty_slot_reads_as_absent_rather_than_as_an_error);
  RUN_TEST(test_the_init_clear_sequence_does_not_erase_the_presence_gate);
  RUN_TEST(test_the_firmwares_timer_initialisation_reaches_two_8254s);
  RUN_TEST(test_a_word_access_touches_the_timer_exactly_once);
  RUN_TEST(test_the_two_windows_are_separate_register_sets);
  RUN_TEST(test_the_unknown_command_slots_are_storage_and_nothing_more);
  RUN_TEST(test_the_data_port_answers_one_word_behind);
  RUN_TEST(test_the_first_windows_data_slot_is_not_the_buffer_port);
  RUN_TEST(test_the_firmwares_own_memory_test_passes);
  RUN_TEST(test_the_first_window_reads_the_node_id);
  return UNITY_END();
}
