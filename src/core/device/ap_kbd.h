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

  /* ## The command channel, which is the half this file used not to have
   *
   * The keyboard is not write-only. The host sends it commands and it answers,
   * and a machine with a display console asks it to identify itself before
   * believing there is one.
   *
   * Every command begins `FF`, and the bytes after it accumulate into a message
   * until one matches. That is why the accumulator is wider than a byte: `FF12`
   * is a prefix and `FF1221` is a command, so a model matching one byte at a
   * time cannot tell them apart.
   *
   * **It starts in loopback**, and that is the state a real one powers up in:
   * until told otherwise it echoes what it is sent rather than acting on it,
   * which is how a host discovers a keyboard is there at all. `00` is the one
   * exception, and the oracle excludes it in two places. */
  bool loopback;
  uint32_t rx_message;
  /* Which of the two code sets is live. `008778-03` Chapter 12's two sets, and
   * the commands that select them. */
  bool keystate_mode;

  /* ## The beeper, which stopped being a decline when the handbook turned up
   *
   * `002398-04` p. 12-2, read from the page image:
   *
   *     The beeper is in the DN3000 keyboard and is accessed by writing to
   *     SIO line 0.  Transmit following sequence to turn tone ON:
   *           $FF  $21  $81  $00
   *     It will go off automatically after 300 milliseconds.
   *     Transmit following sequence to turn tone OFF:
   *           $FF  $21  $82  $00
   *
   * So there are two observables and this core can hold both: whether the tone
   * is sounding, and that it stops by itself after 300 ms without the host
   * saying anything. Neither needs audio, and modelling only the
   * acknowledgement -- which is what this file did -- loses the auto-off, the
   * one part a driver could actually be timing against.
   *
   * Zero when silent. The *sound* is still not modelled and is not claimed to
   * be: this core has no audio path, and a caller wanting one reads the level. */
  ap_time_t beeper_until;

  /* ## The CAPS LOCK LED, which the chapter opens by naming
   *
   * `008778-03` chapter 12's first sentence lists what this part does, and one
   * of the four is "**controls and reports the status of the CAPS LOCK LED**".
   * Nothing here held it, so a host could ask and there was nothing to answer
   * with -- the one observable of a lamp is whether it is lit.
   *
   * §12.2's note to Table 12-2 gives the transitions exactly: "The LED comes on
   * during **down** transitions of the key when it was previously off, and goes
   * off during **up** transitions when it was previously on." Read against an
   * ordinary momentary key that would make CAPS LOCK a shift you have to hold;
   * read against an **alternate-action** keyswitch -- one that latches down on
   * one press and releases on the next -- it is the ordinary locking behaviour,
   * and the down and up transitions are a whole press-release-press-release
   * cycle apart in the operator's hand. That is the reading taken, because it
   * is the only one under which a key labelled CAPS *LOCK* locks.
   *
   * **The codes needed nothing added, and they belong to the lamp rather than
   * to the key.** Table 12-2's key rows stop at RF3 ENTER, and the row *after*
   * the last key is headed "**CAPS LOCK LED**" with `7E (ON)` and `FE (OFF)` in
   * the down- and up-transition columns. The key's own row, D1, has no codes at
   * all -- it prints "CAPITOL LETTERS" and "LOCK KEY" across them, the same way
   * Table 12-1 marks CTRL and SHIFT as state rather than characters. So in the
   * keystate set a host never learns that CAPS LOCK was *struck*; it learns the
   * lamp changed, which is the only thing that changed for it.
   *
   * That makes `7E`/`FE` fall out of the encoding this model already had, since
   * Table 12-2's up code is the down code with bit 7 set for every row in it.
   * Driving the lamp through the ordinary transition path is therefore not a
   * shortcut: the transition *is* the lamp's, and the byte on the wire is the
   * same either way. */
  bool caps_lock_led;
} ap_kbd_t;

/* Table 12-2's final row: the CAPS LOCK **LED**'s two transition codes. `7E` is
 * also the transition this model drives the lamp from, which is why one
 * constant serves both -- see the struct field above for why that is the
 * table's own arrangement rather than a conflation. */
#define AP_KBD_CAPS_LOCK_LED_ON 0x7Eu
#define AP_KBD_CAPS_LOCK_LED_OFF 0xFEu

/* Whether the CAPS LOCK lamp is lit. */
[[nodiscard]] bool ap_kbd_caps_lock_led(const ap_kbd_t *kbd);

/* "It will go off automatically after 300 milliseconds." Written as a quotient
 * of the base so a recomputed `AP_TIME_BASE_HZ` keeps the *duration* rather
 * than the number -- `time/ap_time.h`'s rule, and the one six written-down
 * periods broke before it was enforced. */
#define AP_KBD_BEEPER_DURATION ((ap_time_t)AP_TIME_BASE_HZ * 300u / 1000u)

/* The longest reply the keyboard sends, which is the identification string. */
#define AP_KBD_REPLY_MAX 32u

/* Feed the keyboard a byte the host sent it, and collect what it says back.
 *
 * Returns how many bytes of `reply` were written. Zero is a normal answer:
 * several commands are silent, and a byte that is only a prefix of a longer one
 * is answered when the rest of it arrives. */
[[nodiscard]] unsigned ap_kbd_receive(ap_kbd_t *kbd, uint8_t byte,
                                      uint8_t *reply, unsigned capacity);

/* The identification the keyboard answers `FF 12 21` with. Named because it is
 * a *string the part sends*, not a number this core chose. */
#define AP_KBD_IDENTIFICATION "3-@\r2-0\rSD-03863-MS\r"

void ap_kbd_reset(ap_kbd_t *kbd);

/* Press or release a key. `*code` receives the scan code to transmit, and the
 * call answers false when the key was already in that state -- there is no
 * transition, so there is nothing to send. This is the **keystate** set. */
/* ## The pointing device, which arrives on the keyboard's own line
 *
 * `008778-03` **§13.3, "Keyboard-to-CPU Data Packets"** -- and the existence of
 * that chapter corrects a claim this project carried for a long time, that no
 * Apollo keyboard protocol document exists and that "there are no tables" for
 * this part. Chapter 12 stops at layout and character codes; Chapter 13 is the
 * pointing device, and it has the tables. It was on disk throughout.
 *
 * §13.3: "In Mode 0 (compatibility mode), pointing device data is *escaped* by
 * a special code, DF for relative and E8 for absolute data. In Mode 1, no such
 * escape codes exist. Rather, there are distinct modes which are used for
 * pointing device data, Mode 2 for relative and Mode 3 for absolute data."
 *
 * §13.3.1 with Figure 13-4, the relative packet -- escape then three bytes:
 *
 *     BYTE  bit 7   6   5   4   3   2   1   0
 *     ESC       1   1   0   1   1   1   1   1     = DF
 *     B1        1   M   R   L   Y   Y   X   X
 *     B2       X7  X6  X5  X4  X3  X2  X1  X0
 *     B3       Y7  Y6  Y5  Y4  Y3  Y2  Y1  Y0
 *
 * "L, M, R = Left, Middle, Right Switch Data; 0 = switch depressed", and the
 * sense is the trap here: a **zero** bit is a *pressed* button. "The X and Y
 * bits are Zero when the data for the corresponding direction is valid", so
 * valid movement clears them -- this model always produces valid data and
 * therefore always clears all four.
 *
 * "X and Y relative counts can range from +127 to -128. A positive count means
 * that the pointing device is moving up or right", signed two's complement.
 * Note the axis convention: positive Y is **up**, which is the opposite of
 * every host windowing system, and a frontend that forwards its own dy
 * unchanged gets an inverted mouse. */
#define AP_KBD_MOUSE_ESCAPE_RELATIVE 0xDFu
#define AP_KBD_MOUSE_ESCAPE_ABSOLUTE 0xE8u
/* Figure 13-4's B1. A set bit is a *released* button. */
#define AP_KBD_MOUSE_B1_FIXED 0x80u
#define AP_KBD_MOUSE_B1_MIDDLE 0x40u
#define AP_KBD_MOUSE_B1_RIGHT 0x20u
#define AP_KBD_MOUSE_B1_LEFT 0x10u
/* The invalid indicators, two bits each, zero when the data is valid. */
#define AP_KBD_MOUSE_B1_Y_INVALID 0x0Cu
#define AP_KBD_MOUSE_B1_X_INVALID 0x03u

/* The bytes of one relative-movement packet, `AP_KBD_MOUSE_PACKET` long.
 *
 * `dx` and `dy` are the counts the manual describes: positive is right and
 * **up**. The buttons are true when *depressed*, and this reports them the way
 * Figure 13-4 does, so a caller never has to know the inversion.
 *
 * Returns the number of bytes written: **four in Mode 0 and three in Mode 2**.
 *
 * ## Mode 2, which this used to decline
 *
 * This returned **zero** in keystate mode, with the note that "§13.3 gives that
 * mode its own packet types (2 and 3) rather than an escape, and this model
 * does not implement them". That was honest and it was also a gap, and the
 * `008778-03` walk reached the section that closes it.
 *
 * **§13.3.2**: "In this mode, **all transmissions are relative cursor
 * coordinate information packets**." There is nothing to escape, because in
 * Mode 2 nothing else is on the line -- the escape exists in Mode 0 precisely
 * to distinguish pointing data from key codes, and Mode 2 carries no key codes.
 * Figure 13-6's three bytes are then Figure 13-4's `B1`, `B2` and `B3`
 * **unchanged**: same fixed bit 7, same M/R/L in bits 6-4 with zero meaning
 * depressed, same two-bit invalid indicators, same signed counts with positive
 * meaning up and right. So the whole of Mode 2 is Mode 0 without its first
 * byte, and one builder serves both.
 *
 * **Mode 3 stays unimplemented and is now a named gap rather than a lumped
 * one**, with its packet transcribed here so that implementing it needs no
 * further page render. §13.3.3, Figure 13-7 -- "all transmissions are absolute
 * cursor coordinate information packets transmitted as three bytes of data
 * that are the X and Y coordinate values", after a button byte:
 *
 *     BYTE  bit 7   6    5    4    3    2    1    0
 *     B1        1   M    R    L    0    0    0    0
 *     B2       X7   X6   X5   X4   X3   X2   X1   X0
 *     B3       Y3   Y2   Y1   Y0   X11  X10  X9   X8
 *     B4      Y11  Y10   Y9   Y8   Y7   Y6   Y5   Y4
 *
 * "The bits are labelled such that the higher numbers are the more significant
 * bits." So **X is twelve bits across B2 and B3's low nibble, and Y is twelve
 * bits across B3's high nibble and B4** -- both spanning B3, which is what an
 * earlier note here compressed to "split across bytes 3 and 4".
 *
 * **The button polarity is `1 = switch depressed`**, and Figure 13-7 does not
 * say so -- Figure 13-3 does, for absolute data generally, against Figure
 * 13-2's `0 = switch depressed` for relative. That inversion is why this cannot
 * share the relative builder's button code, and why the two must not be merged
 * on the strength of their identical `M R L` bit positions.
 *
 * Do not confuse Figure 13-3 with Figure 13-7: 13-3 is §13.2.2's absolute
 * *format*, five bytes of Bit Pad One packed binary with even parity in bit 7
 * of each, and 13-7 is this, four bytes and no parity.
 *
 * **What is still missing is not the packet.** It needs an absolute pointing
 * device, which this core does not model and only the SDL frontend could supply
 * -- it already receives absolute coordinates and throws them away, sending
 * deltas. A complete implementation is four pieces: `keystate_mode` becomes a
 * three-valued mode rather than a bool **and it is hashed**, so the change
 * needs an identity boot; an absolute builder beside this one; a board entry
 * queuing it as `ap_board_mouse_move` does; and the frontend passing position
 * instead of delta. Named in `docs/COMPLETION_PLAN.md`.
 *
 * One erratum, recorded so a later reader does not follow it: Figure 13-6's
 * legend reads "X0 to X7 = **Y** coordinate ... Y0 to Y7 = Y coordinate". The
 * first is X, as Figure 13-4's identical legend prints correctly one page
 * earlier.
 *
 * ## And the **Mode 0** absolute packet, which is a different packet
 *
 * `AP_KBD_MOUSE_ESCAPE_ABSOLUTE` has been defined here and used by nobody, and
 * the reason is now clear: the packet it introduces was never transcribed.
 * Figure 13-7 above is *Mode 3*, where "all transmissions are absolute" so there
 * is no escape and `B1` carries the buttons. In Mode 0 the escape is what
 * distinguishes pointing data from key codes, and it takes `B1`'s place.
 *
 * `002398-04` p. 6-20 gives that form, for the **touchpad** -- an absolute
 * device with no buttons, which is why nothing displaces the escape:
 *
 *     BYTE 0   escape code E8
 *     BYTE 1   lo 8 bits of X
 *     BYTE 2   [7:4] lo bits of Y   [3:0] hi 4 bits of X
 *     BYTE 3   hi 8 bits of Y
 *
 * **Bytes 1-3 are Figure 13-7's `B2`, `B3` and `B4` unchanged** -- X twelve bits
 * across byte 1 and byte 2's low nibble, Y twelve bits across byte 2's high
 * nibble and byte 3. Two documents, two modes, one coordinate encoding. So an
 * absolute builder writes the same three bytes either way and differs only in
 * what precedes them: `E8` in Mode 0, a button byte in Mode 3.
 *
 * The same page gives three things `008778-03` does not, and an implementation
 * needs all of them:
 *
 *   - **"approximately 30 data points per second"** -- the report rate, where
 *     the relative device is event-driven.
 *   - **"through the same SIO port (zero) as the keyboard, at a speed of 1200
 *     baud"** -- so absolute data shares the keyboard's line, which is what
 *     makes the escape necessary and what a board entry must respect.
 *   - **"The range of X and Y coordinates is approximately 30 to 1100"** -- not
 *     0 to 4095. A frontend scaling its window to the full twelve bits would
 *     put the pointer off the pad at both ends.
 *
 * Still not implemented, and the gap is unchanged in shape: this core has no
 * absolute pointing device. What the page removes is the excuse that the packet
 * was unknown. */
[[nodiscard]] unsigned ap_kbd_mouse_packet(const ap_kbd_t *kbd, int dx, int dy,
                                           bool left, bool middle, bool right,
                                           uint8_t *out);

/* Mode 0's escape plus three bytes; Mode 2's three alone. A buffer of the
 * larger takes either. */
#define AP_KBD_MOUSE_PACKET 4u
#define AP_KBD_MOUSE_PACKET_MODE2 3u

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

/* Whether the tone is sounding, at the keyboard's own notion of now. The
 * auto-off is a function of the instant rather than an event, so it needs no
 * advance to have run at the right moment and cannot be missed by a coarse
 * one. */
[[nodiscard]] bool ap_kbd_beeper_on(const ap_kbd_t *kbd);

/* Whether a key auto-repeats, by Table 12-1's last column. Absent from the
 * table -- a state key -- answers false rather than defaulting. */
[[nodiscard]] bool ap_kbd_auto_repeats(const ap_kbd_ascii_t *key);

/* The code to transmit so the firmware sees `ascii`, and whether shift is
 * needed to produce it. False when no key on this keyboard can. This is what a
 * frontend needs to type text at the machine, and it is the reason the two
 * tables live side by side. */
[[nodiscard]] bool ap_kbd_encode(char ascii, uint16_t *code, bool *shifted);

#endif /* APOLLO_DEVICE_AP_KBD_H */
