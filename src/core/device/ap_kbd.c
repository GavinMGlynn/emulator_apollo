#include "device/ap_kbd.h"

#include <string.h>

void ap_kbd_reset(ap_kbd_t *kbd) { memset(kbd, 0, sizeof *kbd); }

bool ap_kbd_press(ap_kbd_t *kbd, unsigned key, uint8_t *code) {
  if (key >= AP_KBD_KEYS || kbd->down[key]) {
    return false;
  }
  kbd->down[key] = true;
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
  return true;
}
