#include "device/ap_qic.h"

#include <string.h>

void ap_qic_reset(ap_qic_t *qic) {
  ap_ct_t image = qic->image;
  bool loaded = qic->loaded;
  ap_qic_cartridge_t cartridge = qic->cartridge;

  memset(qic, 0, sizeof *qic);

  /* A reset does not eject the cartridge -- it is a command to the drive, not to
   * the operator. But it does deselect and unlock: `[SC499]` §1.13.1 has RESET
   * among the things that unlock, "Execution of the SELECT command or RESET
   * unlocks the cartridge". */
  qic->image = image;
  qic->loaded = loaded;
  qic->cartridge = cartridge;
}

bool ap_qic_load(ap_qic_t *qic, const uint8_t *data, size_t size,
                 ap_qic_cartridge_t cartridge) {
  if (cartridge == AP_QIC_CARTRIDGE_NONE) {
    return false;
  }
  if (!ap_ct_open(&qic->image, data, size)) {
    return false;
  }
  qic->loaded = true;
  qic->cartridge = cartridge;
  qic->position = 0u;
  qic->reading = false;
  return true;
}

void ap_qic_eject(ap_qic_t *qic) {
  if (qic->soft_lock) {
    /* The soft lock is a lock on the *cartridge*, so it holds against ejection.
     * That is the only thing the lock does that a caller can observe, and a
     * model that ignored it would make the command inert. */
    return;
  }
  memset(&qic->image, 0, sizeof qic->image);
  qic->loaded = false;
  qic->cartridge = AP_QIC_CARTRIDGE_NONE;
  qic->position = 0u;
  qic->reading = false;
}

bool ap_qic_command_known(uint8_t command) {
  switch ((ap_qic_command_t)command) {
  case AP_QIC_CMD_SELECT:
  case AP_QIC_CMD_SELECT_LOCK:
  case AP_QIC_CMD_BOT:
  case AP_QIC_CMD_RETENSION:
  case AP_QIC_CMD_SELECT_Q24:
  case AP_QIC_CMD_WRITE:
  case AP_QIC_CMD_WRITE_FILE_MARK:
  case AP_QIC_CMD_READ:
  case AP_QIC_CMD_READ_FILE_MARK:
  case AP_QIC_CMD_READ_STATUS:
    return true;
  }
  return false;
}

bool ap_qic_command(ap_qic_t *qic, uint8_t command) {
  switch ((ap_qic_command_t)command) {
  case AP_QIC_CMD_SELECT:
    /* "The SELECT command selects the tape drive ... Execution of the SELECT
     * command or RESET unlocks the cartridge." So a plain SELECT clears a lock
     * the other variant set -- the two are not independent switches. */
    qic->selected = true;
    qic->soft_lock = false;
    return true;
  case AP_QIC_CMD_SELECT_LOCK:
    qic->selected = true;
    qic->soft_lock = true;
    return true;
  case AP_QIC_CMD_BOT:
    /* "positions the tape in the cartridge in the selected device to BOT". */
    if (!qic->selected) {
      return false;
    }
    qic->position = 0u;
    qic->reading = false;
    return true;
  case AP_QIC_CMD_RETENSION:
    /* Runs the tape end to end and returns it to the beginning. Nothing about
     * the image changes; the position does. */
    if (!qic->selected) {
      return false;
    }
    qic->position = 0u;
    qic->reading = false;
    return true;
  case AP_QIC_CMD_SELECT_Q24:
    if (!qic->selected) {
      return false;
    }
    qic->q24_format = true;
    return true;
  case AP_QIC_CMD_READ:
    if (!qic->selected || !qic->loaded) {
      return false;
    }
    qic->reading = true;
    return true;
  case AP_QIC_CMD_READ_STATUS:
    /* Always answerable, selected or not: a status read is how a driver finds
     * out that the drive is *not* ready. */
    return true;
  case AP_QIC_CMD_READ_FILE_MARK:
    /* Recognised, and refused: a file mark is a structure within the tape
     * format, and a raw block image carries no marks to find. Answering
     * "found one" or "reached the end" would both be inventions. */
    return false;
  case AP_QIC_CMD_WRITE:
  case AP_QIC_CMD_WRITE_FILE_MARK:
    /* Recognised, and refused: there is no write-back path, and accepting a
     * write that went nowhere would let an installation appear to succeed. */
    return false;
  }
  /* Unrecognised -- including ERASE and SELECT Q11 FORMAT, whose opcodes the
   * scan lost. Refusing is what keeps a guessed code from quietly working. */
  return false;
}

bool ap_qic_read_block(ap_qic_t *qic, uint8_t *out) {
  if (!qic->reading || !qic->loaded || !qic->selected) {
    return false;
  }
  if (!ap_ct_read_block(&qic->image, qic->position, out)) {
    /* Past the end of the tape. The position does not advance, so a driver that
     * keeps reading keeps failing rather than wrapping to the beginning. */
    return false;
  }
  qic->position++;
  return true;
}
