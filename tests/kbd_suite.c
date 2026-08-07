/* The Apollo keyboard: a serial device that reports key transitions.
 *
 * The tests are about what it sends and, more importantly, what it refuses to
 * send. A keyboard that emits codes the hardware never emits is not caught by
 * anything downstream — the boot PROM's translation table would happily match
 * them.
 */

#include "device/ap_kbd.h"

#include <string.h>

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Key down sends the index; key up sends it with bit 7 set. `Numpad 1` is at
 * matrix position `4B`, so it is `4B` down and `CB` up — which is the pair the
 * boot PROM's own table carries. */
static void test_a_key_sends_its_index_down_and_bit_seven_up(void) {
  ap_kbd_t kbd;
  ap_kbd_reset(&kbd);
  uint8_t code = 0;

  TEST_ASSERT_TRUE(ap_kbd_press(&kbd, 0x4Bu, &code));
  TEST_ASSERT_EQUAL_HEX8(0x4Bu, code);
  TEST_ASSERT_TRUE(ap_kbd_release(&kbd, 0x4Bu, &code));
  TEST_ASSERT_EQUAL_HEX8(0xCBu, code);
}

/* The release code is the make code plus the flag, for every key — not a second
 * table that could disagree with the first. */
static void test_every_release_is_its_press_plus_the_flag(void) {
  for (unsigned key = 0; key < AP_KBD_KEYS; key++) {
    ap_kbd_t kbd;
    ap_kbd_reset(&kbd);
    uint8_t down = 0;
    uint8_t up = 0;
    TEST_ASSERT_TRUE(ap_kbd_press(&kbd, key, &down));
    TEST_ASSERT_TRUE(ap_kbd_release(&kbd, key, &up));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)key, down);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(key | AP_KBD_RELEASE), up);
  }
}

/* A repeated press and a release of a key that was never down send nothing. A
 * real matrix scan cannot report a transition that did not happen, and a model
 * that let one through would let a caller desynchronise the firmware's own
 * shift state — which it tracks from these transitions and nothing else. */
static void test_a_non_transition_sends_nothing(void) {
  ap_kbd_t kbd;
  ap_kbd_reset(&kbd);
  uint8_t code = 0;

  TEST_ASSERT_FALSE(ap_kbd_release(&kbd, 0x4Bu, &code)); /* never pressed */
  TEST_ASSERT_TRUE(ap_kbd_press(&kbd, 0x4Bu, &code));
  TEST_ASSERT_FALSE(ap_kbd_press(&kbd, 0x4Bu, &code));   /* already down */
  TEST_ASSERT_TRUE(ap_kbd_release(&kbd, 0x4Bu, &code));
  TEST_ASSERT_FALSE(ap_kbd_release(&kbd, 0x4Bu, &code)); /* already up */
}

/* The matrix is 128 keys because bit 7 is the release flag. A key at or above
 * `0x80` would have a make code indistinguishable from some other key's break
 * code, so the bound is not arbitrary and is refused rather than masked. */
static void test_a_key_outside_the_matrix_is_refused(void) {
  ap_kbd_t kbd;
  ap_kbd_reset(&kbd);
  uint8_t code = 0xEEu;

  TEST_ASSERT_FALSE(ap_kbd_press(&kbd, AP_KBD_KEYS, &code));
  TEST_ASSERT_FALSE(ap_kbd_press(&kbd, 0xFFu, &code));
  /* Untouched: a refused call must not leave a plausible code behind for a
   * caller that forgot to check the return. */
  TEST_ASSERT_EQUAL_HEX8(0xEEu, code);
}

/* Shift and control are keys, not modifiers folded into another key's code.
 * `4B`, `5B` and `7B` differ only in bits 4 and 5, which looks exactly like a
 * modifier encoding and is not — they are `Numpad 1`, `F10` and a third
 * unrelated key. Each must send its own code and nothing else. */
static void test_keys_that_look_like_modifier_variants_are_independent(void) {
  ap_kbd_t kbd;
  ap_kbd_reset(&kbd);
  uint8_t code = 0;

  TEST_ASSERT_TRUE(ap_kbd_press(&kbd, 0x4Bu, &code));
  TEST_ASSERT_EQUAL_HEX8(0x4Bu, code);
  /* Pressing it does not make its neighbours down, and theirs are their own. */
  TEST_ASSERT_TRUE(ap_kbd_press(&kbd, 0x5Bu, &code));
  TEST_ASSERT_EQUAL_HEX8(0x5Bu, code);
  TEST_ASSERT_TRUE(ap_kbd_press(&kbd, 0x7Bu, &code));
  TEST_ASSERT_EQUAL_HEX8(0x7Bu, code);
  /* And each releases independently. */
  TEST_ASSERT_TRUE(ap_kbd_release(&kbd, 0x5Bu, &code));
  TEST_ASSERT_EQUAL_HEX8(0xDBu, code);
  TEST_ASSERT_TRUE(ap_kbd_release(&kbd, 0x4Bu, &code));
  TEST_ASSERT_EQUAL_HEX8(0xCBu, code);
}

/* ---- The ASCII set, `008778-03` Table 12-1 -------------------------------- */

/* The rows the boot PROM's own translation table depends on. Each is a
 * spot-check of the transcription against the page image, and together they are
 * the evidence that the PROM table's left-hand column is Table 12-1 codes. */
static void test_table_12_1_gives_the_codes_the_prom_translates(void) {
  const ap_kbd_ascii_t *k = ap_kbd_ascii_find("D13"); /* RETURN */
  TEST_ASSERT_NOT_NULL(k);
  TEST_ASSERT_EQUAL_HEX16(0xCBu, k->unshifted);
  TEST_ASSERT_EQUAL_HEX16(0xDBu, k->shifted);
  /* RETURN has no control code and no up-transition code. */
  TEST_ASSERT_EQUAL_HEX16(AP_KBD_NO_CODE, k->control);
  TEST_ASSERT_EQUAL_HEX16(AP_KBD_NO_CODE, k->up_trans);

  k = ap_kbd_ascii_find("C1"); /* TAB: three codes, all three in the PROM table */
  TEST_ASSERT_EQUAL_HEX16(0xCAu, k->unshifted);
  TEST_ASSERT_EQUAL_HEX16(0xDAu, k->shifted);
  TEST_ASSERT_EQUAL_HEX16(0xFAu, k->control);

  k = ap_kbd_ascii_find("B15"); /* BACK SPACE: same code shifted or not */
  TEST_ASSERT_EQUAL_HEX16(0xDEu, k->unshifted);
  TEST_ASSERT_EQUAL_HEX16(0xDEu, k->shifted);
  TEST_ASSERT_TRUE(k->auto_repeat);

  k = ap_kbd_ascii_find("E11"); /* ? / */
  TEST_ASSERT_EQUAL_HEX16(0xCCu, k->unshifted);
  TEST_ASSERT_EQUAL_HEX16(0xDCu, k->shifted);
  TEST_ASSERT_EQUAL_HEX16(0xFCu, k->control);
}

/* An ordinary key sends its character, which is why most of a typed line needs
 * no translation at all. */
static void test_an_ordinary_key_sends_the_character_itself(void) {
  const ap_kbd_ascii_t *a = ap_kbd_ascii_find("D2");
  TEST_ASSERT_EQUAL_STRING("A", a->legend);
  TEST_ASSERT_EQUAL_HEX16('a', a->unshifted);
  TEST_ASSERT_EQUAL_HEX16('A', a->shifted);
  TEST_ASSERT_EQUAL_HEX16(0x01u, a->control);
  /* Caps lock gives the capital where shift does, and they are separate
   * columns because they differ on the keys that are not letters. */
  TEST_ASSERT_EQUAL_HEX16('A', a->caps_lock);

  const ap_kbd_ascii_t *one = ap_kbd_ascii_find("B2");
  TEST_ASSERT_EQUAL_HEX16('1', one->unshifted);
  TEST_ASSERT_EQUAL_HEX16('!', one->shifted);
  TEST_ASSERT_EQUAL_HEX16('1', one->caps_lock); /* not `!` */
}

/* **The correction.** `5B` and `7B` are one key's two codes, not two keys. This
 * file's own header used to say the opposite, on the strength of the three
 * differing only in bits 4 and 5 -- "looks exactly like shift and control
 * encoded into a base key, and is not". Table 12-1 says it is. */
static void test_5b_and_7b_are_one_key_shifted_and_unshifted(void) {
  const ap_kbd_ascii_t *k = ap_kbd_ascii_find("C12");
  TEST_ASSERT_NOT_NULL(k);
  TEST_ASSERT_EQUAL_HEX16(0x7Bu, k->unshifted);
  TEST_ASSERT_EQUAL_HEX16(0x5Bu, k->shifted);

  /* And the keyboard sends them the opposite way round from the US
   * convention -- `{` unshifted, `[` shifted -- which is exactly why the boot
   * PROM's table carries `5B -> 7B` beside `7B -> 5B`. Two entries that look
   * like a self-cancelling pair are a layout correction. */
  TEST_ASSERT_EQUAL_HEX16(0x7Bu, ap_kbd_prom_ascii(0x5Bu));
  TEST_ASSERT_EQUAL_HEX16(0x5Bu, ap_kbd_prom_ascii(0x7Bu));
}

/* The keypad sends two bytes, `FE` then the character. A model that dropped the
 * prefix would make keypad `7` indistinguishable from the main `7`. */
static void test_the_keypad_prefixes_its_codes(void) {
  const ap_kbd_ascii_t *k = ap_kbd_ascii_find("RC1");
  TEST_ASSERT_EQUAL_HEX16(AP_KBD_PREFIX | '7', k->unshifted);
  TEST_ASSERT_EQUAL_HEX16(AP_KBD_PREFIX | '&', k->shifted);

  /* Including ENTER, whose payload is RETURN's own code -- so the two are the
   * same character and still tell apart. */
  const ap_kbd_ascii_t *enter = ap_kbd_ascii_find("RF3");
  TEST_ASSERT_EQUAL_HEX16(AP_KBD_PREFIX | 0xCBu, enter->unshifted);
  TEST_ASSERT_EQUAL_HEX16(0xCBu, ap_kbd_ascii_find("D13")->unshifted);
}

/* The state keys have no codes at all, and are absent from the table rather
 * than present with zeroes. Table 12-1 gives their rows the words "Control
 * Key", "Shift Key" and so on where the codes would be. */
static void test_the_state_keys_send_nothing(void) {
  TEST_ASSERT_NULL(ap_kbd_ascii_find("D0"));  /* CTRL */
  TEST_ASSERT_NULL(ap_kbd_ascii_find("D1"));  /* CAPS LOCK */
  TEST_ASSERT_NULL(ap_kbd_ascii_find("E1"));  /* SHIFT */
  TEST_ASSERT_NULL(ap_kbd_ascii_find("E12")); /* SHIFT, the other one */
  TEST_ASSERT_NULL(ap_kbd_ascii_find("E0"));  /* REPEAT */
}

/* Only some keys report their release. The release-code reading assumed all of
 * them did, which is what made the PROM's table look like release traffic. */
static void test_most_keys_have_no_up_transition_code(void) {
  TEST_ASSERT_EQUAL_HEX16(AP_KBD_NO_CODE, ap_kbd_ascii_find("D2")->up_trans);
  TEST_ASSERT_EQUAL_HEX16(AP_KBD_NO_CODE, ap_kbd_ascii_find("C1")->up_trans);
  /* The function and editing keys do. */
  TEST_ASSERT_EQUAL_HEX16(0xE0u, ap_kbd_ascii_find("A1")->up_trans);
  TEST_ASSERT_EQUAL_HEX16(0xA1u, ap_kbd_ascii_find("LA0")->up_trans);
}

/* What a frontend needs: the code to send so the firmware sees a character. */
static void test_encoding_a_character_picks_the_key_that_produces_it(void) {
  uint16_t code = 0u;
  bool shifted = true;

  TEST_ASSERT_TRUE(ap_kbd_encode('a', &code, &shifted));
  TEST_ASSERT_EQUAL_HEX16('a', code);
  TEST_ASSERT_FALSE(shifted);

  TEST_ASSERT_TRUE(ap_kbd_encode('A', &code, &shifted));
  TEST_ASSERT_EQUAL_HEX16('A', code);
  TEST_ASSERT_TRUE(shifted);

  /* A carriage return is **not** `0D` on the wire. No key on this keyboard
   * sends `0D` as a character; RETURN sends `CB` and the firmware translates.
   * Sending `0D` raw is the mistake this function exists to prevent, and it is
   * the one that made an earlier scripted boot feed bytes the hardware never
   * produces. */
  TEST_ASSERT_TRUE(ap_kbd_encode('\r', &code, &shifted));
  TEST_ASSERT_EQUAL_HEX16(0xCBu, code);
  TEST_ASSERT_FALSE(shifted);

  /* Likewise a backslash, which is only reachable through the `| \\` key. */
  TEST_ASSERT_TRUE(ap_kbd_encode('\\', &code, &shifted));
  TEST_ASSERT_EQUAL_HEX16(0xC8u, code);

  /* And a character no key produces is refused rather than invented. */
  TEST_ASSERT_FALSE(ap_kbd_encode('\x01', &code, &shifted));
}

/* ---- Auto-repeat ---------------------------------------------------------- */

/* `008778-03` Chapter 12: auto-repeat keys "repeat the down transition only at
 * 33 milliseconds (+/- 3) after an initial delay of 500 milliseconds (+/- 50)".
 * Both land exactly on the time base, so neither is rounded on top of being a
 * nominal with a tolerance. */
static void test_the_repeat_figures_are_the_manual_s_and_are_exact(void) {
  TEST_ASSERT_EQUAL_UINT64(AP_TIME_BASE_HZ / 2u, AP_KBD_REPEAT_DELAY);
  TEST_ASSERT_EQUAL_UINT64(AP_TIME_BASE_HZ * 33u / 1000u, AP_KBD_REPEAT_PERIOD);
  /* 33 ms exactly: the base divides it. */
  TEST_ASSERT_EQUAL_UINT64(0u, (AP_TIME_BASE_HZ * 33u) % 1000u);
}

/* The delay governs the first repeat and the period every one after it. A model
 * that used the period throughout would repeat fifteen times before the real
 * keyboard had repeated once. */
static void test_the_first_repeat_waits_the_delay_and_the_rest_the_period(void) {
  ap_kbd_t k;
  uint8_t code = 0u;
  unsigned key = 0u;
  ap_kbd_reset(&k);
  TEST_ASSERT_TRUE(ap_kbd_press(&k, 0x20u, &code));

  /* One unit short of the delay: nothing. */
  TEST_ASSERT_FALSE(ap_kbd_advance(&k, AP_KBD_REPEAT_DELAY - 1u, &key));
  /* At the delay: the first repeat, naming the held key. */
  TEST_ASSERT_TRUE(ap_kbd_advance(&k, AP_KBD_REPEAT_DELAY, &key));
  TEST_ASSERT_EQUAL_UINT(0x20u, key);

  /* Then the period, not the delay again. */
  TEST_ASSERT_FALSE(
      ap_kbd_advance(&k, AP_KBD_REPEAT_DELAY + AP_KBD_REPEAT_PERIOD - 1u, &key));
  TEST_ASSERT_TRUE(
      ap_kbd_advance(&k, AP_KBD_REPEAT_DELAY + AP_KBD_REPEAT_PERIOD, &key));
}

/* The deadline advances by a whole period rather than being reset to `now`, so
 * a coarse advance does not lose the intervals it stepped over -- the same
 * property every other advance in this core keeps. */
static void test_a_coarse_advance_does_not_lose_repeats(void) {
  ap_kbd_t k;
  uint8_t code = 0u;
  unsigned key = 0u;
  ap_kbd_reset(&k);
  TEST_ASSERT_TRUE(ap_kbd_press(&k, 0x20u, &code));

  /* Jump well past the delay and three periods. */
  const ap_time_t far = AP_KBD_REPEAT_DELAY + AP_KBD_REPEAT_PERIOD * 3u;
  TEST_ASSERT_TRUE(ap_kbd_advance(&k, far, &key));
  /* The three it stepped over are still owed, and come out one per call at the
   * same instant rather than being discarded. */
  TEST_ASSERT_TRUE(ap_kbd_advance(&k, far, &key));
  TEST_ASSERT_TRUE(ap_kbd_advance(&k, far, &key));
  TEST_ASSERT_TRUE(ap_kbd_advance(&k, far, &key));
  TEST_ASSERT_FALSE(ap_kbd_advance(&k, far, &key));
}

/* Releasing the repeating key stops it, and does not hand the repeat to a key
 * that is still down -- the real part repeats the most recent transition, and
 * reviving an older key would type characters nobody asked for. */
static void test_releasing_the_held_key_stops_the_repeat(void) {
  ap_kbd_t k;
  uint8_t code = 0u;
  unsigned key = 0u;
  ap_kbd_reset(&k);
  TEST_ASSERT_TRUE(ap_kbd_press(&k, 0x20u, &code));
  TEST_ASSERT_TRUE(ap_kbd_press(&k, 0x21u, &code)); /* takes the repeat over */
  TEST_ASSERT_TRUE(ap_kbd_advance(&k, AP_KBD_REPEAT_DELAY * 2u, &key));
  TEST_ASSERT_EQUAL_UINT(0x21u, key);

  TEST_ASSERT_TRUE(ap_kbd_release(&k, 0x21u, &code));
  TEST_ASSERT_FALSE(ap_kbd_advance(&k, AP_KBD_REPEAT_DELAY * 10u, &key));
  /* Even though `0x20` is still down. */
  TEST_ASSERT_TRUE(k.down[0x20u]);
}

/* Only the keys Table 12-1 marks repeat. A keyboard that repeated RETURN would
 * fill a line with them from a key held a moment too long. */
static void test_only_the_keys_the_table_marks_auto_repeat(void) {
  TEST_ASSERT_TRUE(ap_kbd_auto_repeats(ap_kbd_ascii_find("B15")));  /* BACK SPACE */
  TEST_ASSERT_TRUE(ap_kbd_auto_repeats(ap_kbd_ascii_find("F1")));   /* space bar */
  TEST_ASSERT_TRUE(ap_kbd_auto_repeats(ap_kbd_ascii_find("C14")));  /* DELETE */
  TEST_ASSERT_FALSE(ap_kbd_auto_repeats(ap_kbd_ascii_find("D13"))); /* RETURN */
  TEST_ASSERT_FALSE(ap_kbd_auto_repeats(ap_kbd_ascii_find("D2")));  /* A */
  /* A state key is not in the table at all, and answers false rather than
   * dereferencing nothing. */
  TEST_ASSERT_FALSE(ap_kbd_auto_repeats(ap_kbd_ascii_find("E1")));
}

/* ## The command channel
 *
 * The keyboard is not write-only. The host sends commands and it answers, and a
 * machine with a display console asks it to identify itself before believing
 * there is one. `FINDINGS.md` C118.
 */

/* **It powers up in loopback**, echoing what it is sent rather than acting on
 * it -- which is how a host discovers a keyboard is there at all. */
static void test_the_keyboard_powers_up_in_loopback(void) {
  ap_kbd_t k;
  ap_kbd_reset(&k);
  TEST_ASSERT_TRUE(k.loopback);

  uint8_t reply[AP_KBD_REPLY_MAX];
  TEST_ASSERT_EQUAL_UINT(1u, ap_kbd_receive(&k, 0x5Au, reply, sizeof reply));
  TEST_ASSERT_EQUAL_HEX8(0x5Au, reply[0]);
}

/* `00` in loopback ends the conversation and selects the compatibility set. It
 * is **not echoed**, and it **is announced**: two bytes, `FF` then the mode.
 *
 * The history is worth keeping because it is the shape of the mistake. This
 * test once asserted an echo -- one byte -- on the reasoning that the boot PROM
 * writes `00` and polls for a reply. That was reverted when it did not make the
 * PROM's test pass, and the revert was right about the echo and wrong about the
 * conclusion: the reply exists, it is two bytes, and it comes from
 * `apollo_kbd.cpp`'s `set_mode` rather than from a `putdata`. A one-byte echo
 * satisfied the first of the firmware's two reads and could never satisfy the
 * second, which is exactly why it looked like no answer at all. */
static void test_a_zero_leaves_loopback_and_announces_the_mode(void) {
  ap_kbd_t k;
  ap_kbd_reset(&k);
  uint8_t reply[AP_KBD_REPLY_MAX];

  /* Two bytes, and neither is an echo of the `00`. */
  TEST_ASSERT_EQUAL_UINT(2u, ap_kbd_receive(&k, 0x00u, reply, sizeof reply));
  TEST_ASSERT_EQUAL_HEX8(0xFFu, reply[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00u, reply[1]); /* mode 0, compatibility */
  TEST_ASSERT_FALSE(k.loopback);
  TEST_ASSERT_FALSE(k.keystate_mode);
  /* And out of loopback an unrecognised byte is ignored rather than echoed. */
  TEST_ASSERT_EQUAL_UINT(0u, ap_kbd_receive(&k, 0x5Au, reply, sizeof reply));
}

/* Out of loopback `00` announces nothing: the announcement belongs to the mode
 * *change*, and a keyboard already out of loopback is not changing anything.
 * `apollo_kbd.cpp` guards it with `if (m_loopback_mode)`. */
static void test_a_zero_outside_loopback_announces_nothing(void) {
  ap_kbd_t k;
  ap_kbd_reset(&k);
  uint8_t reply[AP_KBD_REPLY_MAX];
  (void)ap_kbd_receive(&k, 0x00u, reply, sizeof reply);
  TEST_ASSERT_FALSE(k.loopback);
  TEST_ASSERT_EQUAL_UINT(0u, ap_kbd_receive(&k, 0x00u, reply, sizeof reply));
}

/* `FF` starts a command **and re-enters loopback**, so a host that has lost
 * track can always get back to a known state. */
static void test_ff_restarts_the_conversation(void) {
  ap_kbd_t k;
  ap_kbd_reset(&k);
  uint8_t reply[AP_KBD_REPLY_MAX];
  TEST_ASSERT_EQUAL_UINT(2u, ap_kbd_receive(&k, 0x00u, reply, sizeof reply));
  TEST_ASSERT_FALSE(k.loopback);

  TEST_ASSERT_EQUAL_UINT(1u, ap_kbd_receive(&k, 0xFFu, reply, sizeof reply));
  TEST_ASSERT_EQUAL_HEX8(0xFFu, reply[0]);
  TEST_ASSERT_TRUE(k.loopback);
}

/* **`FF12` is a prefix and `FF1221` is a command.** A model matching one byte at
 * a time cannot tell them apart, which is why the accumulator is wider than a
 * byte and why a prefix must *keep* the message rather than clearing it -- doing
 * so makes the identification unreachable. */
static void test_the_identification_needs_the_whole_prefix(void) {
  ap_kbd_t k;
  ap_kbd_reset(&k);
  uint8_t reply[AP_KBD_REPLY_MAX];

  TEST_ASSERT_EQUAL_UINT(1u, ap_kbd_receive(&k, 0xFFu, reply, sizeof reply));
  TEST_ASSERT_EQUAL_UINT(1u, ap_kbd_receive(&k, 0x12u, reply, sizeof reply));
  TEST_ASSERT_EQUAL_HEX8(0x12u, reply[0]); /* the prefix is echoed */

  const unsigned n = ap_kbd_receive(&k, 0x21u, reply, sizeof reply);
  /* The echo of `21`, the identification string, then the mode announcement --
   * `apollo_kbd.cpp` ends this arm with a `set_mode` restating the mode it is
   * already in, so the effect is the two bytes and not a change. */
  TEST_ASSERT_EQUAL_HEX8(0x21u, reply[0]);
  const char *id = AP_KBD_IDENTIFICATION;
  const unsigned idlen = (unsigned)strlen(id);
  TEST_ASSERT_EQUAL_UINT(1u + idlen + 2u, n);
  for (unsigned i = 0; id[i] != '\0'; i++) {
    TEST_ASSERT_EQUAL_HEX8((uint8_t)id[i], reply[1u + i]);
  }
  TEST_ASSERT_EQUAL_HEX8(0xFFu, reply[1u + idlen]);
  TEST_ASSERT_EQUAL_HEX8(0x00u, reply[1u + idlen + 1u]);
  /* Identifying leaves loopback: the host now has a keyboard, not an echo. */
  TEST_ASSERT_FALSE(k.loopback);
}

/* The two code sets are commanded, and `008778-03` Chapter 12 names them. */
static void test_the_code_set_is_commanded(void) {
  ap_kbd_t k;
  ap_kbd_reset(&k);
  uint8_t reply[AP_KBD_REPLY_MAX];

  (void)ap_kbd_receive(&k, 0xFFu, reply, sizeof reply);
  (void)ap_kbd_receive(&k, 0x01u, reply, sizeof reply);
  TEST_ASSERT_TRUE(k.keystate_mode);

  (void)ap_kbd_receive(&k, 0xFFu, reply, sizeof reply);
  (void)ap_kbd_receive(&k, 0x00u, reply, sizeof reply);
  TEST_ASSERT_FALSE(k.keystate_mode);
  TEST_ASSERT_FALSE(k.loopback);
}

/* The beeper is acknowledged even though the sound is not modelled: a driver
 * waiting for the acknowledgement would otherwise wait for ever. */
static void test_the_beeper_is_acknowledged_without_being_modelled(void) {
  ap_kbd_t k;
  ap_kbd_reset(&k);
  uint8_t reply[AP_KBD_REPLY_MAX];
  (void)ap_kbd_receive(&k, 0xFFu, reply, sizeof reply);
  (void)ap_kbd_receive(&k, 0x21u, reply, sizeof reply);
  TEST_ASSERT_EQUAL_UINT(1u, ap_kbd_receive(&k, 0x81u, reply, sizeof reply));
  TEST_ASSERT_EQUAL_HEX8(0x81u, reply[0]);
}

static void beep(ap_kbd_t *k, uint8_t which) {
  uint8_t reply[AP_KBD_REPLY_MAX];
  (void)ap_kbd_receive(k, 0xFFu, reply, sizeof reply);
  (void)ap_kbd_receive(k, 0x21u, reply, sizeof reply);
  (void)ap_kbd_receive(k, which, reply, sizeof reply);
}

/* `002398-04` p. 12-2: "$FF $21 $81 $00 ... It will go off automatically after
 * 300 milliseconds." The auto-off is the keyboard's own and is the half of this
 * a host could be timing against, so it is asserted at both edges of the
 * documented interval rather than merely somewhere after it. */
static void test_the_beeper_goes_off_by_itself_after_300_ms(void) {
  ap_kbd_t k;
  ap_kbd_reset(&k);
  TEST_ASSERT_FALSE(ap_kbd_beeper_on(&k));
  beep(&k, 0x81u);
  TEST_ASSERT_TRUE(ap_kbd_beeper_on(&k));

  unsigned key = 0u;
  (void)ap_kbd_advance(&k, AP_KBD_BEEPER_DURATION - 1u, &key);
  TEST_ASSERT_TRUE(ap_kbd_beeper_on(&k));
  (void)ap_kbd_advance(&k, AP_KBD_BEEPER_DURATION, &key);
  TEST_ASSERT_FALSE(ap_kbd_beeper_on(&k));
}

/* The second sequence stops it early, which is the only reason it exists: a
 * tone that always ran its 300 ms would need no off command at all. */
static void test_the_off_sequence_stops_the_tone_early(void) {
  ap_kbd_t k;
  ap_kbd_reset(&k);
  beep(&k, 0x81u);
  unsigned key = 0u;
  (void)ap_kbd_advance(&k, AP_KBD_BEEPER_DURATION / 2u, &key);
  TEST_ASSERT_TRUE(ap_kbd_beeper_on(&k));
  beep(&k, 0x82u);
  TEST_ASSERT_FALSE(ap_kbd_beeper_on(&k));
}

/* A run that steps clean over the whole interval must not leave the tone stuck
 * on: the expiry is a function of the instant, not an event that has to be
 * landed on. This is the property every advance in this core keeps, and the one
 * a coarse caller would otherwise break. */
static void test_an_advance_past_the_whole_tone_still_ends_it(void) {
  ap_kbd_t k;
  ap_kbd_reset(&k);
  beep(&k, 0x81u);
  unsigned key = 0u;
  (void)ap_kbd_advance(&k, AP_KBD_BEEPER_DURATION * 100u, &key);
  TEST_ASSERT_FALSE(ap_kbd_beeper_on(&k));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_key_sends_its_index_down_and_bit_seven_up);
  RUN_TEST(test_every_release_is_its_press_plus_the_flag);
  RUN_TEST(test_a_non_transition_sends_nothing);
  RUN_TEST(test_a_key_outside_the_matrix_is_refused);
  RUN_TEST(test_keys_that_look_like_modifier_variants_are_independent);
  RUN_TEST(test_table_12_1_gives_the_codes_the_prom_translates);
  RUN_TEST(test_an_ordinary_key_sends_the_character_itself);
  RUN_TEST(test_5b_and_7b_are_one_key_shifted_and_unshifted);
  RUN_TEST(test_the_keypad_prefixes_its_codes);
  RUN_TEST(test_the_state_keys_send_nothing);
  RUN_TEST(test_most_keys_have_no_up_transition_code);
  RUN_TEST(test_encoding_a_character_picks_the_key_that_produces_it);
  RUN_TEST(test_the_repeat_figures_are_the_manual_s_and_are_exact);
  RUN_TEST(test_the_first_repeat_waits_the_delay_and_the_rest_the_period);
  RUN_TEST(test_a_coarse_advance_does_not_lose_repeats);
  RUN_TEST(test_releasing_the_held_key_stops_the_repeat);
  RUN_TEST(test_only_the_keys_the_table_marks_auto_repeat);
  RUN_TEST(test_the_keyboard_powers_up_in_loopback);
  RUN_TEST(test_a_zero_leaves_loopback_and_announces_the_mode);
  RUN_TEST(test_a_zero_outside_loopback_announces_nothing);
  RUN_TEST(test_ff_restarts_the_conversation);
  RUN_TEST(test_the_identification_needs_the_whole_prefix);
  RUN_TEST(test_the_code_set_is_commanded);
  RUN_TEST(test_the_beeper_is_acknowledged_without_being_modelled);
  RUN_TEST(test_the_beeper_goes_off_by_itself_after_300_ms);
  RUN_TEST(test_the_off_sequence_stops_the_tone_early);
  RUN_TEST(test_an_advance_past_the_whole_tone_still_ends_it);
  return UNITY_END();
}
