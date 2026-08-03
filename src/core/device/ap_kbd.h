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

/* ## What the firmware does with these codes
 *
 * The boot PROM translates a subset of scan codes to ASCII through **two
 * parallel twenty-entry tables**, and their addresses are not a reading of the
 * bytes but of the addressing modes that index them: the search at `0021FA`
 * compares with `CMP.B (-$38,PC,D0.W),D1` from an extension word at `00220A`,
 * giving `0021D2`, and answers with `MOVE.B (-$30,PC,D0.W),D1` from `002216`,
 * giving `0021E6`. Twenty bytes apart, so the first table is scan codes and the
 * second their characters at the same index.
 *
 *     CB DB -> 0D    FB -> 1B    C8 D8 F8 -> 5C    C9 D9 -> 7C    F9 -> 7F
 *     5B -> 7B       5D -> 7D    7B -> 5B          7D -> 5D
 *     CA DA FA -> 09 CC -> 2F    DC FC -> 3F       DE -> 08
 *
 * Read as matrix indices, the entries with bit 7 set are *release* codes: `CB`
 * is the release of key `4B`, and it is what the firmware turns into a carriage
 * return. So a scripted press-and-release of index `4B` is how a caller sends
 * `CR` from this keyboard, and the make code is not in the table at all --
 * translation happens on the release.
 *
 * The search is guarded: `BTST #1,($01C7,A6)` returns without translating when
 * that bit is set, so the firmware has a raw mode. Measured at `21` during a
 * boot, which leaves the bit clear and translation running.
 *
 * None of this is behaviour of *this* module -- the part sends codes and knows
 * nothing of ASCII. It is recorded here because it is the only place a caller
 * can find out which index to press to send a given character, and because it
 * was recovered from the firmware rather than from any manual. `FINDINGS.md`
 * C109.
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
