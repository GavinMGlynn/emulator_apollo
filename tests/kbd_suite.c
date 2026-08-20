/* The Apollo keyboard: a serial device that reports key transitions.
 *
 * The tests are about what it sends and, more importantly, what it refuses to
 * send. A keyboard that emits codes the hardware never emits is not caught by
 * anything downstream — the boot PROM's translation table would happily match
 * them.
 */

#include "device/ap_kbd.h"

#include <stdio.h>
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

/* §13.3.1, Figure 13-4: escape `DF`, then B1's fixed bit and three switch
 * bits, then signed X and Y. */
static void test_a_mouse_packet_is_the_escape_and_three_bytes(void) {
  ap_kbd_t kbd;
  ap_kbd_reset(&kbd);

  uint8_t packet[AP_KBD_MOUSE_PACKET];
  TEST_ASSERT_EQUAL_UINT(AP_KBD_MOUSE_PACKET,
                         ap_kbd_mouse_packet(&kbd, 5, -3, false, false, false,
                                             packet));
  TEST_ASSERT_EQUAL_HEX8(0xDFu, packet[0]);
  /* No button depressed, so all three switch bits are *set*, with bit 7 fixed
   * and both invalid fields clear. */
  TEST_ASSERT_EQUAL_HEX8(0xF0u, packet[1]);
  TEST_ASSERT_EQUAL_HEX8(0x05u, packet[2]);
  TEST_ASSERT_EQUAL_HEX8(0xFDu, packet[3]); /* -3 in two's complement */
}

/* "L, M, R = Left, Middle, Right Switch Data; 0 = switch depressed" -- the
 * inversion is the trap in this packet, so it gets its own test. */
static void test_a_depressed_button_clears_its_bit(void) {
  ap_kbd_t kbd;
  ap_kbd_reset(&kbd);

  uint8_t packet[AP_KBD_MOUSE_PACKET];
  TEST_ASSERT_EQUAL_UINT(AP_KBD_MOUSE_PACKET,
                         ap_kbd_mouse_packet(&kbd, 0, 0, true, false, false,
                                             packet));
  TEST_ASSERT_EQUAL_HEX8(0u, packet[1] & AP_KBD_MOUSE_B1_LEFT);
  TEST_ASSERT_TRUE((packet[1] & AP_KBD_MOUSE_B1_MIDDLE) != 0u);
  TEST_ASSERT_TRUE((packet[1] & AP_KBD_MOUSE_B1_RIGHT) != 0u);

  TEST_ASSERT_EQUAL_UINT(AP_KBD_MOUSE_PACKET,
                         ap_kbd_mouse_packet(&kbd, 0, 0, true, true, true,
                                             packet));
  TEST_ASSERT_EQUAL_HEX8(AP_KBD_MOUSE_B1_FIXED, packet[1]);
}

/* "X and Y relative counts can range from +127 to -128": one signed byte, so a
 * larger movement clamps. Wrapping would turn a fast drag right into a jump
 * left. */
static void test_a_movement_past_a_signed_byte_clamps(void) {
  ap_kbd_t kbd;
  ap_kbd_reset(&kbd);

  uint8_t packet[AP_KBD_MOUSE_PACKET];
  TEST_ASSERT_EQUAL_UINT(AP_KBD_MOUSE_PACKET,
                         ap_kbd_mouse_packet(&kbd, 5000, -5000, false, false,
                                             false, packet));
  TEST_ASSERT_EQUAL_HEX8(0x7Fu, packet[2]);
  TEST_ASSERT_EQUAL_HEX8(0x80u, packet[3]);
}

/* §13.3.2, Mode 2: "In this mode, **all transmissions are relative cursor
 * coordinate information packets**", so there is nothing to escape and the
 * escape byte is absent -- Mode 0's escape exists to separate pointing data
 * from key codes, and Mode 2 carries no key codes.
 *
 * This test used to assert that keystate mode emitted **nothing**, which was
 * the honest report of an unimplemented mode rather than the hardware's
 * behaviour. §13.3.2 is what closed it. */
static void test_keystate_mode_emits_a_mode_two_packet_with_no_escape(void) {
  ap_kbd_t kbd;
  ap_kbd_reset(&kbd);
  kbd.keystate_mode = true;

  uint8_t packet[AP_KBD_MOUSE_PACKET];
  TEST_ASSERT_EQUAL_UINT(AP_KBD_MOUSE_PACKET_MODE2,
                         ap_kbd_mouse_packet(&kbd, 1, 1, false, false, false,
                                             packet));
  /* The first byte is B1, not the escape. */
  TEST_ASSERT_NOT_EQUAL_UINT8(AP_KBD_MOUSE_ESCAPE_RELATIVE, packet[0]);
  TEST_ASSERT_EQUAL_HEX8(AP_KBD_MOUSE_B1_FIXED, packet[0] & AP_KBD_MOUSE_B1_FIXED);
  TEST_ASSERT_EQUAL_UINT8(1u, packet[1]);
  TEST_ASSERT_EQUAL_UINT8(1u, packet[2]);
}

/* Figure 13-6's bytes are Figure 13-4's B1/B2/B3 unchanged, which is the claim
 * that lets one builder serve both modes. Asserted directly by running the same
 * movement through both and comparing the tails -- if a later change gave Mode
 * 2 its own button polarity or its own sign convention, this is what would
 * catch it. */
static void test_mode_two_is_mode_zero_without_its_first_byte(void) {
  ap_kbd_t mode0;
  ap_kbd_t mode2;
  ap_kbd_reset(&mode0);
  ap_kbd_reset(&mode2);
  mode2.keystate_mode = true;

  uint8_t a[AP_KBD_MOUSE_PACKET];
  uint8_t b[AP_KBD_MOUSE_PACKET];
  TEST_ASSERT_EQUAL_UINT(AP_KBD_MOUSE_PACKET,
                         ap_kbd_mouse_packet(&mode0, -5, 7, true, false, true, a));
  TEST_ASSERT_EQUAL_UINT(AP_KBD_MOUSE_PACKET_MODE2,
                         ap_kbd_mouse_packet(&mode2, -5, 7, true, false, true, b));
  TEST_ASSERT_EQUAL_HEX8(AP_KBD_MOUSE_ESCAPE_RELATIVE, a[0]);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(&a[1], b, AP_KBD_MOUSE_PACKET_MODE2);
}

/* ---- The CAPS LOCK lamp, `008778-03` §12.2 ------------------------------- */

/* Chapter 12's opening sentence has this part "controls and reports the status
 * of the CAPS LOCK LED", and nothing here held one until that chapter was
 * walked. §12.2 gives the transitions: the lamp "comes on during down
 * transitions of the key when it was previously off, and goes off during up
 * transitions when it was previously on". */
static void test_the_caps_lock_lamp_follows_the_keys_two_transitions(void) {
  ap_kbd_t k;
  ap_kbd_reset(&k);
  uint8_t code = 0u;

  /* Dark out of reset: a lamp is off until something lights it. */
  TEST_ASSERT_FALSE(ap_kbd_caps_lock_led(&k));

  TEST_ASSERT_TRUE(ap_kbd_press(&k, AP_KBD_CAPS_LOCK_LED_ON, &code));
  TEST_ASSERT_TRUE(ap_kbd_caps_lock_led(&k));

  /* **It stays lit across the whole latched interval.** This is the reading the
   * header argues for: an alternate-action keyswitch holds down between the
   * press that latches it and the press that frees it, so the lamp is on for
   * everything the operator types in between. A momentary reading would make
   * CAPS LOCK a shift you have to hold, and the key is not labelled that. */
  uint8_t other = 0u;
  TEST_ASSERT_TRUE(ap_kbd_press(&k, 0x2Du, &other)); /* Q */
  TEST_ASSERT_TRUE(ap_kbd_release(&k, 0x2Du, &other));
  TEST_ASSERT_TRUE(ap_kbd_caps_lock_led(&k));

  TEST_ASSERT_TRUE(ap_kbd_release(&k, AP_KBD_CAPS_LOCK_LED_ON, &code));
  TEST_ASSERT_FALSE(ap_kbd_caps_lock_led(&k));
}

/* §12.2: "When the LED comes on, a 7E (hexadecimal) is transmitted; when the
 * LED goes off, an FE (hexadecimal) is transmitted."
 *
 * **Nothing was added to produce those.** Table 12-2's up code is the down code
 * with bit 7 set for every row in it, and CAPS LOCK sits at key number 7E, so
 * the pair falls out of the encoding this model already had. Asserted because
 * the note reads like a special case and is not one -- a later reader who
 * "fixes" it into a table entry would be adding a code the part already
 * sends. */
static void test_the_caps_lock_codes_fall_out_of_the_ordinary_encoding(void) {
  ap_kbd_t k;
  ap_kbd_reset(&k);
  uint8_t code = 0u;

  TEST_ASSERT_TRUE(ap_kbd_press(&k, AP_KBD_CAPS_LOCK_LED_ON, &code));
  TEST_ASSERT_EQUAL_HEX8(0x7Eu, code);
  TEST_ASSERT_TRUE(ap_kbd_release(&k, AP_KBD_CAPS_LOCK_LED_ON, &code));
  TEST_ASSERT_EQUAL_HEX8(0xFEu, code);

  /* The rule itself, since that is what makes the pair not a special case. */
  TEST_ASSERT_EQUAL_HEX8(AP_KBD_CAPS_LOCK_LED_OFF,
                         AP_KBD_CAPS_LOCK_LED_ON | AP_KBD_RELEASE);
}

/* The switch guards the lamp, not the lamp's own code: a repeated press with no
 * release is refused before it reaches the transition, so a host that stutters
 * cannot drive the lamp out of step with the switch. */
static void test_a_repeated_press_cannot_desynchronise_the_lamp(void) {
  ap_kbd_t k;
  ap_kbd_reset(&k);
  uint8_t code = 0u;

  TEST_ASSERT_TRUE(ap_kbd_press(&k, AP_KBD_CAPS_LOCK_LED_ON, &code));
  TEST_ASSERT_FALSE(ap_kbd_press(&k, AP_KBD_CAPS_LOCK_LED_ON, &code));
  TEST_ASSERT_TRUE(ap_kbd_caps_lock_led(&k));
  TEST_ASSERT_TRUE(ap_kbd_release(&k, AP_KBD_CAPS_LOCK_LED_ON, &code));
  TEST_ASSERT_FALSE(ap_kbd_release(&k, AP_KBD_CAPS_LOCK_LED_ON, &code));
  TEST_ASSERT_FALSE(ap_kbd_caps_lock_led(&k));
}

/* ## The two escapes, and why the unused one needs a test more than the used one
 *
 * `AP_KBD_MOUSE_ESCAPE_RELATIVE` is exercised by every packet this core builds,
 * so a wrong value fails somewhere immediately. `AP_KBD_MOUSE_ESCAPE_ABSOLUTE`
 * is referenced by **nothing** -- no builder, no board entry, no other test --
 * because Mode 3 and the Mode 0 absolute form are both unimplemented. A typo in
 * it would sit there until someone implemented the feature and debugged the
 * wrong byte.
 *
 * That is the audit question this project keeps returning to: not "is this
 * modelled" but "would anything notice if it were wrong". Here nothing would,
 * which is the whole reason to pin it.
 *
 * `DF` is `008778-03` Figure 13-4's escape for relative data. `E8` has two
 * documents: §13.2's absolute escape, and `002398-04` p. 6-20's touchpad, whose
 * four-byte packet opens "escape code E8" and whose remaining three bytes are
 * Figure 13-7's coordinate bytes unchanged. */
static void test_both_pointing_device_escapes_match_their_documents(void) {
  TEST_ASSERT_EQUAL_HEX8(0xDFu, AP_KBD_MOUSE_ESCAPE_RELATIVE);
  TEST_ASSERT_EQUAL_HEX8(0xE8u, AP_KBD_MOUSE_ESCAPE_ABSOLUTE);

  /* They must differ: the escape is what tells a host which kind of packet
   * follows, and two modes sharing one would be undecodable. */
  TEST_ASSERT_NOT_EQUAL_UINT8(AP_KBD_MOUSE_ESCAPE_RELATIVE,
                              AP_KBD_MOUSE_ESCAPE_ABSOLUTE);

  /* And neither may collide with a key code. `AP_KBD_KEYS` is 0x80, so a key
   * and its release occupy 00-FF via `AP_KBD_RELEASE` -- but the escapes sit
   * among the *release* codes, which is exactly why Mode 0 needs them: §13.2
   * has the escape distinguish pointing data from key codes on one line, and
   * that only works because the firmware knows a packet follows. Pinned as a
   * property of the encoding rather than asserted away. */
  TEST_ASSERT_TRUE(AP_KBD_MOUSE_ESCAPE_RELATIVE >= AP_KBD_RELEASE);
  TEST_ASSERT_TRUE(AP_KBD_MOUSE_ESCAPE_ABSOLUTE >= AP_KBD_RELEASE);
}

/* ## The keystate chart, spot-checked at its corners and its traps
 *
 * `002398-04` p. 6-14 is a 16x16 grid, column the high nibble and row the low,
 * so the cell at column `6` row `E` is code `6E`. Testing all 106 named codes
 * would restate the table; these are the cells where a transcription goes wrong.
 */
static void test_the_keystate_chart_names_its_codes(void) {
  /* The four corners of the down half. `00` is blank in the document -- the
   * grid's first cell is empty -- which a reader filling in from position would
   * get wrong. */
  TEST_ASSERT_NULL(ap_kbd_key_name(0x00u));
  TEST_ASSERT_EQUAL_STRING("LA0", ap_kbd_key_name(0x01u));
  TEST_ASSERT_EQUAL_STRING("RA2", ap_kbd_key_name(0x10u));
  TEST_ASSERT_EQUAL_STRING("RA1", ap_kbd_key_name(0x0Fu));

  /* Column-major, not row-major: `10` is the *second column's* first row. A
   * transposed transcription would answer `LA0` here. */
  TEST_ASSERT_EQUAL_STRING("B10", ap_kbd_key_name(0x20u));

  /* The space bar, and the one cell that is not a key at all. */
  TEST_ASSERT_EQUAL_STRING("F1", ap_kbd_key_name(0x76u));
  TEST_ASSERT_EQUAL_STRING("LED ON", ap_kbd_key_name(0x7Eu));

  /* A release answers the same name: the chart's right half is the left half
   * with bit 7 set, so one table serves both. */
  TEST_ASSERT_EQUAL_STRING("F1", ap_kbd_key_name(0x76u | AP_KBD_RELEASE));
  TEST_ASSERT_EQUAL_STRING("LA0", ap_kbd_key_name(0x81u));

  /* Blanks stay blank. A code the keyboard cannot send must not acquire a name,
   * which is the difference between a transcription and a guess. */
  TEST_ASSERT_NULL(ap_kbd_key_name(0x16u));
  TEST_ASSERT_NULL(ap_kbd_key_name(0x7Fu));

  /* And the count: 106 of 128 codes are keys, 22 are blank. A cell dropped or
   * invented in transcription moves this. */
  unsigned named = 0u;
  for (unsigned c = 0; c < AP_KBD_KEYS; c++) {
    if (ap_kbd_key_name((uint8_t)c) != nullptr) {
      named++;
    }
  }
  TEST_ASSERT_EQUAL_UINT(106u, named);
}


/* ---- `002398-04` p. 6-13, the ASCII chart -------------------------------- */

/* The page's 256 cells, indexed by the byte the keyboard emits, exactly as
 * printed -- including its `AB` at `C7`, which is a typo, and its `^D14` and
 * `^D13`, which disagree with Table 12-1. Read from the page image at the
 * scan's native 600 ppi. `nullptr` is a blank cell. */
static const char *const P613[256] = {
      nullptr,     "^D2",     "^E6",     "^E4",  /* 00 */
        "^D4",     "^C4",     "^D5",     "^D6",  /* 04 */
        "^D7",     "^C9",     "^D8",     "^D9",  /* 08 */
       "^D10",     "^E8",     "^E7",    "^C10",  /* 0C */
       "^C11",     "^C2",     "^C5",     "^D3",  /* 10 */
        "^C6",     "^C8",     "^E5",     "^C3",  /* 14 */
        "^E3",     "^C7",     "^E2",      "B1",  /* 18 */
         "A0",    "^C13",    "^B14",      "A9",  /* 1C */
         "F1",     "+B2",   nullptr,     "+B4",  /* 20 */
        "+B5",     "+B6",     "+B8",     "D12",  /* 24 */
       "+B10",    "+B11",     "+B9",    "+B13",  /* 28 */
         "E9",     "B12",     "E10",     "+A9",  /* 2C */
        "B11",      "B2",      "B3",      "B4",  /* 30 */
         "B5",      "B6",      "B7",      "B8",  /* 34 */
         "B9",     "B10",   nullptr,     "D11",  /* 38 */
        "+E9",     "B13",    "+E10",     "^A9",  /* 3C */
        "+B3",     "+D2",     "+E6",     "+E4",  /* 40 */
        "+D4",     "+C4",     "+D5",     "+D6",  /* 44 */
        "+D7",     "+C9",     "+D8",     "+D9",  /* 48 */
       "+D10",     "+E8",     "+E7",    "+C10",  /* 4C */
       "+C11",     "+C2",     "+C5",     "+D3",  /* 50 */
        "+C6",     "+C8",     "+E5",     "+C3",  /* 54 */
        "+E3",     "+C7",     "+E2",    "+C12",  /* 58 */
        "+A0",    "+C13",     "+B7",    "+B12",  /* 5C */
        "B14",      "D2",      "E6",      "E4",  /* 60 */
         "D4",      "C4",      "D5",      "D6",  /* 64 */
         "D7",      "C9",      "D8",      "D9",  /* 68 */
        "D10",      "E8",      "E7",     "C10",  /* 6C */
        "C11",      "C2",      "C5",      "D3",  /* 70 */
         "C6",      "C8",      "E5",      "C3",  /* 74 */
         "E3",      "C7",      "E2",     "C12",  /* 78 */
        "^A0",     "C13",    "+B14",     "C14",  /* 7C */
        "E13",     "LA0",     "LA1",     "LA2",  /* 80 */
        "LC0",     "LC1",     "LC2",     "LD0",  /* 84 */
        "LD1",     "LD2",     "LE0",     "LE1",  /* 88 */
        "LE2",     "LF0",     "LF1",     "LF2",  /* 8C */
       "+E13",    "+LA0",    "+LA1",    "+LA2",  /* 90 */
       "+LC0",    "+LC1",    "+LC2",    "+LD0",  /* 94 */
       "+LD1",    "+LD2",    "+LE0",    "+LE1",  /* 98 */
       "+LE2",    "+LF0",    "+LF1",    "+LF2",  /* 9C */
       ":E13",    ":LA0",    ":LA1",    ":LA2",  /* A0 */
       ":LC0",    ":LC1",    ":LC2",    ":LD0",  /* A4 */
       ":LD1",    ":LD2",    ":LE0",    ":LE1",  /* A8 */
       ":LE2",    ":LF0",    ":LF1",    ":LF2",  /* AC */
        "LB0",     "LB1",     "LB2",     "RA4",  /* B0 */
       "+LB0",    "+LB1",    "+LB2",    "+RA4",  /* B4 */
       ":LB0",    ":LB1",    ":LB2",    ":RA4",  /* B8 */
        ":A0",     ":A9",   nullptr,   nullptr,  /* BC */
         "A1",      "A2",      "A3",      "A4",  /* C0 */
         "A5",      "A6",      "A7",      "AB",  /* C4 */
        "D14",   nullptr,      "C1",     "D13",  /* C8 */
        "E11",     "RA0",     "RA1",     "RA2",  /* CC */
        "+A1",     "+A2",     "+A3",     "+A4",  /* D0 */
        "+A5",     "+A6",     "+A7",     "+A8",  /* D4 */
      nullptr,   nullptr,     "+C1",   nullptr,  /* D8 */
       "+E11",   nullptr,     "B15",   nullptr,  /* DC */
        ":A1",     ":A2",     ":A3",     ":A4",  /* E0 */
        ":A5",     ":A6",     ":A7",     ":A8",  /* E4 */
      nullptr,    "+RA0",    "+RA1",    "+RA2",  /* E8 */
       "+RA3",    ":RA0",    ":RA1",    ":RA2",  /* EC */
        "^A1",     "^A2",     "^A3",     "^A4",  /* F0 */
        "^A5",     "^A6",     "^A7",     "^A8",  /* F4 */
       "^D14",   nullptr,     "^C1",    "^D13",  /* F8 */
       "^E11",    ":RA3",   nullptr,   nullptr,  /* FC */
};

/* The chart's own legend: "+ = Shift  ^ = Control  : = Up transition". */
static ap_kbd_mod_t p613_modifier(const char *cell) {
  switch (cell[0]) {
    case '+': return AP_KBD_MOD_SHIFT;
    case '^': return AP_KBD_MOD_CONTROL;
    case ':': return AP_KBD_MOD_UP_TRANS;
    default: return AP_KBD_MOD_NONE;
  }
}

static const char *p613_key(const char *cell) {
  return strchr("+^:", cell[0]) != nullptr ? cell + 1 : cell;
}

/* The whole page, cell by cell, against the map computed from Table 12-1.
 * Every named cell agrees except the three the header names, and those three
 * are asserted to differ in exactly the documented way -- so neither a silent
 * agreement nor a silent divergence can survive. */
static void test_the_ascii_chart_agrees_with_table_12_1(void) {
  unsigned named = 0, agreed = 0;
  for (unsigned code = 0; code < 256; code++) {
    const char *cell = P613[code];
    if (cell == nullptr) {
      continue;
    }
    named++;
    const ap_kbd_ascii_t *key = nullptr;
    ap_kbd_mod_t mod = AP_KBD_MOD_NONE;
    TEST_ASSERT_TRUE(ap_kbd_ascii_decode((uint16_t)code, &key, &mod));
    if (code == 0xC7u || code == 0xF8u || code == 0xFBu) {
      continue; /* the three findings, asserted individually below */
    }
    TEST_ASSERT_EQUAL_STRING(p613_key(cell), key->key);
    TEST_ASSERT_EQUAL_UINT(p613_modifier(cell), mod);
    agreed++;
  }
  TEST_ASSERT_EQUAL_UINT(241u, named);
  TEST_ASSERT_EQUAL_UINT(238u, agreed);
}

/* `C7` prints `AB`, and no key `AB` exists: p. 6-12's map runs `A0`-`A9`. It is
 * `A8`, which is also the only value that closes the `A1`-`A8` block at
 * `C0`-`C7`. */
static void test_the_chart_s_ab_is_a8_misprinted(void) {
  TEST_ASSERT_EQUAL_STRING("AB", P613[0xC7]);
  TEST_ASSERT_NULL(ap_kbd_ascii_find("AB"));

  const ap_kbd_ascii_t *key = nullptr;
  ap_kbd_mod_t mod = AP_KBD_MOD_SHIFT;
  TEST_ASSERT_TRUE(ap_kbd_ascii_decode(0xC7u, &key, &mod));
  TEST_ASSERT_EQUAL_STRING("A8", key->key);
  TEST_ASSERT_EQUAL_UINT(AP_KBD_MOD_NONE, mod);

  /* The block it completes. */
  for (unsigned i = 0; i < 8; i++) {
    char name[4];
    (void)snprintf(name, sizeof name, "A%u", i + 1);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(0xC0u + i),
                             ap_kbd_ascii_find(name)->unshifted);
  }
}

/* The one place the two manuals name different keys for the same byte. The
 * decode follows Table 12-1; the chart's reading is recorded beside it so the
 * disagreement is a fact under test rather than a sentence in a comment. */
static void test_two_control_codes_are_claimed_by_two_different_keys(void) {
  TEST_ASSERT_EQUAL_STRING("^D14", P613[0xF8]);
  TEST_ASSERT_EQUAL_STRING("^D13", P613[0xFB]);

  const ap_kbd_ascii_t *key = nullptr;
  ap_kbd_mod_t mod = AP_KBD_MOD_NONE;
  TEST_ASSERT_TRUE(ap_kbd_ascii_decode(0xF8u, &key, &mod));
  TEST_ASSERT_EQUAL_STRING("D12", key->key);
  TEST_ASSERT_EQUAL_UINT(AP_KBD_MOD_CONTROL, mod);
  TEST_ASSERT_TRUE(ap_kbd_ascii_decode(0xFBu, &key, &mod));
  TEST_ASSERT_EQUAL_STRING("D11", key->key);
  TEST_ASSERT_EQUAL_UINT(AP_KBD_MOD_CONTROL, mod);

  /* Neither key the chart names has a control code of its own in Table 12-1,
   * which is what leaves the byte free for the other reading. */
  TEST_ASSERT_EQUAL_UINT16(AP_KBD_NO_CODE, ap_kbd_ascii_find("D13")->control);
  TEST_ASSERT_EQUAL_UINT16(AP_KBD_NO_CODE, ap_kbd_ascii_find("D14")->control);
}

/* Five bytes Table 12-1 defines that the page leaves blank -- four of them the
 * shifted forms of `D11`-`D14`, the fifth `RA3` unshifted. Every defect on the
 * page is in that one region. */
static void test_the_chart_omits_five_codes_the_sibling_manual_has(void) {
  static const struct {
    uint16_t code;
    const char *key;
    ap_kbd_mod_t mod;
  } OMITTED[] = {
      {0x22u, "D12", AP_KBD_MOD_SHIFT}, /* the double quote */
      {0x3Au, "D11", AP_KBD_MOD_SHIFT}, /* the colon */
      {0xC9u, "D14", AP_KBD_MOD_SHIFT}, {0xDBu, "D13", AP_KBD_MOD_SHIFT},
      {0xDDu, "RA3", AP_KBD_MOD_NONE},
  };
  for (unsigned i = 0; i < sizeof OMITTED / sizeof OMITTED[0]; i++) {
    TEST_ASSERT_NULL(P613[OMITTED[i].code]);
    const ap_kbd_ascii_t *key = nullptr;
    ap_kbd_mod_t mod = AP_KBD_MOD_NONE;
    TEST_ASSERT_TRUE(ap_kbd_ascii_decode(OMITTED[i].code, &key, &mod));
    TEST_ASSERT_EQUAL_STRING(OMITTED[i].key, key->key);
    TEST_ASSERT_EQUAL_UINT(OMITTED[i].mod, mod);
  }
}

/* The round trip the chart is for: a byte to key-plus-modifier and back to the
 * same byte, over every cell the page names. */
static void test_a_byte_decodes_to_a_key_that_sends_it_back(void) {
  unsigned round = 0;
  for (unsigned code = 0; code < 256; code++) {
    if (P613[code] == nullptr) {
      continue;
    }
    const ap_kbd_ascii_t *key = nullptr;
    ap_kbd_mod_t mod = AP_KBD_MOD_NONE;
    TEST_ASSERT_TRUE(ap_kbd_ascii_decode((uint16_t)code, &key, &mod));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)code, ap_kbd_ascii_code(key, mod));
    round++;
  }
  TEST_ASSERT_EQUAL_UINT(241u, round);
}

/* A byte no key sends is refused rather than answered with the first row, and
 * the keypad's two-byte codes decode by their full value. */
static void test_an_unreachable_byte_decodes_to_nothing(void) {
  const ap_kbd_ascii_t *key = nullptr;
  ap_kbd_mod_t mod = AP_KBD_MOD_NONE;
  TEST_ASSERT_FALSE(ap_kbd_ascii_decode(0x00u, &key, &mod));
  TEST_ASSERT_FALSE(ap_kbd_ascii_decode(AP_KBD_NO_CODE, &key, &mod));

  TEST_ASSERT_TRUE(ap_kbd_ascii_decode(0xFE37u, &key, &mod));
  TEST_ASSERT_EQUAL_STRING("RC1", key->key);
  TEST_ASSERT_EQUAL_UINT(AP_KBD_MOD_NONE, mod);
}

/* `1B` is `B1` unshifted and `C12` control both. The chart names `B1`, and the
 * decode's ordering is what reproduces that. */
static void test_a_byte_two_keys_send_decodes_to_the_chart_s_choice(void) {
  TEST_ASSERT_EQUAL_UINT16(0x1Bu, ap_kbd_ascii_find("B1")->unshifted);
  TEST_ASSERT_EQUAL_UINT16(0x1Bu, ap_kbd_ascii_find("C12")->control);
  TEST_ASSERT_EQUAL_STRING("B1", P613[0x1B]);

  const ap_kbd_ascii_t *key = nullptr;
  ap_kbd_mod_t mod = AP_KBD_MOD_SHIFT;
  TEST_ASSERT_TRUE(ap_kbd_ascii_decode(0x1Bu, &key, &mod));
  TEST_ASSERT_EQUAL_STRING("B1", key->key);
  TEST_ASSERT_EQUAL_UINT(AP_KBD_MOD_NONE, mod);
}


/* ---- §12.2's transmit buffer and N-key rollover --------------------------- */

/* "The keyboard buffers at least 16 bytes of data. When all 16 positions are
 * used, further processing of data is inhibited until a new position becomes
 * available." Sixteen go in; the seventeenth is refused rather than dropped. */
static void test_the_buffer_takes_sixteen_bytes_and_inhibits_the_seventeenth(
    void) {
  ap_kbd_t kbd;
  ap_kbd_reset(&kbd);
  TEST_ASSERT_EQUAL_UINT(0u, ap_kbd_buffered(&kbd));
  TEST_ASSERT_FALSE(ap_kbd_buffer_full(&kbd));

  for (unsigned i = 0; i < AP_KBD_BUFFER; i++) {
    TEST_ASSERT_TRUE(ap_kbd_buffer(&kbd, (uint8_t)i));
    TEST_ASSERT_EQUAL_UINT(i + 1u, ap_kbd_buffered(&kbd));
  }
  TEST_ASSERT_TRUE(ap_kbd_buffer_full(&kbd));
  TEST_ASSERT_FALSE(ap_kbd_buffer(&kbd, 0xFFu));
  /* Refused, not dropped in place of an earlier byte. */
  TEST_ASSERT_EQUAL_UINT(AP_KBD_BUFFER, ap_kbd_buffered(&kbd));
}

/* One byte per character time and no more: the buffer drains at 1200 baud 8E1,
 * which is what makes it a buffer rather than a formality. */
static void test_the_buffer_drains_at_one_byte_per_character_time(void) {
  ap_kbd_t kbd;
  uint8_t code = 0u;
  ap_kbd_reset(&kbd);
  TEST_ASSERT_TRUE(ap_kbd_buffer(&kbd, 0x11u));
  TEST_ASSERT_TRUE(ap_kbd_buffer(&kbd, 0x22u));

  /* Not instantly: a byte takes a character time to get out, so at the instant
   * it was queued it is still on the wire. */
  TEST_ASSERT_FALSE(ap_kbd_transmit(&kbd, 0u, &code));
  TEST_ASSERT_FALSE(ap_kbd_transmit(&kbd, AP_KBD_TX_CHARACTER - 1u, &code));
  TEST_ASSERT_TRUE(ap_kbd_transmit(&kbd, AP_KBD_TX_CHARACTER, &code));
  TEST_ASSERT_EQUAL_HEX8(0x11u, code);

  /* The second waits out a further character rather than following it. */
  TEST_ASSERT_FALSE(ap_kbd_transmit(&kbd, AP_KBD_TX_CHARACTER * 2u - 1u, &code));
  TEST_ASSERT_EQUAL_UINT(1u, ap_kbd_buffered(&kbd));
  TEST_ASSERT_TRUE(ap_kbd_transmit(&kbd, AP_KBD_TX_CHARACTER * 2u, &code));
  TEST_ASSERT_EQUAL_HEX8(0x22u, code);

  /* And an empty buffer sends nothing however long it is left. */
  TEST_ASSERT_FALSE(
      ap_kbd_transmit(&kbd, AP_KBD_TX_CHARACTER * 100u, &code));
}

/* The order is the order they were struck in, across the ring's wrap -- the
 * failure a shifting array would not have and a mis-indexed ring would. */
static void test_the_buffer_keeps_its_order_across_the_ring_s_wrap(void) {
  ap_kbd_t kbd;
  uint8_t code = 0u;
  ap_time_t now = AP_KBD_TX_CHARACTER;
  ap_kbd_reset(&kbd);

  /* Fill, drain half, refill: the head is now past zero and the tail wraps. */
  for (unsigned i = 0; i < AP_KBD_BUFFER; i++) {
    TEST_ASSERT_TRUE(ap_kbd_buffer(&kbd, (uint8_t)i));
  }
  for (unsigned i = 0; i < AP_KBD_BUFFER / 2u; i++) {
    TEST_ASSERT_TRUE(ap_kbd_transmit(&kbd, now, &code));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)i, code);
    now += AP_KBD_TX_CHARACTER;
  }
  for (unsigned i = 0; i < AP_KBD_BUFFER / 2u; i++) {
    TEST_ASSERT_TRUE(ap_kbd_buffer(&kbd, (uint8_t)(0xA0u + i)));
  }
  TEST_ASSERT_TRUE(ap_kbd_buffer_full(&kbd));

  for (unsigned i = AP_KBD_BUFFER / 2u; i < AP_KBD_BUFFER; i++) {
    TEST_ASSERT_TRUE(ap_kbd_transmit(&kbd, now, &code));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)i, code);
    now += AP_KBD_TX_CHARACTER;
  }
  for (unsigned i = 0; i < AP_KBD_BUFFER / 2u; i++) {
    TEST_ASSERT_TRUE(ap_kbd_transmit(&kbd, now, &code));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(0xA0u + i), code);
    now += AP_KBD_TX_CHARACTER;
  }
  TEST_ASSERT_EQUAL_UINT(0u, ap_kbd_buffered(&kbd));
}

/* The character time is the wire's, and it lands exactly on the time base --
 * asserted rather than trusted, since a rounded constant would make every
 * keyboard timing drift by a little on every byte. */
static void test_the_character_time_is_eleven_bits_at_1200_baud_exactly(void) {
  TEST_ASSERT_EQUAL_UINT(11u, AP_KBD_TX_BITS);
  TEST_ASSERT_EQUAL_UINT(1200u, AP_KBD_TX_BAUD);
  TEST_ASSERT_EQUAL_UINT64((ap_time_t)197472000000u, AP_KBD_TX_CHARACTER);
  /* Exact: the base divides by the baud rate with nothing left over. */
  TEST_ASSERT_EQUAL_UINT64(
      0u, ((ap_time_t)AP_TIME_BASE_HZ * AP_KBD_TX_BITS) % AP_KBD_TX_BAUD);
}

/* "All keys exhibit N-key rollover for a minimum of six simultaneous key
 * depressions." This model has no limit at all, which is permissive rather than
 * wrong -- but the guarantee is asserted rather than assumed. */
static void test_six_keys_are_down_at_once_and_all_six_report(void) {
  ap_kbd_t kbd;
  uint8_t code = 0u;
  ap_kbd_reset(&kbd);

  static const unsigned keys[AP_KBD_ROLLOVER] = {0x10u, 0x11u, 0x12u,
                                                 0x20u, 0x21u, 0x22u};
  for (unsigned i = 0; i < AP_KBD_ROLLOVER; i++) {
    TEST_ASSERT_TRUE(ap_kbd_press(&kbd, keys[i], &code));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)keys[i], code);
  }
  /* All six still held -- a press does not release the one before it. */
  for (unsigned i = 0; i < AP_KBD_ROLLOVER; i++) {
    TEST_ASSERT_FALSE(ap_kbd_press(&kbd, keys[i], &code)); /* already down */
  }
  /* And each releases as itself, with the release bit set. */
  for (unsigned i = 0; i < AP_KBD_ROLLOVER; i++) {
    TEST_ASSERT_TRUE(ap_kbd_release(&kbd, keys[i], &code));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(keys[i] | AP_KBD_RELEASE), code);
  }
}

/* A reset empties the buffer. A keyboard that came up holding bytes from before
 * the reset would send them into a machine that had just started. */
static void test_a_reset_empties_the_transmit_buffer(void) {
  ap_kbd_t kbd;
  uint8_t code = 0u;
  ap_kbd_reset(&kbd);
  for (unsigned i = 0; i < 4u; i++) {
    TEST_ASSERT_TRUE(ap_kbd_buffer(&kbd, (uint8_t)i));
  }
  ap_kbd_reset(&kbd);
  TEST_ASSERT_EQUAL_UINT(0u, ap_kbd_buffered(&kbd));
  TEST_ASSERT_FALSE(ap_kbd_transmit(&kbd, AP_KBD_TX_CHARACTER * 4u, &code));
}


/* ---- §13.3's absolute packets, Figures 13-5 and 13-7 ---------------------- */

/* The three coordinate bytes are **identical** in Figure 13-5 and Figure 13-7,
 * which is why one builder serves both absolute forms -- the same argument that
 * lets one builder serve Modes 0 and 2 for relative. Asserted directly by
 * running one position through both modes and comparing the tails. */
static void test_both_absolute_modes_pack_the_coordinates_the_same_way(void) {
  ap_kbd_t kbd;
  uint8_t mode0[AP_KBD_MOUSE_PACKET];
  uint8_t mode3[AP_KBD_MOUSE_PACKET];
  ap_kbd_reset(&kbd);

  kbd.keystate_mode = false;
  TEST_ASSERT_EQUAL_UINT(
      4u, ap_kbd_mouse_packet_absolute(&kbd, 0x123u, 0x456u, false, false,
                                       false, mode0));
  kbd.keystate_mode = true;
  TEST_ASSERT_EQUAL_UINT(
      4u, ap_kbd_mouse_packet_absolute(&kbd, 0x123u, 0x456u, false, false,
                                       false, mode3));

  /* Only the leading byte differs. */
  TEST_ASSERT_EQUAL_HEX8(AP_KBD_MOUSE_ESCAPE_ABSOLUTE, mode0[0]);
  TEST_ASSERT_EQUAL_HEX8(AP_KBD_MOUSE_B1_FIXED, mode3[0]);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(mode0 + 1, mode3 + 1, 3);
}

/* Figure 13-5/13-7's packing, bit for bit: X's low eight, then Y's low nibble
 * over X's high nibble, then Y's high eight. Worked by hand from the figure so
 * a transposed nibble cannot pass. */
static void test_the_absolute_coordinates_pack_as_the_figure_draws_them(void) {
  ap_kbd_t kbd;
  uint8_t out[AP_KBD_MOUSE_PACKET];
  ap_kbd_reset(&kbd);
  kbd.keystate_mode = false;

  /* X = 0x123, Y = 0x456.
   *   B1 = X[7:0]            = 0x23
   *   B2 = Y[3:0] | X[11:8]  = 0x6 << 4 | 0x1 = 0x61
   *   B3 = Y[11:4]           = 0x45 */
  TEST_ASSERT_EQUAL_UINT(4u, ap_kbd_mouse_packet_absolute(
                                 &kbd, 0x123u, 0x456u, false, false, false,
                                 out));
  TEST_ASSERT_EQUAL_HEX8(0xE8u, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0x23u, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0x61u, out[2]);
  TEST_ASSERT_EQUAL_HEX8(0x45u, out[3]);

  /* The extremes, so a sign or width slip shows. */
  TEST_ASSERT_EQUAL_UINT(
      4u, ap_kbd_mouse_packet_absolute(&kbd, 0u, 0u, false, false, false, out));
  TEST_ASSERT_EQUAL_HEX8(0x00u, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0x00u, out[2]);
  TEST_ASSERT_EQUAL_HEX8(0x00u, out[3]);

  TEST_ASSERT_EQUAL_UINT(4u, ap_kbd_mouse_packet_absolute(
                                 &kbd, AP_KBD_MOUSE_ABSOLUTE_MAX,
                                 AP_KBD_MOUSE_ABSOLUTE_MAX, false, false, false,
                                 out));
  TEST_ASSERT_EQUAL_HEX8(0xFFu, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0xFFu, out[2]);
  TEST_ASSERT_EQUAL_HEX8(0xFFu, out[3]);
}

/* Past the edge of the format, a coordinate stops rather than wrapping. A wrap
 * would put the pointer at the opposite edge, which is the absolute analogue of
 * the relative builder's fast-drag-right-becomes-jump-left. */
static void test_an_absolute_coordinate_clamps_rather_than_wrapping(void) {
  ap_kbd_t kbd;
  uint8_t out[AP_KBD_MOUSE_PACKET];
  ap_kbd_reset(&kbd);
  kbd.keystate_mode = false;

  TEST_ASSERT_EQUAL_UINT(4u, ap_kbd_mouse_packet_absolute(
                                 &kbd, 0x1000u, 0x2000u, false, false, false,
                                 out));
  /* Both clamped to 0xFFF, not truncated to 0x000. */
  TEST_ASSERT_EQUAL_HEX8(0xFFu, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0xFFu, out[2]);
  TEST_ASSERT_EQUAL_HEX8(0xFFu, out[3]);
}

/* Mode 3's B1 is `1 M R L 0 0 0 0`: the buttons in the top nibble and the low
 * nibble fixed zero, the relative packet's invalid indicators having no
 * counterpart when a coordinate is absolute.
 *
 * The polarity here is `PROVISIONAL` -- Figure 13-7 prints no legend, and the
 * two candidate readings are named in the header. This asserts the one the walk
 * recorded, so that flipping it is a deliberate edit and not a drift. */
static void test_mode_3_puts_the_buttons_in_b1_s_top_nibble(void) {
  ap_kbd_t kbd;
  uint8_t out[AP_KBD_MOUSE_PACKET];
  ap_kbd_reset(&kbd);
  kbd.keystate_mode = true;

  /* Nothing pressed. */
  TEST_ASSERT_EQUAL_UINT(4u, ap_kbd_mouse_packet_absolute(
                                 &kbd, 0u, 0u, false, false, false, out));
  TEST_ASSERT_EQUAL_HEX8(AP_KBD_MOUSE_B1_FIXED, out[0]);

  /* Each button alone, at its documented bit. */
  TEST_ASSERT_EQUAL_UINT(4u, ap_kbd_mouse_packet_absolute(
                                 &kbd, 0u, 0u, true, false, false, out));
  TEST_ASSERT_EQUAL_HEX8(AP_KBD_MOUSE_B1_FIXED | AP_KBD_MOUSE_B1_LEFT, out[0]);
  TEST_ASSERT_EQUAL_UINT(4u, ap_kbd_mouse_packet_absolute(
                                 &kbd, 0u, 0u, false, true, false, out));
  TEST_ASSERT_EQUAL_HEX8(AP_KBD_MOUSE_B1_FIXED | AP_KBD_MOUSE_B1_MIDDLE,
                         out[0]);
  TEST_ASSERT_EQUAL_UINT(4u, ap_kbd_mouse_packet_absolute(
                                 &kbd, 0u, 0u, false, false, true, out));
  TEST_ASSERT_EQUAL_HEX8(AP_KBD_MOUSE_B1_FIXED | AP_KBD_MOUSE_B1_RIGHT, out[0]);

  /* The low nibble stays clear whatever the buttons do. */
  TEST_ASSERT_EQUAL_UINT(4u, ap_kbd_mouse_packet_absolute(
                                 &kbd, 0u, 0u, true, true, true, out));
  TEST_ASSERT_EQUAL_HEX8(0x00u, out[0] & 0x0Fu);
}

/* The button polarity is the opposite of the relative packets', which is the
 * whole reason this cannot share the relative builder's button code. Asserted
 * as a contrast so the two cannot quietly converge. */
static void test_the_absolute_buttons_invert_the_relative_convention(void) {
  ap_kbd_t kbd;
  uint8_t rel[AP_KBD_MOUSE_PACKET];
  uint8_t abs_[AP_KBD_MOUSE_PACKET];
  ap_kbd_reset(&kbd);
  kbd.keystate_mode = true;

  /* Left down. Relative clears the bit (zero = depressed); absolute sets it. */
  TEST_ASSERT_EQUAL_UINT(
      3u, ap_kbd_mouse_packet(&kbd, 0, 0, true, false, false, rel));
  TEST_ASSERT_EQUAL_HEX8(0u, rel[0] & AP_KBD_MOUSE_B1_LEFT);
  TEST_ASSERT_EQUAL_UINT(4u, ap_kbd_mouse_packet_absolute(
                                 &kbd, 0u, 0u, true, false, false, abs_));
  TEST_ASSERT_EQUAL_HEX8(AP_KBD_MOUSE_B1_LEFT,
                         abs_[0] & AP_KBD_MOUSE_B1_LEFT);
}

/* Which packet goes out is the *device's* property, not a commanded mode: p.
 * 149 gives the keyboard two modes and says Modes 2 and 3 are packet types. So
 * the flag is settable, survives a mode command, and is cleared by reset. */
static void test_the_pointing_device_type_is_not_a_keyboard_mode(void) {
  ap_kbd_t kbd;
  ap_kbd_reset(&kbd);
  /* The standard configuration is a quadrature mouse: relative only. */
  TEST_ASSERT_FALSE(ap_kbd_pointing_absolute(&kbd));

  ap_kbd_set_pointing_absolute(&kbd, true);
  TEST_ASSERT_TRUE(ap_kbd_pointing_absolute(&kbd));

  /* `FF01` selects Mode 1 and says nothing about the device. */
  uint8_t reply[8];
  kbd.loopback = false;
  (void)ap_kbd_receive(&kbd, 0xFFu, reply, sizeof reply);
  (void)ap_kbd_receive(&kbd, 0x01u, reply, sizeof reply);
  TEST_ASSERT_TRUE(kbd.keystate_mode);
  TEST_ASSERT_TRUE(ap_kbd_pointing_absolute(&kbd));

  ap_kbd_reset(&kbd);
  TEST_ASSERT_FALSE(ap_kbd_pointing_absolute(&kbd));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_both_pointing_device_escapes_match_their_documents);
  RUN_TEST(test_the_keystate_chart_names_its_codes);
  RUN_TEST(test_a_mouse_packet_is_the_escape_and_three_bytes);
  RUN_TEST(test_a_depressed_button_clears_its_bit);
  RUN_TEST(test_a_movement_past_a_signed_byte_clamps);
  RUN_TEST(test_keystate_mode_emits_a_mode_two_packet_with_no_escape);
  RUN_TEST(test_mode_two_is_mode_zero_without_its_first_byte);
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
  RUN_TEST(test_the_caps_lock_lamp_follows_the_keys_two_transitions);
  RUN_TEST(test_the_caps_lock_codes_fall_out_of_the_ordinary_encoding);
  RUN_TEST(test_a_repeated_press_cannot_desynchronise_the_lamp);
  RUN_TEST(test_the_ascii_chart_agrees_with_table_12_1);
  RUN_TEST(test_the_chart_s_ab_is_a8_misprinted);
  RUN_TEST(test_two_control_codes_are_claimed_by_two_different_keys);
  RUN_TEST(test_the_chart_omits_five_codes_the_sibling_manual_has);
  RUN_TEST(test_a_byte_decodes_to_a_key_that_sends_it_back);
  RUN_TEST(test_an_unreachable_byte_decodes_to_nothing);
  RUN_TEST(test_a_byte_two_keys_send_decodes_to_the_chart_s_choice);
  RUN_TEST(test_the_buffer_takes_sixteen_bytes_and_inhibits_the_seventeenth);
  RUN_TEST(test_the_buffer_drains_at_one_byte_per_character_time);
  RUN_TEST(test_the_buffer_keeps_its_order_across_the_ring_s_wrap);
  RUN_TEST(test_the_character_time_is_eleven_bits_at_1200_baud_exactly);
  RUN_TEST(test_six_keys_are_down_at_once_and_all_six_report);
  RUN_TEST(test_a_reset_empties_the_transmit_buffer);
  RUN_TEST(test_both_absolute_modes_pack_the_coordinates_the_same_way);
  RUN_TEST(test_the_absolute_coordinates_pack_as_the_figure_draws_them);
  RUN_TEST(test_an_absolute_coordinate_clamps_rather_than_wrapping);
  RUN_TEST(test_mode_3_puts_the_buttons_in_b1_s_top_nibble);
  RUN_TEST(test_the_absolute_buttons_invert_the_relative_convention);
  RUN_TEST(test_the_pointing_device_type_is_not_a_keyboard_mode);
  return UNITY_END();
}
