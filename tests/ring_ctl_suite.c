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

  /* `00010A`: the later `$800`. Bit 11's purpose is unknown -- open question A
   * -- so all this asserts is that the board keeps what the host wrote, which
   * is the whole of what "unknown, modelled as storage" means. */
  ap_ring_ctl_write16(&ctl, true, AP_RING_CTL_BANK_STATUS,
                      AP_RING_CTL_STATUS_BIT11);
  TEST_ASSERT_EQUAL_HEX16(
      AP_RING_CTL_STATUS_BIT11 | AP_RING_CTL_STATUS_PRESENT,
      ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_STATUS));
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
  ap_ring_ctl_write16(&ctl, false, AP_RING_CTL_BANK_STATUS + 2u, 0xBEEFu);

  TEST_ASSERT_EQUAL_HEX8(0xB0u, ctl.a2.timer_a.counter[2].control);
  TEST_ASSERT_EQUAL_HEX8(0x00u, ctl.a1.timer_a.counter[2].control);
  TEST_ASSERT_EQUAL_HEX16(
      0xBEEFu, ap_ring_ctl_read16(&ctl, false, AP_RING_CTL_BANK_STATUS + 2u));
  TEST_ASSERT_EQUAL_HEX16(
      0x0000u, ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_STATUS + 2u));
}

/* `+402`, `+404` and `+406` have **no known meaning** -- open question A. They
 * are storage, and this test says exactly that and no more: it asserts a
 * read-back, not a behaviour, so that inventing one later fails here first. */
static void test_the_unknown_status_slots_are_storage_and_nothing_more(void) {
  ap_ring_ctl_t ctl;
  ap_ring_ctl_reset(&ctl, true);

  ap_ring_ctl_write16(&ctl, true, AP_RING_CTL_BANK_STATUS + 2u, 0x1111u);
  ap_ring_ctl_write16(&ctl, true, AP_RING_CTL_BANK_STATUS + 4u, 0x2222u);
  ap_ring_ctl_write16(&ctl, true, AP_RING_CTL_BANK_STATUS + 6u, 0x3333u);

  TEST_ASSERT_EQUAL_HEX16(
      0x1111u, ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_STATUS + 2u));
  TEST_ASSERT_EQUAL_HEX16(
      0x2222u, ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_STATUS + 4u));
  TEST_ASSERT_EQUAL_HEX16(
      0x3333u, ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_STATUS + 6u));

  /* Writing them changes nothing else: no side effect on the gate, the ID or
   * the timers. */
  TEST_ASSERT_EQUAL_HEX16(
      AP_RING_CTL_STATUS_PRESENT,
      ap_ring_ctl_read16(&ctl, true, AP_RING_CTL_BANK_STATUS));
  TEST_ASSERT_EQUAL_HEX8(AP_RING_CTL_ID_6,
                         ap_ring_ctl_read8(&ctl, true, AP_RING_CTL_BANK_ID));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_unit_is_both_of_its_at_windows);
  RUN_TEST(test_the_id_register_answers_one_of_the_two_values_init_accepts);
  RUN_TEST(test_an_empty_slot_reads_as_absent_rather_than_as_an_error);
  RUN_TEST(test_the_init_clear_sequence_does_not_erase_the_presence_gate);
  RUN_TEST(test_the_firmwares_timer_initialisation_reaches_two_8254s);
  RUN_TEST(test_a_word_access_touches_the_timer_exactly_once);
  RUN_TEST(test_the_two_windows_are_separate_register_sets);
  RUN_TEST(test_the_unknown_status_slots_are_storage_and_nothing_more);
  return UNITY_END();
}
