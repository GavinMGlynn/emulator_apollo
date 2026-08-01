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

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_key_sends_its_index_down_and_bit_seven_up);
  RUN_TEST(test_every_release_is_its_press_plus_the_flag);
  RUN_TEST(test_a_non_transition_sends_nothing);
  RUN_TEST(test_a_key_outside_the_matrix_is_refused);
  RUN_TEST(test_keys_that_look_like_modifier_variants_are_independent);
  return UNITY_END();
}
