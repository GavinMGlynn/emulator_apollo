#include "device/ap_omti_cdb.h"

void ap_omti_cdb_decode(const uint8_t *bytes, ap_omti_cdb_t *out) {
  out->command = bytes[0];
  out->command_class = (uint8_t)((bytes[0] >> 5) & 0x07u);
  out->opcode = (uint8_t)(bytes[0] & 0x1Fu);

  out->lun = (uint8_t)((bytes[1] >> 5) & 0x01u);
  out->head = (uint8_t)(bytes[1] & 0x1Fu);

  /* §5.1.1: "Bits 7 and 6 contain bits 9 and 8 respectively of the disk cylinder
   * number", byte 1 bit 7 is C10, and byte 3 is "the eight least significant
   * bits". Eleven bits across three bytes -- the one part of this layout that a
   * plausible-looking shortcut gets wrong only on large disks. */
  out->cylinder = (uint16_t)((((unsigned)bytes[1] & 0x80u) << 3) |
                             (((unsigned)bytes[2] & 0xC0u) << 2) |
                             (unsigned)bytes[3]);

  out->sector = (uint8_t)(bytes[2] & 0x3Fu);
  out->block_count = bytes[4];
  /* The whole byte: §5.2 defines bits 2-0 as the STEP option, which the
   * three-bit shift this used to do discarded. See the header. */
  out->control = bytes[5];
}

bool ap_omti_cdb_accepted_by_esdi(uint8_t command) {
  switch (command) {
  /* Common to all models. */
  case AP_OMTI_CMD_TEST_DRIVE_READY:
  case AP_OMTI_CMD_RECALIBRATE:
  case AP_OMTI_CMD_REQUEST_SENSE:
  case AP_OMTI_CMD_FORMAT_DRIVE:
  case AP_OMTI_CMD_READ_VERIFY:
  case AP_OMTI_CMD_FORMAT_TRACK:
  case AP_OMTI_CMD_FORMAT_BAD_TRACK:
  case AP_OMTI_CMD_READ:
  case AP_OMTI_CMD_WRITE:
  case AP_OMTI_CMD_SEEK:
  case AP_OMTI_CMD_READ_ECC_LENGTH:
  case AP_OMTI_CMD_READ_SECTOR_BUFFER:
  case AP_OMTI_CMD_WRITE_SECTOR_BUFFER:
  case AP_OMTI_CMD_ASSIGN_ALTERNATE:
  case AP_OMTI_CMD_CHANGE_CARTRIDGE:
  case AP_OMTI_CMD_READ_TO_BUFFER:
  case AP_OMTI_CMD_WRITE_FROM_BUFFER:
  case AP_OMTI_CMD_COPY:
  case AP_OMTI_CMD_RAM_DIAGNOSTICS:
  case AP_OMTI_CMD_READ_ID:
  case AP_OMTI_CMD_DRIVE_DIAGNOSTIC:
  case AP_OMTI_CMD_CONTROLLER_DIAGNOSTIC:
  case AP_OMTI_CMD_READ_LONG:
  case AP_OMTI_CMD_WRITE_LONG:
  /* Specific to the ESDI drives, which is what this machine has. */
  case AP_OMTI_CMD_START_STOP:
  case AP_OMTI_CMD_CHECK_TRACK_FORMAT:
  case AP_OMTI_CMD_READ_ESDI_DEFECT_LIST:
  case AP_OMTI_CMD_READ_CAPACITY:
    return true;
  default:
    /* Including INITIALIZE DRIVE CHARACTERISTICS, which §5.1.2 lists under the
     * ST506/412 drives. Rejecting it is the point: accepting it would make an
     * ESDI drive's geometry look settable, where it is reported by READ
     * CAPACITY instead. */
    return false;
  }
}

unsigned ap_omti_cdb_length(uint8_t command) {
  /* §5.1.2 gives COPY ten bytes and everything else six; §5.1.1 says "Refer to
   * the COPY command for the format of the 10 byte CDB". */
  return command == AP_OMTI_CMD_COPY ? AP_OMTI_CDB_LONG : AP_OMTI_CDB_SHORT;
}

bool ap_omti_cdb_touches_surface(uint8_t command) {
  switch (command) {
  /* Positioning, with or without a transfer. `SEEK` and `RECALIBRATE` move the
   * heads and report completion when they arrive, which is the whole of what
   * they do. */
  case AP_OMTI_CMD_RECALIBRATE:
  case AP_OMTI_CMD_SEEK:
  /* Reads and writes of the surface, in every form the controller offers:
   * direct, through the sector buffer, and the long variants that carry the
   * ECC field with the data. */
  case AP_OMTI_CMD_READ:
  case AP_OMTI_CMD_WRITE:
  case AP_OMTI_CMD_READ_VERIFY:
  case AP_OMTI_CMD_READ_TO_BUFFER:
  case AP_OMTI_CMD_WRITE_FROM_BUFFER:
  case AP_OMTI_CMD_READ_LONG:
  case AP_OMTI_CMD_WRITE_LONG:
  case AP_OMTI_CMD_COPY:
  /* Formatting writes every sector of a track or of the drive, so it is the
   * most surface-bound command there is. The duration modelled for it is the
   * same average as any other -- a full format is not a per-track cost and this
   * core does not pretend to time one. */
  case AP_OMTI_CMD_FORMAT_DRIVE:
  case AP_OMTI_CMD_FORMAT_TRACK:
  case AP_OMTI_CMD_FORMAT_BAD_TRACK:
  case AP_OMTI_CMD_CHECK_TRACK_FORMAT:
  case AP_OMTI_CMD_ASSIGN_ALTERNATE:
    return true;
  default:
    /* Everything else is answered from the controller's registers or its
     * sector buffer: TEST DRIVE READY, REQUEST SENSE, READ/WRITE SECTOR BUFFER,
     * READ CAPACITY, the diagnostics, and INITIALIZE DRIVE CHARACTERISTICS. */
    return false;
  }
}
