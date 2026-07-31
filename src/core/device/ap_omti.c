#include "device/ap_omti.h"

#include <string.h>

void ap_omti_reset(ap_omti_t *omti) {
  memset(omti, 0, sizeof *omti);
  /* The measured idle controller: `FF C0 FC 00` across the four fixed-disk
   * ports. `C0` is Table 4-2's two "Not Used (Set to 1)" bits and nothing
   * else -- not interrupting, not requesting DMA, not busy, not in a command
   * phase -- which is what makes the measurement and the table agree exactly.
   * `FINDINGS.md` C21. */
  omti->data = 0xFFFFu;
  omti->status = AP_OMTI_ST_FIXED;
  omti->configuration = 0xFCu;

  /* And the measured floppy half: `00` main status and `80` digital input, the
   * latter being the diskette-change bit with no media in the drive. */
  omti->fdc_data = 0xFFu;
  omti->disk_change = true;
}

bool ap_omti_data_is_byte(const ap_omti_t *omti) {
  return (omti->status & AP_OMTI_ST_CD) != 0u;
}

bool ap_omti_fdc_in_reset(const ap_omti_t *omti) {
  return (omti->dor & AP_OMTI_DOR_NOT_RESET) == 0u;
}

uint8_t ap_omti_disk_read(ap_omti_t *omti, unsigned reg) {
  switch ((ap_omti_disk_reg_t)(reg & (AP_OMTI_DISK_REGISTERS - 1u))) {
  case AP_OMTI_DISK_DATA:
    /* §4.2: byte-wide when C/D is set, word-wide when it is clear. Only the low
     * byte is ever presented on a byte read; the width governs what a *word*
     * access may take, which is why the C/D bit is exposed rather than hidden. */
    return (uint8_t)(omti->data & 0xFFu);
  case AP_OMTI_DISK_STATUS:
    /* The two fixed bits are re-asserted on every read rather than stored, so
     * nothing can clear them. Table 4-2 gives them as constants, not state. */
    return (uint8_t)(omti->status | AP_OMTI_ST_FIXED);
  case AP_OMTI_DISK_CONFIG:
    return omti->configuration;
  case AP_OMTI_DISK_MASK:
    /* Table 4-1 gives this port "N/A" on read. The measured controller answers
     * `00` there rather than floating, so the port is decoded and drives zero --
     * which is a fact about this board and is why the value is not left to the
     * bus. */
    return 0u;
  }
  return 0u;
}

void ap_omti_disk_write(ap_omti_t *omti, unsigned reg, uint8_t value) {
  switch ((ap_omti_disk_reg_t)(reg & (AP_OMTI_DISK_REGISTERS - 1u))) {
  case AP_OMTI_DISK_DATA:
    omti->data = (uint16_t)((omti->data & 0xFF00u) | value);
    return;
  case AP_OMTI_DISK_STATUS:
    /* Table 4-1 calls the write side "RESET (Function)": a command, not a
     * store, so the value is not a parameter. */
    ap_omti_reset(omti);
    return;
  case AP_OMTI_DISK_CONFIG:
    /* "SELECT (Function)". Selecting the controller is what Table 4-2's BSY bit
     * reports -- "1 = Controller Selected" -- so the function has an observable
     * effect and is not merely accepted. */
    omti->status |= AP_OMTI_ST_BSY;
    return;
  case AP_OMTI_DISK_MASK:
    omti->mask = value;
    return;
  }
}

uint8_t ap_omti_fdc_read(ap_omti_t *omti, unsigned reg) {
  switch (reg & (AP_OMTI_FLOPPY_REGISTERS - 1u)) {
  case AP_OMTI_FDC_MSR:
    return omti->fdc_status;
  case AP_OMTI_FDC_DATA:
    return omti->fdc_data;
  case AP_OMTI_FDC_CONTROL:
    /* "N/A" on read, and measured as `00` rather than floating. */
    return 0u;
  case AP_OMTI_FDC_DIR:
    return omti->disk_change ? AP_OMTI_DIR_DISK_CHANGE : 0u;
  default:
    /* Including the Digital Output Register, which is write-only and measured
     * reading `FF`: nothing drives it, so the board's floating value stands. */
    return 0xFFu;
  }
}

void ap_omti_fdc_write(ap_omti_t *omti, unsigned reg, uint8_t value) {
  switch (reg & (AP_OMTI_FLOPPY_REGISTERS - 1u)) {
  case AP_OMTI_FDC_DOR:
    omti->dor = value;
    return;
  case AP_OMTI_FDC_DATA:
    omti->fdc_data = value;
    return;
  case AP_OMTI_FDC_CONTROL:
  case AP_OMTI_FDC_DIR:
    omti->fdc_control = value;
    return;
  default:
    return;
  }
}
