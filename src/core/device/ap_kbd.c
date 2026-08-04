#include "device/ap_kbd.h"

#include <string.h>

void ap_kbd_reset(ap_kbd_t *kbd) {
  memset(kbd, 0, sizeof *kbd);
  /* Nothing held. Zero is a real key index, so the idle value has to be out of
   * range rather than the natural `memset` zero. */
  kbd->held = AP_KBD_KEYS;
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
  *code = (uint8_t)key;
  return true;
}

bool ap_kbd_release(ap_kbd_t *kbd, unsigned key, uint8_t *code) {
  if (key >= AP_KBD_KEYS || !kbd->down[key]) {
    return false;
  }
  kbd->down[key] = false;
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
