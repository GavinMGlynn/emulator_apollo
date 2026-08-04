#include "device/ap_omti.h"

#include <string.h>

void ap_omti_disk_reset(ap_omti_t *omti) {
  /* The measured idle controller: `FF C0 FC 00` across the four fixed-disk
   * ports. `C0` is Table 4-2's two "Not Used (Set to 1)" bits and nothing
   * else -- not interrupting, not requesting DMA, not busy, not in a command
   * phase -- which is what makes the measurement and the table agree exactly.
   * `FINDINGS.md` C21. */
  omti->data = 0xFFFFu;
  omti->status = AP_OMTI_ST_FIXED;
  omti->configuration = 0xFCu;
  omti->mask = 0u;
}

void ap_omti_reset(ap_omti_t *omti) {
  memset(omti, 0, sizeof *omti);
  ap_omti_disk_reset(omti);

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

/* `[OMTI]` §5.1.4's completion byte: bit 1 is the error flag and the rest are
 * the logical unit. A driver reads it, and reads sense if it is set. */
#define COMPLETION_ERROR 0x02u

/* §5.1.3's sense bytes. Only the two codes this core can genuinely produce are
 * ever set -- an address past the drive, and a command with no drive behind it.
 * Everything else would be inventing a failure mode. */
#define SENSE_ILLEGAL_ADDRESS 0x21u
#define SENSE_DRIVE_NOT_READY 0x04u

void ap_omti_attach(ap_omti_t *omti, ap_awd_t *drive) { omti->drive = drive; }

ap_omti_phase_t ap_omti_disk_phase(const ap_omti_t *omti) {
  return omti->phase;
}

static void finish(ap_omti_t *omti, bool error, uint8_t sense) {
  omti->completion = error ? COMPLETION_ERROR : 0u;
  omti->sense[0] = error ? sense : 0u;
  omti->sense[1] = 0u;
  omti->sense[2] = 0u;
  omti->sense[3] = 0u;
  omti->phase = AP_OMTI_PHASE_STATUS;
  omti->buffer_index = 0u;
  omti->blocks_left = 0u;
  /* "1 = Command Complete", and the byte waiting is a status byte, which is
   * what `C/D` says. */
  omti->status |= (uint8_t)(AP_OMTI_ST_IREQ | AP_OMTI_ST_CD);
}

/* The address a data command names, and whether the drive has it. */
static bool addressed(ap_omti_t *omti, const ap_omti_cdb_t *cdb,
                      uint32_t *lba) {
  if (omti->drive == NULL) {
    finish(omti, true, SENSE_DRIVE_NOT_READY);
    return false;
  }
  if (!ap_awd_lba(omti->drive->geometry, cdb->cylinder, cdb->head, cdb->sector,
                  lba)) {
    finish(omti, true, SENSE_ILLEGAL_ADDRESS);
    return false;
  }
  return true;
}

/* Load the next sector of a read, or end the command when there are none. */
static void feed(ap_omti_t *omti) {
  if (omti->blocks_left == 0u) {
    finish(omti, false, 0u);
    return;
  }
  if (!ap_awd_read(omti->drive, omti->next_lba, omti->buffer)) {
    finish(omti, true, SENSE_ILLEGAL_ADDRESS);
    return;
  }
  omti->next_lba++;
  omti->blocks_left--;
  omti->buffer_index = 0u;
  omti->phase = AP_OMTI_PHASE_DATA_IN;
  /* Data, not a command or status byte. */
  omti->status = (uint8_t)((omti->status & ~AP_OMTI_ST_CD) | AP_OMTI_ST_DREQ);
}

/* §5.1.2's block count is a byte, and **zero means 256** -- the count is a
 * count of blocks and a command asking for none would be a command with no
 * purpose. Getting that wrong reads one sector where a driver expected 256, and
 * the symptom is a file system that appears to work until a large transfer. */
static unsigned block_count(const ap_omti_cdb_t *cdb) {
  return cdb->block_count == 0u ? 256u : cdb->block_count;
}

static void execute(ap_omti_t *omti) {
  ap_omti_cdb_t cdb;
  ap_omti_cdb_decode(omti->command, &cdb);

  if (!ap_omti_cdb_accepted_by_esdi(cdb.command)) {
    /* Including `0C INITIALIZE DRIVE CHARACTERISTICS`, which is ST506-only and
     * which this controller must refuse rather than quietly accept. */
    finish(omti, true, SENSE_ILLEGAL_ADDRESS);
    return;
  }

  uint32_t lba = 0;
  switch (cdb.command) {
  case AP_OMTI_CMD_READ:
    if (!addressed(omti, &cdb, &lba)) {
      return;
    }
    omti->next_lba = lba;
    omti->blocks_left = block_count(&cdb);
    feed(omti);
    return;

  case AP_OMTI_CMD_WRITE:
    if (!addressed(omti, &cdb, &lba)) {
      return;
    }
    omti->next_lba = lba;
    omti->blocks_left = block_count(&cdb);
    omti->buffer_index = 0u;
    omti->phase = AP_OMTI_PHASE_DATA_OUT;
    omti->status = (uint8_t)((omti->status & ~AP_OMTI_ST_CD) | AP_OMTI_ST_DREQ);
    return;

  case AP_OMTI_CMD_REQUEST_SENSE:
    /* The four bytes the *previous* command left, which is the whole point of
     * the command: a driver reads it after a failure to learn what failed. */
    memcpy(omti->buffer, omti->sense, sizeof omti->sense);
    omti->buffer_index = 0u;
    omti->blocks_left = 0u;
    omti->phase = AP_OMTI_PHASE_DATA_IN;
    omti->status = (uint8_t)((omti->status & ~AP_OMTI_ST_CD) | AP_OMTI_ST_DREQ);
    return;

  case AP_OMTI_CMD_TEST_DRIVE_READY:
    finish(omti, omti->drive == NULL, SENSE_DRIVE_NOT_READY);
    return;

  case AP_OMTI_CMD_RECALIBRATE:
  case AP_OMTI_CMD_SEEK:
    /* Positioning, which this model has no position to change: a seek is
     * complete the moment it is asked for, and the address is still checked so
     * a seek off the end fails as the hardware would. */
    if (!addressed(omti, &cdb, &lba)) {
      return;
    }
    finish(omti, false, 0u);
    return;

  default:
    /* Accepted by the command set and not implemented here. Reported as an
     * error rather than as success, because a driver told a format succeeded
     * when nothing was written would go on to trust the disk. */
    finish(omti, true, SENSE_ILLEGAL_ADDRESS);
    return;
  }
}

/* A byte the host wrote to the data port, in whatever phase the half is in. */
static void take_byte(ap_omti_t *omti, uint8_t value) {
  switch (omti->phase) {
  case AP_OMTI_PHASE_IDLE:
    /* §5.1.1: the first byte of a descriptor block starts the command phase,
     * and its own opcode says how long the block is. */
    omti->command_length = ap_omti_cdb_length(value);
    omti->command[0] = value;
    omti->command_index = 1u;
    omti->phase = AP_OMTI_PHASE_COMMAND;
    omti->status |= AP_OMTI_ST_CD;
    if (omti->command_index >= omti->command_length) {
      execute(omti);
    }
    return;

  case AP_OMTI_PHASE_COMMAND:
    if (omti->command_index < sizeof omti->command) {
      omti->command[omti->command_index++] = value;
    }
    if (omti->command_index >= omti->command_length) {
      execute(omti);
    }
    return;

  case AP_OMTI_PHASE_DATA_OUT:
    omti->buffer[omti->buffer_index++] = value;
    if (omti->buffer_index < AP_OMTI_BUFFER_BYTES) {
      return;
    }
    if (!ap_awd_write(omti->drive, omti->next_lba, omti->buffer)) {
      finish(omti, true, SENSE_ILLEGAL_ADDRESS);
      return;
    }
    omti->next_lba++;
    omti->blocks_left--;
    omti->buffer_index = 0u;
    if (omti->blocks_left == 0u) {
      finish(omti, false, 0u);
    }
    return;

  case AP_OMTI_PHASE_DATA_IN:
  case AP_OMTI_PHASE_STATUS:
    /* A write while the controller is talking. Ignored rather than merged into
     * the stream: the bus is the controller's in these phases. */
    return;
  }
}

/* A byte the host read from the data port. */
static uint8_t give_byte(ap_omti_t *omti) {
  switch (omti->phase) {
  case AP_OMTI_PHASE_DATA_IN: {
    const uint8_t value = omti->buffer[omti->buffer_index++];
    const unsigned end =
        omti->command[0] == AP_OMTI_CMD_REQUEST_SENSE ? sizeof omti->sense
                                                      : AP_OMTI_BUFFER_BYTES;
    if (omti->buffer_index >= end) {
      if (omti->command[0] == AP_OMTI_CMD_REQUEST_SENSE) {
        finish(omti, false, 0u);
      } else {
        feed(omti);
      }
    }
    return value;
  }
  case AP_OMTI_PHASE_STATUS: {
    const uint8_t value = omti->completion;
    /* The status byte ends the command. `IREQ` is cleared by being read, which
     * is what stops a driver seeing one completion twice. */
    omti->phase = AP_OMTI_PHASE_IDLE;
    omti->status = (uint8_t)(omti->status &
                             ~(AP_OMTI_ST_IREQ | AP_OMTI_ST_DREQ));
    return value;
  }
  case AP_OMTI_PHASE_IDLE:
  case AP_OMTI_PHASE_COMMAND:
  case AP_OMTI_PHASE_DATA_OUT:
    break;
  }
  return (uint8_t)(omti->data & 0xFFu);
}

uint8_t ap_omti_disk_read(ap_omti_t *omti, unsigned reg) {
  switch ((ap_omti_disk_reg_t)(reg & (AP_OMTI_DISK_REGISTERS - 1u))) {
  case AP_OMTI_DISK_DATA:
    /* §4.2: byte-wide when C/D is set, word-wide when it is clear. Only the low
     * byte is ever presented on a byte read; the width governs what a *word*
     * access may take, which is why the C/D bit is exposed rather than hidden. */
    return give_byte(omti);
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
    take_byte(omti, value);
    return;
  case AP_OMTI_DISK_STATUS:
    /* Table 4-1 calls the write side "RESET (Function)": a command, not a
     * store, so the value is not a parameter. It resets *this* half only --
     * clearing the whole part here would stop the floppy's motors as a side
     * effect of a disk command, which §4.1's two independent controllers
     * forbid. Caught by a board-level test after the device's own test missed
     * it, because that one exercised SELECT rather than RESET. */
    ap_omti_disk_reset(omti);
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
