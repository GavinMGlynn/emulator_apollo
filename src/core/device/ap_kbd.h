/* The Apollo keyboard, as the DN3500 sees it: a serial device on serial 1
 * channel A that reports key transitions.
 *
 * ## What it sends
 *
 * A scan code per transition. Key down sends the key's index; key up sends the
 * index with bit 7 set. The index is the key's position in a 128-key matrix,
 * `port x 32 + bit` across the four scanned ports -- so `Numpad 1` is `4B` down
 * and `CB` up.
 *
 * ## What it deliberately does not send
 *
 * Modifiers folded into the code. `4B`, `5B` and `7B` differ only in bits 4 and
 * 5, which looks exactly like shift and control encoded into a base key -- and
 * is not: those are `Numpad 1`, `F10` and a third unrelated key that happen to
 * sit at those matrix positions (`FINDINGS.md` C46). A keyboard that
 * synthesised modifier codes would send bytes the hardware never sends, the
 * boot PROM's translation table would *match* them, and the error would surface
 * much later as wrong characters from function keys.
 *
 * Shift and control are keys. They transmit their own transitions and the
 * firmware tracks them.
 *
 * ## Why it holds no state but the key map
 *
 * The real part scans a matrix on a timer and repeats held keys. Neither is
 * modelled here: nothing in this core advances time yet, so a repeat interval
 * would be a number with no clock behind it. What is modelled is the transition
 * -- which is what a caller can generate honestly, and what the firmware
 * actually reads.
 */

#ifndef APOLLO_DEVICE_AP_KBD_H
#define APOLLO_DEVICE_AP_KBD_H

#include <stdbool.h>
#include <stdint.h>

/* The matrix is 128 keys: four ports of 32 bits, walked `0` to `0x7F`. */
#define AP_KBD_KEYS 0x80u

/* Bit 7 of a scan code marks a release. A key index must therefore be below
 * `0x80`, which is the same bound as the matrix -- not a coincidence, and the
 * reason the matrix stops there. */
#define AP_KBD_RELEASE 0x80u

typedef struct {
  /* Which keys are currently down, so a repeated press or a release of a key
   * that was never pressed sends nothing. The real matrix scan cannot report a
   * transition that did not happen, and a model that let one through would let
   * a caller desynchronise the firmware's own shift state. */
  bool down[AP_KBD_KEYS];
} ap_kbd_t;

void ap_kbd_reset(ap_kbd_t *kbd);

/* Press or release a key. `*code` receives the scan code to transmit, and the
 * call answers false when the key was already in that state -- there is no
 * transition, so there is nothing to send. */
[[nodiscard]] bool ap_kbd_press(ap_kbd_t *kbd, unsigned key, uint8_t *code);
[[nodiscard]] bool ap_kbd_release(ap_kbd_t *kbd, unsigned key, uint8_t *code);

#endif /* APOLLO_DEVICE_AP_KBD_H */
