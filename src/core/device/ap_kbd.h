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

/* ## Two code sets, which is the thing this file used to get wrong
 *
 * `008778-03` Chapter 12 -- the machine's own technical reference, on disk the
 * whole time -- opens by saying it outright: "The Domain System keyboard
 * generates scan codes from **one of two sets of character codes** ... One set
 * of character codes uses ASCII-like code definitions (as with earlier Domain
 * keyboards); the other set of codes uses keystate definitions." Which set is
 * live is commanded: "These commands define the mode of operation for the
 * keyboard and determine how data is sent."
 *
 *   - **Keystate**, described above: a transition per key, up marked by bit 7.
 *     "The keystate codes tell the CPU when each key is pressed and released;
 *     they do not make interpretations about the positions of the state keys."
 *   - **ASCII**, Table 12-1: the key's *character* under the modifiers in force
 *     -- `A` sends `61`, shifted `41`, control `01`. Keys with no character send
 *     a code above `7F` instead: RETURN is `CB`, TAB `CA`, BACK SPACE `DE`.
 *     The numeric keypad sends two bytes, `FE` then the character.
 *
 * **This file previously described the second set as release codes of the
 * first.** It read the boot PROM's translation table -- `CB DB -> 0D`,
 * `CA DA FA -> 09`, `DE -> 08` -- and, seeing bit 7 set, concluded that `CB`
 * was "the release of key `4B`, and it is what the firmware turns into a
 * carriage return", so that "translation happens on the release". Table 12-1
 * says `CB` is RETURN's *unshifted ASCII code* and `DB` its shifted one, sent
 * on the press like any other character. Every entry of the recovered table
 * lands exactly on a row: `CA`/`DA`/`FA` are TAB's three, `DE` is BACK SPACE's,
 * `CC`/`DC`/`FC` are the `? /` key's, `C8`/`C9` the `| \` key's. The firmware
 * table is a map from this keyboard's non-character codes to ASCII, and the
 * pairs like `5B -> 7B` next to `7B -> 5B` are it correcting the `{ [` key,
 * which Table 12-1 shows sending `7B` unshifted and `5B` shifted -- the
 * opposite way round from the US convention.
 *
 * `FINDINGS.md` C46's reading falls with it. It saw `4B`, `5B` and `7B`
 * differing only in bits 4 and 5, judged that "looks exactly like shift and
 * control encoded into a base key", and ruled it out as coincidence -- three
 * unrelated keys at neighbouring matrix positions. In the ASCII set they are
 * exactly what they looked like: `5B` and `7B` are one key's shifted and
 * unshifted codes. The instinct was right and the conclusion was inverted, and
 * what settled it was reading a chapter of a manual already in the repository.
 *
 * ## The firmware's own table, which is not this part's behaviour
 *
 * The boot PROM translates twenty of these codes through two parallel
 * twenty-entry tables, at `0021D2` and `0021E6` -- addresses read from the
 * addressing modes that index them rather than from the bytes: the search at
 * `0021FA` compares with `CMP.B (-$38,PC,D0.W),D1` and answers with
 * `MOVE.B (-$30,PC,D0.W),D1` at `002216`. The search is guarded by
 * `BTST #1,($01C7,A6)`, which returns untranslated when set -- a raw mode,
 * measured at `21` during a boot, so the bit is clear and translation runs.
 *
 * That is firmware, not hardware, and it is modelled separately
 * (`ap_kbd_prom_ascii`) so the two cannot be confused again. `FINDINGS.md`
 * C109.
 */

#ifndef APOLLO_DEVICE_AP_KBD_H
#define APOLLO_DEVICE_AP_KBD_H

#include <stdbool.h>
#include <stdint.h>

#include "time/ap_time.h"

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

  /* Auto-repeat. Only the most recently pressed key repeats -- the real part
   * repeats the key being held, and a second key struck while the first is
   * down takes over. `held` is `AP_KBD_KEYS` when nothing is. */
  unsigned held;
  ap_time_t now;
  /* When the next repeat is due: the press plus the initial delay, then the
   * period thereafter. */
  ap_time_t repeat_at;
  bool repeating; /* past the initial delay, so the period applies */
} ap_kbd_t;

void ap_kbd_reset(ap_kbd_t *kbd);

/* Press or release a key. `*code` receives the scan code to transmit, and the
 * call answers false when the key was already in that state -- there is no
 * transition, so there is nothing to send. This is the **keystate** set. */
[[nodiscard]] bool ap_kbd_press(ap_kbd_t *kbd, unsigned key, uint8_t *code);
[[nodiscard]] bool ap_kbd_release(ap_kbd_t *kbd, unsigned key, uint8_t *code);

/* ---- The ASCII set, `008778-03` Table 12-1 -------------------------------- */

/* A dash in the table: the key sends nothing under that modifier. Not zero,
 * which is a code the table uses. */
#define AP_KBD_NO_CODE 0xFFFFu

/* The keypad's two-byte sequences are `FE` then the character, so a code is
 * wider than a byte. A caller transmits the high byte first when it is set. */
#define AP_KBD_PREFIX 0xFE00u

typedef struct {
  const char *key;    /* Table 12-1's key number: "D13", "B15", "RF3" */
  const char *legend; /* its keycap legend */
  uint16_t unshifted;
  uint16_t shifted;
  uint16_t control;
  uint16_t caps_lock;
  /* "Up Trans Code": the code sent on *release*, for the keys that have one.
   * Most do not -- which is the distinction the release-code reading missed. */
  uint16_t up_trans;
  bool auto_repeat;
} ap_kbd_ascii_t;

/* The table, and its length. CTRL, SHIFT, CAPS LOCK and REPEAT are absent by
 * design: Table 12-1 gives them no codes, listing them as "Control Key",
 * "Shift Key" and so on. They are state, and a keyboard that sent a code for
 * them would send bytes the hardware does not. */
[[nodiscard]] unsigned ap_kbd_ascii_count(void);
[[nodiscard]] const ap_kbd_ascii_t *ap_kbd_ascii_at(unsigned index);
[[nodiscard]] const ap_kbd_ascii_t *ap_kbd_ascii_find(const char *key);

/* What the boot PROM makes of a code the keyboard sends, per its own table.
 * `AP_KBD_NO_CODE` when the code is not one of the twenty -- which for an
 * ordinary character means it passes through unchanged, since the ASCII set
 * already sends ASCII. Firmware behaviour, kept apart from the part's. */
[[nodiscard]] uint16_t ap_kbd_prom_ascii(uint8_t code);

/* ---- Auto-repeat, `008778-03` Chapter 12's notes to Table 12-1 ------------ */

/* "Keys that are specified to auto-repeat default to repeat the down
 * transition only at **33 milliseconds** (+/- 3 milliseconds) after an initial
 * delay of **500 milliseconds** (+/- 50 milliseconds). The repeat rate and
 * delay can be reprogrammed via software."
 *
 * Both are documented figures with tolerances, not a range with no value in it,
 * so neither is `PROVISIONAL`: the nominal is stated and the tolerance is the
 * part's spread. Both land exactly on the time base -- 500 ms is
 * 9,900,000,000 units and 33 ms is 653,400,000 -- so nothing is rounded here
 * either.
 *
 * This is the fifth of the tick loop's named debts, and it was left unmodelled
 * because "a repeat interval would be a number with no clock behind it". There
 * is a clock now, and the number was in the manual all along. */
#define AP_KBD_REPEAT_DELAY ((ap_time_t)AP_TIME_BASE_HZ / 2u)
#define AP_KBD_REPEAT_PERIOD ((ap_time_t)AP_TIME_BASE_HZ * 33u / 1000u)

/* In the **keystate** set the repeat is not the key's code again: "The repeat
 * function is handled by the keyboard by transmitting a `7F` (hexadecimal) when
 * any key (except CAPS LOCK) has been pressed for longer than the repeat rate
 * time." A keystate repeat that resent the down code would be indistinguishable
 * from the key being struck again. */
#define AP_KBD_REPEAT_KEYSTATE 0x7Fu

/* Carry the keyboard to `now`, and report a repeat if one is due.
 *
 * `*key` receives the repeating key's index. False when nothing is held, when
 * the held key does not auto-repeat, or when the interval has not elapsed --
 * which is most calls, since the board advances every device on every step. */
[[nodiscard]] bool ap_kbd_advance(ap_kbd_t *kbd, ap_time_t now, unsigned *key);

/* Whether a key auto-repeats, by Table 12-1's last column. Absent from the
 * table -- a state key -- answers false rather than defaulting. */
[[nodiscard]] bool ap_kbd_auto_repeats(const ap_kbd_ascii_t *key);

/* The code to transmit so the firmware sees `ascii`, and whether shift is
 * needed to produce it. False when no key on this keyboard can. This is what a
 * frontend needs to type text at the machine, and it is the reason the two
 * tables live side by side. */
[[nodiscard]] bool ap_kbd_encode(char ascii, uint16_t *code, bool *shifted);

#endif /* APOLLO_DEVICE_AP_KBD_H */
