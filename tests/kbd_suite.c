/* The Apollo keyboard: a serial device that reports key transitions.
 *
 * The tests are about what it sends and, more importantly, what it refuses to
 * send. A keyboard that emits codes the hardware never emits is not caught by
 * anything downstream — the boot PROM's translation table would happily match
 * them.
 */

#include "device/ap_kbd.h"

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
  return UNITY_END();
}
