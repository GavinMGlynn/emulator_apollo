#include "device/ap_kbd.h"

#include <string.h>

void ap_kbd_reset(ap_kbd_t *kbd) {
  memset(kbd, 0, sizeof *kbd);
  /* Nothing held. Zero is a real key index, so the idle value has to be out of
   * range rather than the natural `memset` zero. */
  kbd->held = AP_KBD_KEYS;
  /* **Loopback at power-on**, which is the state a real one comes up in: until
   * told otherwise it echoes what it is sent rather than acting on it, and that
   * is how a host discovers a keyboard is there at all. `memset` would have
   * made it false, and false is a claim. */
  kbd->loopback = true;
  kbd->rx_message = 0u;
  kbd->keystate_mode = false;
}

bool ap_kbd_press(ap_kbd_t *kbd, unsigned key, uint8_t *code) {
  if (key >= AP_KBD_KEYS || kbd->down[key]) {
    return false;
  }
  kbd->down[key] = true;
  /* The newest key held is the one that repeats, and its delay starts now. A
   * second key struck while the first is down takes the repeat over, which is
   * what a matrix scan reporting the newest transition does. */
  kbd->held = key;
  kbd->repeat_at = kbd->now + AP_KBD_REPEAT_DELAY;
  kbd->repeating = false;
  /* §12.2: the lamp "comes on during down transitions of the key when it was
   * previously off". The guard is the switch's, not ours -- `down[]` above
   * already refused a second press without a release -- so the down transition
   * and the lamp coming on are the same event. See the header for why an
   * alternate-action keyswitch is what makes that a *lock*. */
  if (key == AP_KBD_CAPS_LOCK_LED_ON) {
    kbd->caps_lock_led = true;
  }
  *code = (uint8_t)key;
  return true;
}

bool ap_kbd_caps_lock_led(const ap_kbd_t *kbd) { return kbd->caps_lock_led; }

bool ap_kbd_release(ap_kbd_t *kbd, unsigned key, uint8_t *code) {
  if (key >= AP_KBD_KEYS || !kbd->down[key]) {
    return false;
  }
  kbd->down[key] = false;
  /* And "goes off during up transitions when it was previously on". */
  if (key == AP_KBD_CAPS_LOCK_LED_ON) {
    kbd->caps_lock_led = false;
  }
  /* The make code with bit 7 set. Not a separate table: the release code *is*
   * the press code plus the flag, which is why the matrix stops at 0x80. */
  *code = (uint8_t)(key | AP_KBD_RELEASE);
  if (kbd->held == key) {
    /* Releasing the repeating key stops the repeat. It does **not** hand it to
     * whatever else is still down: the real part repeats the key being held
     * most recently, and reviving an older one would produce characters the
     * operator stopped asking for. */
    kbd->held = AP_KBD_KEYS;
    kbd->repeating = false;
  }
  return true;
}

/* ---- The ASCII set, `008778-03` Table 12-1 -------------------------------- */

/* Read from the **page images** of pages 12-3 and 12-4, not the text layer:
 * the extraction interleaves the seven columns into unlabelled runs of numbers
 * and mangles the keypad's two-byte codes -- `FE 38` arrives as `FE~)8` and
 * `FE 2A` as `FE:2A`, which are plausible-looking and wrong. */
static const ap_kbd_ascii_t ASCII[] = {
    {"LA0", "INS/MARK", 0x81u, 0x91u, 0x81u, 0x81u, 0xA1u, false},
    {"LA1", "LINE DEL", 0x82u, 0x92u, 0x82u, 0x82u, 0xA2u, false},
    {"LA2", "CHAR DEL", 0x83u, 0x93u, 0x83u, 0x83u, 0xA3u, true},
    {"A0", "F0", 0x1Cu, 0x5Cu, 0x7Cu, 0x1Cu, 0xBCu, false},
    {"A1", "F1", 0xC0u, 0xD0u, 0xF0u, 0xC0u, 0xE0u, false},
    {"A2", "F2", 0xC1u, 0xD1u, 0xF1u, 0xC1u, 0xE1u, false},
    {"A3", "F3", 0xC2u, 0xD2u, 0xF2u, 0xC2u, 0xE2u, false},
    {"A4", "F4", 0xC3u, 0xD3u, 0xF3u, 0xC3u, 0xE3u, false},
    {"A5", "F5", 0xC4u, 0xD4u, 0xF4u, 0xC4u, 0xE4u, false},
    {"A6", "F6", 0xC5u, 0xD5u, 0xF5u, 0xC5u, 0xE5u, false},
    {"A7", "F7", 0xC6u, 0xD6u, 0xF6u, 0xC6u, 0xE6u, false},
    {"A8", "F8", 0xC7u, 0xD7u, 0xF7u, 0xC7u, 0xE7u, false},
    {"A9", "F9", 0x1Fu, 0x2Fu, 0x3Fu, 0x1Fu, 0xBDu, false},
    {"RA0", "AGAIN", 0xCDu, 0xE9u, 0xCDu, 0xCDu, 0xEDu, false},
    {"RA1", "READ", 0xCEu, 0xEAu, 0xCEu, 0xCEu, 0xEEu, false},
    {"RA2", "SAVE/EDIT", 0xCFu, 0xEBu, 0xCFu, 0xCFu, 0xEFu, false},
    {"RA3", "ABORT/EXIT", 0xDDu, 0xECu, 0xDDu, 0xDDu, 0xFDu, false},
    {"RA4", "HELP/HOLD", 0xB3u, 0xB7u, 0xB3u, 0xB3u, 0xBBu, false},
    {"LB0", "CUT/COPY", 0xB0u, 0xB4u, 0xB0u, 0xB0u, 0xB8u, false},
    {"LB1", "UNDO/PASTE", 0xB1u, 0xB5u, 0xB1u, 0xB1u, 0xB9u, false},
    {"LB2", "MOVE/GROW", 0xB2u, 0xB6u, 0xB2u, 0xB2u, 0xBAu, false},
    {"B1", "ESC", 0x1Bu, 0x1Bu, AP_KBD_NO_CODE, 0x1Bu, AP_KBD_NO_CODE, false},
    {"B2", "! 1", 0x31u, 0x21u, AP_KBD_NO_CODE, 0x31u, AP_KBD_NO_CODE, false},
    {"B3", "@ 2", 0x32u, 0x40u, AP_KBD_NO_CODE, 0x32u, AP_KBD_NO_CODE, false},
    {"B4", "# 3", 0x33u, 0x23u, AP_KBD_NO_CODE, 0x33u, AP_KBD_NO_CODE, false},
    {"B5", "$ 4", 0x34u, 0x24u, AP_KBD_NO_CODE, 0x34u, AP_KBD_NO_CODE, false},
    {"B6", "% 5", 0x35u, 0x25u, AP_KBD_NO_CODE, 0x35u, AP_KBD_NO_CODE, false},
    {"B7", "^ 6", 0x36u, 0x5Eu, AP_KBD_NO_CODE, 0x36u, AP_KBD_NO_CODE, false},
    {"B8", "& 7", 0x37u, 0x26u, AP_KBD_NO_CODE, 0x37u, AP_KBD_NO_CODE, false},
    {"B9", "* 8", 0x38u, 0x2Au, AP_KBD_NO_CODE, 0x38u, AP_KBD_NO_CODE, false},
    {"B10", "( 9", 0x39u, 0x28u, AP_KBD_NO_CODE, 0x39u, AP_KBD_NO_CODE, false},
    {"B11", ") 0", 0x30u, 0x29u, AP_KBD_NO_CODE, 0x30u, AP_KBD_NO_CODE, false},
    {"B12", "_ -", 0x2Du, 0x5Fu, AP_KBD_NO_CODE, 0x2Du, AP_KBD_NO_CODE, true},
    {"B13", "+ =", 0x3Du, 0x2Bu, AP_KBD_NO_CODE, 0x3Du, AP_KBD_NO_CODE, true},
    {"B14", "~ '", 0x60u, 0x7Eu, 0x1Eu, 0x60u, AP_KBD_NO_CODE, false},
    {"B15", "BACK SPACE", 0xDEu, 0xDEu, AP_KBD_NO_CODE, 0xDEu, AP_KBD_NO_CODE, true},
    {"LC0", "left-bar", 0x84u, 0x94u, 0x84u, 0x84u, 0xA4u, false},
    {"LC1", "SHELL/CMD", 0x85u, 0x95u, 0x85u, 0x85u, 0xA5u, false},
    {"LC2", "right-bar", 0x86u, 0x96u, 0x86u, 0x86u, 0xA6u, false},
    {"C1", "TAB", 0xCAu, 0xDAu, 0xFAu, 0xCAu, AP_KBD_NO_CODE, false},
    {"C2", "Q", 0x71u, 0x51u, 0x11u, 0x51u, AP_KBD_NO_CODE, false},
    {"C3", "W", 0x77u, 0x57u, 0x17u, 0x57u, AP_KBD_NO_CODE, false},
    {"C4", "E", 0x65u, 0x45u, 0x05u, 0x45u, AP_KBD_NO_CODE, false},
    {"C5", "R", 0x72u, 0x52u, 0x12u, 0x52u, AP_KBD_NO_CODE, false},
    {"C6", "T", 0x74u, 0x54u, 0x14u, 0x54u, AP_KBD_NO_CODE, false},
    {"C7", "Y", 0x79u, 0x59u, 0x19u, 0x59u, AP_KBD_NO_CODE, false},
    {"C8", "U", 0x75u, 0x55u, 0x15u, 0x55u, AP_KBD_NO_CODE, false},
    {"C9", "I", 0x69u, 0x49u, 0x09u, 0x49u, AP_KBD_NO_CODE, false},
    {"C10", "O", 0x6Fu, 0x4Fu, 0x0Fu, 0x4Fu, AP_KBD_NO_CODE, false},
    {"C11", "P", 0x70u, 0x50u, 0x10u, 0x50u, AP_KBD_NO_CODE, false},
    {"C12", "{ [", 0x7Bu, 0x5Bu, 0x1Bu, 0x7Bu, AP_KBD_NO_CODE, false},
    {"C13", "} ]", 0x7Du, 0x5Du, 0x1Du, 0x7Du, AP_KBD_NO_CODE, false},
    {"C14", "DELETE", 0x7Fu, 0x7Fu, AP_KBD_NO_CODE, 0x7Fu, AP_KBD_NO_CODE, true},
    {"RC1", "keypad 7", 0xFE37u, 0xFE26u, AP_KBD_NO_CODE, 0xFE37u, AP_KBD_NO_CODE, false},
    {"RC2", "keypad 8", 0xFE38u, 0xFE2Au, AP_KBD_NO_CODE, 0xFE38u, AP_KBD_NO_CODE, false},
    {"RC3", "keypad 9", 0xFE39u, 0xFE28u, AP_KBD_NO_CODE, 0xFE39u, AP_KBD_NO_CODE, false},
    {"RC4", "keypad +", 0xFE2Bu, 0xFE3Du, AP_KBD_NO_CODE, 0xFE2Bu, AP_KBD_NO_CODE, false},
    {"LD0", "left-arrow-bracket", 0x87u, 0x97u, 0x87u, 0x87u, 0xA7u, false},
    {"LD1", "up-arrow", 0x88u, 0x98u, 0x88u, 0x88u, 0xA8u, true},
    {"LD2", "right-arrow-bracket", 0x89u, 0x99u, 0x89u, 0x89u, 0xA9u, false},
    {"D2", "A", 0x61u, 0x41u, 0x01u, 0x41u, AP_KBD_NO_CODE, false},
    {"D3", "S", 0x73u, 0x53u, 0x13u, 0x53u, AP_KBD_NO_CODE, false},
    {"D4", "D", 0x64u, 0x44u, 0x04u, 0x44u, AP_KBD_NO_CODE, false},
    {"D5", "F", 0x66u, 0x46u, 0x06u, 0x46u, AP_KBD_NO_CODE, false},
    {"D6", "G", 0x67u, 0x47u, 0x07u, 0x47u, AP_KBD_NO_CODE, false},
    {"D7", "H", 0x68u, 0x48u, 0x08u, 0x48u, AP_KBD_NO_CODE, false},
    {"D8", "J", 0x6Au, 0x4Au, 0x0Au, 0x4Au, AP_KBD_NO_CODE, false},
    {"D9", "K", 0x6Bu, 0x4Bu, 0x0Bu, 0x4Bu, AP_KBD_NO_CODE, false},
    {"D10", "L", 0x6Cu, 0x4Cu, 0x0Cu, 0x4Cu, AP_KBD_NO_CODE, false},
    {"D11", ": ;", 0x3Bu, 0x3Au, 0xFBu, 0x3Bu, AP_KBD_NO_CODE, false},
    {"D12", "\" '", 0x27u, 0x22u, 0xF8u, 0x27u, AP_KBD_NO_CODE, false},
    {"D13", "RETURN", 0xCBu, 0xDBu, AP_KBD_NO_CODE, 0xCBu, AP_KBD_NO_CODE, false},
    {"D14", "| \\", 0xC8u, 0xC9u, AP_KBD_NO_CODE, 0xC8u, AP_KBD_NO_CODE, false},
    {"RD1", "keypad 4", 0xFE34u, 0xFE24u, AP_KBD_NO_CODE, 0xFE34u, AP_KBD_NO_CODE, false},
    {"RD2", "keypad 5", 0xFE35u, 0xFE25u, AP_KBD_NO_CODE, 0xFE35u, AP_KBD_NO_CODE, false},
    {"RD3", "keypad 6", 0xFE36u, 0xFE5Eu, AP_KBD_NO_CODE, 0xFE36u, AP_KBD_NO_CODE, false},
    {"RD4", "keypad -", 0xFE2Du, 0xFE5Fu, AP_KBD_NO_CODE, 0xFE2Du, AP_KBD_NO_CODE, false},
    {"LE0", "left-arrow", 0x8Au, 0x9Au, 0x9Au, 0x9Au, 0xAAu, true},
    {"LE1", "NEXT WINDOW", 0x8Bu, 0x9Bu, 0x8Bu, 0x8Bu, 0xABu, false},
    {"LE2", "right-arrow", 0x8Cu, 0x9Cu, 0x8Cu, 0x8Cu, 0xACu, true},
    {"E2", "Z", 0x7Au, 0x5Au, 0x1Au, 0x5Au, AP_KBD_NO_CODE, false},
    {"E3", "X", 0x78u, 0x58u, 0x18u, 0x58u, AP_KBD_NO_CODE, false},
    {"E4", "C", 0x63u, 0x43u, 0x03u, 0x43u, AP_KBD_NO_CODE, false},
    {"E5", "V", 0x76u, 0x56u, 0x16u, 0x56u, AP_KBD_NO_CODE, false},
    {"E6", "B", 0x62u, 0x42u, 0x02u, 0x42u, AP_KBD_NO_CODE, false},
    {"E7", "N", 0x6Eu, 0x4Eu, 0x0Eu, 0x4Eu, AP_KBD_NO_CODE, false},
    {"E8", "M", 0x6Du, 0x4Du, 0x0Du, 0x4Du, AP_KBD_NO_CODE, false},
    {"E9", "< ,", 0x2Cu, 0x3Cu, AP_KBD_NO_CODE, 0x2Cu, AP_KBD_NO_CODE, false},
    {"E10", "> .", 0x2Eu, 0x3Eu, AP_KBD_NO_CODE, 0x2Eu, AP_KBD_NO_CODE, true},
    {"E11", "? /", 0xCCu, 0xDCu, 0xFCu, 0xCCu, AP_KBD_NO_CODE, false},
    {"E13", "POP", 0x80u, 0x90u, 0x80u, 0x80u, 0xA0u, false},
    {"RE1", "keypad 1", 0xFE31u, 0xFE21u, AP_KBD_NO_CODE, 0xFE31u, AP_KBD_NO_CODE, false},
    {"RE2", "keypad 2", 0xFE32u, 0xFE40u, AP_KBD_NO_CODE, 0xFE32u, AP_KBD_NO_CODE, false},
    {"RE3", "keypad 3", 0xFE33u, 0xFE23u, AP_KBD_NO_CODE, 0xFE33u, AP_KBD_NO_CODE, false},
    {"LF0", "up-arrow-bracket", 0x8Du, 0x9Du, 0x8Du, 0x8Du, 0xADu, false},
    {"LF1", "down-arrow", 0x8Eu, 0x9Eu, 0x8Eu, 0x8Eu, 0xAEu, true},
    {"LF2", "down-arrow-bracket", 0x8Fu, 0x9Fu, 0x8Fu, 0x8Fu, 0xAFu, false},
    {"F1", "space bar", 0x20u, 0x20u, 0x20u, 0x20u, AP_KBD_NO_CODE, true},
    {"RF1", "keypad 0", 0xFE30u, 0xFE29u, AP_KBD_NO_CODE, 0xFE30u, AP_KBD_NO_CODE, false},
    {"RF2", "keypad .", 0xFE2Eu, 0xFE2Eu, AP_KBD_NO_CODE, 0xFE2Eu, AP_KBD_NO_CODE, false},
    {"RF3", "ENTER", 0xFECBu, 0xFEDBu, AP_KBD_NO_CODE, 0xFECBu, AP_KBD_NO_CODE, false},
};

unsigned ap_kbd_ascii_count(void) { return (unsigned)(sizeof ASCII / sizeof ASCII[0]); }

const ap_kbd_ascii_t *ap_kbd_ascii_at(unsigned index) {
  return index < ap_kbd_ascii_count() ? &ASCII[index] : nullptr;
}

const ap_kbd_ascii_t *ap_kbd_ascii_find(const char *key) {
  if (key == nullptr) {
    return nullptr;
  }
  for (unsigned i = 0; i < ap_kbd_ascii_count(); i++) {
    if (strcmp(ASCII[i].key, key) == 0) {
      return &ASCII[i];
    }
  }
  return nullptr;
}

uint16_t ap_kbd_ascii_code(const ap_kbd_ascii_t *key, ap_kbd_mod_t mod) {
  if (key == nullptr) {
    return AP_KBD_NO_CODE;
  }
  switch (mod) {
    case AP_KBD_MOD_NONE: return key->unshifted;
    case AP_KBD_MOD_SHIFT: return key->shifted;
    case AP_KBD_MOD_CONTROL: return key->control;
    case AP_KBD_MOD_UP_TRANS: return key->up_trans;
  }
  return AP_KBD_NO_CODE;
}

bool ap_kbd_ascii_decode(uint16_t code, const ap_kbd_ascii_t **key,
                         ap_kbd_mod_t *mod) {
  /* Unshifted first, up transition last: `002398-04` p. 6-13's own choice of
   * name wherever a byte is reachable two ways. */
  static const ap_kbd_mod_t ORDER[] = {AP_KBD_MOD_NONE, AP_KBD_MOD_SHIFT,
                                       AP_KBD_MOD_CONTROL,
                                       AP_KBD_MOD_UP_TRANS};
  if (code == AP_KBD_NO_CODE) {
    return false;
  }
  for (unsigned m = 0; m < sizeof ORDER / sizeof ORDER[0]; m++) {
    for (unsigned i = 0; i < ap_kbd_ascii_count(); i++) {
      if (ap_kbd_ascii_code(&ASCII[i], ORDER[m]) == code) {
        if (key != nullptr) {
          *key = &ASCII[i];
        }
        if (mod != nullptr) {
          *mod = ORDER[m];
        }
        return true;
      }
    }
  }
  return false;
}

/* The boot PROM's twenty-entry table, `FINDINGS.md` C109. Firmware behaviour,
 * transcribed here so `ap_kbd_encode` can consult it -- the part itself knows
 * nothing of this.
 *
 * Every code on the left is a Table 12-1 entry: `CB`/`DB` are RETURN's two,
 * `CA`/`DA`/`FA` TAB's three, `DE` BACK SPACE's, `CC`/`DC`/`FC` the `? /`
 * key's, `C8`/`C9` the `| \\` key's. The four in the middle are the firmware
 * correcting the bracket keys, which Table 12-1 shows sending `7B` unshifted
 * and `5B` shifted -- the opposite way round from the US convention. */
static const struct { uint8_t code; uint8_t ascii; } PROM[] = {
    {0xCBu, 0x0Du}, {0xDBu, 0x0Du}, /* RETURN -> CR */
    {0xFBu, 0x1Bu},                 /* `: ;` control -> ESC */
    {0xC8u, 0x5Cu}, {0xD8u, 0x5Cu}, {0xF8u, 0x5Cu}, /* -> backslash */
    {0xC9u, 0x7Cu}, {0xD9u, 0x7Cu},                 /* -> bar */
    {0xF9u, 0x7Fu},                                 /* -> DEL */
    {0x5Bu, 0x7Bu}, {0x7Bu, 0x5Bu},                 /* brackets, swapped */
    {0x5Du, 0x7Du}, {0x7Du, 0x5Du},
    {0xCAu, 0x09u}, {0xDAu, 0x09u}, {0xFAu, 0x09u}, /* TAB -> HT */
    {0xCCu, 0x2Fu},                                 /* `? /` -> slash */
    {0xDCu, 0x3Fu}, {0xFCu, 0x3Fu},                 /* -> question mark */
    {0xDEu, 0x08u},                                 /* BACK SPACE -> BS */
};

uint16_t ap_kbd_prom_ascii(uint8_t code) {
  for (unsigned i = 0; i < sizeof PROM / sizeof PROM[0]; i++) {
    if (PROM[i].code == code) {
      return PROM[i].ascii;
    }
  }
  return AP_KBD_NO_CODE;
}

bool ap_kbd_encode(char ascii, uint16_t *code, bool *shifted) {
  if (code == nullptr || shifted == nullptr) {
    return false;
  }
  const uint8_t want = (uint8_t)ascii;

  /* A code the firmware *translates* is preferred over one that merely looks
   * right. `0D` is reachable only as RETURN's `CB`, and `5C` only through the
   * `| \\` key -- sending `0D` or `5C` raw would be sending a byte no key on
   * this keyboard produces. */
  for (unsigned i = 0; i < sizeof PROM / sizeof PROM[0]; i++) {
    if (PROM[i].ascii != want) {
      continue;
    }
    for (unsigned k = 0; k < ap_kbd_ascii_count(); k++) {
      if (ASCII[k].unshifted == PROM[i].code) {
        *code = PROM[i].code;
        *shifted = false;
        return true;
      }
      if (ASCII[k].shifted == PROM[i].code) {
        *code = PROM[i].code;
        *shifted = true;
        return true;
      }
    }
  }

  /* Otherwise the ASCII set already sends the character itself. Unshifted is
   * tried across the whole table before shifted, so a character reachable both
   * ways comes back the simpler one. */
  for (unsigned k = 0; k < ap_kbd_ascii_count(); k++) {
    if (ASCII[k].unshifted == want) {
      *code = want;
      *shifted = false;
      return true;
    }
  }
  for (unsigned k = 0; k < ap_kbd_ascii_count(); k++) {
    if (ASCII[k].shifted == want) {
      *code = want;
      *shifted = true;
      return true;
    }
  }
  return false;
}

bool ap_kbd_auto_repeats(const ap_kbd_ascii_t *key) {
  return key != nullptr && key->auto_repeat;
}

/* ---- §12.2's transmit buffer ---------------------------------------------- */

unsigned ap_kbd_buffered(const ap_kbd_t *kbd) { return kbd->buffered; }

bool ap_kbd_buffer_full(const ap_kbd_t *kbd) {
  return kbd->buffered >= AP_KBD_BUFFER;
}

bool ap_kbd_buffer(ap_kbd_t *kbd, uint8_t code) {
  if (ap_kbd_buffer_full(kbd)) {
    /* "Further processing of data is inhibited until a new position becomes
     * available." The byte is refused rather than dropped silently at the far
     * end: the caller learns the transition did not happen, which is what lets
     * a press leave the matrix alone so its release cannot be sent either. */
    return false;
  }
  /* **When the character starts is decided here, not at transmit.**
   *
   * An idle transmitter begins this byte the instant it is queued, so it
   * finishes one character time from `kbd->now`. A byte joining a queue that is
   * already going out inherits whatever point the run has reached and needs no
   * stamp of its own.
   *
   * Deciding it at transmit instead cannot work, and the failure is worth
   * naming: a caller advancing coarsely calls `ap_kbd_transmit` once with a
   * `now` several character times past the queueing, and from that alone the
   * part cannot tell a byte queued long ago from one queued this instant. It
   * delivered one byte per call, so a coarse step lost the rest of the burst --
   * which is the same defect `ap_kbd_advance` avoids for repeats. */
  if (kbd->buffered == 0u) {
    kbd->tx_at = kbd->now + AP_KBD_TX_CHARACTER;
  }
  kbd->buffer[(kbd->buffer_head + kbd->buffered) % AP_KBD_BUFFER] = code;
  kbd->buffered++;
  return true;
}

bool ap_kbd_transmit(ap_kbd_t *kbd, ap_time_t now, uint8_t *code) {
  if (kbd->buffered == 0u || now < kbd->tx_at) {
    return false;
  }
  if (code != nullptr) {
    *code = kbd->buffer[kbd->buffer_head];
  }
  kbd->buffer_head = (kbd->buffer_head + 1u) % AP_KBD_BUFFER;
  kbd->buffered--;
  /* A character on from the last one and **not** from `now`, so the rate is a
   * property of the wire rather than of how often this is called. `ap_kbd_buffer`
   * has already set the first character's finish for a transmitter that was
   * idle, so this never runs ahead of the queueing. */
  kbd->tx_at += AP_KBD_TX_CHARACTER;
  return true;
}

bool ap_kbd_advance(ap_kbd_t *kbd, ap_time_t now, unsigned *key) {
  if (now > kbd->now) {
    kbd->now = now;
  }
  if (key == nullptr || kbd->held >= AP_KBD_KEYS || kbd->now < kbd->repeat_at) {
    return false;
  }
  /* Due. The next one is a period away, not a period from *now* -- so a coarse
   * advance that skips several intervals does not silently drop the ones it
   * stepped over, and the repeat rate stays independent of how often this is
   * called. That is the same property every other advance in this core keeps. */
  kbd->repeat_at += AP_KBD_REPEAT_PERIOD;
  /* Past the initial delay now; every repeat after this one is a period apart.
   * The flag is kept because the *first* interval is the delay and the rest are
   * the period, and only a press can put it back. */
  kbd->repeating = true;
  *key = kbd->held;
  return true;
}

bool ap_kbd_beeper_on(const ap_kbd_t *kbd) {
  if (kbd == NULL || kbd->beeper_until == 0u) {
    return false;
  }
  /* A function of the instant, not an event: `kbd->now` is whatever the last
   * advance set, so a run that steps past the whole 300 ms still sees the tone
   * as over rather than stuck on. */
  return kbd->now < kbd->beeper_until;
}

/* `set_mode`'s two bytes: `FF` then the mode number. The keyboard *announces* a
 * mode change rather than merely making one, which is what lets a host that
 * asked for a set know the part is in it -- and what the boot PROM reads two of
 * after the loopback test.
 *
 * Written as a helper because the oracle calls `set_mode` from more than one
 * place and the announcement has to travel with every one of them. Making it a
 * property of "the mode changed" is exactly what the source does; making it a
 * line of code in one arm is how this went missing. */
static void announce_mode(const ap_kbd_t *kbd, uint8_t *reply,
                          unsigned capacity, unsigned *sent) {
  const uint8_t bytes[2] = {0xFFu, kbd->keystate_mode ? 0x01u : 0x00u};
  for (unsigned i = 0; i < 2u; i++) {
    if (*sent < capacity) {
      reply[(*sent)++] = bytes[i];
    }
  }
}

/* The command channel. Every command begins `FF`; the bytes after it accumulate
 * until one matches, because `FF12` is a prefix and `FF1221` is a command and a
 * model matching a byte at a time cannot tell them apart. */
unsigned ap_kbd_receive(ap_kbd_t *kbd, uint8_t byte, uint8_t *reply,
                        unsigned capacity) {
  if (kbd == NULL || reply == NULL) {
    return 0u;
  }
  unsigned sent = 0u;
#define EMIT(b)                       \
  do {                                \
    if (sent < capacity) {            \
      reply[sent++] = (uint8_t)(b);   \
    }                                 \
  } while (0)

  if (byte == 0xFFu) {
    /* The start of a command, and it re-enters loopback: a host that has lost
     * track can always get back to a known state by sending `FF`. */
    kbd->rx_message = 0xFFu;
    kbd->loopback = true;
    EMIT(byte);
    return sent;
  }
  if (byte == 0x00u) {
    /* In loopback it ends the conversation and selects the compatibility set.
     * It is **not echoed** -- and it is **announced**, which is the half this
     * file had missing and the reason `KEYBOARD TEST # 0` failed for eight
     * commits.
     *
     * The oracle is the source; there is no published Apollo keyboard protocol
     * and Chapter 12 stops short of the command channel. `apollo_kbd.cpp`:
     *
     *     else if (data == 0x00) {
     *         if (m_loopback_mode) {
     *             set_mode(KBD_MODE_0_COMPATIBILITY);
     *             m_loopback_mode = 0;
     *         }
     *     }
     *
     * and `set_mode` is not a setter:
     *
     *     void apollo_kbd_device::set_mode(uint16_t mode) {
     *         xmit_char(0xff);
     *         xmit_char(mode);
     *         m_mode = mode;
     *     }
     *
     * **Two bytes go out** -- `FF` then the mode. This project read the same
     * source twice and recorded "handled with no `putdata`", which is true and
     * is not the whole statement: the reply comes from inside `set_mode`, not
     * from an echo. A one-byte echo was implemented on that misreading, and
     * reverted when it did not fix the test -- it satisfied the first of the
     * firmware's *two* reads and could not satisfy the second.
     *
     * The firmware agrees exactly. `0073F0` sends `00`, `007418` and `00741A`
     * read two bytes, and `00741C` overwrites `d1` immediately -- so it
     * requires two bytes and ignores what they are. */
    if (kbd->loopback) {
      kbd->keystate_mode = false;
      kbd->loopback = false;
      announce_mode(kbd, reply, capacity, &sent);
    }
    return sent;
  }

  kbd->rx_message = (kbd->rx_message << 8) | byte;
  switch (kbd->rx_message) {
    case 0xFF00u:
      EMIT(byte);
      kbd->keystate_mode = false;
      kbd->loopback = false;
      kbd->rx_message = 0u;
      return sent;
    case 0xFF01u:
      EMIT(byte);
      kbd->keystate_mode = true;
      kbd->rx_message = 0u;
      return sent;
    case 0xFF11u:
    case 0xFF12u:
      /* Prefixes. Echoed, and the message is *kept* so the next byte can
       * complete it -- clearing it here is what a one-byte matcher does and it
       * makes `FF1221` unreachable. */
      EMIT(byte);
      return sent;
    case 0xFF1116u:
      EMIT(0x00u);
      EMIT(0xFFu);
      EMIT(0x00u);
      kbd->loopback = false;
      kbd->rx_message = 0u;
      return sent;
    case 0xFF1117u:
      /* Silent, and it does not leave loopback. */
      kbd->rx_message = 0u;
      return sent;
    case 0xFF1221u: {
      /* Identify. The host asks this before believing a keyboard is there. */
      kbd->loopback = false;
      EMIT(byte);
      static const char id[] = AP_KBD_IDENTIFICATION;
      for (unsigned i = 0; id[i] != '\0'; i++) {
        EMIT((uint8_t)id[i]);
      }
      /* And the mode, announced after the string. `apollo_kbd.cpp` ends this
       * arm with `set_mode(m_mode == KBD_MODE_0_COMPATIBILITY ?
       * KBD_MODE_0_COMPATIBILITY : KBD_MODE_1_KEYSTATE)` -- which restates the
       * mode it is already in, so the effect is the *announcement* and not a
       * change. Missing here for the same reason the `00` one was. */
      announce_mode(kbd, reply, capacity, &sent);
      kbd->rx_message = 0u;
      return sent;
    }
    case 0xFF2181u:
      /* Tone on, and `002398-04` p. 12-2 says it "will go off automatically
       * after 300 milliseconds" -- so the off is the keyboard's, not the
       * host's, and this is the whole of what makes it worth modelling. The
       * sound is not modelled; the level and its expiry are. */
      kbd->beeper_until = kbd->now + AP_KBD_BEEPER_DURATION;
      EMIT(byte);
      kbd->rx_message = 0u;
      return sent;
    case 0xFF2182u:
      /* Tone off, before the 300 ms are up. Same page, the second sequence. */
      kbd->beeper_until = 0u;
      EMIT(byte);
      kbd->rx_message = 0u;
      return sent;
    default:
      /* In loopback anything else comes straight back, which is what makes it
       * loopback. Outside it, an unrecognised byte is ignored. */
      if (kbd->loopback) {
        EMIT(byte);
      }
      return sent;
  }
#undef EMIT
}

/* §13.3.1 and Figure 13-4. See `ap_kbd.h` for the packet and the two traps in
 * it: a zero button bit means *depressed*, and positive Y is up. */
unsigned ap_kbd_mouse_packet(const ap_kbd_t *kbd, int dx, int dy, bool left,
                             bool middle, bool right, uint8_t *out) {
  if (kbd == NULL || out == NULL) {
    return 0u;
  }
  /* "+127 to -128": the counts are one signed byte, so a larger movement is
   * clamped rather than wrapped. Wrapping would turn a fast drag right into a
   * jump left, which is a bug a person sees and a test does not. */
  if (dx > 127) {
    dx = 127;
  } else if (dx < -128) {
    dx = -128;
  }
  if (dy > 127) {
    dy = 127;
  } else if (dy < -128) {
    dy = -128;
  }

  uint8_t b1 = AP_KBD_MOUSE_B1_FIXED;
  /* Zero is depressed, so a button that is *up* sets its bit. */
  if (!left) {
    b1 |= AP_KBD_MOUSE_B1_LEFT;
  }
  if (!middle) {
    b1 |= AP_KBD_MOUSE_B1_MIDDLE;
  }
  if (!right) {
    b1 |= AP_KBD_MOUSE_B1_RIGHT;
  }
  /* Both invalid fields stay clear: this model's counts are always valid. */

  /* §13.3.2, Mode 2: "In this mode, **all transmissions are relative cursor
   * coordinate information packets**", so there is nothing to escape and the
   * escape byte is absent. Figure 13-6's three bytes are Figure 13-4's B1, B2
   * and B3 unchanged -- same button polarity, same invalid indicators, same
   * signed counts -- which is why one builder serves both modes and only the
   * leading byte differs. See the header. */
  unsigned n = 0u;
  if (!kbd->keystate_mode) {
    out[n++] = AP_KBD_MOUSE_ESCAPE_RELATIVE;
  }
  out[n++] = b1;
  out[n++] = (uint8_t)dx;
  out[n++] = (uint8_t)dy;
  return n;
}

/* ## `002398-04` p. 6-14, the Low-Profile Model II keystate chart
 *
 * A 16x16 grid whose **column is the high nibble and row the low**, so the code
 * at column `6`, row `E` is `6E`. Columns `8`-`F` repeat `0`-`7` under an
 * up-transition mark, which is `AP_KBD_RELEASE` seen as a table: the down codes
 * fill `00`-`7F` and every one has an up code `0x80` above it.
 *
 * Transcribed from a **400 dpi render**, cropped in two halves. At the walk's
 * usual 150 dpi the cells are not reliably legible, and a keyboard table that
 * looks authoritative and mis-maps a key is worse than none -- so the
 * resolution is part of the method, not a convenience.
 *
 * **106 of the 128 codes are keys.** The 22 blanks are blanks in the document,
 * not gaps in this transcription, and `ap_kbd_key_name` returns nullptr for
 * them: a code the keyboard cannot send should not acquire a name here.
 *
 * The names are p. 6-12's positional map -- `A0`-`A9` and `B1`-`B15` across the
 * top rows, `C1`-`C14`, `D2`-`D14`, `E0`-`E13`, `F1` the space bar, `LA0`-`LF2`
 * down the left pad and `RA0`-`RF3` on the right. There is no `RB` row, which a
 * reader assuming a regular grid would invent. `7E` is not a key at all: it is
 * `LED ON`. */
static const char *const kbd_keystate_names[AP_KBD_KEYS] = {
    nullptr, "LA0", "LA1", "LA2",  /* 00 */
    "A0", "A1", "A2", "A3",  /* 04 */
    "A4", "A5", "A6", "A7",  /* 08 */
    "A8", "A9", "RA0", "RA1",  /* 0C */
    "RA2", "RA3", "RA4", "LB0",  /* 10 */
    "LB1", "LB2", nullptr, "B1",  /* 14 */
    "B2", "B3", "B4", "B5",  /* 18 */
    "B6", "B7", "B8", "B9",  /* 1C */
    "B10", "B11", "B12", "B13",  /* 20 */
    "B14", "B15", nullptr, "LC0",  /* 24 */
    "LC1", "LC2", nullptr, nullptr,  /* 28 */
    "C1", "C2", "C3", "C4",  /* 2C */
    "C5", "C6", "C7", "C8",  /* 30 */
    "C9", "C10", "C11", "C12",  /* 34 */
    "C13", nullptr, "C14", nullptr,  /* 38 */
    "RC1", "RC2", "RC3", "RC4",  /* 3C */
    "LD0", "LD1", "LD2", "LD3",  /* 40 */
    nullptr, nullptr, "D2", "D3",  /* 44 */
    "D4", "D5", "D6", "D7",  /* 48 */
    "D8", "D9", "D10", "D11",  /* 4C */
    "D12", nullptr, "D13", "D14",  /* 50 */
    nullptr, "RD1", "RD2", "RD3",  /* 54 */
    "RD4", "LE0", "LE1", "LE2",  /* 58 */
    nullptr, "E0", "E1", nullptr,  /* 5C */
    "E2", "E3", "E4", "E5",  /* 60 */
    "E6", "E7", "E8", "E9",  /* 64 */
    "E10", "E11", "E12", nullptr,  /* 68 */
    "E13", nullptr, "RE1", "RE2",  /* 6C */
    "RE3", nullptr, "LF0", "LF1",  /* 70 */
    "LF2", nullptr, "F1", nullptr,  /* 74 */
    nullptr, "RF1", nullptr, "RF2",  /* 78 */
    "RF3", nullptr, "LED ON", nullptr,  /* 7C */
};

const char *ap_kbd_key_name(uint8_t code) {
  /* A release carries the same key as its press, so both answer one name --
   * which is what makes this usable on a captured byte stream without the
   * caller having to strip the bit first. */
  const uint8_t down = (uint8_t)(code & (uint8_t)~AP_KBD_RELEASE);
  return kbd_keystate_names[down];
}
